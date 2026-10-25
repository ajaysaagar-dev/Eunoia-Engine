#pragma once
#include <Node-Behaviour/NodeBehaviourGraph.h>
#include <imgui.h>

class Scene;

namespace ax {
namespace NodeEditor {
    struct EditorContext;
}
}

namespace Eunoia {
namespace NodeBehaviour {

class NodeBehaviourEditor {
public:
    NodeBehaviourEditor(std::shared_ptr<NodeBehaviourGraph> graph);
    ~NodeBehaviourEditor();

    void Render(const ImVec2& position, const ImVec2& size);
    void SetScene(Scene* scene) { m_scene = scene; }

    bool IsDirty() const { return m_isDirty; }
    void SetDirty(bool dirty) { m_isDirty = dirty; }

    std::shared_ptr<NodeBehaviourGraph> GetGraph() const { return m_graph; }
    const std::string& GetName() const { return m_graph ? m_graph->name : m_defaultName; }
    const std::string& GetFilePath() const { return m_graph ? m_graph->filePath : m_defaultPath; }

    void Save();
    void Compile();

private:
    std::shared_ptr<NodeBehaviourGraph> m_graph;
    Scene* m_scene = nullptr;
    ax::NodeEditor::EditorContext* m_context = nullptr;
    bool m_isDirty = false;
    std::string m_defaultName = "Node Behaviour";
    std::string m_defaultPath = "";

    // Right Sidebar State
    uint32_t m_selectedVarId = 0;
    char m_varNameBuf[128] = {0};
    char m_varTooltipBuf[256] = {0};
    char m_varStringValBuf[256] = {0};

    // Right-Click Action Palette / Search State
    bool m_openSearchPopup = false;
    ImVec2 m_searchPopupPos{0.0f, 0.0f};
    char m_searchFilterBuf[128] = {0};
    bool m_focusSearchInput = false;

    // Pin dragging / link creation state
    uint32_t m_activeNewLinkPinId = 0;

    void RenderTopToolbar();
    void RenderCanvas(const ImVec2& size);
    void RenderRightSidebar(const ImVec2& size);
    void RenderVariablesList(float height);
    void RenderVariableDetails(float height);

    void RenderNode(NodeItem& node);
    void RenderSearchPopup();
};

} // namespace NodeBehaviour
} // namespace Eunoia
