#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace Eunoia {
namespace Atmosphere {

struct VolumetricCloudSettings {
    bool enableClouds = true;
    float cloudBottomAltitude = 1500.0f; // meters
    float cloudTopAltitude = 4000.0f;    // meters
    float cloudCoverage = 0.5f;          // 0.0 to 1.0
    float cloudDensity = 0.05f;          // density scale
    float cloudAbsorption = 0.003f;
    float silverLiningSpread = 0.7f;
    float silverLiningIntensity = 1.5f;
    float forwardScatteringG = 0.8f;     // Henyey-Greenstein g
    float backwardScatteringG = -0.2f;
    glm::vec3 cloudBaseColor{1.0f, 1.0f, 1.0f};
    glm::vec3 windDirection{1.0f, 0.0f, 0.5f};
    float windSpeed = 15.0f;             // km/h
    int raymarchSteps = 64;
    int lightMarchSteps = 6;
};

struct VolumetricFogSettings {
    bool enableFog = true;
    float fogDensity = 0.015f;
    float fogHeightFalloff = 0.1f;       // height scale
    float fogBaseHeight = 0.0f;
    float anisotropy = 0.45f;            // forward in-scattering
    glm::vec3 fogAlbedo{0.8f, 0.85f, 0.95f};
    bool enableGodRays = true;
    float godRayIntensity = 1.0f;
    float godRayDecay = 0.96f;
    float godRayDensity = 0.8f;
    float maxFogDistance = 500.0f;
};

struct VolumetricAtmosphereSettings {
    bool enableAtmosphere = true;
    float rayleighScatteringScale = 1.0f;
    float mieScatteringScale = 1.0f;
    float ozoneAbsorptionScale = 1.0f;
    float atmosphereSunIntensity = 1.0f;
    VolumetricCloudSettings clouds;
    VolumetricFogSettings fog;
};

} // namespace Atmosphere
} // namespace Eunoia
