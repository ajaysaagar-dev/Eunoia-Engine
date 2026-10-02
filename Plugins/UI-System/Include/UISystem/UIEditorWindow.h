#pragma once

#include "UserInterface.h"
#include "UISystem.h"
#include "HtmlCssParser.h"
#include <imgui.h>
#include <imgui_internal.h>

#if __has_include(<EngineScene/Scene.h>)
#include <EngineScene/Scene.h>
#include <EngineScene/BehaviourRegistry.h>
#else
class Scene;
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace EunoiaUI {

inline void LaunchVSCodeSilently(const std::filesystem::path& targetFile, const std::filesystem::path& targetWorkspace = {}) {
    std::filesystem::path workspace = targetWorkspace;
    if (workspace.empty()) {
        workspace = targetFile.parent_path();
        while (!workspace.empty() && workspace != workspace.root_path()) {
            if (std::filesystem::exists(workspace / ".vscode") || std::filesystem::exists(workspace / "Content") || std::filesystem::exists(workspace / "CMakeLists.txt")) {
                break;
            }
            workspace = workspace.parent_path();
        }
        if (workspace.empty() || workspace == workspace.root_path()) {
            workspace = targetFile.parent_path();
        }
    }

#ifdef _WIN32
    std::vector<std::filesystem::path> candidateCodePaths;
    const char* localApp = getenv("LOCALAPPDATA");
    if (localApp) {
        candidateCodePaths.push_back(std::filesystem::path(localApp) / "Programs" / "Microsoft VS Code" / "Code.exe");
        candidateCodePaths.push_back(std::filesystem::path(localApp) / "Programs" / "Microsoft VS Code" / "bin" / "code.cmd");
    }
    const char* progFiles = getenv("ProgramFiles");
    if (progFiles) {
        candidateCodePaths.push_back(std::filesystem::path(progFiles) / "Microsoft VS Code" / "Code.exe");
        candidateCodePaths.push_back(std::filesystem::path(progFiles) / "Microsoft VS Code" / "bin" / "code.cmd");
    }
    const char* progFilesX86 = getenv("ProgramFiles(x86)");
    if (progFilesX86) {
        candidateCodePaths.push_back(std::filesystem::path(progFilesX86) / "Microsoft VS Code" / "Code.exe");
        candidateCodePaths.push_back(std::filesystem::path(progFilesX86) / "Microsoft VS Code" / "bin" / "code.cmd");
    }
    const char* userProfile = getenv("USERPROFILE");
    if (userProfile) {
        candidateCodePaths.push_back(std::filesystem::path(userProfile) / "AppData" / "Local" / "Programs" / "Microsoft VS Code" / "Code.exe");
    }

    bool launched = false;
    for (const auto& cp : candidateCodePaths) {
        std::error_code ec;
        if (std::filesystem::exists(cp, ec)) {
            std::string cmdLine = "\"" + cp.string() + "\" \"" + workspace.string() + "\" \"" + targetFile.string() + "\"";
            STARTUPINFOA si = { sizeof(si) };
            PROCESS_INFORMATION pi = { 0 };
            DWORD creationFlags = CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS;
            std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
            cmdBuf.push_back('\0');

            if (CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, FALSE, creationFlags, NULL, workspace.string().c_str(), &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                launched = true;
                break;
            }
        }
    }

    if (!launched) {
        // Fallback using cmd start to create a fully detached process
        std::string cmdLine = "cmd.exe /c start \"\" code \"" + workspace.string() + "\" \"" + targetFile.string() + "\"";
        STARTUPINFOA si = { sizeof(si) };
        si.dwFlags |= STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = { 0 };
        DWORD creationFlags = CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS;
        std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
        cmdBuf.push_back('\0');

        if (CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, FALSE, creationFlags, NULL, workspace.string().c_str(), &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            launched = true;
        }
    }

    if (!launched) {
        ShellExecuteA(NULL, "open", targetFile.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
#else
    std::string cmd = "code \"" + workspace.string() + "\" \"" + targetFile.string() + "\" &";
    system(cmd.c_str());
#endif
}

// ============================================================================
// C++ Script Method Extractor
// Scans class definition in source files and reflects public callable methods
// ============================================================================
inline std::vector<std::string> ExtractScriptFunctions(const std::string& scriptPath, const std::string& className = "") {
    std::vector<std::string> functions;

    auto addFunc = [&](const std::string& fn) {
        if (fn.empty()) return;
        if (std::find(functions.begin(), functions.end(), fn) == functions.end()) {
            functions.push_back(fn);
        }
    };

    if (!scriptPath.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(scriptPath, ec)) {
            std::ifstream file(scriptPath);
            if (file.is_open()) {
                std::string line;
                bool inClass = false;
                bool inPublic = false;
                int braceDepth = 0;
                bool classBraceStarted = false;

                while (std::getline(file, line)) {
                    // Strip single-line comments
                    size_t cPos = line.find("//");
                    if (cPos != std::string::npos) line = line.substr(0, cPos);

                    size_t start = line.find_first_not_of(" \t\r\n");
                    if (start == std::string::npos) continue;
                    std::string l = line.substr(start);

                    // Detect class / struct
                    if (!inClass) {
                        if (l.find("class ") != std::string::npos || l.find("struct ") != std::string::npos) {
                            if (className.empty() || l.find(className) != std::string::npos) {
                                inClass = true;
                                classBraceStarted = (l.find('{') != std::string::npos);
                                inPublic = (l.find("struct ") != std::string::npos);
                            }
                        }
                    }

                    int openBraces = 0, closeBraces = 0;
                    bool inStr = false;
                    for (char c : l) {
                        if (c == '"') inStr = !inStr;
                        if (!inStr) {
                            if (c == '{') openBraces++;
                            else if (c == '}') closeBraces++;
                        }
                    }

                    if (inClass && openBraces > 0) classBraceStarted = true;

                    if (inClass && (braceDepth == 1 || (braceDepth == 0 && openBraces > 0))) {
                        if (l.rfind("public:", 0) == 0 || l == "public:") {
                            inPublic = true;
                            braceDepth += openBraces - closeBraces;
                            continue;
                        } else if (l.rfind("private:", 0) == 0 || l.rfind("protected:", 0) == 0) {
                            inPublic = false;
                            braceDepth += openBraces - closeBraces;
                            continue;
                        }
                    }

                    bool isClassPublic = (inClass && inPublic && (braceDepth == 1 || (braceDepth == 0 && openBraces > 0)));
                    braceDepth += openBraces - closeBraces;
                    if (classBraceStarted && braceDepth <= 0) {
                        braceDepth = 0;
                        inClass = false;
                        inPublic = false;
                        classBraceStarted = false;
                    }

                    if (!isClassPublic) continue;

                    // Match function signatures
                    if (l.find('~') != std::string::npos) continue; // Destructor
                    if (l.find("REGISTER_BEHAVIOUR") != std::string::npos) continue;
                    if (l.find("operator") != std::string::npos) continue;

                    size_t openParen = l.find('(');
                    size_t closeParen = l.find(')', openParen);
                    if (openParen != std::string::npos && closeParen != std::string::npos) {
                        std::string beforeParen = l.substr(0, openParen);
                        size_t bpEnd = beforeParen.find_last_not_of(" \t*&");
                        if (bpEnd == std::string::npos) continue;
                        beforeParen = beforeParen.substr(0, bpEnd + 1);
                        size_t bpStart = beforeParen.find_last_of(" \t*&");
                        std::string funcName = (bpStart != std::string::npos) ? beforeParen.substr(bpStart + 1) : beforeParen;

                        // Check keywords to ignore
                        if (funcName == "if" || funcName == "for" || funcName == "while" ||
                            funcName == "switch" || funcName == "catch" || funcName == "return" ||
                            funcName == "sizeof" || funcName == "decltype" || funcName == className) {
                            continue;
                        }

                        if (!funcName.empty()) {
                            addFunc(funcName + "()");
                        }
                    }
                }
            }
        }
    }

    // Default common callbacks & lifecycle methods
    addFunc("Start()");
    addFunc("Update()");
    addFunc("OnClick()");
    addFunc("OnHover()");
    addFunc("OnMouseEnter()");
    addFunc("OnMouseLeave()");
    addFunc("OnButtonPress()");
    addFunc("Execute()");
    addFunc("ToggleActive()");

    return functions;
}

#if __has_include(<EngineScene/Scene.h>)
inline void ExecuteScriptFunction(Scene* scene, const UIEventBinding& binding) {
    if (!scene || binding.targetFunction.empty()) return;

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
}
#endif

// ============================================================================
// UI EDITOR WINDOW CLASS
// ============================================================================
class UIEditorWindow {
public:
    UserInterface ui;
    std::string filePath;
    std::string name;
    bool isOpen = true;
    bool isDirty = false;
    bool autoSave = true;

    // Canvas navigation
    float zoom = 0.85f;
    ImVec2 pan = { 0.0f, 0.0f };
    bool isPanning = false;
    ImVec2 panStart = { 0.0f, 0.0f };
    ImVec2 panInitial = { 0.0f, 0.0f };

    std::string lastReloadStatus = "Synced";

    // Element Selection on Canvas
    HtmlNode* selectedNode = nullptr;

    UIEditorWindow(const std::string& path, const std::string& inName = "") {
        filePath = path;
        if (!inName.empty()) {
            name = inName;
        } else if (!path.empty()) {
            name = std::filesystem::path(path).filename().string();
        } else {
            name = "New UI";
        }
        if (!path.empty()) {
            if (!ui.LoadFromFile(path)) {
                ui.name = name;
                ui.SaveToHtml();
                ui.SaveToFile(path);
            }
        } else {
            ui.name = name;
        }
    }

    void OpenHtmlInEditor() {
        std::string hPath = ui.GetHtmlPath();
        if (hPath.empty() && !filePath.empty()) {
            std::filesystem::path p = filePath;
            p.replace_extension(".html");
            hPath = p.string();
        }
        if (!hPath.empty()) {
            std::error_code ec;
            if (!std::filesystem::exists(hPath, ec)) {
                ui.SaveToHtml(hPath);
            }
            LaunchVSCodeSilently(hPath);
        }
    }

    void Save(bool syncHtml = true) {
        if (!filePath.empty()) {
            ui.SaveToFile(filePath, syncHtml);
            isDirty = false;
        }
    }

    void RenderUIEditorContent(Scene* scene = nullptr) {
        // Real-time file watcher check on the HTML file
        std::string selectedKey = selectedNode ? selectedNode->GetSelectorKey() : "";
        if (ui.CheckAndReloadHtml()) {
            lastReloadStatus = "Updated from HTML just now";
            if (!selectedKey.empty()) {
                selectedNode = ui.FindHtmlNodeBySelector(selectedKey);
            } else {
                selectedNode = nullptr;
            }
            if (autoSave) {
                Save(false); // Do not overwrite HTML file on disk that VS Code is editing
            }
        }

        // Top Control Header Bar
        RenderTopToolbar();

        // Workspace Split: Canvas on Left, Properties & Events Panel on Right
        float availW = ImGui::GetContentRegionAvail().x;
        float propPanelW = 350.0f;
        float canvasW = std::max(200.0f, availW - propPanelW);

        // Canvas Area
        RenderCanvasView(ImVec2(canvasW, 0), scene);

        ImGui::SameLine(0, 0);

        // Properties & Events Sidebar Panel
        RenderPropertiesPanel(ImVec2(propPanelW, 0), scene);
    }

    void RenderTopToolbar() {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 6.0f));

        if (ImGui::BeginChild("UIEditorHeaderBar", ImVec2(0, 48.0f), true, ImGuiWindowFlags_NoScrollbar)) {
            // Left: Asset Title & Status
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
            ImGui::TextColored(ImVec4(0.12f, 0.72f, 1.0f, 1.0f), "🎨 UI Canvas Designer:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", ui.name.c_str());

            ImGui::SameLine();
            float availW = ImGui::GetContentRegionAvail().x;
            float buttonsW = 560.0f;
            if (availW > buttonsW) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - buttonsW));
            }

            // [Edit UI] button (Cyan theme)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.02f, 0.50f, 0.75f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.08f, 0.65f, 0.95f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.01f, 0.40f, 0.60f, 1.0f));
            if (ImGui::Button("📝 Edit UI (VS Code)", ImVec2(160.0f, 28.0f))) {
                OpenHtmlInEditor();
            }
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Open corresponding HTML and CSS in VS Code.\nLive edits will immediately reflect on this canvas in real-time.");
            }

            ImGui::SameLine();

            // [Save] button
            if (ImGui::Button("💾 Save", ImVec2(80.0f, 28.0f))) {
                Save();
            }

            ImGui::SameLine();

            // Autosave checkbox
            ImGui::Checkbox("⚡ Autosave", &autoSave);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Automatically save UI asset whenever HTML file is updated.");
            }

            ImGui::SameLine();

            // Resolution Presets
            const char* resOptions[] = { "1920 x 1080 (FHD)", "1280 x 720 (HD)", "2560 x 1440 (2K)", "3840 x 2160 (4K)" };
            int currentResIdx = 0;
            if (ui.canvasResolution.x == 1280.0f) currentResIdx = 1;
            else if (ui.canvasResolution.x == 2560.0f) currentResIdx = 2;
            else if (ui.canvasResolution.x == 3840.0f) currentResIdx = 3;

            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::Combo("##ResSelect", &currentResIdx, resOptions, 4)) {
                if (currentResIdx == 0) ui.canvasResolution = { 1920.0f, 1080.0f };
                else if (currentResIdx == 1) ui.canvasResolution = { 1280.0f, 720.0f };
                else if (currentResIdx == 2) ui.canvasResolution = { 2560.0f, 1440.0f };
                else if (currentResIdx == 3) ui.canvasResolution = { 3840.0f, 2160.0f };
                if (ui.htmlRoot) {
                    ui.htmlRoot->ComputeLayout(glm::vec4(0.0f, 0.0f, ui.canvasResolution.x, ui.canvasResolution.y));
                }
                isDirty = true;
                if (autoSave) Save();
            }

            ImGui::SameLine();
            if (ImGui::Button("↺ Fit", ImVec2(40.0f, 28.0f))) {
                pan = { 0.0f, 0.0f };
                zoom = 0.85f;
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }

    void RenderCanvasView(const ImVec2& size, Scene* scene = nullptr) {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        if (ImGui::BeginChild("UICanvasMainArea", size, true, flags)) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 canvasAreaPos = ImGui::GetCursorScreenPos();
            ImVec2 canvasAreaSize = ImGui::GetContentRegionAvail();

            if (canvasAreaSize.x <= 10.0f || canvasAreaSize.y <= 10.0f) {
                ImGui::EndChild();
                return;
            }

            // Background Grid (Figma / UE5 style canvas)
            ImU32 bgCol = IM_COL32(18, 20, 26, 255);
            ImU32 gridCol = IM_COL32(32, 36, 48, 160);
            ImU32 gridSubCol = IM_COL32(26, 30, 40, 100);

            drawList->AddRectFilled(canvasAreaPos, ImVec2(canvasAreaPos.x + canvasAreaSize.x, canvasAreaPos.y + canvasAreaSize.y), bgCol);

            float gridSize = 32.0f * zoom;
            if (gridSize >= 8.0f) {
                for (float x = std::fmod(pan.x, gridSize); x < canvasAreaSize.x; x += gridSize) {
                    drawList->AddLine(ImVec2(canvasAreaPos.x + x, canvasAreaPos.y), ImVec2(canvasAreaPos.x + x, canvasAreaPos.y + canvasAreaSize.y), gridSubCol, 1.0f);
                }
                for (float y = std::fmod(pan.y, gridSize); y < canvasAreaSize.y; y += gridSize) {
                    drawList->AddLine(ImVec2(canvasAreaPos.x, canvasAreaPos.y + y), ImVec2(canvasAreaPos.x + canvasAreaSize.x, canvasAreaPos.y + y), gridSubCol, 1.0f);
                }
            }

            // Canvas Framing & Auto-centering
            float baseScale = std::min((canvasAreaSize.x - 60.0f) / ui.canvasResolution.x, (canvasAreaSize.y - 60.0f) / ui.canvasResolution.y);
            if (baseScale <= 0.0f) baseScale = 0.5f;

            float currentScale = baseScale * zoom;
            glm::vec2 canvasDisplaySize = ui.canvasResolution * currentScale;

            glm::vec2 canvasOrigin(
                canvasAreaPos.x + (canvasAreaSize.x - canvasDisplaySize.x) * 0.5f + pan.x,
                canvasAreaPos.y + (canvasAreaSize.y - canvasDisplaySize.y) * 0.5f + pan.y
            );

            ImVec2 cMin(canvasOrigin.x, canvasOrigin.y);
            ImVec2 cMax(canvasOrigin.x + canvasDisplaySize.x, canvasOrigin.y + canvasDisplaySize.y);

            // Canvas Drop Shadow
            drawList->AddRectFilled(ImVec2(cMin.x + 8.0f, cMin.y + 12.0f), ImVec2(cMax.x + 8.0f, cMax.y + 12.0f), IM_COL32(0, 0, 0, 160), 12.0f);

            // Canvas Frame Background
            drawList->AddRectFilled(cMin, cMax, IM_COL32(10, 12, 16, 255), 8.0f);

            // Canvas Outline Border
            drawList->AddRect(cMin, cMax, IM_COL32(30, 140, 220, 200), 8.0f, 0, 2.0f);

            // Clip rendering to canvas bounds
            drawList->PushClipRect(cMin, cMax, true);

            // Process interactivity in editor preview canvas (hover, clicks, scroll)
            ImGuiIO& io = ImGui::GetIO();
            glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
            bool isMouseDown = io.MouseDown[0] && !isPanning;
            bool isMousePressed = io.MouseClicked[0] && !isPanning;
            bool isMouseReleased = io.MouseReleased[0] && !isPanning;
            float mouseWheel = io.MouseWheel;
            auto eventDispatcher = [scene](const UIEventBinding& binding) {
#if __has_include(<EngineScene/Scene.h>)
                ExecuteScriptFunction(scene, binding);
#endif
            };
            ui.ProcessInput(mousePos, isMouseDown, isMousePressed, isMouseReleased, mouseWheel, canvasOrigin, currentScale, eventDispatcher);

            // Selection Hit-Testing on Canvas click
            bool isHovered = ImGui::IsWindowHovered();
            if (isMousePressed && isHovered && !isPanning) {
                glm::vec2 localMouse = (mousePos - canvasOrigin) / currentScale;
                if (localMouse.x >= 0.0f && localMouse.x <= ui.canvasResolution.x &&
                    localMouse.y >= 0.0f && localMouse.y <= ui.canvasResolution.y) {
                    if (ui.htmlRoot) {
                        selectedNode = ui.htmlRoot->HitTest(localMouse);
                    }
                }
            }

            // Render HTML + CSS Tree
            if (ui.htmlRoot) {
                ui.RenderHtml(drawList, canvasOrigin, currentScale);
            }

            // Render any native elements
            for (const auto& el : ui.elements) {
                if (!el.visible) continue;
                glm::vec4 worldRect = ui.GetElementWorldRect(el);
                glm::vec2 elemPos = canvasOrigin + glm::vec2(worldRect.x, worldRect.y) * currentScale;
                glm::vec2 elemSize = glm::vec2(worldRect.z, worldRect.w) * currentScale;

                ImVec2 pMin(elemPos.x, elemPos.y);
                ImVec2 pMax(elemPos.x + elemSize.x, elemPos.y + elemSize.y);

                if (el.type == UIElementType::Border) {
                    ImU32 elBgCol = IM_COL32((int)(el.backgroundColor.r * 255), (int)(el.backgroundColor.g * 255), (int)(el.backgroundColor.b * 255), (int)(el.backgroundColor.a * 255));
                    ImU32 elBdCol = IM_COL32((int)(el.borderColor.r * 255), (int)(el.borderColor.g * 255), (int)(el.borderColor.b * 255), (int)(el.borderColor.a * 255));
                    if (el.filled) drawList->AddRectFilled(pMin, pMax, elBgCol, el.cornerRounding * currentScale);
                    if (el.borderThickness > 0.0f) drawList->AddRect(pMin, pMax, elBdCol, el.cornerRounding * currentScale, 0, el.borderThickness * currentScale);
                } else if (el.type == UIElementType::Text) {
                    ImU32 txtCol = IM_COL32((int)(el.textColor.r * 255), (int)(el.textColor.g * 255), (int)(el.textColor.b * 255), (int)(el.textColor.a * 255));
                    float fontSize = el.fontSize * currentScale;
                    drawList->AddText(nullptr, fontSize, pMin, txtCol, el.text.c_str());
                }
            }

            // Draw Visual Selection Highlight on Selected Node
            if (selectedNode && !selectedNode->style.isHidden) {
                glm::vec2 selMin = canvasOrigin + glm::vec2(selectedNode->computedRect.x, selectedNode->computedRect.y) * currentScale;
                glm::vec2 selSize = glm::vec2(selectedNode->computedRect.z, selectedNode->computedRect.w) * currentScale;
                glm::vec2 selMax = selMin + selSize;

                // Selection Box Overlay & Glow
                drawList->AddRectFilled(ImVec2(selMin.x, selMin.y), ImVec2(selMax.x, selMax.y), IM_COL32(0, 180, 255, 30), 2.0f);
                drawList->AddRect(ImVec2(selMin.x, selMin.y), ImVec2(selMax.x, selMax.y), IM_COL32(0, 190, 255, 255), 2.0f, 0, 2.0f);

                // Corner Handles
                float handleSize = 5.0f;
                auto drawHandle = [&](float hx, float hy) {
                    drawList->AddRectFilled(ImVec2(hx - handleSize * 0.5f, hy - handleSize * 0.5f),
                                            ImVec2(hx + handleSize * 0.5f, hy + handleSize * 0.5f),
                                            IM_COL32(255, 255, 255, 255), 1.0f);
                    drawList->AddRect(ImVec2(hx - handleSize * 0.5f, hy - handleSize * 0.5f),
                                      ImVec2(hx + handleSize * 0.5f, hy + handleSize * 0.5f),
                                      IM_COL32(0, 140, 255, 255), 1.0f, 0, 1.0f);
                };
                drawHandle(selMin.x, selMin.y);
                drawHandle(selMax.x, selMin.y);
                drawHandle(selMin.x, selMax.y);
                drawHandle(selMax.x, selMax.y);

                // Element Tag Pill Badge
                std::string badgeText = "<" + selectedNode->tag;
                if (!selectedNode->id.empty()) badgeText += "#" + selectedNode->id;
                else if (!selectedNode->classListStr.empty()) badgeText += "." + selectedNode->classes[0];
                badgeText += ">";

                ImVec2 badgePos(selMin.x, std::max(cMin.y + 4.0f, selMin.y - 20.0f));
                ImVec2 bTextSize = ImGui::CalcTextSize(badgeText.c_str());
                drawList->AddRectFilled(ImVec2(badgePos.x, badgePos.y), ImVec2(badgePos.x + bTextSize.x + 8.0f, badgePos.y + bTextSize.y + 4.0f), IM_COL32(0, 130, 230, 230), 4.0f);
                drawList->AddText(ImVec2(badgePos.x + 4.0f, badgePos.y + 2.0f), IM_COL32(255, 255, 255, 255), badgeText.c_str());
            }

            drawList->PopClipRect();

            // Canvas Info Tag
            char infoTag[128];
            snprintf(infoTag, sizeof(infoTag), "Canvas: %.0f x %.0f  |  Scale: %.0f%%", ui.canvasResolution.x, ui.canvasResolution.y, zoom * 100.0f);
            drawList->AddText(ImVec2(canvasAreaPos.x + 16.0f, canvasAreaPos.y + canvasAreaSize.y - 28.0f), IM_COL32(160, 175, 195, 200), infoTag);

            // Canvas Interaction (Pan & Zoom)
            if (isHovered) {
                // Zoom with scroll wheel
                if (io.MouseWheel != 0.0f) {
                    float zoomDelta = io.MouseWheel * 0.10f;
                    zoom = std::clamp(zoom + zoomDelta, 0.20f, 4.0f);
                }

                // Pan with Right Mouse Button or Middle Mouse Button
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                    isPanning = true;
                    panStart = io.MousePos;
                    panInitial = pan;
                }
            }

            if (isPanning) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                    pan.x = panInitial.x + (io.MousePos.x - panStart.x);
                    pan.y = panInitial.y + (io.MousePos.y - panStart.y);
                } else {
                    isPanning = false;
                }
            }
        }
        ImGui::EndChild();
    }

    void RenderDomTree(HtmlNode* node) {
        if (!node) return;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
        if (node->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
        if (selectedNode == node) flags |= ImGuiTreeNodeFlags_Selected;

        std::string label = "<" + node->tag + ">";
        if (!node->id.empty()) label += " #" + node->id;
        else if (!node->classListStr.empty()) label += " ." + node->classListStr;
        if (!node->textContent.empty()) {
            std::string shortTxt = node->textContent;
            if (shortTxt.size() > 16) shortTxt = shortTxt.substr(0, 16) + "...";
            label += " \"" + shortTxt + "\"";
        }

        bool open = ImGui::TreeNodeEx((void*)node, flags, "%s", label.c_str());
        if (ImGui::IsItemClicked()) {
            selectedNode = node;
        }
        if (open) {
            for (auto& ch : node->children) {
                RenderDomTree(ch.get());
            }
            ImGui::TreePop();
        }
    }

    void RenderPropertiesPanel(const ImVec2& size, Scene* scene = nullptr) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.10f, 0.13f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));

        if (ImGui::BeginChild("UIPropertiesPanel", size, true)) {
            // Header Bar
            ImGui::TextColored(ImVec4(0.12f, 0.75f, 1.0f, 1.0f), "⚡ Properties & Events");
            ImGui::Separator();

            if (!selectedNode) {
                // Empty State Prompt
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.13f, 0.14f, 0.18f, 0.80f));
                if (ImGui::BeginChild("UIEmptySelectCard", ImVec2(0, 110.0f), true)) {
                    ImGui::TextColored(ImVec4(0.85f, 0.90f, 1.0f, 1.0f), "🎯 Select an Element");
                    ImGui::Spacing();
                    ImGui::TextWrapped("Click any element (buttons, divs, headings, text) on the canvas to inspect its computed properties and bind Level Behaviour Script events.");
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // DOM Tree Outliner
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "🌳 UI DOM Hierarchy");
                ImGui::Spacing();
                if (ui.htmlRoot) {
                    RenderDomTree(ui.htmlRoot.get());
                }
            } else {
                // Selected Element Header Card
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.15f, 0.20f, 0.90f));
                if (ImGui::BeginChild("UISelectedHeaderCard", ImVec2(0, 95.0f), true)) {
                    ImGui::TextColored(ImVec4(0.20f, 0.80f, 1.0f, 1.0f), "<%s>", selectedNode->tag.c_str());
                    ImGui::SameLine();
                    if (!selectedNode->id.empty()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.20f, 1.0f), "#%s", selectedNode->id.c_str());
                        ImGui::SameLine();
                    }
                    if (!selectedNode->classListStr.empty()) {
                        ImGui::TextColored(ImVec4(0.40f, 0.90f, 0.50f, 1.0f), ".%s", selectedNode->classListStr.c_str());
                    }

                    float availW = ImGui::GetContentRegionAvail().x;
                    if (availW > 80.0f) {
                        ImGui::SameLine(ImGui::GetWindowWidth() - 95.0f);
                        if (ImGui::SmallButton("✕ Deselect")) {
                            selectedNode = nullptr;
                            ImGui::EndChild();
                            ImGui::PopStyleColor();
                            ImGui::EndChild();
                            ImGui::PopStyleVar();
                            ImGui::PopStyleColor();
                            return;
                        }
                    }

                    ImGui::TextDisabled("Pos: (%.0f, %.0f)  |  Size: %.0f x %.0f",
                        selectedNode->computedRect.x, selectedNode->computedRect.y,
                        selectedNode->computedRect.z, selectedNode->computedRect.w);

                    if (!selectedNode->textContent.empty()) {
                        std::string t = selectedNode->textContent;
                        if (t.size() > 36) t = t.substr(0, 36) + "...";
                        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "Text: \"%s\"", t.c_str());
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();

                ImGui::Spacing();

                // ============================================================
                // EVENTS & BEHAVIOUR SCRIPT BINDINGS SECTION
                // ============================================================
                if (ImGui::CollapsingHeader("⚡ Event Triggers & Functions", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled("Bind UI events to Level Behaviour Scripts:");
                    ImGui::Spacing();

                    // Collect attached behaviour scripts from actors in the active level
                    struct LevelScriptOption {
                        int actorId = -1;
                        std::string actorName;
                        std::string behaviourClass;
                        std::string scriptPath;
                        std::string label;
                        void* behaviourPtr = nullptr;
                    };
                    std::vector<LevelScriptOption> scriptOptions;

                    LevelScriptOption noneOpt;
                    noneOpt.actorId = -1;
                    noneOpt.label = "(None / Unbound)";
                    scriptOptions.push_back(noneOpt);

#if __has_include(<EngineScene/Scene.h>)
                    if (scene) {
                        for (auto& obj : scene->objects) {
                            for (auto& b : obj.behaviours) {
                                if (!b) continue;
                                LevelScriptOption opt;
                                opt.actorId = obj.id;
                                opt.actorName = obj.name;
                                opt.behaviourClass = b->GetClassName();
                                opt.scriptPath = b->GetSourceCppPath();
                                opt.label = obj.name + " (" + b->GetClassName() + ")";
                                opt.behaviourPtr = b.get();
                                scriptOptions.push_back(opt);
                            }
                        }
                    }
#endif

                    struct EventDef {
                        const char* id;
                        const char* label;
                        const char* icon;
                    };
                    static const EventDef kEvents[] = {
                        { "onclick", "On Click (onclick)", "🖱️" },
                        { "onhover", "On Hover (onhover)", "👆" },
                        { "onmouseenter", "On Mouse Enter (onmouseenter)", "➡️" },
                        { "onmouseleave", "On Mouse Leave (onmouseleave)", "⬅️" },
                        { "onpress", "On Mouse Down (onpress)", "🔽" }
                    };

                    for (const auto& ev : kEvents) {
                        ImGui::PushID(ev.id);
                        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.13f, 0.17f, 0.85f));

                        float cardHeight = 118.0f;
                        auto it = selectedNode->eventBindings.find(ev.id);
                        bool isBound = (it != selectedNode->eventBindings.end() && !it->second.targetFunction.empty());
                        if (isBound) cardHeight = 158.0f;

                        if (ImGui::BeginChild("EventCard", ImVec2(0, cardHeight), true)) {
                            // Event Title
                            ImGui::TextColored(isBound ? ImVec4(0.2f, 0.9f, 0.4f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "%s %s", ev.icon, ev.label);
                            ImGui::Spacing();

                            // Find current selection index in scriptOptions
                            int curScriptIdx = 0;
                            if (isBound) {
                                for (size_t i = 1; i < scriptOptions.size(); ++i) {
                                    if (scriptOptions[i].actorId == it->second.targetActorId &&
                                        scriptOptions[i].behaviourClass == it->second.behaviourClass) {
                                        curScriptIdx = (int)i;
                                        break;
                                    }
                                }
                            }

                            // 1. Level Behaviour Script Dropdown
                            ImGui::TextDisabled("Level Behaviour Script:");
                            ImGui::SetNextItemWidth(-1.0f);
                            if (ImGui::BeginCombo("##ScriptSelect", scriptOptions[curScriptIdx].label.c_str())) {
                                for (size_t i = 0; i < scriptOptions.size(); ++i) {
                                    bool isSelected = (curScriptIdx == (int)i);
                                    if (ImGui::Selectable(scriptOptions[i].label.c_str(), isSelected)) {
                                        if (i == 0) {
                                            selectedNode->eventBindings.erase(ev.id);
                                            isDirty = true;
                                            if (autoSave) Save();
                                        } else {
                                            UIEventBinding nb;
                                            nb.eventName = ev.id;
                                            nb.targetActorId = scriptOptions[i].actorId;
                                            nb.targetActorName = scriptOptions[i].actorName;
                                            nb.behaviourClass = scriptOptions[i].behaviourClass;
                                            nb.scriptPath = scriptOptions[i].scriptPath;
                                            auto funcs = ExtractScriptFunctions(nb.scriptPath, nb.behaviourClass);
                                            if (!funcs.empty()) nb.targetFunction = funcs[0];
                                            selectedNode->eventBindings[ev.id] = nb;
                                            isDirty = true;
                                            if (autoSave) Save();
                                        }
                                    }
                                    if (isSelected) ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }

                            // 2. Class Functions Array / Dropdown
                            if (curScriptIdx > 0 && selectedNode->eventBindings.find(ev.id) != selectedNode->eventBindings.end()) {
                                auto& binding = selectedNode->eventBindings[ev.id];
                                auto funcs = ExtractScriptFunctions(scriptOptions[curScriptIdx].scriptPath, scriptOptions[curScriptIdx].behaviourClass);

                                int curFuncIdx = 0;
                                for (size_t f = 0; f < funcs.size(); ++f) {
                                    if (funcs[f] == binding.targetFunction) {
                                        curFuncIdx = (int)f;
                                        break;
                                    }
                                }

                                ImGui::Spacing();
                                ImGui::TextDisabled("Class Function to Execute:");
                                ImGui::SetNextItemWidth(-1.0f);
                                const char* previewFunc = funcs.empty() ? "(No Functions Found)" : funcs[curFuncIdx].c_str();
                                if (ImGui::BeginCombo("##FuncSelect", previewFunc)) {
                                    for (size_t f = 0; f < funcs.size(); ++f) {
                                        bool isFuncSelected = (curFuncIdx == (int)f);
                                        if (ImGui::Selectable(funcs[f].c_str(), isFuncSelected)) {
                                            binding.targetFunction = funcs[f];
                                            isDirty = true;
                                            if (autoSave) Save();
                                        }
                                        if (isFuncSelected) ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }

                                // Action Buttons
                                ImGui::Spacing();
#if __has_include(<EngineScene/Scene.h>)
                                if (ImGui::SmallButton("▶ Test Run")) {
                                    ExecuteScriptFunction(scene, binding);
                                }
                                ImGui::SameLine();
#endif
                                if (ImGui::SmallButton("🗑 Unbind")) {
                                    selectedNode->eventBindings.erase(ev.id);
                                    isDirty = true;
                                    if (autoSave) Save();
                                }
                            }
                        }
                        ImGui::EndChild();
                        ImGui::PopStyleColor();
                        ImGui::PopID();
                        ImGui::Spacing();
                    }
                }

                ImGui::Spacing();

                // ============================================================
                // COMPUTED STYLE & CSS ATTRIBUTES
                // ============================================================
                if (ImGui::CollapsingHeader("🎨 Computed Style & Classes", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Text("Tag: <%s>", selectedNode->tag.c_str());
                    if (!selectedNode->id.empty()) ImGui::Text("ID: #%s", selectedNode->id.c_str());
                    if (!selectedNode->classListStr.empty()) ImGui::Text("Classes: .%s", selectedNode->classListStr.c_str());
                    ImGui::Text("Font Size: %.0f px", selectedNode->style.fontSize);
                    ImGui::Text("Padding: (%.0f, %.0f, %.0f, %.0f)",
                        selectedNode->style.paddingTop, selectedNode->style.paddingRight,
                        selectedNode->style.paddingBottom, selectedNode->style.paddingLeft);
                    ImGui::Text("Margin: (%.0f, %.0f, %.0f, %.0f)",
                        selectedNode->style.marginTop, selectedNode->style.marginRight,
                        selectedNode->style.marginBottom, selectedNode->style.marginLeft);
                }
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
};

// ============================================================================
// UI EDITOR MANAGER SINGLETON
// ============================================================================
class UIEditorManager {
public:
    static UIEditorManager& Get() {
        static UIEditorManager instance;
        return instance;
    }

    int activeTabIndex = 0; // 0 = Main Editor, 1..N = UI Editors

    void Open(const std::string& filePath, const std::string& name = "") {
        for (size_t i = 0; i < m_editors.size(); ++i) {
            if (m_editors[i]->filePath == filePath) {
                m_editors[i]->isOpen = true;
                activeTabIndex = (int)i + 1;
                return;
            }
        }
        m_editors.push_back(std::make_unique<UIEditorWindow>(filePath, name));
        activeTabIndex = (int)m_editors.size();
    }

    void Close(size_t index) {
        if (index < m_editors.size()) {
            if (m_editors[index]->autoSave || m_editors[index]->isDirty) {
                m_editors[index]->Save();
            }
            m_editors.erase(m_editors.begin() + index);
            if (activeTabIndex > (int)m_editors.size()) {
                activeTabIndex = (int)m_editors.size();
            }
        }
    }

    bool IsMainEditorActive() const {
        return activeTabIndex == 0 || m_editors.empty();
    }

    UIEditorWindow* GetActiveEditor() {
        if (activeTabIndex > 0 && activeTabIndex <= (int)m_editors.size()) {
            return m_editors[activeTabIndex - 1].get();
        }
        return nullptr;
    }

    const std::vector<std::unique_ptr<UIEditorWindow>>& GetEditors() const {
        return m_editors;
    }

    size_t GetOpenEditorCount() const {
        return m_editors.size();
    }

    void SwitchToMainEditor() {
        activeTabIndex = 0;
    }

    void SwitchToEditor(int index) {
        if (index >= 0 && index <= (int)m_editors.size()) {
            activeTabIndex = index;
        }
    }

    void RenderActiveTabWorkspace(const glm::vec2& pos, const glm::vec2& size, Scene* scene = nullptr) {
        UIEditorWindow* activeEditor = GetActiveEditor();
        if (activeEditor && activeEditor->isOpen) {
            ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(size.x, size.y), ImGuiCond_Always);
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.08f, 0.10f, 1.0f));

            std::string winId = "##UIEditorWorkspace_" + activeEditor->filePath;
            if (ImGui::Begin(winId.c_str(), nullptr, flags)) {
                activeEditor->RenderUIEditorContent(scene);
            }
            ImGui::End();

            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
        }
    }

    void RenderAll() {
        // Render active tab workspace if a UI tab is selected
        if (!IsMainEditorActive()) {
            // Managed directly in EngineUI layout
        }
    }

    bool HasOpenEditors() const {
        return !m_editors.empty();
    }

private:
    UIEditorManager() = default;
    std::vector<std::unique_ptr<UIEditorWindow>> m_editors;
};

} // namespace EunoiaUI
