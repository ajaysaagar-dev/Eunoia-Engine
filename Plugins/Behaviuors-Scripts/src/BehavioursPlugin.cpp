// ============================================================================
// Eunoia Engine — Behaviuors-Scripts Plugin Implementation
// ============================================================================

#include "Behaviours/IBehaviourService.h"
#include "PluginAPI.h"
#include "IPlugin.h"
#include "ServiceRegistry.h"

#include <iostream>

class BehaviourServiceImpl : public IBehaviourService {
public:
    size_t GetRegisteredCount() const override { return 0; }
};

class BehavioursScriptsPlugin : public IPlugin {
public:
    explicit BehavioursScriptsPlugin(const EunoiaPluginContext* /*ctx*/) {}
    ~BehavioursScriptsPlugin() override = default;

    const char* GetName() const override { return "Behaviuors-Scripts"; }
    bool SupportsHotReload() const override { return true; }

    void OnRegister(ServiceRegistry& registry) override {
        std::cout << "[Behaviuors-Scripts] Registering IBehaviourService service..." << std::endl;
        registry.Register<IBehaviourService>("IBehaviourService", &m_service);
    }

    void OnInit() override {
        std::cout << "[Behaviuors-Scripts] Plugin initialized." << std::endl;
    }

    void OnShutdown() override {
        std::cout << "[Behaviuors-Scripts] Plugin shutdown." << std::endl;
    }

private:
    BehaviourServiceImpl m_service;
};

static EunoiaPluginInfo s_behaviuorsScriptsInfo = {
    "Behaviuors-Scripts",
    "1.0.0",
    "Component gameplay scripting and reflection",
    "Eunoia Team",
    EUNOIA_PLUGIN_API_VERSION,
    EunoiaPluginCategory::Behaviours,
    false // dynamic tier
};

EUNOIA_DECLARE_PLUGIN(BehavioursScriptsPlugin, &s_behaviuorsScriptsInfo)
