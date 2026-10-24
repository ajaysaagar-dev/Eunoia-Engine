#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace Eunoia { namespace PostProcess {

// ============================================================
//  BLOOM
// ============================================================
struct BloomSettings {
    bool  enabled            = false;
    float threshold          = 1.0f;   // Luminance threshold for bleed
    float strength           = 0.5f;   // Overall bloom intensity
    float scatter            = 0.7f;   // Spread radius factor
    int   mipChainLength     = 6;      // Number of blur mip levels
    bool  useHDR             = true;   // HDR-aware bloom
};

// ============================================================
//  MOTION BLUR
// ============================================================
struct MotionBlurSettings {
    bool  enabled            = false;
    float strength           = 1.0f;   // 0..2 scale on velocity
    int   sampleCount        = 16;     // Blur taps per pixel
    float shutterAngle       = 180.0f; // Degrees (cinema = 180)
};

// ============================================================
//  BOKEH DEPTH OF FIELD
// ============================================================
struct BokehDOFSettings {
    bool  enabled            = false;
    float focusDistance      = 10.0f;  // World-space focal distance
    float focusRange         = 3.0f;   // Sharp zone half-extent
    float bokehRadius        = 8.0f;   // Max CoC radius in pixels
    float bokehBladeCurvature= 0.5f;   // 0=circular, 1=polygonal
    int   bokehBladeCount    = 6;      // Aperture blade count
    float bokehRotation      = 0.0f;   // Aperture rotation degrees
    bool  nearField          = true;   // Enable near-field blur
    int   sampleCount        = 32;     // Bokeh sample kernel taps
};

// ============================================================
//  LENS FLARE
// ============================================================
struct LensFlareSettings {
    bool  enabled            = false;
    float scale              = 1.0f;
    float intensity          = 0.3f;
    glm::vec3 tint           = glm::vec3(1.0f, 0.9f, 0.8f);
    bool  occlusionTest      = true;   // GPU occlusion query test
};

// ============================================================
//  LIGHT SHAFTS (CREPUSCULAR GOD RAYS)
// ============================================================
struct LightShaftsSettings {
    bool  enabled            = false;
    int   numSamples         = 64;
    float exposure           = 0.0034f;
    float decay              = 1.0f;
    float density            = 0.84f;
    float weight             = 5.65f;
    glm::vec3 tint           = glm::vec3(1.0f, 0.95f, 0.85f);
};

// ============================================================
//  CHROMATIC ABERRATION
// ============================================================
struct ChromaticAberrationSettings {
    bool  enabled            = false;
    float amount             = 0.003f; // UV offset per channel
    bool  radialFalloff      = true;   // Stronger at screen edges
};

// ============================================================
//  EYE ADAPTATION (AUTO-EXPOSURE / HDR)
// ============================================================
struct EyeAdaptationSettings {
    bool  enabled            = true;
    float minEV              = -4.0f;  // Min log2 exposure value
    float maxEV              = 4.0f;   // Max log2 exposure value
    float adaptSpeed         = 2.0f;   // Adaptation speed (EV/sec)
    float targetLuminance    = 0.5f;   // Target average luminance
    // Histogram-based
    bool  useHistogram       = true;
    float lowPercentile      = 0.1f;   // Clip dark fraction
    float highPercentile     = 0.9f;   // Clip bright fraction
};

// ============================================================
//  SHARPEN FILTER
// ============================================================
struct SharpenSettings {
    bool  enabled            = false;
    float amount             = 0.5f;   // 0..1 sharpening amount
};

// ============================================================
//  EDGE OUTLINE
// ============================================================
struct EdgeOutlineSettings {
    bool      enabled        = false;
    glm::vec4 color          = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // RGBA
    float     thickness      = 1.0f;   // Outline pixel thickness
    float     depthThreshold = 0.01f;
    float     normalThreshold= 0.1f;
};

// ============================================================
//  SCREEN SPACE EFFECTS (SSAO, SSSR, REFRACTIONS)
// ============================================================
enum class SSAOMode : uint8_t {
    Disabled = 0,
    SSAO,   // Basic screen-space ambient occlusion (Wicked Engine AO_SSAO)
    HBAO,   // Horizon-Based AO (Wicked Engine AO_HBAO)
    MSAO,   // Multi-Scale AO (Wicked Engine AO_MSAO)
    RTAO,   // Ray-Traced AO (Wicked Engine AO_RTAO)
};

struct SSSRSettings {
    bool  enableSSR              = false;  // Enable stochastic SSR
    float roughnessThreshold     = 0.6f;   // Max roughness to trace reflections
    int   maxRaySteps            = 64;     // Ray march steps
    int   sampleCount            = 1;      // Temporal sample accumulation count
    float thickness              = 0.1f;   // Depth thickness for hit detection
    float temporalBlend          = 0.9f;   // Temporal accumulation blend factor
    bool  enableDenoiser         = true;   // Spatiotemporal denoiser
};

struct SSRefractionSettings {
    bool  enableRefractions      = false;
    float blurStrength           = 0.03f;  // Refraction blur amount
    float indexOfRefraction      = 1.33f;  // Default: water
};

struct SSAOSettings {
    SSAOMode mode                = SSAOMode::SSAO;
    float    radius              = 0.5f;   // World-space sample radius
    float    power               = 1.5f;   // Intensity exponent
    float    bias                = 0.025f; // Self-occlusion bias
    int      sampleCount         = 16;     // Samples per pixel (SSAO)
    // HBAO specific
    float    hbaoAngleBias       = 0.1f;
    int      hbaoDirections      = 8;
    int      hbaoSteps           = 4;
    // MSAO specific
    float    msaoRadiusMultiplier= 2.5f;
    bool     enableBlur          = true;
    float    blurSigma           = 2.0f;
    // Temporal
    bool     enableTemporalAO    = true;
    float    temporalBlend       = 0.9f;
};

struct ScreenSpaceEffectsSettings {
    SSAOSettings         ao;
    SSSRSettings         ssr;
    SSRefractionSettings refractions;
};

// ============================================================
//  PRESET
// ============================================================
enum class PostProcessPreset : uint8_t {
    Off,
    Cinematic,
    GameBalanced,
    Stylized,
    Photorealistic,
};

// ============================================================
//  COMBINED SETTINGS
// ============================================================
struct PostProcessSettings {
    BloomSettings               bloom;
    MotionBlurSettings          motionBlur;
    BokehDOFSettings            dof;
    LensFlareSettings           lensFlare;
    LightShaftsSettings         lightShafts;
    ChromaticAberrationSettings chromaticAberration;
    EyeAdaptationSettings       eyeAdaptation;
    SharpenSettings             sharpen;
    EdgeOutlineSettings         edgeOutline;
    ScreenSpaceEffectsSettings  sse;
};

// Backward compatibility alias
using AdvancedPostProcessSettings = PostProcessSettings;

}} // namespace Eunoia::PostProcess

// Global namespace alias for SSE compatibility
namespace Eunoia { namespace SSE {
    using SSAOMode = ::Eunoia::PostProcess::SSAOMode;
    using SSSRSettings = ::Eunoia::PostProcess::SSSRSettings;
    using SSRefractionSettings = ::Eunoia::PostProcess::SSRefractionSettings;
    using SSAOSettings = ::Eunoia::PostProcess::SSAOSettings;
    using ScreenSpaceEffectsSettings = ::Eunoia::PostProcess::ScreenSpaceEffectsSettings;
}}
