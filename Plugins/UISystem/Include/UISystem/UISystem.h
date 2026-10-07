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

    // Scene Context & Event Dispatching
    void SetCurrentScene(void* scenePtr) { m_currentScene = scenePtr; }
    void* GetCurrentScene() const { return m_currentScene; }
    void DispatchEvent(const UIEventBinding& binding);

    // Runtime Viewport Rendering
    void RenderActiveUIs(const glm::vec2& viewportPos, const glm::vec2& viewportSize);
    void RenderUI(void* imDrawListPtr, const glm::vec2& screenMin, const glm::vec2& screenMax, const UserInterface& ui);

private:
    UISystemManager() = default;
    ~UISystemManager() override = default;

    void* m_currentScene = nullptr;
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

// Global instance variable & alias so scripts can write: UISystem.Add(UI); UISystem.RemoveAll(); UISystem::Add(UI);
inline EunoiaUI::UISystemProxy UISystem;
inline EunoiaUI::UISystemProxy uiSystem;
using UISystemProxyType = EunoiaUI::UISystemProxy;
using UISystemService = EunoiaUI::UISystemManager;

namespace EunoiaUI {
class UISystem3DManager {
public:
    static UISystem3DManager& Get() {
        static UISystem3DManager inst;
        return inst;
    }
    void ClearAll() {}
    UserInterface* CreateNewUI3D(const std::string& name, const std::string& path) {
        return UISystemManager::Get().CreateNewUI(name, path);
    }
    void RenderAllScene3DUI(void* /*imDrawListPtr*/, void* /*scene*/, const void* /*camera*/,
                            const glm::vec4& /*vpRect*/, bool /*isGameView*/) {}
    template<typename TScene, typename TCamera>
    void RenderAllScene3DUI(void* /*imDrawListPtr*/, TScene& /*scene*/, const TCamera& /*camera*/,
                            const glm::vec4& /*vpRect*/, bool /*isGameView*/) {}
};
}

