#include "UISystem/UISystem.h"
#include <iostream>

#if __has_include(<imgui.h>)
#include <imgui.h>
#define EUNOIA_HAS_IMGUI 1
#else
#define EUNOIA_HAS_IMGUI 0
#endif

#if __has_include(<EngineScene/Scene.h>)
#include <EngineScene/Scene.h>
#include <EngineScene/EunoiaBehaviour.h>
#include <EngineScene/BehaviourRegistry.h>
#include <EngineCore/Log.h>
#define EUNOIA_HAS_SCENE 1
#else
#define EUNOIA_HAS_SCENE 0
#endif

namespace EunoiaUI {

UISystemManager& UISystemManager::Get() {
    static UISystemManager instance;
    return instance;
}

void UISystemManager::DispatchEvent(const UIEventBinding& binding) {
#if EUNOIA_HAS_SCENE
    if (!m_currentScene || binding.targetFunction.empty()) return;
    Scene* scene = reinterpret_cast<Scene*>(m_currentScene);

    GameObject* targetObj = nullptr;
    if (binding.targetActorId >= 0) {
        for (auto& obj : scene->objects) {
            if (obj.id == binding.targetActorId) {
                targetObj = &obj;
                break;
            }
        }
    }
    if (!targetObj && !binding.targetActorName.empty()) {
        for (auto& obj : scene->objects) {
            if (obj.name == binding.targetActorName) {
                targetObj = &obj;
                break;
            }
        }
    }

    auto executeOnObj = [&](GameObject& obj) -> bool {
        for (auto& b : obj.behaviours) {
            if (!b) continue;
            if (!binding.behaviourClass.empty() && b->GetClassName() != binding.behaviourClass) continue;

            std::string fn = binding.targetFunction;
            size_t p = fn.find('(');
            std::string cleanFn = (p != std::string::npos) ? fn.substr(0, p) : fn;
            size_t nb = cleanFn.find_first_not_of(" \t\r\n");
            size_t ne = cleanFn.find_last_not_of(" \t\r\n");
            if (nb != std::string::npos) cleanFn = cleanFn.substr(nb, ne - nb + 1);

            if (cleanFn == "Start") {
                b->Start();
            } else if (cleanFn == "Update") {
                b->Update(0.016f);
            } else {
                b->InvokeFunction(cleanFn);
            }

            std::string logMsg = "UI Event [" + binding.eventName + "] triggered on " + obj.name + " (" + b->GetClassName() + ")::" + cleanFn;
            AddEngineLog("LogUI", logMsg.c_str(), 2);
            return true;
        }
        return false;
    };

    if (targetObj) {
        if (executeOnObj(*targetObj)) return;
    }

    for (auto& obj : scene->objects) {
        if (executeOnObj(obj)) return;
    }
#endif
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
    ui->canvasResolution = glm::vec2(1920.0f, 1080.0f);
    ui->elements.clear();

    // Create the HTML file with modern CSS layout
    std::string hPath = path;
    size_t dotPos = hPath.rfind('.');
    if (dotPos != std::string::npos) {
        hPath = hPath.substr(0, dotPos) + ".html";
    } else {
        hPath += ".html";
    }
    ui->htmlPath = hPath;
    ui->htmlSource = HtmlCssParser::GetDefaultHtml(name);
    ui->SaveToHtml(hPath);

    ui->SaveToFile(path);
    UserInterface* ptr = ui.get();
    m_loadedUIs[path] = ui;
    return ptr;
}

void UISystemManager::RenderUI(void* imDrawListPtr, const glm::vec2& screenMin, const glm::vec2& screenMax, const UserInterface& ui) {
#if EUNOIA_HAS_IMGUI
    if (!ui.isVisible) return;

    ImDrawList* drawList = reinterpret_cast<ImDrawList*>(imDrawListPtr);
    if (!drawList) return;

    glm::vec2 targetSize = screenMax - screenMin;
    if (targetSize.x <= 1.0f || targetSize.y <= 1.0f) return;

    // Check for realtime updates from HTML file on disk
    const_cast<UserInterface&>(ui).CheckAndReloadHtml();

    float scaleX = targetSize.x / ui.canvasResolution.x;
    float scaleY = targetSize.y / ui.canvasResolution.y;
    // Uniform scale preserving aspect ratio
    float scale = std::min(scaleX, scaleY);
    if (scale <= 0.0f) scale = 1.0f;

    // Center design canvas in viewport
    glm::vec2 canvasOrigin = screenMin + (targetSize - ui.canvasResolution * scale) * 0.5f;

    // Process mouse interactivity (hover, click, scroll)
    ImGuiIO& io = ImGui::GetIO();
    glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
    bool isMouseDown = io.MouseDown[0];
    bool isMousePressed = io.MouseClicked[0];
    bool isMouseReleased = io.MouseReleased[0];
    float mouseWheel = io.MouseWheel;
    auto eventDispatcher = [this](const UIEventBinding& binding) {
        this->DispatchEvent(binding);
    };
    const_cast<UserInterface&>(ui).ProcessInput(mousePos, isMouseDown, isMousePressed, isMouseReleased, mouseWheel, canvasOrigin, scale, eventDispatcher);

    // 1. Render HTML + CSS structure
    if (ui.htmlRoot) {
        ui.RenderHtml(drawList, canvasOrigin, scale);
    }

    // 2. Render native elements if present
    for (const auto& el : ui.elements) {
        if (!el.visible) continue;

        glm::vec4 worldRect = ui.GetElementWorldRect(el);
        glm::vec2 elemPos = canvasOrigin + glm::vec2(worldRect.x, worldRect.y) * scale;
        glm::vec2 elemSize = glm::vec2(worldRect.z, worldRect.w) * scale;
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

            float fontSize = el.fontSize * scale;
            ImVec2 textSize = ImGui::CalcTextSize(el.text.c_str());
            ImVec2 textPos = pMin;

            if (el.textAlignment == 1) { // Center
                textPos.x = pMin.x + (elemSize.x - textSize.x) * 0.5f;
            } else if (el.textAlignment == 2) { // Right
                textPos.x = pMax.x - textSize.x;
            }

            if (el.shadow) {
                ImVec2 shdPos(textPos.x + el.shadowOffset.x * scale, textPos.y + el.shadowOffset.y * scale);
                drawList->AddText(nullptr, fontSize, shdPos, shdCol, el.text.c_str());
            }
            drawList->AddText(nullptr, fontSize, textPos, txtCol, el.text.c_str());
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
