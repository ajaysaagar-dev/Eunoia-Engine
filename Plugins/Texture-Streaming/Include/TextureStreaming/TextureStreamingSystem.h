#pragma once
// ============================================================================
// Eunoia Engine — Texture Streaming Plugin
// TextureStreamingSystem: Viewport Visibility-Driven DX12 Texture Streaming
//
// Implements Wicked Engine's Texture Streaming Architecture:
// 1. Visible textures in active camera frustum retain 100% native quality (MinLODClamp = 0.0f).
// 2. Out-of-viewport textures drop to low resolution (MinLODClamp = 2.0f / 3.0f).
// 3. Materials can opt-out with "Disable Texture Streaming" flag.
// 4. Zero GPU stalls: dynamic SRV clamp updates without reallocating VRAM or staging copies.
// 5. Instantaneous DDS/image header probing (<0.05ms) eliminates UI loading hangs.
// ============================================================================

#include "ITextureStreamingSystem.h"
#include "TextureStreamingTypes.h"
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

namespace Eunoia {

class TextureStreamingSystem : public ITextureStreamingSystem {
public:
    static TextureStreamingSystem& Get() {
        static TextureStreamingSystem s_instance;
        return s_instance;
    }

    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* directQueue) override;
    void Shutdown() override;

    void SetDescriptorAllocator(
        ID3D12DescriptorHeap* srvHeap,
        UINT descriptorSize,
        std::function<bool(UINT count, UINT& outBaseIndex)> allocFn
    );

    // Request texture asynchronously. If already ready, returns true.
    bool RequestTexture(const std::string& name, TextureUsageSlot slot = TextureUsageSlot::BaseColor) override;

    // Wicked Engine API: Request resolution for a resource (e.g. 65536 for full quality)
    void StreamingRequestResolution(const std::string& name, uint32_t resolution) override;

    // Returns GPU texture if ready; otherwise returns fallback and registers slot for auto-patching
    DX12GpuTexture GetTextureOrFallback(
        const std::string& name,
        const DX12GpuTexture& fallback,
        TextureUsageSlot slot = TextureUsageSlot::BaseColor,
        D3D12_CPU_DESCRIPTOR_HANDLE optionalTableSlot = {}
    );

    // Registers a material descriptor table slot to be updated when texture completes upload or changes LOD
    void RegisterMaterialTableSlot(const std::string& cacheKey, D3D12_CPU_DESCRIPTOR_HANDLE tableSlot);

    // Called once per frame on the main render thread to process uploads and retire staging buffers
    void Update(size_t maxUploadsPerFrame = 6) override;

    // Wicked Engine technique: called every frame with the set of textures visible in camera viewport
    void UpdateViewportVisibility(const std::unordered_set<std::string>& visibleTextureNames) override;

    // Waits until all pending disk I/O and GPU uploads complete
    void WaitForAllPending();

    bool IsTextureReady(const std::string& name, TextureUsageSlot slot = TextureUsageSlot::BaseColor) const override;
    size_t GetPendingRequestCount() const;
    size_t GetInFlightUploadCount() const;

    void InvalidateTexture(const std::string& name);

    // Instantly probe DDS header or image header without loading entire image into RAM
    StreamingTextureInfo GetOrProbeTextureInfo(const std::string& name, TextureUsageSlot slot = TextureUsageSlot::BaseColor) override;

    bool IsTextureStreamingEnabled() const override { return m_textureStreamingEnabled.load(); }
    void SetTextureStreamingEnabled(bool enabled) override { m_textureStreamingEnabled.store(enabled); }

    float GetNonVisibleLodClamp() const override { return m_nonVisibleLodClamp.load(); }
    void SetNonVisibleLodClamp(float clamp) override { m_nonVisibleLodClamp.store(clamp); }

    TextureStreamingMetrics GetMetrics() const override;

private:
    TextureStreamingSystem() = default;
    ~TextureStreamingSystem() override = default;

    struct InFlightUpload {
        UINT64 fenceValue = 0;
        ComPtr<ID3D12Resource> stagingBuffer;
        ComPtr<ID3D12Resource> defaultTexture;
        DX12GpuTexture gpuTexture;
        std::string cacheKey;
    };

    std::string BuildCacheKey(const std::string& name, TextureUsageSlot slot) const;
    void ApplyLodClampToSrv(DX12GpuTexture& tex, float clamp);

    ComPtr<ID3D12Device>              m_device;
    ComPtr<ID3D12CommandQueue>         m_queue;
    ComPtr<ID3D12CommandAllocator>     m_cmdAlloc;
    ComPtr<ID3D12GraphicsCommandList>  m_cmdList;
    ComPtr<ID3D12Fence>                m_uploadFence;
    HANDLE                             m_fenceEvent = nullptr;
    UINT64                             m_uploadFenceValue = 0;

    ID3D12DescriptorHeap*              m_srvHeap = nullptr;
    UINT                               m_srvDescriptorSize = 0;
    std::function<bool(UINT, UINT&)>   m_srvAllocFn;

    mutable std::mutex                 m_syncMutex;
    std::unordered_map<std::string, DX12GpuTexture>          m_readyTextures;
    std::unordered_map<std::string, ComPtr<ID3D12Resource>>    m_persistentTextures;
    std::unordered_map<std::string, TextureStreamState>      m_textureStates;
    std::unordered_map<std::string, StreamingTextureInfo>    m_metadataCache;
    std::unordered_map<std::string, float>                   m_currentLodClamps;
    std::unordered_set<std::string>                          m_requestedHighResTextures;
    std::vector<std::shared_ptr<TextureStreamRequest>>       m_completedCpuLoads;
    std::vector<InFlightUpload>                              m_inFlightUploads;
    std::unordered_multimap<std::string, D3D12_CPU_DESCRIPTOR_HANDLE> m_activeTableSlots;

    std::atomic<bool>                  m_initialized{false};
    std::atomic<bool>                  m_textureStreamingEnabled{true};
    std::atomic<float>                 m_nonVisibleLodClamp{2.0f}; // 2.0f = 1/4 res for culled textures
};

} // namespace Eunoia

namespace EngineRenderer {
    using TextureStreamingSystem = Eunoia::TextureStreamingSystem;
    using TextureStreamState = Eunoia::TextureStreamState;
    using TextureStreamRequest = Eunoia::TextureStreamRequest;
    using StreamingTextureInfo = Eunoia::StreamingTextureInfo;
    using TextureStreamingMetrics = Eunoia::TextureStreamingMetrics;
}
