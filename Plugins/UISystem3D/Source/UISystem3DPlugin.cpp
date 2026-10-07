// ============================================================================
// Eunoia Engine — UI-System-3D Plugin Lifecycle & Service Registration
// ============================================================================

#include "UISystem3D/IUISystem3D.h"
#include "UISystem3D/UISystem3D.h"

#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>
#include <iostream>

class UISystem3DPlugin : public IPlugin {
public:
    UISystem3DPlugin() = default;
    ~UISystem3DPlugin() override = default;

    const char* GetName() const override { return "UI-System-3D"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override {
        std::cout << "[UI-System-3D] Registering IUISystem3D...\n";
        registry.Register<EunoiaUI::IUISystem3D>("IUISystem3D", &EunoiaUI::UISystem3DManager::Get());
    }

    void OnInit() override {
        std::cout << "[UI-System-3D] Plugin initialized.\n";
    }

    void OnShutdown() override {
        std::cout << "[UI-System-3D] Plugin shutdown.\n";
        EunoiaUI::UISystem3DManager::Get().ClearAll();
    }
};
