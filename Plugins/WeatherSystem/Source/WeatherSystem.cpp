#include "WeatherSystem/WeatherSystem.h"
#include <iostream>
#include <algorithm>

namespace Eunoia {
namespace Weather {

WeatherSystem& WeatherSystem::Get() {
    static WeatherSystem instance;
    return instance;
}

bool WeatherSystem::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return true;

    std::cout << "[WeatherSystem] Initializing Dynamic GPU Weather & Precipitation Simulation...\n";
    m_initialized = true;
    return true;
}

void WeatherSystem::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_initialized = false;
    std::cout << "[WeatherSystem] Weather System shut down.\n";
}

void WeatherSystem::SetWeather(WeatherType weather) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_settings.activeWeather = weather;

    switch (weather) {
        case WeatherType::Clear:
            m_settings.rain.enableRain = false;
            break;
        case WeatherType::Cloudy:
            m_settings.rain.enableRain = false;
            break;
        case WeatherType::LightRain:
            m_settings.rain.enableRain = true;
            m_settings.rain.rainIntensity = 0.3f;
            break;
        case WeatherType::HeavyRain:
            m_settings.rain.enableRain = true;
            m_settings.rain.rainIntensity = 0.8f;
            break;
        case WeatherType::Thunderstorm:
            m_settings.rain.enableRain = true;
            m_settings.rain.rainIntensity = 1.0f;
            break;
        case WeatherType::Foggy:
            m_settings.rain.enableRain = false;
            break;
    }
}

void WeatherSystem::Update(float deltaTime) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_settings.rain.enableRain) {
        // Accumulate surface wetness
        float rate = m_settings.rain.wetnessAccumulationRate * m_settings.rain.rainIntensity;
        m_settings.rain.currentWetness = std::min(1.0f, m_settings.rain.currentWetness + rate * deltaTime);
    } else {
        // Dry surfaces gradually
        m_settings.rain.currentWetness = std::max(0.0f, m_settings.rain.currentWetness - m_settings.rain.dryingRate * deltaTime);
    }
}

} // namespace Weather
} // namespace Eunoia
