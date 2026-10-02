#pragma once

#include "UserInterface.h"
#include "UISystem.h"
#include "HtmlCssParser.h"
#include <imgui.h>
#include <imgui_internal.h>

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
            STARTUPINFOA si = { sizeof(STARTUPINFOA) };
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {};
            std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
            cmdBuf.push_back('\0');
            if (CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                launched = true;
                break;
            }
        }
    }

    if (!launched) {
        std::string cmdLine = "cmd.exe /c code \"" + workspace.string() + "\" \"" + targetFile.string() + "\"";
        STARTUPINFOA si = { sizeof(STARTUPINFOA) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
        cmdBuf.push_back('\0');
        if (CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            launched = true;
        } else {
            ShellExecuteA(NULL, "open", targetFile.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
    }
#else
    std::string cmd = "code \"" + workspace.string() + "\" \"" + targetFile.string() + "\" &";
    system(cmd.c_str());
#endif
}

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

    void Save() {
        if (!filePath.empty()) {
            ui.SaveToFile(filePath);
            isDirty = false;
        }
    }

    void RenderUIEditorContent() {
        // Real-time file watcher check on the HTML file
        if (ui.CheckAndReloadHtml()) {
            lastReloadStatus = "Updated from HTML just now";
            if (autoSave) {
                Save();
            }
        }

        // Top Control Header Bar
        RenderTopToolbar();

        // Canvas Area (Fills entire main view)
        RenderCanvasView();
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
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.20f, 0.85f, 0.40f, 1.0f), "● HTML + CSS Live");

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

    void RenderCanvasView() {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        if (ImGui::BeginChild("UICanvasMainArea", ImVec2(0, 0), true, flags)) {
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
            ui.ProcessInput(mousePos, isMouseDown, isMousePressed, isMouseReleased, mouseWheel, canvasOrigin, currentScale);

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
                    ImU32 bgCol = IM_COL32((int)(el.backgroundColor.r * 255), (int)(el.backgroundColor.g * 255), (int)(el.backgroundColor.b * 255), (int)(el.backgroundColor.a * 255));
                    ImU32 bdCol = IM_COL32((int)(el.borderColor.r * 255), (int)(el.borderColor.g * 255), (int)(el.borderColor.b * 255), (int)(el.borderColor.a * 255));
                    if (el.filled) drawList->AddRectFilled(pMin, pMax, bgCol, el.cornerRounding * currentScale);
                    if (el.borderThickness > 0.0f) drawList->AddRect(pMin, pMax, bdCol, el.cornerRounding * currentScale, 0, el.borderThickness * currentScale);
                } else if (el.type == UIElementType::Text) {
                    ImU32 txtCol = IM_COL32((int)(el.textColor.r * 255), (int)(el.textColor.g * 255), (int)(el.textColor.b * 255), (int)(el.textColor.a * 255));
                    float fontSize = el.fontSize * currentScale;
                    drawList->AddText(nullptr, fontSize, pMin, txtCol, el.text.c_str());
                }
            }

            drawList->PopClipRect();

            // Canvas Info Tag
            char infoTag[128];
            snprintf(infoTag, sizeof(infoTag), "Canvas: %.0f x %.0f  |  Scale: %.0f%%", ui.canvasResolution.x, ui.canvasResolution.y, zoom * 100.0f);
            drawList->AddText(ImVec2(canvasAreaPos.x + 16.0f, canvasAreaPos.y + canvasAreaSize.y - 28.0f), IM_COL32(160, 175, 195, 200), infoTag);

            // Canvas Interaction (Pan & Zoom)
            bool isHovered = ImGui::IsWindowHovered();

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
};

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

    void RenderActiveTabWorkspace(const glm::vec2& pos, const glm::vec2& size) {
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
                activeEditor->RenderUIEditorContent();
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
