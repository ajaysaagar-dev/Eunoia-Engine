#include <EngineAssets/TextureManager.h>
#include <Animation-Blending/AnimationBlendEditor.h>
#include <EngineAssets/AssetRegistry.h>
#include <filesystem>
#include <algorithm>
#include <cmath>

namespace Eunoia {

AnimationBlendEditor::AnimationBlendEditor(std::shared_ptr<AnimationBlend> blend)
    : m_blend(blend) {
    if (m_blend) {
        m_cursorX = (m_blend->xAxis.minVal + m_blend->xAxis.maxVal) * 0.5f;
        m_cursorY = (m_blend->yAxis.minVal + m_blend->yAxis.maxVal) * 0.5f;
        SyncPreviewResources();
    }
}

void AnimationBlendEditor::Save() {
    if (!m_blend) return;
    if (m_blend->assetPath.empty()) {
        m_blend->assetPath = "Content/" + m_blend->name + ".eblend";
    }
    m_blend->SaveToFile(m_blend->assetPath);
    m_isDirty = false;
    m_saveNotificationTimer = 2.0f;
}

void AnimationBlendEditor::ResetPreviewCamera() {
    m_previewYaw = 45.0f;
    m_previewPitch = 20.0f;
    if (m_previewMesh && m_previewMesh->valid) {
        m_previewDistance = std::max(2.5f, m_previewMesh->boundingRadius * 2.5f);
        m_previewTarget = (m_previewMesh->aabbMin + m_previewMesh->aabbMax) * 0.5f;
    } else {
        m_previewDistance = 3.5f;
        m_previewTarget = glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

void AnimationBlendEditor::SyncPreviewResources() {
    if (!m_blend || m_blend->skeletonMeshPath.empty()) {
        m_previewMesh.reset();
        m_loadedClips.clear();
        return;
    }

    m_previewMesh = SkeletalAnimationManager::Get().LoadSkeletalMesh(m_blend->skeletonMeshPath);

    // Preload animation clips referenced by sample points
    const Skeleton* skel = (m_previewMesh && m_previewMesh->valid) ? &m_previewMesh->skeleton : nullptr;
    for (const auto& pt : m_blend->samplePoints) {
        if (!pt.animationClipPath.empty() && m_loadedClips.find(pt.animationClipPath) == m_loadedClips.end()) {
            auto clip = SkeletalAnimationManager::Get().LoadAnimationClip(pt.animationClipPath, skel);
            if (clip) {
                m_loadedClips[pt.animationClipPath] = clip;
            }
        }
    }

    ResetPreviewCamera();
}

void AnimationBlendEditor::Update(float deltaTime) {
    if (m_saveNotificationTimer > 0.0f) {
        m_saveNotificationTimer -= deltaTime;
    }

    if (m_isPlaying) {
        m_previewNormalizedTime += deltaTime * m_playbackSpeed * 0.8f;
        if (m_previewNormalizedTime >= 1.0f) {
            m_previewNormalizedTime = std::fmod(m_previewNormalizedTime, 1.0f);
        }
    }

    if (!m_blend || !m_previewMesh || !m_previewMesh->valid || m_previewMesh->skeleton.bones.empty()) {
        return;
    }

    // Evaluate 2D blend space pose at (m_cursorX, m_cursorY)
    std::vector<BoneTransform> blendedTransforms;
    m_blend->EvaluatePose(m_cursorX, m_cursorY, m_previewNormalizedTime, m_previewMesh->skeleton, m_loadedClips, blendedTransforms);

    // Compute skin matrices and deform mesh in real time
    std::vector<glm::mat4> boneMatrices;
    m_previewMesh->ComputeBoneMatrices(blendedTransforms, boneMatrices);
    m_previewMesh->DeformMesh(boneMatrices);
}

void AnimationBlendEditor::Render(const ImVec2& position, const ImVec2& size) {
    float dt = ImGui::GetIO().DeltaTime;
    Update(dt);

    // Ctrl + S shortcut to save
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        Save();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));

    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);

    std::string windowId = "AnimationBlendWorkspace##" + (m_blend ? m_blend->name : "Editor");
    if (ImGui::Begin(windowId.c_str(), nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse)) {

        float splitterWidth = 4.0f;
        float minSidebarWidth = 200.0f;
        float minCanvasWidth = 250.0f;

        float maxLeftWidth = std::max(minSidebarWidth, size.x - minCanvasWidth - m_rightSidebarWidth - splitterWidth * 2.0f);
        m_leftSidebarWidth = glm::clamp(m_leftSidebarWidth, minSidebarWidth, maxLeftWidth);

        float maxRightWidth = std::max(minSidebarWidth, size.x - minCanvasWidth - m_leftSidebarWidth - splitterWidth * 2.0f);
        m_rightSidebarWidth = glm::clamp(m_rightSidebarWidth, minSidebarWidth, maxRightWidth);

        float canvasWidth = std::max(minCanvasWidth, size.x - m_leftSidebarWidth - m_rightSidebarWidth - splitterWidth * 2.0f);

        // 1. Left Sidebar
        RenderLeftSidebar(position, ImVec2(m_leftSidebarWidth, size.y));

        // Splitter 1: Left Sidebar <-> Canvas
        ImVec2 split1Pos(position.x + m_leftSidebarWidth, position.y);
        ImGui::SetCursorScreenPos(split1Pos);
        ImGui::InvisibleButton("##SplitterLeftBlend", ImVec2(splitterWidth, size.y));
        bool isSplit1Hovered = ImGui::IsItemHovered();
        bool isSplit1Active = ImGui::IsItemActive();
        if (isSplit1Hovered || isSplit1Active) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (isSplit1Active) {
            m_leftSidebarWidth += ImGui::GetIO().MouseDelta.x;
            m_leftSidebarWidth = glm::clamp(m_leftSidebarWidth, minSidebarWidth, std::max(minSidebarWidth, size.x - m_rightSidebarWidth - minCanvasWidth - splitterWidth * 2.0f));
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 split1Col = isSplit1Active ? IM_COL32(0, 180, 255, 255) : (isSplit1Hovered ? IM_COL32(90, 90, 90, 255) : IM_COL32(40, 40, 40, 255));
        dl->AddRectFilled(split1Pos, ImVec2(split1Pos.x + splitterWidth, split1Pos.y + size.y), split1Col);

        // 2. Central 2D Grid Canvas
        ImVec2 canvasPos(position.x + m_leftSidebarWidth + splitterWidth, position.y);
        RenderGridCanvas(canvasPos, ImVec2(canvasWidth, size.y));

        // Splitter 2: Canvas <-> Right Sidebar
        ImVec2 split2Pos(canvasPos.x + canvasWidth, position.y);
        ImGui::SetCursorScreenPos(split2Pos);
        ImGui::InvisibleButton("##SplitterRightBlend", ImVec2(splitterWidth, size.y));
        bool isSplit2Hovered = ImGui::IsItemHovered();
        bool isSplit2Active = ImGui::IsItemActive();
        if (isSplit2Hovered || isSplit2Active) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (isSplit2Active) {
            m_rightSidebarWidth -= ImGui::GetIO().MouseDelta.x;
            m_rightSidebarWidth = glm::clamp(m_rightSidebarWidth, minSidebarWidth, std::max(minSidebarWidth, size.x - m_leftSidebarWidth - minCanvasWidth - splitterWidth * 2.0f));
        }
        ImU32 split2Col = isSplit2Active ? IM_COL32(0, 180, 255, 255) : (isSplit2Hovered ? IM_COL32(90, 90, 90, 255) : IM_COL32(40, 40, 40, 255));
        dl->AddRectFilled(split2Pos, ImVec2(split2Pos.x + splitterWidth, split2Pos.y + size.y), split2Col);

        // 3. Right Sidebar (3D Preview & Properties)
        ImVec2 rightPos(split2Pos.x + splitterWidth, position.y);
        RenderRightSidebar(rightPos, ImVec2(m_rightSidebarWidth, size.y));
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void AnimationBlendEditor::RenderLeftSidebar(const ImVec2& pos, const ImVec2& size) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));

    if (ImGui::BeginChild("BlendLeftSidebar", size, true, ImGuiWindowFlags_None)) {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "ANIMATION BLEND SPACE 2D");
        ImGui::Separator();
        ImGui::Spacing();

        // 1. Skeleton Mesh Selector
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Target Skeleton Mesh:");
        std::string currentMeshLabel = (m_blend && !m_blend->skeletonMeshPath.empty())
            ? std::filesystem::path(m_blend->skeletonMeshPath).filename().string()
            : "[Select Skeleton Mesh...]";

        std::string contentDir = "";
        if (m_blend && !m_blend->assetPath.empty()) {
            std::filesystem::path ap(m_blend->assetPath);
            auto p = ap.parent_path();
            while (!p.empty() && p != p.root_path()) {
                if (p.filename() == "Content") {
                    contentDir = p.string();
                    break;
                }
                p = p.parent_path();
            }
        }
        if (contentDir.empty()) {
            contentDir = AssetRegistry::Get().GetContentRoot().string();
        }

        ImGui::PushItemWidth(-1);
        if (ImGui::BeginCombo("##SkeletonMeshComboBlend", currentMeshLabel.c_str())) {
            std::vector<std::string> meshes = SkeletalAnimationManager::Get().FindProjectSkeletalMeshes(contentDir);
            if (meshes.empty()) {
                ImGui::Selectable("No Skeletal Meshes found in project", false, ImGuiSelectableFlags_Disabled);
            }
            for (const auto& meshPath : meshes) {
                std::string fname = std::filesystem::path(meshPath).filename().string();
                bool isSelected = (m_blend && m_blend->skeletonMeshPath == meshPath);
                if (ImGui::Selectable(fname.c_str(), isSelected)) {
                    if (m_blend) {
                        m_blend->skeletonMeshPath = meshPath;
                        m_isDirty = true;
                        SyncPreviewResources();
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", meshPath.c_str());
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        if (m_blend && m_blend->skeletonMeshPath.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Please select a skeleton mesh.");
        } else if (m_previewMesh && m_previewMesh->valid) {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "Bones: %zu | Submeshes: %zu",
                m_previewMesh->skeleton.bones.size(), m_previewMesh->submeshes.size());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 2. Compatible Animations List
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Project Animations:");
        ImGui::TextDisabled("Drag & drop onto 2D Grid to place sample");

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
        if (ImGui::BeginChild("AnimListChildBlend", ImVec2(0, 150.0f), true)) {
            std::string skelPath = m_blend ? m_blend->skeletonMeshPath : "";
            std::vector<std::string> anims = SkeletalAnimationManager::Get().FindProjectAnimations(contentDir, skelPath);
            if (anims.empty()) {
                ImGui::TextDisabled("No compatible animations found");
            }
            for (const auto& animPath : anims) {
                std::string stem = std::filesystem::path(animPath).stem().string();
                std::string label = "  " + stem;
                ImGui::Selectable(label.c_str(), false);

                // Drag and drop source for animations
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    ImGui::SetDragDropPayload("BLEND_ANIM_CLIP", animPath.c_str(), animPath.size() + 1);
                    ImGui::Text("Animation Sample: %s", stem.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nDrag onto 2D grid to place sample", animPath.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 3. Axis Configuration (X & Y Axis parameters)
        if (m_blend) {
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "HORIZONTAL (X) AXIS");
            char xNameBuf[64];
            snprintf(xNameBuf, sizeof(xNameBuf), "%s", m_blend->xAxis.name.c_str());
            if (ImGui::InputText("Name##XAxis", xNameBuf, sizeof(xNameBuf))) {
                m_blend->xAxis.name = xNameBuf;
                m_isDirty = true;
            }
            if (ImGui::DragFloat("Min##XAxis", &m_blend->xAxis.minVal, 1.0f)) m_isDirty = true;
            if (ImGui::DragFloat("Max##XAxis", &m_blend->xAxis.maxVal, 1.0f)) m_isDirty = true;
            if (ImGui::DragInt("Divisions##XAxis", &m_blend->xAxis.divisions, 1, 1, 20)) m_isDirty = true;

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "VERTICAL (Y) AXIS");
            char yNameBuf[64];
            snprintf(yNameBuf, sizeof(yNameBuf), "%s", m_blend->yAxis.name.c_str());
            if (ImGui::InputText("Name##YAxis", yNameBuf, sizeof(yNameBuf))) {
                m_blend->yAxis.name = yNameBuf;
                m_isDirty = true;
            }
            if (ImGui::DragFloat("Min##YAxis", &m_blend->yAxis.minVal, 1.0f)) m_isDirty = true;
            if (ImGui::DragFloat("Max##YAxis", &m_blend->yAxis.maxVal, 1.0f)) m_isDirty = true;
            if (ImGui::DragInt("Divisions##YAxis", &m_blend->yAxis.divisions, 1, 1, 20)) m_isDirty = true;
        }
    }
    ImGui::EndChild();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void AnimationBlendEditor::RenderGridCanvas(const ImVec2& pos, const ImVec2& size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.11f, 0.13f, 1.0f));

    if (ImGui::BeginChild("BlendCanvasChild", size, false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {

        // Canvas title header bar
        ImGui::SetCursorPos(ImVec2(10.0f, 10.0f));
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "2D BLEND SPACE GRID");
        if (m_saveNotificationTimer > 0.0f) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[SAVED]");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("| Drag animation from left sidebar onto grid | Click/drag crosshair to test blend in 3D");

        // Compute 2D grid inner rectangle with margins for tick numbers and axis labels
        float marginL = 55.0f;
        float marginR = 30.0f;
        float marginT = 45.0f;
        float marginB = 55.0f;

        ImVec2 gridTL(pos.x + marginL, pos.y + marginT);
        ImVec2 gridBR(pos.x + size.x - marginR, pos.y + size.y - marginB);
        ImVec2 gridSize(gridBR.x - gridTL.x, gridBR.y - gridTL.y);

        if (gridSize.x > 50.0f && gridSize.y > 50.0f && m_blend) {
            // Background fill
            dl->AddRectFilled(gridTL, gridBR, IM_COL32(22, 22, 26, 255));
            dl->AddRect(gridTL, gridBR, IM_COL32(60, 60, 70, 255), 0, 0, 1.5f);

            float minX = m_blend->xAxis.minVal;
            float maxX = m_blend->xAxis.maxVal;
            float rangeX = std::max(std::abs(maxX - minX), 0.0001f);
            int divX = std::max(1, m_blend->xAxis.divisions);

            float minY = m_blend->yAxis.minVal;
            float maxY = m_blend->yAxis.maxVal;
            float rangeY = std::max(std::abs(maxY - minY), 0.0001f);
            int divY = std::max(1, m_blend->yAxis.divisions);

            auto toScreen = [&](float ax, float ay) -> ImVec2 {
                float nx = (ax - minX) / rangeX;
                float ny = (ay - minY) / rangeY;
                return ImVec2(gridTL.x + nx * gridSize.x, gridBR.y - ny * gridSize.y);
            };

            auto toAxis = [&](const ImVec2& scr) -> glm::vec2 {
                float nx = glm::clamp((scr.x - gridTL.x) / gridSize.x, 0.0f, 1.0f);
                float ny = glm::clamp((gridBR.y - scr.y) / gridSize.y, 0.0f, 1.0f);
                return glm::vec2(minX + nx * rangeX, minY + ny * rangeY);
            };

            // Draw Vertical Grid Lines & X Axis Tick Labels
            for (int i = 0; i <= divX; ++i) {
                float frac = (float)i / (float)divX;
                float lineX = gridTL.x + frac * gridSize.x;
                dl->AddLine(ImVec2(lineX, gridTL.y), ImVec2(lineX, gridBR.y), IM_COL32(45, 45, 52, 255), 1.0f);

                float valX = minX + frac * rangeX;
                char buf[32];
                snprintf(buf, sizeof(buf), (std::abs(valX) >= 10.0f) ? "%.0f" : "%.1f", valX);
                ImVec2 tsz = ImGui::CalcTextSize(buf);
                dl->AddText(ImVec2(lineX - tsz.x * 0.5f, gridBR.y + 6.0f), IM_COL32(160, 160, 165, 255), buf);
            }

            // Draw Horizontal Grid Lines & Y Axis Tick Labels
            for (int j = 0; j <= divY; ++j) {
                float frac = (float)j / (float)divY;
                float lineY = gridBR.y - frac * gridSize.y;
                dl->AddLine(ImVec2(gridTL.x, lineY), ImVec2(gridBR.x, lineY), IM_COL32(45, 45, 52, 255), 1.0f);

                float valY = minY + frac * rangeY;
                char buf[32];
                snprintf(buf, sizeof(buf), (std::abs(valY) >= 10.0f) ? "%.0f" : "%.1f", valY);
                ImVec2 tsz = ImGui::CalcTextSize(buf);
                dl->AddText(ImVec2(gridTL.x - tsz.x - 8.0f, lineY - tsz.y * 0.5f), IM_COL32(160, 160, 165, 255), buf);
            }

            // Axis Title Labels
            std::string xTitle = m_blend->xAxis.name + " (X Axis)";
            ImVec2 xTitleSz = ImGui::CalcTextSize(xTitle.c_str());
            dl->AddText(ImVec2(gridTL.x + (gridSize.x - xTitleSz.x) * 0.5f, gridBR.y + 26.0f), IM_COL32(0, 200, 255, 255), xTitle.c_str());

            std::string yTitle = m_blend->yAxis.name + " (Y Axis)";
            dl->AddText(ImVec2(gridTL.x, gridTL.y - 20.0f), IM_COL32(0, 200, 255, 255), yTitle.c_str());

            // Handle Drag & Drop of Animations onto 2D Grid
            ImVec2 mousePos = ImGui::GetIO().MousePos;
            bool isMouseInGrid = (mousePos.x >= gridTL.x && mousePos.x <= gridBR.x &&
                                  mousePos.y >= gridTL.y && mousePos.y <= gridBR.y);

            // Invisible button covering grid to catch drag-drop and cursor interaction
            ImGui::SetCursorScreenPos(gridTL);
            ImGui::InvisibleButton("##BlendGridInteractionBtn", gridSize);

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BLEND_ANIM_CLIP")) {
                    std::string animPath = (const char*)payload->Data;
                    std::string animName = std::filesystem::path(animPath).stem().string();

                    glm::vec2 dropCoords = toAxis(mousePos);
                    BlendSamplePoint& newPt = m_blend->AddSamplePoint(animPath, animName, dropCoords);
                    m_selectedSampleId = newPt.id;
                    m_isDirty = true;

                    // Ensure clip is loaded
                    if (m_loadedClips.find(animPath) == m_loadedClips.end()) {
                        const Skeleton* skel = (m_previewMesh && m_previewMesh->valid) ? &m_previewMesh->skeleton : nullptr;
                        auto clip = SkeletalAnimationManager::Get().LoadAnimationClip(animPath, skel);
                        if (clip) m_loadedClips[animPath] = clip;
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Calculate live sample weights for cursor visualization
            std::vector<float> sampleWeights = m_blend->CalculateWeights(m_cursorX, m_cursorY);

            // Interactive Cursor (Crosshair) Movement
            if (isMouseInGrid && ImGui::IsMouseDown(0) && !m_isDraggingSample) {
                glm::vec2 axCoords = toAxis(mousePos);
                m_cursorX = axCoords.x;
                m_cursorY = axCoords.y;
                m_isDraggingCursor = true;
            }
            if (!ImGui::IsMouseDown(0)) {
                m_isDraggingCursor = false;
                m_isDraggingSample = false;
                m_draggedSampleId = 0;
            }

            ImVec2 cursorScreen = toScreen(m_cursorX, m_cursorY);

            // 1. Draw Influence Lines between Preview Cursor and Active Sample Points
            for (size_t s = 0; s < m_blend->samplePoints.size(); ++s) {
                float w = sampleWeights[s];
                if (w > 0.01f) {
                    ImVec2 sScreen = toScreen(m_blend->samplePoints[s].coords.x, m_blend->samplePoints[s].coords.y);
                    int alpha = (int)glm::clamp(w * 255.0f, 40.0f, 255.0f);
                    float thickness = 1.0f + w * 3.5f;
                    dl->AddLine(cursorScreen, sScreen, IM_COL32(0, 220, 255, alpha), thickness);
                }
            }

            // 2. Draw Sample Points as stylish glowing circles
            for (size_t s = 0; s < m_blend->samplePoints.size(); ++s) {
                auto& pt = m_blend->samplePoints[s];
                ImVec2 ptScreen = toScreen(pt.coords.x, pt.coords.y);
                bool isSelected = (m_selectedSampleId == pt.id);

                float distToMouse = std::sqrt(
                    (mousePos.x - ptScreen.x) * (mousePos.x - ptScreen.x) +
                    (mousePos.y - ptScreen.y) * (mousePos.y - ptScreen.y)
                );
                bool isHovered = (distToMouse <= 12.0f);

                // Sample point drag interaction
                if (isHovered && ImGui::IsMouseClicked(0)) {
                    m_selectedSampleId = pt.id;
                    m_isDraggingSample = true;
                    m_draggedSampleId = pt.id;
                }

                if (m_isDraggingSample && m_draggedSampleId == pt.id) {
                    glm::vec2 newAx = toAxis(mousePos);
                    pt.coords = newAx;
                    m_isDirty = true;
                    ptScreen = toScreen(pt.coords.x, pt.coords.y);
                }

                // Sample point visual
                float radius = isHovered ? 9.0f : (isSelected ? 8.5f : 7.0f);
                ImU32 fillCol = isSelected ? IM_COL32(255, 180, 0, 255) :
                                (isHovered ? IM_COL32(0, 230, 255, 255) : IM_COL32(0, 160, 220, 255));
                dl->AddCircleFilled(ptScreen, radius + 3.0f, IM_COL32(0, 0, 0, 160));
                dl->AddCircleFilled(ptScreen, radius, fillCol);
                dl->AddCircle(ptScreen, radius, IM_COL32(255, 255, 255, 220), 0, 1.5f);

                // Sample Name Badge
                std::string badge = pt.animationClipName;
                if (s < sampleWeights.size() && sampleWeights[s] > 0.005f) {
                    char wBuf[32];
                    snprintf(wBuf, sizeof(wBuf), " (%.0f%%)", sampleWeights[s] * 100.0f);
                    badge += wBuf;
                }
                ImVec2 bsz = ImGui::CalcTextSize(badge.c_str());
                ImVec2 bPos(ptScreen.x - bsz.x * 0.5f, ptScreen.y - radius - bsz.y - 4.0f);
                dl->AddRectFilled(ImVec2(bPos.x - 4.0f, bPos.y - 2.0f), ImVec2(bPos.x + bsz.x + 4.0f, bPos.y + bsz.y + 2.0f),
                    IM_COL32(20, 20, 24, 210), 4.0f);
                dl->AddText(bPos, isSelected ? IM_COL32(255, 220, 50, 255) : IM_COL32(230, 230, 230, 255), badge.c_str());
            }

            // 3. Draw Preview Cursor (Crosshair)
            dl->AddCircle(cursorScreen, 12.0f, IM_COL32(255, 255, 255, 240), 0, 2.0f);
            dl->AddCircleFilled(cursorScreen, 4.0f, IM_COL32(255, 50, 50, 255));
            dl->AddLine(ImVec2(cursorScreen.x - 18.0f, cursorScreen.y), ImVec2(cursorScreen.x + 18.0f, cursorScreen.y), IM_COL32(255, 255, 255, 200), 1.5f);
            dl->AddLine(ImVec2(cursorScreen.x, cursorScreen.y - 18.0f), ImVec2(cursorScreen.x, cursorScreen.y + 18.0f), IM_COL32(255, 255, 255, 200), 1.5f);

            // Floating live coordinate tooltip next to cursor
            char curBuf[64];
            snprintf(curBuf, sizeof(curBuf), "X: %.1f, Y: %.1f", m_cursorX, m_cursorY);
            ImVec2 curSz = ImGui::CalcTextSize(curBuf);
            ImVec2 curPos(cursorScreen.x + 16.0f, cursorScreen.y - curSz.y * 0.5f);
            dl->AddRectFilled(ImVec2(curPos.x - 4.0f, curPos.y - 2.0f), ImVec2(curPos.x + curSz.x + 4.0f, curPos.y + curSz.y + 2.0f),
                IM_COL32(10, 10, 14, 220), 4.0f);
            dl->AddText(curPos, IM_COL32(0, 255, 200, 255), curBuf);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void AnimationBlendEditor::RenderRightSidebar(const ImVec2& pos, const ImVec2& size) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));

    if (ImGui::BeginChild("BlendRightSidebar", size, true, ImGuiWindowFlags_None)) {
        float maxPreviewH = std::max(100.0f, size.y - 120.0f);
        m_previewHeight = glm::clamp(m_previewHeight, 150.0f, maxPreviewH);
        float propHeight = std::max(100.0f, size.y - m_previewHeight - 20.0f);

        // Top: 3D Live Preview
        RenderLivePreview(pos, ImVec2(size.x, m_previewHeight));

        ImGui::Spacing();

        // Draggable Vertical Splitter between 3D Live Preview and Properties
        ImVec2 splitterPos = ImGui::GetCursorScreenPos();
        float availW = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton("##PreviewPropSplitterBlend", ImVec2(availW, 6.0f));
        bool isSplitterHovered = ImGui::IsItemHovered();
        bool isSplitterActive = ImGui::IsItemActive();

        if (isSplitterHovered || isSplitterActive) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }
        if (isSplitterActive) {
            m_previewHeight += ImGui::GetIO().MouseDelta.y;
            m_previewHeight = glm::clamp(m_previewHeight, 150.0f, maxPreviewH);
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 splitterCol = isSplitterActive ? IM_COL32(0, 200, 255, 255) :
                            (isSplitterHovered ? IM_COL32(100, 160, 220, 200) : IM_COL32(50, 50, 55, 255));
        dl->AddLine(ImVec2(splitterPos.x, splitterPos.y + 2.5f), ImVec2(splitterPos.x + availW, splitterPos.y + 2.5f), splitterCol, 2.0f);

        ImGui::Spacing();

        // Bottom: Properties Panel in a scrollable child region
        if (ImGui::BeginChild("BlendPropertiesChild", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_None)) {
            RenderProperties(ImVec2(pos.x, pos.y + m_previewHeight + 10.0f), ImVec2(size.x, propHeight));
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void AnimationBlendEditor::RenderLivePreview(const ImVec2& pos, const ImVec2& size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "LIVE PREVIEW - BLEND SPACE");
    ImGui::SameLine(ImGui::GetWindowWidth() - 215.0f);
    ImGui::Checkbox("Bones", &m_showBones);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle bone rig overlay");

    ImGui::SameLine();
    if (ImGui::SmallButton(m_isPlaying ? "Pause" : "Play")) {
        m_isPlaying = !m_isPlaying;
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Reset Cam")) {
        ResetPreviewCamera();
    }

    ImVec2 previewPos = ImGui::GetCursorScreenPos();
    ImVec2 previewSize(ImGui::GetContentRegionAvail().x, size.y - 45.0f);

    dl->PushClipRect(previewPos, ImVec2(previewPos.x + previewSize.x, previewPos.y + previewSize.y), true);
    dl->AddRectFilled(previewPos, ImVec2(previewPos.x + previewSize.x, previewPos.y + previewSize.y), IM_COL32(18, 18, 18, 255));

    // Handle mouse orbit / pan in 3D preview
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

    // Camera view & projection
    float radYaw = glm::radians(m_previewYaw);
    float radPitch = glm::radians(m_previewPitch);

    glm::vec3 camPos;
    camPos.x = m_previewTarget.x + m_previewDistance * std::cos(radPitch) * std::sin(radYaw);
    camPos.y = m_previewTarget.y + m_previewDistance * std::sin(radPitch);
    camPos.z = m_previewTarget.z + m_previewDistance * std::cos(radPitch) * std::cos(radYaw);

    glm::mat4 view = glm::lookAt(camPos, m_previewTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    float aspect = (previewSize.y > 0.0f) ? (previewSize.x / previewSize.y) : 1.0f;
    float nearPlane = std::max(0.01f, m_previewDistance * 0.01f);
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

    // 3D Ground Grid
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
        struct ShadedTriangle {
            ImVec2 s0, s1, s2;
            float depth;
            ImU32 color;
        };

        std::vector<ShadedTriangle> triangles;
        triangles.reserve(8192);

        glm::vec3 keyLightDir = glm::normalize(glm::vec3(0.45f, 0.85f, 0.55f));
        glm::vec3 fillLightDir = glm::normalize(glm::vec3(-0.45f, 0.25f, -0.45f));

        for (size_t subIdx = 0; subIdx < m_previewMesh->submeshes.size(); ++subIdx) {
            const auto& sub = m_previewMesh->submeshes[subIdx];
            const auto& verts = sub.runtimeVertices;
            const auto& inds = sub.indices;
            if (verts.empty() || inds.empty()) continue;

            ::CachedTexture* pTex = nullptr;
            if (sub.material.hasMaterial && !sub.material.baseColorTexture.empty()) {
                pTex = ::TextureManager::Get().GetTexture(sub.material.baseColorTexture);
            }

            for (size_t f = 0; f + 2 < inds.size(); f += 3) {
                uint32_t i0 = inds[f];
                uint32_t i1 = inds[f + 1];
                uint32_t i2 = inds[f + 2];
                if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size()) continue;

                ImVec2 s0, s1, s2;
                float d0, d1, d2;
                if (!projectToScreen(verts[i0].pos, s0, d0) ||
                    !projectToScreen(verts[i1].pos, s1, d1) ||
                    !projectToScreen(verts[i2].pos, s2, d2)) {
                    continue;
                }

                // Backface culling
                float cross2D = (s1.x - s0.x) * (s2.y - s0.y) - (s1.y - s0.y) * (s2.x - s0.x);
                if (cross2D <= 0.0f) continue;

                // Normal & Lighting
                glm::vec3 edge1 = verts[i1].pos - verts[i0].pos;
                glm::vec3 edge2 = verts[i2].pos - verts[i0].pos;
                glm::vec3 faceNorm = glm::cross(edge1, edge2);
                float normLen = glm::length(faceNorm);
                if (normLen > 1e-4f) faceNorm /= normLen;
                else faceNorm = (verts[i0].normal + verts[i1].normal + verts[i2].normal) / 3.0f;

                float NdotKey = glm::clamp(glm::dot(faceNorm, keyLightDir), 0.0f, 1.0f);
                float NdotFill = glm::clamp(glm::dot(faceNorm, fillLightDir), 0.0f, 1.0f) * 0.25f;
                float lighting = 0.30f + NdotKey * 0.60f + NdotFill;

                glm::vec4 baseColor(0.75f, 0.75f, 0.78f, 1.0f);
                if (pTex && pTex->valid) {
                    glm::vec2 avgUv = (verts[i0].uv + verts[i1].uv + verts[i2].uv) / 3.0f;
                    baseColor = pTex->Sample(avgUv.x, avgUv.y);
                } else if (sub.material.hasMaterial && glm::length(sub.material.baseColor) > 0.01f) {
                    baseColor = glm::vec4(sub.material.baseColor, 1.0f);
                } else {
                    static const glm::vec3 s_charPalette[] = {
                        glm::vec3(0.80f, 0.76f, 0.72f),
                        glm::vec3(0.30f, 0.40f, 0.55f),
                        glm::vec3(0.55f, 0.28f, 0.28f),
                        glm::vec3(0.25f, 0.25f, 0.28f),
                        glm::vec3(0.82f, 0.68f, 0.32f)
                    };
                    baseColor = glm::vec4(s_charPalette[subIdx % 5], 1.0f);
                }

                glm::vec3 finalRgb = glm::vec3(baseColor) * lighting;
                ImU32 triCol = IM_COL32(
                    (int)glm::clamp(finalRgb.r * 255.0f, 0.0f, 255.0f),
                    (int)glm::clamp(finalRgb.g * 255.0f, 0.0f, 255.0f),
                    (int)glm::clamp(finalRgb.b * 255.0f, 0.0f, 255.0f),
                    255
                );

                float avgDepth = (d0 + d1 + d2) / 3.0f;
                triangles.push_back({ s0, s1, s2, avgDepth, triCol });
            }
        }

        std::sort(triangles.begin(), triangles.end(), [](const ShadedTriangle& a, const ShadedTriangle& b) {
            return a.depth > b.depth;
        });

        for (const auto& tri : triangles) {
            dl->AddTriangleFilled(tri.s0, tri.s1, tri.s2, tri.color);
        }

        // Draw Bone Rig Overlay only when user explicitly enables it
        if (m_showBones) {
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
                    dl->AddCircle(boneScreen, 5.5f, IM_COL32(0, 0, 0, 180), 0, 1.2f);
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
        }
    } else {
        ImVec2 textPos(previewPos.x + 20.0f, previewPos.y + previewSize.y * 0.5f - 10.0f);
        dl->AddText(textPos, IM_COL32(160, 160, 160, 255), "Select a Skeleton Mesh in Left Sidebar to view 3D preview");
    }

    // Live coordinate badge in preview
    char statusBuf[64];
    snprintf(statusBuf, sizeof(statusBuf), "Blend: X=%.1f, Y=%.1f", m_smoothedX, m_smoothedY);
    dl->AddText(ImVec2(previewPos.x + 8.0f, previewPos.y + 8.0f), IM_COL32(0, 255, 180, 255), statusBuf);

    dl->PopClipRect();

    // Advance layout cursor past the 3D preview viewport
    ImGui::Dummy(previewSize);
}

void AnimationBlendEditor::RenderProperties(const ImVec2& pos, const ImVec2& size) {
    ImGui::TextColored(ImVec4(0.0f, 0.75f, 1.0f, 1.0f), "BLEND PROPERTIES");
    ImGui::Separator();
    ImGui::Spacing();

    if (!m_blend) return;

    // Coordinate test sliders
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Live Blend Coordinates:");
    float minX = m_blend->xAxis.minVal;
    float maxX = m_blend->xAxis.maxVal;
    float minY = m_blend->yAxis.minVal;
    float maxY = m_blend->yAxis.maxVal;

    std::string xLabel = m_blend->xAxis.name + "##CursorXSlider";
    std::string yLabel = m_blend->yAxis.name + "##CursorYSlider";
    ImGui::SliderFloat(xLabel.c_str(), &m_cursorX, std::min(minX, maxX), std::max(minX, maxX), "%.1f");
    ImGui::SliderFloat(yLabel.c_str(), &m_cursorY, std::min(minY, maxY), std::max(minY, maxY), "%.1f");

    ImGui::Spacing();
    ImGui::SliderFloat("Speed##PlaybackSpeed", &m_playbackSpeed, 0.1f, 3.0f, "%.2fx");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Selected Sample Point Details
    BlendSamplePoint* selPoint = m_blend->FindSamplePoint(m_selectedSampleId);
    if (selPoint) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Selected Sample Point: #%u", selPoint->id);
        ImGui::Text("Animation: %s", selPoint->animationClipName.c_str());
        ImGui::TextDisabled("Path: %s", selPoint->animationClipPath.c_str());

        std::string xPtLabel = m_blend->xAxis.name + "##SelPtX";
        std::string yPtLabel = m_blend->yAxis.name + "##SelPtY";
        if (ImGui::DragFloat(xPtLabel.c_str(), &selPoint->coords.x, 0.5f, minX, maxX)) m_isDirty = true;
        if (ImGui::DragFloat(yPtLabel.c_str(), &selPoint->coords.y, 0.5f, minY, maxY)) m_isDirty = true;

        ImGui::Spacing();
        if (ImGui::Button("Delete Sample Point##DeleteSelPtBtn", ImVec2(-1, 0))) {
            m_blend->RemoveSamplePoint(selPoint->id);
            m_selectedSampleId = 0;
            m_isDirty = true;
        }
    } else {
        ImGui::TextDisabled("Click a sample point pin on the grid to inspect/edit its coordinates.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Table of Sample Points and Current Calculated Weights
    ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Sample Influences (%zu):", m_blend->samplePoints.size());
    std::vector<float> weights = m_blend->CalculateWeights(m_smoothedX, m_smoothedY);

    if (ImGui::BeginTable("SampleInfluenceTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Clip");
        ImGui::TableSetupColumn("Position");
        ImGui::TableSetupColumn("Weight");
        ImGui::TableHeadersRow();

        for (size_t s = 0; s < m_blend->samplePoints.size(); ++s) {
            const auto& pt = m_blend->samplePoints[s];
            float w = (s < weights.size()) ? weights[s] : 0.0f;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool isSelected = (m_selectedSampleId == pt.id);
            if (ImGui::Selectable(pt.animationClipName.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                m_selectedSampleId = pt.id;
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("(%.1f, %.1f)", pt.coords.x, pt.coords.y);

            ImGui::TableSetColumnIndex(2);
            if (w > 0.01f) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "%.1f%%", w * 100.0f);
            } else {
                ImGui::TextDisabled("0%%");
            }
        }
        ImGui::EndTable();
    }
}

} // namespace Eunoia
