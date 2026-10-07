#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace Eunoia { namespace OcclusionCulling {

// ============================================================
//  GPU OCCLUSION CULLING (hardware DX12 occlusion queries)
// ============================================================
struct OcclusionCullingSettings {
    bool    enabled              = true;
    bool    twoPassOcclusion     = true;  // 2-pass: prev frame depth + current
    bool    meshletOcclusion     = true;  // Per-meshlet culling (DX12 amplification shader)
    int     maxOcclusionQueries  = 4096;  // Max simultaneous occlusion queries
    float   occlusionBoundsPad   = 0.0f;  // AABB expansion for conservative culling
};

// ============================================================
//  TILED FORWARD+ RENDERING
//  WickedEngine: Tiled forward with 2.5D culling
// ============================================================
enum class LightingMode : uint8_t {
    ForwardSimple  = 0, // Basic forward
    ForwardPlus    = 1, // Tiled Forward+ (per-tile light lists)
    Deferred       = 2, // Deferred shading
    DeferredTiled  = 3, // Tiled Deferred (Forward+ light culling + deferred shade)
};

struct ForwardPlusSettings {
    LightingMode lightingMode    = LightingMode::ForwardPlus;
    glm::uvec2   tileSize        = glm::uvec2(16, 16);  // Screen tile dimensions in pixels
    int          maxLightsPerTile= 256;   // Max lights per tile
    bool         use25DCulling   = true;  // 2.5D culling: per-tile min/max depth
    bool         transparencySort= true;  // Sort transparent objects front-to-back
    int          maxLights       = 10000; // Engine-wide max light count
    bool         clustered       = true;  // Clustered Forward+ (3D tile slices)
    int          clusterSlices   = 32;    // Z-slice cluster count
};

struct OcclusionCullingSystemSettings {
    OcclusionCullingSettings occlusion;
    ForwardPlusSettings      forwardPlus;
};

class OcclusionCullingSystem {
public:
    static OcclusionCullingSystem& Get() {
        static OcclusionCullingSystem inst;
        return inst;
    }

    void Initialize();
    void Shutdown();

    void SetOcclusionEnabled(bool v)          { m_settings.occlusion.enabled = v; }
    bool GetOcclusionEnabled()         const  { return m_settings.occlusion.enabled; }
    void SetLightingMode(LightingMode m)      { m_settings.forwardPlus.lightingMode = m; }
    LightingMode GetLightingMode()     const  { return m_settings.forwardPlus.lightingMode; }

    OcclusionCullingSystemSettings&       GetSettingsRef()       { return m_settings; }
    const OcclusionCullingSystemSettings& GetSettings()    const { return m_settings; }

private:
    OcclusionCullingSystem() = default;
    OcclusionCullingSystemSettings m_settings;
};

}} // namespace Eunoia::OcclusionCulling
