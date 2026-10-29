#pragma once

#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>

namespace Eunoia {

class LevelAnimationPlugin : public IPlugin {
public:
    LevelAnimationPlugin() = default;
    ~LevelAnimationPlugin() override = default;

    const char* GetName() const override { return "Level-Animation"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override;
    void OnInit() override;
    void OnShutdown() override;
};

} // namespace Eunoia
