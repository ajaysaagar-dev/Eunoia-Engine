#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>

namespace EunoiaUI {

enum class UIElementType {
    Panel = 0,
    Button = 1,
    Text = 2,
    ProgressBar = 3,
    Image = 4,
    Slider = 5,
    Checkbox = 6,
    Border = 0 // Compatibility alias for Panel
};

inline const char* UIElementTypeToString(UIElementType type) {
    switch (type) {
        case UIElementType::Panel:       return "Panel";
        case UIElementType::Button:      return "Button";
        case UIElementType::Text:        return "Text";
        case UIElementType::ProgressBar: return "ProgressBar";
        case UIElementType::Image:       return "Image";
        case UIElementType::Slider:      return "Slider";
        case UIElementType::Checkbox:    return "Checkbox";
        default:                         return "Panel";
    }
}

inline UIElementType StringToUIElementType(const std::string& str) {
    if (str == "Panel" || str == "Border") return UIElementType::Panel;
    if (str == "Button")                   return UIElementType::Button;
    if (str == "Text")                     return UIElementType::Text;
    if (str == "ProgressBar")              return UIElementType::ProgressBar;
    if (str == "Image")                    return UIElementType::Image;
    if (str == "Slider")                   return UIElementType::Slider;
    if (str == "Checkbox")                 return UIElementType::Checkbox;
    return UIElementType::Panel;
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
        case UIAnchorPreset::StretchHorizontalCenter: return "Stretch Horizontal (Center)";
        case UIAnchorPreset::StretchHorizontalBottom: return "Stretch Horizontal (Bottom)";
        case UIAnchorPreset::StretchVerticalLeft:     return "Stretch Vertical (Left)";
        case UIAnchorPreset::StretchVerticalCenter:   return "Stretch Vertical (Center)";
        case UIAnchorPreset::StretchVerticalRight:    return "Stretch Vertical (Right)";
        case UIAnchorPreset::StretchFull:             return "Stretch Full";
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
        default:
            break;
    }
}

struct UIEventBinding {
    std::string eventName = "OnClick"; // OnClick, OnHover, OnValueChanged, OnToggle
    int targetActorId = -1;
    std::string targetActorName;
    std::string behaviourClass;
    std::string scriptPath;
    std::string targetFunction;
};

struct UIElement {
    std::string id;
    std::string name = "Element";
    UIElementType type = UIElementType::Panel;

    // Responsive Anchors & Transform
    glm::vec2 anchorMin{ 0.0f, 0.0f };
    glm::vec2 anchorMax{ 0.0f, 0.0f };
    glm::vec2 position{ 50.0f, 50.0f }; // Local position relative to anchor
    glm::vec2 size{ 200.0f, 60.0f };    // Width, Height
    glm::vec2 pivot{ 0.0f, 0.0f };      // 0..1 pivot
    UIAnchorPreset anchorPreset = UIAnchorPreset::TopLeft;

    int zOrder = 0;
    bool visible = true;
    bool enabled = true;
    float opacity = 1.0f;

    // Hierarchy
    std::string parentId = "";
    std::vector<std::string> childIds;
    bool isExpanded = true;

    // Atlas Style & Background
    glm::vec4 backgroundColor{ 0.082f, 0.082f, 0.102f, 0.95f }; // Atlas Surface #15151A
    glm::vec4 borderColor{ 1.0f, 1.0f, 1.0f, 0.08f };          // Subtle Atlas outline
    float borderThickness = 1.0f;
    float cornerRounding = 8.0f;
    bool filled = true;

    // Drop Shadow
    bool hasShadow = false;
    glm::vec4 shadowColor{ 0.0f, 0.0f, 0.0f, 0.50f };
    glm::vec2 shadowOffset{ 0.0f, 4.0f };
    float shadowSpread = 8.0f;

    // Interactive Button / Control States
    glm::vec4 hoverColor{ 0.169f, 0.169f, 0.204f, 1.0f };      // Atlas Hover
    glm::vec4 pressedColor{ 0.290f, 0.486f, 1.0f, 1.0f };       // Atlas Accent #4A7CFF
    glm::vec4 disabledColor{ 0.06f, 0.06f, 0.08f, 0.5f };

    // Text Properties
    std::string text = "Text";
    float fontSize = 16.0f;
    glm::vec4 textColor{ 0.929f, 0.933f, 0.953f, 1.0f };        // Atlas Text #EDEEF3
    int textAlignment = 1; // 0=Left, 1=Center, 2=Right
    bool wordWrap = false;
    bool textShadow = false;
    glm::vec4 textShadowColor{ 0.0f, 0.0f, 0.0f, 0.75f };
    glm::vec2 textShadowOffset{ 1.0f, 1.0f };

    // Progress Bar
    float progressValue = 100.0f; // Current value between min and max
    float progressMin = 0.0f;
    float progressMax = 100.0f;
    glm::vec4 progressFillColor{ 0.290f, 0.486f, 1.0f, 1.0f }; // Atlas Blue #4A7CFF
    glm::vec4 progressTrackColor{ 0.125f, 0.125f, 0.153f, 1.0f }; // Atlas Control #202027
    int fillDirection = 0; // 0=LeftToRight, 1=RightToLeft, 2=BottomToTop, 3=TopToBottom
    bool showPercentage = true;

    // Slider
    float sliderValue = 50.0f;
    float sliderMin = 0.0f;
    float sliderMax = 100.0f;
    float sliderStep = 1.0f;
    glm::vec4 sliderTrackColor{ 0.125f, 0.125f, 0.153f, 1.0f };
    glm::vec4 sliderThumbColor{ 0.290f, 0.486f, 1.0f, 1.0f };

    // Checkbox
    bool isChecked = false;
    glm::vec4 checkboxBoxColor{ 0.125f, 0.125f, 0.153f, 1.0f };
    glm::vec4 checkboxCheckColor{ 0.290f, 0.486f, 1.0f, 1.0f };

    // Image / Texture
    std::string texturePath = "";
    glm::vec4 imageTint{ 1.0f, 1.0f, 1.0f, 1.0f };

    // Event Bindings
    std::vector<UIEventBinding> eventBindings;
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
