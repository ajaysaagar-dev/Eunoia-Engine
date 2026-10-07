// ============================================================================
// Eunoia Engine — Custom UI Widgets Implementation
// ============================================================================

#include "UISystem/CustomUI/CustomUIWidget.h"
#include "UISystem/CustomUI/CustomUIRenderer.h"
#include <algorithm>

namespace Eunoia { namespace UI {

CustomUIWidget::CustomUIWidget(const std::string& name) : m_name(name) {}

void CustomUIWidget::AddChild(std::shared_ptr<CustomUIWidget> child) {
    if (!child) return;
    child->m_parent = this;
    m_children.push_back(child);
    m_dirty = true;
}

void CustomUIWidget::RemoveChild(std::shared_ptr<CustomUIWidget> child) {
    if (!child) return;
    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) {
        child->m_parent = nullptr;
        m_children.erase(it);
        m_dirty = true;
    }
}

void CustomUIWidget::ClearChildren() {
    for (auto& child : m_children) {
        if (child) child->m_parent = nullptr;
    }
    m_children.clear();
    m_dirty = true;
}

void CustomUIWidget::UpdateLayout(const glm::vec4& parentRect) {
    float pX = parentRect.x;
    float pY = parentRect.y;
    float pW = parentRect.z;
    float pH = parentRect.w;

    float anchorX = pX;
    float anchorY = pY;

    switch (m_anchor) {
        case UIAnchor::TopLeft:
            anchorX = pX; anchorY = pY; break;
        case UIAnchor::TopCenter:
            anchorX = pX + pW * 0.5f; anchorY = pY; break;
        case UIAnchor::TopRight:
            anchorX = pX + pW; anchorY = pY; break;
        case UIAnchor::CenterLeft:
            anchorX = pX; anchorY = pY + pH * 0.5f; break;
        case UIAnchor::Center:
            anchorX = pX + pW * 0.5f; anchorY = pY + pH * 0.5f; break;
        case UIAnchor::CenterRight:
            anchorX = pX + pW; anchorY = pY + pH * 0.5f; break;
        case UIAnchor::BottomLeft:
            anchorX = pX; anchorY = pY + pH; break;
        case UIAnchor::BottomCenter:
            anchorX = pX + pW * 0.5f; anchorY = pY + pH; break;
        case UIAnchor::BottomRight:
            anchorX = pX + pW; anchorY = pY + pH; break;
        case UIAnchor::StretchHorizontal:
            m_screenRect = glm::vec4(pX + m_position.x, pY + m_position.y, pW - m_size.x, m_size.y);
            for (auto& child : m_children) if (child && child->IsVisible()) child->UpdateLayout(m_screenRect);
            return;
        case UIAnchor::StretchAll:
            m_screenRect = glm::vec4(pX + m_position.x, pY + m_position.y, pW - m_size.x, pH - m_size.y);
            for (auto& child : m_children) if (child && child->IsVisible()) child->UpdateLayout(m_screenRect);
            return;
        default:
            break;
    }

    float finalX = anchorX + m_position.x - m_pivot.x * m_size.x;
    float finalY = anchorY + m_position.y - m_pivot.y * m_size.y;
    m_screenRect = glm::vec4(finalX, finalY, m_size.x, m_size.y);

    for (auto& child : m_children) {
        if (child && child->IsVisible()) {
            child->UpdateLayout(m_screenRect);
        }
    }
}

void CustomUIWidget::Render() {
    if (!m_visible) return;
    for (auto& child : m_children) {
        if (child && child->IsVisible()) {
            child->Render();
        }
    }
}

bool CustomUIWidget::HandleEvent(const UIEvent& event) {
    if (!m_visible || !m_enabled) return false;

    // Dispatch to children first (top-most in reverse order)
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it) {
        if (*it && (*it)->IsVisible() && (*it)->IsEnabled()) {
            if ((*it)->HandleEvent(event)) return true;
        }
    }

    bool inside = (event.mousePos.x >= m_screenRect.x && event.mousePos.x <= m_screenRect.x + m_screenRect.z &&
                   event.mousePos.y >= m_screenRect.y && event.mousePos.y <= m_screenRect.y + m_screenRect.w);

    if (inside && m_onEvent) {
        m_onEvent(event);
        return true;
    }

    return false;
}

// ----------------------------------------------------------------------------
// Canvas Implementation
// ----------------------------------------------------------------------------
CustomUICanvas::CustomUICanvas(const std::string& name) : CustomUIWidget(name) {
    m_anchor = UIAnchor::StretchAll;
}

void CustomUICanvas::UpdateCanvas(uint32_t screenWidth, uint32_t screenHeight) {
    glm::vec4 screenRect(0.0f, 0.0f, (float)screenWidth, (float)screenHeight);
    UpdateLayout(screenRect);
}

// ----------------------------------------------------------------------------
// Panel Implementation
// ----------------------------------------------------------------------------
CustomUIPanel::CustomUIPanel(const std::string& name) : CustomUIWidget(name) {}

void CustomUIPanel::Render() {
    if (!m_visible) return;
    auto& renderer = CustomUIRenderer::Get();

    // 1. Drop Shadow
    if (m_style.hasShadow) {
        renderer.DrawShadow(m_screenRect, m_style.shadowColor, m_style.cornerRadius, m_style.shadowSpread, m_style.shadowOffset);
    }

    // 2. Rounded Background & Border
    renderer.DrawRoundedRect(m_screenRect, m_style.backgroundColor, m_style.cornerRadius, m_style.borderThickness, m_style.borderColor);

    CustomUIWidget::Render();
}

// ----------------------------------------------------------------------------
// Button Implementation
// ----------------------------------------------------------------------------
CustomUIButton::CustomUIButton(const std::string& text, const std::string& name)
    : CustomUIWidget(name), m_text(text) {
    m_size = glm::vec2(120.0f, 32.0f);
    m_style.cornerRadius = 4.0f;
    m_style.borderThickness = 1.0f;
}

void CustomUIButton::Render() {
    if (!m_visible) return;
    auto& renderer = CustomUIRenderer::Get();

    glm::vec4 bgColor = m_style.backgroundColor;
    glm::vec4 bdColor = m_style.borderColor;

    if (m_state == UIWidgetState::Pressed) {
        bgColor = m_style.pressedColor;
        bdColor = m_style.borderHoverColor;
    } else if (m_state == UIWidgetState::Hovered) {
        bgColor = m_style.hoverColor;
        bdColor = m_style.borderHoverColor;
    }

    renderer.DrawRoundedRect(m_screenRect, bgColor, m_style.cornerRadius, m_style.borderThickness, bdColor);
    renderer.DrawTextCentered(m_text, m_screenRect, m_style.fontSize, m_style.textColor);

    CustomUIWidget::Render();
}

bool CustomUIButton::HandleEvent(const UIEvent& event) {
    if (!m_visible || !m_enabled) return false;

    bool inside = (event.mousePos.x >= m_screenRect.x && event.mousePos.x <= m_screenRect.x + m_screenRect.z &&
                   event.mousePos.y >= m_screenRect.y && event.mousePos.y <= m_screenRect.y + m_screenRect.w);

    if (event.type == UIEvent::Type::MouseMove) {
        if (inside && m_state != UIWidgetState::Pressed) {
            m_state = UIWidgetState::Hovered;
        } else if (!inside && m_state == UIWidgetState::Hovered) {
            m_state = UIWidgetState::Normal;
        }
    } else if (event.type == UIEvent::Type::MouseDown && inside) {
        m_state = UIWidgetState::Pressed;
        return true;
    } else if (event.type == UIEvent::Type::MouseUp) {
        if (m_state == UIWidgetState::Pressed && inside) {
            m_state = UIWidgetState::Hovered;
            if (m_onClick) m_onClick();
            return true;
        }
        m_state = inside ? UIWidgetState::Hovered : UIWidgetState::Normal;
    }

    return inside;
}

// ----------------------------------------------------------------------------
// Label Implementation
// ----------------------------------------------------------------------------
CustomUILabel::CustomUILabel(const std::string& text, const std::string& name)
    : CustomUIWidget(name), m_text(text) {
    m_size = glm::vec2(100.0f, 24.0f);
}

void CustomUILabel::Render() {
    if (!m_visible || m_text.empty()) return;
    auto& renderer = CustomUIRenderer::Get();
    renderer.DrawTextString(m_text, glm::vec2(m_screenRect.x, m_screenRect.y), m_style.fontSize, m_style.textColor);
    CustomUIWidget::Render();
}

// ----------------------------------------------------------------------------
// Menu Implementation
// ----------------------------------------------------------------------------
CustomUIMenu::CustomUIMenu(const std::string& title)
    : CustomUIWidget(title), m_title(title) {
    m_size = glm::vec2(80.0f, 26.0f);
}

void CustomUIMenu::AddItem(const std::string& label, const std::string& shortcut, UIClickCallback onClick, bool checked) {
    CustomUIMenuItemData item;
    item.label = label;
    item.shortcut = shortcut;
    item.onClick = onClick;
    item.checked = checked;
    item.isSeparator = false;
    m_items.push_back(item);
}

void CustomUIMenu::AddSeparator() {
    CustomUIMenuItemData item;
    item.isSeparator = true;
    m_items.push_back(item);
}

void CustomUIMenu::Render() {
    if (!m_visible) return;
    auto& renderer = CustomUIRenderer::Get();

    // Render menu header button
    glm::vec4 bgCol = (m_open || m_state == UIWidgetState::Hovered) ?
        glm::vec4(0.20f, 0.22f, 0.28f, 1.0f) : glm::vec4(0.0f);
    renderer.DrawRoundedRect(m_screenRect, bgCol, 3.0f);
    renderer.DrawTextCentered(m_title, m_screenRect, 14.0f, glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));

    // Render dropdown overlay if open
    if (m_open && !m_items.empty()) {
        float itemH = 26.0f;
        float dropW = 220.0f;
        float dropH = 8.0f;
        for (const auto& it : m_items) {
            dropH += it.isSeparator ? 8.0f : itemH;
        }

        glm::vec4 dropRect(m_screenRect.x, m_screenRect.y + m_screenRect.w, dropW, dropH);
        renderer.DrawShadow(dropRect, glm::vec4(0.0f, 0.0f, 0.0f, 0.6f), 4.0f, 10.0f);
        renderer.DrawRoundedRect(dropRect, glm::vec4(0.13f, 0.14f, 0.18f, 0.98f), 4.0f, 1.0f, glm::vec4(0.25f, 0.28f, 0.35f, 1.0f));

        float curY = dropRect.y + 4.0f;
        for (size_t i = 0; i < m_items.size(); ++i) {
            const auto& item = m_items[i];
            if (item.isSeparator) {
                renderer.DrawSolidRect(glm::vec4(dropRect.x + 6.0f, curY + 3.0f, dropW - 12.0f, 1.0f), glm::vec4(0.25f, 0.27f, 0.33f, 1.0f));
                curY += 8.0f;
                continue;
            }

            glm::vec4 iRect(dropRect.x + 4.0f, curY, dropW - 8.0f, itemH);
            if ((int)i == m_hoveredIndex) {
                renderer.DrawRoundedRect(iRect, glm::vec4(0.14f, 0.45f, 0.85f, 0.85f), 3.0f);
            }

            std::string labelText = (item.checked ? "[x] " : "    ") + item.label;
            renderer.DrawTextString(labelText, glm::vec2(iRect.x + 4.0f, iRect.y + 4.0f), 13.0f, glm::vec4(0.95f, 0.95f, 0.95f, 1.0f));

            if (!item.shortcut.empty()) {
                glm::vec2 scSize = renderer.MeasureText(item.shortcut, 12.0f);
                renderer.DrawTextString(item.shortcut, glm::vec2(iRect.x + iRect.z - scSize.x - 6.0f, iRect.y + 5.0f), 12.0f, glm::vec4(0.6f, 0.65f, 0.75f, 1.0f));
            }

            curY += itemH;
        }
    }
}

bool CustomUIMenu::HandleEvent(const UIEvent& event) {
    if (!m_visible || !m_enabled) return false;

    bool inHeader = (event.mousePos.x >= m_screenRect.x && event.mousePos.x <= m_screenRect.x + m_screenRect.z &&
                     event.mousePos.y >= m_screenRect.y && event.mousePos.y <= m_screenRect.y + m_screenRect.w);

    if (event.type == UIEvent::Type::MouseMove) {
        m_state = inHeader ? UIWidgetState::Hovered : UIWidgetState::Normal;

        if (m_open) {
            float itemH = 26.0f;
            float dropW = 220.0f;
            float curY = m_screenRect.y + m_screenRect.w + 4.0f;
            m_hoveredIndex = -1;

            for (size_t i = 0; i < m_items.size(); ++i) {
                if (m_items[i].isSeparator) {
                    curY += 8.0f;
                    continue;
                }
                if (event.mousePos.x >= m_screenRect.x && event.mousePos.x <= m_screenRect.x + dropW &&
                    event.mousePos.y >= curY && event.mousePos.y <= curY + itemH) {
                    m_hoveredIndex = (int)i;
                    return true;
                }
                curY += itemH;
            }
        }
    } else if (event.type == UIEvent::Type::MouseDown) {
        if (inHeader) {
            m_open = !m_open;
            return true;
        }

        if (m_open && m_hoveredIndex >= 0 && m_hoveredIndex < (int)m_items.size()) {
            if (m_items[m_hoveredIndex].onClick) {
                m_items[m_hoveredIndex].onClick();
            }
            m_open = false;
            return true;
        }

        if (m_open && !inHeader) {
            m_open = false;
        }
    }

    return inHeader;
}

// ----------------------------------------------------------------------------
// Top Menu Bar Implementation
// ----------------------------------------------------------------------------
CustomUITopMenuBar::CustomUITopMenuBar(const std::string& name) : CustomUIWidget(name) {
    m_position = glm::vec2(0.0f, 0.0f);
    m_size = glm::vec2(1920.0f, 28.0f);
    m_anchor = UIAnchor::TopLeft;
    m_style.backgroundColor = glm::vec4(0.10f, 0.11f, 0.14f, 0.98f);
    m_style.borderThickness = 1.0f;
    m_style.borderColor = glm::vec4(0.18f, 0.20f, 0.24f, 1.0f);
}

std::shared_ptr<CustomUIMenu> CustomUITopMenuBar::AddMenu(const std::string& title) {
    auto menu = std::make_shared<CustomUIMenu>(title);
    AddChild(menu);
    m_menus.push_back(menu);

    float curX = 6.0f;
    for (auto& m : m_menus) {
        m->SetPosition(glm::vec2(curX, 1.0f));
        curX += m->GetSize().x + 2.0f;
    }
    return menu;
}

void CustomUITopMenuBar::Render() {
    if (!m_visible) return;
    auto& renderer = CustomUIRenderer::Get();

    // Top menu bar background strip
    renderer.DrawSolidRect(m_screenRect, m_style.backgroundColor);
    renderer.DrawSolidRect(glm::vec4(m_screenRect.x, m_screenRect.y + m_screenRect.w - 1.0f, m_screenRect.z, 1.0f), m_style.borderColor);

    CustomUIWidget::Render();
}

bool CustomUITopMenuBar::HandleEvent(const UIEvent& event) {
    if (!m_visible || !m_enabled) return false;
    return CustomUIWidget::HandleEvent(event);
}

// ----------------------------------------------------------------------------
// Slider Implementation
// ----------------------------------------------------------------------------
CustomUISlider::CustomUISlider(float minVal, float maxVal, float curVal, const std::string& name)
    : CustomUIWidget(name), m_min(minVal), m_max(maxVal), m_currentValue(curVal) {
    m_size = glm::vec2(140.0f, 20.0f);
}

void CustomUISlider::SetValue(float val) {
    m_currentValue = std::clamp(val, m_min, m_max);
    if (m_onValueChanged) m_onValueChanged(m_currentValue);
}

void CustomUISlider::Render() {
    if (!m_visible) return;
    auto& renderer = CustomUIRenderer::Get();

    // Track
    float trackH = 4.0f;
    float trackY = m_screenRect.y + (m_screenRect.w - trackH) * 0.5f;
    glm::vec4 trackRect(m_screenRect.x, trackY, m_screenRect.z, trackH);
    renderer.DrawRoundedRect(trackRect, glm::vec4(0.20f, 0.22f, 0.26f, 1.0f), 2.0f);

    // Filled portion
    float norm = (m_max > m_min) ? ((m_currentValue - m_min) / (m_max - m_min)) : 0.0f;
    glm::vec4 fillRect(m_screenRect.x, trackY, m_screenRect.z * norm, trackH);
    renderer.DrawRoundedRect(fillRect, glm::vec4(0.14f, 0.52f, 0.95f, 1.0f), 2.0f);

    // Thumb Knob
    float knobSize = 12.0f;
    float knobX = m_screenRect.x + m_screenRect.z * norm - knobSize * 0.5f;
    float knobY = m_screenRect.y + (m_screenRect.w - knobSize) * 0.5f;
    glm::vec4 knobRect(knobX, knobY, knobSize, knobSize);
    renderer.DrawRoundedRect(knobRect, glm::vec4(0.95f, 0.95f, 0.98f, 1.0f), knobSize * 0.5f);

    CustomUIWidget::Render();
}

bool CustomUISlider::HandleEvent(const UIEvent& event) {
    if (!m_visible || !m_enabled) return false;

    bool inside = (event.mousePos.x >= m_screenRect.x && event.mousePos.x <= m_screenRect.x + m_screenRect.z &&
                   event.mousePos.y >= m_screenRect.y && event.mousePos.y <= m_screenRect.y + m_screenRect.w);

    if (event.type == UIEvent::Type::MouseDown && inside) {
        m_dragging = true;
        float norm = std::clamp((event.mousePos.x - m_screenRect.x) / m_screenRect.z, 0.0f, 1.0f);
        SetValue(m_min + norm * (m_max - m_min));
        return true;
    } else if (event.type == UIEvent::Type::MouseMove && m_dragging) {
        float norm = std::clamp((event.mousePos.x - m_screenRect.x) / m_screenRect.z, 0.0f, 1.0f);
        SetValue(m_min + norm * (m_max - m_min));
        return true;
    } else if (event.type == UIEvent::Type::MouseUp) {
        if (m_dragging) {
            m_dragging = false;
            return true;
        }
    }

    return inside;
}

}} // namespace Eunoia::UI
