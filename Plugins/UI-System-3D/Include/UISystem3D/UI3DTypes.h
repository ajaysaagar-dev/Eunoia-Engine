#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace EunoiaUI {

enum class UI3DSpaceType {
    World = 0,   // Placed in 3D world space, follows actor transform
    Screen = 1   // Billboarded, always faces camera's view
};

inline const char* UI3DSpaceTypeToString(UI3DSpaceType space) {
    switch (space) {
        case UI3DSpaceType::World:  return "World";
        case UI3DSpaceType::Screen: return "Screen";
        default:                    return "World";
    }
}

inline UI3DSpaceType StringToUI3DSpaceType(const std::string& str) {
    if (str == "Screen" || str == "screen" || str == "1" || str == "Billboard") {
        return UI3DSpaceType::Screen;
    }
    return UI3DSpaceType::World;
}

struct UserInterface3DComponent {
    bool enabled = true;
    std::string uiAssetPath = "";                         // UI asset path (.eui3d or .eui)
    UI3DSpaceType space = UI3DSpaceType::World;           // 0 = World, 1 = Screen
    glm::vec2 size{2.0f, 1.125f};                         // 3D quad size in world units (Width, Height)
    glm::vec2 canvasResolution{1920.0f, 1080.0f};         // Virtual 2D canvas pixel resolution
    bool twoSided = true;                                 // Render both front and back in 3D
    float opacity = 1.0f;                                 // Master opacity
    bool interactable = true;                             // Raycast / pointer interactivity
    glm::vec4 backgroundColor{0.0f, 0.0f, 0.0f, 0.0f};   // Canvas always transparent by default
    glm::vec4 borderColor{0.0f, 0.0f, 0.0f, 0.0f};       // Transparent border by default
    float borderWidth = 0.0f;
};

} // namespace EunoiaUI

using UI3DSpaceType = EunoiaUI::UI3DSpaceType;
using UserInterface3DComponent = EunoiaUI::UserInterface3DComponent;
using UserInterface3DComp = EunoiaUI::UserInterface3DComponent;
