#pragma once

#include "UserInterface.h"
#include "UISystem.h"
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

inline UIEventBinding* FindEventBinding(UIElement& elem, const std::string& eventName) {
    for (auto& b : elem.eventBindings) {
        if (b.eventName == eventName) return &b;
    }
    return nullptr;
}

inline void SetEventBinding(UIElement& elem, const UIEventBinding& binding) {
    for (auto& b : elem.eventBindings) {
        if (b.eventName == binding.eventName) {
            b = binding;
            return;
        }
    }
    elem.eventBindings.push_back(binding);
}

inline void RemoveEventBinding(UIElement& elem, const std::string& eventName) {
    elem.eventBindings.erase(std::remove_if(elem.eventBindings.begin(), elem.eventBindings.end(),
        [&](const UIEventBinding& b) { return b.eventName == eventName; }), elem.eventBindings.end());
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
// UI EDITOR WINDOW CLASS (Atlas WYSIWYG UI Designer)
// ============================================================================
class UIEditorWindow {
public:
    enum class EditorMode {
        Edit,
        LivePreview
    };

    enum class ResizeHandle {
        None,
        TopLeft,
        Top,
        TopRight,
        Right,
        BottomRight,
        Bottom,
        BottomLeft,
        Left
    };

    UserInterface ui;
    std::string filePath;
    std::string name;
    bool isOpen = true;
    bool isDirty = false;
    bool autoSave = true;

    EditorMode mode = EditorMode::Edit;

    // Canvas navigation
    float zoom = 0.85f;
    ImVec2 pan = { 0.0f, 0.0f };
    bool isPanning = false;
    ImVec2 panStart = { 0.0f, 0.0f };
    ImVec2 panInitial = { 0.0f, 0.0f };

    // Canvas Selection & Manipulation
    std::string selectedElementId = "";
    bool isDraggingElement = false;
    ImVec2 dragStartMouse = { 0.0f, 0.0f };
    glm::vec2 dragInitialPos = { 0.0f, 0.0f };

    ResizeHandle activeResizeHandle = ResizeHandle::None;
    glm::vec2 resizeInitialPos = { 0.0f, 0.0f };
    glm::vec2 resizeInitialSize = { 0.0f, 0.0f };

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
                ui.SaveToFile(path);
            }
        } else {
            ui.name = name;
        }
    }

    void Save() {
        if (!filePath.empty()) {
            ui.SaveToFile(filePath);
            isDirty = false;
        }
    }

    UIElement* GetSelectedElement() {
        if (selectedElementId.empty()) return nullptr;
        return ui.GetElementById(selectedElementId);
    }

    void RenderUIEditorContent(Scene* scene = nullptr) {
        // Keyboard Shortcuts (Ctrl+S to save, Ctrl+D to duplicate, Del to delete)
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            Save();
        }
        if (mode == EditorMode::Edit && !selectedElementId.empty()) {
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
                UIElement* dup = ui.DuplicateElement(selectedElementId);
                if (dup) {
                    selectedElementId = dup->id;
                    isDirty = true;
                    if (autoSave) Save();
                }
            } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                ui.RemoveElement(selectedElementId);
                selectedElementId = "";
                isDirty = true;
                if (autoSave) Save();
            }
        }

        // Top Control Header Bar
        RenderTopToolbar();

        // Workspace Layout
        float availW = ImGui::GetContentRegionAvail().x;
        if (mode == EditorMode::LivePreview) {
            // Live Preview: full-width canvas
            RenderCanvasView(ImVec2(availW, 0), scene);
        } else {
            // Edit Mode: Left Sidebar (Palette + Hierarchy) | Center Canvas | Right Sidebar (Inspector)
            float leftPanelW = 240.0f;
            float rightPanelW = 340.0f;
            float canvasW = std::max(200.0f, availW - leftPanelW - rightPanelW);

            RenderLeftSidebar(ImVec2(leftPanelW, 0));
            ImGui::SameLine(0, 0);
            RenderCanvasView(ImVec2(canvasW, 0), scene);
            ImGui::SameLine(0, 0);
            RenderPropertiesPanel(ImVec2(rightPanelW, 0), scene);
        }
    }

    void RenderTopToolbar() {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));

        if (ImGui::BeginChild("UIEditorHeaderBar", ImVec2(0, 46.0f), true, ImGuiWindowFlags_NoScrollbar)) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
            
            // Asset Title
            ImGui::TextColored(ImVec4(0.290f, 0.486f, 1.0f, 1.0f), "🎨 Atlas UI Designer:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.929f, 0.933f, 0.953f, 1.0f), "%s", ui.name.c_str());
            if (isDirty) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.20f, 1.0f), "*");
            }

            ImGui::SameLine();
            ImGui::Spacing();
            ImGui::SameLine();

            // Mode Selector Pill (Edit Mode vs Live Preview)
            bool isEdit = (mode == EditorMode::Edit);
            bool isLive = (mode == EditorMode::LivePreview);

            if (isEdit) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.290f, 0.486f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.125f, 0.125f, 0.153f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.72f, 0.78f, 1.0f));
            }
            if (ImGui::Button("🛠 Edit Mode", ImVec2(110.0f, 26.0f))) {
                mode = EditorMode::Edit;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();

            if (isLive) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.80f, 0.44f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.125f, 0.125f, 0.153f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.72f, 0.78f, 1.0f));
            }
            if (ImGui::Button("▶ Live Preview", ImVec2(120.0f, 26.0f))) {
                mode = EditorMode::LivePreview;
                selectedElementId = "";
            }
            ImGui::PopStyleColor(2);

            // Right side buttons
            float availW = ImGui::GetContentRegionAvail().x;
            float rightControlsW = 420.0f;
            if (availW > rightControlsW) {
                ImGui::SameLine();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - rightControlsW));
            } else {
                ImGui::SameLine();
            }

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
                isDirty = true;
                if (autoSave) Save();
            }

            ImGui::SameLine();
            if (ImGui::Button("↺ Fit", ImVec2(48.0f, 26.0f))) {
                pan = { 0.0f, 0.0f };
                zoom = 0.85f;
            }

            ImGui::SameLine();

            // Save button (Atlas Blue)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.290f, 0.486f, 1.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.380f, 0.560f, 1.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.220f, 0.400f, 0.90f, 1.0f));
            if (ImGui::Button("💾 Save", ImVec2(75.0f, 26.0f))) {
                Save();
            }
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            ImGui::Checkbox("⚡ Autosave", &autoSave);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
    }

    void RenderLeftSidebar(const ImVec2& size) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.082f, 0.082f, 0.102f, 1.0f)); // Atlas #15151A
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));

        if (ImGui::BeginChild("UILeftSidebar", size, true)) {
            // Palette Header
            ImGui::TextColored(ImVec4(0.290f, 0.486f, 1.0f, 1.0f), "🧰 Component Palette");
            ImGui::Separator();
            ImGui::Spacing();

            auto renderAddBtn = [&](UIElementType type, const char* label, const char* icon) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.125f, 0.125f, 0.153f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.169f, 0.169f, 0.204f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.290f, 0.486f, 1.0f, 1.0f));
                
                std::string btnText = std::string(icon) + " " + label;
                if (ImGui::Button(btnText.c_str(), ImVec2(-1.0f, 28.0f))) {
                    UIElement* newElem = ui.AddElement(type);
                    if (newElem) {
                        // Place near center of canvas
                        newElem->position = glm::vec2(
                            ui.canvasResolution.x * 0.5f - newElem->size.x * 0.5f,
                            ui.canvasResolution.y * 0.5f - newElem->size.y * 0.5f
                        );
                        selectedElementId = newElem->id;
                        isDirty = true;
                        if (autoSave) Save();
                    }
                }
                ImGui::PopStyleColor(3);
                ImGui::Spacing();
            };

            renderAddBtn(UIElementType::Panel, "Panel", "📦");
            renderAddBtn(UIElementType::Button, "Button", "🔘");
            renderAddBtn(UIElementType::Text, "Text Label", "🔤");
            renderAddBtn(UIElementType::ProgressBar, "Progress Bar", "📊");
            renderAddBtn(UIElementType::Image, "Image", "🖼️");
            renderAddBtn(UIElementType::Slider, "Slider", "🎚️");
            renderAddBtn(UIElementType::Checkbox, "Checkbox", "☑️");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Hierarchy / Outliner
            ImGui::TextColored(ImVec4(0.290f, 0.486f, 1.0f, 1.0f), "🌳 UI Hierarchy");
            ImGui::Separator();
            ImGui::Spacing();

            if (ui.elements.empty()) {
                ImGui::TextDisabled("No elements added yet.\nClick a component above to add one.");
            } else {
                for (size_t i = 0; i < ui.elements.size(); ++i) {
                    auto& elem = ui.elements[i];
                    bool isSelected = (selectedElementId == elem.id);

                    const char* typeIcon = "📄";
                    if (elem.type == UIElementType::Panel) typeIcon = "📦";
                    else if (elem.type == UIElementType::Button) typeIcon = "🔘";
                    else if (elem.type == UIElementType::Text) typeIcon = "🔤";
                    else if (elem.type == UIElementType::ProgressBar) typeIcon = "📊";
                    else if (elem.type == UIElementType::Image) typeIcon = "🖼️";
                    else if (elem.type == UIElementType::Slider) typeIcon = "🎚️";
                    else if (elem.type == UIElementType::Checkbox) typeIcon = "☑️";

                    std::string itemLabel = std::string(typeIcon) + " " + elem.name;
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

                    ImGui::TreeNodeEx((void*)(uintptr_t)(i + 1), flags, "%s", itemLabel.c_str());
                    if (ImGui::IsItemClicked()) {
                        selectedElementId = elem.id;
                    }

                    // Context Menu for element in hierarchy
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("📄 Duplicate (Ctrl+D)")) {
                            UIElement* dup = ui.DuplicateElement(elem.id);
                            if (dup) {
                                selectedElementId = dup->id;
                                isDirty = true;
                                if (autoSave) Save();
                            }
                        }
                        if (i > 0 && ImGui::MenuItem("⬆ Move Up")) {
                            std::swap(ui.elements[i], ui.elements[i - 1]);
                            isDirty = true;
                            if (autoSave) Save();
                        }
                        if (i + 1 < ui.elements.size() && ImGui::MenuItem("⬇ Move Down")) {
                            std::swap(ui.elements[i], ui.elements[i + 1]);
                            isDirty = true;
                            if (autoSave) Save();
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("🗑 Delete (Del)")) {
                            ui.RemoveElement(elem.id);
                            if (selectedElementId == elem.id) selectedElementId = "";
                            isDirty = true;
                            if (autoSave) Save();
                        }
                        ImGui::EndPopup();
                    }
                }
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
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

            // Atlas Canvas Background Grid (#0D0D11 background)
            ImU32 bgCol = IM_COL32(13, 13, 17, 255);
            ImU32 gridCol = IM_COL32(32, 36, 48, 140);
            ImU32 gridSubCol = IM_COL32(24, 26, 34, 100);

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
            drawList->AddRectFilled(ImVec2(cMin.x + 8.0f, cMin.y + 12.0f), ImVec2(cMax.x + 8.0f, cMax.y + 12.0f), IM_COL32(0, 0, 0, 180), 12.0f);

            // Canvas Frame Background (#15151A)
            drawList->AddRectFilled(cMin, cMax, IM_COL32(21, 21, 26, 255), 8.0f);

            // Canvas Outline Border (#4A7CFF or subtle dark)
            drawList->AddRect(cMin, cMax, (mode == EditorMode::Edit) ? IM_COL32(74, 124, 255, 180) : IM_COL32(46, 204, 113, 200), 8.0f, 0, 2.0f);

            // Clip rendering to canvas bounds
            drawList->PushClipRect(cMin, cMax, true);

            ImGuiIO& io = ImGui::GetIO();
            glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
            bool isMouseDown = io.MouseDown[0] && !isPanning;
            bool isMousePressed = io.MouseClicked[0] && !isPanning;
            bool isMouseReleased = io.MouseReleased[0] && !isPanning;
            float mouseWheel = io.MouseWheel;

            if (mode == EditorMode::LivePreview) {
                // Live interactive mode: dispatch inputs to UI
                auto eventDispatcher = [scene](const UIEventBinding& binding) {
#if __has_include(<EngineScene/Scene.h>)
                    ExecuteScriptFunction(scene, binding);
#endif
                };
                ui.ProcessInput(mousePos, isMouseDown, isMousePressed, isMouseReleased, mouseWheel, canvasOrigin, currentScale, eventDispatcher);
            }

            // Render all UI elements via pure Atlas rendering
            ui.RenderUI(drawList, canvasOrigin, currentScale);

            // ================================================================
            // EDIT MODE: Canvas Hit-Testing, Selection, Drag-Move & 8-Handle Resize
            // ================================================================
            if (mode == EditorMode::Edit) {
                UIElement* selectedElem = GetSelectedElement();
                bool isHovered = ImGui::IsWindowHovered();

                // Mouse release clears dragging states
                if (isMouseReleased) {
                    if (isDraggingElement || activeResizeHandle != ResizeHandle::None) {
                        isDirty = true;
                        if (autoSave) Save();
                    }
                    isDraggingElement = false;
                    activeResizeHandle = ResizeHandle::None;
                }

                // Dragging manipulation
                if (selectedElem) {
                    glm::vec4 wRect = ui.GetElementWorldRect(*selectedElem);
                    glm::vec2 ePos = canvasOrigin + glm::vec2(wRect.x, wRect.y) * currentScale;
                    glm::vec2 eSize = glm::vec2(wRect.z, wRect.w) * currentScale;

                    if (isDraggingElement) {
                        ImVec2 delta = ImVec2(io.MousePos.x - dragStartMouse.x, io.MousePos.y - dragStartMouse.y);
                        selectedElem->position = dragInitialPos + glm::vec2(delta.x / currentScale, delta.y / currentScale);
                    } else if (activeResizeHandle != ResizeHandle::None) {
                        glm::vec2 mouseDelta = glm::vec2((io.MousePos.x - dragStartMouse.x) / currentScale, (io.MousePos.y - dragStartMouse.y) / currentScale);

                        glm::vec2 newPos = resizeInitialPos;
                        glm::vec2 newSize = resizeInitialSize;

                        switch (activeResizeHandle) {
                            case ResizeHandle::Right:
                                newSize.x = std::max(10.0f, resizeInitialSize.x + mouseDelta.x);
                                break;
                            case ResizeHandle::Bottom:
                                newSize.y = std::max(10.0f, resizeInitialSize.y + mouseDelta.y);
                                break;
                            case ResizeHandle::BottomRight:
                                newSize.x = std::max(10.0f, resizeInitialSize.x + mouseDelta.x);
                                newSize.y = std::max(10.0f, resizeInitialSize.y + mouseDelta.y);
                                break;
                            case ResizeHandle::Left: {
                                float dw = std::min(mouseDelta.x, resizeInitialSize.x - 10.0f);
                                newPos.x = resizeInitialPos.x + dw;
                                newSize.x = resizeInitialSize.x - dw;
                                break;
                            }
                            case ResizeHandle::Top: {
                                float dh = std::min(mouseDelta.y, resizeInitialSize.y - 10.0f);
                                newPos.y = resizeInitialPos.y + dh;
                                newSize.y = resizeInitialSize.y - dh;
                                break;
                            }
                            case ResizeHandle::TopLeft: {
                                float dw = std::min(mouseDelta.x, resizeInitialSize.x - 10.0f);
                                float dh = std::min(mouseDelta.y, resizeInitialSize.y - 10.0f);
                                newPos.x = resizeInitialPos.x + dw;
                                newSize.x = resizeInitialSize.x - dw;
                                newPos.y = resizeInitialPos.y + dh;
                                newSize.y = resizeInitialSize.y - dh;
                                break;
                            }
                            case ResizeHandle::TopRight: {
                                float dh = std::min(mouseDelta.y, resizeInitialSize.y - 10.0f);
                                newSize.x = std::max(10.0f, resizeInitialSize.x + mouseDelta.x);
                                newPos.y = resizeInitialPos.y + dh;
                                newSize.y = resizeInitialSize.y - dh;
                                break;
                            }
                            case ResizeHandle::BottomLeft: {
                                float dw = std::min(mouseDelta.x, resizeInitialSize.x - 10.0f);
                                newPos.x = resizeInitialPos.x + dw;
                                newSize.x = resizeInitialSize.x - dw;
                                newSize.y = std::max(10.0f, resizeInitialSize.y + mouseDelta.y);
                                break;
                            }
                            default: break;
                        }

                        selectedElem->position = newPos;
                        selectedElem->size = newSize;
                    }
                }

                // Hit testing on mouse click (handles first, then elements)
                if (isMousePressed && isHovered && !isPanning && activeResizeHandle == ResizeHandle::None && !isDraggingElement) {
                    bool hitHandle = false;

                    // 1. Check handles of selected element
                    if (selectedElem) {
                        glm::vec4 wRect = ui.GetElementWorldRect(*selectedElem);
                        glm::vec2 ePos = canvasOrigin + glm::vec2(wRect.x, wRect.y) * currentScale;
                        glm::vec2 eSize = glm::vec2(wRect.z, wRect.w) * currentScale;

                        float hRadius = 6.0f;
                        auto checkHandle = [&](float hx, float hy, ResizeHandle handle) -> bool {
                            if (mousePos.x >= hx - hRadius && mousePos.x <= hx + hRadius &&
                                mousePos.y >= hy - hRadius && mousePos.y <= hy + hRadius) {
                                activeResizeHandle = handle;
                                dragStartMouse = io.MousePos;
                                resizeInitialPos = selectedElem->position;
                                resizeInitialSize = selectedElem->size;
                                return true;
                            }
                            return false;
                        };

                        if (checkHandle(ePos.x, ePos.y, ResizeHandle::TopLeft) ||
                            checkHandle(ePos.x + eSize.x * 0.5f, ePos.y, ResizeHandle::Top) ||
                            checkHandle(ePos.x + eSize.x, ePos.y, ResizeHandle::TopRight) ||
                            checkHandle(ePos.x + eSize.x, ePos.y + eSize.y * 0.5f, ResizeHandle::Right) ||
                            checkHandle(ePos.x + eSize.x, ePos.y + eSize.y, ResizeHandle::BottomRight) ||
                            checkHandle(ePos.x + eSize.x * 0.5f, ePos.y + eSize.y, ResizeHandle::Bottom) ||
                            checkHandle(ePos.x, ePos.y + eSize.y, ResizeHandle::BottomLeft) ||
                            checkHandle(ePos.x, ePos.y + eSize.y * 0.5f, ResizeHandle::Left)) {
                            hitHandle = true;
                        }
                    }

                    // 2. Check element bodies (reverse order so top element gets picked)
                    if (!hitHandle) {
                        bool foundElement = false;
                        for (int i = (int)ui.elements.size() - 1; i >= 0; --i) {
                            auto& elem = ui.elements[i];
                            if (!elem.visible) continue;
                            glm::vec4 wRect = ui.GetElementWorldRect(elem);
                            glm::vec2 ePos = canvasOrigin + glm::vec2(wRect.x, wRect.y) * currentScale;
                            glm::vec2 eSize = glm::vec2(wRect.z, wRect.w) * currentScale;

                            if (mousePos.x >= ePos.x && mousePos.x <= ePos.x + eSize.x &&
                                mousePos.y >= ePos.y && mousePos.y <= ePos.y + eSize.y) {
                                selectedElementId = elem.id;
                                isDraggingElement = true;
                                dragStartMouse = io.MousePos;
                                dragInitialPos = elem.position;
                                foundElement = true;
                                break;
                            }
                        }

                        if (!foundElement) {
                            selectedElementId = "";
                        }
                    }
                }

                // Draw Visual Selection Frame & Handles
                if (selectedElem) {
                    glm::vec4 wRect = ui.GetElementWorldRect(*selectedElem);
                    glm::vec2 selMin = canvasOrigin + glm::vec2(wRect.x, wRect.y) * currentScale;
                    glm::vec2 selSize = glm::vec2(wRect.z, wRect.w) * currentScale;
                    glm::vec2 selMax = selMin + selSize;

                    // Translucent accent fill & crisp border (#4A7CFF)
                    drawList->AddRectFilled(ImVec2(selMin.x, selMin.y), ImVec2(selMax.x, selMax.y), IM_COL32(74, 124, 255, 30), 2.0f);
                    drawList->AddRect(ImVec2(selMin.x, selMin.y), ImVec2(selMax.x, selMax.y), IM_COL32(74, 124, 255, 255), 2.0f, 0, 2.0f);

                    // 8 Resize Handles
                    float handleSize = 7.0f;
                    auto drawHandle = [&](float hx, float hy) {
                        drawList->AddRectFilled(ImVec2(hx - handleSize * 0.5f, hy - handleSize * 0.5f),
                                                ImVec2(hx + handleSize * 0.5f, hy + handleSize * 0.5f),
                                                IM_COL32(255, 255, 255, 255), 1.0f);
                        drawList->AddRect(ImVec2(hx - handleSize * 0.5f, hy - handleSize * 0.5f),
                                          ImVec2(hx + handleSize * 0.5f, hy + handleSize * 0.5f),
                                          IM_COL32(74, 124, 255, 255), 1.0f, 0, 1.5f);
                    };

                    drawHandle(selMin.x, selMin.y);
                    drawHandle(selMin.x + selSize.x * 0.5f, selMin.y);
                    drawHandle(selMax.x, selMin.y);
                    drawHandle(selMax.x, selMin.y + selSize.y * 0.5f);
                    drawHandle(selMax.x, selMax.y);
                    drawHandle(selMin.x + selSize.x * 0.5f, selMax.y);
                    drawHandle(selMin.x, selMax.y);
                    drawHandle(selMin.x, selMin.y + selSize.y * 0.5f);

                    // Anchor Indicator Pin & Connecting Line
                    glm::vec2 anchorPos = canvasOrigin + (ui.canvasResolution * selectedElem->anchorMin) * currentScale;
                    drawList->AddLine(ImVec2(anchorPos.x, anchorPos.y), ImVec2(selMin.x, selMin.y), IM_COL32(74, 124, 255, 140), 1.0f);
                    drawList->AddCircleFilled(ImVec2(anchorPos.x, anchorPos.y), 4.0f, IM_COL32(74, 124, 255, 255));

                    // Element Tag Pill Badge
                    std::string badgeText = "[" + std::string(UIElementTypeToString(selectedElem->type)) + "] " + selectedElem->name;
                    ImVec2 badgePos(selMin.x, std::max(cMin.y + 4.0f, selMin.y - 22.0f));
                    ImVec2 bTextSize = ImGui::CalcTextSize(badgeText.c_str());
                    drawList->AddRectFilled(ImVec2(badgePos.x, badgePos.y), ImVec2(badgePos.x + bTextSize.x + 8.0f, badgePos.y + bTextSize.y + 4.0f), IM_COL32(32, 70, 170, 240), 4.0f);
                    drawList->AddText(ImVec2(badgePos.x + 4.0f, badgePos.y + 2.0f), IM_COL32(255, 255, 255, 255), badgeText.c_str());
                }
            }

            drawList->PopClipRect();

            // Canvas Info Tag
            char infoTag[128];
            snprintf(infoTag, sizeof(infoTag), "Canvas: %.0f x %.0f  |  Scale: %.0f%%  |  Mode: %s",
                ui.canvasResolution.x, ui.canvasResolution.y, zoom * 100.0f,
                (mode == EditorMode::Edit) ? "Edit" : "Live Preview");
            drawList->AddText(ImVec2(canvasAreaPos.x + 16.0f, canvasAreaPos.y + canvasAreaSize.y - 28.0f), IM_COL32(160, 175, 195, 200), infoTag);

            // Canvas Interaction (Pan & Zoom)
            bool isHovered = ImGui::IsWindowHovered();
            if (isHovered) {
                if (io.MouseWheel != 0.0f) {
                    float zoomDelta = io.MouseWheel * 0.10f;
                    zoom = std::clamp(zoom + zoomDelta, 0.20f, 4.0f);
                }

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

    void RenderPropertiesPanel(const ImVec2& size, Scene* scene = nullptr) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.082f, 0.082f, 0.102f, 1.0f)); // Atlas #15151A
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));

        if (ImGui::BeginChild("UIPropertiesPanel", size, true)) {
            ImGui::TextColored(ImVec4(0.290f, 0.486f, 1.0f, 1.0f), "⚡ Properties Inspector");
            ImGui::Separator();
            ImGui::Spacing();

            UIElement* elem = GetSelectedElement();
            if (!elem) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.125f, 0.125f, 0.153f, 0.80f));
                if (ImGui::BeginChild("UIEmptySelectCard", ImVec2(0, 110.0f), true)) {
                    ImGui::TextColored(ImVec4(0.929f, 0.933f, 0.953f, 1.0f), "🎯 Select an Element");
                    ImGui::Spacing();
                    ImGui::TextWrapped("Click any element on the canvas or in the left hierarchy panel to inspect and customize its transform, style, and C++ event bindings.");
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            } else {
                // Identity
                if (ImGui::CollapsingHeader("📌 Identity", ImGuiTreeNodeFlags_DefaultOpen)) {
                    char nameBuf[128];
                    strncpy_s(nameBuf, elem->name.c_str(), sizeof(nameBuf));
                    ImGui::TextDisabled("Name:");
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputText("##ElemName", nameBuf, sizeof(nameBuf))) {
                        elem->name = nameBuf;
                        isDirty = true;
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Type: %s", UIElementTypeToString(elem->type));
                    ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                    if (ImGui::Checkbox("Visible", &elem->visible)) {
                        isDirty = true;
                    }
                }

                ImGui::Spacing();

                // Transform
                if (ImGui::CollapsingHeader("📐 Transform & Anchoring", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled("Position (X, Y):");
                    float pos[2] = { elem->position.x, elem->position.y };
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::DragFloat2("##ElemPos", pos, 1.0f)) {
                        elem->position = glm::vec2(pos[0], pos[1]);
                        isDirty = true;
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Size (Width, Height):");
                    float sz[2] = { elem->size.x, elem->size.y };
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::DragFloat2("##ElemSize", sz, 1.0f, 5.0f, 4000.0f)) {
                        elem->size = glm::vec2(std::max(5.0f, sz[0]), std::max(5.0f, sz[1]));
                        isDirty = true;
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Z-Index:");
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::DragInt("##ElemZIndex", &elem->zOrder, 1, -100, 100)) {
                        isDirty = true;
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Anchor Preset:");

                    // 3x3 Anchor Preset Grid
                    auto renderAnchorBtn = [&](UIAnchorPreset preset, const char* label) {
                        bool isCur = (elem->anchorPreset == preset);
                        if (isCur) {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.290f, 0.486f, 1.0f, 1.0f));
                        } else {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.125f, 0.125f, 0.153f, 1.0f));
                        }
                        if (ImGui::Button(label, ImVec2(60.0f, 24.0f))) {
                            elem->anchorPreset = preset;
                            AnchorPresetToMinMax(preset, elem->anchorMin, elem->anchorMax, elem->pivot);
                            isDirty = true;
                        }
                        ImGui::PopStyleColor();
                    };

                    renderAnchorBtn(UIAnchorPreset::TopLeft, "TL"); ImGui::SameLine();
                    renderAnchorBtn(UIAnchorPreset::TopCenter, "TC"); ImGui::SameLine();
                    renderAnchorBtn(UIAnchorPreset::TopRight, "TR");

                    renderAnchorBtn(UIAnchorPreset::CenterLeft, "CL"); ImGui::SameLine();
                    renderAnchorBtn(UIAnchorPreset::Center, "C"); ImGui::SameLine();
                    renderAnchorBtn(UIAnchorPreset::CenterRight, "CR");

                    renderAnchorBtn(UIAnchorPreset::BottomLeft, "BL"); ImGui::SameLine();
                    renderAnchorBtn(UIAnchorPreset::BottomCenter, "BC"); ImGui::SameLine();
                    renderAnchorBtn(UIAnchorPreset::BottomRight, "BR");
                }

                ImGui::Spacing();

                // Styling & Appearance
                if (ImGui::CollapsingHeader("🎨 Styling & Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (elem->filled) {
                        ImGui::TextDisabled("Background Color:");
                        float bgCol[4] = { elem->backgroundColor.r, elem->backgroundColor.g, elem->backgroundColor.b, elem->backgroundColor.a };
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::ColorEdit4("##ElemBgCol", bgCol)) {
                            elem->backgroundColor = glm::vec4(bgCol[0], bgCol[1], bgCol[2], bgCol[3]);
                            isDirty = true;
                        }
                    }

                    if (elem->type == UIElementType::Button) {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Hover Color:");
                        float hCol[4] = { elem->hoverColor.r, elem->hoverColor.g, elem->hoverColor.b, elem->hoverColor.a };
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::ColorEdit4("##ElemHovCol", hCol)) {
                            elem->hoverColor = glm::vec4(hCol[0], hCol[1], hCol[2], hCol[3]);
                            isDirty = true;
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Pressed Color:");
                        float pCol[4] = { elem->pressedColor.r, elem->pressedColor.g, elem->pressedColor.b, elem->pressedColor.a };
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::ColorEdit4("##ElemPressCol", pCol)) {
                            elem->pressedColor = glm::vec4(pCol[0], pCol[1], pCol[2], pCol[3]);
                            isDirty = true;
                        }
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Border Color:");
                    float bdCol[4] = { elem->borderColor.r, elem->borderColor.g, elem->borderColor.b, elem->borderColor.a };
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::ColorEdit4("##ElemBdCol", bdCol)) {
                        elem->borderColor = glm::vec4(bdCol[0], bdCol[1], bdCol[2], bdCol[3]);
                        isDirty = true;
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Border Thickness:");
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::SliderFloat("##ElemBdThick", &elem->borderThickness, 0.0f, 10.0f, "%.1f px")) {
                        isDirty = true;
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Corner Rounding:");
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::SliderFloat("##ElemRounding", &elem->cornerRounding, 0.0f, 50.0f, "%.1f px")) {
                        isDirty = true;
                    }

                    ImGui::Spacing();
                    ImGui::Checkbox("Drop Shadow", &elem->hasShadow);
                    if (elem->hasShadow) {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Shadow Spread:");
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::SliderFloat("##ElemShadowSpread", &elem->shadowSpread, 0.0f, 30.0f, "%.1f px")) {
                            isDirty = true;
                        }
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Opacity:");
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::SliderFloat("##ElemOpacity", &elem->opacity, 0.0f, 1.0f, "%.2f")) {
                        isDirty = true;
                    }
                }

                ImGui::Spacing();

                // Type-Specific Attributes
                if (elem->type == UIElementType::Text || elem->type == UIElementType::Button) {
                    if (ImGui::CollapsingHeader("🔤 Text Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
                        char txtBuf[256];
                        strncpy_s(txtBuf, elem->text.c_str(), sizeof(txtBuf));
                        ImGui::TextDisabled("Text String:");
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::InputText("##ElemText", txtBuf, sizeof(txtBuf))) {
                            elem->text = txtBuf;
                            isDirty = true;
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Text Color:");
                        float tCol[4] = { elem->textColor.r, elem->textColor.g, elem->textColor.b, elem->textColor.a };
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::ColorEdit4("##ElemTxtCol", tCol)) {
                            elem->textColor = glm::vec4(tCol[0], tCol[1], tCol[2], tCol[3]);
                            isDirty = true;
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Font Size:");
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::SliderFloat("##ElemFontSize", &elem->fontSize, 8.0f, 72.0f, "%.0f px")) {
                            isDirty = true;
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Alignment:");
                        const char* aligns[] = { "Left", "Center", "Right" };
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::Combo("##ElemAlign", &elem->textAlignment, aligns, 3)) {
                            isDirty = true;
                        }
                    }
                } else if (elem->type == UIElementType::ProgressBar) {
                    if (ImGui::CollapsingHeader("📊 Progress Bar Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextDisabled("Current Value:");
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::SliderFloat("##ElemProgVal", &elem->progressValue, elem->progressMin, elem->progressMax, "%.1f")) {
                            isDirty = true;
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Min / Max Range:");
                        float range[2] = { elem->progressMin, elem->progressMax };
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::DragFloat2("##ElemProgRange", range, 1.0f)) {
                            elem->progressMin = range[0];
                            elem->progressMax = std::max(range[0] + 0.1f, range[1]);
                            isDirty = true;
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Fill Color:");
                        float fCol[4] = { elem->progressFillColor.r, elem->progressFillColor.g, elem->progressFillColor.b, elem->progressFillColor.a };
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::ColorEdit4("##ElemProgFillCol", fCol)) {
                            elem->progressFillColor = glm::vec4(fCol[0], fCol[1], fCol[2], fCol[3]);
                            isDirty = true;
                        }

                        ImGui::Spacing();
                        if (ImGui::Checkbox("Show Percentage Label", &elem->showPercentage)) {
                            isDirty = true;
                        }
                    }
                } else if (elem->type == UIElementType::Slider) {
                    if (ImGui::CollapsingHeader("🎚️ Slider Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextDisabled("Current Value:");
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::SliderFloat("##ElemSliderVal", &elem->sliderValue, elem->sliderMin, elem->sliderMax, "%.1f")) {
                            isDirty = true;
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Min / Max Range:");
                        float range[2] = { elem->sliderMin, elem->sliderMax };
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::DragFloat2("##ElemSliderRange", range, 1.0f)) {
                            elem->sliderMin = range[0];
                            elem->sliderMax = std::max(range[0] + 0.1f, range[1]);
                            isDirty = true;
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Thumb Color:");
                        float thCol[4] = { elem->sliderThumbColor.r, elem->sliderThumbColor.g, elem->sliderThumbColor.b, elem->sliderThumbColor.a };
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::ColorEdit4("##ElemSliderThumbCol", thCol)) {
                            elem->sliderThumbColor = glm::vec4(thCol[0], thCol[1], thCol[2], thCol[3]);
                            isDirty = true;
                        }
                    }
                } else if (elem->type == UIElementType::Checkbox) {
                    if (ImGui::CollapsingHeader("☑️ Checkbox Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (ImGui::Checkbox("Checked", &elem->isChecked)) {
                            isDirty = true;
                        }

                        ImGui::Spacing();
                        char txtBuf[256];
                        strncpy_s(txtBuf, elem->text.c_str(), sizeof(txtBuf));
                        ImGui::TextDisabled("Label Text:");
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::InputText("##ElemCheckText", txtBuf, sizeof(txtBuf))) {
                            elem->text = txtBuf;
                            isDirty = true;
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Checkmark Color:");
                        float cCol[4] = { elem->checkboxCheckColor.r, elem->checkboxCheckColor.g, elem->checkboxCheckColor.b, elem->checkboxCheckColor.a };
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::ColorEdit4("##ElemCheckCol", cCol)) {
                            elem->checkboxCheckColor = glm::vec4(cCol[0], cCol[1], cCol[2], cCol[3]);
                            isDirty = true;
                        }
                    }
                } else if (elem->type == UIElementType::Image) {
                    if (ImGui::CollapsingHeader("🖼️ Image Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
                        char imgBuf[256];
                        strncpy_s(imgBuf, elem->texturePath.c_str(), sizeof(imgBuf));
                        ImGui::TextDisabled("Texture Path:");
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::InputText("##ElemTexPath", imgBuf, sizeof(imgBuf))) {
                            elem->texturePath = imgBuf;
                            isDirty = true;
                        }
                    }
                }

                ImGui::Spacing();

                // C++ Event Triggers & Script Bindings
                if (ImGui::CollapsingHeader("⚡ Event Triggers & Functions", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled("Bind UI events to Level Behaviour Scripts:");
                    ImGui::Spacing();

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
                        { "onpress", "On Mouse Down (onpress)", "🔽" },
                        { "onvaluechanged", "On Value Changed", "🔀" }
                    };

                    for (const auto& ev : kEvents) {
                        ImGui::PushID(ev.id);
                        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.125f, 0.125f, 0.153f, 0.85f));

                        float cardHeight = 118.0f;
                        UIEventBinding* bound = FindEventBinding(*elem, ev.id);
                        bool isBound = (bound != nullptr && !bound->targetFunction.empty());
                        if (isBound) cardHeight = 158.0f;

                        if (ImGui::BeginChild("EventCard", ImVec2(0, cardHeight), true)) {
                            ImGui::TextColored(isBound ? ImVec4(0.2f, 0.9f, 0.4f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "%s %s", ev.icon, ev.label);
                            ImGui::Spacing();

                            int curScriptIdx = 0;
                            if (isBound) {
                                for (size_t i = 1; i < scriptOptions.size(); ++i) {
                                    if (scriptOptions[i].actorId == bound->targetActorId &&
                                        scriptOptions[i].behaviourClass == bound->behaviourClass) {
                                        curScriptIdx = (int)i;
                                        break;
                                    }
                                }
                            }

                            ImGui::TextDisabled("Level Behaviour Script:");
                            ImGui::SetNextItemWidth(-1.0f);
                            if (ImGui::BeginCombo("##ScriptSelect", scriptOptions[curScriptIdx].label.c_str())) {
                                for (size_t i = 0; i < scriptOptions.size(); ++i) {
                                    bool isSelected = (curScriptIdx == (int)i);
                                    if (ImGui::Selectable(scriptOptions[i].label.c_str(), isSelected)) {
                                        if (i == 0) {
                                            RemoveEventBinding(*elem, ev.id);
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
                                            SetEventBinding(*elem, nb);
                                            isDirty = true;
                                            if (autoSave) Save();
                                        }
                                    }
                                    if (isSelected) ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }

                            if (curScriptIdx > 0 && bound != nullptr) {
                                auto funcs = ExtractScriptFunctions(scriptOptions[curScriptIdx].scriptPath, scriptOptions[curScriptIdx].behaviourClass);

                                int curFuncIdx = 0;
                                for (size_t f = 0; f < funcs.size(); ++f) {
                                    if (funcs[f] == bound->targetFunction) {
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
                                            bound->targetFunction = funcs[f];
                                            isDirty = true;
                                            if (autoSave) Save();
                                        }
                                        if (isFuncSelected) ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }

                                ImGui::Spacing();
#if __has_include(<EngineScene/Scene.h>)
                                if (ImGui::SmallButton("▶ Test Run")) {
                                    ExecuteScriptFunction(scene, *bound);
                                }
                                ImGui::SameLine();
#endif
                                if (ImGui::SmallButton("🗑 Unbind")) {
                                    RemoveEventBinding(*elem, ev.id);
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
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.082f, 0.082f, 0.102f, 1.0f));

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
        // Active tab workspace rendering is managed in EngineUI tab system
    }

    bool HasOpenEditors() const {
        return !m_editors.empty();
    }

private:
    UIEditorManager() = default;
    std::vector<std::unique_ptr<UIEditorWindow>> m_editors;
};

} // namespace EunoiaUI
