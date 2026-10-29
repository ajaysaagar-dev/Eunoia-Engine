#include <Level-Animation/LevelAnimationEditor.h>
#include <Level-Animation/LevelAnimationManager.h>
#include <EngineScene/Scene.h>
#include <EngineScene/GameObject.h>
#include <EngineAssets/AssetRegistry.h>
#include <iostream>
#include <cmath>
#include <algorithm>

namespace Eunoia {

LevelAnimationEditor::LevelAnimationEditor() {
    m_currentTime = 0.0f;
    m_duration = 5.0f;
    m_isPlaying = false;
    m_loop = true;
    m_playbackSpeed = 1.0f;
    m_timelineZoom = 120.0f;
    m_scrollX = 0.0f;
    m_sidebarWidth = 260.0f;
}

LevelAnimationEditor& LevelAnimationEditor::Get() {
    static LevelAnimationEditor s_instance;
    return s_instance;
}

void LevelAnimationEditor::SetActiveAnimation(std::shared_ptr<LevelAnimation> anim) {
    LevelAnimationManager::Get().SetActiveAnimation(anim);
    if (anim) {
        m_duration = anim->duration;
        m_loop = anim->loop;
        if (m_currentTime > m_duration) {
            m_currentTime = m_duration;
        }
    }
}

std::shared_ptr<LevelAnimation> LevelAnimationEditor::GetActiveAnimation() {
    return LevelAnimationManager::Get().GetActiveAnimation();
}

void LevelAnimationEditor::Play() {
    m_isPlaying = true;
}

void LevelAnimationEditor::Pause() {
    m_isPlaying = false;
}

void LevelAnimationEditor::Stop() {
    m_isPlaying = false;
    SetCurrentTime(0.0f);
}

void LevelAnimationEditor::StepForward(float step) {
    SetCurrentTime(m_currentTime + step);
}

void LevelAnimationEditor::StepBackward(float step) {
    SetCurrentTime(m_currentTime - step);
}

void LevelAnimationEditor::SetCurrentTime(float t) {
    m_currentTime = std::clamp(t, 0.0f, m_duration);
    LevelAnimationManager::Get().SetCurrentPlayheadTime(m_currentTime);
}

void LevelAnimationEditor::SetDuration(float d) {
    m_duration = std::max(0.1f, d);
    auto anim = GetActiveAnimation();
    if (anim) {
        anim->duration = m_duration;
        if (!anim->assetPath.empty()) {
            anim->SaveToFile(anim->assetPath);
        }
    }
    if (m_currentTime > m_duration) {
        SetCurrentTime(m_duration);
    }
}

void LevelAnimationEditor::UpdatePlayback(Scene& scene, float dt) {
    auto anim = GetActiveAnimation();
    if (!anim) return;

    if (m_isPlaying) {
        m_currentTime += dt * m_playbackSpeed;
        if (m_currentTime > m_duration) {
            if (m_loop && m_duration > 0.001f) {
                m_currentTime = std::fmod(m_currentTime, m_duration);
            } else {
                m_currentTime = m_duration;
                m_isPlaying = false;
            }
        }
        LevelAnimationManager::Get().SetCurrentPlayheadTime(m_currentTime);
        anim->Evaluate(m_currentTime, scene);
    }
}

void LevelAnimationEditor::RenderTimeline(Scene& scene, const ImVec2& pos, const ImVec2& size) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // 1. Top Transport & Level Animation Selection Toolbar
    RenderTopBar(scene, size.x);

    ImGui::Separator();

    float topBarH = 42.0f;
    float contentH = size.y - topBarH;
    if (contentH < 50.0f) contentH = 50.0f;

    // 2. Left Track Hierarchy Sidebar
    RenderLeftSidebar(scene, m_sidebarWidth, contentH);

    ImGui::SameLine(0.0f, 4.0f);

    // 3. Main Timeline Graph (Ruler, Dope Sheet, Playhead)
    float graphW = size.x - m_sidebarWidth - 8.0f;
    if (graphW < 100.0f) graphW = 100.0f;
    RenderTimelineGraph(scene, graphW, contentH);
}

void LevelAnimationEditor::RenderTopBar(Scene& scene, float width) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));

    // A. Dropdown listing all Level Animations in the current scene
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "Level Animation:");
    ImGui::SameLine();

    std::vector<std::pair<int, std::string>> levelAnims; // actorId -> label
    for (const auto& obj : scene.objects) {
        if (obj.hasLevelAnimation || obj.type == PrimitiveType::LevelAnimation || !obj.levelAnimation.animationPath.empty()) {
            std::string label = obj.name;
            if (!obj.levelAnimation.animationPath.empty()) {
                label += " (" + std::filesystem::path(obj.levelAnimation.animationPath).filename().string() + ")";
            }
            levelAnims.push_back({ obj.id, label });
        }
    }

    auto activeAnim = GetActiveAnimation();
    int activeActorId = LevelAnimationManager::Get().GetActiveActorId();
    std::string currentComboLabel = "None Selected (Select or Create)";
    if (activeAnim && !activeAnim->name.empty()) {
        currentComboLabel = activeAnim->name;
        if (activeActorId != -1) {
            GameObject* act = scene.FindObject(activeActorId);
            if (act) currentComboLabel = act->name + " [" + activeAnim->name + "]";
        }
    }

    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::BeginCombo("##LevelAnimSelectCombo", currentComboLabel.c_str())) {
        if (levelAnims.empty()) {
            ImGui::TextDisabled("No Level Animations in Scene");
            ImGui::TextDisabled("Right-click in Content Browser -> New Level Animation");
        } else {
            for (const auto& item : levelAnims) {
                bool isSelected = (activeActorId == item.first);
                if (ImGui::Selectable(item.second.c_str(), isSelected)) {
                    LevelAnimationManager::Get().SetActiveActorId(item.first);
                    GameObject* act = scene.FindObject(item.first);
                    if (act) {
                        if (!act->levelAnimation.animationPath.empty()) {
                            auto loaded = LevelAnimationManager::Get().LoadLevelAnimation(act->levelAnimation.animationPath);
                            SetActiveAnimation(loaded);
                        } else if (act->levelAnimation.animation) {
                            SetActiveAnimation(act->levelAnimation.animation);
                        }
                    }
                }
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine(0.0f, 12.0f);

    // B. Transport Controls
    if (ImGui::Button("⏮##StepBack", ImVec2(28, 26))) {
        StepBackward();
        if (activeAnim) activeAnim->Evaluate(m_currentTime, scene);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step Backward (1 frame)");

    ImGui::SameLine();
    if (m_isPlaying) {
        if (ImGui::Button("⏸##Pause", ImVec2(32, 26))) {
            Pause();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause Playback");
    } else {
        if (ImGui::Button("▶##Play", ImVec2(32, 26))) {
            Play();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play Animation");
    }

    ImGui::SameLine();
    if (ImGui::Button("⏹##Stop", ImVec2(28, 26))) {
        Stop();
        if (activeAnim) activeAnim->Evaluate(m_currentTime, scene);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop & Return to 0.0s");

    ImGui::SameLine();
    if (ImGui::Button("⏭##StepFwd", ImVec2(28, 26))) {
        StepForward();
        if (activeAnim) activeAnim->Evaluate(m_currentTime, scene);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Step Forward (1 frame)");

    ImGui::SameLine(0.0f, 12.0f);

    // C. Record Button [● REC]
    bool isRec = LevelAnimationManager::Get().IsRecording();
    if (isRec) {
        // Pulsing glowing red active record button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.20f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.30f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.14f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.35f, 0.35f, 1.0f));
    }

    if (ImGui::Button(isRec ? "● REC [ON]" : "● REC", ImVec2(85, 26))) {
        LevelAnimationManager::Get().SetRecording(!isRec);
    }
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Auto-Key Record Mode:\nWhen active, modifying properties (Transform, Light, Camera, Material) in the Details panel automatically creates/updates keyframes at the current playhead time.");
    }

    ImGui::SameLine(0.0f, 14.0f);

    // D. Time Readout: 0.00s / Duration
    ImGui::AlignTextToFramePadding();
    char timeBuf[64];
    snprintf(timeBuf, sizeof(timeBuf), "Time: %.2fs", m_currentTime);
    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "%s", timeBuf);

    ImGui::SameLine(0.0f, 12.0f);

    // E. Duration / Custom End Timeline
    ImGui::AlignTextToFramePadding();
    ImGui::Text("End Timeline:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(75.0f);
    float d = m_duration;
    if (ImGui::DragFloat("##EndDuration", &d, 0.1f, 0.1f, 3600.0f, "%.1fs")) {
        SetDuration(d);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Custom End Timeline duration in seconds (starting at 0.0s)");

    ImGui::SameLine(0.0f, 10.0f);

    // F. Loop Checkbox
    if (ImGui::Checkbox("Loop##TimelineLoop", &m_loop)) {
        if (activeAnim) {
            activeAnim->loop = m_loop;
            if (!activeAnim->assetPath.empty()) activeAnim->SaveToFile(activeAnim->assetPath);
        }
    }

    ImGui::SameLine(0.0f, 10.0f);

    // G. Zoom Slider
    ImGui::SetNextItemWidth(80.0f);
    ImGui::SliderFloat("Zoom##TimelineZoom", &m_timelineZoom, 30.0f, 400.0f, "%.0f px/s");

    ImGui::PopStyleVar(2);
}

void LevelAnimationEditor::RenderLeftSidebar(Scene& scene, float width, float height) {
    ImGui::BeginChild("LevelAnimLeftSidebar", ImVec2(width, height), true, ImGuiWindowFlags_NoScrollbar);

    auto anim = GetActiveAnimation();

    // Top of sidebar: "+ Add Object" Button
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.45f, 0.85f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.55f, 0.95f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.35f, 0.75f, 1.0f));
    if (ImGui::Button("+ Add Object to Animation", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
        m_openAddObjectPopup = true;
    }
    ImGui::PopStyleColor(3);

    if (m_openAddObjectPopup) {
        ImGui::OpenPopup("AddLevelObjectPopup");
        m_openAddObjectPopup = false;
        m_searchFilterBuf[0] = '\0';
    }

    // Modal popup to select objects from current level
    if (ImGui::BeginPopup("AddLevelObjectPopup")) {
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Select Level Object to Animate:");
        ImGui::Separator();

        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputTextWithHint("##FilterObjects", "Search object name...", m_searchFilterBuf, sizeof(m_searchFilterBuf));
        std::string filter = m_searchFilterBuf;
        std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

        ImGui::BeginChild("ObjectSelectList", ImVec2(260.0f, 220.0f), true);
        for (const auto& obj : scene.objects) {
            // Filter
            if (!filter.empty()) {
                std::string objLower = obj.name;
                std::transform(objLower.begin(), objLower.end(), objLower.begin(), ::tolower);
                if (objLower.find(filter) == std::string::npos) continue;
            }

            std::string label = obj.name + " (" + GetPrimitiveTypeName(obj.type) + ")";
            if (ImGui::Selectable(label.c_str())) {
                if (anim) {
                    AnimatedObjectTrack* ot = anim->AddObject(obj.id, obj.name);
                    if (ot) {
                        // Populate standard property tracks and snapshot initial keys
                        PropertyTrack& px = ot->FindOrCreateTrack(PropertyChannelType::PositionX);
                        PropertyTrack& py = ot->FindOrCreateTrack(PropertyChannelType::PositionY);
                        PropertyTrack& pz = ot->FindOrCreateTrack(PropertyChannelType::PositionZ);
                        PropertyTrack& rx = ot->FindOrCreateTrack(PropertyChannelType::RotationX);
                        PropertyTrack& ry = ot->FindOrCreateTrack(PropertyChannelType::RotationY);
                        PropertyTrack& rz = ot->FindOrCreateTrack(PropertyChannelType::RotationZ);
                        PropertyTrack& sx = ot->FindOrCreateTrack(PropertyChannelType::ScaleX);
                        PropertyTrack& sy = ot->FindOrCreateTrack(PropertyChannelType::ScaleY);
                        PropertyTrack& sz = ot->FindOrCreateTrack(PropertyChannelType::ScaleZ);

                        if (px.keyframes.empty()) px.AddOrUpdateKey(0.0f, obj.position.x);
                        if (py.keyframes.empty()) py.AddOrUpdateKey(0.0f, obj.position.y);
                        if (pz.keyframes.empty()) pz.AddOrUpdateKey(0.0f, obj.position.z);
                        if (rx.keyframes.empty()) rx.AddOrUpdateKey(0.0f, obj.rotation.x);
                        if (ry.keyframes.empty()) ry.AddOrUpdateKey(0.0f, obj.rotation.y);
                        if (rz.keyframes.empty()) rz.AddOrUpdateKey(0.0f, obj.rotation.z);
                        if (sx.keyframes.empty()) sx.AddOrUpdateKey(0.0f, obj.scale.x);
                        if (sy.keyframes.empty()) sy.AddOrUpdateKey(0.0f, obj.scale.y);
                        if (sz.keyframes.empty()) sz.AddOrUpdateKey(0.0f, obj.scale.z);

                        if (obj.isLight || IsLightPrimitive(obj.type)) {
                            PropertyTrack& lint = ot->FindOrCreateTrack(PropertyChannelType::LightIntensity);
                            PropertyTrack& lcolR = ot->FindOrCreateTrack(PropertyChannelType::LightColorR);
                            PropertyTrack& lcolG = ot->FindOrCreateTrack(PropertyChannelType::LightColorG);
                            PropertyTrack& lcolB = ot->FindOrCreateTrack(PropertyChannelType::LightColorB);
                            if (lint.keyframes.empty()) lint.AddOrUpdateKey(0.0f, obj.light.intensity);
                            if (lcolR.keyframes.empty()) lcolR.AddOrUpdateKey(0.0f, obj.light.color.r);
                            if (lcolG.keyframes.empty()) lcolG.AddOrUpdateKey(0.0f, obj.light.color.g);
                            if (lcolB.keyframes.empty()) lcolB.AddOrUpdateKey(0.0f, obj.light.color.b);
                        }

                        if (obj.isCamera || obj.type == PrimitiveType::Camera) {
                            PropertyTrack& cfov = ot->FindOrCreateTrack(PropertyChannelType::CameraFOV);
                            if (cfov.keyframes.empty()) cfov.AddOrUpdateKey(0.0f, obj.camera.fov);
                        }

                        if (!anim->assetPath.empty()) anim->SaveToFile(anim->assetPath);
                    }
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Track Tree
    if (!anim || anim->objectTracks.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No tracks added yet.");
        ImGui::TextDisabled("Click '+ Add Object to Animation'\nto choose objects from the level.");
        ImGui::EndChild();
        return;
    }

    ImGui::BeginChild("TrackListChild", ImVec2(0, 0), false);

    int objToRemove = -1;
    for (size_t oi = 0; oi < anim->objectTracks.size(); ++oi) {
        auto& ot = anim->objectTracks[oi];
        ImGui::PushID((int)oi);

        GameObject* sceneObj = scene.FindObject(ot.targetObjectId);

        // Object Row
        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (ot.isExpanded) nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;

        std::string objTitle = ot.targetObjectName + " [ID: " + std::to_string(ot.targetObjectId) + "]";
        bool isOpen = ImGui::TreeNodeEx(objTitle.c_str(), nodeFlags);
        ot.isExpanded = isOpen;

        // Remove object button on the right
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 14.0f);
        if (ImGui::SmallButton("✕##RemoveObj")) {
            objToRemove = ot.targetObjectId;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove object from this animation");

        if (isOpen) {
            for (size_t ti = 0; ti < ot.propertyTracks.size(); ++ti) {
                auto& pt = ot.propertyTracks[ti];
                ImGui::PushID((int)ti);

                float curVal = pt.Evaluate(m_currentTime);

                ImGui::Indent(16.0f);
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s:", pt.channelName.c_str());
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%.2f", curVal);

                // Add Keyframe button [+] at current playhead
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 16.0f);
                if (ImGui::SmallButton("+##AddKey")) {
                    float keyVal = curVal;
                    if (sceneObj) {
                        switch (pt.channelType) {
                            case PropertyChannelType::PositionX: keyVal = sceneObj->position.x; break;
                            case PropertyChannelType::PositionY: keyVal = sceneObj->position.y; break;
                            case PropertyChannelType::PositionZ: keyVal = sceneObj->position.z; break;
                            case PropertyChannelType::RotationX: keyVal = sceneObj->rotation.x; break;
                            case PropertyChannelType::RotationY: keyVal = sceneObj->rotation.y; break;
                            case PropertyChannelType::RotationZ: keyVal = sceneObj->rotation.z; break;
                            case PropertyChannelType::ScaleX: keyVal = sceneObj->scale.x; break;
                            case PropertyChannelType::ScaleY: keyVal = sceneObj->scale.y; break;
                            case PropertyChannelType::ScaleZ: keyVal = sceneObj->scale.z; break;
                            case PropertyChannelType::LightIntensity: keyVal = sceneObj->light.intensity; break;
                            case PropertyChannelType::CameraFOV: keyVal = sceneObj->camera.fov; break;
                            default: break;
                        }
                    }
                    pt.AddOrUpdateKey(m_currentTime, keyVal);
                    if (!anim->assetPath.empty()) anim->SaveToFile(anim->assetPath);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Insert Keyframe at current playhead");

                ImGui::Unindent(16.0f);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    if (objToRemove != -1) {
        anim->RemoveObject(objToRemove);
        if (!anim->assetPath.empty()) anim->SaveToFile(anim->assetPath);
    }

    ImGui::EndChild();
    ImGui::EndChild();
}

void LevelAnimationEditor::RenderTimelineGraph(Scene& scene, float width, float height) {
    ImGui::BeginChild("LevelAnimTimelineGraph", ImVec2(width, height), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    auto anim = GetActiveAnimation();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImVec2(width, height);

    float rulerHeight = 24.0f;
    float graphTop = canvasPos.y + rulerHeight;
    float graphHeight = canvasSize.y - rulerHeight;

    // Handle horizontal mouse drag panning
    if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        m_scrollX -= ImGui::GetIO().MouseDelta.x;
        if (m_scrollX < 0.0f) m_scrollX = 0.0f;
    }

    // Handle mouse wheel zoom
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
        m_timelineZoom += ImGui::GetIO().MouseWheel * 15.0f;
        m_timelineZoom = std::clamp(m_timelineZoom, 30.0f, 600.0f);
    }

    // 1. Draw Background Grid
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(20, 22, 26, 255));

    // Ruler bar background
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + rulerHeight),
        IM_COL32(28, 30, 36, 255));
    drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + rulerHeight),
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + rulerHeight), IM_COL32(45, 48, 56, 255), 1.0f);

    // 2. Draw Time Ruler Ticks & Labels
    float timeStep = 1.0f; // 1 second major ticks
    if (m_timelineZoom > 180.0f) timeStep = 0.5f;
    if (m_timelineZoom > 300.0f) timeStep = 0.2f;

    float maxDisplayTime = m_duration + 2.0f;
    for (float t = 0.0f; t <= maxDisplayTime; t += timeStep) {
        float x = canvasPos.x + t * m_timelineZoom - m_scrollX;
        if (x < canvasPos.x || x > canvasPos.x + canvasSize.x) continue;

        // Major tick on ruler
        drawList->AddLine(ImVec2(x, canvasPos.y + rulerHeight - 10.0f),
            ImVec2(x, canvasPos.y + rulerHeight), IM_COL32(180, 180, 190, 220), 1.0f);

        // Grid line in body
        drawList->AddLine(ImVec2(x, graphTop),
            ImVec2(x, canvasPos.y + canvasSize.y), IM_COL32(40, 44, 52, 140), 1.0f);

        char label[32];
        snprintf(label, sizeof(label), "%.1fs", t);
        drawList->AddText(ImVec2(x + 3.0f, canvasPos.y + 4.0f), IM_COL32(190, 190, 200, 255), label);
    }

    // Duration End Marker
    float endX = canvasPos.x + m_duration * m_timelineZoom - m_scrollX;
    if (endX >= canvasPos.x && endX <= canvasPos.x + canvasSize.x) {
        drawList->AddLine(ImVec2(endX, canvasPos.y), ImVec2(endX, canvasPos.y + canvasSize.y),
            IM_COL32(240, 160, 40, 200), 2.0f);
        drawList->AddText(ImVec2(endX + 3.0f, canvasPos.y + 6.0f), IM_COL32(240, 160, 40, 255), "END");
    }

    // 3. Render Property Track Rows and Keyframe Diamonds
    float rowHeight = 22.0f;
    float currentY = graphTop + 4.0f;

    ImVec2 mousePos = ImGui::GetMousePos();
    bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    bool rightClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    if (anim) {
        for (size_t oi = 0; oi < anim->objectTracks.size(); ++oi) {
            auto& ot = anim->objectTracks[oi];

            // Object header band
            drawList->AddRectFilled(ImVec2(canvasPos.x, currentY),
                ImVec2(canvasPos.x + canvasSize.x, currentY + rowHeight),
                IM_COL32(32, 35, 42, 180));
            currentY += rowHeight;

            if (ot.isExpanded) {
                for (size_t ti = 0; ti < ot.propertyTracks.size(); ++ti) {
                    auto& pt = ot.propertyTracks[ti];

                    // Channel row background alternating
                    ImU32 rowBg = (ti % 2 == 0) ? IM_COL32(24, 26, 31, 200) : IM_COL32(28, 30, 36, 200);
                    drawList->AddRectFilled(ImVec2(canvasPos.x, currentY),
                        ImVec2(canvasPos.x + canvasSize.x, currentY + rowHeight), rowBg);

                    // Keyframe Diamonds
                    float diamondY = currentY + rowHeight * 0.5f;
                    for (size_t ki = 0; ki < pt.keyframes.size(); ++ki) {
                        auto& k = pt.keyframes[ki];
                        float kx = canvasPos.x + k.time * m_timelineZoom - m_scrollX;

                        if (kx >= canvasPos.x - 10.0f && kx <= canvasPos.x + canvasSize.x + 10.0f) {
                            bool isSelected = (m_selectedTrackObjId == ot.targetObjectId &&
                                               m_selectedChannel == pt.channelType &&
                                               m_selectedKeyIndex == (int)ki);

                            float dSize = isSelected ? 6.5f : 5.0f;
                            ImVec2 pTop(kx, diamondY - dSize);
                            ImVec2 pRight(kx + dSize, diamondY);
                            ImVec2 pBottom(kx, diamondY + dSize);
                            ImVec2 pLeft(kx - dSize, diamondY);

                            ImU32 keyCol = isSelected ? IM_COL32(255, 225, 60, 255) : IM_COL32(80, 200, 255, 240);
                            drawList->AddQuadFilled(pTop, pRight, pBottom, pLeft, keyCol);
                            drawList->AddQuad(pTop, pRight, pBottom, pLeft, IM_COL32(20, 20, 20, 220), 1.0f);

                            // Hit detection on diamond
                            float distSq = (mousePos.x - kx) * (mousePos.x - kx) + (mousePos.y - diamondY) * (mousePos.y - diamondY);
                            if (distSq <= 40.0f) {
                                if (mouseClicked) {
                                    m_selectedTrackObjId = ot.targetObjectId;
                                    m_selectedChannel = pt.channelType;
                                    m_selectedKeyIndex = (int)ki;
                                    m_isDraggingKey = true;
                                    m_dragKeyInitialTime = k.time;
                                }
                                if (rightClicked) {
                                    m_selectedTrackObjId = ot.targetObjectId;
                                    m_selectedChannel = pt.channelType;
                                    m_selectedKeyIndex = (int)ki;
                                    ImGui::OpenPopup("KeyframeContextMenu");
                                }
                            }
                        }
                    }

                    currentY += rowHeight;
                }
            }
        }
    }

    // Keyframe Context Menu
    if (ImGui::BeginPopup("KeyframeContextMenu")) {
        if (anim && m_selectedTrackObjId != -1 && m_selectedKeyIndex != -1) {
            AnimatedObjectTrack* ot = anim->FindObjectTrack(m_selectedTrackObjId);
            if (ot) {
                PropertyTrack* pt = ot->FindTrack(m_selectedChannel);
                if (pt && m_selectedKeyIndex < (int)pt->keyframes.size()) {
                    auto& k = pt->keyframes[m_selectedKeyIndex];
                    ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Keyframe (Time: %.2fs)", k.time);
                    ImGui::Separator();

                    // Value edit
                    if (ImGui::DragFloat("Value##KeyVal", &k.value, 0.05f)) {
                        if (!anim->assetPath.empty()) anim->SaveToFile(anim->assetPath);
                    }

                    // Interpolation Mode
                    const char* interpModes[] = { "Linear", "Step", "Cubic (Smooth)" };
                    int curMode = static_cast<int>(k.interp);
                    if (ImGui::Combo("Interpolation", &curMode, interpModes, 3)) {
                        k.interp = static_cast<InterpolationMode>(curMode);
                        if (!anim->assetPath.empty()) anim->SaveToFile(anim->assetPath);
                    }

                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete Keyframe")) {
                        pt->RemoveKeyAt(m_selectedKeyIndex);
                        m_selectedKeyIndex = -1;
                        if (!anim->assetPath.empty()) anim->SaveToFile(anim->assetPath);
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
        }
        ImGui::EndPopup();
    }

    // Dragging selected keyframe
    if (m_isDraggingKey) {
        if (mouseReleased) {
            m_isDraggingKey = false;
            if (anim && !anim->assetPath.empty()) anim->SaveToFile(anim->assetPath);
        } else if (anim && m_selectedTrackObjId != -1 && m_selectedKeyIndex != -1) {
            AnimatedObjectTrack* ot = anim->FindObjectTrack(m_selectedTrackObjId);
            if (ot) {
                PropertyTrack* pt = ot->FindTrack(m_selectedChannel);
                if (pt && m_selectedKeyIndex < (int)pt->keyframes.size()) {
                    float newT = (mousePos.x - canvasPos.x + m_scrollX) / m_timelineZoom;
                    newT = std::clamp(newT, 0.0f, m_duration);
                    pt->keyframes[m_selectedKeyIndex].time = newT;
                    pt->SortKeys();
                    // Keep selected index tracking the key
                    for (size_t i = 0; i < pt->keyframes.size(); ++i) {
                        if (std::abs(pt->keyframes[i].time - newT) < 0.001f) {
                            m_selectedKeyIndex = (int)i;
                            break;
                        }
                    }
                }
            }
        }
    }

    // 4. Ruler Interaction & Playhead Scrubbing
    bool inRuler = (mousePos.x >= canvasPos.x && mousePos.x <= canvasPos.x + canvasSize.x &&
                    mousePos.y >= canvasPos.y && mousePos.y <= graphTop);

    if (inRuler && mouseClicked) {
        m_isDraggingPlayhead = true;
    }
    if (mouseReleased) {
        m_isDraggingPlayhead = false;
    }

    if (m_isDraggingPlayhead) {
        float newTime = (mousePos.x - canvasPos.x + m_scrollX) / m_timelineZoom;
        SetCurrentTime(newTime);
        if (anim) {
            anim->Evaluate(m_currentTime, scene);
        }
    }

    // 5. Draw Playhead Vertical Scrubber Line & Top Flag
    float playheadX = canvasPos.x + m_currentTime * m_timelineZoom - m_scrollX;
    if (playheadX >= canvasPos.x && playheadX <= canvasPos.x + canvasSize.x) {
        // Red vertical line
        drawList->AddLine(ImVec2(playheadX, canvasPos.y),
            ImVec2(playheadX, canvasPos.y + canvasSize.y), IM_COL32(255, 60, 60, 255), 2.0f);

        // Top triangle flag handle
        ImVec2 p1(playheadX - 6.0f, canvasPos.y);
        ImVec2 p2(playheadX + 6.0f, canvasPos.y);
        ImVec2 p3(playheadX, canvasPos.y + rulerHeight - 2.0f);
        drawList->AddTriangleFilled(p1, p2, p3, IM_COL32(255, 60, 60, 255));
    }

    ImGui::EndChild();
}

} // namespace Eunoia
