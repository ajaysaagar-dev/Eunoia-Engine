#pragma once
#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>

namespace Eunoia {

class CharacterMechanicsPlugin : public IPlugin {
public:
    CharacterMechanicsPlugin() = default;
    ~CharacterMechanicsPlugin() override = default;

    const char* GetName() const override { return "Character Mechanics"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override;
    void OnInit() override;
    void OnShutdown() override;
};

} // namespace Eunoia
