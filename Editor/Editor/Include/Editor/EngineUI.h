#pragma once
#include <EngineScene/Scene.h>
#include <EngineRenderer/Camera.h>
#include <EngineAssets/AssetSystem.h>
#include <EngineScene/SceneSerializer.h>
#include <Graphics/GraphicsSystem.h>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>

struct GLFWwindow;
struct ImGuiContext;

class SplashScreen {
public:
    static SplashScreen& Get() {
        static SplashScreen s_instance;
        return s_instance;
    }

    void Init(GLFWwindow* window, ImGuiContext* ctx, const std::string& projectName);
    void UpdateProgress(float progress, const std::string& statusMessage);
    void Finish();
    bool IsActive() const { return m_active; }

private:
    SplashScreen() = default;

    GLFWwindow* m_window = nullptr;
    ImGuiContext* m_imguiContext = nullptr;
    std::string m_projectName;
    std::string m_statusMessage = "Loading Eunoia Engine...";
    float m_currentProgress = 0.0f;
    unsigned int m_logoTexture = 0;
    int m_logoWidth = 0;
    int m_logoHeight = 0;
    bool m_active = false;

    void RenderFrame();
};

struct EngineLogEntry {
    std::string category;
    std::string message;
    int level; // 0=Info, 1=Warning, 2=Success, 3=Error
};

struct TextureAssetEntry {
    std::string filename;  // e.g. "grass_ground_diff_2k.png"
    std::string relPath;   // e.g. "Materials/textures/grass_ground_diff_2k.png"
    std::string relFolder; // e.g. "Materials/textures"
    std::string fullPath;  // absolute path
    AssetID assetId;       // Stable AssetID
    std::string virtualPath; // e.g. "/Game/Materials/textures/grass_ground_diff_2k"
};

struct MaterialAsset {
    AssetID assetId;
    std::string virtualPath = "";
    std::string name = "M_NewMaterial";
    std::string filePath = "";

    // Surface Textures with stable AssetIDs (dev.md Section 21)
    std::string baseColorTexture = "";
    AssetID baseColorAssetId;
    std::string normalTexture = "";
    AssetID normalAssetId;
    std::string roughnessTexture = "";
    AssetID roughnessAssetId;
    std::string metallicTexture = "";
    AssetID metallicAssetId;
    std::string aoTexture = "";
    AssetID aoAssetId;
    std::string emissionTexture = "";
    AssetID emissionAssetId;
    std::string opacityTexture = "";
    AssetID opacityAssetId;

    // Values (from dev.md)
    glm::vec3 baseColor{0.8f, 0.8f, 0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float normalStrength = 1.0f;
    float specular = 0.5f;
    glm::vec3 emissiveColor{0.0f, 0.0f, 0.0f};
    float emissiveIntensity = 0.0f; // Emission Power
    float opacity = 1.0f;
    float opacityMaskClipValue = 0.333f;

    // UV Tiling / Scale (default 1.0, 1.0)
    glm::vec2 uvScale{1.0f, 1.0f};

    // Settings (from dev.md)
    int shadingModel = 0; // 0=PBR (Default Lit), 1=Unlit, 2=Subsurface
    int blendMode = 0;    // 0=Opaque, 1=Masked, 2=Translucent
    bool twoSided = false;
    bool castShadows = true;
    bool receiveShadows = true;
    bool normalMapYFlip = false;
    int metallicChannel = 0;  // 0=R, 1=G, 2=B, 3=A
    int roughnessChannel = 1; // 0=R, 1=G, 2=B, 3=A
    int aoChannel = 0;        // 0=R, 1=G, 2=B, 3=A
    int materialDebugMode = 0;
    bool disableStreaming = false; // Wicked Engine: Keep textures of this material at full native resolution
};

struct SkyboxAsset {
    AssetID assetId;
    std::string virtualPath = "";
    std::string name = "Skybox_New";
    std::string filePath = "";

    // HDR Texture (Equirectangular panorama or cubemap)
    std::string hdrTexturePath = "";
    AssetID hdrTextureAssetId;

    // Skybox Properties
    float exposure = 1.0f;
    float rotation = 0.0f; // degrees 0..360
    bool autoRotate = false;
    float autoRotateSpeed = 1.0f;
    glm::vec3 tint{1.0f, 1.0f, 1.0f};
    float ambientContribution = 0.5f;
    bool enableSkybox = true;

    // Skybox Based Lighting Settings (default disabled)
    bool enableSkyboxLighting = false;
    float skyboxLightingIntensity = 1.0f;
    glm::vec3 skyboxLightingColor{1.0f, 1.0f, 1.0f};
    float skyboxLightingSpecular = 1.0f;
    float skyboxLightingDiffuse = 1.0f;
};

struct WaterAsset {
    AssetID assetId;
    std::string virtualPath = "";
    std::string name = "WB_NewWater";
    std::string filePath = "";
    WaterBodyType waterType = WaterBodyType::Ocean;
    WaterBodyComponent settings; // all water settings
};

struct PendingModelMaterial {
    std::string materialName = "M_Material";
    glm::vec3 baseColor{0.8f, 0.8f, 0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    glm::vec3 emissiveColor{0.0f, 0.0f, 0.0f};
    float emissiveIntensity = 0.0f;
    int blendMode = 0; // 0=Opaque, 1=Masked, 2=Translucent
    float opacity = 1.0f;

    std::string baseColorTexture = "";
    std::string normalTexture = "";
    std::string roughnessTexture = "";
    std::string metallicTexture = "";
    std::string aoTexture = "";
    std::string emissionTexture = "";
    std::string opacityTexture = "";

    bool createMaterial = true;
    std::vector<int> submeshIndices;
};

struct ModelImportState {
    std::string sourceFilePath = "";
    std::string modelName = "";
    std::string targetFolder = "";
    bool spawnInLevel = true;
    ImportedModel previewModel;
    std::vector<PendingModelMaterial> materials;

    // Bounds & Geometry statistics
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    glm::vec3 boundsCenter{0.0f};
    float boundsExtent = 1.0f;
    uint32_t totalVertices = 0;
    uint32_t totalTriangles = 0;
    uint32_t meshCount = 0;

    // Interactive 3D preview viewport
    float previewYaw = 35.0f;
    float previewPitch = 20.0f;
    float previewZoom = 1.0f;
    glm::vec2 previewPan{0.0f, 0.0f};
    bool autoRotate = true;
    float autoRotateSpeed = 25.0f;
    int viewMode = 0; // 0=Shaded, 1=Shaded+Wireframe, 2=Wireframe Only
    bool showGrid = true;
};

class EngineUI {
public:
    // Window visibility flags (UE5 panels)
    bool showOutliner = true;
    bool showDetails = true;
    bool showBottomDrawer = true;
    bool showViewportOverlay = true;
    bool showHelpModal = false;
    bool showUndoHistory = false;
    bool showClusterCullingStats = false;
    bool showQualitySettings = false;

    // Tree expanded/collapsed state restoration tracking (Outliner and Content Browser)
    bool m_outlinerStateNeedsApply = true;
    bool m_cbFolderStateNeedsApply = true;
    std::unordered_set<std::string> m_outlinerAppliedNodes;
    std::unordered_set<std::string> m_cbAppliedFolders;
    void ResetTreeStateApplication();

    // Graphics Quality & Anti-Aliasing (managed globally by Graphics Plugin)
    using AntiAliasingMethod = Eunoia::AntiAliasingMethod;
    using AntiAliasingInfo = Eunoia::AntiAliasingInfo;

    AntiAliasingMethod currentAntiAliasing = AntiAliasingMethod::FXAA;
    TextureQuality currentTextureQuality = TextureQuality::None;
    bool vsyncEnabled = false;
    void ApplyTextureQuality(TextureQuality quality);
    void RenderQualitySettings();
    static const std::vector<AntiAliasingInfo>& GetAntiAliasingMethods();
    static const char* GetAntiAliasingName(AntiAliasingMethod method);

    void RenderClusterCullingStats(Scene& scene);

    // Bottom drawer active tab: 0=Content Browser, 1=Output Log
    int bottomDrawerTab = 0;
    bool bottomDrawerOpen = true;

    // Project Management & Browser
    struct ProjectEntry {
        std::string name;
        std::string rootPath;
        std::string lastOpened;
    };

    bool showProjectBrowser = true;
    int projectBrowserTab = 0; // 0 = Recent Projects, 1 = New Project
    char newProjectNameBuf[64] = "MyProject";
    char newProjectPathBuf[260] = "C:\\Projects\\Eunoia-Engine\\Projects";
    char projectBrowserSearchBuf[64] = "";
    int selectedProjectIndex = 0;

    std::filesystem::path activeProjectRoot;
    std::string activeProjectName = "";
    std::vector<ProjectEntry> recentProjects;

    void LoadRecentProjects();
    void SaveRecentProjects();
    bool CreateNewProject(const std::string& parentDir, const std::string& projName, Scene& scene, OrbitCamera& camera);
    bool LoadProject(const std::filesystem::path& projRoot, Scene& scene, OrbitCamera& camera, bool isGameMode = false, const std::string& specificScenePath = "", const std::string& specificBinPath = "");
    static std::string ShowSelectFolderDialog(void* owner = nullptr, const std::string& title = "Select Project Location");
    static std::string ShowOpenProjectFileDialog();

    // Content Browser project root & navigation (Root is in project's folder inside Content)
    std::filesystem::path contentRootPath;
    std::filesystem::path currentContentPath;
    char contentBrowserSearch[64] = "";
    float contentThumbnailSize = 96.0f;
    std::string selectedContentItem = "";
    std::unordered_set<std::string> selectedContentItems;
    std::string lastSelectedContentItem = "";
    std::string popupTargetVirtualDir = "";

    // Folder & Material creation modal/state
    bool showNewFolderPopup = false;
    char newFolderNameBuf[64] = "NewFolder";

    bool showNewMaterialPopup = false;
    char newMaterialNameBuf[64] = "M_NewMaterial";

    // Item / Folder Renaming State & Methods
    bool showRenameAssetPopup = false;
    AssetID renameTargetAssetId;
    std::string renameTargetAssetName = "";
    std::string renameTargetAssetVirtualPath = "";
    bool showRenameFolderPopup = false;
    std::string renameTargetVirtualDir = "";
    char renameItemNameBuf[128] = "";
    bool renameFocusInput = false;

    void OpenRenameAssetModal(const AssetID& id, const std::string& currentName, const std::string& virtualPath);
    void OpenRenameFolderModal(const std::string& virtualDir);
    bool RenameContentAsset(const AssetID& id, const std::string& newName);
    bool RenameContentFolder(const std::string& srcVirtualDir, const std::string& newFolderName);

    // Material Editor Window & In-Editor Tab Workspace
    bool showMaterialEditor = false;
    bool materialWorkspaceActive = false; // Whether the material tab is currently active/focused in the workspace
    MaterialAsset activeMaterial;
    bool materialDirty = false;
    bool liveMaterialViewportSync = true;
    float matEditorLightAngle = 45.0f;
    int matEditorPreviewMesh = 0; // 0 = Cube (Default), 1 = Sphere, 2 = Cylinder, 3 = Plane, 4 = Torus, 5 = Capsule
    float matEditorRotationAngle = 0.0f;
    bool matEditorAutoRotate = true;
    uint64_t matPreviewTextureId = 0;
    std::string matEditorSurfaceMap = "";
    int matEditorLightPreset = 1; // 0 = Directional Key Light, 1 = Studio 3-Point Lights (Default), 2 = Warm Sunset, 3 = Cool Night / Moonlight

    // Gizmos configuration
    bool showGizmo = true;
    int currentGizmoOperation = 7; // ImGuizmo::TRANSLATE
    int currentGizmoMode = 1;      // ImGuizmo::WORLD
    bool gizmoUseCenter = false;   // false = Pivot (at object's pivot point, default), true = Center (mesh bounding-box center)
    bool useSnap = false;
    glm::vec3 snapTranslation{0.5f, 0.5f, 0.5f};
    float snapRotation = 15.0f; // degrees
    float snapScale = 0.25f;

    // UI layout dimensions (scaled for friendly visibility and comfort)
    float uiMargin = 0.0f;
    float uiGap = 0.0f;
    float topBarHeight = 36.0f;        // Sleek single-row header (dev.md h-9 = 36px)
    float tabBarHeight = 28.0f;        // Top Workspace / Document Tab Switcher Bar
    float leftSidebarWidth = 250.0f;
    float rightSidebarWidth = 320.0f;  // dev.md w-80 = 320px
    float outlinerHeightRatio = 0.50f; // dev.md h-1/2 = 50%
    float bottomBarHeight = 26.0f;     // Full-width horizontal bottom status bar
    float bottomDockHeight = 256.0f;   // dev.md h-64 = 256px
    float contentBrowserFolderTreeWidth = 256.0f; // dev.md w-64 = 256px
    float uiScale = 1.0f; // UI Scale factor (0.25f to 2.50f, 1.0f = 100%)
    float GetTopBarsTotalHeight() const { return (tabBarHeight * uiScale) + topBarHeight; }

    struct BottomProgressState {
        bool active = false;
        float progress = 0.0f;
        std::string title = "";
        std::string detail = "";
        float completionTimer = 0.0f;
        bool success = true;
    } m_bottomProgress;

    struct TabSaveProgressState {
        bool isSaving = false;
        float progress = 0.0f;
        std::string tabName = "";
        float fadeTimer = 0.0f;
    };
    TabSaveProgressState m_levelSaveState;
    TabSaveProgressState m_materialSaveState;
    TabSaveProgressState m_uiSaveState;
    std::atomic<bool> m_isAsyncLevelSaving{false};

    struct MaterialDoc {
        MaterialAsset material;
        bool isDirty = false;
        TabSaveProgressState saveState;
    };
    std::vector<MaterialDoc> openMaterialDocs;
    int activeMaterialDocIdx = -1;

    struct ViewportRect {
        float x, y, width, height;
    };

    ViewportRect GetViewportRect(float windowWidth, float windowHeight) const {
        if (isImmersiveMode || isGameOnlyWindow || isPlayModeActive) {
            return { 0.0f, 0.0f, windowWidth, windowHeight };
        }
        float topH = GetTopBarsTotalHeight();
        float bottomBarH = bottomBarHeight * uiScale;
        float curBottomH = showBottomDrawer ? (bottomDrawerOpen ? bottomDockHeight : 26.0f) : 0.0f;
        float rightW = (showOutliner || showDetails) ? rightSidebarWidth : 0.0f;

        float vpX = 0.0f; // Outliner is now docked above Details on the right side
        float vpY = topH;
        float vpW = windowWidth - rightW;
        float vpH = windowHeight - topH - curBottomH - bottomBarH;

        if (vpW < 10.0f) vpW = 10.0f;
        if (vpH < 10.0f) vpH = 10.0f;

        return { vpX, vpY, vpW, vpH };
    }

    struct CameraPipRect {
        bool active = false;
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        int selectedCameraId = -1;
    };

    CameraPipRect GetCameraPipRect(float windowWidth, float windowHeight, const Scene& scene) const {
        CameraPipRect rect;
        if (isGameView || scene.isPlayMode || isGameOnlyWindow || scene.selectedId == -1) return rect;

        const GameObject* selObj = scene.FindObject(scene.selectedId);
        if (!selObj || (!selObj->isCamera && selObj->type != PrimitiveType::Camera)) return rect;

        auto vpRect = GetViewportRect(windowWidth, windowHeight);
        float pipW = 320.0f;
        float pipH = 180.0f;
        float headerH = 28.0f;
        float margin = 14.0f;

        float winX = vpRect.x + vpRect.width - pipW - margin;
        float winY = vpRect.y + vpRect.height - (pipH + headerH) - margin;

        rect.active = true;
        rect.x = winX + 4.0f;
        rect.y = winY + headerH;
        rect.width = pipW - 8.0f;
        rect.height = pipH - 4.0f;
        rect.selectedCameraId = selObj->id;
        return rect;
    }

    struct MatPreviewPipRect {
        bool active = false;
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        int meshType = 0; // 0 = Cube (Default), 1 = Sphere, 2 = Cylinder, 3 = Plane, 4 = Torus, 5 = Capsule
        float rotationAngle = 0.0f;
        int lightPreset = 0;
        float lightAngle = 45.0f;
    };

    MatPreviewPipRect matPreviewPipRect;

    MatPreviewPipRect GetMatPreviewPipRect() const {
        return matPreviewPipRect;
    }

    // Viewport display mode
    int viewMode = 0; // 0=Lit, 1=Wireframe, 2=Unlit
    float cameraSpeed = 4.0f;
    float cameraSpeedOverlayTimer = 0.0f;
    void ShowCameraSpeedOverlay(float speed) { cameraSpeed = speed; cameraSpeedOverlayTimer = 1.0f; }
    float currentFps = 60.0f;
    bool lockAspectScale = false;

    // On-Demand Viewport Rendering (Power-saver: only renders when giving inputs or changing properties)
    bool realtimeViewport = true;
    int viewportRedrawCountdown = 15;
    void RequestViewportRedraw(int frames = 10) {
        if (frames > viewportRedrawCountdown) {
            viewportRedrawCountdown = frames;
        }
    }

    // Outliner search filter
    char outlinerFilter[64] = "";
    char consoleInput[256] = "";
    bool logShowInfo = true;
    bool logShowWarnings = true;
    bool logShowErrors = true;
    bool logAutoScroll = true;
    char logSearchBuf[128] = "";
    std::unordered_set<int> selectedActorIds;
    int lastSelectedActorId = -1;

    std::vector<EngineLogEntry> logs;

    // Game View (G key) & Immersive Viewport (F11 key)
    bool isGameView = false;
    bool prevShowGrid = true;
    bool prevShowGizmo = true;

    bool isImmersiveMode = false;
    bool prevShowOutliner = true;
    bool prevShowDetails = true;
    bool prevShowBottomDrawer = true;

    // Level File Management (In whole project "Level" is used instead of "Scene")
    std::string currentLevelFilePath = "";
    std::string lastOpenedLevelConfig = "";
    bool levelUnsaved = false;
    std::string& currentSceneFilePath = currentLevelFilePath;
    bool& sceneUnsaved = levelUnsaved;

    void ToggleGameView(Scene& scene);
    void ToggleImmersiveMode();

    // Level Save/Load
    bool SaveLevel(Scene& level);                              // Save to current file (or Save As if new)
    bool SaveLevelAs(Scene& level);                            // Save to new file via dialog
    bool OpenLevel(Scene& level);                              // Open level via file dialog
    bool OpenLevelFromPath(Scene& level, const std::string& filePath, bool isGameMode = false, const std::string& binPath = ""); // Open level directly from disk path
    void RenderViewportLoadingOverlay();
    bool IsProgressiveLoadingActive() const { return m_progressiveLoader.active; }
    SceneSerializer::ProgressiveSceneLoader m_progressiveLoader;

    // Custom In-Engine Save Level Modal (anchored strictly to Content root)
    bool showSaveLevelModal = false;
    std::filesystem::path saveLevelCurrentFolder;
    char saveLevelNameBuf[128] = "NewLevel";
    char saveLevelNewFolderBuf[128] = "";
    bool showSaveLevelNewFolderInput = false;
    std::string saveLevelErrorMsg = "";
    Scene* saveLevelTargetScene = nullptr;

    void OpenSaveLevelModal(Scene& scene);
    void RenderSaveLevelModal();

    struct SaveIndicatorState {
        bool active = false;
        bool isSaving = false;
        bool success = true;
        std::string filename;
        float displayTimer = 0.0f;
    };
    SaveIndicatorState m_saveIndicator;
    void RenderSaveIndicator();

    bool SaveScene(Scene& scene) { return SaveLevel(scene); }
    bool SaveSceneAs(Scene& scene) { return SaveLevelAs(scene); }
    bool OpenScene(Scene& scene) { return OpenLevel(scene); }

    static std::string ShowSaveFileDialog();                   // Win32 save level dialog
    static std::string ShowOpenFileDialog();                   // Win32 open level dialog
    static std::string ShowOpenMeshDialog();                   // Win32 open mesh dialog (.obj, .gltf, .glb)
    static std::string ShowSaveMeshLocationDialog(const std::string& defaultFilename, const std::string& initialDir);

    // Import Mesh with Save Location Prompt
    void ImportMeshWithSavePrompt(Scene& level);

    // OS File Drag & Drop (models from system to content browser)
    void HandleFileDrop(const char** paths, int count);

    // Viewport Drag & Drop Target (3D objects from content browser to viewport)
    void RenderViewportDropTarget(Scene& level, OrbitCamera& camera);
    glm::vec3 CalculateDropSpawnPosition(OrbitCamera& camera, float mouseX, float mouseY, float screenW, float screenH);

    EngineUI();
    ~EngineUI();

    // Initialize EngineUI with a pre-selected project path (called after standalone project browser)
    void InitWithProject(const std::filesystem::path& projectPath, Scene& scene, OrbitCamera& camera, bool isGameMode = false, const std::string& specificScenePath = "", const std::string& specificBinPath = "");

    // Dynamic layout splitters and Configs.Editor.econfigs management
    void LoadEditorConfig();
    void SaveEditorConfig();
    void RenderLayoutSplitters();
    void RenderBottomProgressBar();
    bool RenderTabWithSaveProgress(const char* label, bool isActive, const TabSaveProgressState& saveState, float btnHeight);

    // Standalone Project Browser window (runs before editor, returns selected project path or empty)
    static std::filesystem::path RunStandaloneProjectBrowser();

    void SetWindow(GLFWwindow* window) { m_window = window; }
    GLFWwindow* GetWindow() const { return m_window; }

    void AddLog(const std::string& category, const std::string& message, int level = 0);
    void SetupTheme();
    void ApplyUIScale(float scale);
    void Render(Scene& scene, OrbitCamera& camera, float fps, float frameTimeMs, uint32_t vertexCount, uint32_t indexCount, bool& outShouldExit);
    void RenderGizmo(Scene& scene, OrbitCamera& camera, float viewportWidth, float viewportHeight);

    // Reference Viewer (dev.md Section 33)
    bool showReferenceViewer = false;
    AssetID refViewerSelectedAsset;
    std::string currentVirtualDir = "/Game";

    void OpenReferenceViewer(const AssetID& id = AssetID::Null());

    // Standalone Game Build Pipeline
    bool showGameBuildModal = false;
    bool isGameBuilding = false;
    float gameBuildProgress = 0.0f;
    std::string gameBuildStage = "Ready";
    std::string gameBuildLog = "";
    bool gameBuildSuccess = false;
    std::filesystem::path gameBuildOutputDir;
    std::string gameBuildExePath;
    std::thread gameBuildThread;
    std::mutex gameBuildMutex;

    void OpenGameBuildModal(Scene& scene);
    void RenderGameBuildModal(Scene& scene);
    void StartGameBuildAsync(Scene& scene);

    // Material file I/O & Textures
    static bool LoadMaterialFile(const std::string& path, MaterialAsset& outMat);
    static bool SaveMaterialFile(const std::string& path, const MaterialAsset& mat);
    void OpenMaterialEditor(const std::string& path);
    void CloseMaterialEditor(int docIdx);
    void SwitchToMaterialEditor(int docIdx);
    void PropagateMaterialChangesToScene(Scene& scene, const MaterialAsset& mat);
    void RenderMaterialWorkspace(Scene& scene, const glm::vec2& wsPos, const glm::vec2& wsSize);
    void ApplyMaterialToActorAndChildren(Scene& scene, GameObject* rootObj, const MaterialAsset& ma);
    std::vector<TextureAssetEntry> ScanProjectTextures();
    bool DrawTextureSlot(const char* label, std::string& textureSlotValue, AssetID& textureSlotAssetId, const std::vector<TextureAssetEntry>& availableTextures);

    // Skybox Asset File I/O & World Skybox Settings
    bool showSkyboxEditor = false;
    bool showSkyboxSettingsModal = false;
    bool showPostProcessingModal = false;
    bool showAmbientOcclusionModal = false;
    bool showVolumetricFogModal = false;
    SkyboxAsset activeSkybox;
    bool skyboxDirty = false;

    static bool LoadSkyboxFile(const std::string& path, SkyboxAsset& outSkybox);
    static bool SaveSkyboxFile(const std::string& path, const SkyboxAsset& skybox);
    void OpenSkyboxEditor(const std::string& path);
    void ApplySkyboxToScene(Scene& scene, const SkyboxAsset& skybox);

    // Water Asset File I/O & Water Settings
    bool showWaterEditor = false;
    bool showNewWaterPopup = false;
    char newWaterNameBuf[64] = "WB_New";
    WaterAsset activeWater;
    bool waterDirty = false;

    static bool LoadWaterFile(const std::string& path, WaterAsset& out);
    static bool SaveWaterFile(const std::string& path, const WaterAsset& w);
    void OpenWaterEditor(const std::string& path);
    void ApplyWaterToGameObject(Scene& scene, GameObject* obj, const WaterAsset& wa);

    // UI Editor & Asset File I/O
    bool showNewUIPopup = false;
    char newUINameBuf[64] = "UI_NewInterface";
    void OpenUIEditor(const std::string& path);

    // Node Behaviour Visual Scripting Editor
    bool showNewNodeBehaviourPopup = false;
    char newNodeBehaviourNameBuf[64] = "NB_NewBehaviour";
    void OpenNodeBehaviourEditor(const std::string& path);

    // Animator Graph Visual State Machine Editor & Skeletal Animation
    bool showNewAnimatorGraphPopup = false;
    char newAnimatorGraphNameBuf[64] = "AG_NewAnimator";
    void OpenAnimatorGraphEditor(const std::string& path);

    // Batch Animation Import Skeleton Selector Dialog
    bool showBatchSkeletonImportPopup = false;
    std::vector<std::string> pendingBatchImportAnimFiles;
    std::string selectedBatchImportSkeletonMesh = "";

    // Behaviours & Play Mode (dev.md)
    bool isPlayModeActive = false;
    bool showAddBehaviourPopup = false;
    char behaviourSearchBuf[64] = "";
    bool showNewBehaviourPopup = false;
    char newBehaviourNameBuf[64] = "NewBehaviour";
    bool IsBehaviourNameAlreadyExists(const std::string& name) const;

    // In-Editor Code Editor (dev.md & User Request)
    bool showCodeEditor = false;
    bool codeEditorDirty = false;
    std::string activeCodeEditorPath = "";
    std::string activeCodeEditorFilename = "";
    std::string activeCodeEditorContent = "";
    void SetupProjectDependencies(const std::filesystem::path& projectRoot);
    void OpenScriptInCodeEditor(const std::string& path);
    void RenderCodeEditor();
    void RenderBehavioursSection(Scene& scene, GameObject* obj);
    void EnterPlayMode(Scene& scene, OrbitCamera* camera = nullptr);
    void ExitPlayMode(Scene& scene, OrbitCamera* camera = nullptr);
    void LaunchGameSeparateWindow(Scene& scene, OrbitCamera* cameraPtr = nullptr);
    bool IsExternalGameRunning();
    void StopExternalGame();
    void RenderScreenPrintOverlay(float startX, float startY);

    bool isGameOnlyWindow = false;

    // Deferred Scene Loading (opens editor window with no viewport, then loads scene once open)
    bool isInitialSceneLoaded = false;
    bool pendingInitialSceneLoad = false;
    std::string pendingScenePath = "";
    std::string pendingBinPath = "";
    int initialLoadFrameCountdown = 2;
    bool LoadPendingInitialScene(Scene& scene, OrbitCamera& camera, bool isGameMode);

    // Behaviour Script Watcher & Compilation (User Request: ask compile or later)
    bool showScriptCompileModal = false;
    std::atomic<bool> isCompilingScripts{false};
    float scriptWatchTimer = 0.0f;
    std::unordered_map<std::string, std::filesystem::file_time_type> scriptFileTimestamps;
    std::vector<std::string> pendingChangedScripts;
    std::mutex asyncLogMutex;
    std::vector<EngineLogEntry> pendingAsyncLogs;

    void CheckForScriptChanges(Scene* scene = nullptr);
    void TriggerCompileScripts(Scene* scene = nullptr);
    void RenderScriptCompileModal(Scene& scene);

    // 3D Model Real-Time Detection & Import Wizard
    bool showModelImportModal = false;
    float modelWatchTimer = 0.0f;
    std::unordered_set<std::string> knownModelFiles;
    std::vector<std::string> pendingModelImportQueue;
    ModelImportState currentModelImport;

    void CheckForModelChanges();
    void QueueModelImport(const std::string& filePath);
    bool OpenModelImportView(const std::string& filePath);
    void RenderModelImportModal(Scene& scene, OrbitCamera& camera);
    void ExecuteModelImport(Scene& scene, bool spawnInLevel, bool advanceQueue = true);
    void AdvanceModelImportQueue();

    uint64_t lightIconGpuHandle = 0;
    uint64_t directionalLightIconGpuHandle = 0;
    uint64_t pointLightIconGpuHandle = 0;
    uint64_t spotLightIconGpuHandle = 0;
    uint64_t areaLightIconGpuHandle = 0;
    uint64_t skyLightIconGpuHandle = 0;
    uint64_t cameraIconGpuHandle = 0;
    uint64_t engineIconGpuHandle = 0;
    uint64_t outlinerEyeIconGpuHandle = 0;

    // Content Browser Icons (from Resources/Icons/Content Browser/*.png)
    uint64_t cbFolderIconGpuHandle = 0;
    uint64_t cb3dIconGpuHandle = 0;
    uint64_t cbMaterialIconGpuHandle = 0;
    uint64_t cbSkyboxIconGpuHandle = 0;
    uint64_t cbTextureIconGpuHandle = 0;
    uint64_t cbLevelIconGpuHandle = 0;
    uint64_t cbBehaviourIconGpuHandle = 0;

    uint64_t GetLightIconGpuHandle(LightType type) const {
        switch (type) {
            case LightType::Directional: return directionalLightIconGpuHandle ? directionalLightIconGpuHandle : lightIconGpuHandle;
            case LightType::Point:       return pointLightIconGpuHandle ? pointLightIconGpuHandle : lightIconGpuHandle;
            case LightType::Spot:        return spotLightIconGpuHandle ? spotLightIconGpuHandle : lightIconGpuHandle;
            case LightType::Area:        return areaLightIconGpuHandle ? areaLightIconGpuHandle : lightIconGpuHandle;
            case LightType::Sky:         return skyLightIconGpuHandle ? skyLightIconGpuHandle : lightIconGpuHandle;
            default:                     return lightIconGpuHandle;
        }
    }

    uint64_t GetContentBrowserAssetIcon(AssetType type) const {
        switch (type) {
            case AssetType::Mesh:            return cb3dIconGpuHandle;
            case AssetType::Material:        return cbMaterialIconGpuHandle;
            case AssetType::Skybox:          return cbSkyboxIconGpuHandle;
            case AssetType::Texture:         return cbTextureIconGpuHandle;
            case AssetType::Level:           return cbLevelIconGpuHandle;
            case AssetType::Behaviour:
            case AssetType::Script:          return cbBehaviourIconGpuHandle;
            case AssetType::UserInterface:   return cbMaterialIconGpuHandle ? cbMaterialIconGpuHandle : cbBehaviourIconGpuHandle;
            default:                         return 0;
        }
    }

    OrbitCamera* currentCamera = nullptr;
    bool hasSavedPlayModeCamera = false;
    glm::vec3 savedCameraTarget{0.0f, 0.4f, 0.0f};
    float savedCameraDistance = 6.5f;
    float savedCameraYaw = 45.0f;
    float savedCameraPitch = 25.0f;
    float savedCameraFov = 45.0f;
    bool savedCameraIsOrtho = false;
    float savedCameraOrthoSize = 5.0f;
    float savedCameraNearPlane = 0.1f;
    float savedCameraFarPlane = 500.0f;

    void RenderCameraPreviewOverlay(Scene& scene, OrbitCamera& camera);

    // ========================================================================
    // Futuristic Loading Progress System
    // ========================================================================
    struct LoadingProgressState {
        bool active = false;
        std::string title = "Loading...";
        std::string currentDetail = "Please wait...";
        std::string subDetail = "";
        float progress = 0.0f; // 0.0f to 1.0f (or < 0.0f for indeterminate pulse)
        std::vector<std::string> recentHistory;
        double startTime = 0.0;
        double finishTime = 0.0;
        bool completed = false;
        bool hasError = false;
        float autoCloseDelay = 1.2f; // Seconds to auto-dismiss on completion
        float completionTimer = 0.0f;
        bool isModal = true;
    };

    static LoadingProgressState s_loadingState;

    static void StartLoadingTask(const std::string& title, const std::string& initialDetail, float initialProgress = 0.0f, bool modal = true);
    static void UpdateLoadingTask(float progress, const std::string& currentDetail, const std::string& subDetail = "");
    static void FinishLoadingTask(const std::string& completionMessage = "Operation complete.", bool success = true);
    static void CancelLoadingTask();
    static bool IsLoadingTaskActive();

private:
    void RenderTopMenuBar(Scene& scene, OrbitCamera& camera, bool& outShouldExit);
    void RenderCustomTitleBar(Scene& scene, bool& outShouldExit);
    void RenderMainMenuBar(Scene& scene, OrbitCamera& camera, bool& outShouldExit);
    void RenderWorkspaceTabBar(Scene& scene, bool& outShouldExit);
    void RenderViewportOverlay(Scene& scene, OrbitCamera& camera, float fps, float frameTimeMs);
    void RenderOutliner(Scene& scene);
    void DrawOutlinerNode(GameObject& obj, Scene& scene, std::unordered_set<int>& visitedIds, int depth = 0);
    void RenderDetails(Scene& scene);
    void RenderContentBrowser(Scene& scene);
    void RenderMaterialEditor(Scene& scene);
    void RenderSkyboxEditor(Scene& scene);
    void RenderWaterEditor(Scene& scene);
    void RenderWorldSkyboxSettingsModal(Scene& scene);
    void RenderPostProcessingModal(Scene& scene);
    void RenderAmbientOcclusionModal(Scene& scene);
    void RenderVolumetricFogModal(Scene& scene);
    void RenderReferenceViewer(Scene& scene);
    void RenderHelpModal();
    void RenderLoadingModal();
    void RenderProjectBrowser(Scene& scene, OrbitCamera& camera);

    // Custom UE5-style UI widgets
    bool DrawTransformPill(const char* label, float& value, const glm::vec4& color, float resetValue = 0.0f, float speed = 0.05f);
    bool DrawVec3Row(const char* rowLabel, glm::vec3& values, float resetValue = 0.0f, float speed = 0.05f, bool isScale = false, bool* lockAspect = nullptr);

    int m_pendingReparentChild = -1;
    int m_pendingReparentParent = -1;

    GLFWwindow* m_window = nullptr;
};
