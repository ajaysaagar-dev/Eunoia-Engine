#include "UISystem/UISystem.h"
#include <iostream>

#if __has_include(<imgui.h>)
#include <imgui.h>
#define EUNOIA_HAS_IMGUI 1
#else
#define EUNOIA_HAS_IMGUI 0
#endif

namespace EunoiaUI {

UISystemManager& UISystemManager::Get() {
    static UISystemManager instance;
    return instance;
}

void UISystemManager::Add(UserInterface* ui) {
    if (!ui) return;
    auto it = std::find(m_activeUIs.begin(), m_activeUIs.end(), ui);
    if (it == m_activeUIs.end()) {
        m_activeUIs.push_back(ui);
    }
}

void UISystemManager::Remove(UserInterface* ui) {
    if (!ui) return;
    auto it = std::find(m_activeUIs.begin(), m_activeUIs.end(), ui);
    if (it != m_activeUIs.end()) {
        m_activeUIs.erase(it);
    }
}

void UISystemManager::RemoveAll() {
    m_activeUIs.clear();
}

bool UISystemManager::IsActive(UserInterface* ui) const {
    if (!ui) return false;
    return std::find(m_activeUIs.begin(), m_activeUIs.end(), ui) != m_activeUIs.end();
}

UserInterface* UISystemManager::GetOrLoadUI(const std::string& path) {
    if (path.empty()) return nullptr;

    auto it = m_loadedUIs.find(path);
    if (it != m_loadedUIs.end()) {
        return it->second.get();
    }

    auto ui = std::make_shared<UserInterface>();
    if (ui->LoadFromFile(path)) {
        UserInterface* ptr = ui.get();
        m_loadedUIs[path] = ui;
        return ptr;
    }

    return nullptr;
}

UserInterface* UISystemManager::CreateNewUI(const std::string& name, const std::string& path) {
    auto ui = std::make_shared<UserInterface>(name);
    ui->filePath = path;

    // Create default border and text elements
    UIElement* bg = ui->AddElement(UIElementType::Border, "Background");
    bg->position = glm::vec2(100.0f, 100.0f);
    bg->size = glm::vec2(300.0f, 100.0f);
    bg->backgroundColor = glm::vec4(0.10f, 0.10f, 0.12f, 0.85f);
    bg->borderColor = glm::vec4(0.25f, 0.60f, 1.0f, 1.0f);
    bg->borderThickness = 2.0f;
    bg->cornerRounding = 8.0f;

    UIElement* txt = ui->AddElement(UIElementType::Text, "TitleText");
    txt->position = glm::vec2(120.0f, 130.0f);
    txt->text = name;
    txt->fontSize = 22.0f;
    txt->textColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

    ui->SaveToFile(path);
    UserInterface* ptr = ui.get();
    m_loadedUIs[path] = ui;
    return ptr;
}

void UISystemManager::RenderUI(void* imDrawListPtr, const glm::vec2& screenMin, const glm::vec2& screenMax, const UserInterface& ui) {
#if EUNOIA_HAS_IMGUI
    ImDrawList* drawList = reinterpret_cast<ImDrawList*>(imDrawListPtr);
    if (!drawList) return;

    glm::vec2 targetSize = screenMax - screenMin;
    if (targetSize.x <= 1.0f || targetSize.y <= 1.0f) return;

    float scaleX = targetSize.x / ui.canvasResolution.x;
    float scaleY = targetSize.y / ui.canvasResolution.y;
    // Uniform scale preserving aspect ratio
    float scale = std::min(scaleX, scaleY);
    if (scale <= 0.0f) scale = 1.0f;

    // Center scaled design canvas in viewport
    glm::vec2 canvasOrigin = screenMin + (targetSize - ui.canvasResolution * scale) * 0.5f;

    for (const auto& el : ui.elements) {
        if (!el.visible) continue;

        glm::vec2 anchorPos = canvasOrigin + el.anchor * ui.canvasResolution * scale;
        glm::vec2 elemPos = anchorPos + el.position * scale - el.pivot * el.size * scale;
        glm::vec2 elemSize = el.size * scale;
        glm::vec2 elemEnd = elemPos + elemSize;

        ImVec2 pMin(elemPos.x, elemPos.y);
        ImVec2 pMax(elemEnd.x, elemEnd.y);

        if (el.type == UIElementType::Border) {
            ImU32 bgCol = IM_COL32((int)(el.backgroundColor.r * 255), (int)(el.backgroundColor.g * 255), (int)(el.backgroundColor.b * 255), (int)(el.backgroundColor.a * 255));
            ImU32 bdCol = IM_COL32((int)(el.borderColor.r * 255), (int)(el.borderColor.g * 255), (int)(el.borderColor.b * 255), (int)(el.borderColor.a * 255));
            if (el.filled) {
                drawList->AddRectFilled(pMin, pMax, bgCol, el.cornerRounding * scale);
            }
            if (el.borderThickness > 0.0f) {
                drawList->AddRect(pMin, pMax, bdCol, el.cornerRounding * scale, 0, el.borderThickness * scale);
            }
        } else if (el.type == UIElementType::Text) {
            ImU32 txtCol = IM_COL32((int)(el.textColor.r * 255), (int)(el.textColor.g * 255), (int)(el.textColor.b * 255), (int)(el.textColor.a * 255));
            ImU32 shdCol = IM_COL32((int)(el.shadowColor.r * 255), (int)(el.shadowColor.g * 255), (int)(el.shadowColor.b * 255), (int)(el.shadowColor.a * 255));

            if (el.shadow) {
                ImVec2 shdPos(elemPos.x + el.shadowOffset.x * scale, elemPos.y + el.shadowOffset.y * scale);
                drawList->AddText(nullptr, el.fontSize * scale, shdPos, shdCol, el.text.c_str());
            }
            drawList->AddText(nullptr, el.fontSize * scale, pMin, txtCol, el.text.c_str());
        }
    }
#endif
}

void UISystemManager::RenderActiveUIs(const glm::vec2& viewportPos, const glm::vec2& viewportSize) {
#if EUNOIA_HAS_IMGUI
    if (m_activeUIs.empty()) return;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (!drawList) return;

    glm::vec2 screenMin = viewportPos;
    glm::vec2 screenMax = viewportPos + viewportSize;

    drawList->PushClipRect(ImVec2(screenMin.x, screenMin.y), ImVec2(screenMax.x, screenMax.y), true);
    for (UserInterface* ui : m_activeUIs) {
        if (ui) {
            RenderUI(drawList, screenMin, screenMax, *ui);
        }
    }
    drawList->PopClipRect();
#endif
}

} // namespace EunoiaUI
