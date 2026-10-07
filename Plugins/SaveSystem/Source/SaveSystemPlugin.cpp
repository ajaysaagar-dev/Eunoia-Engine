#include "SaveSystem/ISaveSystemService.h"
#include "SaveSystem/SaveSystem.h"
#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>
#include <iostream>

namespace Eunoia {
namespace SaveSystem {

class SaveSystemServiceImpl : public ISaveSystemService {
public:
    SaveSystemServiceImpl() = default;
    ~SaveSystemServiceImpl() override = default;

    SaveStore& GetGlobalStore() override {
        return SaveSystemClass::Get().Global;
    }

    SaveStore& GetLocalStore() override {
        return SaveSystemClass::Get().Local;
    }

    void OnLevelLoaded(const std::string& levelName) override {
        SaveSystemClass::Get().OnLevelLoaded(levelName);
    }

    void OnLevelUnloaded(const std::string& levelName) override {
        SaveSystemClass::Get().OnLevelUnloaded(levelName);
    }

    bool SaveAll(const std::string& slotName) override {
        return SaveSystemClass::Get().SaveAll(slotName);
    }

    bool LoadAll(const std::string& slotName) override {
        return SaveSystemClass::Get().LoadAll(slotName);
    }
};

} // namespace SaveSystem
} // namespace Eunoia

class SaveSystemPlugin : public IPlugin {
public:
    SaveSystemPlugin() = default;
    ~SaveSystemPlugin() override = default;

    const char* GetName() const override { return "Save-System"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override {
        std::cout << "[SaveSystem] Registering ISaveSystemService...\n";
        registry.Register<Eunoia::SaveSystem::ISaveSystemService>("ISaveSystemService", &m_service);
    }

    void OnInit() override {
        Eunoia::SaveSystem::SaveSystemClass::Get().Initialize();
        std::cout << "[SaveSystem] Plugin initialized.\n";
    }

    void OnShutdown() override {
        Eunoia::SaveSystem::SaveSystemClass::Get().Shutdown();
        std::cout << "[SaveSystem] Plugin shutdown.\n";
    }

private:
    Eunoia::SaveSystem::SaveSystemServiceImpl m_service;
};

// Plugin export factory
extern "C" __declspec(dllexport) IPlugin* CreatePlugin() {
    return new SaveSystemPlugin();
}
