#include <Editor/EngineUI.h>
#include <Editor/UndoManager.h>
#include <Editor/EditorGizmoSystem.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cctype>
#include <windows.h>
#include <commdlg.h>
#ifdef GetClassName
#undef GetClassName
#endif
#include <map>
#include <EngineAssets/TextureManager.h>
#include <EngineAssets/DirectXTex.h>
#include <EngineAssets/MeshImporter.h>
#include <EngineCore/EngineLogger.h>
#include <EngineScene/BehaviourRegistry.h>
#include <EnginePlatform/InputSystem.h>
#include <EngineScene/ScreenPrint.h>
#include <shellapi.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>
#include <Water/WaterSystem.h>
#include <UISystem/UIEditorWindow.h>
#include <UISystem/UISystem.h>
#include <Collisions/CollisionTypes.h>
#include <Collisions/CollisionSystem.h>
#include <PostProcess/PostProcessSystem.h>
#include <TextureStreaming/TextureStreamingSystem.h>
#include <Node-Behaviour/NodeBehaviourManager.h>
#include <Node-Behaviour/NodeBehaviourInstance.h>
#include <SFLOD/SFLODSystem.h>
#include <Animation-Graph/AnimatorGraphEditor.h>
#include <Animation-Blending/AnimationBlendManager.h>
#include <EngineAssets/SkeletalAnimation.h>
#include <Level-Animation/LevelAnimation.h>
#include <Level-Animation/LevelAnimationEditor.h>
#include <Level-Animation/LevelAnimationManager.h>
bool EngineUI::DrawVec3Row(const char* rowLabel, glm::vec3& values, float resetValue, float speed, bool isScale, bool* lockAspect) {
    bool modified = false;
    ImGui::PushID(rowLabel);

    ImGui::Text("%s", rowLabel);

    float availW = ImGui::GetContentRegionAvail().x;
    float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
    // 3 columns: X, Y, Z
    float colW = (availW - itemSpacing * 2.0f) / 3.0f;
    float btnW = 20.0f * uiScale;
    float dragW = std::max(20.0f, colW - btnW - 2.0f);

    struct AxisInfo {
        const char* label;
        float* val;
        glm::vec4 col;
    };
    AxisInfo axes[3] = {
        { "X", &values.x, glm::vec4(0.85f, 0.22f, 0.22f, 1.0f) },
        { "Y", &values.y, glm::vec4(0.25f, 0.75f, 0.25f, 1.0f) },
        { "Z", &values.z, glm::vec4(0.22f, 0.50f, 0.90f, 1.0f) }
    };

    float prevX = values.x;

    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, itemSpacing);
        ImGui::PushID(axes[i].label);

        // Axis button pill
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(axes[i].col.r, axes[i].col.g, axes[i].col.b, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(axes[i].col.r * 1.15f, axes[i].col.g * 1.15f, axes[i].col.b * 1.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(axes[i].col.r * 0.9f, axes[i].col.g * 0.9f, axes[i].col.b * 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        if (ImGui::Button(axes[i].label, ImVec2(btnW, 0))) {
            *axes[i].val = resetValue;
            modified = true;
        }
        ImGui::PopStyleColor(4);

        ImGui::SameLine(0.0f, 2.0f);
        ImGui::SetNextItemWidth(dragW);
        char dragId[32];
        snprintf(dragId, sizeof(dragId), "##val_%s", axes[i].label);
        if (ImGui::DragFloat(dragId, axes[i].val, speed, 0.0f, 0.0f, "%.2f")) {
            modified = true;
            if (isScale && i == 0 && lockAspect && *lockAspect) {
                float ratio = (prevX > 0.001f || prevX < -0.001f) ? (values.x / prevX) : 1.0f;
                values.y *= ratio;
                values.z *= ratio;
            }
        }

        ImGui::PopID();
    }

    ImGui::PopID();
    if (modified) {
        RequestViewportRedraw(10);
    }
    return modified;
}

bool EngineUI::DrawTransformPill(const char* label, float& value, const glm::vec4& color, float resetValue, float speed) {
    bool modified = false;
    ImGui::PushID(label);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.r, color.g, color.b, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.r * 1.15f, color.g * 1.15f, color.b * 1.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.r * 0.9f, color.g * 0.9f, color.b * 0.9f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    char btnLabel[32];
    snprintf(btnLabel, sizeof(btnLabel), "%s##btn_%s", label, label);
    if (ImGui::Button(btnLabel, ImVec2(24, 0))) {
        value = resetValue;
        modified = true;
    }
    ImGui::PopStyleColor(4);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 6.0f);
    char valLabel[32];
    snprintf(valLabel, sizeof(valLabel), "##val_%s", label);
    if (ImGui::DragFloat(valLabel, &value, speed, 0.0f, 0.0f, "%.2f")) {
        modified = true;
    }

    ImGui::PopID();
    if (modified) {
        RequestViewportRedraw(10);
    }
    return modified;
}

struct FoundMat {
    std::string name;        // e.g. "Default_Material"
    std::string relPath;     // e.g. "Materials/Default_Material.emat"
    std::string relFolder;   // e.g. "Materials", or "" for project root
    std::string fullPath;    // Full absolute path on disk
    glm::vec3 color{0.55f, 0.55f, 0.55f};
    float metallic = 0.0f;
    float roughness = 0.5f;
};

static std::vector<FoundMat> s_cachedMatList;
static std::map<std::string, std::vector<FoundMat>> s_cachedFolderGroups;
static auto s_lastMatScanTime = std::chrono::steady_clock::time_point::min();

static void RefreshCachedMaterials(const std::filesystem::path& contentRootPath) {
    auto now = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastMatScanTime).count();
    if (elapsedMs < 2000 && !s_cachedMatList.empty()) return;

    s_lastMatScanTime = now;
    s_cachedMatList.clear();
    s_cachedFolderGroups.clear();

    std::error_code matEc;
    if (std::filesystem::exists(contentRootPath, matEc)) {
        auto options = std::filesystem::directory_options::skip_permission_denied;
        for (auto it = std::filesystem::recursive_directory_iterator(contentRootPath, options, matEc);
             !matEc && it != std::filesystem::recursive_directory_iterator();
             it.increment(matEc)) {
            if (!it->is_directory(matEc)) {
                std::string ext = it->path().extension().string();
                if (ext == ".emat" || ext == ".mat") {
                    FoundMat fm;
                    fm.name = it->path().stem().string();
                    fm.fullPath = it->path().string();

                    std::filesystem::path rel = std::filesystem::relative(it->path(), contentRootPath, matEc);
                    if (!matEc) {
                        fm.relPath = rel.generic_string();
                        std::string parentFolder = rel.parent_path().generic_string();
                        fm.relFolder = (parentFolder == ".") ? "" : parentFolder;
                    } else {
                        fm.relPath = it->path().filename().string();
                        fm.relFolder = "";
                    }

                    MaterialAsset ma;
                    if (EngineUI::LoadMaterialFile(fm.fullPath, ma)) {
                        fm.color = ma.baseColor;
                        fm.metallic = ma.metallic;
                        fm.roughness = ma.roughness;
                    }

                    s_cachedMatList.push_back(fm);
                    s_cachedFolderGroups[fm.relFolder].push_back(fm);
                }
            }
        }
    }

    if (s_cachedMatList.empty()) {
        std::filesystem::path defPath = contentRootPath / "Materials" / "Default_Material.emat";
        FoundMat fm;
        fm.name = "Default_Material";
        fm.relPath = "Materials/Default_Material.emat";
        fm.relFolder = "Materials";
        fm.fullPath = defPath.string();
        fm.color = glm::vec3(0.55f, 0.55f, 0.55f);
        s_cachedMatList.push_back(fm);
        s_cachedFolderGroups[fm.relFolder].push_back(fm);
    }
}

void EngineUI::RenderDetails(Scene& scene) {
    ImGuiViewport* vp = ImGui::GetMainViewport();

    float bottomBarH = bottomBarHeight * uiScale;
    // Dockable window: size hint for first launch, can be repositioned or tabbed via DockSpace
    ImGui::SetNextWindowSize(ImVec2(rightSidebarWidth, vp->Size.y * 0.45f), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Details", &showDetails, flags)) {
        // Details Header (dev.md Details Header)
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.894f, 0.894f, 0.906f, 1.0f), "Details"); // text-gray-200

        float closeBtnW = 20.0f * uiScale;
        if (ImGui::GetContentRegionAvail().x > closeBtnW + 4.0f) {
            ImGui::SameLine(ImGui::GetWindowWidth() - closeBtnW - ImGui::GetStyle().WindowPadding.x);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.122f, 0.122f, 0.137f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.631f, 0.631f, 0.667f, 1.0f));
            if (ImGui::Button("Ã¢Å“â€¢##CloseDetailsBtn", ImVec2(closeBtnW, 18.0f * uiScale))) {
                showDetails = false;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close Details");
            ImGui::PopStyleColor(4);
        }
        ImGui::Separator();

        GameObject* obj = scene.GetSelected();
        if (!obj) {
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.443f, 0.443f, 0.478f, 1.0f)); // text-gray-500 #71717a
            ImGui::TextWrapped("Select an actor in the Outliner or Viewport to view details.");
            ImGui::PopStyleColor();
            ImGui::End();
            return;
        }

        ImGui::PushID(obj->id);

        // Actor Header Banner
        if (obj->isLight || IsLightPrimitive(obj->type)) {
            char lightTypeStr[64];
            snprintf(lightTypeStr, sizeof(lightTypeStr), "[%sComponent]", GetLightTypeName(obj->light.type));
            ImGui::TextColored(ImVec4(1.00f, 0.85f, 0.20f, 1.0f), "%s", lightTypeStr);
        } else if (obj->isWaterBody || obj->type == PrimitiveType::WaterBody) {
            ImGui::TextColored(ImVec4(0.00f, 0.60f, 1.00f, 1.0f), "[WaterBodyComponent]");
        } else if (obj->isTerrain || obj->type == PrimitiveType::Terrain) {
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.0f), "[TerrainComponent]");
        } else if (obj->isCamera || obj->type == PrimitiveType::Camera) {
            ImGui::TextColored(ImVec4(0.30f, 0.90f, 0.70f, 1.0f), "[CameraComponent]");
        } else if (obj->isUserInterface || obj->type == PrimitiveType::UserInterface) {
            ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.00f, 1.0f), "[UserInterface3DComponent]");
        } else if (obj->type == PrimitiveType::LevelAnimation || obj->hasLevelAnimation) {
            ImGui::TextColored(ImVec4(1.00f, 0.55f, 0.20f, 1.0f), "[LevelAnimationComponent]");
        } else if (obj->type == PrimitiveType::Empty) {
            if (!obj->modifiers.empty()) {
                if (obj->modifiers[0].isTrigger) {
                    char trigStr[96];
                    snprintf(trigStr, sizeof(trigStr), "[Trigger Volume: %s]", obj->modifiers[0].name.c_str());
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "%s", trigStr);
                } else {
                    char colStr[96];
                    snprintf(colStr, sizeof(colStr), "[Collision Volume: %s]", obj->modifiers[0].name.c_str());
                    ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "%s", colStr);
                }
            } else {
                ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "[TransformActor]");
            }
        } else {
            char meshTypeStr[64];
            snprintf(meshTypeStr, sizeof(meshTypeStr), "[StaticMeshActor (%s)]", GetPrimitiveTypeName(obj->type));
            ImGui::TextColored(ImVec4(0.20f, 0.65f, 1.00f, 1.0f), "%s", meshTypeStr);
        }
        ImGui::SameLine();
        char nameBuf[128];
        strncpy(nameBuf, obj->name.c_str(), sizeof(nameBuf));
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        ImGui::PushItemWidth(-80.0f);
        if (ImGui::InputText("##ActorName", nameBuf, sizeof(nameBuf))) {
            obj->name = nameBuf;
            if (obj->isLight && obj->lightId >= 0 && obj->lightId < (int)scene.pointLights.size()) {
                scene.pointLights[obj->lightId].name = obj->name;
            }
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::TextDisabled("ID: %u", (uint32_t)obj->id);


        ImGui::Separator();

        // Add Component Button (matching Old Eunoia Engine)
        if (ImGui::Button("+ Add Component", ImVec2(-1.0f, 28.0f))) {
            ImGui::OpenPopup("AddComponentPopup");
        }
        if (ImGui::BeginPopup("AddComponentPopup")) {
            if (ImGui::MenuItem("Light Component")) {
                obj->isLight = true;
                obj->light.type = LightType::Point;
                obj->light.color = glm::vec3(1.0f);
                obj->light.intensity = 10.0f;
                obj->light.range = 10.0f;
                AddLog("LogActor", "Added Light Component to " + obj->name, 2);
            }
            if (ImGui::MenuItem("Camera Component")) {
                obj->isCamera = true;
                AddLog("LogActor", "Added Camera Component to " + obj->name, 2);
            }
            if (ImGui::MenuItem("Material Component")) {
                AddLog("LogActor", "Material Component ready on " + obj->name, 2);
            }
            if (ImGui::MenuItem("RigidBody Physics")) {
                obj->hasPhysics = true;
                AddLog("LogActor", "Added RigidBody Physics to " + obj->name, 2);
            }
            if (ImGui::MenuItem("Collider Component")) {
                obj->AddModifier(CollisionShapeType::Box);
                AddLog("LogActor", "Added Collider Component to " + obj->name, 2);
            }
            if (ImGui::MenuItem("Particle Emitter")) {
                AddLog("LogActor", "Added Particle Emitter to " + obj->name, 2);
            }
            if (ImGui::MenuItem("Script Component")) {
                showAddBehaviourPopup = true;
                AddLog("LogActor", "Attaching Script Component to " + obj->name, 2);
            }
            ImGui::EndPopup();
        }
        ImGui::Separator();

        // Unreal Mobility Selector (Static | Stationary | Movable)
        ImGui::TextDisabled("Mobility");
        ImGui::SameLine();
        int mIdx = (int)obj->mobility;
        const char* mobilities[] = { "Static", "Stationary", "Movable" };
        for (int i = 0; i < 3; ++i) {
            bool active = (mIdx == i);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.50f, 0.90f, 1.0f));
            if (ImGui::Button(mobilities[i])) {
                scene.SetObjectMobility(obj->id, (Mobility)i, true);
                if ((Mobility)i == Mobility::Static && !obj->childIds.empty()) {
                    AddLog("LogActor", "Set " + obj->name + " and all children to Static recursively", 1);
                }
            }
            if (active) ImGui::PopStyleColor();
            if (i < 2) ImGui::SameLine();
        }

        // Terrain Mode Switching Tabs (Properties vs Sculpt/Edit vs Materials)
        if (obj->isTerrain || obj->type == PrimitiveType::Terrain) {
            RefreshCachedMaterials(contentRootPath);
            ImGui::Separator();
            ImGui::Spacing();
            static int s_terrainActiveTab = 0; // 0 = Properties, 1 = Sculpt / Edit, 2 = Materials

            // Sync tab with actor state
            if (obj->terrain.editModeActive) {
                s_terrainActiveTab = 1;
            } else if (obj->terrain.materialPaintModeActive) {
                s_terrainActiveTab = 2;
            } else if (s_terrainActiveTab == 1) {
                obj->terrain.editModeActive = true;
                obj->terrain.materialPaintModeActive = false;
            } else if (s_terrainActiveTab == 2) {
                obj->terrain.materialPaintModeActive = true;
                obj->terrain.editModeActive = false;
            }

            float availWidth = ImGui::GetContentRegionAvail().x;
            float tabBtnWidth = (availWidth - 12.0f) / 3.0f;

            // Tab 0: Properties
            if (s_terrainActiveTab == 0) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.55f, 0.90f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.65f, 0.98f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.20f, 0.24f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.28f, 0.35f, 1.0f));
            }
            if (ImGui::Button("Properties##TerrainPropTab", ImVec2(tabBtnWidth, 30.0f))) {
                s_terrainActiveTab = 0;
                obj->terrain.editModeActive = false;
                obj->terrain.materialPaintModeActive = false;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();

            // Tab 1: Sculpt / Edit
            if (s_terrainActiveTab == 1) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.75f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.85f, 0.45f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.20f, 0.24f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.28f, 0.35f, 1.0f));
            }
            if (ImGui::Button("Sculpt / Edit##TerrainEditTab", ImVec2(tabBtnWidth, 30.0f))) {
                s_terrainActiveTab = 1;
                obj->terrain.editModeActive = true;
                obj->terrain.materialPaintModeActive = false;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();

            // Tab 2: Materials
            if (s_terrainActiveTab == 2) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.30f, 0.85f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.40f, 0.95f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.20f, 0.24f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.28f, 0.35f, 1.0f));
            }
            if (ImGui::Button("Materials##TerrainMatTab", ImVec2(tabBtnWidth, 30.0f))) {
                s_terrainActiveTab = 2;
                obj->terrain.materialPaintModeActive = true;
                obj->terrain.editModeActive = false;
            }
            ImGui::PopStyleColor(2);

            if (s_terrainActiveTab == 1) {
                obj->terrain.editModeActive = true;
                obj->terrain.materialPaintModeActive = false;
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "ÃƒÂ¢Ã¢â‚¬â€Ã‚Â Terrain Sculpting Active (LMB on Viewport)");
                ImGui::TextDisabled("Hold Shift to invert brush action (e.g. Lower instead of Raise)");

                if (ImGui::CollapsingHeader("Brush Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                    // Brush Types
                    ImGui::TextDisabled("Brush Mode");
                    const char* brushModes[] = { "Raise", "Lower", "Smooth", "Flatten", "Noise" };
                    int curBrush = (int)obj->terrain.brushType;
                    for (int b = 0; b < 5; ++b) {
                        bool bActive = (curBrush == b);
                        if (bActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.65f, 0.38f, 1.0f));
                        if (ImGui::Button(brushModes[b])) {
                            obj->terrain.brushType = (Eunoia::TerrainBrushType)b;
                        }
                        if (bActive) ImGui::PopStyleColor();
                        if (b < 4) ImGui::SameLine();
                    }
                    ImGui::Spacing();

                    // Falloff
                    const char* falloffNames[] = { "Smooth", "Linear", "Spherical", "Sharp" };
                    int curFalloff = (int)obj->terrain.brushFalloff;
                    if (ImGui::Combo("Falloff", &curFalloff, falloffNames, 4)) {
                        obj->terrain.brushFalloff = (Eunoia::TerrainFalloffType)curFalloff;
                    }

                    // Brush Radius & Strength
                    ImGui::SliderFloat("Brush Radius", &obj->terrain.brushSize, 0.5f, 50.0f, "%.1f m");
                    ImGui::SliderFloat("Brush Strength", &obj->terrain.brushStrength, 0.05f, 20.0f, "%.2f");

                    // Flatten Specifics
                    if (obj->terrain.brushType == Eunoia::TerrainBrushType::Flatten) {
                        ImGui::DragFloat("Target Height", &obj->terrain.flattenTargetHeight, 0.1f, -100.0f, 100.0f, "%.2f m");
                        ImGui::SameLine();
                        if (ImGui::Button("Zero##FlattenZero")) {
                            obj->terrain.flattenTargetHeight = 0.0f;
                        }
                    }

                    // Noise Specifics
                    if (obj->terrain.brushType == Eunoia::TerrainBrushType::Noise) {
                        ImGui::SliderFloat("Noise Scale", &obj->terrain.noiseScale, 0.01f, 1.0f, "%.3f");
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    if (ImGui::Button("Reset All Heights to 0", ImVec2(-1.0f, 26.0f))) {
                        UndoManager::Get().RecordSnapshot(scene, "Reset Heights: " + obj->name);
                        std::fill(obj->terrain.heightmap.begin(), obj->terrain.heightmap.end(), 0.0f);
                        obj->terrain.RebuildTerrainMesh(obj->mesh);
                        AddLog("LogTerrain", "Reset terrain heights to 0 on " + obj->name, 0);
                    }
                }
            } else if (s_terrainActiveTab == 2) {
                obj->terrain.materialPaintModeActive = true;
                obj->terrain.editModeActive = false;
                obj->terrain.SyncBaseMaterial(obj->materialName, obj->color, obj->metallic, obj->roughness,
                                              obj->normalStrength, obj->specular, obj->uvScale,
                                              obj->baseColorTexture, obj->normalTexture, obj->roughnessTexture,
                                              obj->metallicTexture, obj->aoTexture);

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.80f, 0.50f, 1.0f, 1.0f), "ÃƒÂ¢Ã¢â‚¬â€Ã‚Â Terrain Material Painting Active (LMB on Viewport)");
                ImGui::TextDisabled("Select a layer to paint. Drag & drop layers to reorder. Slot [0] is Base Material.");

                if (ImGui::CollapsingHeader("Material Layers Array", ImGuiTreeNodeFlags_DefaultOpen)) {
                    for (int i = 0; i < (int)obj->terrain.materialLayers.size(); ++i) {
                        auto& layer = obj->terrain.materialLayers[i];
                        ImGui::PushID(i);

                        bool isActivePaint = (obj->terrain.activePaintMaterialIndex == i);

                        std::string slotPrefix = "[" + std::to_string(i) + "]";
                        if (i == 0) slotPrefix += " (Base)";
                        else slotPrefix += " (Layer " + std::to_string(i) + ")";

                        if (isActivePaint) {
                            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.80f, 0.50f, 1.0f, 1.0f));
                            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                        }

                        ImGui::BeginGroup();

                        // Swatch button
                        ImGui::ColorButton("##layerSwatch", ImVec4(layer.baseColor.r, layer.baseColor.g, layer.baseColor.b, 1.0f),
                            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(22, 22));
                        ImGui::SameLine();

                        // Selectable layer title / paint selector
                        std::string selectTitle = slotPrefix + " " + layer.name;
                        float availRight = (i > 0) ? 55.0f : 0.0f;
                        if (ImGui::Selectable(selectTitle.c_str(), isActivePaint, 0, ImVec2(ImGui::GetContentRegionAvail().x - availRight, 22.0f))) {
                            obj->terrain.activePaintMaterialIndex = i;
                        }

                        // Drag & Drop Source (reordering)
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                            ImGui::SetDragDropPayload("TERRAIN_LAYER_REORDER", &i, sizeof(int));
                            ImGui::Text("Move %s", selectTitle.c_str());
                            ImGui::EndDragDropSource();
                        }

                        // Drag & Drop Target (reordering & asset drop)
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TERRAIN_LAYER_REORDER")) {
                                int srcIdx = *(const int*)payload->Data;
                                if (srcIdx != i && srcIdx >= 0 && srcIdx < (int)obj->terrain.materialLayers.size()) {
                                    UndoManager::Get().RecordSnapshot(scene, "Reorder Terrain Materials");
                                    obj->terrain.SwapMaterialLayers(srcIdx, i);
                                    if (srcIdx == 0 || i == 0) {
                                        // Slot 0 changed, sync to GameObject
                                        obj->materialName = obj->terrain.materialLayers[0].name;
                                        obj->color = obj->terrain.materialLayers[0].baseColor;
                                        obj->metallic = obj->terrain.materialLayers[0].metallic;
                                        obj->roughness = obj->terrain.materialLayers[0].roughness;
                                        obj->normalStrength = obj->terrain.materialLayers[0].normalStrength;
                                        obj->specular = obj->terrain.materialLayers[0].specular;
                                        obj->uvScale = obj->terrain.materialLayers[0].uvScale;
                                        obj->baseColorTexture = obj->terrain.materialLayers[0].baseColorTexture;
                                        obj->normalTexture = obj->terrain.materialLayers[0].normalTexture;
                                        obj->roughnessTexture = obj->terrain.materialLayers[0].roughnessTexture;
                                        obj->metallicTexture = obj->terrain.materialLayers[0].metallicTexture;
                                        obj->aoTexture = obj->terrain.materialLayers[0].aoTexture;
                                    }
                                    obj->terrain.RebuildTerrainMesh(obj->mesh);
                                    RequestViewportRedraw(5);
                                }
                            }
                            // Also accept dropping an ASSET_ID onto this slot to replace it
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                                char assetIdBuf[40] = {};
                                memcpy(assetIdBuf, payload->Data, std::min((size_t)payload->DataSize, sizeof(assetIdBuf) - 1));
                                AssetID droppedId = AssetID::FromString(assetIdBuf);
                                AssetMetadata* meta = AssetRegistry::Get().Find(droppedId);
                                if (meta && meta->type == AssetType::Material) {
                                    std::filesystem::path fullPath = contentRootPath / meta->sourcePath;
                                    MaterialAsset ma;
                                    if (LoadMaterialFile(fullPath.string(), ma)) {
                                        UndoManager::Get().RecordSnapshot(scene, "Replace Terrain Material Layer");
                                        layer.name = ma.name;
                                        layer.path = meta->sourcePath;
                                        layer.baseColor = ma.baseColor;
                                        layer.metallic = ma.metallic;
                                        layer.roughness = ma.roughness;
                                        layer.normalStrength = ma.normalStrength;
                                        layer.specular = ma.specular;
                                        layer.uvScale = ma.uvScale;
                                        layer.baseColorTexture = ma.baseColorTexture;
                                        layer.normalTexture = ma.normalTexture;
                                        layer.roughnessTexture = ma.roughnessTexture;
                                        layer.metallicTexture = ma.metallicTexture;
                                        layer.aoTexture = ma.aoTexture;
                                        if (i == 0) {
                                            ApplyMaterialToActorAndChildren(scene, obj, ma);
                                        }
                                        obj->terrain.RebuildTerrainMesh(obj->mesh);
                                        RequestViewportRedraw(5);
                                    }
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        if (i > 0) {
                            ImGui::SameLine();
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.15f, 0.15f, 0.8f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
                            if (ImGui::Button("X##RemoveLayer", ImVec2(22, 22))) {
                                UndoManager::Get().RecordSnapshot(scene, "Remove Terrain Material Layer");
                                obj->terrain.RemoveMaterialLayer(i);
                                obj->terrain.RebuildTerrainMesh(obj->mesh);
                                RequestViewportRedraw(5);
                                ImGui::PopStyleColor(2);
                                ImGui::EndGroup();
                                if (isActivePaint) {
                                    ImGui::PopStyleVar();
                                    ImGui::PopStyleColor();
                                }
                                ImGui::PopID();
                                break;
                            }
                            ImGui::PopStyleColor(2);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Remove this material layer (painted quads revert to base material)");
                            }
                        }

                        ImGui::EndGroup();

                        if (isActivePaint) {
                            ImGui::PopStyleVar();
                            ImGui::PopStyleColor();
                        }

                        // UV Tiling per layer
                        ImGui::Indent(28.0f);
                        std::string uvId = "UV Scale##MatLayerUV_" + std::to_string(i);
                        if (ImGui::DragFloat2(uvId.c_str(), &layer.uvScale.x, 0.05f, 0.01f, 100.0f, "%.2f")) {
                            if (i == 0) obj->uvScale = layer.uvScale;
                            RequestViewportRedraw(3);
                        }
                        ImGui::Unindent(28.0f);
                        ImGui::Spacing();

                        ImGui::PopID();
                    }

                    ImGui::Spacing();
                    // Add Material Layer Button
                    if (ImGui::Button("+ Add Material Layer...", ImVec2(ImGui::GetContentRegionAvail().x, 28.0f))) {
                        ImGui::OpenPopup("AddMaterialLayerPopup");
                    }

                    if (ImGui::BeginPopup("AddMaterialLayerPopup")) {
                        ImGui::Text("Select Material to Add:");
                        ImGui::Separator();
                        for (const auto& m : s_cachedMatList) {
                            ImGui::ColorButton("##addSwatch", ImVec4(m.color.r, m.color.g, m.color.b, 1.0f),
                                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(16, 16));
                            ImGui::SameLine();
                            if (ImGui::Selectable(m.name.c_str())) {
                                MaterialAsset ma;
                                if (LoadMaterialFile(m.fullPath, ma)) {
                                    UndoManager::Get().RecordSnapshot(scene, "Add Material Layer: " + ma.name);
                                    Eunoia::TerrainMaterialLayer newLayer;
                                    newLayer.name = ma.name;
                                    newLayer.path = m.relPath;
                                    newLayer.baseColor = ma.baseColor;
                                    newLayer.metallic = ma.metallic;
                                    newLayer.roughness = ma.roughness;
                                    newLayer.normalStrength = ma.normalStrength;
                                    newLayer.specular = ma.specular;
                                    newLayer.uvScale = ma.uvScale;
                                    newLayer.baseColorTexture = ma.baseColorTexture;
                                    newLayer.normalTexture = ma.normalTexture;
                                    newLayer.roughnessTexture = ma.roughnessTexture;
                                    newLayer.metallicTexture = ma.metallicTexture;
                                    newLayer.aoTexture = ma.aoTexture;
                                    obj->terrain.materialLayers.push_back(newLayer);
                                    obj->terrain.activePaintMaterialIndex = (int)obj->terrain.materialLayers.size() - 1;
                                    obj->terrain.RebuildTerrainMesh(obj->mesh);
                                    RequestViewportRedraw(5);
                                }
                            }
                        }
                        ImGui::EndPopup();
                    }
                }

                // Section 2: Material Brush Settings
                if (ImGui::CollapsingHeader("Material Brush Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const char* matFalloffNames[] = { "Smooth (Cosine)", "Linear", "Spherical", "Sharp" };
                    int curMatFalloff = (int)obj->terrain.matBrushFalloff;
                    if (ImGui::Combo("Brush Falloff##MatFalloff", &curMatFalloff, matFalloffNames, 4)) {
                        obj->terrain.matBrushFalloff = (Eunoia::TerrainFalloffType)curMatFalloff;
                    }

                    ImGui::SliderFloat("Brush Radius##MatRadius", &obj->terrain.matBrushSize, 0.5f, 50.0f, "%.1f m");
                    ImGui::SliderFloat("Brush Strength (Flow)##MatStrength", &obj->terrain.matBrushStrength, 0.05f, 10.0f, "%.2f");
                    ImGui::TextDisabled("Tip: Hold Shift + LMB to erase active layer back to base material.");

                    ImGui::Spacing();
                    ImGui::Separator();
                    if (ImGui::Button("Fill All with Active Material", ImVec2(-1.0f, 26.0f))) {
                        UndoManager::Get().RecordSnapshot(scene, "Fill Terrain with Material");
                        obj->terrain.FillAllWithMaterial(obj->terrain.activePaintMaterialIndex);
                        obj->terrain.RebuildTerrainMesh(obj->mesh);
                        RequestViewportRedraw(5);
                    }
                }
            } else {
                obj->terrain.editModeActive = false;
                obj->terrain.materialPaintModeActive = false;
            }
        }

        ImGui::Separator();

        // 1. Transform Category (Horizontal Compact: Location, Rotation, Scale)
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool transformChanged = false;

            // Location
            transformChanged |= DrawVec3Row("Location", obj->position, 0.0f, 0.05f);
            ImGui::Spacing();

            // Rotation
            transformChanged |= DrawVec3Row("Rotation", obj->rotation, 0.0f, 1.0f);
            ImGui::Spacing();

            // Scale
            transformChanged |= DrawVec3Row("Scale", obj->scale, 1.0f, 0.02f, true, &lockAspectScale);
            ImGui::Spacing();
            ImGui::Checkbox("Lock Uniform Scale", &lockAspectScale);

                        if (transformChanged && Eunoia::LevelAnimationManager::Get().IsRecording()) {
                Eunoia::LevelAnimationManager::Get().RecordTransform(scene, *obj);
            }
            if (transformChanged && (obj->isLight || IsLightPrimitive(obj->type))) {
                scene.SyncLightPositionsFromActors();
            }
        }

        // 2. Tags Section (Supported for all objects, actors, and empty actors)
        char tagsHeader[64];
        snprintf(tagsHeader, sizeof(tagsHeader), "Tags (%d)###TagsCategory", (int)obj->tags.size());
        if (ImGui::CollapsingHeader(tagsHeader, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("TagsSection");

            static char s_newTagBuf[128] = "";
            bool addRequested = false;

            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
            if (ImGui::InputTextWithHint("##NewTagInput", "Enter tag name...", s_newTagBuf, sizeof(s_newTagBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                addRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("+ Add", ImVec2(62.0f, 0.0f))) {
                addRequested = true;
            }

            if (addRequested) {
                std::string newTagStr = s_newTagBuf;
                size_t b = newTagStr.find_first_not_of(" \t\r\n");
                size_t e = newTagStr.find_last_not_of(" \t\r\n");
                if (b != std::string::npos && e != std::string::npos) {
                    newTagStr = newTagStr.substr(b, e - b + 1);
                    obj->AddTag(newTagStr);
                    s_newTagBuf[0] = '\0';
                }
            }

            if (obj->tags.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("No tags assigned to this actor.");
                ImGui::Spacing();
            } else {
                ImGui::Spacing();
                int tagToRemove = -1;
                for (size_t ti = 0; ti < obj->tags.size(); ++ti) {
                    ImGui::PushID((int)ti);

                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "[%d]", (int)ti);
                    ImGui::SameLine();

                    char tagBuf[128];
                    strncpy(tagBuf, obj->tags[ti].c_str(), sizeof(tagBuf));
                    tagBuf[sizeof(tagBuf) - 1] = '\0';
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 32.0f);
                    if (ImGui::InputText("##TagEdit", tagBuf, sizeof(tagBuf))) {
                        obj->tags[ti] = tagBuf;
                    }
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 0.8f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                    if (ImGui::Button("X##RemoveTag", ImVec2(24.0f, 0.0f))) {
                        tagToRemove = (int)ti;
                    }
                    ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Remove this tag");
                    }

                    ImGui::PopID();
                }

                if (tagToRemove >= 0 && tagToRemove < (int)obj->tags.size()) {
                    obj->tags.erase(obj->tags.begin() + tagToRemove);
                }

                ImGui::Spacing();
                if (ImGui::Button("Clear All Tags", ImVec2(ImGui::GetContentRegionAvail().x, 22.0f))) {
                    obj->RemoveAllTag();
                }
            }

            ImGui::PopID();
        }

        // Light Component Category (dev.md)
        if (obj->isLight || IsLightPrimitive(obj->type)) {
            char lightHeader[128];
            snprintf(lightHeader, sizeof(lightHeader), "%s Component", GetLightTypeName(obj->light.type));
            if (ImGui::CollapsingHeader(lightHeader, ImGuiTreeNodeFlags_DefaultOpen)) {
                uint64_t iconHandle = GetLightIconGpuHandle(obj->light.type);
                if (iconHandle != 0) {
                    ImGui::Image((ImTextureID)iconHandle, ImVec2(20, 20));
                    ImGui::SameLine();
                }
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "[%s]", GetLightTypeName(obj->light.type));
                ImGui::SameLine();
                ImGui::Checkbox("Light Enabled", &obj->light.enabled);

                ImGui::Separator();
                ImGui::TextDisabled("Light Settings");

                if (ImGui::ColorEdit3("Color##LightComponentColor", &obj->light.color.r,
                                      ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB)) {
                    obj->color = obj->light.color;
                    if (obj->lightId >= 0 && obj->lightId < (int)scene.pointLights.size()) {
                        scene.pointLights[obj->lightId].color = obj->light.color;
                    }
                    if (obj->light.type == LightType::Directional) {
                        scene.lightColor = obj->light.color;
                    } else if (obj->light.type == LightType::Sky) {
                        scene.ambientColor = obj->light.color;
                    }
                }

                ImGui::DragFloat("Intensity##LightIntensity", &obj->light.intensity, 1.0f, 0.0f, 100000000.0f, "%.2f");

                ImGui::Checkbox("Use Temperature (Kelvin)##UseTemp", &obj->light.useTemperature);
                if (obj->light.useTemperature) {
                    ImGui::SliderFloat("Temperature##LightTemp", &obj->light.temperature, 1000.0f, 12000.0f, "%.0f K");
                    glm::vec3 kelvinCol = ColorTemperatureToRGB(obj->light.temperature);
                    ImGui::SameLine();
                    ImGui::ColorButton("##KelvinPreview", ImVec4(kelvinCol.r, kelvinCol.g, kelvinCol.b, 1.0f),
                                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(20, 20));
                    ImGui::TextDisabled("Combined Tint: R:%.2f G:%.2f B:%.2f",
                                        kelvinCol.r * obj->light.color.r,
                                        kelvinCol.g * obj->light.color.g,
                                        kelvinCol.b * obj->light.color.b);
                }

                if (obj->light.type != LightType::Directional && obj->light.type != LightType::Sky) {
                    ImGui::DragFloat("Range / Radius", &obj->light.range, 1.0f, 0.1f, 10000000.0f, "%.1f m");
                    ImGui::DragFloat("Attenuation Exp", &obj->light.attenuation, 0.05f, 0.0f, 1000.0f, "%.2f");
                }

                if (obj->light.type == LightType::Sky) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Sky Light / Ambient System");
                    ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Sky Light provides omnidirectional ambient scene illumination.");
                    ImGui::TextDisabled("Note: Sky light produces pure ambient light and does not cast shadows.");
                }

                if (obj->light.type == LightType::Spot) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Spot Cone & Falloff");
                    ImGui::SliderFloat("Inner Cone Angle", &obj->light.innerConeAngle, 0.0f, 89.0f, "%.1f deg");
                    if (obj->light.outerConeAngle < obj->light.innerConeAngle) obj->light.outerConeAngle = obj->light.innerConeAngle;
                    ImGui::SliderFloat("Outer Cone Angle", &obj->light.outerConeAngle, obj->light.innerConeAngle, 89.9f, "%.1f deg");
                    ImGui::DragFloat("Cone Falloff", &obj->light.coneFalloff, 0.05f, 0.1f, 100.0f, "%.2f");
                    ImGui::SliderFloat("Range Smoothing", &obj->light.rangeSmoothing, 0.01f, 1.0f, "%.2f");
                }

                if (obj->light.type == LightType::Point) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Punctual / Capsule Light");
                    ImGui::DragFloat("Source Radius", &obj->light.radius, 0.005f, 0.001f, 100.0f, "%.3f m");
                    ImGui::DragFloat("Capsule Length", &obj->light.length, 0.05f, 0.0f, 1000.0f, "%.2f m");
                }

                if (obj->light.type == LightType::Rectangle || obj->light.type == LightType::Area) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Rectangle / Area Light");
                    const char* areaShapes[] = { "Rectangle", "Disk" };
                    ImGui::Combo("Shape", &obj->light.areaShape, areaShapes, 2);
                    if (obj->light.areaShape == 0) {
                        float w = obj->light.length > 0.001f ? obj->light.length : obj->light.width;
                        if (ImGui::DragFloat("Width", &w, 0.1f, 0.01f, 1000.0f, "%.2f m")) {
                            obj->light.length = w;
                            obj->light.width = w;
                        }
                        ImGui::DragFloat("Height", &obj->light.height, 0.1f, 0.01f, 1000.0f, "%.2f m");
                    } else {
                        ImGui::DragFloat("Radius", &obj->light.radius, 0.1f, 0.01f, 1000.0f, "%.2f m");
                    }
                    ImGui::Checkbox("Two Sided", &obj->light.twoSided);
                }

                if (obj->light.type == LightType::Directional || obj->light.type == LightType::Point ||
                    obj->light.type == LightType::Spot || obj->light.type == LightType::Rectangle || obj->light.type == LightType::Area) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Shadows (Wicked Engine Technique)");
                    bool castShadow = obj->light.IsCastingShadow();
                    if (ImGui::Checkbox("Cast Shadows", &castShadow)) {
                        obj->light.SetCastShadow(castShadow);
                    }
                    if (castShadow) {
                        ImGui::SliderFloat("Shadow Strength", &obj->light.shadowStrength, 0.0f, 1.0f);
                        ImGui::DragFloat("Shadow Bias", &obj->light.shadowBias, 0.0001f, 0.000001f, 1.0f, "%.6f");

                        const char* resOptions[] = { "Auto (-1)", "256", "512", "1024", "2048", "4096" };
                        int resVals[] = { -1, 256, 512, 1024, 2048, 4096 };
                        int curResIdx = 0;
                        for (int r = 0; r < 6; ++r) {
                            if (obj->light.forced_shadow_resolution == resVals[r]) { curResIdx = r; break; }
                        }
                        if (ImGui::Combo("Forced Resolution", &curResIdx, resOptions, 6)) {
                            obj->light.forced_shadow_resolution = resVals[curResIdx];
                            if (obj->light.forced_shadow_resolution > 0) {
                                obj->light.shadowResolution = obj->light.forced_shadow_resolution;
                            }
                        }

                        ImGui::DragFloat("Shadow Distance", &obj->light.shadowDistance, 1.0f, 1.0f, 1000000.0f, "%.1f m");
                        if (obj->light.type == LightType::Directional) {
                            ImGui::Checkbox("Show Light Frustum", &scene.showLightFrustum);
                            if (obj->light.cascade_distances.size() >= 3) {
                                ImGui::DragFloat("Cascade 1 (Near)", &obj->light.cascade_distances[0], 0.5f, 1.0f, 100.0f, "%.1f m");
                                ImGui::DragFloat("Cascade 2 (Mid)", &obj->light.cascade_distances[1], 1.0f, 10.0f, 500.0f, "%.1f m");
                                ImGui::DragFloat("Cascade 3 (Far)", &obj->light.cascade_distances[2], 5.0f, 50.0f, 5000.0f, "%.1f m");
                            }
                        }
                    }
                }

                ImGui::Separator();
                if (!scene.enableFog) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.70f, 0.25f, 1.0f));
                    ImGui::Text("Volumetric Fog & God Rays [Level Fog Disabled]");
                    ImGui::PopStyleColor();
                    ImGui::TextDisabled("Volumetric fog is disabled for this level.");
                    ImGui::TextDisabled("Turn on Volumetric Fog to activate God Rays.");
                    if (ImGui::Button("Enable Volumetric Fog in Level")) {
                        scene.enableFog = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Enables Volumetric Height Fog for the level (same as World -> Volumetric Fog).");
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Volumetric Fog & God Rays (Wicked Engine Technique)");
                    bool volEnabled = obj->light.IsVolumetricsEnabled();
                    if (ImGui::Checkbox("Enable Volumetric God Rays", &volEnabled)) {
                        obj->light.SetVolumetricsEnabled(volEnabled);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Enables raymarched volumetric scattering and god rays through level fog.");
                    }
                    if (volEnabled) {
                        ImGui::SliderFloat("Volumetric Fog Boost", &obj->light.volumetric_boost, 0.0f, 20.0f, "%.2f");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Adjusts fog penetration and brightness of rays just for this light.");
                        }
                        ImGui::SliderFloat("Scattering Anisotropy (Phase G)", &obj->light.volumetricScattering, 0.0f, 0.95f, "%.2f");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Mie Henyey-Greenstein phase g factor (0.66 = Wicked Engine default forward-scattering).");
                        }
                        ImGui::DragFloat("God Rays Intensity", &obj->light.volumetricIntensity, 0.05f, 0.0f, 20.0f, "%.2f");
                        
                        // Quality sample steps
                        const char* qualityLabels[] = { "Low (8 steps)", "Balanced (16 steps)", "High (24 steps)", "Cinematic (32 steps)" };
                        int qualityValues[] = { 8, 16, 24, 32 };
                        int currentQualityIdx = 1; // default 16
                        for (int q = 0; q < 4; ++q) {
                            if (obj->light.volumetricSteps == qualityValues[q]) { currentQualityIdx = q; break; }
                        }
                        if (ImGui::Combo("Sample Quality", &currentQualityIdx, qualityLabels, 4)) {
                            obj->light.volumetricSteps = qualityValues[currentQualityIdx];
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Number of raymarching samples along the view ray. 16 steps with dither provides excellent performance and quality.");
                        }

                        ImGui::DragFloat("Max Ray Distance", &obj->light.volumetricMaxDistance, 1.0f, 10.0f, 1000.0f, "%.1f m");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Maximum distance along the view ray to calculate god ray volumetric inscattering.");
                        }

                        ImGui::TextDisabled("Status: Active with Level Fog (Density: %.4f, Height: %.0f-%.0fm)",
                            scene.fogDensity, scene.fogHeightStart, scene.fogHeightEnd);
                    }
                }

                ImGui::Separator();
                ImGui::TextDisabled("Optimization & Channels");
                int layer = (int)obj->light.lightLayer;
                if (ImGui::InputInt("Light Layer / Channel", &layer)) {
                    obj->light.lightLayer = (uint32_t)std::max(0, layer);
                }

                ImGui::Separator();
                ImGui::TextDisabled("Performance");
                ImGui::Checkbox("Enable Distance Culling", &obj->light.enableDistanceCulling);
                if (obj->light.enableDistanceCulling) {
                    ImGui::DragFloat("Min Visible Distance", &obj->light.minVisibleDistance, 1.0f, 0.0f, 10000.0f, "%.1f m");
                    if (obj->light.maxVisibleDistance < obj->light.minVisibleDistance) {
                        obj->light.maxVisibleDistance = obj->light.minVisibleDistance;
                    }
                    ImGui::DragFloat("Max View Distance", &obj->light.maxVisibleDistance, 1.0f, obj->light.minVisibleDistance, 10000.0f, "%.1f m");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Smoothly fades light to 0 between min and max visible distance.\nBeyond max visible distance, the light is completely culled and not rendered.");
                    }
                }
            }

            if (obj->lightId >= 0 && obj->lightId < (int)scene.pointLights.size()) {
                auto& pl = scene.pointLights[obj->lightId];
                pl.type = obj->light.type;
                pl.color = obj->light.color;
                pl.intensity = obj->light.intensity;
                pl.range = obj->light.range;
                pl.attenuation = obj->light.attenuation;
                pl.rangeSmoothing = obj->light.rangeSmoothing;
                pl.enabled = obj->light.enabled;
                pl.castShadows = obj->light.castShadows;
                pl.shadowStrength = obj->light.shadowStrength;
                pl.shadowBias = obj->light.shadowBias;
                pl.shadowResolution = obj->light.shadowResolution;
                pl.shadowDistance = obj->light.shadowDistance;
                pl.enableDistanceCulling = obj->light.enableDistanceCulling;
                pl.minVisibleDistance = obj->light.minVisibleDistance;
                pl.maxVisibleDistance = obj->light.maxVisibleDistance;
                pl.innerConeAngle = obj->light.innerConeAngle;
                pl.outerConeAngle = obj->light.outerConeAngle;
                pl.coneFalloff = obj->light.coneFalloff;
                pl.areaShape = obj->light.areaShape;
                pl.width = obj->light.width;
                pl.height = obj->light.height;
                pl.radius = obj->light.radius;
                pl.length = obj->light.length;
                pl.twoSided = obj->light.twoSided;
                pl.volumetric = obj->light.IsVolumetricsEnabled();
                pl.volumetric_boost = obj->light.volumetric_boost;
                pl.volumetricScattering = obj->light.volumetricScattering;
                pl.volumetricIntensity = obj->light.volumetricIntensity;
                pl.volumetricSteps = obj->light.volumetricSteps;
                pl.volumetricMaxDistance = obj->light.volumetricMaxDistance;
                pl.forced_shadow_resolution = obj->light.forced_shadow_resolution;
            }
            if (obj->light.type == LightType::Directional) {
                scene.lightColor = obj->light.color;
                scene.lightIntensity = obj->light.intensity;
                scene.enableShadows = obj->light.castShadows;
                scene.shadowStrength = obj->light.shadowStrength;
                scene.shadowBias = obj->light.shadowBias;
                scene.shadowResolution = obj->light.shadowResolution;
                scene.shadowDistance = obj->light.shadowDistance;
                scene.directionalVolumetric = obj->light.IsVolumetricsEnabled();
                scene.directionalVolumetricBoost = obj->light.volumetric_boost;
                scene.directionalVolumetricScattering = obj->light.volumetricScattering;
                scene.directionalVolumetricIntensity = obj->light.volumetricIntensity;
                scene.directionalVolumetricSteps = obj->light.volumetricSteps;
                scene.directionalVolumetricMaxDistance = obj->light.volumetricMaxDistance;
            } else if (obj->light.type == LightType::Sky) {
                scene.ambientColor = obj->light.color;
                scene.ambientIntensity = obj->light.intensity;
            }
        }

        // Camera Component Category
        if (obj->isCamera || obj->type == PrimitiveType::Camera) {
            if (ImGui::CollapsingHeader("Camera Component", ImGuiTreeNodeFlags_DefaultOpen)) {
                // Projection Mode
                const char* projModes[] = { "Perspective", "Orthographic" };
                int currentMode = obj->camera.isOrthographic ? 1 : 0;
                if (ImGui::Combo("Projection", &currentMode, projModes, 2)) {
                    obj->camera.isOrthographic = (currentMode == 1);
                }

                if (!obj->camera.isOrthographic) {
                    ImGui::SliderFloat("Field of View (FOV)", &obj->camera.fov, 10.0f, 150.0f, "%.1f deg");
                } else {
                    ImGui::DragFloat("Orthographic Size", &obj->camera.orthoSize, 0.1f, 0.1f, 100.0f, "%.2f");
                }

                ImGui::Separator();
                ImGui::TextDisabled("Clipping Planes");
                ImGui::DragFloat("Near Plane", &obj->camera.nearPlane, 0.01f, 0.001f, 50.0f, "%.3f m");
                ImGui::DragFloat("Far Plane", &obj->camera.farPlane, 1.0f, 1.0f, 50000.0f, "%.1f m");
                if (obj->camera.nearPlane < 0.001f) obj->camera.nearPlane = 0.001f;
                if (obj->camera.farPlane <= obj->camera.nearPlane) obj->camera.farPlane = obj->camera.nearPlane + 1.0f;

                ImGui::Separator();
                ImGui::TextDisabled("Aspect Ratio");
                const char* aspectPresets[] = { "16:9 (1.778)", "16:10 (1.600)", "4:3 (1.333)", "21:9 (2.333)", "1:1 (1.000)", "Custom" };
                int curAspectPreset = 5;
                if (std::abs(obj->camera.aspectRatio - 16.0f/9.0f) < 0.01f) curAspectPreset = 0;
                else if (std::abs(obj->camera.aspectRatio - 16.0f/10.0f) < 0.01f) curAspectPreset = 1;
                else if (std::abs(obj->camera.aspectRatio - 4.0f/3.0f) < 0.01f) curAspectPreset = 2;
                else if (std::abs(obj->camera.aspectRatio - 21.0f/9.0f) < 0.01f) curAspectPreset = 3;
                else if (std::abs(obj->camera.aspectRatio - 1.0f) < 0.01f) curAspectPreset = 4;

                if (ImGui::Combo("Preset##CamAspect", &curAspectPreset, aspectPresets, 6)) {
                    if (curAspectPreset == 0) obj->camera.aspectRatio = 16.0f / 9.0f;
                    else if (curAspectPreset == 1) obj->camera.aspectRatio = 16.0f / 10.0f;
                    else if (curAspectPreset == 2) obj->camera.aspectRatio = 4.0f / 3.0f;
                    else if (curAspectPreset == 3) obj->camera.aspectRatio = 21.0f / 9.0f;
                    else if (curAspectPreset == 4) obj->camera.aspectRatio = 1.0f;
                }
                ImGui::DragFloat("Ratio Value##CamRatio", &obj->camera.aspectRatio, 0.01f, 0.2f, 5.0f, "%.3f");

                ImGui::Separator();
                ImGui::TextDisabled("Level Camera");
                bool isLevelCam = (scene.activeLevelCameraId == obj->id);
                if (isLevelCam) {
                    ImGui::TextColored(ImVec4(0.2f, 0.95f, 0.4f, 1.0f), "[ACTIVE LEVEL CAMERA]");
                    if (ImGui::Button("Clear as Level Camera", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                        scene.activeLevelCameraId = -1;
                        AddLog("LogCamera", "Cleared active level camera", 0);
                    }
                } else {
                    ImGui::TextDisabled("Status: Not set as level camera");
                    if (ImGui::Button("Set as Active Level Camera", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                        scene.activeLevelCameraId = obj->id;
                        AddLog("LogCamera", "Set " + obj->name + " as Active Level Camera", 0);
                    }
                }

                ImGui::Separator();
                ImGui::TextDisabled("Camera Pilot & Alignment");
                if (currentCamera) {
                    if (ImGui::Button("Align View to Camera", ImVec2(ImGui::GetContentRegionAvail().x * 0.49f, 26.0f))) {
                        currentCamera->yaw = obj->rotation.y;
                        currentCamera->pitch = -obj->rotation.x;
                        currentCamera->fov = obj->camera.fov;
                        currentCamera->distance = 1.0f;
                        currentCamera->target = scene.GetWorldPosition(*obj) + currentCamera->GetForward() * 1.0f;
                        AddLog("LogCamera", "Aligned Viewport to " + obj->name, 0);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Align Camera to View", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                        obj->position = currentCamera->GetPosition();
                        obj->rotation.y = currentCamera->yaw;
                        obj->rotation.x = -currentCamera->pitch;
                        obj->camera.fov = currentCamera->fov;
                        AddLog("LogCamera", "Aligned " + obj->name + " to current viewport camera", 0);
                    }
                }
            }
        }

        // =========================================================
        // WATER BODY COMPONENT (shown when a Water Body is selected)
        // =========================================================
        if (obj->isWaterBody || obj->type == PrimitiveType::WaterBody) {
            WaterBodyComponent& w = obj->water;

            // Water type selector
            if (ImGui::CollapsingHeader("Water Body", ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* waterTypeNames[] = {
                    "Ocean", "River", "Sea", "Lake", "Stream",
                    "Waterfall", "Pond", "Pool", "Wet Surface",
                    "Puddle", "Flood Water", "Underwater", "Swamp", "Custom"
                };
                int wt = (int)w.type;
                if (ImGui::Combo("Water Type", &wt, waterTypeNames, 14)) {
                    // Re-apply preset for new type (Ãƒâ€šÃ‚Â§30 type switching)
                    Scene::ApplyWaterBodyPreset(w, (WaterBodyType)wt);
                    // Rebuild mesh with new preset dimensions
                    obj->mesh = GeometryBuilder::CreatePlane(w.width, w.length, w.gridResolution, w.gridResolution);
                    obj->position.y = w.elevation;
                    AddLog("LogActor", std::string("Switched water type to: ") + waterTypeNames[wt], 2);
                }
                ImGui::Separator();

                // Geometry
                ImGui::TextDisabled("Geometry");
                bool geomChanged = false;
                geomChanged |= ImGui::DragFloat("Width##WaterW",  &w.width,  0.5f, 0.1f, 10000.0f, "%.1f m");
                geomChanged |= ImGui::DragFloat("Length##WaterL", &w.length, 0.5f, 0.1f, 10000.0f, "%.1f m");
                geomChanged |= ImGui::DragFloat("Elevation##WaterY", &w.elevation, 0.05f, -500.0f, 500.0f, "%.2f m");
                geomChanged |= ImGui::SliderInt("Grid Resolution##WaterGrid", &w.gridResolution, 4, 256);
                if (geomChanged) {
                    // Rebuild plane mesh with new dimensions
                    obj->mesh = GeometryBuilder::CreatePlane(w.width, w.length, w.gridResolution, w.gridResolution);
                    obj->position.y = w.elevation;
                }
            }

            // Ocean Waves (dev.md Section 29)
            if (ImGui::CollapsingHeader("Ocean Waves", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("Wave System: 8 Multi-Scale Gerstner Waves");
                ImGui::SliderFloat("Wave Strength (Height)##WaterH", &w.waveHeight, 0.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Wave Scale (Wavelength)##WaterScale", &w.waveScale, 0.05f, 5.0f, "%.2f");
                ImGui::SliderFloat("Wave Speed##WaterSpd", &w.waveSpeed, 0.0f, 5.0f, "%.2f");
                ImGui::SliderFloat("Choppiness (Steepness)##WaterChop", &w.choppiness, 0.0f, 1.5f, "%.2f");
                ImGui::SliderFloat("Normal Detail Scale##WaterNorm", &w.normalScale, 0.0f, 2.0f, "%.2f");
            }

            // Surface Appearance (dev.md Section 29)
            if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat("Water Level (Elevation)##WaterLvl", &w.elevation, 0.05f, -500.0f, 500.0f, "%.2f m");
                ImGui::ColorEdit4("Deep Water Color",    &w.waterColor.r);
                ImGui::ColorEdit4("Shallow Water Color", &w.shallowColor.r);
                ImGui::SliderFloat("Roughness##WaterRough",  &w.roughness,  0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Specular Intensity##WaterSpec", &w.specularIntensity,  0.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Specular Power##WaterSpecPow",  &w.specularPower, 8.0f, 512.0f, "%.0f");
                ImGui::SliderFloat("Sun Sparkle Power##WaterSpkPow",&w.sparklePower, 64.0f, 2048.0f, "%.0f");
                ImGui::SliderFloat("Fresnel Strength##WaterFres",   &w.fresnelStrength, 0.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Opacity##WaterOpac",     &w.opacity,    0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Depth Range (Fade)##WaterDepthFade", &w.depthRange, 0.1f, 50.0f, "%.1f m");
                ImGui::SliderFloat("Reflection Strength##WaterRefl", &w.reflectionStrength, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Refraction Strength##WaterRefr", &w.refractionStrength, 0.0f, 2.0f, "%.2f");
            }

            // GPU Height-Field Simulation (AquaTerrain DX12, dev.md Section 29)
            if (ImGui::CollapsingHeader("Simulation (Local Ripples)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Enable Simulation##WaterSim", &w.simulationEnabled);
                ImGui::TextDisabled("Simulation Resolution: 256 x 256 (Compute)");
                ImGui::SliderFloat("Ripple Strength (Height Scale)##WaterRipStr", &w.heightScale, 0.0f, 0.5f, "%.3f");
                ImGui::SliderFloat("Ripple Radius / Disturbance##WaterDistStr", &w.disturbanceStrength, 0.05f, 5.0f, "%.2f");
                ImGui::SliderFloat("Wave Damping##WaterDamp", &w.waveDamping, 0.900f, 0.999f, "%.3f");

                if (ImGui::Button("Add Impulse at Center", ImVec2(-1, 0))) {
                    Eunoia::WaterSystem::Get().AddImpulseUV(0.5f, 0.5f, 0.08f, w.disturbanceStrength);
                }
                if (ImGui::Button("Add Random Ripple", ImVec2(-1, 0))) {
                    float ru = 0.25f + (float)(rand() % 50) / 100.0f;
                    float rv = 0.25f + (float)(rand() % 50) / 100.0f;
                    Eunoia::WaterSystem::Get().AddImpulseUV(ru, rv, 0.06f, w.disturbanceStrength * 0.8f);
                }
            }

            // Foam (dev.md Section 29)
            if (ImGui::CollapsingHeader("Foam")) {
                ImGui::Checkbox("Enable Foam##WaterFoam", &w.enableFoam);
                if (w.enableFoam) {
                    ImGui::SliderFloat("Foam Strength##WaterFoamStr",  &w.foamStrength, 0.0f, 3.0f, "%.2f");
                    ImGui::SliderFloat("Foam Distance##WaterFoamDist", &w.foamDistance, 0.1f, 10.0f, "%.1f m");
                    ImGui::SliderFloat("Foam Scale##WaterFoamScale",   &w.foamScale,   0.01f, 10.0f, "%.2f");
                    ImGui::SliderFloat("Foam Speed##WaterFoamSpd",     &w.foamSpeed,    0.0f, 5.0f, "%.2f");
                    ImGui::ColorEdit4("Foam Color",    &w.foamColor.r);
                    auto textures = ScanProjectTextures();
                    DrawTextureSlot("Foam Texture (Optional)", w.foamTexturePath, w.foamTextureAssetId, textures);
                }
            }

            // Rendering & Debug (dev.md Section 29 & 35)
            if (ImGui::CollapsingHeader("Rendering & Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Visible##WaterVis", &obj->visible);
                ImGui::Checkbox("Camera Centered Ocean##WaterCamCent", &w.isCameraCentered);
                ImGui::DragFloat("Ocean Extent##WaterExtent", &w.oceanExtent, 50.0f, 100.0f, 20000.0f, "%.0f m");
                ImGui::TextDisabled("Mesh Resolution: 256 x 256 (66k Vertices)");

                const char* debugModes[] = {
                    "0: Final Shading",
                    "1: Base Color",
                    "2: Normals",
                    "3: Height Field",
                    "4: Foam Mask",
                    "5: Depth / Thickness",
                    "6: Wireframe / Grid Topology"
                };
                ImGui::Combo("Water Debug Mode##WaterDbg", &w.debugMode, debugModes, 7);

                ImGui::Checkbox("Cast Shadows##WaterShadow", &obj->castShadows);
                ImGui::Checkbox("Receive Shadows##WaterRecv",&obj->receiveShadows);
                ImGui::SliderInt("Render Layer##WaterLyr", &w.renderLayer, 0, 7);
            }

            // Flow (River, Stream, Waterfall)
            bool isFlowing = (w.type == WaterBodyType::River || w.type == WaterBodyType::Stream || w.type == WaterBodyType::Waterfall);
            if (isFlowing && ImGui::CollapsingHeader("ÃƒÂ¢Ã…Â¾Ã‚Â¡ÃƒÂ¯Ã‚Â¸Ã‚Â Flow")) {
                ImGui::Checkbox("Enable Flow", &w.enableFlow);
                if (w.enableFlow) {
                    ImGui::DragFloat2("Flow Direction (XZ)", &w.flowDirection.x, 0.01f, -1.0f, 1.0f);
                    ImGui::SliderFloat("Flow Speed",   &w.flowSpeed,   0.0f, 5.0f,  "%.2f");
                    ImGui::SliderFloat("Flow Tiling",  &w.flowTiling,  0.01f, 10.0f, "%.2f");
                    auto textures = ScanProjectTextures();
                    DrawTextureSlot("Flow Map (Optional)", w.flowMapPath, w.flowMapAssetId, textures);
                }
            }

            // Waterfall specific
            if (w.type == WaterBodyType::Waterfall && ImGui::CollapsingHeader("Waterfall Settings")) {
                ImGui::DragFloat("Fall Height",   &w.waterfallFallHeight,  0.5f, 0.0f, 50.0f,  "%.1f m");
                ImGui::SliderFloat("Flow Rate",   &w.waterfallFlowRate,    0.0f, 10.0f, "%.2f");
                ImGui::SliderFloat("Spray Amount",&w.waterfallSprayAmount, 0.0f, 2.0f,  "%.2f");
            }

            // Underwater specific
            if (w.type == WaterBodyType::Underwater && ImGui::CollapsingHeader("Underwater Volume")) {
                ImGui::ColorEdit3("Fog Color",         &w.underwaterFogColor.r);
                ImGui::SliderFloat("Fog Density",      &w.underwaterFogDensity,        0.0f, 5.0f, "%.2f");
                ImGui::DragFloat("Visibility Distance",&w.underwaterVisibilityDistance, 1.0f, 0.0f, 200.0f, "%.1f m");
                ImGui::Checkbox("Underwater Distortion", &w.enableUnderwaterDistortion);
                if (w.enableUnderwaterDistortion) {
                    ImGui::SliderFloat("Distortion Strength##Uw", &w.underwaterDistortionStrength, 0.0f, 2.0f, "%.2f");
                    ImGui::SliderFloat("Distortion Speed##Uw",    &w.underwaterDistortionSpeed,    0.0f, 5.0f, "%.2f");
                }
            }

            // Puddle / Wet Surface specific
            bool isPuddle = (w.type == WaterBodyType::Puddle || w.type == WaterBodyType::WetSurface);
            if (isPuddle && ImGui::CollapsingHeader("Puddle / Wet Surface")) {
                ImGui::SliderFloat("Wetness",    &w.wetness,    0.0f, 1.0f,  "%.2f");
                ImGui::DragFloat("Puddle Size",  &w.puddleSize, 0.1f, 0.0f, 20.0f, "%.1f m");
                ImGui::DragFloat("Puddle Depth", &w.puddleDepth,0.01f,0.0f,  2.0f, "%.2f m");
                ImGui::Checkbox("Animate Ripples", &w.animatePuddle);
                if (w.animatePuddle) {
                    ImGui::SliderFloat("Ripple Speed", &w.rippleSpeed, 0.0f, 5.0f, "%.2f");
                    ImGui::SliderFloat("Ripple Scale", &w.rippleScale, 0.0f, 5.0f, "%.2f");
                }
            }

            // Swamp specific
            if (w.type == WaterBodyType::Swamp && ImGui::CollapsingHeader("Swamp Settings")) {
                ImGui::SliderFloat("Density (Murk)",  &w.swampDensity, 0.0f, 1.0f, "%.2f");
                ImGui::ColorEdit3("Swamp Color",      &w.swampColor.r);
                ImGui::SliderFloat("Algae Amount",    &w.algaeAmount,  0.0f, 1.0f, "%.2f");
            }
        }

        // =========================================================
        // USER INTERFACE 3D COMPONENT
        // =========================================================
        if (obj->isUserInterface || obj->type == PrimitiveType::UserInterface || !obj->ui3d.uiAssetPath.empty()) {
            UserInterface3DComponent& uiComp = obj->ui3d;

            if (ImGui::CollapsingHeader("User Interface 3D", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("UI Enabled", &uiComp.enabled);
                ImGui::Spacing();

                // 1. Project UI Asset Selector dropdown
                ImGui::Text("User Interface Asset");
                std::string currentPreview = "None (Select 3D User Interface)";
                if (!uiComp.uiAssetPath.empty()) {
                    currentPreview = std::filesystem::path(uiComp.uiAssetPath).filename().string();
                }

                if (ImGui::BeginCombo("##UI3DAssetCombo", currentPreview.c_str())) {
                    if (ImGui::Selectable("None", uiComp.uiAssetPath.empty())) {
                        UndoManager::Get().RecordSnapshot(scene, "Clear UI Asset");
                        uiComp.uiAssetPath = "";
                    }
                    ImGui::Separator();

                    // Scan project content directory for all .eui3d and .eui files
                    if (!contentRootPath.empty() && std::filesystem::exists(contentRootPath)) {
                        std::error_code rec;
                        for (auto it = std::filesystem::recursive_directory_iterator(contentRootPath, std::filesystem::directory_options::skip_permission_denied, rec);
                             it != std::filesystem::recursive_directory_iterator(); it.increment(rec)) {
                            if (rec) break;
                            if (it->is_regular_file()) {
                                std::string ext = it->path().extension().string();
                                if (ext == ".eui3d" || ext == ".eui") {
                                    std::string uiPath = it->path().string();
                                    std::string uiFilename = it->path().filename().string();
                                    bool isSel = (!uiComp.uiAssetPath.empty() && (uiComp.uiAssetPath == uiPath || std::filesystem::path(uiComp.uiAssetPath).filename() == it->path().filename()));
                                    if (ImGui::Selectable(uiFilename.c_str(), isSel)) {
                                        UndoManager::Get().RecordSnapshot(scene, "Select UI Asset: " + uiFilename);
                                        uiComp.uiAssetPath = uiPath;
                                    }
                                }
                            }
                        }
                    }
                    ImGui::EndCombo();
                }

                // Drag & drop target for UI assets
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        std::string dragItem = (const char*)payload->Data;
                        if (dragItem.rfind("ASSET:", 0) == 0) {
                            std::string srcRel = dragItem.substr(6);
                            if (srcRel.find(".eui3d") != std::string::npos || srcRel.find(".eui") != std::string::npos) {
                                std::filesystem::path fullDisk = contentRootPath / srcRel;
                                UndoManager::Get().RecordSnapshot(scene, "Drop UI Asset");
                                uiComp.uiAssetPath = fullDisk.string();
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (!uiComp.uiAssetPath.empty()) {
                    ImGui::SameLine();
                    if (ImGui::Button("Edit UI")) {
                        OpenUIEditor(uiComp.uiAssetPath);
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();

                // 2. Space dropdown (World vs Screen)
                ImGui::Text("Space");
                const char* spaceNames[] = { "World", "Screen" };
                int currentSpace = (int)uiComp.space;
                if (ImGui::Combo("##UI3DSpaceMode", &currentSpace, spaceNames, 2)) {
                    UndoManager::Get().RecordSnapshot(scene, "Change UI Space Mode");
                    uiComp.space = (EunoiaUI::UI3DSpaceType)currentSpace;
                }
                if (ImGui::IsItemHovered()) {
                    if (uiComp.space == EunoiaUI::UI3DSpaceType::World) {
                        ImGui::SetTooltip("World: UI is placed in 3D world space and rotates with actor transform.");
                    } else {
                        ImGui::SetTooltip("Screen: UI is billboarded and always targets towards camera's view.");
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();

                // 3. Size & Resolution
                ImGui::TextDisabled("3D Dimensions & Canvas");
                ImGui::DragFloat2("World Size (W x H)##UI3DSize", &uiComp.size.x, 0.05f, 0.1f, 100.0f, "%.2f m");
                ImGui::DragFloat2("Canvas Res (px)##UI3DRes", &uiComp.canvasResolution.x, 10.0f, 100.0f, 7680.0f, "%.0f px");

                ImGui::Spacing();
                ImGui::Separator();

                // 4. Appearance
                ImGui::TextDisabled("Appearance");
                ImGui::Checkbox("Two-Sided##UI3DTwoSided", &uiComp.twoSided);
                ImGui::SliderFloat("Opacity##UI3DOpacity", &uiComp.opacity, 0.0f, 1.0f, "%.2f");
                ImGui::ColorEdit4("Background Color##UI3DBG", &uiComp.backgroundColor.r, ImGuiColorEditFlags_AlphaBar);
                ImGui::ColorEdit4("Border Color##UI3DBorder", &uiComp.borderColor.r, ImGuiColorEditFlags_AlphaBar);
                ImGui::DragFloat("Border Width##UI3DBorderW", &uiComp.borderWidth, 0.1f, 0.0f, 20.0f, "%.1f px");
            }
        }

        // =========================================================
        // LEVEL ANIMATION COMPONENT
        // =========================================================
        if (obj->hasLevelAnimation || obj->type == PrimitiveType::LevelAnimation) {
            if (ImGui::CollapsingHeader("Level Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PushID("LevelAnimationDetailsSection");

                ImGui::Checkbox("Enabled##LAEnabled", &obj->levelAnimation.enabled);
                ImGui::SameLine(0.0f, 15.0f * uiScale);
                ImGui::Checkbox("Play On Start##LAPlayOnStart", &obj->levelAnimation.playOnStart);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("When checked, this level animation automatically plays on start (default: enabled).");
                }
                ImGui::SameLine(0.0f, 15.0f * uiScale);
                ImGui::Checkbox("Loop##LALoop", &obj->levelAnimation.loop);

                ImGui::Spacing();
                ImGui::DragFloat("Playback Speed##LASpeed", &obj->levelAnimation.playbackSpeed, 0.05f, -10.0f, 10.0f, "%.2fx");

                ImGui::Spacing();
                ImGui::Text("Animation Asset (.elanim)");

                std::string currentAnimPreview = "(None - Select or Drop .elanim)";
                if (!obj->levelAnimation.animationPath.empty()) {
                    currentAnimPreview = std::filesystem::path(obj->levelAnimation.animationPath).filename().string();
                }

                if (ImGui::BeginCombo("##LevelAnimAssetCombo", currentAnimPreview.c_str())) {
                    if (ImGui::Selectable("(None)", obj->levelAnimation.animationPath.empty())) {
                        obj->levelAnimation.animation.reset();
                        obj->levelAnimation.animationPath = "";
                    }
                    ImGui::Separator();

                    // Search for .elanim files in content directory
                    if (!contentRootPath.empty() && std::filesystem::exists(contentRootPath)) {
                        std::error_code ec;
                        for (auto it = std::filesystem::recursive_directory_iterator(contentRootPath, ec);
                             it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                            if (ec) break;
                            if (it->is_regular_file() && it->path().extension() == ".elanim") {
                                std::string rel = std::filesystem::relative(it->path(), contentRootPath, ec).string();
                                bool isSel = (obj->levelAnimation.animationPath == it->path().string() || obj->levelAnimation.animationPath == rel);
                                if (ImGui::Selectable(rel.c_str(), isSel)) {
                                    obj->levelAnimation.LoadAnimation(it->path().string());
                                    if (obj->levelAnimation.animation) {
                                        Eunoia::LevelAnimationEditor::Get().SetActiveAnimation(obj->levelAnimation.animation);
                                        Eunoia::LevelAnimationManager::Get().SetActiveActorId(obj->id);
                                    }
                                    AddLog("LogLevelAnimation", "Assigned Level Animation: " + rel + " to " + obj->name, 0);
                                }
                            }
                        }
                    }
                    ImGui::EndCombo();
                }

                // Drag & drop target for .elanim assets from Content Browser
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        std::string droppedPath((const char*)payload->Data);
                        if (droppedPath.find(".elanim") != std::string::npos) {
                            std::filesystem::path fullP = contentRootPath / droppedPath;
                            obj->levelAnimation.LoadAnimation(fullP.string());
                            if (obj->levelAnimation.animation) {
                                Eunoia::LevelAnimationEditor::Get().SetActiveAnimation(obj->levelAnimation.animation);
                                Eunoia::LevelAnimationManager::Get().SetActiveActorId(obj->id);
                            }
                            AddLog("LogLevelAnimation", "Dropped Level Animation: " + droppedPath + " onto " + obj->name, 0);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::Spacing();

                // Open in Animation Timeline button
                if (ImGui::Button("Open in Animation Timeline", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f * uiScale))) {
                    if (!obj->levelAnimation.animationPath.empty()) {
                        OpenLevelAnimationEditor(obj->levelAnimation.animationPath);
                    } else {
                        bottomDrawerTab = 1;
                        bottomDrawerOpen = true;
                    }
                    if (obj->levelAnimation.animation) {
                        Eunoia::LevelAnimationEditor::Get().SetActiveAnimation(obj->levelAnimation.animation);
                    }
                    Eunoia::LevelAnimationManager::Get().SetActiveActorId(obj->id);
                }

                // In-editor preview controls
                if (obj->levelAnimation.animation) {
                    ImGui::Spacing();
                    float endT = obj->levelAnimation.animation->duration;
                    ImGui::Text("Time: %.2fs / %.2fs", obj->levelAnimation.currentTime, endT);

                    float btnW = (ImGui::GetContentRegionAvail().x - 16.0f) / 3.0f;
                    if (ImGui::Button(obj->levelAnimation.isPlaying ? "Pause##LAPreview" : "Play##LAPreview", ImVec2(btnW, 22.0f * uiScale))) {
                        if (obj->levelAnimation.isPlaying) {
                            obj->levelAnimation.Pause();
                        } else {
                            obj->levelAnimation.Play();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Stop##LAPreview", ImVec2(btnW, 22.0f * uiScale))) {
                        obj->levelAnimation.Stop();
                        obj->levelAnimation.Update(0.0f, obj, &scene);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Reset##LAPreview", ImVec2(btnW, 22.0f * uiScale))) {
                        obj->levelAnimation.currentTime = 0.0f;
                        obj->levelAnimation.Update(0.0f, obj, &scene);
                    }
                }

                ImGui::PopID();
            }
        }

        // Primitive Shape Settings Category (dev.md)
        if (!obj->isLight && !IsLightPrimitive(obj->type) && !obj->isCamera && !IsCameraPrimitive(obj->type) && !obj->isWaterBody && obj->type != PrimitiveType::WaterBody && !obj->isTerrain && obj->type != PrimitiveType::Terrain && !obj->isUserInterface && obj->type != PrimitiveType::UserInterface && obj->type != PrimitiveType::Empty && obj->type != PrimitiveType::ImportedMesh && obj->type != PrimitiveType::LevelAnimation) {
            char shapeHeader[128];
            snprintf(shapeHeader, sizeof(shapeHeader), "%s Parameters", GetPrimitiveTypeName(obj->type));
            if (ImGui::CollapsingHeader(shapeHeader, ImGuiTreeNodeFlags_DefaultOpen)) {
                bool paramChanged = false;

                if (obj->type == PrimitiveType::Cube) {
                    paramChanged |= ImGui::DragFloat("Size", &obj->params.size, 0.05f, 0.05f, 20.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Subdivisions", &obj->params.segmentsX, 1, 64);
                }
                else if (obj->type == PrimitiveType::Plane) {
                    paramChanged |= ImGui::DragFloat("Width", &obj->params.width, 0.1f, 0.1f, 100.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Depth", &obj->params.depth, 0.1f, 0.1f, 100.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Segments X", &obj->params.segmentsX, 1, 64);
                    paramChanged |= ImGui::SliderInt("Segments Z", &obj->params.segmentsZ, 1, 64);
                }
                else if (obj->type == PrimitiveType::Box) {
                    paramChanged |= ImGui::DragFloat("Width", &obj->params.width, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Height", &obj->params.height, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Depth", &obj->params.depth, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Segments X", &obj->params.segmentsX, 1, 32);
                    paramChanged |= ImGui::SliderInt("Segments Y", &obj->params.segmentsY, 1, 32);
                    paramChanged |= ImGui::SliderInt("Segments Z", &obj->params.segmentsZ, 1, 32);
                }
                else if (obj->type == PrimitiveType::Sphere || obj->type == PrimitiveType::UVSphere) {
                    paramChanged |= ImGui::DragFloat("Radius", &obj->params.radius, 0.05f, 0.05f, 25.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Rings / Lat", &obj->params.rings, 3, 64);
                    paramChanged |= ImGui::SliderInt("Sectors / Long", &obj->params.sectors, 3, 64);
                }
                else if (obj->type == PrimitiveType::Icosphere) {
                    paramChanged |= ImGui::DragFloat("Radius", &obj->params.radius, 0.05f, 0.05f, 25.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Subdivisions", &obj->params.subdivisions, 0, 5);
                }
                else if (obj->type == PrimitiveType::Cylinder) {
                    paramChanged |= ImGui::DragFloat("Radius", &obj->params.radius, 0.05f, 0.05f, 25.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Height", &obj->params.height, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Radial Segments", &obj->params.radialSegments, 3, 64);
                    paramChanged |= ImGui::SliderInt("Height Segments", &obj->params.heightSegments, 1, 32);
                    paramChanged |= ImGui::Checkbox("Cap Top", &obj->params.capTop);
                    paramChanged |= ImGui::Checkbox("Cap Bottom", &obj->params.capBottom);
                }
                else if (obj->type == PrimitiveType::Cone) {
                    paramChanged |= ImGui::DragFloat("Bottom Radius", &obj->params.radius, 0.05f, 0.0f, 25.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Top Radius", &obj->params.radius2, 0.05f, 0.0f, 25.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Height", &obj->params.height, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Radial Segments", &obj->params.radialSegments, 3, 64);
                    paramChanged |= ImGui::SliderInt("Height Segments", &obj->params.heightSegments, 1, 32);
                    paramChanged |= ImGui::Checkbox("Cap Bottom", &obj->params.capBottom);
                    paramChanged |= ImGui::Checkbox("Cap Top", &obj->params.capTop);
                }
                else if (obj->type == PrimitiveType::Capsule) {
                    paramChanged |= ImGui::DragFloat("Radius", &obj->params.radius, 0.05f, 0.05f, 20.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Height", &obj->params.height, 0.05f, 0.05f, 40.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Radial Segments", &obj->params.radialSegments, 3, 48);
                    paramChanged |= ImGui::SliderInt("Height Segments", &obj->params.heightSegments, 1, 24);
                }
                else if (obj->type == PrimitiveType::Torus) {
                    paramChanged |= ImGui::DragFloat("Major Radius", &obj->params.radius, 0.05f, 0.1f, 25.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Minor Radius", &obj->params.radius2, 0.02f, 0.02f, 10.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Major Segments", &obj->params.radialSegments, 3, 64);
                    paramChanged |= ImGui::SliderInt("Minor Segments", &obj->params.segmentsY, 3, 48);
                }
                else if (obj->type == PrimitiveType::Circle || obj->type == PrimitiveType::Disc) {
                    paramChanged |= ImGui::DragFloat("Radius", &obj->params.radius, 0.05f, 0.05f, 25.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Segments", &obj->params.radialSegments, 3, 64);
                }
                else if (obj->type == PrimitiveType::Quad) {
                    paramChanged |= ImGui::DragFloat("Width", &obj->params.width, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Height", &obj->params.height, 0.05f, 0.05f, 50.0f, "%.2f");
                }
                else if (obj->type == PrimitiveType::Triangle) {
                    paramChanged |= ImGui::DragFloat("Base Width", &obj->params.width, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Height", &obj->params.height, 0.05f, 0.05f, 50.0f, "%.2f");
                }
                else if (obj->type == PrimitiveType::Pyramid) {
                    paramChanged |= ImGui::DragFloat("Base Width", &obj->params.width, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Base Depth", &obj->params.depth, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Height", &obj->params.height, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Sides", &obj->params.sides, 3, 32);
                }
                else if (obj->type == PrimitiveType::Prism) {
                    paramChanged |= ImGui::DragFloat("Radius", &obj->params.radius, 0.05f, 0.05f, 25.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Height", &obj->params.height, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Sides", &obj->params.sides, 3, 32);
                }

                if (paramChanged) {
                    obj->RebuildMesh();
                }
            }
        }

        // Terrain Dimensions & Grid Parameters (when Properties tab is active)
        if ((obj->isTerrain || obj->type == PrimitiveType::Terrain) && ImGui::CollapsingHeader("Terrain Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
            static Eunoia::TerrainComponent s_preResizeTerrain;
            static Scene s_preResizeSceneSnapshot;
            static bool s_isResizingTerrain = false;
            static int s_terrainAnchorMode = 0; // 0: Center, 1: Min Corner (-X, -Z), 2: Max Corner (+X, +Z)

            bool gridChanged = false;
            float w = obj->terrain.width;
            float l = obj->terrain.length;
            int res = obj->terrain.resolution;

            const char* anchorNames[] = { "Center (Preserve World Position)", "Min Corner (-X, -Z)", "Max Corner (+X, +Z)" };
            ImGui::Combo("Extend Anchor##TerrAnchor", &s_terrainAnchorMode, anchorNames, 3);

            if (ImGui::DragFloat("Width (X)##TerrW", &w, 1.0f, 4.0f, 4096.0f, "%.1f m")) {
                if (w < 4.0f) w = 4.0f;
                gridChanged = true;
            }
            bool wDeactivated = ImGui::IsItemDeactivatedAfterEdit();

            if (ImGui::DragFloat("Length (Z)##TerrL", &l, 1.0f, 4.0f, 4096.0f, "%.1f m")) {
                if (l < 4.0f) l = 4.0f;
                gridChanged = true;
            }
            bool lDeactivated = ImGui::IsItemDeactivatedAfterEdit();

            if (ImGui::SliderInt("Resolution (Grid)##TerrRes", &res, 8, 512)) {
                gridChanged = true;
            }
            bool resDeactivated = ImGui::IsItemDeactivatedAfterEdit();

            if (gridChanged) {
                if (!s_isResizingTerrain) {
                    s_preResizeTerrain = obj->terrain;
                    s_preResizeSceneSnapshot = scene;
                    s_isResizingTerrain = true;
                }
                obj->terrain = s_preResizeTerrain;
                obj->terrain.ResizeGrid(w, l, res, s_terrainAnchorMode);
                obj->terrain.RebuildTerrainMesh(obj->mesh);
                RequestViewportRedraw(5);
            }

            if (wDeactivated || lDeactivated || resDeactivated) {
                if (s_isResizingTerrain) {
                    UndoManager::Get().RecordSnapshot(s_preResizeSceneSnapshot, "Resize/Extend Terrain: " + obj->name);
                    s_isResizingTerrain = false;
                    AddLog("LogTerrain", "Resized terrain to " + std::to_string((int)obj->terrain.width) + "m x " + std::to_string((int)obj->terrain.length) + "m (sculpt & painted materials preserved)", 0);
                }
            }

            float quadSpacing = obj->terrain.width / (float)std::max(1, obj->terrain.resolution);
            ImGui::TextDisabled("Grid Vertices: %d x %d (%zu verts, %zu triangles, quad spacing: %.2fm)",
                obj->terrain.resolution + 1, obj->terrain.resolution + 1,
                obj->mesh.vertices.size(), obj->mesh.indices.size() / 3, quadSpacing);
            ImGui::TextDisabled("Extending Width or Length preserves all sculpted heights, tilts, and painted materials.");
        }

        // Empty Actor Information
        if (obj->type == PrimitiveType::Empty) {
            ImGui::Spacing();
            ImGui::TextDisabled("Empty Actor (Transform Pivot / Group Node)");
            ImGui::TextDisabled("Has no visual mesh. Children inherit transform.");
        }

        // 2. Static Mesh Category (only for mesh actors)
        if (!obj->isLight && !obj->isCamera && obj->type != PrimitiveType::Camera && !obj->isUserInterface && obj->type != PrimitiveType::UserInterface && obj->type != PrimitiveType::Empty && obj->type != PrimitiveType::LevelAnimation && ImGui::CollapsingHeader("Static Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
            char meshAsset[64];
            snprintf(meshAsset, sizeof(meshAsset), "SM_%s", GetPrimitiveTypeName(obj->type));
            ImGui::TextDisabled("Static Mesh Asset:");
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "%s", meshAsset);
            ImGui::TextDisabled("Vertices: %d | Triangles: %d", (int)obj->mesh.vertices.size(), (int)obj->mesh.indices.size() / 3);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Shading (Blender Workflow):");

            float btnW = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
            if (ImGui::Button("Shade Smooth", ImVec2(btnW, 24.0f * uiScale))) {
                UndoManager::Get().RecordSnapshot(scene, "Shade Smooth: " + obj->name);
                obj->SetSmoothShading(true, obj->autoSmoothAngle);
                for (int cid : obj->childIds) {
                    if (GameObject* child = scene.FindObject(cid)) {
                        child->SetSmoothShading(true, obj->autoSmoothAngle);
                    }
                }
                AddLog("LogActor", "Applied Shade Smooth to " + obj->name, 2);
                RequestViewportRedraw(2);
            }
            ImGui::SameLine();
            if (ImGui::Button("Shade Flat", ImVec2(btnW, 24.0f * uiScale))) {
                UndoManager::Get().RecordSnapshot(scene, "Shade Flat: " + obj->name);
                obj->SetSmoothShading(false);
                for (int cid : obj->childIds) {
                    if (GameObject* child = scene.FindObject(cid)) {
                        child->SetSmoothShading(false);
                    }
                }
                AddLog("LogActor", "Applied Shade Flat to " + obj->name, 2);
                RequestViewportRedraw(2);
            }

            bool smShade = obj->smoothShading;
            if (ImGui::Checkbox("Smooth Shading", &smShade)) {
                UndoManager::Get().RecordSnapshot(scene, "Toggle Smooth Shading: " + obj->name);
                obj->SetSmoothShading(smShade, obj->autoSmoothAngle);
                for (int cid : obj->childIds) {
                    if (GameObject* child = scene.FindObject(cid)) {
                        child->SetSmoothShading(smShade, obj->autoSmoothAngle);
                    }
                }
                RequestViewportRedraw(2);
            }

            if (obj->smoothShading) {
                float angle = obj->autoSmoothAngle;
                if (ImGui::SliderFloat("Auto Smooth Angle", &angle, 0.0f, 180.0f, "%.0f deg")) {
                    obj->autoSmoothAngle = angle;
                    obj->SetSmoothShading(true, angle);
                    for (int cid : obj->childIds) {
                        if (GameObject* child = scene.FindObject(cid)) {
                            child->SetSmoothShading(true, angle);
                        }
                    }
                    RequestViewportRedraw(2);
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    UndoManager::Get().RecordSnapshot(scene, "Change Auto Smooth Angle: " + obj->name);
                }
            }
        }

        // 3. Materials Category (Element 0) (mesh actors or parent actors with children)
        if (!obj->isLight && !obj->isCamera && obj->type != PrimitiveType::Camera && !obj->isUserInterface && obj->type != PrimitiveType::UserInterface && obj->type != PrimitiveType::LevelAnimation && (obj->type != PrimitiveType::Empty || !obj->childIds.empty()) && ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Element 0: Material Slot");
            if (!obj->childIds.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 0.9f), "(ÃƒÂ¢Ã¢â‚¬Â Ã‚Â³ Cascades to all %zu children)", obj->childIds.size());
            }

            // Scan for all materials in contentRootPath (recursively across the entire project root)
            RefreshCachedMaterials(contentRootPath);
            const auto& matList = s_cachedMatList;
            const auto& folderGroups = s_cachedFolderGroups;

            // Construct combo preview label
            std::string comboPreview = "" + (obj->materialName.empty() ? std::string("Default_Material") : obj->materialName);
            for (const auto& m : matList) {
                if (m.name == obj->materialName || m.relPath == obj->materialName) {
                    if (!m.relFolder.empty()) {
                        comboPreview += "  [" + m.relFolder + "]";
                    } else {
                        comboPreview += "  [Root]";
                    }
                    break;
                }
            }

            // Recurrent Material Dropdown Combo
            static char matFilter[64] = "";
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 36.0f);
            if (ImGui::BeginCombo("##MaterialSlotDropdown", comboPreview.c_str(), ImGuiComboFlags_HeightLarge)) {
                // Search filter inside dropdown
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##matFilter", "Search materials in project...", matFilter, sizeof(matFilter));
                ImGui::Separator();

                std::string searchFilter = matFilter;
                std::transform(searchFilter.begin(), searchFilter.end(), searchFilter.begin(), ::tolower);

                if (!searchFilter.empty()) {
                    // Filtered view across all materials
                    int matchCount = 0;
                    for (const auto& m : matList) {
                        std::string nameLower = m.name;
                        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                        std::string relLower = m.relPath;
                        std::transform(relLower.begin(), relLower.end(), relLower.begin(), ::tolower);

                        if (nameLower.find(searchFilter) != std::string::npos || relLower.find(searchFilter) != std::string::npos) {
                            matchCount++;
                            ImGui::PushID(m.fullPath.c_str());
                            bool isSelected = (obj->materialName == m.name || obj->materialName == m.relPath);

                            ImGui::ColorButton("##swatch", ImVec4(m.color.r, m.color.g, m.color.b, 1.0f),
                                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(16, 16));
                            ImGui::SameLine();

                            std::string itemLabel = m.name;
                            if (!m.relFolder.empty()) {
                                itemLabel += "  (" + m.relFolder + ")";
                            } else {
                                itemLabel += "  (Root)";
                            }

                            if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                                MaterialAsset ma;
                                if (LoadMaterialFile(m.fullPath, ma)) {
                                    ApplyMaterialToActorAndChildren(scene, obj, ma);
                                }
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("File: %s\nPath: %s\nRoughness: %.2f | Metallic: %.2f",
                                    m.name.c_str(), m.relPath.c_str(), m.roughness, m.metallic);
                            }
                            ImGui::PopID();
                        }
                    }
                    if (matchCount == 0) {
                        ImGui::TextDisabled("No materials match \"%s\"", matFilter);
                    }
                } else {
                    // Recurrent / Hierarchical Folder Grouping View
                    for (const auto& [folder, items] : folderGroups) {
                        std::string folderTitle;
                        if (folder.empty()) {
                            folderTitle = "[Project Root] (" + std::to_string(items.size()) + ")";
                        } else {
                            folderTitle = folder + " (" + std::to_string(items.size()) + ")";
                        }

                        bool groupOpen = ImGui::TreeNodeEx(folderTitle.c_str(),
                            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);

                        if (groupOpen) {
                            for (const auto& m : items) {
                                ImGui::PushID(m.fullPath.c_str());
                                bool isSelected = (obj->materialName == m.name || obj->materialName == m.relPath);

                                ImGui::ColorButton("##swatch", ImVec4(m.color.r, m.color.g, m.color.b, 1.0f),
                                    ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(16, 16));
                                ImGui::SameLine();

                                if (ImGui::Selectable(m.name.c_str(), isSelected)) {
                                    MaterialAsset ma;
                                    if (LoadMaterialFile(m.fullPath, ma)) {
                                        ApplyMaterialToActorAndChildren(scene, obj, ma);
                                    }
                                }
                                if (isSelected) ImGui::SetItemDefaultFocus();
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Material: %s\nLocation: %s\nRoughness: %.2f | Metallic: %.2f",
                                        m.name.c_str(), m.relPath.c_str(), m.roughness, m.metallic);
                                }
                                ImGui::PopID();
                            }
                            ImGui::TreePop();
                        }
                    }
                }

                ImGui::EndCombo();
            }

            // Drag & Drop Target for Material Slot (dev.md Section 34)
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                    char assetIdBuf[40] = {};
                    memcpy(assetIdBuf, payload->Data, std::min((size_t)payload->DataSize, sizeof(assetIdBuf) - 1));
                    AssetID droppedId = AssetID::FromString(assetIdBuf);
                    AssetMetadata* meta = AssetRegistry::Get().Find(droppedId);
                    if (meta && meta->type == AssetType::Material) {
                        std::filesystem::path fullPath = contentRootPath / meta->sourcePath;
                        MaterialAsset ma;
                        if (LoadMaterialFile(fullPath.string(), ma)) {
                            ApplyMaterialToActorAndChildren(scene, obj, ma);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Button to open currently selected material in Material Editor
            ImGui::SameLine();
            if (ImGui::Button("Edit##EditMatBtn")) {
                std::string targetPath = "";
                for (const auto& m : matList) {
                    if (m.name == obj->materialName || m.relPath == obj->materialName) {
                        targetPath = m.fullPath;
                        break;
                    }
                }
                if (targetPath.empty()) {
                    targetPath = (contentRootPath / "Materials" / "Default_Material.emat").string();
                }
                OpenMaterialEditor(targetPath);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Open in Material Editor");
            }

            ImGui::SameLine();
            if (ImGui::Button("Ref##MatRefBtn")) {
                AssetMetadata* meta = AssetRegistry::Get().FindByNameOrPath(obj->materialName);
                if (meta) {
                    OpenReferenceViewer(meta->id);
                } else {
                    OpenReferenceViewer();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("View Material in Asset Reference Viewer (dev.md Section 33)");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.2f, 0.75f, 1.0f, 1.0f), "Actor UV Tiling (UV Scale):");
            ImGui::DragFloat2("UV Scale##ActorUV", &obj->uvScale.x, 0.05f, 0.001f, 100.0f, "%.2f");
            ImGui::DragFloat("Tiling X (U)##ActorUVX", &obj->uvScale.x, 0.05f, 0.001f, 100.0f, "%.2f");
            ImGui::DragFloat("Tiling Y (V)##ActorUVY", &obj->uvScale.y, 0.05f, 0.001f, 100.0f, "%.2f");
            ImGui::SameLine();
            if (ImGui::Button("Reset##ActorUVReset")) {
                obj->uvScale = glm::vec2(1.0f, 1.0f);
            }
            ImGui::Spacing();
            if (ImGui::Button("Open in Material Editor", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                std::string targetPath = "";
                for (const auto& m : matList) {
                    if (m.name == obj->materialName || m.relPath == obj->materialName) {
                        targetPath = m.fullPath;
                        break;
                    }
                }
                if (targetPath.empty()) {
                    targetPath = (contentRootPath / "Materials" / "Default_Material.emat").string();
                }
                OpenMaterialEditor(targetPath);
            }
            ImGui::TextDisabled("Material properties are edited in the Material Editor.");
        }

        // 4. Lighting & Shadows
        if (!obj->isLight && obj->type != PrimitiveType::Empty && obj->type != PrimitiveType::LevelAnimation && ImGui::CollapsingHeader("Lighting & Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Cast Shadows", &obj->castShadows);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("When enabled, this actor casts shadows into the real-time shadow map.");
            }
            ImGui::SameLine();
            ImGui::Checkbox("Receive Shadows", &obj->receiveShadows);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("When enabled, this actor receives shadows from directional sun light.");
            }
        }

        // 4.5 Wicked Engine Mesh LOD (Disabled by default - full quality when disabled)
        if (!obj->isLight && obj->type != PrimitiveType::Empty && obj->type != PrimitiveType::LevelAnimation && ImGui::CollapsingHeader("Wicked Engine Mesh LOD", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool lodEnabled = obj->enableLOD;
            if (ImGui::Checkbox("Enable Mesh LOD", &lodEnabled)) {
                scene.SetMeshLODRecursive(obj->id, lodEnabled);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("When disabled (default), the mesh always renders at full quality (LOD 0) at all distances.\nWhen enabled, Wicked Engine screen-space coverage LOD scaling dynamically selects continuous simplified LOD levels.");
            }

            auto PropagateLODToChildren = [&](auto& self, int parentId, int forcedLOD, float lodBias, int minLOD, int maxLOD) -> void {
                GameObject* parent = scene.FindObject(parentId);
                if (!parent) return;
                for (int childId : parent->childIds) {
                    GameObject* child = scene.FindObject(childId);
                    if (child) {
                        child->forcedLOD = forcedLOD;
                        child->lodBias = lodBias;
                        child->minLOD = minLOD;
                        child->maxLOD = maxLOD;
                        self(self, childId, forcedLOD, lodBias, minLOD, maxLOD);
                    }
                }
            };

            uint32_t lodCount = obj->mesh.GetLODCount();

            if (!obj->enableLOD) {
                ImGui::BeginDisabled(true);
            }

            const char* lodOptions[] = {
                "Auto (Distance / Coverage)",
                "Forced LOD 0", "Forced LOD 1", "Forced LOD 2", "Forced LOD 3", "Forced LOD 4",
                "Forced LOD 5", "Forced LOD 6", "Forced LOD 7", "Forced LOD 8", "Forced LOD 9",
                "Forced LOD 10", "Forced LOD 11", "Forced LOD 12", "Forced LOD 13", "Forced LOD 14",
                "Forced LOD 15", "Forced LOD 16", "Forced LOD 17", "Forced LOD 18", "Forced LOD 19",
                "Forced LOD 20"
            };
            int currentLodOpt = (obj->forcedLOD < 0) ? 0 : std::min(obj->forcedLOD + 1, 21);
            if (ImGui::Combo("LOD Mode", &currentLodOpt, lodOptions, 22)) {
                obj->forcedLOD = (currentLodOpt == 0) ? -1 : (currentLodOpt - 1);
                PropagateLODToChildren(PropagateLODToChildren, obj->id, obj->forcedLOD, obj->lodBias, obj->minLOD, obj->maxLOD);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Controls Level of Detail (LOD) selection (0 to 20).\nAuto: dynamically selects LOD based on screen-space coverage.\nForced: locks object and all its children to a specific LOD level.");
            }

            if (ImGui::SliderFloat("LOD Bias", &obj->lodBias, -20.0f, 20.0f, "%.2f")) {
                PropagateLODToChildren(PropagateLODToChildren, obj->id, obj->forcedLOD, obj->lodBias, obj->minLOD, obj->maxLOD);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Offset applied to screen-space LOD calculation (-20.0 to +20.0).\nNegative: prefers higher detail closer.\nPositive: transitions to lower detail sooner.\nApplies recursively to all children.");
            }

            if (ImGui::SliderInt("Min LOD", &obj->minLOD, 0, 20)) {
                if (obj->minLOD > obj->maxLOD) obj->maxLOD = obj->minLOD;
                PropagateLODToChildren(PropagateLODToChildren, obj->id, obj->forcedLOD, obj->lodBias, obj->minLOD, obj->maxLOD);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Minimum LOD level (highest detail limit). Applies recursively to all children.");
            }

            if (ImGui::SliderInt("Max LOD", &obj->maxLOD, obj->minLOD, 20)) {
                PropagateLODToChildren(PropagateLODToChildren, obj->id, obj->forcedLOD, obj->lodBias, obj->minLOD, obj->maxLOD);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Maximum LOD level (lowest detail limit). Applies recursively to all children.");
            }

            if (!obj->enableLOD) {
                ImGui::EndDisabled();
            }

            // Real-time LOD Telemetry
            uint32_t activeLod = obj->enableLOD ? (uint32_t)glm::clamp(obj->currentLOD, 0, (int)lodCount - 1) : 0;
            uint32_t activeTriangles = (uint32_t)obj->mesh.GetIndicesForLOD(activeLod).size() / 3;
            uint32_t lod0Triangles = (uint32_t)obj->mesh.indices.size() / 3;
            float reductionPercent = (lod0Triangles > 0 && obj->enableLOD) ? ((1.0f - (float)activeTriangles / (float)lod0Triangles) * 100.0f) : 0.0f;

            ImGui::Text("Active LOD: ");
            ImGui::SameLine();
            if (obj->enableLOD) {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "LOD %u / %u", activeLod, lodCount);
                ImGui::SameLine();
                ImGui::TextDisabled("(%u Tris, -%.1f%%)", activeTriangles, reductionPercent);
            } else {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "LOD 0 / %u (Disabled - Full Quality)", lodCount);
                ImGui::SameLine();
                ImGui::TextDisabled("(%u Tris)", lod0Triangles);
            }
        }

        // 4.6 SFLOD (Single Face LOD / Imposter Cards System - Nacho Time!)
        if (!obj->isLight && !obj->isCamera && obj->type != PrimitiveType::Empty && obj->type != PrimitiveType::LevelAnimation && ImGui::CollapsingHeader("SFLOD (Single Face LOD - Nacho Time!)", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool sflodEnabled = obj->enableSFLOD;
            if (ImGui::Checkbox("Enable SFLOD", &sflodEnabled)) {
                obj->enableSFLOD = sflodEnabled;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("When enabled, this object automatically transitions to an ultra-efficient\nimposter card proxy (2 to 6 triangles) with dynamic orthogonal atlas sampling.");
            }

            if (!obj->enableSFLOD) {
                ImGui::BeginDisabled(true);
            }

            // 1-Click Nacho Time Generation Button (Prominent)
            std::filesystem::path contentDir = contentRootPath;
            if (contentDir.empty() && !activeProjectRoot.empty()) {
                contentDir = activeProjectRoot / "Content";
            }
            if (contentDir.empty()) {
                contentDir = "Projects/GameTest/Content";
            }

            // Auto-discover baked 6-directional atlas on disk if not yet assigned
            if (obj->sflodCustomTexture.empty() || !obj->sflodIsAtlas) {
                std::string safeName = SFLOD::SFLODSystem::CleanFilename(obj->name);
                std::vector<std::filesystem::path> searchDirs = {
                    contentDir / "Imposters",
                    "Content/Imposters",
                    "Projects/TestGameMake/Content/Imposters",
                    "Projects/GameTest/Content/Imposters"
                };
                for (const auto& sDir : searchDirs) {
                    std::filesystem::path candidate = sDir / ("SFLOD_" + safeName + "_Atlas.tga");
                    std::error_code ec;
                    if (std::filesystem::exists(candidate, ec)) {
                        obj->sflodCustomTexture = candidate.generic_string();
                        obj->sflodIsAtlas = true;
                        break;
                    }
                }
            }

            glm::mat4 modelMat = scene.GetWorldMatrix(*obj);
            glm::vec3 camEye = currentCamera ? currentCamera->GetPosition() : glm::vec3(0.0f, 2.0f, 5.0f);

            static int s_imposterResolution = 512;

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.75f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.90f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.35f, 0.65f, 1.0f));
            if (ImGui::Button("Generate Imposter Cards (Nacho Time!)", ImVec2(ImGui::GetContentRegionAvail().x, 32.0f))) {
                std::string bakedAtlas = SFLOD::SFLODSystem::Get().BakeNachoTimeAtlas(
                    *obj, modelMat, contentDir, s_imposterResolution, 
                    static_cast<SFLOD::SFLODShaderComplexity>(obj->sflodShaderComplexity));
                if (!bakedAtlas.empty()) {
                    AddLog("LogSFLOD", "Successfully baked 6-directional Nacho Time atlas: " + bakedAtlas, 0);
                }
            }
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("1-Click Generator: Captures 6 orthogonal shots (+X, -X, +Y, -Y, +Z, -Z) into a unified\n3x2 texture atlas in Content/Imposters/ and sets up dynamic view vector tracking.");
            }

            // Geometry Mode (Single Plane, Tri-Planar, 5-Face Box, 6-Face Cube, Custom Multi-Face)
            const char* geomModes[] = {
                "Single Plane (2 Tris - Parallax View Vector)",
                "Tri-Planar Nacho Mode (6 Tris - 3 Intersecting Planes)",
                "5-Face Box (10 Tris - Open Container / 5 Directions)",
                "6-Face Cube (12 Tris - Solid Box / 6 Directions)",
                "Custom Multi-Face Box (Varying Faces)"
            };
            int curGeomMode = glm::clamp(obj->sflodGeometryMode, 0, 4);
            if (ImGui::Combo("Geometry Mode", &curGeomMode, geomModes, 5)) {
                obj->sflodGeometryMode = curGeomMode;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Imposter Card Geometry Mode:\n"
                                  "- Single Plane: 1 quad (2 triangles) that dynamically samples dominant atlas tile based on view vector.\n"
                                  "- Tri-Planar Nacho Mode: 3 physical intersecting orthogonal planes (6 triangles) providing full 360 volume.\n"
                                  "- 5-Face Box: 5 physical boundary planes (10 triangles) for open-top containers, crates, bins, and boxes.\n"
                                  "- 6-Face Cube: 6 physical boundary planes (12 triangles) for closed boxes and solid polyhedral geometry.\n"
                                  "- Custom Multi-Face: Fully configurable active face mask (1 to 6 faces).");
            }

            // Open Face Selector (when 5-Face Box is selected)
            if (obj->sflodGeometryMode == 2) {
                const char* faceNames[] = {
                    "Front (+Z)", "Back (-Z)", "Right (+X)", "Left (-X)", "Top (+Y, Open Crate / Box)", "Bottom (-Y, Open Base)"
                };
                int curOpenFace = glm::clamp(obj->sflodOpenFace, 0, 5);
                if (ImGui::Combo("Open / Omitted Face", &curOpenFace, faceNames, 6)) {
                    obj->sflodOpenFace = curOpenFace;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Selects which face of the box is left open (unrendered).\nFor open crates, baskets, and bins (like in image.png), select Top (+Y).");
                }
            }

            // Active Faces Checkboxes (when Custom Multi-Face is selected, or collapsible fine-tuning)
            if (obj->sflodGeometryMode == 4 || ImGui::TreeNode("Active Face Masks (Fine-Tuning)")) {
                bool fFront = (obj->sflodFaceMask & (1 << 0)) != 0;
                bool fBack  = (obj->sflodFaceMask & (1 << 1)) != 0;
                bool fRight = (obj->sflodFaceMask & (1 << 2)) != 0;
                bool fLeft  = (obj->sflodFaceMask & (1 << 3)) != 0;
                bool fTop   = (obj->sflodFaceMask & (1 << 4)) != 0;
                bool fBot   = (obj->sflodFaceMask & (1 << 5)) != 0;

                ImGui::TextDisabled("Toggle which orthogonal face directions are active:");
                if (ImGui::Checkbox("Front (+Z)", &fFront)) { obj->sflodFaceMask ^= (1 << 0); } ImGui::SameLine();
                if (ImGui::Checkbox("Back (-Z)", &fBack))   { obj->sflodFaceMask ^= (1 << 1); } ImGui::SameLine();
                if (ImGui::Checkbox("Right (+X)", &fRight)) { obj->sflodFaceMask ^= (1 << 2); }
                if (ImGui::Checkbox("Left (-X)", &fLeft))   { obj->sflodFaceMask ^= (1 << 3); } ImGui::SameLine();
                if (ImGui::Checkbox("Top (+Y)", &fTop))     { obj->sflodFaceMask ^= (1 << 4); } ImGui::SameLine();
                if (ImGui::Checkbox("Bottom (-Y)", &fBot))  { obj->sflodFaceMask ^= (1 << 5); }

                if (obj->sflodGeometryMode != 4) {
                    ImGui::TreePop();
                }
            }

            // Shader Complexity
            const char* complexityOptions[] = {
                "Low (Diffuse + Alpha Cutout)",
                "Medium (+Normal Atlas)",
                "High (+Normal + RMA Atlas)"
            };
            int curComplexity = glm::clamp(obj->sflodShaderComplexity, 0, 2);
            if (ImGui::Combo("Shader Complexity", &curComplexity, complexityOptions, 3)) {
                obj->sflodShaderComplexity = curComplexity;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Material channel complexity:\n- Low: 1 Atlas (Base Color + Alpha Mask)\n- Medium: 2 Atlases (+Normal Map Atlas)\n- High: 3 Atlases (+Roughness, Metallic, AO Atlas)");
            }

            // Dynamic LOD Swapping: Distance Threshold (imp_threshold)
            ImGui::DragFloat("Distance Threshold (imp_threshold)", &obj->sflodDistanceThreshold, 0.5f, 0.0f, 500.0f, "%.1f m");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Superhive / Nacho Time Distance Threshold (imp_threshold) in meters:\n"
                                  "When camera distance is >= this threshold, the 3D mesh is automatically swapped with the imposter proxy.\n"
                                  "(Set to 0 to disable distance trigger and rely solely on the independent 20-level SFLOD system).");
            }

            // Independent 20-Level SFLOD Controls (Completely separate from Mesh LOD)
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Independent SFLOD System (20 LOD Levels)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("SFLOD operates completely independently of Mesh LOD.\nEven if Mesh LOD is disabled or the mesh only has LOD 0, SFLOD evaluates\ncontinuous screen-space coverage across 20 virtual LOD levels.");
            }

            ImGui::SliderInt("Trigger SFLOD Level", &obj->sflodMinLOD, 0, 20);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("The SFLOD level threshold (>=) at which the 3D mesh transitions to the Imposter Card (0 to 20).\nExample: 3 means SFLOD LOD 3 through 20 will render as an imposter card.\nSetting 0 triggers imposter card immediately at all distances.");
            }

            ImGui::SliderInt("Max SFLOD Level", &obj->sflodMaxLOD, 1, 20);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Maximum SFLOD LOD level ceiling (1 to 20).");
            }

            ImGui::SliderFloat("SFLOD LOD Bias", &obj->sflodLODBias, -20.0f, 20.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Independent LOD Bias for SFLOD (-20.0 to +20.0).\nPositive values increase SFLOD level sooner (earlier transition to imposter).\nNegative values decrease SFLOD level (delays transition).");
            }

            // Billboard Mode (only relevant for Single Plane)
            if (obj->sflodGeometryMode == 0) {
                const char* bbModes[] = {
                    "Camera Facing (Spherical)",
                    "Locked Y-Axis (Axial / Nacho Time)",
                    "Fixed Plane (Object Local)"
                };
                int curBbMode = glm::clamp(obj->sflodBillboardMode, 0, 2);
                if (ImGui::Combo("Billboard Mode", &curBbMode, bbModes, 3)) {
                    obj->sflodBillboardMode = curBbMode;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Orientation mode of the single-plane card:\n"
                                      "- Camera Facing (Spherical): rotates in all directions towards camera.\n"
                                      "- Locked Y-Axis (Axial / Nacho Time): classic foliage mode that stays vertically upright.\n"
                                      "- Fixed Plane: fixed local orientation of the object.");
                }
            }

            // Card Dimensions
            bool isAutoDim = (obj->sflodPlaneWidth <= 0.001f && obj->sflodPlaneHeight <= 0.001f);
            if (ImGui::Checkbox("Auto Dimensions (Mesh Bounds)", &isAutoDim)) {
                if (isAutoDim) {
                    obj->sflodPlaneWidth = 0.0f;
                    obj->sflodPlaneHeight = 0.0f;
                } else {
                    float w = 1.0f, h = 1.0f;
                    glm::vec3 off{0.0f};
                    SFLOD::SFLODSystem::Get().ComputeCardBounds(*obj, w, h, off);
                    obj->sflodPlaneWidth = w;
                    obj->sflodPlaneHeight = h;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Automatically calculates card width and height from the mesh bounding box.");
            }

            if (isAutoDim) {
                ImGui::BeginDisabled(true);
            }
            float dims[2] = { obj->sflodPlaneWidth, obj->sflodPlaneHeight };
            if (ImGui::DragFloat2("Custom Size (W x H)", dims, 0.05f, 0.0f, 100.0f, "%.2f")) {
                obj->sflodPlaneWidth = dims[0];
                obj->sflodPlaneHeight = dims[1];
            }
            if (isAutoDim) {
                ImGui::EndDisabled();
            }

            // Center Offset
            ImGui::DragFloat2("Center Offset (X, Y)", &obj->sflodPlaneOffset.x, 0.05f, -50.0f, 50.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Fine-tunes the horizontal and vertical center positioning of the imposter card.");
            }

            // Texture & Cutout
            ImGui::Checkbox("Alpha Cutout Mask", &obj->sflodCutout);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Uses hardware alpha masking (blendMode = Masked) so transparent card regions are clipped out.");
            }

            // Atlas Status & Custom Texture Path
            if (obj->sflodIsAtlas || obj->sflodCustomTexture.find("_Atlas") != std::string::npos) {
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "[6-WAY ORTHOGONAL ATLAS ACTIVE]");
            }
            char texBuf[256];
            memset(texBuf, 0, sizeof(texBuf));
            strncpy_s(texBuf, obj->sflodCustomTexture.c_str(), sizeof(texBuf) - 1);
            if (ImGui::InputText("Card Texture", texBuf, sizeof(texBuf))) {
                obj->sflodCustomTexture = texBuf;
                obj->sflodIsAtlas = (obj->sflodCustomTexture.find("_Atlas") != std::string::npos);
            }
            if (obj->sflodCustomTexture.empty()) {
                ImGui::TextDisabled("  (Empty = Click 'Generate Imposter Cards' above or uses base material)");
            }

            // Advanced Baking Options
            if (ImGui::TreeNode("Advanced Projection Baking Options")) {
                const char* resOptions[] = { "256 x 256", "512 x 512 (Recommended)", "1024 x 1024" };
                int curResOpt = (s_imposterResolution == 256) ? 0 : (s_imposterResolution == 1024 ? 2 : 1);
                if (ImGui::Combo("Atlas Tile Resolution", &curResOpt, resOptions, 3)) {
                    if (curResOpt == 0) s_imposterResolution = 256;
                    else if (curResOpt == 2) s_imposterResolution = 1024;
                    else s_imposterResolution = 512;
                }

                if (ImGui::Button("Capture Single View Texture", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                    std::string bakedPath = SFLOD::SFLODSystem::Get().BakeAndSaveImposterTexture(
                        *obj, modelMat, camEye, contentDir, -1, s_imposterResolution);
                    if (!bakedPath.empty()) {
                        AddLog("LogSFLOD", "Successfully baked view-dependent imposter texture: " + bakedPath, 0);
                    }
                }

                if (ImGui::Button("Bake 8-Directional View Atlas", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                    bool ok = SFLOD::SFLODSystem::Get().BakeAll8Angles(*obj, modelMat, contentDir, s_imposterResolution);
                    if (ok) {
                        AddLog("LogSFLOD", "Successfully baked 8-directional imposter series into Content/Imposters/", 0);
                    }
                }
                ImGui::TreePop();
            }

            // Real-Time Telemetry & Status
            ImGui::Spacing();
            ImGui::Separator();
            glm::vec3 worldPos = glm::vec3(modelMat[3]);
            float camDist = glm::length(camEye - worldPos);
            bool distTriggered = (obj->sflodDistanceThreshold > 0.0f && camDist >= obj->sflodDistanceThreshold);
            bool lodTriggered = (obj->sflodCurrentLOD >= obj->sflodMinLOD);
            bool previewTriggered = (obj->forcedLOD >= 0 && obj->forcedLOD >= obj->sflodMinLOD);
            bool isCardActive = obj->enableSFLOD && (distTriggered || lodTriggered || previewTriggered);

            uint32_t meshTris = (uint32_t)obj->mesh.indices.size() / 3;
            uint32_t cardTris = 2;
            const char* geomName = "1-Face Parallax Card";
            if (obj->sflodGeometryMode == 1) {
                cardTris = 6;
                geomName = "Tri-Planar Nacho Mode";
            } else if (obj->sflodGeometryMode == 2) {
                cardTris = 10;
                geomName = "5-Face Open Box";
            } else if (obj->sflodGeometryMode == 3) {
                cardTris = 12;
                geomName = "6-Face Closed Cube";
            } else if (obj->sflodGeometryMode == 4) {
                int activeCount = 0;
                for (int b = 0; b < 6; ++b) { if (obj->sflodFaceMask & (1 << b)) activeCount++; }
                cardTris = activeCount * 2;
                geomName = "Custom Multi-Face Box";
            }
            float reduction = meshTris > cardTris ? ((1.0f - (float)cardTris / (float)meshTris) * 100.0f) : 0.0f;

            if (isCardActive) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[ACTIVE: %s (%u Tris)]", geomName, cardTris);
                ImGui::Text("Saved Triangles: %u Tris (-%.2f%% reduction)", meshTris > cardTris ? meshTris - cardTris : 0, reduction);
                if (previewTriggered) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Triggered by Viewport Preview Lock (Forced SFLOD: %d)", obj->forcedLOD);
                } else if (distTriggered) {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Triggered by Distance: %.1fm >= %.1fm", camDist, obj->sflodDistanceThreshold);
                } else if (lodTriggered) {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Triggered by SFLOD Level: LOD %d >= %d", obj->sflodCurrentLOD, obj->sflodMinLOD);
                }
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[INACTIVE: Rendering 3D Mesh (%u Tris)]", meshTris);
                ImGui::TextDisabled("Distance: %.1fm (Thresh: %.1fm) | SFLOD: LOD %d/20 (Trigger: %d) | Mesh LOD: %s",
                    camDist, obj->sflodDistanceThreshold, obj->sflodCurrentLOD, obj->sflodMinLOD,
                    obj->enableLOD ? "Active" : "Disabled");
            }

            // Quick Preview Button
            bool isPreviewing = (obj->forcedLOD >= 0 && obj->forcedLOD >= obj->sflodMinLOD);
            if (ImGui::Button(isPreviewing ? "Exit Imposter Preview" : "Preview Imposter in Viewport", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                if (isPreviewing) {
                    obj->forcedLOD = -1;
                } else {
                    obj->forcedLOD = obj->sflodMinLOD;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Temporarily locks this object to its SFLOD proxy so you can preview it directly in the viewport.");
            }

            if (!obj->enableSFLOD) {
                ImGui::EndDisabled();
            }
        }
        // 4.8 Modifiers Section (includes Collisions and Physics modifiers)
        if (!obj->isLight && !obj->isCamera && !IsLightPrimitive(obj->type) && !IsCameraPrimitive(obj->type)) {
            char modHeaderTitle[64];
            int totalMods = (int)obj->modifiers.size() + (obj->hasPhysics ? 1 : 0) + (obj->hasAnimator ? 1 : 0) + (obj->hasLevelAnimation ? 1 : 0);
            snprintf(modHeaderTitle, sizeof(modHeaderTitle), "Modifiers (%d)###ModifiersSection", totalMods);
            if (ImGui::CollapsingHeader(modHeaderTitle, ImGuiTreeNodeFlags_DefaultOpen)) {
                // Add Modifier button and popup
                if (ImGui::Button("Add Modifier", ImVec2(ImGui::GetContentRegionAvail().x, 28.0f))) {
                    ImGui::OpenPopup("AddModifierPopup");
                }

                if (ImGui::BeginPopup("AddModifierPopup")) {
                    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "COLLISION MODIFIERS");
                    ImGui::Separator();
                    if (ImGui::MenuItem("Box Collision")) {
                        obj->AddModifier(CollisionShapeType::Box);
                        AddLog("LogPhysics", "Added Box Collision to " + obj->name, 0);
                    }
                    if (ImGui::MenuItem("Sphere Collision")) {
                        obj->AddModifier(CollisionShapeType::Sphere);
                        AddLog("LogPhysics", "Added Sphere Collision to " + obj->name, 0);
                    }
                    if (ImGui::MenuItem("Capsule Collision")) {
                        obj->AddModifier(CollisionShapeType::Capsule);
                        AddLog("LogPhysics", "Added Capsule Collision to " + obj->name, 0);
                    }
                    if (ImGui::MenuItem("Cylinder Collision")) {
                        obj->AddModifier(CollisionShapeType::Cylinder);
                        AddLog("LogPhysics", "Added Cylinder Collision to " + obj->name, 0);
                    }
                    if (ImGui::MenuItem("Cone Collision")) {
                        obj->AddModifier(CollisionShapeType::Cone);
                        AddLog("LogPhysics", "Added Cone Collision to " + obj->name, 0);
                    }
                    if (ImGui::MenuItem("Plane Collision")) {
                        obj->AddModifier(CollisionShapeType::Plane);
                        AddLog("LogPhysics", "Added Plane Collision to " + obj->name, 0);
                    }
                    if (ImGui::MenuItem("Mesh Collision")) {
                        obj->AddModifier(CollisionShapeType::Mesh);
                        AddLog("LogPhysics", "Added Mesh Collision to " + obj->name, 0);
                    }
                    if (ImGui::MenuItem("Character Collision")) {
                        obj->AddModifier(CollisionShapeType::Character);
                        AddLog("LogPhysics", "Added Character Collision to " + obj->name, 0);
                    }
                    if (ImGui::MenuItem("Terrain Collision")) {
                        obj->AddModifier(CollisionShapeType::Terrain);
                        AddLog("LogPhysics", "Added Terrain Collision to " + obj->name, 0);
                    }

                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "PHYSICS MODIFIERS");
                    ImGui::Separator();
                    if (!obj->hasPhysics) {
                        if (ImGui::MenuItem("Character Physics")) {
                            obj->AddCharacterPhysicsModifier();
                            AddLog("LogPhysics", "Added Character Physics to " + obj->name, 0);
                        }
                        if (ImGui::MenuItem("Physics Modifier")) {
                            obj->AddPhysicsModifier();
                            AddLog("LogPhysics", "Added Physics Modifier to " + obj->name, 0);
                        }
                    } else {
                        ImGui::TextDisabled("Physics Modifier already added");
                    }

                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.6f, 0.4f, 1.0f, 1.0f), "ANIMATION MODIFIERS");
                    ImGui::Separator();
                    if (!obj->hasAnimator) {
                        if (ImGui::MenuItem("Animation Graph")) {
                            obj->AddAnimatorModifier();
                            AddLog("LogAnimation", "Added Animation Graph modifier to " + obj->name, 0);
                        }
                    } else {
                        ImGui::TextDisabled("Animation Graph already added");
                    }
                    if (!obj->hasLevelAnimation) {
                        if (ImGui::MenuItem("Level Animation")) {
                            obj->AddLevelAnimation();
                            AddLog("LogLevelAnimation", "Added Level Animation component to " + obj->name, 0);
                        }
                    } else {
                        ImGui::TextDisabled("Level Animation already added");
                    }

                    ImGui::EndPopup();
                }

                if (obj->modifiers.empty() && !obj->hasPhysics && !obj->hasAnimator) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("No modifiers attached to this object.");
                    ImGui::TextDisabled("Click '+ Add Modifier' to add collision or physics modifiers.");
                    ImGui::Spacing();
                } else {
                    int modifierToRemove = -1;

                    // 1. Render Collision Modifiers
                    for (size_t mi = 0; mi < obj->modifiers.size(); ++mi) {
                        auto& mod = obj->modifiers[mi];
                        ImGui::PushID((int)mi);

                        ImGui::Spacing();
                        ImGui::Separator();

                        // Header with enabled toggle, name, and remove button
                        ImGui::Checkbox("##ModEnabled", &mod.enabled);
                        ImGui::SameLine();

                        char modHeaderLabel[128];
                        snprintf(modHeaderLabel, sizeof(modHeaderLabel), "%s##Header%d", mod.name.c_str(), (int)mi);
                        bool isModOpen = ImGui::TreeNodeEx(modHeaderLabel, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth);

                        ImGui::SameLine(ImGui::GetContentRegionAvail().x + 20.0f);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 0.8f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                        if (ImGui::SmallButton("X##RemoveMod")) {
                            modifierToRemove = (int)mi;
                        }
                        ImGui::PopStyleColor(2);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Remove this collision modifier");
                        }

                        if (isModOpen) {
                            ImGui::Indent(8.0f);

                            // Modifier Name edit
                            char nameBuf[128];
                            strncpy(nameBuf, mod.name.c_str(), sizeof(nameBuf));
                            nameBuf[sizeof(nameBuf) - 1] = '\0';
                            if (ImGui::InputText("Modifier Name", nameBuf, sizeof(nameBuf))) {
                                mod.name = nameBuf;
                            }

                            // Shape Type selector
                            const char* shapeNames[] = {
                                "None", "Box Collision", "Sphere Collision", "Capsule Collision",
                                "Cylinder Collision", "Cone Collision", "Plane Collision", "Mesh Collision",
                                "Character Collision"
                            };
                            int currentShapeIdx = (int)mod.shapeType;
                            if (ImGui::Combo("Shape", &currentShapeIdx, shapeNames, 9)) {
                                mod.shapeType = (CollisionShapeType)currentShapeIdx;
                                mod.isDefaultFit = false;
                            }

                            ImGui::Checkbox("Is Trigger", &mod.isTrigger);

                            // Center Offset
                            ImGui::DragFloat3("Center Offset", &mod.centerOffset.x, 0.02f, -100.0f, 100.0f, "%.2f");

                            // Shape-specific extents
                            switch (mod.shapeType) {
                                case CollisionShapeType::Box:
                                case CollisionShapeType::Mesh:
                                    ImGui::DragFloat3("Half-Extents", &mod.boxExtents.x, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    break;

                                case CollisionShapeType::Sphere:
                                    ImGui::DragFloat("Radius", &mod.radius, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    break;

                                case CollisionShapeType::Capsule:
                                    ImGui::DragFloat("Radius", &mod.radius, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    ImGui::DragFloat("Height", &mod.height, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    break;

                                case CollisionShapeType::Character:
                                    ImGui::DragFloat("Radius", &mod.radius, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    ImGui::DragFloat("Height", &mod.height, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    if (ImGui::Checkbox("Prevent Side Fall", &mod.preventSideFall)) {
                                        mod.sideFall = !mod.preventSideFall;
                                    }
                                    if (ImGui::IsItemHovered()) {
                                        ImGui::SetTooltip("When enabled (default), keeps character upright and prevents falling in cross/side directions during physics simulation. Disable to allow side falling.");
                                    }
                                    break;

                                case CollisionShapeType::Cylinder:
                                    ImGui::DragFloat("Radius", &mod.radius, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    ImGui::DragFloat("Height", &mod.height, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    break;

                                case CollisionShapeType::Cone:
                                    ImGui::DragFloat("Radius", &mod.radius, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    ImGui::DragFloat("Height", &mod.height, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    break;

                                case CollisionShapeType::Plane:
                                    ImGui::DragFloat2("Plane Size", &mod.planeSize.x, 0.05f, 0.1f, 10000.0f, "%.2f");
                                    break;

                                case CollisionShapeType::Terrain:
                                    ImGui::DragFloat3("Box Bounds", &mod.boxExtents.x, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    ImGui::DragFloat("Height Offset", &mod.terrainOffsetHeight, 0.05f, -100.0f, 100.0f, "%.2f m");
                                    ImGui::SliderFloat("Follow Terrain Tilt", &mod.terrainFollowTilt, 0.0f, 1.0f, "%.2f (Blend)");
                                    ImGui::Checkbox("Dynamic Surface Stick", &mod.terrainStickToSurface);
                                    break;

                                default:
                                    break;
                            }

                            // Physics Presets & Properties
                            const char* presets[] = { "Default", "BlockAll", "OverlapAll", "TriggerOnly", "NoCollision" };
                            int curPresetIdx = 0;
                            for (int p = 0; p < 5; ++p) {
                                if (mod.preset == presets[p]) { curPresetIdx = p; break; }
                            }
                            if (ImGui::Combo("Preset", &curPresetIdx, presets, 5)) {
                                mod.ApplyPreset(presets[curPresetIdx]);
                            }

                            ImGui::SliderFloat("Friction", &mod.friction, 0.0f, 1.0f, "%.2f");
                            ImGui::SliderFloat("Restitution", &mod.restitution, 0.0f, 1.0f, "%.2f");

                            // Visualization
                            ImGui::Checkbox("Show Wireframe", &mod.showWireframe);
                            ImGui::SameLine();
                            ImGui::ColorEdit4("Color##ModWireColor", &mod.wireframeColor.x, ImGuiColorEditFlags_NoInputs);

                            // Auto-fit & Reset buttons
                            if (ImGui::Button("Auto-Fit", ImVec2(ImGui::GetContentRegionAvail().x * 0.48f, 24.0f))) {
                                Collisions::CollisionSystem::Get().AutoFitModifier(*obj, mod);
                                AddLog("LogPhysics", "Auto-fitted bounds for " + mod.name, 0);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Reset Defaults", ImVec2(ImGui::GetContentRegionAvail().x, 24.0f))) {
                                Collisions::CollisionSystem::Get().ResetModifierToDefault(*obj, mod);
                                AddLog("LogPhysics", "Reset " + mod.name + " to defaults", 0);
                            }

                            ImGui::Unindent(8.0f);
                            ImGui::TreePop();
                        }

                        ImGui::PopID();
                    }

                    if (modifierToRemove >= 0 && modifierToRemove < (int)obj->modifiers.size()) {
                        AddLog("LogPhysics", "Removed modifier: " + obj->modifiers[modifierToRemove].name, 0);
                        obj->RemoveModifier((size_t)modifierToRemove);
                    }

                    // 2. Render Physics Modifier (if attached)
                    if (obj->hasPhysics) {
                        ImGui::PushID("PhysicsModifierCard");
                        ImGui::Spacing();
                        ImGui::Separator();

                        // Header with enabled toggle, name, and remove button
                        ImGui::Checkbox("##PhysModEnabled", &obj->physics.enabled);
                        ImGui::SameLine();

                        bool isCharPhys = obj->physics.isCharacterPhysics;
                        std::string physHeader = isCharPhys ? "Character Physics##PhysHeader" : "Physics Modifier##PhysHeader";
                        bool isPhysOpen = ImGui::TreeNodeEx(physHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth);

                        ImGui::SameLine(ImGui::GetContentRegionAvail().x + 20.0f);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 0.8f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                        if (ImGui::SmallButton("X##RemovePhysMod")) {
                            obj->RemovePhysicsModifier();
                            AddLog("LogPhysics", "Removed Physics Modifier from " + obj->name, 0);
                        }
                        ImGui::PopStyleColor(2);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Remove Physics Modifier");
                        }

                        if (isPhysOpen) {
                            ImGui::Indent(8.0f);

                            if (isCharPhys) {
                                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "Character Dynamics Active (Zero Bounce, Stop-on-Collision)");
                                ImGui::Spacing();
                            }

                            // Gravity properties
                            ImGui::Checkbox("Use Gravity", &obj->physics.useGravity);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("When enabled, gravitational acceleration pulls this object downward during Play Mode.");
                            }

                            if (obj->physics.useGravity) {
                                ImGui::DragFloat("Gravity Multiplier", &obj->physics.gravityMultiplier, 0.05f, 0.0f, 100.0f, "%.2fx");
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Multiplies global gravity (e.g. 1.0 = normal 9.81 m/sÃƒâ€šÃ‚Â², 2.0 = double gravity).");
                                }
                            }

                            // Mass property
                            ImGui::DragFloat("Mass (kg)", &obj->physics.mass, 0.1f, 0.01f, 100000.0f, "%.2f kg");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Mass of the object in kilograms. Affects inertia and collision momentum transfer.");
                            }

                            // Dynamics & Drag
                            ImGui::DragFloat("Linear Damping (Drag)", &obj->physics.linearDamping, 0.005f, 0.0f, 1.0f, "%.3f");
                            ImGui::DragFloat("Angular Damping", &obj->physics.angularDamping, 0.005f, 0.0f, 1.0f, "%.3f");

                            // Restitution & Friction
                            if (isCharPhys) {
                                ImGui::TextDisabled("Bounciness: 0.00 (Zero bounce for character)");
                                obj->physics.bounciness = 0.0f;
                            } else {
                                ImGui::SliderFloat("Bounciness", &obj->physics.bounciness, 0.0f, 1.0f, "%.2f");
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Restitution: 0.0 = no bounce, 1.0 = superball.");
                                }
                            }
                            ImGui::SliderFloat("Friction", &obj->physics.friction, 0.0f, 1.0f, "%.2f");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Surface friction coefficient.");
                            }

                            // Kinematic toggle
                            ImGui::Checkbox("Is Kinematic", &obj->physics.isKinematic);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("When true, object transform is driven manually/by scripts and ignores physics forces.");
                            }

                            // Constraints
                            ImGui::Spacing();
                            ImGui::TextDisabled("Freeze Position:");
                            ImGui::SameLine();
                            ImGui::Checkbox("X##FrzX", &obj->physics.lockPositionX);
                            ImGui::SameLine();
                            ImGui::Checkbox("Y##FrzY", &obj->physics.lockPositionY);
                            ImGui::SameLine();
                            ImGui::Checkbox("Z##FrzZ", &obj->physics.lockPositionZ);

                            if (isCharPhys) {
                                ImGui::TextDisabled("Freeze Rotation: X [Locked]  Y [Free]  Z [Locked] (Upright)");

                                ImGui::Spacing();
                                ImGui::Separator();
                                ImGui::Text("Character Collision Impulse");
                                ImGui::Checkbox("Enable Step-Back Impulse##CharImpulse", &obj->physics.enableStepBackImpulse);
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("When enabled, applies a slight impulse when moving back away from an obstacle contact.");
                                }
                                if (obj->physics.enableStepBackImpulse) {
                                    ImGui::DragFloat("Impulse Multiplier##CharImpMult", &obj->physics.stepBackImpulseMultiplier, 0.05f, 0.1f, 10.0f, "%.2fx");
                                }
                            } else {
                                ImGui::TextDisabled("Freeze Rotation:");
                                ImGui::SameLine();
                                ImGui::Checkbox("X##FrzRotX", &obj->physics.lockRotationX);
                                ImGui::SameLine();
                                ImGui::Checkbox("Y##FrzRotY", &obj->physics.lockRotationY);
                                ImGui::SameLine();
                                ImGui::Checkbox("Z##FrzRotZ", &obj->physics.lockRotationZ);
                            }

                            // Runtime Debug info
                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Simulation Info:");
                            ImGui::Text("  Velocity: (%.2f, %.2f, %.2f) m/s", obj->physics.velocity.x, obj->physics.velocity.y, obj->physics.velocity.z);
                            ImGui::Text("  Grounded: %s", obj->physics.isGrounded ? "Yes" : "No");

                            if (ImGui::Button("Reset Velocity", ImVec2(ImGui::GetContentRegionAvail().x, 24.0f))) {
                                obj->physics.ResetVelocity();
                                AddLog("LogPhysics", "Reset velocity for " + obj->name, 0);
                            }

                            ImGui::Unindent(8.0f);
                            ImGui::TreePop();
                        }

                        ImGui::PopID();
                    }

                    // 3. Render Animation Graph Modifier (if attached)
                    if (obj->hasAnimator) {
                        ImGui::PushID("AnimationGraphModifierCard");
                        ImGui::Spacing();
                        ImGui::Separator();

                        ImGui::Checkbox("##AnimModEnabled", &obj->animator.enabled);
                        ImGui::SameLine();

                        bool isAnimOpen = ImGui::TreeNodeEx("Animation Graph##AnimHeader", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth);

                        ImGui::SameLine(ImGui::GetContentRegionAvail().x + 20.0f);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 0.8f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                        if (ImGui::SmallButton("X##RemoveAnimMod")) {
                            obj->RemoveAnimatorModifier();
                            AddLog("LogAnimation", "Removed Animation Graph modifier from " + obj->name, 0);
                        }
                        ImGui::PopStyleColor(2);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Remove Animation Graph modifier");
                        }

                        if (isAnimOpen) {
                            ImGui::Indent(8.0f);

                            // Project .eanimator file picker dropdown
                            std::string previewGraphName = obj->animator.graphPath.empty() ? "(None - Select Animation Graph)" : std::filesystem::path(obj->animator.graphPath).filename().string();
                            if (ImGui::BeginCombo("Graph##AnimGraphSelector", previewGraphName.c_str())) {
                                if (ImGui::Selectable("(None)", obj->animator.graphPath.empty())) {
                                    obj->animator.LoadGraph("");
                                }
                                ImGui::Separator();

                                std::vector<std::string> animGraphs;
                                std::error_code ec;
                                std::filesystem::path contentDir = contentRootPath;
                                if (std::filesystem::exists(contentDir, ec)) {
                                    for (const auto& entry : std::filesystem::recursive_directory_iterator(contentDir, ec)) {
                                        if (entry.is_regular_file()) {
                                            std::string ext = entry.path().extension().string();
                                            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                            if (ext == ".eanimator" || ext == ".animator") {
                                                std::string rel = std::filesystem::relative(entry.path(), contentDir, ec).string();
                                                animGraphs.push_back(rel);
                                            }
                                        }
                                    }
                                }

                                for (const auto& gPath : animGraphs) {
                                    bool isSel = (obj->animator.graphPath == gPath || obj->animator.graphPath.find(gPath) != std::string::npos);
                                    if (ImGui::Selectable(gPath.c_str(), isSel)) {
                                        std::filesystem::path fullP = contentRootPath / gPath;
                                        obj->animator.LoadGraph(fullP.string());
                                        AddLog("LogAnimation", "Assigned Animation Graph: " + gPath + " to " + obj->name, 0);
                                    }
                                }
                                ImGui::EndCombo();
                            }

                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                    std::string droppedPath((const char*)payload->Data);
                                    if (droppedPath.find(".eanimator") != std::string::npos || droppedPath.find(".animator") != std::string::npos) {
                                        std::filesystem::path fullP = contentRootPath / droppedPath;
                                        obj->animator.LoadGraph(fullP.string());
                                        AddLog("LogAnimation", "Dropped Animation Graph: " + droppedPath + " onto " + obj->name, 0);
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }

                            // State & telemetry
                            if (obj->animator.instance && obj->animator.graph) {
                                std::string curState = obj->animator.GetCurrentStateName();
                                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.5f, 1.0f), "Active State: %s", curState.c_str());
                                if (obj->animator.IsTransitioning()) {
                                    ImGui::SameLine();
                                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[Transitioning...]");
                                }

                                if (!obj->animator.instance->runtimeParams.empty()) {
                                    if (ImGui::TreeNodeEx("Live Parameters##AnimLiveParams", ImGuiTreeNodeFlags_None)) {
                                        for (auto& pair : obj->animator.instance->runtimeParams) {
                                            auto& p = pair.second;
                                            if (p.type == Eunoia::AnimatorParamType::Boolean) {
                                                ImGui::Checkbox(p.name.c_str(), &p.boolValue);
                                            } else if (p.type == Eunoia::AnimatorParamType::Float) {
                                                ImGui::DragFloat(p.name.c_str(), &p.floatValue, 0.1f);
                                            } else if (p.type == Eunoia::AnimatorParamType::Int) {
                                                ImGui::DragInt(p.name.c_str(), &p.intValue);
                                            } else if (p.type == Eunoia::AnimatorParamType::String) {
                                                char sbuf[128];
                                                strncpy_s(sbuf, sizeof(sbuf), p.stringValue.c_str(), sizeof(sbuf) - 1);
                                                if (ImGui::InputText(p.name.c_str(), sbuf, sizeof(sbuf))) {
                                                    p.stringValue = sbuf;
                                                }
                                            }
                                        }
                                        ImGui::TreePop();
                                    }
                                }
                            } else if (!obj->animator.graphPath.empty()) {
                                ImGui::TextDisabled("Graph assigned: %s", obj->animator.graphPath.c_str());
                            } else {
                                ImGui::TextDisabled("Select an Animation Graph from dropdown or drag from Content Browser.");
                            }

                            ImGui::Unindent(8.0f);
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                }
            }
        }

        // 5. Actor Behavior
        if (ImGui::CollapsingHeader("Actor Behavior")) {
            ImGui::Checkbox("Auto-Rotate (Movable Only)", &obj->autoRotate);
            if (obj->autoRotate) {
                ImGui::DragFloat3("Speed (deg/s)", &obj->autoRotateSpeed.x, 1.0f, -360.0f, 360.0f);
            }
        }

        // 6. Behaviours (dev.md Section 6, 7, 8, 9, 10, 11, 12, 13)
        RenderBehavioursSection(scene, obj);

        ImGui::PopID();
    }
    ImGui::End();
}

std::vector<TextureAssetEntry> EngineUI::ScanProjectTextures() {
    std::vector<TextureAssetEntry> textures;
    auto regTextures = AssetRegistry::Get().FindByType(AssetType::Texture);
    textures.reserve(regTextures.size());

    for (const auto& meta : regTextures) {
        TextureAssetEntry te;
        te.filename = std::filesystem::path(meta.sourcePath).filename().string();
        te.fullPath = (contentRootPath / meta.sourcePath).string();
        te.relPath = meta.sourcePath;
        te.relFolder = std::filesystem::path(meta.sourcePath).parent_path().generic_string();
        if (te.relFolder == ".") te.relFolder = "";
        te.assetId = meta.id;
        te.virtualPath = meta.virtualPath;
        textures.push_back(te);
    }

    std::sort(textures.begin(), textures.end(), [](const auto& a, const auto& b) {
        return a.virtualPath < b.virtualPath;
    });
    return textures;
}

bool EngineUI::DrawTextureSlot(const char* label, std::string& textureSlotValue, AssetID& textureSlotAssetId, const std::vector<TextureAssetEntry>& availableTextures) {
    bool changed = false;
    ImGui::PushID(label);

    ImGui::Text("%s", label);

    std::string previewText;
    if (textureSlotAssetId.IsValid()) {
        AssetMetadata* meta = AssetRegistry::Get().Find(textureSlotAssetId);
        if (meta) {
            previewText = meta->objectName + "  [" + meta->id.ToString().substr(0, 8) + "...]";
        } else {
            previewText = "[Missing: " + textureSlotAssetId.ToString().substr(0, 8) + "...]";
        }
    } else if (!textureSlotValue.empty()) {
        previewText = std::filesystem::path(textureSlotValue).filename().string();
    } else {
        previewText = "(None)";
    }

    float availW = ImGui::GetContentRegionAvail().x;
    float clearBtnW = 28.0f;
    float comboW = availW - clearBtnW - 8.0f;
    if (comboW < 80.0f) comboW = 80.0f;

    ImGui::SetNextItemWidth(comboW);
    std::string comboId = "##TexCombo_" + std::string(label);
    if (ImGui::BeginCombo(comboId.c_str(), previewText.c_str(), ImGuiComboFlags_HeightLarge)) {
        char searchId[64];
        snprintf(searchId, sizeof(searchId), "##TexSearch_%s", label);
        static std::map<std::string, std::string> textureSearchMap;
        std::string& currentSearch = textureSearchMap[label];
        char searchBuf[64];
        strncpy(searchBuf, currentSearch.c_str(), sizeof(searchBuf));
        searchBuf[sizeof(searchBuf) - 1] = '\0';

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint(searchId, "Search textures or AssetID...", searchBuf, sizeof(searchBuf))) {
            currentSearch = searchBuf;
        }
        ImGui::Separator();

        bool isNoneSelected = !textureSlotAssetId.IsValid() && textureSlotValue.empty();
        if (ImGui::Selectable("(None)", isNoneSelected)) {
            textureSlotValue = "";
            textureSlotAssetId = AssetID::Null();
            changed = true;
        }
        if (isNoneSelected) ImGui::SetItemDefaultFocus();
        ImGui::Separator();

        std::string searchLower = currentSearch;
        std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

        int count = 0;
        for (const auto& tex : availableTextures) {
            std::string nameLower = tex.filename;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            std::string virtLower = tex.virtualPath;
            std::transform(virtLower.begin(), virtLower.end(), virtLower.begin(), ::tolower);
            std::string idStr = tex.assetId.ToString();
            std::string idLower = idStr;
            std::transform(idLower.begin(), idLower.end(), idLower.begin(), ::tolower);

            if (!searchLower.empty() &&
                nameLower.find(searchLower) == std::string::npos &&
                virtLower.find(searchLower) == std::string::npos &&
                idLower.find(searchLower) == std::string::npos) {
                continue;
            }

            count++;
            ImGui::PushID(tex.fullPath.c_str());
            bool isSelected = (textureSlotAssetId == tex.assetId || textureSlotValue == tex.filename || textureSlotValue == tex.relPath || textureSlotValue == tex.virtualPath);

            std::string itemText = "" + tex.filename;
            if (!tex.virtualPath.empty()) {
                itemText += "  (" + tex.virtualPath + ")";
            }

            if (ImGui::Selectable(itemText.c_str(), isSelected)) {
                textureSlotValue = tex.filename;
                textureSlotAssetId = tex.assetId;
                changed = true;
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Virtual: %s\nAssetID: %s\nDisk: %s",
                    tex.virtualPath.c_str(), idStr.c_str(), tex.relPath.c_str());
            }
            ImGui::PopID();
        }

        if (count == 0 && !availableTextures.empty()) {
            ImGui::TextDisabled("No textures match \"%s\"", currentSearch.c_str());
        } else if (availableTextures.empty()) {
            ImGui::TextDisabled("No textures registered in Asset Registry");
        }

        ImGui::EndCombo();
    }

    // Drag & Drop Target for Texture Slot (dev.md Section 34)
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
            char assetIdBuf[40] = {};
            memcpy(assetIdBuf, payload->Data, std::min((size_t)payload->DataSize, sizeof(assetIdBuf) - 1));
            AssetID droppedId = AssetID::FromString(assetIdBuf);
            AssetMetadata* meta = AssetRegistry::Get().Find(droppedId);
            if (meta && meta->type == AssetType::Texture) {
                textureSlotAssetId = droppedId;
                textureSlotValue = std::filesystem::path(meta->sourcePath).filename().string();
                changed = true;
                AddLog("LogMaterial", "Assigned Texture '" + meta->objectName + "' (" + meta->id.ToString().substr(0, 8) + ") to slot", 2);
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    bool hasTex = textureSlotAssetId.IsValid() || !textureSlotValue.empty();
    if (!hasTex) ImGui::BeginDisabled();
    if (ImGui::Button("ÃƒÆ’Ã¢â‚¬â€", ImVec2(clearBtnW, 0))) {
        textureSlotValue = "";
        textureSlotAssetId = AssetID::Null();
        changed = true;
    }
    if (!hasTex) ImGui::EndDisabled();
    if (ImGui::IsItemHovered() && hasTex) {
        ImGui::SetTooltip("Clear texture slot");
    }

    if (hasTex && !textureSlotValue.empty()) {
        auto info = Eunoia::TextureStreamingSystem::Get().GetOrProbeTextureInfo(textureSlotValue);
        if (info.isValid) {
            const char* fmtName = DirectX::GetFormatName(info.format);
            std::string fmtStr = (fmtName && strlen(fmtName) > 0) ? fmtName : "DXGI_FORMAT_R8G8B8A8_UNORM";
            if (info.isGpuReady) {
                ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "   ÃƒÂ¢Ã¢â‚¬Â Ã‚Â³ %dx%d | %s | %u Mip%s",
                    info.width, info.height, fmtStr.c_str(), info.mipLevels, info.mipLevels > 1 ? "s" : "");
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "   ÃƒÂ¢Ã¢â‚¬Â Ã‚Â³ %dx%d | %s | %u Mip%s  [Streaming...]",
                    info.width, info.height, fmtStr.c_str(), info.mipLevels, info.mipLevels > 1 ? "s" : "");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Bake DDS##BakeSlotBtn")) {
                std::string resolved = TextureManager::Get().ResolvePath(textureSlotValue);
                if (!resolved.empty()) {
                    TextureUsageSlot slot = TextureUsageSlot::BaseColor;
                    std::string lbl = label ? label : "";
                    if (lbl.find("Normal") != std::string::npos) slot = TextureUsageSlot::Normal;
                    else if (lbl.find("Rough") != std::string::npos) slot = TextureUsageSlot::Roughness;
                    else if (lbl.find("Metal") != std::string::npos) slot = TextureUsageSlot::Metallic;
                    else if (lbl.find("Occlusion") != std::string::npos || lbl.find("AO") != std::string::npos) slot = TextureUsageSlot::AO;
                    else if (lbl.find("Emission") != std::string::npos) slot = TextureUsageSlot::Emission;
                    else if (lbl.find("Opacity") != std::string::npos) slot = TextureUsageSlot::Opacity;

                    CachedTexture* ct = TextureManager::Get().GetTexture(textureSlotValue, slot);
                    if (ct && ct->valid) {
                        std::filesystem::path p(resolved);
                        std::string ddsPath = p.replace_extension(".dds").string();
                        if (ct->scratchImage.GetImageCount() > 0) {
                            DirectX::SaveToDDSFile(ct->scratchImage.GetImages(), ct->scratchImage.GetImageCount(), ct->scratchImage.GetMetadata(), ddsPath.c_str());
                        } else if (!ct->data.empty()) {
                            DirectX::Image baseImg;
                            baseImg.width = ct->width; baseImg.height = ct->height;
                            baseImg.format = DXGI_FORMAT_R8G8B8A8_UNORM;
                            baseImg.rowPitch = ct->width * 4;
                            baseImg.slicePitch = ct->width * ct->height * 4;
                            baseImg.pixels = ct->data.data();
                            DirectX::ScratchImage mips;
                            DirectX::GenerateMipMaps(baseImg, DirectX::TEX_FILTER_BOX | DirectX::TEX_FILTER_SRGB, 0, mips);
                            DirectX::SaveToDDSFile(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), ddsPath.c_str());
                        }
                        AddLog("DirectXTex", "Exported optimized DDS texture with mipmaps: " + ddsPath, 0);
                    }
                }
            }
        } else {
            ImGui::TextDisabled("   ÃƒÂ¢Ã¢â‚¬Â Ã‚Â³ [File Not Found or Unreadable]");
        }
    }

    ImGui::Spacing();
    ImGui::PopID();
    return changed;
}

void EngineUI::ApplyMaterialToActorAndChildren(Scene& scene, GameObject* rootObj, const MaterialAsset& ma) {
    if (!rootObj) return;

    int childCount = 0;
    std::function<void(GameObject*)> applyRec = [&](GameObject* cur) {
        if (!cur) return;
        if (!cur->isLight && cur->type != PrimitiveType::Empty) {
            cur->materialName = ma.name;
            cur->color = ma.baseColor;
            cur->metallic = ma.metallic;
            cur->roughness = ma.roughness;
            cur->normalStrength = ma.normalStrength;
            cur->specular = ma.specular;
            cur->emissiveColor = ma.emissiveColor;
            cur->emissiveIntensity = ma.emissiveIntensity;
            cur->shadingModel = ma.shadingModel;
            cur->blendMode = ma.blendMode;
            cur->twoSided = ma.twoSided;
            cur->castShadows = ma.castShadows;
            cur->receiveShadows = ma.receiveShadows;
            cur->opacity = ma.opacity;
            cur->opacityMaskClipValue = ma.opacityMaskClipValue;
            cur->baseColorTexture = ma.baseColorTexture;
            cur->normalTexture = ma.normalTexture;
            cur->roughnessTexture = ma.roughnessTexture;
            cur->metallicTexture = ma.metallicTexture;
            cur->aoTexture = ma.aoTexture;
            cur->emissionTexture = ma.emissionTexture;
            cur->opacityTexture = ma.opacityTexture;
            cur->uvScale = ma.uvScale;
            cur->normalMapYFlip = ma.normalMapYFlip;
            cur->metallicChannel = ma.metallicChannel;
            cur->roughnessChannel = ma.roughnessChannel;
            cur->aoChannel = ma.aoChannel;
            cur->materialDebugMode = ma.materialDebugMode;
            cur->disableStreaming = ma.disableStreaming;
            if (cur != rootObj) {
                childCount++;
            }
        } else if (cur->type == PrimitiveType::Empty) {
            cur->materialName = ma.name;
        }

        for (int cId : cur->childIds) {
            GameObject* child = scene.FindObject(cId);
            if (child) {
                applyRec(child);
            }
        }
    };

    applyRec(rootObj);

    if (childCount > 0) {
        AddLog("LogMaterial", "Assigned material '" + ma.name + "' to Actor '" + rootObj->name + "' and " + std::to_string(childCount) + " child actor(s)", 2);
    } else {
        AddLog("LogMaterial", "Assigned material '" + ma.name + "' to Actor '" + rootObj->name + "'", 2);
    }
}

bool EngineUI::LoadMaterialFile(const std::string& path, MaterialAsset& outMat) {
    std::string actualPath = path;
    std::error_code ec;
    if (!std::filesystem::exists(actualPath, ec) || std::filesystem::file_size(actualPath, ec) == 0) {
        std::filesystem::path resolved = AssetManager::Get().ResolvePhysicalOrCookedPath(path);
        if (!resolved.empty() && std::filesystem::exists(resolved, ec) && std::filesystem::file_size(resolved, ec) > 0) {
            actualPath = resolved.string();
        }
    }
    std::ifstream file(actualPath);
    if (!file.is_open()) return false;
    outMat.filePath = path;
    outMat.name = std::filesystem::path(path).stem().string();

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        while (!key.empty() && isspace((unsigned char)key.back())) key.pop_back();
        while (!key.empty() && isspace((unsigned char)key.front())) key.erase(key.begin());
        while (!val.empty() && isspace((unsigned char)val.back())) val.pop_back();
        while (!val.empty() && isspace((unsigned char)val.front())) val.erase(val.begin());

        if (key == "assetId") outMat.assetId = AssetID::FromString(val);
        else if (key == "virtualPath") outMat.virtualPath = val;
        else if (key == "name") outMat.name = val;
        else if (key == "baseColor") {
            std::stringstream ss(val);
            ss >> outMat.baseColor.r >> outMat.baseColor.g >> outMat.baseColor.b;
        }
        else if (key == "metallic" || key == "metalness") {
            try { outMat.metallic = std::stof(val); } catch (...) {}
        }
        else if (key == "roughness") {
            try { outMat.roughness = std::stof(val); } catch (...) {}
        }
        else if (key == "normalStrength" || key == "normalMapStrength") {
            try { outMat.normalStrength = std::stof(val); } catch (...) {}
        }
        else if (key == "specular" || key == "reflectance") {
            try { outMat.specular = std::stof(val); } catch (...) {}
        }
        else if (key == "emissiveColor") {
            std::stringstream ss(val);
            ss >> outMat.emissiveColor.r >> outMat.emissiveColor.g >> outMat.emissiveColor.b;
        }
        else if (key == "emissiveIntensity" || key == "emissionPower") {
            try { outMat.emissiveIntensity = std::stof(val); } catch (...) {}
        }
        else if (key == "shadingModel" || key == "shader" || key == "shaderType") {
            try { outMat.shadingModel = std::stoi(val); } catch (...) {}
        }
        else if (key == "blendMode" || key == "userBlendMode") {
            try { outMat.blendMode = std::stoi(val); } catch (...) {}
        }
        else if (key == "twoSided" || key == "doubleSided") {
            outMat.twoSided = (val == "1" || val == "true");
        }
        else if (key == "castShadows" || key == "castShadow") {
            outMat.castShadows = (val == "1" || val == "true");
        }
        else if (key == "receiveShadows" || key == "receiveShadow") {
            outMat.receiveShadows = (val == "1" || val == "true");
        }
        else if (key == "uvScale" || key == "uvscale" || key == "tiling" || key == "texMulAdd") {
            std::stringstream ss(val);
            ss >> outMat.uvScale.x >> outMat.uvScale.y;
        }
        else if (key == "opacity") {
            try { outMat.opacity = std::stof(val); } catch (...) {}
        }
        else if (key == "opacityMaskClipValue" || key == "alphaRef") {
            try { outMat.opacityMaskClipValue = std::stof(val); } catch (...) {}
        }
        else if (key == "normalMapYFlip") {
            outMat.normalMapYFlip = (val == "1" || val == "true");
        }
        else if (key == "disableStreaming") {
            outMat.disableStreaming = (val == "1" || val == "true");
        }
        else if (key == "metallicChannel") {
            try { outMat.metallicChannel = std::stoi(val); } catch (...) {}
        }
        else if (key == "roughnessChannel") {
            try { outMat.roughnessChannel = std::stoi(val); } catch (...) {}
        }
        else if (key == "aoChannel") {
            try { outMat.aoChannel = std::stoi(val); } catch (...) {}
        }
        else if (key == "materialDebugMode") {
            try { outMat.materialDebugMode = std::stoi(val); } catch (...) {}
        }
        else if (key == "baseColorAssetId") outMat.baseColorAssetId = AssetID::FromString(val);
        else if (key == "normalAssetId") outMat.normalAssetId = AssetID::FromString(val);
        else if (key == "roughnessAssetId") outMat.roughnessAssetId = AssetID::FromString(val);
        else if (key == "metallicAssetId") outMat.metallicAssetId = AssetID::FromString(val);
        else if (key == "aoAssetId") outMat.aoAssetId = AssetID::FromString(val);
        else if (key == "emissionAssetId") outMat.emissionAssetId = AssetID::FromString(val);
        else if (key == "opacityAssetId") outMat.opacityAssetId = AssetID::FromString(val);
        else if (key == "baseColorTexture" || key == "baseColorMap") outMat.baseColorTexture = val;
        else if (key == "normalTexture" || key == "normalMap") outMat.normalTexture = val;
        else if (key == "roughnessTexture" || key == "roughnessMap") outMat.roughnessTexture = val;
        else if (key == "metallicTexture" || key == "metalnessMap") outMat.metallicTexture = val;
        else if (key == "aoTexture" || key == "occlusionMap") outMat.aoTexture = val;
        else if (key == "emissionTexture" || key == "emissiveMap") outMat.emissionTexture = val;
        else if (key == "opacityTexture" || key == "transparencyMap") outMat.opacityTexture = val;
        else if (key == "surfaceMap" || key == "surfacemap" || key == "surfaceTexture") {
            outMat.roughnessTexture = val;
            outMat.metallicTexture = val;
            outMat.aoTexture = val;
            outMat.aoChannel = 0;
            outMat.roughnessChannel = 1;
            outMat.metallicChannel = 2;
        }
    }

    // Resolve AssetID if not present
    if (!outMat.assetId.IsValid()) {
        AssetMetadata* meta = AssetRegistry::Get().FindByNameOrPath(path);
        if (meta) {
            outMat.assetId = meta->id;
            outMat.virtualPath = meta->virtualPath;
        }
    }

    // Resolve texture AssetIDs if missing
    auto resolveTexId = [&](const std::string& texName, AssetID& outId) {
        if (!outId.IsValid() && !texName.empty() && texName != "none") {
            AssetMetadata* m = AssetRegistry::Get().FindByNameOrPath(texName);
            if (m) outId = m->id;
        }
    };
    resolveTexId(outMat.baseColorTexture, outMat.baseColorAssetId);
    resolveTexId(outMat.normalTexture, outMat.normalAssetId);
    resolveTexId(outMat.roughnessTexture, outMat.roughnessAssetId);
    resolveTexId(outMat.metallicTexture, outMat.metallicAssetId);
    resolveTexId(outMat.aoTexture, outMat.aoAssetId);
    resolveTexId(outMat.emissionTexture, outMat.emissionAssetId);
    resolveTexId(outMat.opacityTexture, outMat.opacityAssetId);

    return true;
}

bool EngineUI::SaveMaterialFile(const std::string& path, const MaterialAsset& mat) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << "# Eunoia-Editor Material Asset\n";
    if (mat.assetId.IsValid()) {
        file << "assetId: " << mat.assetId.ToString() << "\n";
    }
    if (!mat.virtualPath.empty()) {
        file << "virtualPath: " << mat.virtualPath << "\n";
    }
    file << "name: " << mat.name << "\n";
    file << "baseColor: " << mat.baseColor.r << " " << mat.baseColor.g << " " << mat.baseColor.b << "\n";
    file << "metallic: " << mat.metallic << "\n";
    file << "roughness: " << mat.roughness << "\n";
    file << "normalStrength: " << mat.normalStrength << "\n";
    file << "specular: " << mat.specular << "\n";
    file << "emissiveColor: " << mat.emissiveColor.r << " " << mat.emissiveColor.g << " " << mat.emissiveColor.b << "\n";
    file << "emissiveIntensity: " << mat.emissiveIntensity << "\n";
    file << "shadingModel: " << mat.shadingModel << "\n";
    file << "blendMode: " << mat.blendMode << "\n";
    file << "twoSided: " << (mat.twoSided ? 1 : 0) << "\n";
    file << "castShadows: " << (mat.castShadows ? 1 : 0) << "\n";
    file << "receiveShadows: " << (mat.receiveShadows ? 1 : 0) << "\n";
    file << "opacity: " << mat.opacity << "\n";
    file << "opacityMaskClipValue: " << mat.opacityMaskClipValue << "\n";
    file << "uvScale: " << mat.uvScale.x << " " << mat.uvScale.y << "\n";
    file << "normalMapYFlip: " << (mat.normalMapYFlip ? 1 : 0) << "\n";
    file << "disableStreaming: " << (mat.disableStreaming ? 1 : 0) << "\n";
    file << "metallicChannel: " << mat.metallicChannel << "\n";
    file << "roughnessChannel: " << mat.roughnessChannel << "\n";
    file << "aoChannel: " << mat.aoChannel << "\n";
    file << "materialDebugMode: " << mat.materialDebugMode << "\n";
    if (mat.baseColorAssetId.IsValid()) file << "baseColorAssetId: " << mat.baseColorAssetId.ToString() << "\n";
    file << "baseColorTexture: " << mat.baseColorTexture << "\n";
    if (mat.normalAssetId.IsValid()) file << "normalAssetId: " << mat.normalAssetId.ToString() << "\n";
    file << "normalTexture: " << mat.normalTexture << "\n";
    if (mat.roughnessAssetId.IsValid()) file << "roughnessAssetId: " << mat.roughnessAssetId.ToString() << "\n";
    file << "roughnessTexture: " << mat.roughnessTexture << "\n";
    if (mat.metallicAssetId.IsValid()) file << "metallicAssetId: " << mat.metallicAssetId.ToString() << "\n";
    file << "metallicTexture: " << mat.metallicTexture << "\n";
    if (mat.aoAssetId.IsValid()) file << "aoAssetId: " << mat.aoAssetId.ToString() << "\n";
    file << "aoTexture: " << mat.aoTexture << "\n";
    if (mat.emissionAssetId.IsValid()) file << "emissionAssetId: " << mat.emissionAssetId.ToString() << "\n";
    file << "emissionTexture: " << mat.emissionTexture << "\n";
    if (mat.opacityAssetId.IsValid()) file << "opacityAssetId: " << mat.opacityAssetId.ToString() << "\n";
    file << "opacityTexture: " << mat.opacityTexture << "\n";

    // Update Asset Registry (dev.md Section 7, 13)
    if (mat.assetId.IsValid()) {
        AssetMetadata* meta = AssetRegistry::Get().Find(mat.assetId);
        if (meta) {
            meta->dependencies.clear();
            if (mat.baseColorAssetId.IsValid()) AssetRegistry::Get().AddDependency(mat.assetId, mat.baseColorAssetId);
            if (mat.normalAssetId.IsValid()) AssetRegistry::Get().AddDependency(mat.assetId, mat.normalAssetId);
            if (mat.roughnessAssetId.IsValid()) AssetRegistry::Get().AddDependency(mat.assetId, mat.roughnessAssetId);
            if (mat.metallicAssetId.IsValid()) AssetRegistry::Get().AddDependency(mat.assetId, mat.metallicAssetId);
            if (mat.aoAssetId.IsValid()) AssetRegistry::Get().AddDependency(mat.assetId, mat.aoAssetId);
            if (mat.emissionAssetId.IsValid()) AssetRegistry::Get().AddDependency(mat.assetId, mat.emissionAssetId);
            if (mat.opacityAssetId.IsValid()) AssetRegistry::Get().AddDependency(mat.assetId, mat.opacityAssetId);
        }
    }
    return true;
}

void EngineUI::PropagateMaterialChangesToScene(Scene& scene, const MaterialAsset& mat) {
    std::string matStem = !mat.filePath.empty() ? std::filesystem::path(mat.filePath).stem().string() : mat.name;
    bool anyChanged = false;

    for (auto& obj : scene.objects) {
        bool objMatched = (!mat.name.empty() && obj.materialName == mat.name) ||
                           (!mat.filePath.empty() && obj.materialName == mat.filePath) ||
                           (!matStem.empty() && obj.materialName == matStem);

        if (objMatched) {
            obj.color = mat.baseColor;
            obj.metallic = mat.metallic;
            obj.roughness = mat.roughness;
            obj.normalStrength = mat.normalStrength;
            obj.specular = mat.specular;
            obj.emissiveColor = mat.emissiveColor;
            obj.emissiveIntensity = mat.emissiveIntensity;
            obj.shadingModel = mat.shadingModel;
            obj.blendMode = mat.blendMode;
            obj.twoSided = mat.twoSided;
            obj.castShadows = mat.castShadows;
            obj.receiveShadows = mat.receiveShadows;
            obj.opacity = mat.opacity;
            obj.opacityMaskClipValue = mat.opacityMaskClipValue;
            obj.baseColorTexture = mat.baseColorTexture;
            obj.normalTexture = mat.normalTexture;
            obj.roughnessTexture = mat.roughnessTexture;
            obj.metallicTexture = mat.metallicTexture;
            obj.aoTexture = mat.aoTexture;
            obj.emissionTexture = mat.emissionTexture;
            obj.opacityTexture = mat.opacityTexture;
            obj.uvScale = mat.uvScale;
            obj.normalMapYFlip = mat.normalMapYFlip;
            obj.metallicChannel = mat.metallicChannel;
            obj.roughnessChannel = mat.roughnessChannel;
            obj.aoChannel = mat.aoChannel;
            obj.materialDebugMode = mat.materialDebugMode;
            obj.disableStreaming = mat.disableStreaming;
            anyChanged = true;
        }

        if (obj.isTerrain || obj.type == PrimitiveType::Terrain) {
            bool terrainLayerMatched = false;
            for (size_t l = 0; l < obj.terrain.materialLayers.size(); ++l) {
                auto& layer = obj.terrain.materialLayers[l];
                std::string layerStem = !layer.path.empty() ? std::filesystem::path(layer.path).stem().string() : layer.name;
                bool layerMatch = (!mat.name.empty() && layer.name == mat.name) ||
                                   (!mat.filePath.empty() && layer.path == mat.filePath) ||
                                   (!matStem.empty() && layer.name == matStem) ||
                                   (!matStem.empty() && layerStem == matStem) ||
                                   (l == 0 && objMatched);

                if (layerMatch) {
                    layer.baseColor = mat.baseColor;
                    layer.metallic = mat.metallic;
                    layer.roughness = mat.roughness;
                    layer.normalStrength = mat.normalStrength;
                    layer.specular = mat.specular;
                    layer.uvScale = mat.uvScale;
                    layer.baseColorTexture = mat.baseColorTexture;
                    layer.normalTexture = mat.normalTexture;
                    layer.roughnessTexture = mat.roughnessTexture;
                    layer.metallicTexture = mat.metallicTexture;
                    layer.aoTexture = mat.aoTexture;
                    terrainLayerMatched = true;

                    if (l == 0) {
                        obj.color = mat.baseColor;
                        obj.metallic = mat.metallic;
                        obj.roughness = mat.roughness;
                        obj.normalStrength = mat.normalStrength;
                        obj.specular = mat.specular;
                        obj.uvScale = mat.uvScale;
                        obj.baseColorTexture = mat.baseColorTexture;
                        obj.normalTexture = mat.normalTexture;
                        obj.roughnessTexture = mat.roughnessTexture;
                        obj.metallicTexture = mat.metallicTexture;
                        obj.aoTexture = mat.aoTexture;
                    }
                }
            }
            if (terrainLayerMatched) {
                obj.terrain.isDirty = true;
                anyChanged = true;
            }
        }
    }

    if (anyChanged) {
        RequestViewportRedraw(5);
    }
}



void EngineUI::RenderBehavioursSection(Scene& scene, GameObject* obj) {
    if (!obj) return;

    ImGui::Spacing();
    ImGui::Separator();
    char headerLabel[128];
    snprintf(headerLabel, sizeof(headerLabel), "Behaviour Scripts (%zu)###BehavioursHeader", obj->behaviours.size());
    if (ImGui::CollapsingHeader(headerLabel, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();

        // Button to add behaviour script
        if (ImGui::Button("+ Add Behaviour Script", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
            showAddBehaviourPopup = true;
            behaviourSearchBuf[0] = '\0';
        }

        if (!obj->behaviours.empty()) {
            bool isPlayActive = scene.isPlayMode;
            if (!isPlayActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.55f, 0.25f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.70f, 0.32f, 1.0f));
                if (ImGui::Button("ÃƒÂ¢Ã¢â‚¬â€œÃ‚Â¶ Play Mode (Full Screen Fit)", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                    EnterPlayMode(scene, currentCamera);
                }
                ImGui::PopStyleColor(2);
                ImGui::TextDisabled("Runs behaviour scripts in full screen play mode.");
                ImGui::Spacing();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.20f, 0.20f, 1.0f));
                if (ImGui::Button("ÃƒÂ¢Ã‚ÂÃ‚Â¹ Stop Play Mode", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                    ExitPlayMode(scene, currentCamera);
                }
                ImGui::PopStyleColor();
                ImGui::TextColored(ImVec4(0.2f, 0.95f, 0.4f, 1.0f), "ÃƒÂ¢Ã¢â‚¬â€œÃ‚Â¶ Simulation Active");
                ImGui::Spacing();
            }
        }

        if (showAddBehaviourPopup) {
            ImGui::OpenPopup("Add Behaviour Script##Modal");
            showAddBehaviourPopup = false;
        }

        if (ImGui::BeginPopupModal("Add Behaviour Script##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(ImVec4(0.12f, 0.68f, 1.00f, 1.0f), "Select Behaviour Script to Attach:");
            ImGui::SetNextItemWidth(300.0f);
            ImGui::InputTextWithHint("##BehSearch", "Search Behaviour Scripts...", behaviourSearchBuf, sizeof(behaviourSearchBuf));
            ImGui::Separator();

            std::string searchLower = behaviourSearchBuf;
            std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

            // Dynamically scan content folder for user-created .cpp script files so they show in the list
            std::error_code sEc;
            if (std::filesystem::exists(contentRootPath, sEc)) {
                for (auto it = std::filesystem::recursive_directory_iterator(contentRootPath, std::filesystem::directory_options::skip_permission_denied, sEc);
                     !sEc && it != std::filesystem::recursive_directory_iterator();
                     it.increment(sEc)) {
                    if (!it->is_directory(sEc)) {
                        if (it->path().extension() == ".cpp") {
                            std::string pStr = it->path().string();
                            std::string pLower = pStr;
                            std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::tolower);
                            if (pLower.find(".assetmeta") == std::string::npos && pLower.find(".meta") == std::string::npos && pLower.find(".internal") == std::string::npos) {
                                std::string stem = it->path().stem().string();
                                if (stem != "Cube" && stem != "EngineUI" && stem != "EunoiaBehaviour" && stem != "TextureManager" && stem != "AssetRegistry" && stem != "AssetManager") {
                                    BehaviourRegistry::Get().RegisterScriptFile(stem, it->path().string());
                                }
                            }
                        }
                    }
                }
            }

            std::error_code cNormEc;
            std::filesystem::path curContentNorm = std::filesystem::weakly_canonical(contentRootPath, cNormEc);
            std::string curContentStr = curContentNorm.generic_string();

            const auto& allBehaviors = BehaviourRegistry::Get().GetAll();
            int matchCount = 0;
            for (const auto& pair : allBehaviors) {
                const auto& info = pair.second;

                // Only show behaviours belonging to this project (or native engine behaviours)
                if (!info.isNative && !info.sourceCpp.empty()) {
                    std::error_code cEc;
                    std::filesystem::path srcNorm = std::filesystem::weakly_canonical(info.sourceCpp, cEc);
                    std::string srcStr = srcNorm.generic_string();
                    if (!curContentStr.empty() && srcStr.rfind(curContentStr, 0) != 0) {
                        continue; // Belongs to a different project! Skip it!
                    }
                    if (srcStr.find(".internal") != std::string::npos) {
                        continue; // Hidden internal Blueprint script! Skip it!
                    }
                }

                std::string nameLower = info.displayName;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                std::string classLower = info.className;
                std::transform(classLower.begin(), classLower.end(), classLower.begin(), ::tolower);

                if (!searchLower.empty() && nameLower.find(searchLower) == std::string::npos && classLower.find(searchLower) == std::string::npos) {
                    continue;
                }

                matchCount++;
                std::string itemText = info.displayName;
                if (ImGui::Selectable(itemText.c_str(), false)) {
                    UndoManager::Get().RecordSnapshot(scene, "Add Behaviour: " + info.className);
                    auto newB = BehaviourRegistry::Get().Create(info.className);
                    if (newB) {
                        newB->SetScene(&scene);
                        obj->AddBehaviour(std::move(newB));
                        obj->behaviours.back()->ResolveReferences(scene);
                        AddLog("LogBehaviour", "Attached " + info.displayName + " to " + obj->name, 2);
                    }
                    ImGui::CloseCurrentPopup();
                }
                if (!info.description.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", info.description.c_str());
                }
            }

            if (matchCount == 0) {
                ImGui::TextDisabled("No behaviours match search query.");
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.0f, 0.82f, 1.0f, 1.0f), "Node Behaviours (.enb):");
            std::error_code nbEc;
            if (std::filesystem::exists(contentRootPath, nbEc)) {
                for (auto it = std::filesystem::recursive_directory_iterator(contentRootPath, std::filesystem::directory_options::skip_permission_denied, nbEc);
                     !nbEc && it != std::filesystem::recursive_directory_iterator();
                     it.increment(nbEc)) {
                    if (!it->is_directory(nbEc) && it->path().extension() == ".enb") {
                        std::string stem = it->path().stem().string();
                        std::string stemLower = stem;
                        std::transform(stemLower.begin(), stemLower.end(), stemLower.begin(), ::tolower);
                        if (!searchLower.empty() && stemLower.find(searchLower) == std::string::npos) continue;

                        std::string label = "[NB] " + stem;
                        if (ImGui::Selectable(label.c_str(), false)) {
                            UndoManager::Get().RecordSnapshot(scene, "Add Node Behaviour: " + stem);
                            auto nbInstance = std::make_unique<Eunoia::NodeBehaviour::NodeBehaviourInstance>(it->path().string());
                            nbInstance->SetScene(&scene);
                            obj->AddBehaviour(std::move(nbInstance));
                            obj->behaviours.back()->ResolveReferences(scene);
                            AddLog("LogBehaviour", "Attached Node Behaviour " + stem + " to " + obj->name, 2);
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Create New Node Behaviour (.enb)...", ImVec2(240, 0))) {
                showNewNodeBehaviourPopup = true;
                snprintf(newNodeBehaviourNameBuf, sizeof(newNodeBehaviourNameBuf), "NB_NewBehaviour");
                popupTargetVirtualDir = currentVirtualDir;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Create New Behaviour Script...", ImVec2(240, 0))) {
                showNewBehaviourPopup = true;
                std::string defName = "NewBehaviour";
                int idx = 1;
                while (IsBehaviourNameAlreadyExists("NewBehaviour_" + std::to_string(idx))) idx++;
                defName = "NewBehaviour_" + std::to_string(idx);
                snprintf(newBehaviourNameBuf, sizeof(newBehaviourNameBuf), "%s", defName.c_str());
                popupTargetVirtualDir = currentVirtualDir;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Render each attached behaviour
        int removeIdx = -1;
        int moveUpIdx = -1;
        int moveDownIdx = -1;

        for (size_t i = 0; i < obj->behaviours.size(); ++i) {
            auto& b = obj->behaviours[i];
            if (!b) continue;

            // Ensure user script behaviours are upgraded to dynamic script reflection with latest properties from .cpp
            const auto& allRegistry = BehaviourRegistry::Get().GetAll();
            auto regIt = allRegistry.find(b->GetClassName());
            if (regIt != allRegistry.end() && !regIt->second.sourceCpp.empty()) {
                if (dynamic_cast<DynamicScriptBehaviour*>(b.get()) == nullptr || b->GetProperties().empty()) {
                    auto dynB = std::make_unique<DynamicScriptBehaviour>(b->GetClassName(), regIt->second.sourceCpp);
                    dynB->SetScene(&scene);
                    dynB->SetOwner(obj);
                    dynB->SetEnabled(b->IsEnabled());
                    dynB->CopyPropertiesFrom(*b);
                    dynB->ResolveReferences(scene);
                    b = std::move(dynB);
                } else {
                    b->SetScene(&scene);
                    b->RefreshPropertiesFromSource();
                    b->ResolveReferences(scene);
                }
            } else {
                b->SetScene(&scene);
                b->RefreshPropertiesFromSource();
                b->ResolveReferences(scene);
            }

            ImGui::PushID((int)i);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
            ImGui::BeginChild("BehaviourCard", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

            // Header line: Reorder, Class Title, Enable checkbox, Reset & Remove buttons
            if (i > 0) {
                if (ImGui::SmallButton("ÃƒÂ¢Ã¢â‚¬â€œÃ‚Â²")) moveUpIdx = (int)i;
                ImGui::SameLine();
            }
            if (i + 1 < obj->behaviours.size()) {
                if (ImGui::SmallButton("ÃƒÂ¢Ã¢â‚¬â€œÃ‚Â¼")) moveDownIdx = (int)i;
                ImGui::SameLine();
            }

            bool isEnabled = b->IsEnabled();
            if (ImGui::Checkbox("##Enabled", &isEnabled)) {
                UndoManager::Get().RecordSnapshot(scene, "Toggle Behaviour Enabled");
                b->SetEnabled(isEnabled);
            }
            ImGui::SameLine();

            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", b->GetDisplayName().c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", b->GetClassName().c_str());

            if (regIt != allRegistry.end() && !regIt->second.sourceCpp.empty()) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.75f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.85f, 1.0f));
                if (ImGui::SmallButton("Edit in VS Code")) {
                    OpenScriptInCodeEditor(regIt->second.sourceCpp);
                }
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Open script in VS Code: %s", regIt->second.sourceCpp.c_str());
                }
            }

            auto nbInst = dynamic_cast<Eunoia::NodeBehaviour::NodeBehaviourInstance*>(b.get());
            if (nbInst) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.45f, 0.75f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.55f, 0.90f, 1.0f));
                if (ImGui::SmallButton("Edit Graph")) {
                    OpenNodeBehaviourEditor(nbInst->GetAssetPath());
                }
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Open Node Behaviour visual scripting graph: %s", nbInst->GetAssetPath().c_str());
                }
            }

            // Right-aligned Reset and Remove buttons
            float availW = ImGui::GetContentRegionAvail().x;
            if (availW > 140.0f) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
                if (ImGui::SmallButton("Reset")) {
                    UndoManager::Get().RecordSnapshot(scene, "Reset Behaviour to Defaults");
                    b->SetScene(&scene);
                    b->ResetToDefaults();
                    b->ResolveReferences(scene);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Reset all properties and references to default values as defined in code.");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    removeIdx = (int)i;
                }
            } else if (availW > 70.0f) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 75.0f);
                if (ImGui::SmallButton("Remove")) {
                    removeIdx = (int)i;
                }
            }

            ImGui::Separator();

            // Properties list
            auto& props = b->GetProperties();
            if (props.empty()) {
                ImGui::TextDisabled("  (No exposed properties)");
            }
            for (size_t pi = 0; pi < props.size(); ++pi) {
                auto& prop = props[pi];
                ImGui::PushID((int)pi);

                switch (prop.type) {
                    case BehaviourPropertyType::Bool: {
                        if (prop.dataPtr) {
                            bool* val = reinterpret_cast<bool*>(prop.dataPtr);
                            ImGui::Checkbox(prop.displayName.c_str(), val);
                        }
                        break;
                    }
                    case BehaviourPropertyType::Int: {
                        if (prop.dataPtr) {
                            int* val = reinterpret_cast<int*>(prop.dataPtr);
                            if (prop.hasRange) {
                                ImGui::SliderInt(prop.displayName.c_str(), val, (int)prop.minVal, (int)prop.maxVal);
                            } else {
                                ImGui::DragInt(prop.displayName.c_str(), val);
                            }
                        }
                        break;
                    }
                    case BehaviourPropertyType::Float: {
                        if (prop.dataPtr) {
                            float* val = reinterpret_cast<float*>(prop.dataPtr);
                            if (prop.hasRange) {
                                ImGui::SliderFloat(prop.displayName.c_str(), val, prop.minVal, prop.maxVal);
                            } else {
                                ImGui::DragFloat(prop.displayName.c_str(), val, 0.1f);
                            }
                        }
                        break;
                    }
                    case BehaviourPropertyType::Double: {
                        if (prop.dataPtr) {
                            double* val = reinterpret_cast<double*>(prop.dataPtr);
                            float tempF = (float)*val;
                            if (ImGui::DragFloat(prop.displayName.c_str(), &tempF, 0.1f)) {
                                *val = (double)tempF;
                            }
                        }
                        break;
                    }
                    case BehaviourPropertyType::String: {
                        if (prop.dataPtr) {
                            std::string* val = reinterpret_cast<std::string*>(prop.dataPtr);
                            char strBuf[256] = {};
                            strncpy(strBuf, val->c_str(), sizeof(strBuf) - 1);
                            if (ImGui::InputText(prop.displayName.c_str(), strBuf, sizeof(strBuf))) {
                                *val = strBuf;
                            }
                        }
                        break;
                    }
                    case BehaviourPropertyType::Vec2: {
                        if (prop.dataPtr) {
                            glm::vec2* val = reinterpret_cast<glm::vec2*>(prop.dataPtr);
                            ImGui::DragFloat2(prop.displayName.c_str(), &val->x, 0.1f);
                        }
                        break;
                    }
                    case BehaviourPropertyType::Vec3: {
                        if (prop.dataPtr) {
                            glm::vec3* val = reinterpret_cast<glm::vec3*>(prop.dataPtr);
                            ImGui::DragFloat3(prop.displayName.c_str(), &val->x, 0.1f);
                        }
                        break;
                    }
                    case BehaviourPropertyType::Vec4: {
                        if (prop.dataPtr) {
                            glm::vec4* val = reinterpret_cast<glm::vec4*>(prop.dataPtr);
                            ImGui::DragFloat4(prop.displayName.c_str(), &val->x, 0.1f);
                        }
                        break;
                    }
                    case BehaviourPropertyType::Color3: {
                        if (prop.dataPtr) {
                            glm::vec3* val = reinterpret_cast<glm::vec3*>(prop.dataPtr);
                            ImGui::ColorEdit3(prop.displayName.c_str(), &val->x);
                        }
                        break;
                    }
                    case BehaviourPropertyType::ObjectRef: {
                        if (prop.refType == ObjectRefType::UserInterface) {
                            ImGui::Text("%s", prop.displayName.c_str());

                            std::string preview = "None (Select User Interface)";
                            if (!prop.assetPath.empty()) {
                                std::filesystem::path p(prop.assetPath);
                                preview = p.stem().string();
                            }

                            if (prop.isMissing) {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
                            }

                            std::string comboId = "##UIAssetCombo_" + std::to_string(obj->id) + "_" + prop.name;
                            if (ImGui::BeginCombo(comboId.c_str(), preview.c_str())) {
                                if (ImGui::Selectable("(None)", prop.assetPath.empty())) {
                                    UndoManager::Get().RecordSnapshot(scene, "Clear UI Reference: " + prop.name);
                                    prop.assetPath = "";
                                    b->ResolveReferences(scene);
                                }
                                ImGui::Separator();

                                // Scan project content directory for all .eui files
                                if (!contentRootPath.empty() && std::filesystem::exists(contentRootPath)) {
                                    std::error_code rec;
                                    for (auto it = std::filesystem::recursive_directory_iterator(contentRootPath, std::filesystem::directory_options::skip_permission_denied, rec);
                                         it != std::filesystem::recursive_directory_iterator(); it.increment(rec)) {
                                        if (rec) break;
                                        if (it->is_regular_file() && it->path().extension() == ".eui") {
                                            std::string uiPath = it->path().string();
                                            std::string uiStem = it->path().stem().string();
                                            bool isSel = (!prop.assetPath.empty() && (prop.assetPath == uiPath || std::filesystem::path(prop.assetPath).filename() == it->path().filename()));
                                            if (ImGui::Selectable(uiStem.c_str(), isSel)) {
                                                UndoManager::Get().RecordSnapshot(scene, "Assign UI: " + prop.name);
                                                prop.assetPath = uiPath;
                                                b->ResolveReferences(scene);
                                            }
                                        }
                                    }
                                }
                                ImGui::EndCombo();
                            }

                            if (prop.isMissing) {
                                ImGui::PopStyleColor();
                            }

                            // Accept Drag & Drop from Content Browser onto this field
                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                                    std::string payloadStr((const char*)payload->Data, payload->DataSize);
                                    std::stringstream pss(payloadStr);
                                    std::string dragItem;
                                    while (std::getline(pss, dragItem, '\n')) {
                                        if (dragItem.rfind("ASSET:", 0) == 0) {
                                            std::string srcRel = dragItem.substr(6);
                                            if (srcRel.find(".eui") != std::string::npos) {
                                                std::filesystem::path fullDisk = contentRootPath / srcRel;
                                                UndoManager::Get().RecordSnapshot(scene, "Drop UI Reference: " + prop.name);
                                                prop.assetPath = fullDisk.string();
                                                b->ResolveReferences(scene);
                                                AddLog("LogBehaviour", "Assigned UI " + fullDisk.stem().string() + " to " + prop.displayName, 2);
                                                break;
                                            }
                                        }
                                    }
                                } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                                    if (payload->DataSize == sizeof(AssetID)) {
                                        AssetID aid = *(const AssetID*)payload->Data;
                                        const AssetMetadata* meta = AssetRegistry::Get().Find(aid);
                                        if (meta && (meta->type == AssetType::UserInterface || meta->type == AssetType::UserInterface3D)) {
                                            std::filesystem::path fullDisk = contentRootPath / meta->sourcePath;
                                            UndoManager::Get().RecordSnapshot(scene, "Drop UI Reference: " + prop.name);
                                            prop.assetPath = fullDisk.string();
                                            b->ResolveReferences(scene);
                                            AddLog("LogBehaviour", "Assigned UI " + meta->objectName + " to " + prop.displayName, 2);
                                        }
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }

                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Select User Interface asset (.eui) or drag & drop from Content Browser onto this field.");
                            }
                            break;
                        }

                        // Object Reference with Typed Dropdown and Drag & Drop (dev.md Section 9, 10, 11, 12, 13)
                        ImGui::Text("%s", prop.displayName.c_str());

                        std::string preview = "None (Select " + std::string(ObjectRefTypeToString(prop.refType)) + ")";
                        if (prop.targetId != -1) {
                            GameObject* targetObj = scene.FindObject(prop.targetId);
                            if (targetObj) {
                                preview = targetObj->name;
                            } else {
                                preview = "Missing Reference (ID: " + std::to_string(prop.targetId) + ")";
                            }
                        }

                        if (prop.isMissing) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
                        }

                        if (ImGui::BeginCombo("##ObjRefCombo", preview.c_str())) {
                            if (ImGui::Selectable("(None)", prop.targetId == -1)) {
                                UndoManager::Get().RecordSnapshot(scene, "Clear Reference: " + prop.name);
                                prop.targetId = -1;
                                b->ResolveReferences(scene);
                            }
                            ImGui::Separator();

                            for (const auto& sceneObj : scene.objects) {
                                // Filter by requested reference type (dev.md Section 10)
                                if (prop.refType == ObjectRefType::Light && !sceneObj.isLight) continue;
                                if (prop.refType == ObjectRefType::Mesh && sceneObj.mesh.indices.empty()) continue;
                                if (prop.refType == ObjectRefType::Camera && !sceneObj.isCamera && sceneObj.type != PrimitiveType::Camera) continue;
                                if (prop.refType == ObjectRefType::AnimationGraph && !sceneObj.hasAnimator) continue;

                                bool isSelected = (prop.targetId == sceneObj.id);
                                std::string itemLabel = sceneObj.name + " (ID: " + std::to_string(sceneObj.id) + ")";
                                if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                                    UndoManager::Get().RecordSnapshot(scene, "Assign Reference: " + prop.name);
                                    prop.targetId = sceneObj.id;
                                    b->ResolveReferences(scene);
                                }
                            }
                            ImGui::EndCombo();
                        }

                        if (prop.isMissing) {
                            ImGui::PopStyleColor();
                        }

                        // Drag & Drop Target from Hierarchy (dev.md Section 12)
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OUTLINER_ACTOR")) {
                                int draggedId = *(const int*)payload->Data;
                                GameObject* draggedObj = scene.FindObject(draggedId);
                                if (draggedObj) {
                                    bool compatible = true;
                                    if (prop.refType == ObjectRefType::Light && !draggedObj->isLight) compatible = false;
                                    if (prop.refType == ObjectRefType::Mesh && draggedObj->mesh.indices.empty()) compatible = false;
                                    if (prop.refType == ObjectRefType::Camera && !draggedObj->isCamera && draggedObj->type != PrimitiveType::Camera) compatible = false;
                                    if (prop.refType == ObjectRefType::AnimationGraph && !draggedObj->hasAnimator) compatible = false;

                                    if (compatible) {
                                        UndoManager::Get().RecordSnapshot(scene, "Drop Reference: " + prop.name);
                                        prop.targetId = draggedId;
                                        b->ResolveReferences(scene);
                                        AddLog("LogBehaviour", "Assigned " + draggedObj->name + " to " + prop.displayName, 2);
                                    } else {
                                        AddLog("LogBehaviour", "Cannot assign " + draggedObj->name + " ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â incompatible type (" + ObjectRefTypeToString(prop.refType) + " expected).", 1);
                                    }
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Select %s or drag & drop actor from Outliner onto this field.", ObjectRefTypeToString(prop.refType));
                        }
                        break;
                    }
                }

                ImGui::PopID();
            }

            ImGui::Spacing();
            if (ImGui::Button("Reset to Code Defaults")) {
                UndoManager::Get().RecordSnapshot(scene, "Reset Behaviour to Defaults");
                b->SetScene(&scene);
                b->ResetToDefaults();
                b->ResolveReferences(scene);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Reset all properties and references to their initial default values as defined in code.");
            }

            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopID();
        }

        if (removeIdx >= 0) {
            UndoManager::Get().RecordSnapshot(scene, "Remove Behaviour");
            obj->RemoveBehaviour((size_t)removeIdx);
        } else if (moveUpIdx > 0) {
            UndoManager::Get().RecordSnapshot(scene, "Reorder Behaviour");
            obj->ReorderBehaviour((size_t)moveUpIdx, (size_t)(moveUpIdx - 1));
        } else if (moveDownIdx >= 0 && moveDownIdx + 1 < (int)obj->behaviours.size()) {
            UndoManager::Get().RecordSnapshot(scene, "Reorder Behaviour");
            obj->ReorderBehaviour((size_t)moveDownIdx, (size_t)(moveDownIdx + 1));
        }
    }
}
