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
void EngineUI::ApplyWaterToGameObject(Scene& scene, GameObject* obj, const WaterAsset& wa) {
    if (!obj || !obj->isWaterBody) return;
    obj->water.type = wa.waterType;
    obj->water.roughness = wa.settings.roughness;
    obj->water.metallic = wa.settings.metallic;
    AddLog("LogWater", "Applied WaterAsset to GameObject", 2);
}

void EngineUI::RenderWaterEditor(Scene& scene) {
    if (!showWaterEditor) return;
    ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Water Editor", &showWaterEditor)) {
        if (ImGui::Button("Save Water")) {
            SaveWaterFile(activeWater.filePath, activeWater);
            waterDirty = false;
        }
        ImGui::Separator();
        int t = (int)activeWater.waterType;
        if (ImGui::Combo("Type", &t, "Ocean\0River\0Sea\0Lake\0Stream\0Waterfall\0Pond\0Pool\0WetSurface\0Puddle\0FloodWater\0Underwater\0Swamp\0")) {
            activeWater.waterType = (WaterBodyType)t;
            waterDirty = true;
        }
        if (ImGui::SliderFloat("Roughness", &activeWater.settings.roughness, 0.0f, 1.0f)) waterDirty = true;
        if (ImGui::SliderFloat("Metallic", &activeWater.settings.metallic, 0.0f, 1.0f)) waterDirty = true;
    }
    ImGui::End();
}

bool EngineUI::LoadSkyboxFile(const std::string& path, SkyboxAsset& outSkybox) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    outSkybox.filePath = path;
    outSkybox.name = std::filesystem::path(path).stem().string();

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

        if (key == "assetId") outSkybox.assetId = AssetID::FromString(val);
        else if (key == "virtualPath") outSkybox.virtualPath = val;
        else if (key == "name") outSkybox.name = val;
        else if (key == "hdrTexture" || key == "hdrTexturePath") outSkybox.hdrTexturePath = val;
        else if (key == "hdrTextureAssetId") outSkybox.hdrTextureAssetId = AssetID::FromString(val);
        else if (key == "exposure") {
            try { outSkybox.exposure = std::stof(val); } catch (...) {}
        }
        else if (key == "rotation") {
            try { outSkybox.rotation = std::stof(val); } catch (...) {}
        }
        else if (key == "autoRotate") {
            outSkybox.autoRotate = (val == "1" || val == "true" || val == "True");
        }
        else if (key == "autoRotateSpeed") {
            try { outSkybox.autoRotateSpeed = std::stof(val); } catch (...) {}
        }
        else if (key == "tint") {
            std::stringstream ss(val);
            ss >> outSkybox.tint.r >> outSkybox.tint.g >> outSkybox.tint.b;
        }
        else if (key == "ambientContribution") {
            try { outSkybox.ambientContribution = std::stof(val); } catch (...) {}
        }
        else if (key == "enableSkybox" || key == "enabled") {
            outSkybox.enableSkybox = (val == "1" || val == "true" || val == "True");
        }
        else if (key == "enableSkyboxLighting" || key == "enableLighting") {
            outSkybox.enableSkyboxLighting = (val == "1" || val == "true" || val == "True");
        }
        else if (key == "skyboxLightingIntensity" || key == "lightingIntensity") {
            try { outSkybox.skyboxLightingIntensity = std::stof(val); } catch (...) {}
        }
        else if (key == "skyboxLightingColor" || key == "lightingColor") {
            std::stringstream ss(val);
            ss >> outSkybox.skyboxLightingColor.r >> outSkybox.skyboxLightingColor.g >> outSkybox.skyboxLightingColor.b;
        }
        else if (key == "skyboxLightingSpecular" || key == "lightingSpecular") {
            try { outSkybox.skyboxLightingSpecular = std::stof(val); } catch (...) {}
        }
        else if (key == "skyboxLightingDiffuse" || key == "lightingDiffuse") {
            try { outSkybox.skyboxLightingDiffuse = std::stof(val); } catch (...) {}
        }
    }
    return true;
}

bool EngineUI::SaveSkyboxFile(const std::string& path, const SkyboxAsset& skybox) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << "# Eunoia-Editor Skybox Asset\n";
    if (skybox.assetId.IsValid()) {
        file << "assetId: " << skybox.assetId.ToString() << "\n";
    }
    if (!skybox.virtualPath.empty()) {
        file << "virtualPath: " << skybox.virtualPath << "\n";
    }
    file << "name: " << skybox.name << "\n";
    if (skybox.hdrTextureAssetId.IsValid()) {
        file << "hdrTextureAssetId: " << skybox.hdrTextureAssetId.ToString() << "\n";
    }
    file << "hdrTexture: " << skybox.hdrTexturePath << "\n";
    file << "exposure: " << skybox.exposure << "\n";
    file << "rotation: " << skybox.rotation << "\n";
    file << "autoRotate: " << (skybox.autoRotate ? 1 : 0) << "\n";
    file << "autoRotateSpeed: " << skybox.autoRotateSpeed << "\n";
    file << "tint: " << skybox.tint.r << " " << skybox.tint.g << " " << skybox.tint.b << "\n";
    file << "ambientContribution: " << skybox.ambientContribution << "\n";
    file << "enableSkybox: " << (skybox.enableSkybox ? 1 : 0) << "\n";
    file << "enableSkyboxLighting: " << (skybox.enableSkyboxLighting ? 1 : 0) << "\n";
    file << "skyboxLightingIntensity: " << skybox.skyboxLightingIntensity << "\n";
    file << "skyboxLightingColor: " << skybox.skyboxLightingColor.r << " " << skybox.skyboxLightingColor.g << " " << skybox.skyboxLightingColor.b << "\n";
    file << "skyboxLightingSpecular: " << skybox.skyboxLightingSpecular << "\n";
    file << "skyboxLightingDiffuse: " << skybox.skyboxLightingDiffuse << "\n";
    return true;
}

void EngineUI::OpenSkyboxEditor(const std::string& path) {
    if (LoadSkyboxFile(path, activeSkybox)) {
        showSkyboxEditor = true;
        skyboxDirty = false;
        AddLog("LogContent", "Opened Skybox Editor: " + activeSkybox.name, 0);
    } else {
        AddLog("LogContent", "Failed to load Skybox from: " + path, 3);
    }
}

void EngineUI::OpenUIEditor(const std::string& path) {
    EunoiaUI::UIEditorManager::Get().Open(path);
    AddLog("LogContent", "Opened UI Editor for: " + path, 0);
}

void EngineUI::OpenAnimatorGraphEditor(const std::string& path) {
    materialWorkspaceActive = false;
    EunoiaUI::UIEditorManager::Get().SwitchToMainEditor();
    Eunoia::NodeBehaviour::NodeBehaviourManager::Get().SwitchToMainEditor();
    Eunoia::AnimatorGraphManager::Get().Open(path);
    AddLog("LogContent", "Opened Animator Graph Editor for: " + path, 0);
}

void EngineUI::OpenLevelAnimationEditor(const std::string& path) {
    auto anim = Eunoia::LevelAnimationManager::Get().LoadLevelAnimation(path);
    if (anim) {
        Eunoia::LevelAnimationEditor::Get().SetActiveAnimation(anim);
        showAnimationWindow = true;
        ImGui::SetWindowFocus("Animation");
        bottomDrawerTab = 1;
        bottomDrawerOpen = true;
        AddLog("LogLevelAnimation", "Opened Level Animation: " + anim->name, 0);
    }
}


void EngineUI::OpenAnimationBlendEditor(const std::string& path) {
    materialWorkspaceActive = false;
    EunoiaUI::UIEditorManager::Get().SwitchToMainEditor();
    Eunoia::NodeBehaviour::NodeBehaviourManager::Get().SwitchToMainEditor();
    Eunoia::AnimatorGraphManager::Get().SwitchToMainEditor();
    Eunoia::AnimationBlendManager::Get().Open(path);
    AddLog("LogContent", "Opened Animation Blend Editor for: " + path, 0);
}

void EngineUI::OpenNodeBehaviourEditor(const std::string& path) {
    materialWorkspaceActive = false;
    EunoiaUI::UIEditorManager::Get().SwitchToMainEditor();
    Eunoia::NodeBehaviour::NodeBehaviourManager::Get().Open(path);
    AddLog("LogContent", "Opened Node Behaviour Editor for: " + path, 0);
}

void EngineUI::ApplySkyboxToScene(Scene& scene, const SkyboxAsset& skybox) {
    scene.enableSkybox = skybox.enableSkybox;
    scene.skyboxAssetPath = skybox.filePath;
    scene.skyboxTexturePath = skybox.hdrTexturePath;
    scene.skyboxExposure = skybox.exposure;
    scene.skyboxRotation = skybox.rotation;
    scene.skyboxAutoRotate = skybox.autoRotate;
    scene.skyboxAutoRotateSpeed = skybox.autoRotateSpeed;
    scene.skyboxTint = skybox.tint;
    scene.skyboxAmbientContribution = skybox.ambientContribution;
    scene.enableSkyboxLighting = skybox.enableSkyboxLighting;
    scene.skyboxLightingIntensity = skybox.skyboxLightingIntensity;
    scene.skyboxLightingColor = skybox.skyboxLightingColor;
    scene.skyboxLightingSpecular = skybox.skyboxLightingSpecular;
    scene.skyboxLightingDiffuse = skybox.skyboxLightingDiffuse;
    AddLog("LogWorld", "Applied Skybox '" + skybox.name + "' to World Level", 2);
}

void EngineUI::RenderSkyboxEditor(Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(620, 540), ImGuiCond_FirstUseEver);
    std::string winTitle = "Skybox Editor - " + activeSkybox.name + (skyboxDirty ? "*" : "") + "###SkyboxEditorWin";
    if (ImGui::Begin(winTitle.c_str(), &showSkyboxEditor, ImGuiWindowFlags_NoCollapse)) {
        // Toolbar
        if (ImGui::Button("Save Skybox") || (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))) {
            if (SaveSkyboxFile(activeSkybox.filePath, activeSkybox)) {
                skyboxDirty = false;
                AddLog("LogContent", "Saved Skybox Asset: " + activeSkybox.name, 2);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply to World")) {
            ApplySkyboxToScene(scene, activeSkybox);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Enable Skybox", &activeSkybox.enableSkybox)) {
            skyboxDirty = true;
        }

        ImGui::Separator();

        ImGui::BeginChild("SkyboxPropsScroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        // SECTION 1: ASSET IDENTITY
        if (ImGui::CollapsingHeader("ASSET IDENTITY", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            char nameBuf[128];
            strncpy(nameBuf, activeSkybox.name.c_str(), sizeof(nameBuf));
            nameBuf[sizeof(nameBuf) - 1] = '\0';
            if (ImGui::InputText("Asset Name", nameBuf, sizeof(nameBuf))) {
                activeSkybox.name = nameBuf;
                skyboxDirty = true;
            }
            ImGui::TextDisabled("Virtual Path: %s", activeSkybox.virtualPath.c_str());
            ImGui::TextDisabled("Asset ID:     %s", activeSkybox.assetId.ToString().c_str());
            ImGui::TextDisabled("File Path:    %s", activeSkybox.filePath.c_str());
            ImGui::Spacing();
        }

        // SECTION 2: HDR PANORAMA TEXTURE
        if (ImGui::CollapsingHeader("HDR PANORAMA TEXTURE", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            ImGui::TextWrapped("Assign an HDR equirectangular panorama texture (.hdr, .dds, .png, .jpg, .exr) from the Content Browser.");
            ImGui::Spacing();

            auto availableTextures = ScanProjectTextures();
            if (DrawTextureSlot("HDR Texture", activeSkybox.hdrTexturePath, activeSkybox.hdrTextureAssetId, availableTextures)) {
                skyboxDirty = true;
            }
            ImGui::Spacing();
        }

        // SECTION 3: ENVIRONMENT PARAMETERS
        if (ImGui::CollapsingHeader("ENVIRONMENT ILLUMINATION", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (ImGui::SliderFloat("Exposure / Intensity", &activeSkybox.exposure, 0.05f, 10.0f, "%.2f")) {
                skyboxDirty = true;
            }
            if (ImGui::SliderFloat("Rotation (Azimuth)", &activeSkybox.rotation, 0.0f, 360.0f, "%.1fÃ‚Â°")) {
                skyboxDirty = true;
            }
            if (ImGui::Checkbox("Auto Rotate Sky", &activeSkybox.autoRotate)) {
                skyboxDirty = true;
            }
            if (activeSkybox.autoRotate) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f * uiScale);
                if (ImGui::SliderFloat("Speed##SkyEditSpeed", &activeSkybox.autoRotateSpeed, -20.0f, 20.0f, "%.1fÃ‚Â°/s")) {
                    skyboxDirty = true;
                }
            }
            if (ImGui::ColorEdit3("Color Tint", &activeSkybox.tint.x)) {
                skyboxDirty = true;
            }
            if (ImGui::SliderFloat("Ambient Scene Contribution", &activeSkybox.ambientContribution, 0.0f, 2.0f, "%.2f")) {
                skyboxDirty = true;
            }
            ImGui::Spacing();
            if (ImGui::Button("Reset Defaults##SkyboxReset")) {
                activeSkybox.exposure = 1.0f;
                activeSkybox.rotation = 0.0f;
                activeSkybox.tint = glm::vec3(1.0f, 1.0f, 1.0f);
                activeSkybox.ambientContribution = 0.5f;
                skyboxDirty = true;
            }
            ImGui::Spacing();
        }

        // SECTION 4: SKYBOX BASED LIGHTING (ENVIRONMENT IBL)
        if (ImGui::CollapsingHeader("SKYBOX BASED LIGHTING (ENVIRONMENT IBL)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            ImGui::TextWrapped("Cast diffuse and specular ambient illumination into the 3D scene from the skybox.");
            if (ImGui::Checkbox("Enable Skybox Based Lighting", &activeSkybox.enableSkyboxLighting)) {
                skyboxDirty = true;
            }
            if (activeSkybox.enableSkyboxLighting) {
                ImGui::Spacing();
                if (ImGui::SliderFloat("Lighting Intensity", &activeSkybox.skyboxLightingIntensity, 0.0f, 10.0f, "%.2f")) {
                    skyboxDirty = true;
                }
                if (ImGui::ColorEdit3("Lighting Color Tint", &activeSkybox.skyboxLightingColor.x)) {
                    skyboxDirty = true;
                }
                if (ImGui::SliderFloat("Diffuse Contribution", &activeSkybox.skyboxLightingDiffuse, 0.0f, 2.0f, "%.2f")) {
                    skyboxDirty = true;
                }
                if (ImGui::SliderFloat("Specular / Reflection Contribution", &activeSkybox.skyboxLightingSpecular, 0.0f, 2.0f, "%.2f")) {
                    skyboxDirty = true;
                }
            } else {
                ImGui::TextDisabled("  (Skybox based scene lighting is currently disabled by default)");
            }
            ImGui::Spacing();
        }

        // Live preview sync to world scene
        if (scene.skyboxAssetPath == activeSkybox.filePath || scene.skyboxAssetPath.empty()) {
            scene.enableSkybox = activeSkybox.enableSkybox;
            scene.skyboxTexturePath = activeSkybox.hdrTexturePath;
            scene.skyboxExposure = activeSkybox.exposure;
            scene.skyboxRotation = activeSkybox.rotation;
            scene.skyboxAutoRotate = activeSkybox.autoRotate;
            scene.skyboxAutoRotateSpeed = activeSkybox.autoRotateSpeed;
            scene.skyboxTint = activeSkybox.tint;
            scene.skyboxAmbientContribution = activeSkybox.ambientContribution;
            scene.enableSkyboxLighting = activeSkybox.enableSkyboxLighting;
            scene.skyboxLightingIntensity = activeSkybox.skyboxLightingIntensity;
            scene.skyboxLightingColor = activeSkybox.skyboxLightingColor;
            scene.skyboxLightingSpecular = activeSkybox.skyboxLightingSpecular;
            scene.skyboxLightingDiffuse = activeSkybox.skyboxLightingDiffuse;
            scene.skyboxAssetPath = activeSkybox.filePath;
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

void EngineUI::RenderWorldSkyboxSettingsModal(Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("World Skybox Settings###WorldSkyboxSettingsWin", &showSkyboxSettingsModal, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "World Environment Skybox Configuration");
        ImGui::TextWrapped("Configure the active world skybox background and ambient scene illumination for the level.");
        ImGui::Separator();

        // Enable toggle
        ImGui::Checkbox("Enable World Skybox", &scene.enableSkybox);
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("TEXTURE & ILLUMINATION", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            auto availableTextures = ScanProjectTextures();
            AssetID dummyAssetId;
            if (DrawTextureSlot("HDR Texture", scene.skyboxTexturePath, dummyAssetId, availableTextures)) {
                scene.skyboxAssetPath = "";
            }
            ImGui::Spacing();

            ImGui::SliderFloat("Exposure / Intensity", &scene.skyboxExposure, 0.05f, 10.0f, "%.2f");
            ImGui::SliderFloat("Rotation (Azimuth)", &scene.skyboxRotation, 0.0f, 360.0f, "%.1fÃ‚Â°");
            ImGui::Checkbox("Auto Rotate Sky", &scene.skyboxAutoRotate);
            if (scene.skyboxAutoRotate) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f * uiScale);
                ImGui::SliderFloat("Speed##SkyRotateSpeed", &scene.skyboxAutoRotateSpeed, -20.0f, 20.0f, "%.1fÃ‚Â°/s");
            }
            ImGui::ColorEdit3("Sky Tint", &scene.skyboxTint.x);
            ImGui::SliderFloat("Ambient Scene Contribution", &scene.skyboxAmbientContribution, 0.0f, 2.0f, "%.2f");
            ImGui::Spacing();

            if (ImGui::Button("Reset to Defaults##ResetSkyboxWorld")) {
                scene.enableSkybox = true;
                scene.skyboxExposure = 1.0f;
                scene.skyboxRotation = 0.0f;
                scene.skyboxTint = glm::vec3(1.0f, 1.0f, 1.0f);
                scene.skyboxAmbientContribution = 0.5f;
                scene.enableSkyboxLighting = false;
                scene.skyboxLightingIntensity = 1.0f;
                scene.skyboxLightingColor = glm::vec3(1.0f, 1.0f, 1.0f);
                scene.skyboxLightingSpecular = 1.0f;
                scene.skyboxLightingDiffuse = 1.0f;
            }
            ImGui::Spacing();
        }

        if (ImGui::CollapsingHeader("SKYBOX BASED LIGHTING (ENVIRONMENT IBL)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            ImGui::TextWrapped("Cast diffuse and specular ambient illumination into the 3D scene from the skybox.");
            ImGui::Checkbox("Enable Skybox Based Lighting", &scene.enableSkyboxLighting);
            if (scene.enableSkyboxLighting) {
                ImGui::Spacing();
                ImGui::SliderFloat("Lighting Intensity", &scene.skyboxLightingIntensity, 0.0f, 10.0f, "%.2f");
                ImGui::ColorEdit3("Lighting Color Tint", &scene.skyboxLightingColor.x);
                ImGui::SliderFloat("Diffuse Contribution", &scene.skyboxLightingDiffuse, 0.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Specular / Reflection Contribution", &scene.skyboxLightingSpecular, 0.0f, 2.0f, "%.2f");
            } else {
                ImGui::TextDisabled("  (Skybox based scene lighting is currently disabled by default)");
            }
            ImGui::Spacing();
        }

        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            showSkyboxSettingsModal = false;
        }
    }
    ImGui::End();
}

void EngineUI::RenderPostProcessingModal(Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(640, 640), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("World Post Processing Settings###WorldPostProcessingWin", &showPostProcessingModal, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "World Post Processing & Color Pipeline");
        ImGui::TextWrapped("Configure real-time exposure and color grading for the active level. Post processing is disabled by default for each level.");
        ImGui::Separator();

        // Master Enable Toggle
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.2f, 0.9f, 0.4f, 1.0f));
        if (ImGui::Checkbox("Enable Post Processing for Level", &scene.enablePostProcessing)) {
            AddLog("LogWorld", scene.enablePostProcessing ? "Enabled level post processing" : "Disabled level post processing", 0);
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (scene.enablePostProcessing) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "[ACTIVE]");
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "[DISABLED (Default)]");
        }
        ImGui::Spacing();

        if (!scene.enablePostProcessing) {
            ImGui::TextDisabled("Enable the checkbox above to activate real-time exposure and color grading in the viewport.");
            ImGui::Spacing();
        }

        // ====================================================================
        // 1. EXPOSURE & LUMINANCE
        // ====================================================================
        if (ImGui::CollapsingHeader("EXPOSURE & LUMINANCE", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            ImGui::SliderFloat("EV Compensation", &scene.postExposureEV, -5.0f, 5.0f, "%.2f EV");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Exposure Value compensation in photographic stops (2^EV factor). 0 = Neutral.");
            }

            // Quick preset EV buttons
            ImGui::TextDisabled("Quick EV Presets:");
            ImGui::SameLine();
            if (ImGui::SmallButton("-2 EV")) scene.postExposureEV = -2.0f;
            ImGui::SameLine();
            if (ImGui::SmallButton("-1 EV")) scene.postExposureEV = -1.0f;
            ImGui::SameLine();
            if (ImGui::SmallButton("0 EV (Neutral)")) scene.postExposureEV = 0.0f;
            ImGui::SameLine();
            if (ImGui::SmallButton("+1 EV")) scene.postExposureEV = 1.0f;
            ImGui::SameLine();
            if (ImGui::SmallButton("+2 EV")) scene.postExposureEV = 2.0f;

            ImGui::SliderFloat("Exposure Multiplier", &scene.postExposureMultiplier, 0.0f, 8.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Linear intensity multiplier applied to camera exposure.");
            }
            ImGui::Spacing();
        }

        // ====================================================================
        // 2. TONEMAPPING & CONTRAST
        // ====================================================================
        if (ImGui::CollapsingHeader("TONEMAPPING & CONTRAST", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            const char* tonemappers[] = {
                "None / Linear (Clamped)",
                "ACES Filmic (Wicked / UE5 Standard)",
                "Reinhard (Soft Shoulder)",
                "Uchimura (Gran Turismo 7 / WickedEngine)",
                "Uncharted 2 / Filmic (NVIDIA Falcor / John Hable)"
            };
            ImGui::SetNextItemWidth(340.0f);
            ImGui::Combo("Tonemapping Operator", &scene.postTonemapper, tonemappers, IM_ARRAYSIZE(tonemappers));

            ImGui::SliderFloat("Contrast", &scene.postContrast, 0.4f, 2.2f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Expands or compresses dynamic range contrast (1.0 = Default neutral).");
            }

            ImGui::SliderFloat("Gamma Curve", &scene.postGamma, 0.4f, 2.2f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Gamma power curve exponent (1.0 = standard 2.2 display gamma).");
            }
            ImGui::Spacing();
        }

        // ====================================================================
        // 3. COLOR GRADING & WHITE BALANCE
        // ====================================================================
        if (ImGui::CollapsingHeader("COLOR GRADING & WHITE BALANCE", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            ImGui::SliderFloat("Saturation", &scene.postSaturation, 0.0f, 2.5f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Color vibrance and intensity (0.0 = Grayscale/Monochrome, 1.0 = Natural, 2.0 = Saturated).");
            }

            ImGui::ColorEdit3("Color Filter / Grading Tint", &scene.postColorFilter.x);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Direct RGB tint filter multiplied across final scene pixels.");
            }

            ImGui::SliderFloat("Color Temperature (K)", &scene.postColorTemperature, 2000.0f, 12000.0f, "%.0f K");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("White balance color temperature in Kelvin. 6500K is neutral daylight. <6500K is warm/amber, >6500K is cool/blue.");
            }
            ImGui::TextDisabled("  Reference: 2500K (Candle) | 4000K (Warm Sun) | 6500K (Daylight) | 9000K (Overcast/Cool)");

            ImGui::SliderFloat("Color Tint (Green - Magenta)", &scene.postColorTint, -1.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("White balance tint axis: negative shifts towards green, positive shifts towards magenta.");
            }
            ImGui::Spacing();
        }

        // ====================================================================
        // 4. SHARPEN FILTER & DENOISER
        // ====================================================================
        if (ImGui::CollapsingHeader("DETAIL & FILTERING (SHARPEN & DENOISER)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            ImGui::SliderFloat("Sharpen Filter", &scene.postSharpenAmount, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Contrast Adaptive / High-Pass sharpening filter intensity. 0 = Off, >0 sharpens fine surface details.");
            }

            ImGui::SliderFloat("Denoiser", &scene.postDenoiserAmount, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Edge-preserving spatiotemporal noise reduction. 0 = Off, >0 suppresses high-frequency grain and noise.");
            }
            ImGui::Spacing();
        }

        // Reset to Defaults & Close Buttons
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Reset to Defaults##ResetPPModal", ImVec2(160, 30))) {
            scene.enablePostProcessing = false;
            scene.postExposureEV = 0.0f;
            scene.postExposureMultiplier = 1.0f;
            scene.postTonemapper = 1;
            scene.postSaturation = 1.0f;
            scene.postContrast = 1.0f;
            scene.postGamma = 1.0f;
            scene.postColorFilter = glm::vec3(1.0f, 1.0f, 1.0f);
            scene.postColorTemperature = 6500.0f;
            scene.postColorTint = 0.0f;
            scene.postSharpenAmount = 0.0f;
            scene.postDenoiserAmount = 0.0f;
            AddLog("LogWorld", "Reset post processing settings to defaults (Disabled).", 0);
        }

        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(100, 30))) {
            showPostProcessingModal = false;
        }
    }
    ImGui::End();
}

void EngineUI::RenderAmbientOcclusionModal(Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(620, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Ambient Occlusion Settings###AmbientOcclusionSettingsWin", &showAmbientOcclusionModal, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Screen Space Ambient Occlusion (SSAO / HBAO / MSAO)");
        ImGui::TextWrapped("Configure real-time Screen Space Ambient Occlusion parameters referencing WickedEngine. Approximates realistic ambient contact shadows and occluded crevices.");
        ImGui::Separator();

        // Master Enable Toggle
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.2f, 0.9f, 0.4f, 1.0f));
        if (ImGui::Checkbox("Enable Ambient Occlusion for Scene", &scene.enableAO)) {
            AddLog("LogRenderer", scene.enableAO ? "Ambient Occlusion Enabled" : "Ambient Occlusion Disabled", 0);
            Eunoia::PostProcess::PostProcessSystem::Get().SetAOMode(scene.enableAO ? (Eunoia::PostProcess::SSAOMode)scene.aoType : Eunoia::PostProcess::SSAOMode::Disabled);
            RequestViewportRedraw(15);
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (scene.enableAO && scene.aoType > 0) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "[ACTIVE]");
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "[DISABLED]");
        }
        ImGui::Spacing();

        // ====================================================================
        // 1. ALGORITHM SELECTION (WickedEngine wiRenderPath3D::AO)
        // ====================================================================
        if (ImGui::CollapsingHeader("AO ALGORITHM (WickedEngine Reference)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            const char* aoAlgorithms[] = {
                "Disabled",
                "SSAO (Simple Screen Space AO)",
                "HBAO (Horizon-Based AO)",
                "MSAO (Multi-Scale AO)",
                "RTAO (Ray-Traced AO)"
            };
            int curType = scene.aoType;
            ImGui::SetNextItemWidth(340.0f);
            if (ImGui::Combo("Algorithm##AoModalAlgo", &curType, aoAlgorithms, IM_ARRAYSIZE(aoAlgorithms))) {
                scene.aoType = curType;
                if (scene.aoType > 0) scene.enableAO = true;
                else scene.enableAO = false;
                Eunoia::PostProcess::PostProcessSystem::Get().SetAOMode((Eunoia::PostProcess::SSAOMode)scene.aoType);
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Choose Ambient Occlusion type (Reference: WickedEngine GraphicsWindow.cpp):\n"
                                  "- SSAO: Standard hemisphere ray sampling\n"
                                  "- HBAO: Horizon-Based AO with angular integration\n"
                                  "- MSAO: Multi-Scale AO with depth-aware upsampling\n"
                                  "- RTAO: Hardware DXR ray-traced ambient occlusion");
            }

            ImGui::TextDisabled("Selected Mode: %s", aoAlgorithms[std::clamp(scene.aoType, 0, 4)]);
            ImGui::Spacing();
        }

        // ====================================================================
        // 2. SAMPLING & OCCLUSION TUNING (WickedEngine Parameters)
        // ====================================================================
        if (ImGui::CollapsingHeader("AO PROPERTIES & TUNING", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();

            // Power (WickedEngine aoPowerSlider: 0.25 - 8.0)
            if (ImGui::SliderFloat("Power##AoPower", &scene.aoPower, 0.25f, 8.0f, "%.2f")) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Set SSAO Power (WickedEngine aoPower: 0.25 - 8.0).\nExponent applied to occlusion: higher values darken ambient crevices and contact corners.");
            }

            // Quick Power preset buttons
            ImGui::TextDisabled("Quick Power:");
            ImGui::SameLine();
            if (ImGui::SmallButton("0.5 (Soft)")) { scene.aoPower = 0.5f; RequestViewportRedraw(15); }
            ImGui::SameLine();
            if (ImGui::SmallButton("1.0 (Standard)")) { scene.aoPower = 1.0f; RequestViewportRedraw(15); }
            ImGui::SameLine();
            if (ImGui::SmallButton("1.5 (Balanced)")) { scene.aoPower = 1.5f; RequestViewportRedraw(15); }
            ImGui::SameLine();
            if (ImGui::SmallButton("2.0 (High)")) { scene.aoPower = 2.0f; RequestViewportRedraw(15); }
            ImGui::SameLine();
            if (ImGui::SmallButton("3.5 (Deep)")) { scene.aoPower = 3.5f; RequestViewportRedraw(15); }

            // Range (WickedEngine aoRangeSlider: 0.1 - 20.0 m)
            if (ImGui::SliderFloat("Range (Radius)##AoRange", &scene.aoRange, 0.1f, 20.0f, "%.2f m")) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Set AO sample radius / ray length in world meters (WickedEngine aoRange).\nControls the physical distance across which ambient occlusion spreads.");
            }

            // Sample Count (WickedEngine aoSampleCountSlider: 1 - 64)
            if (ImGui::SliderInt("Sample Count##AoSamples", &scene.aoSampleCount, 1, 64)) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Set AO ray sample count per pixel (WickedEngine aoSampleCount: 1 - 32).\nMore samples produce cleaner, smoother occlusion gradients.");
            }

            // Bias (Self-occlusion bias)
            if (ImGui::SliderFloat("Angle / Depth Bias##AoBias", &scene.aoBias, 0.001f, 0.1f, "%.4f")) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Threshold bias to prevent false self-occlusion artifacts on flat or low-curvature surfaces.");
            }
            ImGui::Spacing();
        }

        // ====================================================================
        // 3. BILATERAL BLUR & TEMPORAL ACCUMULATION
        // ====================================================================
        if (ImGui::CollapsingHeader("DENOISING & FILTERING (WickedEngine Filter)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();

            if (ImGui::Checkbox("Edge-Preserving Bilateral Blur##AoBlur", &scene.aoBlur)) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Applies depth-aware cross-bilateral blur to eliminate ray sampling grain while preserving crisp silhouette geometry.");
            }

            if (scene.aoBlur) {
                if (ImGui::SliderFloat("Blur Radius (Pixels)##AoBlurRad", &scene.aoBlurRadius, 0.5f, 5.0f, "%.1f px")) {
                    RequestViewportRedraw(15);
                }
            }

            if (ImGui::Checkbox("Temporal Accumulation & Reprojection##AoTemporal", &scene.aoTemporal)) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Blends AO history across frames using camera velocity vectors (WickedEngine temporal filter). Eliminates ray variance flicker.");
            }

            if (scene.aoTemporal) {
                if (ImGui::SliderFloat("Temporal Blend Factor##AoBlend", &scene.aoTemporalBlend, 0.5f, 0.98f, "%.2f")) {
                    RequestViewportRedraw(15);
                }
            }
            ImGui::Spacing();
        }

        // ====================================================================
        // 4. QUICK PRESETS
        // ====================================================================
        if (ImGui::CollapsingHeader("QUALITY PRESETS", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (ImGui::Button("Low (Fast - 8 Samples)", ImVec2(170, 26))) {
                scene.enableAO = true; scene.aoType = 1; scene.aoSampleCount = 8; scene.aoRange = 0.8f; scene.aoPower = 1.0f; scene.aoBlur = true;
                Eunoia::PostProcess::PostProcessSystem::Get().ApplySSEQualityPreset(1);
                RequestViewportRedraw(15);
            }
            ImGui::SameLine();
            if (ImGui::Button("Medium (Balanced - 16 Samples)", ImVec2(190, 26))) {
                scene.enableAO = true; scene.aoType = 1; scene.aoSampleCount = 16; scene.aoRange = 1.0f; scene.aoPower = 1.5f; scene.aoBlur = true;
                Eunoia::PostProcess::PostProcessSystem::Get().ApplySSEQualityPreset(2);
                RequestViewportRedraw(15);
            }
            ImGui::SameLine();
            if (ImGui::Button("High (Quality - 24 Samples)", ImVec2(180, 26))) {
                scene.enableAO = true; scene.aoType = 1; scene.aoSampleCount = 24; scene.aoRange = 1.5f; scene.aoPower = 1.8f; scene.aoBlur = true;
                Eunoia::PostProcess::PostProcessSystem::Get().ApplySSEQualityPreset(3);
                RequestViewportRedraw(15);
            }

            if (ImGui::Button("Ultra (Cinematic - 32 Samples)", ImVec2(200, 26))) {
                scene.enableAO = true; scene.aoType = 1; scene.aoSampleCount = 32; scene.aoRange = 2.0f; scene.aoPower = 2.0f; scene.aoBlur = true;
                Eunoia::PostProcess::PostProcessSystem::Get().ApplySSEQualityPreset(4);
                RequestViewportRedraw(15);
            }
            ImGui::SameLine();
            if (ImGui::Button("Disable AO", ImVec2(120, 26))) {
                scene.enableAO = false; scene.aoType = 0;
                Eunoia::PostProcess::PostProcessSystem::Get().SetAOMode(Eunoia::PostProcess::SSAOMode::Disabled);
                RequestViewportRedraw(15);
            }
            ImGui::Spacing();
        }

        // Reset to Defaults & Close Buttons
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Reset to Defaults##ResetAOModal", ImVec2(160, 30))) {
            scene.enableAO = true;
            scene.aoType = 1;
            scene.aoPower = 1.0f;
            scene.aoRange = 1.0f;
            scene.aoSampleCount = 16;
            scene.aoBias = 0.025f;
            scene.aoBlur = true;
            scene.aoBlurRadius = 2.0f;
            scene.aoTemporal = true;
            scene.aoTemporalBlend = 0.85f;
            Eunoia::PostProcess::PostProcessSystem::Get().SetAOMode(Eunoia::PostProcess::SSAOMode::SSAO);
            AddLog("LogRenderer", "Reset ambient occlusion settings to defaults.", 0);
            RequestViewportRedraw(15);
        }

        ImGui::SameLine();
        if (ImGui::Button("Close##CloseAOModal", ImVec2(100, 30))) {
            showAmbientOcclusionModal = false;
        }
    }
    ImGui::End();
}

void EngineUI::RenderVolumetricFogModal(Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(560, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("World Volumetric Fog Settings###WorldVolumetricFogWin", &showVolumetricFogModal, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Wicked Engine Volumetric Height Fog");
        ImGui::TextWrapped("Configure real-time exponential height fog parameters referencing Wicked Engine fogHF.hlsli. Blends smoothly across distance and altitude.");
        ImGui::Separator();

        // Master Enable Toggle
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.2f, 0.9f, 0.4f, 1.0f));
        if (ImGui::Checkbox("Enable Volumetric Fog for Scene", &scene.enableFog)) {
            AddLog("LogRenderer", scene.enableFog ? "Volumetric Fog Enabled" : "Volumetric Fog Disabled", 0);
            RequestViewportRedraw(15);
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (scene.enableFog) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "[ACTIVE]");
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "[DISABLED]");
        }
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("FOG PROPERTIES (fogHF.hlsli Reference)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();

            if (ImGui::SliderFloat("Fog Start Distance", &scene.fogStart, 0.0f, 500.0f, "%.1f m")) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Distance from camera where fog begins to appear.");
            }

            if (ImGui::SliderFloat("Global Fog Density", &scene.fogDensity, 0.0001f, 0.05f, "%.5f")) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Exponential fog extinction factor (Wicked Engine density).");
            }

            if (ImGui::SliderFloat("Height Start (Bottom)", &scene.fogHeightStart, -100.0f, 200.0f, "%.1f m")) {
                if (scene.fogHeightEnd < scene.fogHeightStart) scene.fogHeightEnd = scene.fogHeightStart + 1.0f;
                RequestViewportRedraw(15);
            }

            if (ImGui::SliderFloat("Height End (Top)", &scene.fogHeightEnd, scene.fogHeightStart, 500.0f, "%.1f m")) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Upper altitude boundary where fog thins out to zero.");
            }

            if (ImGui::ColorEdit3("Fog Scattering Color", &scene.fogColor.x)) {
                RequestViewportRedraw(15);
            }
            ImGui::Spacing();
        }

        if (ImGui::CollapsingHeader("QUICK PRESETS", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (ImGui::Button("Morning Mist", ImVec2(120, 26))) {
                scene.enableFog = true; scene.fogStart = 15.0f; scene.fogDensity = 0.008f;
                scene.fogHeightStart = -5.0f; scene.fogHeightEnd = 30.0f;
                scene.fogColor = glm::vec3(0.75f, 0.80f, 0.90f);
                RequestViewportRedraw(15);
            }
            ImGui::SameLine();
            if (ImGui::Button("Dense Valley Fog", ImVec2(140, 26))) {
                scene.enableFog = true; scene.fogStart = 5.0f; scene.fogDensity = 0.020f;
                scene.fogHeightStart = -10.0f; scene.fogHeightEnd = 20.0f;
                scene.fogColor = glm::vec3(0.65f, 0.70f, 0.75f);
                RequestViewportRedraw(15);
            }
            ImGui::SameLine();
            if (ImGui::Button("Atmospheric Haze", ImVec2(140, 26))) {
                scene.enableFog = true; scene.fogStart = 50.0f; scene.fogDensity = 0.002f;
                scene.fogHeightStart = -50.0f; scene.fogHeightEnd = 200.0f;
                scene.fogColor = glm::vec3(0.70f, 0.75f, 0.85f);
                RequestViewportRedraw(15);
            }
            ImGui::SameLine();
            if (ImGui::Button("Disable Fog", ImVec2(100, 26))) {
                scene.enableFog = false;
                RequestViewportRedraw(15);
            }
            ImGui::Spacing();
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Reset to Defaults##ResetFogModal", ImVec2(160, 30))) {
            scene.enableFog = false;
            scene.fogStart = 10.0f;
            scene.fogDensity = 0.005f;
            scene.fogHeightStart = 0.0f;
            scene.fogHeightEnd = 50.0f;
            scene.fogColor = glm::vec3(0.7f, 0.75f, 0.85f);
            AddLog("LogRenderer", "Reset volumetric fog settings to defaults.", 0);
            RequestViewportRedraw(15);
        }

        ImGui::SameLine();
        if (ImGui::Button("Close##CloseFogModal", ImVec2(100, 30))) {
            showVolumetricFogModal = false;
        }
    }
    ImGui::End();
}







bool EngineUI::IsBehaviourNameAlreadyExists(const std::string& rawName) const {
    if (rawName.empty()) return false;

    std::string nameLower = rawName;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

    // 1. Check BehaviourRegistry (built-in and registered scripts)
    const auto& all = BehaviourRegistry::Get().GetAll();
    for (const auto& pair : all) {
        std::string classLower = pair.second.className;
        std::transform(classLower.begin(), classLower.end(), classLower.begin(), ::tolower);
        std::string displayLower = pair.second.displayName;
        std::transform(displayLower.begin(), displayLower.end(), displayLower.begin(), ::tolower);
        if (classLower == nameLower || displayLower == nameLower) {
            return true;
        }
    }

    // 2. Check AssetRegistry
    const auto& assets = AssetRegistry::Get().GetAllAssets();
    for (const auto& pair : assets) {
        if (pair.second.type == AssetType::Behaviour || pair.second.type == AssetType::Script || pair.second.type == AssetType::VisualBehaviour) {
            std::string objLower = pair.second.objectName;
            std::transform(objLower.begin(), objLower.end(), objLower.begin(), ::tolower);
            if (objLower == nameLower) {
                return true;
            }
        }
    }

    // 3. Scan Content root directory and project directory on disk
    std::filesystem::path searchRoot = !contentRootPath.empty() ? contentRootPath : activeProjectRoot;
    if (!searchRoot.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(searchRoot, ec)) {
            for (auto it = std::filesystem::recursive_directory_iterator(searchRoot, std::filesystem::directory_options::skip_permission_denied, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator();
                 it.increment(ec)) {
                if (!it->is_directory(ec)) {
                    std::string ext = it->path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".ebehaviour") {
                        std::string fileStem = it->path().stem().string();
                        std::transform(fileStem.begin(), fileStem.end(), fileStem.begin(), ::tolower);
                        if (fileStem == nameLower) {
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}
