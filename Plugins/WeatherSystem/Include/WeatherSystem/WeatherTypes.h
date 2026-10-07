#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace Eunoia {
namespace Weather {

enum class WeatherType : uint32_t {
    Clear = 0,
    Cloudy = 1,
    LightRain = 2,
    HeavyRain = 3,
    Thunderstorm = 4,
    Foggy = 5
};

struct RainSettings {
    bool enableRain = false;
    float rainIntensity = 0.5f;        // 0.0 to 1.0
    float rainSpeed = 12.0f;            // m/s
    float rainStreakLength = 0.6f;
    glm::vec3 windVelocity{2.0f, -9.8f, 1.0f};
    int maxRainDroplets = 32768;

    // Surface Wetness & Puddle Simulation
    bool enablePuddles = true;
    float wetnessAccumulationRate = 0.05f;
    float dryingRate = 0.01f;
    float currentWetness = 0.0f;        // 0.0 (dry) to 1.0 (fully saturated)
    float puddleScale = 0.25f;
    float puddleRippleSpeed = 1.5f;
    float puddleReflectivity = 0.95f;
};

struct WeatherSettings {
    WeatherType activeWeather = WeatherType::Clear;
    RainSettings rain;
};

} // namespace Weather
} // namespace Eunoia
