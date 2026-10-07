// ============================================================================
// Eunoia Engine — UI-System Plugin Implementation
// User Interface visual canvas, widgets, drag-and-drop designer, and runtime UI system.
// ============================================================================

#include "UISystem/IUISystem.h"
#include "UISystem/UISystem.h"

#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>
#include <iostream>

class UIPlugin : public IPlugin {
public:
    UIPlugin() = default;
    ~UIPlugin() override = default;

    const char* GetName() const override { return "UI-System"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override {
        std::cout << "[UI-System] Registering IUISystem...\n";
        registry.Register<EunoiaUI::IUISystem>("IUISystem", &EunoiaUI::UISystemManager::Get());
    }

    void OnInit() override {
        std::cout << "[UI-System] Plugin initialized.\n";
    }

    void OnShutdown() override {
        std::cout << "[UI-System] Plugin shutdown.\n";
        EunoiaUI::UISystemManager::Get().RemoveAll();
    }
};
