#include <Water/IWaterSystem.h>
#include <iostream>

class WaterPlugin : public IWaterSystem {
private:
    bool m_enabled = true;

public:
    WaterPlugin() {}
    ~WaterPlugin() override {}

    const char* GetName() const override { return "WaterSystem"; }
    const char* GetVersion() const override { return "1.0"; }
    PluginCategory GetCategory() const override { return PluginCategory::World; }

    bool Initialize() override {
        std::cout << "[WaterPlugin] Initialized.\n";
        return true;
    }

    void Shutdown() override {
        std::cout << "[WaterPlugin] Shutting down.\n";
    }

    void Update(float deltaTime) override {
        // Water physics logic would go here
    }

    int GetActiveWaterBodyCount() const override {
        return 0; // Stub
    }

    void EnableWaterSystem(bool enable) override {
        m_enabled = enable;
    }
};

static PluginInfo s_info = {
    "WaterSystem",
    "1.0",
    "Provides realistic water bodies like oceans, lakes, and rivers",
    "Eunoia Engine",
    PluginCategory::World,
    false, // isCore
    100    // priority
};

EUNOIA_DECLARE_PLUGIN(WaterPlugin, &s_info)
