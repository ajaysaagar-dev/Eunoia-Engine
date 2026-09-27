#include <EunoiaPluginCore/IPlugin.h>
#include <Water/WaterSystem.h>
#include <iostream>

class WaterPlugin : public IPlugin {
public:
    WaterPlugin() = default;
    ~WaterPlugin() override = default;

    void OnRegister(ServiceRegistry& /*registry*/) override {
        // Services can be registered here
    }

    void OnInit() override {
        std::cout << "[WaterPlugin] AquaTerrain Water System plugin registered.\n";
    }

    void OnUpdate(float deltaTime) override {
        Eunoia::WaterSystem::Get().Update(deltaTime);
    }

    void OnShutdown() override {
        Eunoia::WaterSystem::Get().Shutdown();
    }
};
