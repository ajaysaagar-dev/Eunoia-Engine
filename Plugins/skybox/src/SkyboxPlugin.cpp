#include <Skybox/ISkyboxSystem.h>
#include <iostream>

class SkyboxPlugin : public ISkyboxSystem {
private:
    bool m_enabled = true;

public:
    SkyboxPlugin() {}
    ~SkyboxPlugin() override {}

    const char* GetName() const override { return "SkyboxSystem"; }
    const char* GetVersion() const override { return "1.0"; }
    PluginCategory GetCategory() const override { return PluginCategory::World; }

    bool Initialize() override {
        std::cout << "[SkyboxPlugin] Initialized. (Note: Most skybox logic remains in Main.cpp/EngineUI.cpp per instructions)\n";
        return true;
    }

    void Shutdown() override {
        std::cout << "[SkyboxPlugin] Shutting down.\n";
    }

    void Update(float deltaTime) override {
        // Core skybox update handled by engine
    }

    bool GetSkyboxEnabled() const override {
        return m_enabled;
    }

    void SetSkyboxEnabled(bool enabled) override {
        m_enabled = enabled;
    }
};

static PluginInfo s_info = {
    "SkyboxSystem",
    "1.0",
    "Skybox and environment lighting plugin",
    "Eunoia Engine",
    PluginCategory::World,
    false, // isCore
    100    // priority
};

EUNOIA_DECLARE_PLUGIN(SkyboxPlugin, &s_info)
