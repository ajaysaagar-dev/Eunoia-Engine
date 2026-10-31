#include "Behaviour/BehaviourEditorWindow.h"
#include <EngineScene/Scene.h>
#include <EngineAssets/AssetRegistry.h>
#include <EngineAssets/AssetManager.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Eunoia {

static void LaunchCodeEditor(const std::filesystem::path& targetFile, const std::filesystem::path& targetWorkspace = {}) {
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

BehaviourEditorWindow::BehaviourEditorWindow(const std::string& path, const std::string& inName) {
    filePath = path;
    if (!inName.empty()) {
        name = inName;
    } else if (!path.empty()) {
        name = std::filesystem::path(path).stem().string();
    } else {
        name = "NewBehaviour";
    }

    if (!path.empty()) {
        if (!asset.LoadFromFile(path)) {
            asset.name = name;
            asset.SaveToFile(path);
        }
    } else {
        asset.name = name;
    }

    if (!asset.components.empty()) {
        selectedComponentId = asset.components.front().id;
    }
}

void BehaviourEditorWindow::Save() {
    if (!filePath.empty()) {
        asset.SaveToFile(filePath);
        isDirty = false;
    }
}

void BehaviourEditorWindow::CreateInternalScript(const std::string& scriptName) {
    std::string cleanName = scriptName;
    while (!cleanName.empty() && isspace((unsigned char)cleanName.front())) cleanName.erase(cleanName.begin());
    while (!cleanName.empty() && isspace((unsigned char)cleanName.back())) cleanName.pop_back();
    if (cleanName.empty()) cleanName = "NewScript";

    std::filesystem::path assetDir = std::filesystem::path(filePath).parent_path();
    std::filesystem::path internalDir = assetDir / ".internal" / asset.name;
    std::error_code ec;
    std::filesystem::create_directories(internalDir, ec);

    std::filesystem::path cppPath = internalDir / (cleanName + ".cpp");
    std::filesystem::path hPath = internalDir / (cleanName + ".h");

    if (!std::filesystem::exists(cppPath, ec)) {
        std::ofstream hf(hPath);
        if (hf.is_open()) {
            hf << "#pragma once\n"
               << "#include <EngineScene/EunoiaBehaviour.h>\n\n"
               << "class " << cleanName << " : public EunoiaBehaviour {\n"
               << "public:\n"
               << "    // Public assignable variables\n"
               << "    float speed = 5.0f;\n"
               << "    bool isActive = true;\n"
               << "    int targetComponentId = 0;\n\n"
               << "    void Start() override;\n"
               << "    void Update(float deltaTime) override;\n"
               << "};\n";
        }

        std::ofstream cf(cppPath);
        if (cf.is_open()) {
            cf << "#include \"" << cleanName << ".h\"\n"
               << "#include <iostream>\n\n"
               << "REGISTER_BEHAVIOUR(" << cleanName << ")\n\n"
               << "void " << cleanName << "::Start() {\n"
               << "    // Initialize logic here\n"
               << "}\n\n"
               << "void " << cleanName << "::Update(float deltaTime) {\n"
               << "    // Tick logic here\n"
               << "}\n";
        }
    }

    AttachedBehaviourScript* s = asset.AddAttachedScript(cleanName, cppPath.string());
    if (s) {
        selectedScriptId = s->id;
        isSelectingScript = true;
    }
    isDirty = true;
    if (autoSave) Save();

    LaunchCodeEditor(cppPath);
}

void BehaviourEditorWindow::RenderContent(Scene* mainLevelScene) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        Save();
    }

    RenderTopToolbar();

    float totalW = ImGui::GetContentRegionAvail().x;
    float totalH = ImGui::GetContentRegionAvail().y;

    float leftW = 280.0f;
    float rightW = 340.0f;
    float centerW = std::max(200.0f, totalW - leftW - rightW);

    float bottomCBHeight = 180.0f;
    float centerH = std::max(150.0f, totalH - bottomCBHeight);

    // Left Column: Outliner (Top) + Behaviour Scripts (Bottom)
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("LeftCol", ImVec2(leftW, totalH), false);
    {
        float outlinerH = totalH * 0.55f;
        RenderOutlinerPanel(ImVec2(leftW, outlinerH));
        RenderBehaviourScriptsPanel(ImVec2(leftW, totalH - outlinerH));
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Center Column: 3D Viewport (Top) + Content Browser (Bottom)
    ImGui::BeginChild("CenterCol", ImVec2(centerW, totalH), false);
    {
        RenderViewportPanel(ImVec2(centerW, centerH));
        RenderContentBrowserPanel(ImVec2(centerW, bottomCBHeight));
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Column: Details Inspector
    RenderDetailsPanel(ImVec2(rightW, totalH));

    ImGui::PopStyleVar();
}

void BehaviourEditorWindow::RenderTopToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));

    if (ImGui::BeginChild("TopToolbar", ImVec2(0, 44.0f), true, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
        ImGui::TextColored(ImVec4(0.0f, 0.82f, 1.0f, 1.0f), "🔷 Blueprint:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", asset.name.c_str());
        if (isDirty) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.20f, 1.0f), "*");
        }

        ImGui::SameLine();
        float availW = ImGui::GetContentRegionAvail().x;
        float btnW = 320.0f;
        if (availW > btnW) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - btnW));
        }

        if (ImGui::Button("↺ Reset View", ImVec2(95.0f, 26.0f))) {
            cameraTarget = glm::vec3(0.0f, 1.0f, 0.0f);
            cameraYaw = 45.0f;
            cameraPitch = 25.0f;
            cameraDistance = 5.0f;
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.50f, 0.85f, 1.0f));
        if (ImGui::Button("💾 Save", ImVec2(75.0f, 26.0f))) {
            Save();
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::Checkbox("⚡ Autosave", &autoSave);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

void BehaviourEditorWindow::RenderOutlinerPanel(const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.082f, 0.082f, 0.102f, 1.0f));
    if (ImGui::BeginChild("OutlinerPanel", size, true)) {
        ImGui::TextColored(ImVec4(0.290f, 0.486f, 1.0f, 1.0f), "🌳 Components");
        ImGui::SameLine(ImGui::GetWindowWidth() - 75.0f);
        if (ImGui::SmallButton("+ Add")) {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (ImGui::BeginPopup("AddComponentPopup")) {
            if (ImGui::MenuItem("Static Mesh")) {
                asset.AddComponent(PrimitiveType::Cube, "StaticMesh", selectedComponentId);
                isDirty = true;
            }
            if (ImGui::MenuItem("Cube")) {
                asset.AddComponent(PrimitiveType::Cube, "Cube", selectedComponentId);
                isDirty = true;
            }
            if (ImGui::MenuItem("Sphere")) {
                asset.AddComponent(PrimitiveType::Sphere, "Sphere", selectedComponentId);
                isDirty = true;
            }
            if (ImGui::MenuItem("Cylinder")) {
                asset.AddComponent(PrimitiveType::Cylinder, "Cylinder", selectedComponentId);
                isDirty = true;
            }
            if (ImGui::MenuItem("Plane")) {
                asset.AddComponent(PrimitiveType::Plane, "Plane", selectedComponentId);
                isDirty = true;
            }
            if (ImGui::MenuItem("Point Light")) {
                asset.AddComponent(PrimitiveType::PointLight, "PointLight", selectedComponentId);
                isDirty = true;
            }
            if (ImGui::MenuItem("Spot Light")) {
                asset.AddComponent(PrimitiveType::SpotLight, "SpotLight", selectedComponentId);
                isDirty = true;
            }
            if (ImGui::MenuItem("Camera")) {
                asset.AddComponent(PrimitiveType::Camera, "Camera", selectedComponentId);
                isDirty = true;
            }
            if (ImGui::MenuItem("Empty Scene Component")) {
                asset.AddComponent(PrimitiveType::Empty, "SceneComponent", selectedComponentId);
                isDirty = true;
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();
        ImGui::Spacing();

        for (auto& c : asset.components) {
            bool isSelected = (!isSelectingScript && selectedComponentId == c.id);
            const char* icon = "📦";
            if (c.type == PrimitiveType::Empty) icon = "⚓";
            else if (c.type == PrimitiveType::PointLight || c.type == PrimitiveType::SpotLight) icon = "💡";
            else if (c.type == PrimitiveType::Camera) icon = "🎥";

            std::string label = std::string(icon) + " " + c.name;
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

            ImGui::TreeNodeEx((void*)(uintptr_t)c.id, flags, "%s", label.c_str());
            if (ImGui::IsItemClicked()) {
                selectedComponentId = c.id;
                isSelectingScript = false;
            }

            if (ImGui::BeginPopupContextItem()) {
                if (c.id != asset.components.front().id && ImGui::MenuItem("🗑 Delete Component")) {
                    asset.RemoveComponent(c.id);
                    selectedComponentId = asset.components.front().id;
                    isDirty = true;
                    if (autoSave) Save();
                }
                ImGui::EndPopup();
            }
        }

        // Accept Drag & Drop of 3D Models onto Outliner
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                std::string pStr((const char*)payload->Data);
                std::stringstream ss(pStr);
                std::string item;
                while (std::getline(ss, item, '\n')) {
                    if (item.rfind("ASSET:", 0) == 0) {
                        std::string aPath = item.substr(6);
                        std::string ext = std::filesystem::path(aPath).extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
                            auto* comp = asset.AddComponent(PrimitiveType::Cube, std::filesystem::path(aPath).stem().string(), selectedComponentId);
                            if (comp) {
                                comp->meshPath = aPath;
                                selectedComponentId = comp->id;
                                isSelectingScript = false;
                                isDirty = true;
                                if (autoSave) Save();
                            }
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void BehaviourEditorWindow::RenderBehaviourScriptsPanel(const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.075f, 0.095f, 1.0f));
    if (ImGui::BeginChild("BehaviourScriptsPanel", size, true)) {
        ImGui::TextColored(ImVec4(0.85f, 0.45f, 1.0f, 1.0f), "⚡ Behaviour Scripts");
        ImGui::SameLine(ImGui::GetWindowWidth() - 75.0f);
        if (ImGui::SmallButton("+ Add")) {
            ImGui::OpenPopup("AddScriptModal");
        }

        static char newScriptNameBuf[64] = "MyMechanic";
        if (ImGui::BeginPopup("AddScriptModal")) {
            ImGui::Text("New Behaviour Script Name:");
            ImGui::InputText("##NewScrName", newScriptNameBuf, sizeof(newScriptNameBuf));
            if (ImGui::Button("Create & Open", ImVec2(120, 0))) {
                CreateInternalScript(newScriptNameBuf);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (asset.attachedScripts.empty()) {
            ImGui::TextDisabled("No scripts attached.\nClick '+ Add' to attach a script.");
        } else {
            for (size_t i = 0; i < asset.attachedScripts.size(); ++i) {
                auto& s = asset.attachedScripts[i];
                bool isSelected = (isSelectingScript && selectedScriptId == s.id);

                ImGui::PushID(s.id.c_str());

                // Enable/disable checkbox
                ImGui::Checkbox("##ScriptEn", &s.enabled);
                ImGui::SameLine();

                // Selectable row
                std::string itemText = "📜 " + s.scriptName;
                if (!s.enabled) itemText += " (Disabled)";

                if (ImGui::Selectable(itemText.c_str(), isSelected, 0, ImVec2(ImGui::GetContentRegionAvail().x - 45.0f, 20.0f))) {
                    selectedScriptId = s.id;
                    isSelectingScript = true;
                }

                // Show Edit button on hover
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Click to inspect variables in Details panel.");
                }

                ImGui::SameLine(ImGui::GetWindowWidth() - 48.0f);
                if (ImGui::SmallButton("✏")) {
                    LaunchCodeEditor(s.sourceCppPath);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Open %s in VS Code", s.scriptName.c_str());
                }

                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void BehaviourEditorWindow::RenderViewportPanel(const ImVec2& size) {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::BeginChild("ViewportPanel", size, true, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 vpPos = ImGui::GetCursorScreenPos();
        ImVec2 vpSize = ImGui::GetContentRegionAvail();

        if (vpSize.x <= 10.0f || vpSize.y <= 10.0f) {
            ImGui::EndChild();
            return;
        }

        // Viewport Background
        ImU32 bgCol = IM_COL32(16, 18, 24, 255);
        dl->AddRectFilled(vpPos, ImVec2(vpPos.x + vpSize.x, vpPos.y + vpSize.y), bgCol);

        // Clip to viewport
        dl->PushClipRect(vpPos, ImVec2(vpPos.x + vpSize.x, vpPos.y + vpSize.y), true);

        // Orbit Camera & Projection Math
        float radYaw = glm::radians(cameraYaw);
        float radPitch = glm::radians(cameraPitch);
        glm::vec3 camPos = cameraTarget + glm::vec3(
            cameraDistance * std::cos(radPitch) * std::sin(radYaw),
            cameraDistance * std::sin(radPitch),
            cameraDistance * std::cos(radPitch) * std::cos(radYaw)
        );

        glm::mat4 view = glm::lookAt(camPos, cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        float aspect = vpSize.x / std::max(1.0f, vpSize.y);
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 500.0f);
        glm::mat4 vpMat = proj * view;

        auto project = [&](const glm::vec3& worldPos, ImVec2& outScreen) -> bool {
            glm::vec4 clip = vpMat * glm::vec4(worldPos, 1.0f);
            if (clip.w <= 0.01f) return false;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.z < -1.0f || ndc.z > 1.0f) return false;
            outScreen.x = vpPos.x + (ndc.x * 0.5f + 0.5f) * vpSize.x;
            outScreen.y = vpPos.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * vpSize.y;
            return true;
        };

        // Grid Lines on XZ plane
        float gridExtent = 5.0f;
        float gridStep = 0.5f;
        for (float g = -gridExtent; g <= gridExtent; g += gridStep) {
            ImVec2 p0, p1;
            ImU32 gridCol = (std::abs(g) < 0.01f) ? IM_COL32(70, 80, 100, 200) : IM_COL32(35, 40, 52, 120);
            if (project(glm::vec3(g, 0.0f, -gridExtent), p0) && project(glm::vec3(g, 0.0f, gridExtent), p1)) {
                dl->AddLine(p0, p1, gridCol, 1.0f);
            }
            if (project(glm::vec3(-gridExtent, 0.0f, g), p0) && project(glm::vec3(gridExtent, 0.0f, g), p1)) {
                dl->AddLine(p0, p1, gridCol, 1.0f);
            }
        }

        // Render Components in 3D
        for (const auto& c : asset.components) {
            if (!c.visible) continue;
            glm::vec3 center = c.localPosition;
            bool isSelected = (!isSelectingScript && selectedComponentId == c.id);
            ImU32 compCol = isSelected ? IM_COL32(0, 180, 255, 255) : IM_COL32(200, 205, 220, 200);

            ImVec2 cScreen;
            if (project(center, cScreen)) {
                if (c.type == PrimitiveType::PointLight || c.type == PrimitiveType::SpotLight) {
                    dl->AddCircleFilled(cScreen, 8.0f, IM_COL32((int)(c.lightColor.r * 255), (int)(c.lightColor.g * 255), (int)(c.lightColor.b * 255), 255));
                    dl->AddCircle(cScreen, 10.0f, compCol, 0, 1.5f);
                } else if (c.type == PrimitiveType::Empty) {
                    dl->AddCircleFilled(cScreen, 4.0f, compCol);
                } else {
                    // Draw 3D Bounding Wireframe Box
                    glm::vec3 h = c.localScale * 0.5f;
                    glm::vec3 corners[8] = {
                        center + glm::vec3(-h.x, -h.y, -h.z),
                        center + glm::vec3( h.x, -h.y, -h.z),
                        center + glm::vec3( h.x,  h.y, -h.z),
                        center + glm::vec3(-h.x,  h.y, -h.z),
                        center + glm::vec3(-h.x, -h.y,  h.z),
                        center + glm::vec3( h.x, -h.y,  h.z),
                        center + glm::vec3( h.x,  h.y,  h.z),
                        center + glm::vec3(-h.x,  h.y,  h.z),
                    };

                    ImVec2 sc[8];
                    bool allProj = true;
                    for (int i = 0; i < 8; ++i) {
                        if (!project(corners[i], sc[i])) { allProj = false; break; }
                    }

                    if (allProj) {
                        dl->AddLine(sc[0], sc[1], compCol, 1.5f);
                        dl->AddLine(sc[1], sc[2], compCol, 1.5f);
                        dl->AddLine(sc[2], sc[3], compCol, 1.5f);
                        dl->AddLine(sc[3], sc[0], compCol, 1.5f);
                        dl->AddLine(sc[4], sc[5], compCol, 1.5f);
                        dl->AddLine(sc[5], sc[6], compCol, 1.5f);
                        dl->AddLine(sc[6], sc[7], compCol, 1.5f);
                        dl->AddLine(sc[7], sc[4], compCol, 1.5f);
                        dl->AddLine(sc[0], sc[4], compCol, 1.5f);
                        dl->AddLine(sc[1], sc[5], compCol, 1.5f);
                        dl->AddLine(sc[2], sc[6], compCol, 1.5f);
                        dl->AddLine(sc[3], sc[7], compCol, 1.5f);
                    }
                }
                dl->AddText(ImVec2(cScreen.x + 8.0f, cScreen.y - 8.0f), compCol, c.name.c_str());
            }
        }

        // Viewport Overlay Info
        dl->AddText(ImVec2(vpPos.x + 10.0f, vpPos.y + 10.0f), IM_COL32(160, 180, 200, 200), "RMB+Drag: Orbit  |  Wheel: Zoom  |  Drag Model to Spawn");

        dl->PopClipRect();

        // Mouse Orbit Navigation
        ImGuiIO& io = ImGui::GetIO();
        bool isHovered = ImGui::IsWindowHovered();
        if (isHovered && io.MouseWheel != 0.0f) {
            cameraDistance = std::clamp(cameraDistance - io.MouseWheel * 0.5f, 1.0f, 50.0f);
        }

        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            isOrbiting = true;
            mousePrevPos = io.MousePos;
        }

        if (isOrbiting) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                float dx = io.MousePos.x - mousePrevPos.x;
                float dy = io.MousePos.y - mousePrevPos.y;
                cameraYaw += dx * 0.4f;
                cameraPitch = std::clamp(cameraPitch + dy * 0.4f, -85.0f, 85.0f);
                mousePrevPos = io.MousePos;
            } else {
                isOrbiting = false;
            }
        }

        // Drag & Drop of 3D Models directly into Viewport
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                std::string pStr((const char*)payload->Data);
                std::stringstream ss(pStr);
                std::string item;
                while (std::getline(ss, item, '\n')) {
                    if (item.rfind("ASSET:", 0) == 0) {
                        std::string aPath = item.substr(6);
                        std::string ext = std::filesystem::path(aPath).extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
                            auto* comp = asset.AddComponent(PrimitiveType::Cube, std::filesystem::path(aPath).stem().string(), selectedComponentId);
                            if (comp) {
                                comp->meshPath = aPath;
                                selectedComponentId = comp->id;
                                isSelectingScript = false;
                                isDirty = true;
                                if (autoSave) Save();
                            }
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::EndChild();
}

void BehaviourEditorWindow::RenderDetailsPanel(const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.082f, 0.082f, 0.102f, 1.0f));
    if (ImGui::BeginChild("DetailsPanel", size, true)) {
        ImGui::TextColored(ImVec4(0.290f, 0.486f, 1.0f, 1.0f), "⚡ Details Inspector");
        ImGui::Separator();
        ImGui::Spacing();

        if (isSelectingScript) {
            // ================================================================
            // CASE B: BEHAVIOUR SCRIPT SELECTED
            // ================================================================
            AttachedBehaviourScript* s = asset.FindAttachedScript(selectedScriptId);
            if (!s) {
                ImGui::TextDisabled("Select a script or component to inspect.");
            } else {
                ImGui::TextColored(ImVec4(0.85f, 0.45f, 1.0f, 1.0f), "📜 Script: %s", s->scriptName.c_str());
                ImGui::Spacing();

                if (ImGui::Checkbox("Enabled", &s->enabled)) {
                    isDirty = true;
                }

                ImGui::SameLine();
                if (ImGui::SmallButton("Open in VS Code")) {
                    LaunchCodeEditor(s->sourceCppPath);
                }

                ImGui::Spacing();
                ImGui::TextDisabled("Source: %s", s->sourceCppPath.c_str());
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.290f, 0.486f, 1.0f, 1.0f), "Configured Public Variables:");
                ImGui::Spacing();

                for (size_t pIdx = 0; pIdx < s->propertyOverrides.size(); ++pIdx) {
                    auto& prop = s->propertyOverrides[pIdx];
                    ImGui::PushID((int)pIdx);

                    ImGui::TextDisabled("%s (%s):", prop.propertyName.c_str(), prop.typeName.c_str());
                    ImGui::SetNextItemWidth(-1.0f);

                    if (prop.typeName == "object_ref") {
                        // Component Reference Picker strictly listing THIS Behaviour's components!
                        std::string previewName = "(None)";
                        for (const auto& c : asset.components) {
                            if (c.id == prop.targetComponentId) {
                                previewName = c.name + " (ID: " + std::to_string(c.id) + ")";
                                break;
                            }
                        }

                        if (ImGui::BeginCombo("##CompRef", previewName.c_str())) {
                            if (ImGui::Selectable("(None)", prop.targetComponentId == 0)) {
                                prop.targetComponentId = 0;
                                isDirty = true;
                            }
                            for (const auto& c : asset.components) {
                                bool isCur = (prop.targetComponentId == c.id);
                                std::string itemLabel = c.name + " (ID: " + std::to_string(c.id) + ")";
                                if (ImGui::Selectable(itemLabel.c_str(), isCur)) {
                                    prop.targetComponentId = c.id;
                                    isDirty = true;
                                }
                            }
                            ImGui::EndCombo();
                        }
                    } else if (prop.typeName == "float") {
                        if (ImGui::DragFloat("##FloatVal", &prop.floatVal, 0.1f)) isDirty = true;
                    } else if (prop.typeName == "int") {
                        if (ImGui::DragInt("##IntVal", &prop.intVal, 1)) isDirty = true;
                    } else if (prop.typeName == "bool") {
                        if (ImGui::Checkbox("##BoolVal", &prop.boolVal)) isDirty = true;
                    } else if (prop.typeName == "string") {
                        char sBuf[128];
                        strncpy_s(sBuf, prop.stringVal.c_str(), sizeof(sBuf));
                        if (ImGui::InputText("##StrVal", sBuf, sizeof(sBuf))) {
                            prop.stringVal = sBuf;
                            isDirty = true;
                        }
                    }

                    ImGui::PopID();
                    ImGui::Spacing();
                }

                // Add Property Override Field
                if (ImGui::Button("+ Add Property Variable")) {
                    ScriptPropertyOverride newP;
                    newP.propertyName = "targetComponent";
                    newP.typeName = "object_ref";
                    newP.targetComponentId = 1;
                    s->propertyOverrides.push_back(newP);
                    isDirty = true;
                }
            }
        } else {
            // ================================================================
            // CASE A: COMPONENT SELECTED
            // ================================================================
            BehaviourComponentDef* c = asset.FindComponent(selectedComponentId);
            if (!c) {
                ImGui::TextDisabled("Select a component to inspect properties.");
            } else {
                char nameBuf[128];
                strncpy_s(nameBuf, c->name.c_str(), sizeof(nameBuf));
                ImGui::TextDisabled("Component Name:");
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##CompName", nameBuf, sizeof(nameBuf))) {
                    c->name = nameBuf;
                    isDirty = true;
                }

                ImGui::Spacing();
                ImGui::Checkbox("Visible", &c->visible);
                ImGui::Separator();
                ImGui::Spacing();

                // Transform
                if (ImGui::CollapsingHeader("📐 Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled("Location (X, Y, Z):");
                    float pos[3] = { c->localPosition.x, c->localPosition.y, c->localPosition.z };
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::DragFloat3("##Loc", pos, 0.1f)) {
                        c->localPosition = glm::vec3(pos[0], pos[1], pos[2]);
                        isDirty = true;
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Rotation (X, Y, Z):");
                    float rot[3] = { c->localRotation.x, c->localRotation.y, c->localRotation.z };
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::DragFloat3("##Rot", rot, 1.0f)) {
                        c->localRotation = glm::vec3(rot[0], rot[1], rot[2]);
                        isDirty = true;
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Scale (X, Y, Z):");
                    float scl[3] = { c->localScale.x, c->localScale.y, c->localScale.z };
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::DragFloat3("##Scl", scl, 0.05f)) {
                        c->localScale = glm::vec3(scl[0], scl[1], scl[2]);
                        isDirty = true;
                    }
                }

                ImGui::Spacing();

                // Mesh / Visuals
                if (ImGui::CollapsingHeader("📦 Visuals & Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
                    char meshBuf[256];
                    strncpy_s(meshBuf, c->meshPath.c_str(), sizeof(meshBuf));
                    ImGui::TextDisabled("Mesh Asset Path:");
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputText("##MeshPath", meshBuf, sizeof(meshBuf))) {
                        c->meshPath = meshBuf;
                        isDirty = true;
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Tint Color:");
                    float col[4] = { c->color.r, c->color.g, c->color.b, c->color.a };
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::ColorEdit4("##Color", col)) {
                        c->color = glm::vec4(col[0], col[1], col[2], col[3]);
                        isDirty = true;
                    }
                }

                if (c->type == PrimitiveType::PointLight || c->type == PrimitiveType::SpotLight) {
                    ImGui::Spacing();
                    if (ImGui::CollapsingHeader("💡 Light Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                        float lCol[3] = { c->lightColor.r, c->lightColor.g, c->lightColor.b };
                        ImGui::TextDisabled("Light Color:");
                        if (ImGui::ColorEdit3("##LCol", lCol)) {
                            c->lightColor = glm::vec3(lCol[0], lCol[1], lCol[2]);
                            isDirty = true;
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Intensity:");
                        if (ImGui::DragFloat("##LInt", &c->lightIntensity, 0.1f, 0.0f, 100.0f)) isDirty = true;

                        ImGui::Spacing();
                        ImGui::TextDisabled("Range:");
                        if (ImGui::DragFloat("##LRng", &c->lightRange, 0.5f, 0.1f, 500.0f)) isDirty = true;
                    }
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void BehaviourEditorWindow::RenderContentBrowserPanel(const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.065f, 0.065f, 0.080f, 1.0f));
    if (ImGui::BeginChild("MiniContentBrowser", size, true)) {
        ImGui::TextColored(ImVec4(0.6f, 0.7f, 0.85f, 1.0f), "📁 Asset Browser (Drag & drop models to Outliner or Viewport)");
        ImGui::Separator();
        ImGui::Spacing();

        const auto& allAssets = AssetRegistry::Get().GetAllAssets();
        for (const auto& pair : allAssets) {
            const auto& meta = pair.second;
            // Exclude internal scripts
            if (meta.virtualPath.find(".internal") != std::string::npos) continue;

            if (meta.type == AssetType::Mesh || meta.type == AssetType::SkeletalMesh ||
                meta.type == AssetType::Material || meta.type == AssetType::Texture) {

                ImGui::PushID(meta.id.ToString().c_str());

                const char* icon = (meta.type == AssetType::Mesh || meta.type == AssetType::SkeletalMesh) ? "📦" : "🎨";
                std::string itemText = std::string(icon) + " " + meta.objectName;

                ImGui::Selectable(itemText.c_str(), false, 0, ImVec2(160.0f, 24.0f));

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    std::string dragStr = "ASSET:" + meta.sourcePath + "\n";
                    ImGui::SetDragDropPayload("CONTENT_DRAG", dragStr.c_str(), dragStr.length() + 1);
                    std::string idStr = meta.id.ToString();
                    ImGui::SetDragDropPayload("ASSET_ID", idStr.c_str(), idStr.length() + 1);
                    ImGui::Text("%s %s", icon, meta.objectName.c_str());
                    ImGui::EndDragDropSource();
                }

                ImGui::SameLine();
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace Eunoia
