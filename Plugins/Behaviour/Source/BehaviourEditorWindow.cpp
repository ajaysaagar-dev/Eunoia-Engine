#include "Behaviour/BehaviourEditorWindow.h"
#include <Editor/EngineUI.h>
#include <EngineScene/Scene.h>
#include <EngineAssets/AssetRegistry.h>
#include <EngineAssets/AssetManager.h>
#include <EngineScene/BehaviourRegistry.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
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
    previewScene.isStudioPreview = true;
    previewScene.enableSkybox = false;
    previewScene.clearColor = glm::vec4(0.055f, 0.058f, 0.072f, 1.0f);
    previewScene.enableShadows = true;
    previewScene.shadowStrength = 0.85f;
    previewScene.showGrid = true;
    previewScene.gridSize = 16.0f;
    previewScene.gridDivisions = 16;
    previewScene.gridColor = glm::vec3(0.28f, 0.30f, 0.36f);

    // 1. Directional Key Sunlight
    previewScene.lightDirection = glm::normalize(glm::vec3(0.55f, -1.0f, 0.65f));
    previewScene.lightColor = glm::vec3(1.0f, 0.98f, 0.94f);
    previewScene.lightIntensity = 1.5f;
    previewScene.ambientIntensity = 0.40f;
    previewScene.ambientColor = glm::vec3(0.18f, 0.20f, 0.25f);

    // 2. Multi-point Studio Lights (Isolated strictly inside previewScene, never shown in Outliner)
    PointLight keyLight;
    keyLight.id = 10001;
    keyLight.name = "__StudioKeyLight";
    keyLight.position = glm::vec3(3.5f, 4.5f, 3.5f);
    keyLight.color = glm::vec3(1.0f, 0.96f, 0.90f);
    keyLight.intensity = 2.5f;
    keyLight.range = 35.0f;
    keyLight.enabled = true;
    keyLight.castShadows = true;
    previewScene.pointLights.push_back(keyLight);

    PointLight fillLight;
    fillLight.id = 10002;
    fillLight.name = "__StudioFillLight";
    fillLight.position = glm::vec3(-3.5f, 3.0f, -2.5f);
    fillLight.color = glm::vec3(0.85f, 0.92f, 1.0f);
    fillLight.intensity = 1.6f;
    fillLight.range = 30.0f;
    fillLight.enabled = true;
    fillLight.castShadows = false;
    previewScene.pointLights.push_back(fillLight);

    PointLight rimLight;
    rimLight.id = 10003;
    rimLight.name = "__StudioRimLight";
    rimLight.position = glm::vec3(0.0f, 4.5f, -4.5f);
    rimLight.color = glm::vec3(0.95f, 0.97f, 1.0f);
    rimLight.intensity = 2.0f;
    rimLight.range = 30.0f;
    rimLight.enabled = true;
    rimLight.castShadows = false;
    previewScene.pointLights.push_back(rimLight);

    previewSceneInitialized = true;
}

void BehaviourEditorWindow::SyncPreviewScene() {
    if (!previewSceneInitialized) {
        SetupStudioLighting();
    }

    // Keep the 3 studio lights
    std::vector<PointLight> studioLights;
    for (const auto& pl : previewScene.pointLights) {
        if (pl.name.rfind("__Studio", 0) == 0) {
            studioLights.push_back(pl);
        }
    }
    previewScene.pointLights = studioLights;

    // Clear user meshes
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
                obj->light.enabled = c.visible;
                obj->light.innerConeAngle = c.innerConeAngle;
                obj->light.outerConeAngle = c.outerConeAngle;
                obj->light.castShadows = c.castShadows;

                // Also populate in previewScene.pointLights so it illuminates the 3D viewport!
                PointLight pl;
                pl.id = c.id;
                pl.name = c.name;
                pl.type = obj->light.type;
                pl.position = c.localPosition;
                pl.color = c.lightColor;
                pl.intensity = c.lightIntensity;
                pl.range = c.lightRange;
                pl.enabled = c.visible;
                pl.innerConeAngle = c.innerConeAngle;
                pl.outerConeAngle = c.outerConeAngle;
                pl.castShadows = c.castShadows;
                previewScene.pointLights.push_back(pl);
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

    if (m_mainLevelScene && !filePath.empty()) {
        asset.SyncInstancesInScene(*m_mainLevelScene);
    }
    if (EngineUI::GetInstance()) {
        EngineUI::GetInstance()->RequestViewportRedraw(10);
    }
}

void BehaviourEditorWindow::Save() {
    if (!filePath.empty()) {
        asset.SaveToFile(filePath);
        isDirty = false;
        if (m_mainLevelScene) {
            asset.SyncInstancesInScene(*m_mainLevelScene);
        }
        if (EngineUI::GetInstance()) {
            EngineUI::GetInstance()->AddLog("LogBehaviour", "Saved Behaviour Blueprint to: " + filePath, 2);
            EngineUI::GetInstance()->RequestViewportRedraw(15);
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
    if (mainLevelScene) {
        m_mainLevelScene = mainLevelScene;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        Save();
    }

    // 1. Top Blueprint Editor Toolbar
    RenderTopToolbar();

    // 2. Full ImGui DockSpace across the workspace area
    std::string dockSpaceName = "BehaviourDockSpace_" + name;
    ImGuiID dockId = ImGui::GetID(dockSpaceName.c_str());

    // Build default layout on first use or reset
    if (!ImGui::DockBuilderGetNode(dockId) || ImGui::DockBuilderGetNode(dockId)->IsLeafNode()) {
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetContentRegionAvail());

        ImGuiID dockLeft = 0;
        ImGuiID dockRemaining = 0;
        ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, 0.22f, &dockLeft, &dockRemaining);

        ImGuiID dockRight = 0;
        ImGuiID dockCenterArea = 0;
        ImGui::DockBuilderSplitNode(dockRemaining, ImGuiDir_Right, 0.26f, &dockRight, &dockCenterArea);

        ImGuiID dockBottom = 0;
        ImGuiID dockCenter = 0;
        ImGui::DockBuilderSplitNode(dockCenterArea, ImGuiDir_Down, 0.30f, &dockBottom, &dockCenter);

        ImGuiID dockOutliner = 0;
        ImGuiID dockScripts = 0;
        ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Down, 0.40f, &dockScripts, &dockOutliner);

        std::string winVp = "Blueprint Viewport##" + name;
        std::string winOut = "Components##" + name;
        std::string winScr = "Behaviour Scripts##" + name;
        std::string winDet = "Details##" + name;
        std::string winCb  = "Content Browser##" + name;

        ImGui::DockBuilderDockWindow(winOut.c_str(), dockOutliner);
        ImGui::DockBuilderDockWindow(winScr.c_str(), dockScripts);
        ImGui::DockBuilderDockWindow(winVp.c_str(),  dockCenter);
        ImGui::DockBuilderDockWindow(winCb.c_str(),  dockBottom);
        ImGui::DockBuilderDockWindow(winDet.c_str(), dockRight);

        ImGui::DockBuilderFinish(dockId);
    }

    ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    // 3. Render all 5 dockable windows
    RenderOutlinerPanel();
    RenderBehaviourScriptsPanel();
    RenderViewportPanel();
    RenderDetailsPanel();
    RenderContentBrowserPanel();
}

void BehaviourEditorWindow::RenderTopToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));

    if (ImGui::BeginChild("TopToolbar", ImVec2(0, 42.0f), true, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
        ImGui::TextColored(ImVec4(0.0f, 0.65f, 1.0f, 1.0f), "[B] Blueprint:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", asset.name.c_str());
        if (isDirty) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.20f, 1.0f), "*");
        }

        ImGui::SameLine(0.0f, 20.0f);
        if (ImGui::Button("Compile / Sync", ImVec2(110.0f, 26.0f))) {
            SyncPreviewScene();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rebuild components and sync preview scene");

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.40f, 1.0f, 1.0f));
        if (ImGui::Button("Save", ImVec2(70.0f, 26.0f))) {
            Save();
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::Checkbox("Autosave", &autoSave);

        // Right side info
        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > 260.0f) {
            ImGui::SameLine(ImGui::GetWindowWidth() - 260.0f);
            ImGui::TextDisabled("%d Components | %d Scripts", (int)asset.components.size(), (int)asset.attachedScripts.size());
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

void BehaviourEditorWindow::RenderOutlinerNode(int compId, const std::string& filterLower) {
    BehaviourComponentDef* c = asset.FindComponent(compId);
    if (!c) return;

    // Filter check: matches if this component or any descendant matches
    bool matchesFilter = filterLower.empty();
    if (!matchesFilter) {
        std::string nameLower = c->name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        if (nameLower.find(filterLower) != std::string::npos) {
            matchesFilter = true;
        } else {
            std::function<bool(const BehaviourComponentDef*)> checkDescendant = [&](const BehaviourComponentDef* node) -> bool {
                for (int chId : node->childIds) {
                    const auto* ch = asset.FindComponent(chId);
                    if (ch) {
                        std::string chLower = ch->name;
                        std::transform(chLower.begin(), chLower.end(), chLower.begin(), ::tolower);
                        if (chLower.find(filterLower) != std::string::npos) return true;
                        if (checkDescendant(ch)) return true;
                    }
                }
                return false;
            };
            matchesFilter = checkDescendant(c);
        }
    }
    if (!matchesFilter) return;

    bool hasChildren = !c->childIds.empty();
    bool isSelected = (!isSelectingScript && selectedComponentId == c->id);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
    if (!hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    } else {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }
    if (isSelected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const char* typeTag = "[M]";
    if (c->type == PrimitiveType::Empty && c->id == 1) typeTag = "[R]";
    else if (c->type == PrimitiveType::Empty) typeTag = "[S]";
    else if (c->type == PrimitiveType::PointLight || c->type == PrimitiveType::SpotLight) typeTag = "[L]";
    else if (c->type == PrimitiveType::Camera) typeTag = "[C]";

    // Visibility eye toggle
    std::string eyeId = (c->visible ? "O##Eye_" : "-##Eye_") + std::to_string(c->id);
    if (ImGui::SmallButton(eyeId.c_str())) {
        c->visible = !c->visible;
        isDirty = true;
        SyncPreviewScene();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(c->visible ? "Hide Component" : "Show Component");

    ImGui::SameLine(0.0f, 4.0f);

    std::string label = std::string(typeTag) + " " + c->name;
    bool nodeOpen = ImGui::TreeNodeEx((void*)(uintptr_t)c->id, flags, "%s", label.c_str());

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        selectedComponentId = c->id;
        isSelectingScript = false;
    }

    // Drag source to reparent
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        int dragId = c->id;
        ImGui::SetDragDropPayload("BEH_COMP_ID", &dragId, sizeof(int));
        ImGui::Text("Reparent %s", c->name.c_str());
        ImGui::EndDragDropSource();
    }

    // Drag target to receive child
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BEH_COMP_ID")) {
            int draggedId = *(const int*)payload->Data;
            if (draggedId != c->id && !asset.IsDescendant(draggedId, c->id)) {
                asset.SetParent(draggedId, c->id);
                isDirty = true;
                SyncPreviewScene();
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Context menu
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Add Child Component...")) {
            asset.AddComponent(PrimitiveType::Cube, "ChildMesh", c->id);
            isDirty = true;
            SyncPreviewScene();
        }
        if (c->id != 1 && ImGui::MenuItem("Duplicate")) {
            auto* dup = asset.AddComponent(c->type, c->name + "_Copy", c->parentId);
            if (dup) {
                dup->localPosition = c->localPosition + glm::vec3(0.5f, 0.0f, 0.0f);
                dup->localRotation = c->localRotation;
                dup->localScale = c->localScale;
                dup->meshPath = c->meshPath;
                dup->materialPath = c->materialPath;
                dup->color = c->color;
                dup->lightColor = c->lightColor;
                dup->lightIntensity = c->lightIntensity;
                dup->lightRange = c->lightRange;
                dup->innerConeAngle = c->innerConeAngle;
                dup->outerConeAngle = c->outerConeAngle;
                dup->castShadows = c->castShadows;
                isDirty = true;
                SyncPreviewScene();
            }
        }
        if (c->id != 1 && ImGui::MenuItem("Delete Component")) {
            asset.RemoveComponent(c->id);
            selectedComponentId = 1;
            isDirty = true;
            SyncPreviewScene();
            if (autoSave) Save();
        }
        ImGui::EndPopup();
    }

    // Render children recursively!
    if (nodeOpen) {
        for (int chId : c->childIds) {
            RenderOutlinerNode(chId, filterLower);
        }
        ImGui::TreePop();
    }
}

void BehaviourEditorWindow::RenderOutlinerPanel() {
    std::string winTitle = "Components##" + name;
    if (ImGui::Begin(winTitle.c_str())) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.894f, 0.894f, 0.906f, 1.0f), "Components");
        ImGui::SameLine(ImGui::GetWindowWidth() - 75.0f);
        if (ImGui::SmallButton("+ Add")) {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (ImGui::BeginPopup("AddComponentPopup")) {
            if (ImGui::MenuItem("Static Mesh (Imported)")) {
                asset.AddComponent(PrimitiveType::Cube, "StaticMesh", selectedComponentId);
                isDirty = true;
                SyncPreviewScene();
            }
            ImGui::Separator();
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
            ImGui::Separator();
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
            if (ImGui::MenuItem("Scene Component (Empty)")) {
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

        // Hierarchical tree starting from root components (parentId == 0 or parent not in asset)
        for (const auto& c : asset.components) {
            if (c.parentId == 0 || asset.FindComponent(c.parentId) == nullptr) {
                RenderOutlinerNode(c.id, filterLower);
            }
        }

        // Accept Drag & Drop of 3D Models onto Outliner to add as components
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
    ImGui::End();
}

void BehaviourEditorWindow::RenderBehaviourScriptsPanel() {
    std::string winTitle = "Behaviour Scripts##" + name;
    if (ImGui::Begin(winTitle.c_str())) {
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
    ImGui::End();
}

void BehaviourEditorWindow::RenderViewportPanel() {
    std::string winTitle = "Blueprint Viewport##" + name;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    if (ImGui::Begin(winTitle.c_str(), nullptr, flags)) {
        ImGuiDockNode* node = ImGui::GetWindowDockNode();
        if (node) {
            node->LocalFlags |= ImGuiDockNodeFlags_PassthruCentralNode;
        }
        ImVec2 vpScreenPos = ImGui::GetCursorScreenPos();
        ImVec2 vpAvailSize = ImGui::GetContentRegionAvail();

        if (vpAvailSize.x > 20.0f && vpAvailSize.y > 20.0f) {
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

            // Camera Flycam & Orbit Navigation (WASDQE Parity with Level Viewport)
            bool isRMBDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
            if (isHovered && isRMBDown) {
                camera.isFlying = true;
                if (std::abs(io.MouseDelta.x) > 0.001f || std::abs(io.MouseDelta.y) > 0.001f) {
                    camera.LookAround(io.MouseDelta.x, io.MouseDelta.y);
                    if (EngineUI::GetInstance()) EngineUI::GetInstance()->RequestViewportRedraw(10);
                }

                // Flycam keyboard movement: WASDQE with Shift boost
                float dt = (io.DeltaTime > 0.0f && io.DeltaTime < 0.2f) ? io.DeltaTime : (1.0f / 60.0f);
                float speed = camera.moveSpeed;
                if (io.KeyShift) speed *= 2.5f;

                glm::vec3 moveDir(0.0f);
                glm::vec3 forward = camera.GetForward();
                glm::vec3 right   = camera.GetRight();
                glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

                if (ImGui::IsKeyDown(ImGuiKey_W)) moveDir += forward;
                if (ImGui::IsKeyDown(ImGuiKey_S)) moveDir -= forward;
                if (ImGui::IsKeyDown(ImGuiKey_D)) moveDir += right;
                if (ImGui::IsKeyDown(ImGuiKey_A)) moveDir -= right;
                if (ImGui::IsKeyDown(ImGuiKey_E)) moveDir += worldUp;
                if (ImGui::IsKeyDown(ImGuiKey_Q)) moveDir -= worldUp;

                if (glm::length(moveDir) > 0.001f) {
                    camera.Move(glm::normalize(moveDir) * speed * dt);
                    if (EngineUI::GetInstance()) EngineUI::GetInstance()->RequestViewportRedraw(10);
                }
            } else {
                camera.isFlying = false;
            }

            if (isHovered) {
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                    glm::vec3 right = camera.GetRight();
                    glm::vec3 up = camera.GetUp();
                    camera.target -= (right * io.MouseDelta.x - up * io.MouseDelta.y) * 0.005f * camera.distance * cameraSpeed;
                    if (EngineUI::GetInstance()) EngineUI::GetInstance()->RequestViewportRedraw(10);
                }
                if (std::abs(io.MouseWheel) > 0.01f) {
                    camera.distance = std::clamp(camera.distance - io.MouseWheel * (camera.distance * 0.15f), 0.5f, 80.0f);
                    if (EngineUI::GetInstance()) EngineUI::GetInstance()->RequestViewportRedraw(10);
                }
            }

            // Invisible drag & interaction surface for camera navigation
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

            // Viewport Overlay Controls (Loc/Rot/Scale, Lit/Unlit, Reset View)
            ImGui::SetCursorScreenPos(ImVec2(vpScreenPos.x + 10.0f, vpScreenPos.y + 10.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.10f, 0.75f));
            if (ImGui::BeginChild("ViewportOverlayBar", ImVec2(340.0f, 32.0f), true, ImGuiWindowFlags_NoScrollbar)) {
                const ImVec4 activeCol(0.0f, 0.40f, 1.0f, 1.0f);
                const ImVec4 idleCol(0.15f, 0.15f, 0.18f, 1.0f);

                ImGui::PushStyleColor(ImGuiCol_Button, currentGizmoOperation == 0 ? activeCol : idleCol);
                if (ImGui::Button("Loc", ImVec2(32.0f, 22.0f))) { currentGizmoOperation = 0; }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translate (W)");

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, currentGizmoOperation == 1 ? activeCol : idleCol);
                if (ImGui::Button("Rot", ImVec2(32.0f, 22.0f))) { currentGizmoOperation = 1; }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate (E)");

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, currentGizmoOperation == 2 ? activeCol : idleCol);
                if (ImGui::Button("Scale", ImVec2(40.0f, 22.0f))) { currentGizmoOperation = 2; }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale (R)");

                ImGui::SameLine();
                if (ImGui::Button(currentGizmoMode == 0 ? "Local" : "World", ImVec2(46.0f, 22.0f))) {
                    currentGizmoMode = (currentGizmoMode == 0) ? 1 : 0;
                }

                ImGui::SameLine();
                if (ImGui::Button(viewportShadingMode == 0 ? "Lit" : (viewportShadingMode == 1 ? "Unlit" : "Wire"), ImVec2(44.0f, 22.0f))) {
                    viewportShadingMode = (viewportShadingMode + 1) % 3;
                }

                ImGui::SameLine();
                if (ImGui::Button("Reset View", ImVec2(75.0f, 22.0f))) {
                    camera.target = glm::vec3(0.0f, 0.4f, 0.0f);
                    camera.distance = 5.0f;
                    camera.yaw = 45.0f;
                    camera.pitch = 22.0f;
                    if (EngineUI::GetInstance()) EngineUI::GetInstance()->RequestViewportRedraw(10);
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();

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
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

void BehaviourEditorWindow::RenderDetailsPanel() {
    std::string winTitle = "Details##" + name;
    if (ImGui::Begin(winTitle.c_str())) {
        if (isSelectingScript) {
            // Script Inspector
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
                if (ImGui::Button("Edit in VS Code", ImVec2(130.0f, 24.0f))) {
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
            // Component Inspector
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

                ImGui::SameLine(0.0f, 20.0f);
                const char* typeName = "Static Mesh";
                if (c->type == PrimitiveType::Empty && c->id == 1) typeName = "Root Scene Component";
                else if (c->type == PrimitiveType::Empty) typeName = "Scene Component";
                else if (c->type == PrimitiveType::PointLight) typeName = "Point Light";
                else if (c->type == PrimitiveType::SpotLight) typeName = "Spot Light";
                else if (c->type == PrimitiveType::Camera) typeName = "Camera";
                ImGui::TextDisabled("Type: %s", typeName);

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

                // 2. Visuals & Mesh Section (for mesh / primitive components)
                if (c->type != PrimitiveType::PointLight && c->type != PrimitiveType::SpotLight && c->type != PrimitiveType::Camera) {
                    if (ImGui::CollapsingHeader("Visuals & Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
                        // Primitive Shape Selector
                        const char* primNames[] = { "Cube", "Plane", "Sphere", "Cylinder", "Cone", "Capsule", "Empty" };
                        PrimitiveType primTypes[] = { PrimitiveType::Cube, PrimitiveType::Plane, PrimitiveType::Sphere, PrimitiveType::Cylinder, PrimitiveType::Cone, PrimitiveType::Capsule, PrimitiveType::Empty };
                        int currentPrimIdx = 0;
                        for (int pi = 0; pi < 7; ++pi) {
                            if (c->type == primTypes[pi]) { currentPrimIdx = pi; break; }
                        }
                        ImGui::TextDisabled("Primitive Shape:");
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::Combo("##PrimShape", &currentPrimIdx, primNames, 7)) {
                            c->type = primTypes[currentPrimIdx];
                            isDirty = true;
                            SyncPreviewScene();
                        }

                        ImGui::Spacing();
                        char meshBuf[256];
                        strncpy_s(meshBuf, c->meshPath.c_str(), sizeof(meshBuf));
                        ImGui::TextDisabled("Mesh Asset Path (FBX / OBJ / GLTF):");
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.0f);
                        if (ImGui::InputText("##MeshPath", meshBuf, sizeof(meshBuf))) {
                            c->meshPath = meshBuf;
                            isDirty = true;
                            SyncPreviewScene();
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X##ClearMesh")) {
                            c->meshPath = "";
                            isDirty = true;
                            SyncPreviewScene();
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear custom mesh (revert to primitive)");

                        ImGui::Spacing();
                        char matBuf[256];
                        strncpy_s(matBuf, c->materialPath.c_str(), sizeof(matBuf));
                        ImGui::TextDisabled("Material Assignment:");
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30.0f);
                        if (ImGui::InputText("##MatPath", matBuf, sizeof(matBuf))) {
                            c->materialPath = matBuf;
                            isDirty = true;
                            SyncPreviewScene();
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X##ClearMat")) {
                            c->materialPath = "";
                            isDirty = true;
                            SyncPreviewScene();
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear material assignment");

                        ImGui::Spacing();
                        ImGui::TextDisabled("Color Tint:");
                        float col[4] = { c->color.r, c->color.g, c->color.b, c->color.a };
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::ColorEdit4("##Color", col)) {
                            c->color = glm::vec4(col[0], col[1], col[2], col[3]);
                            isDirty = true;
                            SyncPreviewScene();
                        }
                    }
                }

                // 3. Light Settings Section
                if (c->type == PrimitiveType::PointLight || c->type == PrimitiveType::SpotLight) {
                    ImGui::Spacing();
                    if (ImGui::CollapsingHeader("Light Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                        // Light Type toggle
                        const char* lightTypes[] = { "Point Light", "Spot Light" };
                        int lIdx = (c->type == PrimitiveType::SpotLight) ? 1 : 0;
                        ImGui::TextDisabled("Light Type:");
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::Combo("##LType", &lIdx, lightTypes, 2)) {
                            c->type = (lIdx == 1) ? PrimitiveType::SpotLight : PrimitiveType::PointLight;
                            isDirty = true;
                            SyncPreviewScene();
                        }

                        ImGui::Spacing();
                        float lCol[3] = { c->lightColor.r, c->lightColor.g, c->lightColor.b };
                        ImGui::TextDisabled("Light Color:");
                        if (ImGui::ColorEdit3("##LCol", lCol)) {
                            c->lightColor = glm::vec3(lCol[0], lCol[1], lCol[2]);
                            isDirty = true;
                            SyncPreviewScene();
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Intensity (Candela / Lux):");
                        if (ImGui::DragFloat("##LInt", &c->lightIntensity, 0.1f, 0.0f, 100.0f)) {
                            isDirty = true;
                            SyncPreviewScene();
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Attenuation Radius / Range (m):");
                        if (ImGui::DragFloat("##LRng", &c->lightRange, 0.5f, 0.1f, 500.0f)) {
                            isDirty = true;
                            SyncPreviewScene();
                        }

                        if (c->type == PrimitiveType::SpotLight) {
                            ImGui::Spacing();
                            ImGui::TextDisabled("Inner Cone Angle (deg):");
                            if (ImGui::SliderFloat("##LInner", &c->innerConeAngle, 1.0f, 85.0f)) {
                                if (c->innerConeAngle > c->outerConeAngle) c->outerConeAngle = c->innerConeAngle;
                                isDirty = true;
                                SyncPreviewScene();
                            }

                            ImGui::Spacing();
                            ImGui::TextDisabled("Outer Cone Angle (deg):");
                            if (ImGui::SliderFloat("##LOuter", &c->outerConeAngle, 1.0f, 89.0f)) {
                                if (c->outerConeAngle < c->innerConeAngle) c->innerConeAngle = c->outerConeAngle;
                                isDirty = true;
                                SyncPreviewScene();
                            }
                        }

                        ImGui::Spacing();
                        if (ImGui::Checkbox("Cast Shadows", &c->castShadows)) {
                            isDirty = true;
                            SyncPreviewScene();
                        }
                    }
                }

                // 4. Camera Settings Section
                if (c->type == PrimitiveType::Camera) {
                    ImGui::Spacing();
                    if (ImGui::CollapsingHeader("Camera Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextDisabled("Field of View (deg):");
                        if (ImGui::SliderFloat("##CamFov", &c->cameraFov, 10.0f, 120.0f)) {
                            isDirty = true;
                            SyncPreviewScene();
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Near Clip Plane (m):");
                        if (ImGui::DragFloat("##CamNear", &c->cameraNear, 0.05f, 0.01f, 10.0f)) {
                            isDirty = true;
                            SyncPreviewScene();
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Far Clip Plane (m):");
                        if (ImGui::DragFloat("##CamFar", &c->cameraFar, 10.0f, 10.0f, 10000.0f)) {
                            isDirty = true;
                            SyncPreviewScene();
                        }

                        ImGui::Spacing();
                        if (ImGui::Checkbox("Orthographic", &c->isOrthographic)) {
                            isDirty = true;
                            SyncPreviewScene();
                        }
                    }
                }
            }
        }
    }
    ImGui::End();
}

void BehaviourEditorWindow::RenderContentBrowserPanel() {
    std::string winTitle = "Content Browser##" + name;
    if (ImGui::Begin(winTitle.c_str())) {
        if (EngineUI::GetInstance()) {
            EngineUI::GetInstance()->RenderContentBrowserContent(previewScene);
        }
    }
    ImGui::End();
}

} // namespace Eunoia
