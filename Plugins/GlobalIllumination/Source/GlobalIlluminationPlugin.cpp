// ============================================================================
// Eunoia Engine — Global-Illumination Plugin
// GlobalIlluminationPlugin.cpp: Plugin lifecycle and service registration
// ============================================================================

#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>
#include "GlobalIllumination/GlobalIllumination.h"
#include <iostream>

namespace Eunoia {
namespace GI {

class GlobalIlluminationPlugin : public IPlugin {
public:
    GlobalIlluminationPlugin() = default;
    ~GlobalIlluminationPlugin() override = default;

    const char* GetName() const override { return "Global-Illumination"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override {
        std::cout << "[GlobalIlluminationPlugin] Registering IGlobalIllumination service...\n";
        registry.Register<IGlobalIllumination>("IGlobalIllumination", &GlobalIllumination::Get());
    }

    void OnInit() override {
        GlobalIllumination::Get().Initialize();
        std::cout << "[GlobalIlluminationPlugin] Global-Illumination plugin initialized.\n";
    }

    void OnShutdown() override {
        GlobalIllumination::Get().Shutdown();
        std::cout << "[GlobalIlluminationPlugin] Global-Illumination plugin shutdown.\n";
    }
};

} // namespace GI
} // namespace Eunoia
