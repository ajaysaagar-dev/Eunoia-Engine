#pragma once

#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>

namespace Eunoia {

class AnimationGraphPlugin : public IPlugin {
public:
    AnimationGraphPlugin() = default;
    ~AnimationGraphPlugin() override = default;

    const char* GetName() const override { return "Animation-Graph"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override;
    void OnInit() override;
    void OnShutdown() override;
};

} // namespace Eunoia
