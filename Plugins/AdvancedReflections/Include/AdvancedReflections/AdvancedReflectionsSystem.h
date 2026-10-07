#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace Eunoia { namespace Reflections {

// ============================================================
//  PLANAR REFLECTIONS (real-time mirror plane)
// ============================================================
struct PlanarReflectionSettings {
    bool      enabled            = false;
    glm::vec4 reflectionPlane    = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f); // Normal + D
    int       resolution         = 512;   // Render target resolution
    float     clipPlaneOffset    = 0.01f; // Oblique near clip offset
    bool      useRoughnessFade   = true;  // Fade reflection by surface roughness
    float     roughnessCutoff    = 0.1f;  // Above this: fade to cubemap
};

// ============================================================
//  CUBEMAP REFLECTIONS
// ============================================================
enum class CubemapType : uint8_t {
    Static   = 0,  // Pre-baked, loaded from file
    Dynamic  = 1,  // Rendered every N frames (real-time)
};

struct CubemapReflectionSettings {
    bool        enabled           = true;
    CubemapType type              = CubemapType::Dynamic;
    int         resolution        = 128;   // Per-face resolution
    int         updateEveryNFrames= 8;     // Dynamic update rate
    float       nearClip          = 0.1f;
    float       farClip           = 1000.0f;
    bool        useParallaxCorrection = true; // Local AABB parallax correction
    glm::vec3   capturePosition   = glm::vec3(0.0f);
    glm::vec3   captureExtents    = glm::vec3(10.0f);
};

// ============================================================
//  STOCHASTIC SCREEN SPACE REFLECTIONS
// ============================================================
enum class SSRQuality : uint8_t {
    Low    = 0,
    Medium = 1,
    High   = 2,
    Ultra  = 3,
};

struct StochasticSSRSettings {
    bool       enabled             = false;
    SSRQuality quality             = SSRQuality::Medium;
    float      roughnessThreshold  = 0.6f;   // Skip SSR above this roughness
    int        maxRaySteps         = 64;
    float      thickness           = 0.1f;
    float      temporalBlend       = 0.9f;
    bool       enableDenoiser      = true;
    bool       fallbackToCubemap   = true;   // Use cubemap for missed rays
    bool       useHierarchicalZ    = true;   // HiZ raymarching (WickedEngine)
};

// ============================================================
//  COMBINED REFLECTIONS SETTINGS
// ============================================================
struct AdvancedReflectionsSettings {
    PlanarReflectionSettings planar;
    CubemapReflectionSettings cubemap;
    StochasticSSRSettings     ssr;
};

class AdvancedReflectionsSystem {
public:
    static AdvancedReflectionsSystem& Get() {
        static AdvancedReflectionsSystem inst;
        return inst;
    }

    void Initialize();
    void Shutdown();

    void SetPlanarEnabled(bool v)          { m_settings.planar.enabled = v; }
    void SetCubemapEnabled(bool v)         { m_settings.cubemap.enabled = v; }
    void SetSSREnabled(bool v)             { m_settings.ssr.enabled = v; }
    void SetSSRQuality(SSRQuality q)       { m_settings.ssr.quality = q; }

    AdvancedReflectionsSettings&       GetSettingsRef()       { return m_settings; }
    const AdvancedReflectionsSettings& GetSettings()    const { return m_settings; }

private:
    AdvancedReflectionsSystem() = default;
    AdvancedReflectionsSettings m_settings;
};

}} // namespace Eunoia::Reflections
