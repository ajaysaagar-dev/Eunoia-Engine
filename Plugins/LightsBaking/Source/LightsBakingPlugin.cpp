// ============================================================================
// Eunoia Engine — Lights-Baking Plugin
// LightsBakingPlugin.cpp: Plugin lifecycle and service registration
// ============================================================================

#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>
#include <LightsBaking/LightBakeManager.h>
#include <iostream>

class LightsBakingPlugin : public IPlugin {
public:
    LightsBakingPlugin() = default;
    ~LightsBakingPlugin() override = default;

    const char* GetName() const override { return "Lights-Baking"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override {
        std::cout << "[LightsBakingPlugin] Registering ILightBakeSystem service...\n";
        registry.Register<Eunoia::ILightBakeSystem>("ILightBakeSystem", &Eunoia::LightBakeManager::Get());
    }

    void OnInit() override {
        std::cout << "[LightsBakingPlugin] Lights-Baking plugin initialized.\n";
    }

    void OnShutdown() override {
        std::cout << "[LightsBakingPlugin] Lights-Baking plugin shutdown.\n";
    }
};
