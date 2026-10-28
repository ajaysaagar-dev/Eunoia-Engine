#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <imgui.h>
#include <glm/glm.hpp>
#include <Animation-Blending/AnimationBlend.h>
#include <Animation-Graph/SkeletalAnimation.h>

namespace Eunoia {

class AnimationBlendEditor {
public:
    AnimationBlendEditor(std::shared_ptr<AnimationBlend> blend);
    ~AnimationBlendEditor() = default;

    void Render(const ImVec2& pos, const ImVec2& size);
    void Update(float deltaTime);

    bool IsDirty() const { return m_isDirty; }
    void SetDirty(bool dirty) { m_isDirty = dirty; }

    std::shared_ptr<AnimationBlend> GetBlend() const { return m_blend; }
    const std::string& GetName() const { return m_blend ? m_blend->name : m_defaultName; }
    const std::string& GetFilePath() const { return m_blend ? m_blend->assetPath : m_defaultPath; }

    void Save();
    void ResetPreviewCamera();

private:
    std::shared_ptr<AnimationBlend> m_blend;
    std::shared_ptr<SkeletalMesh> m_previewMesh;
    std::unordered_map<std::string, std::shared_ptr<AnimationClip>> m_loadedClips;

    bool m_isDirty = false;
    float m_saveNotificationTimer = 0.0f;
    std::string m_defaultName = "Animation Blend";
    std::string m_defaultPath = "";

    // Sidebar Layout (Adjustable horizontally & vertically)
    float m_leftSidebarWidth = 300.0f;
    float m_rightSidebarWidth = 554.0f;
    float m_previewHeight = 450.0f;
    bool m_showBones = false; // Hidden by default, no yellow bones

    // 2D Blend Grid State
    float m_cursorX = 0.0f;
    float m_cursorY = 0.0f;
    bool m_isDraggingCursor = false;
    uint32_t m_selectedSampleId = 0;
    bool m_isDraggingSample = false;
    uint32_t m_draggedSampleId = 0;

    // Playback state
    float m_previewNormalizedTime = 0.0f;
    float m_playbackSpeed = 1.0f;
    bool m_isPlaying = true;

    // Preview Camera
    float m_previewYaw = 45.0f;
    float m_previewPitch = 20.0f;
    float m_previewDistance = 3.5f;
    glm::vec3 m_previewTarget{0.0f, 1.0f, 0.0f};
    bool m_isPreviewDragging = false;
    ImVec2 m_lastMousePos{0.0f, 0.0f};

    void SyncPreviewResources();
    void RenderLeftSidebar(const ImVec2& pos, const ImVec2& size);
    void RenderGridCanvas(const ImVec2& pos, const ImVec2& size);
    void RenderRightSidebar(const ImVec2& pos, const ImVec2& size);

    void RenderLivePreview(const ImVec2& pos, const ImVec2& size);
    void RenderProperties(const ImVec2& pos, const ImVec2& size);
};

} // namespace Eunoia
