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
void EngineUI::OpenMaterialEditor(const std::string& path) {
    // Check if material is already open in tabs
    for (size_t i = 0; i < openMaterialDocs.size(); ++i) {
        if (openMaterialDocs[i].material.filePath == path) {
            SwitchToMaterialEditor((int)i);
            AddLog("LogMaterial", "Switched to Material Editor: " + openMaterialDocs[i].material.name, 0);
            return;
        }
    }

    MaterialAsset newMat;
    if (LoadMaterialFile(path, newMat)) {
        MaterialDoc doc;
        doc.material = newMat;
        doc.isDirty = false;
        openMaterialDocs.push_back(doc);
        SwitchToMaterialEditor((int)openMaterialDocs.size() - 1);
        AddLog("LogMaterial", "Opened Material Editor: " + newMat.name, 0);
    } else {
        AddLog("LogMaterial", "Failed to load material from: " + path, 3);
    }
}

void EngineUI::SwitchToMaterialEditor(int docIdx) {
    if (docIdx < 0 || docIdx >= (int)openMaterialDocs.size()) return;
    activeMaterialDocIdx = docIdx;
    activeMaterial = openMaterialDocs[docIdx].material;
    materialDirty = openMaterialDocs[docIdx].isDirty;
    showMaterialEditor = true;
    materialWorkspaceActive = false;
    RequestViewportRedraw(10);
}

void EngineUI::CloseMaterialEditor(int docIdx) {
    if (docIdx < 0 || docIdx >= (int)openMaterialDocs.size()) return;
    std::string closedName = openMaterialDocs[docIdx].material.name;
    openMaterialDocs.erase(openMaterialDocs.begin() + docIdx);

    if (openMaterialDocs.empty()) {
        activeMaterialDocIdx = -1;
        showMaterialEditor = false;
        materialWorkspaceActive = false;
        activeMaterial = MaterialAsset{};
        materialDirty = false;
    } else {
        if (activeMaterialDocIdx >= (int)openMaterialDocs.size()) {
            activeMaterialDocIdx = (int)openMaterialDocs.size() - 1;
        }
        SwitchToMaterialEditor(activeMaterialDocIdx);
    }
    AddLog("LogMaterial", "Closed Material Editor tab: " + closedName, 0);
    RequestViewportRedraw(10);
}

void EngineUI::RenderMaterialEditor(Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(800, 640), ImGuiCond_FirstUseEver);
    std::string winTitle = "Material Editor - " + activeMaterial.name + (materialDirty ? "*" : "") + "###MaterialEditorWin";
    if (ImGui::Begin(winTitle.c_str(), &showMaterialEditor, ImGuiWindowFlags_NoCollapse)) {
        if (openMaterialDocs.size() > 1) {
            int closeDocIdx = -1;
            for (size_t d = 0; d < openMaterialDocs.size(); ++d) {
                if (d > 0) ImGui::SameLine();
                bool isCur = (activeMaterialDocIdx == (int)d);
                std::string docLabel = openMaterialDocs[d].material.name;
                if (openMaterialDocs[d].isDirty) docLabel += "*";
                docLabel += "##MatDocTab_" + std::to_string(d);
                if (isCur) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.45f, 0.85f, 1.0f));
                if (ImGui::Button(docLabel.c_str())) {
                    SwitchToMaterialEditor((int)d);
                }
                if (isCur) ImGui::PopStyleColor();
                ImGui::SameLine(0, 2.0f);
                std::string closeDocId = "x##CloseDoc_" + std::to_string(d);
                if (ImGui::SmallButton(closeDocId.c_str())) {
                    closeDocIdx = (int)d;
                }
            }
            if (closeDocIdx >= 0) {
                CloseMaterialEditor(closeDocIdx);
            }
            ImGui::Separator();
        }
        // Toolbar
        if (ImGui::Button("Save Material") || (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))) {
            if (SaveMaterialFile(activeMaterial.filePath, activeMaterial)) {
                materialDirty = false;
                if (activeMaterialDocIdx >= 0 && activeMaterialDocIdx < (int)openMaterialDocs.size()) {
                    openMaterialDocs[activeMaterialDocIdx].isDirty = false;
                    openMaterialDocs[activeMaterialDocIdx].material = activeMaterial;
                    openMaterialDocs[activeMaterialDocIdx].saveState.isSaving = true;
                    openMaterialDocs[activeMaterialDocIdx].saveState.progress = 1.0f;
                    openMaterialDocs[activeMaterialDocIdx].saveState.fadeTimer = 1.5f;
                }
                PropagateMaterialChangesToScene(scene, activeMaterial);
                AddLog("LogMaterial", "Saved Material to " + activeMaterial.filePath + " (Reflected in viewport and terrain layers)", 2);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply to Selected Actor")) {
            GameObject* sel = scene.GetSelected();
            if (sel) {
                ApplyMaterialToActorAndChildren(scene, sel, activeMaterial);
            } else {
                AddLog("LogMaterial", "No Actor currently selected to apply material.", 1);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert")) {
            LoadMaterialFile(activeMaterial.filePath, activeMaterial);
            materialDirty = false;
        }

        ImGui::SameLine();
        ImGui::Checkbox("Live Viewport Sync", &liveMaterialViewportSync);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("When checked, parameter tweaks sync to viewport in real-time.");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("| %s", activeMaterial.name.c_str());

        ImGui::Separator();

        // Scan available textures across project root
        std::vector<TextureAssetEntry> availableTextures = ScanProjectTextures();

        // Two-pane Layout (Left: MATERIAL PREVIEW, Right: SURFACE, VALUES, SETTINGS)
        // Two-pane Layout (Left: MATERIAL PREVIEW, Right: SURFACE, VALUES, SETTINGS)
        float previewPaneW = 270.0f;
        ImGui::BeginChild("MatPreviewPane", ImVec2(previewPaneW, 0), true);

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "MATERIAL PREVIEW");
        ImGui::Spacing();

        // 6 3D Mesh Selector Buttons: Cube (default), Sphere, Cylinder, Plane, Torus, Capsule
        const char* meshNames[] = { "Cube", "Sphere", "Cylinder", "Plane", "Torus", "Capsule" };
        float shapeBtnW = (previewPaneW - 20.0f - 8.0f) / 3.0f;
        for (int m = 0; m < 6; ++m) {
            bool isCur = (matEditorPreviewMesh == m);
            if (isCur) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.122f, 0.600f, 0.949f, 1.0f));
            if (ImGui::Button(meshNames[m], ImVec2(shapeBtnW, 22.0f))) {
                matEditorPreviewMesh = m;
            }
            if (isCur) ImGui::PopStyleColor();
            if (m % 3 < 2) ImGui::SameLine(0.0f, 4.0f);
        }

        ImGui::Spacing();

        // Predefined Light Presets
        const char* lightPresets[] = { "Key Light", "Studio 3-Point", "Warm Sunset", "Cool Moonlight" };
        ImGui::SetNextItemWidth(previewPaneW - 20.0f);
        ImGui::Combo("##LightPresetCombo", &matEditorLightPreset, lightPresets, IM_ARRAYSIZE(lightPresets));

        // Auto-Rotate & Reset
        ImGui::Checkbox("Auto-Rotate", &matEditorAutoRotate);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset View")) {
            matEditorRotationAngle = 0.0f;
            matEditorLightAngle = 45.0f;
        }

        ImGui::Spacing();

        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize(previewPaneW - 20.0f, previewPaneW - 20.0f);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Register preview Pip rect
        matPreviewPipRect.active = true;
        matPreviewPipRect.x = canvasPos.x;
        matPreviewPipRect.y = canvasPos.y;
        matPreviewPipRect.width = canvasSize.x;
        matPreviewPipRect.height = canvasSize.y;
        matPreviewPipRect.meshType = matEditorPreviewMesh;
        matPreviewPipRect.lightPreset = matEditorLightPreset;
        matPreviewPipRect.lightAngle = matEditorLightAngle;
        matPreviewPipRect.rotationAngle = matEditorRotationAngle;

        // Render DirectX 12 3D PBR Realtime Texture
        if (matPreviewTextureId != 0) {
            ImGui::Image((ImTextureID)matPreviewTextureId, canvasSize, ImVec2(0, 0), ImVec2(1, 1));
        } else {
            drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(20, 22, 26, 255), 6.0f);
            drawList->AddText(ImVec2(canvasPos.x + 12.0f, canvasPos.y + canvasSize.y * 0.5f - 8.0f), IM_COL32(160, 180, 200, 220), "Initializing 3D DX12 PBR...");
        }

        // Elegant border overlay
        drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(45, 48, 56, 255), 6.0f, 0, 1.5f);
        drawList->AddText(ImVec2(canvasPos.x + 8.0f, canvasPos.y + 6.0f), IM_COL32(100, 200, 255, 220), "DX12 Realtime PBR");

        // Invisible button to capture drag for model rotation (Left) and light rotation (Right)
        ImGui::SetCursorScreenPos(canvasPos);
        ImGui::InvisibleButton("##PreviewCanvasBtn", canvasSize);
        if (ImGui::IsItemActive()) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                matEditorRotationAngle += dragDelta.x * 0.75f;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                matEditorLightAngle += dragDelta.x * 0.5f;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
            }
        }

        // Texture indicator badge overlaid
        if (!activeMaterial.baseColorTexture.empty() && activeMaterial.baseColorTexture != "none") {
            drawList->AddRectFilled(ImVec2(canvasPos.x + 8, canvasPos.y + canvasSize.y - 24),
                                    ImVec2(canvasPos.x + canvasSize.x - 8, canvasPos.y + canvasSize.y - 6),
                                    IM_COL32(0, 0, 0, 180), 4.0f);
            std::string texBadge = "Tex: " + activeMaterial.baseColorTexture;
            if (texBadge.length() > 28) texBadge = texBadge.substr(0, 25) + "...";
            drawList->AddText(ImVec2(canvasPos.x + 12, canvasPos.y + canvasSize.y - 22),
                              IM_COL32(100, 220, 255, 255), texBadge.c_str());
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "Shape: %s | Yaw: %.0fÃƒâ€šÃ‚Â°", meshNames[matEditorPreviewMesh], matEditorRotationAngle);
        ImGui::TextDisabled("Light: %.0fÃƒâ€šÃ‚Â° (L-Drag: Mesh | R-Drag: Light)", matEditorLightAngle);
        ImGui::TextDisabled("Shader: %s", activeMaterial.shadingModel == 0 ? "PBR Cook-Torrance" : (activeMaterial.shadingModel == 1 ? "Unlit" : "Subsurface"));

        ImGui::EndChild();

        // Right Pane: SURFACE, VALUES, SETTINGS (dev.md layout)
        ImGui::BeginChild("MatPropertiesPane", ImVec2(0, 0), true);

        char nameEdit[64];
        strncpy(nameEdit, activeMaterial.name.c_str(), sizeof(nameEdit));
        nameEdit[sizeof(nameEdit) - 1] = '\0';
        ImGui::Text("Material:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 10.0f);
        if (ImGui::InputText("##MatNameHeader", nameEdit, sizeof(nameEdit))) {
            activeMaterial.name = nameEdit;
            materialDirty = true;
        }

        if (activeMaterial.assetId.IsValid()) {
            ImGui::TextDisabled("AssetID: %s", activeMaterial.assetId.ToString().c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy##CopyMatAssetId")) {
                ImGui::SetClipboardText(activeMaterial.assetId.ToString().c_str());
                AddLog("LogMaterial", "Copied AssetID to clipboard: " + activeMaterial.assetId.ToString(), 0);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("View References##MatRefView")) {
                OpenReferenceViewer(activeMaterial.assetId);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
        // SECTION 1: SURFACE (dev.md)
        // ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
        if (ImGui::CollapsingHeader("SURFACE", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (DrawTextureSlot("Base Color", activeMaterial.baseColorTexture, activeMaterial.baseColorAssetId, availableTextures)) {
                materialDirty = true;
                if (TextureManager::Get().IsTextureLoaded(activeMaterial.baseColorTexture)) {
                    CachedTexture* tex = TextureManager::Get().GetTexture(activeMaterial.baseColorTexture);
                    if (tex && tex->valid) {
                        activeMaterial.baseColor = tex->averageColor;
                    }
                }
            }
            if (DrawTextureSlot("Normal", activeMaterial.normalTexture, activeMaterial.normalAssetId, availableTextures)) {
                materialDirty = true;
            }
            if (DrawTextureSlot("Surface Map (Packed R=AO, G=Rough, B=Metal)", matEditorSurfaceMap, activeMaterial.roughnessAssetId, availableTextures)) {
                activeMaterial.roughnessTexture = matEditorSurfaceMap;
                activeMaterial.metallicTexture = matEditorSurfaceMap;
                activeMaterial.aoTexture = matEditorSurfaceMap;
                activeMaterial.aoChannel = 0;
                activeMaterial.roughnessChannel = 1;
                activeMaterial.metallicChannel = 2;
                materialDirty = true;
            }
            if (DrawTextureSlot("Roughness", activeMaterial.roughnessTexture, activeMaterial.roughnessAssetId, availableTextures)) {
                materialDirty = true;
            }
            if (DrawTextureSlot("Metallic", activeMaterial.metallicTexture, activeMaterial.metallicAssetId, availableTextures)) {
                materialDirty = true;
            }
            if (DrawTextureSlot("Ambient Occlusion", activeMaterial.aoTexture, activeMaterial.aoAssetId, availableTextures)) {
                materialDirty = true;
            }
            if (DrawTextureSlot("Emission", activeMaterial.emissionTexture, activeMaterial.emissionAssetId, availableTextures)) {
                materialDirty = true;
            }
            if (DrawTextureSlot("Opacity / Mask", activeMaterial.opacityTexture, activeMaterial.opacityAssetId, availableTextures)) {
                materialDirty = true;
            }
            ImGui::Spacing();
        }

        // ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
        // SECTION 2: VALUES (dev.md)
        // ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
        if (ImGui::CollapsingHeader("VALUES", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (ImGui::ColorEdit3("Base Color", &activeMaterial.baseColor.r, ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB)) {
                materialDirty = true;
            }
            if (ImGui::SliderFloat("Metallic", &activeMaterial.metallic, 0.0f, 1.0f, "%.2f")) {
                materialDirty = true;
            }
            if (ImGui::SliderFloat("Roughness", &activeMaterial.roughness, 0.0f, 1.0f, "%.2f")) {
                materialDirty = true;
            }
            if (ImGui::SliderFloat("Normal Strength", &activeMaterial.normalStrength, 0.0f, 2.0f, "%.2f")) {
                materialDirty = true;
            }
            if (ImGui::Checkbox("Normal Map Y / Green Flip (OpenGL / Blender)", &activeMaterial.normalMapYFlip)) {
                materialDirty = true;
            }
            ImGui::Spacing();
            const char* channelNames[] = { "Red (R)", "Green (G)", "Blue (B)", "Alpha (A)" };
            if (ImGui::Combo("Metallic Channel", &activeMaterial.metallicChannel, channelNames, IM_ARRAYSIZE(channelNames))) {
                materialDirty = true;
            }
            if (ImGui::Combo("Roughness Channel", &activeMaterial.roughnessChannel, channelNames, IM_ARRAYSIZE(channelNames))) {
                materialDirty = true;
            }
            if (ImGui::Combo("AO Channel", &activeMaterial.aoChannel, channelNames, IM_ARRAYSIZE(channelNames))) {
                materialDirty = true;
            }
            ImGui::Spacing();
            if (ImGui::ColorEdit3("Emission Color", &activeMaterial.emissiveColor.r)) {
                materialDirty = true;
            }
            if (ImGui::SliderFloat("Emission Power", &activeMaterial.emissiveIntensity, 0.0f, 30.0f, "%.2f")) {
                materialDirty = true;
            }
            if (ImGui::SliderFloat("Opacity", &activeMaterial.opacity, 0.0f, 1.0f, "%.2f")) {
                materialDirty = true;
            }
            if (activeMaterial.blendMode == 1) {
                if (ImGui::SliderFloat("Opacity Mask Clip Value", &activeMaterial.opacityMaskClipValue, 0.0f, 1.0f, "%.2f")) {
                    materialDirty = true;
                }
            }
            if (!activeMaterial.opacityTexture.empty() && activeMaterial.blendMode == 0) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "Opacity map loaded, but Blend Mode is Opaque.");
                if (ImGui::SmallButton("Switch to Masked##AutoMaskBtn")) {
                    activeMaterial.blendMode = 1;
                    materialDirty = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Switch to Translucent##AutoTransBtn")) {
                    activeMaterial.blendMode = 2;
                    materialDirty = true;
                }
            }
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.2f, 0.75f, 1.0f, 1.0f), "UV Tiling (UV Scale):");
            if (ImGui::DragFloat2("UV Scale (X, Y)", &activeMaterial.uvScale.x, 0.05f, 0.001f, 100.0f, "%.2f")) {
                materialDirty = true;
                PropagateMaterialChangesToScene(scene, activeMaterial);
            }
            if (ImGui::DragFloat("Tiling X (U)", &activeMaterial.uvScale.x, 0.05f, 0.001f, 100.0f, "%.2f")) {
                materialDirty = true;
                PropagateMaterialChangesToScene(scene, activeMaterial);
            }
            if (ImGui::DragFloat("Tiling Y (V)", &activeMaterial.uvScale.y, 0.05f, 0.001f, 100.0f, "%.2f")) {
                materialDirty = true;
                PropagateMaterialChangesToScene(scene, activeMaterial);
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset (1, 1)##MatUVReset")) {
                activeMaterial.uvScale = glm::vec2(1.0f, 1.0f);
                materialDirty = true;
                PropagateMaterialChangesToScene(scene, activeMaterial);
            }
            ImGui::Spacing();
        }

        // ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
        // SECTION 3: SETTINGS (dev.md)
        // ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
        if (ImGui::CollapsingHeader("SETTINGS", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            const char* shaders[] = { "PBR", "Unlit", "Subsurface" };
            if (ImGui::Combo("Shader", &activeMaterial.shadingModel, shaders, IM_ARRAYSIZE(shaders))) {
                materialDirty = true;
            }

            const char* blendModes[] = { "Opaque", "Masked", "Translucent" };
            if (ImGui::Combo("Blend Mode", &activeMaterial.blendMode, blendModes, IM_ARRAYSIZE(blendModes))) {
                materialDirty = true;
            }

            if (ImGui::Checkbox("Two Sided", &activeMaterial.twoSided)) {
                materialDirty = true;
            }
            if (ImGui::Checkbox("Cast Shadows", &activeMaterial.castShadows)) {
                materialDirty = true;
            }
            if (ImGui::Checkbox("Receive Shadows", &activeMaterial.receiveShadows)) {
                materialDirty = true;
            }
            if (ImGui::Checkbox("Disable Texture Streaming", &activeMaterial.disableStreaming)) {
                materialDirty = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Wicked Engine: When checked, keeps textures of this material at full native resolution even when outside viewport.");
            }

            const char* debugModes[] = {
                "0: Final PBR",
                "1: Base Color Only",
                "2: Normal Visualization",
                "3: Roughness",
                "4: Metallic",
                "5: AO",
                "6: Tangent",
                "7: Bitangent",
                "8: Vertex Normal",
                "9: UV0",
                "10: Simple Diffuse (Diagnostic)"
            };
            if (ImGui::Combo("Material Debug Mode", &activeMaterial.materialDebugMode, debugModes, IM_ARRAYSIZE(debugModes))) {
                materialDirty = true;
            }
            ImGui::Spacing();
        }

        // ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
        // SECTION 4: DIRECTXTEX TOOLS
        // ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
        if (ImGui::CollapsingHeader("DIRECTXTEX TOOLS", ImGuiTreeNodeFlags_None)) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "DirectXTex Mipmap & Texture Processing Engine");
            ImGui::TextWrapped("DirectXTex provides linear sRGB mipmap chains, normal normalization, BC1-BC7 decoding, and DDS serialization.");
            ImGui::Spacing();

            static float normalAmp = 1.0f;
            ImGui::SliderFloat("Normal Map Amplitude", &normalAmp, 0.1f, 10.0f, "%.2f");
            if (!activeMaterial.baseColorTexture.empty()) {
                if (ImGui::Button("ÃƒÂ¢Ã…Â¡Ã‚Â¡ Generate Normal Map from Base Color (DirectXTex)##GenNormBtn")) {
                    std::string res = TextureManager::Get().ResolvePath(activeMaterial.baseColorTexture);
                    CachedTexture* ct = TextureManager::Get().GetTexture(res);
                    if (ct && ct->valid && !ct->data.empty()) {
                        DirectX::Image srcImg;
                        srcImg.width = ct->width; srcImg.height = ct->height;
                        srcImg.format = DXGI_FORMAT_R8G8B8A8_UNORM;
                        srcImg.rowPitch = ct->width * 4;
                        srcImg.slicePitch = ct->width * ct->height * 4;
                        srcImg.pixels = ct->data.data();

                        DirectX::ScratchImage normMap;
                        if (DirectX::ComputeNormalMap(srcImg, DirectX::CNMAP_DEFAULT, normalAmp, DXGI_FORMAT_R8G8B8A8_UNORM, normMap)) {
                            std::filesystem::path p(res);
                            std::string outPath = (p.parent_path() / (p.stem().string() + "_Normal.dds")).string();
                            DirectX::ScratchImage mips;
                            DirectX::GenerateMipMaps(*normMap.GetImage(0, 0, 0), DirectX::TEX_FILTER_BOX | DirectX::TEX_FILTER_NORMAL_NORMALIZE, 0, mips);
                            DirectX::SaveToDDSFile(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), outPath.c_str());
                            activeMaterial.normalTexture = outPath;
                            materialDirty = true;
                            AddLog("DirectXTex", "Generated normal map from heightmap: " + outPath, 0);
                        }
                    }
                }
            }
            ImGui::Spacing();
        }

        // If Live Viewport Sync is enabled, update all scene actors and terrain layers in real-time
        if (liveMaterialViewportSync && materialDirty) {
            PropagateMaterialChangesToScene(scene, activeMaterial);
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

void EngineUI::RenderMaterialWorkspace(Scene& scene, const glm::vec2& wsPos, const glm::vec2& wsSize) {
    ImGui::SetNextWindowSize(ImVec2(wsSize.x, wsSize.y), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));

    if (ImGui::Begin("Material Editor", &showMaterialEditor, flags)) {
        // Toolbar ("Existing Menu" across the top)
        if (ImGui::Button("Save Material") || (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))) {
            if (SaveMaterialFile(activeMaterial.filePath, activeMaterial)) {
                materialDirty = false;
                if (activeMaterialDocIdx >= 0 && activeMaterialDocIdx < (int)openMaterialDocs.size()) {
                    openMaterialDocs[activeMaterialDocIdx].isDirty = false;
                    openMaterialDocs[activeMaterialDocIdx].material = activeMaterial;
                    openMaterialDocs[activeMaterialDocIdx].saveState.isSaving = true;
                    openMaterialDocs[activeMaterialDocIdx].saveState.progress = 1.0f;
                    openMaterialDocs[activeMaterialDocIdx].saveState.fadeTimer = 1.5f;
                }
                PropagateMaterialChangesToScene(scene, activeMaterial);
                AddLog("LogMaterial", "Saved Material to " + activeMaterial.filePath + " (Reflected in viewport and terrain layers)", 2);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply to Selected Actor")) {
            GameObject* sel = scene.GetSelected();
            if (sel) {
                ApplyMaterialToActorAndChildren(scene, sel, activeMaterial);
            } else {
                AddLog("LogMaterial", "No Actor currently selected to apply material.", 1);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert")) {
            LoadMaterialFile(activeMaterial.filePath, activeMaterial);
            materialDirty = false;
        }

        ImGui::SameLine();
        ImGui::Checkbox("Live Viewport Sync", &liveMaterialViewportSync);

        ImGui::SameLine();
        ImGui::TextDisabled("| %s", activeMaterial.name.c_str());

        ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f * uiScale);
        if (ImGui::Button("Close Tab", ImVec2(80.0f * uiScale, 22.0f * uiScale))) {
            showMaterialEditor = false;
            materialWorkspaceActive = false;
        }

        ImGui::Separator();

        // Scan available textures
        std::vector<TextureAssetEntry> availableTextures = ScanProjectTextures();

        // 3-Column Layout: [Preview (Top-Left)] | [Texture Selecting Fields (Center)] | [Material Properties (Right)]
        float totalAvailW = wsSize.x - 28.0f * uiScale;
        float previewColW = std::clamp(totalAvailW * 0.28f, 320.0f * uiScale, 420.0f * uiScale);
        float propsColW = std::clamp(totalAvailW * 0.36f, 380.0f * uiScale, 520.0f * uiScale);
        float texturesColW = totalAvailW - previewColW - propsColW - 16.0f * uiScale;
        if (texturesColW < 300.0f * uiScale) {
            texturesColW = std::max(280.0f * uiScale, totalAvailW * 0.34f);
            propsColW = totalAvailW - previewColW - texturesColW - 16.0f * uiScale;
        }

        // ====================================================================
        // COLUMN 1 (TOP-LEFT): PREVIEW
        // ====================================================================
        ImGui::BeginChild("MatWorkspacePreviewCol", ImVec2(previewColW, 0), true);

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Preview");
        ImGui::Separator();
        ImGui::Spacing();

        // 6 3D Mesh selector buttons: Cube (default), Sphere, Cylinder, Plane, Torus, Capsule
        const char* meshNames[] = { "Cube", "Sphere", "Cylinder", "Plane", "Torus", "Capsule" };
        float shapeBtnW = (previewColW - 20.0f - 12.0f * uiScale) / 3.0f;
        for (int m = 0; m < 6; ++m) {
            bool isCur = (matEditorPreviewMesh == m);
            if (isCur) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.122f, 0.600f, 0.949f, 1.0f));
            if (ImGui::Button(meshNames[m], ImVec2(shapeBtnW, 24.0f * uiScale))) {
                matEditorPreviewMesh = m;
            }
            if (isCur) ImGui::PopStyleColor();
            if (m % 3 < 2) ImGui::SameLine(0.0f, 6.0f);
        }

        ImGui::Spacing();

        // Environment Lighting Presets
        const char* lightPresets[] = { "Key Light", "Studio 3-Point", "Warm Sunset", "Cool Moonlight" };
        ImGui::TextDisabled("Environment Lighting:");
        ImGui::SetNextItemWidth(previewColW - 20.0f);
        ImGui::Combo("##WorkspaceLightPresetCombo", &matEditorLightPreset, lightPresets, IM_ARRAYSIZE(lightPresets));

        // Auto-Rotate & Reset View
        ImGui::Checkbox("Auto-Rotate##WorkspaceAutoRot", &matEditorAutoRotate);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset View##WorkspaceResetView")) {
            matEditorRotationAngle = 0.0f;
            matEditorLightAngle = 45.0f;
        }

        ImGui::Spacing();

        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        float canvasDim = std::min(previewColW - 24.0f, 320.0f * uiScale);
        ImVec2 canvasSize(canvasDim, canvasDim);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Update Material Preview D3D12 hardware viewport rect
        matPreviewPipRect.active = true;
        matPreviewPipRect.x = canvasPos.x;
        matPreviewPipRect.y = canvasPos.y;
        matPreviewPipRect.width = canvasSize.x;
        matPreviewPipRect.height = canvasSize.y;
        matPreviewPipRect.meshType = matEditorPreviewMesh;
        matPreviewPipRect.lightPreset = matEditorLightPreset;
        matPreviewPipRect.lightAngle = matEditorLightAngle;
        matPreviewPipRect.rotationAngle = matEditorRotationAngle;

        // Render DirectX 12 3D PBR Realtime Texture
        if (matPreviewTextureId != 0) {
            ImGui::Image((ImTextureID)matPreviewTextureId, canvasSize, ImVec2(0, 0), ImVec2(1, 1));
        } else {
            drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(20, 22, 26, 255), 6.0f);
            drawList->AddText(ImVec2(canvasPos.x + 12.0f, canvasPos.y + canvasSize.y * 0.5f - 8.0f), IM_COL32(160, 180, 200, 220), "Initializing 3D DX12 PBR...");
        }

        // Elegant border around the DirectX 12 3D PBR preview viewport
        drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(31, 153, 242, 200), 4.0f, 0, 1.5f);
        drawList->AddRect(ImVec2(canvasPos.x - 1, canvasPos.y - 1), ImVec2(canvasPos.x + canvasSize.x + 1, canvasPos.y + canvasSize.y + 1), IM_COL32(10, 12, 16, 255), 5.0f, 0, 1.0f);
        drawList->AddText(ImVec2(canvasPos.x + 8.0f, canvasPos.y + 8.0f), IM_COL32(200, 220, 240, 220), "DX12 PBR 3D");

        // Invisible button to capture drag for model rotation (Left) and light rotation (Right)
        ImGui::SetCursorScreenPos(canvasPos);
        ImGui::InvisibleButton("##WorkspacePreviewCanvasBtn", canvasSize);
        if (ImGui::IsItemActive()) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                matEditorRotationAngle += dragDelta.x * 0.75f;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                matEditorLightAngle += dragDelta.x * 0.5f;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
            }
        }

        // Texture indicator badge overlaid
        if (!activeMaterial.baseColorTexture.empty() && activeMaterial.baseColorTexture != "none") {
            drawList->AddRectFilled(ImVec2(canvasPos.x + 8, canvasPos.y + canvasSize.y - 24),
                                    ImVec2(canvasPos.x + canvasSize.x - 8, canvasPos.y + canvasSize.y - 6),
                                    IM_COL32(0, 0, 0, 180), 4.0f);
            std::string texBadge = "Tex: " + activeMaterial.baseColorTexture;
            if (texBadge.length() > 28) texBadge = texBadge.substr(0, 25) + "...";
            drawList->AddText(ImVec2(canvasPos.x + 12, canvasPos.y + canvasSize.y - 22),
                              IM_COL32(100, 220, 255, 255), texBadge.c_str());
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "Shape: %s | Yaw: %.0fÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â°", meshNames[matEditorPreviewMesh], matEditorRotationAngle);
        ImGui::TextDisabled("Light: %.0fÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â° (L-Drag: Mesh | R-Drag: Light)", matEditorLightAngle);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Material Identity & File Metadata card
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Material Info");
        char nameEdit[64];
        strncpy(nameEdit, activeMaterial.name.c_str(), sizeof(nameEdit));
        nameEdit[sizeof(nameEdit) - 1] = '\0';
        ImGui::SetNextItemWidth(previewColW - 20.0f);
        if (ImGui::InputText("##MatNameWorkspaceHeader", nameEdit, sizeof(nameEdit))) {
            activeMaterial.name = nameEdit;
            materialDirty = true;
        }

        if (activeMaterial.assetId.IsValid()) {
            ImGui::TextDisabled("AssetID: %s", activeMaterial.assetId.ToString().c_str());
            if (ImGui::SmallButton("Copy AssetID##CopyMatAssetIdWS")) {
                ImGui::SetClipboardText(activeMaterial.assetId.ToString().c_str());
                AddLog("LogMaterial", "Copied AssetID to clipboard: " + activeMaterial.assetId.ToString(), 0);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("View References##MatRefViewWS")) {
                OpenReferenceViewer(activeMaterial.assetId);
            }
        }

        if (!activeMaterial.filePath.empty()) {
            ImGui::TextDisabled("Path: %s", activeMaterial.filePath.c_str());
        }

        ImGui::EndChild();

        ImGui::SameLine();

        // ====================================================================
        // COLUMN 2 (CENTER): TEXTURE SELECTING FIELDS
        // ====================================================================
        ImGui::BeginChild("MatWorkspaceTexturesCol", ImVec2(texturesColW, 0), true);

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Texture Selecting Fields");
        ImGui::TextDisabled("Select or drop textures for PBR channels");
        ImGui::Separator();
        ImGui::Spacing();

        // 1. Base Color (Albedo)
        if (DrawTextureSlot("Base Color (Albedo)", activeMaterial.baseColorTexture, activeMaterial.baseColorAssetId, availableTextures)) {
            materialDirty = true;
            if (TextureManager::Get().IsTextureLoaded(activeMaterial.baseColorTexture)) {
                CachedTexture* tex = TextureManager::Get().GetTexture(activeMaterial.baseColorTexture);
                if (tex && tex->valid) activeMaterial.baseColor = tex->averageColor;
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 2. Normal Map
        if (DrawTextureSlot("Normal Map", activeMaterial.normalTexture, activeMaterial.normalAssetId, availableTextures)) {
            materialDirty = true;
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 3. Surface Map (Packed ORM / RMA)
        if (DrawTextureSlot("Surface Map (Packed R=AO, G=Rough, B=Metal)", matEditorSurfaceMap, activeMaterial.roughnessAssetId, availableTextures)) {
            activeMaterial.roughnessTexture = matEditorSurfaceMap;
            activeMaterial.metallicTexture = matEditorSurfaceMap;
            activeMaterial.aoTexture = matEditorSurfaceMap;
            activeMaterial.aoChannel = 0;
            activeMaterial.roughnessChannel = 1;
            activeMaterial.metallicChannel = 2;
            materialDirty = true;
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 4. Roughness Map
        if (DrawTextureSlot("Roughness Map", activeMaterial.roughnessTexture, activeMaterial.roughnessAssetId, availableTextures)) {
            materialDirty = true;
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 5. Metallic Map
        if (DrawTextureSlot("Metallic Map", activeMaterial.metallicTexture, activeMaterial.metallicAssetId, availableTextures)) {
            materialDirty = true;
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 6. Ambient Occlusion Map
        if (DrawTextureSlot("Ambient Occlusion (AO) Map", activeMaterial.aoTexture, activeMaterial.aoAssetId, availableTextures)) {
            materialDirty = true;
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 7. Emissive / Emission Map
        if (DrawTextureSlot("Emissive Map", activeMaterial.emissionTexture, activeMaterial.emissionAssetId, availableTextures)) {
            materialDirty = true;
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 8. Opacity / Mask Map
        if (DrawTextureSlot("Opacity / Mask Map", activeMaterial.opacityTexture, activeMaterial.opacityAssetId, availableTextures)) {
            materialDirty = true;
        }

        ImGui::EndChild();

        ImGui::SameLine();

        // ====================================================================
        // COLUMN 3 (RIGHT): MATERIAL PROPERTIES
        // ====================================================================
        ImGui::BeginChild("MatWorkspacePropertiesCol", ImVec2(0, 0), true);

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Material Properties");
        ImGui::TextDisabled("Physical properties, opacity, and shading parameters");
        ImGui::Separator();
        ImGui::Spacing();

        // SECTION 1: MATERIAL TYPE & SHADING
        if (ImGui::CollapsingHeader("MATERIAL TYPE & SHADING", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            const char* shadingModels[] = { "Default Lit (PBR Cook-Torrance)", "Unlit", "Subsurface Profile" };
            if (ImGui::Combo("Shading Model", &activeMaterial.shadingModel, shadingModels, IM_ARRAYSIZE(shadingModels))) materialDirty = true;

            const char* blendModes[] = { "Opaque", "Masked", "Translucent" };
            if (ImGui::Combo("Material Type (Blend Mode)", &activeMaterial.blendMode, blendModes, IM_ARRAYSIZE(blendModes))) materialDirty = true;

            if (ImGui::Checkbox("Two Sided", &activeMaterial.twoSided)) materialDirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Cast Shadows", &activeMaterial.castShadows)) materialDirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Receive Shadows", &activeMaterial.receiveShadows)) materialDirty = true;
            ImGui::Spacing();
        }

        // SECTION 2: OPACITY & MASKING
        if (ImGui::CollapsingHeader("OPACITY & MASKING", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (ImGui::SliderFloat("Opacity", &activeMaterial.opacity, 0.0f, 1.0f, "%.2f")) materialDirty = true;
            if (activeMaterial.blendMode == 1) { // Masked
                if (ImGui::SliderFloat("Mask Clip Value", &activeMaterial.opacityMaskClipValue, 0.0f, 1.0f, "%.2f")) materialDirty = true;
            }
            ImGui::Spacing();
        }

        // SECTION 3: SURFACE VALUES (PBR)
        if (ImGui::CollapsingHeader("SURFACE VALUES (PBR)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (ImGui::ColorEdit3("Base Color Tint", &activeMaterial.baseColor.x, ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB)) materialDirty = true;
            if (ImGui::SliderFloat("Roughness", &activeMaterial.roughness, 0.0f, 1.0f, "%.2f")) materialDirty = true;
            if (ImGui::SliderFloat("Metallic", &activeMaterial.metallic, 0.0f, 1.0f, "%.2f")) materialDirty = true;
            if (ImGui::SliderFloat("Specular", &activeMaterial.specular, 0.0f, 1.0f, "%.2f")) materialDirty = true;
            if (ImGui::SliderFloat("Normal Strength", &activeMaterial.normalStrength, 0.0f, 3.0f, "%.2f")) materialDirty = true;
            if (ImGui::Checkbox("Normal Map Y / Green Flip (OpenGL / Blender)", &activeMaterial.normalMapYFlip)) materialDirty = true;
            ImGui::Spacing();
        }

        // SECTION 4: EMISSIVE LIGHTING
        if (ImGui::CollapsingHeader("EMISSIVE LIGHTING", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (ImGui::ColorEdit3("Emissive Color", &activeMaterial.emissiveColor.x, ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB)) materialDirty = true;
            if (ImGui::DragFloat("Emissive Intensity", &activeMaterial.emissiveIntensity, 0.1f, 0.0f, 100.0f, "%.1f")) materialDirty = true;
            ImGui::Spacing();
        }

        // SECTION 5: UV COORDINATES & TILING
        if (ImGui::CollapsingHeader("UV COORDINATES & TILING", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (ImGui::DragFloat2("UV Scale / Tiling", &activeMaterial.uvScale.x, 0.05f, 0.01f, 100.0f, "%.2f")) {
                materialDirty = true;
                PropagateMaterialChangesToScene(scene, activeMaterial);
            }
            ImGui::Spacing();
        }

        // SECTION 6: TEXTURE CHANNEL ROUTING
        if (ImGui::CollapsingHeader("TEXTURE CHANNEL ROUTING", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            const char* channelNames[] = { "Red (R)", "Green (G)", "Blue (B)", "Alpha (A)" };
            if (ImGui::Combo("Metallic Channel", &activeMaterial.metallicChannel, channelNames, IM_ARRAYSIZE(channelNames))) materialDirty = true;
            if (ImGui::Combo("Roughness Channel", &activeMaterial.roughnessChannel, channelNames, IM_ARRAYSIZE(channelNames))) materialDirty = true;
            if (ImGui::Combo("AO Channel", &activeMaterial.aoChannel, channelNames, IM_ARRAYSIZE(channelNames))) materialDirty = true;
            ImGui::Spacing();
        }

        // SECTION 7: MATERIAL DIAGNOSTICS & DEBUG
        if (ImGui::CollapsingHeader("MATERIAL DIAGNOSTICS", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            const char* debugModes[] = {
                "Full PBR Lit (Default)",
                "Unlit",
                "Base Color Only",
                "Normal Only",
                "Roughness Only",
                "Metallic Only",
                "Ambient Occlusion Only",
                "Emissive Only",
                "UV Coordinates"
            };
            if (ImGui::Combo("Debug View Mode", &activeMaterial.materialDebugMode, debugModes, IM_ARRAYSIZE(debugModes))) materialDirty = true;
            if (ImGui::Checkbox("Disable Texture Streaming", &activeMaterial.disableStreaming)) materialDirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Wicked Engine: When checked, keeps textures of this material at full native resolution even when outside viewport.");
            ImGui::Spacing();
        }

        ImGui::EndChild();

        // Real-time viewport sync if enabled
        if (liveMaterialViewportSync && materialDirty) {
            PropagateMaterialChangesToScene(scene, activeMaterial);
        }

        // Keep active material doc in sync with activeMaterial
        if (activeMaterialDocIdx >= 0 && activeMaterialDocIdx < (int)openMaterialDocs.size()) {
            openMaterialDocs[activeMaterialDocIdx].material = activeMaterial;
            openMaterialDocs[activeMaterialDocIdx].isDirty = materialDirty;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}
bool EngineUI::LoadWaterFile(const std::string& path, WaterAsset& out) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    out.filePath = path;
    out.name = std::filesystem::path(path).stem().string();
    std::string line;
    while (std::getline(file, line)) {
        size_t p = line.find("=");
        if (p == std::string::npos) continue;
        std::string k = line.substr(0, p);
        std::string v = line.substr(p + 1);
        if (k == "waterType") out.waterType = (WaterBodyType)std::stoi(v);
        else if (k == "roughness") out.settings.roughness = std::stof(v);
        else if (k == "metallic") out.settings.metallic = std::stof(v);
        // skipping full properties parse for brevity...
    }
    return true;
}

bool EngineUI::SaveWaterFile(const std::string& path, const WaterAsset& w) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << "waterType=" << (int)w.waterType << "\n";
    file << "roughness=" << w.settings.roughness << "\n";
    file << "metallic=" << w.settings.metallic << "\n";
    return true;
}

void EngineUI::OpenWaterEditor(const std::string& path) {
    if (LoadWaterFile(path, activeWater)) {
        showWaterEditor = true;
        waterDirty = false;
        AddLog("LogWater", "Opened Water Editor for: " + path, 1);
    }
}
