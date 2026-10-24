#pragma once
// ============================================================================
// Eunoia Engine — Texture Streaming Plugin
// ITextureStreamingSystem: Interface for Texture Streaming service in Plugin Architecture
// ============================================================================

#include "TextureStreamingTypes.h"
#include <EunoiaPluginCore/IPlugin.h>

namespace Eunoia {

class ITextureStreamingSystem {
public:
    virtual ~ITextureStreamingSystem() = default;

    virtual bool Initialize(ID3D12Device* device, ID3D12CommandQueue* directQueue) = 0;
    virtual void Shutdown() = 0;

    // Requests asynchronous texture streaming. If already uploaded, returns true.
    virtual bool RequestTexture(const std::string& name, TextureUsageSlot slot = TextureUsageSlot::BaseColor) = 0;

    // Wicked Engine API: Requests full or specific resolution for a texture resource
    virtual void StreamingRequestResolution(const std::string& name, uint32_t resolution) = 0;

    // Global streaming toggle & threshold controls (Wicked Engine style)
    virtual bool IsTextureStreamingEnabled() const = 0;
    virtual void SetTextureStreamingEnabled(bool enabled) = 0;

    // Out-of-viewport LOD clamp (default 2.0f = 1/4 resolution, 3.0f = 1/8 resolution)
    virtual float GetNonVisibleLodClamp() const = 0;
    virtual void SetNonVisibleLodClamp(float clamp) = 0;

    // Wicked Engine technique: Called each frame with textures visible in active camera frustum
    virtual void UpdateViewportVisibility(const std::unordered_set<std::string>& visibleTextureNames) = 0;

    // Called once per frame on render thread to process upload batches & retire staging fences
    virtual void Update(size_t maxUploadsPerFrame = 6) = 0;

    // Instantly probes texture metadata (DDS header / image header) in < 0.05ms
    virtual StreamingTextureInfo GetOrProbeTextureInfo(const std::string& name, TextureUsageSlot slot = TextureUsageSlot::BaseColor) = 0;

    virtual bool IsTextureReady(const std::string& name, TextureUsageSlot slot = TextureUsageSlot::BaseColor) const = 0;
    virtual TextureStreamingMetrics GetMetrics() const = 0;
};

} // namespace Eunoia
