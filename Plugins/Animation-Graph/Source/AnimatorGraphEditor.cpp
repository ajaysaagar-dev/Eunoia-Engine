#include <Animation-Graph/AnimatorGraphEditor.h>
#include <EngineAssets/AssetManager.h>
#include <EngineAssets/AssetRegistry.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <filesystem>
#include <cmath>

namespace Eunoia {

// ============================================================================
// AnimatorGraphEditor Implementation
// ============================================================================

AnimatorGraphEditor::AnimatorGraphEditor(std::shared_ptr<AnimatorGraph> graph)
    : m_graph(graph)
{
    if (!m_graph) {
        m_graph = std::make_shared<AnimatorGraph>();
    }
    m_instance = std::make_unique<AnimatorInstance>(m_graph);
    m_defaultName = m_graph->name;
    m_defaultPath = m_graph->assetPath;

    SyncPreviewResources();
}

void AnimatorGraphEditor::SyncPreviewResources() {
    if (!m_graph) return;

    if (!m_graph->skeletonMeshPath.empty()) {
        m_previewMesh = SkeletalAnimationManager::Get().LoadSkeletalMesh(m_graph->skeletonMeshPath);
        if (m_previewMesh && m_previewMesh->valid) {
            ResetPreviewCamera();
        }
    }

    // Preload clips for all states
    for (const auto& s : m_graph->states) {
        if (!s.clipPath.empty() && m_loadedClips.find(s.clipPath) == m_loadedClips.end()) {
            const Skeleton* skel = m_previewMesh ? &m_previewMesh->skeleton : nullptr;
            auto clip = SkeletalAnimationManager::Get().LoadAnimationClip(s.clipPath, skel);
            if (clip) {
                m_loadedClips[s.clipPath] = clip;
            }
        }
    }
}

void AnimatorGraphEditor::Save() {
    if (m_graph && !m_graph->assetPath.empty()) {
        if (m_graph->SaveToFile(m_graph->assetPath)) {
            m_isDirty = false;
            m_saveNotificationTimer = 2.5f;
            std::cout << "[AnimatorGraph] Saved graph: " << m_graph->assetPath << std::endl;
        }
    }
}

void AnimatorGraphEditor::ResetPreviewCamera() {
    m_previewYaw = 45.0f;
    m_previewPitch = 15.0f;
    if (m_previewMesh && m_previewMesh->valid) {
        m_previewTarget = m_previewMesh->aabbCenter;
        m_previewDistance = std::max(1.5f, m_previewMesh->boundingRadius * 2.2f);
    } else {
        m_previewTarget = glm::vec3(0.0f, 1.0f, 0.0f);
        m_previewDistance = 3.5f;
    }
}

void AnimatorGraphEditor::UpdateSimulation(float deltaTime) {
    if (!m_instance) return;

    m_instance->Update(deltaTime);

    if (!m_previewMesh || m_previewMesh->skeleton.bones.empty()) return;

    const auto& skeleton = m_previewMesh->skeleton;
    std::vector<BoneTransform> activeTransforms;
    std::vector<BoneTransform> prevTransforms;

    // Sample current state
    const auto* curState = m_graph->FindState(m_instance->activeStateId);
    if (curState && !curState->clipPath.empty()) {
        auto it = m_loadedClips.find(curState->clipPath);
        if (it != m_loadedClips.end() && it->second) {
            it->second->SamplePose(m_instance->timeInCurrentState, skeleton, activeTransforms, curState->loop);
        }
    }
    if (activeTransforms.empty()) {
        activeTransforms.resize(skeleton.bones.size());
        for (size_t i = 0; i < skeleton.bones.size(); ++i) {
            activeTransforms[i].translation = glm::vec3(skeleton.bones[i].localBindTransform[3]);
            activeTransforms[i].rotation = glm::quat_cast(skeleton.bones[i].localBindTransform);
            activeTransforms[i].scale = glm::vec3(1.0f);
        }
    }

    // If transitioning, blend with previous state
    std::vector<BoneTransform> finalTransforms = activeTransforms;
    if (m_instance->isTransitioning && m_instance->previousStateId != 0) {
        const auto* prevState = m_graph->FindState(m_instance->previousStateId);
        if (prevState && !prevState->clipPath.empty()) {
            auto it = m_loadedClips.find(prevState->clipPath);
            if (it != m_loadedClips.end() && it->second) {
                it->second->SamplePose(m_instance->timeInPreviousState, skeleton, prevTransforms, prevState->loop);
            }
        }

        if (!prevTransforms.empty()) {
            float alpha = glm::clamp(m_instance->transitionProgress, 0.0f, 1.0f);
            for (size_t i = 0; i < skeleton.bones.size(); ++i) {
                if (i < prevTransforms.size() && i < activeTransforms.size()) {
                    finalTransforms[i] = BoneTransform::Lerp(prevTransforms[i], activeTransforms[i], alpha);
                }
            }
        }
    }

    // Compute bone matrices and deform mesh
    std::vector<glm::mat4> boneMatrices;
    m_previewMesh->ComputeBoneMatrices(finalTransforms, boneMatrices);
    m_previewMesh->DeformMesh(boneMatrices);
}

void AnimatorGraphEditor::Render(const ImVec2& position, const ImVec2& size) {
    float dt = ImGui::GetIO().DeltaTime;
    UpdateSimulation(dt);

    if (m_saveNotificationTimer > 0.0f) {
        m_saveNotificationTimer -= dt;
    }

    // Shortcut: Ctrl + S to save Animator Graph
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        Save();
    }

    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.176f, 0.176f, 0.176f, 1.0f)); // #2D2D2D background matching image.svg

    std::string winName = "AnimatorGraphWorkspace##" + (m_graph ? m_graph->assetPath : "default");
    if (ImGui::Begin(winName.c_str(), nullptr, flags)) {
        float leftWidth = 280.0f;
        float rightWidth = 369.0f; // Exactly matches image.svg width="369"
        float canvasWidth = std::max(200.0f, size.x - leftWidth - rightWidth);

        // 1. Left Sidebar
        RenderLeftSidebar(position, ImVec2(leftWidth, size.y));

        // 2. Central Canvas
        ImVec2 canvasPos(position.x + leftWidth, position.y);
        RenderCanvas(canvasPos, ImVec2(canvasWidth, size.y));

        // 3. Right Sidebar (Live Preview & Properties)
        ImVec2 rightPos(position.x + leftWidth + canvasWidth, position.y);
        RenderRightSidebar(rightPos, ImVec2(rightWidth, size.y));
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void AnimatorGraphEditor::RenderLeftSidebar(const ImVec2& pos, const ImVec2& size) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));

    if (ImGui::BeginChild("AGLeftSidebar", size, true, ImGuiWindowFlags_None)) {
        ImGui::TextColored(ImVec4(0.0f, 0.75f, 1.0f, 1.0f), "ANIMATOR GRAPH");
        ImGui::Separator();
        ImGui::Spacing();

        // 1. Skeleton Mesh Selector
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Target Skeleton Mesh:");
        std::string currentMeshLabel = m_graph->skeletonMeshPath.empty() 
            ? "[Select Skeleton Mesh...]" 
            : std::filesystem::path(m_graph->skeletonMeshPath).filename().string();

        // Resolve project content directory from graph asset path or AssetRegistry
        std::string contentDir = "";
        if (m_graph && !m_graph->assetPath.empty()) {
            std::string ap = m_graph->assetPath;
            std::replace(ap.begin(), ap.end(), '\\', '/');
            size_t cPos = ap.rfind("/Content");
            if (cPos != std::string::npos) {
                contentDir = ap.substr(0, cPos + 8);
            }
        }
        if (contentDir.empty()) {
            contentDir = AssetRegistry::Get().GetContentRoot().string();
        }

        ImGui::PushItemWidth(-1);
        if (ImGui::BeginCombo("##SkeletonMeshCombo", currentMeshLabel.c_str())) {
            std::vector<std::string> meshes = SkeletalAnimationManager::Get().FindProjectSkeletalMeshes(contentDir);
            if (meshes.empty()) {
                ImGui::Selectable("No Skeletal Meshes found in project", false, ImGuiSelectableFlags_Disabled);
            }
            for (const auto& meshPath : meshes) {
                std::string fname = std::filesystem::path(meshPath).filename().string();
                bool isSelected = (m_graph->skeletonMeshPath == meshPath);
                if (ImGui::Selectable(fname.c_str(), isSelected)) {
                    m_graph->skeletonMeshPath = meshPath;
                    m_isDirty = true;
                    SyncPreviewResources();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", meshPath.c_str());
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        if (m_graph->skeletonMeshPath.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Please select a skeleton mesh.");
        } else if (m_previewMesh) {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "Bones: %zu | Submeshes: %zu", 
                m_previewMesh->skeleton.bones.size(), m_previewMesh->submeshes.size());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 2. Compatible Animations in Project
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Project Animations:");
        ImGui::TextDisabled("Drag & drop to canvas to create state");
        
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
        if (ImGui::BeginChild("AnimListChild", ImVec2(0, 160.0f), true)) {
            std::vector<std::string> anims = SkeletalAnimationManager::Get().FindProjectAnimations(contentDir, m_graph->skeletonMeshPath);
            if (anims.empty()) {
                ImGui::TextDisabled("No compatible animations found");
            }
            for (const auto& animPath : anims) {
                std::string stem = std::filesystem::path(animPath).stem().string();
                std::string label = "  " + stem;
                ImGui::Selectable(label.c_str(), false);

                // Drag and drop source
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    ImGui::SetDragDropPayload("ANIM_CLIP_PATH", animPath.c_str(), animPath.length() + 1);
                    ImGui::Text("Add Animation State: %s", stem.c_str());
                    ImGui::EndDragDropSource();
                }

                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", animPath.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 3. Parameters Panel (Bool, Float, Int, String)
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Parameters:");
        ImGui::SameLine(ImGui::GetWindowWidth() - 40.0f);
        if (ImGui::Button("+##AddParamBtn", ImVec2(24, 20))) {
            m_showAddParamPopup = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add New Parameter");

        if (m_showAddParamPopup) {
            ImGui::OpenPopup("AddParameterPopup");
            m_showAddParamPopup = false;
        }

        if (ImGui::BeginPopup("AddParameterPopup")) {
            ImGui::Text("Create Parameter");
            ImGui::Separator();
            ImGui::InputText("Name##ParamNameInp", m_newParamNameBuf, sizeof(m_newParamNameBuf));
            
            const char* typeNames[] = { "Boolean", "Float", "Int", "String" };
            ImGui::Combo("Type##ParamTypeInp", &m_newParamTypeIdx, typeNames, 4);

            ImGui::Spacing();
            if (ImGui::Button("Add", ImVec2(80, 0))) {
                if (strlen(m_newParamNameBuf) > 0) {
                    m_graph->AddParameter(m_newParamNameBuf, (AnimatorParamType)m_newParamTypeIdx);
                    if (m_instance) {
                        m_instance->Reset();
                    }
                    m_isDirty = true;
                    snprintf(m_newParamNameBuf, sizeof(m_newParamNameBuf), "Param_%zu", m_graph->parameters.size() + 1);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(80, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // List Parameters with interactive runtime controls
        std::string paramToDelete = "";
        for (auto& p : m_graph->parameters) {
            ImGui::PushID(p.name.c_str());

            // Delete button
            if (ImGui::SmallButton("x")) {
                paramToDelete = p.name;
            }
            ImGui::SameLine();

            // Parameter name & type
            ImGui::Text("%s (%s):", p.name.c_str(), AnimatorParamTypeToString(p.type));

            // Interactive value control
            switch (p.type) {
                case AnimatorParamType::Boolean: {
                    bool val = m_instance ? m_instance->GetBool(p.name, p.boolValue) : p.boolValue;
                    if (ImGui::Checkbox("##pValBool", &val)) {
                        p.boolValue = val;
                        if (m_instance) m_instance->SetBool(p.name, val);
                        m_isDirty = true;
                    }
                    break;
                }
                case AnimatorParamType::Float: {
                    float val = m_instance ? m_instance->GetFloat(p.name, p.floatValue) : p.floatValue;
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::DragFloat("##pValFloat", &val, 0.05f, -1000.0f, 1000.0f, "%.2f")) {
                        p.floatValue = val;
                        if (m_instance) m_instance->SetFloat(p.name, val);
                        m_isDirty = true;
                    }
                    break;
                }
                case AnimatorParamType::Int: {
                    int val = m_instance ? m_instance->GetInt(p.name, p.intValue) : p.intValue;
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::DragInt("##pValInt", &val, 1, -1000, 1000)) {
                        p.intValue = val;
                        if (m_instance) m_instance->SetInt(p.name, val);
                        m_isDirty = true;
                    }
                    break;
                }
                case AnimatorParamType::String: {
                    char strBuf[128];
                    std::string curStr = m_instance ? m_instance->GetString(p.name, p.stringValue) : p.stringValue;
                    strncpy_s(strBuf, sizeof(strBuf), curStr.c_str(), sizeof(strBuf) - 1);
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::InputText("##pValStr", strBuf, sizeof(strBuf))) {
                        p.stringValue = strBuf;
                        if (m_instance) m_instance->SetString(p.name, strBuf);
                        m_isDirty = true;
                    }
                    break;
                }
            }

            ImGui::PopID();
            ImGui::Spacing();
        }

        if (!paramToDelete.empty()) {
            m_graph->RemoveParameter(paramToDelete);
            m_isDirty = true;
        }
    }
    ImGui::EndChild();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void AnimatorGraphEditor::DrawArrow(ImDrawList* dl, const ImVec2& p1, const ImVec2& p2, ImU32 color, float thickness, bool isSelected, uint32_t transitionId) {
    dl->AddLine(p1, p2, color, thickness);

    // Arrowhead calculations
    glm::vec2 d(p2.x - p1.x, p2.y - p1.y);
    float len = glm::length(d);
    if (len > 1e-4f) {
        glm::vec2 dir = d / len;
        glm::vec2 perp(-dir.y, dir.x);

        float arrowSize = isSelected ? 15.0f : 12.0f;
        ImVec2 tip = p2;
        ImVec2 side1(p2.x - dir.x * arrowSize + perp.x * (arrowSize * 0.5f),
                     p2.y - dir.y * arrowSize + perp.y * (arrowSize * 0.5f));
        ImVec2 side2(p2.x - dir.x * arrowSize - perp.x * (arrowSize * 0.5f),
                     p2.y - dir.y * arrowSize - perp.y * (arrowSize * 0.5f));

        dl->AddTriangleFilled(tip, side1, side2, color);
    }

    if (isSelected) {
        // Glowing dot and ring at midpoint
        ImVec2 mid((p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f);
        dl->AddCircleFilled(mid, 6.0f, IM_COL32(0, 220, 255, 255));
        dl->AddCircle(mid, 9.0f, IM_COL32(0, 220, 255, 120), 0, 2.0f);
    }
}

void AnimatorGraphEditor::RenderCanvas(const ImVec2& pos, const ImVec2& size) {
    ImGui::SetCursorScreenPos(pos);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Clip to canvas area
    dl->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);

    // Canvas background (#2D2D2D)
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(45, 45, 45, 255));

    // Draw Grid Lines
    float gridStep = 40.0f * m_canvasZoom;
    float startX = std::fmod(m_canvasPan.x, gridStep) + pos.x;
    float startY = std::fmod(m_canvasPan.y, gridStep) + pos.y;

    ImU32 gridColor = IM_COL32(55, 55, 55, 255);
    for (float x = startX; x < pos.x + size.x; x += gridStep) {
        dl->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + size.y), gridColor);
    }
    for (float y = startY; y < pos.y + size.y; y += gridStep) {
        dl->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + size.x, y), gridColor);
    }

    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool isCanvasHovered = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + size.x, pos.y + size.y));

    // Canvas panning with middle mouse or right mouse drag
    if (isCanvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        m_canvasPan.x += ImGui::GetIO().MouseDelta.x;
        m_canvasPan.y += ImGui::GetIO().MouseDelta.y;
    }

    // Canvas zooming with mouse wheel
    if (isCanvasHovered && ImGui::GetIO().MouseWheel != 0.0f) {
        float oldZoom = m_canvasZoom;
        m_canvasZoom = glm::clamp(m_canvasZoom + ImGui::GetIO().MouseWheel * 0.05f, 0.5f, 2.0f);
        m_canvasPan.x = (m_canvasPan.x - (mousePos.x - pos.x)) * (m_canvasZoom / oldZoom) + (mousePos.x - pos.x);
        m_canvasPan.y = (m_canvasPan.y - (mousePos.y - pos.y)) * (m_canvasZoom / oldZoom) + (mousePos.y - pos.y);
    }

    // Node card dimensions (matching image.svg: 203x62)
    const float nodeWidth = 203.0f * m_canvasZoom;
    const float nodeHeight = 62.0f * m_canvasZoom;

    uint32_t clickedArrowId = 0;

    // 1. Draw Transitions / Arrows between nodes
    for (const auto& t : m_graph->transitions) {
        const auto* src = m_graph->FindState(t.sourceStateId);
        const auto* dst = m_graph->FindState(t.targetStateId);
        if (!src || !dst) continue;

        ImVec2 srcPos(pos.x + m_canvasPan.x + src->position.x * m_canvasZoom,
                      pos.y + m_canvasPan.y + src->position.y * m_canvasZoom);
        ImVec2 dstPos(pos.x + m_canvasPan.x + dst->position.x * m_canvasZoom,
                      pos.y + m_canvasPan.y + dst->position.y * m_canvasZoom);

        ImVec2 srcCenter(srcPos.x + nodeWidth * 0.5f, srcPos.y + nodeHeight * 0.5f);
        ImVec2 dstCenter(dstPos.x + nodeWidth * 0.5f, dstPos.y + nodeHeight * 0.5f);

        // Compute edge attachment points
        ImVec2 p1 = srcCenter;
        ImVec2 p2 = dstCenter;

        if (std::abs(dstCenter.x - srcCenter.x) > std::abs(dstCenter.y - srcCenter.y)) {
            // Horizontal attachment
            if (dstCenter.x > srcCenter.x) {
                p1 = ImVec2(srcPos.x + nodeWidth, srcCenter.y - 8.0f);
                p2 = ImVec2(dstPos.x, dstCenter.y - 8.0f);
            } else {
                p1 = ImVec2(srcPos.x, srcCenter.y + 8.0f);
                p2 = ImVec2(dstPos.x + nodeWidth, dstCenter.y + 8.0f);
            }
        } else {
            // Vertical attachment
            if (dstCenter.y > srcCenter.y) {
                p1 = ImVec2(srcCenter.x - 8.0f, srcPos.y + nodeHeight);
                p2 = ImVec2(dstCenter.x - 8.0f, dstPos.y);
            } else {
                p1 = ImVec2(srcCenter.x + 8.0f, srcPos.y);
                p2 = ImVec2(dstCenter.x + 8.0f, dstPos.y + nodeHeight);
            }
        }

        bool isSelected = (m_selectedArrowId == t.id);
        ImU32 arrowColor = isSelected ? IM_COL32(0, 220, 255, 255) : IM_COL32(230, 230, 230, 255);

        // Point-to-segment distance check for clicking anywhere along arrow line
        glm::vec2 vSeg(p2.x - p1.x, p2.y - p1.y);
        glm::vec2 wMouse(mousePos.x - p1.x, mousePos.y - p1.y);
        float lenSq = glm::dot(vSeg, vSeg);
        float tProj = (lenSq > 0.0001f) ? glm::clamp(glm::dot(wMouse, vSeg) / lenSq, 0.0f, 1.0f) : 0.0f;
        glm::vec2 nearestPt = glm::vec2(p1.x, p1.y) + tProj * vSeg;
        float distToLine = glm::length(glm::vec2(mousePos.x, mousePos.y) - nearestPt);

        bool isArrowHovered = (distToLine < 12.0f);
        if (isArrowHovered && !isSelected) {
            arrowColor = IM_COL32(100, 220, 255, 255);
        }

        if (isCanvasHovered && isArrowHovered && ImGui::IsMouseClicked(0) && !m_isConnecting) {
            clickedArrowId = t.id;
        }

        DrawArrow(dl, p1, p2, arrowColor, isSelected ? 3.5f : (isArrowHovered ? 2.5f : 2.0f), isSelected, t.id);
    }

    if (clickedArrowId != 0) {
        m_selectedArrowId = clickedArrowId;
        m_selectedNodeId = 0;
    }

    // 2. Dragging a new transition arrow
    if (m_isConnecting && m_connectingFromNodeId != 0) {
        const auto* src = m_graph->FindState(m_connectingFromNodeId);
        if (src) {
            ImVec2 srcCenter(pos.x + m_canvasPan.x + (src->position.x + 100.0f) * m_canvasZoom,
                             pos.y + m_canvasPan.y + (src->position.y + 31.0f) * m_canvasZoom);
            dl->AddLine(srcCenter, mousePos, IM_COL32(255, 200, 0, 255), 2.5f);
            dl->AddCircleFilled(mousePos, 4.0f, IM_COL32(255, 200, 0, 255));
        }

        if (!ImGui::IsMouseDown(0)) {
            // Released connection drag: check if dropped on a node
            for (const auto& s : m_graph->states) {
                if (s.id == m_connectingFromNodeId) continue;
                ImVec2 nPos(pos.x + m_canvasPan.x + s.position.x * m_canvasZoom,
                            pos.y + m_canvasPan.y + s.position.y * m_canvasZoom);
                if (mousePos.x >= nPos.x && mousePos.x <= nPos.x + nodeWidth &&
                    mousePos.y >= nPos.y && mousePos.y <= nPos.y + nodeHeight) {
                    // Create transition
                    StateTransition& newTrans = m_graph->AddTransition(m_connectingFromNodeId, s.id);
                    m_selectedArrowId = newTrans.id;
                    m_selectedNodeId = 0;
                    m_isDirty = true;
                    break;
                }
            }
            m_isConnecting = false;
            m_connectingFromNodeId = 0;
        }
    }

    // 3. Draw State Nodes (Cards)
    for (auto& s : m_graph->states) {
        ImVec2 nPos(pos.x + m_canvasPan.x + s.position.x * m_canvasZoom,
                    pos.y + m_canvasPan.y + s.position.y * m_canvasZoom);
        ImVec2 nEnd(nPos.x + nodeWidth, nPos.y + nodeHeight);

        bool isHovered = (mousePos.x >= nPos.x && mousePos.x <= nEnd.x &&
                          mousePos.y >= nPos.y && mousePos.y <= nEnd.y);
        bool isSelected = (m_selectedNodeId == s.id);
        bool isActivePlaying = (m_instance && m_instance->activeStateId == s.id);

        // Node card background (#151515 matching image.svg: fill="#151515")
        ImU32 cardBg = IM_COL32(21, 21, 21, 255);
        dl->AddRectFilled(nPos, nEnd, cardBg, 4.0f);

        // Border: Green for Entry/Begin, Cyan for Selected, Gold glow for Active Playing
        ImU32 borderCol = IM_COL32(60, 60, 60, 255);
        float borderThickness = 1.0f;

        if (isActivePlaying) {
            borderCol = IM_COL32(255, 200, 50, 255);
            borderThickness = 2.5f;
        } else if (isSelected) {
            borderCol = IM_COL32(0, 180, 255, 255);
            borderThickness = 2.0f;
        } else if (s.isEntry) {
            borderCol = IM_COL32(60, 200, 100, 255);
            borderThickness = 1.5f;
        }

        dl->AddRect(nPos, nEnd, borderCol, 4.0f, 0, borderThickness);

        // Edge port circle for dragging arrows
        ImVec2 portPos(nEnd.x, nPos.y + nodeHeight * 0.5f);
        bool isPortHovered = glm::length(glm::vec2(mousePos.x - portPos.x, mousePos.y - portPos.y)) < 12.0f;
        dl->AddCircleFilled(portPos, isPortHovered ? 6.0f : 4.0f, isPortHovered ? IM_COL32(0, 220, 255, 255) : IM_COL32(180, 180, 180, 255));

        // Node Text (white, matching image.svg)
        std::string title = s.name;
        if (s.isEntry && title != "Begin") title += " (Entry)";
        ImVec2 titleSize = ImGui::CalcTextSize(title.c_str());
        ImVec2 textPos(nPos.x + (nodeWidth - titleSize.x) * 0.5f, nPos.y + 12.0f * m_canvasZoom);
        dl->AddText(textPos, IM_COL32(255, 255, 255, 255), title.c_str());

        // Clip name underneath
        std::string clipSub = s.clipPath.empty() ? "(No Clip)" : std::filesystem::path(s.clipPath).stem().string();
        ImVec2 clipSize = ImGui::CalcTextSize(clipSub.c_str());
        ImVec2 clipPos(nPos.x + (nodeWidth - clipSize.x) * 0.5f, nPos.y + 34.0f * m_canvasZoom);
        dl->AddText(clipPos, IM_COL32(160, 160, 160, 255), clipSub.c_str());

        // Drag node or start transition connection
        if (isCanvasHovered && isHovered && ImGui::IsMouseClicked(0)) {
            if (isPortHovered) {
                // Dragging arrow from node port
                m_isConnecting = true;
                m_connectingFromNodeId = s.id;
            } else {
                // Select node
                m_selectedNodeId = s.id;
                m_selectedArrowId = 0;
                m_isDraggingNode = true;
                m_draggedNodeId = s.id;
                m_dragNodeStartPos = ImVec2(s.position.x, s.position.y);
            }
        }

        // Context menu on node
        if (isCanvasHovered && isHovered && ImGui::IsMouseClicked(1)) {
            m_selectedNodeId = s.id;
            m_selectedArrowId = 0;
            ImGui::OpenPopup("NodeContextMenu");
        }
    }

    // Node dragging logic
    if (m_isDraggingNode && ImGui::IsMouseDragging(0)) {
        auto* node = m_graph->FindState(m_draggedNodeId);
        if (node) {
            node->position.x += ImGui::GetIO().MouseDelta.x / m_canvasZoom;
            node->position.y += ImGui::GetIO().MouseDelta.y / m_canvasZoom;
            m_isDirty = true;
        }
    }
    if (!ImGui::IsMouseDown(0)) {
        m_isDraggingNode = false;
        m_draggedNodeId = 0;
    }

    // Canvas click to deselect (only if neither a node nor an arrow was clicked)
    if (isCanvasHovered && ImGui::IsMouseClicked(0) && !m_isConnecting) {
        bool hitAnyNode = false;
        for (const auto& s : m_graph->states) {
            ImVec2 nPos(pos.x + m_canvasPan.x + s.position.x * m_canvasZoom,
                        pos.y + m_canvasPan.y + s.position.y * m_canvasZoom);
            if (mousePos.x >= nPos.x && mousePos.x <= nPos.x + nodeWidth &&
                mousePos.y >= nPos.y && mousePos.y <= nPos.y + nodeHeight) {
                hitAnyNode = true;
                break;
            }
        }
        if (!hitAnyNode && clickedArrowId == 0) {
            m_selectedNodeId = 0;
            m_selectedArrowId = 0;
        }
    }

    // Accept Drag & Drop Payload to create animation state node from sidebar
    if (ImGui::BeginDragDropTargetCustom(ImRect(pos, ImVec2(pos.x + size.x, pos.y + size.y)), ImGui::GetID("CanvasDropTarget"))) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ANIM_CLIP_PATH")) {
            const char* clipPathStr = (const char*)payload->Data;
            std::string clipPath(clipPathStr);
            std::string stateName = std::filesystem::path(clipPath).stem().string();

            glm::vec2 dropLocalPos(
                (mousePos.x - pos.x - m_canvasPan.x) / m_canvasZoom,
                (mousePos.y - pos.y - m_canvasPan.y) / m_canvasZoom
            );

            AnimationStateNode& newNode = m_graph->AddState(stateName, dropLocalPos);
            newNode.clipPath = clipPath;
            m_selectedNodeId = newNode.id;
            m_selectedArrowId = 0;
            m_isDirty = true;
            SyncPreviewResources();
        }
        ImGui::EndDragDropTarget();
    }

    // Right-click Canvas Menu
    if (isCanvasHovered && ImGui::IsMouseClicked(1) && m_selectedNodeId == 0) {
        m_canvasContextPos = mousePos;
        ImGui::OpenPopup("CanvasContextMenu");
    }

    if (ImGui::BeginPopup("NodeContextMenu")) {
        auto* node = m_graph->FindState(m_selectedNodeId);
        if (node) {
            ImGui::Text("State: %s", node->name.c_str());
            ImGui::Separator();
            if (!node->isEntry && ImGui::MenuItem("Set as Begin (Entry)")) {
                for (auto& s : m_graph->states) s.isEntry = false;
                node->isEntry = true;
                m_graph->entryStateId = node->id;
                m_isDirty = true;
            }
            if (ImGui::MenuItem("Delete State")) {
                m_graph->RemoveState(node->id);
                m_selectedNodeId = 0;
                m_isDirty = true;
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("CanvasContextMenu")) {
        if (ImGui::MenuItem("Add State")) {
            glm::vec2 localPos(
                (m_canvasContextPos.x - pos.x - m_canvasPan.x) / m_canvasZoom,
                (m_canvasContextPos.y - pos.y - m_canvasPan.y) / m_canvasZoom
            );
            AnimationStateNode& newNode = m_graph->AddState("NewState", localPos);
            m_selectedNodeId = newNode.id;
            m_selectedArrowId = 0;
            m_isDirty = true;
        }
        if (ImGui::MenuItem("Reset View")) {
            m_canvasPan = ImVec2(0.0f, 0.0f);
            m_canvasZoom = 1.0f;
        }
        ImGui::EndPopup();
    }

    dl->PopClipRect();
}

void AnimatorGraphEditor::RenderRightSidebar(const ImVec2& pos, const ImVec2& size) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f)); // #1A1919 matching image.svg: fill="#1A1919"
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));

    if (ImGui::BeginChild("AGRightSidebar", size, true, ImGuiWindowFlags_None)) {
        float previewHeight = 369.0f; // Exactly matches image.svg height="369"
        float propHeight = std::max(200.0f, size.y - previewHeight - 16.0f);

        // Top: Selected Skeleton / Live Preview
        RenderLivePreview(pos, ImVec2(size.x, previewHeight));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Bottom: Properties
        RenderProperties(ImVec2(pos.x, pos.y + previewHeight), ImVec2(size.x, propHeight));
    }
    ImGui::EndChild();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void AnimatorGraphEditor::RenderLivePreview(const ImVec2& pos, const ImVec2& size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "LIVE PREVIEW - SKELETON");
    if (m_saveNotificationTimer > 0.0f) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[SAVED]");
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 145.0f);
    if (ImGui::SmallButton("Restart")) {
        if (m_instance) m_instance->Reset();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restart state machine simulation from Entry state");

    ImGui::SameLine();
    if (ImGui::SmallButton("Reset Cam")) {
        ResetPreviewCamera();
    }

    ImVec2 previewPos = ImGui::GetCursorScreenPos();
    ImVec2 previewSize(ImGui::GetContentRegionAvail().x, size.y - 45.0f);

    dl->PushClipRect(previewPos, ImVec2(previewPos.x + previewSize.x, previewPos.y + previewSize.y), true);
    dl->AddRectFilled(previewPos, ImVec2(previewPos.x + previewSize.x, previewPos.y + previewSize.y), IM_COL32(18, 18, 18, 255));

    // Handle mouse orbit / pan in preview
    bool isPreviewHovered = ImGui::IsMouseHoveringRect(previewPos, ImVec2(previewPos.x + previewSize.x, previewPos.y + previewSize.y));
    if (isPreviewHovered) {
        if (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1)) {
            m_isPreviewDragging = true;
            m_lastMousePos = ImGui::GetIO().MousePos;
        }

        if (ImGui::GetIO().MouseWheel != 0.0f) {
            float zoomSpeed = std::max(0.1f, m_previewDistance * 0.1f);
            m_previewDistance = glm::clamp(m_previewDistance - ImGui::GetIO().MouseWheel * zoomSpeed, 0.1f, 5000.0f);
        }
    }
    if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(1)) {
        m_isPreviewDragging = false;
    }

    if (m_isPreviewDragging) {
        ImVec2 delta(ImGui::GetIO().MousePos.x - m_lastMousePos.x, ImGui::GetIO().MousePos.y - m_lastMousePos.y);
        m_lastMousePos = ImGui::GetIO().MousePos;

        m_previewYaw += delta.x * 0.5f;
        m_previewPitch = glm::clamp(m_previewPitch + delta.y * 0.5f, -85.0f, 85.0f);
    }

    // Compute Camera View & Projection Matrices
    float radYaw = glm::radians(m_previewYaw);
    float radPitch = glm::radians(m_previewPitch);

    glm::vec3 camPos;
    camPos.x = m_previewTarget.x + m_previewDistance * std::cos(radPitch) * std::sin(radYaw);
    camPos.y = m_previewTarget.y + m_previewDistance * std::sin(radPitch);
    camPos.z = m_previewTarget.z + m_previewDistance * std::cos(radPitch) * std::cos(radYaw);

    glm::mat4 view = glm::lookAt(camPos, m_previewTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    float aspect = previewSize.x / std::max(1.0f, previewSize.y);
    float nearPlane = std::max(0.01f, m_previewDistance * 0.005f);
    float farPlane = std::max(100.0f, m_previewDistance * 25.0f);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, nearPlane, farPlane);
    glm::mat4 viewProj = proj * view;

    auto projectToScreen = [&](const glm::vec3& worldPos, ImVec2& outScreen, float& outDepth) -> bool {
        glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.001f) return false;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < -1.0f || ndc.z > 1.0f) return false;

        outScreen.x = previewPos.x + (ndc.x * 0.5f + 0.5f) * previewSize.x;
        outScreen.y = previewPos.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * previewSize.y;
        outDepth = ndc.z;
        return true;
    };

    // Draw 3D Ground Grid (adaptive to mesh bounds)
    float gridExtent = (m_previewMesh && m_previewMesh->valid) ? std::max(2.5f, m_previewMesh->boundingRadius * 1.5f) : 5.0f;
    float gridStep = gridExtent / 10.0f;
    float groundY = (m_previewMesh && m_previewMesh->valid) ? m_previewMesh->aabbMin.y : 0.0f;
    ImU32 groundGridCol = IM_COL32(40, 40, 40, 255);
    for (int g = -10; g <= 10; ++g) {
        float offset = g * gridStep;
        ImVec2 pA, pB;
        float dA, dB;
        if (projectToScreen(glm::vec3(offset, groundY, -gridExtent), pA, dA) &&
            projectToScreen(glm::vec3(offset, groundY, gridExtent), pB, dB)) {
            dl->AddLine(pA, pB, (g == 0) ? IM_COL32(65, 65, 65, 255) : groundGridCol);
        }
        if (projectToScreen(glm::vec3(-gridExtent, groundY, offset), pA, dA) &&
            projectToScreen(glm::vec3(gridExtent, groundY, offset), pB, dB)) {
            dl->AddLine(pA, pB, (g == 0) ? IM_COL32(65, 65, 65, 255) : groundGridCol);
        }
    }

    // Render Skeletal Mesh in Real Time
    if (m_previewMesh && m_previewMesh->valid) {
        // 1. Draw Skinned Character Mesh Triangles / Wireframe
        ImU32 meshWireCol = IM_COL32(100, 180, 255, 160);
        for (const auto& sub : m_previewMesh->submeshes) {
            const auto& verts = sub.runtimeVertices;
            const auto& indices = sub.indices;

            size_t triCount = indices.size() / 3;
            // Draw subset of edges for high performance wireframe preview
            size_t step = (triCount > 2500) ? (triCount / 2500) : 1;

            for (size_t ti = 0; ti < triCount; ti += step) {
                uint32_t i0 = indices[ti * 3 + 0];
                uint32_t i1 = indices[ti * 3 + 1];
                uint32_t i2 = indices[ti * 3 + 2];
                if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size()) continue;

                ImVec2 s0, s1, s2;
                float d0, d1, d2;
                if (projectToScreen(verts[i0].pos, s0, d0) &&
                    projectToScreen(verts[i1].pos, s1, d1) &&
                    projectToScreen(verts[i2].pos, s2, d2)) {
                    dl->AddTriangle(s0, s1, s2, meshWireCol, 1.0f);
                }
            }
        }

        // 2. Draw Bone Hierarchy Rig Lines & Joints in Model Space
        const auto& bones = m_previewMesh->skeleton.bones;
        std::vector<glm::mat4> modelBoneTransforms(bones.size(), glm::mat4(1.0f));
        for (size_t b = 0; b < bones.size(); ++b) {
            glm::mat4 localMat = bones[b].localBindTransform;
            int pIdx = bones[b].parentIndex;
            if (pIdx >= 0 && pIdx < (int)bones.size()) {
                modelBoneTransforms[b] = modelBoneTransforms[pIdx] * localMat;
            } else {
                modelBoneTransforms[b] = localMat;
            }
        }

        for (size_t b = 0; b < bones.size(); ++b) {
            glm::vec3 boneWorldPos(modelBoneTransforms[b][3]);

            ImVec2 boneScreen;
            float boneDepth;
            if (projectToScreen(boneWorldPos, boneScreen, boneDepth)) {
                dl->AddCircleFilled(boneScreen, 4.0f, IM_COL32(255, 220, 50, 255));
            }

            int pIdx = bones[b].parentIndex;
            if (pIdx >= 0 && pIdx < (int)bones.size()) {
                glm::vec3 parentPos(modelBoneTransforms[pIdx][3]);
                ImVec2 pScreen;
                float pDepth;
                if (projectToScreen(parentPos, pScreen, pDepth) &&
                    projectToScreen(boneWorldPos, boneScreen, boneDepth)) {
                    dl->AddLine(pScreen, boneScreen, IM_COL32(255, 140, 0, 255), 2.5f);
                }
            }
        }
    } else {
        // No mesh selected prompt
        ImVec2 textPos(previewPos.x + 20.0f, previewPos.y + previewSize.y * 0.5f - 10.0f);
        dl->AddText(textPos, IM_COL32(160, 160, 160, 255), "Select a Skeleton Mesh in Left Sidebar to view 3D preview");
    }

    // Status overlay in preview
    std::string activeStateName = "None";
    if (m_instance) {
        const auto* s = m_graph->FindState(m_instance->activeStateId);
        if (s) activeStateName = s->name;
    }
    std::string statusText = "Active: " + activeStateName;
    if (m_instance && m_instance->isTransitioning) {
        char buf[64];
        snprintf(buf, sizeof(buf), " -> Blending (%.0f%%)", m_instance->transitionProgress * 100.0f);
        statusText += buf;
    }
    dl->AddText(ImVec2(previewPos.x + 8.0f, previewPos.y + 8.0f), IM_COL32(0, 255, 180, 255), statusText.c_str());

    dl->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(previewPos.x, previewPos.y + previewSize.y));
}

void AnimatorGraphEditor::RenderProperties(const ImVec2& pos, const ImVec2& size) {
    ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "PROPERTIES");
    ImGui::Separator();

    if (m_selectedNodeId != 0) {
        auto* node = m_graph->FindState(m_selectedNodeId);
        if (!node) {
            m_selectedNodeId = 0;
            return;
        }

        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Animation State Node");
        ImGui::Spacing();

        // State Name
        char nameBuf[128];
        strncpy_s(nameBuf, sizeof(nameBuf), node->name.c_str(), sizeof(nameBuf) - 1);
        if (ImGui::InputText("Name##NodeNameProp", nameBuf, sizeof(nameBuf))) {
            node->name = nameBuf;
            m_isDirty = true;
        }

        // Animation Clip Path
        ImGui::Spacing();
        ImGui::Text("Animation Clip:");
        std::string clipLabel = node->clipPath.empty() 
            ? "[Choose Animation Clip...]" 
            : std::filesystem::path(node->clipPath).filename().string();

        ImGui::PushItemWidth(-1);
        if (ImGui::BeginCombo("##NodeClipCombo", clipLabel.c_str())) {
            std::vector<std::string> anims = SkeletalAnimationManager::Get().FindProjectAnimations("Content", m_graph->skeletonMeshPath);
            for (const auto& aPath : anims) {
                std::string stem = std::filesystem::path(aPath).stem().string();
                bool isSel = (node->clipPath == aPath);
                if (ImGui::Selectable(stem.c_str(), isSel)) {
                    node->clipPath = aPath;
                    m_isDirty = true;
                    SyncPreviewResources();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        // Playback Speed
        ImGui::Spacing();
        if (ImGui::SliderFloat("Speed Multiplier", &node->speed, 0.1f, 5.0f, "%.2fx")) {
            m_isDirty = true;
        }

        // Looping
        if (ImGui::Checkbox("Looping", &node->loop)) {
            m_isDirty = true;
        }

        // Is Entry / Begin state
        if (ImGui::Checkbox("Entry State (Begin)", &node->isEntry)) {
            if (node->isEntry) {
                for (auto& s : m_graph->states) {
                    if (s.id != node->id) s.isEntry = false;
                }
                m_graph->entryStateId = node->id;
            }
            m_isDirty = true;
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Delete State##DeleteNodeBtn", ImVec2(-1, 0))) {
            m_graph->RemoveState(node->id);
            m_selectedNodeId = 0;
            m_isDirty = true;
        }

    } else if (m_selectedArrowId != 0) {
        auto* trans = m_graph->FindTransition(m_selectedArrowId);
        if (!trans) {
            m_selectedArrowId = 0;
            return;
        }

        auto* srcNode = m_graph->FindState(trans->sourceStateId);
        auto* dstNode = m_graph->FindState(trans->targetStateId);

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Transition Arrow");
        ImGui::Text("From: %s  ->  To: %s", 
            srcNode ? srcNode->name.c_str() : "Unknown", 
            dstNode ? dstNode->name.c_str() : "Unknown");
        ImGui::Spacing();

        // Smoothing Time (Crossfade blend duration)
        if (ImGui::DragFloat("Smoothing Time (s)", &trans->smoothingTime, 0.02f, 0.0f, 5.0f, "%.2f s")) {
            m_isDirty = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Duration in seconds to smoothly blend between the two animation states");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Conditions List
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Conditions:");
        ImGui::SameLine(ImGui::GetWindowWidth() - 40.0f);
        if (ImGui::Button("+##AddCondBtn", ImVec2(24, 20))) {
            if (!m_graph->parameters.empty()) {
                TransitionCondition cond;
                cond.paramName = m_graph->parameters.front().name;
                if (m_graph->parameters.front().type == AnimatorParamType::Boolean) {
                    cond.op = ConditionOp::IsTrue;
                    cond.targetBool = true;
                } else {
                    cond.op = ConditionOp::Greater;
                }
                trans->conditions.push_back(cond);
                m_isDirty = true;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add Condition");

        if (trans->conditions.empty()) {
            ImGui::TextDisabled("No conditions (Entry auto-transitions; others hold)");
        }

        size_t condToDelete = (size_t)-1;
        for (size_t c = 0; c < trans->conditions.size(); ++c) {
            auto& cond = trans->conditions[c];
            ImGui::PushID((int)c);

            // Delete condition button
            if (ImGui::SmallButton("x")) {
                condToDelete = c;
            }
            ImGui::SameLine();

            // Param selector
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::BeginCombo("##CondParamCombo", cond.paramName.c_str())) {
                for (const auto& p : m_graph->parameters) {
                    if (ImGui::Selectable(p.name.c_str(), p.name == cond.paramName)) {
                        cond.paramName = p.name;
                        m_isDirty = true;
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            auto* p = m_graph->FindParameter(cond.paramName);
            if (p) {
                // Operator selector based on type
                if (p->type == AnimatorParamType::Boolean) {
                    ImGui::SetNextItemWidth(70.0f);
                    const char* boolOps[] = { "True", "False" };
                    int opIdx = (cond.op == ConditionOp::IsFalse) ? 1 : 0;
                    if (ImGui::Combo("##BoolOpCombo", &opIdx, boolOps, 2)) {
                        cond.op = (opIdx == 1) ? ConditionOp::IsFalse : ConditionOp::IsTrue;
                        cond.targetBool = (opIdx == 0);
                        m_isDirty = true;
                    }
                } else if (p->type == AnimatorParamType::Float || p->type == AnimatorParamType::Int) {
                    ImGui::SetNextItemWidth(65.0f);
                    const char* mathOps[] = { ">", "<", ">=", "<=", "==", "!=" };
                    int opIdx = 0;
                    if (cond.op == ConditionOp::Less) opIdx = 1;
                    else if (cond.op == ConditionOp::GreaterEqual) opIdx = 2;
                    else if (cond.op == ConditionOp::LessEqual) opIdx = 3;
                    else if (cond.op == ConditionOp::Equal) opIdx = 4;
                    else if (cond.op == ConditionOp::NotEqual) opIdx = 5;

                    if (ImGui::Combo("##MathOpCombo", &opIdx, mathOps, 6)) {
                        if (opIdx == 0) cond.op = ConditionOp::Greater;
                        else if (opIdx == 1) cond.op = ConditionOp::Less;
                        else if (opIdx == 2) cond.op = ConditionOp::GreaterEqual;
                        else if (opIdx == 3) cond.op = ConditionOp::LessEqual;
                        else if (opIdx == 4) cond.op = ConditionOp::Equal;
                        else if (opIdx == 5) cond.op = ConditionOp::NotEqual;
                        m_isDirty = true;
                    }

                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(70.0f);
                    if (p->type == AnimatorParamType::Float) {
                        if (ImGui::DragFloat("##CondValF", &cond.targetFloat, 0.1f, -1000.0f, 1000.0f, "%.2f")) {
                            m_isDirty = true;
                        }
                    } else {
                        if (ImGui::DragInt("##CondValI", &cond.targetInt, 1, -1000, 1000)) {
                            m_isDirty = true;
                        }
                    }
                } else if (p->type == AnimatorParamType::String) {
                    ImGui::SetNextItemWidth(55.0f);
                    const char* strOps[] = { "==", "!=" };
                    int opIdx = (cond.op == ConditionOp::NotEqual) ? 1 : 0;
                    if (ImGui::Combo("##StrOpCombo", &opIdx, strOps, 2)) {
                        cond.op = (opIdx == 1) ? ConditionOp::NotEqual : ConditionOp::Equal;
                        m_isDirty = true;
                    }

                    ImGui::SameLine();
                    char sBuf[64];
                    strncpy_s(sBuf, sizeof(sBuf), cond.targetString.c_str(), sizeof(sBuf) - 1);
                    ImGui::SetNextItemWidth(70.0f);
                    if (ImGui::InputText("##CondValS", sBuf, sizeof(sBuf))) {
                        cond.targetString = sBuf;
                        m_isDirty = true;
                    }
                }

                // Live evaluation indicator
                if (m_instance) {
                    bool isMet = cond.Evaluate(m_instance->runtimeParams);
                    ImGui::SameLine();
                    ImGui::TextColored(isMet ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0.3f, 0.3f, 1), isMet ? "[TRUE]" : "[FALSE]");
                }
            }

            ImGui::PopID();
        }

        if (condToDelete < trans->conditions.size()) {
            trans->conditions.erase(trans->conditions.begin() + condToDelete);
            m_isDirty = true;
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Delete Transition##DeleteArrowBtn", ImVec2(-1, 0))) {
            m_graph->RemoveTransition(trans->id);
            m_selectedArrowId = 0;
            m_isDirty = true;
        }

    } else {
        ImGui::TextDisabled("Select an Animation Node or Transition Arrow to inspect its properties.");
    }
}

// ============================================================================
// AnimatorGraphManager Implementation
// ============================================================================

AnimatorGraphManager& AnimatorGraphManager::Get() {
    static AnimatorGraphManager s_instance;
    return s_instance;
}

std::shared_ptr<AnimatorGraphEditor> AnimatorGraphManager::Open(const std::string& filePath) {
    std::string normPath = filePath;
    std::replace(normPath.begin(), normPath.end(), '\\', '/');

    for (size_t i = 0; i < m_editors.size(); ++i) {
        if (m_editors[i]->GetFilePath() == normPath) {
            activeTabIndex = (int)i;
            return m_editors[i];
        }
    }

    auto graph = std::make_shared<AnimatorGraph>();
    if (!graph->LoadFromFile(normPath)) {
        graph->name = std::filesystem::path(normPath).stem().string();
        graph->assetPath = normPath;
        graph->SaveToFile(normPath);
    }

    auto editor = std::make_shared<AnimatorGraphEditor>(graph);
    m_editors.push_back(editor);
    activeTabIndex = (int)m_editors.size() - 1;
    return editor;
}

void AnimatorGraphManager::Close(size_t index) {
    if (index >= m_editors.size()) return;
    if (m_editors[index]->IsDirty()) {
        m_editors[index]->Save();
    }
    m_editors.erase(m_editors.begin() + index);
    if (activeTabIndex >= (int)m_editors.size()) {
        activeTabIndex = (int)m_editors.size() - 1;
    }
}

void AnimatorGraphManager::SwitchToEditor(int index) {
    if (index >= 0 && index < (int)m_editors.size()) {
        activeTabIndex = index;
    }
}

void AnimatorGraphManager::SwitchToMainEditor() {
    activeTabIndex = -1;
}

std::shared_ptr<AnimatorGraphEditor> AnimatorGraphManager::GetActiveEditor() {
    if (activeTabIndex >= 0 && activeTabIndex < (int)m_editors.size()) {
        return m_editors[activeTabIndex];
    }
    return nullptr;
}

void AnimatorGraphManager::RenderActiveTabWorkspace(const ImVec2& pos, const ImVec2& size) {
    auto editor = GetActiveEditor();
    if (editor) {
        editor->Render(pos, size);
    }
}

std::string AnimatorGraphManager::CreateNewAnimatorGraph(const std::string& folderPath, const std::string& name) {
    std::filesystem::path dir(folderPath);
    std::string cleanName = name;
    if (cleanName.find(".eanimator") == std::string::npos) {
        cleanName += ".eanimator";
    }

    std::filesystem::path fullPath = dir / cleanName;
    auto graph = std::make_shared<AnimatorGraph>();
    graph->name = std::filesystem::path(cleanName).stem().string();
    graph->assetPath = fullPath.string();
    graph->SaveToFile(fullPath.string());

    return fullPath.string();
}

} // namespace Eunoia
