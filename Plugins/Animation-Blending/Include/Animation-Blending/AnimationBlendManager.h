#pragma once

#include <string>
#include <vector>
#include <memory>
#include <imgui.h>
#include <Animation-Blending/AnimationBlendEditor.h>

namespace Eunoia {

class AnimationBlendManager {
public:
    static AnimationBlendManager& Get();

    int activeTabIndex = -1;

    std::shared_ptr<AnimationBlendEditor> Open(const std::string& filePath);
    void Close(size_t index);
    void SwitchToEditor(int index);
    void SwitchToMainEditor();

    bool IsActive() const { return activeTabIndex >= 0 && activeTabIndex < (int)m_editors.size(); }
    int GetActiveEditorIndex() const { return activeTabIndex; }
    std::shared_ptr<AnimationBlendEditor> GetActiveEditor();

    const std::vector<std::shared_ptr<AnimationBlendEditor>>& GetEditors() const { return m_editors; }

    void RenderActiveTabWorkspace(const ImVec2& pos, const ImVec2& size);

    // Creates a new .eblend file on disk
    std::string CreateNewAnimationBlend(const std::string& folderPath, const std::string& name);

private:
    AnimationBlendManager() = default;
    ~AnimationBlendManager() = default;

    std::vector<std::shared_ptr<AnimationBlendEditor>> m_editors;
};

} // namespace Eunoia
