// ============================================================================
// Eunoia Engine — Texture Streaming Plugin
// TextureStreamingSystem: Viewport Visibility-Driven DX12 Texture Streaming
// ============================================================================

#include <TextureStreaming/TextureStreamingSystem.h>
#include <EngineAssets/DirectXTexD3D12.h>
#include <EngineAssets/StbImage.h>
#include <iostream>
#include <algorithm>
#include <filesystem>

namespace Eunoia {

bool TextureStreamingSystem::Initialize(ID3D12Device* device, ID3D12CommandQueue* directQueue) {
    if (!device || !directQueue) return false;

    std::lock_guard<std::mutex> lock(m_syncMutex);
    m_device = device;
    m_queue = directQueue;

    HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmdAlloc));
    if (FAILED(hr)) return false;

    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&m_cmdList));
    if (FAILED(hr)) return false;
    m_cmdList->Close();

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_uploadFence));
    if (FAILED(hr)) return false;

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) return false;

    m_uploadFenceValue = 0;
    m_initialized.store(true);
    return true;
}

void TextureStreamingSystem::Shutdown() {
    WaitForAllPending();

    std::lock_guard<std::mutex> lock(m_syncMutex);
    m_initialized.store(false);

    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }

    m_inFlightUploads.clear();
    m_completedCpuLoads.clear();
    m_readyTextures.clear();
    m_persistentTextures.clear();
    m_textureStates.clear();
    m_activeTableSlots.clear();
    m_metadataCache.clear();
    m_currentLodClamps.clear();
    m_requestedHighResTextures.clear();

    m_uploadFence.Reset();
    m_cmdList.Reset();
    m_cmdAlloc.Reset();
    m_queue.Reset();
    m_device.Reset();
}

void TextureStreamingSystem::SetDescriptorAllocator(
    ID3D12DescriptorHeap* srvHeap,
    UINT descriptorSize,
    std::function<bool(UINT, UINT&)> allocFn
) {
    std::lock_guard<std::mutex> lock(m_syncMutex);
    m_srvHeap = srvHeap;
    m_srvDescriptorSize = descriptorSize;
    m_srvAllocFn = allocFn;
}

std::string TextureStreamingSystem::BuildCacheKey(const std::string& name, TextureUsageSlot slot) const {
    std::string resolved = TextureManager::Get().ResolvePath(name);
    if (resolved.empty()) resolved = name;
    return resolved + "@slot=" + std::to_string(static_cast<int>(slot));
}

bool TextureStreamingSystem::IsTextureReady(const std::string& name, TextureUsageSlot slot) const {
    std::lock_guard<std::mutex> lock(m_syncMutex);
    std::string key = BuildCacheKey(name, slot);
    return m_readyTextures.find(key) != m_readyTextures.end();
}

size_t TextureStreamingSystem::GetPendingRequestCount() const {
    std::lock_guard<std::mutex> lock(m_syncMutex);
    return m_completedCpuLoads.size();
}

size_t TextureStreamingSystem::GetInFlightUploadCount() const {
    std::lock_guard<std::mutex> lock(m_syncMutex);
    return m_inFlightUploads.size();
}

void TextureStreamingSystem::StreamingRequestResolution(const std::string& name, uint32_t resolution) {
    if (name.empty() || name == "none") return;
    std::lock_guard<std::mutex> lock(m_syncMutex);
    std::string resolved = TextureManager::Get().ResolvePath(name);
    if (resolved.empty()) resolved = name;
    if (resolution >= 1024) {
        m_requestedHighResTextures.insert(resolved);
        m_requestedHighResTextures.insert(name);
    }
}

StreamingTextureInfo TextureStreamingSystem::GetOrProbeTextureInfo(const std::string& name, TextureUsageSlot slot) {
    StreamingTextureInfo info;
    if (name.empty() || name == "none") return info;

    std::string key = BuildCacheKey(name, slot);
    std::string resolved = TextureManager::Get().ResolvePath(name);
    if (resolved.empty()) resolved = name;

    {
        std::lock_guard<std::mutex> lock(m_syncMutex);
        auto itReady = m_readyTextures.find(key);
        if (itReady != m_readyTextures.end()) {
            info.width = itReady->second.width;
            info.height = itReady->second.height;
            info.mipLevels = itReady->second.mipLevels;
            info.format = itReady->second.format;
            info.isValid = true;
            info.isGpuReady = true;
            info.isStreaming = false;
            auto itClamp = m_currentLodClamps.find(key);
            info.currentLodClamp = (itClamp != m_currentLodClamps.end()) ? itClamp->second : 0.0f;
            return info;
        }

        auto itMeta = m_metadataCache.find(resolved);
        if (itMeta != m_metadataCache.end()) {
            info = itMeta->second;
            auto itState = m_textureStates.find(key);
            if (itState != m_textureStates.end() && itState->second == TextureStreamState::Ready) {
                info.isGpuReady = true;
                info.isStreaming = false;
            } else {
                info.isGpuReady = false;
                info.isStreaming = true;
            }
            return info;
        }
    }

    // Fast disk probing (<0.05ms): Read only header
    std::error_code ec;
    if (!std::filesystem::exists(resolved, ec) || std::filesystem::is_directory(resolved, ec) || std::filesystem::file_size(resolved, ec) == 0) {
        std::string tmResolved = TextureManager::Get().ResolvePath(name);
        if (!tmResolved.empty() && std::filesystem::exists(tmResolved, ec) && std::filesystem::file_size(tmResolved, ec) > 0) {
            resolved = tmResolved;
        } else {
            info.isValid = false;
            return info;
        }
    }

    std::string ext = std::filesystem::path(resolved).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".dds") {
        DirectX::TexMetadata meta;
        DirectX::ScratchImage scratch;
        if (DirectX::LoadFromDDSFile(resolved.c_str(), meta, scratch)) {
            info.width = static_cast<int>(meta.width);
            info.height = static_cast<int>(meta.height);
            info.mipLevels = static_cast<UINT>(meta.mipLevels);
            info.format = meta.format;
            info.isValid = true;
            info.isGpuReady = false;
            info.isStreaming = true;
        }
    } else {
        int w = 0, h = 0, comp = 0;
        if (stbi_info(resolved.c_str(), &w, &h, &comp)) {
            info.width = w;
            info.height = h;
            info.mipLevels = 1;
            info.format = (slot == TextureUsageSlot::BaseColor || slot == TextureUsageSlot::Emission) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
            info.isValid = true;
            info.isGpuReady = false;
            info.isStreaming = true;
        } else {
            // Check if alternative .dds file exists
            std::filesystem::path ddsAlt = std::filesystem::path(resolved).replace_extension(".dds");
            if (std::filesystem::exists(ddsAlt, ec) && std::filesystem::file_size(ddsAlt, ec) > 0) {
                DirectX::TexMetadata meta;
                DirectX::ScratchImage scratch;
                if (DirectX::LoadFromDDSFile(ddsAlt.string().c_str(), meta, scratch)) {
                    resolved = ddsAlt.string();
                    info.width = static_cast<int>(meta.width);
                    info.height = static_cast<int>(meta.height);
                    info.mipLevels = static_cast<UINT>(meta.mipLevels);
                    info.format = meta.format;
                    info.isValid = true;
                    info.isGpuReady = false;
                    info.isStreaming = true;
                }
            }
        }
    }

    if (info.isValid) {
        std::lock_guard<std::mutex> lock(m_syncMutex);
        m_metadataCache[resolved] = info;
        m_metadataCache[name] = info;
    }

    // Trigger asynchronous GPU upload
    RequestTexture(name, slot);
    return info;
}

bool TextureStreamingSystem::RequestTexture(const std::string& name, TextureUsageSlot slot) {
    if (name.empty() || name == "none" || !m_initialized.load()) return false;

    std::string resolved = TextureManager::Get().ResolvePath(name);
    if (resolved.empty()) resolved = name;
    std::string key = resolved + "@slot=" + std::to_string(static_cast<int>(slot));

    {
        std::lock_guard<std::mutex> lock(m_syncMutex);
        auto itReady = m_readyTextures.find(key);
        if (itReady != m_readyTextures.end()) {
            return true;
        }

        auto itState = m_textureStates.find(key);
        if (itState != m_textureStates.end() &&
            (itState->second == TextureStreamState::QueuedForDiskRead ||
             itState->second == TextureStreamState::ReadingDisk ||
             itState->second == TextureStreamState::ReadyForGpuUpload ||
             itState->second == TextureStreamState::UploadingToGpu)) {
            return false;
        }

        m_textureStates[key] = TextureStreamState::QueuedForDiskRead;
    }

    auto req = std::make_shared<TextureStreamRequest>();
    req->sourcePath = resolved;
    req->cacheKey = key;
    req->slot = slot;
    req->isNormalMap = (slot == TextureUsageSlot::Normal);
    req->state = TextureStreamState::ReadingDisk;

    // Asynchronous background disk I/O & decompression via JobSystem
    EngineCore::JobSystem::Get().Execute([this, req]() {
        CachedTexture* cached = TextureManager::Get().GetTexture(req->sourcePath, req->slot);
        if (cached && cached->valid) {
            req->cpuData = *cached;
            req->state = TextureStreamState::ReadyForGpuUpload;

            std::lock_guard<std::mutex> lock(m_syncMutex);
            m_textureStates[req->cacheKey] = TextureStreamState::ReadyForGpuUpload;
            m_completedCpuLoads.push_back(req);
        } else {
            req->state = TextureStreamState::Failed;
            std::lock_guard<std::mutex> lock(m_syncMutex);
            m_textureStates[req->cacheKey] = TextureStreamState::Failed;
        }
    });

    return false;
}

void TextureStreamingSystem::RegisterMaterialTableSlot(const std::string& cacheKey, D3D12_CPU_DESCRIPTOR_HANDLE tableSlot) {
    if (tableSlot.ptr == 0) return;
    std::lock_guard<std::mutex> lock(m_syncMutex);
    m_activeTableSlots.insert({ cacheKey, tableSlot });
}

DX12GpuTexture TextureStreamingSystem::GetTextureOrFallback(
    const std::string& name,
    const DX12GpuTexture& fallback,
    TextureUsageSlot slot,
    D3D12_CPU_DESCRIPTOR_HANDLE optionalTableSlot
) {
    if (name.empty() || name == "none") return fallback;

    std::string key = BuildCacheKey(name, slot);

    {
        std::lock_guard<std::mutex> lock(m_syncMutex);
        auto it = m_readyTextures.find(key);
        if (it != m_readyTextures.end()) {
            if (optionalTableSlot.ptr != 0) {
                m_activeTableSlots.insert({ key, optionalTableSlot });
            }
            return it->second;
        }
    }

    if (optionalTableSlot.ptr != 0) {
        RegisterMaterialTableSlot(key, optionalTableSlot);
    }

    RequestTexture(name, slot);
    return fallback;
}

void TextureStreamingSystem::ApplyLodClampToSrv(DX12GpuTexture& tex, float clamp) {
    if (!m_device || !tex.resource || tex.cpuHandle.ptr == 0) return;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = tex.format;
    if (tex.isCubemap) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MipLevels = tex.mipLevels;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.ResourceMinLODClamp = clamp;
    } else {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = tex.mipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = clamp;
    }

    m_device->CreateShaderResourceView(tex.resource, &srvDesc, tex.cpuHandle);
}

void TextureStreamingSystem::Update(size_t maxUploadsPerFrame) {
    if (!m_initialized.load() || !m_device || !m_queue || !m_uploadFence) return;

    // 1. Check completed GPU fences and retire finished staging buffers
    UINT64 completedFence = m_uploadFence->GetCompletedValue();
    {
        std::lock_guard<std::mutex> lock(m_syncMutex);
        auto it = m_inFlightUploads.begin();
        while (it != m_inFlightUploads.end()) {
            if (completedFence >= it->fenceValue) {
                // Upload is complete on GPU!
                m_readyTextures[it->cacheKey] = it->gpuTexture;
                m_persistentTextures[it->cacheKey] = it->defaultTexture;
                m_textureStates[it->cacheKey] = TextureStreamState::Ready;
                m_currentLodClamps[it->cacheKey] = 0.0f;

                // Auto-patch any material descriptor table slots bound for this texture
                auto range = m_activeTableSlots.equal_range(it->cacheKey);
                for (auto slotIt = range.first; slotIt != range.second; ++slotIt) {
                    if (slotIt->second.ptr != 0 && it->gpuTexture.cpuHandle.ptr != 0) {
                        m_device->CopyDescriptorsSimple(
                            1, slotIt->second, it->gpuTexture.cpuHandle,
                            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
                        );
                    }
                }

                it = m_inFlightUploads.erase(it);
            } else {
                ++it;
            }
        }
    }

    // 2. Prevent resetting command allocator while previous upload batch is still executing on GPU
    if (m_uploadFenceValue > 0 && m_uploadFence->GetCompletedValue() < m_uploadFenceValue) {
        return;
    }

    // 3. Process up to maxUploadsPerFrame ready CPU loads
    std::vector<std::shared_ptr<TextureStreamRequest>> toUpload;
    {
        std::lock_guard<std::mutex> lock(m_syncMutex);
        size_t count = std::min(maxUploadsPerFrame, m_completedCpuLoads.size());
        if (count > 0) {
            toUpload.assign(m_completedCpuLoads.begin(), m_completedCpuLoads.begin() + count);
            m_completedCpuLoads.erase(m_completedCpuLoads.begin(), m_completedCpuLoads.begin() + count);
        }
    }

    if (toUpload.empty()) return;

    m_cmdAlloc->Reset();
    m_cmdList->Reset(m_cmdAlloc.Get(), nullptr);

    std::vector<InFlightUpload> newUploads;
    newUploads.reserve(toUpload.size());

    for (const auto& req : toUpload) {
        if (!req || !req->cpuData.valid) continue;

        const DirectX::ScratchImage& scratch = req->cpuData.scratchImage;
        if (scratch.GetImageCount() == 0) continue;

        const DirectX::TexMetadata& meta = scratch.GetMetadata();
        if (meta.width == 0 || meta.height == 0) continue;

        // Allocate default heap texture (GPU destination)
        ID3D12Resource* texRes = nullptr;
        HRESULT hr = DirectX::CreateTexture(m_device.Get(), meta, &texRes);
        if (FAILED(hr) || !texRes) continue;

        std::wstring wname(req->sourcePath.begin(), req->sourcePath.end());
        texRes->SetName(wname.c_str());

        std::vector<D3D12_SUBRESOURCE_DATA> subresources;
        hr = DirectX::PrepareUpload(m_device.Get(), scratch.GetImages(), scratch.GetImageCount(), meta, subresources);
        if (FAILED(hr) || subresources.empty()) {
            texRes->Release();
            continue;
        }

        UINT numSub = static_cast<UINT>(subresources.size());
        UINT64 uploadSize = DirectX::GetRequiredIntermediateSize(m_device.Get(), texRes, 0, numSub);

        // Allocate upload heap staging buffer
        D3D12_RESOURCE_DESC upDesc = {};
        upDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        upDesc.Width = uploadSize;
        upDesc.Height = 1;
        upDesc.DepthOrArraySize = 1;
        upDesc.MipLevels = 1;
        upDesc.Format = DXGI_FORMAT_UNKNOWN;
        upDesc.SampleDesc.Count = 1;
        upDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        D3D12_HEAP_PROPERTIES upHeap = {};
        upHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        upHeap.CreationNodeMask = 1;
        upHeap.VisibleNodeMask = 1;

        ID3D12Resource* uploadBuf = nullptr;
        hr = m_device->CreateCommittedResource(
            &upHeap, D3D12_HEAP_FLAG_NONE, &upDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf)
        );
        if (FAILED(hr) || !uploadBuf) {
            texRes->Release();
            continue;
        }

        // Record UpdateSubresources (CopyTextureRegion)
        DirectX::UpdateSubresources(m_device.Get(), m_cmdList.Get(), texRes, uploadBuf, 0, 0, numSub, subresources.data());

        // Transition barrier: COPY_DEST -> PIXEL_SHADER_RESOURCE
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texRes;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_cmdList->ResourceBarrier(1, &barrier);

        // Allocate SRV descriptor
        UINT descIdx = 0;
        if (!m_srvAllocFn || !m_srvAllocFn(1, descIdx)) {
            uploadBuf->Release();
            texRes->Release();
            continue;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpuH = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        cpuH.ptr += static_cast<SIZE_T>(descIdx) * m_srvDescriptorSize;

        D3D12_GPU_DESCRIPTOR_HANDLE gpuH = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        gpuH.ptr += static_cast<SIZE_T>(descIdx) * m_srvDescriptorSize;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = meta.format;
        if (meta.IsCubemap()) {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MipLevels = static_cast<UINT>(meta.mipLevels);
            srvDesc.TextureCube.MostDetailedMip = 0;
            srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
        } else {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = static_cast<UINT>(meta.mipLevels);
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
        }

        m_device->CreateShaderResourceView(texRes, &srvDesc, cpuH);

        DX12GpuTexture gpuTex;
        gpuTex.resource = texRes;
        gpuTex.cpuHandle = cpuH;
        gpuTex.gpuHandle = gpuH;
        gpuTex.width = static_cast<int>(meta.width);
        gpuTex.height = static_cast<int>(meta.height);
        gpuTex.mipLevels = static_cast<UINT>(meta.mipLevels);
        gpuTex.format = meta.format;
        gpuTex.isCubemap = meta.IsCubemap();

        InFlightUpload upload;
        upload.stagingBuffer = uploadBuf;
        upload.defaultTexture = texRes;
        uploadBuf->Release();
        texRes->Release();

        upload.gpuTexture = gpuTex;
        upload.cacheKey = req->cacheKey;
        newUploads.push_back(upload);
    }

    if (newUploads.empty()) return;

    m_cmdList->Close();
    ID3D12CommandList* lists[] = { m_cmdList.Get() };
    m_queue->ExecuteCommandLists(1, lists);

    m_uploadFenceValue++;
    m_queue->Signal(m_uploadFence.Get(), m_uploadFenceValue);

    for (auto& up : newUploads) {
        up.fenceValue = m_uploadFenceValue;
    }

    {
        std::lock_guard<std::mutex> lock(m_syncMutex);
        for (auto& up : newUploads) {
            m_textureStates[up.cacheKey] = TextureStreamState::UploadingToGpu;
            m_inFlightUploads.push_back(up);
        }
    }
}

void TextureStreamingSystem::UpdateViewportVisibility(const std::unordered_set<std::string>& visibleTextureNames) {
    if (!m_initialized.load() || !m_device) return;

    std::lock_guard<std::mutex> lock(m_syncMutex);

    // Build fast lookup set of resolved paths that are visible or explicitly requested
    std::unordered_set<std::string> visibleKeys;
    visibleKeys.reserve(visibleTextureNames.size() * 2 + m_requestedHighResTextures.size());

    for (const auto& name : visibleTextureNames) {
        if (name.empty() || name == "none") continue;
        visibleKeys.insert(name);
        std::string resolved = TextureManager::Get().ResolvePath(name);
        if (!resolved.empty()) visibleKeys.insert(resolved);
    }

    for (const auto& name : m_requestedHighResTextures) {
        if (name.empty() || name == "none") continue;
        visibleKeys.insert(name);
        std::string resolved = TextureManager::Get().ResolvePath(name);
        if (!resolved.empty()) visibleKeys.insert(resolved);
    }
    m_requestedHighResTextures.clear();

    const bool streamingEnabled = m_textureStreamingEnabled.load();
    const float nonVisibleClamp = m_nonVisibleLodClamp.load();

    for (auto& pair : m_readyTextures) {
        const std::string& cacheKey = pair.first;
        DX12GpuTexture& tex = pair.second;
        if (!tex.resource || tex.cpuHandle.ptr == 0) continue;

        // Extract raw resolved path from cacheKey (strip @slot=...)
        std::string pathKey = cacheKey;
        size_t atPos = pathKey.find("@slot=");
        if (atPos != std::string::npos) {
            pathKey = pathKey.substr(0, atPos);
        }

        bool isVisible = (!streamingEnabled) || (visibleKeys.find(pathKey) != visibleKeys.end()) || (visibleKeys.find(cacheKey) != visibleKeys.end());
        float targetClamp = isVisible ? 0.0f : nonVisibleClamp;

        float currentClamp = 0.0f;
        auto itClamp = m_currentLodClamps.find(cacheKey);
        if (itClamp != m_currentLodClamps.end()) {
            currentClamp = itClamp->second;
        }

        if (std::abs(currentClamp - targetClamp) > 0.001f) {
            m_currentLodClamps[cacheKey] = targetClamp;
            ApplyLodClampToSrv(tex, targetClamp);

            // Re-copy updated SRV to all active material descriptor tables bound to this texture
            auto range = m_activeTableSlots.equal_range(cacheKey);
            for (auto slotIt = range.first; slotIt != range.second; ++slotIt) {
                if (slotIt->second.ptr != 0) {
                    m_device->CopyDescriptorsSimple(
                        1, slotIt->second, tex.cpuHandle,
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
                    );
                }
            }
        }
    }
}

TextureStreamingMetrics TextureStreamingSystem::GetMetrics() const {
    std::lock_guard<std::mutex> lock(m_syncMutex);
    TextureStreamingMetrics m;
    m.totalRegistered = m_readyTextures.size();
    m.inFlightUploads = m_inFlightUploads.size();
    m.pendingDiskReads = m_completedCpuLoads.size();
    m.isEnabled = m_textureStreamingEnabled.load();
    m.nonVisibleLodClamp = m_nonVisibleLodClamp.load();

    for (const auto& pair : m_readyTextures) {
        float clamp = 0.0f;
        auto it = m_currentLodClamps.find(pair.first);
        if (it != m_currentLodClamps.end()) clamp = it->second;
        if (clamp <= 0.001f) {
            m.visibleHighResCount++;
        } else {
            m.nonVisibleLowResCount++;
        }
        // Approximate VRAM footprint
        size_t bpp = 4;
        if (DirectX::IsCompressed(pair.second.format)) bpp = 1;
        m.totalGpuMemoryBytes += static_cast<size_t>(pair.second.width) * pair.second.height * bpp;
    }

    return m;
}

void TextureStreamingSystem::WaitForAllPending() {
    while (true) {
        {
            std::lock_guard<std::mutex> lock(m_syncMutex);
            if (m_completedCpuLoads.empty() && m_inFlightUploads.empty()) {
                break;
            }
        }
        Update(16);
        if (m_queue && m_uploadFence && m_fenceEvent) {
            if (m_uploadFence->GetCompletedValue() < m_uploadFenceValue) {
                m_uploadFence->SetEventOnCompletion(m_uploadFenceValue, m_fenceEvent);
                WaitForSingleObject(m_fenceEvent, 20);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void TextureStreamingSystem::InvalidateTexture(const std::string& name) {
    if (name.empty()) return;
    std::lock_guard<std::mutex> lock(m_syncMutex);
    std::string resolved = TextureManager::Get().ResolvePath(name);
    if (resolved.empty()) resolved = name;

    m_metadataCache.erase(resolved);
    m_metadataCache.erase(name);

    for (int s = 0; s < 10; ++s) {
        std::string key = resolved + "@slot=" + std::to_string(s);
        m_readyTextures.erase(key);
        m_persistentTextures.erase(key);
        m_textureStates.erase(key);
        m_currentLodClamps.erase(key);
        m_activeTableSlots.erase(key);
    }
}

} // namespace Eunoia
