#pragma once

#include "UITypes.h"
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <unordered_set>
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>

#if __has_include(<imgui.h>)
#include <imgui.h>
#endif

namespace EunoiaUI {

class UserInterface {
public:
    std::string name = "New User Interface";
    std::string filePath = "";
    glm::vec2 canvasResolution{ 1920.0f, 1080.0f };
    std::vector<UIElement> elements;
    bool isVisible = true;

    UserInterface() = default;
    explicit UserInterface(const std::string& inName) : name(inName) {}

    // Runtime Visibility API
    void Show() { isVisible = true; }
    void Hide() { isVisible = false; }
    void SetVisible(bool v) { isVisible = v; }
    bool IsVisible() const { return isVisible; }

    UIElement* AddElement(UIElementType type, const std::string& elemName = "", const std::string& parentId = "") {
        static uint64_t s_elemCounter = 1;
        UIElement elem;
        elem.id = "elem_" + std::to_string(s_elemCounter++) + "_" + std::to_string(elements.size() + 1);
        elem.type = type;

        if (!elemName.empty()) {
            elem.name = elemName;
        } else {
            elem.name = std::string(UIElementTypeToString(type)) + "_" + std::to_string(elements.size() + 1);
        }

        elem.anchorPreset = UIAnchorPreset::TopLeft;
        AnchorPresetToMinMax(elem.anchorPreset, elem.anchorMin, elem.anchorMax, elem.pivot);

        // Atlas theme default configurations
        if (type == UIElementType::Panel) {
            elem.position = glm::vec2(100.0f, 100.0f);
            elem.size = glm::vec2(400.0f, 260.0f);
            elem.backgroundColor = glm::vec4(0.082f, 0.082f, 0.102f, 0.95f); // Atlas Surface #15151A
            elem.borderColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.08f);           // Subtle outline
            elem.borderThickness = 1.0f;
            elem.cornerRounding = 10.0f;
            elem.hasShadow = true;
            elem.shadowColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.50f);
            elem.shadowSpread = 10.0f;
            elem.filled = true;
        } else if (type == UIElementType::Button) {
            elem.position = glm::vec2(120.0f, 120.0f);
            elem.size = glm::vec2(180.0f, 44.0f);
            elem.text = "Click Me";
            elem.fontSize = 15.0f;
            elem.backgroundColor = glm::vec4(0.125f, 0.125f, 0.153f, 1.0f); // Atlas Control #202027
            elem.hoverColor = glm::vec4(0.169f, 0.169f, 0.204f, 1.0f);
            elem.pressedColor = glm::vec4(0.290f, 0.486f, 1.0f, 1.0f);      // Atlas Blue #4A7CFF
            elem.borderColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.08f);
            elem.borderThickness = 1.0f;
            elem.cornerRounding = 7.0f;
            elem.textColor = glm::vec4(0.929f, 0.933f, 0.953f, 1.0f);
            elem.textAlignment = 1; // Center
            elem.filled = true;
        } else if (type == UIElementType::Text) {
            elem.position = glm::vec2(120.0f, 80.0f);
            elem.size = glm::vec2(250.0f, 32.0f);
            elem.text = "Heading Text";
            elem.fontSize = 18.0f;
            elem.textColor = glm::vec4(0.929f, 0.933f, 0.953f, 1.0f);
            elem.textAlignment = 0; // Left
            elem.filled = false;
            elem.borderThickness = 0.0f;
        } else if (type == UIElementType::ProgressBar) {
            elem.position = glm::vec2(120.0f, 180.0f);
            elem.size = glm::vec2(300.0f, 22.0f);
            elem.progressValue = 75.0f;
            elem.progressMin = 0.0f;
            elem.progressMax = 100.0f;
            elem.progressTrackColor = glm::vec4(0.125f, 0.125f, 0.153f, 1.0f); // Track #202027
            elem.progressFillColor = glm::vec4(0.290f, 0.486f, 1.0f, 1.0f);   // Fill #4A7CFF
            elem.borderColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.06f);
            elem.borderThickness = 1.0f;
            elem.cornerRounding = 6.0f;
            elem.showPercentage = true;
            elem.filled = true;
        } else if (type == UIElementType::Slider) {
            elem.position = glm::vec2(120.0f, 220.0f);
            elem.size = glm::vec2(260.0f, 26.0f);
            elem.sliderValue = 50.0f;
            elem.sliderMin = 0.0f;
            elem.sliderMax = 100.0f;
            elem.sliderTrackColor = glm::vec4(0.125f, 0.125f, 0.153f, 1.0f);
            elem.sliderThumbColor = glm::vec4(0.290f, 0.486f, 1.0f, 1.0f);
            elem.cornerRounding = 6.0f;
            elem.filled = true;
        } else if (type == UIElementType::Checkbox) {
            elem.position = glm::vec2(120.0f, 260.0f);
            elem.size = glm::vec2(180.0f, 28.0f);
            elem.text = "Enable Setting";
            elem.isChecked = true;
            elem.fontSize = 15.0f;
            elem.checkboxBoxColor = glm::vec4(0.125f, 0.125f, 0.153f, 1.0f);
            elem.checkboxCheckColor = glm::vec4(0.290f, 0.486f, 1.0f, 1.0f);
            elem.textColor = glm::vec4(0.929f, 0.933f, 0.953f, 1.0f);
            elem.cornerRounding = 5.0f;
            elem.filled = true;
        } else if (type == UIElementType::Image) {
            elem.position = glm::vec2(120.0f, 120.0f);
            elem.size = glm::vec2(128.0f, 128.0f);
            elem.imageTint = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            elem.cornerRounding = 8.0f;
            elem.filled = true;
        }

        elem.zOrder = (int)elements.size();
        elem.parentId = parentId;

        elements.push_back(elem);

        if (!parentId.empty()) {
            UIElement* parent = FindElement(parentId);
            if (parent) {
                parent->childIds.push_back(elements.back().id);
                parent->isExpanded = true;
            }
        }

        return &elements.back();
    }

    UIElement* FindElement(const std::string& idOrName) {
        for (auto& el : elements) {
            if (el.id == idOrName || el.name == idOrName) return &el;
        }
        return nullptr;
    }

    const UIElement* FindElement(const std::string& idOrName) const {
        for (const auto& el : elements) {
            if (el.id == idOrName || el.name == idOrName) return &el;
        }
        return nullptr;
    }

    bool IsDescendantOf(const std::string& potentialChildId, const std::string& potentialAncestorId) const {
        if (potentialChildId.empty() || potentialAncestorId.empty()) return false;
        if (potentialChildId == potentialAncestorId) return true;

        const UIElement* child = FindElement(potentialChildId);
        while (child && !child->parentId.empty()) {
            if (child->parentId == potentialAncestorId) return true;
            child = FindElement(child->parentId);
        }
        return false;
    }

    bool SetParent(const std::string& childId, const std::string& newParentId) {
        if (childId.empty() || childId == newParentId) return false;
        if (!newParentId.empty() && IsDescendantOf(newParentId, childId)) {
            return false;
        }

        UIElement* child = FindElement(childId);
        if (!child) return false;

        // Remove from old parent
        if (!child->parentId.empty()) {
            UIElement* oldParent = FindElement(child->parentId);
            if (oldParent) {
                auto it = std::find(oldParent->childIds.begin(), oldParent->childIds.end(), childId);
                if (it != oldParent->childIds.end()) oldParent->childIds.erase(it);
            }
        }

        // Assign new parent
        child->parentId = newParentId;
        if (!newParentId.empty()) {
            UIElement* newParent = FindElement(newParentId);
            if (newParent) {
                if (std::find(newParent->childIds.begin(), newParent->childIds.end(), childId) == newParent->childIds.end()) {
                    newParent->childIds.push_back(childId);
                }
                newParent->isExpanded = true;
            }
        }
        return true;
    }

    bool RemoveElement(const std::string& id) {
        std::unordered_set<std::string> toRemove;
        auto collectDescendants = [&](auto& self, const std::string& curId) -> void {
            toRemove.insert(curId);
            const UIElement* el = FindElement(curId);
            if (el) {
                for (const auto& ch : el->childIds) {
                    self(self, ch);
                }
            }
        };
        collectDescendants(collectDescendants, id);

        UIElement* target = FindElement(id);
        if (target && !target->parentId.empty()) {
            UIElement* parent = FindElement(target->parentId);
            if (parent) {
                auto it = std::find(parent->childIds.begin(), parent->childIds.end(), id);
                if (it != parent->childIds.end()) parent->childIds.erase(it);
            }
        }

        elements.erase(std::remove_if(elements.begin(), elements.end(), [&](const UIElement& el) {
            return toRemove.find(el.id) != toRemove.end();
        }), elements.end());

        return true;
    }

    UIElement* DuplicateElement(const std::string& id) {
        const UIElement* source = FindElement(id);
        if (!source) return nullptr;

        UIElement copy = *source;
        static uint64_t s_dupCounter = 1;
        copy.id = "elem_" + std::to_string(s_dupCounter++) + "_" + std::to_string(elements.size() + 1);
        copy.name = source->name + "_Copy";
        copy.position += glm::vec2(20.0f, 20.0f);
        copy.childIds.clear();
        copy.zOrder = (int)elements.size();

        elements.push_back(copy);

        if (!copy.parentId.empty()) {
            UIElement* parent = FindElement(copy.parentId);
            if (parent) parent->childIds.push_back(elements.back().id);
        }

        return &elements.back();
    }

    glm::vec4 GetElementWorldRect(const UIElement& el) const {
        if (el.parentId.empty()) {
            glm::vec4 canvasRect(0.0f, 0.0f, canvasResolution.x, canvasResolution.y);
            return ComputeElementRect(canvasRect, el);
        }
        const UIElement* parent = FindElement(el.parentId);
        if (!parent) {
            glm::vec4 canvasRect(0.0f, 0.0f, canvasResolution.x, canvasResolution.y);
            return ComputeElementRect(canvasRect, el);
        }
        glm::vec4 parentRect = GetElementWorldRect(*parent);
        return ComputeElementRect(parentRect, el);
    }

    // Script & Gameplay Control APIs
    bool SetText(const std::string& elemNameOrId, const std::string& newText) {
        UIElement* el = FindElement(elemNameOrId);
        if (el) {
            el->text = newText;
            return true;
        }
        return false;
    }

    bool SetTextColor(const std::string& elemNameOrId, const glm::vec4& color) {
        UIElement* el = FindElement(elemNameOrId);
        if (el) {
            el->textColor = color;
            return true;
        }
        return false;
    }

    bool SetBackgroundColor(const std::string& elemNameOrId, const glm::vec4& color) {
        UIElement* el = FindElement(elemNameOrId);
        if (el) {
            el->backgroundColor = color;
            return true;
        }
        return false;
    }

    bool SetProgressBarValue(const std::string& elemNameOrId, float value) {
        UIElement* el = FindElement(elemNameOrId);
        if (el && el->type == UIElementType::ProgressBar) {
            el->progressValue = std::clamp(value, el->progressMin, el->progressMax);
            return true;
        }
        return false;
    }

    bool SetSliderValue(const std::string& elemNameOrId, float value) {
        UIElement* el = FindElement(elemNameOrId);
        if (el && el->type == UIElementType::Slider) {
            el->sliderValue = std::clamp(value, el->sliderMin, el->sliderMax);
            return true;
        }
        return false;
    }

    float GetSliderValue(const std::string& elemNameOrId) const {
        const UIElement* el = FindElement(elemNameOrId);
        if (el && el->type == UIElementType::Slider) {
            return el->sliderValue;
        }
        return 0.0f;
    }

    bool SetCheckboxValue(const std::string& elemNameOrId, bool checked) {
        UIElement* el = FindElement(elemNameOrId);
        if (el && el->type == UIElementType::Checkbox) {
            el->isChecked = checked;
            return true;
        }
        return false;
    }

    bool IsCheckboxChecked(const std::string& elemNameOrId) const {
        const UIElement* el = FindElement(elemNameOrId);
        if (el && el->type == UIElementType::Checkbox) {
            return el->isChecked;
        }
        return false;
    }

    bool SetElementVisible(const std::string& elemNameOrId, bool visible) {
        UIElement* el = FindElement(elemNameOrId);
        if (el) {
            el->visible = visible;
            return true;
        }
        return false;
    }

    // Interactive runtime input processing (Buttons, Sliders, Checkboxes)
    void ProcessInput(const glm::vec2& mousePos, bool isMouseDown, bool isMousePressed, bool isMouseReleased,
                      float wheelDelta, const glm::vec2& canvasOrigin, float scale,
                      const std::function<void(const UIEventBinding&)>& onTriggerEvent = nullptr) {
        if (!isVisible) return;

        // Iterate elements in reverse Z-order so topmost receives clicks first
        for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
            auto& el = *it;
            if (!el.visible || !el.enabled) continue;

            glm::vec4 worldRect = GetElementWorldRect(el);
            glm::vec2 screenPos = canvasOrigin + glm::vec2(worldRect.x, worldRect.y) * scale;
            glm::vec2 screenSize = glm::vec2(worldRect.z, worldRect.w) * scale;

            bool isHovered = (mousePos.x >= screenPos.x && mousePos.x <= screenPos.x + screenSize.x &&
                              mousePos.y >= screenPos.y && mousePos.y <= screenPos.y + screenSize.y);

            if (el.type == UIElementType::Button) {
                if (isHovered && isMouseReleased) {
                    for (const auto& binding : el.eventBindings) {
                        if (binding.eventName == "OnClick" && onTriggerEvent) {
                            onTriggerEvent(binding);
                        }
                    }
                }
            } else if (el.type == UIElementType::Checkbox) {
                if (isHovered && isMousePressed) {
                    el.isChecked = !el.isChecked;
                    for (const auto& binding : el.eventBindings) {
                        if (binding.eventName == "OnToggle" && onTriggerEvent) {
                            onTriggerEvent(binding);
                        }
                    }
                }
            } else if (el.type == UIElementType::Slider) {
                if (isHovered && isMouseDown) {
                    float relX = (mousePos.x - screenPos.x) / std::max(1.0f, screenSize.x);
                    relX = std::clamp(relX, 0.0f, 1.0f);
                    el.sliderValue = el.sliderMin + relX * (el.sliderMax - el.sliderMin);
                    for (const auto& binding : el.eventBindings) {
                        if (binding.eventName == "OnValueChanged" && onTriggerEvent) {
                            onTriggerEvent(binding);
                        }
                    }
                }
            }
        }
    }

    UIElement* GetElementById(const std::string& id) {
        return FindElement(id);
    }
    const UIElement* GetElementById(const std::string& id) const {
        return FindElement(id);
    }

    void RenderUI(void* imDrawListPtr, const glm::vec2& canvasOrigin, float scale) const {
#if __has_include(<imgui.h>)
        if (!isVisible) return;
        ImDrawList* drawList = reinterpret_cast<ImDrawList*>(imDrawListPtr);
        if (!drawList) return;

        std::vector<const UIElement*> sortedElements;
        for (const auto& el : elements) {
            if (el.visible) sortedElements.push_back(&el);
        }
        std::sort(sortedElements.begin(), sortedElements.end(), [](const UIElement* a, const UIElement* b) {
            return a->zOrder < b->zOrder;
        });

        ImGuiIO& io = ImGui::GetIO();
        glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
        bool isMouseDown = io.MouseDown[0];

        for (const UIElement* pEl : sortedElements) {
            const auto& el = *pEl;
            glm::vec4 worldRect = GetElementWorldRect(el);
            glm::vec2 elemPos = canvasOrigin + glm::vec2(worldRect.x, worldRect.y) * scale;
            glm::vec2 elemSize = glm::vec2(worldRect.z, worldRect.w) * scale;
            glm::vec2 elemEnd = elemPos + elemSize;

            ImVec2 pMin(elemPos.x, elemPos.y);
            ImVec2 pMax(elemEnd.x, elemEnd.y);

            bool isHovered = (mousePos.x >= elemPos.x && mousePos.x <= elemEnd.x &&
                              mousePos.y >= elemPos.y && mousePos.y <= elemEnd.y);
            bool isPressed = isHovered && isMouseDown;

            if (el.hasShadow) {
                ImVec2 sMin(pMin.x + el.shadowOffset.x * scale - el.shadowSpread * scale * 0.5f,
                            pMin.y + el.shadowOffset.y * scale - el.shadowSpread * scale * 0.5f);
                ImVec2 sMax(pMax.x + el.shadowOffset.x * scale + el.shadowSpread * scale * 0.5f,
                            pMax.y + el.shadowOffset.y * scale + el.shadowSpread * scale * 0.5f);
                ImU32 sCol = IM_COL32((int)(el.shadowColor.r * 255), (int)(el.shadowColor.g * 255),
                                      (int)(el.shadowColor.b * 255), (int)(el.shadowColor.a * 255 * el.opacity));
                drawList->AddRectFilled(sMin, sMax, sCol, (el.cornerRounding + el.shadowSpread * 0.5f) * scale);
            }

            if (el.type == UIElementType::Panel) {
                ImU32 bgCol = IM_COL32((int)(el.backgroundColor.r * 255), (int)(el.backgroundColor.g * 255),
                                       (int)(el.backgroundColor.b * 255), (int)(el.backgroundColor.a * 255 * el.opacity));
                ImU32 bdCol = IM_COL32((int)(el.borderColor.r * 255), (int)(el.borderColor.g * 255),
                                       (int)(el.borderColor.b * 255), (int)(el.borderColor.a * 255 * el.opacity));
                if (el.filled) {
                    drawList->AddRectFilled(pMin, pMax, bgCol, el.cornerRounding * scale);
                }
                if (el.borderThickness > 0.0f) {
                    drawList->AddRect(pMin, pMax, bdCol, el.cornerRounding * scale, 0, el.borderThickness * scale);
                }
            } else if (el.type == UIElementType::Button) {
                glm::vec4 curBg = isPressed ? el.pressedColor : (isHovered ? el.hoverColor : el.backgroundColor);
                ImU32 bgCol = IM_COL32((int)(curBg.r * 255), (int)(curBg.g * 255),
                                       (int)(curBg.b * 255), (int)(curBg.a * 255 * el.opacity));
                ImU32 bdCol = IM_COL32((int)(el.borderColor.r * 255), (int)(el.borderColor.g * 255),
                                       (int)(el.borderColor.b * 255), (int)(el.borderColor.a * 255 * el.opacity));
                drawList->AddRectFilled(pMin, pMax, bgCol, el.cornerRounding * scale);
                if (el.borderThickness > 0.0f) {
                    drawList->AddRect(pMin, pMax, bdCol, el.cornerRounding * scale, 0, el.borderThickness * scale);
                }
                if (!el.text.empty()) {
                    ImU32 txtCol = IM_COL32((int)(el.textColor.r * 255), (int)(el.textColor.g * 255),
                                            (int)(el.textColor.b * 255), (int)(el.textColor.a * 255 * el.opacity));
                    float fSize = el.fontSize * scale;
                    ImVec2 tSize = ImGui::CalcTextSize(el.text.c_str());
                    ImVec2 tPos(pMin.x + (elemSize.x - tSize.x) * 0.5f, pMin.y + (elemSize.y - tSize.y) * 0.5f);
                    drawList->AddText(nullptr, fSize, tPos, txtCol, el.text.c_str());
                }
            } else if (el.type == UIElementType::Text) {
                ImU32 txtCol = IM_COL32((int)(el.textColor.r * 255), (int)(el.textColor.g * 255),
                                        (int)(el.textColor.b * 255), (int)(el.textColor.a * 255 * el.opacity));
                float fSize = el.fontSize * scale;
                ImVec2 tSize = ImGui::CalcTextSize(el.text.c_str());
                ImVec2 tPos = pMin;
                if (el.textAlignment == 1) { // Center
                    tPos.x = pMin.x + (elemSize.x - tSize.x) * 0.5f;
                } else if (el.textAlignment == 2) { // Right
                    tPos.x = pMax.x - tSize.x;
                }
                if (el.textShadow) {
                    ImU32 shdCol = IM_COL32((int)(el.textShadowColor.r * 255), (int)(el.textShadowColor.g * 255),
                                            (int)(el.textShadowColor.b * 255), (int)(el.textShadowColor.a * 255 * el.opacity));
                    ImVec2 sPos(tPos.x + el.textShadowOffset.x * scale, tPos.y + el.textShadowOffset.y * scale);
                    drawList->AddText(nullptr, fSize, sPos, shdCol, el.text.c_str());
                }
                drawList->AddText(nullptr, fSize, tPos, txtCol, el.text.c_str());
            } else if (el.type == UIElementType::ProgressBar) {
                ImU32 trkCol = IM_COL32((int)(el.progressTrackColor.r * 255), (int)(el.progressTrackColor.g * 255),
                                        (int)(el.progressTrackColor.b * 255), (int)(el.progressTrackColor.a * 255 * el.opacity));
                ImU32 filCol = IM_COL32((int)(el.progressFillColor.r * 255), (int)(el.progressFillColor.g * 255),
                                        (int)(el.progressFillColor.b * 255), (int)(el.progressFillColor.a * 255 * el.opacity));
                drawList->AddRectFilled(pMin, pMax, trkCol, el.cornerRounding * scale);
                float frac = (el.progressMax > el.progressMin) ?
                    std::clamp((el.progressValue - el.progressMin) / (el.progressMax - el.progressMin), 0.0f, 1.0f) : 0.0f;
                ImVec2 fillMax(pMin.x + elemSize.x * frac, pMax.y);
                if (frac > 0.01f) {
                    drawList->AddRectFilled(pMin, fillMax, filCol, el.cornerRounding * scale);
                }
                if (el.borderThickness > 0.0f) {
                    ImU32 bdCol = IM_COL32((int)(el.borderColor.r * 255), (int)(el.borderColor.g * 255),
                                           (int)(el.borderColor.b * 255), (int)(el.borderColor.a * 255 * el.opacity));
                    drawList->AddRect(pMin, pMax, bdCol, el.cornerRounding * scale, 0, el.borderThickness * scale);
                }
                if (el.showPercentage) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%d%%", (int)(frac * 100.0f));
                    ImVec2 tSize = ImGui::CalcTextSize(buf);
                    ImVec2 tPos(pMin.x + (elemSize.x - tSize.x) * 0.5f, pMin.y + (elemSize.y - tSize.y) * 0.5f);
                    drawList->AddText(nullptr, 12.0f * scale, tPos, IM_COL32(240, 240, 245, 240), buf);
                }
            } else if (el.type == UIElementType::Slider) {
                float barH = 6.0f * scale;
                float barY = pMin.y + (elemSize.y - barH) * 0.5f;
                ImVec2 tMin(pMin.x, barY);
                ImVec2 tMax(pMax.x, barY + barH);
                ImU32 trkCol = IM_COL32((int)(el.sliderTrackColor.r * 255), (int)(el.sliderTrackColor.g * 255),
                                        (int)(el.sliderTrackColor.b * 255), (int)(el.sliderTrackColor.a * 255 * el.opacity));
                drawList->AddRectFilled(tMin, tMax, trkCol, 3.0f * scale);
                float frac = (el.sliderMax > el.sliderMin) ?
                    std::clamp((el.sliderValue - el.sliderMin) / (el.sliderMax - el.sliderMin), 0.0f, 1.0f) : 0.0f;
                ImVec2 fMax(pMin.x + elemSize.x * frac, barY + barH);
                ImU32 thCol = IM_COL32((int)(el.sliderThumbColor.r * 255), (int)(el.sliderThumbColor.g * 255),
                                       (int)(el.sliderThumbColor.b * 255), (int)(el.sliderThumbColor.a * 255 * el.opacity));
                drawList->AddRectFilled(tMin, fMax, thCol, 3.0f * scale);
                float thumbR = 8.0f * scale;
                ImVec2 thumbCenter(pMin.x + elemSize.x * frac, pMin.y + elemSize.y * 0.5f);
                drawList->AddCircleFilled(thumbCenter, thumbR, thCol);
                drawList->AddCircle(thumbCenter, thumbR, IM_COL32(255, 255, 255, 120), 0, 1.5f * scale);
            } else if (el.type == UIElementType::Checkbox) {
                float boxSize = std::min(elemSize.y, 22.0f * scale);
                ImVec2 bMin(pMin.x, pMin.y + (elemSize.y - boxSize) * 0.5f);
                ImVec2 bMax(bMin.x + boxSize, bMin.y + boxSize);
                ImU32 boxCol = IM_COL32((int)(el.checkboxBoxColor.r * 255), (int)(el.checkboxBoxColor.g * 255),
                                        (int)(el.checkboxBoxColor.b * 255), (int)(el.checkboxBoxColor.a * 255 * el.opacity));
                ImU32 chkCol = IM_COL32((int)(el.checkboxCheckColor.r * 255), (int)(el.checkboxCheckColor.g * 255),
                                        (int)(el.checkboxCheckColor.b * 255), (int)(el.checkboxCheckColor.a * 255 * el.opacity));
                drawList->AddRectFilled(bMin, bMax, boxCol, el.cornerRounding * scale);
                drawList->AddRect(bMin, bMax, IM_COL32(255, 255, 255, 25), el.cornerRounding * scale, 0, 1.0f * scale);
                if (el.isChecked) {
                    float pad = 4.0f * scale;
                    ImVec2 p1(bMin.x + pad, bMin.y + boxSize * 0.5f);
                    ImVec2 p2(bMin.x + boxSize * 0.40f, bMax.y - pad);
                    ImVec2 p3(bMax.x - pad, bMin.y + pad);
                    drawList->AddLine(p1, p2, chkCol, 2.5f * scale);
                    drawList->AddLine(p2, p3, chkCol, 2.5f * scale);
                }
                if (!el.text.empty()) {
                    ImU32 txtCol = IM_COL32((int)(el.textColor.r * 255), (int)(el.textColor.g * 255),
                                            (int)(el.textColor.b * 255), (int)(el.textColor.a * 255 * el.opacity));
                    ImVec2 tPos(bMax.x + 8.0f * scale, pMin.y + (elemSize.y - ImGui::CalcTextSize(el.text.c_str()).y) * 0.5f);
                    drawList->AddText(nullptr, el.fontSize * scale, tPos, txtCol, el.text.c_str());
                }
            } else if (el.type == UIElementType::Image) {
                drawList->AddRectFilled(pMin, pMax, IM_COL32(35, 38, 48, 200), el.cornerRounding * scale);
                drawList->AddRect(pMin, pMax, IM_COL32(74, 124, 255, 120), el.cornerRounding * scale, 0, 1.0f);
                drawList->AddText(nullptr, 12.0f * scale, ImVec2(pMin.x + 6.0f, pMin.y + 6.0f), IM_COL32(180, 185, 200, 200), "Image");
            }
        }
#endif
    }

    // Pure .eui JSON Persistence
    bool SaveToFile(const std::string& path) {
        nlohmann::json root;
        root["name"] = name;
        root["canvasResolution"] = { canvasResolution.x, canvasResolution.y };
        root["isVisible"] = isVisible;

        nlohmann::json elemArray = nlohmann::json::array();
        for (const auto& el : elements) {
            nlohmann::json e;
            e["id"] = el.id;
            e["name"] = el.name;
            e["type"] = UIElementTypeToString(el.type);
            e["position"] = { el.position.x, el.position.y };
            e["size"] = { el.size.x, el.size.y };
            e["anchorMin"] = { el.anchorMin.x, el.anchorMin.y };
            e["anchorMax"] = { el.anchorMax.x, el.anchorMax.y };
            e["pivot"] = { el.pivot.x, el.pivot.y };
            e["anchorPreset"] = (int)el.anchorPreset;
            e["zOrder"] = el.zOrder;
            e["visible"] = el.visible;
            e["enabled"] = el.enabled;
            e["opacity"] = el.opacity;
            e["parentId"] = el.parentId;
            e["childIds"] = el.childIds;
            e["isExpanded"] = el.isExpanded;

            // Atlas Styling
            e["backgroundColor"] = { el.backgroundColor.r, el.backgroundColor.g, el.backgroundColor.b, el.backgroundColor.a };
            e["borderColor"] = { el.borderColor.r, el.borderColor.g, el.borderColor.b, el.borderColor.a };
            e["borderThickness"] = el.borderThickness;
            e["cornerRounding"] = el.cornerRounding;
            e["filled"] = el.filled;
            e["hasShadow"] = el.hasShadow;
            e["shadowColor"] = { el.shadowColor.r, el.shadowColor.g, el.shadowColor.b, el.shadowColor.a };
            e["shadowOffset"] = { el.shadowOffset.x, el.shadowOffset.y };
            e["shadowSpread"] = el.shadowSpread;

            // Interactive states
            e["hoverColor"] = { el.hoverColor.r, el.hoverColor.g, el.hoverColor.b, el.hoverColor.a };
            e["pressedColor"] = { el.pressedColor.r, el.pressedColor.g, el.pressedColor.b, el.pressedColor.a };
            e["disabledColor"] = { el.disabledColor.r, el.disabledColor.g, el.disabledColor.b, el.disabledColor.a };

            // Text
            e["text"] = el.text;
            e["fontSize"] = el.fontSize;
            e["textColor"] = { el.textColor.r, el.textColor.g, el.textColor.b, el.textColor.a };
            e["textAlignment"] = el.textAlignment;
            e["wordWrap"] = el.wordWrap;
            e["textShadow"] = el.textShadow;
            e["textShadowColor"] = { el.textShadowColor.r, el.textShadowColor.g, el.textShadowColor.b, el.textShadowColor.a };
            e["textShadowOffset"] = { el.textShadowOffset.x, el.textShadowOffset.y };

            // ProgressBar
            e["progressValue"] = el.progressValue;
            e["progressMin"] = el.progressMin;
            e["progressMax"] = el.progressMax;
            e["progressFillColor"] = { el.progressFillColor.r, el.progressFillColor.g, el.progressFillColor.b, el.progressFillColor.a };
            e["progressTrackColor"] = { el.progressTrackColor.r, el.progressTrackColor.g, el.progressTrackColor.b, el.progressTrackColor.a };
            e["fillDirection"] = el.fillDirection;
            e["showPercentage"] = el.showPercentage;

            // Slider
            e["sliderValue"] = el.sliderValue;
            e["sliderMin"] = el.sliderMin;
            e["sliderMax"] = el.sliderMax;
            e["sliderStep"] = el.sliderStep;
            e["sliderTrackColor"] = { el.sliderTrackColor.r, el.sliderTrackColor.g, el.sliderTrackColor.b, el.sliderTrackColor.a };
            e["sliderThumbColor"] = { el.sliderThumbColor.r, el.sliderThumbColor.g, el.sliderThumbColor.b, el.sliderThumbColor.a };

            // Checkbox
            e["isChecked"] = el.isChecked;
            e["checkboxBoxColor"] = { el.checkboxBoxColor.r, el.checkboxBoxColor.g, el.checkboxBoxColor.b, el.checkboxBoxColor.a };
            e["checkboxCheckColor"] = { el.checkboxCheckColor.r, el.checkboxCheckColor.g, el.checkboxCheckColor.b, el.checkboxCheckColor.a };

            // Image
            e["texturePath"] = el.texturePath;
            e["imageTint"] = { el.imageTint.r, el.imageTint.g, el.imageTint.b, el.imageTint.a };

            // Event Bindings
            nlohmann::json evArray = nlohmann::json::array();
            for (const auto& b : el.eventBindings) {
                nlohmann::json bj;
                bj["eventName"] = b.eventName;
                bj["targetActorId"] = b.targetActorId;
                bj["targetActorName"] = b.targetActorName;
                bj["behaviourClass"] = b.behaviourClass;
                bj["scriptPath"] = b.scriptPath;
                bj["targetFunction"] = b.targetFunction;
                evArray.push_back(bj);
            }
            e["eventBindings"] = evArray;

            elemArray.push_back(e);
        }
        root["elements"] = elemArray;

        std::ofstream out(path);
        if (!out.is_open()) return false;
        out << std::setw(4) << root << std::endl;
        filePath = path;
        return true;
    }

    bool LoadFromFile(const std::string& path) {
        std::ifstream in(path);
        if (!in.is_open()) return false;

        try {
            nlohmann::json root;
            in >> root;

            if (root.contains("name")) name = root["name"].get<std::string>();
            if (root.contains("canvasResolution") && root["canvasResolution"].is_array() && root["canvasResolution"].size() >= 2) {
                canvasResolution.x = root["canvasResolution"][0].get<float>();
                canvasResolution.y = root["canvasResolution"][1].get<float>();
            }
            if (root.contains("isVisible")) isVisible = root["isVisible"].get<bool>();

            elements.clear();
            if (root.contains("elements") && root["elements"].is_array()) {
                for (const auto& e : root["elements"]) {
                    UIElement el;
                    if (e.contains("id")) el.id = e["id"].get<std::string>();
                    if (e.contains("name")) el.name = e["name"].get<std::string>();
                    if (e.contains("type")) el.type = StringToUIElementType(e["type"].get<std::string>());
                    if (e.contains("position") && e["position"].is_array() && e["position"].size() >= 2) {
                        el.position.x = e["position"][0].get<float>();
                        el.position.y = e["position"][1].get<float>();
                    }
                    if (e.contains("size") && e["size"].is_array() && e["size"].size() >= 2) {
                        el.size.x = e["size"][0].get<float>();
                        el.size.y = e["size"][1].get<float>();
                    }
                    if (e.contains("anchorMin") && e["anchorMin"].is_array() && e["anchorMin"].size() >= 2) {
                        el.anchorMin.x = e["anchorMin"][0].get<float>();
                        el.anchorMin.y = e["anchorMin"][1].get<float>();
                    }
                    if (e.contains("anchorMax") && e["anchorMax"].is_array() && e["anchorMax"].size() >= 2) {
                        el.anchorMax.x = e["anchorMax"][0].get<float>();
                        el.anchorMax.y = e["anchorMax"][1].get<float>();
                    }
                    if (e.contains("pivot") && e["pivot"].is_array() && e["pivot"].size() >= 2) {
                        el.pivot.x = e["pivot"][0].get<float>();
                        el.pivot.y = e["pivot"][1].get<float>();
                    }
                    if (e.contains("anchorPreset")) el.anchorPreset = (UIAnchorPreset)e["anchorPreset"].get<int>();
                    if (e.contains("zOrder")) el.zOrder = e["zOrder"].get<int>();
                    if (e.contains("visible")) el.visible = e["visible"].get<bool>();
                    if (e.contains("enabled")) el.enabled = e["enabled"].get<bool>();
                    if (e.contains("opacity")) el.opacity = e["opacity"].get<float>();
                    if (e.contains("parentId")) el.parentId = e["parentId"].get<std::string>();
                    if (e.contains("childIds") && e["childIds"].is_array()) {
                        for (const auto& ch : e["childIds"]) el.childIds.push_back(ch.get<std::string>());
                    }
                    if (e.contains("isExpanded")) el.isExpanded = e["isExpanded"].get<bool>();

                    // Atlas Styling
                    if (e.contains("backgroundColor") && e["backgroundColor"].is_array() && e["backgroundColor"].size() >= 4) {
                        el.backgroundColor = glm::vec4(e["backgroundColor"][0].get<float>(), e["backgroundColor"][1].get<float>(), e["backgroundColor"][2].get<float>(), e["backgroundColor"][3].get<float>());
                    }
                    if (e.contains("borderColor") && e["borderColor"].is_array() && e["borderColor"].size() >= 4) {
                        el.borderColor = glm::vec4(e["borderColor"][0].get<float>(), e["borderColor"][1].get<float>(), e["borderColor"][2].get<float>(), e["borderColor"][3].get<float>());
                    }
                    if (e.contains("borderThickness")) el.borderThickness = e["borderThickness"].get<float>();
                    if (e.contains("cornerRounding")) el.cornerRounding = e["cornerRounding"].get<float>();
                    if (e.contains("filled")) el.filled = e["filled"].get<bool>();
                    if (e.contains("hasShadow")) el.hasShadow = e["hasShadow"].get<bool>();
                    if (e.contains("shadowColor") && e["shadowColor"].is_array() && e["shadowColor"].size() >= 4) {
                        el.shadowColor = glm::vec4(e["shadowColor"][0].get<float>(), e["shadowColor"][1].get<float>(), e["shadowColor"][2].get<float>(), e["shadowColor"][3].get<float>());
                    }
                    if (e.contains("shadowOffset") && e["shadowOffset"].is_array() && e["shadowOffset"].size() >= 2) {
                        el.shadowOffset.x = e["shadowOffset"][0].get<float>();
                        el.shadowOffset.y = e["shadowOffset"][1].get<float>();
                    }
                    if (e.contains("shadowSpread")) el.shadowSpread = e["shadowSpread"].get<float>();

                    // Interactive
                    if (e.contains("hoverColor") && e["hoverColor"].is_array() && e["hoverColor"].size() >= 4) {
                        el.hoverColor = glm::vec4(e["hoverColor"][0].get<float>(), e["hoverColor"][1].get<float>(), e["hoverColor"][2].get<float>(), e["hoverColor"][3].get<float>());
                    }
                    if (e.contains("pressedColor") && e["pressedColor"].is_array() && e["pressedColor"].size() >= 4) {
                        el.pressedColor = glm::vec4(e["pressedColor"][0].get<float>(), e["pressedColor"][1].get<float>(), e["pressedColor"][2].get<float>(), e["pressedColor"][3].get<float>());
                    }
                    if (e.contains("disabledColor") && e["disabledColor"].is_array() && e["disabledColor"].size() >= 4) {
                        el.disabledColor = glm::vec4(e["disabledColor"][0].get<float>(), e["disabledColor"][1].get<float>(), e["disabledColor"][2].get<float>(), e["disabledColor"][3].get<float>());
                    }

                    // Text
                    if (e.contains("text")) el.text = e["text"].get<std::string>();
                    if (e.contains("fontSize")) el.fontSize = e["fontSize"].get<float>();
                    if (e.contains("textColor") && e["textColor"].is_array() && e["textColor"].size() >= 4) {
                        el.textColor = glm::vec4(e["textColor"][0].get<float>(), e["textColor"][1].get<float>(), e["textColor"][2].get<float>(), e["textColor"][3].get<float>());
                    }
                    if (e.contains("textAlignment")) el.textAlignment = e["textAlignment"].get<int>();
                    if (e.contains("wordWrap")) el.wordWrap = e["wordWrap"].get<bool>();
                    if (e.contains("textShadow")) el.textShadow = e["textShadow"].get<bool>();

                    // ProgressBar
                    if (e.contains("progressValue")) el.progressValue = e["progressValue"].get<float>();
                    if (e.contains("progressMin")) el.progressMin = e["progressMin"].get<float>();
                    if (e.contains("progressMax")) el.progressMax = e["progressMax"].get<float>();
                    if (e.contains("progressFillColor") && e["progressFillColor"].is_array() && e["progressFillColor"].size() >= 4) {
                        el.progressFillColor = glm::vec4(e["progressFillColor"][0].get<float>(), e["progressFillColor"][1].get<float>(), e["progressFillColor"][2].get<float>(), e["progressFillColor"][3].get<float>());
                    }
                    if (e.contains("progressTrackColor") && e["progressTrackColor"].is_array() && e["progressTrackColor"].size() >= 4) {
                        el.progressTrackColor = glm::vec4(e["progressTrackColor"][0].get<float>(), e["progressTrackColor"][1].get<float>(), e["progressTrackColor"][2].get<float>(), e["progressTrackColor"][3].get<float>());
                    }
                    if (e.contains("fillDirection")) el.fillDirection = e["fillDirection"].get<int>();
                    if (e.contains("showPercentage")) el.showPercentage = e["showPercentage"].get<bool>();

                    // Slider
                    if (e.contains("sliderValue")) el.sliderValue = e["sliderValue"].get<float>();
                    if (e.contains("sliderMin")) el.sliderMin = e["sliderMin"].get<float>();
                    if (e.contains("sliderMax")) el.sliderMax = e["sliderMax"].get<float>();
                    if (e.contains("sliderStep")) el.sliderStep = e["sliderStep"].get<float>();
                    if (e.contains("sliderTrackColor") && e["sliderTrackColor"].is_array() && e["sliderTrackColor"].size() >= 4) {
                        el.sliderTrackColor = glm::vec4(e["sliderTrackColor"][0].get<float>(), e["sliderTrackColor"][1].get<float>(), e["sliderTrackColor"][2].get<float>(), e["sliderTrackColor"][3].get<float>());
                    }
                    if (e.contains("sliderThumbColor") && e["sliderThumbColor"].is_array() && e["sliderThumbColor"].size() >= 4) {
                        el.sliderThumbColor = glm::vec4(e["sliderThumbColor"][0].get<float>(), e["sliderThumbColor"][1].get<float>(), e["sliderThumbColor"][2].get<float>(), e["sliderThumbColor"][3].get<float>());
                    }

                    // Checkbox
                    if (e.contains("isChecked")) el.isChecked = e["isChecked"].get<bool>();
                    if (e.contains("checkboxBoxColor") && e["checkboxBoxColor"].is_array() && e["checkboxBoxColor"].size() >= 4) {
                        el.checkboxBoxColor = glm::vec4(e["checkboxBoxColor"][0].get<float>(), e["checkboxBoxColor"][1].get<float>(), e["checkboxBoxColor"][2].get<float>(), e["checkboxBoxColor"][3].get<float>());
                    }
                    if (e.contains("checkboxCheckColor") && e["checkboxCheckColor"].is_array() && e["checkboxCheckColor"].size() >= 4) {
                        el.checkboxCheckColor = glm::vec4(e["checkboxCheckColor"][0].get<float>(), e["checkboxCheckColor"][1].get<float>(), e["checkboxCheckColor"][2].get<float>(), e["checkboxCheckColor"][3].get<float>());
                    }

                    // Image
                    if (e.contains("texturePath")) el.texturePath = e["texturePath"].get<std::string>();
                    if (e.contains("imageTint") && e["imageTint"].is_array() && e["imageTint"].size() >= 4) {
                        el.imageTint = glm::vec4(e["imageTint"][0].get<float>(), e["imageTint"][1].get<float>(), e["imageTint"][2].get<float>(), e["imageTint"][3].get<float>());
                    }

                    // Event Bindings
                    if (e.contains("eventBindings") && e["eventBindings"].is_array()) {
                        for (const auto& bj : e["eventBindings"]) {
                            UIEventBinding b;
                            if (bj.contains("eventName")) b.eventName = bj["eventName"].get<std::string>();
                            if (bj.contains("targetActorId")) b.targetActorId = bj["targetActorId"].get<int>();
                            if (bj.contains("targetActorName")) b.targetActorName = bj["targetActorName"].get<std::string>();
                            if (bj.contains("behaviourClass")) b.behaviourClass = bj["behaviourClass"].get<std::string>();
                            if (bj.contains("scriptPath")) b.scriptPath = bj["scriptPath"].get<std::string>();
                            if (bj.contains("targetFunction")) b.targetFunction = bj["targetFunction"].get<std::string>();
                            el.eventBindings.push_back(b);
                        }
                    }

                    elements.push_back(el);
                }
            }

            filePath = path;
            return true;
        } catch (...) {
            return false;
        }
    }
};

} // namespace EunoiaUI

using UserInterface = EunoiaUI::UserInterface;

