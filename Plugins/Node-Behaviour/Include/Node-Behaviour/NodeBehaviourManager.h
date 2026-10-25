#pragma once
#include <Node-Behaviour/NodeBehaviourEditor.h>
#include <vector>
#include <memory>
#include <string>

namespace Eunoia {
namespace NodeBehaviour {

class NodeBehaviourManager {
public:
    static NodeBehaviourManager& Get();

    int activeTabIndex = -1; // -1 = Level Editor or other workspace

    std::shared_ptr<NodeBehaviourEditor> Open(const std::string& filePath);
    void Close(size_t index);
    void SwitchToEditor(int index);
    void SwitchToMainEditor();

    bool IsActive() const { return activeTabIndex >= 0 && activeTabIndex < (int)m_editors.size(); }
    int GetActiveEditorIndex() const { return activeTabIndex; }
    std::shared_ptr<NodeBehaviourEditor> GetActiveEditor();

    const std::vector<std::shared_ptr<NodeBehaviourEditor>>& GetEditors() const { return m_editors; }

    void RenderActiveTabWorkspace(const ImVec2& pos, const ImVec2& size);

    // Creates a new .enb file on disk with default events and opens it
    std::string CreateNewNodeBehaviour(const std::string& folderPath, const std::string& name);

private:
    NodeBehaviourManager() = default;
    ~NodeBehaviourManager() = default;

    std::vector<std::shared_ptr<NodeBehaviourEditor>> m_editors;
};

} // namespace NodeBehaviour
} // namespace Eunoia
