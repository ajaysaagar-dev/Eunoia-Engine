#pragma once

#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>

namespace Eunoia {

class BehaviourPlugin : public IPlugin {
public:
    BehaviourPlugin() = default;
    ~BehaviourPlugin() override = default;

    const char* GetName() const override { return "Behaviour"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override;
    void OnInit() override;
    void OnShutdown() override;
};

} // namespace Eunoia
