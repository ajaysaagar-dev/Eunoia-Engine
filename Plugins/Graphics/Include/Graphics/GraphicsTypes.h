#pragma once
// ============================================================================
// Eunoia Engine — Graphics Plugin
// GraphicsTypes: Core types for Graphics Quality, Anti-Aliasing, and Presets
// ============================================================================

#include <string>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

namespace Eunoia {

// Supported Anti-Aliasing methods as specified in dev.md
enum class AntiAliasingMethod : uint32_t {
    None = 0,
    TAA,
    TSR,
    SMAA,
    FXAA,
    MSAA_2x_4x,
    SSAA,
    DLSS,
    FSR,
    XeSS
};

struct AntiAliasingInfo {
    AntiAliasingMethod method;
    const char* name;
    const char* quality;
    const char* cost;
    const char* role;
    const char* shortcutHint;
};

struct GraphicsQualitySettings {
    AntiAliasingMethod antiAliasing = AntiAliasingMethod::TAA;
    int shadowQuality = 2;       // 0=Low, 1=Medium, 2=High, 3=Ultra
    int textureQuality = 2;      // 0=Low, 1=Medium, 2=High, 3=Ultra
    int postProcessQuality = 2;  // 0=Low, 1=Medium, 2=High, 3=Ultra
    float resolutionScale = 1.0f;
    float sharpness = 0.5f;
    bool vsync = true;
};

} // namespace Eunoia
