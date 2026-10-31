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

UISystemManager::UISystemManager() {
}

UISystemManager& UISystemManager::Get() {
    static UISystemManager instance;
    return instance;
}

uint64_t UISystemManager::LoadTexture(const std::string& path, int* outWidth, int* outHeight) {
    if (path.empty() || !m_textureLoader) return 0;
    return m_textureLoader(path, outWidth, outHeight);
}

uint64_t UISystemManager::ResolveTextureId(const std::string& rawPath, const std::string& baseDir, int* outWidth, int* outHeight) {
    if (rawPath.empty()) return 0;

    std::string clean = rawPath;
    size_t first = clean.find_first_not_of(" \t\r\n\"'");
    size_t last = clean.find_last_not_of(" \t\r\n\"'");
    if (first == std::string::npos) return 0;
    clean = clean.substr(first, (last - first + 1));

    if (clean.rfind("file:///", 0) == 0) {
        clean = clean.substr(8);
    } else if (clean.rfind("file://", 0) == 0) {
        clean = clean.substr(7);
    }

    for (char& c : clean) {
        if (c == '\\') c = '/';
    }

    std::error_code ec;

    // 1. Try relative to baseDir
    if (!baseDir.empty()) {
        std::filesystem::path p = std::filesystem::path(baseDir) / clean;
        if (std::filesystem::exists(p, ec)) {
            uint64_t id = LoadTexture(p.lexically_normal().string(), outWidth, outHeight);
            if (id != 0) return id;
        }
    }

    // 2. Try direct filesystem path
    if (std::filesystem::exists(clean, ec)) {
        uint64_t id = LoadTexture(std::filesystem::path(clean).lexically_normal().string(), outWidth, outHeight);
        if (id != 0) return id;
    }

    // 3. Try passing directly to loader
    uint64_t id = LoadTexture(clean, outWidth, outHeight);
    if (id != 0) return id;

    // 4. Try under Content/
    if (clean.rfind("Content/", 0) != 0 && clean.rfind("content/", 0) != 0) {
        std::string withContent = "Content/" + clean;
        if (std::filesystem::exists(withContent, ec)) {
            uint64_t cid = LoadTexture(withContent, outWidth, outHeight);
            if (cid != 0) return cid;
        }
    }

    return 0;
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

    if (targetObj) {
        for (auto& b : targetObj->behaviours) {
            if (!b) continue;
            if (!binding.behaviourClass.empty() && b->GetClassName() != binding.behaviourClass) continue;
            std::string funcName = binding.targetFunction;
            size_t pPos = funcName.find('(');
            if (pPos != std::string::npos) funcName = funcName.substr(0, pPos);
            size_t nb = funcName.find_first_not_of(" \t\r\n");
            size_t ne = funcName.find_last_not_of(" \t\r\n");
            if (nb != std::string::npos) funcName = funcName.substr(nb, ne - nb + 1);

            if (funcName == "Start") {
                b->Start();
            } else if (funcName == "Update") {
                b->Update(0.016f);
            } else {
                b->InvokeFunction(funcName);
            }
            return;
        }
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

    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !std::filesystem::is_directory(path, ec)) {
        auto ui = std::make_shared<UserInterface>();
        if (ui->LoadFromFile(path)) {
            UserInterface* ptr = ui.get();
            m_loadedUIs[path] = ui;
            return ptr;
        }
    }

    // Try appending .eui if missing
    if (path.rfind(".eui") == std::string::npos) {
        std::string withEui = path + ".eui";
        if (std::filesystem::exists(withEui, ec)) {
            auto ui = std::make_shared<UserInterface>();
            if (ui->LoadFromFile(withEui)) {
                UserInterface* ptr = ui.get();
                m_loadedUIs[path] = ui;
                m_loadedUIs[withEui] = ui;
                return ptr;
            }
        }
    }

    // Check loaded UIs by name
    for (const auto& pair : m_loadedUIs) {
        if (pair.second && (pair.second->name == path || pair.first.find(path) != std::string::npos)) {
            return pair.second.get();
        }
    }

    // Check common content directories
    if (path.find('/') == std::string::npos && path.find('\\') == std::string::npos) {
        static const std::vector<std::string> prefixes = {
            "Content/", "Content/UI/", "Content/UI/UI_", "Content/UI_", "Data/Content/", "Data/Content/UI/"
        };
        for (const auto& pre : prefixes) {
            UserInterface* u = GetOrLoadUI(pre + path);
            if (u) {
                m_loadedUIs[path] = m_loadedUIs[pre + path];
                return u;
            }
            if (!path.starts_with("UI_")) {
                u = GetOrLoadUI(pre + "UI_" + path);
                if (u) {
                    m_loadedUIs[path] = m_loadedUIs[pre + "UI_" + path];
                    return u;
                }
            }
        }
    }

    return nullptr;
}

UserInterface* UISystemManager::CreateNewUI(const std::string& name, const std::string& path) {
    auto ui = std::make_shared<UserInterface>(name);
    ui->filePath = path;

    // Create a starter Atlas panel & heading
    ui->AddElement(UIElementType::Panel, "MainPanel");

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

    float scaleX = targetSize.x / ui.canvasResolution.x;
    float scaleY = targetSize.y / ui.canvasResolution.y;
    float scale = std::min(scaleX, scaleY);
    if (scale <= 0.0f) scale = 1.0f;

    glm::vec2 canvasOrigin = screenMin + (targetSize - ui.canvasResolution * scale) * 0.5f;

    // Mouse input processing
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

    // Render native UI elements in ascending Z-order
    std::vector<const UIElement*> sortedElements;
    for (const auto& el : ui.elements) {
        if (el.visible) sortedElements.push_back(&el);
    }
    std::sort(sortedElements.begin(), sortedElements.end(), [](const UIElement* a, const UIElement* b) {
        return a->zOrder < b->zOrder;
    });

    for (const UIElement* pEl : sortedElements) {
        const auto& el = *pEl;
        glm::vec4 worldRect = ui.GetElementWorldRect(el);
        glm::vec2 elemPos = canvasOrigin + glm::vec2(worldRect.x, worldRect.y) * scale;
        glm::vec2 elemSize = glm::vec2(worldRect.z, worldRect.w) * scale;
        glm::vec2 elemEnd = elemPos + elemSize;

        ImVec2 pMin(elemPos.x, elemPos.y);
        ImVec2 pMax(elemEnd.x, elemEnd.y);

        bool isHovered = (mousePos.x >= elemPos.x && mousePos.x <= elemEnd.x &&
                          mousePos.y >= elemPos.y && mousePos.y <= elemEnd.y);
        bool isPressed = isHovered && isMouseDown;

        // 1. Drop Shadow
        if (el.hasShadow) {
            ImVec2 sMin(pMin.x + el.shadowOffset.x * scale - el.shadowSpread * scale * 0.5f,
                        pMin.y + el.shadowOffset.y * scale - el.shadowSpread * scale * 0.5f);
            ImVec2 sMax(pMax.x + el.shadowOffset.x * scale + el.shadowSpread * scale * 0.5f,
                        pMax.y + el.shadowOffset.y * scale + el.shadowSpread * scale * 0.5f);
            ImU32 sCol = IM_COL32((int)(el.shadowColor.r * 255), (int)(el.shadowColor.g * 255),
                                  (int)(el.shadowColor.b * 255), (int)(el.shadowColor.a * 255 * el.opacity));
            drawList->AddRectFilled(sMin, sMax, sCol, (el.cornerRounding + el.shadowSpread * 0.5f) * scale);
        }

        // 2. Component-Specific Rendering
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
                                   (int)(curBg.g * 255), (int)(curBg.a * 255 * el.opacity));
            ImU32 bdCol = IM_COL32((int)(el.borderColor.r * 255), (int)(el.borderColor.g * 255),
                                   (int)(el.borderColor.b * 255), (int)(el.borderColor.a * 255 * el.opacity));
            drawList->AddRectFilled(pMin, pMax, bgCol, el.cornerRounding * scale);
            if (el.borderThickness > 0.0f) {
                drawList->AddRect(pMin, pMax, bdCol, el.cornerRounding * scale, 0, el.borderThickness * scale);
            }

            // Button Label
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
            // Track
            ImU32 trkCol = IM_COL32((int)(el.progressTrackColor.r * 255), (int)(el.progressTrackColor.g * 255),
                                    (int)(el.progressTrackColor.b * 255), (int)(el.progressTrackColor.a * 255 * el.opacity));
            ImU32 filCol = IM_COL32((int)(el.progressFillColor.r * 255), (int)(el.progressFillColor.g * 255),
                                    (int)(el.progressFillColor.b * 255), (int)(el.progressFillColor.a * 255 * el.opacity));
            drawList->AddRectFilled(pMin, pMax, trkCol, el.cornerRounding * scale);

            // Fill
            float frac = (el.progressMax > el.progressMin) ?
                std::clamp((el.progressValue - el.progressMin) / (el.progressMax - el.progressMin), 0.0f, 1.0f) : 0.0f;
            ImVec2 fillMax(pMin.x + elemSize.x * frac, pMax.y);
            if (frac > 0.01f) {
                drawList->AddRectFilled(pMin, fillMax, filCol, el.cornerRounding * scale);
            }

            // Outline
            if (el.borderThickness > 0.0f) {
                ImU32 bdCol = IM_COL32((int)(el.borderColor.r * 255), (int)(el.borderColor.g * 255),
                                       (int)(el.borderColor.b * 255), (int)(el.borderColor.a * 255 * el.opacity));
                drawList->AddRect(pMin, pMax, bdCol, el.cornerRounding * scale, 0, el.borderThickness * scale);
            }

            // Percentage Label
            if (el.showPercentage) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d%%", (int)(frac * 100.0f));
                ImVec2 tSize = ImGui::CalcTextSize(buf);
                ImVec2 tPos(pMin.x + (elemSize.x - tSize.x) * 0.5f, pMin.y + (elemSize.y - tSize.y) * 0.5f);
                drawList->AddText(nullptr, 12.0f * scale, tPos, IM_COL32(240, 240, 245, 240), buf);
            }
        } else if (el.type == UIElementType::Slider) {
            // Track Bar
            float barH = 6.0f * scale;
            float barY = pMin.y + (elemSize.y - barH) * 0.5f;
            ImVec2 tMin(pMin.x, barY);
            ImVec2 tMax(pMax.x, barY + barH);
            ImU32 trkCol = IM_COL32((int)(el.sliderTrackColor.r * 255), (int)(el.sliderTrackColor.g * 255),
                                    (int)(el.sliderTrackColor.b * 255), (int)(el.sliderTrackColor.a * 255 * el.opacity));
            drawList->AddRectFilled(tMin, tMax, trkCol, 3.0f * scale);

            // Filled portion
            float frac = (el.sliderMax > el.sliderMin) ?
                std::clamp((el.sliderValue - el.sliderMin) / (el.sliderMax - el.sliderMin), 0.0f, 1.0f) : 0.0f;
            ImVec2 fMax(pMin.x + elemSize.x * frac, barY + barH);
            ImU32 thCol = IM_COL32((int)(el.sliderThumbColor.r * 255), (int)(el.sliderThumbColor.g * 255),
                                   (int)(el.sliderThumbColor.b * 255), (int)(el.sliderThumbColor.a * 255 * el.opacity));
            drawList->AddRectFilled(tMin, fMax, thCol, 3.0f * scale);

            // Thumb
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
                // Checkmark
                float pad = 4.0f * scale;
                ImVec2 p1(bMin.x + pad, bMin.y + boxSize * 0.5f);
                ImVec2 p2(bMin.x + boxSize * 0.40f, bMax.y - pad);
                ImVec2 p3(bMax.x - pad, bMin.y + pad);
                drawList->AddLine(p1, p2, chkCol, 2.5f * scale);
                drawList->AddLine(p2, p3, chkCol, 2.5f * scale);
            }

            // Checkbox Label
            if (!el.text.empty()) {
                ImU32 txtCol = IM_COL32((int)(el.textColor.r * 255), (int)(el.textColor.g * 255),
                                        (int)(el.textColor.b * 255), (int)(el.textColor.a * 255 * el.opacity));
                ImVec2 tPos(bMax.x + 8.0f * scale, pMin.y + (elemSize.y - ImGui::CalcTextSize(el.text.c_str()).y) * 0.5f);
                drawList->AddText(nullptr, el.fontSize * scale, tPos, txtCol, el.text.c_str());
            }
        } else if (el.type == UIElementType::Image) {
            uint64_t texId = 0;
            if (!el.texturePath.empty()) {
                texId = ResolveTextureId(el.texturePath);
            }
            if (texId != 0) {
                ImU32 tintCol = IM_COL32((int)(el.imageTint.r * 255), (int)(el.imageTint.g * 255),
                                         (int)(el.imageTint.b * 255), (int)(el.imageTint.a * 255 * el.opacity));
                if (el.cornerRounding > 0.0f) {
                    drawList->AddImageRounded((ImTextureID)texId, pMin, pMax, ImVec2(0, 0), ImVec2(1, 1), tintCol, el.cornerRounding * scale);
                } else {
                    drawList->AddImage((ImTextureID)texId, pMin, pMax, ImVec2(0, 0), ImVec2(1, 1), tintCol);
                }
            } else {
                // Placeholder image quad
                drawList->AddRectFilled(pMin, pMax, IM_COL32(35, 38, 48, 200), el.cornerRounding * scale);
                drawList->AddRect(pMin, pMax, IM_COL32(74, 124, 255, 120), el.cornerRounding * scale, 0, 1.0f);
                drawList->AddText(nullptr, 12.0f * scale, ImVec2(pMin.x + 6.0f, pMin.y + 6.0f), IM_COL32(180, 185, 200, 200), "Image");
            }
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
