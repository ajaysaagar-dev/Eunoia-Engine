#pragma once
#include <cstdint>

namespace Eunoia { namespace SSE {

// --- SSAO Mode ---
enum class SSAOMode : uint8_t {
    Disabled = 0,
    SSAO,   // Basic screen-space ambient occlusion
    HBAO,   // Horizon-Based AO (higher quality, angular estimation)
    MSAO,   // Multi-Scale AO (multiple radius samples, WickedEngine style)
};

// --- SSSR (Stochastic Screen Space Reflections) ---
struct SSSRSettings {
    bool enableSSR               = false;  // Enable stochastic SSR
    float roughnessThreshold     = 0.6f;   // Max roughness to trace reflections
    int   maxRaySteps            = 64;     // Ray march steps
    int   sampleCount            = 1;      // Temporal sample accumulation count
    float thickness              = 0.1f;   // Depth thickness for hit detection
    float temporalBlend          = 0.9f;   // Temporal accumulation blend factor
    bool  enableDenoiser         = true;   // Spatiotemporal denoiser
};

// --- Screen-Space Refractions ---
struct SSRefractionSettings {
    bool  enableRefractions      = false;
    float blurStrength           = 0.03f;  // Refraction blur amount
    float indexOfRefraction      = 1.33f;  // Default: water
};

// --- SSAO / HBAO / MSAO ---
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

// --- Full SSE Settings ---
struct ScreenSpaceEffectsSettings {
    SSAOSettings       ao;
    SSSRSettings       ssr;
    SSRefractionSettings refractions;
};

}} // namespace Eunoia::SSE
