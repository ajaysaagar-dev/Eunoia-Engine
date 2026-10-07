#pragma once

#include "UI3DTypes.h"
#include "IUISystem3D.h"
#include "UserInterface3D.h"
#include <UISystem/UserInterface.h>
#include <UISystem/UISystem.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <glm/glm.hpp>

class GameObject;
class Scene;
class OrbitCamera;
struct ImDrawList;

namespace EunoiaUI {

class UISystem3DManager : public IUISystem3D {
public:
    static UISystem3DManager& Get();

    void ClearAll() override;

    // Asset Management
    UserInterface* GetOrLoadUIAsset(const std::string& path);
    std::shared_ptr<UserInterface3D> GetOrLoad3DUI(const std::string& path);
    UserInterface* CreateNewUI3D(const std::string& name, const std::string& path);

    // 3D Space Viewport Rendering
    void Render3DUI(void* imDrawListPtr, GameObject& obj, const OrbitCamera& camera,
                    const glm::vec4& vpRect, bool isGameView, bool isSelected = false, Scene* scene = nullptr);

    void RenderAllScene3DUI(void* imDrawListPtr, Scene& scene, const OrbitCamera& camera,
                            const glm::vec4& vpRect, bool isGameView);

private:
    UISystem3DManager() = default;
    ~UISystem3DManager() override = default;

    std::unordered_map<std::string, std::shared_ptr<UserInterface3D>> m_loaded3DUIs;
};

// Global accessor proxy struct allowing:
// UISystem3D.ClearAll();
// UISystem3D::Get();
struct UISystem3DProxy {
    static UISystem3DManager& Get() {
        return UISystem3DManager::Get();
    }
    static void ClearAll() {
        UISystem3DManager::Get().ClearAll();
    }
    static UserInterface* GetOrLoadUIAsset(const std::string& path) {
        return UISystem3DManager::Get().GetOrLoadUIAsset(path);
    }
    static UserInterface* CreateNewUI3D(const std::string& name, const std::string& path) {
        return UISystem3DManager::Get().CreateNewUI3D(name, path);
    }
};

} // namespace EunoiaUI

// Global instance variable & aliases
inline EunoiaUI::UISystem3DProxy UISystem3D;
inline EunoiaUI::UISystem3DProxy uiSystem3D;
using UISystem3DProxyType = EunoiaUI::UISystem3DProxy;
using UISystem3DService = EunoiaUI::UISystem3DManager;
