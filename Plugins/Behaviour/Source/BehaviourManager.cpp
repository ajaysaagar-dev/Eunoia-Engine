#include "Behaviour/BehaviourManager.h"
#include <EngineAssets/AssetRegistry.h>
#include <EngineScene/Scene.h>
#include <fstream>
#include <iostream>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Eunoia {

BehaviourManager& BehaviourManager::Get() {
    static BehaviourManager instance;
    return instance;
}

std::shared_ptr<BehaviourEditorWindow> BehaviourManager::Open(const std::string& filePath, const std::string& name) {
    for (size_t i = 0; i < m_editors.size(); ++i) {
        if (m_editors[i]->filePath == filePath) {
            m_editors[i]->isOpen = true;
            activeTabIndex = (int)i;
            return m_editors[i];
        }
    }

    auto editor = std::make_shared<BehaviourEditorWindow>(filePath, name);
    m_editors.push_back(editor);
    activeTabIndex = (int)m_editors.size() - 1;
    return editor;
}

void BehaviourManager::Close(size_t index) {
    if (index < m_editors.size()) {
        if (m_editors[index]->autoSave || m_editors[index]->isDirty) {
            m_editors[index]->Save();
        }
        m_editors.erase(m_editors.begin() + index);
        if (activeTabIndex >= (int)m_editors.size()) {
            activeTabIndex = (int)m_editors.size() - 1;
        }
    }
}

void BehaviourManager::SwitchToEditor(int index) {
    if (index >= 0 && index < (int)m_editors.size()) {
        activeTabIndex = index;
    }
}

void BehaviourManager::SwitchToMainEditor() {
    activeTabIndex = -1;
}

std::shared_ptr<BehaviourEditorWindow> BehaviourManager::GetActiveEditor() {
    if (activeTabIndex >= 0 && activeTabIndex < (int)m_editors.size()) {
        return m_editors[activeTabIndex];
    }
    return nullptr;
}

void BehaviourManager::RenderActiveTabWorkspace(const glm::vec2& pos, const glm::vec2& size, Scene* scene) {
    auto activeEditor = GetActiveEditor();
    if (!activeEditor || !activeEditor->isOpen) return;

    ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(size.x, size.y), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.082f, 0.082f, 0.102f, 1.0f));

    std::string winId = "##BehaviourWorkspace_" + activeEditor->filePath;
    if (ImGui::Begin(winId.c_str(), nullptr, flags)) {
        activeEditor->RenderContent(scene);
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

std::string BehaviourManager::CreateNewBehaviour(const std::string& folderPath, const std::string& name) {
    std::filesystem::path fPath = std::filesystem::path(folderPath) / (name + ".behaviour");
    std::filesystem::path metaPath = std::filesystem::path(folderPath) / (name + ".behaviour.assetmeta");

    std::error_code ec;
    std::filesystem::create_directories(folderPath, ec);

    BehaviourAsset asset(name);
    asset.SaveToFile(fPath.string());

    AssetID aid = AssetID::CreateRandom();
    std::string vPath = "/" + name;

    std::ofstream mf(metaPath);
    if (mf.is_open()) {
        mf << "# Eunoia-Editor Asset Sidecar Metadata\n";
        mf << "assetId: " << aid.ToString() << "\n";
        mf << "type: Behaviour\n";
        mf << "virtualPath: " << vPath << "\n";
    }
#ifdef _WIN32
    SetFileAttributesW(metaPath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif

    AssetMetadata meta;
    meta.id = aid;
    meta.type = AssetType::Behaviour;
    meta.objectName = name;
    meta.sourcePath = (name + ".behaviour");
    meta.virtualPath = vPath;
    AssetRegistry::Get().RegisterAsset(meta);

    return fPath.string();
}

std::string BehaviourManager::CreateNewBehaviour(const std::string& fullPath) {
    std::filesystem::path p(fullPath);
    std::string stem = p.stem().string();
    return CreateNewBehaviour(p.parent_path().string(), stem);
}

GameObject* BehaviourManager::SpawnBehaviourIntoScene(Scene& scene, const std::string& path, const glm::vec3& spawnPos) {
    BehaviourAsset asset;
    if (!asset.LoadFromFile(path)) return nullptr;
    return asset.SpawnIntoScene(scene, spawnPos);
}

} // namespace Eunoia
