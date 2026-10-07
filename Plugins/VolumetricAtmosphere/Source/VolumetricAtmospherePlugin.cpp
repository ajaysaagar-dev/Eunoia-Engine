// ============================================================================
// Eunoia Engine — Volumetric-Atmosphere Plugin
// VolumetricAtmospherePlugin.cpp: Plugin lifecycle and service registration
// ============================================================================

#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>
#include "VolumetricAtmosphere/VolumetricAtmosphere.h"
#include <iostream>

namespace Eunoia {
namespace Atmosphere {

class VolumetricAtmospherePlugin : public IPlugin {
public:
    VolumetricAtmospherePlugin() = default;
    ~VolumetricAtmospherePlugin() override = default;

    const char* GetName() const override { return "Volumetric-Atmosphere"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override {
        std::cout << "[VolumetricAtmospherePlugin] Registering IVolumetricAtmosphere service...\n";
        registry.Register<IVolumetricAtmosphere>("IVolumetricAtmosphere", &VolumetricAtmosphere::Get());
    }

    void OnInit() override {
        VolumetricAtmosphere::Get().Initialize();
        std::cout << "[VolumetricAtmospherePlugin] Volumetric-Atmosphere plugin initialized.\n";
    }

    void OnShutdown() override {
        VolumetricAtmosphere::Get().Shutdown();
        std::cout << "[VolumetricAtmospherePlugin] Volumetric-Atmosphere plugin shutdown.\n";
    }
};

} // namespace Atmosphere
} // namespace Eunoia
