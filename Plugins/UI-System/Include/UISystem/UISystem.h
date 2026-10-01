#pragma once

#include "UITypes.h"
#include "UserInterface.h"
#include "IUISystem.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <algorithm>

struct ImDrawList;
struct ImVec2;

namespace EunoiaUI {

class UISystemManager : public IUISystem {
public:
    static UISystemManager& Get();

    // Script / Gameplay APIs
    void Add(UserInterface* ui) override;
    void Remove(UserInterface* ui) override;
    void RemoveAll() override;
    bool IsActive(UserInterface* ui) const override;

    // Static shorthand wrappers
    static void AddUI(UserInterface* ui) { Get().Add(ui); }
    static void RemoveUI(UserInterface* ui) { Get().Remove(ui); }
    static void ClearAll() { Get().RemoveAll(); }

    // Asset Management
    UserInterface* GetOrLoadUI(const std::string& path);
    UserInterface* CreateNewUI(const std::string& name, const std::string& path);
    const std::unordered_map<std::string, std::shared_ptr<UserInterface>>& GetLoadedUIs() const { return m_loadedUIs; }

    // Active UIs list
    const std::vector<UserInterface*>& GetActiveUIs() const { return m_activeUIs; }

    // Runtime Viewport Rendering
    void RenderActiveUIs(const glm::vec2& viewportPos, const glm::vec2& viewportSize);
    void RenderUI(void* imDrawListPtr, const glm::vec2& screenMin, const glm::vec2& screenMax, const UserInterface& ui);

private:
    UISystemManager() = default;
    ~UISystemManager() override = default;

    std::vector<UserInterface*> m_activeUIs;
    std::unordered_map<std::string, std::shared_ptr<UserInterface>> m_loadedUIs;
};

// Global accessor proxy struct allowing:
// UISystem.Add(UI);
// UISystem.Remove(UI);
// UISystem.RemoveAll();
// UISystem::Add(UI);
// UISystem::Remove(UI);
// UISystem::RemoveAll();
struct UISystemProxy {
    static void Add(UserInterface* ui) {
        UISystemManager::Get().Add(ui);
    }
    static void Remove(UserInterface* ui) {
        UISystemManager::Get().Remove(ui);
    }
    static void RemoveAll() {
        UISystemManager::Get().RemoveAll();
    }
    static bool IsActive(UserInterface* ui) {
        return UISystemManager::Get().IsActive(ui);
    }
    static UISystemManager& Get() {
        return UISystemManager::Get();
    }
    static UserInterface* GetOrLoadUI(const std::string& path) {
        return UISystemManager::Get().GetOrLoadUI(path);
    }
    static UserInterface* CreateNewUI(const std::string& name, const std::string& path) {
        return UISystemManager::Get().CreateNewUI(name, path);
    }
};

} // namespace EunoiaUI

// Global instance variable & alias so scripts can write: UISystem.Add(UI); UISystem.RemoveAll(); UISystem::Add(UI);
inline EunoiaUI::UISystemProxy UISystem;
inline EunoiaUI::UISystemProxy uiSystem;
using UISystemProxyType = EunoiaUI::UISystemProxy;
using UISystemService = EunoiaUI::UISystemManager;
