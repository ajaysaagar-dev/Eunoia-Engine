#include <Node-Behaviour/NodeBehaviourManager.h>
#include <filesystem>

namespace Eunoia {
namespace NodeBehaviour {

NodeBehaviourManager& NodeBehaviourManager::Get() {
    static NodeBehaviourManager instance;
    return instance;
}

std::shared_ptr<NodeBehaviourEditor> NodeBehaviourManager::Open(const std::string& filePath) {
    std::error_code ec;
    std::filesystem::path canonicalPath = std::filesystem::canonical(filePath, ec);
    std::string pathStr = ec ? filePath : canonicalPath.string();

    // Check if already open
    for (size_t i = 0; i < m_editors.size(); ++i) {
        if (m_editors[i]->GetFilePath() == pathStr || m_editors[i]->GetFilePath() == filePath) {
            activeTabIndex = (int)i;
            return m_editors[i];
        }
    }

    // Load graph from file
    auto graph = std::make_shared<NodeBehaviourGraph>();
    if (!graph->LoadFromFile(pathStr)) {
        // Fallback for new empty file
        graph->filePath = pathStr;
        graph->name = std::filesystem::path(pathStr).stem().string();
        graph->InitializeDefaultEvents();
    }

    auto editor = std::make_shared<NodeBehaviourEditor>(graph);
    m_editors.push_back(editor);
    activeTabIndex = (int)m_editors.size() - 1;
    return editor;
}

void NodeBehaviourManager::Close(size_t index) {
    if (index >= m_editors.size()) return;

    m_editors.erase(m_editors.begin() + index);
    if (m_editors.empty()) {
        activeTabIndex = -1;
    } else if (activeTabIndex >= (int)m_editors.size()) {
        activeTabIndex = (int)m_editors.size() - 1;
    }
}

void NodeBehaviourManager::SwitchToEditor(int index) {
    if (index >= 0 && index < (int)m_editors.size()) {
        activeTabIndex = index;
    }
}

void NodeBehaviourManager::SwitchToMainEditor() {
    activeTabIndex = -1;
}

std::shared_ptr<NodeBehaviourEditor> NodeBehaviourManager::GetActiveEditor() {
    if (IsActive()) {
        return m_editors[activeTabIndex];
    }
    return nullptr;
}

void NodeBehaviourManager::RenderActiveTabWorkspace(const ImVec2& pos, const ImVec2& size) {
    if (IsActive()) {
        m_editors[activeTabIndex]->Render(pos, size);
    }
}

std::string NodeBehaviourManager::CreateNewNodeBehaviour(const std::string& folderPath, const std::string& name) {
    std::filesystem::path dir(folderPath);
    std::filesystem::path fullPath = dir / (name + ".enb");

    auto graph = std::make_shared<NodeBehaviourGraph>();
    graph->filePath = fullPath.string();
    graph->name = name;
    graph->InitializeDefaultEvents();
    graph->SaveToFile(fullPath.string());

    Open(fullPath.string());
    return fullPath.string();
}

} // namespace NodeBehaviour
} // namespace Eunoia
