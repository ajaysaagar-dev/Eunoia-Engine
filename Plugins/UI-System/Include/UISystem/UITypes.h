#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace EunoiaUI {

enum class UIElementType {
    Border,
    Text,
    Button,
    Image,
    ProgressBar
};

inline const char* UIElementTypeToString(UIElementType type) {
    switch (type) {
        case UIElementType::Border:      return "Border";
        case UIElementType::Text:        return "Text";
        case UIElementType::Button:      return "Button";
        case UIElementType::Image:       return "Image";
        case UIElementType::ProgressBar: return "ProgressBar";
        default:                         return "Unknown";
    }
}

inline UIElementType StringToUIElementType(const std::string& str) {
    if (str == "Border")      return UIElementType::Border;
    if (str == "Text")        return UIElementType::Text;
    if (str == "Button")      return UIElementType::Button;
    if (str == "Image")       return UIElementType::Image;
    if (str == "ProgressBar") return UIElementType::ProgressBar;
    return UIElementType::Border;
}

enum class UIAnchorPreset {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
    Custom
};

inline glm::vec2 AnchorPresetToVec2(UIAnchorPreset preset) {
    switch (preset) {
        case UIAnchorPreset::TopLeft:      return glm::vec2(0.0f, 0.0f);
        case UIAnchorPreset::TopCenter:    return glm::vec2(0.5f, 0.0f);
        case UIAnchorPreset::TopRight:     return glm::vec2(1.0f, 0.0f);
        case UIAnchorPreset::CenterLeft:   return glm::vec2(0.0f, 0.5f);
        case UIAnchorPreset::Center:       return glm::vec2(0.5f, 0.5f);
        case UIAnchorPreset::CenterRight:  return glm::vec2(1.0f, 0.5f);
        case UIAnchorPreset::BottomLeft:   return glm::vec2(0.0f, 1.0f);
        case UIAnchorPreset::BottomCenter: return glm::vec2(0.5f, 1.0f);
        case UIAnchorPreset::BottomRight:  return glm::vec2(1.0f, 1.0f);
        default:                           return glm::vec2(0.0f, 0.0f);
    }
}

struct UIElement {
    std::string id;
    std::string name = "Element";
    UIElementType type = UIElementType::Border;

    // Transform / Layout (Pixels relative to Anchor on Canvas resolution, default 1920x1080)
    glm::vec2 position{ 100.0f, 100.0f };
    glm::vec2 size{ 200.0f, 60.0f };
    glm::vec2 anchor{ 0.0f, 0.0f }; // 0..1 normalized anchor
    glm::vec2 pivot{ 0.0f, 0.0f };  // 0..1 normalized pivot
    int zOrder = 0;
    bool visible = true;

    // Hierarchy
    std::string parentId = "";
    std::vector<std::string> childIds;

    // Border properties
    glm::vec4 backgroundColor{ 0.12f, 0.12f, 0.15f, 0.90f };
    glm::vec4 borderColor{ 0.25f, 0.60f, 1.0f, 1.0f };
    float borderThickness = 2.0f;
    float cornerRounding = 6.0f;
    bool filled = true;

    // Text properties
    std::string text = "New Text";
    float fontSize = 18.0f;
    glm::vec4 textColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    int textAlignment = 0; // 0=Left, 1=Center, 2=Right
    bool wordWrap = false;
    bool shadow = true;
    glm::vec4 shadowColor{ 0.0f, 0.0f, 0.0f, 0.8f };
    glm::vec2 shadowOffset{ 1.5f, 1.5f };

    // Button / Interactive properties (future extension)
    glm::vec4 hoverColor{ 0.20f, 0.22f, 0.28f, 1.0f };
    glm::vec4 pressedColor{ 0.15f, 0.40f, 0.80f, 1.0f };

    // Image properties
    std::string texturePath = "";
};

} // namespace EunoiaUI
