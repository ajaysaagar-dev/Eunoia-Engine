#pragma once

#include "BehaviourEditorWindow.h"
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>

class Scene;

namespace Eunoia {

class BehaviourManager {
public:
    static BehaviourManager& Get();

    int activeTabIndex = -1; // -1 = Main Level Editor, 0..N = Open Behaviour Editors

    std::shared_ptr<BehaviourEditorWindow> Open(const std::string& filePath, const std::string& name = "");
    void Close(size_t index);
    void SwitchToEditor(int index);
    void SwitchToMainEditor();

    bool IsActive() const { return activeTabIndex >= 0 && activeTabIndex < (int)m_editors.size(); }
    int GetActiveEditorIndex() const { return activeTabIndex; }
    std::shared_ptr<BehaviourEditorWindow> GetActiveEditor();

    const std::vector<std::shared_ptr<BehaviourEditorWindow>>& GetEditors() const { return m_editors; }
    size_t GetOpenEditorCount() const { return m_editors.size(); }

    void RenderActiveTabWorkspace(const glm::vec2& pos, const glm::vec2& size, Scene* scene = nullptr);

    // Asset Creation & Level Spawning
    std::string CreateNewBehaviour(const std::string& folderPath, const std::string& name);
    std::string CreateNewBehaviour(const std::string& fullPath);
    GameObject* SpawnBehaviourIntoScene(Scene& scene, const std::string& path, const glm::vec3& spawnPos);

private:
    BehaviourManager() = default;
    ~BehaviourManager() = default;

    std::vector<std::shared_ptr<BehaviourEditorWindow>> m_editors;
};

} // namespace Eunoia
