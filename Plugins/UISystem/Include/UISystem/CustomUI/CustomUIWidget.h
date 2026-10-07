#pragma once
// ============================================================================
// Eunoia Engine — Custom UI Widgets
// Pure native widget hierarchy: Canvas, Panel, Button, Label, TopMenuBar, Menus.
// ============================================================================

#include "CustomUITypes.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace Eunoia { namespace UI {

class CustomUIWidget;

// Base widget in hierarchy
class CustomUIWidget : public std::enable_shared_from_this<CustomUIWidget> {
public:
    CustomUIWidget(const std::string& name = "Widget");
    virtual ~CustomUIWidget() = default;

    // Hierarchy
    void AddChild(std::shared_ptr<CustomUIWidget> child);
    void RemoveChild(std::shared_ptr<CustomUIWidget> child);
    void ClearChildren();
    const std::vector<std::shared_ptr<CustomUIWidget>>& GetChildren() const { return m_children; }
    CustomUIWidget* GetParent() const { return m_parent; }

    // Transform & Layout (Pixels or Anchors)
    void SetPosition(const glm::vec2& pos) { m_position = pos; m_dirty = true; }
    void SetSize(const glm::vec2& size) { m_size = size; m_dirty = true; }
    void SetAnchor(UIAnchor anchor) { m_anchor = anchor; m_dirty = true; }
    void SetPivot(const glm::vec2& pivot) { m_pivot = pivot; m_dirty = true; }
    
    glm::vec2 GetPosition() const { return m_position; }
    glm::vec2 GetSize() const { return m_size; }
    UIAnchor GetAnchor() const { return m_anchor; }
    glm::vec4 GetScreenRect() const { return m_screenRect; }

    // Appearance & State
    void SetVisible(bool v) { m_visible = v; }
    bool IsVisible() const { return m_visible; }
    void SetEnabled(bool e) { m_enabled = e; }
    bool IsEnabled() const { return m_enabled; }
    void SetStyle(const UIStyle& style) { m_style = style; }
    UIStyle& GetStyle() { return m_style; }
    const UIStyle& GetStyle() const { return m_style; }
    UIWidgetState GetState() const { return m_state; }

    // Layout computation & Rendering
    virtual void UpdateLayout(const glm::vec4& parentRect);
    virtual void Render();

    // Event Handling
    virtual bool HandleEvent(const UIEvent& event);
    void SetOnEventCallback(UIEventCallback cb) { m_onEvent = cb; }

protected:
    std::string m_name;
    CustomUIWidget* m_parent = nullptr;
    std::vector<std::shared_ptr<CustomUIWidget>> m_children;

    glm::vec2 m_position{ 0.0f, 0.0f };
    glm::vec2 m_size{ 100.0f, 40.0f };
    glm::vec2 m_pivot{ 0.0f, 0.0f };
    UIAnchor m_anchor = UIAnchor::TopLeft;

    glm::vec4 m_screenRect{ 0.0f, 0.0f, 100.0f, 40.0f };
    bool m_dirty = true;

    bool m_visible = true;
    bool m_enabled = true;
    UIWidgetState m_state = UIWidgetState::Normal;
    UIStyle m_style;
    UIEventCallback m_onEvent;
};

// Canvas: Root container for UI trees
class CustomUICanvas : public CustomUIWidget {
public:
    CustomUICanvas(const std::string& name = "Canvas");
    void SetReferenceResolution(const glm::vec2& res) { m_referenceResolution = res; }
    void UpdateCanvas(uint32_t screenWidth, uint32_t screenHeight);

private:
    glm::vec2 m_referenceResolution{ 1920.0f, 1080.0f };
};

// Panel: Container with background, border, and drop shadow
class CustomUIPanel : public CustomUIWidget {
public:
    CustomUIPanel(const std::string& name = "Panel");
    void Render() override;
};

// Button: Interactive clickable button
class CustomUIButton : public CustomUIWidget {
public:
    CustomUIButton(const std::string& text = "Button", const std::string& name = "Button");
    void SetText(const std::string& text) { m_text = text; }
    const std::string& GetText() const { return m_text; }
    void SetOnClick(UIClickCallback cb) { m_onClick = cb; }

    void Render() override;
    bool HandleEvent(const UIEvent& event) override;

private:
    std::string m_text;
    UIClickCallback m_onClick;
};

// Label: Text display
class CustomUILabel : public CustomUIWidget {
public:
    CustomUILabel(const std::string& text = "Label", const std::string& name = "Label");
    void SetText(const std::string& text) { m_text = text; }
    const std::string& GetText() const { return m_text; }
    void SetFontSize(float size) { m_style.fontSize = size; }
    void SetTextColor(const glm::vec4& col) { m_style.textColor = col; }

    void Render() override;

private:
    std::string m_text;
};

// Menu Item in TopMenuBar or Dropdowns
struct CustomUIMenuItemData {
    std::string label;
    std::string shortcut;
    bool checked = false;
    bool enabled = true;
    bool isSeparator = false;
    UIClickCallback onClick;
};

// Dropdown Menu
class CustomUIMenu : public CustomUIWidget {
public:
    CustomUIMenu(const std::string& title = "Menu");
    void AddItem(const std::string& label, const std::string& shortcut = "", UIClickCallback onClick = nullptr, bool checked = false);
    void AddSeparator();
    const std::string& GetTitle() const { return m_title; }
    bool IsOpen() const { return m_open; }
    void SetOpen(bool open) { m_open = open; }

    void Render() override;
    bool HandleEvent(const UIEvent& event) override;

    const std::vector<CustomUIMenuItemData>& GetItems() const { return m_items; }

private:
    std::string m_title;
    bool m_open = false;
    int m_hoveredIndex = -1;
    std::vector<CustomUIMenuItemData> m_items;
};

// Top Menu Bar
class CustomUITopMenuBar : public CustomUIWidget {
public:
    CustomUITopMenuBar(const std::string& name = "TopMenuBar");
    std::shared_ptr<CustomUIMenu> AddMenu(const std::string& title);
    
    void Render() override;
    bool HandleEvent(const UIEvent& event) override;

private:
    std::vector<std::shared_ptr<CustomUIMenu>> m_menus;
    int m_activeMenuIndex = -1;
};

// Slider: Value adjustment
class CustomUISlider : public CustomUIWidget {
public:
    CustomUISlider(float minVal = 0.0f, float maxVal = 1.0f, float curVal = 0.5f, const std::string& name = "Slider");
    void SetRange(float minVal, float maxVal) { m_min = minVal; m_max = maxVal; }
    void SetValue(float val);
    float GetValue() const { return m_currentValue; }
    void SetOnValueChanged(std::function<void(float)> cb) { m_onValueChanged = cb; }

    void Render() override;
    bool HandleEvent(const UIEvent& event) override;

private:
    float m_min = 0.0f;
    float m_max = 1.0f;
    float m_currentValue = 0.5f;
    bool m_dragging = false;
    std::function<void(float)> m_onValueChanged;
};

}} // namespace Eunoia::UI
