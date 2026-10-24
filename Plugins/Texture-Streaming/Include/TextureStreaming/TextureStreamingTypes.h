#pragma once
// ============================================================================
// Eunoia Engine — Texture Streaming Plugin
// Types and data structures for viewport visibility-driven DX12 texture streaming
// ============================================================================

#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <functional>
#include <memory>
#include <EngineAssets/TextureManager.h>
#include <EngineCore/JobSystem.h>

#ifndef DX12GPUTEXTURE_DEFINED
#define DX12GPUTEXTURE_DEFINED
struct DX12GpuTexture {
    ID3D12Resource* resource = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
    int width = 0;
    int height = 0;
    UINT mipLevels = 1;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    bool isCubemap = false;
};
#endif

namespace Eunoia {

using Microsoft::WRL::ComPtr;

enum class TextureStreamState : int {
    Unloaded = 0,
    QueuedForDiskRead = 1,
    ReadingDisk = 2,
    ReadyForGpuUpload = 3,
    UploadingToGpu = 4,
    Ready = 5,
    Failed = 6
};

struct TextureStreamRequest {
    std::string sourcePath;
    std::string cacheKey;
    TextureUsageSlot slot = TextureUsageSlot::BaseColor;
    bool isNormalMap = false;
    TextureStreamState state = TextureStreamState::Unloaded;
    CachedTexture cpuData;
};

struct StreamingTextureInfo {
    int width = 0;
    int height = 0;
    UINT mipLevels = 1;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool isValid = false;
    bool isGpuReady = false;
    bool isStreaming = false;
    float currentLodClamp = 0.0f;
};

struct TextureStreamingMetrics {
    size_t totalRegistered = 0;
    size_t visibleHighResCount = 0;
    size_t nonVisibleLowResCount = 0;
    size_t inFlightUploads = 0;
    size_t pendingDiskReads = 0;
    bool isEnabled = true;
    float nonVisibleLodClamp = 2.0f;
    size_t totalGpuMemoryBytes = 0;
};

} // namespace Eunoia
