#pragma once

#include <Animation-Graph/AnimatorGraph.h>
#include <Animation-Graph/SkeletalAnimation.h>
#include <imgui.h>
#include <memory>
#include <vector>
#include <string>

namespace Eunoia {

class AnimatorGraphEditor {
public:
    AnimatorGraphEditor(std::shared_ptr<AnimatorGraph> graph);
    ~AnimatorGraphEditor() = default;

    void Render(const ImVec2& position, const ImVec2& size);

    bool IsDirty() const { return m_isDirty; }
    void SetDirty(bool dirty) { m_isDirty = dirty; }

    std::shared_ptr<AnimatorGraph> GetGraph() const { return m_graph; }
    const std::string& GetName() const { return m_graph ? m_graph->name : m_defaultName; }
    const std::string& GetFilePath() const { return m_graph ? m_graph->assetPath : m_defaultPath; }

    void Save();

    // Live preview camera controls
    void ResetPreviewCamera();

private:
    std::shared_ptr<AnimatorGraph> m_graph;
    std::unique_ptr<AnimatorInstance> m_instance;
    std::shared_ptr<SkeletalMesh> m_previewMesh;
    std::unordered_map<std::string, std::shared_ptr<AnimationClip>> m_loadedClips;

    bool m_isDirty = false;
    float m_saveNotificationTimer = 0.0f;
    std::string m_defaultName = "Animator Graph";
    std::string m_defaultPath = "";

    // Sidebar Layout (Adjustable horizontally)
    float m_leftSidebarWidth = 300.0f;
    float m_rightSidebarWidth = 554.0f; // +50% increase over previous 369px

    // Selection State
    uint32_t m_selectedNodeId = 0;
    uint32_t m_selectedArrowId = 0;

    // Canvas Interaction
    ImVec2 m_canvasPan{0.0f, 0.0f};
    float m_canvasZoom = 1.0f;
    bool m_isDraggingNode = false;
    uint32_t m_draggedNodeId = 0;
    ImVec2 m_dragNodeStartPos{0.0f, 0.0f};

    // Transition Arrow Dragging
    bool m_isConnecting = false;
    uint32_t m_connectingFromNodeId = 0;
    ImVec2 m_connectingMousePos{0.0f, 0.0f};

    // Canvas Context Menu
    bool m_openCanvasContext = false;
    ImVec2 m_canvasContextPos{0.0f, 0.0f};

    // Parameters UI
    char m_newParamNameBuf[128] = "NewParam";
    int m_newParamTypeIdx = 0;
    bool m_showAddParamPopup = false;

    // Preview Camera
    float m_previewYaw = 45.0f;
    float m_previewPitch = 20.0f;
    float m_previewDistance = 3.5f;
    glm::vec3 m_previewTarget{0.0f, 1.0f, 0.0f};
    bool m_isPreviewDragging = false;
    ImVec2 m_lastMousePos{0.0f, 0.0f};

    void SyncPreviewResources();
    void UpdateSimulation(float deltaTime);

    void RenderLeftSidebar(const ImVec2& pos, const ImVec2& size);
    void RenderCanvas(const ImVec2& pos, const ImVec2& size);
    void RenderRightSidebar(const ImVec2& pos, const ImVec2& size);

    void RenderLivePreview(const ImVec2& pos, const ImVec2& size);
    void RenderProperties(const ImVec2& pos, const ImVec2& size);

    // Helper functions for drawing arrows & nodes
    void DrawArrow(ImDrawList* dl, const ImVec2& p1, const ImVec2& p2, ImU32 color, float thickness, bool isSelected, uint32_t transitionId);
};

class AnimatorGraphManager {
public:
    static AnimatorGraphManager& Get();

    int activeTabIndex = -1;

    std::shared_ptr<AnimatorGraphEditor> Open(const std::string& filePath);
    void Close(size_t index);
    void SwitchToEditor(int index);
    void SwitchToMainEditor();

    bool IsActive() const { return activeTabIndex >= 0 && activeTabIndex < (int)m_editors.size(); }
    int GetActiveEditorIndex() const { return activeTabIndex; }
    std::shared_ptr<AnimatorGraphEditor> GetActiveEditor();

    const std::vector<std::shared_ptr<AnimatorGraphEditor>>& GetEditors() const { return m_editors; }

    void RenderActiveTabWorkspace(const ImVec2& pos, const ImVec2& size);

    // Creates a new .eanimator file on disk with default "Begin" node
    std::string CreateNewAnimatorGraph(const std::string& folderPath, const std::string& name);

private:
    AnimatorGraphManager() = default;
    ~AnimatorGraphManager() = default;

    std::vector<std::shared_ptr<AnimatorGraphEditor>> m_editors;
};

} // namespace Eunoia
