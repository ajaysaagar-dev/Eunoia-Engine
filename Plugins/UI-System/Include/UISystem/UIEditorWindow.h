#pragma once

#include "UserInterface.h"
#include "UISystem.h"
#include "HtmlTailwindParser.h"
#include <imgui.h>
#include <imgui_internal.h>
#if __has_include(<imgui_impl_glfw.h>)
#include <imgui_impl_glfw.h>
#endif
#if __has_include(<imgui_impl_opengl3.h>)
#include <imgui_impl_opengl3.h>
#endif
#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
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

    UIEditorWindow(const std::string& path, const std::string& name = "") {
        filePath = path;
        if (!path.empty()) {
            if (!ui.LoadFromFile(path)) {
                ui.name = name.empty() ? "New UI" : name;
                ui.SaveToHtml();
                ui.SaveToFile(path);
            }
        } else {
            ui.name = name.empty() ? "New UI" : name;
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
            ImGui::TextColored(ImVec4(0.20f, 0.85f, 0.40f, 1.0f), "● HTML + TailwindCSS Live");

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
                ImGui::SetTooltip("Open corresponding HTML and Tailwind CSS in VS Code.\nLive edits will immediately reflect on this canvas in real-time.");
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

            // Render HTML + Tailwind CSS Tree
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
            ImGuiIO& io = ImGui::GetIO();

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

inline void RunStandaloneUIEditor(const std::string& filePath, const std::string& name = "") {
    // Create dedicated secondary GLFW window for UI Editor
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);

    const int WIN_W = 1280;
    const int WIN_H = 800;

    std::string winTitle = "Eunoia Engine - UI Editor";
    if (!name.empty()) winTitle += " - " + name;
    else if (!filePath.empty()) winTitle += " - " + std::filesystem::path(filePath).stem().string();

    GLFWwindow* uiWindow = glfwCreateWindow(WIN_W, WIN_H, winTitle.c_str(), nullptr, nullptr);
    if (!uiWindow) {
        std::cerr << "[UIEditor] Failed to create standalone UI Editor window!\n";
        return;
    }

    glfwMakeContextCurrent(uiWindow);
    glfwSwapInterval(1);

    // Save previous ImGui context
    ImGuiContext* prevContext = ImGui::GetCurrentContext();

    IMGUI_CHECKVERSION();
    ImGuiContext* uiContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(uiContext);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    // Load fonts
    ImFontConfig fontConfig;
    fontConfig.OversampleH = 1;
    fontConfig.OversampleV = 1;
    ImFont* mainFont = nullptr;
    if (std::filesystem::exists("C:\\Windows\\Fonts\\segoeui.ttf")) {
        mainFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f, &fontConfig);
    } else if (std::filesystem::exists("C:\\Windows\\Fonts\\arial.ttf")) {
        mainFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 18.0f, &fontConfig);
    }
    if (mainFont && std::filesystem::exists("C:\\Windows\\Fonts\\seguisym.ttf")) {
        ImFontConfig symbolConfig;
        symbolConfig.MergeMode = true;
        symbolConfig.OversampleH = 2;
        symbolConfig.OversampleV = 2;
        static const ImWchar symbolRanges[] = { 0x2000, 0x2BFF, 0 };
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisym.ttf", 18.0f, &symbolConfig, symbolRanges);
    }
    if (!mainFont) {
        io.Fonts->AddFontDefault();
        io.FontGlobalScale = 1.25f;
    }

    // Modern Dark Theme
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.15f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.13f, 0.18f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.15f, 0.20f, 0.30f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.30f, 0.45f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.10f, 0.40f, 0.70f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.18f, 0.20f, 0.28f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.30f, 0.42f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.10f, 0.45f, 0.80f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.16f, 0.22f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.24f, 0.32f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.30f, 0.40f, 1.0f);

    ImGui_ImplGlfw_InitForOpenGL(uiWindow, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    UIEditorWindow editor(filePath, name);

    // Standalone loop
    while (!glfwWindowShouldClose(uiWindow)) {
        glfwPollEvents();

        int displayW = 0, displayH = 0;
        glfwGetFramebufferSize(uiWindow, &displayW, &displayH);
        if (displayW <= 0 || displayH <= 0) continue;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)displayW, (float)displayH));
        ImGuiWindowFlags winFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
        if (ImGui::Begin("UIEditorMainWindow", nullptr, winFlags)) {
            editor.RenderUIEditorContent();
        }
        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::Render();
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(uiWindow);
    }

    // Auto save on exit
    editor.Save();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(uiContext);

    glfwDestroyWindow(uiWindow);

    // Restore previous ImGui context
    if (prevContext) {
        ImGui::SetCurrentContext(prevContext);
    }
}

class UIEditorManager {
public:
    static UIEditorManager& Get() {
        static UIEditorManager instance;
        return instance;
    }

    void Open(const std::string& filePath, const std::string& name = "") {
        RunStandaloneUIEditor(filePath, name);
    }

    void RenderAll() {
        // UI Editor runs in its own separate window
    }

    bool HasOpenEditors() const {
        return false;
    }

private:
    UIEditorManager() = default;
};

} // namespace EunoiaUI
