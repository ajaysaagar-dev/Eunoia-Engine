#include <Editor/EngineUI.h>
#include <Editor/UndoManager.h>
#include <EngineCore/EngineLogger.h>
#include <EngineScene/Scene.h>
#include "imgui.h"
#include <string>
#include <algorithm>
void EngineUI::RenderOutliner(Scene& scene) {
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // Dockable window: size hint for first launch, can be repositioned via DockSpace
    ImGui::SetNextWindowSize(ImVec2(rightSidebarWidth, vp->Size.y * 0.55f), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Outliner", &showOutliner, flags)) {
        // Outliner Header (dev.md Outliner Header)
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.894f, 0.894f, 0.906f, 1.0f), "Outliner"); // text-gray-200

        float addBtnW = 20.0f * uiScale;
        float closeBtnW = 20.0f * uiScale;
        float rightBtnsW = addBtnW + closeBtnW + 6.0f * uiScale;

        if (ImGui::GetContentRegionAvail().x > rightBtnsW) {
            ImGui::SameLine(ImGui::GetWindowWidth() - rightBtnsW - ImGui::GetStyle().WindowPadding.x);
        } else {
            ImGui::SameLine();
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.122f, 0.122f, 0.137f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.631f, 0.631f, 0.667f, 1.0f));
        if (ImGui::Button("+##AddActorBtn", ImVec2(addBtnW, 18.0f * uiScale))) {
            ImGui::OpenPopup("AddActorTopPopup");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add Actor");

        ImGui::SameLine(0.0f, 2.0f * uiScale);
        if (ImGui::Button("Ã¢Å“â€¢##CloseOutlinerBtn", ImVec2(closeBtnW, 18.0f * uiScale))) {
            showOutliner = false;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close Outliner");
        ImGui::PopStyleColor(4);

        if (ImGui::BeginPopup("AddActorTopPopup")) {
            if (ImGui::MenuItem("Cube"))      { scene.AddObject(PrimitiveType::Cube, glm::vec3(0, 0.5f, 0), glm::vec3(0.55f)); AddLog("LogActor", "Spawned Cube", 2); }
            if (ImGui::MenuItem("Plane"))     { scene.AddObject(PrimitiveType::Plane, glm::vec3(0), glm::vec3(0.55f)); AddLog("LogActor", "Spawned Plane", 2); }
            if (ImGui::MenuItem("Sphere"))    { scene.AddObject(PrimitiveType::Sphere, glm::vec3(0, 0.6f, 0), glm::vec3(0.55f)); AddLog("LogActor", "Spawned Sphere", 2); }
            if (ImGui::MenuItem("Cylinder"))  { scene.AddObject(PrimitiveType::Cylinder, glm::vec3(0, 0.6f, 0), glm::vec3(0.55f)); AddLog("LogActor", "Spawned Cylinder", 2); }
            if (ImGui::MenuItem("Cone"))      { scene.AddObject(PrimitiveType::Cone, glm::vec3(0), glm::vec3(0.55f)); AddLog("LogActor", "Spawned Cone", 2); }
            if (ImGui::MenuItem("Torus"))     { scene.AddObject(PrimitiveType::Torus, glm::vec3(0, 0.6f, 0), glm::vec3(0.55f)); AddLog("LogActor", "Spawned Torus", 2); }
            ImGui::Separator();
            if (ImGui::MenuItem("Directional Light")) { scene.AddNewLight(PrimitiveType::DirectionalLight, glm::vec3(0, 2.5f, 0)); AddLog("LogActor", "Added Directional Light", 2); }
            if (ImGui::MenuItem("Point Light"))       { scene.AddNewLight(PrimitiveType::PointLight,       glm::vec3(0, 2.5f, 0)); AddLog("LogActor", "Added Point Light", 2); }
            if (ImGui::MenuItem("Spot Light"))        { scene.AddNewLight(PrimitiveType::SpotLight,        glm::vec3(0, 2.5f, 0)); AddLog("LogActor", "Added Spot Light", 2); }
            ImGui::Separator();
            if (ImGui::MenuItem("Camera"))            { GameObject& c = scene.AddObject(PrimitiveType::Camera, glm::vec3(0.0f, 2.0f, -4.0f)); c.name = "Camera Actor"; AddLog("LogActor", "Spawned Camera", 2); }
            if (ImGui::MenuItem("Empty Actor"))       { scene.AddEmptyActor(glm::vec3(0)); AddLog("LogActor", "Spawned Empty Actor", 2); }
            if (ImGui::MenuItem("Level Animation"))   {
                GameObject& la = scene.AddEmptyActor(glm::vec3(0));
                la.name = "LevelAnimation";
                la.type = PrimitiveType::LevelAnimation;
                la.AddLevelAnimation();
                AddLog("LogActor", "Spawned Level Animation Actor", 2);
            }
            ImGui::EndPopup();
        }

        // Outliner Search Row (dev.md)
        ImGui::PushItemWidth(-1.0f);
        ImGui::InputTextWithHint("##Filter", "Search Actors...", outlinerFilter, sizeof(outlinerFilter));
        ImGui::PopItemWidth();
        ImGui::Separator();

        // Outliner Actor List Table
        ImGui::BeginChild("OutlinerList", ImVec2(0, 0), false);

        // If filtering, display matching flat list
        if (outlinerFilter[0] != '\0') {
            std::string filterStr = outlinerFilter;
            std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

            for (auto& obj : scene.objects) {
                std::string nameStr = obj.name;
                std::transform(nameStr.begin(), nameStr.end(), nameStr.begin(), ::tolower);
                bool matches = (nameStr.find(filterStr) != std::string::npos);
                if (!matches) {
                    for (const auto& t : obj.tags) {
                        std::string tLow = t;
                        std::transform(tLow.begin(), tLow.end(), tLow.begin(), ::tolower);
                        if (tLow.find(filterStr) != std::string::npos) {
                            matches = true;
                            break;
                        }
                    }
                }
                if (!matches) continue;

                ImGui::PushID(obj.id);

                ImVec2 rowPos = ImGui::GetCursorScreenPos();
                float rowHeight = ImGui::GetFrameHeight();
                ImVec2 rowMin(ImGui::GetWindowPos().x, rowPos.y);
                ImVec2 rowMax(ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), rowPos.y + rowHeight);
                bool isRowHovered = ImGui::IsMouseHoveringRect(rowMin, rowMax);

                if (outlinerEyeIconGpuHandle) {
                    ImVec4 eyeTint;
                    if (obj.visible) {
                        eyeTint = isRowHovered ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 0.0f);
                    } else {
                        eyeTint = isRowHovered ? ImVec4(1.0f, 1.0f, 1.0f, 0.85f) : ImVec4(1.0f, 1.0f, 1.0f, 0.35f);
                    }

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.25f, 0.35f, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.15f, 0.25f, 0.7f));
                    if (ImGui::ImageButton("##FilterEyeBtn", (ImTextureID)outlinerEyeIconGpuHandle, ImVec2(14.0f, 14.0f), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), eyeTint)) {
                        obj.visible = !obj.visible;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(obj.visible ? "Hide Actor in Viewport" : "Show Actor in Viewport");
                    }
                    ImGui::PopStyleColor(3);
                } else {
                    if (ImGui::SmallButton(obj.visible ? "eye" : " - ")) {
                        obj.visible = !obj.visible;
                    }
                }
                ImGui::SameLine();

                bool isSelected = (selectedActorIds.count(obj.id) > 0 || scene.selectedId == obj.id);
                if (isSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.14f, 0.45f, 0.85f, 0.75f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.52f, 0.95f, 0.85f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.22f, 0.58f, 1.00f, 0.95f));
                }
                ImGui::Selectable(obj.name.c_str(), isSelected);
                bool isClickedUp = ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                float dragDistSqr = ImGui::GetIO().MouseDragMaxDistanceSqr[0];
                float dragThreshold = ImGui::GetIO().MouseDragThreshold;
                bool wasDragging = (dragDistSqr >= dragThreshold * dragThreshold) || (ImGui::GetDragDropPayload() != nullptr);
                if (isClickedUp && !wasDragging) {
                    ImGuiIO& io = ImGui::GetIO();
                    if (io.KeyCtrl) {
                        if (selectedActorIds.count(obj.id)) {
                            selectedActorIds.erase(obj.id);
                            if (scene.selectedId == obj.id) {
                                scene.selectedId = selectedActorIds.empty() ? -1 : *selectedActorIds.begin();
                            }
                        } else {
                            selectedActorIds.insert(obj.id);
                            scene.selectedId = obj.id;
                        }
                        lastSelectedActorId = obj.id;
                    } else if (io.KeyShift && lastSelectedActorId != -1) {
                        int idx1 = -1, idx2 = -1;
                        for (size_t i = 0; i < scene.objects.size(); ++i) {
                            if (scene.objects[i].id == lastSelectedActorId) idx1 = (int)i;
                            if (scene.objects[i].id == obj.id) idx2 = (int)i;
                        }
                        if (idx1 != -1 && idx2 != -1) {
                            int start = std::min(idx1, idx2);
                            int end = std::max(idx1, idx2);
                            selectedActorIds.clear();
                            for (int i = start; i <= end; ++i) {
                                selectedActorIds.insert(scene.objects[i].id);
                            }
                            scene.selectedId = obj.id;
                        } else {
                            selectedActorIds.clear();
                            selectedActorIds.insert(obj.id);
                            scene.selectedId = obj.id;
                            lastSelectedActorId = obj.id;
                        }
                    } else {
                        selectedActorIds.clear();
                        selectedActorIds.insert(obj.id);
                        scene.selectedId = obj.id;
                        lastSelectedActorId = obj.id;
                    }
                }
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    int actorId = obj.id;
                    ImGui::SetDragDropPayload("OUTLINER_ACTOR", &actorId, sizeof(int));
                    ImGui::Text("Reparent %s", obj.name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (isSelected) {
                    ImGui::PopStyleColor(3);
                }
                ImGui::PopID();
            }
        } else {
            // Hierarchical tree view: Level name as Root of all in the respective level
            std::string levelName = "Untitled Level";
            if (!currentLevelFilePath.empty()) {
                levelName = std::filesystem::path(currentLevelFilePath).stem().string();
            }

            ImGuiTreeNodeFlags levelFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            std::string rootKey = "##LevelRootNode";
            if (m_outlinerStateNeedsApply && m_outlinerAppliedNodes.find(rootKey) == m_outlinerAppliedNodes.end()) {
                auto it = scene.outlinerNodeOpenState.find(rootKey);
                if (it != scene.outlinerNodeOpenState.end()) {
                    ImGui::SetNextItemOpen(it->second);
                }
                m_outlinerAppliedNodes.insert(rootKey);
            }
            bool levelNodeOpen = ImGui::TreeNodeEx("##LevelRootNode", levelFlags, "%s (%d actors)", levelName.c_str(), (int)scene.objects.size());
            scene.outlinerNodeOpenState[rootKey] = levelNodeOpen;

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OUTLINER_ACTOR")) {
                    if (payload->Data && payload->DataSize == sizeof(int)) {
                        int draggedId = *(const int*)payload->Data;
                        m_pendingReparentChild = draggedId;
                        m_pendingReparentParent = -1;
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (levelNodeOpen) {
                std::unordered_set<int> visitedIds;
                std::vector<int> rootIds;
                for (const auto& obj : scene.objects) {
                    if (obj.parentId == -1) {
                        rootIds.push_back(obj.id);
                    }
                }
                for (int rid : rootIds) {
                    GameObject* rootObj = scene.FindObject(rid);
                    if (rootObj) {
                        DrawOutlinerNode(*rootObj, scene, visitedIds, 0);
                    }
                }
                ImGui::TreePop();
            }
        }

        // Drop on Outliner empty space to unparent / move to root
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OUTLINER_ACTOR")) {
                if (payload->Data && payload->DataSize == sizeof(int)) {
                    int draggedId = *(const int*)payload->Data;
                    m_pendingReparentChild = draggedId;
                    m_pendingReparentParent = -1;
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Right click context menu on outliner background
        if (ImGui::BeginPopupContextWindow("OutlinerContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::BeginMenu("3D Meshes / Primitives")) {
                if (ImGui::MenuItem("Cube"))      { scene.AddObject(PrimitiveType::Cube); AddLog("LogActor", "Spawned Cube", 2); }
                if (ImGui::MenuItem("Box"))       { scene.AddObject(PrimitiveType::Box); AddLog("LogActor", "Spawned Box", 2); }
                if (ImGui::MenuItem("Plane"))     { scene.AddObject(PrimitiveType::Plane); AddLog("LogActor", "Spawned Plane", 2); }
                if (ImGui::MenuItem("Sphere"))    { scene.AddObject(PrimitiveType::Sphere); AddLog("LogActor", "Spawned Sphere", 2); }
                if (ImGui::MenuItem("UV Sphere")) { scene.AddObject(PrimitiveType::UVSphere); AddLog("LogActor", "Spawned UV Sphere", 2); }
                if (ImGui::MenuItem("Icosphere")) { scene.AddObject(PrimitiveType::Icosphere); AddLog("LogActor", "Spawned Icosphere", 2); }
                if (ImGui::MenuItem("Cylinder"))  { scene.AddObject(PrimitiveType::Cylinder); AddLog("LogActor", "Spawned Cylinder", 2); }
                if (ImGui::MenuItem("Cone"))      { scene.AddObject(PrimitiveType::Cone); AddLog("LogActor", "Spawned Cone", 2); }
                if (ImGui::MenuItem("Capsule"))   { scene.AddObject(PrimitiveType::Capsule); AddLog("LogActor", "Spawned Capsule", 2); }
                if (ImGui::MenuItem("Torus"))     { scene.AddObject(PrimitiveType::Torus); AddLog("LogActor", "Spawned Torus", 2); }
                if (ImGui::MenuItem("Circle"))    { scene.AddObject(PrimitiveType::Circle); AddLog("LogActor", "Spawned Circle", 2); }
                if (ImGui::MenuItem("Disc"))      { scene.AddObject(PrimitiveType::Disc); AddLog("LogActor", "Spawned Disc", 2); }
                if (ImGui::MenuItem("Quad"))      { scene.AddObject(PrimitiveType::Quad); AddLog("LogActor", "Spawned Quad", 2); }
                if (ImGui::MenuItem("Triangle"))  { scene.AddObject(PrimitiveType::Triangle); AddLog("LogActor", "Spawned Triangle", 2); }
                if (ImGui::MenuItem("Pyramid"))   { scene.AddObject(PrimitiveType::Pyramid); AddLog("LogActor", "Spawned Pyramid", 2); }
                if (ImGui::MenuItem("Prism"))     { scene.AddObject(PrimitiveType::Prism); AddLog("LogActor", "Spawned Prism", 2); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Lights")) {
                if (ImGui::MenuItem("Directional Light")) { scene.AddNewLight(PrimitiveType::DirectionalLight); AddLog("LogActor", "Spawned Directional Light", 2); }
                if (ImGui::MenuItem("Point Light"))       { scene.AddNewLight(PrimitiveType::PointLight); AddLog("LogActor", "Spawned Point Light", 2); }
                if (ImGui::MenuItem("Spot Light"))        { scene.AddNewLight(PrimitiveType::SpotLight); AddLog("LogActor", "Spawned Spot Light", 2); }
                if (ImGui::MenuItem("Area Light"))        { scene.AddNewLight(PrimitiveType::AreaLight); AddLog("LogActor", "Spawned Area Light", 2); }
                if (ImGui::MenuItem("Sky Light"))         { scene.AddNewLight(PrimitiveType::SkyLight); AddLog("LogActor", "Spawned Sky Light", 2); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Volumes / Collisions")) {
                if (ImGui::BeginMenu("Collision Shapes (Blockers)")) {
                    if (ImGui::MenuItem("Box Collision"))      { scene.AddCollisionActor(CollisionShapeType::Box,       {0,0.5f,0}, false); AddLog("LogActor", "Spawned Box Collision Volume", 2); }
                    if (ImGui::MenuItem("Sphere Collision"))   { scene.AddCollisionActor(CollisionShapeType::Sphere,    {0,0.5f,0}, false); AddLog("LogActor", "Spawned Sphere Collision Volume", 2); }
                    if (ImGui::MenuItem("Capsule Collision"))  { scene.AddCollisionActor(CollisionShapeType::Capsule,   {0,0.5f,0}, false); AddLog("LogActor", "Spawned Capsule Collision Volume", 2); }
                    if (ImGui::MenuItem("Cylinder Collision")) { scene.AddCollisionActor(CollisionShapeType::Cylinder,  {0,0.5f,0}, false); AddLog("LogActor", "Spawned Cylinder Collision Volume", 2); }
                    if (ImGui::MenuItem("Plane Collision"))    { scene.AddCollisionActor(CollisionShapeType::Plane,     {0,0.0f,0}, false); AddLog("LogActor", "Spawned Plane Collision Volume", 2); }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Trigger Volumes")) {
                    if (ImGui::MenuItem("Box Trigger"))        { scene.AddCollisionActor(CollisionShapeType::Box,       {0,0.5f,0}, true); AddLog("LogActor", "Spawned Box Trigger Volume", 2); }
                    if (ImGui::MenuItem("Sphere Trigger"))     { scene.AddCollisionActor(CollisionShapeType::Sphere,    {0,0.5f,0}, true); AddLog("LogActor", "Spawned Sphere Trigger Volume", 2); }
                    if (ImGui::MenuItem("Capsule Trigger"))    { scene.AddCollisionActor(CollisionShapeType::Capsule,   {0,0.5f,0}, true); AddLog("LogActor", "Spawned Capsule Trigger Volume", 2); }
                    if (ImGui::MenuItem("Cylinder Trigger"))   { scene.AddCollisionActor(CollisionShapeType::Cylinder,  {0,0.5f,0}, true); AddLog("LogActor", "Spawned Cylinder Trigger Volume", 2); }
                    if (ImGui::MenuItem("Plane Trigger"))      { scene.AddCollisionActor(CollisionShapeType::Plane,     {0,0.0f,0}, true); AddLog("LogActor", "Spawned Plane Trigger Volume", 2); }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Camera")) {
                GameObject& cam = scene.AddObject(PrimitiveType::Camera, {0.0f, 2.0f, -4.0f});
                cam.name = "Camera Actor";
                AddLog("LogActor", "Spawned Camera Actor via Outliner Context Menu", 2);
            }
            if (ImGui::MenuItem("User Interface")) {
                scene.AddUserInterface();
                AddLog("LogActor", "Spawned User Interface Actor", 2);
            }
            if (ImGui::MenuItem("Terrain")) {
                UndoManager::Get().RecordSnapshot(scene, "Add Terrain");
                GameObject& terrain = scene.AddTerrain();
                selectedActorIds.clear();
                selectedActorIds.insert(terrain.id);
                scene.selectedId = terrain.id;
                scene.outlinerNodeOpenState["##LevelRootNode"] = true;
                AddLog("LogActor", "Spawned Terrain Actor: " + terrain.name, 2);
            }
            if (ImGui::BeginMenu("Water Body")) {
                if (ImGui::MenuItem("Ocean"))       { scene.AddWaterBody(WaterBodyType::Ocean,      {0,0,0}, "Ocean");       AddLog("LogActor", "Spawned Ocean Water Body", 2); }
                if (ImGui::MenuItem("Sea"))          { scene.AddWaterBody(WaterBodyType::Sea,        {0,0,0}, "Sea");         AddLog("LogActor", "Spawned Sea Water Body", 2); }
                if (ImGui::MenuItem("Lake"))         { scene.AddWaterBody(WaterBodyType::Lake,       {0,0,0}, "Lake");        AddLog("LogActor", "Spawned Lake Water Body", 2); }
                if (ImGui::MenuItem("River"))        { scene.AddWaterBody(WaterBodyType::River,      {0,0,0}, "River");       AddLog("LogActor", "Spawned River Water Body", 2); }
                if (ImGui::MenuItem("Stream"))       { scene.AddWaterBody(WaterBodyType::Stream,     {0,0,0}, "Stream");      AddLog("LogActor", "Spawned Stream Water Body", 2); }
                if (ImGui::MenuItem("Waterfall"))    { scene.AddWaterBody(WaterBodyType::Waterfall,  {0,0,0}, "Waterfall");   AddLog("LogActor", "Spawned Waterfall Water Body", 2); }
                if (ImGui::MenuItem("Pond"))         { scene.AddWaterBody(WaterBodyType::Pond,       {0,0,0}, "Pond");        AddLog("LogActor", "Spawned Pond Water Body", 2); }
                if (ImGui::MenuItem("Pool"))         { scene.AddWaterBody(WaterBodyType::Pool,       {0,0,0}, "Pool");        AddLog("LogActor", "Spawned Pool Water Body", 2); }
                if (ImGui::MenuItem("Wet Surface"))  { scene.AddWaterBody(WaterBodyType::WetSurface, {0,0,0}, "Wet Surface"); AddLog("LogActor", "Spawned Wet Surface Water Body", 2); }
                if (ImGui::MenuItem("Puddle"))       { scene.AddWaterBody(WaterBodyType::Puddle,     {0,0,0}, "Puddle");      AddLog("LogActor", "Spawned Puddle Water Body", 2); }
                if (ImGui::MenuItem("Flood Water"))  { scene.AddWaterBody(WaterBodyType::FloodWater, {0,0,0}, "Flood Water"); AddLog("LogActor", "Spawned Flood Water Body", 2); }
                if (ImGui::MenuItem("Underwater"))   { scene.AddWaterBody(WaterBodyType::Underwater, {0,0,0}, "Underwater");  AddLog("LogActor", "Spawned Underwater Volume", 2); }
                if (ImGui::MenuItem("Swamp"))        { scene.AddWaterBody(WaterBodyType::Swamp,      {0,0,0}, "Swamp");       AddLog("LogActor", "Spawned Swamp Water Body", 2); }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Empty Actor")) { scene.AddEmptyActor(); AddLog("LogActor", "Spawned Empty Actor", 2); }
            if (ImGui::MenuItem("Level Animation")) {
                GameObject& la = scene.AddEmptyActor();
                la.name = "LevelAnimation";
                la.type = PrimitiveType::LevelAnimation;
                la.AddLevelAnimation();
                AddLog("LogActor", "Spawned Level Animation Actor", 2);
            }
            ImGui::EndPopup();
        }

        ImGui::EndChild();
        // Process any deferred reparent request after all outliner nodes and tree pops have cleanly finished
        if (m_pendingReparentChild != -1) {
            int childId = m_pendingReparentChild;
            int parentId = m_pendingReparentParent;
            m_pendingReparentChild = -1;
            m_pendingReparentParent = -1;

            GameObject* child = scene.FindObject(childId);
            std::string childName = child ? child->name : ("Actor " + std::to_string(childId));

            if (parentId != -1) {
                GameObject* parent = scene.FindObject(parentId);
                std::string parentName = parent ? parent->name : ("Actor " + std::to_string(parentId));
                scene.ReparentObject(childId, parentId);
                AddLog("LogActor", "Reparented '" + childName + "' into '" + parentName + "'", 0);
                EngineLogger::Get().LogAction("REPARENT_ACTOR", childName, "Parent: " + parentName);
            } else {
                scene.ReparentObject(childId, -1);
                AddLog("LogActor", "Detached '" + childName + "' to Root Level", 0);
                EngineLogger::Get().LogAction("UNPARENT_ACTOR", childName, "Moved to Root");
            }
        }
    }
    ImGui::End();
}// Touch
