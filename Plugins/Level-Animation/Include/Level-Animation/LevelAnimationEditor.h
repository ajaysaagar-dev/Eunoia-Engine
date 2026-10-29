#pragma once

#include <string>
#include <vector>
#include <memory>
#include <imgui.h>
#include <Level-Animation/LevelAnimation.h>

class Scene;

namespace Eunoia {

class LevelAnimationEditor {
public:
    static LevelAnimationEditor& Get();

    void RenderTimeline(Scene& scene, const ImVec2& pos, const ImVec2& size);
    void UpdatePlayback(Scene& scene, float dt);

    void SetActiveAnimation(std::shared_ptr<LevelAnimation> anim);
    std::shared_ptr<LevelAnimation> GetActiveAnimation();

    void Play();
    void Pause();
    void Stop();
    void StepForward(float step = 1.0f / 30.0f);
    void StepBackward(float step = 1.0f / 30.0f);

    float GetCurrentTime() const { return m_currentTime; }
    void SetCurrentTime(float t);

    float GetDuration() const { return m_duration; }
    void SetDuration(float d);

    bool IsPlaying() const { return m_isPlaying; }

private:
    LevelAnimationEditor();
    ~LevelAnimationEditor() = default;

    void RenderTopBar(Scene& scene, float width);
    void RenderLeftSidebar(Scene& scene, float width, float height);
    void RenderTimelineGraph(Scene& scene, float width, float height);

    float m_currentTime = 0.0f;
    float m_duration = 5.0f;
    bool m_isPlaying = false;
    bool m_loop = true;
    float m_playbackSpeed = 1.0f;

    // Timeline view settings
    float m_timelineZoom = 120.0f; // Pixels per second
    float m_scrollX = 0.0f;
    float m_sidebarWidth = 260.0f;

    // Keyframe selection & dragging
    int m_selectedTrackObjId = -1;
    PropertyChannelType m_selectedChannel = PropertyChannelType::PositionX;
    int m_selectedKeyIndex = -1;
    bool m_isDraggingPlayhead = false;
    bool m_isDraggingKey = false;
    float m_dragKeyInitialTime = 0.0f;

    // Object search in + Add popup
    char m_searchFilterBuf[128] = "";
    bool m_openAddObjectPopup = false;
};

} // namespace Eunoia
