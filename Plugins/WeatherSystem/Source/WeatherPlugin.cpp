// ============================================================================
// Eunoia Engine — Weather-System Plugin
// WeatherPlugin.cpp: Plugin lifecycle and service registration
// ============================================================================

#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>
#include "WeatherSystem/WeatherSystem.h"
#include <iostream>

namespace Eunoia {
namespace Weather {

class WeatherPlugin : public IPlugin {
public:
    WeatherPlugin() = default;
    ~WeatherPlugin() override = default;

    const char* GetName() const override { return "Weather-System"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override {
        std::cout << "[WeatherPlugin] Registering IWeatherSystem service...\n";
        registry.Register<IWeatherSystem>("IWeatherSystem", &WeatherSystem::Get());
    }

    void OnInit() override {
        WeatherSystem::Get().Initialize();
        std::cout << "[WeatherPlugin] Weather-System plugin initialized.\n";
    }

    void OnShutdown() override {
        WeatherSystem::Get().Shutdown();
        std::cout << "[WeatherPlugin] Weather-System plugin shutdown.\n";
    }
};

} // namespace Weather
} // namespace Eunoia
