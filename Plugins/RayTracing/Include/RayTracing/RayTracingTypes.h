#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>

namespace Eunoia {
namespace RayTracing {

enum class RayTracingMode : uint32_t {
    Off = 0,
    HybridSSRT = 1,       // High-performance screen-space ray tracing + world probe fallback
    HardwareDXR = 2       // DirectX 12 DXR 1.1 hardware acceleration structures (BLAS/TLAS)
};

enum class RayTracingQuality : uint32_t {
    Low = 0,        // 1 ray per pixel
    Medium = 1,     // 2 rays per pixel
    High = 2,       // 4 rays per pixel
    Ultra = 3,      // 8 rays per pixel
    Cinematic = 4   // 16 rays per pixel
};

enum class ToneMappingOperator : uint32_t {
    ACESFilmic = 0,
    Reinhard = 1,
    Uncharted2 = 2,
    Neutral = 3
};

struct RayTracingSettings {
    // Master Switches
    bool enableRayTracing = false;
    RayTracingMode mode = RayTracingMode::HybridSSRT;
    RayTracingQuality quality = RayTracingQuality::High;

    // Ray Traced Ambient Occlusion (RTAO)
    bool enableRTAO = true;
    float rtaoRadius = 2.5f;
    float rtaoPower = 1.5f;
    int rtaoSamples = 4;

    // Ray Traced Reflections (RTR)
    bool enableRTR = true;
    float rtrRoughnessCutoff = 0.65f;
    float rtrMaxDistance = 50.0f;
    int rtrMaxSteps = 32;
    float rtrThickness = 0.5f;

    // Ray Traced Shadows (RTS - Contact Hardening & Penumbra)
    bool enableRTS = true;
    float rtsSoftness = 1.0f;
    float rtsLightRadius = 0.05f;

    // Ray Traced Global Illumination (RTGI - Indirect Diffuse Irradiance)
    bool enableRTGI = true;
    float rtgiIntensity = 1.0f;
    int rtgiBounces = 1;

    // Temporal-Spatial Edge-Stopping Denoiser Filter
    bool enableDenoiser = true;
    float denoiserSharpness = 0.8f;

    // DirectX 12 Advanced Visual Effects & Volumetrics
    bool enableVolumetricFog = true;
    float fogDensity = 0.02f;
    float fogAnisotropy = 0.4f;

    bool enableBloom = true;
    float bloomThreshold = 1.0f;
    float bloomIntensity = 0.8f;

    bool enableDepthOfField = false;
    float dofFocalDistance = 10.0f;
    float dofFocalRange = 15.0f;

    bool enableMotionBlur = false;
    float motionBlurStrength = 0.5f;

    bool enableChromaticAberration = false;
    float chromaticAberrationStrength = 0.003f;

    bool enableVignette = false;
    float vignetteIntensity = 0.35f;

    ToneMappingOperator toneMapping = ToneMappingOperator::ACESFilmic;
};

struct RayTracingStats {
    uint32_t totalRaysDispatched = 0;
    uint32_t reflectionRays = 0;
    uint32_t shadowRays = 0;
    uint32_t ambientOcclusionRays = 0;
    float frameRayTraceTimeMs = 0.0f;
    bool isHardwareDxrAvailable = false;
};

} // namespace RayTracing
} // namespace Eunoia
