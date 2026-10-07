#pragma once
// ============================================================================
// Eunoia Engine — Custom UI Framework
// Core types for standalone hardware-accelerated UI architecture.
// Completely native: no ImGui dependencies.
// ============================================================================

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <glm/glm.hpp>

namespace Eunoia { namespace UI {

// Vertex format sent directly to DirectX 12 input assembler
struct CustomUIVertex {
    glm::vec2 position; // Screen space pixels (X, Y)
    glm::vec2 uv;       // Normalized texture coordinates
    glm::vec4 color;    // RGBA tint / vertex color
    glm::vec4 params;   // x: cornerRadius, y: borderThickness, z: mode (0=solid, 1=rounded, 2=textured, 3=glyph, 4=shadow), w: shadowSpread
    glm::vec4 rect;     // Element bounding box (left, top, width, height) in pixels
};

// Anchor modes for responsive UI placement
enum class UIAnchor {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
    StretchHorizontal,
    StretchVertical,
    StretchAll
};

// UI Interaction states
enum class UIWidgetState {
    Normal,
    Hovered,
    Pressed,
    Disabled
};

// Visual styling properties
struct UIStyle {
    glm::vec4 backgroundColor{ 0.12f, 0.13f, 0.16f, 0.95f };
    glm::vec4 hoverColor{ 0.18f, 0.22f, 0.28f, 1.0f };
    glm::vec4 pressedColor{ 0.10f, 0.35f, 0.75f, 1.0f };
    glm::vec4 disabledColor{ 0.08f, 0.09f, 0.10f, 0.5f };
    
    glm::vec4 borderColor{ 0.25f, 0.28f, 0.35f, 1.0f };
    glm::vec4 borderHoverColor{ 0.30f, 0.60f, 1.0f, 1.0f };
    float borderThickness = 1.0f;
    float cornerRadius = 6.0f;
    
    glm::vec4 textColor{ 0.92f, 0.94f, 0.98f, 1.0f };
    float fontSize = 15.0f;
    
    glm::vec4 shadowColor{ 0.0f, 0.0f, 0.0f, 0.50f };
    float shadowSpread = 8.0f;
    glm::vec2 shadowOffset{ 0.0f, 3.0f };
    bool hasShadow = false;
    
    glm::vec4 padding{ 8.0f, 6.0f, 8.0f, 6.0f }; // Left, Top, Right, Bottom
};

// Event structure for mouse and keyboard callbacks
struct UIEvent {
    enum class Type {
        MouseMove,
        MouseDown,
        MouseUp,
        Click,
        Scroll,
        MouseEnter,
        MouseLeave
    } type;
    
    glm::vec2 mousePos{ 0.0f, 0.0f };
    int mouseButton = 0; // 0=Left, 1=Right, 2=Middle
    float scrollDelta = 0.0f;
    bool handled = false;
};

using UIEventCallback = std::function<void(const UIEvent&)>;
using UIClickCallback = std::function<void()>;

}} // namespace Eunoia::UI
