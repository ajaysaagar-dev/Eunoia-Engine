#pragma once
#include "IWeatherSystem.h"
#include <mutex>

namespace Eunoia {
namespace Weather {

class WeatherSystem : public IWeatherSystem {
public:
    static WeatherSystem& Get();

    bool Initialize() override;
    void Shutdown() override;

    WeatherType GetWeather() const override { return m_settings.activeWeather; }
    void SetWeather(WeatherType weather) override;

    const WeatherSettings& GetSettings() const override { return m_settings; }
    WeatherSettings& GetSettingsRef() override { return m_settings; }
    void SetSettings(const WeatherSettings& settings) override { m_settings = settings; }

    void Update(float deltaTime) override;

private:
    WeatherSystem() = default;
    ~WeatherSystem() override = default;

    WeatherSettings m_settings;
    mutable std::mutex m_mutex;
    bool m_initialized = false;
};

} // namespace Weather
} // namespace Eunoia
