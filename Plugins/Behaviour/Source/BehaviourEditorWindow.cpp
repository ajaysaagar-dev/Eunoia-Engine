#include "Behaviour/BehaviourEditorWindow.h"
#include <Editor/EngineUI.h>
#include <EngineScene/Scene.h>
#include <EngineAssets/AssetRegistry.h>
#include <EngineAssets/AssetManager.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
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

    // Initialize Orbit Camera for Preview Viewport
    camera.target = glm::vec3(0.0f, 0.4f, 0.0f);
    camera.distance = 5.0f;
    camera.yaw = 45.0f;
    camera.pitch = 22.0f;
    camera.fov = 45.0f;

    SetupStudioLighting();
    SyncPreviewScene();
}

void BehaviourEditorWindow::SetupStudioLighting() {
    previewScene.Clear();
    previewScene.enableSkybox = false;
    previewScene.clearColor = glm::vec4(0.055f, 0.058f, 0.072f, 1.0f);
    previewScene.enableShadows = true;
    previewScene.shadowStrength = 0.85f;

    // 1. Directional Key Sunlight
    previewScene.lightDirection = glm::normalize(glm::vec3(0.55f, -1.0f, 0.65f));
    previewScene.lightColor = glm::vec3(1.0f, 0.98f, 0.94f);
    previewScene.lightIntensity = 1.4f;
    previewScene.ambientIntensity = 0.35f;

    // 2. Multi-point Studio Lights (Isolated strictly inside previewScene, never shown in Outliner)
    PointLight keyLight;
    keyLight.id = 10001;
    keyLight.name = "__StudioKeyLight";
    keyLight.position = glm::vec3(3.5f, 4.5f, 3.5f);
    keyLight.color = glm::vec3(1.0f, 0.96f, 0.90f);
    keyLight.intensity = 2.2f;
    keyLight.range = 30.0f;
    keyLight.castShadows = true;
    previewScene.pointLights.push_back(keyLight);

    PointLight fillLight;
    fillLight.id = 10002;
    fillLight.name = "__StudioFillLight";
    fillLight.position = glm::vec3(-3.5f, 3.0f, -2.5f);
    fillLight.color = glm::vec3(0.85f, 0.92f, 1.0f);
    fillLight.intensity = 1.3f;
    fillLight.range = 25.0f;
    fillLight.castShadows = false;
    previewScene.pointLights.push_back(fillLight);

    PointLight rimLight;
    rimLight.id = 10003;
    rimLight.name = "__StudioRimLight";
    rimLight.position = glm::vec3(0.0f, 4.5f, -4.5f);
    rimLight.color = glm::vec3(0.95f, 0.97f, 1.0f);
    rimLight.intensity = 1.6f;
    rimLight.range = 25.0f;
    rimLight.castShadows = false;
    previewScene.pointLights.push_back(rimLight);

    previewSceneInitialized = true;
}

void BehaviourEditorWindow::SyncPreviewScene() {
    if (!previewSceneInitialized) {
        SetupStudioLighting();
    }

    // Keep the 3 studio lights, remove all user object components
    previewScene.objects.clear();

    // Rebuild user components from asset into previewScene
    std::unordered_map<int, int> compIdToObjId;

    for (size_t i = 0; i < asset.components.size(); ++i) {
        const auto& c = asset.components[i];
        if (!c.visible) continue;

        GameObject* obj = nullptr;
        if (!c.meshPath.empty()) {
            std::filesystem::path resolvedMesh = AssetManager::Get().ResolvePhysicalOrCookedPath(c.meshPath);
            std::error_code ec;
            if (resolvedMesh.empty() || !std::filesystem::exists(resolvedMesh, ec)) {
                resolvedMesh = c.meshPath;
            }
            GameObject& mObj = previewScene.AddImportedMesh(resolvedMesh.string(), c.localPosition);
            obj = &mObj;
        } else if (c.type == PrimitiveType::Empty) {
            GameObject& eObj = previewScene.AddEmptyActor(c.localPosition);
            obj = &eObj;
        } else {
            GameObject& pObj = previewScene.AddObject(c.type, c.localPosition);
            obj = &pObj;
        }

        if (obj) {
            obj->id = c.id; // Preserve ID mapping
            obj->name = c.name;
            obj->rotation = c.localRotation;
            obj->scale = c.localScale;
            obj->color = c.color;
            obj->visible = c.visible;
            if (!c.materialPath.empty()) {
                obj->materialName = c.materialPath;
            }
            if (c.type == PrimitiveType::PointLight || c.type == PrimitiveType::SpotLight) {
                obj->isLight = true;
                obj->light.type = (c.type == PrimitiveType::SpotLight) ? LightType::Spot : LightType::Point;
                obj->light.color = c.lightColor;
                obj->light.intensity = c.lightIntensity;
                obj->light.range = c.lightRange;
            }

            compIdToObjId[c.id] = obj->id;

            // Reconstruct parenting inside previewScene
            if (c.parentId != 0 && compIdToObjId.find(c.parentId) != compIdToObjId.end()) {
                obj->parentId = compIdToObjId[c.parentId];
                GameObject* pObj = previewScene.FindObject(obj->parentId);
                if (pObj) {
                    pObj->childIds.push_back(obj->id);
                }
            }
        }
    }

    if (EngineUI::GetInstance()) {
        EngineUI::GetInstance()->RequestViewportRedraw(10);
    }
}

void BehaviourEditorWindow::Save() {
    if (!filePath.empty()) {
        asset.SaveToFile(filePath);
        isDirty = false;
        if (EngineUI::GetInstance()) {
            EngineUI::GetInstance()->AddLog("LogBehaviour", "Saved Behaviour Blueprint to: " + filePath, 2);
        }
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
               << "#include <EngineCore/EngineLogger.h>\n\n"
               << "EUNOIA_REGISTER_BEHAVIOUR(" << cleanName << ");\n\n"
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

    float bottomCBHeight = 220.0f;
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
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));

    if (ImGui::BeginChild("TopToolbar", ImVec2(0, 40.0f), true, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
        ImGui::TextColored(ImVec4(0.0f, 0.65f, 1.0f, 1.0f), "[B] Blueprint:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", asset.name.c_str());
        if (isDirty) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.20f, 1.0f), "*");
        }

        ImGui::SameLine(0.0f, 20.0f);

        // Gizmo Mode Switches (Loc, Rot, Scale)
        const ImVec4 activeCol(0.0f, 0.40f, 1.0f, 1.0f);
        const ImVec4 idleCol(0.12f, 0.12f, 0.14f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, currentGizmoOperation == 0 ? activeCol : idleCol);
        if (ImGui::Button("Loc", ImVec2(34.0f, 24.0f))) { currentGizmoOperation = 0; }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translate Mode (W)");

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, currentGizmoOperation == 1 ? activeCol : idleCol);
        if (ImGui::Button("Rot", ImVec2(34.0f, 24.0f))) { currentGizmoOperation = 1; }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate Mode (E)");

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, currentGizmoOperation == 2 ? activeCol : idleCol);
        if (ImGui::Button("Scale", ImVec2(42.0f, 24.0f))) { currentGizmoOperation = 2; }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale Mode (R)");

        ImGui::SameLine();
        if (ImGui::Button(currentGizmoMode == 0 ? "Local" : "World", ImVec2(48.0f, 24.0f))) {
            currentGizmoMode = (currentGizmoMode == 0) ? 1 : 0;
        }

        ImGui::SameLine(0.0f, 15.0f);
        // Shading Mode: Lit / Unlit / Wireframe
        if (ImGui::Button(viewportShadingMode == 0 ? "Lit" : (viewportShadingMode == 1 ? "Unlit" : "Wireframe"), ImVec2(60.0f, 24.0f))) {
            viewportShadingMode = (viewportShadingMode + 1) % 3;
        }

        // Right side controls
        float availW = ImGui::GetContentRegionAvail().x;
        float rightControlsW = 280.0f;
        if (availW > rightControlsW) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - rightControlsW));
        }

        if (ImGui::Button("Reset View", ImVec2(80.0f, 24.0f))) {
            camera.target = glm::vec3(0.0f, 0.4f, 0.0f);
            camera.distance = 5.0f;
            camera.yaw = 45.0f;
            camera.pitch = 22.0f;
            if (EngineUI::GetInstance()) EngineUI::GetInstance()->RequestViewportRedraw(10);
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.40f, 1.0f, 1.0f));
        if (ImGui::Button("Save", ImVec2(60.0f, 24.0f))) {
            Save();
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::Checkbox("Autosave", &autoSave);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

void BehaviourEditorWindow::RenderOutlinerPanel(const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.082f, 0.082f, 0.102f, 1.0f));
    if (ImGui::BeginChild("OutlinerPanel", size, true)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.894f, 0.894f, 0.906f, 1.0f), "Outliner");
        ImGui::SameLine(ImGui::GetWindowWidth() - 75.0f);
        if (ImGui::SmallButton("+ Add")) {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (ImGui::BeginPopup("AddComponentPopup")) {
            if (ImGui::MenuItem("Static Mesh")) {
                asset.AddComponent(PrimitiveType::Cube, "StaticMesh", selectedComponentId);
                isDirty = true;
                SyncPreviewScene();
            }
            if (ImGui::MenuItem("Cube")) {
                asset.AddComponent(PrimitiveType::Cube, "Cube", selectedComponentId);
                isDirty = true;
                SyncPreviewScene();
            }
            if (ImGui::MenuItem("Sphere")) {
                asset.AddComponent(PrimitiveType::Sphere, "Sphere", selectedComponentId);
                isDirty = true;
                SyncPreviewScene();
            }
            if (ImGui::MenuItem("Cylinder")) {
                asset.AddComponent(PrimitiveType::Cylinder, "Cylinder", selectedComponentId);
                isDirty = true;
                SyncPreviewScene();
            }
            if (ImGui::MenuItem("Plane")) {
                asset.AddComponent(PrimitiveType::Plane, "Plane", selectedComponentId);
                isDirty = true;
                SyncPreviewScene();
            }
            if (ImGui::MenuItem("Point Light")) {
                asset.AddComponent(PrimitiveType::PointLight, "PointLight", selectedComponentId);
                isDirty = true;
                SyncPreviewScene();
            }
            if (ImGui::MenuItem("Spot Light")) {
                asset.AddComponent(PrimitiveType::SpotLight, "SpotLight", selectedComponentId);
                isDirty = true;
                SyncPreviewScene();
            }
            if (ImGui::MenuItem("Camera")) {
                asset.AddComponent(PrimitiveType::Camera, "Camera", selectedComponentId);
                isDirty = true;
                SyncPreviewScene();
            }
            if (ImGui::MenuItem("Empty Scene Component")) {
                asset.AddComponent(PrimitiveType::Empty, "SceneComponent", selectedComponentId);
                isDirty = true;
                SyncPreviewScene();
            }
            ImGui::EndPopup();
        }

        // Search Filter
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##OutlinerFilter", "Filter components...", outlinerSearchBuf, sizeof(outlinerSearchBuf));

        ImGui::Separator();
        ImGui::Spacing();

        std::string filterLower = outlinerSearchBuf;
        std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

        for (auto& c : asset.components) {
            if (!filterLower.empty()) {
                std::string compNameLower = c.name;
                std::transform(compNameLower.begin(), compNameLower.end(), compNameLower.begin(), ::tolower);
                if (compNameLower.find(filterLower) == std::string::npos) continue;
            }

            bool isSelected = (!isSelectingScript && selectedComponentId == c.id);
            const char* typeTag = "[M]";
            if (c.type == PrimitiveType::Empty) typeTag = "[R]";
            else if (c.type == PrimitiveType::PointLight || c.type == PrimitiveType::SpotLight) typeTag = "[L]";
            else if (c.type == PrimitiveType::Camera) typeTag = "[C]";

            // Visibility eye button
            std::string eyeId = (c.visible ? "O##Eye_" : "-##Eye_") + std::to_string(c.id);
            if (ImGui::SmallButton(eyeId.c_str())) {
                c.visible = !c.visible;
                isDirty = true;
                SyncPreviewScene();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(c.visible ? "Hide Component" : "Show Component");

            ImGui::SameLine(0.0f, 4.0f);

            std::string label = std::string(typeTag) + " " + c.name;
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

            ImGui::TreeNodeEx((void*)(uintptr_t)c.id, flags, "%s", label.c_str());
            if (ImGui::IsItemClicked()) {
                selectedComponentId = c.id;
                isSelectingScript = false;
            }

            // Drag source to reparent
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                int compId = c.id;
                ImGui::SetDragDropPayload("BEH_COMP_ID", &compId, sizeof(int));
                ImGui::Text("Reparent %s", c.name.c_str());
                ImGui::EndDragDropSource();
            }

            // Drag target to receive child
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BEH_COMP_ID")) {
                    int draggedId = *(const int*)payload->Data;
                    if (draggedId != c.id) {
                        asset.SetParent(draggedId, c.id);
                        isDirty = true;
                        SyncPreviewScene();
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Add Child Component...")) {
                    asset.AddComponent(PrimitiveType::Cube, "ChildMesh", c.id);
                    isDirty = true;
                    SyncPreviewScene();
                }
                if (c.id != asset.components.front().id && ImGui::MenuItem("Duplicate")) {
                    auto* dup = asset.AddComponent(c.type, c.name + "_Copy", c.parentId);
                    if (dup) {
                        dup->localPosition = c.localPosition + glm::vec3(0.5f, 0.0f, 0.0f);
                        dup->localRotation = c.localRotation;
                        dup->localScale = c.localScale;
                        dup->meshPath = c.meshPath;
                        dup->materialPath = c.materialPath;
                        dup->color = c.color;
                        isDirty = true;
                        SyncPreviewScene();
                    }
                }
                if (c.id != asset.components.front().id && ImGui::MenuItem("Delete Component")) {
                    asset.RemoveComponent(c.id);
                    selectedComponentId = asset.components.front().id;
                    isDirty = true;
                    SyncPreviewScene();
                    if (autoSave) Save();
                }
                ImGui::EndPopup();
            }
        }

        // Accept Drag & Drop of 3D Models onto Outliner
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                std::string payloadData((const char*)payload->Data);
                std::stringstream ss(payloadData);
                std::string item;
                while (std::getline(ss, item, '\n')) {
                    if (item.rfind("FILE:", 0) == 0) item = item.substr(5);
                    if (item.rfind("ASSET:", 0) == 0) item = item.substr(6);
                    std::string ext = std::filesystem::path(item).extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
                        auto* comp = asset.AddComponent(PrimitiveType::Cube, std::filesystem::path(item).stem().string(), selectedComponentId);
                        if (comp) {
                            comp->meshPath = item;
                            selectedComponentId = comp->id;
                            isSelectingScript = false;
                            isDirty = true;
                            SyncPreviewScene();
                            if (autoSave) Save();
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
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.070f, 0.070f, 0.088f, 1.0f));
    if (ImGui::BeginChild("BehaviourScriptsPanel", size, true)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.85f, 0.45f, 1.0f, 1.0f), "Behaviour Scripts");
        ImGui::SameLine(ImGui::GetWindowWidth() - 75.0f);
        if (ImGui::SmallButton("+ Add")) {
            ImGui::OpenPopup("AddBehaviourScriptPopup");
        }

        if (ImGui::BeginPopup("AddBehaviourScriptPopup")) {
            static char newScriptNameBuf[64] = "NewScript";
            ImGui::Text("Create New Behaviour Script:");
            ImGui::InputText("##NewScrName", newScriptNameBuf, sizeof(newScriptNameBuf));
            if (ImGui::Button("Create & Open in VS Code", ImVec2(-1, 24))) {
                CreateInternalScript(newScriptNameBuf);
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
            ImGui::Text("Or attach existing Behaviour Script:");
            const auto& regList = BehaviourRegistry::Get().GetRegisteredClassNames();
            for (const auto& rName : regList) {
                if (ImGui::MenuItem(rName.c_str())) {
                    std::string autoPath = "Content/Behaviours/" + rName + ".cpp";
                    AttachedBehaviourScript* s = asset.AddAttachedScript(rName, autoPath);
                    if (s) {
                        selectedScriptId = s->id;
                        isSelectingScript = true;
                    }
                    isDirty = true;
                    if (autoSave) Save();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (asset.attachedScripts.empty()) {
            ImGui::TextDisabled("No scripts attached.");
            ImGui::TextDisabled("Click '+ Add' to attach a script.");
        } else {
            for (size_t i = 0; i < asset.attachedScripts.size(); ++i) {
                auto& s = asset.attachedScripts[i];
                ImGui::PushID(s.id.c_str());

                bool isSelected = (isSelectingScript && selectedScriptId == s.id);
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

                std::string sLabel = (s.enabled ? "[ON] " : "[OFF] ") + s.scriptName;
                ImGui::TreeNodeEx((void*)(uintptr_t)i, flags, "%s", sLabel.c_str());
                if (ImGui::IsItemClicked()) {
                    selectedScriptId = s.id;
                    isSelectingScript = true;
                }

                // Hover edit button for VS Code
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Click to inspect variables | Right-click to edit or delete");
                }

                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Edit in VS Code")) {
                        LaunchCodeEditor(s.sourceCppPath);
                    }
                    if (ImGui::MenuItem("Delete Script from Blueprint")) {
                        asset.RemoveAttachedScript(s.id);
                        if (selectedScriptId == s.id) {
                            selectedScriptId = "";
                            isSelectingScript = false;
                        }
                        isDirty = true;
                        if (autoSave) Save();
                    }
                    ImGui::EndPopup();
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginChild("ViewportPanel", size, true, flags)) {
        ImVec2 vpScreenPos = ImGui::GetCursorScreenPos();
        ImVec2 vpAvailSize = ImGui::GetContentRegionAvail();

        if (vpAvailSize.x > 10.0f && vpAvailSize.y > 10.0f) {
            ImGuiViewport* mainVp = ImGui::GetMainViewport();
            float rx = vpScreenPos.x - mainVp->Pos.x;
            float ry = vpScreenPos.y - mainVp->Pos.y;
            float rw = vpAvailSize.x;
            float rh = vpAvailSize.y;

            // Transmit exact dynamic hardware viewport bounds to the D3D12 pipeline!
            if (EngineUI::GetInstance()) {
                EngineUI::GetInstance()->SetDynamicViewportRect(rx, ry, rw, rh);
                EngineUI::GetInstance()->RequestViewportRedraw(2);
            }

            ImGuiIO& io = ImGui::GetIO();
            bool isHovered = ImGui::IsWindowHovered();

            // Camera Orbit & Pan Navigation
            if (isHovered) {
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                    camera.yaw -= io.MouseDelta.x * 0.35f;
                    camera.pitch = std::clamp(camera.pitch + io.MouseDelta.y * 0.35f, -89.0f, 89.0f);
                    if (EngineUI::GetInstance()) EngineUI::GetInstance()->RequestViewportRedraw(10);
                }
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                    glm::vec3 right = camera.GetRight();
                    glm::vec3 up = camera.GetUp();
                    camera.target -= (right * io.MouseDelta.x - up * io.MouseDelta.y) * 0.005f * camera.distance;
                    if (EngineUI::GetInstance()) EngineUI::GetInstance()->RequestViewportRedraw(10);
                }
                if (std::abs(io.MouseWheel) > 0.01f) {
                    camera.distance = std::clamp(camera.distance - io.MouseWheel * (camera.distance * 0.15f), 0.5f, 60.0f);
                    if (EngineUI::GetInstance()) EngineUI::GetInstance()->RequestViewportRedraw(10);
                }
            }

            // Invisible drag & interaction surface
            ImGui::InvisibleButton("##ViewportSurface", vpAvailSize);

            // ImGuizmo 3D Transform Gizmo for Selected Component
            if (!isSelectingScript) {
                BehaviourComponentDef* comp = asset.FindComponent(selectedComponentId);
                if (comp) {
                    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
                    ImGuizmo::SetRect(vpScreenPos.x, vpScreenPos.y, vpAvailSize.x, vpAvailSize.y);

                    float aspect = vpAvailSize.x / std::max(1.0f, vpAvailSize.y);
                    glm::mat4 view = camera.GetViewMatrix();
                    glm::mat4 proj = camera.GetProjectionMatrix(aspect);

                    glm::mat4 model = glm::translate(glm::mat4(1.0f), comp->localPosition);
                    model = glm::rotate(model, glm::radians(comp->localRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
                    model = glm::rotate(model, glm::radians(comp->localRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
                    model = glm::rotate(model, glm::radians(comp->localRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
                    model = glm::scale(model, comp->localScale);

                    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
                    if (currentGizmoOperation == 1) op = ImGuizmo::ROTATE;
                    else if (currentGizmoOperation == 2) op = ImGuizmo::SCALE;

                    ImGuizmo::MODE mode = (currentGizmoMode == 0) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

                    if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, mode, glm::value_ptr(model))) {
                        float matrixTranslation[3], matrixRotation[3], matrixScale[3];
                        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), matrixTranslation, matrixRotation, matrixScale);
                        comp->localPosition = glm::vec3(matrixTranslation[0], matrixTranslation[1], matrixTranslation[2]);
                        comp->localRotation = glm::vec3(matrixRotation[0], matrixRotation[1], matrixRotation[2]);
                        comp->localScale = glm::vec3(matrixScale[0], matrixScale[1], matrixScale[2]);
                        isDirty = true;
                        SyncPreviewScene();
                    }
                }
            }

            // Viewport Drop Target for 3D Meshes & Materials
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                    std::string payloadData((const char*)payload->Data);
                    std::stringstream ss(payloadData);
                    std::string item;
                    while (std::getline(ss, item, '\n')) {
                        if (item.rfind("FILE:", 0) == 0) item = item.substr(5);
                        if (item.rfind("ASSET:", 0) == 0) item = item.substr(6);
                        std::string ext = std::filesystem::path(item).extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
                            auto* comp = asset.AddComponent(PrimitiveType::Cube, std::filesystem::path(item).stem().string(), selectedComponentId);
                            if (comp) {
                                comp->meshPath = item;
                                selectedComponentId = comp->id;
                                isSelectingScript = false;
                                isDirty = true;
                                SyncPreviewScene();
                                if (autoSave) Save();
                            }
                        } else if (ext == ".emat" || ext == ".png" || ext == ".jpg" || ext == ".dds") {
                            BehaviourComponentDef* comp = asset.FindComponent(selectedComponentId);
                            if (comp) {
                                comp->materialPath = item;
                                isDirty = true;
                                SyncPreviewScene();
                                if (autoSave) Save();
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void BehaviourEditorWindow::RenderDetailsPanel(const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.082f, 0.082f, 0.102f, 1.0f));
    if (ImGui::BeginChild("DetailsPanel", size, true)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.894f, 0.894f, 0.906f, 1.0f), "Details");
        ImGui::Separator();
        ImGui::Spacing();

        if (isSelectingScript) {
            // Script Details
            AttachedBehaviourScript* s = asset.FindAttachedScript(selectedScriptId);
            if (!s) {
                ImGui::TextDisabled("Select a script or component to inspect.");
            } else {
                ImGui::TextColored(ImVec4(0.85f, 0.45f, 1.0f, 1.0f), "Script: %s", s->scriptName.c_str());
                ImGui::Spacing();

                if (ImGui::Checkbox("Enabled", &s->enabled)) {
                    isDirty = true;
                }

                ImGui::SameLine(0.0f, 15.0f);
                if (ImGui::Button("Edit in VS Code", ImVec2(120.0f, 24.0f))) {
                    LaunchCodeEditor(s->sourceCppPath);
                }

                ImGui::Spacing();
                ImGui::TextDisabled("Source File: %s", s->sourceCppPath.c_str());
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.290f, 0.486f, 1.0f, 1.0f), "Public Script Variables & Properties:");
                ImGui::Spacing();

                for (size_t pIdx = 0; pIdx < s->propertyOverrides.size(); ++pIdx) {
                    auto& prop = s->propertyOverrides[pIdx];
                    ImGui::PushID((int)pIdx);

                    ImGui::TextDisabled("%s (%s):", prop.propertyName.c_str(), prop.typeName.c_str());
                    ImGui::SetNextItemWidth(-1.0f);

                    if (prop.typeName == "object_ref") {
                        // Component reference picker strictly from this Behaviour!
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
                        char sBuf[256];
                        strncpy_s(sBuf, prop.stringVal.c_str(), sizeof(sBuf));
                        if (ImGui::InputText("##StrVal", sBuf, sizeof(sBuf))) {
                            prop.stringVal = sBuf;
                            isDirty = true;
                        }
                    }

                    ImGui::PopID();
                    ImGui::Spacing();
                }

                if (ImGui::Button("+ Add Property Variable Field")) {
                    ScriptPropertyOverride newP;
                    newP.propertyName = "targetComponent";
                    newP.typeName = "object_ref";
                    newP.targetComponentId = 1;
                    s->propertyOverrides.push_back(newP);
                    isDirty = true;
                }
            }
        } else {
            // Component Details
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
                    SyncPreviewScene();
                }

                ImGui::Spacing();
                if (ImGui::Checkbox("Visible", &c->visible)) {
                    isDirty = true;
                    SyncPreviewScene();
                }
                ImGui::Separator();
                ImGui::Spacing();

                // 1. Transform Section
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled("Location (X, Y, Z):");
                    float pos[3] = { c->localPosition.x, c->localPosition.y, c->localPosition.z };
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.0f);
                    if (ImGui::DragFloat3("##Loc", pos, 0.1f)) {
                        c->localPosition = glm::vec3(pos[0], pos[1], pos[2]);
                        isDirty = true;
                        SyncPreviewScene();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("0##ResetLoc")) {
                        c->localPosition = glm::vec3(0.0f);
                        isDirty = true;
                        SyncPreviewScene();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Location to 0,0,0");

                    ImGui::Spacing();
                    ImGui::TextDisabled("Rotation (X, Y, Z):");
                    float rot[3] = { c->localRotation.x, c->localRotation.y, c->localRotation.z };
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.0f);
                    if (ImGui::DragFloat3("##Rot", rot, 1.0f)) {
                        c->localRotation = glm::vec3(rot[0], rot[1], rot[2]);
                        isDirty = true;
                        SyncPreviewScene();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("0##ResetRot")) {
                        c->localRotation = glm::vec3(0.0f);
                        isDirty = true;
                        SyncPreviewScene();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Rotation to 0,0,0");

                    ImGui::Spacing();
                    ImGui::TextDisabled("Scale (X, Y, Z):");
                    float scl[3] = { c->localScale.x, c->localScale.y, c->localScale.z };
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.0f);
                    if (ImGui::DragFloat3("##Scl", scl, 0.05f)) {
                        c->localScale = glm::vec3(scl[0], scl[1], scl[2]);
                        isDirty = true;
                        SyncPreviewScene();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("1##ResetScl")) {
                        c->localScale = glm::vec3(1.0f);
                        isDirty = true;
                        SyncPreviewScene();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Scale to 1,1,1");
                }

                ImGui::Spacing();

                // 2. Visuals & Mesh Section
                if (ImGui::CollapsingHeader("Visuals & Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
                    char meshBuf[256];
                    strncpy_s(meshBuf, c->meshPath.c_str(), sizeof(meshBuf));
                    ImGui::TextDisabled("Mesh Asset Path:");
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputText("##MeshPath", meshBuf, sizeof(meshBuf))) {
                        c->meshPath = meshBuf;
                        isDirty = true;
                        SyncPreviewScene();
                    }

                    ImGui::Spacing();
                    char matBuf[256];
                    strncpy_s(matBuf, c->materialPath.c_str(), sizeof(matBuf));
                    ImGui::TextDisabled("Material Assignment:");
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputText("##MatPath", matBuf, sizeof(matBuf))) {
                        c->materialPath = matBuf;
                        isDirty = true;
                        SyncPreviewScene();
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Tint Color:");
                    float col[4] = { c->color.r, c->color.g, c->color.b, c->color.a };
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::ColorEdit4("##Color", col)) {
                        c->color = glm::vec4(col[0], col[1], col[2], col[3]);
                        isDirty = true;
                        SyncPreviewScene();
                    }
                }

                // 3. Light Settings
                if (c->type == PrimitiveType::PointLight || c->type == PrimitiveType::SpotLight) {
                    ImGui::Spacing();
                    if (ImGui::CollapsingHeader("Light Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                        float lCol[3] = { c->lightColor.r, c->lightColor.g, c->lightColor.b };
                        ImGui::TextDisabled("Light Color:");
                        if (ImGui::ColorEdit3("##LCol", lCol)) {
                            c->lightColor = glm::vec3(lCol[0], lCol[1], lCol[2]);
                            isDirty = true;
                            SyncPreviewScene();
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Intensity:");
                        if (ImGui::DragFloat("##LInt", &c->lightIntensity, 0.1f, 0.0f, 100.0f)) {
                            isDirty = true;
                            SyncPreviewScene();
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Range:");
                        if (ImGui::DragFloat("##LRng", &c->lightRange, 0.5f, 0.1f, 500.0f)) {
                            isDirty = true;
                            SyncPreviewScene();
                        }
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
    if (ImGui::BeginChild("BehaviourContentBrowser", size, true)) {
        if (EngineUI::GetInstance()) {
            EngineUI::GetInstance()->RenderContentBrowserContent(previewScene);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

} // namespace Eunoia
