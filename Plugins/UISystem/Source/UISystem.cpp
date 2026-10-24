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
    HtmlNode::SetTextureResolver([this](const std::string& path, const std::string& baseDir, int* outW, int* outH) -> uint64_t {
        return this->ResolveTextureId(path, baseDir, outW, outH);
    });
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

    // 1. Try relative to baseDir (HTML directory)
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

    // 3. Try passing directly to loader (which queries TextureManager / AssetRegistry)
    uint64_t id = LoadTexture(clean, outWidth, outHeight);
    if (id != 0) return id;

    // 4. Try under Content/ if not already prefixed
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

    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !std::filesystem::is_directory(path, ec)) {
        auto ui = std::make_shared<UserInterface>();
        bool loaded = false;
        std::string lowerPath = path;
        for (char& c : lowerPath) c = (char)tolower(c);
        if (lowerPath.ends_with(".html") || lowerPath.ends_with(".htm")) {
            loaded = ui->LoadFromHtml(path);
        } else {
            loaded = ui->LoadFromFile(path);
            if (!loaded) {
                loaded = ui->LoadFromHtml(path);
            }
        }
        if (loaded) {
            UserInterface* ptr = ui.get();
            m_loadedUIs[path] = ui;
            return ptr;
        }
    }

    // Try appending .html or .eui if missing
    if (path.rfind(".eui") == std::string::npos && path.rfind(".html") == std::string::npos && path.rfind(".htm") == std::string::npos) {
        std::string withHtml = path + ".html";
        if (std::filesystem::exists(withHtml, ec)) {
            auto ui = std::make_shared<UserInterface>();
            if (ui->LoadFromHtml(withHtml)) {
                UserInterface* ptr = ui.get();
                m_loadedUIs[path] = ui;
                m_loadedUIs[withHtml] = ui;
                return ptr;
            }
        }
        std::string withEui = path + ".eui";
        if (std::filesystem::exists(withEui, ec)) {
            auto ui = std::make_shared<UserInterface>();
            if (ui->LoadFromFile(withEui) || ui->LoadFromHtml(withEui)) {
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

    // If not found and path is a simple name (no slashes), check common content directories
    if (path.find('/') == std::string::npos && path.find('\\') == std::string::npos) {
        static const std::vector<std::string> prefixes = {
            "Content/", "Content/UI/", "Content/UI/UI_", "Content/UI_", "Data/Content/", "Data/Content/UI/", "Data/Content/UI/UI_"
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
        } else if (el.type == UIElementType::Image) {
            uint64_t texId = 0;
            if (!el.texturePath.empty()) {
                texId = ResolveTextureId(el.texturePath, ui.GetHtmlDirectory());
            }
            if (texId != 0) {
                ImU32 tintCol = IM_COL32((int)(el.backgroundColor.r * 255), (int)(el.backgroundColor.g * 255), (int)(el.backgroundColor.b * 255), (int)(el.backgroundColor.a * 255));
                if (el.backgroundColor.a <= 0.0f) {
                    tintCol = IM_COL32(255, 255, 255, 255);
                }
                if (el.cornerRounding > 0.0f) {
                    drawList->AddImageRounded((ImTextureID)texId, pMin, pMax, ImVec2(0, 0), ImVec2(1, 1), tintCol, el.cornerRounding * scale);
                } else {
                    drawList->AddImage((ImTextureID)texId, pMin, pMax, ImVec2(0, 0), ImVec2(1, 1), tintCol);
                }
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
