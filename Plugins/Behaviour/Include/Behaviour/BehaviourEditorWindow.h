#pragma once

#include "BehaviourAsset.h"
#include <EngineScene/Scene.h>
#include <EngineRenderer/Camera.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <glm/glm.hpp>

class Scene;

namespace Eunoia {

class BehaviourEditorWindow {
public:
    BehaviourAsset asset;
    std::string filePath;
    std::string name;
    bool isOpen = true;
    bool isDirty = false;
    bool autoSave = true;

    const std::string& GetName() const { return name; }
    bool IsDirty() const { return isDirty; }

    // Development Preview Stage & Isolated Studio Lights
    Scene previewScene;
    OrbitCamera camera;
    bool previewSceneInitialized = false;
    int currentGizmoOperation = 0; // 0=Translate, 1=Rotate, 2=Scale
    int currentGizmoMode = 0; // 0=Local, 1=World
    int viewportShadingMode = 0; // 0=Lit, 1=Unlit, 2=Wireframe
    char outlinerSearchBuf[128] = "";

    Scene& GetPreviewScene() { return previewScene; }
    OrbitCamera& GetCamera() { return camera; }
    void SyncPreviewScene();
    void SetupStudioLighting();

    // Selection States
    int selectedComponentId = 1; // Default to root
    std::string selectedScriptId = ""; // If non-empty, details shows script properties
    bool isSelectingScript = false;

    // Viewport Camera Navigation
    glm::vec3 cameraTarget{0.0f, 0.4f, 0.0f};
    float cameraYaw = 45.0f;
    float cameraPitch = 22.0f;
    float cameraDistance = 5.0f;
    float cameraSpeed = 1.0f;

    BehaviourEditorWindow(const std::string& path, const std::string& inName = "");

    void Save();
    void RenderContent(Scene* mainLevelScene = nullptr);

    // Sub-Panel Dockable Renderers
    void RenderTopToolbar();
    void RenderOutlinerPanel();
    void RenderOutlinerNode(int compId, const std::string& filterLower);
    void RenderBehaviourScriptsPanel();
    void RenderViewportPanel();
    void RenderDetailsPanel();
    void RenderContentBrowserPanel();

    void CreateInternalScript(const std::string& scriptName);
};

} // namespace Eunoia
