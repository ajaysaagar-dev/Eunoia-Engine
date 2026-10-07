#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
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
    StretchHorizontalTop,
    StretchHorizontalCenter,
    StretchHorizontalBottom,
    StretchVerticalLeft,
    StretchVerticalCenter,
    StretchVerticalRight,
    StretchFull,
    Custom
};

inline const char* UIAnchorPresetToString(UIAnchorPreset preset) {
    switch (preset) {
        case UIAnchorPreset::TopLeft:                 return "Top-Left";
        case UIAnchorPreset::TopCenter:               return "Top-Center";
        case UIAnchorPreset::TopRight:                return "Top-Right";
        case UIAnchorPreset::CenterLeft:              return "Center-Left";
        case UIAnchorPreset::Center:                  return "Center";
        case UIAnchorPreset::CenterRight:             return "Center-Right";
        case UIAnchorPreset::BottomLeft:              return "Bottom-Left";
        case UIAnchorPreset::BottomCenter:            return "Bottom-Center";
        case UIAnchorPreset::BottomRight:             return "Bottom-Right";
        case UIAnchorPreset::StretchHorizontalTop:    return "Stretch Horizontal (Top)";
        case UIAnchorPreset::StretchHorizontalCenter: return "Stretch Horizontal (Middle)";
        case UIAnchorPreset::StretchHorizontalBottom: return "Stretch Horizontal (Bottom)";
        case UIAnchorPreset::StretchVerticalLeft:     return "Stretch Vertical (Left)";
        case UIAnchorPreset::StretchVerticalCenter:   return "Stretch Vertical (Center)";
        case UIAnchorPreset::StretchVerticalRight:    return "Stretch Vertical (Right)";
        case UIAnchorPreset::StretchFull:             return "Stretch Full (Both)";
        case UIAnchorPreset::Custom:                  return "Custom";
        default:                                      return "Top-Left";
    }
}

inline void AnchorPresetToMinMax(UIAnchorPreset preset, glm::vec2& outMin, glm::vec2& outMax, glm::vec2& outPivot) {
    switch (preset) {
        case UIAnchorPreset::TopLeft:
            outMin = { 0.0f, 0.0f }; outMax = { 0.0f, 0.0f }; outPivot = { 0.0f, 0.0f }; break;
        case UIAnchorPreset::TopCenter:
            outMin = { 0.5f, 0.0f }; outMax = { 0.5f, 0.0f }; outPivot = { 0.5f, 0.0f }; break;
        case UIAnchorPreset::TopRight:
            outMin = { 1.0f, 0.0f }; outMax = { 1.0f, 0.0f }; outPivot = { 1.0f, 0.0f }; break;
        case UIAnchorPreset::CenterLeft:
            outMin = { 0.0f, 0.5f }; outMax = { 0.0f, 0.5f }; outPivot = { 0.0f, 0.5f }; break;
        case UIAnchorPreset::Center:
            outMin = { 0.5f, 0.5f }; outMax = { 0.5f, 0.5f }; outPivot = { 0.5f, 0.5f }; break;
        case UIAnchorPreset::CenterRight:
            outMin = { 1.0f, 0.5f }; outMax = { 1.0f, 0.5f }; outPivot = { 1.0f, 0.5f }; break;
        case UIAnchorPreset::BottomLeft:
            outMin = { 0.0f, 1.0f }; outMax = { 0.0f, 1.0f }; outPivot = { 0.0f, 1.0f }; break;
        case UIAnchorPreset::BottomCenter:
            outMin = { 0.5f, 1.0f }; outMax = { 0.5f, 1.0f }; outPivot = { 0.5f, 1.0f }; break;
        case UIAnchorPreset::BottomRight:
            outMin = { 1.0f, 1.0f }; outMax = { 1.0f, 1.0f }; outPivot = { 1.0f, 1.0f }; break;
        case UIAnchorPreset::StretchHorizontalTop:
            outMin = { 0.0f, 0.0f }; outMax = { 1.0f, 0.0f }; outPivot = { 0.5f, 0.0f }; break;
        case UIAnchorPreset::StretchHorizontalCenter:
            outMin = { 0.0f, 0.5f }; outMax = { 1.0f, 0.5f }; outPivot = { 0.5f, 0.5f }; break;
        case UIAnchorPreset::StretchHorizontalBottom:
            outMin = { 0.0f, 1.0f }; outMax = { 1.0f, 1.0f }; outPivot = { 0.5f, 1.0f }; break;
        case UIAnchorPreset::StretchVerticalLeft:
            outMin = { 0.0f, 0.0f }; outMax = { 0.0f, 1.0f }; outPivot = { 0.0f, 0.5f }; break;
        case UIAnchorPreset::StretchVerticalCenter:
            outMin = { 0.5f, 0.0f }; outMax = { 0.5f, 1.0f }; outPivot = { 0.5f, 0.5f }; break;
        case UIAnchorPreset::StretchVerticalRight:
            outMin = { 1.0f, 0.0f }; outMax = { 1.0f, 1.0f }; outPivot = { 1.0f, 0.5f }; break;
        case UIAnchorPreset::StretchFull:
            outMin = { 0.0f, 0.0f }; outMax = { 1.0f, 1.0f }; outPivot = { 0.5f, 0.5f }; break;
        case UIAnchorPreset::Custom:
        default:
            break;
    }
}

inline UIAnchorPreset AnchorMinMaxToPreset(const glm::vec2& min, const glm::vec2& max) {
    auto match = [](float a, float b) { return std::abs(a - b) < 0.01f; };
    if (match(min.x, 0.0f) && match(min.y, 0.0f) && match(max.x, 0.0f) && match(max.y, 0.0f)) return UIAnchorPreset::TopLeft;
    if (match(min.x, 0.5f) && match(min.y, 0.0f) && match(max.x, 0.5f) && match(max.y, 0.0f)) return UIAnchorPreset::TopCenter;
    if (match(min.x, 1.0f) && match(min.y, 0.0f) && match(max.x, 1.0f) && match(max.y, 0.0f)) return UIAnchorPreset::TopRight;
    if (match(min.x, 0.0f) && match(min.y, 0.5f) && match(max.x, 0.0f) && match(max.y, 0.5f)) return UIAnchorPreset::CenterLeft;
    if (match(min.x, 0.5f) && match(min.y, 0.5f) && match(max.x, 0.5f) && match(max.y, 0.5f)) return UIAnchorPreset::Center;
    if (match(min.x, 1.0f) && match(min.y, 0.5f) && match(max.x, 1.0f) && match(max.y, 0.5f)) return UIAnchorPreset::CenterRight;
    if (match(min.x, 0.0f) && match(min.y, 1.0f) && match(max.x, 0.0f) && match(max.y, 1.0f)) return UIAnchorPreset::BottomLeft;
    if (match(min.x, 0.5f) && match(min.y, 1.0f) && match(max.x, 0.5f) && match(max.y, 1.0f)) return UIAnchorPreset::BottomCenter;
    if (match(min.x, 1.0f) && match(min.y, 1.0f) && match(max.x, 1.0f) && match(max.y, 1.0f)) return UIAnchorPreset::BottomRight;
    if (match(min.x, 0.0f) && match(min.y, 0.0f) && match(max.x, 1.0f) && match(max.y, 0.0f)) return UIAnchorPreset::StretchHorizontalTop;
    if (match(min.x, 0.0f) && match(min.y, 0.5f) && match(max.x, 1.0f) && match(max.y, 0.5f)) return UIAnchorPreset::StretchHorizontalCenter;
    if (match(min.x, 0.0f) && match(min.y, 1.0f) && match(max.x, 1.0f) && match(max.y, 1.0f)) return UIAnchorPreset::StretchHorizontalBottom;
    if (match(min.x, 0.0f) && match(min.y, 0.0f) && match(max.x, 0.0f) && match(max.y, 1.0f)) return UIAnchorPreset::StretchVerticalLeft;
    if (match(min.x, 0.5f) && match(min.y, 0.0f) && match(max.x, 0.5f) && match(max.y, 1.0f)) return UIAnchorPreset::StretchVerticalCenter;
    if (match(min.x, 1.0f) && match(min.y, 0.0f) && match(max.x, 1.0f) && match(max.y, 1.0f)) return UIAnchorPreset::StretchVerticalRight;
    if (match(min.x, 0.0f) && match(min.y, 0.0f) && match(max.x, 1.0f) && match(max.y, 1.0f)) return UIAnchorPreset::StretchFull;
    return UIAnchorPreset::Custom;
}

struct UIElement {
    std::string id;
    std::string name = "Element";
    UIElementType type = UIElementType::Border;

    // Anchors (0..1 normalized range)
    glm::vec2 anchorMin{ 0.0f, 0.0f };
    glm::vec2 anchorMax{ 0.0f, 0.0f };
    glm::vec2 position{ 50.0f, 50.0f }; // Local position relative to anchor (or left/top offset in stretch)
    glm::vec2 size{ 200.0f, 60.0f };    // Size (width/height) or right/bottom margin offset in stretch
    glm::vec2 pivot{ 0.0f, 0.0f };      // 0..1 pivot point
    UIAnchorPreset anchorPreset = UIAnchorPreset::TopLeft;

    int zOrder = 0;
    bool visible = true;

    // Hierarchy
    std::string parentId = "";
    std::vector<std::string> childIds;
    bool isExpanded = true;

    // Border properties
    glm::vec4 backgroundColor{ 0.10f, 0.12f, 0.16f, 0.90f };
    glm::vec4 borderColor{ 0.25f, 0.60f, 1.0f, 1.0f };
    float borderThickness = 2.0f;
    float cornerRounding = 6.0f;
    bool filled = true;

    // Text properties
    std::string text = "New Text";
    float fontSize = 20.0f;
    glm::vec4 textColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    int textAlignment = 0; // 0=Left, 1=Center, 2=Right
    bool wordWrap = false;
    bool shadow = true;
    glm::vec4 shadowColor{ 0.0f, 0.0f, 0.0f, 0.85f };
    glm::vec2 shadowOffset{ 1.5f, 1.5f };

    // Interactive / button properties (future extensions)
    glm::vec4 hoverColor{ 0.20f, 0.22f, 0.28f, 1.0f };
    glm::vec4 pressedColor{ 0.15f, 0.40f, 0.80f, 1.0f };
    std::string texturePath = "";
};

// Computes local bounding rectangle (X, Y, Width, Height) inside parent rect
inline glm::vec4 ComputeElementRect(const glm::vec4& parentRect, const UIElement& el) {
    float pX = parentRect.x;
    float pY = parentRect.y;
    float pW = std::max(1.0f, parentRect.z);
    float pH = std::max(1.0f, parentRect.w);

    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    // Horizontal Layout
    if (std::abs(el.anchorMin.x - el.anchorMax.x) < 0.001f) {
        float anchorX = pX + el.anchorMin.x * pW;
        left = anchorX + el.position.x - el.pivot.x * el.size.x;
        width = el.size.x;
    } else {
        float anchorLeft = pX + el.anchorMin.x * pW;
        float anchorRight = pX + el.anchorMax.x * pW;
        left = anchorLeft + el.position.x;
        float right = anchorRight - el.size.x;
        width = std::max(1.0f, right - left);
    }

    // Vertical Layout
    if (std::abs(el.anchorMin.y - el.anchorMax.y) < 0.001f) {
        float anchorY = pY + el.anchorMin.y * pH;
        top = anchorY + el.position.y - el.pivot.y * el.size.y;
        height = el.size.y;
    } else {
        float anchorTop = pY + el.anchorMin.y * pH;
        float anchorBottom = pY + el.anchorMax.y * pH;
        top = anchorTop + el.position.y;
        float bottom = anchorBottom - el.size.y;
        height = std::max(1.0f, bottom - top);
    }

    return glm::vec4(left, top, width, height);
}

// -------------------------------------------------------------
// 3D Space User Interface Components
// -------------------------------------------------------------
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

