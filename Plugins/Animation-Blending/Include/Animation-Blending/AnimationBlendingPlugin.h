#pragma once

#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>

namespace Eunoia {

class AnimationBlendingPlugin : public IPlugin {
public:
    AnimationBlendingPlugin() = default;
    ~AnimationBlendingPlugin() override = default;

    const char* GetName() const override { return "Animation-Blending"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override;
    void OnInit() override;
    void OnShutdown() override;
};

} // namespace Eunoia
