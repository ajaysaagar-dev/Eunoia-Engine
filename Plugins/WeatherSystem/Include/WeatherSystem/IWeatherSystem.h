#pragma once
#include "WeatherTypes.h"

namespace Eunoia {
namespace Weather {

class IWeatherSystem {
public:
    virtual ~IWeatherSystem() = default;

    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual WeatherType GetWeather() const = 0;
    virtual void SetWeather(WeatherType weather) = 0;

    virtual const WeatherSettings& GetSettings() const = 0;
    virtual WeatherSettings& GetSettingsRef() = 0;
    virtual void SetSettings(const WeatherSettings& settings) = 0;

    virtual void Update(float deltaTime) = 0;
};

} // namespace Weather
} // namespace Eunoia
