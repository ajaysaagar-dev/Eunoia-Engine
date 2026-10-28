#include <Animation-Blending/AnimationBlendManager.h>
#include <filesystem>
#include <algorithm>

namespace Eunoia {

AnimationBlendManager& AnimationBlendManager::Get() {
    static AnimationBlendManager s_instance;
    return s_instance;
}

std::shared_ptr<AnimationBlendEditor> AnimationBlendManager::Open(const std::string& filePath) {
    std::string normPath = filePath;
    std::replace(normPath.begin(), normPath.end(), '\\', '/');

    for (size_t i = 0; i < m_editors.size(); ++i) {
        if (m_editors[i]->GetFilePath() == normPath) {
            activeTabIndex = (int)i;
            return m_editors[i];
        }
    }

    auto blend = std::make_shared<AnimationBlend>();
    if (!blend->LoadFromFile(normPath)) {
        blend->name = std::filesystem::path(normPath).stem().string();
        blend->assetPath = normPath;
        blend->SaveToFile(normPath);
    }

    auto editor = std::make_shared<AnimationBlendEditor>(blend);
    m_editors.push_back(editor);
    activeTabIndex = (int)m_editors.size() - 1;
    return editor;
}

void AnimationBlendManager::Close(size_t index) {
    if (index >= m_editors.size()) return;
    if (m_editors[index]->IsDirty()) {
        m_editors[index]->Save();
    }
    m_editors.erase(m_editors.begin() + index);
    if (activeTabIndex >= (int)m_editors.size()) {
        activeTabIndex = (int)m_editors.size() - 1;
    }
}

void AnimationBlendManager::SwitchToEditor(int index) {
    if (index >= 0 && index < (int)m_editors.size()) {
        activeTabIndex = index;
    }
}

void AnimationBlendManager::SwitchToMainEditor() {
    activeTabIndex = -1;
}

std::shared_ptr<AnimationBlendEditor> AnimationBlendManager::GetActiveEditor() {
    if (activeTabIndex >= 0 && activeTabIndex < (int)m_editors.size()) {
        return m_editors[activeTabIndex];
    }
    return nullptr;
}

void AnimationBlendManager::RenderActiveTabWorkspace(const ImVec2& pos, const ImVec2& size) {
    auto activeEditor = GetActiveEditor();
    if (activeEditor) {
        activeEditor->Render(pos, size);
    }
}

std::string AnimationBlendManager::CreateNewAnimationBlend(const std::string& folderPath, const std::string& name) {
    std::filesystem::path dir(folderPath);
    std::string fileName = name;
    if (fileName.find(".eblend") == std::string::npos) {
        fileName += ".eblend";
    }
    std::filesystem::path fullPath = dir / fileName;

    auto blend = std::make_shared<AnimationBlend>();
    blend->name = name;
    blend->assetPath = fullPath.generic_string();
    blend->SaveToFile(blend->assetPath);

    return blend->assetPath;
}

} // namespace Eunoia
