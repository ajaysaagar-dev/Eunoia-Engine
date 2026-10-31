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
    glm::vec3 cameraTarget{0.0f, 1.0f, 0.0f};
    float cameraYaw = 45.0f;
    float cameraPitch = 25.0f;
    float cameraDistance = 5.0f;
    bool isOrbiting = false;
    bool isPanning = false;
    ImVec2 mousePrevPos{0.0f, 0.0f};

    // Content Browser Navigation (inside Behaviour Editor)
    std::string cbCurrentDir = "/Game";

    BehaviourEditorWindow(const std::string& path, const std::string& inName = "");

    void Save();
    void RenderContent(Scene* mainLevelScene = nullptr);

    // Sub-Panel Renderers
    void RenderTopToolbar();
    void RenderOutlinerPanel(const ImVec2& size);
    void RenderBehaviourScriptsPanel(const ImVec2& size);
    void RenderViewportPanel(const ImVec2& size);
    void RenderDetailsPanel(const ImVec2& size);
    void RenderContentBrowserPanel(const ImVec2& size);

    void CreateInternalScript(const std::string& scriptName);
};

} // namespace Eunoia
