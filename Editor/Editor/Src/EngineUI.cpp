#include <Editor/EngineUI.h>
#include <Editor/UndoManager.h>
#include <Editor/EditorGizmoSystem.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cctype>
#include <windows.h>
#include <commdlg.h>
#ifdef GetClassName
#undef GetClassName
#endif
#include <map>
#include <EngineAssets/TextureManager.h>
#include <EngineAssets/DirectXTex.h>
#include <EngineAssets/MeshImporter.h>
#include <EngineCore/EngineLogger.h>
#include <EngineScene/BehaviourRegistry.h>
#include <EnginePlatform/InputSystem.h>
#include <EngineScene/ScreenPrint.h>
#include <shellapi.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>
#include <Water/WaterSystem.h>
#include <UISystem/UIEditorWindow.h>
#include <UISystem/UISystem.h>
#include <Collisions/CollisionTypes.h>
#include <Collisions/CollisionSystem.h>
#include <PostProcess/PostProcessSystem.h>
#include <TextureStreaming/TextureStreamingSystem.h>
#include <Node-Behaviour/NodeBehaviourManager.h>
#include <Node-Behaviour/NodeBehaviourInstance.h>
#include <SFLOD/SFLODSystem.h>
#include <EngineAssets/AnimatorGraphEditor.h>
#include <Animation-Blending/AnimationBlendManager.h>
#include <EngineAssets/SkeletalAnimation.h>
#include <Level-Animation/LevelAnimation.h>
#include <Level-Animation/LevelAnimationEditor.h>
#include <Level-Animation/LevelAnimationManager.h>
#include <Behaviour/BehaviourManager.h>
#include <Behaviour/BehaviourAsset.h>

inline bool HasSceneStateChanged(const Scene& a, const Scene& b) {
    if (a.objects.size() != b.objects.size()) return true;
    if (a.pointLights.size() != b.pointLights.size()) return true;
    if (a.lightDirection != b.lightDirection || a.lightColor != b.lightColor || a.lightIntensity != b.lightIntensity) return true;
    if (a.ambientIntensity != b.ambientIntensity || a.enableShadows != b.enableShadows || a.shadowStrength != b.shadowStrength || a.shadowBias != b.shadowBias) return true;
    if (a.enableAO != b.enableAO || a.aoType != b.aoType || a.aoPower != b.aoPower || a.aoRange != b.aoRange || a.aoSampleCount != b.aoSampleCount || a.aoBias != b.aoBias || a.aoBlur != b.aoBlur) return true;
    for (size_t i = 0; i < a.objects.size(); ++i) {
        const auto& o1 = a.objects[i];
        const auto& o2 = b.objects[i];
        if (o1.id != o2.id || o1.position != o2.position || o1.rotation != o2.rotation || o1.scale != o2.scale) return true;
        if (o1.color != o2.color || o1.metallic != o2.metallic || o1.roughness != o2.roughness) return true;
        if (o1.light.intensity != o2.light.intensity || o1.light.color != o2.light.color || o1.light.range != o2.light.range || o1.light.castShadows != o2.light.castShadows) return true;
        if (o1.materialName != o2.materialName || o1.name != o2.name) return true;
        if (o1.behaviours.size() != o2.behaviours.size()) return true;
        if (o1.isTerrain != o2.isTerrain) return true;
        if (o1.isTerrain && o2.isTerrain) {
            if (o1.terrain.width != o2.terrain.width ||
                o1.terrain.length != o2.terrain.length ||
                o1.terrain.resolution != o2.terrain.resolution ||
                o1.terrain.heightmap != o2.terrain.heightmap ||
                o1.terrain.quadMaterialIndices != o2.terrain.quadMaterialIndices ||
                o1.terrain.materialLayers.size() != o2.terrain.materialLayers.size()) {
                return true;
            }
        }
    }
    for (size_t i = 0; i < a.pointLights.size(); ++i) {
        const auto& p1 = a.pointLights[i];
        const auto& p2 = b.pointLights[i];
        if (p1.position != p2.position || p1.color != p2.color || p1.intensity != p2.intensity || p1.range != p2.range || p1.castShadows != p2.castShadows || p1.enabled != p2.enabled) return true;
    }
    return false;
}

static EngineUI* g_pEngineUI = nullptr;
extern bool g_pendingPickClick;
static bool s_justExitedPlayMode = false;

struct PromotedTabWindow {
    std::string name;
    bool active = true;
};
static std::vector<PromotedTabWindow> s_promotedTabs;
static int s_activePromotedTabIndex = -1;

static bool IsWindowPromotedTab(const std::string& name) {
    for (const auto& tab : s_promotedTabs) {
        if (tab.name == name) return true;
    }
    return false;
}

static void PromoteWindowToWorkspaceTab(const std::string& name) {
    for (size_t i = 0; i < s_promotedTabs.size(); ++i) {
        if (s_promotedTabs[i].name == name) {
            s_activePromotedTabIndex = (int)i;
            return;
        }
    }
    PromotedTabWindow tab;
    tab.name = name;
    tab.active = true;
    s_promotedTabs.push_back(tab);
    s_activePromotedTabIndex = (int)s_promotedTabs.size() - 1;
}

static void ClosePromotedWorkspaceTab(int idx) {
    if (idx >= 0 && idx < (int)s_promotedTabs.size()) {
        s_promotedTabs.erase(s_promotedTabs.begin() + idx);
        if (s_activePromotedTabIndex >= (int)s_promotedTabs.size()) {
            s_activePromotedTabIndex = (int)s_promotedTabs.size() - 1;
        }
    }
}

void AddEngineLog(const std::string& category, const std::string& message, int level) {
    if (g_pEngineUI) {
        g_pEngineUI->AddLog(category, message, level);
    }
}

EngineUI::LoadingProgressState EngineUI::s_loadingState;

void EngineUI::StartLoadingTask(const std::string& title, const std::string& initialDetail, float initialProgress, bool modal) {
    s_loadingState.active = true;
    s_loadingState.title = title;
    s_loadingState.currentDetail = initialDetail;
    s_loadingState.subDetail = "";
    s_loadingState.progress = initialProgress;
    s_loadingState.recentHistory.clear();
    if (!initialDetail.empty()) {
        s_loadingState.recentHistory.push_back(initialDetail);
    }
    s_loadingState.startTime = ImGui::GetTime();
    s_loadingState.finishTime = 0.0;
    s_loadingState.completed = false;
    s_loadingState.hasError = false;
    s_loadingState.autoCloseDelay = 1.0f;
    s_loadingState.completionTimer = 0.0f;
    s_loadingState.isModal = modal;
}

void EngineUI::UpdateLoadingTask(float progress, const std::string& currentDetail, const std::string& subDetail) {
    if (!s_loadingState.active) return;
    s_loadingState.progress = progress;
    if (!currentDetail.empty() && currentDetail != s_loadingState.currentDetail) {
        if (s_loadingState.recentHistory.empty() || s_loadingState.recentHistory.back() != s_loadingState.currentDetail) {
            s_loadingState.recentHistory.push_back(s_loadingState.currentDetail);
            if (s_loadingState.recentHistory.size() > 5) {
                s_loadingState.recentHistory.erase(s_loadingState.recentHistory.begin());
            }
        }
        s_loadingState.currentDetail = currentDetail;
    }
    if (!subDetail.empty()) {
        s_loadingState.subDetail = subDetail;
    }
}

void EngineUI::FinishLoadingTask(const std::string& completionMessage, bool success) {
    if (!s_loadingState.active) return;
    s_loadingState.progress = 1.0f;
    s_loadingState.completed = true;
    s_loadingState.hasError = !success;
    s_loadingState.currentDetail = completionMessage;
    s_loadingState.finishTime = ImGui::GetTime();
    s_loadingState.completionTimer = s_loadingState.autoCloseDelay;
    if (!completionMessage.empty()) {
        s_loadingState.recentHistory.push_back(completionMessage);
    }
}

void EngineUI::CancelLoadingTask() {
    s_loadingState.active = false;
    s_loadingState.completed = false;
}

bool EngineUI::IsLoadingTaskActive() {
    return s_loadingState.active;
}

struct BackgroundSyncState {
    std::atomic<bool> isRunning{false};
    std::atomic<float> progress{0.0f};
    std::string currentDetail = "";
    float completionTimer = 0.0f;
    std::mutex mutex;
};
static BackgroundSyncState s_bgSync;

static void TriggerBackgroundRegistrySync(const std::filesystem::path& path) {
    if (path.empty()) return;
    if (s_bgSync.isRunning.load()) return;
    s_bgSync.isRunning = true;
    s_bgSync.progress = 0.05f;
    {
        std::lock_guard<std::mutex> lock(s_bgSync.mutex);
        s_bgSync.currentDetail = "Scanning content...";
    }
    std::thread([path]() {
        AssetRegistry::Get().ScanAndSync(path, [](float p, const std::string& step, const std::string& detail) {
            s_bgSync.progress = p;
            std::lock_guard<std::mutex> lock(s_bgSync.mutex);
            s_bgSync.currentDetail = detail.empty() ? step : detail;
        });
        s_bgSync.progress = 1.0f;
        {
            std::lock_guard<std::mutex> lock(s_bgSync.mutex);
            s_bgSync.currentDetail = "Synced (" + std::to_string(AssetRegistry::Get().GetAssetCount()) + " assets)";
        }
        s_bgSync.completionTimer = 2.5f;
        s_bgSync.isRunning = false;
    }).detach();
}

static void SyncRegistryWithUIProgress(const std::filesystem::path& path) {
    TriggerBackgroundRegistrySync(path);
}

static bool MoveAssetToDirectory(const AssetID& id, const std::string& targetVirtualDir, const std::filesystem::path& contentRoot) {
    const AssetMetadata* meta = AssetRegistry::Get().FindById(id);
    if (!meta) return false;
    std::string currentDir = AssetPath::GetDirectory(meta->virtualPath);
    if (currentDir == targetVirtualDir) return false;

    std::filesystem::path srcDisk = contentRoot / meta->sourcePath;
    std::string diskSub = targetVirtualDir;
    if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
    if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
    std::filesystem::path targetDiskDir = diskSub.empty() ? contentRoot : (contentRoot / diskSub);

    std::error_code ec;
    std::filesystem::create_directories(targetDiskDir, ec);

    std::filesystem::path dstDisk = targetDiskDir / srcDisk.filename();
    if (std::filesystem::exists(srcDisk, ec)) {
        std::filesystem::rename(srcDisk, dstDisk, ec);
        if (ec) {
            ec.clear();
            std::filesystem::copy_file(srcDisk, dstDisk, std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) std::filesystem::remove(srcDisk, ec);
        }
    }

    std::filesystem::path srcMeta = srcDisk.string() + ".assetmeta";
    std::filesystem::path dstMeta = dstDisk.string() + ".assetmeta";
    if (std::filesystem::exists(srcMeta, ec)) {
        ec.clear();
        std::filesystem::rename(srcMeta, dstMeta, ec);
    }

    AssetRegistry::Get().MoveAsset(id, targetVirtualDir);
    std::filesystem::path newRel = std::filesystem::relative(dstDisk, contentRoot, ec);
    if (!ec) {
        AssetMetadata updated = *meta;
        updated.sourcePath = newRel.generic_string();
        updated.virtualPath = AssetPath::Combine(targetVirtualDir, updated.objectName);
        AssetRegistry::Get().RegisterAsset(updated);
    }
    return true;
}

static bool MoveFolderToDirectory(const std::string& srcVirtualDir, const std::string& targetVirtualDir, const std::filesystem::path& contentRoot) {
    if (srcVirtualDir == "/Game" || srcVirtualDir.empty() || targetVirtualDir.empty()) return false;
    if (srcVirtualDir == targetVirtualDir) return false;
    if (targetVirtualDir.rfind(srcVirtualDir + "/", 0) == 0) return false;

    std::string folderName = AssetPath::GetObjectName(srcVirtualDir);
    if (folderName.empty()) return false;

    std::string srcSub = srcVirtualDir;
    if (srcSub.rfind("/Game", 0) == 0) srcSub = srcSub.substr(5);
    if (!srcSub.empty() && srcSub[0] == '/') srcSub.erase(0, 1);
    std::filesystem::path srcDiskDir = srcSub.empty() ? contentRoot : (contentRoot / srcSub);

    std::string dstSub = targetVirtualDir;
    if (dstSub.rfind("/Game", 0) == 0) dstSub = dstSub.substr(5);
    if (!dstSub.empty() && dstSub[0] == '/') dstSub.erase(0, 1);
    std::filesystem::path targetParentDisk = dstSub.empty() ? contentRoot : (contentRoot / dstSub);
    std::filesystem::path dstDiskDir = targetParentDisk / folderName;

    std::error_code ec;
    if (!std::filesystem::exists(srcDiskDir, ec)) return false;
    std::filesystem::create_directories(targetParentDisk, ec);

    std::filesystem::rename(srcDiskDir, dstDiskDir, ec);
    if (ec) {
        ec.clear();
        std::filesystem::copy(srcDiskDir, dstDiskDir, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) std::filesystem::remove_all(srcDiskDir, ec);
        else return false;
    }

    TriggerBackgroundRegistrySync(contentRoot);
    return true;
}

void EngineUI::OpenRenameAssetModal(const AssetID& id, const std::string& currentName, const std::string& virtualPath) {
    renameTargetAssetId = id;
    renameTargetAssetName = currentName;
    renameTargetAssetVirtualPath = virtualPath;
    snprintf(renameItemNameBuf, sizeof(renameItemNameBuf), "%s", currentName.c_str());
    showRenameAssetPopup = true;
    renameFocusInput = true;
}

void EngineUI::OpenRenameFolderModal(const std::string& virtualDir) {
    if (virtualDir == "/Game" || virtualDir.empty()) return;
    renameTargetVirtualDir = virtualDir;
    std::string dirName = AssetPath::GetObjectName(virtualDir);
    snprintf(renameItemNameBuf, sizeof(renameItemNameBuf), "%s", dirName.c_str());
    showRenameFolderPopup = true;
    renameFocusInput = true;
}

bool EngineUI::RenameContentAsset(const AssetID& id, const std::string& newName) {
    std::string cleanNewName = newName;
    while (!cleanNewName.empty() && isspace((unsigned char)cleanNewName.front())) cleanNewName.erase(cleanNewName.begin());
    while (!cleanNewName.empty() && isspace((unsigned char)cleanNewName.back())) cleanNewName.pop_back();
    if (cleanNewName.empty()) return false;

    const AssetMetadata* meta = AssetRegistry::Get().FindById(id);
    if (!meta) return false;
    if (meta->objectName == cleanNewName) return true;

    std::filesystem::path srcDisk = contentRootPath / meta->sourcePath;
    std::string ext = srcDisk.extension().string();

    // Strip trailing extension if user typed it
    if (cleanNewName.size() > ext.size() &&
        cleanNewName.compare(cleanNewName.size() - ext.size(), ext.size(), ext) == 0) {
        cleanNewName = cleanNewName.substr(0, cleanNewName.size() - ext.size());
    }

    std::string newFileName = cleanNewName + ext;
    std::filesystem::path dstDisk = srcDisk.parent_path() / newFileName;

    std::error_code ec;
    if (std::filesystem::exists(dstDisk, ec)) {
        AddLog("LogContent", "Cannot rename asset: '" + newFileName + "' already exists!", 3);
        return false;
    }

    if (std::filesystem::exists(srcDisk, ec)) {
        std::filesystem::rename(srcDisk, dstDisk, ec);
        if (ec) {
            ec.clear();
            std::filesystem::copy_file(srcDisk, dstDisk, std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) std::filesystem::remove(srcDisk, ec);
            else {
                AddLog("LogContent", "Failed to rename file on disk: " + srcDisk.string(), 3);
                return false;
            }
        }
    }

    // Rename sidecar files (.assetmeta, .meta)
    std::vector<std::string> sidecarExts = { ".assetmeta", ".meta" };
    for (const auto& scExt : sidecarExts) {
        std::filesystem::path srcMeta = srcDisk.string() + scExt;
        std::filesystem::path dstMeta = dstDisk.string() + scExt;
        if (std::filesystem::exists(srcMeta, ec)) {
            ec.clear();
            std::filesystem::rename(srcMeta, dstMeta, ec);
        }
    }

    std::string oldVirtual = meta->virtualPath;
    std::string parentVirtual = AssetPath::GetDirectory(oldVirtual);
    std::string newVirtual = AssetPath::Combine(parentVirtual, cleanNewName);

    // Update active editors if currently editing this file
    if (activeMaterial.filePath == srcDisk.string()) {
        activeMaterial.filePath = dstDisk.string();
        activeMaterial.name = cleanNewName;
        activeMaterial.virtualPath = newVirtual;
    }
    if (activeSkybox.filePath == srcDisk.string()) {
        activeSkybox.filePath = dstDisk.string();
        activeSkybox.name = cleanNewName;
        activeSkybox.virtualPath = newVirtual;
    }
    if (activeWater.filePath == srcDisk.string()) {
        activeWater.filePath = dstDisk.string();
        activeWater.name = cleanNewName;
        activeWater.virtualPath = newVirtual;
    }

    // Update asset internal metadata files
    if (meta->type == AssetType::Material) {
        MaterialAsset ma;
        if (LoadMaterialFile(dstDisk.string(), ma)) {
            ma.name = cleanNewName;
            ma.virtualPath = newVirtual;
            ma.filePath = dstDisk.string();
            SaveMaterialFile(dstDisk.string(), ma);
        }
    } else if (meta->type == AssetType::Skybox) {
        SkyboxAsset sky;
        if (LoadSkyboxFile(dstDisk.string(), sky)) {
            sky.name = cleanNewName;
            sky.virtualPath = newVirtual;
            sky.filePath = dstDisk.string();
            SaveSkyboxFile(dstDisk.string(), sky);
        }
    } else if (meta->type == AssetType::Water) {
        WaterAsset water;
        if (LoadWaterFile(dstDisk.string(), water)) {
            water.name = cleanNewName;
            water.virtualPath = newVirtual;
            water.filePath = dstDisk.string();
            SaveWaterFile(dstDisk.string(), water);
        }
    } else if (meta->type == AssetType::Behaviour || meta->type == AssetType::Script) {
        std::string scriptExt = dstDisk.extension().string();
        std::transform(scriptExt.begin(), scriptExt.end(), scriptExt.begin(), ::tolower);
        if (scriptExt == ".cpp") {
            BehaviourRegistry::Get().RegisterScriptFile(cleanNewName, dstDisk.string());
        }
    }

    // Update Asset Registry
    std::filesystem::path newRel = std::filesystem::relative(dstDisk, contentRootPath, ec);
    AssetRegistry::Get().RenameAsset(id, cleanNewName);
    AssetMetadata* updated = AssetRegistry::Get().FindById(id);
    if (updated) {
        if (!ec) updated->sourcePath = newRel.generic_string();
        updated->virtualPath = newVirtual;
        updated->objectName = cleanNewName;
    }

    // Update selection states
    if (selectedContentItem == oldVirtual) {
        selectedContentItem = newVirtual;
    }
    if (selectedContentItems.count("ASSET:" + oldVirtual)) {
        selectedContentItems.erase("ASSET:" + oldVirtual);
        selectedContentItems.insert("ASSET:" + newVirtual);
    }
    if (lastSelectedContentItem == "ASSET:" + oldVirtual) {
        lastSelectedContentItem = "ASSET:" + newVirtual;
    }

    TriggerBackgroundRegistrySync(contentRootPath);
    AddLog("LogContent", "Renamed asset from '" + meta->objectName + "' to '" + cleanNewName + "'", 2);
    return true;
}

bool EngineUI::RenameContentFolder(const std::string& srcVirtualDir, const std::string& newFolderName) {
    std::string cleanNewName = newFolderName;
    while (!cleanNewName.empty() && isspace((unsigned char)cleanNewName.front())) cleanNewName.erase(cleanNewName.begin());
    while (!cleanNewName.empty() && isspace((unsigned char)cleanNewName.back())) cleanNewName.pop_back();

    if (srcVirtualDir == "/Game" || srcVirtualDir.empty() || cleanNewName.empty()) return false;
    std::string oldFolderName = AssetPath::GetObjectName(srcVirtualDir);
    if (oldFolderName == cleanNewName) return true;

    std::string srcSub = srcVirtualDir;
    if (srcSub.rfind("/Game", 0) == 0) srcSub = srcSub.substr(5);
    if (!srcSub.empty() && srcSub[0] == '/') srcSub.erase(0, 1);
    std::filesystem::path srcDiskDir = srcSub.empty() ? contentRootPath : (contentRootPath / srcSub);

    std::filesystem::path dstDiskDir = srcDiskDir.parent_path() / cleanNewName;

    std::error_code ec;
    if (std::filesystem::exists(dstDiskDir, ec)) {
        AddLog("LogContent", "Cannot rename folder: '" + cleanNewName + "' already exists!", 3);
        return false;
    }

    if (!std::filesystem::exists(srcDiskDir, ec)) return false;

    std::filesystem::rename(srcDiskDir, dstDiskDir, ec);
    if (ec) {
        ec.clear();
        std::filesystem::copy(srcDiskDir, dstDiskDir, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) std::filesystem::remove_all(srcDiskDir, ec);
        else {
            AddLog("LogContent", "Failed to rename folder on disk: " + srcDiskDir.string(), 3);
            return false;
        }
    }

    std::string parentVirtual = AssetPath::GetDirectory(srcVirtualDir);
    std::string newVirtualDir = AssetPath::Combine(parentVirtual, cleanNewName);

    // Update currentVirtualDir if it was inside or equal to srcVirtualDir
    if (currentVirtualDir == srcVirtualDir) {
        currentVirtualDir = newVirtualDir;
    } else if (currentVirtualDir.rfind(srcVirtualDir + "/", 0) == 0) {
        currentVirtualDir = newVirtualDir + currentVirtualDir.substr(srcVirtualDir.length());
    }

    if (selectedContentItem == srcVirtualDir) {
        selectedContentItem = newVirtualDir;
    }
    if (selectedContentItems.count("DIR:" + srcVirtualDir)) {
        selectedContentItems.erase("DIR:" + srcVirtualDir);
        selectedContentItems.insert("DIR:" + newVirtualDir);
    }
    if (lastSelectedContentItem == "DIR:" + srcVirtualDir) {
        lastSelectedContentItem = "DIR:" + newVirtualDir;
    }

    TriggerBackgroundRegistrySync(contentRootPath);
    AddLog("LogContent", "Renamed folder from '" + oldFolderName + "' to '" + cleanNewName + "'", 2);
    return true;
}

static std::filesystem::path FindEngineSourceRoot() {
    std::error_code ec;
    std::vector<std::filesystem::path> candidates = {
        std::filesystem::current_path(),
        std::filesystem::current_path().parent_path(),
        std::filesystem::current_path().parent_path().parent_path(),
        "C:\\Projects\\Eunoia-Engine\\Eunoia-Engine"
    };
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c / "Engine" / "EngineScene" / "Include", ec) &&
            std::filesystem::exists(c / "deps" / "glm", ec)) {
            return std::filesystem::canonical(c, ec);
        }
    }
    return "C:\\Projects\\Eunoia-Engine\\Eunoia-Engine";
}

static std::filesystem::path FindCompilerPath(const std::filesystem::path& engineRoot) {
    std::error_code ec;
    std::vector<std::filesystem::path> candidates = {
        engineRoot / "tools" / "w64devkit" / "bin" / "g++.exe",
        "C:\\Projects\\Eunoia-Engine\\Eunoia-Engine\\tools\\w64devkit\\bin\\g++.exe"
    };
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c, ec)) {
            return c;
        }
    }
    return "g++";
}

static void CopyHeadersRecursive(const std::filesystem::path& src, const std::filesystem::path& dst) {
    std::error_code ec;
    if (!std::filesystem::exists(src, ec)) return;
    for (auto it = std::filesystem::recursive_directory_iterator(src, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_directory(ec)) {
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".h" || ext == ".hpp" || ext == ".inl" || ext == ".c") {
                std::filesystem::path rel = std::filesystem::relative(it->path(), src, ec);
                std::filesystem::path target = dst / rel;
                if (!std::filesystem::exists(target, ec) || std::filesystem::last_write_time(it->path(), ec) > std::filesystem::last_write_time(target, ec)) {
                    std::filesystem::create_directories(target.parent_path(), ec);
                    std::filesystem::copy_file(it->path(), target, std::filesystem::copy_options::overwrite_existing, ec);
                }
            }
        }
    }
}

void EngineUI::SetupProjectDependencies(const std::filesystem::path& projectRoot) {
    std::error_code ec;
    if (projectRoot.empty() || !std::filesystem::exists(projectRoot, ec)) return;

    std::filesystem::path engineRoot = FindEngineSourceRoot();
    std::filesystem::path compilerPath = FindCompilerPath(engineRoot);

    std::filesystem::path depDir = projectRoot / "Dependencies";
    std::filesystem::path depEngineDir = depDir / "Engine";
    std::filesystem::path depDepsDir = depDir / "deps";
    std::filesystem::path depPluginsDir = depDir / "Plugins";

    // 1. Copy Engine Include headers
    std::vector<std::string> engineModules = {
        "EngineCore", "EnginePlatform", "EngineRHI", "EngineRenderer",
        "EngineScene", "EngineAssets", "EunoiaPluginCore"
    };
    for (const auto& mod : engineModules) {
        std::filesystem::path srcInc = engineRoot / "Engine" / mod / "Include";
        std::filesystem::path dstInc = depEngineDir / mod / "Include";
        if (std::filesystem::exists(srcInc, ec)) {
            CopyHeadersRecursive(srcInc, dstInc);
        }
    }

    // 1b. Copy Plugins Include headers (UI-System, Collisions, Physics, RayTrace, Save-System, etc.)
    std::vector<std::string> pluginModules = { "UISystem", "Collisions", "Physics", "RayTrace", "SaveSystem", "Water", "Node-Behaviour", "SFLOD", "Character-Mechanics", "Animation-Graph", "Level-Animation" };
    for (const auto& plug : pluginModules) {
        std::filesystem::path srcInc = engineRoot / "Plugins" / plug / "Include";
        if (!std::filesystem::exists(srcInc, ec)) {
            srcInc = engineRoot / "Plugins" / plug / "include";
        }
        std::filesystem::path dstInc = depPluginsDir / plug / "Include";
        if (std::filesystem::exists(srcInc, ec)) {
            CopyHeadersRecursive(srcInc, dstInc);
        }
        std::filesystem::path srcTp = engineRoot / "Plugins" / plug / "ThirdParty";
        std::filesystem::path dstTp = depPluginsDir / plug / "ThirdParty";
        if (std::filesystem::exists(srcTp, ec)) {
            CopyHeadersRecursive(srcTp, dstTp);
        }
    }

    // 2. Copy third-party headers (GLFW, GLM, JSON, ImGui)
    std::filesystem::path glfwSrc = engineRoot / "deps" / "glfw-3.5.1.bin.WIN64" / "include";
    std::filesystem::path glfwDst = depDepsDir / "glfw-3.5.1.bin.WIN64" / "include";
    if (std::filesystem::exists(glfwSrc, ec)) {
        CopyHeadersRecursive(glfwSrc, glfwDst);
    }

    std::filesystem::path glmSrc = engineRoot / "deps" / "glm";
    std::filesystem::path glmDst = depDepsDir / "glm";
    if (std::filesystem::exists(glmSrc, ec)) {
        CopyHeadersRecursive(glmSrc, glmDst);
    }

    std::filesystem::path jsonSrc = engineRoot / "deps" / "json";
    std::filesystem::path jsonDst = depDepsDir / "json";
    if (std::filesystem::exists(jsonSrc, ec)) {
        CopyHeadersRecursive(jsonSrc, jsonDst);
    }

    std::filesystem::path imguiSrc = engineRoot / "deps" / "imgui";
    std::filesystem::path imguiDst = depDepsDir / "imgui";
    if (std::filesystem::exists(imguiSrc, ec)) {
        CopyHeadersRecursive(imguiSrc, imguiDst);
    }

    // 3. Write projectRoot/.vscode/c_cpp_properties.json
    std::filesystem::path vscodeDir = projectRoot / ".vscode";
    std::filesystem::create_directories(vscodeDir, ec);

    std::string compStr = compilerPath.generic_string();
    std::string engStr = engineRoot.generic_string();

    std::filesystem::path propPath = vscodeDir / "c_cpp_properties.json";
    std::ofstream pf(propPath);
    if (pf.is_open()) {
        pf << "{\n"
           << "  \"configurations\": [\n"
           << "    {\n"
           << "      \"name\": \"Eunoia-Engine\",\n"
           << "      \"includePath\": [\n"
           << "        \"${workspaceFolder}/**\",\n"
           << "        \"${workspaceFolder}/Content/**\",\n"
           << "        \"${workspaceFolder}/Dependencies/Engine/EngineCore/Include\",\n"
           << "        \"${workspaceFolder}/Dependencies/Engine/EnginePlatform/Include\",\n"
           << "        \"${workspaceFolder}/Dependencies/Engine/EngineRHI/Include\",\n"
           << "        \"${workspaceFolder}/Dependencies/Engine/EngineRenderer/Include\",\n"
           << "        \"${workspaceFolder}/Dependencies/Engine/EngineScene/Include\",\n"
           << "        \"${workspaceFolder}/Dependencies/Engine/EngineAssets/Include\",\n"
           << "        \"${workspaceFolder}/Dependencies/Engine/EunoiaPluginCore/Include\",\n"
           << "        \"${workspaceFolder}/Dependencies/Plugins/UISystem/Include\",\n"
           << "        \"${workspaceFolder}/Dependencies/Plugins/**\",\n"
           << "        \"${workspaceFolder}/Dependencies/deps/glfw-3.5.1.bin.WIN64/include\",\n"
           << "        \"${workspaceFolder}/Dependencies/deps/glm\",\n"
           << "        \"${workspaceFolder}/Dependencies/deps/json\",\n"
           << "        \"${workspaceFolder}/Dependencies/deps/imgui\",\n"
           << "        \"" << engStr << "/Engine/EngineCore/Include\",\n"
           << "        \"" << engStr << "/Engine/EnginePlatform/Include\",\n"
           << "        \"" << engStr << "/Engine/EngineRHI/Include\",\n"
           << "        \"" << engStr << "/Engine/EngineRenderer/Include\",\n"
           << "        \"" << engStr << "/Engine/EngineScene/Include\",\n"
           << "        \"" << engStr << "/Engine/EngineAssets/Include\",\n"
           << "        \"" << engStr << "/Engine/EunoiaPluginCore/Include\",\n"
           << "        \"" << engStr << "/Plugins/UISystem/Include\",\n"
           << "        \"" << engStr << "/Plugins/**\",\n"
           << "        \"" << engStr << "/deps/glfw-3.5.1.bin.WIN64/include\",\n"
           << "        \"" << engStr << "/deps/glm\",\n"
           << "        \"" << engStr << "/deps/json\",\n"
           << "        \"" << engStr << "/deps/imgui\"\n"
           << "      ],\n"
           << "      \"defines\": [\n"
           << "        \"_DEBUG\",\n"
           << "        \"UNICODE\",\n"
           << "        \"_UNICODE\",\n"
           << "        \"WIN32_LEAN_AND_MEAN\",\n"
           << "        \"NOMINMAX\"\n"
           << "      ],\n"
           << "      \"compilerPath\": \"" << compStr << "\",\n"
           << "      \"cStandard\": \"c11\",\n"
           << "      \"cppStandard\": \"c++20\",\n"
           << "      \"intelliSenseMode\": \"windows-gcc-x64\"\n"
           << "    }\n"
           << "  ],\n"
           << "  \"version\": 4\n"
           << "}\n";
        pf.close();
    }

    // 4. Write projectRoot/.vscode/settings.json
    std::filesystem::path settingsPath = vscodeDir / "settings.json";
    std::ofstream sf(settingsPath);
    if (sf.is_open()) {
        sf << "{\n"
           << "  \"files.exclude\": {\n"
           << "    \"**/*.assetmeta\": true,\n"
           << "    \"**/*.meta\": true\n"
           << "  }\n"
           << "}\n";
        sf.close();
    }

    // 5. Write stamp file to cache dependency state
    std::filesystem::path stampPath = depDir / ".engine_stamp";
    std::ofstream stampf(stampPath);
    if (stampf.is_open()) {
        stampf << engineRoot.generic_string() << "\n";
        stampf.close();
    }
}

static void LaunchVSCodeWorkspace(const std::string& filePath) {
    std::filesystem::path absFilePath = std::filesystem::absolute(filePath);
    std::filesystem::path targetWorkspace;
    if (g_pEngineUI && !g_pEngineUI->activeProjectRoot.empty()) {
        targetWorkspace = g_pEngineUI->activeProjectRoot;
    } else {
        std::filesystem::path p = absFilePath.parent_path();
        while (p.has_parent_path() && p != p.parent_path()) {
            if (std::filesystem::exists(p / "Content") || std::filesystem::exists(p / ".vscode")) {
                targetWorkspace = p;
                break;
            }
            p = p.parent_path();
        }
    }
    if (targetWorkspace.empty()) {
        targetWorkspace = std::filesystem::current_path();
    }

    if (g_pEngineUI) {
        g_pEngineUI->SetupProjectDependencies(targetWorkspace);
    }

    EunoiaUI::LaunchVSCodeSilently(absFilePath, targetWorkspace);
}

EngineUI::EngineUI() {
    g_pEngineUI = this;
    AddLog("LogInit", "Eunoia-Editor Initialized (DirectX 12)", 2);
    AddLog("LogD3D12", "Hardware Adapter: NVIDIA GeForce RTX 3060 Laptop GPU (Feature Level 12_1)", 0);

    // Setup default projects directory path for the project browser
    std::filesystem::path defaultProjectsDir = "C:\\Projects\\Eunoia-Engine\\Projects";
    std::error_code ec;
    if (!std::filesystem::exists(defaultProjectsDir, ec)) {
        std::filesystem::path altProjectsDir = std::filesystem::current_path() / "Projects";
        if (std::filesystem::exists(altProjectsDir, ec)) {
            defaultProjectsDir = altProjectsDir;
        } else {
            std::filesystem::create_directories(defaultProjectsDir, ec);
        }
    }
    strncpy(newProjectPathBuf, defaultProjectsDir.string().c_str(), sizeof(newProjectPathBuf) - 1);

    // Load recent projects list (needed by both standalone browser and in-editor browser)
    LoadRecentProjects();

    // Project Browser is NOT shown on startup ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â the standalone window handles it before the editor opens
    showProjectBrowser = false;
}

EngineUI::~EngineUI() {
    if (gameBuildThread.joinable()) {
        gameBuildThread.join();
    }
}

void EngineUI::InitWithProject(const std::filesystem::path& projectPath, Scene& scene, OrbitCamera& camera, bool isGameMode, const std::string& specificScenePath, const std::string& specificBinPath) {
    if (projectPath.empty()) {
        // No project selected ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â show in-editor project browser as fallback if not game mode
        if (!isGameMode) {
            showProjectBrowser = true;
        }

        // Set a default project path so the editor doesn't crash
        std::filesystem::path defaultProjectsDir = "C:\\Projects\\Eunoia-Engine\\Projects";
        std::error_code ec;
        if (!std::filesystem::exists(defaultProjectsDir, ec)) {
            defaultProjectsDir = std::filesystem::current_path() / "Projects";
        }
        activeProjectRoot = defaultProjectsDir / "Default";
        activeProjectName = "Default";
        contentRootPath = activeProjectRoot / "Content";
        std::filesystem::create_directories(contentRootPath, ec);
        currentContentPath = contentRootPath;
        TextureManager::Get().SetProjectRoot(contentRootPath);
        AssetManager::Get().Initialize(contentRootPath);
        AssetRegistry::Get().SetContentRoot(contentRootPath);
        knownModelFiles.clear();
        pendingModelImportQueue.clear();
        showModelImportModal = false;
        if (!isGameMode) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(contentRootPath, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (!entry.is_directory(ec)) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".fbx" || ext == ".glb" || ext == ".gltf" || ext == ".obj") {
                        knownModelFiles.insert(std::filesystem::absolute(entry.path()).string());
                    }
                }
            }
            LoadEditorConfig();
        }
        return;
    }

    // Load the selected project
    LoadProject(projectPath, scene, camera, isGameMode, specificScenePath, specificBinPath);
    if (!isGameMode) {
        AddLog("LogWorld", "Project loaded: " + activeProjectName, 2);
    }

    currentAntiAliasing = Eunoia::GraphicsSystem::Get().GetAntiAliasingMethod();
    Eunoia::GraphicsSystem::Get().RegisterAntiAliasingChangedCallback("EngineUI", [this](Eunoia::AntiAliasingMethod /*oldM*/, Eunoia::AntiAliasingMethod newM) {
        this->currentAntiAliasing = newM;
        this->AddLog("LogGraphics", "Anti-Aliasing applied to level: " + std::string(Eunoia::GraphicsSystem::Get().GetAntiAliasingName(newM)), 0);
    });
}

void EngineUI::LoadEditorConfig() {
    if (activeProjectRoot.empty()) return;
    std::filesystem::path configPath = activeProjectRoot / "Configs.Editor.econfigs";
    std::error_code ec;
    if (!std::filesystem::exists(configPath, ec)) {
        // If config doesn't exist yet, save current defaults to create it in the project root
        SaveEditorConfig();
        return;
    }

    std::ifstream file(configPath);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(0, eqPos);
        std::string val = line.substr(eqPos + 1);

        auto Trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            size_t last = s.find_last_not_of(" \t\r\n");
            if (last != std::string::npos) s.erase(last + 1);
        };
        Trim(key);
        Trim(val);

        if (key == "LastOpenedLevel" || key == "lastOpenedLevel") {
            lastOpenedLevelConfig = val;
            continue;
        }

        try {
            float fVal = std::stof(val);
            if (key == "LeftSidebarWidth" || key == "leftSidebarWidth") {
                leftSidebarWidth = std::clamp(fVal, 180.0f, 800.0f);
            } else if (key == "RightSidebarWidth" || key == "rightSidebarWidth") {
                rightSidebarWidth = std::clamp(fVal, 200.0f, 900.0f);
            } else if (key == "OutlinerHeightRatio" || key == "outlinerHeightRatio") {
                outlinerHeightRatio = std::clamp(fVal, 0.15f, 0.85f);
            } else if (key == "BottomDockHeight" || key == "bottomDockHeight" || key == "BottomContentHeight") {
                bottomDockHeight = std::clamp(fVal, 100.0f, 800.0f);
            } else if (key == "ContentBrowserFolderTreeWidth" || key == "contentBrowserFolderTreeWidth" || key == "FolderTreeWidth" || key == "folderTreeWidth") {
                contentBrowserFolderTreeWidth = std::clamp(fVal, 120.0f, 800.0f);
            } else if (key == "AntiAliasing" || key == "antiAliasing") {
                int aa = (int)fVal;
                if (aa >= 0 && aa <= 9) {
                    currentAntiAliasing = (AntiAliasingMethod)aa;
                    Eunoia::GraphicsSystem::Get().SetAntiAliasingMethod(currentAntiAliasing);
                }
            } else if (key == "TextureQuality" || key == "textureQuality") {
                int tq = (int)fVal;
                if (tq >= 0 && tq <= 5) {
                    currentTextureQuality = (TextureQuality)tq;
                    TextureManager::Get().SetTextureQualitySilent(currentTextureQuality);
                }
            } else if (key == "VSync" || key == "vsync") {
                vsyncEnabled = (fVal > 0.5f);
            } else if (key == "ContentThumbnailSize" || key == "contentThumbnailSize" || key == "ThumbnailSize" || key == "thumbnailSize") {
                contentThumbnailSize = std::clamp(fVal, 48.0f, 256.0f);
            } else if (key == "UIScale" || key == "uiScale" || key == "Scale") {
                float sc = fVal;
                if (sc > 5.0f) sc /= 100.0f;
                ApplyUIScale(sc);
            }
        } catch (...) {
            // Ignore format errors
        }
    }
}

void EngineUI::ApplyTextureQuality(TextureQuality quality) {
    currentTextureQuality = quality;
    TextureManager::Get().SetTextureQuality(quality);
}

void EngineUI::SaveEditorConfig() {
    if (activeProjectRoot.empty()) return;
    std::error_code ec;
    if (!std::filesystem::exists(activeProjectRoot, ec)) return;

    std::filesystem::path configPath = activeProjectRoot / "Configs.Editor.econfigs";
    std::ofstream file(configPath);
    if (!file.is_open()) return;

    file << "[EditorLayout]\n";
    file << "LeftSidebarWidth=" << leftSidebarWidth << "\n";
    file << "RightSidebarWidth=" << rightSidebarWidth << "\n";
    file << "OutlinerHeightRatio=" << outlinerHeightRatio << "\n";
    file << "BottomDockHeight=" << bottomDockHeight << "\n";
    file << "ContentBrowserFolderTreeWidth=" << contentBrowserFolderTreeWidth << "\n";
    file << "UIScale=" << uiScale << "\n";
    file << "ContentThumbnailSize=" << contentThumbnailSize << "\n";
    file << "\n[Graphics]\n";
    file << "AntiAliasing=" << (int)currentAntiAliasing << "\n";
    file << "TextureQuality=" << (int)currentTextureQuality << "\n";
    file << "VSync=" << (vsyncEnabled ? 1 : 0) << "\n";
    file << "\n[Project]\n";
    if (!currentLevelFilePath.empty()) {
        file << "LastOpenedLevel=" << currentLevelFilePath << "\n";
    } else if (!lastOpenedLevelConfig.empty()) {
        file << "LastOpenedLevel=" << lastOpenedLevelConfig << "\n";
    }
    file.close();
}

// ================================================================================
// STANDALONE PROJECT BROWSER WINDOW
// Opens as a separate GLFW+OpenGL3 window BEFORE the main D3D12 editor.
// Returns the selected/created project path, or empty if user closed without selecting.
// ================================================================================

// Forward-declare OpenGL and GLFW ImGui backends (compiled in Build.bat)
#ifndef GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

static std::filesystem::path GetRecentProjectsConfigPath() {
    char* appdata = getenv("LOCALAPPDATA");
    if (appdata) {
        std::filesystem::path p = std::filesystem::path(appdata) / "EunoiaEngine" / "RecentProjects.json";
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        return p;
    }
    return "RecentProjects.json";
}

// Helper: Apply the same dark theme as the editor
static void ApplyProjectBrowserTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 10.0f;
    style.ChildRounding     = 8.0f;
    style.FrameRounding     = 7.0f;
    style.PopupRounding     = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding      = 6.0f;
    style.TabRounding       = 6.0f;
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.WindowPadding     = ImVec2(16.0f, 14.0f);
    style.FramePadding      = ImVec2(10.0f, 6.0f);
    style.ItemSpacing       = ImVec2(10.0f, 8.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]       = ImVec4(0.082f, 0.082f, 0.102f, 1.00f); // Atlas Surface #15151A
    colors[ImGuiCol_ChildBg]        = ImVec4(0.082f, 0.082f, 0.102f, 1.00f);
    colors[ImGuiCol_Border]         = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);       // Atlas Outline
    colors[ImGuiCol_FrameBg]        = ImVec4(0.125f, 0.125f, 0.153f, 1.00f); // Atlas Control #202027
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.169f, 0.169f, 0.204f, 1.00f); // Atlas Hovered
    colors[ImGuiCol_FrameBgActive]  = ImVec4(0.125f, 0.165f, 0.259f, 1.00f); // Atlas Selected
    colors[ImGuiCol_TitleBg]        = ImVec4(0.102f, 0.102f, 0.125f, 1.00f); // Atlas Header
    colors[ImGuiCol_TitleBgActive]  = ImVec4(0.102f, 0.102f, 0.125f, 1.00f);
    colors[ImGuiCol_Button]         = ImVec4(0.125f, 0.125f, 0.153f, 1.00f);
    colors[ImGuiCol_ButtonHovered]  = ImVec4(0.169f, 0.169f, 0.204f, 1.00f);
    colors[ImGuiCol_ButtonActive]   = ImVec4(0.290f, 0.486f, 1.000f, 1.00f); // Atlas Accent #4A7CFF
    colors[ImGuiCol_Header]         = ImVec4(0.290f, 0.486f, 1.000f, 0.18f);
    colors[ImGuiCol_HeaderHovered]  = ImVec4(0.290f, 0.486f, 1.000f, 0.28f);
    colors[ImGuiCol_HeaderActive]   = ImVec4(0.290f, 0.486f, 1.000f, 0.40f);
    colors[ImGuiCol_Separator]      = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
    colors[ImGuiCol_Text]           = ImVec4(0.929f, 0.933f, 0.953f, 1.00f); // Atlas Text #EDEEF3
    colors[ImGuiCol_TextDisabled]   = ImVec4(0.541f, 0.553f, 0.600f, 1.00f); // Atlas Faint #8A8D99
    colors[ImGuiCol_ScrollbarBg]    = ImVec4(0.039f, 0.039f, 0.051f, 0.50f);
    colors[ImGuiCol_ScrollbarGrab]  = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_Tab]            = ImVec4(0.145f, 0.145f, 0.145f, 1.00f);
    colors[ImGuiCol_TabHovered]     = ImVec4(0.200f, 0.200f, 0.200f, 1.00f);
    colors[ImGuiCol_TabActive]      = ImVec4(0.122f, 0.600f, 0.949f, 1.00f);
    colors[ImGuiCol_PopupBg]        = ImVec4(0.135f, 0.135f, 0.135f, 0.98f);
}

#include <GL/gl.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

extern "C" {
    unsigned char *stbi_load(char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);
    void stbi_image_free(void *retval_from_stbi_load);
}

void SplashScreen::Init(GLFWwindow* window, ImGuiContext* ctx, const std::string& projectName) {
    m_window = window;
    m_imguiContext = ctx;
    m_projectName = projectName;
    m_statusMessage = "Initializing Eunoia Engine...";
    m_currentProgress = 0.05f;
    m_active = true;

    if (!m_window) return;

    glfwMakeContextCurrent(m_window);
    glfwSetWindowTitle(m_window, "Eunoia Engine");

    // Load Eunoia Engine logo from Resources/Icons/eunoia.png
    const char* paths[] = {
        "Resources/Icons/eunoia.png",
        "../Resources/Icons/eunoia.png",
        "../../Resources/Icons/eunoia.png",
        "Build/Engine/Resources/Icons/eunoia.png"
    };
    unsigned char* data = nullptr;
    int w = 0, h = 0, comp = 0;
    for (const char* p : paths) {
        std::error_code ec;
        if (std::filesystem::exists(p, ec)) {
            data = stbi_load(p, &w, &h, &comp, 4);
            if (data) break;
        }
    }
    if (data) {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(data);
        m_logoTexture = tex;
        m_logoWidth = w;
        m_logoHeight = h;
    }

    RenderFrame();
}

void SplashScreen::RenderFrame() {
    if (!m_window || !m_active) return;

    ImGuiContext* prevCtx = ImGui::GetCurrentContext();
    glfwMakeContextCurrent(m_window);
    if (m_imguiContext) {
        ImGui::SetCurrentContext(m_imguiContext);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    int fbW = 860, fbH = 580;
    glfwGetFramebufferSize(m_window, &fbW, &fbH);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)fbW, (float)fbH));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.118f, 0.118f, 0.118f, 1.0f));

    if (ImGui::Begin("##SplashScreenWindow", nullptr, flags)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();

        // 1. Subtle background gradient
        drawList->AddRectFilledMultiColor(
            winPos,
            ImVec2(winPos.x + winSize.x, winPos.y + winSize.y),
            IM_COL32(14, 16, 22, 255),
            IM_COL32(14, 16, 22, 255),
            IM_COL32(8, 9, 12, 255),
            IM_COL32(8, 9, 12, 255)
        );

        float centerX = winPos.x + winSize.x * 0.5f;
        float logoCenterY = winPos.y + 165.0f;

        // Ambient radial glow behind the logo
        drawList->AddCircleFilled(ImVec2(centerX, logoCenterY), 110.0f, IM_COL32(14, 110, 220, 25), 64);
        drawList->AddCircleFilled(ImVec2(centerX, logoCenterY), 70.0f, IM_COL32(0, 190, 255, 30), 48);

        // 2. Eunoia Engine Logo
        float logoSize = 130.0f;
        float logoX = centerX - logoSize * 0.5f;
        float logoY = winPos.y + 100.0f;

        if (m_logoTexture != 0) {
            drawList->AddRectFilled(
                ImVec2(logoX + 2, logoY + 4),
                ImVec2(logoX + logoSize + 2, logoY + logoSize + 4),
                IM_COL32(0, 0, 0, 100), 16.0f
            );
            drawList->AddImage(
                (ImTextureID)(intptr_t)m_logoTexture,
                ImVec2(logoX, logoY),
                ImVec2(logoX + logoSize, logoY + logoSize)
            );
        } else {
            drawList->AddRectFilled(
                ImVec2(logoX, logoY),
                ImVec2(logoX + logoSize, logoY + logoSize),
                IM_COL32(18, 20, 26, 255), 16.0f
            );
            drawList->AddRect(
                ImVec2(logoX, logoY),
                ImVec2(logoX + logoSize, logoY + logoSize),
                IM_COL32(20, 120, 240, 200), 16.0f, 0, 2.0f
            );
            const char* fallbackText = "EUEN";
            ImVec2 textSize = ImGui::CalcTextSize(fallbackText);
            drawList->AddText(
                ImVec2(centerX - textSize.x * 0.5f, logoY + (logoSize - textSize.y) * 0.5f),
                IM_COL32(255, 255, 255, 255), fallbackText
            );
        }

        // 3. Engine Title Header
        const char* titleText = "E U N O I A   E N G I N E";
        ImVec2 titleSize = ImGui::CalcTextSize(titleText);
        drawList->AddText(
            ImVec2(centerX - titleSize.x * 0.5f, logoY + logoSize + 22.0f),
            IM_COL32(255, 255, 255, 255), titleText
        );

        // Subtitle
        const char* subText = "Next-Generation DirectX 12 3D Game Engine";
        ImVec2 subSize = ImGui::CalcTextSize(subText);
        drawList->AddText(
            ImVec2(centerX - subSize.x * 0.5f, logoY + logoSize + 46.0f),
            IM_COL32(130, 140, 160, 230), subText
        );

        // Project Name
        if (!m_projectName.empty()) {
            std::string projStr = "Opening Project: " + m_projectName;
            ImVec2 projSize = ImGui::CalcTextSize(projStr.c_str());
            drawList->AddText(
                ImVec2(centerX - projSize.x * 0.5f, logoY + logoSize + 70.0f),
                IM_COL32(32, 180, 255, 255), projStr.c_str()
            );
        }

        // 4. Progress bar area
        float barWidth = 560.0f;
        float barHeight = 12.0f;
        float barX = centerX - barWidth * 0.5f;
        float barY = winPos.y + winSize.y - 120.0f;

        // Status Message (above bar, left aligned with bar)
        drawList->AddText(
            ImVec2(barX, barY - 26.0f),
            IM_COL32(210, 215, 225, 255), m_statusMessage.c_str()
        );

        // Percentage Text (above bar, right aligned with bar)
        int pct = (int)std::round(glm::clamp(m_currentProgress, 0.0f, 1.0f) * 100.0f);
        char pctBuf[16];
        snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
        ImVec2 pctSize = ImGui::CalcTextSize(pctBuf);
        drawList->AddText(
            ImVec2(barX + barWidth - pctSize.x, barY - 26.0f),
            IM_COL32(0, 229, 255, 255), pctBuf
        );

        // Track (Bar Background)
        drawList->AddRectFilled(
            ImVec2(barX, barY),
            ImVec2(barX + barWidth, barY + barHeight),
            IM_COL32(20, 22, 28, 255), 6.0f
        );
        drawList->AddRect(
            ImVec2(barX, barY),
            ImVec2(barX + barWidth, barY + barHeight),
            IM_COL32(40, 44, 55, 180), 6.0f, 0, 1.0f
        );

        // Fill Bar (Vibrant cyan-blue gradient with rounded corners)
        float fillW = barWidth * glm::clamp(m_currentProgress, 0.0f, 1.0f);
        if (fillW > 4.0f) {
            drawList->AddRectFilledMultiColor(
                ImVec2(barX, barY),
                ImVec2(barX + fillW, barY + barHeight),
                IM_COL32(14, 112, 224, 255), // Left: Eunoia Blue
                IM_COL32(0, 229, 255, 255),  // Right: Vibrant Cyan
                IM_COL32(0, 229, 255, 255),
                IM_COL32(14, 112, 224, 255)
            );

            // Subtle leading-edge highlight
            drawList->AddRectFilled(
                ImVec2(barX + fillW - 3.0f, barY),
                ImVec2(barX + fillW, barY + barHeight),
                IM_COL32(255, 255, 255, 220), 4.0f
            );
        }

        // 5. Footer info
        const char* footerText = "DirectX 12 Ultimate  |  Clustered PBR  |  Mesh Culling Pipeline";
        ImVec2 footerSize = ImGui::CalcTextSize(footerText);
        drawList->AddText(
            ImVec2(centerX - footerSize.x * 0.5f, winPos.y + winSize.y - 40.0f),
            IM_COL32(75, 82, 100, 200), footerText
        );
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    ImGui::Render();

    glViewport(0, 0, fbW, fbH);
    glClearColor(0.118f, 0.118f, 0.118f, 1.00f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(m_window);
    glfwPollEvents();

    if (prevCtx && prevCtx != m_imguiContext) {
        ImGui::SetCurrentContext(prevCtx);
    }
}

void SplashScreen::UpdateProgress(float progress, const std::string& statusMessage) {
    if (!m_active || !m_window) return;
    m_statusMessage = statusMessage;
    float start = m_currentProgress;
    int frames = 6;
    for (int i = 1; i <= frames; ++i) {
        float t = (float)i / (float)frames;
        m_currentProgress = start + (progress - start) * t;
        RenderFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    m_currentProgress = progress;
    RenderFrame();
}

void SplashScreen::Finish() {
    if (!m_active) return;

    if (m_window) {
        glfwMakeContextCurrent(m_window);

        // IMPORTANT: Set the splash ImGui context as current BEFORE shutting down
        // its backends. Without this, if the main editor ImGui context has already
        // been created (which it has at this point in startup), the shutdown calls
        // would operate on the wrong context, unregistering the main editor's GLFW
        // callbacks and breaking the editor window entirely.
        if (m_imguiContext) {
            ImGui::SetCurrentContext(m_imguiContext);
        }

        if (m_logoTexture != 0) {
            glDeleteTextures(1, &m_logoTexture);
            m_logoTexture = 0;
        }
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        if (m_imguiContext) {
            ImGui::DestroyContext(m_imguiContext);
            m_imguiContext = nullptr;
        }

        // Clear the current context pointer since the splash context is now destroyed.
        // The main editor's render loop will re-establish its own context on first frame.
        ImGui::SetCurrentContext(nullptr);

        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwDefaultWindowHints();
    m_active = false;
}

std::filesystem::path EngineUI::RunStandaloneProjectBrowser() {
    // GLFW must already be initialized before calling this

    // Create a dedicated OpenGL window for the project browser
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);

    const int BROWSER_W = 860;
    const int BROWSER_H = 580;

    GLFWwindow* browserWindow = glfwCreateWindow(BROWSER_W, BROWSER_H, "Eunoia Engine - Project Browser", nullptr, nullptr);
    if (!browserWindow) {
        std::cerr << "[ProjectBrowser] Failed to create GLFW OpenGL window!\n";
        return {};
    }

    glfwMakeContextCurrent(browserWindow);
    glfwSwapInterval(1); // VSync on

#ifdef _WIN32
    HWND browserHwnd = glfwGetWin32Window(browserWindow);
    if (browserHwnd) {
        HICON hIconBig = (HICON)LoadImageA(NULL, "Resources/Icons/eunoia.ico", IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
        HICON hIconSmall = (HICON)LoadImageA(NULL, "Resources/Icons/eunoia.ico", IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
        if (!hIconBig) hIconBig = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(1));
        if (!hIconSmall) {
            hIconSmall = (HICON)LoadImageA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(1), IMAGE_ICON, 16, 16, 0);
            if (!hIconSmall) hIconSmall = hIconBig;
        }
        if (hIconBig) SendMessageA(browserHwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
        if (hIconSmall) SendMessageA(browserHwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
    }
#endif

    // Center the window on the primary monitor
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (mode) {
            glfwSetWindowPos(browserWindow, (mode->width - BROWSER_W) / 2, (mode->height - BROWSER_H) / 2);
        }
    }

    // Create a separate ImGui context for the project browser
    IMGUI_CHECKVERSION();
    ImGuiContext* browserCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(browserCtx);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    // Load font
    ImFontConfig fontConfig;
    fontConfig.OversampleH = 1;
    fontConfig.OversampleV = 1;
    ImFont* mainFont = nullptr;
    if (std::filesystem::exists("C:\\Windows\\Fonts\\segoeui.ttf")) {
        mainFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f, &fontConfig);
    } else if (std::filesystem::exists("C:\\Windows\\Fonts\\arial.ttf")) {
        mainFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 18.0f, &fontConfig);
    }
    if (mainFont && std::filesystem::exists("C:\\Windows\\Fonts\\seguisym.ttf")) {
        ImFontConfig symbolConfig;
        symbolConfig.MergeMode = true;
        symbolConfig.OversampleH = 2;
        symbolConfig.OversampleV = 2;
        static const ImWchar symbolRanges[] = { 0x2000, 0x2BFF, 0 };
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisym.ttf", 18.0f, &symbolConfig, symbolRanges);
    }
    if (!mainFont) {
        io.Fonts->AddFontDefault();
        io.FontGlobalScale = 1.25f;
    }

    ApplyProjectBrowserTheme();

    ImGui_ImplGlfw_InitForOpenGL(browserWindow, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Project browser state
    std::vector<ProjectEntry> projects;
    char searchBuf[64] = "";
    char newNameBuf[64] = "MyProject";
    char newPathBuf[260] = "";
    int selectedIdx = 0;
    int activeTab = 0; // 0 = Recent, 1 = New

    // Setup default path
    std::filesystem::path defaultDir = "C:\\Projects\\Eunoia-Engine\\Projects";
    std::error_code ec;
    if (!std::filesystem::exists(defaultDir, ec)) {
        defaultDir = std::filesystem::current_path() / "Projects";
    }
    strncpy(newPathBuf, defaultDir.string().c_str(), sizeof(newPathBuf) - 1);

    // Load recent projects from config
    std::filesystem::path cfgPath;
    {
        char* appdata = getenv("LOCALAPPDATA");
        if (appdata) {
            cfgPath = std::filesystem::path(appdata) / "EunoiaEngine" / "RecentProjects.json";
        } else {
            cfgPath = "RecentProjects.json";
        }
    }

    if (std::filesystem::exists(cfgPath, ec)) {
        std::ifstream f(cfgPath);
        if (f.is_open()) {
            try {
                nlohmann::json j;
                f >> j;
                if (j.is_array()) {
                    for (const auto& item : j) {
                        if (item.contains("rootPath")) {
                            ProjectEntry pe;
                            pe.rootPath = item["rootPath"].get<std::string>();
                            pe.name = item.value("name", std::filesystem::path(pe.rootPath).filename().string());
                            pe.lastOpened = item.value("lastOpened", "");
                            if (std::filesystem::exists(pe.rootPath, ec)) {
                                projects.push_back(pe);
                            }
                        }
                    }
                }
            } catch (...) {}
        }
    }

    // Discover projects on disk
    std::vector<std::filesystem::path> searchDirs = {
        "C:\\Projects\\Eunoia-Engine\\Projects",
        std::filesystem::current_path() / "Projects",
        std::filesystem::current_path().parent_path() / "Projects"
    };
    for (const auto& dir : searchDirs) {
        if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.is_directory(ec)) {
                std::filesystem::path pRoot = entry.path();
                bool alreadyIn = false;
                for (const auto& r : projects) {
                    if (std::filesystem::equivalent(std::filesystem::path(r.rootPath), pRoot, ec) || r.rootPath == pRoot.string()) {
                        alreadyIn = true;
                        break;
                    }
                }
                if (!alreadyIn) {
                    bool isProj = false;
                    for (const auto& sub : std::filesystem::directory_iterator(pRoot, ec)) {
                        if (sub.path().extension() == ".euproject" || sub.path().extension() == ".eproject" || sub.path().filename() == "Content") {
                            isProj = true;
                            break;
                        }
                    }
                    if (isProj) {
                        ProjectEntry pe;
                        pe.name = pRoot.filename().string();
                        pe.rootPath = pRoot.string();
                        pe.lastOpened = "Discovered on disk";
                        projects.push_back(pe);
                    }
                }
            }
        }
    }

    std::filesystem::path selectedProject;
    bool shouldCreateNew = false;
    std::string createParentDir, createProjName;

    // Explicitly show and bring the Project Browser window to the foreground
    glfwShowWindow(browserWindow);
#ifdef _WIN32
    if (browserHwnd) {
        ShowWindow(browserHwnd, SW_SHOWNORMAL);
        BringWindowToTop(browserHwnd);
        SetForegroundWindow(browserHwnd);
    }
#endif

    // Main event loop for the standalone project browser window
    while (!glfwWindowShouldClose(browserWindow) && selectedProject.empty() && !shouldCreateNew) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Full-window project browser UI
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)BROWSER_W, (float)BROWSER_H));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));

        if (ImGui::Begin("##StandaloneProjectBrowser", nullptr, flags)) {
            // Title Header
            ImGui::TextColored(ImVec4(0.12f, 0.68f, 1.00f, 1.0f), "EUNOIA ENGINE");
            ImGui::SameLine();
            ImGui::TextDisabled("ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â Project Browser");
            ImGui::Spacing();
            ImGui::TextWrapped("Select an existing project or create a new empty game project to get started.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Tab buttons
            float tabW = 180.0f;
            bool tab0Active = (activeTab == 0);
            if (tab0Active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.50f, 0.90f, 1.0f));
            std::string recentLabel = "Recent Projects (" + std::to_string(projects.size()) + ")";
            if (ImGui::Button(recentLabel.c_str(), ImVec2(tabW, 32))) {
                activeTab = 0;
            }
            if (tab0Active) ImGui::PopStyleColor();

            ImGui::SameLine();
            bool tab1Active = (activeTab == 1);
            if (tab1Active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.50f, 0.90f, 1.0f));
            if (ImGui::Button("New Project", ImVec2(tabW, 32))) {
                activeTab = 1;
            }
            if (tab1Active) ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            if (ImGui::Button("Browse for Project...", ImVec2(170, 32))) {
                std::string folder = ShowSelectFolderDialog(nullptr, "Select Existing Project Folder");
                if (!folder.empty()) {
                    selectedProject = folder;
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (activeTab == 0) {
                // ============= RECENT PROJECTS TAB =============
                ImGui::SetNextItemWidth((float)BROWSER_W - 230.0f);
                ImGui::InputTextWithHint("##Filter", "Filter projects...", searchBuf, sizeof(searchBuf));
                ImGui::SameLine();
                if (ImGui::Button("Refresh", ImVec2(100, 0))) {
                    // Re-scan (simplified)
                }
                ImGui::Spacing();

                ImGui::BeginChild("ProjectList", ImVec2(0, (float)BROWSER_H - 280.0f), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
                if (projects.empty()) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("No recent projects found.");
                    ImGui::Text("Click 'New Project' to create your first game project!");
                } else {
                    for (size_t i = 0; i < projects.size(); ++i) {
                        const auto& p = projects[i];

                        // Search filter
                        if (strlen(searchBuf) > 0) {
                            std::string sLower = searchBuf;
                            std::string nLower = p.name;
                            for (auto& c : sLower) c = (char)tolower(c);
                            for (auto& c : nLower) c = (char)tolower(c);
                            if (nLower.find(sLower) == std::string::npos && p.rootPath.find(searchBuf) == std::string::npos)
                                continue;
                        }

                        ImGui::PushID((int)i);
                        bool isSelected = (selectedIdx == (int)i);

                        ImVec2 curPos = ImGui::GetCursorPos();
                        ImVec2 itemSize(ImGui::GetContentRegionAvail().x, 48.0f);
                        ImVec2 screenPos = ImGui::GetCursorScreenPos();
                        ImVec2 pMin = screenPos;
                        ImVec2 pMax = ImVec2(screenPos.x + itemSize.x, screenPos.y + itemSize.y);

                        // Full-width invisible button to reliably capture clicks anywhere on the card
                        ImGui::InvisibleButton("##ProjSelBtn", itemSize);
                        bool isHovered = ImGui::IsItemHovered();
                        if (ImGui::IsItemClicked()) {
                            selectedIdx = (int)i;
                        }
                        if (isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            selectedProject = p.rootPath;
                        }

                        // Background styling
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImU32 bgCol = isSelected ? IM_COL32(30, 95, 195, 180)
                                                 : (isHovered ? IM_COL32(42, 52, 70, 160) : IM_COL32(24, 28, 38, 120));
                        ImU32 borderCol = isSelected ? IM_COL32(50, 160, 255, 230)
                                                     : (isHovered ? IM_COL32(70, 130, 220, 150) : IM_COL32(40, 45, 58, 100));
                        dl->AddRectFilled(pMin, pMax, bgCol, 4.0f);
                        dl->AddRect(pMin, pMax, borderCol, 4.0f, 0, isSelected ? 2.0f : 1.0f);

                        // Render text inside selectable card area using draw list (prevents SetCursorPos assertion)
                        dl->AddText(ImVec2(pMin.x + 12.0f, pMin.y + 6.0f), IM_COL32(50, 204, 255, 255), p.name.c_str());
                        std::string pathText = "Path: " + p.rootPath;
                        dl->AddText(ImVec2(pMin.x + 12.0f, pMin.y + 24.0f), IM_COL32(150, 150, 160, 255), pathText.c_str());

                        if (!p.lastOpened.empty()) {
                            ImVec2 lastOpenedSize = ImGui::CalcTextSize(p.lastOpened.c_str());
                            dl->AddText(ImVec2(pMax.x - lastOpenedSize.x - 16.0f, pMin.y + 14.0f), IM_COL32(150, 150, 160, 255), p.lastOpened.c_str());
                        }

                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Bottom actions
                bool hasSelection = (selectedIdx >= 0 && selectedIdx < (int)projects.size());
                if (!hasSelection) ImGui::BeginDisabled();
                if (ImGui::Button("Open Selected Project", ImVec2(200, 34))) {
                    if (hasSelection) {
                        selectedProject = projects[selectedIdx].rootPath;
                    }
                }
                if (!hasSelection) ImGui::EndDisabled();

                ImGui::SameLine();
                ImGui::TextDisabled("(Double-click a project to open immediately)");

                ImGui::SameLine((float)BROWSER_W - 140.0f);
                if (ImGui::Button("Quit", ImVec2(100, 34))) {
                    glfwSetWindowShouldClose(browserWindow, GLFW_TRUE);
                }

            } else {
                // ============= NEW PROJECT TAB =============
                ImGui::TextColored(ImVec4(0.12f, 0.68f, 1.00f, 1.0f), "Create New Game Project");
                ImGui::TextDisabled("Set the project name and location. An empty project with Content folder will be created.");
                ImGui::Spacing();

                ImGui::Text("Project Name:");
                ImGui::SetNextItemWidth(360.0f);
                ImGui::InputText("##NewName", newNameBuf, sizeof(newNameBuf));
                ImGui::Spacing();

                ImGui::Text("Project Location (Parent Folder):");
                ImGui::SetNextItemWidth(540.0f);
                ImGui::InputText("##NewLoc", newPathBuf, sizeof(newPathBuf));
                ImGui::SameLine();
                if (ImGui::Button("Browse...", ImVec2(120, 0))) {
                    std::string picked = ShowSelectFolderDialog(nullptr, "Select Folder for New Project");
                    if (!picked.empty()) {
                        strncpy(newPathBuf, picked.c_str(), sizeof(newPathBuf) - 1);
                    }
                }
                ImGui::Spacing();

                // Preview
                std::filesystem::path targetDir = std::filesystem::path(newPathBuf) / newNameBuf;
                ImGui::BeginChild("Preview", ImVec2(0, 140), true);
                ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.25f, 1.0f), "Project Structure:");
                ImGui::BulletText("Project Directory: %s", targetDir.string().c_str());
                ImGui::BulletText("Content Root: %s", (targetDir / "Content").string().c_str());
                ImGui::BulletText("Build Output: %s", (targetDir / "Build").string().c_str());
                ImGui::BulletText("Template: Eunoia Project Template (Default Level & Configs)");
                ImGui::EndChild();
                ImGui::Spacing();

                // Validation
                bool nameValid = strlen(newNameBuf) > 0;
                bool pathValid = strlen(newPathBuf) > 0;
                bool targetExists = std::filesystem::exists(targetDir, ec);

                if (!nameValid) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Please enter a project name.");
                } else if (!pathValid) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Please select a valid directory.");
                } else if (targetExists) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Folder already exists. Will initialize/load project.");
                } else {
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Ready to create empty project.");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                bool canCreate = nameValid && pathValid;
                if (!canCreate) ImGui::BeginDisabled();
                if (ImGui::Button("Create Project", ImVec2(160, 36))) {
                    shouldCreateNew = true;
                    createParentDir = newPathBuf;
                    createProjName = newNameBuf;
                }
                if (!canCreate) ImGui::EndDisabled();

                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100, 36))) {
                    activeTab = 0;
                }

                ImGui::SameLine((float)BROWSER_W - 140.0f);
                if (ImGui::Button("Quit", ImVec2(100, 36))) {
                    glfwSetWindowShouldClose(browserWindow, GLFW_TRUE);
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);

        ImGui::Render();

        int fbW, fbH;
        glfwGetFramebufferSize(browserWindow, &fbW, &fbH);

        // We need the OpenGL functions. Since we use the ImGui OpenGL3 loader, gl functions are available.
        typedef void (*PFNGLVIEWPORTPROC)(int, int, int, int);
        typedef void (*PFNGLCLEARCOLORPROC)(float, float, float, float);
        typedef void (*PFNGLCLEARPROC)(unsigned int);
        auto glViewportFn = (PFNGLVIEWPORTPROC)glfwGetProcAddress("glViewport");
        auto glClearColorFn = (PFNGLCLEARCOLORPROC)glfwGetProcAddress("glClearColor");
        auto glClearFn = (PFNGLCLEARPROC)glfwGetProcAddress("glClear");

        if (glViewportFn) glViewportFn(0, 0, fbW, fbH);
        if (glClearColorFn) glClearColorFn(0.118f, 0.118f, 0.118f, 1.00f);
        if (glClearFn) glClearFn(0x00004000); // GL_COLOR_BUFFER_BIT
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(browserWindow);
    }

    // If user chose to create a new project, create the directory structure and return the path
    if (shouldCreateNew && !createProjName.empty() && !createParentDir.empty()) {
        std::filesystem::path root = std::filesystem::path(createParentDir) / createProjName;
        std::filesystem::path configsDir = root / "Configs";
        std::filesystem::path contentDir = root / "Content";
        std::filesystem::path levelsDir  = contentDir / "Levels";
        std::filesystem::path depsDir    = root / "Dependencies";
        std::filesystem::path intermDir  = root / "Intermediates";
        std::filesystem::path regDir     = root / "Registry";
        std::filesystem::path buildDir   = root / "Build";

        std::filesystem::create_directories(configsDir, ec);
        std::filesystem::create_directories(contentDir, ec);
        std::filesystem::create_directories(levelsDir, ec);
        std::filesystem::create_directories(depsDir, ec);
        std::filesystem::create_directories(intermDir, ec);
        std::filesystem::create_directories(regDir, ec);
        std::filesystem::create_directories(buildDir, ec);

        // Project descriptor file <createProjName>.euproject (and backward-compatible .eproject)
        std::filesystem::path projFileEu = root / (createProjName + ".euproject");
        std::filesystem::path projFileE  = root / (createProjName + ".eproject");
        nlohmann::json j;
        j["name"] = createProjName;
        j["version"] = "1.0.0";
        j["engineVersion"] = "0.1.0";
        j["contentDirectory"] = "Content";
        j["defaultLevel"] = "Content/Levels/Level.eulevel";
        j["activeLevel"] = "Content/Levels/Level.eulevel";
        j["defaultScene"] = "Content/Levels/Level.eulevel"; // backward-compat

        if (!std::filesystem::exists(projFileEu, ec)) {
            std::ofstream pf(projFileEu);
            if (pf.is_open()) {
                pf << j.dump(4);
                pf.close();
            }
        }
        if (!std::filesystem::exists(projFileE, ec)) {
            std::ofstream pf(projFileE);
            if (pf.is_open()) {
                pf << j.dump(4);
                pf.close();
            }
        }

        // Starter level in Content/Levels/Level.eulevel (completely clean & empty, AO and Post Processing disabled)
        std::filesystem::path starterLevelPath = levelsDir / "Level.eulevel";
        if (!std::filesystem::exists(starterLevelPath, ec)) {
            Level starterLevel;
            starterLevel.Clear();
            starterLevel.enableAO = false;
            starterLevel.aoType = 0;
            starterLevel.enablePostProcessing = false;
            LevelSerializer::SaveLevel(starterLevel, starterLevelPath.string());
        }

        // Initial Configs.Editor.econfigs in project root and Configs/
        std::filesystem::path cfgPath = root / "Configs.Editor.econfigs";
        std::filesystem::path cfgPathSub = configsDir / "Configs.Editor.econfigs";
        if (!std::filesystem::exists(cfgPath, ec)) {
            std::ofstream cf(cfgPath);
            if (cf.is_open()) {
                cf << "[EditorLayout]\n";
                cf << "LeftSidebarWidth=280.0\n";
                cf << "RightSidebarWidth=320.0\n";
                cf << "BottomDockHeight=260.0\n";
                cf << "ContentBrowserFolderTreeWidth=220.0\n";
                cf << "ContentThumbnailSize=96.0\n";
                cf.close();
            }
        }
        if (!std::filesystem::exists(cfgPathSub, ec)) {
            std::filesystem::copy_file(cfgPath, cfgPathSub, std::filesystem::copy_options::overwrite_existing, ec);
        }

        // Update recent projects config
        {
            time_t now = time(nullptr);
            char timeBuf[64];
            strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M", localtime(&now));

            std::filesystem::path recentCfg = GetRecentProjectsConfigPath();

            // Read existing, prepend new entry, save
            nlohmann::json arr = nlohmann::json::array();
            nlohmann::json newEntry;
            newEntry["name"] = createProjName;
            newEntry["rootPath"] = root.string();
            newEntry["lastOpened"] = timeBuf;
            arr.push_back(newEntry);

            // Copy over existing entries (skip duplicates)
            if (std::filesystem::exists(recentCfg, ec)) {
                std::ifstream existingFile(recentCfg);
                if (existingFile.is_open()) {
                    try {
                        nlohmann::json existing;
                        existingFile >> existing;
                        if (existing.is_array()) {
                            for (const auto& e : existing) {
                                if (e.contains("rootPath") && e["rootPath"].get<std::string>() != root.string()) {
                                    arr.push_back(e);
                                }
                            }
                        }
                    } catch (...) {}
                }
            }

            std::filesystem::create_directories(recentCfg.parent_path(), ec);
            std::ofstream cfgOut(recentCfg);
            if (cfgOut.is_open()) {
                cfgOut << arr.dump(4);
                cfgOut.close();
            }
        }

        selectedProject = root;
    }

    // If user closed the window or clicked Quit without selecting:
    if (selectedProject.empty()) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(browserCtx);
        glfwDestroyWindow(browserWindow);
        glfwDefaultWindowHints();
        return {};
    }

    // Project selected ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â clean up the project browser window and return directly.
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(browserCtx);
    glfwDestroyWindow(browserWindow);
    glfwDefaultWindowHints();

    return selectedProject;
}

void EngineUI::OpenReferenceViewer(const AssetID& id) {
    showReferenceViewer = true;
    if (id.IsValid()) {
        refViewerSelectedAsset = id;
    } else if (!AssetRegistry::Get().GetAllAssets().empty()) {
        refViewerSelectedAsset = AssetRegistry::Get().GetAllAssets().begin()->first;
    }
}

void EngineUI::AddLog(const std::string& category, const std::string& message, int level) {
    logs.push_back({ category, message, level });
    if (logs.size() > 250) {
        logs.erase(logs.begin(), logs.begin() + 50);
    }
}

void EngineUI::ToggleGameView(Scene& scene) {
    isGameView = !isGameView;
    if (isGameView) {
        prevShowGrid = scene.showGrid;
        prevShowGizmo = showGizmo;
        scene.showGrid = false;
        showGizmo = false;
        AddLog("LogViewport", "Game View Enabled [G]", 0);
    } else {
        scene.showGrid = prevShowGrid;
        showGizmo = prevShowGizmo;
        AddLog("LogViewport", "Game View Disabled [G]", 0);
    }
}

void EngineUI::ToggleImmersiveMode() {
    isImmersiveMode = !isImmersiveMode;
    if (isImmersiveMode) {
        prevShowOutliner = showOutliner;
        prevShowDetails = showDetails;
        prevShowBottomDrawer = showBottomDrawer;
        showOutliner = false;
        showDetails = false;
        showBottomDrawer = false;
        AddLog("LogViewport", "Immersive Viewport Mode Enabled [F11]", 0);
    } else {
        showOutliner = prevShowOutliner;
        showDetails = prevShowDetails;
        showBottomDrawer = prevShowBottomDrawer;
        AddLog("LogViewport", "Immersive Viewport Mode Restored [F11]", 0);
    }
}

void EngineUI::ResetTreeStateApplication() {
    m_outlinerAppliedNodes.clear();
    m_cbAppliedFolders.clear();
    m_outlinerStateNeedsApply = true;
    m_cbFolderStateNeedsApply = true;
}

bool EngineUI::SaveLevel(Scene& level) {
    if (currentLevelFilePath.empty()) {
        return SaveLevelAs(level);
    }
    std::string filename = std::filesystem::path(currentLevelFilePath).filename().string();
    m_saveIndicator.active = true;
    m_saveIndicator.isSaving = true;
    m_saveIndicator.success = true;
    m_saveIndicator.filename = filename;
    m_saveIndicator.displayTimer = 2.5f;

    m_levelSaveState.isSaving = true;
    m_levelSaveState.progress = 0.10f;
    m_levelSaveState.tabName = "Level Editor";

    // Snapshot scene to prevent runtime state/destruction or UI changes from corrupting level
    std::shared_ptr<Scene> sceneSnapshot = std::make_shared<Scene>();
    *sceneSnapshot = level;
    if (level.isPlayMode) {
        sceneSnapshot->objects = level.playModePreObjects;
        sceneSnapshot->selectedId = level.playModePreSelectedId;
        sceneSnapshot->isPlayMode = false;
    }

    std::string savePath = currentLevelFilePath;
    std::filesystem::path projRoot = activeProjectRoot;
    std::string projName = activeProjectName;

    std::thread([this, sceneSnapshot, savePath, projRoot, projName]() {
        bool ok = SceneSerializer::SaveScene(*sceneSnapshot, savePath, [this](float p, const std::string& step, const std::string& detail) {
            m_levelSaveState.progress = std::max(0.1f, p);
        });

        m_levelSaveState.isSaving = false;
        m_levelSaveState.progress = 1.0f;
        m_levelSaveState.fadeTimer = 1.5f;
        levelUnsaved = !ok;

        if (ok) {
            m_saveIndicator.isSaving = false;
            m_saveIndicator.success = true;
            m_saveIndicator.displayTimer = 2.5f;
            lastOpenedLevelConfig = savePath;
            SaveEditorConfig();

            // Persist activeLevel in project descriptor (.euproject / .eproject)
            if (!projRoot.empty() && !projName.empty()) {
                std::error_code ec;
                std::vector<std::filesystem::path> projFiles = {
                    projRoot / (projName + ".euproject"),
                    projRoot / (projName + ".eproject")
                };
                for (const auto& pf : projFiles) {
                    if (std::filesystem::exists(pf, ec)) {
                        try {
                            nlohmann::json pj;
                            {
                                std::ifstream fin(pf);
                                if (fin.is_open()) fin >> pj;
                            }
                            std::filesystem::path p(savePath);
                            std::string rel = savePath;
                            if (savePath.rfind(projRoot.string(), 0) == 0) {
                                rel = std::filesystem::relative(p, projRoot, ec).generic_string();
                            }
                            pj["activeLevel"] = rel;
                            {
                                std::ofstream fout(pf);
                                if (fout.is_open()) fout << pj.dump(4);
                            }
                        } catch (...) {}
                    }
                }
            }

            AddLog("LogWorld", "Saved Level reliably in background to: " + savePath, 2);
        } else {
            m_saveIndicator.isSaving = false;
            m_saveIndicator.success = false;
            m_saveIndicator.displayTimer = 3.0f;
            AddLog("LogWorld", "Failed to save Level to: " + savePath, 3);
        }
    }).detach();

    return true;
}

void EngineUI::OpenSaveLevelModal(Scene& scene) {
    saveLevelTargetScene = &scene;
    showSaveLevelModal = true;
    showSaveLevelNewFolderInput = false;
    saveLevelNewFolderBuf[0] = '\0';
    saveLevelErrorMsg = "";

    // Resolve contentRootPath
    std::filesystem::path rootDir = contentRootPath;
    std::error_code ec;
    if (rootDir.empty() || !std::filesystem::exists(rootDir, ec)) {
        if (!activeProjectRoot.empty()) {
            rootDir = activeProjectRoot / "Content";
        }
    }
    if (rootDir.empty() || !std::filesystem::exists(rootDir, ec)) {
        rootDir = std::filesystem::current_path() / "Content";
    }
    std::filesystem::create_directories(rootDir, ec);

    // If a level is already loaded, default to its parent folder (if within Content) and its stem name
    if (!currentLevelFilePath.empty()) {
        std::filesystem::path currentP(currentLevelFilePath);
        std::string stem = currentP.stem().string();
        strncpy_s(saveLevelNameBuf, sizeof(saveLevelNameBuf), stem.c_str(), sizeof(saveLevelNameBuf) - 1);

        std::filesystem::path parentP = currentP.parent_path();
        std::string rel = std::filesystem::relative(parentP, rootDir, ec).generic_string();
        if (!ec && rel.find("..") == std::string::npos && std::filesystem::exists(parentP, ec)) {
            saveLevelCurrentFolder = parentP;
        } else {
            std::filesystem::path levelsDir = rootDir / "Levels";
            if (std::filesystem::exists(levelsDir, ec)) {
                saveLevelCurrentFolder = levelsDir;
            } else {
                saveLevelCurrentFolder = rootDir;
            }
        }
    } else {
        strncpy_s(saveLevelNameBuf, sizeof(saveLevelNameBuf), "NewLevel", sizeof(saveLevelNameBuf) - 1);
        std::filesystem::path levelsDir = rootDir / "Levels";
        if (std::filesystem::exists(levelsDir, ec)) {
            saveLevelCurrentFolder = levelsDir;
        } else {
            saveLevelCurrentFolder = rootDir;
        }
    }
}

void EngineUI::RenderSaveLevelModal() {
    if (!showSaveLevelModal) return;

    ImGui::OpenPopup("Save Level As###SaveLevelModalWindow");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(720.0f, 500.0f), ImGuiCond_Appearing);

    std::error_code ec;
    std::filesystem::path rootDir = contentRootPath;
    if (rootDir.empty() || !std::filesystem::exists(rootDir, ec)) {
        if (!activeProjectRoot.empty()) rootDir = activeProjectRoot / "Content";
        else rootDir = std::filesystem::current_path() / "Content";
        std::filesystem::create_directories(rootDir, ec);
    }

    // Safety clamp: saveLevelCurrentFolder must exist and be inside rootDir
    if (saveLevelCurrentFolder.empty() || !std::filesystem::exists(saveLevelCurrentFolder, ec)) {
        saveLevelCurrentFolder = rootDir;
    } else {
        std::string rel = std::filesystem::relative(saveLevelCurrentFolder, rootDir, ec).generic_string();
        if (ec || rel.find("..") != std::string::npos) {
            saveLevelCurrentFolder = rootDir;
        }
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::BeginPopupModal("Save Level As###SaveLevelModalWindow", &showSaveLevelModal, flags)) {
        ImGui::TextColored(ImVec4(0.12f, 0.68f, 1.00f, 1.0f), "[Eunoia Engine] Save Level File");
        ImGui::SameLine();
        ImGui::TextDisabled("(Root: Content/)");
        ImGui::Separator();

        // Top Navigation Bar
        bool isAtRoot = (saveLevelCurrentFolder == rootDir);
        if (isAtRoot) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button(" Ã¢Â¬â€  Up ")) {
            std::filesystem::path parent = saveLevelCurrentFolder.parent_path();
            std::string rel = std::filesystem::relative(parent, rootDir, ec).generic_string();
            if (!ec && rel.find("..") == std::string::npos && std::filesystem::exists(parent, ec)) {
                saveLevelCurrentFolder = parent;
            } else {
                saveLevelCurrentFolder = rootDir;
            }
        }
        if (isAtRoot) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        ImGui::Text("Path:");
        ImGui::SameLine();

        // Breadcrumbs: Content / Sub / Sub2
        if (ImGui::SmallButton("Content")) {
            saveLevelCurrentFolder = rootDir;
        }

        std::filesystem::path currentRel = std::filesystem::relative(saveLevelCurrentFolder, rootDir, ec);
        if (!ec && !currentRel.empty() && currentRel.string() != ".") {
            std::filesystem::path accum = rootDir;
            for (auto it = currentRel.begin(); it != currentRel.end(); ++it) {
                ImGui::SameLine();
                ImGui::TextDisabled("/");
                ImGui::SameLine();
                accum /= *it;
                std::string partName = it->string();
                ImGui::PushID(accum.string().c_str());
                if (ImGui::SmallButton(partName.c_str())) {
                    saveLevelCurrentFolder = accum;
                }
                ImGui::PopID();
            }
        }

        // New folder toggle button on right side of top bar
        float availWidth = ImGui::GetContentRegionAvail().x;
        float btnWidth = 110.0f;
        if (availWidth > btnWidth) {
            ImGui::SameLine(ImGui::GetCursorPosX() + availWidth - btnWidth);
        }
        if (ImGui::Button("+ New Folder", ImVec2(btnWidth, 0))) {
            showSaveLevelNewFolderInput = !showSaveLevelNewFolderInput;
            if (showSaveLevelNewFolderInput) {
                saveLevelNewFolderBuf[0] = '\0';
                saveLevelErrorMsg = "";
            }
        }

        // Inline New Folder Input
        if (showSaveLevelNewFolderInput) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.17f, 0.20f, 1.0f));
            if (ImGui::BeginChild("NewFolderInlineBar", ImVec2(0, 36), true, ImGuiWindowFlags_NoScrollbar)) {
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Folder Name:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200.0f);
                bool enterFolder = ImGui::InputText("##NewFolderName", saveLevelNewFolderBuf, sizeof(saveLevelNewFolderBuf), ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                bool createClicked = ImGui::Button("Create");
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    showSaveLevelNewFolderInput = false;
                    saveLevelNewFolderBuf[0] = '\0';
                }

                if (enterFolder || createClicked) {
                    std::string folderName = saveLevelNewFolderBuf;
                    // Trim whitespace
                    while (!folderName.empty() && (folderName.front() == ' ' || folderName.front() == '\t')) folderName.erase(0, 1);
                    while (!folderName.empty() && (folderName.back() == ' ' || folderName.back() == '\t')) folderName.pop_back();

                    if (folderName.empty()) {
                        saveLevelErrorMsg = "Folder name cannot be empty.";
                    } else if (folderName.find_first_of("\\/:*?\"<>|") != std::string::npos) {
                        saveLevelErrorMsg = "Folder name contains invalid characters (\\/:*?\"<>|).";
                    } else {
                        std::filesystem::path newDir = saveLevelCurrentFolder / folderName;
                        if (std::filesystem::exists(newDir, ec)) {
                            saveLevelErrorMsg = "Folder already exists.";
                            saveLevelCurrentFolder = newDir;
                            showSaveLevelNewFolderInput = false;
                        } else {
                            if (std::filesystem::create_directories(newDir, ec)) {
                                saveLevelCurrentFolder = newDir;
                                showSaveLevelNewFolderInput = false;
                                saveLevelNewFolderBuf[0] = '\0';
                                saveLevelErrorMsg = "";
                                TriggerBackgroundRegistrySync(contentRootPath);
                                AddLog("LogAsset", "Created folder: " + newDir.string(), 0);
                            } else {
                                saveLevelErrorMsg = "Failed to create directory.";
                            }
                        }
                    }
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        // Explorer view of current folder
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
        if (ImGui::BeginChild("SaveLevelFolderContentPane", ImVec2(0, -90.0f), true)) {
            // Collect directories and level files
            struct EntryItem {
                bool isDir;
                std::string name;
                std::filesystem::path fullPath;
            };
            std::vector<EntryItem> dirs;
            std::vector<EntryItem> files;

            try {
                for (const auto& entry : std::filesystem::directory_iterator(saveLevelCurrentFolder)) {
                    if (entry.is_directory()) {
                        dirs.push_back({ true, entry.path().filename().string(), entry.path() });
                    } else if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".elevel" || ext == ".escene" || ext == ".level") {
                            files.push_back({ false, entry.path().filename().string(), entry.path() });
                        }
                    }
                }
            } catch (...) {}

            std::sort(dirs.begin(), dirs.end(), [](const EntryItem& a, const EntryItem& b) {
                return a.name < b.name;
            });
            std::sort(files.begin(), files.end(), [](const EntryItem& a, const EntryItem& b) {
                return a.name < b.name;
            });

            if (dirs.empty() && files.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("  (Empty Folder)");
            }

            // Render Directories
            for (const auto& d : dirs) {
                std::string label = "[Folder] " + d.name;
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        saveLevelCurrentFolder = d.fullPath;
                        break;
                    }
                }
            }

            // Render Existing Levels
            for (const auto& f : files) {
                std::string label = "[Level]  " + f.name;
                bool isSelected = (f.fullPath.stem().string() == saveLevelNameBuf);
                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    strncpy_s(saveLevelNameBuf, sizeof(saveLevelNameBuf), f.fullPath.stem().string().c_str(), sizeof(saveLevelNameBuf) - 1);
                    saveLevelErrorMsg = "";
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        // Double click on level file triggers save/overwrite immediately
                        std::filesystem::path targetFile = saveLevelCurrentFolder / (std::string(saveLevelNameBuf) + ".elevel");
                        currentLevelFilePath = targetFile.string();
                        if (saveLevelTargetScene) {
                            SaveLevel(*saveLevelTargetScene);
                        }
                        TriggerBackgroundRegistrySync(contentRootPath);
                        showSaveLevelModal = false;
                        ImGui::CloseCurrentPopup();
                        break;
                    }
                }
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        // Bottom Bar
        ImGui::Separator();

        // Destination display
        std::string relFolder = std::filesystem::relative(saveLevelCurrentFolder, rootDir, ec).generic_string();
        if (relFolder == "." || relFolder.empty()) relFolder = "";
        std::string targetRelPath = "Content/" + (relFolder.empty() ? "" : (relFolder + "/")) + saveLevelNameBuf + ".elevel";
        ImGui::TextDisabled("Save To: %s", targetRelPath.c_str());

        // Error message if any
        if (!saveLevelErrorMsg.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", saveLevelErrorMsg.c_str());
        }

        // Input field and Save/Cancel buttons
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Level Name:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(320.0f);
        bool enterPressed = ImGui::InputText("##SaveLevelNameInput", saveLevelNameBuf, sizeof(saveLevelNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::TextDisabled(".elevel");

        float btnAreaWidth = 190.0f;
        float rightAvail = ImGui::GetContentRegionAvail().x;
        if (rightAvail > btnAreaWidth) {
            ImGui::SameLine(ImGui::GetCursorPosX() + rightAvail - btnAreaWidth);
        }

        auto DoSave = [this, &rootDir, &ec]() {
            std::string name = saveLevelNameBuf;
            while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.erase(0, 1);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();

            if (name.empty()) {
                saveLevelErrorMsg = "Please enter a valid level name.";
                return;
            }
            if (name.find_first_of("\\/:*?\"<>|") != std::string::npos) {
                saveLevelErrorMsg = "Level name contains invalid characters (\\/:*?\"<>|).";
                return;
            }

            std::filesystem::path targetFile = saveLevelCurrentFolder / (name + ".elevel");
            currentLevelFilePath = targetFile.string();

            if (saveLevelTargetScene) {
                SaveLevel(*saveLevelTargetScene);
            }
            TriggerBackgroundRegistrySync(contentRootPath);
            showSaveLevelModal = false;
            ImGui::CloseCurrentPopup();
            AddLog("LogWorld", "Saving level as: " + targetFile.string(), 1);
        };

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.50f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.60f, 0.95f, 1.0f));
        if (ImGui::Button("Save Level", ImVec2(90, 0)) || enterPressed) {
            DoSave();
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            showSaveLevelModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

bool EngineUI::SaveLevelAs(Scene& level) {
    OpenSaveLevelModal(level);
    return true;
}

bool EngineUI::OpenLevel(Scene& level) {
    std::string path = ShowOpenFileDialog();
    if (path.empty()) return false;
    return OpenLevelFromPath(level, path);
}

bool EngineUI::OpenLevelFromPath(Scene& level, const std::string& filePath, bool isGameMode, const std::string& binPath) {
    std::string filename = std::filesystem::path(filePath).filename().string();
    currentLevelFilePath = filePath;
    lastOpenedLevelConfig = filePath;
    levelUnsaved = false;
    SaveEditorConfig();

    // Persist activeLevel in project descriptor (.euproject / .eproject)
    if (!activeProjectRoot.empty() && !activeProjectName.empty()) {
        std::error_code ec;
        std::vector<std::filesystem::path> projFiles = {
            activeProjectRoot / (activeProjectName + ".euproject"),
            activeProjectRoot / (activeProjectName + ".eproject")
        };
        for (const auto& pf : projFiles) {
            if (std::filesystem::exists(pf, ec)) {
                try {
                    nlohmann::json pj;
                    {
                        std::ifstream fin(pf);
                        if (fin.is_open()) fin >> pj;
                    }
                    std::filesystem::path p(filePath);
                    std::string rel = filePath;
                    if (filePath.rfind(activeProjectRoot.string(), 0) == 0) {
                        rel = std::filesystem::relative(p, activeProjectRoot, ec).generic_string();
                    }
                    pj["activeLevel"] = rel;
                    {
                        std::ofstream fout(pf);
                        if (fout.is_open()) fout << pj.dump(4);
                    }
                } catch (...) {}
            }
        }
    }

    // Reset applied tree sets so loaded tree states restore smoothly across hierarchy
    ResetTreeStateApplication();

    // Resolve actual disk path: if filePath is a 0-byte dummy reference in Content/, load from Cooked/Content/
    std::string actualLoadPath = filePath;
    {
        std::error_code ec;
        if (!std::filesystem::exists(actualLoadPath, ec) || std::filesystem::file_size(actualLoadPath, ec) == 0) {
            std::filesystem::path resolved = AssetManager::Get().ResolvePhysicalOrCookedPath(filePath);
            if (std::filesystem::exists(resolved, ec) && std::filesystem::file_size(resolved, ec) > 0) {
                actualLoadPath = resolved.string();
            } else if (!activeProjectRoot.empty()) {
                std::filesystem::path cookedCand = activeProjectRoot / "Cooked" / "Content" / std::filesystem::path(filePath).filename();
                if (std::filesystem::exists(cookedCand, ec) && std::filesystem::file_size(cookedCand, ec) > 0) {
                    actualLoadPath = cookedCand.string();
                }
            }
        }
    }

    // WickedEngine Loading System Technique:
    // Non-blocking asynchronous task execution with progress reporting & fast binary archive cache
    m_progressiveLoader.Start(level, actualLoadPath, binPath);
    AddLog("LogWorld", "Opened Level: " + filePath + (actualLoadPath != filePath ? (" [Cooked Binary: " + actualLoadPath + "]") : "") + " (loading asynchronously via WickedEngine task pipeline)", 2);
    return true;
}

std::string EngineUI::ShowSelectFolderDialog(void* owner, const std::string& title) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    BROWSEINFOA bi = { 0 };
    bi.hwndOwner = (HWND)owner;
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    std::string result = "";
    if (pidl != 0) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path)) {
            result = std::string(path);
        }
        IMalloc* imalloc = nullptr;
        if (SUCCEEDED(SHGetMalloc(&imalloc)) && imalloc) {
            imalloc->Free(pidl);
            imalloc->Release();
        }
    }
    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }
    return result;
}

std::string EngineUI::ShowOpenProjectFileDialog() {
    OPENFILENAMEA ofn;
    char szFile[260] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Eunoia Project (*.euproject;*.eproject)\0*.euproject;*.eproject\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    std::string initDir = (g_pEngineUI && !g_pEngineUI->activeProjectRoot.empty()) ? g_pEngineUI->activeProjectRoot.string() : "C:\\Projects\\Eunoia-Engine\\Projects";
    ofn.lpstrInitialDir = initDir.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
    return "";
}

void EngineUI::LoadRecentProjects() {
    recentProjects.clear();
    std::error_code ec;

    // 1. Try reading from config file
    std::filesystem::path cfgPath = GetRecentProjectsConfigPath();
    if (std::filesystem::exists(cfgPath, ec)) {
        std::ifstream f(cfgPath);
        if (f.is_open()) {
            try {
                nlohmann::json j;
                f >> j;
                if (j.is_array()) {
                    for (const auto& item : j) {
                        if (item.contains("rootPath")) {
                            ProjectEntry pe;
                            pe.rootPath = item["rootPath"].get<std::string>();
                            pe.name = item.value("name", std::filesystem::path(pe.rootPath).filename().string());
                            pe.lastOpened = item.value("lastOpened", "");
                            if (std::filesystem::exists(pe.rootPath, ec)) {
                                recentProjects.push_back(pe);
                            }
                        }
                    }
                }
            } catch (...) {}
        }
    }

    // 2. Discover existing projects in known directories
    std::vector<std::filesystem::path> searchDirs = {
        "C:\\Projects\\Eunoia-Engine\\Projects",
        std::filesystem::current_path() / "Projects",
        std::filesystem::current_path().parent_path() / "Projects"
    };

    for (const auto& dir : searchDirs) {
        if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.is_directory(ec)) {
                std::filesystem::path pRoot = entry.path();
                bool alreadyIn = false;
                for (const auto& r : recentProjects) {
                    if (std::filesystem::equivalent(std::filesystem::path(r.rootPath), pRoot, ec) || r.rootPath == pRoot.string()) {
                        alreadyIn = true;
                        break;
                    }
                }
                if (!alreadyIn) {
                    bool isProj = false;
                    for (const auto& sub : std::filesystem::directory_iterator(pRoot, ec)) {
                        if (sub.path().extension() == ".euproject" || sub.path().extension() == ".eproject" || sub.path().filename() == "Content" || sub.path().filename() == "Levels" || sub.path().filename() == "Scenes" || sub.path().filename() == "Materials") {
                            isProj = true;
                            break;
                        }
                    }
                    if (isProj) {
                        ProjectEntry pe;
                        pe.name = pRoot.filename().string();
                        pe.rootPath = pRoot.string();
                        pe.lastOpened = "Discovered on disk";
                        recentProjects.push_back(pe);
                    }
                }
            }
        }
    }
}

void EngineUI::SaveRecentProjects() {
    std::filesystem::path cfgPath = GetRecentProjectsConfigPath();
    std::ofstream f(cfgPath);
    if (!f.is_open()) return;

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& p : recentProjects) {
        nlohmann::json item;
        item["name"] = p.name;
        item["rootPath"] = p.rootPath;
        item["lastOpened"] = p.lastOpened;
        arr.push_back(item);
    }
    f << arr.dump(4);
    f.close();
}

bool EngineUI::CreateNewProject(const std::string& parentDir, const std::string& projName, Scene& scene, OrbitCamera& camera) {
    if (projName.empty()) {
        AddLog("LogProject", "Failed to create project: Project name cannot be empty!", 3);
        return false;
    }
    if (parentDir.empty()) {
        AddLog("LogProject", "Failed to create project: Directory path cannot be empty!", 3);
        return false;
    }

    std::filesystem::path root = std::filesystem::path(parentDir) / projName;
    std::error_code ec;

    // 1. Create project directories matching Eunoia Project Template
    std::filesystem::path configsDir = root / "Configs";
    std::filesystem::path contentDir = root / "Content";
    std::filesystem::path levelsDir  = contentDir / "Levels";
    std::filesystem::path depsDir    = root / "Dependencies";
    std::filesystem::path intermDir  = root / "Intermediates";
    std::filesystem::path regDir     = root / "Registry";
    std::filesystem::path buildDir   = root / "Build";

    std::filesystem::create_directories(configsDir, ec);
    std::filesystem::create_directories(contentDir, ec);
    std::filesystem::create_directories(levelsDir, ec);
    std::filesystem::create_directories(depsDir, ec);
    std::filesystem::create_directories(intermDir, ec);
    std::filesystem::create_directories(regDir, ec);
    std::filesystem::create_directories(buildDir, ec);

    // 2. Starter level in Content/Levels/Level.eulevel (completely clean & empty, AO and Post Processing disabled)
    std::filesystem::path starterLevelPath = levelsDir / "Level.eulevel";
    if (!std::filesystem::exists(starterLevelPath, ec)) {
        Level starterLevel;
        starterLevel.Clear();
        starterLevel.enableAO = false;
        starterLevel.aoType = 0;
        starterLevel.enablePostProcessing = false;
        LevelSerializer::SaveLevel(starterLevel, starterLevelPath.string());
    }

    // 3. Project descriptor files <ProjectName>.euproject & <ProjectName>.eproject
    std::filesystem::path projFileEu = root / (projName + ".euproject");
    std::filesystem::path projFileE  = root / (projName + ".eproject");
    nlohmann::json j;
    j["name"] = projName;
    j["version"] = "1.0.0";
    j["engineVersion"] = "0.1.0";
    j["contentDirectory"] = "Content";
    j["defaultLevel"] = "Content/Levels/Level.eulevel";
    j["activeLevel"] = "Content/Levels/Level.eulevel";
    j["defaultScene"] = "Content/Levels/Level.eulevel"; // backward compatibility

    {
        std::ofstream pf(projFileEu);
        if (pf.is_open()) {
            pf << j.dump(4);
            pf.close();
        }
    }
    {
        std::ofstream pf(projFileE);
        if (pf.is_open()) {
            pf << j.dump(4);
            pf.close();
        }
    }

    // 5. Setup Project Dependencies & VS Code configuration
    SetupProjectDependencies(root);

    AddLog("LogProject", "Created new empty project: " + projName + " at " + root.string(), 2);
    return LoadProject(root, scene, camera);
}

bool EngineUI::LoadProject(const std::filesystem::path& projRoot, Scene& scene, OrbitCamera& camera, bool isGameMode, const std::string& specificScenePath, const std::string& specificBinPath) {
    std::error_code ec;
    if (!std::filesystem::exists(projRoot, ec)) {
        AddLog("LogProject", "Failed to open project: directory does not exist: " + projRoot.string(), 3);
        return false;
    }

    std::filesystem::path actualRoot = std::filesystem::absolute(projRoot);
    std::string explicitProjectFile = "";
    if (std::filesystem::is_regular_file(actualRoot, ec)) {
        std::string ext = actualRoot.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".euproject" || ext == ".eproject") {
            explicitProjectFile = actualRoot.string();
            actualRoot = actualRoot.parent_path();
        }
    }

    activeProjectRoot = actualRoot;
    activeProjectName = activeProjectRoot.filename().string();

    // Check project descriptor file for explicit project name
    std::filesystem::path projFile = !explicitProjectFile.empty() ? std::filesystem::path(explicitProjectFile) : (activeProjectRoot / (activeProjectName + ".euproject"));
    if (!std::filesystem::exists(projFile, ec)) {
        projFile = activeProjectRoot / (activeProjectName + ".eproject");
    }
    if (std::filesystem::exists(projFile, ec)) {
        std::ifstream pf(projFile);
        if (pf.is_open()) {
            try {
                nlohmann::json pj;
                pf >> pj;
                if (pj.contains("name") && !pj["name"].get<std::string>().empty()) {
                    activeProjectName = pj["name"].get<std::string>();
                }
            } catch (...) {}
        }
    }

    if (!isGameMode) {
        LoadEditorConfig();
        SetupProjectDependencies(activeProjectRoot);
    }

    // The in-engine-editor content browser root is inside project's folder called Content!
    contentRootPath = activeProjectRoot / "Content";
    if (!std::filesystem::exists(contentRootPath, ec)) {
        std::filesystem::create_directories(contentRootPath, ec);
    }
    currentContentPath = contentRootPath;
    currentVirtualDir = "/Game";
    selectedContentItem = "";

    // Ensure any running background sync thread finishes before wiping and swapping projects
    while (s_bgSync.isRunning.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (m_window) {
        std::string winTitle = "Eunoia Engine ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â [" + activeProjectName + "]";
        glfwSetWindowTitle(m_window, winTitle.c_str());
    }

    // Clear stale AssetRegistry cache from any previous project and re-initialize subsystems
    AssetRegistry::Get().Clear();
    TextureManager::Get().SetProjectRoot(activeProjectRoot);
    AssetManager::Get().Initialize(activeProjectRoot);
    AssetRegistry::Get().SetContentRoot(contentRootPath);

    // Single unified recursive directory walk for models, behaviour scripts, and scene discovery
    if (!isGameMode) {
        knownModelFiles.clear();
        pendingModelImportQueue.clear();
        showModelImportModal = false;
    }

    std::string targetScenePath = specificScenePath;
    if (targetScenePath.empty()) {
        // Priority 1: Check last opened level in editor configuration (Always open the last opened level on startup)
        if (!lastOpenedLevelConfig.empty()) {
            std::filesystem::path p(lastOpenedLevelConfig);
            if (std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec)) {
                targetScenePath = std::filesystem::absolute(p).string();
            } else {
                std::filesystem::path relP1 = activeProjectRoot / lastOpenedLevelConfig;
                if (std::filesystem::exists(relP1, ec) && !std::filesystem::is_directory(relP1, ec)) {
                    targetScenePath = relP1.string();
                } else {
                    std::filesystem::path relP2 = contentRootPath / lastOpenedLevelConfig;
                    if (std::filesystem::exists(relP2, ec) && !std::filesystem::is_directory(relP2, ec)) {
                        targetScenePath = relP2.string();
                    }
                }
            }
        }

        // 1. Check GameConfig.json in Data/ or project descriptor files in project root
        std::vector<std::filesystem::path> cfgPaths = {
            activeProjectRoot / "Data" / "GameConfig.json",
            activeProjectRoot / "GameConfig.json",
            activeProjectRoot / (activeProjectName + ".euproject"),
            activeProjectRoot / (activeProjectName + ".eproject")
        };
        for (const auto& cfgP : cfgPaths) {
            if (std::filesystem::exists(cfgP, ec)) {
                std::ifstream cf(cfgP);
                if (cf.is_open()) {
                    try {
                        nlohmann::json pj;
                        cf >> pj;
                        std::string foundLvl;
                        if (pj.contains("startupLevel")) foundLvl = pj["startupLevel"].get<std::string>();
                        else if (pj.contains("activeLevel")) foundLvl = pj["activeLevel"].get<std::string>();
                        else if (pj.contains("defaultLevel")) foundLvl = pj["defaultLevel"].get<std::string>();
                        else if (pj.contains("defaultScene")) foundLvl = pj["defaultScene"].get<std::string>();

                        if (!foundLvl.empty()) {
                            std::filesystem::path resolvedLvl = activeProjectRoot / foundLvl;
                            if (std::filesystem::exists(resolvedLvl, ec)) {
                                targetScenePath = resolvedLvl.string();
                                break;
                            }
                            resolvedLvl = contentRootPath / foundLvl;
                            if (std::filesystem::exists(resolvedLvl, ec)) {
                                targetScenePath = resolvedLvl.string();
                                break;
                            }
                        }
                    } catch (...) {}
                }
            }
        }

        // 2. Fallbacks
        if (targetScenePath.empty()) {
            std::filesystem::path defLevelEu = contentRootPath / "Levels" / "Level.eulevel";
            std::filesystem::path defLevelE  = contentRootPath / "Levels" / "Level.elevel";
            std::filesystem::path mainLevel  = contentRootPath / "Levels" / "Main.elevel";
            std::filesystem::path mainScene  = contentRootPath / "Scenes" / "Main.escene";
            if (std::filesystem::exists(defLevelEu, ec)) {
                targetScenePath = defLevelEu.string();
            } else if (std::filesystem::exists(defLevelE, ec)) {
                targetScenePath = defLevelE.string();
            } else if (std::filesystem::exists(mainLevel, ec)) {
                targetScenePath = mainLevel.string();
            } else if (std::filesystem::exists(mainScene, ec)) {
                targetScenePath = mainScene.string();
            }
        }
    }

    BehaviourRegistry::Get().ClearProjectScripts();
    for (const auto& entry : std::filesystem::recursive_directory_iterator(contentRootPath, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_directory(ec)) {
            std::string ext = entry.path().extension().string();
            std::string extLower = ext;
            std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);

            // 1. Known 3D model formats
            if (!isGameMode && (extLower == ".fbx" || extLower == ".glb" || extLower == ".gltf" || extLower == ".obj")) {
                knownModelFiles.insert(std::filesystem::absolute(entry.path()).string());
            }
            // 2. Gameplay behaviour scripts
            else if (extLower == ".cpp") {
                std::string pStr = entry.path().string();
                std::string pLower = pStr;
                std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::tolower);
                if (pLower.find(".assetmeta") == std::string::npos && pLower.find(".meta") == std::string::npos) {
                    std::string stem = entry.path().stem().string();
                    if (stem != "Cube" && stem != "EngineUI" && stem != "EunoiaBehaviour" &&
                        stem != "TextureManager" && stem != "AssetRegistry" && stem != "AssetManager") {
                        BehaviourRegistry::Get().RegisterScriptFile(stem, pStr);
                    }
                }
            }
            // 3. Respective scene discovery if in game mode and not already specified
            else if (isGameMode && targetScenePath.empty() && (extLower == ".escene" || extLower == ".elevel")) {
                targetScenePath = entry.path().string();
            }
        }
    }

    if (isGameMode) {
        // Game mode: load scene immediately (no editor window)
        bool found = false;
        if (!targetScenePath.empty() && std::filesystem::exists(targetScenePath, ec)) {
            found = OpenLevelFromPath(scene, targetScenePath, true, specificBinPath);
        }
        if (!found) {
            scene.Clear();
            currentLevelFilePath = "";
        }
        isInitialSceneLoaded = true;
        pendingInitialSceneLoad = false;
    } else {
        // Editor mode: If target level exists in the project, load it so the project is immediately visible
        bool found = false;
        if (!targetScenePath.empty() && std::filesystem::exists(targetScenePath, ec)) {
            found = OpenLevelFromPath(scene, targetScenePath, false, specificBinPath);
        }
        if (!found) {
            scene.Clear();
            currentLevelFilePath = "";
        }
        levelUnsaved = false;
        pendingScenePath = "";
        pendingBinPath = "";
        pendingInitialSceneLoad = false;
        isInitialSceneLoaded = true;
        initialLoadFrameCountdown = 0;

        camera.target = glm::vec3(0.0f, 0.5f, 0.0f);
        camera.distance = 7.0f;
        camera.yaw = 45.0f;
        camera.pitch = 25.0f;
    }

    if (!isGameMode) {
        // Update recent projects history
        time_t now = time(nullptr);
        char timeBuf[64];
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M", localtime(&now));

        for (auto it = recentProjects.begin(); it != recentProjects.end();) {
            if (std::filesystem::equivalent(std::filesystem::path(it->rootPath), activeProjectRoot, ec) || it->rootPath == activeProjectRoot.string()) {
                it = recentProjects.erase(it);
            } else {
                ++it;
            }
        }

        ProjectEntry pe;
        pe.name = activeProjectName;
        pe.rootPath = activeProjectRoot.string();
        pe.lastOpened = timeBuf;
        recentProjects.insert(recentProjects.begin(), pe);
        SaveRecentProjects();
    }

    showProjectBrowser = false;
    if (!isGameMode) {
        AddLog("LogProject", "Opened project '" + activeProjectName + "' [Content Root: " + contentRootPath.string() + "]", 2);
        TriggerBackgroundRegistrySync(contentRootPath);
    }

    return true;
}

bool EngineUI::LoadPendingInitialScene(Scene& scene, OrbitCamera& camera, bool isGameMode) {
    if (!pendingInitialSceneLoad) return false;
    pendingInitialSceneLoad = false;

    bool found = false;
    std::error_code ec;
    if (!pendingScenePath.empty() && std::filesystem::exists(pendingScenePath, ec)) {
        found = OpenLevelFromPath(scene, pendingScenePath, isGameMode, pendingBinPath);
        if (found) {
            AddLog("LogScene", "Loaded respective level: " + pendingScenePath, 2);
        }
    }
    if (!found) {
        scene.Clear();
        currentLevelFilePath = "";
    }

    camera.target = glm::vec3(0.0f, 0.5f, 0.0f);
    camera.distance = 7.0f;
    camera.yaw = 45.0f;
    camera.pitch = 25.0f;

    isInitialSceneLoaded = true;
    return found;
}

std::string EngineUI::ShowSaveFileDialog() {
    OPENFILENAMEA ofn;
    char szFile[260] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Eunoia Level (*.elevel;*.escene;*.level)\0*.elevel;*.escene;*.level\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    std::string initDir = (g_pEngineUI && !g_pEngineUI->contentRootPath.empty()) ? g_pEngineUI->contentRootPath.string() : "C:\\Projects\\Eunoia-Engine\\Projects";
    ofn.lpstrInitialDir = initDir.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
    return "";
}

std::string EngineUI::ShowOpenFileDialog() {
    OPENFILENAMEA ofn;
    char szFile[260] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Eunoia Level (*.elevel;*.escene;*.level)\0*.elevel;*.escene;*.level\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    std::string initDir = (g_pEngineUI && !g_pEngineUI->contentRootPath.empty()) ? g_pEngineUI->contentRootPath.string() : "C:\\Projects\\Eunoia-Engine\\Projects";
    ofn.lpstrInitialDir = initDir.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
    return "";
}

std::string EngineUI::ShowOpenMeshDialog() {
    OPENFILENAMEA ofn;
    char szFile[260] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "3D Models (*.obj;*.gltf;*.glb;*.fbx)\0*.obj;*.gltf;*.glb;*.fbx\0FBX Model (*.fbx)\0*.fbx\0Wavefront OBJ (*.obj)\0*.obj\0glTF Model (*.gltf;*.glb)\0*.gltf;*.glb\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    std::string initDir = (g_pEngineUI && !g_pEngineUI->contentRootPath.empty()) ? g_pEngineUI->contentRootPath.string() : "C:\\Projects\\Eunoia-Engine\\Projects";
    ofn.lpstrInitialDir = initDir.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
    return "";
}

std::string EngineUI::ShowSaveMeshLocationDialog(const std::string& defaultFilename, const std::string& initialDir) {
    OPENFILENAMEA ofn;
    char szFile[260] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    if (!defaultFilename.empty()) {
        strncpy(szFile, defaultFilename.c_str(), sizeof(szFile) - 1);
    }
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "3D Models (*.obj;*.gltf;*.glb;*.fbx)\0*.obj;*.gltf;*.glb;*.fbx\0FBX Model (*.fbx)\0*.fbx\0Wavefront OBJ (*.obj)\0*.obj\0glTF Model (*.gltf;*.glb)\0*.gltf;*.glb\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = "Select Save Location in Project Folder";
    ofn.lpstrInitialDir = initialDir.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameA(&ofn) == TRUE) {
        return std::string(ofn.lpstrFile);
    }
    return "";
}

void EngineUI::ImportMeshWithSavePrompt(Scene& level) {
    std::string sourceMesh = ShowOpenMeshDialog();
    if (sourceMesh.empty()) return;

    std::filesystem::path srcPath(sourceMesh);
    std::string filename = srcPath.filename().string();

    // Default target directory inside project
    std::string diskSub = currentVirtualDir;
    if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
    if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
    std::filesystem::path defaultDir = contentRootPath / diskSub;
    std::error_code ec;
    if (defaultDir == contentRootPath) {
        std::filesystem::path modelsDir = contentRootPath / "Models";
        if (std::filesystem::exists(modelsDir, ec)) {
            defaultDir = modelsDir;
        }
    }

    std::string saveLoc = ShowSaveMeshLocationDialog(filename, defaultDir.string());
    if (saveLoc.empty()) {
        AddLog("LogMesh", "Mesh import cancelled by user.", 1);
        return;
    }

    std::filesystem::path destPath(saveLoc);
    std::filesystem::create_directories(destPath.parent_path(), ec);

    // Copy file into project location
    if (std::filesystem::absolute(srcPath) != std::filesystem::absolute(destPath)) {
        std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
        std::string ext = srcPath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        // Also copy companion .mtl or .bin if they exist
        if (ext == ".obj") {
            std::filesystem::path srcMtl = srcPath.parent_path() / (srcPath.stem().string() + ".mtl");
            if (std::filesystem::exists(srcMtl, ec)) {
                std::filesystem::path destMtl = destPath.parent_path() / (destPath.stem().string() + ".mtl");
                std::filesystem::copy_file(srcMtl, destMtl, std::filesystem::copy_options::overwrite_existing, ec);
            }
        } else if (ext == ".gltf") {
            std::filesystem::path srcBin = srcPath.parent_path() / (srcPath.stem().string() + ".bin");
            if (std::filesystem::exists(srcBin, ec)) {
                std::filesystem::path destBin = destPath.parent_path() / (destPath.stem().string() + ".bin");
                std::filesystem::copy_file(srcBin, destBin, std::filesystem::copy_options::overwrite_existing, ec);
            }
        }

        // Copy companion texture folders and files from source directory into project location
        std::filesystem::path srcDir = srcPath.parent_path();
        std::filesystem::path destDir = destPath.parent_path();
        std::string srcStem = srcPath.stem().string();

        std::vector<std::string> companionFolderNames = {
            "Textures", "textures", "Materials", "materials",
            srcStem + ".fbm", srcStem + "_textures", srcStem + "-textures", srcStem + "_Textures"
        };

        for (const auto& folderName : companionFolderNames) {
            std::filesystem::path srcFolder = srcDir / folderName;
            if (std::filesystem::exists(srcFolder, ec) && std::filesystem::is_directory(srcFolder, ec)) {
                std::filesystem::path destFolder = destDir / folderName;
                std::filesystem::create_directories(destFolder, ec);
                for (const auto& entry : std::filesystem::directory_iterator(srcFolder, ec)) {
                    if (entry.is_regular_file(ec)) {
                        std::filesystem::copy_file(entry.path(), destFolder / entry.path().filename(),
                            std::filesystem::copy_options::overwrite_existing, ec);
                    }
                }
            }
        }

        // Copy any loose image textures residing beside the 3D model
        std::vector<std::string> imgExts = { ".png", ".jpg", ".jpeg", ".tga", ".dds", ".bmp", ".webp", ".tif", ".tiff" };
        std::filesystem::path destTexDir = destDir / "Textures";
        for (const auto& entry : std::filesystem::directory_iterator(srcDir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            std::string fileExt = entry.path().extension().string();
            std::transform(fileExt.begin(), fileExt.end(), fileExt.begin(), ::tolower);
            for (const auto& ie : imgExts) {
                if (fileExt == ie) {
                    std::filesystem::create_directories(destTexDir, ec);
                    std::filesystem::copy_file(entry.path(), destTexDir / entry.path().filename(),
                        std::filesystem::copy_options::overwrite_existing, ec);
                    break;
                }
            }
        }
    }

    // Sync Asset Registry so Content Browser immediately has the new asset
    SyncRegistryWithUIProgress(contentRootPath);

    // Queue model for rich 3D inspection and material/texture auto-assignment
    QueueModelImport(destPath.string());
    bool hasAnimPrompt = false;
    if (Eunoia::SkeletalAnimationManager::FileHasBones(destPath.string(), &hasAnimPrompt)) {
        pendingBonesImportFilePath = destPath.string();
        bonesImportOptionType = 0;
        showBonesImportOptionPopup = true;
    }
}

void EngineUI::HandleFileDrop(const char** paths, int count) {
    if (!paths || count <= 0) return;

    std::string diskSub = currentVirtualDir;
    if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
    if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
    std::filesystem::path targetDir = contentRootPath / diskSub;
    std::error_code ec;
    std::filesystem::create_directories(targetDir, ec);

    int importedCount = 0;
    std::vector<std::string> modelsToQueue;

    for (int i = 0; i < count; ++i) {
        if (!paths[i]) continue;
        std::filesystem::path srcPath(paths[i]);
        if (!std::filesystem::exists(srcPath, ec)) continue;

        if (std::filesystem::is_directory(srcPath, ec)) {
            // User dropped an entire folder from Windows Explorer
            std::filesystem::path folderDest = targetDir / srcPath.filename();
            std::filesystem::create_directories(folderDest, ec);

            int folderFileCount = 0;
            auto options = std::filesystem::directory_options::skip_permission_denied;
            for (auto it = std::filesystem::recursive_directory_iterator(srcPath, options, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator();
                 it.increment(ec))
            {
                auto rel = std::filesystem::relative(it->path(), srcPath, ec);
                std::filesystem::path destItem = folderDest / rel;
                if (it->is_directory(ec)) {
                    std::filesystem::create_directories(destItem, ec);
                } else {
                    std::filesystem::create_directories(destItem.parent_path(), ec);
                    if (std::filesystem::absolute(it->path()) != std::filesystem::absolute(destItem)) {
                        std::filesystem::copy_file(it->path(), destItem, std::filesystem::copy_options::overwrite_existing, ec);
                    }
                    std::string fileExt = it->path().extension().string();
                    std::transform(fileExt.begin(), fileExt.end(), fileExt.begin(), ::tolower);
                    bool fileIsModel = (fileExt == ".obj" || fileExt == ".gltf" || fileExt == ".glb" || fileExt == ".fbx");
                    if (fileIsModel) {
                        modelsToQueue.push_back(destItem.string());
                    }
                    folderFileCount++;
                    importedCount++;
                }
            }
            AddLog("LogContent", "Imported folder: " + srcPath.filename().string() + " (" + std::to_string(folderFileCount) + " files) into " + currentVirtualDir, 2);
        } else {
            // User dropped an individual file
            std::string ext = srcPath.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            std::filesystem::path destPath = targetDir / srcPath.filename();
            if (std::filesystem::absolute(srcPath) != std::filesystem::absolute(destPath)) {
                std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
                if (ext == ".obj") {
                    std::filesystem::path srcMtl = srcPath.parent_path() / (srcPath.stem().string() + ".mtl");
                    if (std::filesystem::exists(srcMtl, ec)) {
                        std::filesystem::path destMtl = targetDir / (srcPath.stem().string() + ".mtl");
                        std::filesystem::copy_file(srcMtl, destMtl, std::filesystem::copy_options::overwrite_existing, ec);
                    }
                }
            }
            importedCount++;
            AddLog("LogContent", "Imported from system: " + srcPath.filename().string() + " into " + currentVirtualDir, 2);

            bool isModel = (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx");
            if (isModel) {
                modelsToQueue.push_back(destPath.string());
            }
        }
    }

    if (importedCount > 0) {
        TextureManager::Get().ClearCache();
        AssetRegistry::Get().ScanDirectory(contentRootPath);
        SyncRegistryWithUIProgress(contentRootPath);

        // Queue models for import after registry and textures are synced
        for (const auto& modelPath : modelsToQueue) {
            QueueModelImport(modelPath);
            std::string mExt = std::filesystem::path(modelPath).extension().string();
            std::transform(mExt.begin(), mExt.end(), mExt.begin(), ::tolower);
            if (mExt == ".fbx") {
                bool hasAnim = false;
                if (Eunoia::SkeletalAnimationManager::FileHasBones(modelPath, &hasAnim)) {
                    pendingBonesImportFilePath = modelPath;
                    bonesImportOptionType = 0;
                    showBonesImportOptionPopup = true;
                }
            } else if (mExt == ".anim" || mExt == ".eanim") {
                pendingBatchImportAnimFiles.push_back(modelPath);
                showBatchSkeletonImportPopup = true;
            }
        }

        AddLog("LogContent", "Successfully imported " + std::to_string(importedCount) + " file(s)/folder(s) into Content Browser.", 2);
    }
}

glm::vec3 EngineUI::CalculateDropSpawnPosition(OrbitCamera& camera, float mouseX, float mouseY, float screenW, float screenH) {
    if (screenW <= 0.0f || screenH <= 0.0f) return camera.target;
    float ndcX = (2.0f * mouseX) / screenW - 1.0f;
    float ndcY = 1.0f - (2.0f * mouseY) / screenH;
    float aspect = screenW / screenH;

    glm::mat4 proj = camera.GetProjectionMatrix(aspect);
    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 invVP = glm::inverse(proj * view);

    glm::vec4 pNear = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 pFar  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    if (pNear.w != 0.0f) pNear /= pNear.w;
    if (pFar.w != 0.0f)  pFar  /= pFar.w;

    glm::vec3 rayOrig = glm::vec3(pNear);
    glm::vec3 rayDir  = glm::normalize(glm::vec3(pFar - pNear));

    glm::vec3 spawnPos = camera.target;
    if (std::abs(rayDir.y) > 0.0001f) {
        float t = -rayOrig.y / rayDir.y;
        if (t > 0.0f && t < 200.0f) {
            spawnPos = rayOrig + rayDir * t;
            spawnPos.y = 0.5f;
            return spawnPos;
        }
    }
    spawnPos.y = 0.5f;
    return spawnPos;
}

void EngineUI::RenderViewportDropTarget(Scene& level, OrbitCamera& camera) {
    const ImGuiPayload* curPayload = ImGui::GetDragDropPayload();
    if (!curPayload || !curPayload->IsDataType("ASSET_ID")) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    auto vpRect = GetViewportRect(vp->Size.x, vp->Size.y);
    float vpX = vp->Pos.x + vpRect.x;
    float vpY = vp->Pos.y + vpRect.y;
    float vpW = vpRect.width;
    float vpH = vpRect.height;

    if (vpW <= 10.0f || vpH <= 10.0f) return;

    ImGui::SetNextWindowPos(ImVec2(vpX, vpY));
    ImGui::SetNextWindowSize(ImVec2(vpW, vpH));
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("##ViewportDropArea", nullptr, flags)) {
        ImGui::InvisibleButton("##ViewportDropBtn", ImVec2(vpW, vpH));

        if (ImGui::BeginDragDropTarget()) {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            dl->AddRect(ImVec2(vpX + 4.0f, vpY + 4.0f), ImVec2(vpX + vpW - 4.0f, vpY + vpH - 4.0f),
                        IM_COL32(50, 180, 255, 180), 0.0f, 0, 2.5f);

            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                std::string idStr = (const char*)payload->Data;
                AssetID id = AssetID::FromString(idStr);
                const AssetMetadata* meta = AssetRegistry::Get().GetMetadata(id);
                if (meta) {
                    if (meta->type == AssetType::Mesh || meta->type == AssetType::SkeletalMesh) {
                        std::filesystem::path fullMesh = AssetManager::Get().ResolvePhysicalOrCookedPath(meta->sourcePath);
                        std::error_code ec;
                        if (fullMesh.empty() || !std::filesystem::exists(fullMesh, ec) || std::filesystem::file_size(fullMesh, ec) == 0) {
                            fullMesh = contentRootPath / meta->sourcePath;
                        }
                        ImVec2 mousePos = ImGui::GetMousePos();
                        glm::vec3 spawnPos = CalculateDropSpawnPosition(camera, mousePos.x - vpX, mousePos.y - vpY, vpW, vpH);

                        GameObject& newObj = level.AddImportedMesh(fullMesh.string(), spawnPos);
                        level.selectedId = newObj.id;
                        AddLog("LogMesh", "Spawned 3D Object '" + meta->objectName + "' into Level at cursor (" +
                               std::to_string(spawnPos.x).substr(0, 4) + ", " +
                               std::to_string(spawnPos.z).substr(0, 4) + ")", 2);
                    } else if (meta->type == AssetType::Level || meta->type == AssetType::Scene) {
                        std::filesystem::path fullLevel = AssetManager::Get().ResolvePhysicalOrCookedPath(meta->sourcePath);
                        std::error_code ec;
                        if (fullLevel.empty() || !std::filesystem::exists(fullLevel, ec) || std::filesystem::file_size(fullLevel, ec) == 0) {
                            fullLevel = contentRootPath / meta->sourcePath;
                        }
                        OpenLevelFromPath(level, fullLevel.string());
                    } else if (meta->type == AssetType::LevelAnimation || meta->sourcePath.find(".elanim") != std::string::npos) {
                        std::filesystem::path fullAnim = AssetManager::Get().ResolvePhysicalOrCookedPath(meta->sourcePath);
                        std::error_code ec;
                        if (fullAnim.empty() || !std::filesystem::exists(fullAnim, ec) || std::filesystem::file_size(fullAnim, ec) == 0) {
                            fullAnim = contentRootPath / meta->sourcePath;
                        }
                        ImVec2 mousePos = ImGui::GetMousePos();
                        glm::vec3 spawnPos = CalculateDropSpawnPosition(camera, mousePos.x - vpX, mousePos.y - vpY, vpW, vpH);

                        GameObject& newObj = level.AddEmptyActor(spawnPos);
                        newObj.name = meta->objectName.empty() ? "LevelAnimation" : meta->objectName;
                        newObj.type = PrimitiveType::LevelAnimation;
                        newObj.hasLevelAnimation = true;
                        newObj.levelAnimation.enabled = true;
                        newObj.levelAnimation.playOnStart = true;
                        newObj.levelAnimation.loop = true;
                        newObj.levelAnimation.animationPath = fullAnim.string();
                        newObj.levelAnimation.LoadAnimation(fullAnim.string());
                        level.selectedId = newObj.id;
                        Eunoia::LevelAnimationManager::Get().SetActiveActorId(newObj.id);
                        if (newObj.levelAnimation.animation) {
                            Eunoia::LevelAnimationEditor::Get().SetActiveAnimation(newObj.levelAnimation.animation);
                        }
                        AddLog("LogLevelAnimation", "Spawned Level Animation '" + newObj.name + "' into Level", 2);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::End();
    }
}

void EngineUI::SetupTheme() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigDebugHighlightIdConflicts = false;

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    ImGui::StyleColorsDark(&style);

    // Atlas UI Theme: Deep dark obsidian backdrop, sleek rounded cards, crisp readouts, and vibrant electric royal blue (#4A7CFF)
    style.WindowPadding        = ImVec2(12.0f, 10.0f);
    style.FramePadding         = ImVec2(9.0f, 6.0f);
    style.CellPadding          = ImVec2(8.0f, 5.0f);
    style.ItemSpacing          = ImVec2(9.0f, 7.0f);
    style.ItemInnerSpacing     = ImVec2(7.0f, 5.0f);
    style.IndentSpacing        = 18.0f;
    style.ScrollbarSize        = 9.0f;
    style.GrabMinSize          = 10.0f;

    // Border-accented card architecture (matching image copy.png reference)
    style.WindowBorderSize     = 1.0f;
    style.ChildBorderSize      = 1.0f;
    style.PopupBorderSize      = 1.0f;
    style.FrameBorderSize      = 1.0f;
    style.TabBorderSize        = 0.0f;
    style.TabBarBorderSize     = 1.0f;
    style.TabBarOverlineSize   = 2.5f;

    style.WindowRounding       = 10.0f;
    style.ChildRounding        = 8.0f;
    style.FrameRounding        = 7.0f;
    style.PopupRounding        = 8.0f;
    style.ScrollbarRounding    = 8.0f;
    style.GrabRounding         = 6.0f;
    style.TabRounding          = 6.0f;
    style.DockingSeparatorSize = 3.0f;

    // Atlas color palette (matching Style.cpp and image copy.png)
    const ImVec4 col_backdrop      = ImVec4(0.039f, 0.039f, 0.051f, 1.0f); // #0A0A0D deep backdrop
    const ImVec4 col_surface       = ImVec4(0.082f, 0.082f, 0.102f, 1.0f); // #15151A surface card
    const ImVec4 col_elevated      = ImVec4(0.118f, 0.118f, 0.145f, 1.0f); // #1E1E25 elevated panel
    const ImVec4 col_header        = ImVec4(0.102f, 0.102f, 0.125f, 1.0f); // #1A1A20 header
    const ImVec4 col_control       = ImVec4(0.125f, 0.125f, 0.153f, 1.0f); // #202027 input/button control
    const ImVec4 col_hovered       = ImVec4(0.169f, 0.169f, 0.204f, 1.0f); // #2B2B34 hovered
    const ImVec4 col_pressed       = ImVec4(0.102f, 0.102f, 0.125f, 1.0f); // #1A1A20 pressed
    const ImVec4 col_selected      = ImVec4(0.125f, 0.165f, 0.259f, 1.0f); // #202A42 selected
    const ImVec4 col_popup         = ImVec4(0.110f, 0.110f, 0.133f, 0.98f);// #1C1C22 popup
    const ImVec4 col_tab           = ImVec4(0.063f, 0.063f, 0.078f, 1.0f); // #101014 inactive tab
    const ImVec4 col_tab_active    = ImVec4(0.118f, 0.118f, 0.145f, 1.0f); // #1E1E25 active tab
    const ImVec4 col_text          = ImVec4(0.929f, 0.933f, 0.953f, 1.0f); // #EDEEF3 crisp white text
    const ImVec4 col_faint         = ImVec4(0.541f, 0.553f, 0.600f, 1.0f); // #8A8D99 muted text
    const ImVec4 col_accent        = ImVec4(0.290f, 0.486f, 1.000f, 1.0f); // #4A7CFF Electric Royal Blue
    const ImVec4 col_accent_soft   = ImVec4(0.510f, 0.651f, 1.000f, 1.0f); // #82A6FF
    const ImVec4 col_border        = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);       // Subtle Outline
    const ImVec4 col_scroll_thumb  = ImVec4(0.243f, 0.243f, 0.290f, 1.0f); // #3E3E4A
    const ImVec4 col_danger        = ImVec4(0.941f, 0.361f, 0.408f, 1.0f); // #F05C68

    colors[ImGuiCol_Text]                  = col_text;
    colors[ImGuiCol_TextDisabled]          = col_faint;
    colors[ImGuiCol_WindowBg]              = col_surface;
    colors[ImGuiCol_ChildBg]               = col_surface;
    colors[ImGuiCol_PopupBg]               = col_popup;
    colors[ImGuiCol_Border]                = col_border;
    colors[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg]               = col_control;
    colors[ImGuiCol_FrameBgHovered]        = col_hovered;
    colors[ImGuiCol_FrameBgActive]         = col_selected;
    colors[ImGuiCol_TitleBg]               = col_header;
    colors[ImGuiCol_TitleBgActive]         = col_header;
    colors[ImGuiCol_TitleBgCollapsed]      = col_header;
    colors[ImGuiCol_MenuBarBg]             = col_backdrop;
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.118f, 0.118f, 0.145f, 0.40f);
    colors[ImGuiCol_ScrollbarGrab]         = col_scroll_thumb;
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.32f, 0.32f, 0.38f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive]   = col_accent;
    colors[ImGuiCol_CheckMark]             = col_accent;
    colors[ImGuiCol_SliderGrab]            = col_accent;
    colors[ImGuiCol_SliderGrabActive]      = col_accent_soft;
    colors[ImGuiCol_Button]                = col_control;
    colors[ImGuiCol_ButtonHovered]         = col_hovered;
    colors[ImGuiCol_ButtonActive]          = col_accent;
    colors[ImGuiCol_Header]                = ImVec4(0.290f, 0.486f, 1.000f, 0.18f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.290f, 0.486f, 1.000f, 0.28f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.290f, 0.486f, 1.000f, 0.40f);
    colors[ImGuiCol_Separator]             = col_border;
    colors[ImGuiCol_SeparatorHovered]      = col_accent;
    colors[ImGuiCol_SeparatorActive]       = col_accent;
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripHovered]     = col_accent;
    colors[ImGuiCol_ResizeGripActive]      = col_accent;
    colors[ImGuiCol_Tab]                   = col_tab;
    colors[ImGuiCol_TabHovered]            = col_elevated;
    colors[ImGuiCol_TabActive]             = col_tab_active;
    colors[ImGuiCol_TabUnfocused]          = col_tab;
    colors[ImGuiCol_TabUnfocusedActive]   = col_tab_active;
    colors[ImGuiCol_TabSelectedOverline]   = col_accent;
    colors[ImGuiCol_TabDimmedSelectedOverline] = col_accent;
    colors[ImGuiCol_TableHeaderBg]         = col_header;
    colors[ImGuiCol_TableBorderStrong]     = col_border;
    colors[ImGuiCol_TableBorderLight]      = ImVec4(1.0f, 1.0f, 1.0f, 0.04f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.0f, 1.0f, 1.0f, 0.015f);
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.290f, 0.486f, 1.000f, 0.35f);
    colors[ImGuiCol_DragDropTarget]        = col_accent;
    colors[ImGuiCol_NavHighlight]          = col_accent;
    colors[ImGuiCol_NavWindowingHighlight] = col_text;
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0, 0, 0, 0.60f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, 0.75f);
    colors[ImGuiCol_DockingPreview]        = ImVec4(0.290f, 0.486f, 1.000f, 0.25f);
    colors[ImGuiCol_DockingEmptyBg]        = col_backdrop;

    if (uiScale != 1.0f) {
        style.ScaleAllSizes(uiScale);
    }
    io.FontGlobalScale = uiScale;
}
void EngineUI::ApplyUIScale(float scale) {
    uiScale = std::clamp(scale, 0.25f, 2.50f);
    topBarHeight = 36.0f * uiScale;
    SetupTheme();
}

void EngineUI::Render(Scene& scene, OrbitCamera& camera, float fps, float frameTimeMs, uint32_t vertexCount, uint32_t indexCount, bool& outShouldExit) {
    if (InputSystem::Get().IsCursorLocked()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

    if (isGameOnlyWindow) {
        if (m_progressiveLoader.active) {
            if (!m_progressiveLoader.completed) {
                m_progressiveLoader.Step(scene, 128);
                if (m_progressiveLoader.completed) {
                    m_progressiveLoader.active = false;
                    RequestViewportRedraw(15);
                }
            }
        }

        bool isCtrlDown = ImGui::GetIO().KeyCtrl ||
                          InputSystem::Get().IsKeyDown(Key::LeftControl) ||
                          InputSystem::Get().IsKeyDown(Key::RightControl);
        if (isCtrlDown && (ImGui::IsKeyPressed(ImGuiKey_Delete) || InputSystem::Get().IsKeyPressed(Key::Delete))) {
            outShouldExit = true;
        }

        // Render active runtime UIs and on-screen prints / HUD in game window
        ImGuiViewport* mainVp = ImGui::GetMainViewport();
        EunoiaUI::UISystemManager::Get().SetCurrentScene(&scene);
        EunoiaUI::UISystemManager::Get().RenderActiveUIs(glm::vec2(mainVp->WorkPos.x, mainVp->WorkPos.y), glm::vec2(mainVp->WorkSize.x, mainVp->WorkSize.y));

        ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();
        if (fgDrawList) {
            EunoiaUI::UISystem3DManager::Get().RenderAllScene3DUI(
                fgDrawList, scene, camera,
                glm::vec4(mainVp->WorkPos.x, mainVp->WorkPos.y, mainVp->WorkSize.x, mainVp->WorkSize.y),
                true
            );
        }

        RenderScreenPrintOverlay(24.0f, 24.0f);
        return;
    }

    currentCamera = &camera;
    currentFps = fps;
    if (!scene.onPreChange) {
        scene.onPreChange = [&scene](const std::string& action) {
            UndoManager::Get().RecordSnapshot(scene, action);
        };
    }

    ImGuizmo::BeginFrame();

    // Reactive Viewport Dirty Tracking (On-Demand Rendering)
    if (scene.isPlayMode || isPlayModeActive) {
        RequestViewportRedraw(15);
    }
    if (m_progressiveLoader.active && !m_progressiveLoader.completed) {
        RequestViewportRedraw(15);
    }
    if (levelUnsaved) {
        RequestViewportRedraw(10);
    }
        float uiDt = ImGui::GetIO().DeltaTime;
    Eunoia::LevelAnimationEditor::Get().UpdatePlayback(scene, uiDt);
    if (Eunoia::LevelAnimationEditor::Get().IsPlaying()) {
        RequestViewportRedraw(4);
    }

    if (ImGuizmo::IsUsing() || ImGuizmo::IsOver()) {
        RequestViewportRedraw(10);
    }

    // Selection changes
    static int s_lastSelectedId = -1;
    if (scene.selectedId != s_lastSelectedId) {
        s_lastSelectedId = scene.selectedId;
        RequestViewportRedraw(12);
    }

    // Actor count / hierarchy changes
    static size_t s_lastObjCount = 0;
    if (scene.objects.size() != s_lastObjCount) {
        s_lastObjCount = scene.objects.size();
        RequestViewportRedraw(12);
    }

    // Active ImGui widget interaction (dragging sliders, inputs, etc.)
    if (ImGui::IsAnyItemActive() || ImGui::IsItemEdited()) {
        RequestViewportRedraw(10);
    }

    // Step progressive scene loader if active
    if (m_progressiveLoader.active) {
        if (!m_progressiveLoader.completed) {
            m_progressiveLoader.Step(scene, 6);
            if (m_progressiveLoader.completed) {
                ResetTreeStateApplication();
                AddLog("LogWorld", "Level loaded: " + m_progressiveLoader.filePath + " (" + std::to_string(scene.objects.size()) + " actors)", 2);
            }
        }
    }

    // Flush asynchronous compilation logs
    {
        std::lock_guard<std::mutex> lock(asyncLogMutex);
        if (!pendingAsyncLogs.empty()) {
            for (const auto& logEntry : pendingAsyncLogs) {
                AddLog(logEntry.category, logEntry.message, logEntry.level);
            }
            pendingAsyncLogs.clear();
        }
    }

    // Periodic check for behaviour script modifications (Auto-compilation on focus)
    scriptWatchTimer += (frameTimeMs * 0.001f);
    if (scriptWatchTimer >= 0.2f) {
        scriptWatchTimer = 0.0f;
        CheckForScriptChanges(&scene);
    }

    // Periodic check for 3D model files added in real-time
    modelWatchTimer += (frameTimeMs * 0.001f);
    if (modelWatchTimer >= 0.5f) {
        modelWatchTimer = 0.0f;
        CheckForModelChanges();
    }
    if (!showModelImportModal && !pendingModelImportQueue.empty()) {
        OpenModelImportView(pendingModelImportQueue.front());
    }

    // Unreal Keyboard Shortcuts (when not typing and not currently in free fly mode)
    // During Play Mode, suppress editor shortcuts so gameplay input is uninterrupted (dev.md Ãƒâ€šÃ‚Â§54)
    if (!ImGui::GetIO().WantTextInput && !camera.isFlying && !scene.isPlayMode) {
        // F7: Compile Behaviour Scripts
        if (ImGui::IsKeyPressed(ImGuiKey_F7) && !isCompilingScripts) {
            TriggerCompileScripts(&scene);
        }

        // F: Focus selected actor
        if (ImGui::IsKeyPressed(ImGuiKey_F) && scene.selectedId != -1) {
            GameObject* obj = scene.GetSelected();
            if (obj) {
                camera.FocusOn(obj->position, obj->scale);
                AddLog("LogViewport", "Focused Viewport on Actor: " + obj->name + " [F]", 0);
            }
        }

        // G: Toggle Game View (Hide Grid & Gizmos)
        if (ImGui::IsKeyPressed(ImGuiKey_G)) {
            ToggleGameView(scene);
        }

        // F11: Toggle Immersive Viewport
        if (ImGui::IsKeyPressed(ImGuiKey_F11)) {
            ToggleImmersiveMode();
        }

        // Transform Tool shortcuts
        if (ImGui::IsKeyPressed(ImGuiKey_W)) {
            currentGizmoOperation = ImGuizmo::TRANSLATE;
            AddLog("LogEditor", "Active Tool: Translate (W)", 0);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E)) {
            currentGizmoOperation = ImGuizmo::ROTATE;
            AddLog("LogEditor", "Active Tool: Rotate (E)", 0);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            currentGizmoOperation = ImGuizmo::SCALE;
            AddLog("LogEditor", "Active Tool: Scale (R)", 0);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
            currentGizmoMode = (currentGizmoMode == ImGuizmo::WORLD) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
            AddLog("LogEditor", (currentGizmoMode == ImGuizmo::WORLD) ? "Coordinate Space: World" : "Coordinate Space: Local", 0);
        }
        // Undo / Redo Shortcuts (52 steps)
        if (ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            if (UndoManager::Get().CanUndo()) {
                std::string act = UndoManager::Get().GetUndoActionName();
                UndoManager::Get().Undo(scene);
                AddLog("LogEditor", "Undo: " + act, 0);
            }
        }
        if (ImGui::GetIO().KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y) || (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)))) {
            if (UndoManager::Get().CanRedo()) {
                std::string act = UndoManager::Get().GetRedoActionName();
                UndoManager::Get().Redo(scene);
                AddLog("LogEditor", "Redo: " + act, 0);
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Z) && !ImGui::GetIO().KeyCtrl) {
            gizmoUseCenter = !gizmoUseCenter;
            AddLog("LogEditor", gizmoUseCenter ? "Gizmo Position: Center (Z)" : "Gizmo Position: Pivot (Z)", 0);
        }
        static int s_exitPlayModeCooldown = 0;
        if (s_justExitedPlayMode) {
            s_exitPlayModeCooldown = 30;
            s_justExitedPlayMode = false;
        }
        if (s_exitPlayModeCooldown > 0) {
            s_exitPlayModeCooldown--;
        }

        bool isCtrlDownForDelete = ImGui::GetIO().KeyCtrl ||
                                   InputSystem::Get().IsKeyDown(Key::LeftControl) ||
                                   InputSystem::Get().IsKeyDown(Key::RightControl) ||
                                   InputSystem::Get().IsKeyDown(Key::LCtrl) ||
                                   InputSystem::Get().IsKeyDown(Key::RCtrl) ||
                                   (m_window && (glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(m_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS));

        if (!s_justExitedPlayMode && s_exitPlayModeCooldown == 0 && !isCtrlDownForDelete &&
            ImGui::IsKeyPressed(ImGuiKey_Delete) && scene.selectedId != -1) {
            GameObject* o = scene.GetSelected();
            if (o) {
                UndoManager::Get().RecordSnapshot(scene, "Delete Actor: " + o->name);
                AddLog("LogActor", "Deleted Actor: " + o->name, 1);
            }
            scene.RemoveObject(scene.selectedId);
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && scene.selectedId != -1) {
            if (!ImGui::GetIO().WantTextInput) {
                UndoManager::Get().RecordSnapshot(scene, "Duplicate Actor");
                GameObject* copy = scene.DuplicateObject(scene.selectedId);
                if (copy) {
                    selectedActorIds.clear();
                    selectedActorIds.insert(copy->id);
                    scene.selectedId = copy->id;
                    std::string logMsg = "Duplicated Actor: " + copy->name;
                    if (!copy->childIds.empty()) {
                        logMsg += " (along with " + std::to_string(copy->childIds.size()) + " children)";
                    }
                    AddLog("LogActor", logMsg, 2);
                    EngineLogger::Get().LogAction("DUPLICATE_ACTOR", copy->name, "Children: " + std::to_string(copy->childIds.size()));
                }
            }
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
            if (Eunoia::AnimatorGraphManager::Get().IsActive()) {
                auto activeAgEditor = Eunoia::AnimatorGraphManager::Get().GetActiveEditor();
                if (activeAgEditor) {
                    activeAgEditor->Save();
                }
            } else if (ImGui::GetIO().KeyShift) {
                SaveSceneAs(scene);
            } else {
                SaveScene(scene);
            }
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
            OpenScene(scene);
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N)) {
            UndoManager::Get().RecordSnapshot(scene, "Clear Scene");
            scene.Clear();
            currentLevelFilePath = "";
            AddLog("LogWorld", "Cleared Level (New Level)", 0);
        }
        // Ctrl+B: Build Standalone Game Project (Runs in background without modal)
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_B) && !isGameBuilding) {
            StartGameBuildAsync(scene);
        }
        if (!s_justExitedPlayMode && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            scene.selectedId = -1;
        }
    }

    // Play Mode Presentation & Stop-on-Ctrl+Delete
    if (scene.isPlayMode) {
        isPlayModeActive = true;
        bool isCtrlDown = ImGui::GetIO().KeyCtrl ||
                          InputSystem::Get().IsKeyDown(Key::LeftControl) ||
                          InputSystem::Get().IsKeyDown(Key::RightControl);
        bool isDeletePressed = ImGui::IsKeyPressed(ImGuiKey_Delete) ||
                               InputSystem::Get().IsKeyPressed(Key::Delete);
        if (isCtrlDown && isDeletePressed) {
            ExitPlayMode(scene, &camera);
            return;
        }

        ImGuiViewport* mainVp = ImGui::GetMainViewport();

        // Render Active Runtime UIs added via UISystem.Add(UI)
        EunoiaUI::UISystemManager::Get().SetCurrentScene(&scene);
        EunoiaUI::UISystemManager::Get().RenderActiveUIs(glm::vec2(mainVp->WorkPos.x, mainVp->WorkPos.y), glm::vec2(mainVp->WorkSize.x, mainVp->WorkSize.y));

        // Render 3D World-Space and Screen-Space User Interfaces in Play Mode
        ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();
        if (fgDrawList) {
            EunoiaUI::UISystem3DManager::Get().RenderAllScene3DUI(
                fgDrawList, scene, camera,
                glm::vec4(mainVp->WorkPos.x, mainVp->WorkPos.y, mainVp->WorkSize.x, mainVp->WorkSize.y),
                true
            );
        }

        // Game View top-left: Render on-screen Print messages (Print(value, time))
        RenderScreenPrintOverlay(mainVp->WorkPos.x + 16.0f, mainVp->WorkPos.y + 16.0f);

        // Game View occupies the available area; editor UI is hidden
        RenderLoadingModal();
        RenderSaveIndicator();
        return;
    }

    // Global editor change tracking for continuous edits (sliders, drag inputs, colors, text fields)
    static bool s_hasPreEditSnapshot = false;
    static Scene s_preEditSceneSnapshot;
    if (ImGui::IsAnyItemActive()) {
        if (!s_hasPreEditSnapshot) {
            s_hasPreEditSnapshot = true;
            s_preEditSceneSnapshot = scene;
        }
    } else if (s_hasPreEditSnapshot) {
        s_hasPreEditSnapshot = false;
        if (HasSceneStateChanged(s_preEditSceneSnapshot, scene)) {
            UndoManager::Get().RecordSnapshot(s_preEditSceneSnapshot, "Modify Properties");
        }
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    matPreviewPipRect.active = false;
    bool isMainEditor = EunoiaUI::UIEditorManager::Get().IsMainEditorActive() && 
                        !(showMaterialEditor && materialWorkspaceActive) &&
                        !Eunoia::NodeBehaviour::NodeBehaviourManager::Get().IsActive() &&
                        !Eunoia::AnimatorGraphManager::Get().IsActive() &&
                        !Eunoia::AnimationBlendManager::Get().IsActive() &&
                        !Eunoia::BehaviourManager::Get().IsActive() && (s_activePromotedTabIndex < 0 || s_activePromotedTabIndex >= (int)s_promotedTabs.size());

    // 1. Top Document Tab Switches (Level Editor, Material Editor, UI Editor) at the very top!
    RenderWorkspaceTabBar(scene, outShouldExit);


    // === DOCKSPACE (covers area below all top bars) ===
    {
        float topH = GetTopBarsTotalHeight();
        ImGuiViewport* mainVp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(mainVp->Pos.x, mainVp->Pos.y + topH));
        ImGui::SetNextWindowSize(ImVec2(mainVp->Size.x, mainVp->Size.y - topH - bottomBarHeight * uiScale));
        ImGuiWindowFlags dockWinFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove     |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoDocking  | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        bool dockOpen = true;
        ImGui::Begin("##EunoiaMainDockspace", &dockOpen, dockWinFlags);
        ImGui::PopStyleVar(3);

        ImGuiID dockId = ImGui::GetID("EunoiaDockSpace");
        // Build default layout only once (first run or after reset)
        if (!ImGui::DockBuilderGetNode(dockId) || ImGui::DockBuilderGetNode(dockId)->IsLeafNode()) {
            ImGui::DockBuilderRemoveNode(dockId);
            ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetContentRegionAvail());

            ImGuiID dockRight = 0;
            ImGuiID dockRemaining = 0;
            ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Right, 0.22f, &dockRight, &dockRemaining);

            ImGuiID dockBottom = 0;
            ImGuiID dockCenter = 0;
            ImGui::DockBuilderSplitNode(dockRemaining, ImGuiDir_Down, 0.28f, &dockBottom, &dockCenter);

            ImGuiID dockDetails = 0;
            ImGuiID dockOutliner = 0;
            ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.50f, &dockDetails, &dockOutliner);

            ImGui::DockBuilderDockWindow("Outliner",        dockOutliner);
            ImGui::DockBuilderDockWindow("Details",         dockDetails);
            ImGui::DockBuilderDockWindow("Content Browser", dockBottom);
            ImGui::DockBuilderDockWindow("Animation",       dockBottom);
            ImGui::DockBuilderDockWindow("Output Log",      dockBottom);
            ImGui::DockBuilderFinish(dockId);
        }

        ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f),
            ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode);

        ImGuiDockNode* centralNode = ImGui::DockBuilderGetCentralNode(dockId);
        if (centralNode && centralNode->Size.x > 20.0f && centralNode->Size.y > 20.0f) {
            m_dynamicViewportRect.x = centralNode->Pos.x - mainVp->Pos.x;
            m_dynamicViewportRect.y = centralNode->Pos.y - mainVp->Pos.y;
            m_dynamicViewportRect.width = centralNode->Size.x;
            m_dynamicViewportRect.height = centralNode->Size.y;
            m_hasDynamicViewportRect = true;
        }

        ImGui::End();
    }
    // === END DOCKSPACE ===

    // 2. Top Menu Bar
    RenderTopMenuBar(scene, camera, outShouldExit);

    if (isMainEditor) {
        // 2. Outliner (Matches UI_Ref.svg "Outliner" left sidebar)
        if (showOutliner && !IsWindowPromotedTab("Outliner")) {
            RenderOutliner(scene);
        }

        // 3. Details Panel (Matches UI_Ref.svg "Details" right sidebar)
        if (showDetails) {
            GameObject* prevSel = scene.GetSelected();
            glm::vec3 prePos = prevSel ? prevSel->position : glm::vec3(0.0f);
            glm::vec3 preRot = prevSel ? prevSel->rotation : glm::vec3(0.0f);
            glm::vec3 preScale = prevSel ? prevSel->scale : glm::vec3(1.0f);
            glm::vec3 preCol = prevSel ? prevSel->color : glm::vec3(1.0f);
            bool preVis = prevSel ? prevSel->visible : true;
            glm::vec3 preLightCol = (prevSel && prevSel->isLight) ? prevSel->light.color : glm::vec3(1.0f);
            float preLightInt = (prevSel && prevSel->isLight) ? prevSel->light.intensity : 0.0f;
            float preLightRange = (prevSel && prevSel->isLight) ? prevSel->light.range : 0.0f;
            bool preLightShadows = (prevSel && prevSel->isLight) ? prevSel->light.castShadows : false;

            if (!IsWindowPromotedTab("Details")) {
                RenderDetails(scene);
            }

            GameObject* postSel = scene.GetSelected();
            if (postSel) {
                if (glm::distance(postSel->position, prePos) > 1e-4f ||
                    glm::distance(postSel->rotation, preRot) > 1e-4f ||
                    glm::distance(postSel->scale, preScale) > 1e-4f ||
                    glm::distance(postSel->color, preCol) > 1e-4f ||
                    postSel->visible != preVis ||
                    (postSel->isLight && (glm::distance(postSel->light.color, preLightCol) > 1e-4f ||
                                          std::abs(postSel->light.intensity - preLightInt) > 1e-4f ||
                                          std::abs(postSel->light.range - preLightRange) > 1e-4f ||
                                          postSel->light.castShadows != preLightShadows)))
                {
                    RequestViewportRedraw(10);
                }
            }
        }

        // 4. Dockable Bottom Windows (Content Browser, Animation, Output Log)
        if (showContentBrowserWindow && !IsWindowPromotedTab("Content Browser")) {
            RenderContentBrowserWindow(scene);
        }
        if (showAnimationWindow && !IsWindowPromotedTab("Animation")) {
            RenderAnimationTimelineWindow(scene);
        }
        if (showOutputLogWindow && !IsWindowPromotedTab("Output Log")) {
            RenderOutputLogWindow();
        }

        // Dynamic splitters to resize Left Sidebar, Right Sidebar, and Bottom Content Browser
        RenderLayoutSplitters();

        // 5. Central 3D Viewport Overlay & Drop Target
        if (showViewportOverlay) {
            RenderViewportOverlay(scene, camera, fps, frameTimeMs);
        }
        RenderViewportDropTarget(scene, camera);
    } else {
        // Active UI, Visual Behaviour, or Material Editor Tab Full Workspace
        float topBarH = GetTopBarsTotalHeight();
        ImGuiViewport* vpWorkspace = ImGui::GetMainViewport();
        glm::vec2 wsPos(vpWorkspace->Pos.x, vpWorkspace->Pos.y + topBarH);
        glm::vec2 wsSize(vpWorkspace->Size.x, vpWorkspace->Size.y - topBarH);

        if (showMaterialEditor && materialWorkspaceActive) {
            float cbH = showBottomDrawer ? (bottomDrawerOpen ? bottomDockHeight : 26.0f) : 0.0f;
            glm::vec2 matWsSize(wsSize.x, wsSize.y - cbH);
            RenderMaterialWorkspace(scene, wsPos, matWsSize);
            if (showBottomDrawer) {
                RenderContentBrowser(scene);
            }
        } else if (Eunoia::BehaviourManager::Get().IsActive()) {
            Eunoia::BehaviourManager::Get().RenderActiveTabWorkspace(wsPos, wsSize, &scene);
        } else if (Eunoia::AnimatorGraphManager::Get().IsActive()) {
            Eunoia::AnimatorGraphManager::Get().RenderActiveTabWorkspace(ImVec2(wsPos.x, wsPos.y), ImVec2(wsSize.x, wsSize.y));
        } else if (Eunoia::AnimationBlendManager::Get().IsActive()) {
            Eunoia::AnimationBlendManager::Get().RenderActiveTabWorkspace(ImVec2(wsPos.x, wsPos.y), ImVec2(wsSize.x, wsSize.y));
        } else if (Eunoia::NodeBehaviour::NodeBehaviourManager::Get().IsActive()) {
            Eunoia::NodeBehaviour::NodeBehaviourManager::Get().RenderActiveTabWorkspace(ImVec2(wsPos.x, wsPos.y), ImVec2(wsSize.x, wsSize.y), &scene);
        } else if (!EunoiaUI::UIEditorManager::Get().IsMainEditorActive()) {
            EunoiaUI::UIEditorManager::Get().RenderActiveTabWorkspace(wsPos, wsSize, &scene);
        } else if (s_activePromotedTabIndex >= 0 && s_activePromotedTabIndex < (int)s_promotedTabs.size()) {
            const std::string& pName = s_promotedTabs[s_activePromotedTabIndex].name;
            ImGui::SetNextWindowPos(ImVec2(wsPos.x, wsPos.y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(wsSize.x, wsSize.y), ImGuiCond_Always);
            ImGuiWindowFlags pFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;
            if (ImGui::Begin(pName.c_str(), nullptr, pFlags)) {
                if (pName == "Content Browser") {
                    RenderContentBrowserContent(scene);
                } else if (pName == "Animation") {
                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    Eunoia::LevelAnimationEditor::Get().RenderTimeline(scene, ImGui::GetCursorScreenPos(), avail);
                } else if (pName == "Output Log") {
                    if (ImGui::Button("Clear")) { logs.clear(); }
                    ImGui::SameLine(); ImGui::Checkbox("Info", &logShowInfo);
                    ImGui::SameLine(); ImGui::Checkbox("Warnings", &logShowWarnings);
                    ImGui::SameLine(); ImGui::Checkbox("Errors", &logShowErrors);
                    ImGui::SameLine(); ImGui::Checkbox("Auto-scroll", &logAutoScroll);
                    ImGui::SameLine(ImGui::GetWindowWidth() - 220.0f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputTextWithHint("##LogSearchTab", "Filter Log...", logSearchBuf, sizeof(logSearchBuf));
                    ImGui::Separator();
                    ImGui::BeginChild("OutputLogScrollingRegionTab", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
                    std::string searchLower = logSearchBuf;
                    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
                    for (const auto& entry : logs) {
                        if (entry.level == 0 && !logShowInfo) continue;
                        if (entry.level == 1 && !logShowWarnings) continue;
                        if ((entry.level == 2 || entry.level == 3) && !logShowErrors) continue;
                        if (!searchLower.empty()) {
                            std::string msgLower = entry.message;
                            std::transform(msgLower.begin(), msgLower.end(), msgLower.begin(), ::tolower);
                            if (msgLower.find(searchLower) == std::string::npos) continue;
                        }
                        ImVec4 col = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
                        if (entry.level == 1) col = ImVec4(1.0f, 0.85f, 0.2f, 1.0f);
                        else if (entry.level == 2 || entry.level == 3) col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                        else if (entry.level == 4) col = ImVec4(0.3f, 1.0f, 0.4f, 1.0f);
                        ImGui::TextColored(col, "[%s] %s", entry.category.c_str(), entry.message.c_str());
                    }
                    if (logAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                        ImGui::SetScrollHereY(1.0f);
                    ImGui::EndChild();
                } else if (pName == "Outliner") {
                    RenderOutliner(scene);
                } else if (pName == "Details") {
                    RenderDetails(scene);
                }
            }
            ImGui::End();
        }
    }

    // Full-Width Horizontal Bottom Progress Bar (matching Frame.svg blueprint)
    RenderBottomProgressBar();

    // Update timers for tab saves and bottom progress bar
    float dt = ImGui::GetIO().DeltaTime;
    if (m_bottomProgress.completionTimer > 0.0f) m_bottomProgress.completionTimer = std::max(0.0f, m_bottomProgress.completionTimer - dt);
    if (m_levelSaveState.fadeTimer > 0.0f) m_levelSaveState.fadeTimer = std::max(0.0f, m_levelSaveState.fadeTimer - dt);
    if (m_materialSaveState.fadeTimer > 0.0f) m_materialSaveState.fadeTimer = std::max(0.0f, m_materialSaveState.fadeTimer - dt);
    if (m_uiSaveState.fadeTimer > 0.0f) m_uiSaveState.fadeTimer = std::max(0.0f, m_uiSaveState.fadeTimer - dt);

    if (showHelpModal) {
        RenderHelpModal();
    }
    if (showSaveLevelModal) {
        RenderSaveLevelModal();
    }
    if (showClusterCullingStats) {
        RenderClusterCullingStats(scene);
    }
    if (showQualitySettings) {
        RenderQualitySettings();
    }

    // Skybox Editor & World Skybox Settings Modal
    if (showSkyboxEditor) {
        RenderSkyboxEditor(scene);
    }
    if (showWaterEditor) {
        RenderWaterEditor(scene);
    }
    if (showSkyboxSettingsModal) {
        RenderWorldSkyboxSettingsModal(scene);
    }
    if (showPostProcessingModal) {
        RenderPostProcessingModal(scene);
    }
    if (showAmbientOcclusionModal) {
        RenderAmbientOcclusionModal(scene);
    }
    if (showVolumetricFogModal) {
        RenderVolumetricFogModal(scene);
    }

    // 7. Asset Reference Viewer (dev.md Section 33)
    if (showReferenceViewer) {
        RenderReferenceViewer(scene);
    }

    // Standalone Game Build Pipeline Modal
    if (showGameBuildModal) {
        RenderGameBuildModal(scene);
    }

    // 9. In-Engine Code Editor (dev.md & User Request)
    if (showCodeEditor) {
        RenderCodeEditor();
    }

    // 10. Behaviour Script Change & Compilation Prompt (User Request: ask as compile or later)
    RenderScriptCompileModal(scene);

    // 10c. UI-System Visual Canvas Designer Windows (.eui assets)
    EunoiaUI::UIEditorManager::Get().RenderAll();

    // 11. 3D Model Real-Time Detection & Import View ("One by One" wizard)
    if (showModelImportModal) {
        RenderModelImportModal(scene, camera);
    }

    // Undo History Window (52 steps capacity)
    if (showUndoHistory) {
        ImGui::SetNextWindowSize(ImVec2(360, 440), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Undo History (52 Steps)###UndoHistoryWin", &showUndoHistory)) {
            ImGui::TextColored(ImVec4(0.12f, 0.68f, 1.00f, 1.0f), "History Stack: %zu / 52 steps", UndoManager::Get().GetUndoCount());
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear History")) {
                UndoManager::Get().Clear();
            }
            ImGui::Separator();

            const auto& stack = UndoManager::Get().GetUndoStack();
            if (stack.empty()) {
                ImGui::TextDisabled("No undo history yet. Edit or transform objects!");
            } else {
                for (int i = (int)stack.size() - 1; i >= 0; --i) {
                    std::string label = "#" + std::to_string(i + 1) + ": " + stack[i].actionName;
                    if (ImGui::Selectable(label.c_str(), false)) {
                        UndoManager::Get().JumpToUndoStep(i, scene);
                        AddLog("LogEditor", "Jumped to: " + stack[i].actionName, 0);
                    }
                }
            }
        }
        ImGui::End();
    }

    // 9. Interactive 3D Transform Gizmo
    if (isMainEditor) {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        auto vpRect = GetViewportRect(vp->Size.x, vp->Size.y);
        if (!m_progressiveLoader.active || m_progressiveLoader.completed) {
            RenderGizmo(scene, camera, vpRect.width, vpRect.height);
        }
    }

    // 10. Futuristic Loading Progress Modal (all loading situations)
    RenderLoadingModal();
    RenderViewportLoadingOverlay();
    RenderSaveIndicator();

    // 11. Project Browser Dialog Window (Startup & On-Demand)
    RenderProjectBrowser(scene, camera);

    // 12. Custom Window Frame Perimeter Border (when not maximized and not in immersive mode)
    bool isMax = false;
#ifdef _WIN32
    if (m_window) {
        HWND hwnd = glfwGetWin32Window(m_window);
        if (hwnd) isMax = (IsZoomed(hwnd) != 0);
    }
#endif
    if (!isMax && !isImmersiveMode) {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        fg->AddRect(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                    IM_COL32(48, 52, 64, 255), 0.0f, 0, 1.0f);
    }

}

bool EngineUI::RenderTabWithSaveProgress(const char* label, bool isActive, const TabSaveProgressState& saveState, float btnHeight) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (isActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.129f, 0.129f, 0.129f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
    }
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.160f, 0.160f, 0.160f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.129f, 0.129f, 0.129f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * uiScale, 2.0f * uiScale));

    bool clicked = ImGui::Button(label, ImVec2(0.0f, btnHeight));

    ImVec2 itemMin = ImGui::GetItemRectMin();
    ImVec2 itemMax = ImGui::GetItemRectMax();

    if (saveState.isSaving) {
        float progress = std::clamp(saveState.progress, 0.05f, 1.0f);
        float fillW = (itemMax.x - itemMin.x) * progress;
        drawList->AddRectFilled(itemMin, ImVec2(itemMin.x + fillW, itemMax.y), IM_COL32(31, 153, 242, 160));
        drawList->AddLine(ImVec2(itemMin.x, itemMax.y - 2.0f), ImVec2(itemMin.x + fillW, itemMax.y - 2.0f), IM_COL32(64, 200, 255, 255), 2.0f);
    } else if (saveState.fadeTimer > 0.0f) {
        float alpha = std::clamp(saveState.fadeTimer / 1.5f, 0.0f, 1.0f);
        drawList->AddRectFilled(itemMin, itemMax, IM_COL32(40, 180, 80, (uint8_t)(alpha * 120)));
        drawList->AddLine(ImVec2(itemMin.x, itemMax.y - 2.0f), ImVec2(itemMax.x, itemMax.y - 2.0f), IM_COL32(80, 220, 100, (uint8_t)(alpha * 240)), 2.0f);
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);

    return clicked;
}

void EngineUI::RenderBottomProgressBar() {
    if (isImmersiveMode || isGameOnlyWindow || isPlayModeActive) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!vp || vp->Size.x <= 10.0f || vp->Size.y <= 10.0f) return;

    float bottomBarH = bottomBarHeight * uiScale;
    float posX = vp->Pos.x;
    float posY = vp->Pos.y + vp->Size.y - bottomBarH;
    float width = vp->Size.x;

    ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, bottomBarH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                            ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));

    if (ImGui::Begin("###BottomProgressBarWin", nullptr, flags)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        bool isBuildingNow = false;
        float buildP = 0.0f;
        std::string stage;
        {
            std::lock_guard<std::mutex> lock(gameBuildMutex);
            isBuildingNow = isGameBuilding;
            buildP = gameBuildProgress;
            stage = gameBuildStage;
        }

        float fontH = ImGui::GetFontSize();
        float textY = posY + (bottomBarH - fontH) * 0.5f;
        float textX = posX + 12.0f * uiScale;

        // Top border #1f1f23
        drawList->AddLine(ImVec2(posX, posY), ImVec2(posX + width, posY), IM_COL32(31, 31, 35, 255), 1.0f);

        if (isBuildingNow) {
            float fillW = width * std::clamp(buildP, 0.02f, 1.0f);
            drawList->AddRectFilled(ImVec2(posX, posY), ImVec2(posX + fillW, posY + bottomBarH), IM_COL32(16, 185, 129, 180));

            char buf[512];
            snprintf(buf, sizeof(buf), "Packaging Standalone Game [%d%%] : %s", (int)(buildP * 100.0f), stage.c_str());
            drawList->AddText(ImVec2(textX, textY), IM_COL32(244, 244, 245, 255), buf);
        } else if (m_bottomProgress.completionTimer > 0.0f) {
            drawList->AddRectFilled(ImVec2(posX, posY), ImVec2(posX + width, posY + bottomBarH),
                                    m_bottomProgress.success ? IM_COL32(16, 185, 129, 120) : IM_COL32(239, 68, 68, 120));
            drawList->AddText(ImVec2(textX, textY), IM_COL32(244, 244, 245, 255), m_bottomProgress.title.c_str());
        } else {
            drawList->AddText(ImVec2(textX, textY), IM_COL32(161, 161, 170, 255), "Ready | DirectX 12 High Performance Renderer | All Assets Synchronized");
        }

        // Right side: Emerald dot #10b981 + "Engine Live" (dev.md StatusBar)
        const char* liveText = "Engine Live";
        ImVec2 liveSize = ImGui::CalcTextSize(liveText);
        float dotRadius = 3.5f * uiScale;
        float rightMargin = 14.0f * uiScale;
        float liveX = posX + width - rightMargin - liveSize.x;
        float dotX = liveX - 10.0f * uiScale;
        float dotY = posY + bottomBarH * 0.5f;

        drawList->AddCircleFilled(ImVec2(dotX, dotY), dotRadius, IM_COL32(16, 185, 129, 255));
        drawList->AddText(ImVec2(liveX, textY), IM_COL32(161, 161, 170, 255), liveText);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void EngineUI::RenderTopMenuBar(Scene& scene, OrbitCamera& camera, bool& outShouldExit) {
    RenderCustomTitleBar(scene, outShouldExit);
    RenderMainMenuBar(scene, camera, outShouldExit);
}

void EngineUI::RenderCustomTitleBar(Scene& scene, bool& outShouldExit) {
    // Merged into unified single-row RenderMainMenuBar per dev.md specification
}
void EngineUI::RenderMainMenuBar(Scene& scene, OrbitCamera& camera, bool& outShouldExit) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float topBarW = vp->Size.x;
    float topBarH = topBarHeight;
    float topBarY = vp->Pos.y + (tabBarHeight * uiScale);

    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, topBarY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(topBarW, topBarH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * uiScale, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.035f, 0.043f, 1.0f)); // #09090b

    if (ImGui::Begin("TopMenuBarWin", nullptr, flags)) {
        if (ImGui::BeginMenuBar()) {
            // Menu Bar Dropdowns
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Level", "Ctrl+N")) {
                    scene.Clear();
                    currentLevelFilePath = "";
                    AddLog("LogWorld", "Cleared Level (New Level)", 0);
                }
                if (ImGui::MenuItem("Open Level...", "Ctrl+O")) {
                    OpenLevel(scene);
                }
                if (ImGui::MenuItem("Save Level", "Ctrl+S")) {
                    SaveLevel(scene);
                }
                if (ImGui::MenuItem("Save Level As...", "Ctrl+Shift+S")) {
                    SaveLevelAs(scene);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Default Level")) {
                    scene.LoadDefaultScene();
                    currentLevelFilePath = "";
                    ResetTreeStateApplication();
                    AddLog("LogWorld", "Loaded Default Level", 0);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Project Browser...", "Ctrl+Shift+P")) {
                    showProjectBrowser = true;
                    projectBrowserTab = 0;
                }
                if (ImGui::MenuItem("New Project...")) {
                    showProjectBrowser = true;
                    projectBrowserTab = 1;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Compile", "F7", false, !isCompilingScripts)) {
                    TriggerCompileScripts(&scene);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) {
                    outShouldExit = true;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit")) {
                std::string undoLabel = "Undo";
                if (UndoManager::Get().CanUndo()) undoLabel += " " + UndoManager::Get().GetUndoActionName();
                if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, UndoManager::Get().CanUndo())) {
                    UndoManager::Get().Undo(scene);
                    AddLog("LogEditor", "Undo: " + undoLabel, 0);
                }

                std::string redoLabel = "Redo";
                if (UndoManager::Get().CanRedo()) redoLabel += " " + UndoManager::Get().GetRedoActionName();
                if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, UndoManager::Get().CanRedo())) {
                    UndoManager::Get().Redo(scene);
                    AddLog("LogEditor", "Redo: " + redoLabel, 0);
                }
                ImGui::Separator();
                ImGui::MenuItem("Undo History (52 Steps)", nullptr, &showUndoHistory);
                ImGui::Separator();

                if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, scene.selectedId != -1)) {
                    UndoManager::Get().RecordSnapshot(scene, "Duplicate Actor");
                    GameObject* copy = scene.DuplicateObject(scene.selectedId);
                    if (copy) {
                        selectedActorIds.clear();
                        selectedActorIds.insert(copy->id);
                        scene.selectedId = copy->id;
                        std::string logMsg = "Duplicated Actor: " + copy->name;
                        if (!copy->childIds.empty()) {
                            logMsg += " (along with " + std::to_string(copy->childIds.size()) + " children)";
                        }
                        AddLog("LogActor", logMsg, 2);
                        EngineLogger::Get().LogAction("DUPLICATE_ACTOR", copy->name, "Children: " + std::to_string(copy->childIds.size()));
                    }
                }
                if (ImGui::MenuItem("Delete", "Del", false, scene.selectedId != -1)) {
                    GameObject* o = scene.GetSelected();
                    if (o) {
                        UndoManager::Get().RecordSnapshot(scene, "Delete Actor: " + o->name);
                        AddLog("LogActor", "Deleted Actor: " + o->name, 1);
                    }
                    scene.RemoveObject(scene.selectedId);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Deselect All", "Esc")) {
                    scene.selectedId = -1;
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("Editor")) {
                    if (ImGui::BeginMenu("Appearance")) {
                        if (ImGui::BeginMenu("UI Scale")) {
                            int currentPercent = (int)std::round(uiScale * 100.0f);
                            char headerText[64];
                            snprintf(headerText, sizeof(headerText), "Current Scale: %d%%", currentPercent);
                            ImGui::TextDisabled("%s", headerText);
                            ImGui::Separator();

                            // Realtime slider for continuous adjustment between 25% and 250%
                            int sliderPercent = currentPercent;
                            ImGui::SetNextItemWidth(180.0f);
                            if (ImGui::SliderInt("##UIScaleSlider", &sliderPercent, 25, 250, "%d%%")) {
                                ApplyUIScale(sliderPercent / 100.0f);
                                SaveEditorConfig();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Drag to adjust editor UI scale in real time (25% - 250%)");
                            }

                            ImGui::Separator();
                            ImGui::TextDisabled("Presets:");

                            struct ScalePreset {
                                int percent;
                                const char* label;
                            };
                            static const ScalePreset presets[] = {
                                {  25, " 25% (Tiny)" },
                                {  50, " 50% (Small)" },
                                {  75, " 75% (Compact)" },
                                { 100, "100% (Default)" },
                                { 125, "125% (Medium)" },
                                { 150, "150% (Large)" },
                                { 175, "175% (Extra Large)" },
                                { 200, "200% (2x High-DPI)" },
                                { 225, "225% (2.25x)" },
                                { 250, "250% (2.5x Maximum)" }
                            };

                            for (const auto& p : presets) {
                                bool isSelected = (currentPercent == p.percent);
                                char itemLabel[64];
                                snprintf(itemLabel, sizeof(itemLabel), "%s##preset_%d", p.label, p.percent);
                                if (ImGui::MenuItem(itemLabel, nullptr, isSelected)) {
                                    ApplyUIScale(p.percent / 100.0f);
                                    SaveEditorConfig();
                                    AddLog("LogEditor", "UI Scale set to " + std::to_string(p.percent) + "%", 0);
                                }
                            }

                            ImGui::Separator();
                            if (ImGui::MenuItem("ÃƒÂ¢Ã¢â‚¬Â Ã‚Âº Reset to 100% Default", nullptr, false, currentPercent != 100)) {
                                ApplyUIScale(1.0f);
                                SaveEditorConfig();
                                AddLog("LogEditor", "UI Scale reset to 100%", 0);
                            }

                            ImGui::EndMenu();
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Graphics")) {

                // ================================================================
                // SHADOWS (Max Shadow Distance & Realtime Controls)
                // ================================================================
                if (ImGui::BeginMenu("Shadows")) {
                    if (ImGui::Checkbox("Enable Realtime Shadows", &scene.enableShadows)) {
                        AddLog("LogRenderer", scene.enableShadows ? "Shadows Enabled" : "Shadows Disabled", 0);
                    }
                    ImGui::Separator();

                    ImGui::TextDisabled("Distance Range & Culling:");
                    ImGui::Text("Max Shadow Distance:");
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::SliderFloat("##MaxShadowDist", &scene.shadowDistance, 5.0f, 500.0f, "%.1f m")) {
                        for (auto& obj : scene.objects) {
                            if ((obj.isLight || IsLightPrimitive(obj.type)) && obj.light.type == LightType::Directional) {
                                obj.light.shadowDistance = scene.shadowDistance;
                            }
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Maximum distance from camera to render shadows (%.1fm).\nOnly shadows within this range will be rendered and showed.\nShadows beyond this distance are completely culled.", scene.shadowDistance);
                    }

                    // Distance Presets
                    ImGui::TextDisabled("Quick Distance Presets:");
                    auto SetShadowDist = [&](float d) {
                        scene.shadowDistance = d;
                        for (auto& obj : scene.objects) {
                            if ((obj.isLight || IsLightPrimitive(obj.type)) && obj.light.type == LightType::Directional) {
                                obj.light.shadowDistance = d;
                            }
                        }
                    };
                    if (ImGui::MenuItem("Near (25 m)", nullptr, scene.shadowDistance == 25.0f)) SetShadowDist(25.0f);
                    if (ImGui::MenuItem("Medium / Balanced (60 m)", nullptr, scene.shadowDistance == 60.0f)) SetShadowDist(60.0f);
                    if (ImGui::MenuItem("Far (120 m)", nullptr, scene.shadowDistance == 120.0f)) SetShadowDist(120.0f);
                    if (ImGui::MenuItem("Very Far (250 m)", nullptr, scene.shadowDistance == 250.0f)) SetShadowDist(250.0f);
                    if (ImGui::MenuItem("Extreme (500 m)", nullptr, scene.shadowDistance == 500.0f)) SetShadowDist(500.0f);

                    ImGui::Separator();
                    ImGui::TextDisabled("Shadow Quality:");
                    const int resolutions[] = { 512, 1024, 2048, 4096 };
                    const char* resLabels[] = { "512 (Low)", "1024 (Medium)", "2048 (High)", "4096 (Ultra)" };
                    for (int r = 0; r < 4; ++r) {
                        bool isCur = (scene.shadowResolution == resolutions[r]);
                        if (ImGui::MenuItem(resLabels[r], nullptr, isCur)) {
                            scene.shadowResolution = resolutions[r];
                            for (auto& obj : scene.objects) {
                                if ((obj.isLight || IsLightPrimitive(obj.type)) && obj.light.type == LightType::Directional) {
                                    obj.light.shadowResolution = resolutions[r];
                                }
                            }
                        }
                    }

                    ImGui::Separator();
                    ImGui::TextDisabled("Shadow Tuning:");
                    ImGui::SetNextItemWidth(180.0f);
                    ImGui::SliderFloat("Strength", &scene.shadowStrength, 0.0f, 1.0f, "%.2f");
                    ImGui::SetNextItemWidth(180.0f);
                    ImGui::SliderFloat("Bias", &scene.shadowBias, 0.0001f, 0.01f, "%.4f");
                    ImGui::SetNextItemWidth(180.0f);
                    ImGui::SliderFloat("Softness (PCF)", &scene.pcfRadius, 0.5f, 4.0f, "%.1f");

                    ImGui::Separator();
                    ImGui::MenuItem("Show Light Frustum Wireframe", nullptr, &scene.showLightFrustum);

                    ImGui::EndMenu();
                }

                // ================================================================
                // POST PROCESSING
                // ================================================================
                if (ImGui::BeginMenu("Post Processing")) {
                    if (ImGui::Checkbox("Enable Post Processing", &scene.enablePostProcessing))
                        AddLog("LogWorld", scene.enablePostProcessing ? "Post Processing Enabled" : "Post Processing Disabled", 0);
                    ImGui::Separator();

                    ImGui::TextDisabled("HDR & Brightness:");
                    ImGui::SliderFloat("EV Compensation",    &scene.postExposureEV,         -5.0f, 5.0f, "%.2f EV");
                    ImGui::SliderFloat("Exposure Multiplier",&scene.postExposureMultiplier,  0.0f, 5.0f, "%.2f");
                    ImGui::Separator();

                    ImGui::TextDisabled("Color Grading:");
                    const char* tonemappers[] = { "None / Linear", "ACES Filmic (Wicked/UE5)", "Reinhard", "Uchimura (GT7 / Wicked)", "Uncharted 2 / Filmic (NVIDIA Falcor)" };
                    ImGui::Combo("Tonemapper",   &scene.postTonemapper, tonemappers, IM_ARRAYSIZE(tonemappers));
                    ImGui::SliderFloat("Saturation",  &scene.postSaturation,       0.0f, 2.0f, "%.2f");
                    ImGui::SliderFloat("Contrast",    &scene.postContrast,          0.5f, 2.0f, "%.2f");
                    ImGui::SliderFloat("Gamma",       &scene.postGamma,             0.5f, 2.0f, "%.2f");
                    ImGui::ColorEdit3("Color Filter", &scene.postColorFilter.x);
                    ImGui::SliderFloat("Temperature", &scene.postColorTemperature, 2000.0f, 12000.0f, "%.0f K");
                    ImGui::SliderFloat("Tint",        &scene.postColorTint,        -1.0f, 1.0f, "%.2f");
                    ImGui::Separator();

                    ImGui::TextDisabled("Detail & Filtering:");
                    ImGui::SliderFloat("Sharpen Filter", &scene.postSharpenAmount, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Denoiser",       &scene.postDenoiserAmount, 0.0f, 1.0f, "%.2f");
                    ImGui::Separator();

                    if (ImGui::MenuItem("Post Processing Settings...", nullptr, showPostProcessingModal)) showPostProcessingModal = true;
                    ImGui::EndMenu();
                }

                // ================================================================
                // AMBIENT OCCLUSION (Screen Space Ambient Occlusion - WickedEngine Reference)
                // ================================================================
                if (ImGui::BeginMenu("Ambient Occlusion")) {
                    if (ImGui::Checkbox("Enable Ambient Occlusion", &scene.enableAO)) {
                        AddLog("LogRenderer", scene.enableAO ? "Ambient Occlusion Enabled" : "Ambient Occlusion Disabled", 0);
                        Eunoia::PostProcess::PostProcessSystem::Get().SetAOMode(scene.enableAO ? (Eunoia::PostProcess::SSAOMode)scene.aoType : Eunoia::PostProcess::SSAOMode::Disabled);
                        RequestViewportRedraw(15);
                    }
                    ImGui::Separator();

                    ImGui::TextDisabled("AO Algorithm (WickedEngine Reference):");
                    const char* aoAlgorithms[] = {
                        "Disabled",
                        "SSAO (Simple Screen Space AO)",
                        "HBAO (Horizon-Based AO)",
                        "MSAO (Multi-Scale AO)",
                        "RTAO (Ray-Traced AO)"
                    };
                    int curAoType = scene.aoType;
                    if (ImGui::Combo("Algorithm", &curAoType, aoAlgorithms, IM_ARRAYSIZE(aoAlgorithms))) {
                        scene.aoType = curAoType;
                        if (scene.aoType > 0) {
                            scene.enableAO = true;
                        } else {
                            scene.enableAO = false;
                        }
                        Eunoia::PostProcess::PostProcessSystem::Get().SetAOMode((Eunoia::PostProcess::SSAOMode)scene.aoType);
                        AddLog("LogRenderer", std::string("AO Algorithm switched to: ") + aoAlgorithms[scene.aoType], 0);
                        RequestViewportRedraw(15);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Choose Ambient Occlusion type (Reference: WickedEngine GraphicsWindow.cpp):\n"
                                          "- SSAO: Standard hemisphere ray sampling\n"
                                          "- HBAO: Horizon-Based AO with angular integration\n"
                                          "- MSAO: Multi-Scale AO with depth-aware upsampling\n"
                                          "- RTAO: Hardware DXR ray-traced ambient occlusion");
                    }

                    ImGui::Separator();
                    ImGui::TextDisabled("AO Properties (WickedEngine Parameters):");

                    // 1. Power (WickedEngine aoPowerSlider: 0.25 - 8.0)
                    ImGui::SetNextItemWidth(190.0f);
                    if (ImGui::SliderFloat("Power", &scene.aoPower, 0.25f, 8.0f, "%.2f")) {
                        Eunoia::PostProcess::PostProcessSystem::Get().GetSettingsRef().sse.ao.power = scene.aoPower;
                        RequestViewportRedraw(15);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Set SSAO Power (WickedEngine aoPower: 0.25 - 8.0).\nHigher values produce darker, more pronounced occlusion in crevices and corners.");
                    }

                    // 2. Range (WickedEngine aoRangeSlider: 0.1 - 20.0 m)
                    ImGui::SetNextItemWidth(190.0f);
                    if (ImGui::SliderFloat("Range (Radius)", &scene.aoRange, 0.1f, 20.0f, "%.2f m")) {
                        Eunoia::PostProcess::PostProcessSystem::Get().GetSettingsRef().sse.ao.radius = scene.aoRange;
                        RequestViewportRedraw(15);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Set AO ray length / search radius in world space (WickedEngine aoRange).\nControls the spread distance of ambient contact shadows.");
                    }

                    // 3. Sample Count (WickedEngine aoSampleCountSlider: 1 - 64)
                    ImGui::SetNextItemWidth(190.0f);
                    if (ImGui::SliderInt("Sample Count", &scene.aoSampleCount, 1, 64)) {
                        Eunoia::PostProcess::PostProcessSystem::Get().GetSettingsRef().sse.ao.sampleCount = scene.aoSampleCount;
                        RequestViewportRedraw(15);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Set AO ray sample count per pixel (WickedEngine aoSampleCount: 1 - 32).\nHigher values reduce noise at the cost of GPU performance.");
                    }

                    // 4. Bias
                    ImGui::SetNextItemWidth(190.0f);
                    if (ImGui::SliderFloat("Bias", &scene.aoBias, 0.001f, 0.1f, "%.4f")) {
                        Eunoia::PostProcess::PostProcessSystem::Get().GetSettingsRef().sse.ao.bias = scene.aoBias;
                        RequestViewportRedraw(15);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Angular/depth self-occlusion bias to prevent acne/shadow artifacts on planar surfaces.");
                    }

                    ImGui::Separator();
                    ImGui::TextDisabled("Filtering & Temporal:");
                    if (ImGui::Checkbox("Bilateral Blur", &scene.aoBlur)) {
                        Eunoia::PostProcess::PostProcessSystem::Get().GetSettingsRef().sse.ao.enableBlur = scene.aoBlur;
                        RequestViewportRedraw(15);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Edge-preserving cross-bilateral depth blur (WickedEngine msao_blurupsampleCS).");
                    }
                    if (scene.aoBlur) {
                        ImGui::SetNextItemWidth(190.0f);
                        if (ImGui::SliderFloat("Blur Radius", &scene.aoBlurRadius, 0.5f, 5.0f, "%.1f")) {
                            RequestViewportRedraw(15);
                        }
                    }

                    if (ImGui::Checkbox("Temporal Accumulation", &scene.aoTemporal)) {
                        Eunoia::PostProcess::PostProcessSystem::Get().GetSettingsRef().sse.ao.enableTemporalAO = scene.aoTemporal;
                        RequestViewportRedraw(15);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Accumulates ambient occlusion samples across consecutive frames using history buffer.");
                    }

                    ImGui::Separator();
                    ImGui::TextDisabled("Quick Presets:");
                    if (ImGui::MenuItem("Disabled", nullptr, !scene.enableAO || scene.aoType == 0)) {
                        scene.enableAO = false; scene.aoType = 0;
                        Eunoia::PostProcess::PostProcessSystem::Get().SetAOMode(Eunoia::PostProcess::SSAOMode::Disabled);
                        RequestViewportRedraw(15);
                    }
                    if (ImGui::MenuItem("Low (8 Samples, Range 0.8m, Power 1.0)")) {
                        scene.enableAO = true; scene.aoType = 1; scene.aoSampleCount = 8; scene.aoRange = 0.8f; scene.aoPower = 1.0f; scene.aoBlur = true;
                        Eunoia::PostProcess::PostProcessSystem::Get().ApplySSEQualityPreset(1);
                        RequestViewportRedraw(15);
                    }
                    if (ImGui::MenuItem("Medium (16 Samples, Range 1.0m, Power 1.5)")) {
                        scene.enableAO = true; scene.aoType = 1; scene.aoSampleCount = 16; scene.aoRange = 1.0f; scene.aoPower = 1.5f; scene.aoBlur = true;
                        Eunoia::PostProcess::PostProcessSystem::Get().ApplySSEQualityPreset(2);
                        RequestViewportRedraw(15);
                    }
                    if (ImGui::MenuItem("High (24 Samples, Range 1.5m, Power 1.8)")) {
                        scene.enableAO = true; scene.aoType = 1; scene.aoSampleCount = 24; scene.aoRange = 1.5f; scene.aoPower = 1.8f; scene.aoBlur = true;
                        Eunoia::PostProcess::PostProcessSystem::Get().ApplySSEQualityPreset(3);
                        RequestViewportRedraw(15);
                    }
                    if (ImGui::MenuItem("Ultra (32 Samples, Range 2.0m, Power 2.0)")) {
                        scene.enableAO = true; scene.aoType = 1; scene.aoSampleCount = 32; scene.aoRange = 2.0f; scene.aoPower = 2.0f; scene.aoBlur = true;
                        Eunoia::PostProcess::PostProcessSystem::Get().ApplySSEQualityPreset(4);
                        RequestViewportRedraw(15);
                    }

                    ImGui::Separator();
                    if (ImGui::MenuItem("Ambient Occlusion Settings...", nullptr, showAmbientOcclusionModal)) showAmbientOcclusionModal = true;

                    ImGui::EndMenu();
                }

                // ================================================================
                // ANTI-ALIASING (From Adria: FXAA & Catmull-Rom Bicubic TAA)
                // ================================================================
                if (ImGui::BeginMenu("Anti-Aliasing")) {
                    const auto& methods = GetAntiAliasingMethods();
                    for (const auto& m : methods) {
                        bool isSelected = (currentAntiAliasing == m.method);
                        std::string label = m.name;
                        if (m.method == AntiAliasingMethod::FXAA) {
                            label += " (Adria Luma Span)";
                        } else if (m.method == AntiAliasingMethod::TAA) {
                            label += " (Adria Catmull-Rom Bicubic)";
                        }

                        if (ImGui::MenuItem(label.c_str(), m.shortcutHint, isSelected)) {
                            currentAntiAliasing = m.method;
                            Eunoia::GraphicsSystem::Get().SetAntiAliasingMethod(m.method);
                            SaveEditorConfig();
                            AddLog("LogRenderer", std::string("Anti-Aliasing set to: ") + m.name + " (" + m.role + ")", 0);
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Quality: %s\nCost: %s\nRole: %s", m.quality, m.cost, m.role);
                        }
                    }
                    ImGui::EndMenu();
                }

                // ================================================================
                // TEXTURE RESOLUTION & STREAMING
                // ================================================================
                if (ImGui::BeginMenu("Texture")) {
                    ImGui::TextDisabled("Max Streaming Resolution:");
                    ImGui::Separator();

                    struct TexResOption {
                        TextureQuality quality;
                        const char* label;
                        const char* desc;
                    };
                    static const TexResOption resOptions[] = {
                        { TextureQuality::None,   "Native (Unconstrained)", "Render all textures at native resolution without clamp" },
                        { TextureQuality::Max,    "4K (4096 x 4096)",       "Max texture resolution 4K. Textures > 4K are clamped to 4K" },
                        { TextureQuality::Ultra,  "2K (2048 x 2048)",       "Max texture resolution 2K. 4K textures in level stream as 2K" },
                        { TextureQuality::High,   "1K (1024 x 1024)",       "Max texture resolution 1K. Textures > 1K stream as 1K" },
                        { TextureQuality::Medium, "512 x 512",              "Max texture resolution 512. Textures > 512 stream as 512" },
                        { TextureQuality::Low,    "256 x 256",              "Max texture resolution 256. Textures > 256 stream as 256" }
                    };

                    for (const auto& opt : resOptions) {
                        bool isSelected = (currentTextureQuality == opt.quality);
                        if (ImGui::MenuItem(opt.label, nullptr, isSelected)) {
                            ApplyTextureQuality(opt.quality);
                            SaveEditorConfig();
                            AddLog("LogRenderer", std::string("Max Texture Streaming Resolution set to: ") + opt.label, 0);
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s\n(Textures natively <= max limit remain unscaled; higher resolutions clamped via D3D12 MinLODClamp)", opt.desc);
                        }
                    }

                    ImGui::EndMenu();
                }

                // ================================================================
                // VSYNC & FRAME SYNC
                // ================================================================
                ImGui::Separator();
                if (ImGui::MenuItem("VSync (Vertical Sync)", nullptr, vsyncEnabled)) {
                    vsyncEnabled = !vsyncEnabled;
                    SaveEditorConfig();
                    AddLog("LogRenderer", vsyncEnabled ? "VSync Enabled (capped to monitor refresh rate)" : "VSync Disabled (unlocked high FPS)", 0);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Toggle VSync: When OFF, frame rate unlocks for maximum FPS performance.\nWhen ON, syncs with monitor refresh rate to eliminate screen tearing.");
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window")) {
                ImGui::MenuItem("Outliner", nullptr, &showOutliner);
                ImGui::MenuItem("Details", nullptr, &showDetails);
                ImGui::MenuItem("Content Browser", nullptr, &showContentBrowserWindow);
                ImGui::MenuItem("Animation", nullptr, &showAnimationWindow);
                ImGui::MenuItem("Output Log", nullptr, &showOutputLogWindow);
                ImGui::MenuItem("Viewport Overlay", nullptr, &showViewportOverlay);
                if (ImGui::BeginMenu("Editor Gizmos")) {
                    ImGui::MenuItem("Show Light Gizmos", nullptr, &Eunoia::EditorGizmoSystem::Get().showLightGizmos);
                    ImGui::MenuItem("Show Camera Gizmos", nullptr, &Eunoia::EditorGizmoSystem::Get().showCameraGizmos);
                    ImGui::Separator();
                    ImGui::MenuItem("Show Light Ranges", nullptr, &Eunoia::EditorGizmoSystem::Get().showLightRanges);
                    ImGui::MenuItem("Show Light Directions", nullptr, &Eunoia::EditorGizmoSystem::Get().showLightDirections);
                    ImGui::MenuItem("Show Camera Frustums", nullptr, &Eunoia::EditorGizmoSystem::Get().showCameraFrustums);
                    ImGui::MenuItem("Show Clip Planes", nullptr, &Eunoia::EditorGizmoSystem::Get().showClipPlanes);
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("ÃƒÂ¢Ã¢â‚¬Â Ã‚Âº Reset Layout to Default")) {
                    leftSidebarWidth = 280.0f;
                    rightSidebarWidth = 320.0f;
                    bottomDockHeight = 260.0f;
                    contentBrowserFolderTreeWidth = 220.0f;
                    contentThumbnailSize = 96.0f;
                    SaveEditorConfig();
                    AddLog("LogEditor", "Reset editor layout to defaults (280 / 320 / 260, 96px thumbnails)", 0);
                }
                ImGui::Separator();
                ImGui::MenuItem("Undo History (52 Steps)", nullptr, &showUndoHistory);
                ImGui::MenuItem("Material Editor", nullptr, &showMaterialEditor);
                ImGui::MenuItem("Asset Reference Viewer", nullptr, &showReferenceViewer);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("World")) {
                if (ImGui::BeginMenu("Level Camera")) {
                    bool noneSelected = (scene.activeLevelCameraId == -1);
                    if (ImGui::MenuItem("None (Viewport Camera)", nullptr, noneSelected)) {
                        scene.activeLevelCameraId = -1;
                        AddLog("LogCamera", "Level Camera set to: None (Viewport Camera)", 0);
                    }
                    ImGui::Separator();

                    int cameraCount = 0;
                    for (const auto& obj : scene.objects) {
                        if (obj.isCamera || obj.type == PrimitiveType::Camera) {
                            cameraCount++;
                            bool isCurrent = (scene.activeLevelCameraId == obj.id);
                            std::string itemLabel = obj.name + " (ID: " + std::to_string(obj.id) + ")";
                            if (ImGui::MenuItem(itemLabel.c_str(), nullptr, isCurrent)) {
                                scene.activeLevelCameraId = obj.id;
                                AddLog("LogCamera", "Selected Level Camera: " + obj.name, 0);
                            }
                        }
                    }

                    if (cameraCount == 0) {
                        ImGui::TextDisabled("  (No cameras in level)");
                        ImGui::Separator();
                        if (ImGui::MenuItem("+ Add Camera to Level")) {
                            GameObject& newCam = scene.AddObject(PrimitiveType::Camera, {0.0f, 2.0f, -4.0f});
                            newCam.name = "Camera Actor";
                            newCam.rotation = {0.0f, 0.0f, 0.0f};
                            scene.activeLevelCameraId = newCam.id;
                            AddLog("LogCamera", "Created and set Level Camera: " + newCam.name, 2);
                        }
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();
                if (ImGui::BeginMenu("Skybox")) {
                    if (ImGui::MenuItem("Enable Skybox", nullptr, scene.enableSkybox)) {
                        scene.enableSkybox = !scene.enableSkybox;
                        AddLog("LogWorld", scene.enableSkybox ? "Enabled World Skybox" : "Disabled World Skybox (Pitch Dark)", 0);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Skybox Settings...", nullptr, showSkyboxSettingsModal)) {
                        showSkyboxSettingsModal = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("Volumetric Fog")) {
                    if (ImGui::MenuItem("Enable Volumetric Fog", nullptr, scene.enableFog)) {
                        scene.enableFog = !scene.enableFog;
                        AddLog("LogWorld", scene.enableFog ? "Enabled Volumetric Height Fog" : "Disabled Volumetric Height Fog", 0);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Volumetric Fog Settings...", nullptr, showVolumetricFogModal)) {
                        showVolumetricFogModal = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Assets")) {
                if (ImGui::MenuItem("Scan & Sync Asset Registry")) {
                    SyncRegistryWithUIProgress(contentRootPath);
                    AddLog("LogAsset", "Rescanned and synchronized Asset Registry (" + std::to_string(AssetRegistry::Get().GetAssetCount()) + " assets)", 2);
                }
                if (ImGui::MenuItem("Asset Reference Viewer")) {
                    OpenReferenceViewer();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Build")) {
                if (ImGui::MenuItem("ÃƒÂ¢Ã…Â¡Ã‚Â¡ Compile Behaviour Scripts", "F7", false, !isCompilingScripts)) {
                    TriggerCompileScripts(&scene);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Build Game Project (Stand-Alone)...", "Ctrl+B", false, !isGameBuilding)) {
                    StartGameBuildAsync(scene);
                }

                if (ImGui::MenuItem(scene.isPlayMode ? "ÃƒÂ¢Ã‚ÂÃ‚Â¹ Stop Play Mode" : "ÃƒÂ¢Ã¢â‚¬â€œÃ‚Â¶ Play in Editor (Full Screen)", "Ctrl+Delete")) {
                    if (scene.isPlayMode) ExitPlayMode(scene, currentCamera);
                    else EnterPlayMode(scene, currentCamera);
                }
                if (ImGui::MenuItem("Open Project Build Folder")) {
                    std::filesystem::path projectBase = activeProjectRoot.empty() ? contentRootPath.parent_path() : activeProjectRoot;
                    std::filesystem::path gameBuildDir = projectBase / "Build";
                    ShellExecuteA(NULL, "open", gameBuildDir.string().c_str(), NULL, NULL, SW_SHOW);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("About Eunoia Engine")) {
                    AddLog("Help", "Eunoia Engine v2.0 - Powered by EunoiaEngine.", 3);
                    showHelpModal = true;
                }
                ImGui::EndMenu();
            }

            // ================================================================
            // VIEWPORT & TOOL TOGGLES (dev.md TopMenuBar)
            // ================================================================
            const ImVec4 accentBlue     = ImVec4(0.000f, 0.400f, 1.000f, 1.0f); // #0066ff
            const ImVec4 accentEmerald  = ImVec4(0.063f, 0.725f, 0.506f, 1.0f); // #10b981
            const ImVec4 panelBg        = ImVec4(0.071f, 0.071f, 0.078f, 1.0f); // #121214
            const ImVec4 textLight      = ImVec4(1.000f, 1.000f, 1.000f, 1.0f);
            const ImVec4 textMuted      = ImVec4(0.631f, 0.631f, 0.667f, 1.0f); // #a1a1aa
            const ImVec4 redStop        = ImVec4(0.863f, 0.149f, 0.149f, 1.0f); // #dc2626
            const ImVec4 borderCol      = ImVec4(0.122f, 0.122f, 0.137f, 1.0f); // #1f1f23

            float btnHeight = 20.0f * uiScale;

            // Divider |
            ImGui::SameLine(0.0f, 6.0f * uiScale);
            ImGui::TextColored(borderCol, "|");
            ImGui::SameLine(0.0f, 6.0f * uiScale);

            // Group 1: Render Mode [Lit] [Unlit]
            bool isLit = (viewMode == 0);
            bool isUnlit = (viewMode == 2);

            ImGui::PushStyleColor(ImGuiCol_Button, isLit ? accentBlue : panelBg);
            ImGui::PushStyleColor(ImGuiCol_Text, isLit ? textLight : textMuted);
            if (ImGui::Button("Lit", ImVec2(34.0f * uiScale, btnHeight))) { viewMode = 0; RequestViewportRedraw(10); }
            ImGui::PopStyleColor(2);

            ImGui::SameLine(0.0f, 3.0f * uiScale);
            ImGui::PushStyleColor(ImGuiCol_Button, isUnlit ? accentBlue : panelBg);
            ImGui::PushStyleColor(ImGuiCol_Text, isUnlit ? textLight : textMuted);
            if (ImGui::Button("Unlit", ImVec2(42.0f * uiScale, btnHeight))) { viewMode = 2; RequestViewportRedraw(10); }
            ImGui::PopStyleColor(2);

            // Divider |
            ImGui::SameLine(0.0f, 6.0f * uiScale);
            ImGui::TextColored(borderCol, "|");
            ImGui::SameLine(0.0f, 6.0f * uiScale);

            // Group 2: Transform Tools [Loc] [Rot] [Scale]
            bool isTrans = (currentGizmoOperation == ImGuizmo::TRANSLATE);
            bool isRot   = (currentGizmoOperation == ImGuizmo::ROTATE);
            bool isScale = (currentGizmoOperation == ImGuizmo::SCALE);

            ImGui::PushStyleColor(ImGuiCol_Button, isTrans ? accentBlue : panelBg);
            ImGui::PushStyleColor(ImGuiCol_Text, isTrans ? textLight : textMuted);
            if (ImGui::Button("Loc", ImVec2(34.0f * uiScale, btnHeight))) { currentGizmoOperation = ImGuizmo::TRANSLATE; RequestViewportRedraw(10); }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translate Mode (W)");

            ImGui::SameLine(0.0f, 3.0f * uiScale);
            ImGui::PushStyleColor(ImGuiCol_Button, isRot ? accentBlue : panelBg);
            ImGui::PushStyleColor(ImGuiCol_Text, isRot ? textLight : textMuted);
            if (ImGui::Button("Rot", ImVec2(34.0f * uiScale, btnHeight))) { currentGizmoOperation = ImGuizmo::ROTATE; RequestViewportRedraw(10); }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate Mode (E)");

            ImGui::SameLine(0.0f, 3.0f * uiScale);
            ImGui::PushStyleColor(ImGuiCol_Button, isScale ? accentBlue : panelBg);
            ImGui::PushStyleColor(ImGuiCol_Text, isScale ? textLight : textMuted);
            if (ImGui::Button("Scale", ImVec2(44.0f * uiScale, btnHeight))) { currentGizmoOperation = ImGuizmo::SCALE; RequestViewportRedraw(10); }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale Mode (R)");

            // Divider |
            ImGui::SameLine(0.0f, 6.0f * uiScale);
            ImGui::TextColored(borderCol, "|");
            ImGui::SameLine(0.0f, 6.0f * uiScale);

            // Group 3: Space / Snapping [Local] [Pivot] [Snap]
            bool isLocal = (currentGizmoMode == ImGuizmo::LOCAL);
            ImGui::PushStyleColor(ImGuiCol_Button, isLocal ? accentBlue : panelBg);
            ImGui::PushStyleColor(ImGuiCol_Text, isLocal ? textLight : textMuted);
            if (ImGui::Button(isLocal ? "Local" : "World", ImVec2(44.0f * uiScale, btnHeight))) {
                currentGizmoMode = isLocal ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
                RequestViewportRedraw(10);
            }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle World/Local coordinate space (Q)");

            ImGui::SameLine(0.0f, 3.0f * uiScale);
            ImGui::PushStyleColor(ImGuiCol_Button, panelBg);
            ImGui::PushStyleColor(ImGuiCol_Text, textMuted);
            if (ImGui::Button("Pivot", ImVec2(40.0f * uiScale, btnHeight))) {
                RequestViewportRedraw(10);
            }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Actor Pivot Mode");

            ImGui::SameLine(0.0f, 3.0f * uiScale);
            ImGui::PushStyleColor(ImGuiCol_Button, useSnap ? accentBlue : panelBg);
            ImGui::PushStyleColor(ImGuiCol_Text, useSnap ? textLight : textMuted);
            if (ImGui::Button("Snap", ImVec2(40.0f * uiScale, btnHeight))) {
                useSnap = !useSnap;
                RequestViewportRedraw(10);
            }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Grid Snapping (%s)", useSnap ? "ON" : "OFF");

            // Divider |
            ImGui::SameLine(0.0f, 6.0f * uiScale);
            ImGui::TextColored(borderCol, "|");
            ImGui::SameLine(0.0f, 6.0f * uiScale);

            // Group 4: Realtime & Play
            if (realtimeViewport) {
                ImGui::PushStyleColor(ImGuiCol_Button, accentEmerald);
                ImGui::PushStyleColor(ImGuiCol_Text, textLight);
                if (ImGui::Button("Realtime", ImVec2(62.0f * uiScale, btnHeight))) {
                    realtimeViewport = false;
                    RequestViewportRedraw(10);
                }
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Continuous Rendering. Click to switch to On-Demand.");
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, panelBg);
                ImGui::PushStyleColor(ImGuiCol_Text, textMuted);
                if (ImGui::Button("On-Demand", ImVec2(72.0f * uiScale, btnHeight))) {
                    realtimeViewport = true;
                }
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("On-Demand Rendering Active. Click to switch to continuous Realtime.");
            }

            ImGui::SameLine(0.0f, 4.0f * uiScale);
            if (!scene.isPlayMode) {
                ImGui::PushStyleColor(ImGuiCol_Button, accentEmerald);
                ImGui::PushStyleColor(ImGuiCol_Text, textLight);
                if (ImGui::Button("Play", ImVec2(50.0f * uiScale, btnHeight))) {
                    EnterPlayMode(scene, currentCamera);
                }
                ImGui::PopStyleColor(2);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, redStop);
                ImGui::PushStyleColor(ImGuiCol_Text, textLight);
                if (ImGui::Button("Stop", ImVec2(50.0f * uiScale, btnHeight))) {
                    ExitPlayMode(scene, currentCamera);
                }
                ImGui::PopStyleColor(2);
            }

            ImGui::EndMenuBar();
        }

        // Draw bottom border #1f1f23
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(vp->Pos.x, topBarY + topBarH),
                    ImVec2(vp->Pos.x + topBarW, topBarY + topBarH),
                    IM_COL32(31, 31, 35, 255), 1.0f);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}
void EngineUI::RenderWorkspaceTabBar(Scene& scene, bool& outShouldExit) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float barW = vp->Size.x;
    float barH = tabBarHeight * uiScale;
    float barY = vp->Pos.y; // Topmost row in the application window

    HWND hwnd = nullptr;
    bool isMaximized = false;
#ifdef _WIN32
    if (m_window) {
        hwnd = glfwGetWin32Window(m_window);
        if (hwnd) {
            isMaximized = (IsZoomed(hwnd) != 0);
        }
    }
#endif

    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, barY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(barW, barH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * uiScale, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.020f, 0.020f, 0.025f, 1.0f)); // #050506

    if (ImGui::Begin("###WorkspaceTabBarWin", nullptr, flags)) {
        auto& uiMgr = EunoiaUI::UIEditorManager::Get();
        float tabBtnH = 22.0f * uiScale;
        float tabY = (barH - tabBtnH) * 0.5f;
        ImGui::SetCursorPosY(tabY);

        // 0. Eunoia Logo Icon [E]
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.000f, 0.400f, 1.000f, 1.0f)); // #0066ff accent
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::Button(" E ", ImVec2(22.0f * uiScale, tabBtnH));
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Eunoia Engine v2.0");

        ImGui::SameLine(0.0f, 6.0f * uiScale);

        // 1. Level Editor Tab (Always open, permanent root switch)
        bool isLevelActive = (!materialWorkspaceActive && uiMgr.IsMainEditorActive() && !Eunoia::NodeBehaviour::NodeBehaviourManager::Get().IsActive() && !Eunoia::AnimatorGraphManager::Get().IsActive() && !Eunoia::AnimationBlendManager::Get().IsActive() && !Eunoia::BehaviourManager::Get().IsActive() && (s_activePromotedTabIndex < 0 || s_activePromotedTabIndex >= (int)s_promotedTabs.size()));
        std::string levelTabName = "Level Editor";
        if (levelUnsaved) levelTabName += "*";
        levelTabName += "##Tab_Level";

        if (RenderTabWithSaveProgress(levelTabName.c_str(), isLevelActive, m_levelSaveState, tabBtnH)) {
            materialWorkspaceActive = false;
            Eunoia::NodeBehaviour::NodeBehaviourManager::Get().SwitchToMainEditor();
            Eunoia::AnimatorGraphManager::Get().SwitchToMainEditor();
            Eunoia::AnimationBlendManager::Get().SwitchToMainEditor();
            uiMgr.SwitchToMainEditor();
            Eunoia::BehaviourManager::Get().SwitchToMainEditor();
            s_activePromotedTabIndex = -1;
            if (activeMaterialDocIdx >= 0 && activeMaterialDocIdx < (int)openMaterialDocs.size()) {
                PropagateMaterialChangesToScene(scene, openMaterialDocs[activeMaterialDocIdx].material);
            }
        }

        // 2. Material Editor Tabs (Multiple open material editors)
        int matToClose = -1;
        for (size_t i = 0; i < openMaterialDocs.size(); ++i) {
            ImGui::SameLine(0.0f, 4.0f * uiScale);
            bool isMatActive = (materialWorkspaceActive && activeMaterialDocIdx == (int)i);
            std::string matLabel = "[M] " + openMaterialDocs[i].material.name;
            if (openMaterialDocs[i].isDirty) matLabel += "*";
            matLabel += "##MatTab_" + std::to_string(i);

            if (RenderTabWithSaveProgress(matLabel.c_str(), isMatActive, openMaterialDocs[i].saveState, tabBtnH)) {
                Eunoia::NodeBehaviour::NodeBehaviourManager::Get().SwitchToMainEditor();
            Eunoia::AnimatorGraphManager::Get().SwitchToMainEditor();
            Eunoia::AnimationBlendManager::Get().SwitchToMainEditor();
                Eunoia::BehaviourManager::Get().SwitchToMainEditor();
                s_activePromotedTabIndex = -1;
                SwitchToMaterialEditor((int)i);
            }

            // Close button ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¢
            ImGui::SameLine(0.0f, 2.0f * uiScale);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.863f, 0.149f, 0.149f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.10f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.631f, 0.631f, 0.667f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * uiScale, 1.0f * uiScale));

            std::string closeBtnId = "ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¢##CloseMatTab_" + std::to_string(i);
            if (ImGui::Button(closeBtnId.c_str(), ImVec2(16.0f * uiScale, tabBtnH))) {
                matToClose = (int)i;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close %s", openMaterialDocs[i].material.name.c_str());

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        }

        if (matToClose >= 0) {
            CloseMaterialEditor(matToClose);
        }

        // 3. UI Editor Tabs (Multiple open UI editors)
        const auto& uiEditors = uiMgr.GetEditors();
        int uiToClose = -1;
        for (size_t i = 0; i < uiEditors.size(); ++i) {
            ImGui::SameLine(0.0f, 4.0f * uiScale);
            int tabIdx = (int)i + 1;
            bool isUiActive = (!materialWorkspaceActive && uiMgr.activeTabIndex == tabIdx);
            std::string uiLabel = "[UI] " + uiEditors[i]->name;
            if (uiEditors[i]->isDirty) uiLabel += "*";
            uiLabel += "##UiTab_" + std::to_string(i);

            if (RenderTabWithSaveProgress(uiLabel.c_str(), isUiActive, m_uiSaveState, tabBtnH)) {
                materialWorkspaceActive = false;
                Eunoia::NodeBehaviour::NodeBehaviourManager::Get().SwitchToMainEditor();
            Eunoia::AnimatorGraphManager::Get().SwitchToMainEditor();
            Eunoia::AnimationBlendManager::Get().SwitchToMainEditor();
                Eunoia::BehaviourManager::Get().SwitchToMainEditor();
                s_activePromotedTabIndex = -1;
                uiMgr.SwitchToEditor(tabIdx);
            }

            // Close button Ã¢Å“â€¢
            ImGui::SameLine(0.0f, 2.0f * uiScale);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.863f, 0.149f, 0.149f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.10f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.631f, 0.631f, 0.667f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * uiScale, 1.0f * uiScale));

            std::string closeBtnId = "Ã¢Å“â€¢##CloseUiTab_" + std::to_string(i);
            if (ImGui::Button(closeBtnId.c_str(), ImVec2(16.0f * uiScale, tabBtnH))) {
                uiToClose = (int)i;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close %s", uiEditors[i]->name.c_str());

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        }

        if (uiToClose >= 0) {
            uiMgr.Close((size_t)uiToClose);
        }

        // 3.5 Node Behaviour Tabs (Multiple open Node Behaviour visual scripting editors)
        auto& nbMgr = Eunoia::NodeBehaviour::NodeBehaviourManager::Get();
        const auto& nbEditors = nbMgr.GetEditors();
        int nbToClose = -1;
        for (size_t i = 0; i < nbEditors.size(); ++i) {
            ImGui::SameLine(0.0f, 4.0f * uiScale);
            bool isNbActive = (nbMgr.IsActive() && nbMgr.GetActiveEditorIndex() == (int)i);
            std::string nbLabel = "[NB] " + nbEditors[i]->GetName();
            if (nbEditors[i]->IsDirty()) nbLabel += "*";
            nbLabel += "##NbTab_" + std::to_string(i);

            TabSaveProgressState dummyNbSaveState;
            if (RenderTabWithSaveProgress(nbLabel.c_str(), isNbActive, dummyNbSaveState, tabBtnH)) {
                materialWorkspaceActive = false;
                uiMgr.SwitchToMainEditor();
                Eunoia::AnimatorGraphManager::Get().SwitchToMainEditor();
            Eunoia::AnimationBlendManager::Get().SwitchToMainEditor();
                Eunoia::BehaviourManager::Get().SwitchToMainEditor();
                s_activePromotedTabIndex = -1;
                nbMgr.SwitchToEditor((int)i);
            }

            // Close button Ã¢Å“â€¢
            ImGui::SameLine(0.0f, 2.0f * uiScale);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.863f, 0.149f, 0.149f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.10f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.631f, 0.631f, 0.667f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * uiScale, 1.0f * uiScale));

            std::string closeBtnId = "Ã¢Å“â€¢##CloseNbTab_" + std::to_string(i);
            if (ImGui::Button(closeBtnId.c_str(), ImVec2(16.0f * uiScale, tabBtnH))) {
                nbToClose = (int)i;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close %s", nbEditors[i]->GetName().c_str());

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        }

        if (nbToClose >= 0) {
            nbMgr.Close((size_t)nbToClose);
        }

        // 3.6 Animator Graph Tabs (Multiple open Animator Graph state machine editors)
        auto& agMgr = Eunoia::AnimatorGraphManager::Get();
        const auto& agEditors = agMgr.GetEditors();
        int agToClose = -1;
        for (size_t i = 0; i < agEditors.size(); ++i) {
            ImGui::SameLine(0.0f, 4.0f * uiScale);
            bool isAgActive = (agMgr.IsActive() && agMgr.GetActiveEditorIndex() == (int)i);
            std::string agLabel = "[AG] " + agEditors[i]->GetName();
            if (agEditors[i]->IsDirty()) agLabel += "*";
            agLabel += "##AgTab_" + std::to_string(i);

            TabSaveProgressState dummyAgSaveState;
            if (RenderTabWithSaveProgress(agLabel.c_str(), isAgActive, dummyAgSaveState, tabBtnH)) {
                materialWorkspaceActive = false;
                uiMgr.SwitchToMainEditor();
                nbMgr.SwitchToMainEditor();
                Eunoia::BehaviourManager::Get().SwitchToMainEditor();
                s_activePromotedTabIndex = -1;
                agMgr.SwitchToEditor((int)i);
            }

            // Close button Ã¢Å“â€¢
            ImGui::SameLine(0.0f, 2.0f * uiScale);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.863f, 0.149f, 0.149f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.10f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.631f, 0.631f, 0.667f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * uiScale, 1.0f * uiScale));

            std::string closeBtnId = "Ã¢Å“â€¢##CloseAgTab_" + std::to_string(i);
            if (ImGui::Button(closeBtnId.c_str(), ImVec2(16.0f * uiScale, tabBtnH))) {
                agToClose = (int)i;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close %s", agEditors[i]->GetName().c_str());

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        }

        if (agToClose >= 0) {
            agMgr.Close((size_t)agToClose);
        }

        // 3.7 Animation Blend Tabs (Multiple open Animation Blend 2D editors)
        auto& blendMgr = Eunoia::AnimationBlendManager::Get();
        const auto& blendEditors = blendMgr.GetEditors();
        int blendToClose = -1;
        for (size_t i = 0; i < blendEditors.size(); ++i) {
            ImGui::SameLine(0.0f, 4.0f * uiScale);
            bool isBlendActive = (blendMgr.IsActive() && blendMgr.GetActiveEditorIndex() == (int)i);
            std::string blendLabel = "[Blend] " + blendEditors[i]->GetName();
            if (blendEditors[i]->IsDirty()) blendLabel += "*";
            blendLabel += "##BlendTab_" + std::to_string(i);

            TabSaveProgressState dummyBlendSaveState;
            if (RenderTabWithSaveProgress(blendLabel.c_str(), isBlendActive, dummyBlendSaveState, tabBtnH)) {
                materialWorkspaceActive = false;
                uiMgr.SwitchToMainEditor();
                nbMgr.SwitchToMainEditor();
                agMgr.SwitchToMainEditor();
                Eunoia::BehaviourManager::Get().SwitchToMainEditor();
                s_activePromotedTabIndex = -1;
                blendMgr.SwitchToEditor((int)i);
            }

            // Close button ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¢
            ImGui::SameLine(0.0f, 2.0f * uiScale);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.863f, 0.149f, 0.149f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.10f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.631f, 0.631f, 0.667f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * uiScale, 1.0f * uiScale));

            std::string closeBtnId = "ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¢##CloseBlendTab_" + std::to_string(i);
            if (ImGui::Button(closeBtnId.c_str(), ImVec2(16.0f * uiScale, tabBtnH))) {
                blendToClose = (int)i;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close %s", blendEditors[i]->GetName().c_str());

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        }

        if (blendToClose >= 0) {
            blendMgr.Close((size_t)blendToClose);
        }

        // 3.8 Behaviour Blueprint Tabs (Unreal Engine Blueprint Actor editors)
        auto& behMgr = Eunoia::BehaviourManager::Get();
        const auto& behEditors = behMgr.GetEditors();
        int behToClose = -1;
        for (size_t i = 0; i < behEditors.size(); ++i) {
            ImGui::SameLine(0.0f, 4.0f * uiScale);
            bool isBehActive = (behMgr.IsActive() && behMgr.GetActiveEditorIndex() == (int)i);
            std::string behLabel = "[B] " + behEditors[i]->GetName();
            if (behEditors[i]->IsDirty()) behLabel += "*";
            behLabel += "##BehTab_" + std::to_string(i);

            TabSaveProgressState dummyBehSaveState;
            if (RenderTabWithSaveProgress(behLabel.c_str(), isBehActive, dummyBehSaveState, tabBtnH)) {
                materialWorkspaceActive = false;
                uiMgr.SwitchToMainEditor();
                nbMgr.SwitchToMainEditor();
                agMgr.SwitchToMainEditor();
                blendMgr.SwitchToMainEditor();
                s_activePromotedTabIndex = -1;
                behMgr.SwitchToEditor((int)i);
            }

            // Close button
            ImGui::SameLine(0.0f, 2.0f * uiScale);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.863f, 0.149f, 0.149f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.10f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.631f, 0.631f, 0.667f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * uiScale, 1.0f * uiScale));

            std::string closeBtnId = "x##CloseBehTab_" + std::to_string(i);
            if (ImGui::Button(closeBtnId.c_str(), ImVec2(16.0f * uiScale, tabBtnH))) {
                behToClose = (int)i;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close %s", behEditors[i]->GetName().c_str());

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        }

        if (behToClose >= 0) {
            behMgr.Close((size_t)behToClose);
        }

                // 3.9 Promoted Workspace Tabs (Dockable windows dragged to top tab switcher bar)
        int promotedToClose = -1;
        for (size_t i = 0; i < s_promotedTabs.size(); ++i) {
            ImGui::SameLine(0.0f, 4.0f * uiScale);
            bool isPromotedActive = (s_activePromotedTabIndex == (int)i);
            std::string tabLabel = "[" + s_promotedTabs[i].name + "]##PromotedTab_" + std::to_string(i);
            TabSaveProgressState dummySaveState;
            if (RenderTabWithSaveProgress(tabLabel.c_str(), isPromotedActive, dummySaveState, tabBtnH)) {
                materialWorkspaceActive = false;
                uiMgr.SwitchToMainEditor();
                nbMgr.SwitchToMainEditor();
                agMgr.SwitchToMainEditor();
                blendMgr.SwitchToMainEditor();
                behMgr.SwitchToMainEditor();
                s_activePromotedTabIndex = (int)i;
            }

            // Close button for promoted tab
            ImGui::SameLine(0.0f, 2.0f * uiScale);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.863f, 0.149f, 0.149f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.10f, 0.10f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.631f, 0.631f, 0.667f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * uiScale, 1.0f * uiScale));
            std::string closeBtnId = "x##ClosePromotedTab_" + std::to_string(i);
            if (ImGui::Button(closeBtnId.c_str(), ImVec2(16.0f * uiScale, tabBtnH))) {
                promotedToClose = (int)i;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close %s Tab", s_promotedTabs[i].name.c_str());
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        }
        if (promotedToClose >= 0) {
            ClosePromotedWorkspaceTab(promotedToClose);
        }

        // 4. "+" Quick Add New Document Switch
        ImGui::SameLine(0.0f, 6.0f * uiScale);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.071f, 0.071f, 0.078f, 1.0f)); // #121214
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.122f, 0.122f, 0.137f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.631f, 0.631f, 0.667f, 1.0f));
        if (ImGui::Button("+##AddNewTabDocBtn", ImVec2(22.0f * uiScale, tabBtnH))) {
            ImGui::OpenPopup("QuickAddTabDocPopup");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open or create Material, UI, or Node Behaviour");
        ImGui::PopStyleColor(3);

        if (ImGui::BeginPopup("QuickAddTabDocPopup")) {
            if (ImGui::MenuItem("New Material (.emat)...")) {
                showNewMaterialPopup = true;
                snprintf(newMaterialNameBuf, sizeof(newMaterialNameBuf), "M_NewMaterial");
            }
            if (ImGui::MenuItem("New Animator Graph (.eanimator)...")) {
                showNewAnimatorGraphPopup = true;
                snprintf(newAnimatorGraphNameBuf, sizeof(newAnimatorGraphNameBuf), "AG_NewAnimator");
            }
                        if (ImGui::MenuItem("New Level Animation (.elanim)...")) {
                popupTargetVirtualDir = currentVirtualDir;
                showNewLevelAnimationPopup = true;
                snprintf(newLevelAnimationNameBuf, sizeof(newLevelAnimationNameBuf), "LA_NewAnimation");
            }
            if (ImGui::MenuItem("New Animation Blend (.eblend)...")) {
                showNewAnimationBlendPopup = true;
                snprintf(newAnimationBlendNameBuf, sizeof(newAnimationBlendNameBuf), "BS_NewBlend");
            }
                        if (ImGui::MenuItem("New Behaviour (.behaviour)...")) {
                    showNewBehaviourAssetPopup = true;
                    snprintf(newBehaviourAssetNameBuf, sizeof(newBehaviourAssetNameBuf), "B_NewActor");
                }
                if (ImGui::MenuItem("New Node Behaviour (.enb)...")) {
                showNewNodeBehaviourPopup = true;
                snprintf(newNodeBehaviourNameBuf, sizeof(newNodeBehaviourNameBuf), "NB_NewBehaviour");
            }
            if (ImGui::MenuItem("New User Interface (.eui)...")) {
                showNewUIPopup = true;
                snprintf(newUINameBuf, sizeof(newUINameBuf), "UI_NewScreen");
            }
            ImGui::Separator();
            // List project Node Behaviours to quickly open
            if (ImGui::BeginMenu("Open Existing Node Behaviour...")) {
                std::error_code nbec;
                if (std::filesystem::exists(contentRootPath, nbec)) {
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(contentRootPath, nbec)) {
                        if (entry.path().extension() == ".enb") {
                            std::string stemName = entry.path().stem().string();
                            if (ImGui::MenuItem(stemName.c_str())) {
                                OpenNodeBehaviourEditor(entry.path().string());
                            }
                        }
                    }
                }
                ImGui::EndMenu();
            }
            // List project materials to quickly open
            if (ImGui::BeginMenu("Open Existing Material...")) {
                std::error_code mec;
                if (std::filesystem::exists(contentRootPath, mec)) {
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(contentRootPath, mec)) {
                        if (entry.path().extension() == ".emat") {
                            std::string stemName = entry.path().stem().string();
                            if (ImGui::MenuItem(stemName.c_str())) {
                                OpenMaterialEditor(entry.path().string());
                            }
                        }
                    }
                }
                ImGui::EndMenu();
            }
            // List project UIs to quickly open
            if (ImGui::BeginMenu("Open Existing User Interface...")) {
                std::error_code uec;
                if (std::filesystem::exists(contentRootPath, uec)) {
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(contentRootPath, uec)) {
                        if (entry.path().extension() == ".eui") {
                            std::string stemName = entry.path().stem().string();
                            if (ImGui::MenuItem(stemName.c_str())) {
                                OpenUIEditor(entry.path().string());
                            }
                        }
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        // 5. Window Frame Controls ONLY at the end of the tab switch view (Minimize, Maximize, Close)
        float winBtnW = 28.0f * uiScale;
        float winBtnH = tabBtnH;
        float totalWinControlsW = winBtnW * 3.0f + 16.0f * uiScale;
        const ImVec4 textMuted = ImVec4(0.631f, 0.631f, 0.667f, 1.0f);

        ImGui::SameLine(barW - totalWinControlsW);

        // Minimize [-]
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.122f, 0.122f, 0.137f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, textMuted);
        if (ImGui::Button("-##WinMin", ImVec2(winBtnW, winBtnH))) {
            if (m_window) glfwIconifyWindow(m_window);
        }
        ImGui::PopStyleColor(4);

        // Maximize [Square]
        ImGui::SameLine(0.0f, 2.0f * uiScale);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.122f, 0.122f, 0.137f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, textMuted);
        if (ImGui::Button(isMaximized ? "2##WinMax" : "1##WinMax", ImVec2(winBtnW, winBtnH))) {
#ifdef _WIN32
            if (hwnd) {
                if (isMaximized) ShowWindow(hwnd, SW_RESTORE);
                else ShowWindow(hwnd, SW_MAXIMIZE);
            }
#endif
        }
        ImGui::PopStyleColor(4);

        // Close [X]
        ImGui::SameLine(0.0f, 2.0f * uiScale);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.863f, 0.149f, 0.149f, 1.0f)); // Red hover
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.10f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, textMuted);
        if (ImGui::Button("X##WinClose", ImVec2(winBtnW, winBtnH))) {
            outShouldExit = true;
        }
        ImGui::PopStyleColor(4);

        // Window drag & double-click maximize on empty tab bar space
#ifdef _WIN32
        bool isHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        bool anyActive = ImGui::IsAnyItemActive() || ImGui::IsAnyItemHovered();
        if (isHovered && !anyActive) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (hwnd) {
                    if (isMaximized) ShowWindow(hwnd, SW_RESTORE);
                    else ShowWindow(hwnd, SW_MAXIMIZE);
                }
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (hwnd) {
                    ReleaseCapture();
                    SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                    ImGui::GetIO().MouseDown[0] = false;
                }
            }
        }
#endif

        // Window Drag & Drop target to promote any dockable window into a fullscreen workspace tab
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_WINDOW)) {
                ImGuiWindow* draggedWin = *(ImGuiWindow**)payload->Data;
                if (draggedWin && draggedWin->Name) {
                    std::string winName = draggedWin->Name;
                    size_t hashPos = winName.find("###");
                    if (hashPos != std::string::npos) winName = winName.substr(0, hashPos);
                    size_t doubleHashPos = winName.find("##");
                    if (doubleHashPos != std::string::npos) winName = winName.substr(0, doubleHashPos);
                    if (!winName.empty() && winName[0] != '#') {
                        PromoteWindowToWorkspaceTab(winName);
                        materialWorkspaceActive = false;
                        uiMgr.SwitchToMainEditor();
                        nbMgr.SwitchToMainEditor();
                        agMgr.SwitchToMainEditor();
                        blendMgr.SwitchToMainEditor();
                        behMgr.SwitchToMainEditor();
                        AddLog("LogUI", "Promoted window to workspace tab: " + winName, 0);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Bottom border line #1f1f23
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(vp->Pos.x, barY + barH),
                    ImVec2(vp->Pos.x + barW, barY + barH),
                    IM_COL32(31, 31, 35, 255), 1.0f);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void EngineUI::RenderViewportOverlay(Scene& scene, OrbitCamera& camera, float fps, float frameTimeMs) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    auto vpRect = GetViewportRect(vp->Size.x, vp->Size.y);
    float overlayX = vp->Pos.x + vpRect.x + 12.0f;
    float overlayY = vp->Pos.y + vpRect.y + 12.0f;

    // Hotkey controls (only when not typing in text fields)
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) currentGizmoOperation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) currentGizmoOperation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) currentGizmoOperation = ImGuizmo::SCALE;
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
            currentGizmoMode = (currentGizmoMode == ImGuizmo::WORLD) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
        }
    }

    // Viewport controls (FPS, Lit/Wire/Unlit, Gizmos, Snapping) are now displayed in the Main MenuBar.
    // Hotkeys (W, E, R, Q) are handled above.

    // Render Active Runtime UIs added via UISystem.Add(UI)
    EunoiaUI::UISystemManager::Get().SetCurrentScene(&scene);
    EunoiaUI::UISystemManager::Get().RenderActiveUIs(glm::vec2(vp->Pos.x + vpRect.x, vp->Pos.y + vpRect.y), glm::vec2(vpRect.width, vpRect.height));

    // Render 3D World-Space and Screen-Space User Interfaces
    ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();
    if (fgDrawList) {
        EunoiaUI::UISystem3DManager::Get().RenderAllScene3DUI(
            fgDrawList, scene, camera,
            glm::vec4(vp->Pos.x + vpRect.x, vp->Pos.y + vpRect.y, vpRect.width, vpRect.height),
            isGameView || scene.isPlayMode
        );
    }

    // Viewport Overlay Header / Stats: FPS badge matching dev.md
    if (fgDrawList) {
        char fpsText[32];
        snprintf(fpsText, sizeof(fpsText), "FPS %d", (int)std::round(fps));
        ImVec2 textSz = ImGui::CalcTextSize(fpsText);
        float padX = 8.0f * uiScale;
        float padY = 3.0f * uiScale;
        float badgeW = textSz.x + padX * 2.0f;
        float badgeH = textSz.y + padY * 2.0f;
        float badgeX = vp->Pos.x + vpRect.x + vpRect.width - badgeW - 12.0f * uiScale;
        float badgeY = vp->Pos.y + vpRect.y + 8.0f * uiScale;

        fgDrawList->AddRectFilled(ImVec2(badgeX, badgeY), ImVec2(badgeX + badgeW, badgeY + badgeH), IM_COL32(0, 0, 0, 160), 4.0f);
        fgDrawList->AddRect(ImVec2(badgeX, badgeY), ImVec2(badgeX + badgeW, badgeY + badgeH), IM_COL32(31, 31, 35, 255), 4.0f, 0, 1.0f);
        fgDrawList->AddText(ImVec2(badgeX + padX, badgeY + padY), IM_COL32(228, 228, 231, 255), fpsText);
    }

    // Viewport top-left: Render on-screen Print messages
    RenderScreenPrintOverlay(overlayX, overlayY + 36.0f);

    // 3D Viewport Point Light Sprite Billboards (dev.md)
    if (!isGameView) {
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        float aspect = vpRect.width / vpRect.height;
        glm::mat4 proj = camera.GetProjectionMatrix(aspect);
        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 vpMatrix = proj * view;

        bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        ImVec2 mousePos = ImGui::GetMousePos();

        for (auto& obj : scene.objects) {
            if (!obj.visible || (!obj.isLight && !IsLightPrimitive(obj.type))) continue;
            glm::vec3 worldPos = scene.GetWorldPosition(obj);
            glm::vec4 clip = vpMatrix * glm::vec4(worldPos, 1.0f);
            if (clip.w <= 0.05f) continue;

            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.z < -1.0f || ndc.z > 1.0f) continue;

            float screenX = vp->Pos.x + vpRect.x + (ndc.x * 0.5f + 0.5f) * vpRect.width;
            float screenY = vp->Pos.y + vpRect.y + ((1.0f - ndc.y) * 0.5f) * vpRect.height;

            if (screenX < vp->Pos.x + vpRect.x || screenX > vp->Pos.x + vpRect.x + vpRect.width ||
                screenY < vp->Pos.y + vpRect.y || screenY > vp->Pos.y + vpRect.y + vpRect.height) continue;

            float iconSize = 38.0f;
            ImVec2 pMin(screenX - iconSize * 0.5f, screenY - iconSize * 0.5f);
            ImVec2 pMax(screenX + iconSize * 0.5f, screenY + iconSize * 0.5f);

            bool isSelected = (scene.selectedId == obj.id);

            LightType lType = obj.light.type;
            if (obj.type == PrimitiveType::DirectionalLight) lType = LightType::Directional;
            else if (obj.type == PrimitiveType::PointLight) lType = LightType::Point;
            else if (obj.type == PrimitiveType::SpotLight) lType = LightType::Spot;
            else if (obj.type == PrimitiveType::AreaLight) lType = LightType::Area;
            else if (obj.type == PrimitiveType::SkyLight) lType = LightType::Sky;

            uint64_t iconHandle = GetLightIconGpuHandle(lType);

            // Draw Sprite Billboard
            if (iconHandle != 0) {
                drawList->AddImage((ImTextureID)iconHandle, pMin, pMax,
                                   ImVec2(0, 0), ImVec2(1, 1),
                                   isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(230, 230, 230, 210));
            } else {
                drawList->AddCircleFilled(ImVec2(screenX, screenY), 12.0f, IM_COL32(255, 210, 70, 220));
            }

            // Selection ring
            if (isSelected) {
                drawList->AddCircle(ImVec2(screenX, screenY), 20.0f, IM_COL32(0, 255, 100, 255), 24, 2.0f);
                drawList->AddCircle(ImVec2(screenX, screenY), 22.0f, IM_COL32(255, 230, 80, 180), 24, 1.0f);
            } else {
                drawList->AddCircle(ImVec2(screenX, screenY), 16.0f, IM_COL32(255, 220, 60, 90), 16, 1.0f);
            }

            // Click detection on sprite
            if (mouseClicked && !ImGui::GetIO().WantCaptureMouse && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
                float dx = mousePos.x - screenX;
                float dy = mousePos.y - screenY;
                if (dx * dx + dy * dy <= 20.0f * 20.0f) {
                    scene.selectedId = obj.id;
                    AddLog("LogActor", "Selected Light Actor via Viewport Sprite: " + obj.name, 0);
                }
            }
        }

        // 3D Viewport Camera Sprite Billboards
        for (auto& obj : scene.objects) {
            if (!obj.visible || (!obj.isCamera && obj.type != PrimitiveType::Camera)) continue;
            glm::vec3 worldPos = scene.GetWorldPosition(obj);
            glm::vec4 clip = vpMatrix * glm::vec4(worldPos, 1.0f);
            if (clip.w <= 0.05f) continue;

            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.z < -1.0f || ndc.z > 1.0f) continue;

            float screenX = vp->Pos.x + vpRect.x + (ndc.x * 0.5f + 0.5f) * vpRect.width;
            float screenY = vp->Pos.y + vpRect.y + ((1.0f - ndc.y) * 0.5f) * vpRect.height;

            if (screenX < vp->Pos.x + vpRect.x || screenX > vp->Pos.x + vpRect.x + vpRect.width ||
                screenY < vp->Pos.y + vpRect.y || screenY > vp->Pos.y + vpRect.y + vpRect.height) continue;

            float iconSize = 38.0f;
            ImVec2 pMin(screenX - iconSize * 0.5f, screenY - iconSize * 0.5f);
            ImVec2 pMax(screenX + iconSize * 0.5f, screenY + iconSize * 0.5f);

            bool isSelected = (scene.selectedId == obj.id);

            // Draw Sprite Billboard (@resources/icons/camera.png)
            if (cameraIconGpuHandle != 0) {
                drawList->AddImage((ImTextureID)cameraIconGpuHandle, pMin, pMax,
                                   ImVec2(0, 0), ImVec2(1, 1),
                                   isSelected ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 235, 255, 210));
            } else {
                drawList->AddCircleFilled(ImVec2(screenX, screenY), 12.0f, IM_COL32(60, 180, 240, 220));
            }

            // Selection ring
            if (isSelected) {
                drawList->AddCircle(ImVec2(screenX, screenY), 20.0f, IM_COL32(0, 255, 100, 255), 24, 2.0f);
                drawList->AddCircle(ImVec2(screenX, screenY), 22.0f, IM_COL32(80, 210, 255, 180), 24, 1.0f);
            } else {
                drawList->AddCircle(ImVec2(screenX, screenY), 16.0f, IM_COL32(80, 180, 230, 90), 16, 1.0f);
            }

            // Active level camera badge
            if (scene.activeLevelCameraId == obj.id) {
                drawList->AddCircleFilled(ImVec2(screenX + 12.0f, screenY - 12.0f), 5.0f, IM_COL32(0, 255, 100, 255));
            }

            // Click detection on camera sprite
            if (mouseClicked && !ImGui::GetIO().WantCaptureMouse && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
                float dx = mousePos.x - screenX;
                float dy = mousePos.y - screenY;
                if (dx * dx + dy * dy <= 20.0f * 20.0f) {
                    scene.selectedId = obj.id;
                    AddLog("LogActor", "Selected Camera Actor via Viewport Sprite: " + obj.name, 0);
                }
            }
        }

        // Camera Picture-in-Picture (PiP) Preview in bottom right of viewport
        RenderCameraPreviewOverlay(scene, camera);

        // Terrain Sculpting & Material Painting 3D Brush Ring & Interactive Raycast
        GameObject* selObj = scene.GetSelected();
        if (selObj && (selObj->isTerrain || selObj->type == PrimitiveType::Terrain) &&
            (selObj->terrain.editModeActive || selObj->terrain.materialPaintModeActive)) {
            float mouseVpX = mousePos.x - (vp->Pos.x + vpRect.x);
            float mouseVpY = mousePos.y - (vp->Pos.y + vpRect.y);

            // Verify mouse is within viewport bounds
            if (mouseVpX >= 0.0f && mouseVpX <= vpRect.width && mouseVpY >= 0.0f && mouseVpY <= vpRect.height) {
                float ndcX = (2.0f * mouseVpX) / vpRect.width - 1.0f;
                float ndcY = 1.0f - (2.0f * mouseVpY) / vpRect.height;

                glm::mat4 invVP = glm::inverse(vpMatrix);
                glm::vec4 pNear = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
                glm::vec4 pFar  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
                glm::vec3 rayWorldOrigin = glm::vec3(pNear) / pNear.w;
                glm::vec3 rayWorldFar    = glm::vec3(pFar) / pFar.w;
                glm::vec3 rayWorldDir    = glm::normalize(rayWorldFar - rayWorldOrigin);

                glm::mat4 model = scene.GetWorldMatrix(*selObj);
                glm::mat4 invModel = glm::inverse(model);

                glm::vec3 rayLocalOrigin = glm::vec3(invModel * glm::vec4(rayWorldOrigin, 1.0f));
                glm::vec3 rayLocalDir = glm::normalize(glm::vec3(invModel * glm::vec4(rayWorldDir, 0.0f)));

                glm::vec3 hitLocalPos;
                if (selObj->terrain.Raycast(rayLocalOrigin, rayLocalDir, hitLocalPos)) {
                    glm::vec3 hitWorldPos = glm::vec3(model * glm::vec4(hitLocalPos, 1.0f));

                    bool isPaintMode = selObj->terrain.materialPaintModeActive;
                    float r = isPaintMode ? selObj->terrain.matBrushSize : selObj->terrain.brushSize;
                    float rInner = r * 0.45f;

                    // Draw 3D projected brush circle ring on the terrain
                    ImDrawList* fgDraw = ImGui::GetForegroundDrawList();
                    const int circleSegments = 36;
                    ImVec2 circlePts[circleSegments];
                    ImVec2 innerPts[circleSegments];

                    bool allProjected = true;
                    for (int s = 0; s < circleSegments; ++s) {
                        float angle = (float)s / (float)circleSegments * 6.2831853f;
                        glm::vec3 pLocalOuter = hitLocalPos + glm::vec3(cosf(angle) * r, 0.0f, sinf(angle) * r);
                        glm::vec3 pWorldOuter = glm::vec3(model * glm::vec4(pLocalOuter, 1.0f));

                        glm::vec4 cClip = vpMatrix * glm::vec4(pWorldOuter, 1.0f);
                        if (cClip.w <= 0.05f) { allProjected = false; break; }
                        glm::vec3 cNdc = glm::vec3(cClip) / cClip.w;
                        circlePts[s] = ImVec2(
                            vp->Pos.x + vpRect.x + (cNdc.x * 0.5f + 0.5f) * vpRect.width,
                            vp->Pos.y + vpRect.y + ((1.0f - cNdc.y) * 0.5f) * vpRect.height
                        );

                        glm::vec3 pLocalInner = hitLocalPos + glm::vec3(cosf(angle) * rInner, 0.0f, sinf(angle) * rInner);
                        glm::vec3 pWorldInner = glm::vec3(model * glm::vec4(pLocalInner, 1.0f));
                        glm::vec4 cClipInner = vpMatrix * glm::vec4(pWorldInner, 1.0f);
                        glm::vec3 cNdcInner = glm::vec3(cClipInner) / cClipInner.w;
                        innerPts[s] = ImVec2(
                            vp->Pos.x + vpRect.x + (cNdcInner.x * 0.5f + 0.5f) * vpRect.width,
                            vp->Pos.y + vpRect.y + ((1.0f - cNdcInner.y) * 0.5f) * vpRect.height
                        );
                    }

                    if (allProjected && fgDraw) {
                        ImU32 outerCol = isPaintMode ? IM_COL32(200, 80, 240, 230) : IM_COL32(50, 220, 100, 230);
                        ImU32 innerCol = isPaintMode ? IM_COL32(220, 140, 255, 140) : IM_COL32(80, 240, 140, 120);

                        // Outer primary circle
                        fgDraw->AddPolyline(circlePts, circleSegments, outerCol, ImDrawFlags_Closed, 2.5f);
                        // Inner falloff guide circle
                        fgDraw->AddPolyline(innerPts, circleSegments, innerCol, ImDrawFlags_Closed, 1.2f);
                        // Center dot
                        glm::vec4 hClip = vpMatrix * glm::vec4(hitWorldPos, 1.0f);
                        if (hClip.w > 0.05f) {
                            glm::vec3 hNdc = glm::vec3(hClip) / hClip.w;
                            ImVec2 centerPt(
                                vp->Pos.x + vpRect.x + (hNdc.x * 0.5f + 0.5f) * vpRect.width,
                                vp->Pos.y + vpRect.y + ((1.0f - hNdc.y) * 0.5f) * vpRect.height
                            );
                            fgDraw->AddCircleFilled(centerPt, 3.5f, IM_COL32(255, 255, 255, 240));
                        }
                    }

                    // Stroke tracking for Undo/Redo
                    static bool s_isInteractingStroke = false;
                    static Scene s_preInteractSceneSnapshot;

                    // Interaction on LMB drag (when not interacting with UI widgets, gizmos, or fly camera)
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::GetIO().WantCaptureMouse &&
                        !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                        
                        if (!s_isInteractingStroke) {
                            s_isInteractingStroke = true;
                            s_preInteractSceneSnapshot = scene;
                        }

                        float dt = ImGui::GetIO().DeltaTime;
                        if (dt > 0.05f) dt = 0.05f;

                        bool invert = ImGui::GetIO().KeyShift;
                        if (isPaintMode) {
                            selObj->terrain.ApplyMaterialPaint(hitLocalPos, (uint8_t)selObj->terrain.activePaintMaterialIndex, dt, invert);
                        } else {
                            selObj->terrain.ApplyBrush(hitLocalPos, dt, invert);
                        }
                        selObj->terrain.RebuildTerrainMesh(selObj->mesh);
                        RequestViewportRedraw(5);
                    } else if (s_isInteractingStroke) {
                        s_isInteractingStroke = false;
                        if (HasSceneStateChanged(s_preInteractSceneSnapshot, scene)) {
                            std::string actionName = isPaintMode ? "Terrain Material Paint: " : "Terrain Sculpt: ";
                            UndoManager::Get().RecordSnapshot(s_preInteractSceneSnapshot, actionName + selObj->name);
                            AddLog("LogTerrain", "Recorded Undo snapshot for " + actionName + selObj->name, 0);
                        }
                    }
                }
            }
        }
    }

    // Rounded border framing the center 3D viewport (rx = 11.0f) + FPS display per SVG blueprint
    ImDrawList* borderDrawList = ImGui::GetForegroundDrawList();
    ImVec2 vpMin(vp->Pos.x + vpRect.x, vp->Pos.y + vpRect.y);
    ImVec2 vpMax(vp->Pos.x + vpRect.x + vpRect.width, vp->Pos.y + vpRect.y + vpRect.height);
    borderDrawList->AddRect(vpMin, vpMax, IM_COL32(50, 52, 58, 220), 11.0f * uiScale, 0, 1.5f);

    // Viewport Center Screen: Camera Speed overlay (fades out after ~1s)
    if (cameraSpeedOverlayTimer > 0.0f) {
        cameraSpeedOverlayTimer -= ImGui::GetIO().DeltaTime;
        if (cameraSpeedOverlayTimer < 0.0f) cameraSpeedOverlayTimer = 0.0f;

        float alpha = 1.0f;
        if (cameraSpeedOverlayTimer < 0.35f) {
            alpha = cameraSpeedOverlayTimer / 0.35f;
        }

        float centerX = vpMin.x + vpRect.width * 0.5f;
        float centerY = vpMin.y + vpRect.height * 0.5f;

        char speedBuf[64];
        snprintf(speedBuf, sizeof(speedBuf), "Camera Speed: %.1f", cameraSpeed);

        ImVec2 textSize = ImGui::CalcTextSize(speedBuf);
        float padX = 22.0f * uiScale;
        float padY = 12.0f * uiScale;
        ImVec2 boxMin(centerX - (textSize.x * 0.5f) - padX, centerY - (textSize.y * 0.5f) - padY);
        ImVec2 boxMax(centerX + (textSize.x * 0.5f) + padX, centerY + (textSize.y * 0.5f) + padY);

        uint8_t aBg = (uint8_t)(230.0f * alpha);
        uint8_t aBorder = (uint8_t)(190.0f * alpha);
        uint8_t aText = (uint8_t)(255.0f * alpha);

        borderDrawList->AddRectFilled(boxMin, boxMax, IM_COL32(22, 22, 26, aBg), 8.0f * uiScale);
        borderDrawList->AddRect(boxMin, boxMax, IM_COL32(31, 153, 242, aBorder), 8.0f * uiScale, 0, 1.5f);
        borderDrawList->AddText(ImVec2(centerX - textSize.x * 0.5f, centerY - textSize.y * 0.5f),
                                IM_COL32(255, 255, 255, aText), speedBuf);
    }
}

void EngineUI::RenderScreenPrintOverlay(float startX, float startY) {
    auto& screenPrint = ScreenPrint::System::Get();
    const auto messages = screenPrint.GetMessages();
    if (messages.empty()) return;

    ImGui::SetNextWindowPos(ImVec2(startX, startY), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoInputs;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 4.0f));

    if (ImGui::Begin("##ScreenPrintOverlay", nullptr, flags)) {
        for (size_t i = 0; i < messages.size(); ++i) {
            const auto& msg = messages[i];
            float alpha = 1.0f;
            if (msg.remainingTime < 0.5f) {
                alpha = std::clamp(msg.remainingTime / 0.5f, 0.0f, 1.0f);
            }

            ImVec4 textColor = ImVec4(msg.color.x, msg.color.y, msg.color.z, alpha);
            ImVec4 bgColor   = ImVec4(0.05f, 0.07f, 0.09f, 0.85f * alpha);

            ImGui::PushStyleColor(ImGuiCol_Text, textColor);
            ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bgColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, bgColor);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 5.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

            std::string btnId = "##screen_msg_" + std::to_string(i);
            ImGui::Button((msg.text + btnId).c_str());

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
}

void EngineUI::RenderCameraPreviewOverlay(Scene& scene, OrbitCamera& camera) {
    if (isGameView || scene.isPlayMode || isGameOnlyWindow || scene.selectedId == -1) return;

    GameObject* selObj = scene.FindObject(scene.selectedId);
    if (!selObj || (!selObj->isCamera && selObj->type != PrimitiveType::Camera)) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    auto pipRect = GetCameraPipRect(vp->Size.x, vp->Size.y, scene);
    if (!pipRect.active) return;

    float canvasX = vp->Pos.x + pipRect.x;
    float canvasY = vp->Pos.y + pipRect.y;
    float canvasW = pipRect.width;
    float canvasH = pipRect.height;

    float winX = canvasX - 4.0f;
    float winY = canvasY - 28.0f;
    float winW = canvasW + 8.0f;
    float winH = canvasH + 32.0f;

    ImGui::SetNextWindowPos(ImVec2(winX, winY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    // Transparent window background so the D3D12 hardware rendering of the camera view shines through
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    if (ImGui::Begin("##CameraPiPWindow", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // 1. Header background (dark blue/slate with rounded top corners)
        dl->AddRectFilled(ImVec2(winX, winY), ImVec2(winX + winW, winY + 28.0f), IM_COL32(16, 20, 28, 240), 0.0f);

        // 2. Full window perimeter border
        dl->AddRect(ImVec2(winX, winY), ImVec2(winX + winW, winY + winH), IM_COL32(40, 130, 230, 220), 0.0f, 0, 1.5f);

        // 3. Viewport canvas inner border (accent framing D3D12 hardware preview)
        dl->AddRect(ImVec2(canvasX, canvasY), ImVec2(canvasX + canvasW, canvasY + canvasH), IM_COL32(65, 75, 95, 255), 0.0f, 0, 1.0f);

        // Header controls
        ImGui::SetCursorPos(ImVec2(8.0f, 4.0f));
        ImGui::TextColored(ImVec4(0.30f, 0.85f, 1.0f, 1.0f), "%s", selObj->name.c_str());
        ImGui::SameLine();
        bool isLevelCam = (scene.activeLevelCameraId == selObj->id);
        if (isLevelCam) {
            ImGui::TextColored(ImVec4(0.2f, 0.95f, 0.4f, 1.0f), "[Active]");
        } else {
            if (ImGui::SmallButton("Set Level Cam")) {
                scene.activeLevelCameraId = selObj->id;
                AddLog("LogCamera", "Set Active Level Camera: " + selObj->name, 0);
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Align")) {
            camera.yaw = selObj->rotation.y;
            camera.pitch = -selObj->rotation.x;
            camera.fov = selObj->camera.fov;
            camera.distance = 1.0f;
            camera.target = scene.GetWorldPosition(*selObj) + camera.GetForward() * 1.0f;
            AddLog("LogCamera", "Aligned Viewport to " + selObj->name, 0);
        }

        // Crosshair on camera view
        float midX = canvasX + canvasW * 0.5f;
        float midY = canvasY + canvasH * 0.5f;
        dl->AddLine(ImVec2(midX - 7.0f, midY), ImVec2(midX + 7.0f, midY), IM_COL32(255, 255, 255, 100));
        dl->AddLine(ImVec2(midX, midY - 7.0f), ImVec2(midX, midY + 7.0f), IM_COL32(255, 255, 255, 100));

        // Action Safe Area (90% rectangle)
        dl->AddRect(ImVec2(canvasX + canvasW * 0.05f, canvasY + canvasH * 0.05f),
                    ImVec2(canvasX + canvasW * 0.95f, canvasY + canvasH * 0.95f),
                    IM_COL32(255, 255, 255, 45));

        // Camera info overlay badge (bottom-left)
        char camInfo[64];
        if (!selObj->camera.isOrthographic) {
            snprintf(camInfo, sizeof(camInfo), "FOV: %.1fÃƒâ€šÃ‚Â° | Persp", selObj->camera.fov);
        } else {
            snprintf(camInfo, sizeof(camInfo), "Size: %.1f | Ortho", selObj->camera.orthoSize);
        }
        ImVec2 badgePos(canvasX + 6.0f, canvasY + canvasH - 22.0f);
        dl->AddRectFilled(badgePos, ImVec2(badgePos.x + 130.0f, badgePos.y + 18.0f), IM_COL32(12, 14, 18, 200), 4.0f);
        dl->AddText(ImVec2(badgePos.x + 6.0f, badgePos.y + 2.0f), IM_COL32(200, 225, 255, 230), camInfo);

        // Game Mode appearance indicator (bottom-right)
        const char* previewTag = "GAME PREVIEW";
        ImVec2 tagPos(canvasX + canvasW - 100.0f, canvasY + canvasH - 22.0f);
        dl->AddRectFilled(tagPos, ImVec2(tagPos.x + 94.0f, tagPos.y + 18.0f), IM_COL32(20, 60, 40, 210), 4.0f);
        dl->AddText(ImVec2(tagPos.x + 6.0f, tagPos.y + 2.0f), IM_COL32(80, 240, 140, 230), previewTag);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void EngineUI::DrawOutlinerNode(GameObject& obj, Scene& scene, std::unordered_set<int>& visitedIds, int depth) {
    if (depth > 64 || visitedIds.count(obj.id)) return;
    visitedIds.insert(obj.id);

    ImGui::PushID(obj.id);

    // Hover detection for the row
    ImVec2 rowPos = ImGui::GetCursorScreenPos();
    float rowHeight = ImGui::GetFrameHeight();
    ImVec2 rowMin(ImGui::GetWindowPos().x, rowPos.y);
    ImVec2 rowMax(ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), rowPos.y + rowHeight);
    bool isRowHovered = ImGui::IsMouseHoveringRect(rowMin, rowMax);

    // Type badge matching Old Eunoia Engine
    const char* typeBadge = "[T]";
    if (obj.isLight || IsLightPrimitive(obj.type)) typeBadge = "[L]";
    else if (obj.isCamera || obj.type == PrimitiveType::Camera) typeBadge = "[C]";
    else if (obj.type != PrimitiveType::Empty) typeBadge = "[M]";

    bool hasChildren = !obj.childIds.empty();
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
    if (!hasChildren) {
        nodeFlags |= ImGuiTreeNodeFlags_Leaf;
    } else {
        nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
    }
    bool isSelected = (selectedActorIds.count(obj.id) > 0 || scene.selectedId == obj.id);
    if (isSelected) {
        nodeFlags |= ImGuiTreeNodeFlags_Selected;
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.14f, 0.45f, 0.85f, 0.75f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.52f, 0.95f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.22f, 0.58f, 1.00f, 0.95f));
    }

    // Restore collapsed/expanded state if level loaded or requested
    std::string outlinerNodeKey = obj.name;
    std::string outlinerIdKey = "id:" + std::to_string(obj.id);
    if (m_outlinerStateNeedsApply && hasChildren &&
        m_outlinerAppliedNodes.find(outlinerIdKey) == m_outlinerAppliedNodes.end() &&
        m_outlinerAppliedNodes.find(outlinerNodeKey) == m_outlinerAppliedNodes.end()) {
        auto it = scene.outlinerNodeOpenState.find(outlinerIdKey);
        if (it == scene.outlinerNodeOpenState.end()) {
            it = scene.outlinerNodeOpenState.find(outlinerNodeKey);
        }
        if (it == scene.outlinerNodeOpenState.end()) {
            it = scene.outlinerNodeOpenState.find(std::to_string(obj.id));
        }
        if (it != scene.outlinerNodeOpenState.end()) {
            ImGui::SetNextItemOpen(it->second);
        }
        m_outlinerAppliedNodes.insert(outlinerIdKey);
        m_outlinerAppliedNodes.insert(outlinerNodeKey);
    }

    // TreeNodeEx is the FIRST item on the line: ensures perfect vertical alignment for all siblings
    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)obj.id, nodeFlags, "%s  %s", typeBadge, obj.name.c_str());
    if (isSelected) {
        ImGui::PopStyleColor(3);
    }

    if (hasChildren) {
        scene.outlinerNodeOpenState[outlinerNodeKey] = nodeOpen;
        scene.outlinerNodeOpenState[outlinerIdKey] = nodeOpen;
    }

    // Capture TreeNode click and toggle immediately before any other item on this line is rendered
    bool isNodeClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    bool isNodeToggledOpen = ImGui::IsItemToggledOpen();

    if (isNodeClicked && !isNodeToggledOpen) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl) {
            if (selectedActorIds.count(obj.id)) {
                selectedActorIds.erase(obj.id);
                if (scene.selectedId == obj.id) {
                    scene.selectedId = selectedActorIds.empty() ? -1 : *selectedActorIds.begin();
                }
            } else {
                selectedActorIds.insert(obj.id);
                scene.selectedId = obj.id;
            }
            lastSelectedActorId = obj.id;
        } else if (io.KeyShift && lastSelectedActorId != -1) {
            int idx1 = -1, idx2 = -1;
            for (size_t i = 0; i < scene.objects.size(); ++i) {
                if (scene.objects[i].id == lastSelectedActorId) idx1 = (int)i;
                if (scene.objects[i].id == obj.id) idx2 = (int)i;
            }
            if (idx1 != -1 && idx2 != -1) {
                int start = std::min(idx1, idx2);
                int end = std::max(idx1, idx2);
                selectedActorIds.clear();
                for (int i = start; i <= end; ++i) {
                    selectedActorIds.insert(scene.objects[i].id);
                }
                scene.selectedId = obj.id;
            } else {
                selectedActorIds.clear();
                selectedActorIds.insert(obj.id);
                scene.selectedId = obj.id;
                lastSelectedActorId = obj.id;
            }
        } else {
            selectedActorIds.clear();
            selectedActorIds.insert(obj.id);
            scene.selectedId = obj.id;
            lastSelectedActorId = obj.id;
        }
    }

    // Drag source on TreeNode item
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        int actorId = obj.id;
        ImGui::SetDragDropPayload("OUTLINER_ACTOR", &actorId, sizeof(int));
        ImGui::Text("Reparent %s", obj.name.c_str());
        ImGui::EndDragDropSource();
    }

    // Drag target on TreeNode item
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OUTLINER_ACTOR")) {
            if (payload->Data && payload->DataSize == sizeof(int)) {
                int draggedId = *(const int*)payload->Data;
                if (draggedId != obj.id && !scene.IsDescendantOf(obj.id, draggedId)) {
                    m_pendingReparentChild = draggedId;
                    m_pendingReparentParent = obj.id;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Right-aligned eye icon for clean outliner visibility toggle
    float outlinerRightX = ImGui::GetWindowContentRegionMax().x - 22.0f;
    if (outlinerRightX > ImGui::GetItemRectMin().x + 50.0f) {
        ImGui::SameLine(outlinerRightX);
        if (outlinerEyeIconGpuHandle) {
            ImVec4 eyeTint;
            if (obj.visible) {
                eyeTint = isRowHovered ? ImVec4(1.0f, 1.0f, 1.0f, 0.95f) : ImVec4(1.0f, 1.0f, 1.0f, 0.30f);
            } else {
                eyeTint = isRowHovered ? ImVec4(1.0f, 0.4f, 0.4f, 0.95f) : ImVec4(0.85f, 0.3f, 0.3f, 0.45f);
            }
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.25f, 0.35f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.15f, 0.25f, 0.7f));
            if (ImGui::ImageButton("##EyeBtn", (ImTextureID)outlinerEyeIconGpuHandle, ImVec2(14.0f, 14.0f), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), eyeTint)) {
                obj.visible = !obj.visible;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(obj.visible ? "Hide Actor in Viewport" : "Show Actor in Viewport");
            }
            ImGui::PopStyleColor(3);
        } else {
            if (ImGui::SmallButton(obj.visible ? "[O]" : "[ ]")) {
                obj.visible = !obj.visible;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Toggle Visibility");
            }
        }
    }

    if (ImGui::BeginPopupContextItem()) {
        if (!selectedActorIds.count(obj.id)) {
            selectedActorIds.clear();
            selectedActorIds.insert(obj.id);
            scene.selectedId = obj.id;
            lastSelectedActorId = obj.id;
        }
        if (obj.isCamera || obj.type == PrimitiveType::Camera) {
            bool isCurrent = (scene.activeLevelCameraId == obj.id);
            if (ImGui::MenuItem(isCurrent ? "Clear as Active Level Camera" : "Set as Active Level Camera")) {
                if (isCurrent) scene.activeLevelCameraId = -1;
                else scene.activeLevelCameraId = obj.id;
            }
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
            if (selectedActorIds.count(obj.id) && selectedActorIds.size() > 1) {
                std::vector<int> toDup(selectedActorIds.begin(), selectedActorIds.end());
                selectedActorIds.clear();
                for (int dupId : toDup) {
                    GameObject* copy = scene.DuplicateObject(dupId);
                    if (copy) {
                        selectedActorIds.insert(copy->id);
                        scene.selectedId = copy->id;
                    }
                }
                AddLog("LogActor", "Duplicated " + std::to_string(toDup.size()) + " Actors", 2);
            } else {
                GameObject* copy = scene.DuplicateObject(obj.id);
                if (copy) {
                    selectedActorIds.clear();
                    selectedActorIds.insert(copy->id);
                    scene.selectedId = copy->id;
                    std::string logMsg = "Duplicated Actor: " + copy->name;
                    if (!copy->childIds.empty()) {
                        logMsg += " (along with " + std::to_string(copy->childIds.size()) + " children)";
                    }
                    AddLog("LogActor", logMsg, 2);
                    EngineLogger::Get().LogAction("DUPLICATE_ACTOR", copy->name, "Children: " + std::to_string(copy->childIds.size()));
                }
            }
        }
        ImGui::Separator();
        if (!obj.isLight && !obj.isCamera && obj.type != PrimitiveType::Camera && !obj.isUserInterface && obj.type != PrimitiveType::Empty && obj.type != PrimitiveType::LevelAnimation) {
            bool hasMesh = !obj.mesh.vertices.empty() || !obj.childIds.empty();
            if (hasMesh) {
                ImGui::Separator();
                if (ImGui::MenuItem("Shade Smooth")) {
                    UndoManager::Get().RecordSnapshot(scene, "Shade Smooth: " + obj.name);
                    obj.SetSmoothShading(true, obj.autoSmoothAngle);
                    for (int cid : obj.childIds) {
                        if (GameObject* child = scene.FindObject(cid)) {
                            child->SetSmoothShading(true, obj.autoSmoothAngle);
                        }
                    }
                    AddLog("LogActor", "Applied Shade Smooth to " + obj.name, 2);
                    RequestViewportRedraw(2);
                }
                if (ImGui::MenuItem("Shade Flat")) {
                    UndoManager::Get().RecordSnapshot(scene, "Shade Flat: " + obj.name);
                    obj.SetSmoothShading(false);
                    for (int cid : obj.childIds) {
                        if (GameObject* child = scene.FindObject(cid)) {
                            child->SetSmoothShading(false);
                        }
                    }
                    AddLog("LogActor", "Applied Shade Flat to " + obj.name, 2);
                    RequestViewportRedraw(2);
                }
            }
        }
        if (ImGui::BeginMenu("Add Child")) {
            if (ImGui::BeginMenu("3D Meshes / Primitives")) {
                if (ImGui::MenuItem("Cube"))      { scene.AddObject(PrimitiveType::Cube,     glm::vec3(0), glm::vec3(0.55f), obj.id); AddLog("LogActor", "Added Cube as child", 2); }
                if (ImGui::MenuItem("Box"))       { scene.AddObject(PrimitiveType::Box,      glm::vec3(0), glm::vec3(0.55f), obj.id); AddLog("LogActor", "Added Box as child", 2); }
                if (ImGui::MenuItem("Plane"))     { scene.AddObject(PrimitiveType::Plane,    glm::vec3(0), glm::vec3(0.55f), obj.id); AddLog("LogActor", "Added Plane as child", 2); }
                if (ImGui::MenuItem("Sphere"))    { scene.AddObject(PrimitiveType::Sphere,   glm::vec3(0), glm::vec3(0.55f), obj.id); AddLog("LogActor", "Added Sphere as child", 2); }
                if (ImGui::MenuItem("UV Sphere")) { scene.AddObject(PrimitiveType::UVSphere, glm::vec3(0), glm::vec3(0.55f), obj.id); AddLog("LogActor", "Added UV Sphere as child", 2); }
                if (ImGui::MenuItem("Icosphere")) { scene.AddObject(PrimitiveType::Icosphere,glm::vec3(0), glm::vec3(0.55f), obj.id); AddLog("LogActor", "Added Icosphere as child", 2); }
                if (ImGui::MenuItem("Cylinder"))  { scene.AddObject(PrimitiveType::Cylinder, glm::vec3(0), glm::vec3(0.55f), obj.id); AddLog("LogActor", "Added Cylinder as child", 2); }
                if (ImGui::MenuItem("Cone"))      { scene.AddObject(PrimitiveType::Cone,     glm::vec3(0), glm::vec3(0.55f), obj.id); AddLog("LogActor", "Added Cone as child", 2); }
                if (ImGui::MenuItem("Capsule"))   { scene.AddObject(PrimitiveType::Capsule,  glm::vec3(0), glm::vec3(0.55f), obj.id); AddLog("LogActor", "Added Capsule as child", 2); }
                if (ImGui::MenuItem("Torus"))     { scene.AddObject(PrimitiveType::Torus,    glm::vec3(0), glm::vec3(0.55f), obj.id); AddLog("LogActor", "Added Torus as child", 2); }
                if (ImGui::MenuItem("Circle"))    { scene.AddObject(PrimitiveType::Circle,   glm::vec3(0), glm::vec3(0.55f), obj.id); AddLog("LogActor", "Added Circle as child", 2); }
                if (ImGui::MenuItem("Disc"))      { scene.AddObject(PrimitiveType::Disc,     glm::vec3(0), glm::vec3(0.55f), obj.id); AddLog("LogActor", "Added Disc as child", 2); }
                if (ImGui::MenuItem("Quad"))      { scene.AddObject(PrimitiveType::Quad,     glm::vec3(0), glm::vec3(0.55f), obj.id); AddLog("LogActor", "Added Quad as child", 2); }
                if (ImGui::MenuItem("Triangle"))  { scene.AddObject(PrimitiveType::Triangle, glm::vec3(0), glm::vec3(0.55f), obj.id); AddLog("LogActor", "Added Triangle as child", 2); }
                if (ImGui::MenuItem("Pyramid"))   { scene.AddObject(PrimitiveType::Pyramid,  glm::vec3(0), glm::vec3(0.55f), obj.id); AddLog("LogActor", "Added Pyramid as child", 2); }
                if (ImGui::MenuItem("Prism"))     { scene.AddObject(PrimitiveType::Prism,    glm::vec3(0), glm::vec3(0.55f), obj.id); AddLog("LogActor", "Added Prism as child", 2); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Lights")) {
                if (ImGui::MenuItem("Directional Light")) { scene.AddNewLight(PrimitiveType::DirectionalLight, glm::vec3(0, 2.5f, 0), obj.id); AddLog("LogActor", "Added Directional Light as child", 2); }
                if (ImGui::MenuItem("Point Light"))       { scene.AddNewLight(PrimitiveType::PointLight,       glm::vec3(0, 2.5f, 0), obj.id); AddLog("LogActor", "Added Point Light as child", 2); }
                if (ImGui::MenuItem("Spot Light"))        { scene.AddNewLight(PrimitiveType::SpotLight,        glm::vec3(0, 2.5f, 0), obj.id); AddLog("LogActor", "Added Spot Light as child", 2); }
                if (ImGui::MenuItem("Area Light"))        { scene.AddNewLight(PrimitiveType::AreaLight,        glm::vec3(0, 2.5f, 0), obj.id); AddLog("LogActor", "Added Area Light as child", 2); }
                if (ImGui::MenuItem("Sky Light"))         { scene.AddNewLight(PrimitiveType::SkyLight,         glm::vec3(0, 2.5f, 0), obj.id); AddLog("LogActor", "Added Sky Light as child", 2); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Volumes / Collisions")) {
                if (ImGui::BeginMenu("Collision Shapes (Blockers)")) {
                    if (ImGui::MenuItem("Box Collision"))      { scene.AddCollisionActor(CollisionShapeType::Box,       glm::vec3(0), false, obj.id); AddLog("LogActor", "Added Box Collision as child", 2); }
                    if (ImGui::MenuItem("Sphere Collision"))   { scene.AddCollisionActor(CollisionShapeType::Sphere,    glm::vec3(0), false, obj.id); AddLog("LogActor", "Added Sphere Collision as child", 2); }
                    if (ImGui::MenuItem("Capsule Collision"))  { scene.AddCollisionActor(CollisionShapeType::Capsule,   glm::vec3(0), false, obj.id); AddLog("LogActor", "Added Capsule Collision as child", 2); }
                    if (ImGui::MenuItem("Cylinder Collision")) { scene.AddCollisionActor(CollisionShapeType::Cylinder,  glm::vec3(0), false, obj.id); AddLog("LogActor", "Added Cylinder Collision as child", 2); }
                    if (ImGui::MenuItem("Plane Collision"))    { scene.AddCollisionActor(CollisionShapeType::Plane,     glm::vec3(0), false, obj.id); AddLog("LogActor", "Added Plane Collision as child", 2); }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Trigger Volumes")) {
                    if (ImGui::MenuItem("Box Trigger"))        { scene.AddCollisionActor(CollisionShapeType::Box,       glm::vec3(0), true, obj.id); AddLog("LogActor", "Added Box Trigger as child", 2); }
                    if (ImGui::MenuItem("Sphere Trigger"))     { scene.AddCollisionActor(CollisionShapeType::Sphere,    glm::vec3(0), true, obj.id); AddLog("LogActor", "Added Sphere Trigger as child", 2); }
                    if (ImGui::MenuItem("Capsule Trigger"))    { scene.AddCollisionActor(CollisionShapeType::Capsule,   glm::vec3(0), true, obj.id); AddLog("LogActor", "Added Capsule Trigger as child", 2); }
                    if (ImGui::MenuItem("Cylinder Trigger"))   { scene.AddCollisionActor(CollisionShapeType::Cylinder,  glm::vec3(0), true, obj.id); AddLog("LogActor", "Added Cylinder Trigger as child", 2); }
                    if (ImGui::MenuItem("Plane Trigger"))      { scene.AddCollisionActor(CollisionShapeType::Plane,     glm::vec3(0), true, obj.id); AddLog("LogActor", "Added Plane Trigger as child", 2); }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Camera")) {
                GameObject& cam = scene.AddObject(PrimitiveType::Camera, glm::vec3(0.0f, 2.0f, -4.0f), glm::vec3(0.55f), obj.id);
                cam.name = "Camera Actor";
                AddLog("LogActor", "Added Camera as child", 2);
            }
            if (ImGui::MenuItem("User Interface")) {
                scene.AddUserInterface(glm::vec3(0.0f, 1.5f, 0.0f), "", obj.id);
                AddLog("LogActor", "Added User Interface as child", 2);
            }
            if (ImGui::MenuItem("Terrain")) {
                UndoManager::Get().RecordSnapshot(scene, "Add Terrain Child");
                GameObject& t = scene.AddTerrain(glm::vec3(0.0f), 64.0f, 64.0f, 128, obj.id);
                selectedActorIds.clear();
                selectedActorIds.insert(t.id);
                scene.selectedId = t.id;
                AddLog("LogActor", "Added Terrain as child: " + t.name, 2);
            }
            if (ImGui::BeginMenu("Water Body")) {
                if (ImGui::MenuItem("Ocean"))       { scene.AddWaterBody(WaterBodyType::Ocean,      glm::vec3(0), "Ocean",       obj.id); AddLog("LogActor", "Added Ocean as child", 2); }
                if (ImGui::MenuItem("Sea"))          { scene.AddWaterBody(WaterBodyType::Sea,        glm::vec3(0), "Sea",         obj.id); AddLog("LogActor", "Added Sea as child", 2); }
                if (ImGui::MenuItem("Lake"))         { scene.AddWaterBody(WaterBodyType::Lake,       glm::vec3(0), "Lake",        obj.id); AddLog("LogActor", "Added Lake as child", 2); }
                if (ImGui::MenuItem("River"))        { scene.AddWaterBody(WaterBodyType::River,      glm::vec3(0), "River",       obj.id); AddLog("LogActor", "Added River as child", 2); }
                if (ImGui::MenuItem("Stream"))       { scene.AddWaterBody(WaterBodyType::Stream,     glm::vec3(0), "Stream",      obj.id); AddLog("LogActor", "Added Stream as child", 2); }
                if (ImGui::MenuItem("Waterfall"))    { scene.AddWaterBody(WaterBodyType::Waterfall,  glm::vec3(0), "Waterfall",   obj.id); AddLog("LogActor", "Added Waterfall as child", 2); }
                if (ImGui::MenuItem("Pond"))         { scene.AddWaterBody(WaterBodyType::Pond,       glm::vec3(0), "Pond",        obj.id); AddLog("LogActor", "Added Pond as child", 2); }
                if (ImGui::MenuItem("Pool"))         { scene.AddWaterBody(WaterBodyType::Pool,       glm::vec3(0), "Pool",        obj.id); AddLog("LogActor", "Added Pool as child", 2); }
                if (ImGui::MenuItem("Wet Surface"))  { scene.AddWaterBody(WaterBodyType::WetSurface, glm::vec3(0), "Wet Surface", obj.id); AddLog("LogActor", "Added Wet Surface as child", 2); }
                if (ImGui::MenuItem("Puddle"))       { scene.AddWaterBody(WaterBodyType::Puddle,     glm::vec3(0), "Puddle",      obj.id); AddLog("LogActor", "Added Puddle as child", 2); }
                if (ImGui::MenuItem("Flood Water"))  { scene.AddWaterBody(WaterBodyType::FloodWater, glm::vec3(0), "Flood Water", obj.id); AddLog("LogActor", "Added Flood Water as child", 2); }
                if (ImGui::MenuItem("Underwater"))   { scene.AddWaterBody(WaterBodyType::Underwater, glm::vec3(0), "Underwater",  obj.id); AddLog("LogActor", "Added Underwater as child", 2); }
                if (ImGui::MenuItem("Swamp"))        { scene.AddWaterBody(WaterBodyType::Swamp,      glm::vec3(0), "Swamp",       obj.id); AddLog("LogActor", "Added Swamp as child", 2); }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Empty Actor")) {
                scene.AddEmptyActor(glm::vec3(0), obj.id);
                AddLog("LogActor", "Added Empty Actor as child", 2);
            }
            if (ImGui::MenuItem("Level Animation")) {
                GameObject& la = scene.AddEmptyActor(glm::vec3(0), obj.id);
                la.name = "LevelAnimation";
                la.type = PrimitiveType::LevelAnimation;
                la.AddLevelAnimation();
                AddLog("LogActor", "Added Level Animation as child", 2);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Delete")) {
            if (selectedActorIds.count(obj.id) && selectedActorIds.size() > 1) {
                for (int delId : selectedActorIds) scene.RemoveObject(delId);
                selectedActorIds.clear();
                scene.selectedId = -1;
            } else {
                scene.RemoveObject(obj.id);
                selectedActorIds.erase(obj.id);
                if (scene.selectedId == obj.id) scene.selectedId = -1;
            }
        }
        ImGui::EndPopup();
    }

    // Drag source: drag this actor to reparent it
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        int actorId = obj.id;
        ImGui::SetDragDropPayload("OUTLINER_ACTOR", &actorId, sizeof(int));
        ImGui::Text("Reparent %s", obj.name.c_str());
        ImGui::EndDragDropSource();
    }

    // Drag target: drop onto this actor to make dragged actor a child (deferred to end of outliner)
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OUTLINER_ACTOR")) {
            if (payload->Data && payload->DataSize == sizeof(int)) {
                int draggedId = *(const int*)payload->Data;
                if (draggedId != obj.id && !scene.IsDescendantOf(obj.id, draggedId)) {
                    m_pendingReparentChild = draggedId;
                    m_pendingReparentParent = obj.id;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (nodeOpen) {
        if (hasChildren) {
            std::vector<int> childList = obj.childIds;
            for (int cid : childList) {
                GameObject* childObj = scene.FindObject(cid);
                if (childObj) {
                    DrawOutlinerNode(*childObj, scene, visitedIds, depth + 1);
                }
            }
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void EngineUI::RenderOutliner(Scene& scene) {
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // Dockable window: size hint for first launch, can be repositioned via DockSpace
    ImGui::SetNextWindowSize(ImVec2(rightSidebarWidth, vp->Size.y * 0.55f), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Outliner", &showOutliner, flags)) {
        // Outliner Header (dev.md Outliner Header)
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.894f, 0.894f, 0.906f, 1.0f), "Outliner"); // text-gray-200

        float addBtnW = 20.0f * uiScale;
        float closeBtnW = 20.0f * uiScale;
        float rightBtnsW = addBtnW + closeBtnW + 6.0f * uiScale;

        if (ImGui::GetContentRegionAvail().x > rightBtnsW) {
            ImGui::SameLine(ImGui::GetWindowWidth() - rightBtnsW - ImGui::GetStyle().WindowPadding.x);
        } else {
            ImGui::SameLine();
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.122f, 0.122f, 0.137f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.631f, 0.631f, 0.667f, 1.0f));
        if (ImGui::Button("+##AddActorBtn", ImVec2(addBtnW, 18.0f * uiScale))) {
            ImGui::OpenPopup("AddActorTopPopup");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add Actor");

        ImGui::SameLine(0.0f, 2.0f * uiScale);
        if (ImGui::Button("Ã¢Å“â€¢##CloseOutlinerBtn", ImVec2(closeBtnW, 18.0f * uiScale))) {
            showOutliner = false;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close Outliner");
        ImGui::PopStyleColor(4);

        if (ImGui::BeginPopup("AddActorTopPopup")) {
            if (ImGui::MenuItem("Cube"))      { scene.AddObject(PrimitiveType::Cube, glm::vec3(0, 0.5f, 0), glm::vec3(0.55f)); AddLog("LogActor", "Spawned Cube", 2); }
            if (ImGui::MenuItem("Plane"))     { scene.AddObject(PrimitiveType::Plane, glm::vec3(0), glm::vec3(0.55f)); AddLog("LogActor", "Spawned Plane", 2); }
            if (ImGui::MenuItem("Sphere"))    { scene.AddObject(PrimitiveType::Sphere, glm::vec3(0, 0.6f, 0), glm::vec3(0.55f)); AddLog("LogActor", "Spawned Sphere", 2); }
            if (ImGui::MenuItem("Cylinder"))  { scene.AddObject(PrimitiveType::Cylinder, glm::vec3(0, 0.6f, 0), glm::vec3(0.55f)); AddLog("LogActor", "Spawned Cylinder", 2); }
            if (ImGui::MenuItem("Cone"))      { scene.AddObject(PrimitiveType::Cone, glm::vec3(0), glm::vec3(0.55f)); AddLog("LogActor", "Spawned Cone", 2); }
            if (ImGui::MenuItem("Torus"))     { scene.AddObject(PrimitiveType::Torus, glm::vec3(0, 0.6f, 0), glm::vec3(0.55f)); AddLog("LogActor", "Spawned Torus", 2); }
            ImGui::Separator();
            if (ImGui::MenuItem("Directional Light")) { scene.AddNewLight(PrimitiveType::DirectionalLight, glm::vec3(0, 2.5f, 0)); AddLog("LogActor", "Added Directional Light", 2); }
            if (ImGui::MenuItem("Point Light"))       { scene.AddNewLight(PrimitiveType::PointLight,       glm::vec3(0, 2.5f, 0)); AddLog("LogActor", "Added Point Light", 2); }
            if (ImGui::MenuItem("Spot Light"))        { scene.AddNewLight(PrimitiveType::SpotLight,        glm::vec3(0, 2.5f, 0)); AddLog("LogActor", "Added Spot Light", 2); }
            ImGui::Separator();
            if (ImGui::MenuItem("Camera"))            { GameObject& c = scene.AddObject(PrimitiveType::Camera, glm::vec3(0.0f, 2.0f, -4.0f)); c.name = "Camera Actor"; AddLog("LogActor", "Spawned Camera", 2); }
            if (ImGui::MenuItem("Empty Actor"))       { scene.AddEmptyActor(glm::vec3(0)); AddLog("LogActor", "Spawned Empty Actor", 2); }
            if (ImGui::MenuItem("Level Animation"))   {
                GameObject& la = scene.AddEmptyActor(glm::vec3(0));
                la.name = "LevelAnimation";
                la.type = PrimitiveType::LevelAnimation;
                la.AddLevelAnimation();
                AddLog("LogActor", "Spawned Level Animation Actor", 2);
            }
            ImGui::EndPopup();
        }

        // Outliner Search Row (dev.md)
        ImGui::PushItemWidth(-1.0f);
        ImGui::InputTextWithHint("##Filter", "Search Actors...", outlinerFilter, sizeof(outlinerFilter));
        ImGui::PopItemWidth();
        ImGui::Separator();

        // Outliner Actor List Table
        ImGui::BeginChild("OutlinerList", ImVec2(0, 0), false);

        // If filtering, display matching flat list
        if (outlinerFilter[0] != '\0') {
            std::string filterStr = outlinerFilter;
            std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

            for (auto& obj : scene.objects) {
                std::string nameStr = obj.name;
                std::transform(nameStr.begin(), nameStr.end(), nameStr.begin(), ::tolower);
                bool matches = (nameStr.find(filterStr) != std::string::npos);
                if (!matches) {
                    for (const auto& t : obj.tags) {
                        std::string tLow = t;
                        std::transform(tLow.begin(), tLow.end(), tLow.begin(), ::tolower);
                        if (tLow.find(filterStr) != std::string::npos) {
                            matches = true;
                            break;
                        }
                    }
                }
                if (!matches) continue;

                ImGui::PushID(obj.id);

                ImVec2 rowPos = ImGui::GetCursorScreenPos();
                float rowHeight = ImGui::GetFrameHeight();
                ImVec2 rowMin(ImGui::GetWindowPos().x, rowPos.y);
                ImVec2 rowMax(ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), rowPos.y + rowHeight);
                bool isRowHovered = ImGui::IsMouseHoveringRect(rowMin, rowMax);

                if (outlinerEyeIconGpuHandle) {
                    ImVec4 eyeTint;
                    if (obj.visible) {
                        eyeTint = isRowHovered ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 0.0f);
                    } else {
                        eyeTint = isRowHovered ? ImVec4(1.0f, 1.0f, 1.0f, 0.85f) : ImVec4(1.0f, 1.0f, 1.0f, 0.35f);
                    }

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.25f, 0.35f, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.15f, 0.25f, 0.7f));
                    if (ImGui::ImageButton("##FilterEyeBtn", (ImTextureID)outlinerEyeIconGpuHandle, ImVec2(14.0f, 14.0f), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), eyeTint)) {
                        obj.visible = !obj.visible;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(obj.visible ? "Hide Actor in Viewport" : "Show Actor in Viewport");
                    }
                    ImGui::PopStyleColor(3);
                } else {
                    if (ImGui::SmallButton(obj.visible ? "eye" : " - ")) {
                        obj.visible = !obj.visible;
                    }
                }
                ImGui::SameLine();

                bool isSelected = (selectedActorIds.count(obj.id) > 0 || scene.selectedId == obj.id);
                if (isSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.14f, 0.45f, 0.85f, 0.75f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.52f, 0.95f, 0.85f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.22f, 0.58f, 1.00f, 0.95f));
                }
                ImGui::Selectable(obj.name.c_str(), isSelected);
                bool isClickedUp = ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                float dragDistSqr = ImGui::GetIO().MouseDragMaxDistanceSqr[0];
                float dragThreshold = ImGui::GetIO().MouseDragThreshold;
                bool wasDragging = (dragDistSqr >= dragThreshold * dragThreshold) || (ImGui::GetDragDropPayload() != nullptr);
                if (isClickedUp && !wasDragging) {
                    ImGuiIO& io = ImGui::GetIO();
                    if (io.KeyCtrl) {
                        if (selectedActorIds.count(obj.id)) {
                            selectedActorIds.erase(obj.id);
                            if (scene.selectedId == obj.id) {
                                scene.selectedId = selectedActorIds.empty() ? -1 : *selectedActorIds.begin();
                            }
                        } else {
                            selectedActorIds.insert(obj.id);
                            scene.selectedId = obj.id;
                        }
                        lastSelectedActorId = obj.id;
                    } else if (io.KeyShift && lastSelectedActorId != -1) {
                        int idx1 = -1, idx2 = -1;
                        for (size_t i = 0; i < scene.objects.size(); ++i) {
                            if (scene.objects[i].id == lastSelectedActorId) idx1 = (int)i;
                            if (scene.objects[i].id == obj.id) idx2 = (int)i;
                        }
                        if (idx1 != -1 && idx2 != -1) {
                            int start = std::min(idx1, idx2);
                            int end = std::max(idx1, idx2);
                            selectedActorIds.clear();
                            for (int i = start; i <= end; ++i) {
                                selectedActorIds.insert(scene.objects[i].id);
                            }
                            scene.selectedId = obj.id;
                        } else {
                            selectedActorIds.clear();
                            selectedActorIds.insert(obj.id);
                            scene.selectedId = obj.id;
                            lastSelectedActorId = obj.id;
                        }
                    } else {
                        selectedActorIds.clear();
                        selectedActorIds.insert(obj.id);
                        scene.selectedId = obj.id;
                        lastSelectedActorId = obj.id;
                    }
                }
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    int actorId = obj.id;
                    ImGui::SetDragDropPayload("OUTLINER_ACTOR", &actorId, sizeof(int));
                    ImGui::Text("Reparent %s", obj.name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (isSelected) {
                    ImGui::PopStyleColor(3);
                }
                ImGui::PopID();
            }
        } else {
            // Hierarchical tree view: Level name as Root of all in the respective level
            std::string levelName = "Untitled Level";
            if (!currentLevelFilePath.empty()) {
                levelName = std::filesystem::path(currentLevelFilePath).stem().string();
            }

            ImGuiTreeNodeFlags levelFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            std::string rootKey = "##LevelRootNode";
            if (m_outlinerStateNeedsApply && m_outlinerAppliedNodes.find(rootKey) == m_outlinerAppliedNodes.end()) {
                auto it = scene.outlinerNodeOpenState.find(rootKey);
                if (it != scene.outlinerNodeOpenState.end()) {
                    ImGui::SetNextItemOpen(it->second);
                }
                m_outlinerAppliedNodes.insert(rootKey);
            }
            bool levelNodeOpen = ImGui::TreeNodeEx("##LevelRootNode", levelFlags, "%s (%d actors)", levelName.c_str(), (int)scene.objects.size());
            scene.outlinerNodeOpenState[rootKey] = levelNodeOpen;

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OUTLINER_ACTOR")) {
                    if (payload->Data && payload->DataSize == sizeof(int)) {
                        int draggedId = *(const int*)payload->Data;
                        m_pendingReparentChild = draggedId;
                        m_pendingReparentParent = -1;
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (levelNodeOpen) {
                std::unordered_set<int> visitedIds;
                std::vector<int> rootIds;
                for (const auto& obj : scene.objects) {
                    if (obj.parentId == -1) {
                        rootIds.push_back(obj.id);
                    }
                }
                for (int rid : rootIds) {
                    GameObject* rootObj = scene.FindObject(rid);
                    if (rootObj) {
                        DrawOutlinerNode(*rootObj, scene, visitedIds, 0);
                    }
                }
                ImGui::TreePop();
            }
        }

        // Drop on Outliner empty space to unparent / move to root
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OUTLINER_ACTOR")) {
                if (payload->Data && payload->DataSize == sizeof(int)) {
                    int draggedId = *(const int*)payload->Data;
                    m_pendingReparentChild = draggedId;
                    m_pendingReparentParent = -1;
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Right click context menu on outliner background
        if (ImGui::BeginPopupContextWindow("OutlinerContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::BeginMenu("3D Meshes / Primitives")) {
                if (ImGui::MenuItem("Cube"))      { scene.AddObject(PrimitiveType::Cube); AddLog("LogActor", "Spawned Cube", 2); }
                if (ImGui::MenuItem("Box"))       { scene.AddObject(PrimitiveType::Box); AddLog("LogActor", "Spawned Box", 2); }
                if (ImGui::MenuItem("Plane"))     { scene.AddObject(PrimitiveType::Plane); AddLog("LogActor", "Spawned Plane", 2); }
                if (ImGui::MenuItem("Sphere"))    { scene.AddObject(PrimitiveType::Sphere); AddLog("LogActor", "Spawned Sphere", 2); }
                if (ImGui::MenuItem("UV Sphere")) { scene.AddObject(PrimitiveType::UVSphere); AddLog("LogActor", "Spawned UV Sphere", 2); }
                if (ImGui::MenuItem("Icosphere")) { scene.AddObject(PrimitiveType::Icosphere); AddLog("LogActor", "Spawned Icosphere", 2); }
                if (ImGui::MenuItem("Cylinder"))  { scene.AddObject(PrimitiveType::Cylinder); AddLog("LogActor", "Spawned Cylinder", 2); }
                if (ImGui::MenuItem("Cone"))      { scene.AddObject(PrimitiveType::Cone); AddLog("LogActor", "Spawned Cone", 2); }
                if (ImGui::MenuItem("Capsule"))   { scene.AddObject(PrimitiveType::Capsule); AddLog("LogActor", "Spawned Capsule", 2); }
                if (ImGui::MenuItem("Torus"))     { scene.AddObject(PrimitiveType::Torus); AddLog("LogActor", "Spawned Torus", 2); }
                if (ImGui::MenuItem("Circle"))    { scene.AddObject(PrimitiveType::Circle); AddLog("LogActor", "Spawned Circle", 2); }
                if (ImGui::MenuItem("Disc"))      { scene.AddObject(PrimitiveType::Disc); AddLog("LogActor", "Spawned Disc", 2); }
                if (ImGui::MenuItem("Quad"))      { scene.AddObject(PrimitiveType::Quad); AddLog("LogActor", "Spawned Quad", 2); }
                if (ImGui::MenuItem("Triangle"))  { scene.AddObject(PrimitiveType::Triangle); AddLog("LogActor", "Spawned Triangle", 2); }
                if (ImGui::MenuItem("Pyramid"))   { scene.AddObject(PrimitiveType::Pyramid); AddLog("LogActor", "Spawned Pyramid", 2); }
                if (ImGui::MenuItem("Prism"))     { scene.AddObject(PrimitiveType::Prism); AddLog("LogActor", "Spawned Prism", 2); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Lights")) {
                if (ImGui::MenuItem("Directional Light")) { scene.AddNewLight(PrimitiveType::DirectionalLight); AddLog("LogActor", "Spawned Directional Light", 2); }
                if (ImGui::MenuItem("Point Light"))       { scene.AddNewLight(PrimitiveType::PointLight); AddLog("LogActor", "Spawned Point Light", 2); }
                if (ImGui::MenuItem("Spot Light"))        { scene.AddNewLight(PrimitiveType::SpotLight); AddLog("LogActor", "Spawned Spot Light", 2); }
                if (ImGui::MenuItem("Area Light"))        { scene.AddNewLight(PrimitiveType::AreaLight); AddLog("LogActor", "Spawned Area Light", 2); }
                if (ImGui::MenuItem("Sky Light"))         { scene.AddNewLight(PrimitiveType::SkyLight); AddLog("LogActor", "Spawned Sky Light", 2); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Volumes / Collisions")) {
                if (ImGui::BeginMenu("Collision Shapes (Blockers)")) {
                    if (ImGui::MenuItem("Box Collision"))      { scene.AddCollisionActor(CollisionShapeType::Box,       {0,0.5f,0}, false); AddLog("LogActor", "Spawned Box Collision Volume", 2); }
                    if (ImGui::MenuItem("Sphere Collision"))   { scene.AddCollisionActor(CollisionShapeType::Sphere,    {0,0.5f,0}, false); AddLog("LogActor", "Spawned Sphere Collision Volume", 2); }
                    if (ImGui::MenuItem("Capsule Collision"))  { scene.AddCollisionActor(CollisionShapeType::Capsule,   {0,0.5f,0}, false); AddLog("LogActor", "Spawned Capsule Collision Volume", 2); }
                    if (ImGui::MenuItem("Cylinder Collision")) { scene.AddCollisionActor(CollisionShapeType::Cylinder,  {0,0.5f,0}, false); AddLog("LogActor", "Spawned Cylinder Collision Volume", 2); }
                    if (ImGui::MenuItem("Plane Collision"))    { scene.AddCollisionActor(CollisionShapeType::Plane,     {0,0.0f,0}, false); AddLog("LogActor", "Spawned Plane Collision Volume", 2); }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Trigger Volumes")) {
                    if (ImGui::MenuItem("Box Trigger"))        { scene.AddCollisionActor(CollisionShapeType::Box,       {0,0.5f,0}, true); AddLog("LogActor", "Spawned Box Trigger Volume", 2); }
                    if (ImGui::MenuItem("Sphere Trigger"))     { scene.AddCollisionActor(CollisionShapeType::Sphere,    {0,0.5f,0}, true); AddLog("LogActor", "Spawned Sphere Trigger Volume", 2); }
                    if (ImGui::MenuItem("Capsule Trigger"))    { scene.AddCollisionActor(CollisionShapeType::Capsule,   {0,0.5f,0}, true); AddLog("LogActor", "Spawned Capsule Trigger Volume", 2); }
                    if (ImGui::MenuItem("Cylinder Trigger"))   { scene.AddCollisionActor(CollisionShapeType::Cylinder,  {0,0.5f,0}, true); AddLog("LogActor", "Spawned Cylinder Trigger Volume", 2); }
                    if (ImGui::MenuItem("Plane Trigger"))      { scene.AddCollisionActor(CollisionShapeType::Plane,     {0,0.0f,0}, true); AddLog("LogActor", "Spawned Plane Trigger Volume", 2); }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Camera")) {
                GameObject& cam = scene.AddObject(PrimitiveType::Camera, {0.0f, 2.0f, -4.0f});
                cam.name = "Camera Actor";
                AddLog("LogActor", "Spawned Camera Actor via Outliner Context Menu", 2);
            }
            if (ImGui::MenuItem("User Interface")) {
                scene.AddUserInterface();
                AddLog("LogActor", "Spawned User Interface Actor", 2);
            }
            if (ImGui::MenuItem("Terrain")) {
                UndoManager::Get().RecordSnapshot(scene, "Add Terrain");
                GameObject& terrain = scene.AddTerrain();
                selectedActorIds.clear();
                selectedActorIds.insert(terrain.id);
                scene.selectedId = terrain.id;
                scene.outlinerNodeOpenState["##LevelRootNode"] = true;
                AddLog("LogActor", "Spawned Terrain Actor: " + terrain.name, 2);
            }
            if (ImGui::BeginMenu("Water Body")) {
                if (ImGui::MenuItem("Ocean"))       { scene.AddWaterBody(WaterBodyType::Ocean,      {0,0,0}, "Ocean");       AddLog("LogActor", "Spawned Ocean Water Body", 2); }
                if (ImGui::MenuItem("Sea"))          { scene.AddWaterBody(WaterBodyType::Sea,        {0,0,0}, "Sea");         AddLog("LogActor", "Spawned Sea Water Body", 2); }
                if (ImGui::MenuItem("Lake"))         { scene.AddWaterBody(WaterBodyType::Lake,       {0,0,0}, "Lake");        AddLog("LogActor", "Spawned Lake Water Body", 2); }
                if (ImGui::MenuItem("River"))        { scene.AddWaterBody(WaterBodyType::River,      {0,0,0}, "River");       AddLog("LogActor", "Spawned River Water Body", 2); }
                if (ImGui::MenuItem("Stream"))       { scene.AddWaterBody(WaterBodyType::Stream,     {0,0,0}, "Stream");      AddLog("LogActor", "Spawned Stream Water Body", 2); }
                if (ImGui::MenuItem("Waterfall"))    { scene.AddWaterBody(WaterBodyType::Waterfall,  {0,0,0}, "Waterfall");   AddLog("LogActor", "Spawned Waterfall Water Body", 2); }
                if (ImGui::MenuItem("Pond"))         { scene.AddWaterBody(WaterBodyType::Pond,       {0,0,0}, "Pond");        AddLog("LogActor", "Spawned Pond Water Body", 2); }
                if (ImGui::MenuItem("Pool"))         { scene.AddWaterBody(WaterBodyType::Pool,       {0,0,0}, "Pool");        AddLog("LogActor", "Spawned Pool Water Body", 2); }
                if (ImGui::MenuItem("Wet Surface"))  { scene.AddWaterBody(WaterBodyType::WetSurface, {0,0,0}, "Wet Surface"); AddLog("LogActor", "Spawned Wet Surface Water Body", 2); }
                if (ImGui::MenuItem("Puddle"))       { scene.AddWaterBody(WaterBodyType::Puddle,     {0,0,0}, "Puddle");      AddLog("LogActor", "Spawned Puddle Water Body", 2); }
                if (ImGui::MenuItem("Flood Water"))  { scene.AddWaterBody(WaterBodyType::FloodWater, {0,0,0}, "Flood Water"); AddLog("LogActor", "Spawned Flood Water Body", 2); }
                if (ImGui::MenuItem("Underwater"))   { scene.AddWaterBody(WaterBodyType::Underwater, {0,0,0}, "Underwater");  AddLog("LogActor", "Spawned Underwater Volume", 2); }
                if (ImGui::MenuItem("Swamp"))        { scene.AddWaterBody(WaterBodyType::Swamp,      {0,0,0}, "Swamp");       AddLog("LogActor", "Spawned Swamp Water Body", 2); }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Empty Actor")) { scene.AddEmptyActor(); AddLog("LogActor", "Spawned Empty Actor", 2); }
            if (ImGui::MenuItem("Level Animation")) {
                GameObject& la = scene.AddEmptyActor();
                la.name = "LevelAnimation";
                la.type = PrimitiveType::LevelAnimation;
                la.AddLevelAnimation();
                AddLog("LogActor", "Spawned Level Animation Actor", 2);
            }
            ImGui::EndPopup();
        }

        ImGui::EndChild();
        // Process any deferred reparent request after all outliner nodes and tree pops have cleanly finished
        if (m_pendingReparentChild != -1) {
            int childId = m_pendingReparentChild;
            int parentId = m_pendingReparentParent;
            m_pendingReparentChild = -1;
            m_pendingReparentParent = -1;

            GameObject* child = scene.FindObject(childId);
            std::string childName = child ? child->name : ("Actor " + std::to_string(childId));

            if (parentId != -1) {
                GameObject* parent = scene.FindObject(parentId);
                std::string parentName = parent ? parent->name : ("Actor " + std::to_string(parentId));
                scene.ReparentObject(childId, parentId);
                AddLog("LogActor", "Reparented '" + childName + "' into '" + parentName + "'", 0);
                EngineLogger::Get().LogAction("REPARENT_ACTOR", childName, "Parent: " + parentName);
            } else {
                scene.ReparentObject(childId, -1);
                AddLog("LogActor", "Detached '" + childName + "' to Root Level", 0);
                EngineLogger::Get().LogAction("UNPARENT_ACTOR", childName, "Moved to Root");
            }
        }
    }
    ImGui::End();
}

bool EngineUI::DrawVec3Row(const char* rowLabel, glm::vec3& values, float resetValue, float speed, bool isScale, bool* lockAspect) {
    bool modified = false;
    ImGui::PushID(rowLabel);

    ImGui::Text("%s", rowLabel);

    float availW = ImGui::GetContentRegionAvail().x;
    float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
    // 3 columns: X, Y, Z
    float colW = (availW - itemSpacing * 2.0f) / 3.0f;
    float btnW = 20.0f * uiScale;
    float dragW = std::max(20.0f, colW - btnW - 2.0f);

    struct AxisInfo {
        const char* label;
        float* val;
        glm::vec4 col;
    };
    AxisInfo axes[3] = {
        { "X", &values.x, glm::vec4(0.85f, 0.22f, 0.22f, 1.0f) },
        { "Y", &values.y, glm::vec4(0.25f, 0.75f, 0.25f, 1.0f) },
        { "Z", &values.z, glm::vec4(0.22f, 0.50f, 0.90f, 1.0f) }
    };

    float prevX = values.x;

    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, itemSpacing);
        ImGui::PushID(axes[i].label);

        // Axis button pill
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(axes[i].col.r, axes[i].col.g, axes[i].col.b, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(axes[i].col.r * 1.15f, axes[i].col.g * 1.15f, axes[i].col.b * 1.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(axes[i].col.r * 0.9f, axes[i].col.g * 0.9f, axes[i].col.b * 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        if (ImGui::Button(axes[i].label, ImVec2(btnW, 0))) {
            *axes[i].val = resetValue;
            modified = true;
        }
        ImGui::PopStyleColor(4);

        ImGui::SameLine(0.0f, 2.0f);
        ImGui::SetNextItemWidth(dragW);
        char dragId[32];
        snprintf(dragId, sizeof(dragId), "##val_%s", axes[i].label);
        if (ImGui::DragFloat(dragId, axes[i].val, speed, 0.0f, 0.0f, "%.2f")) {
            modified = true;
            if (isScale && i == 0 && lockAspect && *lockAspect) {
                float ratio = (prevX > 0.001f || prevX < -0.001f) ? (values.x / prevX) : 1.0f;
                values.y *= ratio;
                values.z *= ratio;
            }
        }

        ImGui::PopID();
    }

    ImGui::PopID();
    if (modified) {
        RequestViewportRedraw(10);
    }
    return modified;
}

bool EngineUI::DrawTransformPill(const char* label, float& value, const glm::vec4& color, float resetValue, float speed) {
    bool modified = false;
    ImGui::PushID(label);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.r, color.g, color.b, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.r * 1.15f, color.g * 1.15f, color.b * 1.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.r * 0.9f, color.g * 0.9f, color.b * 0.9f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    char btnLabel[32];
    snprintf(btnLabel, sizeof(btnLabel), "%s##btn_%s", label, label);
    if (ImGui::Button(btnLabel, ImVec2(24, 0))) {
        value = resetValue;
        modified = true;
    }
    ImGui::PopStyleColor(4);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 6.0f);
    char valLabel[32];
    snprintf(valLabel, sizeof(valLabel), "##val_%s", label);
    if (ImGui::DragFloat(valLabel, &value, speed, 0.0f, 0.0f, "%.2f")) {
        modified = true;
    }

    ImGui::PopID();
    if (modified) {
        RequestViewportRedraw(10);
    }
    return modified;
}

struct FoundMat {
    std::string name;        // e.g. "Default_Material"
    std::string relPath;     // e.g. "Materials/Default_Material.emat"
    std::string relFolder;   // e.g. "Materials", or "" for project root
    std::string fullPath;    // Full absolute path on disk
    glm::vec3 color{0.55f, 0.55f, 0.55f};
    float metallic = 0.0f;
    float roughness = 0.5f;
};

static std::vector<FoundMat> s_cachedMatList;
static std::map<std::string, std::vector<FoundMat>> s_cachedFolderGroups;
static auto s_lastMatScanTime = std::chrono::steady_clock::time_point::min();

static void RefreshCachedMaterials(const std::filesystem::path& contentRootPath) {
    auto now = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastMatScanTime).count();
    if (elapsedMs < 2000 && !s_cachedMatList.empty()) return;

    s_lastMatScanTime = now;
    s_cachedMatList.clear();
    s_cachedFolderGroups.clear();

    std::error_code matEc;
    if (std::filesystem::exists(contentRootPath, matEc)) {
        auto options = std::filesystem::directory_options::skip_permission_denied;
        for (auto it = std::filesystem::recursive_directory_iterator(contentRootPath, options, matEc);
             !matEc && it != std::filesystem::recursive_directory_iterator();
             it.increment(matEc)) {
            if (!it->is_directory(matEc)) {
                std::string ext = it->path().extension().string();
                if (ext == ".emat" || ext == ".mat") {
                    FoundMat fm;
                    fm.name = it->path().stem().string();
                    fm.fullPath = it->path().string();

                    std::filesystem::path rel = std::filesystem::relative(it->path(), contentRootPath, matEc);
                    if (!matEc) {
                        fm.relPath = rel.generic_string();
                        std::string parentFolder = rel.parent_path().generic_string();
                        fm.relFolder = (parentFolder == ".") ? "" : parentFolder;
                    } else {
                        fm.relPath = it->path().filename().string();
                        fm.relFolder = "";
                    }

                    MaterialAsset ma;
                    if (EngineUI::LoadMaterialFile(fm.fullPath, ma)) {
                        fm.color = ma.baseColor;
                        fm.metallic = ma.metallic;
                        fm.roughness = ma.roughness;
                    }

                    s_cachedMatList.push_back(fm);
                    s_cachedFolderGroups[fm.relFolder].push_back(fm);
                }
            }
        }
    }

    if (s_cachedMatList.empty()) {
        std::filesystem::path defPath = contentRootPath / "Materials" / "Default_Material.emat";
        FoundMat fm;
        fm.name = "Default_Material";
        fm.relPath = "Materials/Default_Material.emat";
        fm.relFolder = "Materials";
        fm.fullPath = defPath.string();
        fm.color = glm::vec3(0.55f, 0.55f, 0.55f);
        s_cachedMatList.push_back(fm);
        s_cachedFolderGroups[fm.relFolder].push_back(fm);
    }
}

void EngineUI::RenderDetails(Scene& scene) {
    ImGuiViewport* vp = ImGui::GetMainViewport();

    float bottomBarH = bottomBarHeight * uiScale;
    // Dockable window: size hint for first launch, can be repositioned or tabbed via DockSpace
    ImGui::SetNextWindowSize(ImVec2(rightSidebarWidth, vp->Size.y * 0.45f), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Details", &showDetails, flags)) {
        // Details Header (dev.md Details Header)
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.894f, 0.894f, 0.906f, 1.0f), "Details"); // text-gray-200

        float closeBtnW = 20.0f * uiScale;
        if (ImGui::GetContentRegionAvail().x > closeBtnW + 4.0f) {
            ImGui::SameLine(ImGui::GetWindowWidth() - closeBtnW - ImGui::GetStyle().WindowPadding.x);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.122f, 0.122f, 0.137f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.631f, 0.631f, 0.667f, 1.0f));
            if (ImGui::Button("Ã¢Å“â€¢##CloseDetailsBtn", ImVec2(closeBtnW, 18.0f * uiScale))) {
                showDetails = false;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close Details");
            ImGui::PopStyleColor(4);
        }
        ImGui::Separator();

        GameObject* obj = scene.GetSelected();
        if (!obj) {
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.443f, 0.443f, 0.478f, 1.0f)); // text-gray-500 #71717a
            ImGui::TextWrapped("Select an actor in the Outliner or Viewport to view details.");
            ImGui::PopStyleColor();
            ImGui::End();
            return;
        }

        ImGui::PushID(obj->id);

        // Actor Header Banner
        if (obj->isLight || IsLightPrimitive(obj->type)) {
            char lightTypeStr[64];
            snprintf(lightTypeStr, sizeof(lightTypeStr), "[%sComponent]", GetLightTypeName(obj->light.type));
            ImGui::TextColored(ImVec4(1.00f, 0.85f, 0.20f, 1.0f), "%s", lightTypeStr);
        } else if (obj->isWaterBody || obj->type == PrimitiveType::WaterBody) {
            ImGui::TextColored(ImVec4(0.00f, 0.60f, 1.00f, 1.0f), "[WaterBodyComponent]");
        } else if (obj->isTerrain || obj->type == PrimitiveType::Terrain) {
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.0f), "[TerrainComponent]");
        } else if (obj->isCamera || obj->type == PrimitiveType::Camera) {
            ImGui::TextColored(ImVec4(0.30f, 0.90f, 0.70f, 1.0f), "[CameraComponent]");
        } else if (obj->isUserInterface || obj->type == PrimitiveType::UserInterface) {
            ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.00f, 1.0f), "[UserInterface3DComponent]");
        } else if (obj->type == PrimitiveType::LevelAnimation || obj->hasLevelAnimation) {
            ImGui::TextColored(ImVec4(1.00f, 0.55f, 0.20f, 1.0f), "[LevelAnimationComponent]");
        } else if (obj->type == PrimitiveType::Empty) {
            if (!obj->modifiers.empty()) {
                if (obj->modifiers[0].isTrigger) {
                    char trigStr[96];
                    snprintf(trigStr, sizeof(trigStr), "[Trigger Volume: %s]", obj->modifiers[0].name.c_str());
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "%s", trigStr);
                } else {
                    char colStr[96];
                    snprintf(colStr, sizeof(colStr), "[Collision Volume: %s]", obj->modifiers[0].name.c_str());
                    ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "%s", colStr);
                }
            } else {
                ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "[TransformActor]");
            }
        } else {
            char meshTypeStr[64];
            snprintf(meshTypeStr, sizeof(meshTypeStr), "[StaticMeshActor (%s)]", GetPrimitiveTypeName(obj->type));
            ImGui::TextColored(ImVec4(0.20f, 0.65f, 1.00f, 1.0f), "%s", meshTypeStr);
        }
        ImGui::SameLine();
        char nameBuf[128];
        strncpy(nameBuf, obj->name.c_str(), sizeof(nameBuf));
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        ImGui::PushItemWidth(-80.0f);
        if (ImGui::InputText("##ActorName", nameBuf, sizeof(nameBuf))) {
            obj->name = nameBuf;
            if (obj->isLight && obj->lightId >= 0 && obj->lightId < (int)scene.pointLights.size()) {
                scene.pointLights[obj->lightId].name = obj->name;
            }
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::TextDisabled("ID: %u", (uint32_t)obj->id);
        ImGui::Separator();

        // Add Component Button (matching Old Eunoia Engine)
        if (ImGui::Button("+ Add Component", ImVec2(-1.0f, 28.0f))) {
            ImGui::OpenPopup("AddComponentPopup");
        }
        if (ImGui::BeginPopup("AddComponentPopup")) {
            if (ImGui::MenuItem("Light Component")) {
                obj->isLight = true;
                obj->light.type = LightType::Point;
                obj->light.color = glm::vec3(1.0f);
                obj->light.intensity = 10.0f;
                obj->light.range = 10.0f;
                AddLog("LogActor", "Added Light Component to " + obj->name, 2);
            }
            if (ImGui::MenuItem("Camera Component")) {
                obj->isCamera = true;
                AddLog("LogActor", "Added Camera Component to " + obj->name, 2);
            }
            if (ImGui::MenuItem("Material Component")) {
                AddLog("LogActor", "Material Component ready on " + obj->name, 2);
            }
            if (ImGui::MenuItem("RigidBody Physics")) {
                obj->hasPhysics = true;
                AddLog("LogActor", "Added RigidBody Physics to " + obj->name, 2);
            }
            if (ImGui::MenuItem("Collider Component")) {
                obj->AddModifier(CollisionShapeType::Box);
                AddLog("LogActor", "Added Collider Component to " + obj->name, 2);
            }
            if (ImGui::MenuItem("Particle Emitter")) {
                AddLog("LogActor", "Added Particle Emitter to " + obj->name, 2);
            }
            if (ImGui::MenuItem("Script Component")) {
                showAddBehaviourPopup = true;
                AddLog("LogActor", "Attaching Script Component to " + obj->name, 2);
            }
            ImGui::EndPopup();
        }
        ImGui::Separator();

        // Unreal Mobility Selector (Static | Stationary | Movable)
        ImGui::TextDisabled("Mobility");
        ImGui::SameLine();
        int mIdx = (int)obj->mobility;
        const char* mobilities[] = { "Static", "Stationary", "Movable" };
        for (int i = 0; i < 3; ++i) {
            bool active = (mIdx == i);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.50f, 0.90f, 1.0f));
            if (ImGui::Button(mobilities[i])) {
                scene.SetObjectMobility(obj->id, (Mobility)i, true);
                if ((Mobility)i == Mobility::Static && !obj->childIds.empty()) {
                    AddLog("LogActor", "Set " + obj->name + " and all children to Static recursively", 1);
                }
            }
            if (active) ImGui::PopStyleColor();
            if (i < 2) ImGui::SameLine();
        }

        // Terrain Mode Switching Tabs (Properties vs Sculpt/Edit vs Materials)
        if (obj->isTerrain || obj->type == PrimitiveType::Terrain) {
            RefreshCachedMaterials(contentRootPath);
            ImGui::Separator();
            ImGui::Spacing();
            static int s_terrainActiveTab = 0; // 0 = Properties, 1 = Sculpt / Edit, 2 = Materials

            // Sync tab with actor state
            if (obj->terrain.editModeActive) {
                s_terrainActiveTab = 1;
            } else if (obj->terrain.materialPaintModeActive) {
                s_terrainActiveTab = 2;
            } else if (s_terrainActiveTab == 1) {
                obj->terrain.editModeActive = true;
                obj->terrain.materialPaintModeActive = false;
            } else if (s_terrainActiveTab == 2) {
                obj->terrain.materialPaintModeActive = true;
                obj->terrain.editModeActive = false;
            }

            float availWidth = ImGui::GetContentRegionAvail().x;
            float tabBtnWidth = (availWidth - 12.0f) / 3.0f;

            // Tab 0: Properties
            if (s_terrainActiveTab == 0) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.55f, 0.90f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.65f, 0.98f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.20f, 0.24f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.28f, 0.35f, 1.0f));
            }
            if (ImGui::Button("Properties##TerrainPropTab", ImVec2(tabBtnWidth, 30.0f))) {
                s_terrainActiveTab = 0;
                obj->terrain.editModeActive = false;
                obj->terrain.materialPaintModeActive = false;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();

            // Tab 1: Sculpt / Edit
            if (s_terrainActiveTab == 1) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.75f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.85f, 0.45f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.20f, 0.24f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.28f, 0.35f, 1.0f));
            }
            if (ImGui::Button("Sculpt / Edit##TerrainEditTab", ImVec2(tabBtnWidth, 30.0f))) {
                s_terrainActiveTab = 1;
                obj->terrain.editModeActive = true;
                obj->terrain.materialPaintModeActive = false;
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();

            // Tab 2: Materials
            if (s_terrainActiveTab == 2) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.30f, 0.85f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.40f, 0.95f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.20f, 0.24f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.28f, 0.35f, 1.0f));
            }
            if (ImGui::Button("Materials##TerrainMatTab", ImVec2(tabBtnWidth, 30.0f))) {
                s_terrainActiveTab = 2;
                obj->terrain.materialPaintModeActive = true;
                obj->terrain.editModeActive = false;
            }
            ImGui::PopStyleColor(2);

            if (s_terrainActiveTab == 1) {
                obj->terrain.editModeActive = true;
                obj->terrain.materialPaintModeActive = false;
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "ÃƒÂ¢Ã¢â‚¬â€Ã‚Â Terrain Sculpting Active (LMB on Viewport)");
                ImGui::TextDisabled("Hold Shift to invert brush action (e.g. Lower instead of Raise)");

                if (ImGui::CollapsingHeader("Brush Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                    // Brush Types
                    ImGui::TextDisabled("Brush Mode");
                    const char* brushModes[] = { "Raise", "Lower", "Smooth", "Flatten", "Noise" };
                    int curBrush = (int)obj->terrain.brushType;
                    for (int b = 0; b < 5; ++b) {
                        bool bActive = (curBrush == b);
                        if (bActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.65f, 0.38f, 1.0f));
                        if (ImGui::Button(brushModes[b])) {
                            obj->terrain.brushType = (Eunoia::TerrainBrushType)b;
                        }
                        if (bActive) ImGui::PopStyleColor();
                        if (b < 4) ImGui::SameLine();
                    }
                    ImGui::Spacing();

                    // Falloff
                    const char* falloffNames[] = { "Smooth", "Linear", "Spherical", "Sharp" };
                    int curFalloff = (int)obj->terrain.brushFalloff;
                    if (ImGui::Combo("Falloff", &curFalloff, falloffNames, 4)) {
                        obj->terrain.brushFalloff = (Eunoia::TerrainFalloffType)curFalloff;
                    }

                    // Brush Radius & Strength
                    ImGui::SliderFloat("Brush Radius", &obj->terrain.brushSize, 0.5f, 50.0f, "%.1f m");
                    ImGui::SliderFloat("Brush Strength", &obj->terrain.brushStrength, 0.05f, 20.0f, "%.2f");

                    // Flatten Specifics
                    if (obj->terrain.brushType == Eunoia::TerrainBrushType::Flatten) {
                        ImGui::DragFloat("Target Height", &obj->terrain.flattenTargetHeight, 0.1f, -100.0f, 100.0f, "%.2f m");
                        ImGui::SameLine();
                        if (ImGui::Button("Zero##FlattenZero")) {
                            obj->terrain.flattenTargetHeight = 0.0f;
                        }
                    }

                    // Noise Specifics
                    if (obj->terrain.brushType == Eunoia::TerrainBrushType::Noise) {
                        ImGui::SliderFloat("Noise Scale", &obj->terrain.noiseScale, 0.01f, 1.0f, "%.3f");
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    if (ImGui::Button("Reset All Heights to 0", ImVec2(-1.0f, 26.0f))) {
                        UndoManager::Get().RecordSnapshot(scene, "Reset Heights: " + obj->name);
                        std::fill(obj->terrain.heightmap.begin(), obj->terrain.heightmap.end(), 0.0f);
                        obj->terrain.RebuildTerrainMesh(obj->mesh);
                        AddLog("LogTerrain", "Reset terrain heights to 0 on " + obj->name, 0);
                    }
                }
            } else if (s_terrainActiveTab == 2) {
                obj->terrain.materialPaintModeActive = true;
                obj->terrain.editModeActive = false;
                obj->terrain.SyncBaseMaterial(obj->materialName, obj->color, obj->metallic, obj->roughness,
                                              obj->normalStrength, obj->specular, obj->uvScale,
                                              obj->baseColorTexture, obj->normalTexture, obj->roughnessTexture,
                                              obj->metallicTexture, obj->aoTexture);

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.80f, 0.50f, 1.0f, 1.0f), "ÃƒÂ¢Ã¢â‚¬â€Ã‚Â Terrain Material Painting Active (LMB on Viewport)");
                ImGui::TextDisabled("Select a layer to paint. Drag & drop layers to reorder. Slot [0] is Base Material.");

                if (ImGui::CollapsingHeader("Material Layers Array", ImGuiTreeNodeFlags_DefaultOpen)) {
                    for (int i = 0; i < (int)obj->terrain.materialLayers.size(); ++i) {
                        auto& layer = obj->terrain.materialLayers[i];
                        ImGui::PushID(i);

                        bool isActivePaint = (obj->terrain.activePaintMaterialIndex == i);

                        std::string slotPrefix = "[" + std::to_string(i) + "]";
                        if (i == 0) slotPrefix += " (Base)";
                        else slotPrefix += " (Layer " + std::to_string(i) + ")";

                        if (isActivePaint) {
                            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.80f, 0.50f, 1.0f, 1.0f));
                            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                        }

                        ImGui::BeginGroup();

                        // Swatch button
                        ImGui::ColorButton("##layerSwatch", ImVec4(layer.baseColor.r, layer.baseColor.g, layer.baseColor.b, 1.0f),
                            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(22, 22));
                        ImGui::SameLine();

                        // Selectable layer title / paint selector
                        std::string selectTitle = slotPrefix + " " + layer.name;
                        float availRight = (i > 0) ? 55.0f : 0.0f;
                        if (ImGui::Selectable(selectTitle.c_str(), isActivePaint, 0, ImVec2(ImGui::GetContentRegionAvail().x - availRight, 22.0f))) {
                            obj->terrain.activePaintMaterialIndex = i;
                        }

                        // Drag & Drop Source (reordering)
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                            ImGui::SetDragDropPayload("TERRAIN_LAYER_REORDER", &i, sizeof(int));
                            ImGui::Text("Move %s", selectTitle.c_str());
                            ImGui::EndDragDropSource();
                        }

                        // Drag & Drop Target (reordering & asset drop)
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TERRAIN_LAYER_REORDER")) {
                                int srcIdx = *(const int*)payload->Data;
                                if (srcIdx != i && srcIdx >= 0 && srcIdx < (int)obj->terrain.materialLayers.size()) {
                                    UndoManager::Get().RecordSnapshot(scene, "Reorder Terrain Materials");
                                    obj->terrain.SwapMaterialLayers(srcIdx, i);
                                    if (srcIdx == 0 || i == 0) {
                                        // Slot 0 changed, sync to GameObject
                                        obj->materialName = obj->terrain.materialLayers[0].name;
                                        obj->color = obj->terrain.materialLayers[0].baseColor;
                                        obj->metallic = obj->terrain.materialLayers[0].metallic;
                                        obj->roughness = obj->terrain.materialLayers[0].roughness;
                                        obj->normalStrength = obj->terrain.materialLayers[0].normalStrength;
                                        obj->specular = obj->terrain.materialLayers[0].specular;
                                        obj->uvScale = obj->terrain.materialLayers[0].uvScale;
                                        obj->baseColorTexture = obj->terrain.materialLayers[0].baseColorTexture;
                                        obj->normalTexture = obj->terrain.materialLayers[0].normalTexture;
                                        obj->roughnessTexture = obj->terrain.materialLayers[0].roughnessTexture;
                                        obj->metallicTexture = obj->terrain.materialLayers[0].metallicTexture;
                                        obj->aoTexture = obj->terrain.materialLayers[0].aoTexture;
                                    }
                                    obj->terrain.RebuildTerrainMesh(obj->mesh);
                                    RequestViewportRedraw(5);
                                }
                            }
                            // Also accept dropping an ASSET_ID onto this slot to replace it
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                                char assetIdBuf[40] = {};
                                memcpy(assetIdBuf, payload->Data, std::min((size_t)payload->DataSize, sizeof(assetIdBuf) - 1));
                                AssetID droppedId = AssetID::FromString(assetIdBuf);
                                AssetMetadata* meta = AssetRegistry::Get().Find(droppedId);
                                if (meta && meta->type == AssetType::Material) {
                                    std::filesystem::path fullPath = contentRootPath / meta->sourcePath;
                                    MaterialAsset ma;
                                    if (LoadMaterialFile(fullPath.string(), ma)) {
                                        UndoManager::Get().RecordSnapshot(scene, "Replace Terrain Material Layer");
                                        layer.name = ma.name;
                                        layer.path = meta->sourcePath;
                                        layer.baseColor = ma.baseColor;
                                        layer.metallic = ma.metallic;
                                        layer.roughness = ma.roughness;
                                        layer.normalStrength = ma.normalStrength;
                                        layer.specular = ma.specular;
                                        layer.uvScale = ma.uvScale;
                                        layer.baseColorTexture = ma.baseColorTexture;
                                        layer.normalTexture = ma.normalTexture;
                                        layer.roughnessTexture = ma.roughnessTexture;
                                        layer.metallicTexture = ma.metallicTexture;
                                        layer.aoTexture = ma.aoTexture;
                                        if (i == 0) {
                                            ApplyMaterialToActorAndChildren(scene, obj, ma);
                                        }
                                        obj->terrain.RebuildTerrainMesh(obj->mesh);
                                        RequestViewportRedraw(5);
                                    }
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        if (i > 0) {
                            ImGui::SameLine();
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.15f, 0.15f, 0.8f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
                            if (ImGui::Button("X##RemoveLayer", ImVec2(22, 22))) {
                                UndoManager::Get().RecordSnapshot(scene, "Remove Terrain Material Layer");
                                obj->terrain.RemoveMaterialLayer(i);
                                obj->terrain.RebuildTerrainMesh(obj->mesh);
                                RequestViewportRedraw(5);
                                ImGui::PopStyleColor(2);
                                ImGui::EndGroup();
                                if (isActivePaint) {
                                    ImGui::PopStyleVar();
                                    ImGui::PopStyleColor();
                                }
                                ImGui::PopID();
                                break;
                            }
                            ImGui::PopStyleColor(2);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Remove this material layer (painted quads revert to base material)");
                            }
                        }

                        ImGui::EndGroup();

                        if (isActivePaint) {
                            ImGui::PopStyleVar();
                            ImGui::PopStyleColor();
                        }

                        // UV Tiling per layer
                        ImGui::Indent(28.0f);
                        std::string uvId = "UV Scale##MatLayerUV_" + std::to_string(i);
                        if (ImGui::DragFloat2(uvId.c_str(), &layer.uvScale.x, 0.05f, 0.01f, 100.0f, "%.2f")) {
                            if (i == 0) obj->uvScale = layer.uvScale;
                            RequestViewportRedraw(3);
                        }
                        ImGui::Unindent(28.0f);
                        ImGui::Spacing();

                        ImGui::PopID();
                    }

                    ImGui::Spacing();
                    // Add Material Layer Button
                    if (ImGui::Button("+ Add Material Layer...", ImVec2(ImGui::GetContentRegionAvail().x, 28.0f))) {
                        ImGui::OpenPopup("AddMaterialLayerPopup");
                    }

                    if (ImGui::BeginPopup("AddMaterialLayerPopup")) {
                        ImGui::Text("Select Material to Add:");
                        ImGui::Separator();
                        for (const auto& m : s_cachedMatList) {
                            ImGui::ColorButton("##addSwatch", ImVec4(m.color.r, m.color.g, m.color.b, 1.0f),
                                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(16, 16));
                            ImGui::SameLine();
                            if (ImGui::Selectable(m.name.c_str())) {
                                MaterialAsset ma;
                                if (LoadMaterialFile(m.fullPath, ma)) {
                                    UndoManager::Get().RecordSnapshot(scene, "Add Material Layer: " + ma.name);
                                    Eunoia::TerrainMaterialLayer newLayer;
                                    newLayer.name = ma.name;
                                    newLayer.path = m.relPath;
                                    newLayer.baseColor = ma.baseColor;
                                    newLayer.metallic = ma.metallic;
                                    newLayer.roughness = ma.roughness;
                                    newLayer.normalStrength = ma.normalStrength;
                                    newLayer.specular = ma.specular;
                                    newLayer.uvScale = ma.uvScale;
                                    newLayer.baseColorTexture = ma.baseColorTexture;
                                    newLayer.normalTexture = ma.normalTexture;
                                    newLayer.roughnessTexture = ma.roughnessTexture;
                                    newLayer.metallicTexture = ma.metallicTexture;
                                    newLayer.aoTexture = ma.aoTexture;
                                    obj->terrain.materialLayers.push_back(newLayer);
                                    obj->terrain.activePaintMaterialIndex = (int)obj->terrain.materialLayers.size() - 1;
                                    obj->terrain.RebuildTerrainMesh(obj->mesh);
                                    RequestViewportRedraw(5);
                                }
                            }
                        }
                        ImGui::EndPopup();
                    }
                }

                // Section 2: Material Brush Settings
                if (ImGui::CollapsingHeader("Material Brush Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const char* matFalloffNames[] = { "Smooth (Cosine)", "Linear", "Spherical", "Sharp" };
                    int curMatFalloff = (int)obj->terrain.matBrushFalloff;
                    if (ImGui::Combo("Brush Falloff##MatFalloff", &curMatFalloff, matFalloffNames, 4)) {
                        obj->terrain.matBrushFalloff = (Eunoia::TerrainFalloffType)curMatFalloff;
                    }

                    ImGui::SliderFloat("Brush Radius##MatRadius", &obj->terrain.matBrushSize, 0.5f, 50.0f, "%.1f m");
                    ImGui::SliderFloat("Brush Strength (Flow)##MatStrength", &obj->terrain.matBrushStrength, 0.05f, 10.0f, "%.2f");
                    ImGui::TextDisabled("Tip: Hold Shift + LMB to erase active layer back to base material.");

                    ImGui::Spacing();
                    ImGui::Separator();
                    if (ImGui::Button("Fill All with Active Material", ImVec2(-1.0f, 26.0f))) {
                        UndoManager::Get().RecordSnapshot(scene, "Fill Terrain with Material");
                        obj->terrain.FillAllWithMaterial(obj->terrain.activePaintMaterialIndex);
                        obj->terrain.RebuildTerrainMesh(obj->mesh);
                        RequestViewportRedraw(5);
                    }
                }
            } else {
                obj->terrain.editModeActive = false;
                obj->terrain.materialPaintModeActive = false;
            }
        }

        ImGui::Separator();

        // 1. Transform Category (Horizontal Compact: Location, Rotation, Scale)
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool transformChanged = false;

            // Location
            transformChanged |= DrawVec3Row("Location", obj->position, 0.0f, 0.05f);
            ImGui::Spacing();

            // Rotation
            transformChanged |= DrawVec3Row("Rotation", obj->rotation, 0.0f, 1.0f);
            ImGui::Spacing();

            // Scale
            transformChanged |= DrawVec3Row("Scale", obj->scale, 1.0f, 0.02f, true, &lockAspectScale);
            ImGui::Spacing();
            ImGui::Checkbox("Lock Uniform Scale", &lockAspectScale);

                        if (transformChanged && Eunoia::LevelAnimationManager::Get().IsRecording()) {
                Eunoia::LevelAnimationManager::Get().RecordTransform(scene, *obj);
            }
            if (transformChanged && (obj->isLight || IsLightPrimitive(obj->type))) {
                scene.SyncLightPositionsFromActors();
            }
        }

        // 2. Tags Section (Supported for all objects, actors, and empty actors)
        char tagsHeader[64];
        snprintf(tagsHeader, sizeof(tagsHeader), "Tags (%d)###TagsCategory", (int)obj->tags.size());
        if (ImGui::CollapsingHeader(tagsHeader, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("TagsSection");

            static char s_newTagBuf[128] = "";
            bool addRequested = false;

            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f);
            if (ImGui::InputTextWithHint("##NewTagInput", "Enter tag name...", s_newTagBuf, sizeof(s_newTagBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                addRequested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("+ Add", ImVec2(62.0f, 0.0f))) {
                addRequested = true;
            }

            if (addRequested) {
                std::string newTagStr = s_newTagBuf;
                size_t b = newTagStr.find_first_not_of(" \t\r\n");
                size_t e = newTagStr.find_last_not_of(" \t\r\n");
                if (b != std::string::npos && e != std::string::npos) {
                    newTagStr = newTagStr.substr(b, e - b + 1);
                    obj->AddTag(newTagStr);
                    s_newTagBuf[0] = '\0';
                }
            }

            if (obj->tags.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("No tags assigned to this actor.");
                ImGui::Spacing();
            } else {
                ImGui::Spacing();
                int tagToRemove = -1;
                for (size_t ti = 0; ti < obj->tags.size(); ++ti) {
                    ImGui::PushID((int)ti);

                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "[%d]", (int)ti);
                    ImGui::SameLine();

                    char tagBuf[128];
                    strncpy(tagBuf, obj->tags[ti].c_str(), sizeof(tagBuf));
                    tagBuf[sizeof(tagBuf) - 1] = '\0';
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 32.0f);
                    if (ImGui::InputText("##TagEdit", tagBuf, sizeof(tagBuf))) {
                        obj->tags[ti] = tagBuf;
                    }
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 0.8f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                    if (ImGui::Button("X##RemoveTag", ImVec2(24.0f, 0.0f))) {
                        tagToRemove = (int)ti;
                    }
                    ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Remove this tag");
                    }

                    ImGui::PopID();
                }

                if (tagToRemove >= 0 && tagToRemove < (int)obj->tags.size()) {
                    obj->tags.erase(obj->tags.begin() + tagToRemove);
                }

                ImGui::Spacing();
                if (ImGui::Button("Clear All Tags", ImVec2(ImGui::GetContentRegionAvail().x, 22.0f))) {
                    obj->RemoveAllTag();
                }
            }

            ImGui::PopID();
        }

        // Light Component Category (dev.md)
        if (obj->isLight || IsLightPrimitive(obj->type)) {
            char lightHeader[128];
            snprintf(lightHeader, sizeof(lightHeader), "%s Component", GetLightTypeName(obj->light.type));
            if (ImGui::CollapsingHeader(lightHeader, ImGuiTreeNodeFlags_DefaultOpen)) {
                uint64_t iconHandle = GetLightIconGpuHandle(obj->light.type);
                if (iconHandle != 0) {
                    ImGui::Image((ImTextureID)iconHandle, ImVec2(20, 20));
                    ImGui::SameLine();
                }
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "[%s]", GetLightTypeName(obj->light.type));
                ImGui::SameLine();
                ImGui::Checkbox("Light Enabled", &obj->light.enabled);

                ImGui::Separator();
                ImGui::TextDisabled("Light Settings");

                if (ImGui::ColorEdit3("Color##LightComponentColor", &obj->light.color.r,
                                      ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB)) {
                    obj->color = obj->light.color;
                    if (obj->lightId >= 0 && obj->lightId < (int)scene.pointLights.size()) {
                        scene.pointLights[obj->lightId].color = obj->light.color;
                    }
                    if (obj->light.type == LightType::Directional) {
                        scene.lightColor = obj->light.color;
                    } else if (obj->light.type == LightType::Sky) {
                        scene.ambientColor = obj->light.color;
                    }
                }

                ImGui::DragFloat("Intensity##LightIntensity", &obj->light.intensity, 1.0f, 0.0f, 100000000.0f, "%.2f");

                ImGui::Checkbox("Use Temperature (Kelvin)##UseTemp", &obj->light.useTemperature);
                if (obj->light.useTemperature) {
                    ImGui::SliderFloat("Temperature##LightTemp", &obj->light.temperature, 1000.0f, 12000.0f, "%.0f K");
                    glm::vec3 kelvinCol = ColorTemperatureToRGB(obj->light.temperature);
                    ImGui::SameLine();
                    ImGui::ColorButton("##KelvinPreview", ImVec4(kelvinCol.r, kelvinCol.g, kelvinCol.b, 1.0f),
                                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(20, 20));
                    ImGui::TextDisabled("Combined Tint: R:%.2f G:%.2f B:%.2f",
                                        kelvinCol.r * obj->light.color.r,
                                        kelvinCol.g * obj->light.color.g,
                                        kelvinCol.b * obj->light.color.b);
                }

                if (obj->light.type != LightType::Directional && obj->light.type != LightType::Sky) {
                    ImGui::DragFloat("Range / Radius", &obj->light.range, 1.0f, 0.1f, 10000000.0f, "%.1f m");
                    ImGui::DragFloat("Attenuation Exp", &obj->light.attenuation, 0.05f, 0.0f, 1000.0f, "%.2f");
                }

                if (obj->light.type == LightType::Sky) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Sky Light / Ambient System");
                    ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Sky Light provides omnidirectional ambient scene illumination.");
                    ImGui::TextDisabled("Note: Sky light produces pure ambient light and does not cast shadows.");
                }

                if (obj->light.type == LightType::Spot) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Spot Cone & Falloff");
                    ImGui::SliderFloat("Inner Cone Angle", &obj->light.innerConeAngle, 0.0f, 89.0f, "%.1f deg");
                    if (obj->light.outerConeAngle < obj->light.innerConeAngle) obj->light.outerConeAngle = obj->light.innerConeAngle;
                    ImGui::SliderFloat("Outer Cone Angle", &obj->light.outerConeAngle, obj->light.innerConeAngle, 89.9f, "%.1f deg");
                    ImGui::DragFloat("Cone Falloff", &obj->light.coneFalloff, 0.05f, 0.1f, 100.0f, "%.2f");
                    ImGui::SliderFloat("Range Smoothing", &obj->light.rangeSmoothing, 0.01f, 1.0f, "%.2f");
                }

                if (obj->light.type == LightType::Point) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Punctual / Capsule Light");
                    ImGui::DragFloat("Source Radius", &obj->light.radius, 0.005f, 0.001f, 100.0f, "%.3f m");
                    ImGui::DragFloat("Capsule Length", &obj->light.length, 0.05f, 0.0f, 1000.0f, "%.2f m");
                }

                if (obj->light.type == LightType::Rectangle || obj->light.type == LightType::Area) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Rectangle / Area Light");
                    const char* areaShapes[] = { "Rectangle", "Disk" };
                    ImGui::Combo("Shape", &obj->light.areaShape, areaShapes, 2);
                    if (obj->light.areaShape == 0) {
                        float w = obj->light.length > 0.001f ? obj->light.length : obj->light.width;
                        if (ImGui::DragFloat("Width", &w, 0.1f, 0.01f, 1000.0f, "%.2f m")) {
                            obj->light.length = w;
                            obj->light.width = w;
                        }
                        ImGui::DragFloat("Height", &obj->light.height, 0.1f, 0.01f, 1000.0f, "%.2f m");
                    } else {
                        ImGui::DragFloat("Radius", &obj->light.radius, 0.1f, 0.01f, 1000.0f, "%.2f m");
                    }
                    ImGui::Checkbox("Two Sided", &obj->light.twoSided);
                }

                if (obj->light.type == LightType::Directional || obj->light.type == LightType::Point ||
                    obj->light.type == LightType::Spot || obj->light.type == LightType::Rectangle || obj->light.type == LightType::Area) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Shadows (Wicked Engine Technique)");
                    bool castShadow = obj->light.IsCastingShadow();
                    if (ImGui::Checkbox("Cast Shadows", &castShadow)) {
                        obj->light.SetCastShadow(castShadow);
                    }
                    if (castShadow) {
                        ImGui::SliderFloat("Shadow Strength", &obj->light.shadowStrength, 0.0f, 1.0f);
                        ImGui::DragFloat("Shadow Bias", &obj->light.shadowBias, 0.0001f, 0.000001f, 1.0f, "%.6f");

                        const char* resOptions[] = { "Auto (-1)", "256", "512", "1024", "2048", "4096" };
                        int resVals[] = { -1, 256, 512, 1024, 2048, 4096 };
                        int curResIdx = 0;
                        for (int r = 0; r < 6; ++r) {
                            if (obj->light.forced_shadow_resolution == resVals[r]) { curResIdx = r; break; }
                        }
                        if (ImGui::Combo("Forced Resolution", &curResIdx, resOptions, 6)) {
                            obj->light.forced_shadow_resolution = resVals[curResIdx];
                            if (obj->light.forced_shadow_resolution > 0) {
                                obj->light.shadowResolution = obj->light.forced_shadow_resolution;
                            }
                        }

                        ImGui::DragFloat("Shadow Distance", &obj->light.shadowDistance, 1.0f, 1.0f, 1000000.0f, "%.1f m");
                        if (obj->light.type == LightType::Directional) {
                            ImGui::Checkbox("Show Light Frustum", &scene.showLightFrustum);
                            if (obj->light.cascade_distances.size() >= 3) {
                                ImGui::DragFloat("Cascade 1 (Near)", &obj->light.cascade_distances[0], 0.5f, 1.0f, 100.0f, "%.1f m");
                                ImGui::DragFloat("Cascade 2 (Mid)", &obj->light.cascade_distances[1], 1.0f, 10.0f, 500.0f, "%.1f m");
                                ImGui::DragFloat("Cascade 3 (Far)", &obj->light.cascade_distances[2], 5.0f, 50.0f, 5000.0f, "%.1f m");
                            }
                        }
                    }
                }

                ImGui::Separator();
                if (!scene.enableFog) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.70f, 0.25f, 1.0f));
                    ImGui::Text("Volumetric Fog & God Rays [Level Fog Disabled]");
                    ImGui::PopStyleColor();
                    ImGui::TextDisabled("Volumetric fog is disabled for this level.");
                    ImGui::TextDisabled("Turn on Volumetric Fog to activate God Rays.");
                    if (ImGui::Button("Enable Volumetric Fog in Level")) {
                        scene.enableFog = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Enables Volumetric Height Fog for the level (same as World -> Volumetric Fog).");
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Volumetric Fog & God Rays (Wicked Engine Technique)");
                    bool volEnabled = obj->light.IsVolumetricsEnabled();
                    if (ImGui::Checkbox("Enable Volumetric God Rays", &volEnabled)) {
                        obj->light.SetVolumetricsEnabled(volEnabled);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Enables raymarched volumetric scattering and god rays through level fog.");
                    }
                    if (volEnabled) {
                        ImGui::SliderFloat("Volumetric Fog Boost", &obj->light.volumetric_boost, 0.0f, 20.0f, "%.2f");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Adjusts fog penetration and brightness of rays just for this light.");
                        }
                        ImGui::SliderFloat("Scattering Anisotropy (Phase G)", &obj->light.volumetricScattering, 0.0f, 0.95f, "%.2f");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Mie Henyey-Greenstein phase g factor (0.66 = Wicked Engine default forward-scattering).");
                        }
                        ImGui::DragFloat("God Rays Intensity", &obj->light.volumetricIntensity, 0.05f, 0.0f, 20.0f, "%.2f");
                        
                        // Quality sample steps
                        const char* qualityLabels[] = { "Low (8 steps)", "Balanced (16 steps)", "High (24 steps)", "Cinematic (32 steps)" };
                        int qualityValues[] = { 8, 16, 24, 32 };
                        int currentQualityIdx = 1; // default 16
                        for (int q = 0; q < 4; ++q) {
                            if (obj->light.volumetricSteps == qualityValues[q]) { currentQualityIdx = q; break; }
                        }
                        if (ImGui::Combo("Sample Quality", &currentQualityIdx, qualityLabels, 4)) {
                            obj->light.volumetricSteps = qualityValues[currentQualityIdx];
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Number of raymarching samples along the view ray. 16 steps with dither provides excellent performance and quality.");
                        }

                        ImGui::DragFloat("Max Ray Distance", &obj->light.volumetricMaxDistance, 1.0f, 10.0f, 1000.0f, "%.1f m");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Maximum distance along the view ray to calculate god ray volumetric inscattering.");
                        }

                        ImGui::TextDisabled("Status: Active with Level Fog (Density: %.4f, Height: %.0f-%.0fm)",
                            scene.fogDensity, scene.fogHeightStart, scene.fogHeightEnd);
                    }
                }

                ImGui::Separator();
                ImGui::TextDisabled("Optimization & Channels");
                int layer = (int)obj->light.lightLayer;
                if (ImGui::InputInt("Light Layer / Channel", &layer)) {
                    obj->light.lightLayer = (uint32_t)std::max(0, layer);
                }

                ImGui::Separator();
                ImGui::TextDisabled("Performance");
                ImGui::Checkbox("Enable Distance Culling", &obj->light.enableDistanceCulling);
                if (obj->light.enableDistanceCulling) {
                    ImGui::DragFloat("Min Visible Distance", &obj->light.minVisibleDistance, 1.0f, 0.0f, 10000.0f, "%.1f m");
                    if (obj->light.maxVisibleDistance < obj->light.minVisibleDistance) {
                        obj->light.maxVisibleDistance = obj->light.minVisibleDistance;
                    }
                    ImGui::DragFloat("Max View Distance", &obj->light.maxVisibleDistance, 1.0f, obj->light.minVisibleDistance, 10000.0f, "%.1f m");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Smoothly fades light to 0 between min and max visible distance.\nBeyond max visible distance, the light is completely culled and not rendered.");
                    }
                }
            }

            if (obj->lightId >= 0 && obj->lightId < (int)scene.pointLights.size()) {
                auto& pl = scene.pointLights[obj->lightId];
                pl.type = obj->light.type;
                pl.color = obj->light.color;
                pl.intensity = obj->light.intensity;
                pl.range = obj->light.range;
                pl.attenuation = obj->light.attenuation;
                pl.rangeSmoothing = obj->light.rangeSmoothing;
                pl.enabled = obj->light.enabled;
                pl.castShadows = obj->light.castShadows;
                pl.shadowStrength = obj->light.shadowStrength;
                pl.shadowBias = obj->light.shadowBias;
                pl.shadowResolution = obj->light.shadowResolution;
                pl.shadowDistance = obj->light.shadowDistance;
                pl.enableDistanceCulling = obj->light.enableDistanceCulling;
                pl.minVisibleDistance = obj->light.minVisibleDistance;
                pl.maxVisibleDistance = obj->light.maxVisibleDistance;
                pl.innerConeAngle = obj->light.innerConeAngle;
                pl.outerConeAngle = obj->light.outerConeAngle;
                pl.coneFalloff = obj->light.coneFalloff;
                pl.areaShape = obj->light.areaShape;
                pl.width = obj->light.width;
                pl.height = obj->light.height;
                pl.radius = obj->light.radius;
                pl.length = obj->light.length;
                pl.twoSided = obj->light.twoSided;
                pl.volumetric = obj->light.IsVolumetricsEnabled();
                pl.volumetric_boost = obj->light.volumetric_boost;
                pl.volumetricScattering = obj->light.volumetricScattering;
                pl.volumetricIntensity = obj->light.volumetricIntensity;
                pl.volumetricSteps = obj->light.volumetricSteps;
                pl.volumetricMaxDistance = obj->light.volumetricMaxDistance;
                pl.forced_shadow_resolution = obj->light.forced_shadow_resolution;
            }
            if (obj->light.type == LightType::Directional) {
                scene.lightColor = obj->light.color;
                scene.lightIntensity = obj->light.intensity;
                scene.enableShadows = obj->light.castShadows;
                scene.shadowStrength = obj->light.shadowStrength;
                scene.shadowBias = obj->light.shadowBias;
                scene.shadowResolution = obj->light.shadowResolution;
                scene.shadowDistance = obj->light.shadowDistance;
                scene.directionalVolumetric = obj->light.IsVolumetricsEnabled();
                scene.directionalVolumetricBoost = obj->light.volumetric_boost;
                scene.directionalVolumetricScattering = obj->light.volumetricScattering;
                scene.directionalVolumetricIntensity = obj->light.volumetricIntensity;
                scene.directionalVolumetricSteps = obj->light.volumetricSteps;
                scene.directionalVolumetricMaxDistance = obj->light.volumetricMaxDistance;
            } else if (obj->light.type == LightType::Sky) {
                scene.ambientColor = obj->light.color;
                scene.ambientIntensity = obj->light.intensity;
            }
        }

        // Camera Component Category
        if (obj->isCamera || obj->type == PrimitiveType::Camera) {
            if (ImGui::CollapsingHeader("Camera Component", ImGuiTreeNodeFlags_DefaultOpen)) {
                // Projection Mode
                const char* projModes[] = { "Perspective", "Orthographic" };
                int currentMode = obj->camera.isOrthographic ? 1 : 0;
                if (ImGui::Combo("Projection", &currentMode, projModes, 2)) {
                    obj->camera.isOrthographic = (currentMode == 1);
                }

                if (!obj->camera.isOrthographic) {
                    ImGui::SliderFloat("Field of View (FOV)", &obj->camera.fov, 10.0f, 150.0f, "%.1f deg");
                } else {
                    ImGui::DragFloat("Orthographic Size", &obj->camera.orthoSize, 0.1f, 0.1f, 100.0f, "%.2f");
                }

                ImGui::Separator();
                ImGui::TextDisabled("Clipping Planes");
                ImGui::DragFloat("Near Plane", &obj->camera.nearPlane, 0.01f, 0.001f, 50.0f, "%.3f m");
                ImGui::DragFloat("Far Plane", &obj->camera.farPlane, 1.0f, 1.0f, 50000.0f, "%.1f m");
                if (obj->camera.nearPlane < 0.001f) obj->camera.nearPlane = 0.001f;
                if (obj->camera.farPlane <= obj->camera.nearPlane) obj->camera.farPlane = obj->camera.nearPlane + 1.0f;

                ImGui::Separator();
                ImGui::TextDisabled("Aspect Ratio");
                const char* aspectPresets[] = { "16:9 (1.778)", "16:10 (1.600)", "4:3 (1.333)", "21:9 (2.333)", "1:1 (1.000)", "Custom" };
                int curAspectPreset = 5;
                if (std::abs(obj->camera.aspectRatio - 16.0f/9.0f) < 0.01f) curAspectPreset = 0;
                else if (std::abs(obj->camera.aspectRatio - 16.0f/10.0f) < 0.01f) curAspectPreset = 1;
                else if (std::abs(obj->camera.aspectRatio - 4.0f/3.0f) < 0.01f) curAspectPreset = 2;
                else if (std::abs(obj->camera.aspectRatio - 21.0f/9.0f) < 0.01f) curAspectPreset = 3;
                else if (std::abs(obj->camera.aspectRatio - 1.0f) < 0.01f) curAspectPreset = 4;

                if (ImGui::Combo("Preset##CamAspect", &curAspectPreset, aspectPresets, 6)) {
                    if (curAspectPreset == 0) obj->camera.aspectRatio = 16.0f / 9.0f;
                    else if (curAspectPreset == 1) obj->camera.aspectRatio = 16.0f / 10.0f;
                    else if (curAspectPreset == 2) obj->camera.aspectRatio = 4.0f / 3.0f;
                    else if (curAspectPreset == 3) obj->camera.aspectRatio = 21.0f / 9.0f;
                    else if (curAspectPreset == 4) obj->camera.aspectRatio = 1.0f;
                }
                ImGui::DragFloat("Ratio Value##CamRatio", &obj->camera.aspectRatio, 0.01f, 0.2f, 5.0f, "%.3f");

                ImGui::Separator();
                ImGui::TextDisabled("Level Camera");
                bool isLevelCam = (scene.activeLevelCameraId == obj->id);
                if (isLevelCam) {
                    ImGui::TextColored(ImVec4(0.2f, 0.95f, 0.4f, 1.0f), "[ACTIVE LEVEL CAMERA]");
                    if (ImGui::Button("Clear as Level Camera", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                        scene.activeLevelCameraId = -1;
                        AddLog("LogCamera", "Cleared active level camera", 0);
                    }
                } else {
                    ImGui::TextDisabled("Status: Not set as level camera");
                    if (ImGui::Button("Set as Active Level Camera", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                        scene.activeLevelCameraId = obj->id;
                        AddLog("LogCamera", "Set " + obj->name + " as Active Level Camera", 0);
                    }
                }

                ImGui::Separator();
                ImGui::TextDisabled("Camera Pilot & Alignment");
                if (currentCamera) {
                    if (ImGui::Button("Align View to Camera", ImVec2(ImGui::GetContentRegionAvail().x * 0.49f, 26.0f))) {
                        currentCamera->yaw = obj->rotation.y;
                        currentCamera->pitch = -obj->rotation.x;
                        currentCamera->fov = obj->camera.fov;
                        currentCamera->distance = 1.0f;
                        currentCamera->target = scene.GetWorldPosition(*obj) + currentCamera->GetForward() * 1.0f;
                        AddLog("LogCamera", "Aligned Viewport to " + obj->name, 0);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Align Camera to View", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                        obj->position = currentCamera->GetPosition();
                        obj->rotation.y = currentCamera->yaw;
                        obj->rotation.x = -currentCamera->pitch;
                        obj->camera.fov = currentCamera->fov;
                        AddLog("LogCamera", "Aligned " + obj->name + " to current viewport camera", 0);
                    }
                }
            }
        }

        // =========================================================
        // WATER BODY COMPONENT (shown when a Water Body is selected)
        // =========================================================
        if (obj->isWaterBody || obj->type == PrimitiveType::WaterBody) {
            WaterBodyComponent& w = obj->water;

            // Water type selector
            if (ImGui::CollapsingHeader("Water Body", ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* waterTypeNames[] = {
                    "Ocean", "River", "Sea", "Lake", "Stream",
                    "Waterfall", "Pond", "Pool", "Wet Surface",
                    "Puddle", "Flood Water", "Underwater", "Swamp", "Custom"
                };
                int wt = (int)w.type;
                if (ImGui::Combo("Water Type", &wt, waterTypeNames, 14)) {
                    // Re-apply preset for new type (Ãƒâ€šÃ‚Â§30 type switching)
                    Scene::ApplyWaterBodyPreset(w, (WaterBodyType)wt);
                    // Rebuild mesh with new preset dimensions
                    obj->mesh = GeometryBuilder::CreatePlane(w.width, w.length, w.gridResolution, w.gridResolution);
                    obj->position.y = w.elevation;
                    AddLog("LogActor", std::string("Switched water type to: ") + waterTypeNames[wt], 2);
                }
                ImGui::Separator();

                // Geometry
                ImGui::TextDisabled("Geometry");
                bool geomChanged = false;
                geomChanged |= ImGui::DragFloat("Width##WaterW",  &w.width,  0.5f, 0.1f, 10000.0f, "%.1f m");
                geomChanged |= ImGui::DragFloat("Length##WaterL", &w.length, 0.5f, 0.1f, 10000.0f, "%.1f m");
                geomChanged |= ImGui::DragFloat("Elevation##WaterY", &w.elevation, 0.05f, -500.0f, 500.0f, "%.2f m");
                geomChanged |= ImGui::SliderInt("Grid Resolution##WaterGrid", &w.gridResolution, 4, 256);
                if (geomChanged) {
                    // Rebuild plane mesh with new dimensions
                    obj->mesh = GeometryBuilder::CreatePlane(w.width, w.length, w.gridResolution, w.gridResolution);
                    obj->position.y = w.elevation;
                }
            }

            // Ocean Waves (dev.md Section 29)
            if (ImGui::CollapsingHeader("Ocean Waves", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("Wave System: 8 Multi-Scale Gerstner Waves");
                ImGui::SliderFloat("Wave Strength (Height)##WaterH", &w.waveHeight, 0.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Wave Scale (Wavelength)##WaterScale", &w.waveScale, 0.05f, 5.0f, "%.2f");
                ImGui::SliderFloat("Wave Speed##WaterSpd", &w.waveSpeed, 0.0f, 5.0f, "%.2f");
                ImGui::SliderFloat("Choppiness (Steepness)##WaterChop", &w.choppiness, 0.0f, 1.5f, "%.2f");
                ImGui::SliderFloat("Normal Detail Scale##WaterNorm", &w.normalScale, 0.0f, 2.0f, "%.2f");
            }

            // Surface Appearance (dev.md Section 29)
            if (ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat("Water Level (Elevation)##WaterLvl", &w.elevation, 0.05f, -500.0f, 500.0f, "%.2f m");
                ImGui::ColorEdit4("Deep Water Color",    &w.waterColor.r);
                ImGui::ColorEdit4("Shallow Water Color", &w.shallowColor.r);
                ImGui::SliderFloat("Roughness##WaterRough",  &w.roughness,  0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Specular Intensity##WaterSpec", &w.specularIntensity,  0.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Specular Power##WaterSpecPow",  &w.specularPower, 8.0f, 512.0f, "%.0f");
                ImGui::SliderFloat("Sun Sparkle Power##WaterSpkPow",&w.sparklePower, 64.0f, 2048.0f, "%.0f");
                ImGui::SliderFloat("Fresnel Strength##WaterFres",   &w.fresnelStrength, 0.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Opacity##WaterOpac",     &w.opacity,    0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Depth Range (Fade)##WaterDepthFade", &w.depthRange, 0.1f, 50.0f, "%.1f m");
                ImGui::SliderFloat("Reflection Strength##WaterRefl", &w.reflectionStrength, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Refraction Strength##WaterRefr", &w.refractionStrength, 0.0f, 2.0f, "%.2f");
            }

            // GPU Height-Field Simulation (AquaTerrain DX12, dev.md Section 29)
            if (ImGui::CollapsingHeader("Simulation (Local Ripples)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Enable Simulation##WaterSim", &w.simulationEnabled);
                ImGui::TextDisabled("Simulation Resolution: 256 x 256 (Compute)");
                ImGui::SliderFloat("Ripple Strength (Height Scale)##WaterRipStr", &w.heightScale, 0.0f, 0.5f, "%.3f");
                ImGui::SliderFloat("Ripple Radius / Disturbance##WaterDistStr", &w.disturbanceStrength, 0.05f, 5.0f, "%.2f");
                ImGui::SliderFloat("Wave Damping##WaterDamp", &w.waveDamping, 0.900f, 0.999f, "%.3f");

                if (ImGui::Button("Add Impulse at Center", ImVec2(-1, 0))) {
                    Eunoia::WaterSystem::Get().AddImpulseUV(0.5f, 0.5f, 0.08f, w.disturbanceStrength);
                }
                if (ImGui::Button("Add Random Ripple", ImVec2(-1, 0))) {
                    float ru = 0.25f + (float)(rand() % 50) / 100.0f;
                    float rv = 0.25f + (float)(rand() % 50) / 100.0f;
                    Eunoia::WaterSystem::Get().AddImpulseUV(ru, rv, 0.06f, w.disturbanceStrength * 0.8f);
                }
            }

            // Foam (dev.md Section 29)
            if (ImGui::CollapsingHeader("Foam")) {
                ImGui::Checkbox("Enable Foam##WaterFoam", &w.enableFoam);
                if (w.enableFoam) {
                    ImGui::SliderFloat("Foam Strength##WaterFoamStr",  &w.foamStrength, 0.0f, 3.0f, "%.2f");
                    ImGui::SliderFloat("Foam Distance##WaterFoamDist", &w.foamDistance, 0.1f, 10.0f, "%.1f m");
                    ImGui::SliderFloat("Foam Scale##WaterFoamScale",   &w.foamScale,   0.01f, 10.0f, "%.2f");
                    ImGui::SliderFloat("Foam Speed##WaterFoamSpd",     &w.foamSpeed,    0.0f, 5.0f, "%.2f");
                    ImGui::ColorEdit4("Foam Color",    &w.foamColor.r);
                    auto textures = ScanProjectTextures();
                    DrawTextureSlot("Foam Texture (Optional)", w.foamTexturePath, w.foamTextureAssetId, textures);
                }
            }

            // Rendering & Debug (dev.md Section 29 & 35)
            if (ImGui::CollapsingHeader("Rendering & Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Visible##WaterVis", &obj->visible);
                ImGui::Checkbox("Camera Centered Ocean##WaterCamCent", &w.isCameraCentered);
                ImGui::DragFloat("Ocean Extent##WaterExtent", &w.oceanExtent, 50.0f, 100.0f, 20000.0f, "%.0f m");
                ImGui::TextDisabled("Mesh Resolution: 256 x 256 (66k Vertices)");

                const char* debugModes[] = {
                    "0: Final Shading",
                    "1: Base Color",
                    "2: Normals",
                    "3: Height Field",
                    "4: Foam Mask",
                    "5: Depth / Thickness",
                    "6: Wireframe / Grid Topology"
                };
                ImGui::Combo("Water Debug Mode##WaterDbg", &w.debugMode, debugModes, 7);

                ImGui::Checkbox("Cast Shadows##WaterShadow", &obj->castShadows);
                ImGui::Checkbox("Receive Shadows##WaterRecv",&obj->receiveShadows);
                ImGui::SliderInt("Render Layer##WaterLyr", &w.renderLayer, 0, 7);
            }

            // Flow (River, Stream, Waterfall)
            bool isFlowing = (w.type == WaterBodyType::River || w.type == WaterBodyType::Stream || w.type == WaterBodyType::Waterfall);
            if (isFlowing && ImGui::CollapsingHeader("ÃƒÂ¢Ã…Â¾Ã‚Â¡ÃƒÂ¯Ã‚Â¸Ã‚Â Flow")) {
                ImGui::Checkbox("Enable Flow", &w.enableFlow);
                if (w.enableFlow) {
                    ImGui::DragFloat2("Flow Direction (XZ)", &w.flowDirection.x, 0.01f, -1.0f, 1.0f);
                    ImGui::SliderFloat("Flow Speed",   &w.flowSpeed,   0.0f, 5.0f,  "%.2f");
                    ImGui::SliderFloat("Flow Tiling",  &w.flowTiling,  0.01f, 10.0f, "%.2f");
                    auto textures = ScanProjectTextures();
                    DrawTextureSlot("Flow Map (Optional)", w.flowMapPath, w.flowMapAssetId, textures);
                }
            }

            // Waterfall specific
            if (w.type == WaterBodyType::Waterfall && ImGui::CollapsingHeader("Waterfall Settings")) {
                ImGui::DragFloat("Fall Height",   &w.waterfallFallHeight,  0.5f, 0.0f, 50.0f,  "%.1f m");
                ImGui::SliderFloat("Flow Rate",   &w.waterfallFlowRate,    0.0f, 10.0f, "%.2f");
                ImGui::SliderFloat("Spray Amount",&w.waterfallSprayAmount, 0.0f, 2.0f,  "%.2f");
            }

            // Underwater specific
            if (w.type == WaterBodyType::Underwater && ImGui::CollapsingHeader("Underwater Volume")) {
                ImGui::ColorEdit3("Fog Color",         &w.underwaterFogColor.r);
                ImGui::SliderFloat("Fog Density",      &w.underwaterFogDensity,        0.0f, 5.0f, "%.2f");
                ImGui::DragFloat("Visibility Distance",&w.underwaterVisibilityDistance, 1.0f, 0.0f, 200.0f, "%.1f m");
                ImGui::Checkbox("Underwater Distortion", &w.enableUnderwaterDistortion);
                if (w.enableUnderwaterDistortion) {
                    ImGui::SliderFloat("Distortion Strength##Uw", &w.underwaterDistortionStrength, 0.0f, 2.0f, "%.2f");
                    ImGui::SliderFloat("Distortion Speed##Uw",    &w.underwaterDistortionSpeed,    0.0f, 5.0f, "%.2f");
                }
            }

            // Puddle / Wet Surface specific
            bool isPuddle = (w.type == WaterBodyType::Puddle || w.type == WaterBodyType::WetSurface);
            if (isPuddle && ImGui::CollapsingHeader("Puddle / Wet Surface")) {
                ImGui::SliderFloat("Wetness",    &w.wetness,    0.0f, 1.0f,  "%.2f");
                ImGui::DragFloat("Puddle Size",  &w.puddleSize, 0.1f, 0.0f, 20.0f, "%.1f m");
                ImGui::DragFloat("Puddle Depth", &w.puddleDepth,0.01f,0.0f,  2.0f, "%.2f m");
                ImGui::Checkbox("Animate Ripples", &w.animatePuddle);
                if (w.animatePuddle) {
                    ImGui::SliderFloat("Ripple Speed", &w.rippleSpeed, 0.0f, 5.0f, "%.2f");
                    ImGui::SliderFloat("Ripple Scale", &w.rippleScale, 0.0f, 5.0f, "%.2f");
                }
            }

            // Swamp specific
            if (w.type == WaterBodyType::Swamp && ImGui::CollapsingHeader("Swamp Settings")) {
                ImGui::SliderFloat("Density (Murk)",  &w.swampDensity, 0.0f, 1.0f, "%.2f");
                ImGui::ColorEdit3("Swamp Color",      &w.swampColor.r);
                ImGui::SliderFloat("Algae Amount",    &w.algaeAmount,  0.0f, 1.0f, "%.2f");
            }
        }

        // =========================================================
        // USER INTERFACE 3D COMPONENT
        // =========================================================
        if (obj->isUserInterface || obj->type == PrimitiveType::UserInterface || !obj->ui3d.uiAssetPath.empty()) {
            UserInterface3DComponent& uiComp = obj->ui3d;

            if (ImGui::CollapsingHeader("User Interface 3D", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("UI Enabled", &uiComp.enabled);
                ImGui::Spacing();

                // 1. Project UI Asset Selector dropdown
                ImGui::Text("User Interface Asset");
                std::string currentPreview = "None (Select 3D User Interface)";
                if (!uiComp.uiAssetPath.empty()) {
                    currentPreview = std::filesystem::path(uiComp.uiAssetPath).filename().string();
                }

                if (ImGui::BeginCombo("##UI3DAssetCombo", currentPreview.c_str())) {
                    if (ImGui::Selectable("None", uiComp.uiAssetPath.empty())) {
                        UndoManager::Get().RecordSnapshot(scene, "Clear UI Asset");
                        uiComp.uiAssetPath = "";
                    }
                    ImGui::Separator();

                    // Scan project content directory for all .eui3d and .eui files
                    if (!contentRootPath.empty() && std::filesystem::exists(contentRootPath)) {
                        std::error_code rec;
                        for (auto it = std::filesystem::recursive_directory_iterator(contentRootPath, std::filesystem::directory_options::skip_permission_denied, rec);
                             it != std::filesystem::recursive_directory_iterator(); it.increment(rec)) {
                            if (rec) break;
                            if (it->is_regular_file()) {
                                std::string ext = it->path().extension().string();
                                if (ext == ".eui3d" || ext == ".eui") {
                                    std::string uiPath = it->path().string();
                                    std::string uiFilename = it->path().filename().string();
                                    bool isSel = (!uiComp.uiAssetPath.empty() && (uiComp.uiAssetPath == uiPath || std::filesystem::path(uiComp.uiAssetPath).filename() == it->path().filename()));
                                    if (ImGui::Selectable(uiFilename.c_str(), isSel)) {
                                        UndoManager::Get().RecordSnapshot(scene, "Select UI Asset: " + uiFilename);
                                        uiComp.uiAssetPath = uiPath;
                                    }
                                }
                            }
                        }
                    }
                    ImGui::EndCombo();
                }

                // Drag & drop target for UI assets
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        std::string dragItem = (const char*)payload->Data;
                        if (dragItem.rfind("ASSET:", 0) == 0) {
                            std::string srcRel = dragItem.substr(6);
                            if (srcRel.find(".eui3d") != std::string::npos || srcRel.find(".eui") != std::string::npos) {
                                std::filesystem::path fullDisk = contentRootPath / srcRel;
                                UndoManager::Get().RecordSnapshot(scene, "Drop UI Asset");
                                uiComp.uiAssetPath = fullDisk.string();
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (!uiComp.uiAssetPath.empty()) {
                    ImGui::SameLine();
                    if (ImGui::Button("Edit UI")) {
                        OpenUIEditor(uiComp.uiAssetPath);
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();

                // 2. Space dropdown (World vs Screen)
                ImGui::Text("Space");
                const char* spaceNames[] = { "World", "Screen" };
                int currentSpace = (int)uiComp.space;
                if (ImGui::Combo("##UI3DSpaceMode", &currentSpace, spaceNames, 2)) {
                    UndoManager::Get().RecordSnapshot(scene, "Change UI Space Mode");
                    uiComp.space = (EunoiaUI::UI3DSpaceType)currentSpace;
                }
                if (ImGui::IsItemHovered()) {
                    if (uiComp.space == EunoiaUI::UI3DSpaceType::World) {
                        ImGui::SetTooltip("World: UI is placed in 3D world space and rotates with actor transform.");
                    } else {
                        ImGui::SetTooltip("Screen: UI is billboarded and always targets towards camera's view.");
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();

                // 3. Size & Resolution
                ImGui::TextDisabled("3D Dimensions & Canvas");
                ImGui::DragFloat2("World Size (W x H)##UI3DSize", &uiComp.size.x, 0.05f, 0.1f, 100.0f, "%.2f m");
                ImGui::DragFloat2("Canvas Res (px)##UI3DRes", &uiComp.canvasResolution.x, 10.0f, 100.0f, 7680.0f, "%.0f px");

                ImGui::Spacing();
                ImGui::Separator();

                // 4. Appearance
                ImGui::TextDisabled("Appearance");
                ImGui::Checkbox("Two-Sided##UI3DTwoSided", &uiComp.twoSided);
                ImGui::SliderFloat("Opacity##UI3DOpacity", &uiComp.opacity, 0.0f, 1.0f, "%.2f");
                ImGui::ColorEdit4("Background Color##UI3DBG", &uiComp.backgroundColor.r, ImGuiColorEditFlags_AlphaBar);
                ImGui::ColorEdit4("Border Color##UI3DBorder", &uiComp.borderColor.r, ImGuiColorEditFlags_AlphaBar);
                ImGui::DragFloat("Border Width##UI3DBorderW", &uiComp.borderWidth, 0.1f, 0.0f, 20.0f, "%.1f px");
            }
        }

        // =========================================================
        // LEVEL ANIMATION COMPONENT
        // =========================================================
        if (obj->hasLevelAnimation || obj->type == PrimitiveType::LevelAnimation) {
            if (ImGui::CollapsingHeader("Level Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PushID("LevelAnimationDetailsSection");

                ImGui::Checkbox("Enabled##LAEnabled", &obj->levelAnimation.enabled);
                ImGui::SameLine(0.0f, 15.0f * uiScale);
                ImGui::Checkbox("Play On Start##LAPlayOnStart", &obj->levelAnimation.playOnStart);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("When checked, this level animation automatically plays on start (default: enabled).");
                }
                ImGui::SameLine(0.0f, 15.0f * uiScale);
                ImGui::Checkbox("Loop##LALoop", &obj->levelAnimation.loop);

                ImGui::Spacing();
                ImGui::DragFloat("Playback Speed##LASpeed", &obj->levelAnimation.playbackSpeed, 0.05f, -10.0f, 10.0f, "%.2fx");

                ImGui::Spacing();
                ImGui::Text("Animation Asset (.elanim)");

                std::string currentAnimPreview = "(None - Select or Drop .elanim)";
                if (!obj->levelAnimation.animationPath.empty()) {
                    currentAnimPreview = std::filesystem::path(obj->levelAnimation.animationPath).filename().string();
                }

                if (ImGui::BeginCombo("##LevelAnimAssetCombo", currentAnimPreview.c_str())) {
                    if (ImGui::Selectable("(None)", obj->levelAnimation.animationPath.empty())) {
                        obj->levelAnimation.animation.reset();
                        obj->levelAnimation.animationPath = "";
                    }
                    ImGui::Separator();

                    // Search for .elanim files in content directory
                    if (!contentRootPath.empty() && std::filesystem::exists(contentRootPath)) {
                        std::error_code ec;
                        for (auto it = std::filesystem::recursive_directory_iterator(contentRootPath, ec);
                             it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                            if (ec) break;
                            if (it->is_regular_file() && it->path().extension() == ".elanim") {
                                std::string rel = std::filesystem::relative(it->path(), contentRootPath, ec).string();
                                bool isSel = (obj->levelAnimation.animationPath == it->path().string() || obj->levelAnimation.animationPath == rel);
                                if (ImGui::Selectable(rel.c_str(), isSel)) {
                                    obj->levelAnimation.LoadAnimation(it->path().string());
                                    if (obj->levelAnimation.animation) {
                                        Eunoia::LevelAnimationEditor::Get().SetActiveAnimation(obj->levelAnimation.animation);
                                        Eunoia::LevelAnimationManager::Get().SetActiveActorId(obj->id);
                                    }
                                    AddLog("LogLevelAnimation", "Assigned Level Animation: " + rel + " to " + obj->name, 0);
                                }
                            }
                        }
                    }
                    ImGui::EndCombo();
                }

                // Drag & drop target for .elanim assets from Content Browser
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                        std::string droppedPath((const char*)payload->Data);
                        if (droppedPath.find(".elanim") != std::string::npos) {
                            std::filesystem::path fullP = contentRootPath / droppedPath;
                            obj->levelAnimation.LoadAnimation(fullP.string());
                            if (obj->levelAnimation.animation) {
                                Eunoia::LevelAnimationEditor::Get().SetActiveAnimation(obj->levelAnimation.animation);
                                Eunoia::LevelAnimationManager::Get().SetActiveActorId(obj->id);
                            }
                            AddLog("LogLevelAnimation", "Dropped Level Animation: " + droppedPath + " onto " + obj->name, 0);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::Spacing();

                // Open in Animation Timeline button
                if (ImGui::Button("Open in Animation Timeline", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f * uiScale))) {
                    if (!obj->levelAnimation.animationPath.empty()) {
                        OpenLevelAnimationEditor(obj->levelAnimation.animationPath);
                    } else {
                        bottomDrawerTab = 1;
                        bottomDrawerOpen = true;
                    }
                    if (obj->levelAnimation.animation) {
                        Eunoia::LevelAnimationEditor::Get().SetActiveAnimation(obj->levelAnimation.animation);
                    }
                    Eunoia::LevelAnimationManager::Get().SetActiveActorId(obj->id);
                }

                // In-editor preview controls
                if (obj->levelAnimation.animation) {
                    ImGui::Spacing();
                    float endT = obj->levelAnimation.animation->duration;
                    ImGui::Text("Time: %.2fs / %.2fs", obj->levelAnimation.currentTime, endT);

                    float btnW = (ImGui::GetContentRegionAvail().x - 16.0f) / 3.0f;
                    if (ImGui::Button(obj->levelAnimation.isPlaying ? "Pause##LAPreview" : "Play##LAPreview", ImVec2(btnW, 22.0f * uiScale))) {
                        if (obj->levelAnimation.isPlaying) {
                            obj->levelAnimation.Pause();
                        } else {
                            obj->levelAnimation.Play();
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Stop##LAPreview", ImVec2(btnW, 22.0f * uiScale))) {
                        obj->levelAnimation.Stop();
                        obj->levelAnimation.Update(0.0f, obj, &scene);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Reset##LAPreview", ImVec2(btnW, 22.0f * uiScale))) {
                        obj->levelAnimation.currentTime = 0.0f;
                        obj->levelAnimation.Update(0.0f, obj, &scene);
                    }
                }

                ImGui::PopID();
            }
        }

        // Primitive Shape Settings Category (dev.md)
        if (!obj->isLight && !IsLightPrimitive(obj->type) && !obj->isCamera && !IsCameraPrimitive(obj->type) && !obj->isWaterBody && obj->type != PrimitiveType::WaterBody && !obj->isTerrain && obj->type != PrimitiveType::Terrain && !obj->isUserInterface && obj->type != PrimitiveType::UserInterface && obj->type != PrimitiveType::Empty && obj->type != PrimitiveType::ImportedMesh && obj->type != PrimitiveType::LevelAnimation) {
            char shapeHeader[128];
            snprintf(shapeHeader, sizeof(shapeHeader), "%s Parameters", GetPrimitiveTypeName(obj->type));
            if (ImGui::CollapsingHeader(shapeHeader, ImGuiTreeNodeFlags_DefaultOpen)) {
                bool paramChanged = false;

                if (obj->type == PrimitiveType::Cube) {
                    paramChanged |= ImGui::DragFloat("Size", &obj->params.size, 0.05f, 0.05f, 20.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Subdivisions", &obj->params.segmentsX, 1, 64);
                }
                else if (obj->type == PrimitiveType::Plane) {
                    paramChanged |= ImGui::DragFloat("Width", &obj->params.width, 0.1f, 0.1f, 100.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Depth", &obj->params.depth, 0.1f, 0.1f, 100.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Segments X", &obj->params.segmentsX, 1, 64);
                    paramChanged |= ImGui::SliderInt("Segments Z", &obj->params.segmentsZ, 1, 64);
                }
                else if (obj->type == PrimitiveType::Box) {
                    paramChanged |= ImGui::DragFloat("Width", &obj->params.width, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Height", &obj->params.height, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Depth", &obj->params.depth, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Segments X", &obj->params.segmentsX, 1, 32);
                    paramChanged |= ImGui::SliderInt("Segments Y", &obj->params.segmentsY, 1, 32);
                    paramChanged |= ImGui::SliderInt("Segments Z", &obj->params.segmentsZ, 1, 32);
                }
                else if (obj->type == PrimitiveType::Sphere || obj->type == PrimitiveType::UVSphere) {
                    paramChanged |= ImGui::DragFloat("Radius", &obj->params.radius, 0.05f, 0.05f, 25.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Rings / Lat", &obj->params.rings, 3, 64);
                    paramChanged |= ImGui::SliderInt("Sectors / Long", &obj->params.sectors, 3, 64);
                }
                else if (obj->type == PrimitiveType::Icosphere) {
                    paramChanged |= ImGui::DragFloat("Radius", &obj->params.radius, 0.05f, 0.05f, 25.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Subdivisions", &obj->params.subdivisions, 0, 5);
                }
                else if (obj->type == PrimitiveType::Cylinder) {
                    paramChanged |= ImGui::DragFloat("Radius", &obj->params.radius, 0.05f, 0.05f, 25.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Height", &obj->params.height, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Radial Segments", &obj->params.radialSegments, 3, 64);
                    paramChanged |= ImGui::SliderInt("Height Segments", &obj->params.heightSegments, 1, 32);
                    paramChanged |= ImGui::Checkbox("Cap Top", &obj->params.capTop);
                    paramChanged |= ImGui::Checkbox("Cap Bottom", &obj->params.capBottom);
                }
                else if (obj->type == PrimitiveType::Cone) {
                    paramChanged |= ImGui::DragFloat("Bottom Radius", &obj->params.radius, 0.05f, 0.0f, 25.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Top Radius", &obj->params.radius2, 0.05f, 0.0f, 25.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Height", &obj->params.height, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Radial Segments", &obj->params.radialSegments, 3, 64);
                    paramChanged |= ImGui::SliderInt("Height Segments", &obj->params.heightSegments, 1, 32);
                    paramChanged |= ImGui::Checkbox("Cap Bottom", &obj->params.capBottom);
                    paramChanged |= ImGui::Checkbox("Cap Top", &obj->params.capTop);
                }
                else if (obj->type == PrimitiveType::Capsule) {
                    paramChanged |= ImGui::DragFloat("Radius", &obj->params.radius, 0.05f, 0.05f, 20.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Height", &obj->params.height, 0.05f, 0.05f, 40.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Radial Segments", &obj->params.radialSegments, 3, 48);
                    paramChanged |= ImGui::SliderInt("Height Segments", &obj->params.heightSegments, 1, 24);
                }
                else if (obj->type == PrimitiveType::Torus) {
                    paramChanged |= ImGui::DragFloat("Major Radius", &obj->params.radius, 0.05f, 0.1f, 25.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Minor Radius", &obj->params.radius2, 0.02f, 0.02f, 10.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Major Segments", &obj->params.radialSegments, 3, 64);
                    paramChanged |= ImGui::SliderInt("Minor Segments", &obj->params.segmentsY, 3, 48);
                }
                else if (obj->type == PrimitiveType::Circle || obj->type == PrimitiveType::Disc) {
                    paramChanged |= ImGui::DragFloat("Radius", &obj->params.radius, 0.05f, 0.05f, 25.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Segments", &obj->params.radialSegments, 3, 64);
                }
                else if (obj->type == PrimitiveType::Quad) {
                    paramChanged |= ImGui::DragFloat("Width", &obj->params.width, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Height", &obj->params.height, 0.05f, 0.05f, 50.0f, "%.2f");
                }
                else if (obj->type == PrimitiveType::Triangle) {
                    paramChanged |= ImGui::DragFloat("Base Width", &obj->params.width, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Height", &obj->params.height, 0.05f, 0.05f, 50.0f, "%.2f");
                }
                else if (obj->type == PrimitiveType::Pyramid) {
                    paramChanged |= ImGui::DragFloat("Base Width", &obj->params.width, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Base Depth", &obj->params.depth, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Height", &obj->params.height, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Sides", &obj->params.sides, 3, 32);
                }
                else if (obj->type == PrimitiveType::Prism) {
                    paramChanged |= ImGui::DragFloat("Radius", &obj->params.radius, 0.05f, 0.05f, 25.0f, "%.2f");
                    paramChanged |= ImGui::DragFloat("Height", &obj->params.height, 0.05f, 0.05f, 50.0f, "%.2f");
                    paramChanged |= ImGui::SliderInt("Sides", &obj->params.sides, 3, 32);
                }

                if (paramChanged) {
                    obj->RebuildMesh();
                }
            }
        }

        // Terrain Dimensions & Grid Parameters (when Properties tab is active)
        if ((obj->isTerrain || obj->type == PrimitiveType::Terrain) && ImGui::CollapsingHeader("Terrain Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
            static Eunoia::TerrainComponent s_preResizeTerrain;
            static Scene s_preResizeSceneSnapshot;
            static bool s_isResizingTerrain = false;
            static int s_terrainAnchorMode = 0; // 0: Center, 1: Min Corner (-X, -Z), 2: Max Corner (+X, +Z)

            bool gridChanged = false;
            float w = obj->terrain.width;
            float l = obj->terrain.length;
            int res = obj->terrain.resolution;

            const char* anchorNames[] = { "Center (Preserve World Position)", "Min Corner (-X, -Z)", "Max Corner (+X, +Z)" };
            ImGui::Combo("Extend Anchor##TerrAnchor", &s_terrainAnchorMode, anchorNames, 3);

            if (ImGui::DragFloat("Width (X)##TerrW", &w, 1.0f, 4.0f, 4096.0f, "%.1f m")) {
                if (w < 4.0f) w = 4.0f;
                gridChanged = true;
            }
            bool wDeactivated = ImGui::IsItemDeactivatedAfterEdit();

            if (ImGui::DragFloat("Length (Z)##TerrL", &l, 1.0f, 4.0f, 4096.0f, "%.1f m")) {
                if (l < 4.0f) l = 4.0f;
                gridChanged = true;
            }
            bool lDeactivated = ImGui::IsItemDeactivatedAfterEdit();

            if (ImGui::SliderInt("Resolution (Grid)##TerrRes", &res, 8, 512)) {
                gridChanged = true;
            }
            bool resDeactivated = ImGui::IsItemDeactivatedAfterEdit();

            if (gridChanged) {
                if (!s_isResizingTerrain) {
                    s_preResizeTerrain = obj->terrain;
                    s_preResizeSceneSnapshot = scene;
                    s_isResizingTerrain = true;
                }
                obj->terrain = s_preResizeTerrain;
                obj->terrain.ResizeGrid(w, l, res, s_terrainAnchorMode);
                obj->terrain.RebuildTerrainMesh(obj->mesh);
                RequestViewportRedraw(5);
            }

            if (wDeactivated || lDeactivated || resDeactivated) {
                if (s_isResizingTerrain) {
                    UndoManager::Get().RecordSnapshot(s_preResizeSceneSnapshot, "Resize/Extend Terrain: " + obj->name);
                    s_isResizingTerrain = false;
                    AddLog("LogTerrain", "Resized terrain to " + std::to_string((int)obj->terrain.width) + "m x " + std::to_string((int)obj->terrain.length) + "m (sculpt & painted materials preserved)", 0);
                }
            }

            float quadSpacing = obj->terrain.width / (float)std::max(1, obj->terrain.resolution);
            ImGui::TextDisabled("Grid Vertices: %d x %d (%zu verts, %zu triangles, quad spacing: %.2fm)",
                obj->terrain.resolution + 1, obj->terrain.resolution + 1,
                obj->mesh.vertices.size(), obj->mesh.indices.size() / 3, quadSpacing);
            ImGui::TextDisabled("Extending Width or Length preserves all sculpted heights, tilts, and painted materials.");
        }

        // Empty Actor Information
        if (obj->type == PrimitiveType::Empty) {
            ImGui::Spacing();
            ImGui::TextDisabled("Empty Actor (Transform Pivot / Group Node)");
            ImGui::TextDisabled("Has no visual mesh. Children inherit transform.");
        }

        // 2. Static Mesh Category (only for mesh actors)
        if (!obj->isLight && !obj->isCamera && obj->type != PrimitiveType::Camera && !obj->isUserInterface && obj->type != PrimitiveType::UserInterface && obj->type != PrimitiveType::Empty && obj->type != PrimitiveType::LevelAnimation && ImGui::CollapsingHeader("Static Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
            char meshAsset[64];
            snprintf(meshAsset, sizeof(meshAsset), "SM_%s", GetPrimitiveTypeName(obj->type));
            ImGui::TextDisabled("Static Mesh Asset:");
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "%s", meshAsset);
            ImGui::TextDisabled("Vertices: %d | Triangles: %d", (int)obj->mesh.vertices.size(), (int)obj->mesh.indices.size() / 3);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Shading (Blender Workflow):");

            float btnW = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
            if (ImGui::Button("Shade Smooth", ImVec2(btnW, 24.0f * uiScale))) {
                UndoManager::Get().RecordSnapshot(scene, "Shade Smooth: " + obj->name);
                obj->SetSmoothShading(true, obj->autoSmoothAngle);
                for (int cid : obj->childIds) {
                    if (GameObject* child = scene.FindObject(cid)) {
                        child->SetSmoothShading(true, obj->autoSmoothAngle);
                    }
                }
                AddLog("LogActor", "Applied Shade Smooth to " + obj->name, 2);
                RequestViewportRedraw(2);
            }
            ImGui::SameLine();
            if (ImGui::Button("Shade Flat", ImVec2(btnW, 24.0f * uiScale))) {
                UndoManager::Get().RecordSnapshot(scene, "Shade Flat: " + obj->name);
                obj->SetSmoothShading(false);
                for (int cid : obj->childIds) {
                    if (GameObject* child = scene.FindObject(cid)) {
                        child->SetSmoothShading(false);
                    }
                }
                AddLog("LogActor", "Applied Shade Flat to " + obj->name, 2);
                RequestViewportRedraw(2);
            }

            bool smShade = obj->smoothShading;
            if (ImGui::Checkbox("Smooth Shading", &smShade)) {
                UndoManager::Get().RecordSnapshot(scene, "Toggle Smooth Shading: " + obj->name);
                obj->SetSmoothShading(smShade, obj->autoSmoothAngle);
                for (int cid : obj->childIds) {
                    if (GameObject* child = scene.FindObject(cid)) {
                        child->SetSmoothShading(smShade, obj->autoSmoothAngle);
                    }
                }
                RequestViewportRedraw(2);
            }

            if (obj->smoothShading) {
                float angle = obj->autoSmoothAngle;
                if (ImGui::SliderFloat("Auto Smooth Angle", &angle, 0.0f, 180.0f, "%.0f deg")) {
                    obj->autoSmoothAngle = angle;
                    obj->SetSmoothShading(true, angle);
                    for (int cid : obj->childIds) {
                        if (GameObject* child = scene.FindObject(cid)) {
                            child->SetSmoothShading(true, angle);
                        }
                    }
                    RequestViewportRedraw(2);
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    UndoManager::Get().RecordSnapshot(scene, "Change Auto Smooth Angle: " + obj->name);
                }
            }
        }

        // 3. Materials Category (Element 0) (mesh actors or parent actors with children)
        if (!obj->isLight && !obj->isCamera && obj->type != PrimitiveType::Camera && !obj->isUserInterface && obj->type != PrimitiveType::UserInterface && obj->type != PrimitiveType::LevelAnimation && (obj->type != PrimitiveType::Empty || !obj->childIds.empty()) && ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Element 0: Material Slot");
            if (!obj->childIds.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 0.9f), "(ÃƒÂ¢Ã¢â‚¬Â Ã‚Â³ Cascades to all %zu children)", obj->childIds.size());
            }

            // Scan for all materials in contentRootPath (recursively across the entire project root)
            RefreshCachedMaterials(contentRootPath);
            const auto& matList = s_cachedMatList;
            const auto& folderGroups = s_cachedFolderGroups;

            // Construct combo preview label
            std::string comboPreview = "" + (obj->materialName.empty() ? std::string("Default_Material") : obj->materialName);
            for (const auto& m : matList) {
                if (m.name == obj->materialName || m.relPath == obj->materialName) {
                    if (!m.relFolder.empty()) {
                        comboPreview += "  [" + m.relFolder + "]";
                    } else {
                        comboPreview += "  [Root]";
                    }
                    break;
                }
            }

            // Recurrent Material Dropdown Combo
            static char matFilter[64] = "";
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 36.0f);
            if (ImGui::BeginCombo("##MaterialSlotDropdown", comboPreview.c_str(), ImGuiComboFlags_HeightLarge)) {
                // Search filter inside dropdown
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##matFilter", "Search materials in project...", matFilter, sizeof(matFilter));
                ImGui::Separator();

                std::string searchFilter = matFilter;
                std::transform(searchFilter.begin(), searchFilter.end(), searchFilter.begin(), ::tolower);

                if (!searchFilter.empty()) {
                    // Filtered view across all materials
                    int matchCount = 0;
                    for (const auto& m : matList) {
                        std::string nameLower = m.name;
                        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                        std::string relLower = m.relPath;
                        std::transform(relLower.begin(), relLower.end(), relLower.begin(), ::tolower);

                        if (nameLower.find(searchFilter) != std::string::npos || relLower.find(searchFilter) != std::string::npos) {
                            matchCount++;
                            ImGui::PushID(m.fullPath.c_str());
                            bool isSelected = (obj->materialName == m.name || obj->materialName == m.relPath);

                            ImGui::ColorButton("##swatch", ImVec4(m.color.r, m.color.g, m.color.b, 1.0f),
                                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(16, 16));
                            ImGui::SameLine();

                            std::string itemLabel = m.name;
                            if (!m.relFolder.empty()) {
                                itemLabel += "  (" + m.relFolder + ")";
                            } else {
                                itemLabel += "  (Root)";
                            }

                            if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                                MaterialAsset ma;
                                if (LoadMaterialFile(m.fullPath, ma)) {
                                    ApplyMaterialToActorAndChildren(scene, obj, ma);
                                }
                            }
                            if (isSelected) ImGui::SetItemDefaultFocus();
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("File: %s\nPath: %s\nRoughness: %.2f | Metallic: %.2f",
                                    m.name.c_str(), m.relPath.c_str(), m.roughness, m.metallic);
                            }
                            ImGui::PopID();
                        }
                    }
                    if (matchCount == 0) {
                        ImGui::TextDisabled("No materials match \"%s\"", matFilter);
                    }
                } else {
                    // Recurrent / Hierarchical Folder Grouping View
                    for (const auto& [folder, items] : folderGroups) {
                        std::string folderTitle;
                        if (folder.empty()) {
                            folderTitle = "[Project Root] (" + std::to_string(items.size()) + ")";
                        } else {
                            folderTitle = folder + " (" + std::to_string(items.size()) + ")";
                        }

                        bool groupOpen = ImGui::TreeNodeEx(folderTitle.c_str(),
                            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth);

                        if (groupOpen) {
                            for (const auto& m : items) {
                                ImGui::PushID(m.fullPath.c_str());
                                bool isSelected = (obj->materialName == m.name || obj->materialName == m.relPath);

                                ImGui::ColorButton("##swatch", ImVec4(m.color.r, m.color.g, m.color.b, 1.0f),
                                    ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(16, 16));
                                ImGui::SameLine();

                                if (ImGui::Selectable(m.name.c_str(), isSelected)) {
                                    MaterialAsset ma;
                                    if (LoadMaterialFile(m.fullPath, ma)) {
                                        ApplyMaterialToActorAndChildren(scene, obj, ma);
                                    }
                                }
                                if (isSelected) ImGui::SetItemDefaultFocus();
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Material: %s\nLocation: %s\nRoughness: %.2f | Metallic: %.2f",
                                        m.name.c_str(), m.relPath.c_str(), m.roughness, m.metallic);
                                }
                                ImGui::PopID();
                            }
                            ImGui::TreePop();
                        }
                    }
                }

                ImGui::EndCombo();
            }

            // Drag & Drop Target for Material Slot (dev.md Section 34)
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                    char assetIdBuf[40] = {};
                    memcpy(assetIdBuf, payload->Data, std::min((size_t)payload->DataSize, sizeof(assetIdBuf) - 1));
                    AssetID droppedId = AssetID::FromString(assetIdBuf);
                    AssetMetadata* meta = AssetRegistry::Get().Find(droppedId);
                    if (meta && meta->type == AssetType::Material) {
                        std::filesystem::path fullPath = contentRootPath / meta->sourcePath;
                        MaterialAsset ma;
                        if (LoadMaterialFile(fullPath.string(), ma)) {
                            ApplyMaterialToActorAndChildren(scene, obj, ma);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Button to open currently selected material in Material Editor
            ImGui::SameLine();
            if (ImGui::Button("Edit##EditMatBtn")) {
                std::string targetPath = "";
                for (const auto& m : matList) {
                    if (m.name == obj->materialName || m.relPath == obj->materialName) {
                        targetPath = m.fullPath;
                        break;
                    }
                }
                if (targetPath.empty()) {
                    targetPath = (contentRootPath / "Materials" / "Default_Material.emat").string();
                }
                OpenMaterialEditor(targetPath);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Open in Material Editor");
            }

            ImGui::SameLine();
            if (ImGui::Button("Ref##MatRefBtn")) {
                AssetMetadata* meta = AssetRegistry::Get().FindByNameOrPath(obj->materialName);
                if (meta) {
                    OpenReferenceViewer(meta->id);
                } else {
                    OpenReferenceViewer();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("View Material in Asset Reference Viewer (dev.md Section 33)");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.2f, 0.75f, 1.0f, 1.0f), "Actor UV Tiling (UV Scale):");
            ImGui::DragFloat2("UV Scale##ActorUV", &obj->uvScale.x, 0.05f, 0.001f, 100.0f, "%.2f");
            ImGui::DragFloat("Tiling X (U)##ActorUVX", &obj->uvScale.x, 0.05f, 0.001f, 100.0f, "%.2f");
            ImGui::DragFloat("Tiling Y (V)##ActorUVY", &obj->uvScale.y, 0.05f, 0.001f, 100.0f, "%.2f");
            ImGui::SameLine();
            if (ImGui::Button("Reset##ActorUVReset")) {
                obj->uvScale = glm::vec2(1.0f, 1.0f);
            }
            ImGui::Spacing();
            if (ImGui::Button("Open in Material Editor", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                std::string targetPath = "";
                for (const auto& m : matList) {
                    if (m.name == obj->materialName || m.relPath == obj->materialName) {
                        targetPath = m.fullPath;
                        break;
                    }
                }
                if (targetPath.empty()) {
                    targetPath = (contentRootPath / "Materials" / "Default_Material.emat").string();
                }
                OpenMaterialEditor(targetPath);
            }
            ImGui::TextDisabled("Material properties are edited in the Material Editor.");
        }

        // 4. Lighting & Shadows
        if (!obj->isLight && obj->type != PrimitiveType::Empty && obj->type != PrimitiveType::LevelAnimation && ImGui::CollapsingHeader("Lighting & Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Cast Shadows", &obj->castShadows);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("When enabled, this actor casts shadows into the real-time shadow map.");
            }
            ImGui::SameLine();
            ImGui::Checkbox("Receive Shadows", &obj->receiveShadows);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("When enabled, this actor receives shadows from directional sun light.");
            }
        }

        // 4.5 Wicked Engine Mesh LOD (Disabled by default - full quality when disabled)
        if (!obj->isLight && obj->type != PrimitiveType::Empty && obj->type != PrimitiveType::LevelAnimation && ImGui::CollapsingHeader("Wicked Engine Mesh LOD", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool lodEnabled = obj->enableLOD;
            if (ImGui::Checkbox("Enable Mesh LOD", &lodEnabled)) {
                scene.SetMeshLODRecursive(obj->id, lodEnabled);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("When disabled (default), the mesh always renders at full quality (LOD 0) at all distances.\nWhen enabled, Wicked Engine screen-space coverage LOD scaling dynamically selects continuous simplified LOD levels.");
            }

            auto PropagateLODToChildren = [&](auto& self, int parentId, int forcedLOD, float lodBias, int minLOD, int maxLOD) -> void {
                GameObject* parent = scene.FindObject(parentId);
                if (!parent) return;
                for (int childId : parent->childIds) {
                    GameObject* child = scene.FindObject(childId);
                    if (child) {
                        child->forcedLOD = forcedLOD;
                        child->lodBias = lodBias;
                        child->minLOD = minLOD;
                        child->maxLOD = maxLOD;
                        self(self, childId, forcedLOD, lodBias, minLOD, maxLOD);
                    }
                }
            };

            uint32_t lodCount = obj->mesh.GetLODCount();

            if (!obj->enableLOD) {
                ImGui::BeginDisabled(true);
            }

            const char* lodOptions[] = {
                "Auto (Distance / Coverage)",
                "Forced LOD 0", "Forced LOD 1", "Forced LOD 2", "Forced LOD 3", "Forced LOD 4",
                "Forced LOD 5", "Forced LOD 6", "Forced LOD 7", "Forced LOD 8", "Forced LOD 9",
                "Forced LOD 10", "Forced LOD 11", "Forced LOD 12", "Forced LOD 13", "Forced LOD 14",
                "Forced LOD 15", "Forced LOD 16", "Forced LOD 17", "Forced LOD 18", "Forced LOD 19",
                "Forced LOD 20"
            };
            int currentLodOpt = (obj->forcedLOD < 0) ? 0 : std::min(obj->forcedLOD + 1, 21);
            if (ImGui::Combo("LOD Mode", &currentLodOpt, lodOptions, 22)) {
                obj->forcedLOD = (currentLodOpt == 0) ? -1 : (currentLodOpt - 1);
                PropagateLODToChildren(PropagateLODToChildren, obj->id, obj->forcedLOD, obj->lodBias, obj->minLOD, obj->maxLOD);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Controls Level of Detail (LOD) selection (0 to 20).\nAuto: dynamically selects LOD based on screen-space coverage.\nForced: locks object and all its children to a specific LOD level.");
            }

            if (ImGui::SliderFloat("LOD Bias", &obj->lodBias, -20.0f, 20.0f, "%.2f")) {
                PropagateLODToChildren(PropagateLODToChildren, obj->id, obj->forcedLOD, obj->lodBias, obj->minLOD, obj->maxLOD);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Offset applied to screen-space LOD calculation (-20.0 to +20.0).\nNegative: prefers higher detail closer.\nPositive: transitions to lower detail sooner.\nApplies recursively to all children.");
            }

            if (ImGui::SliderInt("Min LOD", &obj->minLOD, 0, 20)) {
                if (obj->minLOD > obj->maxLOD) obj->maxLOD = obj->minLOD;
                PropagateLODToChildren(PropagateLODToChildren, obj->id, obj->forcedLOD, obj->lodBias, obj->minLOD, obj->maxLOD);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Minimum LOD level (highest detail limit). Applies recursively to all children.");
            }

            if (ImGui::SliderInt("Max LOD", &obj->maxLOD, obj->minLOD, 20)) {
                PropagateLODToChildren(PropagateLODToChildren, obj->id, obj->forcedLOD, obj->lodBias, obj->minLOD, obj->maxLOD);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Maximum LOD level (lowest detail limit). Applies recursively to all children.");
            }

            if (!obj->enableLOD) {
                ImGui::EndDisabled();
            }

            // Real-time LOD Telemetry
            uint32_t activeLod = obj->enableLOD ? (uint32_t)glm::clamp(obj->currentLOD, 0, (int)lodCount - 1) : 0;
            uint32_t activeTriangles = (uint32_t)obj->mesh.GetIndicesForLOD(activeLod).size() / 3;
            uint32_t lod0Triangles = (uint32_t)obj->mesh.indices.size() / 3;
            float reductionPercent = (lod0Triangles > 0 && obj->enableLOD) ? ((1.0f - (float)activeTriangles / (float)lod0Triangles) * 100.0f) : 0.0f;

            ImGui::Text("Active LOD: ");
            ImGui::SameLine();
            if (obj->enableLOD) {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "LOD %u / %u", activeLod, lodCount);
                ImGui::SameLine();
                ImGui::TextDisabled("(%u Tris, -%.1f%%)", activeTriangles, reductionPercent);
            } else {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "LOD 0 / %u (Disabled - Full Quality)", lodCount);
                ImGui::SameLine();
                ImGui::TextDisabled("(%u Tris)", lod0Triangles);
            }
        }

        // 4.6 SFLOD (Single Face LOD / Imposter Cards System - Nacho Time!)
        if (!obj->isLight && !obj->isCamera && obj->type != PrimitiveType::Empty && obj->type != PrimitiveType::LevelAnimation && ImGui::CollapsingHeader("SFLOD (Single Face LOD - Nacho Time!)", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool sflodEnabled = obj->enableSFLOD;
            if (ImGui::Checkbox("Enable SFLOD", &sflodEnabled)) {
                obj->enableSFLOD = sflodEnabled;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("When enabled, this object automatically transitions to an ultra-efficient\nimposter card proxy (2 to 6 triangles) with dynamic orthogonal atlas sampling.");
            }

            if (!obj->enableSFLOD) {
                ImGui::BeginDisabled(true);
            }

            // 1-Click Nacho Time Generation Button (Prominent)
            std::filesystem::path contentDir = contentRootPath;
            if (contentDir.empty() && !activeProjectRoot.empty()) {
                contentDir = activeProjectRoot / "Content";
            }
            if (contentDir.empty()) {
                contentDir = "Projects/GameTest/Content";
            }

            // Auto-discover baked 6-directional atlas on disk if not yet assigned
            if (obj->sflodCustomTexture.empty() || !obj->sflodIsAtlas) {
                std::string safeName = SFLOD::SFLODSystem::CleanFilename(obj->name);
                std::vector<std::filesystem::path> searchDirs = {
                    contentDir / "Imposters",
                    "Content/Imposters",
                    "Projects/TestGameMake/Content/Imposters",
                    "Projects/GameTest/Content/Imposters"
                };
                for (const auto& sDir : searchDirs) {
                    std::filesystem::path candidate = sDir / ("SFLOD_" + safeName + "_Atlas.tga");
                    std::error_code ec;
                    if (std::filesystem::exists(candidate, ec)) {
                        obj->sflodCustomTexture = candidate.generic_string();
                        obj->sflodIsAtlas = true;
                        break;
                    }
                }
            }

            glm::mat4 modelMat = scene.GetWorldMatrix(*obj);
            glm::vec3 camEye = currentCamera ? currentCamera->GetPosition() : glm::vec3(0.0f, 2.0f, 5.0f);

            static int s_imposterResolution = 512;

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.75f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.90f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.35f, 0.65f, 1.0f));
            if (ImGui::Button("Generate Imposter Cards (Nacho Time!)", ImVec2(ImGui::GetContentRegionAvail().x, 32.0f))) {
                std::string bakedAtlas = SFLOD::SFLODSystem::Get().BakeNachoTimeAtlas(
                    *obj, modelMat, contentDir, s_imposterResolution, 
                    static_cast<SFLOD::SFLODShaderComplexity>(obj->sflodShaderComplexity));
                if (!bakedAtlas.empty()) {
                    AddLog("LogSFLOD", "Successfully baked 6-directional Nacho Time atlas: " + bakedAtlas, 0);
                }
            }
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("1-Click Generator: Captures 6 orthogonal shots (+X, -X, +Y, -Y, +Z, -Z) into a unified\n3x2 texture atlas in Content/Imposters/ and sets up dynamic view vector tracking.");
            }

            // Geometry Mode (Single Plane, Tri-Planar, 5-Face Box, 6-Face Cube, Custom Multi-Face)
            const char* geomModes[] = {
                "Single Plane (2 Tris - Parallax View Vector)",
                "Tri-Planar Nacho Mode (6 Tris - 3 Intersecting Planes)",
                "5-Face Box (10 Tris - Open Container / 5 Directions)",
                "6-Face Cube (12 Tris - Solid Box / 6 Directions)",
                "Custom Multi-Face Box (Varying Faces)"
            };
            int curGeomMode = glm::clamp(obj->sflodGeometryMode, 0, 4);
            if (ImGui::Combo("Geometry Mode", &curGeomMode, geomModes, 5)) {
                obj->sflodGeometryMode = curGeomMode;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Imposter Card Geometry Mode:\n"
                                  "- Single Plane: 1 quad (2 triangles) that dynamically samples dominant atlas tile based on view vector.\n"
                                  "- Tri-Planar Nacho Mode: 3 physical intersecting orthogonal planes (6 triangles) providing full 360 volume.\n"
                                  "- 5-Face Box: 5 physical boundary planes (10 triangles) for open-top containers, crates, bins, and boxes.\n"
                                  "- 6-Face Cube: 6 physical boundary planes (12 triangles) for closed boxes and solid polyhedral geometry.\n"
                                  "- Custom Multi-Face: Fully configurable active face mask (1 to 6 faces).");
            }

            // Open Face Selector (when 5-Face Box is selected)
            if (obj->sflodGeometryMode == 2) {
                const char* faceNames[] = {
                    "Front (+Z)", "Back (-Z)", "Right (+X)", "Left (-X)", "Top (+Y, Open Crate / Box)", "Bottom (-Y, Open Base)"
                };
                int curOpenFace = glm::clamp(obj->sflodOpenFace, 0, 5);
                if (ImGui::Combo("Open / Omitted Face", &curOpenFace, faceNames, 6)) {
                    obj->sflodOpenFace = curOpenFace;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Selects which face of the box is left open (unrendered).\nFor open crates, baskets, and bins (like in image.png), select Top (+Y).");
                }
            }

            // Active Faces Checkboxes (when Custom Multi-Face is selected, or collapsible fine-tuning)
            if (obj->sflodGeometryMode == 4 || ImGui::TreeNode("Active Face Masks (Fine-Tuning)")) {
                bool fFront = (obj->sflodFaceMask & (1 << 0)) != 0;
                bool fBack  = (obj->sflodFaceMask & (1 << 1)) != 0;
                bool fRight = (obj->sflodFaceMask & (1 << 2)) != 0;
                bool fLeft  = (obj->sflodFaceMask & (1 << 3)) != 0;
                bool fTop   = (obj->sflodFaceMask & (1 << 4)) != 0;
                bool fBot   = (obj->sflodFaceMask & (1 << 5)) != 0;

                ImGui::TextDisabled("Toggle which orthogonal face directions are active:");
                if (ImGui::Checkbox("Front (+Z)", &fFront)) { obj->sflodFaceMask ^= (1 << 0); } ImGui::SameLine();
                if (ImGui::Checkbox("Back (-Z)", &fBack))   { obj->sflodFaceMask ^= (1 << 1); } ImGui::SameLine();
                if (ImGui::Checkbox("Right (+X)", &fRight)) { obj->sflodFaceMask ^= (1 << 2); }
                if (ImGui::Checkbox("Left (-X)", &fLeft))   { obj->sflodFaceMask ^= (1 << 3); } ImGui::SameLine();
                if (ImGui::Checkbox("Top (+Y)", &fTop))     { obj->sflodFaceMask ^= (1 << 4); } ImGui::SameLine();
                if (ImGui::Checkbox("Bottom (-Y)", &fBot))  { obj->sflodFaceMask ^= (1 << 5); }

                if (obj->sflodGeometryMode != 4) {
                    ImGui::TreePop();
                }
            }

            // Shader Complexity
            const char* complexityOptions[] = {
                "Low (Diffuse + Alpha Cutout)",
                "Medium (+Normal Atlas)",
                "High (+Normal + RMA Atlas)"
            };
            int curComplexity = glm::clamp(obj->sflodShaderComplexity, 0, 2);
            if (ImGui::Combo("Shader Complexity", &curComplexity, complexityOptions, 3)) {
                obj->sflodShaderComplexity = curComplexity;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Material channel complexity:\n- Low: 1 Atlas (Base Color + Alpha Mask)\n- Medium: 2 Atlases (+Normal Map Atlas)\n- High: 3 Atlases (+Roughness, Metallic, AO Atlas)");
            }

            // Dynamic LOD Swapping: Distance Threshold (imp_threshold)
            ImGui::DragFloat("Distance Threshold (imp_threshold)", &obj->sflodDistanceThreshold, 0.5f, 0.0f, 500.0f, "%.1f m");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Superhive / Nacho Time Distance Threshold (imp_threshold) in meters:\n"
                                  "When camera distance is >= this threshold, the 3D mesh is automatically swapped with the imposter proxy.\n"
                                  "(Set to 0 to disable distance trigger and rely solely on the independent 20-level SFLOD system).");
            }

            // Independent 20-Level SFLOD Controls (Completely separate from Mesh LOD)
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Independent SFLOD System (20 LOD Levels)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("SFLOD operates completely independently of Mesh LOD.\nEven if Mesh LOD is disabled or the mesh only has LOD 0, SFLOD evaluates\ncontinuous screen-space coverage across 20 virtual LOD levels.");
            }

            ImGui::SliderInt("Trigger SFLOD Level", &obj->sflodMinLOD, 0, 20);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("The SFLOD level threshold (>=) at which the 3D mesh transitions to the Imposter Card (0 to 20).\nExample: 3 means SFLOD LOD 3 through 20 will render as an imposter card.\nSetting 0 triggers imposter card immediately at all distances.");
            }

            ImGui::SliderInt("Max SFLOD Level", &obj->sflodMaxLOD, 1, 20);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Maximum SFLOD LOD level ceiling (1 to 20).");
            }

            ImGui::SliderFloat("SFLOD LOD Bias", &obj->sflodLODBias, -20.0f, 20.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Independent LOD Bias for SFLOD (-20.0 to +20.0).\nPositive values increase SFLOD level sooner (earlier transition to imposter).\nNegative values decrease SFLOD level (delays transition).");
            }

            // Billboard Mode (only relevant for Single Plane)
            if (obj->sflodGeometryMode == 0) {
                const char* bbModes[] = {
                    "Camera Facing (Spherical)",
                    "Locked Y-Axis (Axial / Nacho Time)",
                    "Fixed Plane (Object Local)"
                };
                int curBbMode = glm::clamp(obj->sflodBillboardMode, 0, 2);
                if (ImGui::Combo("Billboard Mode", &curBbMode, bbModes, 3)) {
                    obj->sflodBillboardMode = curBbMode;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Orientation mode of the single-plane card:\n"
                                      "- Camera Facing (Spherical): rotates in all directions towards camera.\n"
                                      "- Locked Y-Axis (Axial / Nacho Time): classic foliage mode that stays vertically upright.\n"
                                      "- Fixed Plane: fixed local orientation of the object.");
                }
            }

            // Card Dimensions
            bool isAutoDim = (obj->sflodPlaneWidth <= 0.001f && obj->sflodPlaneHeight <= 0.001f);
            if (ImGui::Checkbox("Auto Dimensions (Mesh Bounds)", &isAutoDim)) {
                if (isAutoDim) {
                    obj->sflodPlaneWidth = 0.0f;
                    obj->sflodPlaneHeight = 0.0f;
                } else {
                    float w = 1.0f, h = 1.0f;
                    glm::vec3 off{0.0f};
                    SFLOD::SFLODSystem::Get().ComputeCardBounds(*obj, w, h, off);
                    obj->sflodPlaneWidth = w;
                    obj->sflodPlaneHeight = h;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Automatically calculates card width and height from the mesh bounding box.");
            }

            if (isAutoDim) {
                ImGui::BeginDisabled(true);
            }
            float dims[2] = { obj->sflodPlaneWidth, obj->sflodPlaneHeight };
            if (ImGui::DragFloat2("Custom Size (W x H)", dims, 0.05f, 0.0f, 100.0f, "%.2f")) {
                obj->sflodPlaneWidth = dims[0];
                obj->sflodPlaneHeight = dims[1];
            }
            if (isAutoDim) {
                ImGui::EndDisabled();
            }

            // Center Offset
            ImGui::DragFloat2("Center Offset (X, Y)", &obj->sflodPlaneOffset.x, 0.05f, -50.0f, 50.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Fine-tunes the horizontal and vertical center positioning of the imposter card.");
            }

            // Texture & Cutout
            ImGui::Checkbox("Alpha Cutout Mask", &obj->sflodCutout);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Uses hardware alpha masking (blendMode = Masked) so transparent card regions are clipped out.");
            }

            // Atlas Status & Custom Texture Path
            if (obj->sflodIsAtlas || obj->sflodCustomTexture.find("_Atlas") != std::string::npos) {
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "[6-WAY ORTHOGONAL ATLAS ACTIVE]");
            }
            char texBuf[256];
            memset(texBuf, 0, sizeof(texBuf));
            strncpy_s(texBuf, obj->sflodCustomTexture.c_str(), sizeof(texBuf) - 1);
            if (ImGui::InputText("Card Texture", texBuf, sizeof(texBuf))) {
                obj->sflodCustomTexture = texBuf;
                obj->sflodIsAtlas = (obj->sflodCustomTexture.find("_Atlas") != std::string::npos);
            }
            if (obj->sflodCustomTexture.empty()) {
                ImGui::TextDisabled("  (Empty = Click 'Generate Imposter Cards' above or uses base material)");
            }

            // Advanced Baking Options
            if (ImGui::TreeNode("Advanced Projection Baking Options")) {
                const char* resOptions[] = { "256 x 256", "512 x 512 (Recommended)", "1024 x 1024" };
                int curResOpt = (s_imposterResolution == 256) ? 0 : (s_imposterResolution == 1024 ? 2 : 1);
                if (ImGui::Combo("Atlas Tile Resolution", &curResOpt, resOptions, 3)) {
                    if (curResOpt == 0) s_imposterResolution = 256;
                    else if (curResOpt == 2) s_imposterResolution = 1024;
                    else s_imposterResolution = 512;
                }

                if (ImGui::Button("Capture Single View Texture", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                    std::string bakedPath = SFLOD::SFLODSystem::Get().BakeAndSaveImposterTexture(
                        *obj, modelMat, camEye, contentDir, -1, s_imposterResolution);
                    if (!bakedPath.empty()) {
                        AddLog("LogSFLOD", "Successfully baked view-dependent imposter texture: " + bakedPath, 0);
                    }
                }

                if (ImGui::Button("Bake 8-Directional View Atlas", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                    bool ok = SFLOD::SFLODSystem::Get().BakeAll8Angles(*obj, modelMat, contentDir, s_imposterResolution);
                    if (ok) {
                        AddLog("LogSFLOD", "Successfully baked 8-directional imposter series into Content/Imposters/", 0);
                    }
                }
                ImGui::TreePop();
            }

            // Real-Time Telemetry & Status
            ImGui::Spacing();
            ImGui::Separator();
            glm::vec3 worldPos = glm::vec3(modelMat[3]);
            float camDist = glm::length(camEye - worldPos);
            bool distTriggered = (obj->sflodDistanceThreshold > 0.0f && camDist >= obj->sflodDistanceThreshold);
            bool lodTriggered = (obj->sflodCurrentLOD >= obj->sflodMinLOD);
            bool previewTriggered = (obj->forcedLOD >= 0 && obj->forcedLOD >= obj->sflodMinLOD);
            bool isCardActive = obj->enableSFLOD && (distTriggered || lodTriggered || previewTriggered);

            uint32_t meshTris = (uint32_t)obj->mesh.indices.size() / 3;
            uint32_t cardTris = 2;
            const char* geomName = "1-Face Parallax Card";
            if (obj->sflodGeometryMode == 1) {
                cardTris = 6;
                geomName = "Tri-Planar Nacho Mode";
            } else if (obj->sflodGeometryMode == 2) {
                cardTris = 10;
                geomName = "5-Face Open Box";
            } else if (obj->sflodGeometryMode == 3) {
                cardTris = 12;
                geomName = "6-Face Closed Cube";
            } else if (obj->sflodGeometryMode == 4) {
                int activeCount = 0;
                for (int b = 0; b < 6; ++b) { if (obj->sflodFaceMask & (1 << b)) activeCount++; }
                cardTris = activeCount * 2;
                geomName = "Custom Multi-Face Box";
            }
            float reduction = meshTris > cardTris ? ((1.0f - (float)cardTris / (float)meshTris) * 100.0f) : 0.0f;

            if (isCardActive) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[ACTIVE: %s (%u Tris)]", geomName, cardTris);
                ImGui::Text("Saved Triangles: %u Tris (-%.2f%% reduction)", meshTris > cardTris ? meshTris - cardTris : 0, reduction);
                if (previewTriggered) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Triggered by Viewport Preview Lock (Forced SFLOD: %d)", obj->forcedLOD);
                } else if (distTriggered) {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Triggered by Distance: %.1fm >= %.1fm", camDist, obj->sflodDistanceThreshold);
                } else if (lodTriggered) {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Triggered by SFLOD Level: LOD %d >= %d", obj->sflodCurrentLOD, obj->sflodMinLOD);
                }
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[INACTIVE: Rendering 3D Mesh (%u Tris)]", meshTris);
                ImGui::TextDisabled("Distance: %.1fm (Thresh: %.1fm) | SFLOD: LOD %d/20 (Trigger: %d) | Mesh LOD: %s",
                    camDist, obj->sflodDistanceThreshold, obj->sflodCurrentLOD, obj->sflodMinLOD,
                    obj->enableLOD ? "Active" : "Disabled");
            }

            // Quick Preview Button
            bool isPreviewing = (obj->forcedLOD >= 0 && obj->forcedLOD >= obj->sflodMinLOD);
            if (ImGui::Button(isPreviewing ? "Exit Imposter Preview" : "Preview Imposter in Viewport", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                if (isPreviewing) {
                    obj->forcedLOD = -1;
                } else {
                    obj->forcedLOD = obj->sflodMinLOD;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Temporarily locks this object to its SFLOD proxy so you can preview it directly in the viewport.");
            }

            if (!obj->enableSFLOD) {
                ImGui::EndDisabled();
            }
        }
        // 4.8 Modifiers Section (includes Collisions and Physics modifiers)
        if (!obj->isLight && !obj->isCamera && !IsLightPrimitive(obj->type) && !IsCameraPrimitive(obj->type)) {
            char modHeaderTitle[64];
            int totalMods = (int)obj->modifiers.size() + (obj->hasPhysics ? 1 : 0) + (obj->hasAnimator ? 1 : 0) + (obj->hasLevelAnimation ? 1 : 0);
            snprintf(modHeaderTitle, sizeof(modHeaderTitle), "Modifiers (%d)###ModifiersSection", totalMods);
            if (ImGui::CollapsingHeader(modHeaderTitle, ImGuiTreeNodeFlags_DefaultOpen)) {
                // Add Modifier button and popup
                if (ImGui::Button("Add Modifier", ImVec2(ImGui::GetContentRegionAvail().x, 28.0f))) {
                    ImGui::OpenPopup("AddModifierPopup");
                }

                if (ImGui::BeginPopup("AddModifierPopup")) {
                    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "COLLISION MODIFIERS");
                    ImGui::Separator();
                    if (ImGui::MenuItem("Box Collision")) {
                        obj->AddModifier(CollisionShapeType::Box);
                        AddLog("LogPhysics", "Added Box Collision to " + obj->name, 0);
                    }
                    if (ImGui::MenuItem("Sphere Collision")) {
                        obj->AddModifier(CollisionShapeType::Sphere);
                        AddLog("LogPhysics", "Added Sphere Collision to " + obj->name, 0);
                    }
                    if (ImGui::MenuItem("Capsule Collision")) {
                        obj->AddModifier(CollisionShapeType::Capsule);
                        AddLog("LogPhysics", "Added Capsule Collision to " + obj->name, 0);
                    }
                    if (ImGui::MenuItem("Cylinder Collision")) {
                        obj->AddModifier(CollisionShapeType::Cylinder);
                        AddLog("LogPhysics", "Added Cylinder Collision to " + obj->name, 0);
                    }
                    if (ImGui::MenuItem("Cone Collision")) {
                        obj->AddModifier(CollisionShapeType::Cone);
                        AddLog("LogPhysics", "Added Cone Collision to " + obj->name, 0);
                    }
                    if (ImGui::MenuItem("Plane Collision")) {
                        obj->AddModifier(CollisionShapeType::Plane);
                        AddLog("LogPhysics", "Added Plane Collision to " + obj->name, 0);
                    }
                    if (ImGui::MenuItem("Mesh Collision")) {
                        obj->AddModifier(CollisionShapeType::Mesh);
                        AddLog("LogPhysics", "Added Mesh Collision to " + obj->name, 0);
                    }
                    if (ImGui::MenuItem("Character Collision")) {
                        obj->AddModifier(CollisionShapeType::Character);
                        AddLog("LogPhysics", "Added Character Collision to " + obj->name, 0);
                    }
                    if (ImGui::MenuItem("Terrain Collision")) {
                        obj->AddModifier(CollisionShapeType::Terrain);
                        AddLog("LogPhysics", "Added Terrain Collision to " + obj->name, 0);
                    }

                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "PHYSICS MODIFIERS");
                    ImGui::Separator();
                    if (!obj->hasPhysics) {
                        if (ImGui::MenuItem("Character Physics")) {
                            obj->AddCharacterPhysicsModifier();
                            AddLog("LogPhysics", "Added Character Physics to " + obj->name, 0);
                        }
                        if (ImGui::MenuItem("Physics Modifier")) {
                            obj->AddPhysicsModifier();
                            AddLog("LogPhysics", "Added Physics Modifier to " + obj->name, 0);
                        }
                    } else {
                        ImGui::TextDisabled("Physics Modifier already added");
                    }

                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.6f, 0.4f, 1.0f, 1.0f), "ANIMATION MODIFIERS");
                    ImGui::Separator();
                    if (!obj->hasAnimator) {
                        if (ImGui::MenuItem("Animation Graph")) {
                            obj->AddAnimatorModifier();
                            AddLog("LogAnimation", "Added Animation Graph modifier to " + obj->name, 0);
                        }
                    } else {
                        ImGui::TextDisabled("Animation Graph already added");
                    }
                    if (!obj->hasLevelAnimation) {
                        if (ImGui::MenuItem("Level Animation")) {
                            obj->AddLevelAnimation();
                            AddLog("LogLevelAnimation", "Added Level Animation component to " + obj->name, 0);
                        }
                    } else {
                        ImGui::TextDisabled("Level Animation already added");
                    }

                    ImGui::EndPopup();
                }

                if (obj->modifiers.empty() && !obj->hasPhysics && !obj->hasAnimator) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("No modifiers attached to this object.");
                    ImGui::TextDisabled("Click '+ Add Modifier' to add collision or physics modifiers.");
                    ImGui::Spacing();
                } else {
                    int modifierToRemove = -1;

                    // 1. Render Collision Modifiers
                    for (size_t mi = 0; mi < obj->modifiers.size(); ++mi) {
                        auto& mod = obj->modifiers[mi];
                        ImGui::PushID((int)mi);

                        ImGui::Spacing();
                        ImGui::Separator();

                        // Header with enabled toggle, name, and remove button
                        ImGui::Checkbox("##ModEnabled", &mod.enabled);
                        ImGui::SameLine();

                        char modHeaderLabel[128];
                        snprintf(modHeaderLabel, sizeof(modHeaderLabel), "%s##Header%d", mod.name.c_str(), (int)mi);
                        bool isModOpen = ImGui::TreeNodeEx(modHeaderLabel, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth);

                        ImGui::SameLine(ImGui::GetContentRegionAvail().x + 20.0f);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 0.8f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                        if (ImGui::SmallButton("X##RemoveMod")) {
                            modifierToRemove = (int)mi;
                        }
                        ImGui::PopStyleColor(2);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Remove this collision modifier");
                        }

                        if (isModOpen) {
                            ImGui::Indent(8.0f);

                            // Modifier Name edit
                            char nameBuf[128];
                            strncpy(nameBuf, mod.name.c_str(), sizeof(nameBuf));
                            nameBuf[sizeof(nameBuf) - 1] = '\0';
                            if (ImGui::InputText("Modifier Name", nameBuf, sizeof(nameBuf))) {
                                mod.name = nameBuf;
                            }

                            // Shape Type selector
                            const char* shapeNames[] = {
                                "None", "Box Collision", "Sphere Collision", "Capsule Collision",
                                "Cylinder Collision", "Cone Collision", "Plane Collision", "Mesh Collision",
                                "Character Collision"
                            };
                            int currentShapeIdx = (int)mod.shapeType;
                            if (ImGui::Combo("Shape", &currentShapeIdx, shapeNames, 9)) {
                                mod.shapeType = (CollisionShapeType)currentShapeIdx;
                                mod.isDefaultFit = false;
                            }

                            ImGui::Checkbox("Is Trigger", &mod.isTrigger);

                            // Center Offset
                            ImGui::DragFloat3("Center Offset", &mod.centerOffset.x, 0.02f, -100.0f, 100.0f, "%.2f");

                            // Shape-specific extents
                            switch (mod.shapeType) {
                                case CollisionShapeType::Box:
                                case CollisionShapeType::Mesh:
                                    ImGui::DragFloat3("Half-Extents", &mod.boxExtents.x, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    break;

                                case CollisionShapeType::Sphere:
                                    ImGui::DragFloat("Radius", &mod.radius, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    break;

                                case CollisionShapeType::Capsule:
                                    ImGui::DragFloat("Radius", &mod.radius, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    ImGui::DragFloat("Height", &mod.height, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    break;

                                case CollisionShapeType::Character:
                                    ImGui::DragFloat("Radius", &mod.radius, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    ImGui::DragFloat("Height", &mod.height, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    if (ImGui::Checkbox("Prevent Side Fall", &mod.preventSideFall)) {
                                        mod.sideFall = !mod.preventSideFall;
                                    }
                                    if (ImGui::IsItemHovered()) {
                                        ImGui::SetTooltip("When enabled (default), keeps character upright and prevents falling in cross/side directions during physics simulation. Disable to allow side falling.");
                                    }
                                    break;

                                case CollisionShapeType::Cylinder:
                                    ImGui::DragFloat("Radius", &mod.radius, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    ImGui::DragFloat("Height", &mod.height, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    break;

                                case CollisionShapeType::Cone:
                                    ImGui::DragFloat("Radius", &mod.radius, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    ImGui::DragFloat("Height", &mod.height, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    break;

                                case CollisionShapeType::Plane:
                                    ImGui::DragFloat2("Plane Size", &mod.planeSize.x, 0.05f, 0.1f, 10000.0f, "%.2f");
                                    break;

                                case CollisionShapeType::Terrain:
                                    ImGui::DragFloat3("Box Bounds", &mod.boxExtents.x, 0.02f, 0.01f, 1000.0f, "%.2f");
                                    ImGui::DragFloat("Height Offset", &mod.terrainOffsetHeight, 0.05f, -100.0f, 100.0f, "%.2f m");
                                    ImGui::SliderFloat("Follow Terrain Tilt", &mod.terrainFollowTilt, 0.0f, 1.0f, "%.2f (Blend)");
                                    ImGui::Checkbox("Dynamic Surface Stick", &mod.terrainStickToSurface);
                                    break;

                                default:
                                    break;
                            }

                            // Physics Presets & Properties
                            const char* presets[] = { "Default", "BlockAll", "OverlapAll", "TriggerOnly", "NoCollision" };
                            int curPresetIdx = 0;
                            for (int p = 0; p < 5; ++p) {
                                if (mod.preset == presets[p]) { curPresetIdx = p; break; }
                            }
                            if (ImGui::Combo("Preset", &curPresetIdx, presets, 5)) {
                                mod.ApplyPreset(presets[curPresetIdx]);
                            }

                            ImGui::SliderFloat("Friction", &mod.friction, 0.0f, 1.0f, "%.2f");
                            ImGui::SliderFloat("Restitution", &mod.restitution, 0.0f, 1.0f, "%.2f");

                            // Visualization
                            ImGui::Checkbox("Show Wireframe", &mod.showWireframe);
                            ImGui::SameLine();
                            ImGui::ColorEdit4("Color##ModWireColor", &mod.wireframeColor.x, ImGuiColorEditFlags_NoInputs);

                            // Auto-fit & Reset buttons
                            if (ImGui::Button("Auto-Fit", ImVec2(ImGui::GetContentRegionAvail().x * 0.48f, 24.0f))) {
                                Collisions::CollisionSystem::Get().AutoFitModifier(*obj, mod);
                                AddLog("LogPhysics", "Auto-fitted bounds for " + mod.name, 0);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Reset Defaults", ImVec2(ImGui::GetContentRegionAvail().x, 24.0f))) {
                                Collisions::CollisionSystem::Get().ResetModifierToDefault(*obj, mod);
                                AddLog("LogPhysics", "Reset " + mod.name + " to defaults", 0);
                            }

                            ImGui::Unindent(8.0f);
                            ImGui::TreePop();
                        }

                        ImGui::PopID();
                    }

                    if (modifierToRemove >= 0 && modifierToRemove < (int)obj->modifiers.size()) {
                        AddLog("LogPhysics", "Removed modifier: " + obj->modifiers[modifierToRemove].name, 0);
                        obj->RemoveModifier((size_t)modifierToRemove);
                    }

                    // 2. Render Physics Modifier (if attached)
                    if (obj->hasPhysics) {
                        ImGui::PushID("PhysicsModifierCard");
                        ImGui::Spacing();
                        ImGui::Separator();

                        // Header with enabled toggle, name, and remove button
                        ImGui::Checkbox("##PhysModEnabled", &obj->physics.enabled);
                        ImGui::SameLine();

                        bool isCharPhys = obj->physics.isCharacterPhysics;
                        std::string physHeader = isCharPhys ? "Character Physics##PhysHeader" : "Physics Modifier##PhysHeader";
                        bool isPhysOpen = ImGui::TreeNodeEx(physHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth);

                        ImGui::SameLine(ImGui::GetContentRegionAvail().x + 20.0f);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 0.8f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                        if (ImGui::SmallButton("X##RemovePhysMod")) {
                            obj->RemovePhysicsModifier();
                            AddLog("LogPhysics", "Removed Physics Modifier from " + obj->name, 0);
                        }
                        ImGui::PopStyleColor(2);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Remove Physics Modifier");
                        }

                        if (isPhysOpen) {
                            ImGui::Indent(8.0f);

                            if (isCharPhys) {
                                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.4f, 1.0f), "Character Dynamics Active (Zero Bounce, Stop-on-Collision)");
                                ImGui::Spacing();
                            }

                            // Gravity properties
                            ImGui::Checkbox("Use Gravity", &obj->physics.useGravity);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("When enabled, gravitational acceleration pulls this object downward during Play Mode.");
                            }

                            if (obj->physics.useGravity) {
                                ImGui::DragFloat("Gravity Multiplier", &obj->physics.gravityMultiplier, 0.05f, 0.0f, 100.0f, "%.2fx");
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Multiplies global gravity (e.g. 1.0 = normal 9.81 m/sÃƒâ€šÃ‚Â², 2.0 = double gravity).");
                                }
                            }

                            // Mass property
                            ImGui::DragFloat("Mass (kg)", &obj->physics.mass, 0.1f, 0.01f, 100000.0f, "%.2f kg");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Mass of the object in kilograms. Affects inertia and collision momentum transfer.");
                            }

                            // Dynamics & Drag
                            ImGui::DragFloat("Linear Damping (Drag)", &obj->physics.linearDamping, 0.005f, 0.0f, 1.0f, "%.3f");
                            ImGui::DragFloat("Angular Damping", &obj->physics.angularDamping, 0.005f, 0.0f, 1.0f, "%.3f");

                            // Restitution & Friction
                            if (isCharPhys) {
                                ImGui::TextDisabled("Bounciness: 0.00 (Zero bounce for character)");
                                obj->physics.bounciness = 0.0f;
                            } else {
                                ImGui::SliderFloat("Bounciness", &obj->physics.bounciness, 0.0f, 1.0f, "%.2f");
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Restitution: 0.0 = no bounce, 1.0 = superball.");
                                }
                            }
                            ImGui::SliderFloat("Friction", &obj->physics.friction, 0.0f, 1.0f, "%.2f");
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Surface friction coefficient.");
                            }

                            // Kinematic toggle
                            ImGui::Checkbox("Is Kinematic", &obj->physics.isKinematic);
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("When true, object transform is driven manually/by scripts and ignores physics forces.");
                            }

                            // Constraints
                            ImGui::Spacing();
                            ImGui::TextDisabled("Freeze Position:");
                            ImGui::SameLine();
                            ImGui::Checkbox("X##FrzX", &obj->physics.lockPositionX);
                            ImGui::SameLine();
                            ImGui::Checkbox("Y##FrzY", &obj->physics.lockPositionY);
                            ImGui::SameLine();
                            ImGui::Checkbox("Z##FrzZ", &obj->physics.lockPositionZ);

                            if (isCharPhys) {
                                ImGui::TextDisabled("Freeze Rotation: X [Locked]  Y [Free]  Z [Locked] (Upright)");

                                ImGui::Spacing();
                                ImGui::Separator();
                                ImGui::Text("Character Collision Impulse");
                                ImGui::Checkbox("Enable Step-Back Impulse##CharImpulse", &obj->physics.enableStepBackImpulse);
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("When enabled, applies a slight impulse when moving back away from an obstacle contact.");
                                }
                                if (obj->physics.enableStepBackImpulse) {
                                    ImGui::DragFloat("Impulse Multiplier##CharImpMult", &obj->physics.stepBackImpulseMultiplier, 0.05f, 0.1f, 10.0f, "%.2fx");
                                }
                            } else {
                                ImGui::TextDisabled("Freeze Rotation:");
                                ImGui::SameLine();
                                ImGui::Checkbox("X##FrzRotX", &obj->physics.lockRotationX);
                                ImGui::SameLine();
                                ImGui::Checkbox("Y##FrzRotY", &obj->physics.lockRotationY);
                                ImGui::SameLine();
                                ImGui::Checkbox("Z##FrzRotZ", &obj->physics.lockRotationZ);
                            }

                            // Runtime Debug info
                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Simulation Info:");
                            ImGui::Text("  Velocity: (%.2f, %.2f, %.2f) m/s", obj->physics.velocity.x, obj->physics.velocity.y, obj->physics.velocity.z);
                            ImGui::Text("  Grounded: %s", obj->physics.isGrounded ? "Yes" : "No");

                            if (ImGui::Button("Reset Velocity", ImVec2(ImGui::GetContentRegionAvail().x, 24.0f))) {
                                obj->physics.ResetVelocity();
                                AddLog("LogPhysics", "Reset velocity for " + obj->name, 0);
                            }

                            ImGui::Unindent(8.0f);
                            ImGui::TreePop();
                        }

                        ImGui::PopID();
                    }

                    // 3. Render Animation Graph Modifier (if attached)
                    if (obj->hasAnimator) {
                        ImGui::PushID("AnimationGraphModifierCard");
                        ImGui::Spacing();
                        ImGui::Separator();

                        ImGui::Checkbox("##AnimModEnabled", &obj->animator.enabled);
                        ImGui::SameLine();

                        bool isAnimOpen = ImGui::TreeNodeEx("Animation Graph##AnimHeader", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth);

                        ImGui::SameLine(ImGui::GetContentRegionAvail().x + 20.0f);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 0.8f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                        if (ImGui::SmallButton("X##RemoveAnimMod")) {
                            obj->RemoveAnimatorModifier();
                            AddLog("LogAnimation", "Removed Animation Graph modifier from " + obj->name, 0);
                        }
                        ImGui::PopStyleColor(2);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Remove Animation Graph modifier");
                        }

                        if (isAnimOpen) {
                            ImGui::Indent(8.0f);

                            // Project .eanimator file picker dropdown
                            std::string previewGraphName = obj->animator.graphPath.empty() ? "(None - Select Animation Graph)" : std::filesystem::path(obj->animator.graphPath).filename().string();
                            if (ImGui::BeginCombo("Graph##AnimGraphSelector", previewGraphName.c_str())) {
                                if (ImGui::Selectable("(None)", obj->animator.graphPath.empty())) {
                                    obj->animator.LoadGraph("");
                                }
                                ImGui::Separator();

                                std::vector<std::string> animGraphs;
                                std::error_code ec;
                                std::filesystem::path contentDir = contentRootPath;
                                if (std::filesystem::exists(contentDir, ec)) {
                                    for (const auto& entry : std::filesystem::recursive_directory_iterator(contentDir, ec)) {
                                        if (entry.is_regular_file()) {
                                            std::string ext = entry.path().extension().string();
                                            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                            if (ext == ".eanimator" || ext == ".animator") {
                                                std::string rel = std::filesystem::relative(entry.path(), contentDir, ec).string();
                                                animGraphs.push_back(rel);
                                            }
                                        }
                                    }
                                }

                                for (const auto& gPath : animGraphs) {
                                    bool isSel = (obj->animator.graphPath == gPath || obj->animator.graphPath.find(gPath) != std::string::npos);
                                    if (ImGui::Selectable(gPath.c_str(), isSel)) {
                                        std::filesystem::path fullP = contentRootPath / gPath;
                                        obj->animator.LoadGraph(fullP.string());
                                        AddLog("LogAnimation", "Assigned Animation Graph: " + gPath + " to " + obj->name, 0);
                                    }
                                }
                                ImGui::EndCombo();
                            }

                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                    std::string droppedPath((const char*)payload->Data);
                                    if (droppedPath.find(".eanimator") != std::string::npos || droppedPath.find(".animator") != std::string::npos) {
                                        std::filesystem::path fullP = contentRootPath / droppedPath;
                                        obj->animator.LoadGraph(fullP.string());
                                        AddLog("LogAnimation", "Dropped Animation Graph: " + droppedPath + " onto " + obj->name, 0);
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }

                            // State & telemetry
                            if (obj->animator.instance && obj->animator.graph) {
                                std::string curState = obj->animator.GetCurrentStateName();
                                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.5f, 1.0f), "Active State: %s", curState.c_str());
                                if (obj->animator.IsTransitioning()) {
                                    ImGui::SameLine();
                                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[Transitioning...]");
                                }

                                if (!obj->animator.instance->runtimeParams.empty()) {
                                    if (ImGui::TreeNodeEx("Live Parameters##AnimLiveParams", ImGuiTreeNodeFlags_None)) {
                                        for (auto& pair : obj->animator.instance->runtimeParams) {
                                            auto& p = pair.second;
                                            if (p.type == Eunoia::AnimatorParamType::Boolean) {
                                                ImGui::Checkbox(p.name.c_str(), &p.boolValue);
                                            } else if (p.type == Eunoia::AnimatorParamType::Float) {
                                                ImGui::DragFloat(p.name.c_str(), &p.floatValue, 0.1f);
                                            } else if (p.type == Eunoia::AnimatorParamType::Int) {
                                                ImGui::DragInt(p.name.c_str(), &p.intValue);
                                            } else if (p.type == Eunoia::AnimatorParamType::String) {
                                                char sbuf[128];
                                                strncpy_s(sbuf, sizeof(sbuf), p.stringValue.c_str(), sizeof(sbuf) - 1);
                                                if (ImGui::InputText(p.name.c_str(), sbuf, sizeof(sbuf))) {
                                                    p.stringValue = sbuf;
                                                }
                                            }
                                        }
                                        ImGui::TreePop();
                                    }
                                }
                            } else if (!obj->animator.graphPath.empty()) {
                                ImGui::TextDisabled("Graph assigned: %s", obj->animator.graphPath.c_str());
                            } else {
                                ImGui::TextDisabled("Select an Animation Graph from dropdown or drag from Content Browser.");
                            }

                            ImGui::Unindent(8.0f);
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                }
            }
        }

        // 5. Actor Behavior
        if (ImGui::CollapsingHeader("Actor Behavior")) {
            ImGui::Checkbox("Auto-Rotate (Movable Only)", &obj->autoRotate);
            if (obj->autoRotate) {
                ImGui::DragFloat3("Speed (deg/s)", &obj->autoRotateSpeed.x, 1.0f, -360.0f, 360.0f);
            }
        }

        // 6. Behaviours (dev.md Section 6, 7, 8, 9, 10, 11, 12, 13)
        RenderBehavioursSection(scene, obj);

        ImGui::PopID();
    }
    ImGui::End();
}

std::vector<TextureAssetEntry> EngineUI::ScanProjectTextures() {
    std::vector<TextureAssetEntry> textures;
    auto regTextures = AssetRegistry::Get().FindByType(AssetType::Texture);
    textures.reserve(regTextures.size());

    for (const auto& meta : regTextures) {
        TextureAssetEntry te;
        te.filename = std::filesystem::path(meta.sourcePath).filename().string();
        te.fullPath = (contentRootPath / meta.sourcePath).string();
        te.relPath = meta.sourcePath;
        te.relFolder = std::filesystem::path(meta.sourcePath).parent_path().generic_string();
        if (te.relFolder == ".") te.relFolder = "";
        te.assetId = meta.id;
        te.virtualPath = meta.virtualPath;
        textures.push_back(te);
    }

    std::sort(textures.begin(), textures.end(), [](const auto& a, const auto& b) {
        return a.virtualPath < b.virtualPath;
    });
    return textures;
}

bool EngineUI::DrawTextureSlot(const char* label, std::string& textureSlotValue, AssetID& textureSlotAssetId, const std::vector<TextureAssetEntry>& availableTextures) {
    bool changed = false;
    ImGui::PushID(label);

    ImGui::Text("%s", label);

    std::string previewText;
    if (textureSlotAssetId.IsValid()) {
        AssetMetadata* meta = AssetRegistry::Get().Find(textureSlotAssetId);
        if (meta) {
            previewText = meta->objectName + "  [" + meta->id.ToString().substr(0, 8) + "...]";
        } else {
            previewText = "[Missing: " + textureSlotAssetId.ToString().substr(0, 8) + "...]";
        }
    } else if (!textureSlotValue.empty()) {
        previewText = std::filesystem::path(textureSlotValue).filename().string();
    } else {
        previewText = "(None)";
    }

    float availW = ImGui::GetContentRegionAvail().x;
    float clearBtnW = 28.0f;
    float comboW = availW - clearBtnW - 8.0f;
    if (comboW < 80.0f) comboW = 80.0f;

    ImGui::SetNextItemWidth(comboW);
    std::string comboId = "##TexCombo_" + std::string(label);
    if (ImGui::BeginCombo(comboId.c_str(), previewText.c_str(), ImGuiComboFlags_HeightLarge)) {
        char searchId[64];
        snprintf(searchId, sizeof(searchId), "##TexSearch_%s", label);
        static std::map<std::string, std::string> textureSearchMap;
        std::string& currentSearch = textureSearchMap[label];
        char searchBuf[64];
        strncpy(searchBuf, currentSearch.c_str(), sizeof(searchBuf));
        searchBuf[sizeof(searchBuf) - 1] = '\0';

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputTextWithHint(searchId, "Search textures or AssetID...", searchBuf, sizeof(searchBuf))) {
            currentSearch = searchBuf;
        }
        ImGui::Separator();

        bool isNoneSelected = !textureSlotAssetId.IsValid() && textureSlotValue.empty();
        if (ImGui::Selectable("(None)", isNoneSelected)) {
            textureSlotValue = "";
            textureSlotAssetId = AssetID::Null();
            changed = true;
        }
        if (isNoneSelected) ImGui::SetItemDefaultFocus();
        ImGui::Separator();

        std::string searchLower = currentSearch;
        std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

        int count = 0;
        for (const auto& tex : availableTextures) {
            std::string nameLower = tex.filename;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            std::string virtLower = tex.virtualPath;
            std::transform(virtLower.begin(), virtLower.end(), virtLower.begin(), ::tolower);
            std::string idStr = tex.assetId.ToString();
            std::string idLower = idStr;
            std::transform(idLower.begin(), idLower.end(), idLower.begin(), ::tolower);

            if (!searchLower.empty() &&
                nameLower.find(searchLower) == std::string::npos &&
                virtLower.find(searchLower) == std::string::npos &&
                idLower.find(searchLower) == std::string::npos) {
                continue;
            }

            count++;
            ImGui::PushID(tex.fullPath.c_str());
            bool isSelected = (textureSlotAssetId == tex.assetId || textureSlotValue == tex.filename || textureSlotValue == tex.relPath || textureSlotValue == tex.virtualPath);

            std::string itemText = "" + tex.filename;
            if (!tex.virtualPath.empty()) {
                itemText += "  (" + tex.virtualPath + ")";
            }

            if (ImGui::Selectable(itemText.c_str(), isSelected)) {
                textureSlotValue = tex.filename;
                textureSlotAssetId = tex.assetId;
                changed = true;
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Virtual: %s\nAssetID: %s\nDisk: %s",
                    tex.virtualPath.c_str(), idStr.c_str(), tex.relPath.c_str());
            }
            ImGui::PopID();
        }

        if (count == 0 && !availableTextures.empty()) {
            ImGui::TextDisabled("No textures match \"%s\"", currentSearch.c_str());
        } else if (availableTextures.empty()) {
            ImGui::TextDisabled("No textures registered in Asset Registry");
        }

        ImGui::EndCombo();
    }

    // Drag & Drop Target for Texture Slot (dev.md Section 34)
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
            char assetIdBuf[40] = {};
            memcpy(assetIdBuf, payload->Data, std::min((size_t)payload->DataSize, sizeof(assetIdBuf) - 1));
            AssetID droppedId = AssetID::FromString(assetIdBuf);
            AssetMetadata* meta = AssetRegistry::Get().Find(droppedId);
            if (meta && meta->type == AssetType::Texture) {
                textureSlotAssetId = droppedId;
                textureSlotValue = std::filesystem::path(meta->sourcePath).filename().string();
                changed = true;
                AddLog("LogMaterial", "Assigned Texture '" + meta->objectName + "' (" + meta->id.ToString().substr(0, 8) + ") to slot", 2);
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    bool hasTex = textureSlotAssetId.IsValid() || !textureSlotValue.empty();
    if (!hasTex) ImGui::BeginDisabled();
    if (ImGui::Button("ÃƒÆ’Ã¢â‚¬â€", ImVec2(clearBtnW, 0))) {
        textureSlotValue = "";
        textureSlotAssetId = AssetID::Null();
        changed = true;
    }
    if (!hasTex) ImGui::EndDisabled();
    if (ImGui::IsItemHovered() && hasTex) {
        ImGui::SetTooltip("Clear texture slot");
    }

    if (hasTex && !textureSlotValue.empty()) {
        auto info = Eunoia::TextureStreamingSystem::Get().GetOrProbeTextureInfo(textureSlotValue);
        if (info.isValid) {
            const char* fmtName = DirectX::GetFormatName(info.format);
            std::string fmtStr = (fmtName && strlen(fmtName) > 0) ? fmtName : "DXGI_FORMAT_R8G8B8A8_UNORM";
            if (info.isGpuReady) {
                ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "   ÃƒÂ¢Ã¢â‚¬Â Ã‚Â³ %dx%d | %s | %u Mip%s",
                    info.width, info.height, fmtStr.c_str(), info.mipLevels, info.mipLevels > 1 ? "s" : "");
            } else {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "   ÃƒÂ¢Ã¢â‚¬Â Ã‚Â³ %dx%d | %s | %u Mip%s  [Streaming...]",
                    info.width, info.height, fmtStr.c_str(), info.mipLevels, info.mipLevels > 1 ? "s" : "");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Bake DDS##BakeSlotBtn")) {
                std::string resolved = TextureManager::Get().ResolvePath(textureSlotValue);
                if (!resolved.empty()) {
                    TextureUsageSlot slot = TextureUsageSlot::BaseColor;
                    std::string lbl = label ? label : "";
                    if (lbl.find("Normal") != std::string::npos) slot = TextureUsageSlot::Normal;
                    else if (lbl.find("Rough") != std::string::npos) slot = TextureUsageSlot::Roughness;
                    else if (lbl.find("Metal") != std::string::npos) slot = TextureUsageSlot::Metallic;
                    else if (lbl.find("Occlusion") != std::string::npos || lbl.find("AO") != std::string::npos) slot = TextureUsageSlot::AO;
                    else if (lbl.find("Emission") != std::string::npos) slot = TextureUsageSlot::Emission;
                    else if (lbl.find("Opacity") != std::string::npos) slot = TextureUsageSlot::Opacity;

                    CachedTexture* ct = TextureManager::Get().GetTexture(textureSlotValue, slot);
                    if (ct && ct->valid) {
                        std::filesystem::path p(resolved);
                        std::string ddsPath = p.replace_extension(".dds").string();
                        if (ct->scratchImage.GetImageCount() > 0) {
                            DirectX::SaveToDDSFile(ct->scratchImage.GetImages(), ct->scratchImage.GetImageCount(), ct->scratchImage.GetMetadata(), ddsPath.c_str());
                        } else if (!ct->data.empty()) {
                            DirectX::Image baseImg;
                            baseImg.width = ct->width; baseImg.height = ct->height;
                            baseImg.format = DXGI_FORMAT_R8G8B8A8_UNORM;
                            baseImg.rowPitch = ct->width * 4;
                            baseImg.slicePitch = ct->width * ct->height * 4;
                            baseImg.pixels = ct->data.data();
                            DirectX::ScratchImage mips;
                            DirectX::GenerateMipMaps(baseImg, DirectX::TEX_FILTER_BOX | DirectX::TEX_FILTER_SRGB, 0, mips);
                            DirectX::SaveToDDSFile(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), ddsPath.c_str());
                        }
                        AddLog("DirectXTex", "Exported optimized DDS texture with mipmaps: " + ddsPath, 0);
                    }
                }
            }
        } else {
            ImGui::TextDisabled("   ÃƒÂ¢Ã¢â‚¬Â Ã‚Â³ [File Not Found or Unreadable]");
        }
    }

    ImGui::Spacing();
    ImGui::PopID();
    return changed;
}

void EngineUI::ApplyMaterialToActorAndChildren(Scene& scene, GameObject* rootObj, const MaterialAsset& ma) {
    if (!rootObj) return;

    int childCount = 0;
    std::function<void(GameObject*)> applyRec = [&](GameObject* cur) {
        if (!cur) return;
        if (!cur->isLight && cur->type != PrimitiveType::Empty) {
            cur->materialName = ma.name;
            cur->color = ma.baseColor;
            cur->metallic = ma.metallic;
            cur->roughness = ma.roughness;
            cur->normalStrength = ma.normalStrength;
            cur->specular = ma.specular;
            cur->emissiveColor = ma.emissiveColor;
            cur->emissiveIntensity = ma.emissiveIntensity;
            cur->shadingModel = ma.shadingModel;
            cur->blendMode = ma.blendMode;
            cur->twoSided = ma.twoSided;
            cur->castShadows = ma.castShadows;
            cur->receiveShadows = ma.receiveShadows;
            cur->opacity = ma.opacity;
            cur->opacityMaskClipValue = ma.opacityMaskClipValue;
            cur->baseColorTexture = ma.baseColorTexture;
            cur->normalTexture = ma.normalTexture;
            cur->roughnessTexture = ma.roughnessTexture;
            cur->metallicTexture = ma.metallicTexture;
            cur->aoTexture = ma.aoTexture;
            cur->emissionTexture = ma.emissionTexture;
            cur->opacityTexture = ma.opacityTexture;
            cur->uvScale = ma.uvScale;
            cur->normalMapYFlip = ma.normalMapYFlip;
            cur->metallicChannel = ma.metallicChannel;
            cur->roughnessChannel = ma.roughnessChannel;
            cur->aoChannel = ma.aoChannel;
            cur->materialDebugMode = ma.materialDebugMode;
            cur->disableStreaming = ma.disableStreaming;
            if (cur != rootObj) {
                childCount++;
            }
        } else if (cur->type == PrimitiveType::Empty) {
            cur->materialName = ma.name;
        }

        for (int cId : cur->childIds) {
            GameObject* child = scene.FindObject(cId);
            if (child) {
                applyRec(child);
            }
        }
    };

    applyRec(rootObj);

    if (childCount > 0) {
        AddLog("LogMaterial", "Assigned material '" + ma.name + "' to Actor '" + rootObj->name + "' and " + std::to_string(childCount) + " child actor(s)", 2);
    } else {
        AddLog("LogMaterial", "Assigned material '" + ma.name + "' to Actor '" + rootObj->name + "'", 2);
    }
}

bool EngineUI::LoadMaterialFile(const std::string& path, MaterialAsset& outMat) {
    std::string actualPath = path;
    std::error_code ec;
    if (!std::filesystem::exists(actualPath, ec) || std::filesystem::file_size(actualPath, ec) == 0) {
        std::filesystem::path resolved = AssetManager::Get().ResolvePhysicalOrCookedPath(path);
        if (!resolved.empty() && std::filesystem::exists(resolved, ec) && std::filesystem::file_size(resolved, ec) > 0) {
            actualPath = resolved.string();
        }
    }
    std::ifstream file(actualPath);
    if (!file.is_open()) return false;
    outMat.filePath = path;
    outMat.name = std::filesystem::path(path).stem().string();

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        while (!key.empty() && isspace((unsigned char)key.back())) key.pop_back();
        while (!key.empty() && isspace((unsigned char)key.front())) key.erase(key.begin());
        while (!val.empty() && isspace((unsigned char)val.back())) val.pop_back();
        while (!val.empty() && isspace((unsigned char)val.front())) val.erase(val.begin());

        if (key == "assetId") outMat.assetId = AssetID::FromString(val);
        else if (key == "virtualPath") outMat.virtualPath = val;
        else if (key == "name") outMat.name = val;
        else if (key == "baseColor") {
            std::stringstream ss(val);
            ss >> outMat.baseColor.r >> outMat.baseColor.g >> outMat.baseColor.b;
        }
        else if (key == "metallic" || key == "metalness") {
            try { outMat.metallic = std::stof(val); } catch (...) {}
        }
        else if (key == "roughness") {
            try { outMat.roughness = std::stof(val); } catch (...) {}
        }
        else if (key == "normalStrength" || key == "normalMapStrength") {
            try { outMat.normalStrength = std::stof(val); } catch (...) {}
        }
        else if (key == "specular" || key == "reflectance") {
            try { outMat.specular = std::stof(val); } catch (...) {}
        }
        else if (key == "emissiveColor") {
            std::stringstream ss(val);
            ss >> outMat.emissiveColor.r >> outMat.emissiveColor.g >> outMat.emissiveColor.b;
        }
        else if (key == "emissiveIntensity" || key == "emissionPower") {
            try { outMat.emissiveIntensity = std::stof(val); } catch (...) {}
        }
        else if (key == "shadingModel" || key == "shader" || key == "shaderType") {
            try { outMat.shadingModel = std::stoi(val); } catch (...) {}
        }
        else if (key == "blendMode" || key == "userBlendMode") {
            try { outMat.blendMode = std::stoi(val); } catch (...) {}
        }
        else if (key == "twoSided" || key == "doubleSided") {
            outMat.twoSided = (val == "1" || val == "true");
        }
        else if (key == "castShadows" || key == "castShadow") {
            outMat.castShadows = (val == "1" || val == "true");
        }
        else if (key == "receiveShadows" || key == "receiveShadow") {
            outMat.receiveShadows = (val == "1" || val == "true");
        }
        else if (key == "uvScale" || key == "uvscale" || key == "tiling" || key == "texMulAdd") {
            std::stringstream ss(val);
            ss >> outMat.uvScale.x >> outMat.uvScale.y;
        }
        else if (key == "opacity") {
            try { outMat.opacity = std::stof(val); } catch (...) {}
        }
        else if (key == "opacityMaskClipValue" || key == "alphaRef") {
            try { outMat.opacityMaskClipValue = std::stof(val); } catch (...) {}
        }
        else if (key == "normalMapYFlip") {
            outMat.normalMapYFlip = (val == "1" || val == "true");
        }
        else if (key == "disableStreaming") {
            outMat.disableStreaming = (val == "1" || val == "true");
        }
        else if (key == "metallicChannel") {
            try { outMat.metallicChannel = std::stoi(val); } catch (...) {}
        }
        else if (key == "roughnessChannel") {
            try { outMat.roughnessChannel = std::stoi(val); } catch (...) {}
        }
        else if (key == "aoChannel") {
            try { outMat.aoChannel = std::stoi(val); } catch (...) {}
        }
        else if (key == "materialDebugMode") {
            try { outMat.materialDebugMode = std::stoi(val); } catch (...) {}
        }
        else if (key == "baseColorAssetId") outMat.baseColorAssetId = AssetID::FromString(val);
        else if (key == "normalAssetId") outMat.normalAssetId = AssetID::FromString(val);
        else if (key == "roughnessAssetId") outMat.roughnessAssetId = AssetID::FromString(val);
        else if (key == "metallicAssetId") outMat.metallicAssetId = AssetID::FromString(val);
        else if (key == "aoAssetId") outMat.aoAssetId = AssetID::FromString(val);
        else if (key == "emissionAssetId") outMat.emissionAssetId = AssetID::FromString(val);
        else if (key == "opacityAssetId") outMat.opacityAssetId = AssetID::FromString(val);
        else if (key == "baseColorTexture" || key == "baseColorMap") outMat.baseColorTexture = val;
        else if (key == "normalTexture" || key == "normalMap") outMat.normalTexture = val;
        else if (key == "roughnessTexture" || key == "roughnessMap") outMat.roughnessTexture = val;
        else if (key == "metallicTexture" || key == "metalnessMap") outMat.metallicTexture = val;
        else if (key == "aoTexture" || key == "occlusionMap") outMat.aoTexture = val;
        else if (key == "emissionTexture" || key == "emissiveMap") outMat.emissionTexture = val;
        else if (key == "opacityTexture" || key == "transparencyMap") outMat.opacityTexture = val;
        else if (key == "surfaceMap" || key == "surfacemap" || key == "surfaceTexture") {
            outMat.roughnessTexture = val;
            outMat.metallicTexture = val;
            outMat.aoTexture = val;
            outMat.aoChannel = 0;
            outMat.roughnessChannel = 1;
            outMat.metallicChannel = 2;
        }
    }

    // Resolve AssetID if not present
    if (!outMat.assetId.IsValid()) {
        AssetMetadata* meta = AssetRegistry::Get().FindByNameOrPath(path);
        if (meta) {
            outMat.assetId = meta->id;
            outMat.virtualPath = meta->virtualPath;
        }
    }

    // Resolve texture AssetIDs if missing
    auto resolveTexId = [&](const std::string& texName, AssetID& outId) {
        if (!outId.IsValid() && !texName.empty() && texName != "none") {
            AssetMetadata* m = AssetRegistry::Get().FindByNameOrPath(texName);
            if (m) outId = m->id;
        }
    };
    resolveTexId(outMat.baseColorTexture, outMat.baseColorAssetId);
    resolveTexId(outMat.normalTexture, outMat.normalAssetId);
    resolveTexId(outMat.roughnessTexture, outMat.roughnessAssetId);
    resolveTexId(outMat.metallicTexture, outMat.metallicAssetId);
    resolveTexId(outMat.aoTexture, outMat.aoAssetId);
    resolveTexId(outMat.emissionTexture, outMat.emissionAssetId);
    resolveTexId(outMat.opacityTexture, outMat.opacityAssetId);

    return true;
}

bool EngineUI::SaveMaterialFile(const std::string& path, const MaterialAsset& mat) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << "# Eunoia-Editor Material Asset\n";
    if (mat.assetId.IsValid()) {
        file << "assetId: " << mat.assetId.ToString() << "\n";
    }
    if (!mat.virtualPath.empty()) {
        file << "virtualPath: " << mat.virtualPath << "\n";
    }
    file << "name: " << mat.name << "\n";
    file << "baseColor: " << mat.baseColor.r << " " << mat.baseColor.g << " " << mat.baseColor.b << "\n";
    file << "metallic: " << mat.metallic << "\n";
    file << "roughness: " << mat.roughness << "\n";
    file << "normalStrength: " << mat.normalStrength << "\n";
    file << "specular: " << mat.specular << "\n";
    file << "emissiveColor: " << mat.emissiveColor.r << " " << mat.emissiveColor.g << " " << mat.emissiveColor.b << "\n";
    file << "emissiveIntensity: " << mat.emissiveIntensity << "\n";
    file << "shadingModel: " << mat.shadingModel << "\n";
    file << "blendMode: " << mat.blendMode << "\n";
    file << "twoSided: " << (mat.twoSided ? 1 : 0) << "\n";
    file << "castShadows: " << (mat.castShadows ? 1 : 0) << "\n";
    file << "receiveShadows: " << (mat.receiveShadows ? 1 : 0) << "\n";
    file << "opacity: " << mat.opacity << "\n";
    file << "opacityMaskClipValue: " << mat.opacityMaskClipValue << "\n";
    file << "uvScale: " << mat.uvScale.x << " " << mat.uvScale.y << "\n";
    file << "normalMapYFlip: " << (mat.normalMapYFlip ? 1 : 0) << "\n";
    file << "disableStreaming: " << (mat.disableStreaming ? 1 : 0) << "\n";
    file << "metallicChannel: " << mat.metallicChannel << "\n";
    file << "roughnessChannel: " << mat.roughnessChannel << "\n";
    file << "aoChannel: " << mat.aoChannel << "\n";
    file << "materialDebugMode: " << mat.materialDebugMode << "\n";
    if (mat.baseColorAssetId.IsValid()) file << "baseColorAssetId: " << mat.baseColorAssetId.ToString() << "\n";
    file << "baseColorTexture: " << mat.baseColorTexture << "\n";
    if (mat.normalAssetId.IsValid()) file << "normalAssetId: " << mat.normalAssetId.ToString() << "\n";
    file << "normalTexture: " << mat.normalTexture << "\n";
    if (mat.roughnessAssetId.IsValid()) file << "roughnessAssetId: " << mat.roughnessAssetId.ToString() << "\n";
    file << "roughnessTexture: " << mat.roughnessTexture << "\n";
    if (mat.metallicAssetId.IsValid()) file << "metallicAssetId: " << mat.metallicAssetId.ToString() << "\n";
    file << "metallicTexture: " << mat.metallicTexture << "\n";
    if (mat.aoAssetId.IsValid()) file << "aoAssetId: " << mat.aoAssetId.ToString() << "\n";
    file << "aoTexture: " << mat.aoTexture << "\n";
    if (mat.emissionAssetId.IsValid()) file << "emissionAssetId: " << mat.emissionAssetId.ToString() << "\n";
    file << "emissionTexture: " << mat.emissionTexture << "\n";
    if (mat.opacityAssetId.IsValid()) file << "opacityAssetId: " << mat.opacityAssetId.ToString() << "\n";
    file << "opacityTexture: " << mat.opacityTexture << "\n";

    // Update Asset Registry (dev.md Section 7, 13)
    if (mat.assetId.IsValid()) {
        AssetMetadata* meta = AssetRegistry::Get().Find(mat.assetId);
        if (meta) {
            meta->dependencies.clear();
            if (mat.baseColorAssetId.IsValid()) AssetRegistry::Get().AddDependency(mat.assetId, mat.baseColorAssetId);
            if (mat.normalAssetId.IsValid()) AssetRegistry::Get().AddDependency(mat.assetId, mat.normalAssetId);
            if (mat.roughnessAssetId.IsValid()) AssetRegistry::Get().AddDependency(mat.assetId, mat.roughnessAssetId);
            if (mat.metallicAssetId.IsValid()) AssetRegistry::Get().AddDependency(mat.assetId, mat.metallicAssetId);
            if (mat.aoAssetId.IsValid()) AssetRegistry::Get().AddDependency(mat.assetId, mat.aoAssetId);
            if (mat.emissionAssetId.IsValid()) AssetRegistry::Get().AddDependency(mat.assetId, mat.emissionAssetId);
            if (mat.opacityAssetId.IsValid()) AssetRegistry::Get().AddDependency(mat.assetId, mat.opacityAssetId);
        }
    }
    return true;
}

void EngineUI::PropagateMaterialChangesToScene(Scene& scene, const MaterialAsset& mat) {
    std::string matStem = !mat.filePath.empty() ? std::filesystem::path(mat.filePath).stem().string() : mat.name;
    bool anyChanged = false;

    for (auto& obj : scene.objects) {
        bool objMatched = (!mat.name.empty() && obj.materialName == mat.name) ||
                           (!mat.filePath.empty() && obj.materialName == mat.filePath) ||
                           (!matStem.empty() && obj.materialName == matStem);

        if (objMatched) {
            obj.color = mat.baseColor;
            obj.metallic = mat.metallic;
            obj.roughness = mat.roughness;
            obj.normalStrength = mat.normalStrength;
            obj.specular = mat.specular;
            obj.emissiveColor = mat.emissiveColor;
            obj.emissiveIntensity = mat.emissiveIntensity;
            obj.shadingModel = mat.shadingModel;
            obj.blendMode = mat.blendMode;
            obj.twoSided = mat.twoSided;
            obj.castShadows = mat.castShadows;
            obj.receiveShadows = mat.receiveShadows;
            obj.opacity = mat.opacity;
            obj.opacityMaskClipValue = mat.opacityMaskClipValue;
            obj.baseColorTexture = mat.baseColorTexture;
            obj.normalTexture = mat.normalTexture;
            obj.roughnessTexture = mat.roughnessTexture;
            obj.metallicTexture = mat.metallicTexture;
            obj.aoTexture = mat.aoTexture;
            obj.emissionTexture = mat.emissionTexture;
            obj.opacityTexture = mat.opacityTexture;
            obj.uvScale = mat.uvScale;
            obj.normalMapYFlip = mat.normalMapYFlip;
            obj.metallicChannel = mat.metallicChannel;
            obj.roughnessChannel = mat.roughnessChannel;
            obj.aoChannel = mat.aoChannel;
            obj.materialDebugMode = mat.materialDebugMode;
            obj.disableStreaming = mat.disableStreaming;
            anyChanged = true;
        }

        if (obj.isTerrain || obj.type == PrimitiveType::Terrain) {
            bool terrainLayerMatched = false;
            for (size_t l = 0; l < obj.terrain.materialLayers.size(); ++l) {
                auto& layer = obj.terrain.materialLayers[l];
                std::string layerStem = !layer.path.empty() ? std::filesystem::path(layer.path).stem().string() : layer.name;
                bool layerMatch = (!mat.name.empty() && layer.name == mat.name) ||
                                   (!mat.filePath.empty() && layer.path == mat.filePath) ||
                                   (!matStem.empty() && layer.name == matStem) ||
                                   (!matStem.empty() && layerStem == matStem) ||
                                   (l == 0 && objMatched);

                if (layerMatch) {
                    layer.baseColor = mat.baseColor;
                    layer.metallic = mat.metallic;
                    layer.roughness = mat.roughness;
                    layer.normalStrength = mat.normalStrength;
                    layer.specular = mat.specular;
                    layer.uvScale = mat.uvScale;
                    layer.baseColorTexture = mat.baseColorTexture;
                    layer.normalTexture = mat.normalTexture;
                    layer.roughnessTexture = mat.roughnessTexture;
                    layer.metallicTexture = mat.metallicTexture;
                    layer.aoTexture = mat.aoTexture;
                    terrainLayerMatched = true;

                    if (l == 0) {
                        obj.color = mat.baseColor;
                        obj.metallic = mat.metallic;
                        obj.roughness = mat.roughness;
                        obj.normalStrength = mat.normalStrength;
                        obj.specular = mat.specular;
                        obj.uvScale = mat.uvScale;
                        obj.baseColorTexture = mat.baseColorTexture;
                        obj.normalTexture = mat.normalTexture;
                        obj.roughnessTexture = mat.roughnessTexture;
                        obj.metallicTexture = mat.metallicTexture;
                        obj.aoTexture = mat.aoTexture;
                    }
                }
            }
            if (terrainLayerMatched) {
                obj.terrain.isDirty = true;
                anyChanged = true;
            }
        }
    }

    if (anyChanged) {
        RequestViewportRedraw(5);
    }
}

void EngineUI::OpenMaterialEditor(const std::string& path) {
    // Check if material is already open in tabs
    for (size_t i = 0; i < openMaterialDocs.size(); ++i) {
        if (openMaterialDocs[i].material.filePath == path) {
            SwitchToMaterialEditor((int)i);
            AddLog("LogMaterial", "Switched to Material Editor: " + openMaterialDocs[i].material.name, 0);
            return;
        }
    }

    MaterialAsset newMat;
    if (LoadMaterialFile(path, newMat)) {
        MaterialDoc doc;
        doc.material = newMat;
        doc.isDirty = false;
        openMaterialDocs.push_back(doc);
        SwitchToMaterialEditor((int)openMaterialDocs.size() - 1);
        AddLog("LogMaterial", "Opened Material Editor: " + newMat.name, 0);
    } else {
        AddLog("LogMaterial", "Failed to load material from: " + path, 3);
    }
}

void EngineUI::SwitchToMaterialEditor(int docIdx) {
    if (docIdx < 0 || docIdx >= (int)openMaterialDocs.size()) return;
    activeMaterialDocIdx = docIdx;
    activeMaterial = openMaterialDocs[docIdx].material;
    materialDirty = openMaterialDocs[docIdx].isDirty;
    showMaterialEditor = true;
    materialWorkspaceActive = true;
    EunoiaUI::UIEditorManager::Get().SwitchToMainEditor();
    RequestViewportRedraw(10);
}

void EngineUI::CloseMaterialEditor(int docIdx) {
    if (docIdx < 0 || docIdx >= (int)openMaterialDocs.size()) return;
    std::string closedName = openMaterialDocs[docIdx].material.name;
    openMaterialDocs.erase(openMaterialDocs.begin() + docIdx);

    if (openMaterialDocs.empty()) {
        activeMaterialDocIdx = -1;
        showMaterialEditor = false;
        materialWorkspaceActive = false;
        activeMaterial = MaterialAsset{};
        materialDirty = false;
    } else {
        if (activeMaterialDocIdx >= (int)openMaterialDocs.size()) {
            activeMaterialDocIdx = (int)openMaterialDocs.size() - 1;
        }
        SwitchToMaterialEditor(activeMaterialDocIdx);
    }
    AddLog("LogMaterial", "Closed Material Editor tab: " + closedName, 0);
    RequestViewportRedraw(10);
}

void EngineUI::RenderMaterialEditor(Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(800, 640), ImGuiCond_FirstUseEver);
    std::string winTitle = "Material Editor - " + activeMaterial.name + (materialDirty ? "*" : "") + "###MaterialEditorWin";
    if (ImGui::Begin(winTitle.c_str(), &showMaterialEditor, ImGuiWindowFlags_NoCollapse)) {
        // Toolbar
        if (ImGui::Button("Save Material") || (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))) {
            if (SaveMaterialFile(activeMaterial.filePath, activeMaterial)) {
                materialDirty = false;
                if (activeMaterialDocIdx >= 0 && activeMaterialDocIdx < (int)openMaterialDocs.size()) {
                    openMaterialDocs[activeMaterialDocIdx].isDirty = false;
                    openMaterialDocs[activeMaterialDocIdx].material = activeMaterial;
                    openMaterialDocs[activeMaterialDocIdx].saveState.isSaving = true;
                    openMaterialDocs[activeMaterialDocIdx].saveState.progress = 1.0f;
                    openMaterialDocs[activeMaterialDocIdx].saveState.fadeTimer = 1.5f;
                }
                PropagateMaterialChangesToScene(scene, activeMaterial);
                AddLog("LogMaterial", "Saved Material to " + activeMaterial.filePath + " (Reflected in viewport and terrain layers)", 2);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply to Selected Actor")) {
            GameObject* sel = scene.GetSelected();
            if (sel) {
                ApplyMaterialToActorAndChildren(scene, sel, activeMaterial);
            } else {
                AddLog("LogMaterial", "No Actor currently selected to apply material.", 1);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert")) {
            LoadMaterialFile(activeMaterial.filePath, activeMaterial);
            materialDirty = false;
        }

        ImGui::SameLine();
        ImGui::Checkbox("Live Viewport Sync", &liveMaterialViewportSync);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("When checked, parameter tweaks sync to viewport in real-time.");
        }

        ImGui::SameLine();
        ImGui::TextDisabled("| %s", activeMaterial.name.c_str());

        ImGui::Separator();

        // Scan available textures across project root
        std::vector<TextureAssetEntry> availableTextures = ScanProjectTextures();

        // Two-pane Layout (Left: MATERIAL PREVIEW, Right: SURFACE, VALUES, SETTINGS)
        // Two-pane Layout (Left: MATERIAL PREVIEW, Right: SURFACE, VALUES, SETTINGS)
        float previewPaneW = 270.0f;
        ImGui::BeginChild("MatPreviewPane", ImVec2(previewPaneW, 0), true);

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "MATERIAL PREVIEW");
        ImGui::Spacing();

        // 6 3D Mesh Selector Buttons: Cube (default), Sphere, Cylinder, Plane, Torus, Capsule
        const char* meshNames[] = { "Cube", "Sphere", "Cylinder", "Plane", "Torus", "Capsule" };
        float shapeBtnW = (previewPaneW - 20.0f - 8.0f) / 3.0f;
        for (int m = 0; m < 6; ++m) {
            bool isCur = (matEditorPreviewMesh == m);
            if (isCur) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.122f, 0.600f, 0.949f, 1.0f));
            if (ImGui::Button(meshNames[m], ImVec2(shapeBtnW, 22.0f))) {
                matEditorPreviewMesh = m;
            }
            if (isCur) ImGui::PopStyleColor();
            if (m % 3 < 2) ImGui::SameLine(0.0f, 4.0f);
        }

        ImGui::Spacing();

        // Predefined Light Presets
        const char* lightPresets[] = { "Key Light", "Studio 3-Point", "Warm Sunset", "Cool Moonlight" };
        ImGui::SetNextItemWidth(previewPaneW - 20.0f);
        ImGui::Combo("##LightPresetCombo", &matEditorLightPreset, lightPresets, IM_ARRAYSIZE(lightPresets));

        // Auto-Rotate & Reset
        ImGui::Checkbox("Auto-Rotate", &matEditorAutoRotate);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset View")) {
            matEditorRotationAngle = 0.0f;
            matEditorLightAngle = 45.0f;
        }

        ImGui::Spacing();

        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize(previewPaneW - 20.0f, previewPaneW - 20.0f);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Register preview Pip rect
        matPreviewPipRect.active = true;
        matPreviewPipRect.x = canvasPos.x;
        matPreviewPipRect.y = canvasPos.y;
        matPreviewPipRect.width = canvasSize.x;
        matPreviewPipRect.height = canvasSize.y;
        matPreviewPipRect.meshType = matEditorPreviewMesh;
        matPreviewPipRect.lightPreset = matEditorLightPreset;
        matPreviewPipRect.lightAngle = matEditorLightAngle;
        matPreviewPipRect.rotationAngle = matEditorRotationAngle;

        // Render DirectX 12 3D PBR Realtime Texture
        if (matPreviewTextureId != 0) {
            ImGui::Image((ImTextureID)matPreviewTextureId, canvasSize, ImVec2(0, 0), ImVec2(1, 1));
        } else {
            drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(20, 22, 26, 255), 6.0f);
            drawList->AddText(ImVec2(canvasPos.x + 12.0f, canvasPos.y + canvasSize.y * 0.5f - 8.0f), IM_COL32(160, 180, 200, 220), "Initializing 3D DX12 PBR...");
        }

        // Elegant border overlay
        drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(45, 48, 56, 255), 6.0f, 0, 1.5f);
        drawList->AddText(ImVec2(canvasPos.x + 8.0f, canvasPos.y + 6.0f), IM_COL32(100, 200, 255, 220), "DX12 Realtime PBR");

        // Invisible button to capture drag for model rotation (Left) and light rotation (Right)
        ImGui::SetCursorScreenPos(canvasPos);
        ImGui::InvisibleButton("##PreviewCanvasBtn", canvasSize);
        if (ImGui::IsItemActive()) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                matEditorRotationAngle += dragDelta.x * 0.75f;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                matEditorLightAngle += dragDelta.x * 0.5f;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
            }
        }

        // Texture indicator badge overlaid
        if (!activeMaterial.baseColorTexture.empty() && activeMaterial.baseColorTexture != "none") {
            drawList->AddRectFilled(ImVec2(canvasPos.x + 8, canvasPos.y + canvasSize.y - 24),
                                    ImVec2(canvasPos.x + canvasSize.x - 8, canvasPos.y + canvasSize.y - 6),
                                    IM_COL32(0, 0, 0, 180), 4.0f);
            std::string texBadge = "Tex: " + activeMaterial.baseColorTexture;
            if (texBadge.length() > 28) texBadge = texBadge.substr(0, 25) + "...";
            drawList->AddText(ImVec2(canvasPos.x + 12, canvasPos.y + canvasSize.y - 22),
                              IM_COL32(100, 220, 255, 255), texBadge.c_str());
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "Shape: %s | Yaw: %.0fÃƒâ€šÃ‚Â°", meshNames[matEditorPreviewMesh], matEditorRotationAngle);
        ImGui::TextDisabled("Light: %.0fÃƒâ€šÃ‚Â° (L-Drag: Mesh | R-Drag: Light)", matEditorLightAngle);
        ImGui::TextDisabled("Shader: %s", activeMaterial.shadingModel == 0 ? "PBR Cook-Torrance" : (activeMaterial.shadingModel == 1 ? "Unlit" : "Subsurface"));

        ImGui::EndChild();

        // Right Pane: SURFACE, VALUES, SETTINGS (dev.md layout)
        ImGui::BeginChild("MatPropertiesPane", ImVec2(0, 0), true);

        char nameEdit[64];
        strncpy(nameEdit, activeMaterial.name.c_str(), sizeof(nameEdit));
        nameEdit[sizeof(nameEdit) - 1] = '\0';
        ImGui::Text("Material:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 10.0f);
        if (ImGui::InputText("##MatNameHeader", nameEdit, sizeof(nameEdit))) {
            activeMaterial.name = nameEdit;
            materialDirty = true;
        }

        if (activeMaterial.assetId.IsValid()) {
            ImGui::TextDisabled("AssetID: %s", activeMaterial.assetId.ToString().c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy##CopyMatAssetId")) {
                ImGui::SetClipboardText(activeMaterial.assetId.ToString().c_str());
                AddLog("LogMaterial", "Copied AssetID to clipboard: " + activeMaterial.assetId.ToString(), 0);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("View References##MatRefView")) {
                OpenReferenceViewer(activeMaterial.assetId);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
        // SECTION 1: SURFACE (dev.md)
        // ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
        if (ImGui::CollapsingHeader("SURFACE", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (DrawTextureSlot("Base Color", activeMaterial.baseColorTexture, activeMaterial.baseColorAssetId, availableTextures)) {
                materialDirty = true;
                if (TextureManager::Get().IsTextureLoaded(activeMaterial.baseColorTexture)) {
                    CachedTexture* tex = TextureManager::Get().GetTexture(activeMaterial.baseColorTexture);
                    if (tex && tex->valid) {
                        activeMaterial.baseColor = tex->averageColor;
                    }
                }
            }
            if (DrawTextureSlot("Normal", activeMaterial.normalTexture, activeMaterial.normalAssetId, availableTextures)) {
                materialDirty = true;
            }
            if (DrawTextureSlot("Surface Map (Packed R=AO, G=Rough, B=Metal)", matEditorSurfaceMap, activeMaterial.roughnessAssetId, availableTextures)) {
                activeMaterial.roughnessTexture = matEditorSurfaceMap;
                activeMaterial.metallicTexture = matEditorSurfaceMap;
                activeMaterial.aoTexture = matEditorSurfaceMap;
                activeMaterial.aoChannel = 0;
                activeMaterial.roughnessChannel = 1;
                activeMaterial.metallicChannel = 2;
                materialDirty = true;
            }
            if (DrawTextureSlot("Roughness", activeMaterial.roughnessTexture, activeMaterial.roughnessAssetId, availableTextures)) {
                materialDirty = true;
            }
            if (DrawTextureSlot("Metallic", activeMaterial.metallicTexture, activeMaterial.metallicAssetId, availableTextures)) {
                materialDirty = true;
            }
            if (DrawTextureSlot("Ambient Occlusion", activeMaterial.aoTexture, activeMaterial.aoAssetId, availableTextures)) {
                materialDirty = true;
            }
            if (DrawTextureSlot("Emission", activeMaterial.emissionTexture, activeMaterial.emissionAssetId, availableTextures)) {
                materialDirty = true;
            }
            if (DrawTextureSlot("Opacity / Mask", activeMaterial.opacityTexture, activeMaterial.opacityAssetId, availableTextures)) {
                materialDirty = true;
            }
            ImGui::Spacing();
        }

        // ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
        // SECTION 2: VALUES (dev.md)
        // ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
        if (ImGui::CollapsingHeader("VALUES", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (ImGui::ColorEdit3("Base Color", &activeMaterial.baseColor.r, ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB)) {
                materialDirty = true;
            }
            if (ImGui::SliderFloat("Metallic", &activeMaterial.metallic, 0.0f, 1.0f, "%.2f")) {
                materialDirty = true;
            }
            if (ImGui::SliderFloat("Roughness", &activeMaterial.roughness, 0.0f, 1.0f, "%.2f")) {
                materialDirty = true;
            }
            if (ImGui::SliderFloat("Normal Strength", &activeMaterial.normalStrength, 0.0f, 2.0f, "%.2f")) {
                materialDirty = true;
            }
            if (ImGui::Checkbox("Normal Map Y / Green Flip (OpenGL / Blender)", &activeMaterial.normalMapYFlip)) {
                materialDirty = true;
            }
            ImGui::Spacing();
            const char* channelNames[] = { "Red (R)", "Green (G)", "Blue (B)", "Alpha (A)" };
            if (ImGui::Combo("Metallic Channel", &activeMaterial.metallicChannel, channelNames, IM_ARRAYSIZE(channelNames))) {
                materialDirty = true;
            }
            if (ImGui::Combo("Roughness Channel", &activeMaterial.roughnessChannel, channelNames, IM_ARRAYSIZE(channelNames))) {
                materialDirty = true;
            }
            if (ImGui::Combo("AO Channel", &activeMaterial.aoChannel, channelNames, IM_ARRAYSIZE(channelNames))) {
                materialDirty = true;
            }
            ImGui::Spacing();
            if (ImGui::ColorEdit3("Emission Color", &activeMaterial.emissiveColor.r)) {
                materialDirty = true;
            }
            if (ImGui::SliderFloat("Emission Power", &activeMaterial.emissiveIntensity, 0.0f, 30.0f, "%.2f")) {
                materialDirty = true;
            }
            if (ImGui::SliderFloat("Opacity", &activeMaterial.opacity, 0.0f, 1.0f, "%.2f")) {
                materialDirty = true;
            }
            if (activeMaterial.blendMode == 1) {
                if (ImGui::SliderFloat("Opacity Mask Clip Value", &activeMaterial.opacityMaskClipValue, 0.0f, 1.0f, "%.2f")) {
                    materialDirty = true;
                }
            }
            if (!activeMaterial.opacityTexture.empty() && activeMaterial.blendMode == 0) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "Opacity map loaded, but Blend Mode is Opaque.");
                if (ImGui::SmallButton("Switch to Masked##AutoMaskBtn")) {
                    activeMaterial.blendMode = 1;
                    materialDirty = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Switch to Translucent##AutoTransBtn")) {
                    activeMaterial.blendMode = 2;
                    materialDirty = true;
                }
            }
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.2f, 0.75f, 1.0f, 1.0f), "UV Tiling (UV Scale):");
            if (ImGui::DragFloat2("UV Scale (X, Y)", &activeMaterial.uvScale.x, 0.05f, 0.001f, 100.0f, "%.2f")) {
                materialDirty = true;
                PropagateMaterialChangesToScene(scene, activeMaterial);
            }
            if (ImGui::DragFloat("Tiling X (U)", &activeMaterial.uvScale.x, 0.05f, 0.001f, 100.0f, "%.2f")) {
                materialDirty = true;
                PropagateMaterialChangesToScene(scene, activeMaterial);
            }
            if (ImGui::DragFloat("Tiling Y (V)", &activeMaterial.uvScale.y, 0.05f, 0.001f, 100.0f, "%.2f")) {
                materialDirty = true;
                PropagateMaterialChangesToScene(scene, activeMaterial);
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset (1, 1)##MatUVReset")) {
                activeMaterial.uvScale = glm::vec2(1.0f, 1.0f);
                materialDirty = true;
                PropagateMaterialChangesToScene(scene, activeMaterial);
            }
            ImGui::Spacing();
        }

        // ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
        // SECTION 3: SETTINGS (dev.md)
        // ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
        if (ImGui::CollapsingHeader("SETTINGS", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            const char* shaders[] = { "PBR", "Unlit", "Subsurface" };
            if (ImGui::Combo("Shader", &activeMaterial.shadingModel, shaders, IM_ARRAYSIZE(shaders))) {
                materialDirty = true;
            }

            const char* blendModes[] = { "Opaque", "Masked", "Translucent" };
            if (ImGui::Combo("Blend Mode", &activeMaterial.blendMode, blendModes, IM_ARRAYSIZE(blendModes))) {
                materialDirty = true;
            }

            if (ImGui::Checkbox("Two Sided", &activeMaterial.twoSided)) {
                materialDirty = true;
            }
            if (ImGui::Checkbox("Cast Shadows", &activeMaterial.castShadows)) {
                materialDirty = true;
            }
            if (ImGui::Checkbox("Receive Shadows", &activeMaterial.receiveShadows)) {
                materialDirty = true;
            }
            if (ImGui::Checkbox("Disable Texture Streaming", &activeMaterial.disableStreaming)) {
                materialDirty = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Wicked Engine: When checked, keeps textures of this material at full native resolution even when outside viewport.");
            }

            const char* debugModes[] = {
                "0: Final PBR",
                "1: Base Color Only",
                "2: Normal Visualization",
                "3: Roughness",
                "4: Metallic",
                "5: AO",
                "6: Tangent",
                "7: Bitangent",
                "8: Vertex Normal",
                "9: UV0",
                "10: Simple Diffuse (Diagnostic)"
            };
            if (ImGui::Combo("Material Debug Mode", &activeMaterial.materialDebugMode, debugModes, IM_ARRAYSIZE(debugModes))) {
                materialDirty = true;
            }
            ImGui::Spacing();
        }

        // ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
        // SECTION 4: DIRECTXTEX TOOLS
        // ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
        if (ImGui::CollapsingHeader("DIRECTXTEX TOOLS", ImGuiTreeNodeFlags_None)) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "DirectXTex Mipmap & Texture Processing Engine");
            ImGui::TextWrapped("DirectXTex provides linear sRGB mipmap chains, normal normalization, BC1-BC7 decoding, and DDS serialization.");
            ImGui::Spacing();

            static float normalAmp = 1.0f;
            ImGui::SliderFloat("Normal Map Amplitude", &normalAmp, 0.1f, 10.0f, "%.2f");
            if (!activeMaterial.baseColorTexture.empty()) {
                if (ImGui::Button("ÃƒÂ¢Ã…Â¡Ã‚Â¡ Generate Normal Map from Base Color (DirectXTex)##GenNormBtn")) {
                    std::string res = TextureManager::Get().ResolvePath(activeMaterial.baseColorTexture);
                    CachedTexture* ct = TextureManager::Get().GetTexture(res);
                    if (ct && ct->valid && !ct->data.empty()) {
                        DirectX::Image srcImg;
                        srcImg.width = ct->width; srcImg.height = ct->height;
                        srcImg.format = DXGI_FORMAT_R8G8B8A8_UNORM;
                        srcImg.rowPitch = ct->width * 4;
                        srcImg.slicePitch = ct->width * ct->height * 4;
                        srcImg.pixels = ct->data.data();

                        DirectX::ScratchImage normMap;
                        if (DirectX::ComputeNormalMap(srcImg, DirectX::CNMAP_DEFAULT, normalAmp, DXGI_FORMAT_R8G8B8A8_UNORM, normMap)) {
                            std::filesystem::path p(res);
                            std::string outPath = (p.parent_path() / (p.stem().string() + "_Normal.dds")).string();
                            DirectX::ScratchImage mips;
                            DirectX::GenerateMipMaps(*normMap.GetImage(0, 0, 0), DirectX::TEX_FILTER_BOX | DirectX::TEX_FILTER_NORMAL_NORMALIZE, 0, mips);
                            DirectX::SaveToDDSFile(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), outPath.c_str());
                            activeMaterial.normalTexture = outPath;
                            materialDirty = true;
                            AddLog("DirectXTex", "Generated normal map from heightmap: " + outPath, 0);
                        }
                    }
                }
            }
            ImGui::Spacing();
        }

        // If Live Viewport Sync is enabled, update all scene actors and terrain layers in real-time
        if (liveMaterialViewportSync && materialDirty) {
            PropagateMaterialChangesToScene(scene, activeMaterial);
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

void EngineUI::RenderMaterialWorkspace(Scene& scene, const glm::vec2& wsPos, const glm::vec2& wsSize) {
    ImGui::SetNextWindowSize(ImVec2(wsSize.x, wsSize.y), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));

    if (ImGui::Begin("Material Editor", &showMaterialEditor, flags)) {
        // Toolbar ("Existing Menu" across the top)
        if (ImGui::Button("Save Material") || (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))) {
            if (SaveMaterialFile(activeMaterial.filePath, activeMaterial)) {
                materialDirty = false;
                if (activeMaterialDocIdx >= 0 && activeMaterialDocIdx < (int)openMaterialDocs.size()) {
                    openMaterialDocs[activeMaterialDocIdx].isDirty = false;
                    openMaterialDocs[activeMaterialDocIdx].material = activeMaterial;
                    openMaterialDocs[activeMaterialDocIdx].saveState.isSaving = true;
                    openMaterialDocs[activeMaterialDocIdx].saveState.progress = 1.0f;
                    openMaterialDocs[activeMaterialDocIdx].saveState.fadeTimer = 1.5f;
                }
                PropagateMaterialChangesToScene(scene, activeMaterial);
                AddLog("LogMaterial", "Saved Material to " + activeMaterial.filePath + " (Reflected in viewport and terrain layers)", 2);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply to Selected Actor")) {
            GameObject* sel = scene.GetSelected();
            if (sel) {
                ApplyMaterialToActorAndChildren(scene, sel, activeMaterial);
            } else {
                AddLog("LogMaterial", "No Actor currently selected to apply material.", 1);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert")) {
            LoadMaterialFile(activeMaterial.filePath, activeMaterial);
            materialDirty = false;
        }

        ImGui::SameLine();
        ImGui::Checkbox("Live Viewport Sync", &liveMaterialViewportSync);

        ImGui::SameLine();
        ImGui::TextDisabled("| %s", activeMaterial.name.c_str());

        ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f * uiScale);
        if (ImGui::Button("Close Tab", ImVec2(80.0f * uiScale, 22.0f * uiScale))) {
            showMaterialEditor = false;
            materialWorkspaceActive = false;
        }

        ImGui::Separator();

        // Scan available textures
        std::vector<TextureAssetEntry> availableTextures = ScanProjectTextures();

        // 3-Column Layout: [Preview (Top-Left)] | [Texture Selecting Fields (Center)] | [Material Properties (Right)]
        float totalAvailW = wsSize.x - 28.0f * uiScale;
        float previewColW = std::clamp(totalAvailW * 0.28f, 320.0f * uiScale, 420.0f * uiScale);
        float propsColW = std::clamp(totalAvailW * 0.36f, 380.0f * uiScale, 520.0f * uiScale);
        float texturesColW = totalAvailW - previewColW - propsColW - 16.0f * uiScale;
        if (texturesColW < 300.0f * uiScale) {
            texturesColW = std::max(280.0f * uiScale, totalAvailW * 0.34f);
            propsColW = totalAvailW - previewColW - texturesColW - 16.0f * uiScale;
        }

        // ====================================================================
        // COLUMN 1 (TOP-LEFT): PREVIEW
        // ====================================================================
        ImGui::BeginChild("MatWorkspacePreviewCol", ImVec2(previewColW, 0), true);

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Preview");
        ImGui::Separator();
        ImGui::Spacing();

        // 6 3D Mesh selector buttons: Cube (default), Sphere, Cylinder, Plane, Torus, Capsule
        const char* meshNames[] = { "Cube", "Sphere", "Cylinder", "Plane", "Torus", "Capsule" };
        float shapeBtnW = (previewColW - 20.0f - 12.0f * uiScale) / 3.0f;
        for (int m = 0; m < 6; ++m) {
            bool isCur = (matEditorPreviewMesh == m);
            if (isCur) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.122f, 0.600f, 0.949f, 1.0f));
            if (ImGui::Button(meshNames[m], ImVec2(shapeBtnW, 24.0f * uiScale))) {
                matEditorPreviewMesh = m;
            }
            if (isCur) ImGui::PopStyleColor();
            if (m % 3 < 2) ImGui::SameLine(0.0f, 6.0f);
        }

        ImGui::Spacing();

        // Environment Lighting Presets
        const char* lightPresets[] = { "Key Light", "Studio 3-Point", "Warm Sunset", "Cool Moonlight" };
        ImGui::TextDisabled("Environment Lighting:");
        ImGui::SetNextItemWidth(previewColW - 20.0f);
        ImGui::Combo("##WorkspaceLightPresetCombo", &matEditorLightPreset, lightPresets, IM_ARRAYSIZE(lightPresets));

        // Auto-Rotate & Reset View
        ImGui::Checkbox("Auto-Rotate##WorkspaceAutoRot", &matEditorAutoRotate);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset View##WorkspaceResetView")) {
            matEditorRotationAngle = 0.0f;
            matEditorLightAngle = 45.0f;
        }

        ImGui::Spacing();

        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        float canvasDim = std::min(previewColW - 24.0f, 320.0f * uiScale);
        ImVec2 canvasSize(canvasDim, canvasDim);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Update Material Preview D3D12 hardware viewport rect
        matPreviewPipRect.active = true;
        matPreviewPipRect.x = canvasPos.x;
        matPreviewPipRect.y = canvasPos.y;
        matPreviewPipRect.width = canvasSize.x;
        matPreviewPipRect.height = canvasSize.y;
        matPreviewPipRect.meshType = matEditorPreviewMesh;
        matPreviewPipRect.lightPreset = matEditorLightPreset;
        matPreviewPipRect.lightAngle = matEditorLightAngle;
        matPreviewPipRect.rotationAngle = matEditorRotationAngle;

        // Render DirectX 12 3D PBR Realtime Texture
        if (matPreviewTextureId != 0) {
            ImGui::Image((ImTextureID)matPreviewTextureId, canvasSize, ImVec2(0, 0), ImVec2(1, 1));
        } else {
            drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(20, 22, 26, 255), 6.0f);
            drawList->AddText(ImVec2(canvasPos.x + 12.0f, canvasPos.y + canvasSize.y * 0.5f - 8.0f), IM_COL32(160, 180, 200, 220), "Initializing 3D DX12 PBR...");
        }

        // Elegant border around the DirectX 12 3D PBR preview viewport
        drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(31, 153, 242, 200), 4.0f, 0, 1.5f);
        drawList->AddRect(ImVec2(canvasPos.x - 1, canvasPos.y - 1), ImVec2(canvasPos.x + canvasSize.x + 1, canvasPos.y + canvasSize.y + 1), IM_COL32(10, 12, 16, 255), 5.0f, 0, 1.0f);
        drawList->AddText(ImVec2(canvasPos.x + 8.0f, canvasPos.y + 8.0f), IM_COL32(200, 220, 240, 220), "DX12 PBR 3D");

        // Invisible button to capture drag for model rotation (Left) and light rotation (Right)
        ImGui::SetCursorScreenPos(canvasPos);
        ImGui::InvisibleButton("##WorkspacePreviewCanvasBtn", canvasSize);
        if (ImGui::IsItemActive()) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                matEditorRotationAngle += dragDelta.x * 0.75f;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            }
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                matEditorLightAngle += dragDelta.x * 0.5f;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
            }
        }

        // Texture indicator badge overlaid
        if (!activeMaterial.baseColorTexture.empty() && activeMaterial.baseColorTexture != "none") {
            drawList->AddRectFilled(ImVec2(canvasPos.x + 8, canvasPos.y + canvasSize.y - 24),
                                    ImVec2(canvasPos.x + canvasSize.x - 8, canvasPos.y + canvasSize.y - 6),
                                    IM_COL32(0, 0, 0, 180), 4.0f);
            std::string texBadge = "Tex: " + activeMaterial.baseColorTexture;
            if (texBadge.length() > 28) texBadge = texBadge.substr(0, 25) + "...";
            drawList->AddText(ImVec2(canvasPos.x + 12, canvasPos.y + canvasSize.y - 22),
                              IM_COL32(100, 220, 255, 255), texBadge.c_str());
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "Shape: %s | Yaw: %.0fÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â°", meshNames[matEditorPreviewMesh], matEditorRotationAngle);
        ImGui::TextDisabled("Light: %.0fÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â° (L-Drag: Mesh | R-Drag: Light)", matEditorLightAngle);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Material Identity & File Metadata card
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Material Info");
        char nameEdit[64];
        strncpy(nameEdit, activeMaterial.name.c_str(), sizeof(nameEdit));
        nameEdit[sizeof(nameEdit) - 1] = '\0';
        ImGui::SetNextItemWidth(previewColW - 20.0f);
        if (ImGui::InputText("##MatNameWorkspaceHeader", nameEdit, sizeof(nameEdit))) {
            activeMaterial.name = nameEdit;
            materialDirty = true;
        }

        if (activeMaterial.assetId.IsValid()) {
            ImGui::TextDisabled("AssetID: %s", activeMaterial.assetId.ToString().c_str());
            if (ImGui::SmallButton("Copy AssetID##CopyMatAssetIdWS")) {
                ImGui::SetClipboardText(activeMaterial.assetId.ToString().c_str());
                AddLog("LogMaterial", "Copied AssetID to clipboard: " + activeMaterial.assetId.ToString(), 0);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("View References##MatRefViewWS")) {
                OpenReferenceViewer(activeMaterial.assetId);
            }
        }

        if (!activeMaterial.filePath.empty()) {
            ImGui::TextDisabled("Path: %s", activeMaterial.filePath.c_str());
        }

        ImGui::EndChild();

        ImGui::SameLine();

        // ====================================================================
        // COLUMN 2 (CENTER): TEXTURE SELECTING FIELDS
        // ====================================================================
        ImGui::BeginChild("MatWorkspaceTexturesCol", ImVec2(texturesColW, 0), true);

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Texture Selecting Fields");
        ImGui::TextDisabled("Select or drop textures for PBR channels");
        ImGui::Separator();
        ImGui::Spacing();

        // 1. Base Color (Albedo)
        if (DrawTextureSlot("Base Color (Albedo)", activeMaterial.baseColorTexture, activeMaterial.baseColorAssetId, availableTextures)) {
            materialDirty = true;
            if (TextureManager::Get().IsTextureLoaded(activeMaterial.baseColorTexture)) {
                CachedTexture* tex = TextureManager::Get().GetTexture(activeMaterial.baseColorTexture);
                if (tex && tex->valid) activeMaterial.baseColor = tex->averageColor;
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 2. Normal Map
        if (DrawTextureSlot("Normal Map", activeMaterial.normalTexture, activeMaterial.normalAssetId, availableTextures)) {
            materialDirty = true;
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 3. Surface Map (Packed ORM / RMA)
        if (DrawTextureSlot("Surface Map (Packed R=AO, G=Rough, B=Metal)", matEditorSurfaceMap, activeMaterial.roughnessAssetId, availableTextures)) {
            activeMaterial.roughnessTexture = matEditorSurfaceMap;
            activeMaterial.metallicTexture = matEditorSurfaceMap;
            activeMaterial.aoTexture = matEditorSurfaceMap;
            activeMaterial.aoChannel = 0;
            activeMaterial.roughnessChannel = 1;
            activeMaterial.metallicChannel = 2;
            materialDirty = true;
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 4. Roughness Map
        if (DrawTextureSlot("Roughness Map", activeMaterial.roughnessTexture, activeMaterial.roughnessAssetId, availableTextures)) {
            materialDirty = true;
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 5. Metallic Map
        if (DrawTextureSlot("Metallic Map", activeMaterial.metallicTexture, activeMaterial.metallicAssetId, availableTextures)) {
            materialDirty = true;
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 6. Ambient Occlusion Map
        if (DrawTextureSlot("Ambient Occlusion (AO) Map", activeMaterial.aoTexture, activeMaterial.aoAssetId, availableTextures)) {
            materialDirty = true;
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 7. Emissive / Emission Map
        if (DrawTextureSlot("Emissive Map", activeMaterial.emissionTexture, activeMaterial.emissionAssetId, availableTextures)) {
            materialDirty = true;
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 8. Opacity / Mask Map
        if (DrawTextureSlot("Opacity / Mask Map", activeMaterial.opacityTexture, activeMaterial.opacityAssetId, availableTextures)) {
            materialDirty = true;
        }

        ImGui::EndChild();

        ImGui::SameLine();

        // ====================================================================
        // COLUMN 3 (RIGHT): MATERIAL PROPERTIES
        // ====================================================================
        ImGui::BeginChild("MatWorkspacePropertiesCol", ImVec2(0, 0), true);

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Material Properties");
        ImGui::TextDisabled("Physical properties, opacity, and shading parameters");
        ImGui::Separator();
        ImGui::Spacing();

        // SECTION 1: MATERIAL TYPE & SHADING
        if (ImGui::CollapsingHeader("MATERIAL TYPE & SHADING", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            const char* shadingModels[] = { "Default Lit (PBR Cook-Torrance)", "Unlit", "Subsurface Profile" };
            if (ImGui::Combo("Shading Model", &activeMaterial.shadingModel, shadingModels, IM_ARRAYSIZE(shadingModels))) materialDirty = true;

            const char* blendModes[] = { "Opaque", "Masked", "Translucent" };
            if (ImGui::Combo("Material Type (Blend Mode)", &activeMaterial.blendMode, blendModes, IM_ARRAYSIZE(blendModes))) materialDirty = true;

            if (ImGui::Checkbox("Two Sided", &activeMaterial.twoSided)) materialDirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Cast Shadows", &activeMaterial.castShadows)) materialDirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Receive Shadows", &activeMaterial.receiveShadows)) materialDirty = true;
            ImGui::Spacing();
        }

        // SECTION 2: OPACITY & MASKING
        if (ImGui::CollapsingHeader("OPACITY & MASKING", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (ImGui::SliderFloat("Opacity", &activeMaterial.opacity, 0.0f, 1.0f, "%.2f")) materialDirty = true;
            if (activeMaterial.blendMode == 1) { // Masked
                if (ImGui::SliderFloat("Mask Clip Value", &activeMaterial.opacityMaskClipValue, 0.0f, 1.0f, "%.2f")) materialDirty = true;
            }
            ImGui::Spacing();
        }

        // SECTION 3: SURFACE VALUES (PBR)
        if (ImGui::CollapsingHeader("SURFACE VALUES (PBR)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (ImGui::ColorEdit3("Base Color Tint", &activeMaterial.baseColor.x, ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB)) materialDirty = true;
            if (ImGui::SliderFloat("Roughness", &activeMaterial.roughness, 0.0f, 1.0f, "%.2f")) materialDirty = true;
            if (ImGui::SliderFloat("Metallic", &activeMaterial.metallic, 0.0f, 1.0f, "%.2f")) materialDirty = true;
            if (ImGui::SliderFloat("Specular", &activeMaterial.specular, 0.0f, 1.0f, "%.2f")) materialDirty = true;
            if (ImGui::SliderFloat("Normal Strength", &activeMaterial.normalStrength, 0.0f, 3.0f, "%.2f")) materialDirty = true;
            if (ImGui::Checkbox("Normal Map Y / Green Flip (OpenGL / Blender)", &activeMaterial.normalMapYFlip)) materialDirty = true;
            ImGui::Spacing();
        }

        // SECTION 4: EMISSIVE LIGHTING
        if (ImGui::CollapsingHeader("EMISSIVE LIGHTING", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (ImGui::ColorEdit3("Emissive Color", &activeMaterial.emissiveColor.x, ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB)) materialDirty = true;
            if (ImGui::DragFloat("Emissive Intensity", &activeMaterial.emissiveIntensity, 0.1f, 0.0f, 100.0f, "%.1f")) materialDirty = true;
            ImGui::Spacing();
        }

        // SECTION 5: UV COORDINATES & TILING
        if (ImGui::CollapsingHeader("UV COORDINATES & TILING", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (ImGui::DragFloat2("UV Scale / Tiling", &activeMaterial.uvScale.x, 0.05f, 0.01f, 100.0f, "%.2f")) {
                materialDirty = true;
                PropagateMaterialChangesToScene(scene, activeMaterial);
            }
            ImGui::Spacing();
        }

        // SECTION 6: TEXTURE CHANNEL ROUTING
        if (ImGui::CollapsingHeader("TEXTURE CHANNEL ROUTING", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            const char* channelNames[] = { "Red (R)", "Green (G)", "Blue (B)", "Alpha (A)" };
            if (ImGui::Combo("Metallic Channel", &activeMaterial.metallicChannel, channelNames, IM_ARRAYSIZE(channelNames))) materialDirty = true;
            if (ImGui::Combo("Roughness Channel", &activeMaterial.roughnessChannel, channelNames, IM_ARRAYSIZE(channelNames))) materialDirty = true;
            if (ImGui::Combo("AO Channel", &activeMaterial.aoChannel, channelNames, IM_ARRAYSIZE(channelNames))) materialDirty = true;
            ImGui::Spacing();
        }

        // SECTION 7: MATERIAL DIAGNOSTICS & DEBUG
        if (ImGui::CollapsingHeader("MATERIAL DIAGNOSTICS", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            const char* debugModes[] = {
                "Full PBR Lit (Default)",
                "Unlit",
                "Base Color Only",
                "Normal Only",
                "Roughness Only",
                "Metallic Only",
                "Ambient Occlusion Only",
                "Emissive Only",
                "UV Coordinates"
            };
            if (ImGui::Combo("Debug View Mode", &activeMaterial.materialDebugMode, debugModes, IM_ARRAYSIZE(debugModes))) materialDirty = true;
            if (ImGui::Checkbox("Disable Texture Streaming", &activeMaterial.disableStreaming)) materialDirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Wicked Engine: When checked, keeps textures of this material at full native resolution even when outside viewport.");
            ImGui::Spacing();
        }

        ImGui::EndChild();

        // Real-time viewport sync if enabled
        if (liveMaterialViewportSync && materialDirty) {
            PropagateMaterialChangesToScene(scene, activeMaterial);
        }

        // Keep active material doc in sync with activeMaterial
        if (activeMaterialDocIdx >= 0 && activeMaterialDocIdx < (int)openMaterialDocs.size()) {
            openMaterialDocs[activeMaterialDocIdx].material = activeMaterial;
            openMaterialDocs[activeMaterialDocIdx].isDirty = materialDirty;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}
bool EngineUI::LoadWaterFile(const std::string& path, WaterAsset& out) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    out.filePath = path;
    out.name = std::filesystem::path(path).stem().string();
    std::string line;
    while (std::getline(file, line)) {
        size_t p = line.find("=");
        if (p == std::string::npos) continue;
        std::string k = line.substr(0, p);
        std::string v = line.substr(p + 1);
        if (k == "waterType") out.waterType = (WaterBodyType)std::stoi(v);
        else if (k == "roughness") out.settings.roughness = std::stof(v);
        else if (k == "metallic") out.settings.metallic = std::stof(v);
        // skipping full properties parse for brevity...
    }
    return true;
}

bool EngineUI::SaveWaterFile(const std::string& path, const WaterAsset& w) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << "waterType=" << (int)w.waterType << "\n";
    file << "roughness=" << w.settings.roughness << "\n";
    file << "metallic=" << w.settings.metallic << "\n";
    return true;
}

void EngineUI::OpenWaterEditor(const std::string& path) {
    if (LoadWaterFile(path, activeWater)) {
        showWaterEditor = true;
        waterDirty = false;
        AddLog("LogWater", "Opened Water Editor for: " + path, 1);
    }
}

void EngineUI::ApplyWaterToGameObject(Scene& scene, GameObject* obj, const WaterAsset& wa) {
    if (!obj || !obj->isWaterBody) return;
    obj->water.type = wa.waterType;
    obj->water.roughness = wa.settings.roughness;
    obj->water.metallic = wa.settings.metallic;
    AddLog("LogWater", "Applied WaterAsset to GameObject", 2);
}

void EngineUI::RenderWaterEditor(Scene& scene) {
    if (!showWaterEditor) return;
    ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Water Editor", &showWaterEditor)) {
        if (ImGui::Button("Save Water")) {
            SaveWaterFile(activeWater.filePath, activeWater);
            waterDirty = false;
        }
        ImGui::Separator();
        int t = (int)activeWater.waterType;
        if (ImGui::Combo("Type", &t, "Ocean\0River\0Sea\0Lake\0Stream\0Waterfall\0Pond\0Pool\0WetSurface\0Puddle\0FloodWater\0Underwater\0Swamp\0")) {
            activeWater.waterType = (WaterBodyType)t;
            waterDirty = true;
        }
        if (ImGui::SliderFloat("Roughness", &activeWater.settings.roughness, 0.0f, 1.0f)) waterDirty = true;
        if (ImGui::SliderFloat("Metallic", &activeWater.settings.metallic, 0.0f, 1.0f)) waterDirty = true;
    }
    ImGui::End();
}

bool EngineUI::LoadSkyboxFile(const std::string& path, SkyboxAsset& outSkybox) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    outSkybox.filePath = path;
    outSkybox.name = std::filesystem::path(path).stem().string();

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        while (!key.empty() && isspace((unsigned char)key.back())) key.pop_back();
        while (!key.empty() && isspace((unsigned char)key.front())) key.erase(key.begin());
        while (!val.empty() && isspace((unsigned char)val.back())) val.pop_back();
        while (!val.empty() && isspace((unsigned char)val.front())) val.erase(val.begin());

        if (key == "assetId") outSkybox.assetId = AssetID::FromString(val);
        else if (key == "virtualPath") outSkybox.virtualPath = val;
        else if (key == "name") outSkybox.name = val;
        else if (key == "hdrTexture" || key == "hdrTexturePath") outSkybox.hdrTexturePath = val;
        else if (key == "hdrTextureAssetId") outSkybox.hdrTextureAssetId = AssetID::FromString(val);
        else if (key == "exposure") {
            try { outSkybox.exposure = std::stof(val); } catch (...) {}
        }
        else if (key == "rotation") {
            try { outSkybox.rotation = std::stof(val); } catch (...) {}
        }
        else if (key == "autoRotate") {
            outSkybox.autoRotate = (val == "1" || val == "true" || val == "True");
        }
        else if (key == "autoRotateSpeed") {
            try { outSkybox.autoRotateSpeed = std::stof(val); } catch (...) {}
        }
        else if (key == "tint") {
            std::stringstream ss(val);
            ss >> outSkybox.tint.r >> outSkybox.tint.g >> outSkybox.tint.b;
        }
        else if (key == "ambientContribution") {
            try { outSkybox.ambientContribution = std::stof(val); } catch (...) {}
        }
        else if (key == "enableSkybox" || key == "enabled") {
            outSkybox.enableSkybox = (val == "1" || val == "true" || val == "True");
        }
        else if (key == "enableSkyboxLighting" || key == "enableLighting") {
            outSkybox.enableSkyboxLighting = (val == "1" || val == "true" || val == "True");
        }
        else if (key == "skyboxLightingIntensity" || key == "lightingIntensity") {
            try { outSkybox.skyboxLightingIntensity = std::stof(val); } catch (...) {}
        }
        else if (key == "skyboxLightingColor" || key == "lightingColor") {
            std::stringstream ss(val);
            ss >> outSkybox.skyboxLightingColor.r >> outSkybox.skyboxLightingColor.g >> outSkybox.skyboxLightingColor.b;
        }
        else if (key == "skyboxLightingSpecular" || key == "lightingSpecular") {
            try { outSkybox.skyboxLightingSpecular = std::stof(val); } catch (...) {}
        }
        else if (key == "skyboxLightingDiffuse" || key == "lightingDiffuse") {
            try { outSkybox.skyboxLightingDiffuse = std::stof(val); } catch (...) {}
        }
    }
    return true;
}

bool EngineUI::SaveSkyboxFile(const std::string& path, const SkyboxAsset& skybox) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << "# Eunoia-Editor Skybox Asset\n";
    if (skybox.assetId.IsValid()) {
        file << "assetId: " << skybox.assetId.ToString() << "\n";
    }
    if (!skybox.virtualPath.empty()) {
        file << "virtualPath: " << skybox.virtualPath << "\n";
    }
    file << "name: " << skybox.name << "\n";
    if (skybox.hdrTextureAssetId.IsValid()) {
        file << "hdrTextureAssetId: " << skybox.hdrTextureAssetId.ToString() << "\n";
    }
    file << "hdrTexture: " << skybox.hdrTexturePath << "\n";
    file << "exposure: " << skybox.exposure << "\n";
    file << "rotation: " << skybox.rotation << "\n";
    file << "autoRotate: " << (skybox.autoRotate ? 1 : 0) << "\n";
    file << "autoRotateSpeed: " << skybox.autoRotateSpeed << "\n";
    file << "tint: " << skybox.tint.r << " " << skybox.tint.g << " " << skybox.tint.b << "\n";
    file << "ambientContribution: " << skybox.ambientContribution << "\n";
    file << "enableSkybox: " << (skybox.enableSkybox ? 1 : 0) << "\n";
    file << "enableSkyboxLighting: " << (skybox.enableSkyboxLighting ? 1 : 0) << "\n";
    file << "skyboxLightingIntensity: " << skybox.skyboxLightingIntensity << "\n";
    file << "skyboxLightingColor: " << skybox.skyboxLightingColor.r << " " << skybox.skyboxLightingColor.g << " " << skybox.skyboxLightingColor.b << "\n";
    file << "skyboxLightingSpecular: " << skybox.skyboxLightingSpecular << "\n";
    file << "skyboxLightingDiffuse: " << skybox.skyboxLightingDiffuse << "\n";
    return true;
}

void EngineUI::OpenSkyboxEditor(const std::string& path) {
    if (LoadSkyboxFile(path, activeSkybox)) {
        showSkyboxEditor = true;
        skyboxDirty = false;
        AddLog("LogContent", "Opened Skybox Editor: " + activeSkybox.name, 0);
    } else {
        AddLog("LogContent", "Failed to load Skybox from: " + path, 3);
    }
}

void EngineUI::OpenUIEditor(const std::string& path) {
    EunoiaUI::UIEditorManager::Get().Open(path);
    AddLog("LogContent", "Opened UI Editor for: " + path, 0);
}

void EngineUI::OpenAnimatorGraphEditor(const std::string& path) {
    materialWorkspaceActive = false;
    EunoiaUI::UIEditorManager::Get().SwitchToMainEditor();
    Eunoia::NodeBehaviour::NodeBehaviourManager::Get().SwitchToMainEditor();
    Eunoia::AnimatorGraphManager::Get().Open(path);
    AddLog("LogContent", "Opened Animator Graph Editor for: " + path, 0);
}

void EngineUI::OpenLevelAnimationEditor(const std::string& path) {
    auto anim = Eunoia::LevelAnimationManager::Get().LoadLevelAnimation(path);
    if (anim) {
        Eunoia::LevelAnimationEditor::Get().SetActiveAnimation(anim);
        showAnimationWindow = true;
        ImGui::SetWindowFocus("Animation");
        bottomDrawerTab = 1;
        bottomDrawerOpen = true;
        AddLog("LogLevelAnimation", "Opened Level Animation: " + anim->name, 0);
    }
}

void EngineUI::OpenBehaviourEditor(const std::string& path) {
    materialWorkspaceActive = false;
    EunoiaUI::UIEditorManager::Get().SwitchToMainEditor();
    Eunoia::NodeBehaviour::NodeBehaviourManager::Get().SwitchToMainEditor();
    Eunoia::AnimatorGraphManager::Get().SwitchToMainEditor();
    Eunoia::AnimationBlendManager::Get().SwitchToMainEditor();
    Eunoia::BehaviourManager::Get().Open(path);
    AddLog("LogContent", "Opened Behaviour Blueprint Editor: " + path, 0);
}

void EngineUI::OpenAnimationBlendEditor(const std::string& path) {
    materialWorkspaceActive = false;
    EunoiaUI::UIEditorManager::Get().SwitchToMainEditor();
    Eunoia::NodeBehaviour::NodeBehaviourManager::Get().SwitchToMainEditor();
    Eunoia::AnimatorGraphManager::Get().SwitchToMainEditor();
    Eunoia::AnimationBlendManager::Get().Open(path);
    AddLog("LogContent", "Opened Animation Blend Editor for: " + path, 0);
}

void EngineUI::OpenNodeBehaviourEditor(const std::string& path) {
    materialWorkspaceActive = false;
    EunoiaUI::UIEditorManager::Get().SwitchToMainEditor();
    Eunoia::NodeBehaviour::NodeBehaviourManager::Get().Open(path);
    AddLog("LogContent", "Opened Node Behaviour Editor for: " + path, 0);
}

void EngineUI::ApplySkyboxToScene(Scene& scene, const SkyboxAsset& skybox) {
    scene.enableSkybox = skybox.enableSkybox;
    scene.skyboxAssetPath = skybox.filePath;
    scene.skyboxTexturePath = skybox.hdrTexturePath;
    scene.skyboxExposure = skybox.exposure;
    scene.skyboxRotation = skybox.rotation;
    scene.skyboxAutoRotate = skybox.autoRotate;
    scene.skyboxAutoRotateSpeed = skybox.autoRotateSpeed;
    scene.skyboxTint = skybox.tint;
    scene.skyboxAmbientContribution = skybox.ambientContribution;
    scene.enableSkyboxLighting = skybox.enableSkyboxLighting;
    scene.skyboxLightingIntensity = skybox.skyboxLightingIntensity;
    scene.skyboxLightingColor = skybox.skyboxLightingColor;
    scene.skyboxLightingSpecular = skybox.skyboxLightingSpecular;
    scene.skyboxLightingDiffuse = skybox.skyboxLightingDiffuse;
    AddLog("LogWorld", "Applied Skybox '" + skybox.name + "' to World Level", 2);
}

void EngineUI::RenderSkyboxEditor(Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(620, 540), ImGuiCond_FirstUseEver);
    std::string winTitle = "Skybox Editor - " + activeSkybox.name + (skyboxDirty ? "*" : "") + "###SkyboxEditorWin";
    if (ImGui::Begin(winTitle.c_str(), &showSkyboxEditor, ImGuiWindowFlags_NoCollapse)) {
        // Toolbar
        if (ImGui::Button("Save Skybox") || (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))) {
            if (SaveSkyboxFile(activeSkybox.filePath, activeSkybox)) {
                skyboxDirty = false;
                AddLog("LogContent", "Saved Skybox Asset: " + activeSkybox.name, 2);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply to World")) {
            ApplySkyboxToScene(scene, activeSkybox);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Enable Skybox", &activeSkybox.enableSkybox)) {
            skyboxDirty = true;
        }

        ImGui::Separator();

        ImGui::BeginChild("SkyboxPropsScroll", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        // SECTION 1: ASSET IDENTITY
        if (ImGui::CollapsingHeader("ASSET IDENTITY", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            char nameBuf[128];
            strncpy(nameBuf, activeSkybox.name.c_str(), sizeof(nameBuf));
            nameBuf[sizeof(nameBuf) - 1] = '\0';
            if (ImGui::InputText("Asset Name", nameBuf, sizeof(nameBuf))) {
                activeSkybox.name = nameBuf;
                skyboxDirty = true;
            }
            ImGui::TextDisabled("Virtual Path: %s", activeSkybox.virtualPath.c_str());
            ImGui::TextDisabled("Asset ID:     %s", activeSkybox.assetId.ToString().c_str());
            ImGui::TextDisabled("File Path:    %s", activeSkybox.filePath.c_str());
            ImGui::Spacing();
        }

        // SECTION 2: HDR PANORAMA TEXTURE
        if (ImGui::CollapsingHeader("HDR PANORAMA TEXTURE", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            ImGui::TextWrapped("Assign an HDR equirectangular panorama texture (.hdr, .dds, .png, .jpg, .exr) from the Content Browser.");
            ImGui::Spacing();

            auto availableTextures = ScanProjectTextures();
            if (DrawTextureSlot("HDR Texture", activeSkybox.hdrTexturePath, activeSkybox.hdrTextureAssetId, availableTextures)) {
                skyboxDirty = true;
            }
            ImGui::Spacing();
        }

        // SECTION 3: ENVIRONMENT PARAMETERS
        if (ImGui::CollapsingHeader("ENVIRONMENT ILLUMINATION", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (ImGui::SliderFloat("Exposure / Intensity", &activeSkybox.exposure, 0.05f, 10.0f, "%.2f")) {
                skyboxDirty = true;
            }
            if (ImGui::SliderFloat("Rotation (Azimuth)", &activeSkybox.rotation, 0.0f, 360.0f, "%.1fÃ‚Â°")) {
                skyboxDirty = true;
            }
            if (ImGui::Checkbox("Auto Rotate Sky", &activeSkybox.autoRotate)) {
                skyboxDirty = true;
            }
            if (activeSkybox.autoRotate) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f * uiScale);
                if (ImGui::SliderFloat("Speed##SkyEditSpeed", &activeSkybox.autoRotateSpeed, -20.0f, 20.0f, "%.1fÃ‚Â°/s")) {
                    skyboxDirty = true;
                }
            }
            if (ImGui::ColorEdit3("Color Tint", &activeSkybox.tint.x)) {
                skyboxDirty = true;
            }
            if (ImGui::SliderFloat("Ambient Scene Contribution", &activeSkybox.ambientContribution, 0.0f, 2.0f, "%.2f")) {
                skyboxDirty = true;
            }
            ImGui::Spacing();
            if (ImGui::Button("Reset Defaults##SkyboxReset")) {
                activeSkybox.exposure = 1.0f;
                activeSkybox.rotation = 0.0f;
                activeSkybox.tint = glm::vec3(1.0f, 1.0f, 1.0f);
                activeSkybox.ambientContribution = 0.5f;
                skyboxDirty = true;
            }
            ImGui::Spacing();
        }

        // SECTION 4: SKYBOX BASED LIGHTING (ENVIRONMENT IBL)
        if (ImGui::CollapsingHeader("SKYBOX BASED LIGHTING (ENVIRONMENT IBL)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            ImGui::TextWrapped("Cast diffuse and specular ambient illumination into the 3D scene from the skybox.");
            if (ImGui::Checkbox("Enable Skybox Based Lighting", &activeSkybox.enableSkyboxLighting)) {
                skyboxDirty = true;
            }
            if (activeSkybox.enableSkyboxLighting) {
                ImGui::Spacing();
                if (ImGui::SliderFloat("Lighting Intensity", &activeSkybox.skyboxLightingIntensity, 0.0f, 10.0f, "%.2f")) {
                    skyboxDirty = true;
                }
                if (ImGui::ColorEdit3("Lighting Color Tint", &activeSkybox.skyboxLightingColor.x)) {
                    skyboxDirty = true;
                }
                if (ImGui::SliderFloat("Diffuse Contribution", &activeSkybox.skyboxLightingDiffuse, 0.0f, 2.0f, "%.2f")) {
                    skyboxDirty = true;
                }
                if (ImGui::SliderFloat("Specular / Reflection Contribution", &activeSkybox.skyboxLightingSpecular, 0.0f, 2.0f, "%.2f")) {
                    skyboxDirty = true;
                }
            } else {
                ImGui::TextDisabled("  (Skybox based scene lighting is currently disabled by default)");
            }
            ImGui::Spacing();
        }

        // Live preview sync to world scene
        if (scene.skyboxAssetPath == activeSkybox.filePath || scene.skyboxAssetPath.empty()) {
            scene.enableSkybox = activeSkybox.enableSkybox;
            scene.skyboxTexturePath = activeSkybox.hdrTexturePath;
            scene.skyboxExposure = activeSkybox.exposure;
            scene.skyboxRotation = activeSkybox.rotation;
            scene.skyboxAutoRotate = activeSkybox.autoRotate;
            scene.skyboxAutoRotateSpeed = activeSkybox.autoRotateSpeed;
            scene.skyboxTint = activeSkybox.tint;
            scene.skyboxAmbientContribution = activeSkybox.ambientContribution;
            scene.enableSkyboxLighting = activeSkybox.enableSkyboxLighting;
            scene.skyboxLightingIntensity = activeSkybox.skyboxLightingIntensity;
            scene.skyboxLightingColor = activeSkybox.skyboxLightingColor;
            scene.skyboxLightingSpecular = activeSkybox.skyboxLightingSpecular;
            scene.skyboxLightingDiffuse = activeSkybox.skyboxLightingDiffuse;
            scene.skyboxAssetPath = activeSkybox.filePath;
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

void EngineUI::RenderWorldSkyboxSettingsModal(Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("World Skybox Settings###WorldSkyboxSettingsWin", &showSkyboxSettingsModal, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "World Environment Skybox Configuration");
        ImGui::TextWrapped("Configure the active world skybox background and ambient scene illumination for the level.");
        ImGui::Separator();

        // Enable toggle
        ImGui::Checkbox("Enable World Skybox", &scene.enableSkybox);
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("TEXTURE & ILLUMINATION", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            auto availableTextures = ScanProjectTextures();
            AssetID dummyAssetId;
            if (DrawTextureSlot("HDR Texture", scene.skyboxTexturePath, dummyAssetId, availableTextures)) {
                scene.skyboxAssetPath = "";
            }
            ImGui::Spacing();

            ImGui::SliderFloat("Exposure / Intensity", &scene.skyboxExposure, 0.05f, 10.0f, "%.2f");
            ImGui::SliderFloat("Rotation (Azimuth)", &scene.skyboxRotation, 0.0f, 360.0f, "%.1fÃ‚Â°");
            ImGui::Checkbox("Auto Rotate Sky", &scene.skyboxAutoRotate);
            if (scene.skyboxAutoRotate) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f * uiScale);
                ImGui::SliderFloat("Speed##SkyRotateSpeed", &scene.skyboxAutoRotateSpeed, -20.0f, 20.0f, "%.1fÃ‚Â°/s");
            }
            ImGui::ColorEdit3("Sky Tint", &scene.skyboxTint.x);
            ImGui::SliderFloat("Ambient Scene Contribution", &scene.skyboxAmbientContribution, 0.0f, 2.0f, "%.2f");
            ImGui::Spacing();

            if (ImGui::Button("Reset to Defaults##ResetSkyboxWorld")) {
                scene.enableSkybox = true;
                scene.skyboxExposure = 1.0f;
                scene.skyboxRotation = 0.0f;
                scene.skyboxTint = glm::vec3(1.0f, 1.0f, 1.0f);
                scene.skyboxAmbientContribution = 0.5f;
                scene.enableSkyboxLighting = false;
                scene.skyboxLightingIntensity = 1.0f;
                scene.skyboxLightingColor = glm::vec3(1.0f, 1.0f, 1.0f);
                scene.skyboxLightingSpecular = 1.0f;
                scene.skyboxLightingDiffuse = 1.0f;
            }
            ImGui::Spacing();
        }

        if (ImGui::CollapsingHeader("SKYBOX BASED LIGHTING (ENVIRONMENT IBL)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            ImGui::TextWrapped("Cast diffuse and specular ambient illumination into the 3D scene from the skybox.");
            ImGui::Checkbox("Enable Skybox Based Lighting", &scene.enableSkyboxLighting);
            if (scene.enableSkyboxLighting) {
                ImGui::Spacing();
                ImGui::SliderFloat("Lighting Intensity", &scene.skyboxLightingIntensity, 0.0f, 10.0f, "%.2f");
                ImGui::ColorEdit3("Lighting Color Tint", &scene.skyboxLightingColor.x);
                ImGui::SliderFloat("Diffuse Contribution", &scene.skyboxLightingDiffuse, 0.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Specular / Reflection Contribution", &scene.skyboxLightingSpecular, 0.0f, 2.0f, "%.2f");
            } else {
                ImGui::TextDisabled("  (Skybox based scene lighting is currently disabled by default)");
            }
            ImGui::Spacing();
        }

        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            showSkyboxSettingsModal = false;
        }
    }
    ImGui::End();
}

void EngineUI::RenderPostProcessingModal(Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(640, 640), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("World Post Processing Settings###WorldPostProcessingWin", &showPostProcessingModal, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "World Post Processing & Color Pipeline");
        ImGui::TextWrapped("Configure real-time exposure and color grading for the active level. Post processing is disabled by default for each level.");
        ImGui::Separator();

        // Master Enable Toggle
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.2f, 0.9f, 0.4f, 1.0f));
        if (ImGui::Checkbox("Enable Post Processing for Level", &scene.enablePostProcessing)) {
            AddLog("LogWorld", scene.enablePostProcessing ? "Enabled level post processing" : "Disabled level post processing", 0);
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (scene.enablePostProcessing) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "[ACTIVE]");
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "[DISABLED (Default)]");
        }
        ImGui::Spacing();

        if (!scene.enablePostProcessing) {
            ImGui::TextDisabled("Enable the checkbox above to activate real-time exposure and color grading in the viewport.");
            ImGui::Spacing();
        }

        // ====================================================================
        // 1. EXPOSURE & LUMINANCE
        // ====================================================================
        if (ImGui::CollapsingHeader("EXPOSURE & LUMINANCE", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            ImGui::SliderFloat("EV Compensation", &scene.postExposureEV, -5.0f, 5.0f, "%.2f EV");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Exposure Value compensation in photographic stops (2^EV factor). 0 = Neutral.");
            }

            // Quick preset EV buttons
            ImGui::TextDisabled("Quick EV Presets:");
            ImGui::SameLine();
            if (ImGui::SmallButton("-2 EV")) scene.postExposureEV = -2.0f;
            ImGui::SameLine();
            if (ImGui::SmallButton("-1 EV")) scene.postExposureEV = -1.0f;
            ImGui::SameLine();
            if (ImGui::SmallButton("0 EV (Neutral)")) scene.postExposureEV = 0.0f;
            ImGui::SameLine();
            if (ImGui::SmallButton("+1 EV")) scene.postExposureEV = 1.0f;
            ImGui::SameLine();
            if (ImGui::SmallButton("+2 EV")) scene.postExposureEV = 2.0f;

            ImGui::SliderFloat("Exposure Multiplier", &scene.postExposureMultiplier, 0.0f, 8.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Linear intensity multiplier applied to camera exposure.");
            }
            ImGui::Spacing();
        }

        // ====================================================================
        // 2. TONEMAPPING & CONTRAST
        // ====================================================================
        if (ImGui::CollapsingHeader("TONEMAPPING & CONTRAST", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            const char* tonemappers[] = {
                "None / Linear (Clamped)",
                "ACES Filmic (Wicked / UE5 Standard)",
                "Reinhard (Soft Shoulder)",
                "Uchimura (Gran Turismo 7 / WickedEngine)",
                "Uncharted 2 / Filmic (NVIDIA Falcor / John Hable)"
            };
            ImGui::SetNextItemWidth(340.0f);
            ImGui::Combo("Tonemapping Operator", &scene.postTonemapper, tonemappers, IM_ARRAYSIZE(tonemappers));

            ImGui::SliderFloat("Contrast", &scene.postContrast, 0.4f, 2.2f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Expands or compresses dynamic range contrast (1.0 = Default neutral).");
            }

            ImGui::SliderFloat("Gamma Curve", &scene.postGamma, 0.4f, 2.2f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Gamma power curve exponent (1.0 = standard 2.2 display gamma).");
            }
            ImGui::Spacing();
        }

        // ====================================================================
        // 3. COLOR GRADING & WHITE BALANCE
        // ====================================================================
        if (ImGui::CollapsingHeader("COLOR GRADING & WHITE BALANCE", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            ImGui::SliderFloat("Saturation", &scene.postSaturation, 0.0f, 2.5f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Color vibrance and intensity (0.0 = Grayscale/Monochrome, 1.0 = Natural, 2.0 = Saturated).");
            }

            ImGui::ColorEdit3("Color Filter / Grading Tint", &scene.postColorFilter.x);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Direct RGB tint filter multiplied across final scene pixels.");
            }

            ImGui::SliderFloat("Color Temperature (K)", &scene.postColorTemperature, 2000.0f, 12000.0f, "%.0f K");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("White balance color temperature in Kelvin. 6500K is neutral daylight. <6500K is warm/amber, >6500K is cool/blue.");
            }
            ImGui::TextDisabled("  Reference: 2500K (Candle) | 4000K (Warm Sun) | 6500K (Daylight) | 9000K (Overcast/Cool)");

            ImGui::SliderFloat("Color Tint (Green - Magenta)", &scene.postColorTint, -1.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("White balance tint axis: negative shifts towards green, positive shifts towards magenta.");
            }
            ImGui::Spacing();
        }

        // ====================================================================
        // 4. SHARPEN FILTER & DENOISER
        // ====================================================================
        if (ImGui::CollapsingHeader("DETAIL & FILTERING (SHARPEN & DENOISER)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            ImGui::SliderFloat("Sharpen Filter", &scene.postSharpenAmount, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Contrast Adaptive / High-Pass sharpening filter intensity. 0 = Off, >0 sharpens fine surface details.");
            }

            ImGui::SliderFloat("Denoiser", &scene.postDenoiserAmount, 0.0f, 1.0f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Edge-preserving spatiotemporal noise reduction. 0 = Off, >0 suppresses high-frequency grain and noise.");
            }
            ImGui::Spacing();
        }

        // Reset to Defaults & Close Buttons
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Reset to Defaults##ResetPPModal", ImVec2(160, 30))) {
            scene.enablePostProcessing = false;
            scene.postExposureEV = 0.0f;
            scene.postExposureMultiplier = 1.0f;
            scene.postTonemapper = 1;
            scene.postSaturation = 1.0f;
            scene.postContrast = 1.0f;
            scene.postGamma = 1.0f;
            scene.postColorFilter = glm::vec3(1.0f, 1.0f, 1.0f);
            scene.postColorTemperature = 6500.0f;
            scene.postColorTint = 0.0f;
            scene.postSharpenAmount = 0.0f;
            scene.postDenoiserAmount = 0.0f;
            AddLog("LogWorld", "Reset post processing settings to defaults (Disabled).", 0);
        }

        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(100, 30))) {
            showPostProcessingModal = false;
        }
    }
    ImGui::End();
}

void EngineUI::RenderAmbientOcclusionModal(Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(620, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Ambient Occlusion Settings###AmbientOcclusionSettingsWin", &showAmbientOcclusionModal, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Screen Space Ambient Occlusion (SSAO / HBAO / MSAO)");
        ImGui::TextWrapped("Configure real-time Screen Space Ambient Occlusion parameters referencing WickedEngine. Approximates realistic ambient contact shadows and occluded crevices.");
        ImGui::Separator();

        // Master Enable Toggle
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.2f, 0.9f, 0.4f, 1.0f));
        if (ImGui::Checkbox("Enable Ambient Occlusion for Scene", &scene.enableAO)) {
            AddLog("LogRenderer", scene.enableAO ? "Ambient Occlusion Enabled" : "Ambient Occlusion Disabled", 0);
            Eunoia::PostProcess::PostProcessSystem::Get().SetAOMode(scene.enableAO ? (Eunoia::PostProcess::SSAOMode)scene.aoType : Eunoia::PostProcess::SSAOMode::Disabled);
            RequestViewportRedraw(15);
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (scene.enableAO && scene.aoType > 0) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "[ACTIVE]");
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "[DISABLED]");
        }
        ImGui::Spacing();

        // ====================================================================
        // 1. ALGORITHM SELECTION (WickedEngine wiRenderPath3D::AO)
        // ====================================================================
        if (ImGui::CollapsingHeader("AO ALGORITHM (WickedEngine Reference)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            const char* aoAlgorithms[] = {
                "Disabled",
                "SSAO (Simple Screen Space AO)",
                "HBAO (Horizon-Based AO)",
                "MSAO (Multi-Scale AO)",
                "RTAO (Ray-Traced AO)"
            };
            int curType = scene.aoType;
            ImGui::SetNextItemWidth(340.0f);
            if (ImGui::Combo("Algorithm##AoModalAlgo", &curType, aoAlgorithms, IM_ARRAYSIZE(aoAlgorithms))) {
                scene.aoType = curType;
                if (scene.aoType > 0) scene.enableAO = true;
                else scene.enableAO = false;
                Eunoia::PostProcess::PostProcessSystem::Get().SetAOMode((Eunoia::PostProcess::SSAOMode)scene.aoType);
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Choose Ambient Occlusion type (Reference: WickedEngine GraphicsWindow.cpp):\n"
                                  "- SSAO: Standard hemisphere ray sampling\n"
                                  "- HBAO: Horizon-Based AO with angular integration\n"
                                  "- MSAO: Multi-Scale AO with depth-aware upsampling\n"
                                  "- RTAO: Hardware DXR ray-traced ambient occlusion");
            }

            ImGui::TextDisabled("Selected Mode: %s", aoAlgorithms[std::clamp(scene.aoType, 0, 4)]);
            ImGui::Spacing();
        }

        // ====================================================================
        // 2. SAMPLING & OCCLUSION TUNING (WickedEngine Parameters)
        // ====================================================================
        if (ImGui::CollapsingHeader("AO PROPERTIES & TUNING", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();

            // Power (WickedEngine aoPowerSlider: 0.25 - 8.0)
            if (ImGui::SliderFloat("Power##AoPower", &scene.aoPower, 0.25f, 8.0f, "%.2f")) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Set SSAO Power (WickedEngine aoPower: 0.25 - 8.0).\nExponent applied to occlusion: higher values darken ambient crevices and contact corners.");
            }

            // Quick Power preset buttons
            ImGui::TextDisabled("Quick Power:");
            ImGui::SameLine();
            if (ImGui::SmallButton("0.5 (Soft)")) { scene.aoPower = 0.5f; RequestViewportRedraw(15); }
            ImGui::SameLine();
            if (ImGui::SmallButton("1.0 (Standard)")) { scene.aoPower = 1.0f; RequestViewportRedraw(15); }
            ImGui::SameLine();
            if (ImGui::SmallButton("1.5 (Balanced)")) { scene.aoPower = 1.5f; RequestViewportRedraw(15); }
            ImGui::SameLine();
            if (ImGui::SmallButton("2.0 (High)")) { scene.aoPower = 2.0f; RequestViewportRedraw(15); }
            ImGui::SameLine();
            if (ImGui::SmallButton("3.5 (Deep)")) { scene.aoPower = 3.5f; RequestViewportRedraw(15); }

            // Range (WickedEngine aoRangeSlider: 0.1 - 20.0 m)
            if (ImGui::SliderFloat("Range (Radius)##AoRange", &scene.aoRange, 0.1f, 20.0f, "%.2f m")) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Set AO sample radius / ray length in world meters (WickedEngine aoRange).\nControls the physical distance across which ambient occlusion spreads.");
            }

            // Sample Count (WickedEngine aoSampleCountSlider: 1 - 64)
            if (ImGui::SliderInt("Sample Count##AoSamples", &scene.aoSampleCount, 1, 64)) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Set AO ray sample count per pixel (WickedEngine aoSampleCount: 1 - 32).\nMore samples produce cleaner, smoother occlusion gradients.");
            }

            // Bias (Self-occlusion bias)
            if (ImGui::SliderFloat("Angle / Depth Bias##AoBias", &scene.aoBias, 0.001f, 0.1f, "%.4f")) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Threshold bias to prevent false self-occlusion artifacts on flat or low-curvature surfaces.");
            }
            ImGui::Spacing();
        }

        // ====================================================================
        // 3. BILATERAL BLUR & TEMPORAL ACCUMULATION
        // ====================================================================
        if (ImGui::CollapsingHeader("DENOISING & FILTERING (WickedEngine Filter)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();

            if (ImGui::Checkbox("Edge-Preserving Bilateral Blur##AoBlur", &scene.aoBlur)) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Applies depth-aware cross-bilateral blur to eliminate ray sampling grain while preserving crisp silhouette geometry.");
            }

            if (scene.aoBlur) {
                if (ImGui::SliderFloat("Blur Radius (Pixels)##AoBlurRad", &scene.aoBlurRadius, 0.5f, 5.0f, "%.1f px")) {
                    RequestViewportRedraw(15);
                }
            }

            if (ImGui::Checkbox("Temporal Accumulation & Reprojection##AoTemporal", &scene.aoTemporal)) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Blends AO history across frames using camera velocity vectors (WickedEngine temporal filter). Eliminates ray variance flicker.");
            }

            if (scene.aoTemporal) {
                if (ImGui::SliderFloat("Temporal Blend Factor##AoBlend", &scene.aoTemporalBlend, 0.5f, 0.98f, "%.2f")) {
                    RequestViewportRedraw(15);
                }
            }
            ImGui::Spacing();
        }

        // ====================================================================
        // 4. QUICK PRESETS
        // ====================================================================
        if (ImGui::CollapsingHeader("QUALITY PRESETS", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (ImGui::Button("Low (Fast - 8 Samples)", ImVec2(170, 26))) {
                scene.enableAO = true; scene.aoType = 1; scene.aoSampleCount = 8; scene.aoRange = 0.8f; scene.aoPower = 1.0f; scene.aoBlur = true;
                Eunoia::PostProcess::PostProcessSystem::Get().ApplySSEQualityPreset(1);
                RequestViewportRedraw(15);
            }
            ImGui::SameLine();
            if (ImGui::Button("Medium (Balanced - 16 Samples)", ImVec2(190, 26))) {
                scene.enableAO = true; scene.aoType = 1; scene.aoSampleCount = 16; scene.aoRange = 1.0f; scene.aoPower = 1.5f; scene.aoBlur = true;
                Eunoia::PostProcess::PostProcessSystem::Get().ApplySSEQualityPreset(2);
                RequestViewportRedraw(15);
            }
            ImGui::SameLine();
            if (ImGui::Button("High (Quality - 24 Samples)", ImVec2(180, 26))) {
                scene.enableAO = true; scene.aoType = 1; scene.aoSampleCount = 24; scene.aoRange = 1.5f; scene.aoPower = 1.8f; scene.aoBlur = true;
                Eunoia::PostProcess::PostProcessSystem::Get().ApplySSEQualityPreset(3);
                RequestViewportRedraw(15);
            }

            if (ImGui::Button("Ultra (Cinematic - 32 Samples)", ImVec2(200, 26))) {
                scene.enableAO = true; scene.aoType = 1; scene.aoSampleCount = 32; scene.aoRange = 2.0f; scene.aoPower = 2.0f; scene.aoBlur = true;
                Eunoia::PostProcess::PostProcessSystem::Get().ApplySSEQualityPreset(4);
                RequestViewportRedraw(15);
            }
            ImGui::SameLine();
            if (ImGui::Button("Disable AO", ImVec2(120, 26))) {
                scene.enableAO = false; scene.aoType = 0;
                Eunoia::PostProcess::PostProcessSystem::Get().SetAOMode(Eunoia::PostProcess::SSAOMode::Disabled);
                RequestViewportRedraw(15);
            }
            ImGui::Spacing();
        }

        // Reset to Defaults & Close Buttons
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Reset to Defaults##ResetAOModal", ImVec2(160, 30))) {
            scene.enableAO = true;
            scene.aoType = 1;
            scene.aoPower = 1.0f;
            scene.aoRange = 1.0f;
            scene.aoSampleCount = 16;
            scene.aoBias = 0.025f;
            scene.aoBlur = true;
            scene.aoBlurRadius = 2.0f;
            scene.aoTemporal = true;
            scene.aoTemporalBlend = 0.85f;
            Eunoia::PostProcess::PostProcessSystem::Get().SetAOMode(Eunoia::PostProcess::SSAOMode::SSAO);
            AddLog("LogRenderer", "Reset ambient occlusion settings to defaults.", 0);
            RequestViewportRedraw(15);
        }

        ImGui::SameLine();
        if (ImGui::Button("Close##CloseAOModal", ImVec2(100, 30))) {
            showAmbientOcclusionModal = false;
        }
    }
    ImGui::End();
}

void EngineUI::RenderVolumetricFogModal(Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(560, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("World Volumetric Fog Settings###WorldVolumetricFogWin", &showVolumetricFogModal, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Wicked Engine Volumetric Height Fog");
        ImGui::TextWrapped("Configure real-time exponential height fog parameters referencing Wicked Engine fogHF.hlsli. Blends smoothly across distance and altitude.");
        ImGui::Separator();

        // Master Enable Toggle
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.2f, 0.9f, 0.4f, 1.0f));
        if (ImGui::Checkbox("Enable Volumetric Fog for Scene", &scene.enableFog)) {
            AddLog("LogRenderer", scene.enableFog ? "Volumetric Fog Enabled" : "Volumetric Fog Disabled", 0);
            RequestViewportRedraw(15);
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (scene.enableFog) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "[ACTIVE]");
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "[DISABLED]");
        }
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("FOG PROPERTIES (fogHF.hlsli Reference)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();

            if (ImGui::SliderFloat("Fog Start Distance", &scene.fogStart, 0.0f, 500.0f, "%.1f m")) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Distance from camera where fog begins to appear.");
            }

            if (ImGui::SliderFloat("Global Fog Density", &scene.fogDensity, 0.0001f, 0.05f, "%.5f")) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Exponential fog extinction factor (Wicked Engine density).");
            }

            if (ImGui::SliderFloat("Height Start (Bottom)", &scene.fogHeightStart, -100.0f, 200.0f, "%.1f m")) {
                if (scene.fogHeightEnd < scene.fogHeightStart) scene.fogHeightEnd = scene.fogHeightStart + 1.0f;
                RequestViewportRedraw(15);
            }

            if (ImGui::SliderFloat("Height End (Top)", &scene.fogHeightEnd, scene.fogHeightStart, 500.0f, "%.1f m")) {
                RequestViewportRedraw(15);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Upper altitude boundary where fog thins out to zero.");
            }

            if (ImGui::ColorEdit3("Fog Scattering Color", &scene.fogColor.x)) {
                RequestViewportRedraw(15);
            }
            ImGui::Spacing();
        }

        if (ImGui::CollapsingHeader("QUICK PRESETS", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();
            if (ImGui::Button("Morning Mist", ImVec2(120, 26))) {
                scene.enableFog = true; scene.fogStart = 15.0f; scene.fogDensity = 0.008f;
                scene.fogHeightStart = -5.0f; scene.fogHeightEnd = 30.0f;
                scene.fogColor = glm::vec3(0.75f, 0.80f, 0.90f);
                RequestViewportRedraw(15);
            }
            ImGui::SameLine();
            if (ImGui::Button("Dense Valley Fog", ImVec2(140, 26))) {
                scene.enableFog = true; scene.fogStart = 5.0f; scene.fogDensity = 0.020f;
                scene.fogHeightStart = -10.0f; scene.fogHeightEnd = 20.0f;
                scene.fogColor = glm::vec3(0.65f, 0.70f, 0.75f);
                RequestViewportRedraw(15);
            }
            ImGui::SameLine();
            if (ImGui::Button("Atmospheric Haze", ImVec2(140, 26))) {
                scene.enableFog = true; scene.fogStart = 50.0f; scene.fogDensity = 0.002f;
                scene.fogHeightStart = -50.0f; scene.fogHeightEnd = 200.0f;
                scene.fogColor = glm::vec3(0.70f, 0.75f, 0.85f);
                RequestViewportRedraw(15);
            }
            ImGui::SameLine();
            if (ImGui::Button("Disable Fog", ImVec2(100, 26))) {
                scene.enableFog = false;
                RequestViewportRedraw(15);
            }
            ImGui::Spacing();
        }

        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Reset to Defaults##ResetFogModal", ImVec2(160, 30))) {
            scene.enableFog = false;
            scene.fogStart = 10.0f;
            scene.fogDensity = 0.005f;
            scene.fogHeightStart = 0.0f;
            scene.fogHeightEnd = 50.0f;
            scene.fogColor = glm::vec3(0.7f, 0.75f, 0.85f);
            AddLog("LogRenderer", "Reset volumetric fog settings to defaults.", 0);
            RequestViewportRedraw(15);
        }

        ImGui::SameLine();
        if (ImGui::Button("Close##CloseFogModal", ImVec2(100, 30))) {
            showVolumetricFogModal = false;
        }
    }
    ImGui::End();
}







bool EngineUI::IsBehaviourNameAlreadyExists(const std::string& rawName) const {
    if (rawName.empty()) return false;

    std::string nameLower = rawName;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

    // 1. Check BehaviourRegistry (built-in and registered scripts)
    const auto& all = BehaviourRegistry::Get().GetAll();
    for (const auto& pair : all) {
        std::string classLower = pair.second.className;
        std::transform(classLower.begin(), classLower.end(), classLower.begin(), ::tolower);
        std::string displayLower = pair.second.displayName;
        std::transform(displayLower.begin(), displayLower.end(), displayLower.begin(), ::tolower);
        if (classLower == nameLower || displayLower == nameLower) {
            return true;
        }
    }

    // 2. Check AssetRegistry
    const auto& assets = AssetRegistry::Get().GetAllAssets();
    for (const auto& pair : assets) {
        if (pair.second.type == AssetType::Behaviour || pair.second.type == AssetType::Script || pair.second.type == AssetType::VisualBehaviour) {
            std::string objLower = pair.second.objectName;
            std::transform(objLower.begin(), objLower.end(), objLower.begin(), ::tolower);
            if (objLower == nameLower) {
                return true;
            }
        }
    }

    // 3. Scan Content root directory and project directory on disk
    std::filesystem::path searchRoot = !contentRootPath.empty() ? contentRootPath : activeProjectRoot;
    if (!searchRoot.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(searchRoot, ec)) {
            for (auto it = std::filesystem::recursive_directory_iterator(searchRoot, std::filesystem::directory_options::skip_permission_denied, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator();
                 it.increment(ec)) {
                if (!it->is_directory(ec)) {
                    std::string ext = it->path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".behaviour" || ext == ".ebehaviour") {
                        std::string fileStem = it->path().stem().string();
                        std::transform(fileStem.begin(), fileStem.end(), fileStem.begin(), ::tolower);
                        if (fileStem == nameLower) {
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

void EngineUI::RenderContentBrowser(Scene& scene) {
    RenderContentBrowserWindow(scene);
}

void EngineUI::RenderContentBrowserWindow(Scene& scene) {
    if (!showContentBrowserWindow) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // Dockable window: size hint for first launch, can be repositioned via DockSpace
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.75f, 260.0f * uiScale), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Content Browser", &showContentBrowserWindow, flags)) {
        RenderContentBrowserContent(scene);
    }
    ImGui::End();
}

void EngineUI::RenderAnimationTimelineWindow(Scene& scene) {
    if (!showAnimationWindow) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // Dockable window: size hint for first launch, can be repositioned via DockSpace
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.75f, 260.0f * uiScale), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Animation", &showAnimationWindow, flags)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        Eunoia::LevelAnimationEditor::Get().RenderTimeline(scene, ImGui::GetCursorScreenPos(), avail);
    }
    ImGui::End();
}

void EngineUI::RenderOutputLogWindow() {
    if (!showOutputLogWindow) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // Dockable window: size hint for first launch, can be repositioned via DockSpace
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.75f, 260.0f * uiScale), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Output Log", &showOutputLogWindow, flags)) {
        // Filter toolbar matching Old Eunoia Engine
        if (ImGui::Button("Clear")) {
            logs.clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Info", &logShowInfo);
        ImGui::SameLine();
        ImGui::Checkbox("Warnings", &logShowWarnings);
        ImGui::SameLine();
        ImGui::Checkbox("Errors", &logShowErrors);
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &logAutoScroll);

        ImGui::SameLine(ImGui::GetWindowWidth() - 220.0f);
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##LogSearch", "Filter Log...", logSearchBuf, sizeof(logSearchBuf));

        ImGui::Separator();

        // Log messages child
        ImGui::BeginChild("LogScrollArea", ImVec2(0, -32), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        std::string searchLower = logSearchBuf;
        std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

        for (const auto& entry : logs) {
            if (entry.level == 0 && !logShowInfo) continue;
            if (entry.level == 1 && !logShowWarnings) continue;
            if ((entry.level == 2 || entry.level == 3) && !logShowErrors) continue;

            if (!searchLower.empty()) {
                std::string msgLower = entry.message;
                std::transform(msgLower.begin(), msgLower.end(), msgLower.begin(), ::tolower);
                if (msgLower.find(searchLower) == std::string::npos) continue;
            }

            ImVec4 col = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
            if (entry.level == 1) col = ImVec4(1.0f, 0.85f, 0.2f, 1.0f);      // Warning Yellow
            else if (entry.level == 2 || entry.level == 3) col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Error Red
            else if (entry.level == 4) col = ImVec4(0.3f, 1.0f, 0.4f, 1.0f);  // Success Green

            ImGui::TextColored(col, "[%s] %s", entry.category.c_str(), entry.message.c_str());
        }

        if (logAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        // Command line input
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##LogCommandInput", "Enter Lua / Engine command and press Enter...", consoleInput, sizeof(consoleInput), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (consoleInput[0] != '\0') {
                AddLog("Command", consoleInput, 0);
                consoleInput[0] = '\0';
            }
        }
        ImGui::PopItemWidth();
    }
    ImGui::End();
}

void EngineUI::RenderContentBrowserContent(Scene& scene) {
    const ImVec4 cbAccentBlue  = ImVec4(0.000f, 0.400f, 1.000f, 1.0f); // #0066ff
    const ImVec4 accentBlue    = cbAccentBlue;
    const ImVec4 cbTextWhite   = ImVec4(1.000f, 1.000f, 1.000f, 1.0f);
    const ImVec4 cbTextMuted   = ImVec4(0.631f, 0.631f, 0.667f, 1.0f);

    // Registry-Backed Content Browser (dev.md Section 14, 34, 35)
                std::error_code ec;
                if (currentVirtualDir.empty()) currentVirtualDir = "/Game";

                // 1. Top Navigation & Action Toolbar (matching Old Eunoia Engine)
                bool isAtRoot = (currentVirtualDir == "/Game");
                if (isAtRoot) ImGui::BeginDisabled();
                if (ImGui::Button("< Up")) {
                    currentVirtualDir = AssetPath::GetDirectory(currentVirtualDir);
                    selectedContentItem = "";
                    selectedContentItems.clear();
                }
                if (isAtRoot) ImGui::EndDisabled();

                // Drop target on "< Up" button to move items up into parent directory
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                        std::string targetDir = AssetPath::GetDirectory(currentVirtualDir);
                        if (!targetDir.empty()) {
                            std::string payloadData((const char*)payload->Data);
                            std::stringstream pss(payloadData);
                            std::string item;
                            while (std::getline(pss, item, '\n')) {
                                if (item.empty()) continue;
                                if (item.rfind("DIR:", 0) == 0) {
                                    MoveFolderToDirectory(item.substr(4), targetDir, contentRootPath);
                                } else if (item.rfind("ASSET:", 0) == 0) {
                                    const AssetMetadata* m = AssetRegistry::Get().FindByPath(item.substr(6));
                                    if (m) MoveAssetToDirectory(m->id, targetDir, contentRootPath);
                                }
                            }
                            TriggerBackgroundRegistrySync(contentRootPath);
                        }
                    } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                        std::string targetDir = AssetPath::GetDirectory(currentVirtualDir);
                        if (!targetDir.empty()) {
                            std::string idStr((const char*)payload->Data);
                            MoveAssetToDirectory(AssetID::FromString(idStr), targetDir, contentRootPath);
                            TriggerBackgroundRegistrySync(contentRootPath);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SameLine();
                if (ImGui::Button("Content")) {
                    currentVirtualDir = "/Game";
                    selectedContentItem = "";
                    selectedContentItems.clear();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                        std::string payloadData((const char*)payload->Data);
                        std::stringstream pss(payloadData);
                        std::string item;
                        while (std::getline(pss, item, '\n')) {
                            if (item.empty()) continue;
                            if (item.rfind("DIR:", 0) == 0) {
                                MoveFolderToDirectory(item.substr(4), "/Game", contentRootPath);
                            } else if (item.rfind("ASSET:", 0) == 0) {
                                const AssetMetadata* m = AssetRegistry::Get().FindByPath(item.substr(6));
                                if (m) MoveAssetToDirectory(m->id, "/Game", contentRootPath);
                            }
                        }
                        TriggerBackgroundRegistrySync(contentRootPath);
                    } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                        std::string idStr((const char*)payload->Data);
                        MoveAssetToDirectory(AssetID::FromString(idStr), "/Game", contentRootPath);
                        TriggerBackgroundRegistrySync(contentRootPath);
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("|");
                ImGui::SameLine();

                // + Add / Create Menu (matching Old Eunoia Engine)
                if (ImGui::Button("+ Add / Create")) {
                    ImGui::OpenPopup("CBAddCreatePopup");
                }
                if (ImGui::BeginPopup("CBAddCreatePopup")) {
                    if (ImGui::MenuItem("New Level (.eulevel)...")) {
                        scene.Clear();
                        currentLevelFilePath = "";
                        AddLog("ContentBrowser", "Created New Level", 3);
                    }
                    if (ImGui::MenuItem("New Folder...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewFolderPopup = true;
                        snprintf(newFolderNameBuf, sizeof(newFolderNameBuf), "NewFolder");
                    }
                    if (ImGui::MenuItem("New Material (.emat)...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewMaterialPopup = true;
                        snprintf(newMaterialNameBuf, sizeof(newMaterialNameBuf), "M_NewMaterial");
                    }
                    if (ImGui::MenuItem("New Level Animation (.elanim)...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewLevelAnimationPopup = true;
                        snprintf(newLevelAnimationNameBuf, sizeof(newLevelAnimationNameBuf), "LA_NewAnimation");
                    }
                    if (ImGui::MenuItem("New Behavior (.eubeh)...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewBehaviourPopup = true;
                        std::string defName = "NewBehaviour";
                        if (IsBehaviourNameAlreadyExists(defName)) {
                            int bIdx = 1;
                            while (IsBehaviourNameAlreadyExists("NewBehaviour_" + std::to_string(bIdx))) bIdx++;
                            defName = "NewBehaviour_" + std::to_string(bIdx);
                        }
                        snprintf(newBehaviourNameBuf, sizeof(newBehaviourNameBuf), "%s", defName.c_str());
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Import 3D Model / Level...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        ImportMeshWithSavePrompt(scene);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Show in File Explorer")) {
                        std::string diskSub = currentVirtualDir;
                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                        std::filesystem::path folderDisk = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                        ShellExecuteA(NULL, "explore", folderDisk.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
                    }
                    ImGui::EndPopup();
                }

                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("|");
                ImGui::SameLine();

                // Interactive Segmented Breadcrumbs
                std::string breadcrumb = currentVirtualDir;
                std::stringstream bss(breadcrumb);
                std::string seg;
                std::string accumVirtual = "";
                int segIdx = 0;
                while (std::getline(bss, seg, '/')) {
                    if (seg.empty()) continue;
                    accumVirtual += "/" + seg;
                    if (segIdx > 0) {
                        ImGui::SameLine();
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled(">");
                        ImGui::SameLine();
                    }
                    ImGui::PushID(segIdx++);
                    bool isCurSeg = (accumVirtual == currentVirtualDir);
                    if (isCurSeg) ImGui::PushStyleColor(ImGuiCol_Text, accentBlue);
                    if (ImGui::Button(seg.c_str())) {
                        currentVirtualDir = accumVirtual;
                        selectedContentItem = "";
                        selectedContentItems.clear();
                    }
                    if (isCurSeg) ImGui::PopStyleColor();

                    std::string bcTarget = accumVirtual;
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                            std::string payloadData((const char*)payload->Data);
                            std::stringstream pss(payloadData);
                            std::string item;
                            while (std::getline(pss, item, '\n')) {
                                if (item.empty()) continue;
                                if (item.rfind("DIR:", 0) == 0) {
                                    MoveFolderToDirectory(item.substr(4), bcTarget, contentRootPath);
                                } else if (item.rfind("ASSET:", 0) == 0) {
                                    const AssetMetadata* m = AssetRegistry::Get().FindByPath(item.substr(6));
                                    if (m) MoveAssetToDirectory(m->id, bcTarget, contentRootPath);
                                }
                            }
                            TriggerBackgroundRegistrySync(contentRootPath);
                        } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                            std::string idStr((const char*)payload->Data);
                            MoveAssetToDirectory(AssetID::FromString(idStr), bcTarget, contentRootPath);
                            TriggerBackgroundRegistrySync(contentRootPath);
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::PopID();
                }

                // Controls on right: Search, Zoom Slider (matching Old Eunoia Engine)
                float rightControlsWidth = 140.0f + 80.0f + ImGui::GetStyle().ItemSpacing.x;
                if (contentBrowserSearch[0] != '\0') {
                    rightControlsWidth += ImGui::CalcTextSize("X").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
                }
                float availW = ImGui::GetContentRegionAvail().x;
                if (availW > rightControlsWidth + 10.0f) {
                    ImGui::SameLine(ImGui::GetWindowWidth() - rightControlsWidth - ImGui::GetStyle().WindowPadding.x);
                } else {
                    ImGui::SameLine();
                }

                ImGui::SetNextItemWidth(140.0f);
                ImGui::InputTextWithHint("##CBSearch", "Search...", contentBrowserSearch, sizeof(contentBrowserSearch));
                if (contentBrowserSearch[0] != '\0') {
                    ImGui::SameLine();
                    if (ImGui::Button("X##ClearCBSearch")) {
                        contentBrowserSearch[0] = '\0';
                    }
                }

                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::SliderFloat("##CBZoom", &contentThumbnailSize, 64.0f, 128.0f, "%.0f px")) {
                    SaveEditorConfig();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Thumbnail Card Scale");

                ImGui::Separator();

                // Modals for Renaming Asset / Folder
                if (showRenameAssetPopup) {
                    ImGui::OpenPopup("Rename Asset##Modal");
                    showRenameAssetPopup = false;
                }
                if (ImGui::BeginPopupModal("Rename Asset##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    const AssetMetadata* rMeta = AssetRegistry::Get().FindById(renameTargetAssetId);
                    std::string curName = rMeta ? rMeta->objectName : renameTargetAssetName;
                    std::string curType = rMeta ? AssetTypeToString(rMeta->type) : "Asset";

                    ImGui::Text("Renaming %s: %s", curType.c_str(), curName.c_str());
                    ImGui::Spacing();
                    ImGui::Text("Enter new name:");
                    ImGui::SetNextItemWidth(300.0f);
                    if (renameFocusInput) {
                        ImGui::SetKeyboardFocusHere();
                        renameFocusInput = false;
                    }
                    ImGui::InputText("##RenameAssetInput", renameItemNameBuf, sizeof(renameItemNameBuf));

                    std::string inputName = renameItemNameBuf;
                    while (!inputName.empty() && isspace((unsigned char)inputName.front())) inputName.erase(inputName.begin());
                    while (!inputName.empty() && isspace((unsigned char)inputName.back())) inputName.pop_back();

                    bool hasInvalidChar = (inputName.find('/') != std::string::npos || inputName.find('\\') != std::string::npos ||
                                           inputName.find(':') != std::string::npos || inputName.find('*') != std::string::npos ||
                                           inputName.find('?') != std::string::npos || inputName.find('\"') != std::string::npos ||
                                           inputName.find('<') != std::string::npos || inputName.find('>') != std::string::npos ||
                                           inputName.find('|') != std::string::npos);

                    bool isValid = !inputName.empty() && !hasInvalidChar;
                    if (hasInvalidChar) {
                        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Name contains invalid filename characters (/ \\ : * ? \" < > |)");
                    }

                    ImGui::Spacing();
                    if (!isValid) ImGui::BeginDisabled();
                    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
                    bool okClicked = ImGui::Button("Rename", ImVec2(120, 0));
                    if (!isValid) ImGui::EndDisabled();

                    if (isValid && (okClicked || enterPressed)) {
                        RenameContentAsset(renameTargetAssetId, inputName);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                if (showRenameFolderPopup) {
                    ImGui::OpenPopup("Rename Folder##Modal");
                    showRenameFolderPopup = false;
                }
                if (ImGui::BeginPopupModal("Rename Folder##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string oldDirName = AssetPath::GetObjectName(renameTargetVirtualDir);
                    ImGui::Text("Renaming virtual folder: %s", renameTargetVirtualDir.c_str());
                    ImGui::Spacing();
                    ImGui::Text("Enter new folder name:");
                    ImGui::SetNextItemWidth(300.0f);
                    if (renameFocusInput) {
                        ImGui::SetKeyboardFocusHere();
                        renameFocusInput = false;
                    }
                    ImGui::InputText("##RenameFolderInput", renameItemNameBuf, sizeof(renameItemNameBuf));

                    std::string inputName = renameItemNameBuf;
                    while (!inputName.empty() && isspace((unsigned char)inputName.front())) inputName.erase(inputName.begin());
                    while (!inputName.empty() && isspace((unsigned char)inputName.back())) inputName.pop_back();

                    bool hasInvalidChar = (inputName.find('/') != std::string::npos || inputName.find('\\') != std::string::npos ||
                                           inputName.find(':') != std::string::npos || inputName.find('*') != std::string::npos ||
                                           inputName.find('?') != std::string::npos || inputName.find('\"') != std::string::npos ||
                                           inputName.find('<') != std::string::npos || inputName.find('>') != std::string::npos ||
                                           inputName.find('|') != std::string::npos);

                    bool isValid = !inputName.empty() && !hasInvalidChar && renameTargetVirtualDir != "/Game";
                    if (hasInvalidChar) {
                        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Name contains invalid folder characters (/ \\ : * ? \" < > |)");
                    }

                    ImGui::Spacing();
                    if (!isValid) ImGui::BeginDisabled();
                    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
                    bool okClicked = ImGui::Button("Rename", ImVec2(120, 0));
                    if (!isValid) ImGui::EndDisabled();

                    if (isValid && (okClicked || enterPressed)) {
                        RenameContentFolder(renameTargetVirtualDir, inputName);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // Modals for creating Folder / Material / Skybox / Behaviour
                if (showNewFolderPopup) {
                    ImGui::OpenPopup("Create Folder##Modal");
                    showNewFolderPopup = false;
                }
                if (ImGui::BeginPopupModal("Create Folder##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Creating virtual folder in: %s", targetDir.c_str());
                    ImGui::Spacing();
                    ImGui::Text("Enter folder name:");
                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputText("##FolderNameInput", newFolderNameBuf, sizeof(newFolderNameBuf));
                    ImGui::Spacing();
                    if (ImGui::Button("Create", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                        if (strlen(newFolderNameBuf) > 0) {
                            std::string diskSub = targetDir;
                            if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                            if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                            std::filesystem::path newDiskDir = contentRootPath / diskSub / newFolderNameBuf;
                            std::error_code dirEc;
                            if (std::filesystem::create_directories(newDiskDir, dirEc)) {
                                TriggerBackgroundRegistrySync(contentRootPath);
                                AddLog("LogContent", "Created folder: " + std::string(newFolderNameBuf) + " in " + targetDir, 2);
                            }
                        }
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                if (showNewBehaviourAssetPopup) {
                    ImGui::OpenPopup("Create Behaviour##Modal");
                    showNewBehaviourAssetPopup = false;
                }
                if (ImGui::BeginPopupModal("Create Behaviour##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Creating Behaviour Asset in: %s", targetDir.c_str());
                    ImGui::Spacing();
                    ImGui::Text("Enter behaviour asset name:");
                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputText("##BehAssetNameInput", newBehaviourAssetNameBuf, sizeof(newBehaviourAssetNameBuf));
                    ImGui::Spacing();
                    if (ImGui::Button("Create", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                        if (strlen(newBehaviourAssetNameBuf) > 0) {
                            std::string behFileName = newBehaviourAssetNameBuf;
                            if (behFileName.find(".behaviour") == std::string::npos) behFileName += ".behaviour";
                            std::string diskSub = targetDir;
                            if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                            if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                            std::filesystem::path dstDir = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                            std::error_code ec;
                            std::filesystem::create_directories(dstDir, ec);
                            std::filesystem::path behFilePath = dstDir / behFileName;

                            Eunoia::BehaviourManager::Get().CreateNewBehaviour(behFilePath.string());
                            TriggerBackgroundRegistrySync(contentRootPath);
                            AddLog("LogContent", "Created Behaviour asset: " + behFilePath.string(), 2);
                            OpenBehaviourEditor(behFilePath.string());
                        }
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                if (showNewMaterialPopup) {
                    ImGui::OpenPopup("Create Material##Modal");
                    showNewMaterialPopup = false;
                }
                if (ImGui::BeginPopupModal("Create Material##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Creating Material in: %s", targetDir.c_str());
                    ImGui::Spacing();
                    ImGui::Text("Enter material asset name:");
                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputText("##MatNameInput", newMaterialNameBuf, sizeof(newMaterialNameBuf));
                    ImGui::Spacing();
                    if (ImGui::Button("Create", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                        if (strlen(newMaterialNameBuf) > 0) {
                            std::string matFileName = newMaterialNameBuf;
                            if (matFileName.find(".emat") == std::string::npos) matFileName += ".emat";
                            std::string diskSub = targetDir;
                            if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                            if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                            std::filesystem::path dstDir = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                            std::filesystem::create_directories(dstDir, ec);
                            std::filesystem::path matFilePath = dstDir / matFileName;

                            MaterialAsset newMat;
                            newMat.name = std::filesystem::path(matFileName).stem().string();
                            newMat.filePath = matFilePath.string();
                            newMat.assetId = AssetID::CreateRandom();
                            newMat.virtualPath = AssetPath::Combine(targetDir, newMat.name);
                            newMat.baseColor = glm::vec3(0.75f, 0.75f, 0.75f);
                            newMat.metallic = 0.0f;
                            newMat.roughness = 0.5f;
                            newMat.specular = 0.5f;

                            if (SaveMaterialFile(matFilePath.string(), newMat)) {
                                TriggerBackgroundRegistrySync(contentRootPath);
                                AddLog("LogContent", "Created material asset: " + newMat.name + " (" + newMat.assetId.ToString() + ") in " + targetDir, 2);
                                OpenMaterialEditor(matFilePath.string());
                            }
                        }
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                if (showNewBehaviourPopup) {
                    ImGui::OpenPopup("Create Behaviour##Modal");
                    showNewBehaviourPopup = false;
                }
                if (ImGui::BeginPopupModal("Create Behaviour##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Enter Behaviour Class Name (inherits EunoiaBehaviour):");
                    ImGui::SetNextItemWidth(300.0f);
                    ImGui::InputText("##BehNameInput", newBehaviourNameBuf, sizeof(newBehaviourNameBuf));

                    std::string inputName = newBehaviourNameBuf;
                    while (!inputName.empty() && isspace((unsigned char)inputName.front())) inputName.erase(inputName.begin());
                    while (!inputName.empty() && isspace((unsigned char)inputName.back())) inputName.pop_back();

                    bool isDuplicate = !inputName.empty() && IsBehaviourNameAlreadyExists(inputName);
                    bool isValidName = !inputName.empty();

                    if (isDuplicate) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "A behaviour named '%s' already exists in this project!", inputName.c_str());
                    } else if (isValidName) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "Valid name (will be created in folder: %s)", targetDir.c_str());
                    }

                    ImGui::Spacing();

                    bool canCreate = isValidName && !isDuplicate;
                    if (!canCreate) {
                        ImGui::BeginDisabled();
                    }

                    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
                    bool createClicked = ImGui::Button("Create", ImVec2(120, 0));

                    if (!canCreate) {
                        ImGui::EndDisabled();
                    }

                    if (canCreate && (createClicked || enterPressed)) {
                        std::string behName = inputName;
                        std::string diskSub = targetDir;
                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                        std::filesystem::path behDir = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                        std::error_code bec;
                        std::filesystem::create_directories(behDir, bec);

                        std::filesystem::path cppPath = behDir / (behName + ".cpp");
                        std::filesystem::path metaPath = behDir / (behName + ".cpp.assetmeta");

                        std::ofstream cppFile(cppPath);
                        if (cppFile.is_open()) {
                            cppFile << "#include <EngineScene/EunoiaBehaviour.h>\n"
                                << "#include <EngineScene/BehaviourRegistry.h>\n"
                                << "#include <EngineScene/GameObject.h>\n"
                                << "#include <EnginePlatform/InputSystem.h>\n"
                                << "#include <EngineCore/Log.h>\n\n"
                                << "// ============================================================================\n"
                                << "// Empty Base Behaviour Structure\n"
                                << "// ============================================================================\n"
                                << "class " << behName << " : public EunoiaBehaviour {\n"
                                << "public:\n"
                                << "    // Public variables are automatically registered as editable properties\n\n"
                                << "    // ------------------------------------------------------------------------\n"
                                << "    // Lifecycle Methods\n"
                                << "    // ------------------------------------------------------------------------\n"
                                << "    void OnCreate() override {\n"
                                << "        // Called once when behaviour is instantiated or attached\n"
                                << "    }\n\n"
                                << "    void OnEnable() override {\n"
                                << "        // Called when behaviour or owner becomes enabled\n"
                                << "    }\n\n"
                                << "    void Start() override {\n"
                                << "        // Called once before the first frame Update\n"
                                << "    }\n\n"
                                << "    void Update(float deltaTime) override {\n"
                                << "        // Called every frame\n"
                                << "    }\n\n"
                                << "    void FixedUpdate(float fixedDeltaTime) override {\n"
                                << "        // Called at fixed time intervals (physics / fixed tick)\n"
                                << "    }\n\n"
                                << "    void LateUpdate(float deltaTime) override {\n"
                                << "        // Called after all Update calls each frame\n"
                                << "    }\n\n"
                                << "    void OnDisable() override {\n"
                                << "        // Called when behaviour or owner becomes disabled\n"
                                << "    }\n\n"
                                << "    void OnDestroy() override {\n"
                                << "        // Called when behaviour is removed or owner is destroyed\n"
                                << "    }\n\n"
                                << "};\n\n"
                                << "// Register behaviour with the engine's BehaviourRegistry\n"
                                << "REGISTER_BEHAVIOUR(" << behName << ")\n";
                        }

                        std::ofstream metaFile(metaPath);
                        if (metaFile.is_open()) {
                            AssetID aid = AssetID::CreateRandom();
                            std::string vPath = (targetDir.empty() || targetDir == "/") ?
                                ("/" + behName) : (targetDir + "/" + behName);
                            metaFile << "# Eunoia-Editor Asset Sidecar Metadata\n"
                                     << "assetId: " << aid.ToString() << "\n"
                                     << "type: Behaviour\n"
                                     << "baseClass: EunoiaBehaviour\n"
                                     << "virtualPath: " << vPath << "\n";
                        }
#ifdef _WIN32
                        SetFileAttributesW(metaPath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif

                        // Register the newly created script file immediately so it appears in Details Panel Behaviours list
                        BehaviourRegistry::Get().RegisterScriptFile(behName, cppPath.string());
                        TriggerBackgroundRegistrySync(contentRootPath);
                        AddLog("LogContent", "Created Behaviour asset: " + behName + ".cpp in " + targetDir, 2);

                        // Open project as workspace in VS Code and open the script file
                        OpenScriptInCodeEditor(cppPath.string());

                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // User Interface asset creation modal
                if (showNewUIPopup) {
                    ImGui::OpenPopup("Create User Interface##UIModal");
                    showNewUIPopup = false;
                }
                if (ImGui::BeginPopupModal("Create User Interface##UIModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Enter User Interface Name (.eui):");
                    ImGui::SetNextItemWidth(300.0f);
                    ImGui::InputText("##UINameInput", newUINameBuf, sizeof(newUINameBuf));

                    std::string inputName = newUINameBuf;
                    while (!inputName.empty() && isspace((unsigned char)inputName.front())) inputName.erase(inputName.begin());
                    while (!inputName.empty() && isspace((unsigned char)inputName.back())) inputName.pop_back();

                    bool isValidName = !inputName.empty();
                    if (isValidName) {
                        ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "Will create: %s.eui in %s", inputName.c_str(), targetDir.c_str());
                    }
                    ImGui::Spacing();

                    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
                    if (!isValidName) ImGui::BeginDisabled();
                    bool createClicked = ImGui::Button("Create", ImVec2(120, 0));
                    if (!isValidName) ImGui::EndDisabled();

                    if (isValidName && (createClicked || enterPressed)) {
                        std::string diskSub = targetDir;
                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                        std::filesystem::path uiDir = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                        std::error_code bec;
                        std::filesystem::create_directories(uiDir, bec);

                        std::filesystem::path euiPath = uiDir / (inputName + ".eui");
                        EunoiaUI::UISystemManager::Get().CreateNewUI(inputName, euiPath.string());

                        // Write .assetmeta sidecar
                        std::filesystem::path metaPath = uiDir / (inputName + ".eui.assetmeta");
                        std::ofstream metaFile(metaPath);
                        AssetID aid = AssetID::CreateRandom();
                        std::string vPath = (targetDir.empty() || targetDir == "/") ?
                            ("/" + inputName) : (targetDir + "/" + inputName);
                        if (metaFile.is_open()) {
                            metaFile << "# Eunoia-Editor Asset Sidecar Metadata\n"
                                     << "assetId: " << aid.ToString() << "\n"
                                     << "type: User Interface\n"
                                     << "virtualPath: " << vPath << "\n";
                        }
#ifdef _WIN32
                        SetFileAttributesW(metaPath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
                        // Register in AssetRegistry immediately
                        AssetMetadata vMeta;
                        vMeta.id = aid;
                        vMeta.type = AssetType::UserInterface;
                        vMeta.objectName = inputName;
                        std::filesystem::path rel = std::filesystem::relative(euiPath, contentRootPath, bec);
                        vMeta.sourcePath = bec ? (inputName + ".eui") : rel.generic_string();
                        vMeta.virtualPath = vPath;
                        AssetRegistry::Get().RegisterAsset(vMeta);

                        TriggerBackgroundRegistrySync(contentRootPath);
                        OpenUIEditor(euiPath.string());
                        AddLog("LogContent", "Created User Interface: " + inputName + ".eui in " + targetDir, 2);

                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // Node Behaviour asset creation modal
                if (showNewNodeBehaviourPopup) {
                    ImGui::OpenPopup("Create Node Behaviour##NBModal");
                    showNewNodeBehaviourPopup = false;
                }
                if (ImGui::BeginPopupModal("Create Node Behaviour##NBModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Enter Node Behaviour Name (.enb):");
                    ImGui::SetNextItemWidth(300.0f);
                    ImGui::InputText("##NBNameInput", newNodeBehaviourNameBuf, sizeof(newNodeBehaviourNameBuf));

                    std::string inputName = newNodeBehaviourNameBuf;
                    while (!inputName.empty() && isspace((unsigned char)inputName.front())) inputName.erase(inputName.begin());
                    while (!inputName.empty() && isspace((unsigned char)inputName.back())) inputName.pop_back();

                    bool isValidName = !inputName.empty();
                    if (isValidName) {
                        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Will create: %s.enb in %s", inputName.c_str(), targetDir.c_str());
                    }
                    ImGui::Spacing();

                    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
                    if (!isValidName) ImGui::BeginDisabled();
                    bool createClicked = ImGui::Button("Create", ImVec2(120, 0));
                    if (!isValidName) ImGui::EndDisabled();

                    if (isValidName && (createClicked || enterPressed)) {
                        std::string diskSub = targetDir;
                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                        std::filesystem::path nbDir = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                        std::error_code bec;
                        std::filesystem::create_directories(nbDir, bec);

                        std::string createdPath = Eunoia::NodeBehaviour::NodeBehaviourManager::Get().CreateNewNodeBehaviour(nbDir.string(), inputName);

                        // Write .assetmeta sidecar
                        std::filesystem::path enbPath(createdPath);
                        std::filesystem::path metaPath = nbDir / (inputName + ".enb.assetmeta");
                        std::ofstream metaFile(metaPath);
                        AssetID aid = AssetID::CreateRandom();
                        std::string vPath = (targetDir.empty() || targetDir == "/") ?
                            ("/" + inputName) : (targetDir + "/" + inputName);
                        if (metaFile.is_open()) {
                            metaFile << "# Eunoia-Editor Asset Sidecar Metadata\n"
                                     << "assetId: " << aid.ToString() << "\n"
                                     << "type: Node Behaviour\n"
                                     << "virtualPath: " << vPath << "\n";
                        }
#ifdef _WIN32
                        SetFileAttributesW(metaPath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
                        // Register in AssetRegistry immediately
                        AssetMetadata vMeta;
                        vMeta.id = aid;
                        vMeta.type = AssetType::NodeBehaviour;
                        vMeta.objectName = inputName;
                        std::filesystem::path rel = std::filesystem::relative(enbPath, contentRootPath, bec);
                        vMeta.sourcePath = bec ? (inputName + ".enb") : rel.generic_string();
                        vMeta.virtualPath = vPath;
                        AssetRegistry::Get().RegisterAsset(vMeta);

                        TriggerBackgroundRegistrySync(contentRootPath);
                        OpenNodeBehaviourEditor(createdPath);
                        AddLog("LogContent", "Created Node Behaviour: " + inputName + ".enb in " + targetDir, 2);

                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // Animator Graph asset creation modal
                // Animation Blend asset creation modal
                                // Level Animation asset creation modal
                if (showNewLevelAnimationPopup) {
                    ImGui::OpenPopup("Create Level Animation##LAModal");
                    showNewLevelAnimationPopup = false;
                }
                if (ImGui::BeginPopupModal("Create Level Animation##LAModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Enter Level Animation Name (.elanim):");
                    ImGui::SetNextItemWidth(300.0f);
                    ImGui::InputText("##LANameInput", newLevelAnimationNameBuf, sizeof(newLevelAnimationNameBuf));

                    std::string inputName = newLevelAnimationNameBuf;
                    while (!inputName.empty() && isspace((unsigned char)inputName.front())) inputName.erase(inputName.begin());
                    while (!inputName.empty() && isspace((unsigned char)inputName.back())) inputName.pop_back();

                    bool isValidName = !inputName.empty();
                    if (isValidName) {
                        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Will create: %s.elanim in %s", inputName.c_str(), targetDir.c_str());
                    }
                    ImGui::Spacing();

                    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
                    if (!isValidName) ImGui::BeginDisabled();
                    bool createClicked = ImGui::Button("Create", ImVec2(120, 0));
                    if (!isValidName) ImGui::EndDisabled();

                    if (isValidName && (createClicked || enterPressed)) {
                        std::string diskSub = targetDir;
                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                        std::filesystem::path laDir = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                        std::error_code laec;
                        std::filesystem::create_directories(laDir, laec);

                        std::string createdPath = Eunoia::LevelAnimationManager::Get().CreateNewLevelAnimation(laDir.string(), inputName);

                        // Write .assetmeta sidecar
                        std::filesystem::path laPath(createdPath);
                        std::filesystem::path metaPath = laDir / (inputName + ".elanim.assetmeta");
                        std::ofstream metaFile(metaPath);
                        AssetID aid = AssetID::CreateRandom();
                        std::string vPath = (targetDir.empty() || targetDir == "/") ?
                            ("/" + inputName) : (targetDir + "/" + inputName);
                        if (metaFile.is_open()) {
                            metaFile << "# Eunoia-Editor Asset Sidecar Metadata\n"
                                     << "assetId: " << aid.ToString() << "\n"
                                     << "type: Level Animation\n"
                                     << "virtualPath: " << vPath << "\n";
                        }
#ifdef _WIN32
                        SetFileAttributesW(metaPath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
                        AssetMetadata vMeta;
                        vMeta.id = aid;
                        vMeta.type = AssetType::LevelAnimation;
                        vMeta.objectName = inputName;
                        vMeta.sourcePath = std::filesystem::relative(laPath, contentRootPath).string();
                        vMeta.virtualPath = vPath;
                        AssetRegistry::Get().RegisterAsset(vMeta);
                        TriggerBackgroundRegistrySync(contentRootPath);
                        OpenLevelAnimationEditor(createdPath);
                        AddLog("LogContent", "Created Level Animation: " + inputName + ".elanim in " + targetDir, 2);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                if (showNewAnimationBlendPopup) {
                    ImGui::OpenPopup("Create Animation Blend##BlendModal");
                    showNewAnimationBlendPopup = false;
                }
                if (ImGui::BeginPopupModal("Create Animation Blend##BlendModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Enter Animation Blend Name (.eblend):");
                    ImGui::SetNextItemWidth(300.0f);
                    ImGui::InputText("##BlendNameInput", newAnimationBlendNameBuf, sizeof(newAnimationBlendNameBuf));

                    std::string inputName = newAnimationBlendNameBuf;
                    while (!inputName.empty() && isspace((unsigned char)inputName.front())) inputName.erase(inputName.begin());
                    while (!inputName.empty() && isspace((unsigned char)inputName.back())) inputName.pop_back();

                    bool isValidName = !inputName.empty();
                    if (isValidName) {
                        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Will create: %s.eblend in %s", inputName.c_str(), targetDir.c_str());
                    }
                    ImGui::Spacing();

                    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
                    if (!isValidName) ImGui::BeginDisabled();
                    bool createClicked = ImGui::Button("Create", ImVec2(120, 0));
                    if (!isValidName) ImGui::EndDisabled();

                    if (isValidName && (createClicked || enterPressed)) {
                        std::string diskSub = targetDir;
                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                        std::filesystem::path blendDir = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                        std::error_code bec;
                        std::filesystem::create_directories(blendDir, bec);

                        std::string createdPath = Eunoia::AnimationBlendManager::Get().CreateNewAnimationBlend(blendDir.string(), inputName);

                        // Write .assetmeta sidecar
                        std::filesystem::path blendPath(createdPath);
                        std::filesystem::path metaPath = blendDir / (inputName + ".eblend.assetmeta");
                        std::ofstream metaFile(metaPath);
                        AssetID aid = AssetID::CreateRandom();
                        std::string vPath = (targetDir.empty() || targetDir == "/") ?
                            ("/" + inputName) : (targetDir + "/" + inputName);
                        if (metaFile.is_open()) {
                            metaFile << "# Eunoia-Editor Asset Sidecar Metadata\n"
                                     << "assetId: " << aid.ToString() << "\n"
                                     << "type: Animation Blend\n"
                                     << "virtualPath: " << vPath << "\n";
                        }
#ifdef _WIN32
                        SetFileAttributesW(metaPath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
                        // Register in AssetRegistry immediately
                        AssetMetadata vMeta;
                        vMeta.id = aid;
                        vMeta.type = AssetType::AnimationBlend;
                        vMeta.objectName = inputName;
                        std::filesystem::path rel = std::filesystem::relative(blendPath, contentRootPath, bec);
                        vMeta.sourcePath = bec ? (inputName + ".eblend") : rel.generic_string();
                        vMeta.virtualPath = vPath;
                        AssetRegistry::Get().RegisterAsset(vMeta);
                        TriggerBackgroundRegistrySync(contentRootPath);
                        OpenAnimationBlendEditor(createdPath);
                        AddLog("LogContent", "Created Animation Blend: " + inputName + ".eblend in " + targetDir, 2);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                if (showNewAnimatorGraphPopup) {
                    ImGui::OpenPopup("Create Animator Graph##AGModal");
                    showNewAnimatorGraphPopup = false;
                }
                if (ImGui::BeginPopupModal("Create Animator Graph##AGModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Enter Animator Graph Name (.eanimator):");
                    ImGui::SetNextItemWidth(300.0f);
                    ImGui::InputText("##AGNameInput", newAnimatorGraphNameBuf, sizeof(newAnimatorGraphNameBuf));

                    std::string inputName = newAnimatorGraphNameBuf;
                    while (!inputName.empty() && isspace((unsigned char)inputName.front())) inputName.erase(inputName.begin());
                    while (!inputName.empty() && isspace((unsigned char)inputName.back())) inputName.pop_back();

                    bool isValidName = !inputName.empty();
                    if (isValidName) {
                        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Will create: %s.eanimator in %s", inputName.c_str(), targetDir.c_str());
                    }
                    ImGui::Spacing();

                    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
                    if (!isValidName) ImGui::BeginDisabled();
                    bool createClicked = ImGui::Button("Create", ImVec2(120, 0));
                    if (!isValidName) ImGui::EndDisabled();

                    if (isValidName && (createClicked || enterPressed)) {
                        std::string diskSub = targetDir;
                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                        std::filesystem::path agDir = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                        std::error_code bec;
                        std::filesystem::create_directories(agDir, bec);

                        std::string createdPath = Eunoia::AnimatorGraphManager::Get().CreateNewAnimatorGraph(agDir.string(), inputName);

                        // Write .assetmeta sidecar
                        std::filesystem::path agPath(createdPath);
                        std::filesystem::path metaPath = agDir / (inputName + ".eanimator.assetmeta");
                        std::ofstream metaFile(metaPath);
                        AssetID aid = AssetID::CreateRandom();
                        std::string vPath = (targetDir.empty() || targetDir == "/") ?
                            ("/" + inputName) : (targetDir + "/" + inputName);
                        if (metaFile.is_open()) {
                            metaFile << "# Eunoia-Editor Asset Sidecar Metadata\n"
                                     << "assetId: " << aid.ToString() << "\n"
                                     << "type: Animator Graph\n"
                                     << "virtualPath: " << vPath << "\n";
                        }
#ifdef _WIN32
                        SetFileAttributesW(metaPath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
                        // Register in AssetRegistry immediately
                        AssetMetadata vMeta;
                        vMeta.id = aid;
                        vMeta.type = AssetType::AnimatorGraph;
                        vMeta.objectName = inputName;
                        std::filesystem::path rel = std::filesystem::relative(agPath, contentRootPath, bec);
                        vMeta.sourcePath = bec ? (inputName + ".eanimator") : rel.generic_string();
                        vMeta.virtualPath = vPath;
                        AssetRegistry::Get().RegisterAsset(vMeta);

                        TriggerBackgroundRegistrySync(contentRootPath);
                        OpenAnimatorGraphEditor(createdPath);
                        AddLog("LogContent", "Created Animator Graph: " + inputName + ".eanimator in " + targetDir, 2);

                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // Batch Animation Import Dialog: asks user to select skeleton for all currently importing animation files
                if (showBatchSkeletonImportPopup) {
                    ImGui::OpenPopup("Select Skeleton for Animation Batch##AnimBatchModal");
                    showBatchSkeletonImportPopup = false;
                }
                if (ImGui::BeginPopupModal("Select Skeleton for Animation Batch##AnimBatchModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Importing %zu Animation File(s)", pendingBatchImportAnimFiles.size());
                    ImGui::Text("Select the target Skeleton Mesh for all currently importing animations:");
                    ImGui::Spacing();

                    std::vector<std::string> skelMeshes = Eunoia::SkeletalAnimationManager::Get().FindProjectSkeletalMeshes(contentRootPath.string());
                    std::string comboLabel = selectedBatchImportSkeletonMesh.empty() 
                        ? "[Choose Target Skeleton Mesh...]" 
                        : std::filesystem::path(selectedBatchImportSkeletonMesh).filename().string();

                    ImGui::SetNextItemWidth(360.0f);
                    if (ImGui::BeginCombo("##BatchSkelCombo", comboLabel.c_str())) {
                        if (skelMeshes.empty()) {
                            ImGui::Selectable("No 3D Models in Content/", false, ImGuiSelectableFlags_Disabled);
                        }
                        for (const auto& sm : skelMeshes) {
                            std::string smName = std::filesystem::path(sm).filename().string();
                            bool isSel = (sm == selectedBatchImportSkeletonMesh);
                            if (ImGui::Selectable(smName.c_str(), isSel)) {
                                selectedBatchImportSkeletonMesh = sm;
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    bool hasSelected = !selectedBatchImportSkeletonMesh.empty();
                    if (!hasSelected) ImGui::BeginDisabled();
                    if (ImGui::Button("Apply Skeleton & Import", ImVec2(200, 0))) {
                        // Apply selected skeleton to all importing animations
                        for (const auto& animPath : pendingBatchImportAnimFiles) {
                            std::string metaPath = animPath + ".assetmeta";
                            std::ofstream mf(metaPath, std::ios::app);
                            if (mf.is_open()) {
                                mf << "targetSkeleton: " << selectedBatchImportSkeletonMesh << "\n";
                            }
                            AddLog("LogContent", "Imported Animation " + std::filesystem::path(animPath).filename().string() + " linked to skeleton: " + std::filesystem::path(selectedBatchImportSkeletonMesh).filename().string(), 0);
                        }
                        pendingBatchImportAnimFiles.clear();
                        TriggerBackgroundRegistrySync(contentRootPath);
                        ImGui::CloseCurrentPopup();
                    }
                    if (!hasSelected) ImGui::EndDisabled();

                    ImGui::SameLine();
                    if (ImGui::Button("Cancel Batch", ImVec2(120, 0))) {
                        pendingBatchImportAnimFiles.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // Bones Detected Import Options Dialog (Ask whether Skeletal Mesh or Animation when bones found)
                if (showBonesImportOptionPopup) {
                    ImGui::OpenPopup("Bones Detected - Import Options##BonesImportModal");
                    showBonesImportOptionPopup = false;
                }
                if (ImGui::BeginPopupModal("Bones Detected - Import Options##BonesImportModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string fileName = std::filesystem::path(pendingBonesImportFilePath).filename().string();
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Bones / Skeleton Detected in Asset:");
                    ImGui::TextDisabled("%s", fileName.c_str());
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    ImGui::Text("How would you like to import this asset?");
                    ImGui::Spacing();

                    ImGui::RadioButton("Skeletal Mesh (Rigged 3D Character / Mesh)", &bonesImportOptionType, 0);
                    ImGui::Indent(24.0f);
                    ImGui::TextDisabled("Imports as a rigged 3D mesh with bones hierarchy.\nThis model will be registered as a Skeleton Mesh and will show\nwherever a Skeleton is needed (e.g. in Animator Graphs and animation linking).");
                    ImGui::Unindent(24.0f);

                    ImGui::Spacing();
                    ImGui::RadioButton("Animation (Skeletal Animation Clip / Motion)", &bonesImportOptionType, 1);
                    ImGui::Indent(24.0f);
                    ImGui::TextDisabled("Imports skeletal motion/keyframe curves linked to a project Skeleton Mesh.");

                    if (bonesImportOptionType == 1) {
                        ImGui::Spacing();
                        ImGui::Text("Target Skeleton Mesh:");
                        std::vector<std::string> skelMeshes = Eunoia::SkeletalAnimationManager::Get().FindProjectSkeletalMeshes(contentRootPath.string());

                        std::string comboLabel = selectedBonesAnimationTargetSkeleton.empty()
                            ? "[Choose Target Skeleton Mesh...]"
                            : std::filesystem::path(selectedBonesAnimationTargetSkeleton).filename().string();

                        ImGui::SetNextItemWidth(380.0f);
                        if (ImGui::BeginCombo("##BonesTargetSkelCombo", comboLabel.c_str())) {
                            if (skelMeshes.empty()) {
                                ImGui::Selectable("No Skeletal Meshes found in Content/", false, ImGuiSelectableFlags_Disabled);
                            }
                            for (const auto& sm : skelMeshes) {
                                std::string smName = std::filesystem::path(sm).filename().string();
                                bool isSel = (sm == selectedBonesAnimationTargetSkeleton);
                                if (ImGui::Selectable(smName.c_str(), isSel)) {
                                    selectedBonesAnimationTargetSkeleton = sm;
                                }
                            }
                            ImGui::EndCombo();
                        }

                        if (skelMeshes.empty()) {
                            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "(Note: No Skeletal Meshes found yet. You can import this model as a Skeletal Mesh first)");
                        }
                    }
                    ImGui::Unindent(24.0f);

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    if (ImGui::Button("Confirm & Import", ImVec2(160, 0))) {
                        std::filesystem::path fPath(pendingBonesImportFilePath);
                        std::string metaPath = pendingBonesImportFilePath + ".assetmeta";

                        AssetMetadata meta;
                        AssetMetadata* existingMeta = AssetRegistry::Get().FindByNameOrPath(fPath.filename().string());
                        if (existingMeta) {
                            meta = *existingMeta;
                        } else {
                            AssetRegistry::Get().ReadOrGenerateSidecar(fPath, meta);
                            meta.objectName = fPath.stem().string();
                            std::error_code bec;
                            std::filesystem::path rel = std::filesystem::relative(fPath, contentRootPath, bec);
                            meta.sourcePath = bec ? fPath.filename().string() : rel.generic_string();
                            meta.virtualPath = currentVirtualDir + "/" + meta.objectName;
                        }

                        if (bonesImportOptionType == 0) {
                            // Skeletal Mesh
                            meta.type = AssetType::SkeletalMesh;
                            AssetRegistry::Get().RegisterAsset(meta);

                            std::ofstream mf(metaPath);
                            if (mf.is_open()) {
                                mf << "# Eunoia-Editor Asset Sidecar Metadata\n";
                                mf << "assetId: " << meta.id.ToString() << "\n";
                                mf << "type: Skeletal Mesh\n";
                                mf << "isSkeletalMesh: true\n";
                                mf << "hasSkeleton: true\n";
                                mf << "virtualPath: " << meta.virtualPath << "\n";
                            }
#ifdef _WIN32
                            SetFileAttributesW(std::filesystem::path(metaPath).wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
                            AddLog("LogContent", "Registered Skeletal Mesh: " + fPath.filename().string() + " (Ready for Animator Graph)", 2);
                        } else {
                            // Animation
                            meta.type = AssetType::Animation;
                            AssetRegistry::Get().RegisterAsset(meta);

                            std::ofstream mf(metaPath);
                            if (mf.is_open()) {
                                mf << "# Eunoia-Editor Asset Sidecar Metadata\n";
                                mf << "assetId: " << meta.id.ToString() << "\n";
                                mf << "type: Animation\n";
                                mf << "isAnimation: true\n";
                                mf << "targetSkeleton: " << selectedBonesAnimationTargetSkeleton << "\n";
                                mf << "virtualPath: " << meta.virtualPath << "\n";
                            }
#ifdef _WIN32
                            SetFileAttributesW(std::filesystem::path(metaPath).wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
                            std::string skelName = selectedBonesAnimationTargetSkeleton.empty() ? "None" : std::filesystem::path(selectedBonesAnimationTargetSkeleton).filename().string();
                            AddLog("LogContent", "Registered Animation: " + fPath.filename().string() + " linked to Skeleton: " + skelName, 2);
                        }

                        TriggerBackgroundRegistrySync(contentRootPath);
                        pendingBonesImportFilePath = "";
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                        pendingBonesImportFilePath = "";
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }

                // Two-Pane Content Browser Layout (Left: Folders, Right: Assets)
                float maxTreeW = ImGui::GetWindowWidth() - 200.0f;
                if (maxTreeW < 140.0f) maxTreeW = 140.0f;
                contentBrowserFolderTreeWidth = std::clamp(contentBrowserFolderTreeWidth, 120.0f, maxTreeW);

                ImGui::BeginChild("CBFolderTreePane", ImVec2(contentBrowserFolderTreeWidth, 0), true);
                ImGui::TextColored(ImVec4(0.443f, 0.443f, 0.478f, 1.0f), "FOLDERS"); // dev.md text-gray-500
                ImGui::Separator();

                // Directory Hierarchy with highlighting and drag/drop
                auto RenderVirtualDirNode = [&](auto& self, const std::string& vDir) -> void {
                    std::vector<std::string> subdirs = AssetRegistry::Get().GetSubdirectories(vDir);
                    subdirs.erase(std::remove_if(subdirs.begin(), subdirs.end(), [](const std::string& s) {
                        return s.find(".internal") != std::string::npos;
                    }), subdirs.end());
                    std::string dirName = AssetPath::GetObjectName(vDir);
                    if (vDir == "/Game") dirName = "Content";

                    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                    bool isCurrent = (vDir == currentVirtualDir);
                    if (isCurrent) nodeFlags |= ImGuiTreeNodeFlags_Selected;
                    if (subdirs.empty()) nodeFlags |= ImGuiTreeNodeFlags_Leaf;
                    else nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;

                    if (isCurrent) {
                        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.00f, 0.40f, 1.00f, 1.00f)); // #0066ff
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.10f, 0.50f, 1.00f, 1.00f));
                        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.00f, 0.35f, 0.90f, 1.00f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                    }

                    if (m_cbFolderStateNeedsApply && !subdirs.empty() && m_cbAppliedFolders.find(vDir) == m_cbAppliedFolders.end()) {
                        auto it = scene.contentBrowserFolderOpenState.find(vDir);
                        if (it != scene.contentBrowserFolderOpenState.end()) {
                            ImGui::SetNextItemOpen(it->second);
                        }
                        m_cbAppliedFolders.insert(vDir);
                    }

                    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)std::hash<std::string>{}(vDir), nodeFlags, "%s", dirName.c_str());

                    if (isCurrent) {
                        ImGui::PopStyleColor(4);
                    }

                    if (!subdirs.empty()) {
                        scene.contentBrowserFolderOpenState[vDir] = nodeOpen;
                    }

                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                        currentVirtualDir = vDir;
                        selectedContentItem = "";
                        selectedContentItems.clear();
                        lastSelectedContentItem = "";
                    }

                    // Drag source for folder in tree
                    if (vDir != "/Game" && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        std::string payloadStr = "DIR:" + vDir + "\n";
                        ImGui::SetDragDropPayload("CONTENT_DRAG", payloadStr.c_str(), payloadStr.length() + 1);
                        ImGui::Text("%s", dirName.c_str());
                        ImGui::EndDragDropSource();
                    }

                    // Drop target on folder tree node
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                            std::string payloadData((const char*)payload->Data);
                            std::stringstream pss(payloadData);
                            std::string item;
                            while (std::getline(pss, item, '\n')) {
                                if (item.empty()) continue;
                                if (item.rfind("DIR:", 0) == 0) {
                                    MoveFolderToDirectory(item.substr(4), vDir, contentRootPath);
                                } else if (item.rfind("ASSET:", 0) == 0) {
                                    const AssetMetadata* m = AssetRegistry::Get().FindByPath(item.substr(6));
                                    if (m) MoveAssetToDirectory(m->id, vDir, contentRootPath);
                                }
                            }
                            TriggerBackgroundRegistrySync(contentRootPath);
                        } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                            std::string idStr((const char*)payload->Data);
                            MoveAssetToDirectory(AssetID::FromString(idStr), vDir, contentRootPath);
                            TriggerBackgroundRegistrySync(contentRootPath);
                        }
                        ImGui::EndDragDropTarget();
                    }

                    // Right click context menu on folder in tree
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Open Folder")) {
                            currentVirtualDir = vDir;
                            selectedContentItem = "";
                            selectedContentItems.clear();
                            lastSelectedContentItem = "";
                        }
                        if (vDir != "/Game") {
                            if (ImGui::MenuItem("Rename Folder...", "F2")) {
                                OpenRenameFolderModal(vDir);
                            }
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("New Folder...")) {
                            popupTargetVirtualDir = vDir;
                            showNewFolderPopup = true;
                            snprintf(newFolderNameBuf, sizeof(newFolderNameBuf), "NewFolder");
                        }
                        if (ImGui::MenuItem("New Material (.emat)...")) {
                            popupTargetVirtualDir = vDir;
                            showNewMaterialPopup = true;
                            snprintf(newMaterialNameBuf, sizeof(newMaterialNameBuf), "M_NewMaterial");
                        }
                        if (ImGui::MenuItem("New Level Animation (.elanim)...")) {
                            popupTargetVirtualDir = vDir;
                            showNewLevelAnimationPopup = true;
                            snprintf(newLevelAnimationNameBuf, sizeof(newLevelAnimationNameBuf), "LA_NewAnimation");
                        }
                        if (ImGui::MenuItem("New Behaviour (.behaviour)...")) {
                            popupTargetVirtualDir = vDir;
                            showNewBehaviourAssetPopup = true;
                            snprintf(newBehaviourAssetNameBuf, sizeof(newBehaviourAssetNameBuf), "B_NewActor");
                        }
                        if (ImGui::MenuItem("New Behaviour Script (.cpp)...")) {
                            popupTargetVirtualDir = vDir;
                            showNewBehaviourPopup = true;
                            std::string defName = "NewBehaviour";
                            if (IsBehaviourNameAlreadyExists(defName)) {
                                int idx = 1;
                                while (IsBehaviourNameAlreadyExists("NewBehaviour_" + std::to_string(idx))) idx++;
                                defName = "NewBehaviour_" + std::to_string(idx);
                            }
                            snprintf(newBehaviourNameBuf, sizeof(newBehaviourNameBuf), "%s", defName.c_str());
                        }
                        if (ImGui::MenuItem("New User Interface")) {
                            popupTargetVirtualDir = vDir;
                            showNewUIPopup = true;
                            snprintf(newUINameBuf, sizeof(newUINameBuf), "UI_NewInterface");
                        }
                        if (ImGui::MenuItem("New Animator Graph (.eanimator)...")) {
                            popupTargetVirtualDir = vDir;
                            showNewAnimatorGraphPopup = true;
                            snprintf(newAnimatorGraphNameBuf, sizeof(newAnimatorGraphNameBuf), "AG_NewAnimator");
                        }
                        if (ImGui::MenuItem("New Node Behaviour (.enb)...")) {
                            popupTargetVirtualDir = vDir;
                            showNewNodeBehaviourPopup = true;
                            snprintf(newNodeBehaviourNameBuf, sizeof(newNodeBehaviourNameBuf), "NB_NewBehaviour");
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Import Model...")) {
                            popupTargetVirtualDir = vDir;
                            ImportMeshWithSavePrompt(scene);
                        }
                        if (ImGui::MenuItem("Show in Explorer")) {
                            std::string diskSub = vDir;
                            if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                            if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                            std::filesystem::path folderDisk = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                            std::string cmd = "explorer \"" + folderDisk.string() + "\"";
                            system(cmd.c_str());
                        }
                        if (vDir != "/Game") {
                            ImGui::Separator();
                            if (ImGui::MenuItem("Delete Folder")) {
                                std::string diskSub = vDir;
                                if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                                if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                                std::filesystem::path folderDisk = contentRootPath / diskSub;
                                std::error_code delEc;
                                std::filesystem::remove_all(folderDisk, delEc);
                                if (currentVirtualDir == vDir || currentVirtualDir.rfind(vDir + "/", 0) == 0) {
                                    currentVirtualDir = AssetPath::GetDirectory(vDir);
                                }
                                TriggerBackgroundRegistrySync(contentRootPath);
                            }
                        }
                        ImGui::EndPopup();
                    }

                    if (nodeOpen) {
                        for (const auto& sub : subdirs) {
                            self(self, sub);
                        }
                        ImGui::TreePop();
                    }
                };

                RenderVirtualDirNode(RenderVirtualDirNode, "/Game");
                ImGui::EndChild();

                ImGui::SameLine(0.0f, 2.0f);

                // Horizontal Splitter between Folders tree and Asset items
                float splitterW = 5.0f;
                ImVec2 splitterPos = ImGui::GetCursorScreenPos();
                float splitterH = ImGui::GetContentRegionAvail().y;

                ImGui::InvisibleButton("##CBFolderSplitter", ImVec2(splitterW, splitterH > 10.0f ? splitterH : -1.0f));
                bool splitterHovered = ImGui::IsItemHovered();
                bool splitterActive = ImGui::IsItemActive();

                if (splitterHovered || splitterActive) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                }

                if (splitterActive) {
                    float delta = ImGui::GetIO().MouseDelta.x;
                    if (delta != 0.0f) {
                        contentBrowserFolderTreeWidth = std::clamp(contentBrowserFolderTreeWidth + delta, 120.0f, maxTreeW);
                    }
                }
                if (ImGui::IsItemDeactivated()) {
                    SaveEditorConfig();
                }
                if (splitterHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    contentBrowserFolderTreeWidth = 220.0f;
                    SaveEditorConfig();
                }

                // Draw splitter visual indicator line
                ImU32 splitterCol = splitterActive ? IM_COL32(0, 122, 204, 255) :
                                    (splitterHovered ? IM_COL32(0, 122, 204, 200) : IM_COL32(45, 48, 54, 255));
                ImGui::GetWindowDrawList()->AddRectFilled(splitterPos, ImVec2(splitterPos.x + splitterW, splitterPos.y + (splitterH > 10.0f ? splitterH : 200.0f)), splitterCol);

                ImGui::SameLine(0.0f, 2.0f);

                // Right Pane: Asset Table backed by AssetRegistry (dev.md Section 14)
                ImGui::BeginChild("CBAssetListPane", ImVec2(0, 0), true);

                // Query assets in current virtual directory or search across registry
                std::string searchStr = contentBrowserSearch;
                std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

                std::vector<AssetMetadata> displayAssets;
                if (!searchStr.empty()) {
                    // Global search
                    if (searchStr.rfind("type:", 0) == 0) {
                        std::string typeFilter = searchStr.substr(5);
                        displayAssets = AssetRegistry::Get().FindByType(StringToAssetType(typeFilter));
                    } else if (searchStr.rfind("path:", 0) == 0) {
                        std::string pathFilter = searchStr.substr(5);
                        displayAssets = AssetRegistry::Get().FindInDirectory(pathFilter, true);
                    } else {
                        // Search by name, path, or AssetID
                        const auto& all = AssetRegistry::Get().GetAllAssets();
                        for (const auto& pair : all) {
                            const auto& m = pair.second;
                            std::string nLower = m.objectName;
                            std::transform(nLower.begin(), nLower.end(), nLower.begin(), ::tolower);
                            std::string pLower = m.virtualPath;
                            std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::tolower);
                            std::string idLower = m.id.ToString();
                            std::transform(idLower.begin(), idLower.end(), idLower.begin(), ::tolower);

                            if (nLower.find(searchStr) != std::string::npos ||
                                pLower.find(searchStr) != std::string::npos ||
                                idLower.find(searchStr) != std::string::npos) {
                                displayAssets.push_back(m);
                            }
                        }
                    }
                } else {
                    displayAssets = AssetRegistry::Get().FindInDirectory(currentVirtualDir, false);
                }

                // Subdirectories in current virtual folder
                std::vector<std::string> currentSubdirs = AssetRegistry::Get().GetSubdirectories(currentVirtualDir);
                currentSubdirs.erase(std::remove_if(currentSubdirs.begin(), currentSubdirs.end(), [](const std::string& s) {
                    return s.find(".internal") != std::string::npos;
                }), currentSubdirs.end());

                // Build ordered list of all visible item keys for Windows Shift+Click contiguous selection
                std::vector<std::string> visibleItemKeys;
                if (searchStr.empty()) {
                    for (const auto& subDir : currentSubdirs) {
                        visibleItemKeys.push_back("DIR:" + subDir);
                    }
                }
                std::unordered_set<std::string> seenItemKeys;
                for (const auto& meta : displayAssets) {
                    std::string sPath = meta.sourcePath;
                    std::transform(sPath.begin(), sPath.end(), sPath.begin(), ::tolower);
                    std::string vPath = meta.virtualPath;
                    std::transform(vPath.begin(), vPath.end(), vPath.begin(), ::tolower);
                    std::string oName = meta.objectName;
                    std::transform(oName.begin(), oName.end(), oName.begin(), ::tolower);

                    if (sPath.find(".assetmeta") != std::string::npos ||
                        sPath.find(".meta") != std::string::npos ||
                        sPath.find(".metadata") != std::string::npos ||
                        sPath.find(".html") != std::string::npos ||
                        sPath.find(".elogs") != std::string::npos ||
                        sPath.find(".log") != std::string::npos ||
                        sPath.find(".internal") != std::string::npos ||
                        vPath.find(".assetmeta") != std::string::npos ||
                        vPath.find(".meta") != std::string::npos ||
                        vPath.find(".metadata") != std::string::npos ||
                        vPath.find(".html") != std::string::npos ||
                        vPath.find(".elogs") != std::string::npos ||
                        vPath.find(".log") != std::string::npos ||
                        vPath.find(".internal") != std::string::npos ||
                        oName.find(".assetmeta") != std::string::npos ||
                        oName.find(".meta") != std::string::npos ||
                        oName.find(".internal") != std::string::npos ||
                        oName.find(".metadata") != std::string::npos) {
                        continue;
                    }

                    std::string itemKey = "ASSET:" + meta.virtualPath;
                    if (seenItemKeys.count(itemKey) == 0) {
                        seenItemKeys.insert(itemKey);
                        visibleItemKeys.push_back(itemKey);
                    }
                }

                // Selection click handler for Windows Ctrl/Shift multi-selection
                auto HandleItemClick = [&](const std::string& itemKey, const std::string& primaryVal) {
                    ImGuiIO& io = ImGui::GetIO();
                    if (io.KeyCtrl) {
                        if (selectedContentItems.count(itemKey)) {
                            selectedContentItems.erase(itemKey);
                            if (selectedContentItem == primaryVal) {
                                selectedContentItem = selectedContentItems.empty() ? "" : *selectedContentItems.begin();
                            }
                        } else {
                            selectedContentItems.insert(itemKey);
                            selectedContentItem = primaryVal;
                        }
                        lastSelectedContentItem = itemKey;
                    } else if (io.KeyShift && !lastSelectedContentItem.empty()) {
                        int idx1 = -1, idx2 = -1;
                        for (size_t i = 0; i < visibleItemKeys.size(); ++i) {
                            if (visibleItemKeys[i] == lastSelectedContentItem) idx1 = (int)i;
                            if (visibleItemKeys[i] == itemKey) idx2 = (int)i;
                        }
                        if (idx1 != -1 && idx2 != -1) {
                            int start = std::min(idx1, idx2);
                            int end = std::max(idx1, idx2);
                            selectedContentItems.clear();
                            for (int i = start; i <= end; ++i) {
                                selectedContentItems.insert(visibleItemKeys[i]);
                            }
                            selectedContentItem = primaryVal;
                        } else {
                            selectedContentItems.clear();
                            selectedContentItems.insert(itemKey);
                            selectedContentItem = primaryVal;
                            lastSelectedContentItem = itemKey;
                        }
                    } else {
                        selectedContentItems.clear();
                        selectedContentItems.insert(itemKey);
                        selectedContentItem = primaryVal;
                        lastSelectedContentItem = itemKey;
                    }
                };

                // Unified Content Item structure for Content View cards
                struct CBItem {
                    bool isFolder = false;
                    std::string subDir;
                    AssetMetadata meta;
                    std::string itemKey;
                    std::string primaryVal;
                    std::string displayName;
                };

                std::vector<CBItem> allItems;
                std::unordered_set<std::string> seenDisplayKeys;

                if (searchStr.empty()) {
                    for (const auto& subDir : currentSubdirs) {
                        CBItem item;
                        item.isFolder = true;
                        item.subDir = subDir;
                        item.itemKey = "DIR:" + subDir;
                        item.primaryVal = subDir;
                        item.displayName = AssetPath::GetObjectName(subDir);
                        allItems.push_back(std::move(item));
                    }
                }

                for (const auto& meta : displayAssets) {
                    std::string sPath = meta.sourcePath;
                    std::transform(sPath.begin(), sPath.end(), sPath.begin(), ::tolower);
                    std::string vPath = meta.virtualPath;
                    std::transform(vPath.begin(), vPath.end(), vPath.begin(), ::tolower);
                    std::string oName = meta.objectName;
                    std::transform(oName.begin(), oName.end(), oName.begin(), ::tolower);

                    if (sPath.find(".assetmeta") != std::string::npos ||
                        sPath.find(".meta") != std::string::npos ||
                        sPath.find(".metadata") != std::string::npos ||
                        sPath.find(".html") != std::string::npos ||
                        sPath.find(".elogs") != std::string::npos ||
                        sPath.find(".log") != std::string::npos ||
                        vPath.find(".assetmeta") != std::string::npos ||
                        vPath.find(".meta") != std::string::npos ||
                        vPath.find(".metadata") != std::string::npos ||
                        vPath.find(".html") != std::string::npos ||
                        vPath.find(".elogs") != std::string::npos ||
                        vPath.find(".log") != std::string::npos ||
                        oName.find(".assetmeta") != std::string::npos ||
                        oName.find(".meta") != std::string::npos ||
                        oName.find(".metadata") != std::string::npos) {
                        continue;
                    }

                    std::string key = "ASSET:" + meta.virtualPath;
                    if (seenDisplayKeys.count(key) > 0 || (!sPath.empty() && seenDisplayKeys.count("SRC:" + sPath) > 0)) {
                        continue;
                    }
                    seenDisplayKeys.insert(key);
                    if (!sPath.empty()) seenDisplayKeys.insert("SRC:" + sPath);

                    CBItem item;
                    item.isFolder = false;
                    item.meta = meta;
                    item.itemKey = key;
                    item.primaryVal = meta.virtualPath;
                    item.displayName = meta.objectName;
                    allItems.push_back(std::move(item));
                }

                // Check physical directory for project files (.euproject, .eproject) or standalone files not in registry
                if (searchStr.empty()) {
                    std::string diskSub = currentVirtualDir;
                    if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                    if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                    std::filesystem::path currentDiskPath = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);

                    std::error_code dirEc;
                    if (std::filesystem::exists(currentDiskPath, dirEc) && std::filesystem::is_directory(currentDiskPath, dirEc)) {
                        for (const auto& entry : std::filesystem::directory_iterator(currentDiskPath, dirEc)) {
                            if (!entry.is_directory(dirEc)) {
                                std::string ext = entry.path().extension().string();
                                std::string extLower = ext;
                                std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);

                                if (extLower == ".euproject" || extLower == ".eproject") {
                                    std::filesystem::path relPath = std::filesystem::relative(entry.path(), contentRootPath, dirEc);
                                    std::string srcPath = dirEc ? entry.path().filename().string() : relPath.generic_string();
                                    std::string vPath = currentVirtualDir + "/" + entry.path().stem().string();

                                    std::string key = "PROJ:" + entry.path().string();
                                    if (seenDisplayKeys.count(key) == 0 && seenDisplayKeys.count("SRC:" + srcPath) == 0) {
                                        seenDisplayKeys.insert(key);
                                        seenDisplayKeys.insert("SRC:" + srcPath);

                                        CBItem projItem;
                                        projItem.isFolder = false;
                                        projItem.meta.id = AssetID::CreateRandom();
                                        projItem.meta.type = AssetType::Project;
                                        projItem.meta.objectName = entry.path().stem().string();
                                        projItem.meta.sourcePath = srcPath;
                                        projItem.meta.virtualPath = vPath;
                                        projItem.itemKey = key;
                                        projItem.primaryVal = vPath;
                                        projItem.displayName = entry.path().filename().string();
                                        allItems.push_back(std::move(projItem));
                                    }
                                }
                            }
                        }
                    }
                }

                if (allItems.empty()) {
                    ImGui::Spacing();
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.55f, 0.62f, 0.72f, 1.0f), "    (No items found in this directory. Right-click here to create)");
                } else {
                    float availWidth = ImGui::GetContentRegionAvail().x;
                    float cardW = contentThumbnailSize;
                    if (cardW < 72.0f) cardW = 72.0f;
                    if (cardW > 160.0f) cardW = 160.0f;
                    float cardH = cardW * 1.20f;
                    float spacing = 8.0f;

                    int itemsPerRow = (int)((availWidth + spacing) / (cardW + spacing));
                    if (itemsPerRow < 1) itemsPerRow = 1;

                    ImDrawList* drawList = ImGui::GetWindowDrawList();

                    for (size_t idx = 0; idx < allItems.size(); ++idx) {
                        const auto& item = allItems[idx];
                        if (idx > 0 && (idx % itemsPerRow) != 0) {
                            ImGui::SameLine(0, spacing);
                        }

                        ImGui::PushID(item.itemKey.c_str());

                        bool isSelected = (selectedContentItems.count(item.itemKey) > 0 || selectedContentItem == item.primaryVal);
                        ImVec2 cardPos = ImGui::GetCursorScreenPos();
                        ImVec2 cardSize(cardW, cardH);
                        ImVec2 pMin = cardPos;
                        ImVec2 pMax = ImVec2(cardPos.x + cardW, cardPos.y + cardH);

                        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));

                        bool clicked = ImGui::Selectable("##cbCard", isSelected, ImGuiSelectableFlags_AllowDoubleClick, cardSize);

                        ImGui::PopStyleColor(3);

                        bool hovered = ImGui::IsItemHovered();

                        if (clicked) {
                            HandleItemClick(item.itemKey, item.primaryVal);
                        }

                        // Double-click action
                        if (hovered && ImGui::IsMouseDoubleClicked(0)) {
                            if (item.isFolder) {
                                currentVirtualDir = item.subDir;
                                selectedContentItem = "";
                                selectedContentItems.clear();
                                lastSelectedContentItem = "";
                            } else {
                                if (item.meta.type == AssetType::Material) {
                                    std::filesystem::path fullMat = AssetManager::Get().ResolvePhysicalOrCookedPath(item.meta.sourcePath);
                                    std::error_code ec;
                                    if (fullMat.empty() || !std::filesystem::exists(fullMat, ec) || std::filesystem::file_size(fullMat, ec) == 0) {
                                        fullMat = contentRootPath / item.meta.sourcePath;
                                    }
                                    OpenMaterialEditor(fullMat.string());
                                } else if (item.meta.type == AssetType::Skybox) {
                                    std::filesystem::path fullSky = contentRootPath / item.meta.sourcePath;
                                    OpenSkyboxEditor(fullSky.string());
                                } else if (item.meta.type == AssetType::Water) {
                                    std::filesystem::path fullWater = contentRootPath / item.meta.sourcePath;
                                    OpenWaterEditor(fullWater.string());
                                } else if (item.meta.type == AssetType::NodeBehaviour || item.meta.sourcePath.find(".enb") != std::string::npos) {
                                    std::filesystem::path fullNb = contentRootPath / item.meta.sourcePath;
                                    OpenNodeBehaviourEditor(fullNb.string());
                                    AddLog("LogContent", "Opened Node Behaviour: " + item.meta.objectName, 2);
                                } else if (item.meta.type == AssetType::AnimatorGraph || item.meta.sourcePath.find(".eanimator") != std::string::npos || item.meta.sourcePath.find(".animator") != std::string::npos) {
                                    std::filesystem::path fullAg = contentRootPath / item.meta.sourcePath;
                                    OpenAnimatorGraphEditor(fullAg.string());
                                    AddLog("LogContent", "Opened Animator Graph: " + item.meta.objectName, 2);
                                } else if (item.meta.type == AssetType::UserInterface || item.meta.type == AssetType::UserInterface3D) {
                                    std::filesystem::path fullUI = contentRootPath / item.meta.sourcePath;
                                    OpenUIEditor(fullUI.string());
                                    AddLog("LogContent", "Opened User Interface: " + item.meta.objectName, 2);
                                } else if (item.meta.type == AssetType::Mesh || item.meta.type == AssetType::SkeletalMesh) {
                                    std::filesystem::path fullMesh = AssetManager::Get().ResolvePhysicalOrCookedPath(item.meta.sourcePath);
                                    std::error_code ec;
                                    if (fullMesh.empty() || !std::filesystem::exists(fullMesh, ec) || std::filesystem::file_size(fullMesh, ec) == 0) {
                                        fullMesh = contentRootPath / item.meta.sourcePath;
                                    }
                                    GameObject& newObj = scene.AddImportedMesh(fullMesh.string(), glm::vec3(0.0f, 0.5f, 0.0f));
                                    scene.selectedId = newObj.id;
                                    AddLog("LogMesh", "Spawned 3D Mesh in level: " + item.meta.objectName, 2);
                                } else if (item.meta.type == AssetType::Level) {
                                    std::filesystem::path fullLevel = AssetManager::Get().ResolvePhysicalOrCookedPath(item.meta.sourcePath);
                                    std::error_code ec;
                                    if (fullLevel.empty() || !std::filesystem::exists(fullLevel, ec) || std::filesystem::file_size(fullLevel, ec) == 0) {
                                        fullLevel = contentRootPath / item.meta.sourcePath;
                                    }
                                    OpenLevelFromPath(scene, fullLevel.string());
                                } else if (item.meta.type == AssetType::Behaviour || item.meta.sourcePath.find(".behaviour") != std::string::npos) {
                                    std::filesystem::path fullBeh = contentRootPath / item.meta.sourcePath;
                                    OpenBehaviourEditor(fullBeh.string());
                                    AddLog("LogContent", "Opened Behaviour Blueprint: " + item.meta.objectName, 2);
                                } else if (item.meta.type == AssetType::Script) {
                                    std::filesystem::path fullScript = contentRootPath / item.meta.sourcePath;
                                    OpenScriptInCodeEditor(fullScript.string());
                                } else if (item.meta.type == AssetType::Project) {
                                    std::filesystem::path fullProjPath = contentRootPath / item.meta.sourcePath;
                                    std::error_code ec;
                                    if (!std::filesystem::exists(fullProjPath, ec)) {
                                        fullProjPath = activeProjectRoot / item.meta.sourcePath;
                                    }
                                    std::filesystem::path projRootDir = fullProjPath.parent_path();
                                    if (projRootDir.filename() == "Content") {
                                        projRootDir = projRootDir.parent_path();
                                    }
                                    if (currentCamera) {
                                        LoadProject(projRootDir, scene, *currentCamera);
                                    }
                                } else {
                                    OpenReferenceViewer(item.meta.id);
                                }
                            }
                        }

                        // Drag & Drop Source
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                            std::string dragStr = "";
                            if (selectedContentItems.count(item.itemKey) > 0 && selectedContentItems.size() > 1) {
                                for (const auto& selKey : selectedContentItems) dragStr += selKey + "\n";
                            } else {
                                dragStr = item.itemKey + "\n";
                            }
                            ImGui::SetDragDropPayload("CONTENT_DRAG", dragStr.c_str(), dragStr.length() + 1);

                            if (!item.isFolder) {
                                std::string idStr = item.meta.id.ToString();
                                ImGui::SetDragDropPayload("ASSET_ID", idStr.c_str(), idStr.length() + 1);
                            }

                            if (selectedContentItems.size() > 1 && selectedContentItems.count(item.itemKey)) {
                                ImGui::Text("Moving %d items", (int)selectedContentItems.size());
                            } else {
                                const char* icon = item.isFolder ? "" : GetAssetTypeIcon(item.meta.type);
                                ImGui::Text("%s %s", icon, item.displayName.c_str());
                            }
                            if (!item.isFolder) {
                                if (item.meta.type == AssetType::Mesh || item.meta.type == AssetType::SkeletalMesh) {
                                    ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "[Drag to Viewport to spawn in Level]");
                                } else if (item.meta.type == AssetType::Level) {
                                    ImGui::TextColored(ImVec4(0.30f, 0.85f, 0.95f, 1.0f), "[Drag to Viewport to open Level]");
                                }
                            }
                            ImGui::EndDragDropSource();
                        }

                        // Drop Target on Folder Cards
                        if (item.isFolder && ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                                std::string payloadData((const char*)payload->Data);
                                std::stringstream pss(payloadData);
                                std::string dragItem;
                                while (std::getline(pss, dragItem, '\n')) {
                                    if (dragItem.empty()) continue;
                                    if (dragItem.rfind("DIR:", 0) == 0) {
                                        MoveFolderToDirectory(dragItem.substr(4), item.subDir, contentRootPath);
                                    } else if (dragItem.rfind("ASSET:", 0) == 0) {
                                        const AssetMetadata* m = AssetRegistry::Get().FindByPath(dragItem.substr(6));
                                        if (m) MoveAssetToDirectory(m->id, item.subDir, contentRootPath);
                                    }
                                }
                                TriggerBackgroundRegistrySync(contentRootPath);
                            } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                                std::string idStr((const char*)payload->Data);
                                MoveAssetToDirectory(AssetID::FromString(idStr), item.subDir, contentRootPath);
                                TriggerBackgroundRegistrySync(contentRootPath);
                            }
                            ImGui::EndDragDropTarget();
                        }

                        // Context Menus
                        if (item.isFolder) {
                            if (ImGui::BeginPopupContextItem("FolderCardCtx")) {
                                if (ImGui::MenuItem("Open Folder")) {
                                    currentVirtualDir = item.subDir;
                                    selectedContentItem = "";
                                    selectedContentItems.clear();
                                    lastSelectedContentItem = "";
                                }
                                if (item.subDir != "/Game") {
                                    if (ImGui::MenuItem("Rename Folder...", "F2")) {
                                        OpenRenameFolderModal(item.subDir);
                                    }
                                }
                                ImGui::Separator();
                                if (ImGui::MenuItem("New Folder")) {
                                    popupTargetVirtualDir = item.subDir;
                                    showNewFolderPopup = true;
                                    snprintf(newFolderNameBuf, sizeof(newFolderNameBuf), "NewFolder");
                                }
                                if (ImGui::MenuItem("New Material")) {
                                    popupTargetVirtualDir = item.subDir;
                                    showNewMaterialPopup = true;
                                    snprintf(newMaterialNameBuf, sizeof(newMaterialNameBuf), "M_NewMaterial");
                                }
                                if (ImGui::MenuItem("New Level Animation (.elanim)...")) {
                                    popupTargetVirtualDir = item.subDir;
                                    showNewLevelAnimationPopup = true;
                                    snprintf(newLevelAnimationNameBuf, sizeof(newLevelAnimationNameBuf), "LA_NewAnimation");
                                }
                                if (ImGui::MenuItem("New Behaviour (.behaviour)...")) {
                                    popupTargetVirtualDir = item.subDir;
                                    showNewBehaviourAssetPopup = true;
                                    snprintf(newBehaviourAssetNameBuf, sizeof(newBehaviourAssetNameBuf), "B_NewActor");
                                }
                                if (ImGui::MenuItem("New Behaviour Script (.cpp)...")) {
                                    popupTargetVirtualDir = item.subDir;
                                    showNewBehaviourPopup = true;
                                    std::string defName = "NewBehaviour";
                                    if (IsBehaviourNameAlreadyExists(defName)) {
                                        int bIdx = 1;
                                        while (IsBehaviourNameAlreadyExists("NewBehaviour_" + std::to_string(bIdx))) bIdx++;
                                        defName = "NewBehaviour_" + std::to_string(bIdx);
                                    }
                                    snprintf(newBehaviourNameBuf, sizeof(newBehaviourNameBuf), "%s", defName.c_str());
                                }
                                if (ImGui::MenuItem("New User Interface")) {
                                    popupTargetVirtualDir = item.subDir;
                                    showNewUIPopup = true;
                                    snprintf(newUINameBuf, sizeof(newUINameBuf), "UI_NewInterface");
                                }
                                if (ImGui::MenuItem("New Animator Graph (.eanimator)...")) {
                                    popupTargetVirtualDir = item.subDir;
                                    showNewAnimatorGraphPopup = true;
                                    snprintf(newAnimatorGraphNameBuf, sizeof(newAnimatorGraphNameBuf), "AG_NewAnimator");
                                }
                        if (ImGui::MenuItem("New Node Behaviour (.enb)...")) {
                                    popupTargetVirtualDir = item.subDir;
                                    showNewNodeBehaviourPopup = true;
                                    snprintf(newNodeBehaviourNameBuf, sizeof(newNodeBehaviourNameBuf), "NB_NewBehaviour");
                                }
                                ImGui::Separator();
                                if (ImGui::MenuItem("Import Model...")) {
                                    popupTargetVirtualDir = item.subDir;
                                    ImportMeshWithSavePrompt(scene);
                                }
                                if (ImGui::MenuItem("Show in Explorer")) {
                                    std::string diskSub = item.subDir;
                                    if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                                    if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                                    std::filesystem::path folderDisk = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                                    std::string cmd = "explorer \"" + folderDisk.string() + "\"";
                                    system(cmd.c_str());
                                }
                                if (item.subDir != "/Game") {
                                    ImGui::Separator();
                                    if (ImGui::MenuItem("Delete Folder")) {
                                        std::string diskSub = item.subDir;
                                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                                        std::filesystem::path folderDisk = contentRootPath / diskSub;
                                        std::error_code delEc;
                                        std::filesystem::remove_all(folderDisk, delEc);
                                        if (currentVirtualDir == item.subDir || currentVirtualDir.rfind(item.subDir + "/", 0) == 0) {
                                            currentVirtualDir = AssetPath::GetDirectory(item.subDir);
                                        }
                                        TriggerBackgroundRegistrySync(contentRootPath);
                                    }
                                }
                                ImGui::EndPopup();
                            }
                        } else {
                            if (ImGui::BeginPopupContextItem("AssetCardCtx")) {
                                if (!selectedContentItems.count(item.itemKey)) {
                                    selectedContentItems.clear();
                                    selectedContentItems.insert(item.itemKey);
                                    selectedContentItem = item.meta.virtualPath;
                                    lastSelectedContentItem = item.itemKey;
                                }

                                if (item.meta.type == AssetType::Behaviour || item.meta.type == AssetType::Script) {
                                    std::filesystem::path fullScript = contentRootPath / item.meta.sourcePath;
                                    if (ImGui::MenuItem("Open in VS Code")) {
                                        OpenScriptInCodeEditor(fullScript.string());
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::Material) {
                                    if (ImGui::MenuItem("Open in Material Editor")) {
                                        std::filesystem::path fullMat = contentRootPath / item.meta.sourcePath;
                                        OpenMaterialEditor(fullMat.string());
                                    }
                                    if (ImGui::MenuItem("Apply to Selected Actor")) {
                                        std::filesystem::path fullMat = contentRootPath / item.meta.sourcePath;
                                        MaterialAsset ma;
                                        if (LoadMaterialFile(fullMat.string(), ma)) {
                                            GameObject* sel = scene.GetSelected();
                                            if (sel) {
                                                ApplyMaterialToActorAndChildren(scene, sel, ma);
                                            }
                                        }
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::Skybox) {
                                    if (ImGui::MenuItem("Open in Skybox Editor")) {
                                        std::filesystem::path fullSky = contentRootPath / item.meta.sourcePath;
                                        OpenSkyboxEditor(fullSky.string());
                                    }
                                    if (ImGui::MenuItem("Set as Active World Skybox")) {
                                        std::filesystem::path fullSky = contentRootPath / item.meta.sourcePath;
                                        SkyboxAsset sky;
                                        if (LoadSkyboxFile(fullSky.string(), sky)) {
                                            ApplySkyboxToScene(scene, sky);
                                        }
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::Water) {
                                    if (ImGui::MenuItem("Open in Water Editor")) {
                                        std::filesystem::path fullWater = contentRootPath / item.meta.sourcePath;
                                        OpenWaterEditor(fullWater.string());
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::NodeBehaviour || item.meta.sourcePath.find(".enb") != std::string::npos) {
                                    if (ImGui::MenuItem("Open in Node Behaviour Editor")) {
                                        std::filesystem::path fullNb = contentRootPath / item.meta.sourcePath;
                                        OpenNodeBehaviourEditor(fullNb.string());
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::AnimationBlend || item.meta.sourcePath.find(".eblend") != std::string::npos) {
                                    if (ImGui::MenuItem("Open in Animation Blend Editor")) {
                                        std::filesystem::path fullBlend = contentRootPath / item.meta.sourcePath;
                                        OpenAnimationBlendEditor(fullBlend.string());
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::LevelAnimation || item.meta.sourcePath.find(".elanim") != std::string::npos) {
                                    if (ImGui::MenuItem("Open in Animation Timeline")) {
                                        std::filesystem::path fullAnim = contentRootPath / item.meta.sourcePath;
                                        OpenLevelAnimationEditor(fullAnim.string());
                                        AddLog("LogLevelAnimation", "Opened Level Animation: " + item.meta.objectName, 2);
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::AnimatorGraph || item.meta.sourcePath.find(".eanimator") != std::string::npos || item.meta.sourcePath.find(".animator") != std::string::npos) {
                                    if (ImGui::MenuItem("Open in Animator Graph Editor")) {
                                        std::filesystem::path fullAg = contentRootPath / item.meta.sourcePath;
                                        OpenAnimatorGraphEditor(fullAg.string());
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::UserInterface || item.meta.type == AssetType::UserInterface3D) {
                                    if (ImGui::MenuItem(item.meta.type == AssetType::UserInterface3D ? "Open in UI 3D Editor" : "Open in UI Editor")) {
                                        std::filesystem::path fullUI = contentRootPath / item.meta.sourcePath;
                                        OpenUIEditor(fullUI.string());
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::Mesh || item.meta.type == AssetType::SkeletalMesh) {
                                    if (ImGui::MenuItem("Spawn Mesh in Level")) {
                                        std::filesystem::path fullMesh = contentRootPath / item.meta.sourcePath;
                                        GameObject& newObj = scene.AddImportedMesh(fullMesh.string(), glm::vec3(0.0f, 0.5f, 0.0f));
                                        scene.selectedId = newObj.id;
                                        AddLog("LogMesh", "Spawned 3D Mesh in level: " + item.meta.objectName, 2);
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::Level) {
                                    if (ImGui::MenuItem("Open Level in Viewport")) {
                                        std::filesystem::path fullLevel = contentRootPath / item.meta.sourcePath;
                                        OpenLevelFromPath(scene, fullLevel.string());
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::Behaviour || item.meta.sourcePath.find(".behaviour") != std::string::npos) {
                                    if (ImGui::MenuItem("Open in Behaviour Editor")) {
                                        std::filesystem::path fullBeh = contentRootPath / item.meta.sourcePath;
                                        OpenBehaviourEditor(fullBeh.string());
                                    }
                                    if (ImGui::MenuItem("Spawn Actor in Level")) {
                                        std::filesystem::path fullBeh = contentRootPath / item.meta.sourcePath;
                                        Eunoia::BehaviourManager::Get().SpawnBehaviourIntoScene(scene, fullBeh.string(), glm::vec3(0.0f, 0.0f, 0.0f));
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::Script) {
                                    if (ImGui::MenuItem("Edit Script in VS Code")) {
                                        std::filesystem::path fullScript = contentRootPath / item.meta.sourcePath;
                                        OpenScriptInCodeEditor(fullScript.string());
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::Project) {
                                    if (ImGui::MenuItem("Open Project in Editor")) {
                                        std::filesystem::path fullProjPath = contentRootPath / item.meta.sourcePath;
                                        std::error_code ec;
                                        if (!std::filesystem::exists(fullProjPath, ec)) {
                                            fullProjPath = activeProjectRoot / item.meta.sourcePath;
                                        }
                                        std::filesystem::path projRootDir = fullProjPath.parent_path();
                                        if (projRootDir.filename() == "Content") {
                                            projRootDir = projRootDir.parent_path();
                                        }
                                        if (currentCamera) {
                                            LoadProject(projRootDir, scene, *currentCamera);
                                        }
                                    }
                                    ImGui::Separator();
                                }

                                if (ImGui::MenuItem("Rename...", "F2")) {
                                    OpenRenameAssetModal(item.meta.id, item.meta.objectName, item.meta.virtualPath);
                                }
                                if (ImGui::MenuItem("View in Reference Viewer")) {
                                    OpenReferenceViewer(item.meta.id);
                                }
                                if (ImGui::MenuItem("Copy AssetID")) {
                                    ImGui::SetClipboardText(item.meta.id.ToString().c_str());
                                    AddLog("LogAsset", "Copied AssetID to clipboard: " + item.meta.id.ToString(), 0);
                                }
                                if (ImGui::MenuItem("Copy Virtual Path")) {
                                    ImGui::SetClipboardText(item.meta.virtualPath.c_str());
                                    AddLog("LogAsset", "Copied Virtual Path to clipboard: " + item.meta.virtualPath, 0);
                                }
                                ImGui::Separator();
                                if (ImGui::MenuItem("Delete Asset (Safe Check)")) {
                                    if (selectedContentItems.size() > 1 && selectedContentItems.count(item.itemKey)) {
                                        std::vector<std::string> toDelete(selectedContentItems.begin(), selectedContentItems.end());
                                        int delCount = 0;
                                        for (const auto& delKey : toDelete) {
                                            if (delKey.rfind("ASSET:", 0) == 0) {
                                                const AssetMetadata* dMeta = AssetRegistry::Get().FindByPath(delKey.substr(6));
                                                if (dMeta) {
                                                    auto refs = AssetRegistry::Get().GetReferences(dMeta->id);
                                                    if (refs.empty()) {
                                                        std::filesystem::path fullDisk = contentRootPath / dMeta->sourcePath;
                                                        std::filesystem::remove(fullDisk, ec);
                                                        std::filesystem::remove(fullDisk.string() + ".assetmeta", ec);

                                                        // Also delete respective cooked content files
                                                        std::filesystem::path cookedFile = AssetManager::Get().ResolvePhysicalOrCookedPath(dMeta->id);
                                                        if (!cookedFile.empty() && std::filesystem::exists(cookedFile, ec) && cookedFile != fullDisk) {
                                                            std::filesystem::remove(cookedFile, ec);
                                                        }
                                                        if (!activeProjectRoot.empty() && !dMeta->cookedPath.empty()) {
                                                            std::filesystem::path cookedByMeta = activeProjectRoot / "Cooked" / dMeta->cookedPath;
                                                            if (std::filesystem::exists(cookedByMeta, ec)) std::filesystem::remove(cookedByMeta, ec);
                                                        }
                                                        if (!activeProjectRoot.empty()) {
                                                            std::filesystem::path cookedBySrc = activeProjectRoot / "Cooked" / "Content" / dMeta->sourcePath;
                                                            if (std::filesystem::exists(cookedBySrc, ec)) std::filesystem::remove(cookedBySrc, ec);
                                                        }

                                                        AssetRegistry::Get().UnregisterAsset(dMeta->id);
                                                        delCount++;
                                                    }
                                                }
                                            }
                                        }
                                        selectedContentItems.clear();
                                        selectedContentItem = "";
                                        AddLog("LogAsset", "Deleted " + std::to_string(delCount) + " assets (including cooked content)", 1);
                                        TriggerBackgroundRegistrySync(contentRootPath);
                                    } else {
                                        auto refs = AssetRegistry::Get().GetReferences(item.meta.id);
                                        if (!refs.empty()) {
                                            AddLog("LogAsset", "Cannot delete '" + item.meta.objectName + "': referenced by " + std::to_string(refs.size()) + " assets! Inspect in Reference Viewer.", 3);
                                            OpenReferenceViewer(item.meta.id);
                                        } else {
                                            std::filesystem::path fullDisk = contentRootPath / item.meta.sourcePath;
                                            std::filesystem::remove(fullDisk, ec);
                                            std::filesystem::remove(fullDisk.string() + ".assetmeta", ec);

                                            // Also delete respective cooked content files
                                            std::filesystem::path cookedFile = AssetManager::Get().ResolvePhysicalOrCookedPath(item.meta.id);
                                            if (!cookedFile.empty() && std::filesystem::exists(cookedFile, ec) && cookedFile != fullDisk) {
                                                std::filesystem::remove(cookedFile, ec);
                                            }
                                            if (!activeProjectRoot.empty() && !item.meta.cookedPath.empty()) {
                                                std::filesystem::path cookedByMeta = activeProjectRoot / "Cooked" / item.meta.cookedPath;
                                                if (std::filesystem::exists(cookedByMeta, ec)) std::filesystem::remove(cookedByMeta, ec);
                                            }
                                            if (!activeProjectRoot.empty()) {
                                                std::filesystem::path cookedBySrc = activeProjectRoot / "Cooked" / "Content" / item.meta.sourcePath;
                                                if (std::filesystem::exists(cookedBySrc, ec)) std::filesystem::remove(cookedBySrc, ec);
                                            }

                                            AssetRegistry::Get().UnregisterAsset(item.meta.id);
                                            selectedContentItems.erase(item.itemKey);
                                            if (selectedContentItem == item.meta.virtualPath) selectedContentItem = "";
                                            AddLog("LogAsset", "Deleted asset and respective cooked content: " + item.meta.objectName, 1);
                                            TriggerBackgroundRegistrySync(contentRootPath);
                                        }
                                    }
                                }
                                ImGui::EndPopup();
                            }
                        }

                        // Tooltip
                        if (hovered) {
                            if (item.isFolder) {
                                ImGui::SetTooltip("Folder: %s\n(Double-click to open | Drag & Drop items here)", item.subDir.c_str());
                            } else {
                                size_t depCount = item.meta.dependencies.size();
                                size_t refCount = AssetRegistry::Get().GetReferences(item.meta.id).size();
                                bool isDummyRef = false;
                                std::error_code ec;
                                std::filesystem::path fullDisk = contentRootPath / item.meta.sourcePath;
                                if (!std::filesystem::exists(fullDisk, ec) || std::filesystem::file_size(fullDisk, ec) == 0) {
                                    if (!item.meta.cookedPath.empty()) isDummyRef = true;
                                }
                                const char* statusStr = isDummyRef ? "Cooked (Dummy Reference)" : (item.meta.isMissing ? "Missing" : (AssetManager::Get().IsLoaded(item.meta.id) ? "Loaded" : "Ready"));
                                ImGui::SetTooltip("Object: %s\nType: %s\nVirtual: %s\nAssetID: %s\nDisk: %s%s\nDeps: %d | Refs: %d | Status: %s\n(Double-click to open | Drag & Drop to viewport/folder)",
                                    item.meta.objectName.c_str(), AssetTypeToString(item.meta.type), item.meta.virtualPath.c_str(), item.meta.id.ToString().c_str(), item.meta.sourcePath.c_str(), isDummyRef ? " [Virtual Dummy]" : "", (int)depCount, (int)refCount, statusStr);
                            }
                        }

                        // Drawing Card Visuals with drawList
                        // 1. Outer Box & Selection Glow
                        ImU32 bgCol = isSelected ? IM_COL32(20, 60, 115, 235)
                                                 : (hovered ? IM_COL32(35, 43, 58, 240)
                                                            : IM_COL32(23, 27, 36, 215));
                        ImU32 borderCol = isSelected ? IM_COL32(40, 160, 255, 255)
                                                     : (hovered ? IM_COL32(80, 145, 235, 200)
                                                                : IM_COL32(46, 52, 68, 150));
                        float borderThick = isSelected ? 2.0f : 1.0f;

                        drawList->AddRectFilled(pMin, pMax, bgCol, 5.0f);
                        drawList->AddRect(pMin, pMax, borderCol, 5.0f, 0, borderThick);

                        // 2. Thumbnail Slot
                        float thumbPad = 5.0f;
                        float thumbH = cardW * 0.62f;
                        ImVec2 tMin(pMin.x + thumbPad, pMin.y + thumbPad);
                        ImVec2 tMax(pMax.x - thumbPad, pMin.y + thumbPad + thumbH);
                        drawList->AddRectFilled(tMin, tMax, IM_COL32(13, 16, 22, 255), 4.0f);
                        drawList->AddRect(tMin, tMax, IM_COL32(36, 42, 54, 180), 4.0f, 0, 1.0f);

                        // Determine Icon, GPU Texture Handle, and Accent Color
                        uint64_t iconGpuHandle = 0;
                        const char* iconTag = "FILE";
                        ImU32 accentCol = IM_COL32(135, 150, 170, 255);
                        const char* typeStr = "Unknown";

                        if (item.isFolder) {
                            iconGpuHandle = cbFolderIconGpuHandle;
                            iconTag = "DIR";
                            accentCol = IM_COL32(64, 204, 242, 255); // Cyan
                            typeStr = "Folder";
                        } else {
                            typeStr = AssetTypeToString(item.meta.type);
                            iconGpuHandle = GetContentBrowserAssetIcon(item.meta.type);
                            switch (item.meta.type) {
                                case AssetType::Material:        iconTag = "MAT";   accentCol = IM_COL32(89, 166, 255, 255); break; // Blue
                                case AssetType::Mesh:            iconTag = "3D";    accentCol = IM_COL32(242, 140, 51, 255);  break; // Orange
                                case AssetType::Skybox:          iconTag = "SKY";   accentCol = IM_COL32(76, 191, 230, 255);  break; // Sky Blue
                                case AssetType::Water:           iconTag = "WATER"; accentCol = IM_COL32(0, 160, 255, 255);   break;
                                case AssetType::Texture:         iconTag = "TEX";   accentCol = IM_COL32(51, 217, 128, 255);  break; // Emerald
                                case AssetType::VisualBehaviour: iconTag = "VBEH";  accentCol = IM_COL32(230, 89, 153, 255);  break; // Magenta
                                case AssetType::NodeBehaviour:   iconTag = "NB";    accentCol = IM_COL32(0, 210, 255, 255);   break; // Blueprint Cyan
                                case AssetType::Behaviour:
                                case AssetType::Script:          iconTag = "BEH";   accentCol = IM_COL32(217, 102, 242, 255); break; // Purple
                                case AssetType::Level:           iconTag = "LEVEL"; accentCol = IM_COL32(51, 217, 242, 255);  break; // Cyan
                                case AssetType::Project:         iconTag = "PROJ";  accentCol = IM_COL32(255, 204, 0, 255);   break; // Gold
                                case AssetType::AnimationBlend:  iconTag = "BLEND"; accentCol = IM_COL32(160, 100, 240, 255); break; // Purple
                                case AssetType::AnimatorGraph:   iconTag = "AG";    accentCol = IM_COL32(0, 220, 255, 255);    break; // Cyan
                                case AssetType::LevelAnimation:  iconTag = "L-ANIM"; accentCol = IM_COL32(255, 120, 50, 255); break; // Orange
                                default:                         iconTag = "FILE";  accentCol = IM_COL32(153, 153, 153, 255); break;
                            }
                        }

                        // Center Icon (GPU texture or fallback badge)
                        if (iconGpuHandle != 0) {
                            float maxIconDim = std::min(tMax.x - tMin.x - 10.0f, tMax.y - tMin.y - 10.0f);
                            float iconDim = std::clamp(maxIconDim, 24.0f, 44.0f);
                            float iconCenterX = tMin.x + (tMax.x - tMin.x) * 0.5f;
                            float iconCenterY = tMin.y + (tMax.y - tMin.y) * 0.5f - 1.0f;
                            ImVec2 imgMin(iconCenterX - iconDim * 0.5f, iconCenterY - iconDim * 0.5f);
                            ImVec2 imgMax(iconCenterX + iconDim * 0.5f, iconCenterY + iconDim * 0.5f);
                            drawList->AddImage((ImTextureID)iconGpuHandle, imgMin, imgMax);
                        } else {
                            std::string badgeStr = std::string("[") + iconTag + "]";
                            ImVec2 iconSz = ImGui::CalcTextSize(badgeStr.c_str());
                            ImVec2 iconPos(tMin.x + (tMax.x - tMin.x - iconSz.x) * 0.5f,
                                           tMin.y + (tMax.y - tMin.y - iconSz.y) * 0.5f - 2.0f);
                            drawList->AddText(iconPos, accentCol, badgeStr.c_str());
                        }

                        // Unreal-Engine Signature Accent Stripe
                        drawList->AddRectFilled(ImVec2(tMin.x + 1.0f, tMax.y - 3.5f),
                                                ImVec2(tMax.x - 1.0f, tMax.y - 1.0f),
                                                accentCol, 2.0f);

                        // 3. Type Label Pill
                        float typeY = tMax.y + 3.0f;
                        std::string truncType = typeStr;
                        float maxLabelW = cardW - 10.0f;
                        if (ImGui::CalcTextSize(truncType.c_str()).x > maxLabelW) {
                            while (!truncType.empty() && ImGui::CalcTextSize((truncType + "..").c_str()).x > maxLabelW) {
                                truncType.pop_back();
                            }
                            truncType += "..";
                        }
                        drawList->AddText(ImVec2(pMin.x + 5.0f, typeY), IM_COL32(130, 148, 170, 220), truncType.c_str());

                        // 4. Object Name Label
                        float nameY = typeY + 13.0f;
                        std::string truncName = item.displayName;
                        if (ImGui::CalcTextSize(truncName.c_str()).x > maxLabelW) {
                            while (!truncName.empty() && ImGui::CalcTextSize((truncName + "...").c_str()).x > maxLabelW) {
                                truncName.pop_back();
                            }
                            truncName += "...";
                        }
                        ImU32 textCol = isSelected ? IM_COL32(255, 255, 255, 255)
                                                   : (hovered ? IM_COL32(235, 242, 255, 255)
                                                              : IM_COL32(210, 220, 235, 240));
                        drawList->AddText(ImVec2(pMin.x + 5.0f, nameY), textCol, truncName.c_str());

                        ImGui::PopID();
                    }
                }


                // Right click context menu on empty space of current directory
                if (ImGui::BeginPopupContextWindow("CBBackgroundContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                    if (ImGui::MenuItem("New Folder")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewFolderPopup = true;
                        snprintf(newFolderNameBuf, sizeof(newFolderNameBuf), "NewFolder");
                    }
                    if (ImGui::MenuItem("New Material")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewMaterialPopup = true;
                        snprintf(newMaterialNameBuf, sizeof(newMaterialNameBuf), "M_NewMaterial");
                    }
                    if (ImGui::MenuItem("New User Interface")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewUIPopup = true;
                        snprintf(newUINameBuf, sizeof(newUINameBuf), "UI_NewInterface");
                    }
                    if (ImGui::MenuItem("New Animator Graph (.eanimator)...")) {
                showNewAnimatorGraphPopup = true;
                snprintf(newAnimatorGraphNameBuf, sizeof(newAnimatorGraphNameBuf), "AG_NewAnimator");
            }
                        if (ImGui::MenuItem("New Level Animation (.elanim)...")) {
                popupTargetVirtualDir = currentVirtualDir;
                showNewLevelAnimationPopup = true;
                snprintf(newLevelAnimationNameBuf, sizeof(newLevelAnimationNameBuf), "LA_NewAnimation");
            }
            if (ImGui::MenuItem("New Animation Blend (.eblend)...")) {
                showNewAnimationBlendPopup = true;
                snprintf(newAnimationBlendNameBuf, sizeof(newAnimationBlendNameBuf), "BS_NewBlend");
            }
                        if (ImGui::MenuItem("New Node Behaviour (.enb)...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewNodeBehaviourPopup = true;
                        snprintf(newNodeBehaviourNameBuf, sizeof(newNodeBehaviourNameBuf), "NB_NewBehaviour");
                    }
                    if (ImGui::BeginMenu("Create")) {
                        if (ImGui::MenuItem("Level Animation (.elanim)")) {
                            popupTargetVirtualDir = currentVirtualDir;
                            showNewLevelAnimationPopup = true;
                            snprintf(newLevelAnimationNameBuf, sizeof(newLevelAnimationNameBuf), "LA_NewAnimation");
                        }
                        if (ImGui::MenuItem("Animator Graph (.eanimator)")) {
                            popupTargetVirtualDir = currentVirtualDir;
                            showNewAnimatorGraphPopup = true;
                            snprintf(newAnimatorGraphNameBuf, sizeof(newAnimatorGraphNameBuf), "AG_NewAnimator");
                        }
                        if (ImGui::MenuItem("Node Behaviour (.enb)")) {
                            popupTargetVirtualDir = currentVirtualDir;
                            showNewNodeBehaviourPopup = true;
                            snprintf(newNodeBehaviourNameBuf, sizeof(newNodeBehaviourNameBuf), "NB_NewBehaviour");
                        }
                        if (ImGui::MenuItem("User Interface (.eui)")) {
                            popupTargetVirtualDir = currentVirtualDir;
                            showNewUIPopup = true;
                            snprintf(newUINameBuf, sizeof(newUINameBuf), "UI_NewInterface");
                        }
                        if (ImGui::MenuItem("Behaviour Script (.cpp)")) {
                            popupTargetVirtualDir = currentVirtualDir;
                            showNewBehaviourPopup = true;
                            std::string defName = "NewBehaviour";
                            if (IsBehaviourNameAlreadyExists(defName)) {
                                int idx = 1;
                                while (IsBehaviourNameAlreadyExists("NewBehaviour_" + std::to_string(idx))) idx++;
                                defName = "NewBehaviour_" + std::to_string(idx);
                            }
                            snprintf(newBehaviourNameBuf, sizeof(newBehaviourNameBuf), "%s", defName.c_str());
                        }
                        if (ImGui::MenuItem("Material")) {
                            popupTargetVirtualDir = currentVirtualDir;
                            showNewMaterialPopup = true;
                            snprintf(newMaterialNameBuf, sizeof(newMaterialNameBuf), "M_NewMaterial");
                        }
                        if (ImGui::MenuItem("Folder")) {
                            popupTargetVirtualDir = currentVirtualDir;
                            showNewFolderPopup = true;
                            snprintf(newFolderNameBuf, sizeof(newFolderNameBuf), "NewFolder");
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("New Behaviour (.behaviour)...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewBehaviourAssetPopup = true;
                        snprintf(newBehaviourAssetNameBuf, sizeof(newBehaviourAssetNameBuf), "B_NewActor");
                    }
                    if (ImGui::MenuItem("New Behaviour Script (.cpp)...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewBehaviourPopup = true;
                        std::string defName = "NewBehaviour";
                        if (IsBehaviourNameAlreadyExists(defName)) {
                            int idx = 1;
                            while (IsBehaviourNameAlreadyExists("NewBehaviour_" + std::to_string(idx))) idx++;
                            defName = "NewBehaviour_" + std::to_string(idx);
                        }
                        snprintf(newBehaviourNameBuf, sizeof(newBehaviourNameBuf), "%s", defName.c_str());
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Import Model...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        ImportMeshWithSavePrompt(scene);
                    }

                    if (ImGui::MenuItem("Show in Explorer")) {
                        std::string diskSub = currentVirtualDir;
                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                        std::filesystem::path folderDisk = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                        std::string cmd = "explorer \"" + folderDisk.string() + "\"";
                        system(cmd.c_str());
                    }
                    if (ImGui::MenuItem("Rescan Directory")) {
                        TriggerBackgroundRegistrySync(contentRootPath);
                    }
                    ImGui::EndPopup();
                }

                // Drop target on CBAssetListPane empty space (moves content into currentVirtualDir)
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                        std::string payloadData((const char*)payload->Data);
                        std::stringstream pss(payloadData);
                        std::string item;
                        while (std::getline(pss, item, '\n')) {
                            if (item.empty()) continue;
                            if (item.rfind("DIR:", 0) == 0) {
                                MoveFolderToDirectory(item.substr(4), currentVirtualDir, contentRootPath);
                            } else if (item.rfind("ASSET:", 0) == 0) {
                                const AssetMetadata* m = AssetRegistry::Get().FindByPath(item.substr(6));
                                if (m) MoveAssetToDirectory(m->id, currentVirtualDir, contentRootPath);
                            }
                        }
                        TriggerBackgroundRegistrySync(contentRootPath);
                    } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                        std::string idStr((const char*)payload->Data);
                        MoveAssetToDirectory(AssetID::FromString(idStr), currentVirtualDir, contentRootPath);
                        TriggerBackgroundRegistrySync(contentRootPath);
                    }
                    ImGui::EndDragDropTarget();
                }

                // F2 Keyboard shortcut to rename currently selected item in Content Browser
                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F2)) {
                    if (!selectedContentItem.empty()) {
                        if (selectedContentItem.rfind("DIR:", 0) == 0) {
                            OpenRenameFolderModal(selectedContentItem.substr(4));
                        } else {
                            std::string pathQuery = selectedContentItem;
                            if (pathQuery.rfind("ASSET:", 0) == 0) pathQuery = pathQuery.substr(6);
                            const AssetMetadata* m = AssetRegistry::Get().FindByPath(pathQuery);
                            if (m) {
                                OpenRenameAssetModal(m->id, m->objectName, m->virtualPath);
                            }
                        }
                    }
                }

                ImGui::EndChild();
            

// Periodic real-time check for external file changes in contentRootPath
    static auto s_lastDiskCheck = std::chrono::steady_clock::now();
    static std::filesystem::file_time_type s_lastContentTime{};
    auto nowTime = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - s_lastDiskCheck).count() > 1500) {
        s_lastDiskCheck = nowTime;
        if (!contentRootPath.empty() && !s_bgSync.isRunning.load()) {
            std::error_code ec;
            auto curTime = std::filesystem::last_write_time(contentRootPath, ec);
            if (!ec) {
                if (s_lastContentTime != std::filesystem::file_time_type{} && curTime != s_lastContentTime) {
                    TriggerBackgroundRegistrySync(contentRootPath);
                }
                s_lastContentTime = curTime;
            }
        }
    }

    // Subtle Bottom-Right Background Sync Progress Indicator
    bool bgSyncActive = s_bgSync.isRunning.load();
    static auto s_lastActiveTime = std::chrono::steady_clock::now();
    if (bgSyncActive) {
        s_lastActiveTime = std::chrono::steady_clock::now();
    }
    auto timeSinceSync = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_lastActiveTime).count();
    bool showBgIndicator = bgSyncActive || (timeSinceSync < 1500);

    if (showBgIndicator) {
        ImVec2 badgeSize(220.0f, 24.0f);
        ImVec2 winPos = ImGui::GetWindowPos(); ImVec2 winSize = ImGui::GetWindowSize();
        ImVec2 badgePos(winPos.x + winSize.x - badgeSize.x - 14.0f, winPos.y + winSize.y - badgeSize.y - 7.0f);
        ImDrawList* fgList = ImGui::GetForegroundDrawList();
        fgList->AddRectFilled(badgePos, ImVec2(badgePos.x + badgeSize.x, badgePos.y + badgeSize.y), IM_COL32(16, 20, 28, 230), 4.0f);
        fgList->AddRect(badgePos, ImVec2(badgePos.x + badgeSize.x, badgePos.y + badgeSize.y), IM_COL32(40, 90, 160, 200), 4.0f);

        if (bgSyncActive) {
            float prog = s_bgSync.progress.load();
            float barW = (badgeSize.x - 20.0f) * std::max(0.05f, prog);
            fgList->AddRectFilled(ImVec2(badgePos.x + 10.0f, badgePos.y + badgeSize.y - 4.0f),
                                  ImVec2(badgePos.x + 10.0f + barW, badgePos.y + badgeSize.y - 2.0f),
                                  IM_COL32(40, 160, 240, 255), 1.0f);
            char buf[64];
            snprintf(buf, sizeof(buf), "Syncing... (%d%%)", (int)(prog * 100.0f));
            fgList->AddText(ImVec2(badgePos.x + 12.0f, badgePos.y + 3.0f), IM_COL32(180, 220, 255, 240), buf);
        } else {
            fgList->AddText(ImVec2(badgePos.x + 12.0f, badgePos.y + 3.0f), IM_COL32(90, 230, 130, 240), "Registry Synchronized");
        }
    }
}



void EngineUI::RenderLayoutSplitters() {
    if (isImmersiveMode) return;
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) return; // DockSpace handles layout splitters natively

    ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!vp || vp->Size.x <= 10.0f || vp->Size.y <= 10.0f) return;

    float bottomBarH = bottomBarHeight * uiScale;
    float sidebarsY = vp->Pos.y + GetTopBarsTotalHeight();
    float sidebarsH = vp->Size.y - GetTopBarsTotalHeight() - bottomBarH;
    float curBottomH = (showBottomDrawer ? (bottomDrawerOpen ? bottomDockHeight : 26.0f) : 0.0f);
    float bottomY = vp->Pos.y + vp->Size.y - bottomBarH - curBottomH;
    float rightW = (showOutliner || showDetails) ? rightSidebarWidth : 0.0f;

    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    ImDrawList* drawList = ImGui::GetForegroundDrawList();

    static bool s_dragRight = false;
    static bool s_dragOutlinerDetails = false;
    static bool s_dragBottom = false;

    // 1. Right Sidebar Vertical Splitter (adjusts rightSidebarWidth)
    if (showOutliner || showDetails) {
        float splitX = vp->Pos.x + vp->Size.x - rightSidebarWidth;
        bool inRightHit = (mousePos.x >= splitX - 5.0f && mousePos.x <= splitX + 5.0f &&
                           mousePos.y >= sidebarsY && mousePos.y <= sidebarsY + sidebarsH);

        if (!s_dragOutlinerDetails && !s_dragBottom) {
            if (inRightHit && mouseClicked) s_dragRight = true;
        }

        if (s_dragRight) {
            if (mouseDown) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                float delta = ImGui::GetIO().MouseDelta.x;
                if (delta != 0.0f) {
                    float maxW = vp->Size.x - 300.0f;
                    rightSidebarWidth = std::clamp(rightSidebarWidth - delta, 200.0f, std::max(220.0f, maxW));
                }
                drawList->AddLine(ImVec2(splitX, sidebarsY), ImVec2(splitX, sidebarsY + sidebarsH), IM_COL32(0, 122, 204, 255), 3.0f);
            } else {
                s_dragRight = false;
                SaveEditorConfig();
            }
        } else if (inRightHit && !s_dragOutlinerDetails && !s_dragBottom) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            drawList->AddLine(ImVec2(splitX, sidebarsY), ImVec2(splitX, sidebarsY + sidebarsH), IM_COL32(0, 122, 204, 200), 2.0f);
        }
    }

    // 2. Horizontal Splitter between Outliner (top) and Details (bottom) in the Right Sidebar
    if (showOutliner && showDetails) {
        float splitX = vp->Pos.x + vp->Size.x - rightSidebarWidth;
        float outlinerH = std::round(sidebarsH * outlinerHeightRatio);
        float splitY = sidebarsY + outlinerH;
        float endX = vp->Pos.x + vp->Size.x;
        bool inSplitHit = (mousePos.y >= splitY - 5.0f && mousePos.y <= splitY + 5.0f &&
                           mousePos.x >= splitX && mousePos.x <= endX);

        if (!s_dragRight && !s_dragBottom) {
            if (inSplitHit && mouseClicked) s_dragOutlinerDetails = true;
        }

        if (s_dragOutlinerDetails) {
            if (mouseDown) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                float relY = mousePos.y - sidebarsY;
                if (sidebarsH > 50.0f) {
                    outlinerHeightRatio = std::clamp(relY / sidebarsH, 0.15f, 0.85f);
                }
                drawList->AddLine(ImVec2(splitX, splitY), ImVec2(endX, splitY), IM_COL32(0, 122, 204, 255), 3.0f);
            } else {
                s_dragOutlinerDetails = false;
                SaveEditorConfig();
            }
        } else if (inSplitHit && !s_dragRight && !s_dragBottom) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            drawList->AddLine(ImVec2(splitX, splitY), ImVec2(endX, splitY), IM_COL32(0, 122, 204, 200), 2.0f);
        }
    }

    // 3. Bottom Content Browser Splitter (adjusts bottomDockHeight)
    if (showBottomDrawer && bottomDrawerOpen) {
        float splitY = bottomY;
        float startX = vp->Pos.x;
        float endX = vp->Pos.x + vp->Size.x - rightW;
        bool inBottomHit = (mousePos.y >= splitY - 5.0f && mousePos.y <= splitY + 5.0f &&
                            mousePos.x >= startX && mousePos.x <= endX);

        if (!s_dragRight && !s_dragOutlinerDetails) {
            if (inBottomHit && mouseClicked) s_dragBottom = true;
        }

        if (s_dragBottom) {
            if (mouseDown) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                float delta = ImGui::GetIO().MouseDelta.y;
                if (delta != 0.0f) {
                    float maxH = vp->Size.y - GetTopBarsTotalHeight() - bottomBarH - 120.0f;
                    bottomDockHeight = std::clamp(bottomDockHeight - delta, 100.0f, std::max(150.0f, maxH));
                }
                drawList->AddLine(ImVec2(startX, splitY), ImVec2(endX, splitY), IM_COL32(0, 122, 204, 255), 3.0f);
            } else {
                s_dragBottom = false;
                SaveEditorConfig();
            }
        } else if (inBottomHit && !s_dragRight && !s_dragOutlinerDetails) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            drawList->AddLine(ImVec2(startX, splitY), ImVec2(endX, splitY), IM_COL32(0, 122, 204, 200), 2.0f);
        }
    }
}

void EngineUI::RenderGizmo(Scene& scene, OrbitCamera& camera, float viewportWidth, float viewportHeight) {
    if (scene.isPlayMode || !showGizmo || scene.selectedId == -1) return;

    GameObject* obj = scene.GetSelected();
    if (!obj || !obj->visible) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!vp || vp->Size.x <= 10.0f || vp->Size.y <= 10.0f) return;

    // Only allow ImGuizmo to handle mouse if not interacting with UI windows, menus, or popups (e.g. Color Picker)
    bool mouseOverUI = ImGui::GetIO().WantCaptureMouse;
    ImGuizmo::PushID(obj->id);
    ImGuizmo::Enable(!mouseOverUI || ImGuizmo::IsUsing());
    ImGuizmo::SetOrthographic(false);

    // The DirectX 12 3D scene renders to the center viewport (vpRect) matching UI_Ref.svg
    ViewportRect vpRect = GetViewportRect(vp->Size.x, vp->Size.y);
    ImGuizmo::SetRect(vp->Pos.x + vpRect.x, vp->Pos.y + vpRect.y, vpRect.width, vpRect.height);

    bool isLight = (obj->isLight || IsLightPrimitive(obj->type));

    // When a light is selected, obtain the light's actual world-space position from its transform/component.
    // If the light has a parent, GetWorldMatrix computes: WorldTransform = ParentWorldTransform * LocalTransform.
    // Never use mesh bounds center, actor bounds center, or bounding-box center for lights.
    glm::mat4 modelMatrix = scene.GetWorldMatrix(*obj);
    if (!isLight && gizmoUseCenter && !obj->mesh.vertices.empty()) {
        glm::vec3 worldCenter = scene.GetWorldCenter(*obj);
        modelMatrix[3] = glm::vec4(worldCenter, 1.0f);
    }

    // Task 2: bail out before ImGuizmo::Manipulate if modelMatrix is corrupted.
    // Selecting a broken object should never hang rendering ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â it should just
    // not show a gizmo for it until its data is fixed.
    {
        const float* mp = glm::value_ptr(modelMatrix);
        bool matrixValid = true;
        for (int i = 0; i < 16; ++i) {
            if (std::isnan(mp[i]) || !std::isfinite(mp[i])) { matrixValid = false; break; }
        }
        if (!matrixValid) {
            static int lastWarnedId = -1;
            if (lastWarnedId != obj->id) {
                std::cerr << "[RECOVERY] Object \"" << obj->name << "\" (id=" << obj->id
                          << ") has a corrupted transform/mesh ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â gizmo suppressed to avoid GPU hang.\n";
                AddLog("LogRecovery",
                    "Object \"" + obj->name + "\" has a corrupted transform or mesh and its gizmo was suppressed. Fix or delete it.", 2);
                lastWarnedId = obj->id;
            }
            ImGuizmo::PopID();
            return;
        }
    }
    glm::mat4 viewMatrix = camera.GetViewMatrix();
    glm::mat4 projMatrix = camera.GetProjectionMatrix(vpRect.width / vpRect.height);

    float snapValues[3];
    float* pSnap = nullptr;
    if (useSnap) {
        if (currentGizmoOperation == ImGuizmo::ROTATE) {
            snapValues[0] = snapRotation;
            pSnap = snapValues;
        } else if (currentGizmoOperation == ImGuizmo::SCALE) {
            snapValues[0] = snapValues[1] = snapValues[2] = snapScale;
            pSnap = snapValues;
        } else {
            snapValues[0] = snapTranslation.x;
            snapValues[1] = snapTranslation.y;
            snapValues[2] = snapTranslation.z;
            pSnap = snapValues;
        }
    }

    // Scale operation in ImGuizmo requires LOCAL mode
    ImGuizmo::MODE actualMode = (currentGizmoOperation == ImGuizmo::SCALE) 
        ? ImGuizmo::LOCAL 
        : (ImGuizmo::MODE)currentGizmoMode;

    bool manipulated = ImGuizmo::Manipulate(
        glm::value_ptr(viewMatrix),
        glm::value_ptr(projMatrix),
        (ImGuizmo::OPERATION)currentGizmoOperation,
        actualMode,
        glm::value_ptr(modelMatrix),
        nullptr,
        pSnap
    );

    static bool s_isGizmoUsing = false;
    if (ImGuizmo::IsUsing() || manipulated) {
        if (!s_isGizmoUsing) {
            s_isGizmoUsing = true;
            UndoManager::Get().RecordSnapshot(scene, "Transform Actor: " + obj->name);
        }
        obj->autoRotate = false; // Pause and disable auto-spinning when user is moving object

        // --- Task 1: Guard parent-inverse against degenerate/singular parent transforms ---
        // Converts World Space -> Local Space for child components/lights
        glm::mat4 localMatrix = modelMatrix;
        if (obj->parentId != -1) {
            glm::mat4 parentWorld = scene.GetWorldMatrix(obj->parentId);
            float parentDet = glm::determinant(parentWorld);
            if (std::abs(parentDet) > 1e-6f && !std::isnan(parentDet)) {
                localMatrix = glm::inverse(parentWorld) * modelMatrix;
            }
            // else: parent transform is degenerate ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â fall back to using modelMatrix
            // as-is (world space) rather than propagating Inf/NaN into localMatrix.
        }

        // Helper lambda: check a vec3 is safe (no NaN, no Inf)
        auto isVec3Valid = [](const glm::vec3& v) -> bool {
            return !std::isnan(v.x) && !std::isnan(v.y) && !std::isnan(v.z) &&
                    std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };

        if (currentGizmoOperation == ImGuizmo::TRANSLATE) {
            // Directly extract translation without Euler angle decomposition to prevent jitter
            glm::vec3 newPos;
            if (!isLight && gizmoUseCenter && !obj->mesh.vertices.empty()) {
                glm::vec3 minB(1e9f), maxB(-1e9f);
                for (const auto& v : obj->mesh.vertices) {
                    minB = glm::min(minB, v.pos);
                    maxB = glm::max(maxB, v.pos);
                }
                glm::vec3 localCenterOffset = (minB + maxB) * 0.5f;
                newPos = glm::vec3(localMatrix[3][0], localMatrix[3][1], localMatrix[3][2]) - localCenterOffset;
            } else {
                newPos = glm::vec3(localMatrix[3][0], localMatrix[3][1], localMatrix[3][2]);
            }
            // Task 2: only apply if the new position is finite/non-NaN
            if (isVec3Valid(newPos)) {
                obj->position = newPos;
            }
        } else if (currentGizmoOperation == ImGuizmo::SCALE) {
            // Task 2: guard scale extraction against NaN / non-finite column lengths
            glm::vec3 newScale(
                glm::length(glm::vec3(localMatrix[0])),
                glm::length(glm::vec3(localMatrix[1])),
                glm::length(glm::vec3(localMatrix[2]))
            );
            if (isVec3Valid(newScale)) {
                // Clamp to a small positive minimum so scale can never hit exactly 0
                // (which would make THIS object's own world matrix singular for future children/gizmo use)
                const float kMinScale = 1e-4f;
                obj->scale = glm::max(newScale, glm::vec3(kMinScale));
            }
            // else: ignore this frame's gizmo update rather than corrupting obj->scale.
        } else if (currentGizmoOperation == ImGuizmo::ROTATE) {
            float translation[3], rotation[3], scale[3];
            ImGuizmo::DecomposeMatrixToComponents(
                glm::value_ptr(localMatrix),
                translation,
                rotation,
                scale
            );
            glm::vec3 newRot(rotation[0], rotation[1], rotation[2]);
            if (isVec3Valid(newRot)) {
                obj->rotation = newRot;
            }
        } else {
            float translation[3], rotation[3], scale[3];
            ImGuizmo::DecomposeMatrixToComponents(
                glm::value_ptr(localMatrix),
                translation,
                rotation,
                scale
            );
            glm::vec3 newPos(translation[0], translation[1], translation[2]);
            glm::vec3 newRot(rotation[0], rotation[1], rotation[2]);
            glm::vec3 newSca(scale[0], scale[1], scale[2]);
            if (isVec3Valid(newPos)) obj->position = newPos;
            if (isVec3Valid(newRot)) obj->rotation = newRot;
            if (isVec3Valid(newSca)) {
                const float kMinScale = 1e-4f;
                obj->scale = glm::max(newSca, glm::vec3(kMinScale));
            }
        }

        if (isLight) {
            scene.SyncLightPositionsFromActors();
        }
    }

    if (!ImGuizmo::IsUsing()) {
        s_isGizmoUsing = false;
    }
    scene.isInteracting = ImGuizmo::IsUsing();
    ImGuizmo::PopID();
}

void EngineUI::RenderHelpModal() {
    ImGui::OpenPopup("Eunoia-Editor Guide");
    if (ImGui::BeginPopupModal("Eunoia-Editor Guide", &showHelpModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.12f, 0.68f, 1.00f, 1.0f), "Viewport Fly Mode & Camera Controls (Unreal Engine):");
        ImGui::BulletText("Hold RMB + W: Fly Forward");
        ImGui::BulletText("Hold RMB + S: Fly Backward");
        ImGui::BulletText("Hold RMB + A: Strafe Left");
        ImGui::BulletText("Hold RMB + D: Strafe Right");
        ImGui::BulletText("Hold RMB + E: Move Up");
        ImGui::BulletText("Hold RMB + Q: Move Down");
        ImGui::BulletText("Hold RMB + Mouse Movement: Look Around (360 FPS camera)");
        ImGui::BulletText("Hold RMB + Mouse Wheel: Increase / Decrease Camera Speed");
        ImGui::BulletText("Hold Shift while flying: Sprint / 2.5x Speed Boost");
        ImGui::BulletText("F Key: Focus Selected Actor in Viewport");
        ImGui::BulletText("Alt + LMB + Drag: Orbit Around Pivot / Selected Object");
        ImGui::BulletText("Alt + RMB + Drag: Dolly / Zoom");
        ImGui::BulletText("Alt + MMB + Drag: Pan / Track Viewport");
        ImGui::BulletText("F11 Key: Toggle Immersive Viewport Mode");
        ImGui::BulletText("G Key: Toggle Game View (Hide Grid & Gizmos)");
        ImGui::Separator();

        ImGui::TextColored(ImVec4(0.12f, 0.68f, 1.00f, 1.0f), "Transform Gizmos & Shortcuts:");
        ImGui::BulletText("W Key: Translate Tool");
        ImGui::BulletText("E Key: Rotate Tool");
        ImGui::BulletText("R Key: Scale Tool");
        ImGui::BulletText("Q Key: Toggle World / Local Space");
        ImGui::BulletText("Ctrl + D: Duplicate Actor");
        ImGui::BulletText("Delete Key: Delete Actor");
        ImGui::Separator();

        ImGui::TextColored(ImVec4(0.12f, 0.68f, 1.00f, 1.0f), "Editor Layout Overview (UI_Ref Blueprint):");
        ImGui::BulletText("Top Bar: Unified Menu Bar, Tool toggles, Simulation PIE controls");
        ImGui::BulletText("Outliner (Left): Actor hierarchy, visibility, duplicate/delete");
        ImGui::BulletText("Details (Right): Transform pills, Mobility, Static Mesh, Materials");
        ImGui::BulletText("Content Browser (Bottom): Quick-spawn asset shapes, Output Log");
        ImGui::Separator();

        if (ImGui::Button("Close Guide", ImVec2(120, 0))) {
            showHelpModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EngineUI::RenderClusterCullingStats(Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(480, 150), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Mesh Cluster Culling Statistics", &showClusterCullingStats)) {
        ImGui::TextDisabled("Mesh Cluster Culling plugin is not loaded.");
    }
    ImGui::End();
}

const std::vector<EngineUI::AntiAliasingInfo>& EngineUI::GetAntiAliasingMethods() {
    return Eunoia::GraphicsSystem::GetAllMethods();
}

const char* EngineUI::GetAntiAliasingName(AntiAliasingMethod method) {
    return Eunoia::GraphicsSystem::MethodToString(method);
}

void EngineUI::RenderQualitySettings() {
    ImGui::SetNextWindowSize(ImVec2(660, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Quality Settings###QualitySettingsWin", &showQualitySettings)) {
        ImGui::TextColored(ImVec4(0.3f, 0.75f, 1.0f, 1.0f), "Graphics Quality Settings (Graphics Plugin)");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Anti-Aliasing", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Active Global Method: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "%s", GetAntiAliasingName(currentAntiAliasing));

            const auto& methods = GetAntiAliasingMethods();
            const char* currentLabel = GetAntiAliasingName(currentAntiAliasing);

            if (ImGui::BeginCombo("Select Method##AACombo", currentLabel)) {
                for (const auto& m : methods) {
                    bool isSelected = (currentAntiAliasing == m.method);
                    if (ImGui::Selectable(m.name, isSelected)) {
                        currentAntiAliasing = m.method;
                        Eunoia::GraphicsSystem::Get().SetAntiAliasingMethod(m.method);
                        SaveEditorConfig();
                        AddLog("LogRenderer", std::string("Anti-Aliasing set globally to: ") + m.name + " (" + m.role + ")", 0);
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Quality: %s\nCost: %s\nRecommended: %s", m.quality, m.cost, m.role);
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Available Methods (from dev.md):");

            if (ImGui::BeginTable("AAMethodsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Method", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Quality", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Cost", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Recommended Role", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (const auto& m : methods) {
                    ImGui::TableNextRow();
                    bool isSelected = (currentAntiAliasing == m.method);
                    if (isSelected) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(30, 90, 160, 120));
                    }

                    ImGui::TableSetColumnIndex(0);
                    char radioLabel[128];
                    snprintf(radioLabel, sizeof(radioLabel), "%s##row_%d", m.name, (int)m.method);
                    if (ImGui::RadioButton(radioLabel, isSelected)) {
                        currentAntiAliasing = m.method;
                        Eunoia::GraphicsSystem::Get().SetAntiAliasingMethod(m.method);
                        SaveEditorConfig();
                        AddLog("LogRenderer", std::string("Anti-Aliasing set globally to: ") + m.name + " (" + m.role + ")", 0);
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", m.quality);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%s", m.cost);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%s", m.role);
                }
                ImGui::EndTable();
            }
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Texture Quality", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Active Texture Quality: ");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "%s", GetTextureQualityName(currentTextureQuality));

            struct TQEntry {
                TextureQuality quality;
                const char* name;
                const char* res;
            };
            static const TQEntry tqEntries[] = {
                { TextureQuality::None,   "None",   "Native Quality" },
                { TextureQuality::Low,    "Low",    "256 x 256" },
                { TextureQuality::Medium, "Medium", "512 x 512" },
                { TextureQuality::High,   "High",   "1024 x 1024" },
                { TextureQuality::Ultra,  "Ultra",  "2048 x 2048" },
                { TextureQuality::Max,    "Max",    "4096 x 4096" }
            };

            for (const auto& entry : tqEntries) {
                bool isSelected = (currentTextureQuality == entry.quality);
                char radioLabel[128];
                snprintf(radioLabel, sizeof(radioLabel), "%s (%s)##tq_radio_%d", entry.name, entry.res, (int)entry.quality);
                if (ImGui::RadioButton(radioLabel, isSelected)) {
                    ApplyTextureQuality(entry.quality);
                    SaveEditorConfig();
                    AddLog("LogRenderer", std::string("Texture Quality set globally to: ") + entry.name + " (" + entry.res + ")", 0);
                }
                if (ImGui::IsItemHovered()) {
                    if (entry.quality == TextureQuality::None) {
                        ImGui::SetTooltip("Native Realtime Quality (Unconstrained)\nAll textures render at their full native resolution with no LOD clamp.");
                    } else {
                        ImGui::SetTooltip("Max Texture Resolution: %s\nNative resolutions <= %s remain unscaled (full native detail).\nResolutions > %s are clamped via D3D12 Sampler Feedback / MinLODClamp.", entry.res, entry.res, entry.res);
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "TEXTURE STREAMING (Wicked Engine Viewport Visibility)");
            
            bool streamingEnabled = Eunoia::TextureStreamingSystem::Get().IsTextureStreamingEnabled();
            if (ImGui::Checkbox("Enable Viewport Texture Streaming", &streamingEnabled)) {
                Eunoia::TextureStreamingSystem::Get().SetTextureStreamingEnabled(streamingEnabled);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("When enabled, only textures of objects visible in the active camera viewport render at full native quality.\nOut-of-view textures drop to low resolution (dynamic LOD clamp) saving GPU memory and bandwidth.");
            }

            float lodClamp = Eunoia::TextureStreamingSystem::Get().GetNonVisibleLodClamp();
            if (ImGui::SliderFloat("Out-of-View LOD Clamp", &lodClamp, 1.0f, 4.0f, "Mip +%.1f")) {
                Eunoia::TextureStreamingSystem::Get().SetNonVisibleLodClamp(lodClamp);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("LOD clamp applied to out-of-view textures (2.0 = 1/4 resolution, 3.0 = 1/8 resolution). Zero GPU reallocations.");
            }

            auto metrics = Eunoia::TextureStreamingSystem::Get().GetMetrics();
            ImGui::Spacing();
            ImGui::TextDisabled("Streaming Telemetry:");
            ImGui::BulletText("Total Streamed Textures: %zu", metrics.totalRegistered);
            ImGui::BulletText("Viewport Visible (Full Quality): %zu", metrics.visibleHighResCount);
            ImGui::BulletText("Out-of-Viewport (Low Resolution): %zu", metrics.nonVisibleLowResCount);
            ImGui::BulletText("In-Flight GPU Uploads: %zu", metrics.inFlightUploads);
            ImGui::BulletText("Allocated Texture VRAM: ~%zu MB", metrics.totalGpuMemoryBytes / (1024 * 1024));
        }
    }
    ImGui::End();
}

void EngineUI::RenderLoadingModal() {
    if (!s_loadingState.active) return;

    float dt = ImGui::GetIO().DeltaTime;
    if (s_loadingState.completed) {
        s_loadingState.completionTimer -= dt;
        if (s_loadingState.completionTimer <= 0.0f) {
            s_loadingState.active = false;
            return;
        }
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();

    // 1. Semi-transparent backdrop dimming
    if (s_loadingState.isModal) {
        ImDrawList* bgDrawList = ImGui::GetForegroundDrawList();
        bgDrawList->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(8, 10, 15, 185));
    }

    // 2. Centered dialog box
    float boxW = 560.0f;
    float boxH = 260.0f;
    ImVec2 centerPos(vp->Pos.x + (vp->Size.x - boxW) * 0.5f, vp->Pos.y + (vp->Size.y - boxH) * 0.5f);

    ImGui::SetNextWindowPos(centerPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(boxW, 0.0f), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));

    ImVec4 borderColor = s_loadingState.hasError ? ImVec4(0.95f, 0.25f, 0.25f, 0.95f) :
                         (s_loadingState.completed ? ImVec4(0.25f, 0.90f, 0.45f, 0.95f) :
                         ImVec4(0.12f, 0.68f, 1.00f, 0.95f));

    ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.118f, 0.118f, 0.118f, 0.98f));

    if (ImGui::Begin("###LoadingOverlayWindow", nullptr, flags)) {
        float curTime = (float)ImGui::GetTime();
        double elapsed = s_loadingState.completed ? (s_loadingState.finishTime - s_loadingState.startTime) : (ImGui::GetTime() - s_loadingState.startTime);
        if (elapsed < 0.0) elapsed = 0.0;

        // Header Row: Spinner / Checkmark + Title + Status badge
        float spinnerRadius = 11.0f;
        ImVec2 curCursor = ImGui::GetCursorScreenPos();
        ImVec2 spinnerCenter(curCursor.x + spinnerRadius, curCursor.y + spinnerRadius + 2.0f);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        if (s_loadingState.completed) {
            // Completed icon
            drawList->AddCircleFilled(spinnerCenter, spinnerRadius, IM_COL32(30, 200, 80, 255));
            drawList->AddLine(ImVec2(spinnerCenter.x - 5, spinnerCenter.y), ImVec2(spinnerCenter.x - 1, spinnerCenter.y + 4), IM_COL32(255, 255, 255, 255), 2.5f);
            drawList->AddLine(ImVec2(spinnerCenter.x - 1, spinnerCenter.y + 4), ImVec2(spinnerCenter.x + 6, spinnerCenter.y - 4), IM_COL32(255, 255, 255, 255), 2.5f);
        } else if (s_loadingState.hasError) {
            drawList->AddCircleFilled(spinnerCenter, spinnerRadius, IM_COL32(220, 50, 50, 255));
            drawList->AddLine(ImVec2(spinnerCenter.x - 5, spinnerCenter.y - 5), ImVec2(spinnerCenter.x + 5, spinnerCenter.y + 5), IM_COL32(255, 255, 255, 255), 2.5f);
            drawList->AddLine(ImVec2(spinnerCenter.x + 5, spinnerCenter.y - 5), ImVec2(spinnerCenter.x - 5, spinnerCenter.y + 5), IM_COL32(255, 255, 255, 255), 2.5f);
        } else {
            // Rotating animated futuristic arc spinner
            drawList->AddCircle(spinnerCenter, spinnerRadius, IM_COL32(40, 50, 70, 255), 24, 2.5f);
            float startAngle = curTime * 7.0f;
            float endAngle = startAngle + 1.8f + 0.8f * sinf(curTime * 3.0f);
            drawList->PathArcTo(spinnerCenter, spinnerRadius, startAngle, endAngle, 20);
            drawList->PathStroke(IM_COL32(31, 162, 255, 255), false, 3.0f);
        }

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + spinnerRadius * 2.0f + 14.0f);

        // Title
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", s_loadingState.title.c_str());

        // Status badge on right
        float badgeW = 90.0f;
        ImGui::SameLine(boxW - badgeW - 48.0f);
        if (s_loadingState.completed) {
            ImGui::TextColored(ImVec4(0.3f, 0.95f, 0.5f, 1.0f), "[COMPLETE]");
        } else if (s_loadingState.hasError) {
            ImGui::TextColored(ImVec4(0.95f, 0.3f, 0.3f, 1.0f), "[FAILED]");
        } else {
            ImGui::TextColored(ImVec4(0.2f, 0.75f, 1.0f, 1.0f), "[PROCESSING]");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // High-tech Progress Bar
        float clampedProgress = s_loadingState.progress;
        if (clampedProgress < 0.0f) {
            clampedProgress = 0.5f + 0.45f * sinf(curTime * 4.0f);
        } else if (clampedProgress > 1.0f) {
            clampedProgress = 1.0f;
        }

        char overlayBuf[64];
        if (s_loadingState.completed) {
            snprintf(overlayBuf, sizeof(overlayBuf), "100%% - Finished (%.1fs)", (float)elapsed);
        } else if (s_loadingState.progress < 0.0f) {
            snprintf(overlayBuf, sizeof(overlayBuf), "Working... (%.1fs)", (float)elapsed);
        } else {
            snprintf(overlayBuf, sizeof(overlayBuf), "%.1f%% (%.1fs)", clampedProgress * 100.0f, (float)elapsed);
        }

        ImVec4 barColor = s_loadingState.hasError ? ImVec4(0.85f, 0.20f, 0.20f, 1.0f) :
                          (s_loadingState.completed ? ImVec4(0.20f, 0.85f, 0.40f, 1.0f) :
                          ImVec4(0.12f, 0.65f, 1.00f, 1.0f));

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.04f, 0.05f, 0.07f, 1.0f));
        ImGui::ProgressBar(clampedProgress, ImVec2(-1, 24.0f), overlayBuf);
        ImGui::PopStyleColor(2);

        ImGui::Spacing();

        // Details Panel under progress bar showing "What's happening now"
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.145f, 0.145f, 0.145f, 0.95f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
        ImGui::BeginChild("LoadingDetailsRegion", ImVec2(-1, 80.0f), true, ImGuiWindowFlags_NoScrollbar);

        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.0f), "CURRENT STATUS:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.25f, 0.85f, 1.00f, 1.0f), "> %s", s_loadingState.currentDetail.c_str());

        if (!s_loadingState.subDetail.empty()) {
            ImGui::TextColored(ImVec4(0.48f, 0.52f, 0.60f, 1.0f), "    %s", s_loadingState.subDetail.c_str());
        }

        // Show last history item if available
        if (!s_loadingState.recentHistory.empty()) {
            size_t count = s_loadingState.recentHistory.size();
            const std::string& prev = (count >= 2) ? s_loadingState.recentHistory[count - 2] : s_loadingState.recentHistory[0];
            if (prev != s_loadingState.currentDetail) {
                ImGui::TextColored(ImVec4(0.35f, 0.70f, 0.40f, 1.0f), "  [OK] %s", prev.c_str());
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // Footer: Elapsed time & Dismiss button
        ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.65f, 1.0f), "Elapsed Time: %.2f sec", (float)elapsed);
        ImGui::SameLine(boxW - 140.0f);
        if (s_loadingState.completed) {
            if (ImGui::Button("Close Now", ImVec2(92.0f, 26.0f))) {
                s_loadingState.active = false;
            }
        } else {
            static const char* dots[] = { "Running.", "Running..", "Running...", "Running...." };
            int dotIdx = (int)(curTime * 3.0f) % 4;
            ImGui::TextColored(ImVec4(0.2f, 0.7f, 1.0f, 1.0f), "%s", dots[dotIdx]);
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void EngineUI::RenderViewportLoadingOverlay() {
    if (!m_progressiveLoader.active) return;

    float dt = ImGui::GetIO().DeltaTime;
    float alpha = 1.0f;
    if (m_progressiveLoader.completed) {
        m_progressiveLoader.completionTimer -= dt;
        if (m_progressiveLoader.completionTimer <= 0.0f) {
            m_progressiveLoader.active = false;
            return;
        }
        if (m_progressiveLoader.completionTimer < 0.35f) {
            alpha = m_progressiveLoader.completionTimer / 0.35f;
        }
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!vp) return;

    ViewportRect vpRect = GetViewportRect(vp->Size.x, vp->Size.y);
    float centerX = vp->Pos.x + vpRect.x + vpRect.width * 0.5f;
    float centerY = vp->Pos.y + vpRect.y + vpRect.height * 0.5f;

    float cardW = 500.0f;
    float cardH = 136.0f;
    ImVec2 cardMin(centerX - cardW * 0.5f, centerY - cardH * 0.5f);
    ImVec2 cardMax(centerX + cardW * 0.5f, centerY + cardH * 0.5f);

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (!drawList) return;

    // Drop shadow
    drawList->AddRectFilled(ImVec2(cardMin.x - 4.0f, cardMin.y - 4.0f), ImVec2(cardMax.x + 4.0f, cardMax.y + 6.0f),
                            IM_COL32(0, 0, 0, (int)(110 * alpha)), 14.0f);

    // Glass backdrop
    drawList->AddRectFilled(cardMin, cardMax,
                            IM_COL32(11, 14, 20, (int)(235 * alpha)), 12.0f);

    // Border
    ImU32 borderColor = m_progressiveLoader.hasError
        ? IM_COL32(235, 65, 65, (int)(230 * alpha))
        : (m_progressiveLoader.completed
            ? IM_COL32(40, 210, 110, (int)(230 * alpha))
            : IM_COL32(35, 155, 255, (int)(220 * alpha)));
    drawList->AddRect(cardMin, cardMax, borderColor, 12.0f, 0, 2.0f);

    // Header row: Title + Stage badge
    float curTime = (float)ImGui::GetTime();
    std::string filename = std::filesystem::path(m_progressiveLoader.filePath).filename().string();
    std::string headerTitle = "LOADING LEVEL: " + filename;

    std::string stageName;
    std::string currentDetail;
    std::string subDetail;
    {
        std::lock_guard<std::mutex> lock(m_progressiveLoader.stateMutex);
        stageName = m_progressiveLoader.stageName;
        currentDetail = m_progressiveLoader.currentDetail;
        subDetail = m_progressiveLoader.subDetail;
    }

    drawList->AddText(ImVec2(cardMin.x + 20.0f, cardMin.y + 16.0f),
                      IM_COL32(245, 248, 255, (int)(255 * alpha)),
                      headerTitle.c_str());

    // Stage Badge (Pill)
    std::string badgeStr = "[" + stageName + "]";
    ImVec2 badgeSize = ImGui::CalcTextSize(badgeStr.c_str());
    float badgePadding = 8.0f;
    ImVec2 badgeMax(cardMax.x - 20.0f, cardMin.y + 16.0f + badgeSize.y + 4.0f);
    ImVec2 badgeMin(badgeMax.x - badgeSize.x - badgePadding * 2.0f, cardMin.y + 14.0f);

    ImU32 badgeBg = m_progressiveLoader.completed
        ? IM_COL32(20, 80, 45, (int)(220 * alpha))
        : IM_COL32(15, 50, 90, (int)(220 * alpha));
    ImU32 badgeTextCol = m_progressiveLoader.completed
        ? IM_COL32(60, 235, 120, (int)(255 * alpha))
        : IM_COL32(70, 195, 255, (int)(255 * alpha));

    drawList->AddRectFilled(badgeMin, badgeMax, badgeBg, 6.0f);
    drawList->AddText(ImVec2(badgeMin.x + badgePadding, badgeMin.y + 2.0f), badgeTextCol, badgeStr.c_str());

    // Progress bar track
    float barX = cardMin.x + 20.0f;
    float barY = cardMin.y + 46.0f;
    float barW = cardW - 40.0f;
    float barH = 18.0f;

    drawList->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH),
                            IM_COL32(20, 25, 36, (int)(255 * alpha)), 6.0f);

    // Progress bar fill
    float clampedProgress = std::clamp(m_progressiveLoader.progress.load(), 0.0f, 1.0f);
    float fillW = barW * clampedProgress;
    if (fillW > 2.0f) {
        ImU32 fillColLeft = m_progressiveLoader.completed
            ? IM_COL32(30, 180, 90, (int)(255 * alpha))
            : IM_COL32(0, 130, 240, (int)(255 * alpha));
        ImU32 fillColRight = m_progressiveLoader.completed
            ? IM_COL32(60, 230, 130, (int)(255 * alpha))
            : IM_COL32(30, 215, 255, (int)(255 * alpha));

        drawList->AddRectFilledMultiColor(ImVec2(barX, barY), ImVec2(barX + fillW, barY + barH),
                                          fillColLeft, fillColRight, fillColRight, fillColLeft);
    }

    // Progress percentage text centered on bar
    char pctBuf[32];
    snprintf(pctBuf, sizeof(pctBuf), "%.0f%%", clampedProgress * 100.0f);
    ImVec2 pctSize = ImGui::CalcTextSize(pctBuf);
    drawList->AddText(ImVec2(barX + (barW - pctSize.x) * 0.5f, barY + (barH - pctSize.y) * 0.5f),
                      IM_COL32(255, 255, 255, (int)(245 * alpha)), pctBuf);

    // Animated indicator icon / pulse dot
    ImVec2 dotCenter(cardMin.x + 30.0f, cardMin.y + 86.0f);
    if (m_progressiveLoader.completed) {
        drawList->AddCircleFilled(dotCenter, 6.0f, IM_COL32(40, 210, 110, (int)(255 * alpha)));
    } else {
        float pulse = 5.0f + 1.5f * sinf(curTime * 6.0f);
        drawList->AddCircleFilled(dotCenter, pulse, IM_COL32(0, 205, 255, (int)(255 * alpha)));
        drawList->AddCircle(dotCenter, pulse + 3.0f, IM_COL32(0, 160, 255, (int)(110 * alpha)), 16, 1.5f);
    }

    // Detail text
    drawList->AddText(ImVec2(cardMin.x + 46.0f, cardMin.y + 76.0f),
                      IM_COL32(215, 230, 248, (int)(255 * alpha)),
                      currentDetail.c_str());

    // Sub-detail text
    if (!subDetail.empty()) {
        drawList->AddText(ImVec2(cardMin.x + 46.0f, cardMin.y + 98.0f),
                          IM_COL32(130, 145, 165, (int)(230 * alpha)),
                          subDetail.c_str());
    }
}

void EngineUI::RenderSaveIndicator() {
    if (!m_saveIndicator.active) return;

    float dt = ImGui::GetIO().DeltaTime;
    m_saveIndicator.displayTimer -= dt;
    if (m_saveIndicator.displayTimer <= 0.0f) {
        m_saveIndicator.active = false;
        return;
    }

    float alpha = 1.0f;
    if (m_saveIndicator.displayTimer < 0.4f) {
        alpha = m_saveIndicator.displayTimer / 0.4f;
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!vp) return;

    float margin = uiMargin;
    float curBottomH = bottomDrawerOpen ? bottomDockHeight : 38.0f;
    float bottomW = vp->Size.x - 2.0f * margin;
    float bottomY = vp->Pos.y + vp->Size.y - curBottomH - margin;

    ImVec2 badgeSize(250.0f, 26.0f);
    ImVec2 badgePos;
    if (showBottomDrawer) {
        badgePos = ImVec2(vp->Pos.x + bottomW - badgeSize.x - 14.0f, bottomY + curBottomH - badgeSize.y - 6.0f);
    } else {
        badgePos = ImVec2(vp->Pos.x + vp->Size.x - badgeSize.x - 16.0f, vp->Pos.y + vp->Size.y - badgeSize.y - 14.0f);
    }

    // If background registry sync is also active, offset upwards to avoid overlap
    if (s_bgSync.isRunning.load()) {
        badgePos.y -= (badgeSize.y + 6.0f);
    }

    ImDrawList* fgList = ImGui::GetForegroundDrawList();
    if (!fgList) return;

    // Drop shadow
    fgList->AddRectFilled(ImVec2(badgePos.x - 2.0f, badgePos.y - 2.0f),
                          ImVec2(badgePos.x + badgeSize.x + 2.0f, badgePos.y + badgeSize.y + 3.0f),
                          IM_COL32(0, 0, 0, (int)(80 * alpha)), 6.0f);

    // Glass backdrop
    fgList->AddRectFilled(badgePos, ImVec2(badgePos.x + badgeSize.x, badgePos.y + badgeSize.y),
                          IM_COL32(14, 18, 26, (int)(235 * alpha)), 6.0f);

    // Border
    ImU32 borderColor = m_saveIndicator.isSaving
        ? IM_COL32(35, 140, 240, (int)(220 * alpha))
        : (m_saveIndicator.success
            ? IM_COL32(40, 200, 100, (int)(220 * alpha))
            : IM_COL32(230, 50, 50, (int)(220 * alpha)));
    fgList->AddRect(badgePos, ImVec2(badgePos.x + badgeSize.x, badgePos.y + badgeSize.y), borderColor, 6.0f, 0, 1.5f);

    float curTime = (float)ImGui::GetTime();

    if (m_saveIndicator.isSaving) {
        // Animated pulsing cyan circle
        float pulse = 4.5f + 1.2f * sinf(curTime * 8.0f);
        fgList->AddCircleFilled(ImVec2(badgePos.x + 15.0f, badgePos.y + 13.0f), pulse,
                                IM_COL32(0, 200, 255, (int)(255 * alpha)));
        fgList->AddCircle(ImVec2(badgePos.x + 15.0f, badgePos.y + 13.0f), pulse + 2.0f,
                          IM_COL32(0, 150, 255, (int)(110 * alpha)), 16, 1.0f);

        std::string text = "Saving " + m_saveIndicator.filename + "...";
        fgList->AddText(ImVec2(badgePos.x + 28.0f, badgePos.y + 5.0f),
                        IM_COL32(210, 235, 255, (int)(245 * alpha)), text.c_str());
    } else if (m_saveIndicator.success) {
        // Green check circle
        fgList->AddCircleFilled(ImVec2(badgePos.x + 15.0f, badgePos.y + 13.0f), 5.5f,
                                IM_COL32(40, 210, 110, (int)(255 * alpha)));

        std::string text = "Saved " + m_saveIndicator.filename;
        fgList->AddText(ImVec2(badgePos.x + 28.0f, badgePos.y + 5.0f),
                        IM_COL32(160, 245, 190, (int)(245 * alpha)), text.c_str());
    } else {
        // Red error circle
        fgList->AddCircleFilled(ImVec2(badgePos.x + 15.0f, badgePos.y + 13.0f), 5.5f,
                                IM_COL32(230, 50, 50, (int)(255 * alpha)));

        std::string text = "Failed to save: " + m_saveIndicator.filename;
        fgList->AddText(ImVec2(badgePos.x + 28.0f, badgePos.y + 5.0f),
                        IM_COL32(255, 160, 160, (int)(245 * alpha)), text.c_str());
    }
}



void EngineUI::RenderReferenceViewer(Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(800, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Asset Reference Viewer (dev.md Section 33)", &showReferenceViewer)) {
        ImGui::End();
        return;
    }

    const auto& allAssets = AssetRegistry::Get().GetAllAssets();
    if (allAssets.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "No assets currently registered in AssetRegistry.");
        ImGui::End();
        return;
    }

    // Header Asset Selector
    ImGui::TextColored(ImVec4(0.12f, 0.68f, 1.00f, 1.0f), "Select Asset to Inspect:");
    ImGui::SameLine();
    
    std::string currentAssetName = "None";
    const AssetMetadata* currentMeta = AssetRegistry::Get().FindById(refViewerSelectedAsset);
    if (currentMeta) {
        currentAssetName = std::string("[") + AssetTypeToString(currentMeta->type) + "] " + currentMeta->objectName + " (" + currentMeta->virtualPath + ")";
    }

    if (ImGui::BeginCombo("##RefViewerSelect", currentAssetName.c_str())) {
        for (const auto& pair : allAssets) {
            const AssetMetadata& m = pair.second;
            bool isSel = (m.id == refViewerSelectedAsset);
            std::string label = std::string("[") + AssetTypeToString(m.type) + "] " + m.objectName + " (" + m.virtualPath + ")";
            if (ImGui::Selectable(label.c_str(), isSel)) {
                refViewerSelectedAsset = m.id;
            }
            if (isSel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (!currentMeta) {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Please select an asset above to view its dependency and reference graph.");
        ImGui::End();
        return;
    }

    ImGui::Separator();

    // Asset Info Card
    ImGui::BeginChild("AssetInfoCard", ImVec2(0, 110), true);
    ImGui::Columns(2, "AssetInfoCols", false);
    ImGui::SetColumnWidth(0, 380);

    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Asset Details:");
    ImGui::Text("Object Name:  %s", currentMeta->objectName.c_str());
    ImGui::Text("Type:         %s", AssetTypeToString(currentMeta->type));
    ImGui::Text("Virtual Path: %s", currentMeta->virtualPath.c_str());

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Identifiers & Storage:");
    ImGui::Text("AssetID:      %s", currentMeta->id.ToString().c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy ID##Ref")) {
        ImGui::SetClipboardText(currentMeta->id.ToString().c_str());
        AddLog("LogAsset", "Copied AssetID to clipboard: " + currentMeta->id.ToString(), 0);
    }
    ImGui::Text("Source File:  %s", currentMeta->sourcePath.c_str());
    ImGui::Text("Cooked Path:  %s", currentMeta->cookedPath.empty() ? "(Not cooked yet)" : currentMeta->cookedPath.c_str());

    ImGui::Columns(1);
    ImGui::EndChild();

    ImGui::Spacing();

    // Split Graph: Left = Dependencies (Assets this uses), Right = References (Assets/Actors using this)
    float colWidth = (ImGui::GetContentRegionAvail().x - 16.0f) * 0.5f;

    ImGui::BeginChild("DependenciesPanel", ImVec2(colWidth, 260), true);
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Dependencies (What this asset uses):");
    ImGui::TextDisabled("Direct outbound references required by %s", currentMeta->objectName.c_str());
    ImGui::Separator();

    if (currentMeta->dependencies.empty()) {
        ImGui::TextDisabled("No outbound dependencies recorded.");
    } else {
        ImGui::BeginTable("DepsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();

        for (const auto& depId : currentMeta->dependencies) {
            const AssetMetadata* depMeta = AssetRegistry::Get().FindById(depId);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (depMeta) {
                ImGui::Text("%s", AssetTypeToString(depMeta->type));
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", depMeta->objectName.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", depMeta->virtualPath.c_str(), depId.ToString().c_str());
            } else {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Missing");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", depId.ToString().c_str());
            }

            ImGui::TableSetColumnIndex(2);
            std::string btnLabel = "View##" + depId.ToString();
            if (ImGui::SmallButton(btnLabel.c_str())) {
                refViewerSelectedAsset = depId;
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("ReferencesPanel", ImVec2(colWidth, 260), true);
    ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "References (What uses this asset):");
    ImGui::TextDisabled("Assets & World Actors referencing this asset");
    ImGui::Separator();

    // 1. Other Assets referencing this
    const auto& refs = currentMeta->references;
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.9f, 1.0f), "Asset References (%zu):", refs.size());
    if (refs.empty()) {
        ImGui::TextDisabled("No other registered assets depend on this.");
    } else {
        for (const auto& refId : refs) {
            const AssetMetadata* rMeta = AssetRegistry::Get().FindById(refId);
            if (rMeta) {
                ImGui::BulletText("[%s] %s", AssetTypeToString(rMeta->type), rMeta->objectName.c_str());
                ImGui::SameLine();
                std::string btnLabel = "Inspect##" + refId.ToString();
                if (ImGui::SmallButton(btnLabel.c_str())) {
                    refViewerSelectedAsset = refId;
                }
            } else {
                ImGui::BulletText("Unknown (%s)", refId.ToString().c_str());
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // 2. Active Level Actors referencing this
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.9f, 1.0f), "Active Level Actors referencing this:");
    int sceneRefCount = 0;
    for (const auto& obj : scene.objects) {
        bool match = false;
        if (currentMeta->type == AssetType::Material) {
            if (obj.materialName == currentMeta->objectName || obj.materialName == currentMeta->virtualPath) {
                match = true;
            }
        } else if (currentMeta->type == AssetType::Texture) {
            if (obj.baseColorTexture == currentMeta->objectName || obj.baseColorTexture == currentMeta->sourcePath ||
                obj.normalTexture == currentMeta->objectName || obj.roughnessTexture == currentMeta->objectName ||
                obj.metallicTexture == currentMeta->objectName || obj.aoTexture == currentMeta->objectName ||
                obj.emissionTexture == currentMeta->objectName) {
                match = true;
            }
        }
        if (match) {
            sceneRefCount++;
            ImGui::BulletText("Actor '%s' (ID: %d)", obj.name.c_str(), obj.id);
            ImGui::SameLine();
            std::string btn = "Select##Actor" + std::to_string(obj.id);
            if (ImGui::SmallButton(btn.c_str())) {
                scene.selectedId = obj.id;
                AddLog("LogRefViewer", "Selected Actor '" + obj.name + "' from Reference Viewer", 0);
            }
        }
    }
    if (sceneRefCount == 0) {
        ImGui::TextDisabled("No active level actors currently bound.");
    }

    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::Button("Close Reference Viewer", ImVec2(180, 26))) {
        showReferenceViewer = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh Graph", ImVec2(130, 26))) {
        AssetRegistry::Get().ScanDirectory(contentRootPath);
        AddLog("LogRefViewer", "Refreshed Asset Registry dependency graph", 0);
    }

    ImGui::End();
}

// ============================================================================
// Play Mode / External Game Window Management (User Request: external window only)
// ============================================================================

static HANDLE s_gameProcessHandle = NULL;
static DWORD s_gameProcessId = 0;

bool EngineUI::IsExternalGameRunning() {
    if (!s_gameProcessHandle) return false;
    DWORD exitCode = 0;
    if (GetExitCodeProcess(s_gameProcessHandle, &exitCode)) {
        if (exitCode == STILL_ACTIVE) {
            return true;
        }
    }
    CloseHandle(s_gameProcessHandle);
    s_gameProcessHandle = NULL;
    s_gameProcessId = 0;
    return false;
}

void EngineUI::StopExternalGame() {
    if (!s_gameProcessHandle) return;
    DWORD exitCode = 0;
    if (GetExitCodeProcess(s_gameProcessHandle, &exitCode) && exitCode == STILL_ACTIVE) {
        TerminateProcess(s_gameProcessHandle, 0);
        AddLog("LogPlayLevel", "External Game window closed.", 0);
    }
    CloseHandle(s_gameProcessHandle);
    s_gameProcessHandle = NULL;
    s_gameProcessId = 0;
}

void EngineUI::EnterPlayMode(Scene& scene, OrbitCamera* cameraPtr) {
    if (scene.isPlayMode) return;

    OrbitCamera* activeCam = cameraPtr ? cameraPtr : currentCamera;
    if (activeCam) {
        savedCameraYaw = activeCam->yaw;
        savedCameraPitch = activeCam->pitch;
        savedCameraTarget = activeCam->target;
        savedCameraDistance = activeCam->distance;
        savedCameraFov = activeCam->fov;
        hasSavedPlayModeCamera = true;
    }

    // If an Active Level Camera actor exists, align viewport camera to it for gameplay
    int activeCamId = scene.activeLevelCameraId;
    GameObject* camObj = (activeCamId != -1) ? scene.FindObject(activeCamId) : nullptr;
    if (!camObj) {
        for (auto& obj : scene.objects) {
            if (obj.isCamera || obj.type == PrimitiveType::Camera) {
                camObj = &obj;
                break;
            }
        }
    }
    if (camObj && activeCam) {
        glm::vec3 worldPos = scene.GetWorldPosition(*camObj);
        activeCam->yaw = camObj->rotation.y;
        activeCam->pitch = -std::clamp(camObj->rotation.x, -89.0f, 89.0f);
        activeCam->fov = camObj->camera.fov;
        activeCam->nearPlane = std::max(0.01f, camObj->camera.nearPlane);
        activeCam->farPlane = std::max(1.0f, camObj->camera.farPlane);
        activeCam->distance = 0.001f;
        activeCam->target = worldPos + activeCam->GetForward() * 0.001f;
    }

    isPlayModeActive = true;
    CursorState.Locked();
    scene.SyncLightPositionsFromActors();
    scene.StartPlayMode();
    AddLog("LogPlayLevel", "Entered Play Mode in Editor (Full Screen Fit)", 2);
}

void EngineUI::ExitPlayMode(Scene& scene, OrbitCamera* cameraPtr) {
    if (!scene.isPlayMode) return;

    scene.StopPlayMode();
    EunoiaUI::UISystemManager::Get().RemoveAll();
    isPlayModeActive = false;
    CursorState.UnLocked();
    InputSystem::Get().SetCursorLocked(false);

    OrbitCamera* activeCam = cameraPtr ? cameraPtr : currentCamera;
    if (activeCam && hasSavedPlayModeCamera) {
        activeCam->yaw = savedCameraYaw;
        activeCam->pitch = savedCameraPitch;
        activeCam->target = savedCameraTarget;
        activeCam->distance = savedCameraDistance;
        activeCam->fov = savedCameraFov;
        hasSavedPlayModeCamera = false;
    }

    s_justExitedPlayMode = true;
    AddLog("LogPlayLevel", "Exited Play Mode, restored editor state", 0);
}

void EngineUI::LaunchGameSeparateWindow(Scene& scene, OrbitCamera* cameraPtr) {
    if (IsExternalGameRunning()) {
        AddLog("LogPlayLevel", "Game Mode is already running in an external window.", 1);
        return;
    }

    std::filesystem::path projectBase = activeProjectRoot.empty() ? contentRootPath.parent_path() : activeProjectRoot;
    std::string projName = projectBase.filename().string();
    std::filesystem::path gameBuildDir = projectBase / "Build";
    std::error_code ec;

    // Fast-save active level snapshot to dedicated session files:
    // .active_play_level.escene (JSON scene state) and .active_play_level.bin (fast binary mesh cache)
    std::filesystem::path targetScene = projectBase / "Content" / ".active_play_level.escene";
    std::filesystem::path targetBin   = projectBase / "Content" / ".active_play_level.bin";
    std::filesystem::create_directories(targetScene.parent_path(), ec);

    SceneSerializer::SaveScene(scene, targetScene.string());
    SceneSerializer::SaveFastMeshCache(scene, targetBin.string());
    AddLog("LogPlayLevel", "Prepared active level exact snapshot for Play Mode", 0);

    OrbitCamera* activeCam = cameraPtr ? cameraPtr : currentCamera;
    float camYaw = activeCam ? activeCam->yaw : 45.0f;
    float camPitch = activeCam ? activeCam->pitch : 25.0f;
    glm::vec3 camTarget = activeCam ? activeCam->target : glm::vec3(0.0f, 0.5f, 0.0f);
    float camDist = activeCam ? activeCam->distance : 7.0f;
    float camFov = activeCam ? activeCam->fov : 60.0f;

    // Get current Eunoia-Editor.exe executable path
    char exePathBuf[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exePathBuf, MAX_PATH);
    std::filesystem::path editorExe(exePathBuf);

    if (std::filesystem::exists(editorExe, ec)) {
        std::string cmdArgs = "--game \"" + projectBase.string() + "\""
                            + " --scene \"" + targetScene.string() + "\""
                            + " --bin \"" + targetBin.string() + "\""
                            + " --cam-yaw " + std::to_string(camYaw)
                            + " --cam-pitch " + std::to_string(camPitch)
                            + " --cam-tx " + std::to_string(camTarget.x)
                            + " --cam-ty " + std::to_string(camTarget.y)
                            + " --cam-tz " + std::to_string(camTarget.z)
                            + " --cam-dist " + std::to_string(camDist)
                            + " --cam-fov " + std::to_string(camFov);

        std::string fullCmd = "\"" + editorExe.string() + "\" " + cmdArgs;

        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        std::vector<char> cmdBuf(fullCmd.begin(), fullCmd.end());
        cmdBuf.push_back('\0');

        std::filesystem::path engineWorkingDir = editorExe.parent_path();
        BOOL success = CreateProcessA(
            NULL,
            cmdBuf.data(),
            NULL,
            NULL,
            FALSE,
            0,
            NULL,
            engineWorkingDir.string().c_str(),
            &si,
            &pi
        );

        if (success) {
            s_gameProcessHandle = pi.hProcess;
            s_gameProcessId = pi.dwProcessId;
            CloseHandle(pi.hThread);
            AddLog("LogPlayLevel", "Launched Game Mode in separate window (PID: " + std::to_string(pi.dwProcessId) + ")", 2);
        } else {
            AddLog("LogPlayLevel", "Failed to launch game process (Error code: " + std::to_string(GetLastError()) + ")", 3);
        }
    } else {
        AddLog("LogPlayLevel", "Could not locate engine executable: " + editorExe.string(), 3);
    }
}

// ============================================================================
// Behaviour Script Watcher & Compilation (Auto-compile on focus & Centered Overlay)
// ============================================================================

void EngineUI::CheckForScriptChanges(Scene* scene) {
    if (isCompilingScripts) return;

    std::filesystem::path projectBase = activeProjectRoot.empty() ? contentRootPath.parent_path() : activeProjectRoot;
    std::filesystem::path contentDir = projectBase / "Content";
    std::error_code ec;
    if (!std::filesystem::exists(contentDir, ec)) return;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(contentDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".cpp") {
            std::string pStr = entry.path().string();
            std::string pLower = pStr;
            std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::tolower);
            if (pLower.find(".assetmeta") != std::string::npos || pLower.find(".meta") != std::string::npos) continue;

            auto currentLwt = entry.last_write_time(ec);
            if (ec) continue;

            std::string stem = entry.path().stem().string();
            BehaviourRegistry::Get().RegisterScriptFile(stem, pStr);

            auto it = scriptFileTimestamps.find(pStr);
            if (it == scriptFileTimestamps.end()) {
                // Initial recording of this script file
                scriptFileTimestamps[pStr] = currentLwt;
            } else {
                // If timestamp is newer, file was modified externally
                if (currentLwt > it->second) {
                    if (std::find(pendingChangedScripts.begin(), pendingChangedScripts.end(), pStr) == pendingChangedScripts.end()) {
                        pendingChangedScripts.push_back(pStr);
                    }
                    // Update timestamp to current so we only queue once per file edit
                    scriptFileTimestamps[pStr] = currentLwt;
                }
            }
        }
    }

    // Only auto-compile when the editor window is focused (main window)
    bool isEditorFocused = false;
    if (m_window) {
        isEditorFocused = (glfwGetWindowAttrib(m_window, GLFW_FOCUSED) == GLFW_TRUE);
    }
#ifdef _WIN32
    if (m_window) {
        HWND editorHwnd = glfwGetWin32Window(m_window);
        HWND fgHwnd = GetForegroundWindow();
        if (editorHwnd && fgHwnd != editorHwnd) {
            isEditorFocused = false;
        }
    }
#endif

    if (!pendingChangedScripts.empty() && isEditorFocused) {
        TriggerCompileScripts(scene);
    }
}

void EngineUI::TriggerCompileScripts(Scene* scene) {
    if (isCompilingScripts) return;
    isCompilingScripts = true;

    AddLog("LogScript", "Starting compilation of behaviour scripts...", 0);

    std::filesystem::path projectBase = activeProjectRoot.empty() ? contentRootPath.parent_path() : activeProjectRoot;
    std::filesystem::path gameBuildDir = projectBase / "Build";
    std::error_code ec;
    std::filesystem::create_directories(gameBuildDir / "Scenes", ec);
    std::filesystem::create_directories(projectBase / "Content" / "Scenes", ec);

    if (scene) {
        SceneSerializer::SaveScene(*scene, (gameBuildDir / "Scenes" / "Main.escene").string());
        SceneSerializer::SaveScene(*scene, (gameBuildDir / "Main.escene").string());
        SceneSerializer::SaveScene(*scene, (projectBase / "Content" / "Scenes" / "Main.escene").string());
        AddLog("LogScript", "Active level exported to game build.", 0);
    }

    std::filesystem::path engineRoot = FindEngineSourceRoot();
    std::filesystem::path batPath = engineRoot / "CompileGameScripts.bat";
    if (!std::filesystem::exists(batPath, ec)) {
        batPath = "C:\\Projects\\Eunoia-Engine\\Eunoia-Engine\\CompileGameScripts.bat";
    }

    std::string cmd = "cmd.exe /c \"" + batPath.string() + "\" \"" + projectBase.string() + "\" 2>&1";

    // Asynchronously run compiler process in background thread
    std::thread([this, cmd, projectBase]() {
        FILE* pipe = _popen(cmd.c_str(), "r");
        if (!pipe) {
            std::lock_guard<std::mutex> lock(asyncLogMutex);
            pendingAsyncLogs.push_back({ "LogScript", "Failed to launch script compiler batch process.", 3 });
            isCompilingScripts = false;
            return;
        }

        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string line = buffer;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
            if (!line.empty()) {
                std::lock_guard<std::mutex> lock(asyncLogMutex);
                if (line.rfind("[ERROR]", 0) == 0 || line.find("error:") != std::string::npos) {
                    pendingAsyncLogs.push_back({ "LogScript", line, 3 });
                } else if (line.rfind("[SUCCESS]", 0) == 0) {
                    pendingAsyncLogs.push_back({ "LogScript", line, 2 });
                } else if (line.rfind("[INFO]", 0) == 0) {
                    pendingAsyncLogs.push_back({ "LogScript", line, 0 });
                } else {
                    pendingAsyncLogs.push_back({ "LogBuild", line, 0 });
                }
            }
        }

        int exitCode = _pclose(pipe);
        {
            std::lock_guard<std::mutex> lock(asyncLogMutex);
            if (exitCode == 0) {
                pendingAsyncLogs.push_back({ "LogScript", "All behaviour scripts compiled successfully! Standalone Game binary is ready.", 2 });
            } else {
                pendingAsyncLogs.push_back({ "LogScript", "Script compilation finished with errors (Exit code " + std::to_string(exitCode) + ").", 3 });
            }
            pendingChangedScripts.clear();
        }

        // Update timestamps to latest after compile
        std::error_code sEc;
        std::filesystem::path contentDir = projectBase / "Content";
        for (const auto& entry : std::filesystem::recursive_directory_iterator(contentDir, std::filesystem::directory_options::skip_permission_denied, sEc)) {
            if (entry.is_regular_file(sEc) && entry.path().extension() == ".cpp") {
                scriptFileTimestamps[entry.path().string()] = entry.last_write_time(sEc);
            }
        }

        isCompilingScripts = false;
    }).detach();
}

void EngineUI::RenderScriptCompileModal(Scene& scene) {
    if (!isCompilingScripts) return;

    ImDrawList* fgDraw = ImGui::GetForegroundDrawList();
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 0.0f || displaySize.y <= 0.0f) return;

    ImVec2 center(displaySize.x * 0.5f, displaySize.y * 0.5f);

    // Dimmed translucent backdrop
    fgDraw->AddRectFilled(ImVec2(0, 0), displaySize, IM_COL32(12, 14, 18, 175));

    // Centered modal card
    float cardWidth = 420.0f;
    float cardHeight = 135.0f;
    ImVec2 cardMin(center.x - cardWidth * 0.5f, center.y - cardHeight * 0.5f);
    ImVec2 cardMax(center.x + cardWidth * 0.5f, center.y + cardHeight * 0.5f);

    // Glowing pulsating border
    float time = (float)glfwGetTime();
    float pulse = 0.65f + 0.35f * sinf(time * 4.0f);
    ImU32 borderColor = IM_COL32((int)(30 * pulse), (int)(180 * pulse), (int)(255 * pulse), 245);
    ImU32 cardBg = IM_COL32(20, 22, 28, 250);

    // Drop shadow & Card body
    fgDraw->AddRectFilled(ImVec2(cardMin.x - 6, cardMin.y - 6), ImVec2(cardMax.x + 6, cardMax.y + 6), IM_COL32(0, 0, 0, 90), 14.0f);
    fgDraw->AddRectFilled(cardMin, cardMax, cardBg, 12.0f);
    fgDraw->AddRect(cardMin, cardMax, borderColor, 12.0f, 0, 2.0f);

    // Animated Spinner Arc
    float spinnerRadius = 18.0f;
    ImVec2 spinnerCenter(center.x, center.y - 18.0f);
    int numSegments = 32;
    for (int i = 0; i < numSegments; ++i) {
        float aMin = ((float)i / (float)numSegments) * 6.2831853f + time * 5.5f;
        float aMax = (((float)i + 0.75f) / (float)numSegments) * 6.2831853f + time * 5.5f;
        float alpha = (float)i / (float)numSegments;
        ImU32 spinColor = IM_COL32(20, (int)(180 + 75 * alpha), 255, (int)(alpha * 255));
        fgDraw->PathArcTo(spinnerCenter, spinnerRadius, aMin, aMax, 4);
        fgDraw->PathStroke(spinColor, 0, 3.5f);
    }

    // Centered Title Text: "Compiling C++ Scripts..."
    const char* titleText = "Compiling C++ Scripts...";
    ImVec2 titleSize = ImGui::CalcTextSize(titleText);
    fgDraw->AddText(ImVec2(center.x - titleSize.x * 0.5f, center.y + 12.0f), IM_COL32(245, 248, 255, 255), titleText);

    // Subtext
    const char* subText = "Please wait while code compiles...";
    ImVec2 subSize = ImGui::CalcTextSize(subText);
    fgDraw->AddText(ImVec2(center.x - subSize.x * 0.5f, center.y + 34.0f), IM_COL32(130, 155, 180, 220), subText);
}

// ============================================================================
// 3D Model Real-Time Detection & Import Wizard ("One by One")
// ============================================================================

void EngineUI::CheckForModelChanges() {
    std::filesystem::path projectBase = activeProjectRoot.empty() ? contentRootPath.parent_path() : activeProjectRoot;
    std::filesystem::path contentDir = projectBase / "Content";
    std::error_code ec;
    if (!std::filesystem::exists(contentDir, ec)) return;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(contentDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_directory(ec)) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".fbx" || ext == ".glb" || ext == ".gltf" || ext == ".obj") {
                std::string fullPath = std::filesystem::absolute(entry.path()).string();
                if (knownModelFiles.find(fullPath) == knownModelFiles.end()) {
                    knownModelFiles.insert(fullPath);
                    QueueModelImport(fullPath);
                }
            }
        }
    }
}

void EngineUI::QueueModelImport(const std::string& filePath) {
    if (filePath.empty()) return;
    std::string absPath = std::filesystem::absolute(filePath).string();
    knownModelFiles.insert(absPath);
    for (const auto& queued : pendingModelImportQueue) {
        if (queued == absPath) return;
    }
    pendingModelImportQueue.push_back(absPath);
    AddLog("LogImport", "Queued 3D model for import inspection: " + std::filesystem::path(filePath).filename().string(), 0);

    if (!showModelImportModal) {
        OpenModelImportView(pendingModelImportQueue.front());
    }
}

void EngineUI::AdvanceModelImportQueue() {
    if (!pendingModelImportQueue.empty()) {
        pendingModelImportQueue.erase(pendingModelImportQueue.begin());
    }
    if (!pendingModelImportQueue.empty()) {
        OpenModelImportView(pendingModelImportQueue.front());
    } else {
        showModelImportModal = false;
    }
}

bool EngineUI::OpenModelImportView(const std::string& filePath) {
    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec)) {
        AddLog("LogImport", "Failed to open model import view: File does not exist: " + filePath, 3);
        AdvanceModelImportQueue();
        return false;
    }

    currentModelImport = ModelImportState();
    currentModelImport.sourceFilePath = filePath;
    std::filesystem::path p(filePath);
    currentModelImport.modelName = p.stem().string();

    std::filesystem::path defaultMatDir = p.parent_path() / "Materials";
    std::filesystem::create_directories(defaultMatDir, ec);
    currentModelImport.targetFolder = defaultMatDir.string();

    // Load model for geometry and material extraction
    currentModelImport.previewModel = MeshImporter::Load(filePath);
    if (!currentModelImport.previewModel.valid || currentModelImport.previewModel.meshes.empty()) {
        AddLog("LogImport", "Warning: Could not parse 3D mesh geometry for '" + filePath + "': " + currentModelImport.previewModel.errorMsg, 1);
    }

    // Calculate bounds & statistics
    glm::vec3 minB(1e9f);
    glm::vec3 maxB(-1e9f);
    uint32_t totalV = 0;
    uint32_t totalT = 0;

    for (const auto& mesh : currentModelImport.previewModel.meshes) {
        for (const auto& v : mesh.vertices) {
            if (!std::isnan(v.pos.x) && !std::isnan(v.pos.y) && !std::isnan(v.pos.z)) {
                minB = glm::min(minB, v.pos);
                maxB = glm::max(maxB, v.pos);
            }
        }
        totalV += (uint32_t)mesh.vertices.size();
        totalT += (uint32_t)(mesh.indices.size() / 3);
    }

    currentModelImport.totalVertices = totalV;
    currentModelImport.totalTriangles = totalT;
    currentModelImport.meshCount = (uint32_t)currentModelImport.previewModel.meshes.size();

    if (totalV > 0 && maxB.x >= minB.x) {
        currentModelImport.boundsMin = minB;
        currentModelImport.boundsMax = maxB;
        currentModelImport.boundsCenter = (minB + maxB) * 0.5f;
        currentModelImport.boundsExtent = glm::max(glm::length(maxB - minB), 0.001f);
    } else {
        currentModelImport.boundsMin = glm::vec3(-0.5f);
        currentModelImport.boundsMax = glm::vec3(0.5f);
        currentModelImport.boundsCenter = glm::vec3(0.0f);
        currentModelImport.boundsExtent = 1.0f;
    }

    // Extract materials & textures from the imported model
    std::unordered_map<std::string, size_t> matMap;
    int subIdx = 0;
    for (const auto& mesh : currentModelImport.previewModel.meshes) {
        std::string rawMatName = mesh.material.name;
        if (rawMatName.empty()) rawMatName = "Material_" + std::to_string(subIdx);

        auto it = matMap.find(rawMatName);
        if (it != matMap.end()) {
            currentModelImport.materials[it->second].submeshIndices.push_back(subIdx);
        } else {
            PendingModelMaterial pmm;
            std::string cleanMatName = rawMatName;
            for (char& c : cleanMatName) {
                if (c == ' ' || c == '.' || c == ':' || c == '/' || c == '\\') c = '_';
            }
            if (cleanMatName.rfind("M_", 0) != 0) {
                pmm.materialName = "M_" + currentModelImport.modelName + "_" + cleanMatName;
            } else {
                pmm.materialName = cleanMatName;
            }

            pmm.baseColor = mesh.material.baseColor;
            pmm.metallic = mesh.material.metallic;
            pmm.roughness = mesh.material.roughness;
            pmm.emissiveColor = mesh.material.emissiveColor;
            pmm.emissiveIntensity = mesh.material.emissiveIntensity;
            pmm.blendMode = mesh.material.blendMode;
            pmm.opacity = mesh.material.opacity;
            pmm.baseColorTexture = mesh.material.baseColorTexture;
            pmm.normalTexture = mesh.material.normalTexture;
            pmm.roughnessTexture = mesh.material.roughnessTexture;
            pmm.metallicTexture = mesh.material.metallicTexture;
            pmm.aoTexture = mesh.material.aoTexture;
            pmm.emissionTexture = mesh.material.emissionTexture;
            pmm.opacityTexture = mesh.material.opacityTexture;
            pmm.submeshIndices.push_back(subIdx);
            pmm.createMaterial = true;

            matMap[rawMatName] = currentModelImport.materials.size();
            currentModelImport.materials.push_back(pmm);
        }
        subIdx++;
    }

    // Default material fallback
    if (currentModelImport.materials.empty()) {
        PendingModelMaterial pmm;
        pmm.materialName = "M_" + currentModelImport.modelName + "_Default";
        pmm.baseColor = glm::vec3(0.8f, 0.8f, 0.8f);
        pmm.metallic = 0.0f;
        pmm.roughness = 0.5f;
        pmm.createMaterial = true;
        for (int i = 0; i < (int)currentModelImport.previewModel.meshes.size(); ++i) {
            pmm.submeshIndices.push_back(i);
        }
        currentModelImport.materials.push_back(pmm);
    }

    showModelImportModal = true;
    return true;
}

void EngineUI::ExecuteModelImport(Scene& scene, bool spawnInLevel, bool advanceQueue) {
    StartLoadingTask("Importing 3D Model: " + currentModelImport.modelName, "Analyzing geometry and textures...", 0.12f, false);

    std::error_code ec;
    std::filesystem::path modelPath(currentModelImport.sourceFilePath);
    std::filesystem::path modelDir = modelPath.parent_path();
    std::filesystem::path matDir = currentModelImport.targetFolder.empty() ? (modelDir / "Materials") : std::filesystem::path(currentModelImport.targetFolder);
    std::filesystem::path texDir = modelDir / "Textures";

    std::filesystem::create_directories(matDir, ec);
    std::filesystem::create_directories(texDir, ec);

    std::string matVirtualDir = "/Game/Materials";
    std::filesystem::path relMat = std::filesystem::relative(matDir, contentRootPath, ec);
    if (!ec && !relMat.empty() && relMat != ".") {
        matVirtualDir = "/Game/" + relMat.generic_string();
    }

    std::string texVirtualDir = "/Game/Textures";
    std::filesystem::path relTex = std::filesystem::relative(texDir, contentRootPath, ec);
    if (!ec && !relTex.empty() && relTex != ".") {
        texVirtualDir = "/Game/" + relTex.generic_string();
    }

    // Helper to ensure a texture is copied locally into the model's Textures folder and registered
    auto ensureTextureLocalAndRegistered = [&](std::string& texPath) {
        if (texPath.empty()) return;
        std::filesystem::path tp(texPath);
        std::error_code tec;
        if (!std::filesystem::exists(tp, tec)) {
            std::string resolved = MeshImporter::ResolveModelTexturePath(texPath, modelDir, currentModelImport.modelName);
            if (!resolved.empty()) tp = resolved;
        }

        if (std::filesystem::exists(tp, tec)) {
            std::filesystem::path targetTexPath = texDir / tp.filename();
            if (std::filesystem::absolute(tp) != std::filesystem::absolute(targetTexPath)) {
                std::filesystem::copy_file(tp, targetTexPath, std::filesystem::copy_options::overwrite_existing, tec);
            }
            texPath = targetTexPath.string();

            AssetMetadata texMeta;
            texMeta.id = AssetID::CreateRandom();
            texMeta.objectName = targetTexPath.stem().string();
            texMeta.type = AssetType::Texture;
            texMeta.sourcePath = targetTexPath.string();
            texMeta.virtualPath = texVirtualDir + "/" + texMeta.objectName;
            AssetRegistry::Get().RegisterAsset(texMeta);
        }
    };

    UpdateLoadingTask(0.35f, "Organizing and linking textures...", currentModelImport.modelName);

    // 1. Process all materials and copy textures to <ModelDir>/Textures/
    for (auto& pm : currentModelImport.materials) {
        ensureTextureLocalAndRegistered(pm.baseColorTexture);
        ensureTextureLocalAndRegistered(pm.normalTexture);
        ensureTextureLocalAndRegistered(pm.roughnessTexture);
        ensureTextureLocalAndRegistered(pm.metallicTexture);
        ensureTextureLocalAndRegistered(pm.aoTexture);
        ensureTextureLocalAndRegistered(pm.emissionTexture);
        ensureTextureLocalAndRegistered(pm.opacityTexture);
    }

    UpdateLoadingTask(0.60f, "Generating material assets (.emat)...", currentModelImport.modelName);

    int matsCreated = 0;
    std::vector<ImportedMaterial> updatedCachedMats;

    // 2. Create .emat files and register in AssetRegistry
    for (const auto& pm : currentModelImport.materials) {
        if (!pm.createMaterial) continue;

        MaterialAsset ma;
        ma.assetId = AssetID::CreateRandom();
        ma.name = pm.materialName;
        std::filesystem::path matFilePath = matDir / (pm.materialName + ".emat");
        ma.filePath = matFilePath.string();
        ma.virtualPath = matVirtualDir + "/" + pm.materialName;
        ma.baseColor = pm.baseColor;
        ma.metallic = pm.metallic;
        ma.roughness = pm.roughness;
        ma.baseColorTexture = pm.baseColorTexture;
        ma.normalTexture = pm.normalTexture;
        ma.roughnessTexture = pm.roughnessTexture;
        ma.metallicTexture = pm.metallicTexture;
        ma.aoTexture = pm.aoTexture;
        ma.emissionTexture = pm.emissionTexture;
        ma.emissiveColor = pm.emissiveColor;
        ma.emissiveIntensity = pm.emissiveIntensity;
        ma.blendMode = pm.blendMode;
        ma.opacity = pm.opacity;
        ma.opacityTexture = pm.opacityTexture;

        if (SaveMaterialFile(ma.filePath, ma)) {
            matsCreated++;
            AssetMetadata meta;
            meta.id = ma.assetId;
            meta.objectName = ma.name;
            meta.type = AssetType::Material;
            meta.sourcePath = ma.filePath;
            meta.virtualPath = ma.virtualPath;
            AssetRegistry::Get().RegisterAsset(meta);
        }

        ImportedMaterial im;
        im.name = pm.materialName;
        im.baseColor = pm.baseColor;
        im.metallic = pm.metallic;
        im.roughness = pm.roughness;
        im.baseColorTexture = pm.baseColorTexture;
        im.normalTexture = pm.normalTexture;
        im.roughnessTexture = pm.roughnessTexture;
        im.metallicTexture = pm.metallicTexture;
        im.aoTexture = pm.aoTexture;
        im.emissionTexture = pm.emissionTexture;
        im.emissiveColor = pm.emissiveColor;
        im.emissiveIntensity = pm.emissiveIntensity;
        im.blendMode = pm.blendMode;
        im.opacity = pm.opacity;
        im.opacityTexture = pm.opacityTexture;
        im.hasMaterial = true;
        updatedCachedMats.push_back(im);
    }

    UpdateLoadingTask(0.80f, "Updating model cache & scene objects...", currentModelImport.modelName);

    // Update MeshImporter cached materials so any current or subsequent instantiation loads with these textures & materials
    MeshImporter::UpdateCachedModelMaterials(currentModelImport.sourceFilePath, updatedCachedMats);

    // 3. Register 3D Model in AssetRegistry
    if (std::filesystem::exists(currentModelImport.sourceFilePath, ec)) {
        AssetMetadata meta;
        meta.id = AssetID::CreateRandom();
        meta.objectName = currentModelImport.modelName;
        meta.type = AssetType::Mesh;
        meta.sourcePath = currentModelImport.sourceFilePath;
        std::string modelVirtualDir = "/Game/Models";
        std::filesystem::path relModel = std::filesystem::relative(modelPath.parent_path(), contentRootPath, ec);
        if (!ec && !relModel.empty() && relModel != ".") {
            modelVirtualDir = "/Game/" + relModel.generic_string();
        }
        meta.virtualPath = modelVirtualDir + "/" + currentModelImport.modelName;
        AssetRegistry::Get().RegisterAsset(meta);
    }

    // 4. Helper to apply created materials to a GameObject
    auto applyMatToObj = [&](GameObject* obj, int submeshIdx) {
        if (!obj) return;
        const PendingModelMaterial* matchedPm = nullptr;
        for (const auto& pm : currentModelImport.materials) {
            for (int sIdx : pm.submeshIndices) {
                if (sIdx == submeshIdx) {
                    matchedPm = &pm;
                    break;
                }
            }
            if (matchedPm) break;
        }
        if (!matchedPm && !currentModelImport.materials.empty()) {
            matchedPm = &currentModelImport.materials[0];
        }
        if (matchedPm) {
            obj->materialName = matchedPm->materialName;
            obj->color = matchedPm->baseColor;
            obj->metallic = matchedPm->metallic;
            obj->roughness = matchedPm->roughness;
            obj->baseColorTexture = matchedPm->baseColorTexture;
            obj->normalTexture = matchedPm->normalTexture;
            obj->roughnessTexture = matchedPm->roughnessTexture;
            obj->metallicTexture = matchedPm->metallicTexture;
            obj->aoTexture = matchedPm->aoTexture;
            obj->emissionTexture = matchedPm->emissionTexture;
            obj->emissiveColor = matchedPm->emissiveColor;
            obj->emissiveIntensity = matchedPm->emissiveIntensity;
            obj->blendMode = matchedPm->blendMode;
            obj->opacity = matchedPm->opacity;
            obj->opacityTexture = matchedPm->opacityTexture;
        }
    };

    // 5. If spawn requested, add imported mesh to scene and apply materials
    if (spawnInLevel) {
        GameObject& spawned = scene.AddImportedMesh(currentModelImport.sourceFilePath, {0.0f, 0.5f, 0.0f});
        scene.selectedId = spawned.id;

        if (!spawned.childIds.empty()) {
            for (int childId : spawned.childIds) {
                GameObject* child = scene.FindObject(childId);
                if (child) {
                    applyMatToObj(child, child->submeshIndex);
                }
            }
        } else {
            applyMatToObj(&spawned, 0);
        }
    }

    // Also update any existing actors in the scene that reference this model file
    for (auto& obj : scene.objects) {
        if (obj.meshFilePath == currentModelImport.sourceFilePath) {
            applyMatToObj(&obj, obj.submeshIndex);
        }
    }

    UpdateLoadingTask(0.95f, "Synchronizing asset registry & content browser...", currentModelImport.modelName);
    SyncRegistryWithUIProgress(contentRootPath);

    FinishLoadingTask("Successfully imported " + currentModelImport.modelName + " (" + std::to_string(matsCreated) + " materials created & assigned)", true);
    AddLog("LogImport", "Imported 3D Model: " + currentModelImport.modelName + " (" + std::to_string(matsCreated) + " material(s) created & applied)", 2);

    if (advanceQueue) {
        AdvanceModelImportQueue();
    }
}

void EngineUI::RenderModelImportModal(Scene& scene, OrbitCamera& camera) {
    if (!showModelImportModal) return;

    ImGui::SetNextWindowSize(ImVec2(1060, 680), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    std::string title = "3D Model Import Wizard ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â " + currentModelImport.modelName;
    if (pendingModelImportQueue.size() > 1) {
        title += " (1 of " + std::to_string(pendingModelImportQueue.size()) + " queued)";
    }
    title += "###ModelImportWizardModal";

    bool open = true;
    if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse)) {
        float bottomBarHeight = 48.0f;
        float contentHeight = ImGui::GetContentRegionAvail().y - bottomBarHeight;
        float leftPaneWidth = 510.0f;
        float rightPaneWidth = ImGui::GetContentRegionAvail().x - leftPaneWidth - 12.0f;

        // ====================================================================
        // LEFT PANE: 3D MODEL INTERACTIVE PREVIEW
        // ====================================================================
        ImGui::BeginChild("ModelPreviewPane", ImVec2(leftPaneWidth, contentHeight), true);

        ImGui::TextColored(ImVec4(0.2f, 0.85f, 1.0f, 1.0f), "3D MODEL INTERACTIVE PREVIEW");
        ImGui::Spacing();

        // Toolbar controls
        const char* viewModes[] = { "Shaded (Lit)", "Shaded + Wire", "Wireframe Only" };
        ImGui::SetNextItemWidth(140.0f);
        ImGui::Combo("##ViewMode", &currentModelImport.viewMode, viewModes, IM_ARRAYSIZE(viewModes));
        ImGui::SameLine();
        ImGui::Checkbox("Turntable", &currentModelImport.autoRotate);
        ImGui::SameLine();
        ImGui::Checkbox("Grid", &currentModelImport.showGrid);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset View")) {
            currentModelImport.previewYaw = 35.0f;
            currentModelImport.previewPitch = 20.0f;
            currentModelImport.previewZoom = 1.0f;
            currentModelImport.previewPan = glm::vec2(0.0f);
        }

        // Viewport canvas
        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        if (canvasSize.y > contentHeight - 110.0f) canvasSize.y = contentHeight - 110.0f;
        if (canvasSize.y < 220.0f) canvasSize.y = 220.0f;

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImGui::InvisibleButton("##ModelCanvasBtn", canvasSize);
        bool isHovered = ImGui::IsItemHovered();
        bool isActive = ImGui::IsItemActive();

        if (isActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            currentModelImport.previewYaw += delta.x * 0.5f;
            currentModelImport.previewPitch = glm::clamp(currentModelImport.previewPitch + delta.y * 0.5f, -85.0f, 85.0f);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }
        if (isActive && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
            currentModelImport.previewPan.x += delta.x * 0.003f * currentModelImport.previewZoom;
            currentModelImport.previewPan.y -= delta.y * 0.003f * currentModelImport.previewZoom;
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
        }
        if (isHovered) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                currentModelImport.previewZoom = glm::clamp(currentModelImport.previewZoom * (1.0f - wheel * 0.12f), 0.1f, 15.0f);
            }
        }
        if (currentModelImport.autoRotate && !isActive) {
            currentModelImport.previewYaw += currentModelImport.autoRotateSpeed * ImGui::GetIO().DeltaTime;
            if (currentModelImport.previewYaw > 360.0f) currentModelImport.previewYaw -= 360.0f;
        }

        // Canvas background
        drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(16, 18, 22, 255), 6.0f);
        drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(45, 52, 65, 255), 6.0f);

        drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

        float aspect = canvasSize.x / (canvasSize.y > 0.0f ? canvasSize.y : 1.0f);
        glm::mat4 projMat = glm::perspective(glm::radians(45.0f), aspect, 0.05f, 100.0f);

        float camDist = 2.8f * currentModelImport.previewZoom;
        glm::mat4 viewMat = glm::translate(glm::mat4(1.0f), glm::vec3(currentModelImport.previewPan.x, -currentModelImport.previewPan.y, -camDist));
        viewMat = glm::rotate(viewMat, glm::radians(currentModelImport.previewPitch), glm::vec3(1.0f, 0.0f, 0.0f));
        viewMat = glm::rotate(viewMat, glm::radians(currentModelImport.previewYaw), glm::vec3(0.0f, 1.0f, 0.0f));

        float scaleFactor = 1.8f / glm::max(currentModelImport.boundsExtent, 0.001f);
        glm::mat4 modelMat = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
        modelMat = glm::translate(modelMat, -currentModelImport.boundsCenter);

        glm::mat4 mvp = projMat * viewMat * modelMat;
        glm::mat3 normMat = glm::mat3(viewMat * modelMat);

        // Ground grid
        if (currentModelImport.showGrid) {
            float groundY = (currentModelImport.boundsMin.y - currentModelImport.boundsCenter.y) * scaleFactor;
            float gridSize = 1.4f;
            float step = 0.28f;
            glm::mat4 gridMVP = projMat * viewMat;

            auto projectPoint = [&](float gx, float gy, float gz, ImVec2& outScreen, bool& outVisible) {
                glm::vec4 clip = gridMVP * glm::vec4(gx, gy, gz, 1.0f);
                if (clip.w <= 0.05f) { outVisible = false; return; }
                float invW = 1.0f / clip.w;
                outScreen.x = canvasPos.x + (clip.x * invW * 0.5f + 0.5f) * canvasSize.x;
                outScreen.y = canvasPos.y + (0.5f - clip.y * invW * 0.5f) * canvasSize.y;
                outVisible = true;
            };

            for (float g = -gridSize; g <= gridSize + 0.001f; g += step) {
                ImVec2 p0, p1, p2, p3;
                bool v0, v1, v2, v3;
                projectPoint(g, groundY, -gridSize, p0, v0);
                projectPoint(g, groundY, gridSize, p1, v1);
                if (v0 && v1) {
                    ImU32 lineCol = (std::abs(g) < 0.01f) ? IM_COL32(80, 140, 200, 160) : IM_COL32(40, 48, 62, 120);
                    drawList->AddLine(p0, p1, lineCol, (std::abs(g) < 0.01f) ? 1.5f : 1.0f);
                }
                projectPoint(-gridSize, groundY, g, p2, v2);
                projectPoint(gridSize, groundY, g, p3, v3);
                if (v2 && v3) {
                    ImU32 lineCol = (std::abs(g) < 0.01f) ? IM_COL32(200, 80, 80, 160) : IM_COL32(40, 48, 62, 120);
                    drawList->AddLine(p2, p3, lineCol, (std::abs(g) < 0.01f) ? 1.5f : 1.0f);
                }
            }
        }

        // Draw model triangles
        glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, 0.8f, 1.0f));
        uint32_t totalTris = currentModelImport.totalTriangles;
        uint32_t maxTris = 4500;
        uint32_t triStride = 1;
        if (totalTris > maxTris) {
            triStride = (totalTris / maxTris) + 1;
        }

        int subIdx = 0;
        for (const auto& mesh : currentModelImport.previewModel.meshes) {
            if (!mesh.valid || mesh.vertices.empty() || mesh.indices.empty()) {
                subIdx++;
                continue;
            }

            glm::vec3 baseCol(0.8f, 0.8f, 0.8f);
            float roughness = 0.5f;
            for (const auto& pm : currentModelImport.materials) {
                for (int s : pm.submeshIndices) {
                    if (s == subIdx) {
                        baseCol = pm.baseColor;
                        roughness = pm.roughness;
                        break;
                    }
                }
            }

            const auto& verts = mesh.vertices;
            const auto& indices = mesh.indices;
            size_t numTris = indices.size() / 3;

            for (size_t t = 0; t < numTris; t += triStride) {
                uint32_t i0 = indices[t * 3 + 0];
                uint32_t i1 = indices[t * 3 + 1];
                uint32_t i2 = indices[t * 3 + 2];
                if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size()) continue;

                const auto& v0 = verts[i0];
                const auto& v1 = verts[i1];
                const auto& v2 = verts[i2];

                glm::vec4 c0 = mvp * glm::vec4(v0.pos, 1.0f);
                glm::vec4 c1 = mvp * glm::vec4(v1.pos, 1.0f);
                glm::vec4 c2 = mvp * glm::vec4(v2.pos, 1.0f);

                if (c0.w <= 0.05f || c1.w <= 0.05f || c2.w <= 0.05f) continue;

                ImVec2 s0(canvasPos.x + (c0.x / c0.w * 0.5f + 0.5f) * canvasSize.x,
                          canvasPos.y + (0.5f - c0.y / c0.w * 0.5f) * canvasSize.y);
                ImVec2 s1(canvasPos.x + (c1.x / c1.w * 0.5f + 0.5f) * canvasSize.x,
                          canvasPos.y + (0.5f - c1.y / c1.w * 0.5f) * canvasSize.y);
                ImVec2 s2(canvasPos.x + (c2.x / c2.w * 0.5f + 0.5f) * canvasSize.x,
                          canvasPos.y + (0.5f - c2.y / c2.w * 0.5f) * canvasSize.y);

                float det = (s1.x - s0.x) * (s2.y - s0.y) - (s1.y - s0.y) * (s2.x - s0.x);
                if (det <= 0.0f) continue;

                if (currentModelImport.viewMode != 2) {
                    glm::vec3 nAvg = (v0.normal + v1.normal + v2.normal) * 0.3333f;
                    if (glm::length(nAvg) < 0.001f) {
                        nAvg = glm::normalize(glm::cross(v1.pos - v0.pos, v2.pos - v0.pos));
                    } else {
                        nAvg = glm::normalize(nAvg);
                    }
                    glm::vec3 nView = glm::normalize(normMat * nAvg);
                    float NdotL = glm::clamp(glm::dot(nView, lightDir), 0.0f, 1.0f);
                    float diff = 0.30f + 0.70f * NdotL;

                    glm::vec3 viewDir(0.0f, 0.0f, 1.0f);
                    glm::vec3 h = glm::normalize(lightDir + viewDir);
                    float NdotH = glm::clamp(glm::dot(nView, h), 0.0f, 1.0f);
                    float spec = powf(NdotH, 16.0f) * (1.0f - roughness) * 0.35f;

                    glm::vec3 finalRgb = glm::clamp(baseCol * diff + glm::vec3(spec), 0.0f, 1.0f);
                    ImU32 triCol = IM_COL32((int)(finalRgb.r * 255.0f), (int)(finalRgb.g * 255.0f), (int)(finalRgb.b * 255.0f), 255);
                    drawList->AddTriangleFilled(s0, s1, s2, triCol);

                    if (currentModelImport.viewMode == 1) {
                        drawList->AddTriangle(s0, s1, s2, IM_COL32(30, 30, 35, 90), 1.0f);
                    }
                } else {
                    drawList->AddTriangle(s0, s1, s2, IM_COL32(75, 185, 255, 200), 1.0f);
                }
            }
            subIdx++;
        }

        drawList->PopClipRect();

        // Info & stats badges
        ImGui::Spacing();
        std::filesystem::path p(currentModelImport.sourceFilePath);
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
        glm::vec3 d = currentModelImport.boundsMax - currentModelImport.boundsMin;

        ImGui::TextColored(ImVec4(0.2f, 0.85f, 0.4f, 1.0f), "[%s]", ext.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "Tris: %u | Verts: %u | Submeshes: %u",
            currentModelImport.totalTriangles, currentModelImport.totalVertices, currentModelImport.meshCount);
        ImGui::TextDisabled("Dimensions: %.2fm x %.2fm x %.2fm", d.x, d.y, d.z);
        ImGui::TextDisabled("Controls: [L-Drag] Orbit | [R-Drag] Pan | [Wheel] Zoom");

        ImGui::EndChild();

        ImGui::SameLine();

        // ====================================================================
        // RIGHT PANE: DETECTED MATERIALS & TEXTURES
        // ====================================================================
        ImGui::BeginChild("ModelMaterialsPane", ImVec2(rightPaneWidth, contentHeight), true);

        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.6f, 1.0f), "DETECTED MATERIALS & TEXTURES (%zu)", currentModelImport.materials.size());
        ImGui::TextDisabled("PBR materials will be created (.emat) and assigned to model submeshes.");
        ImGui::Separator();

        std::vector<TextureAssetEntry> projectTextures = ScanProjectTextures();

        for (size_t mIdx = 0; mIdx < currentModelImport.materials.size(); ++mIdx) {
            PendingModelMaterial& pm = currentModelImport.materials[mIdx];
            ImGui::PushID((int)mIdx);

            std::string headerTitle = pm.materialName + " (" + std::to_string(pm.submeshIndices.size()) + " submesh" + (pm.submeshIndices.size() > 1 ? "es" : "") + ")";
            if (ImGui::CollapsingHeader(headerTitle.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Checkbox("Generate Material Asset (.emat)", &pm.createMaterial);

                char nameBuf[128];
                strncpy_s(nameBuf, pm.materialName.c_str(), sizeof(nameBuf) - 1);
                ImGui::SetNextItemWidth(260.0f);
                if (ImGui::InputText("Material Name", nameBuf, sizeof(nameBuf))) {
                    pm.materialName = nameBuf;
                }

                ImGui::ColorEdit3("Base Color", &pm.baseColor.r);
                ImGui::SliderFloat("Metallic", &pm.metallic, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Roughness", &pm.roughness, 0.0f, 1.0f, "%.2f");
                ImGui::ColorEdit3("Emissive Color", &pm.emissiveColor.r);
                ImGui::SliderFloat("Emissive Power", &pm.emissiveIntensity, 0.0f, 20.0f, "%.1f");

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.8f, 0.85f, 0.95f, 1.0f), "Assigned Textures:");

                auto drawTextureRow = [&](const char* slotLabel, std::string& texSlot) {
                    ImGui::PushID(slotLabel);
                    ImGui::Columns(3, nullptr, false);
                    ImGui::SetColumnWidth(0, 100.0f);
                    ImGui::SetColumnWidth(1, 230.0f);

                    ImGui::TextUnformatted(slotLabel);
                    ImGui::NextColumn();

                    if (!texSlot.empty()) {
                        std::string fname = std::filesystem::path(texSlot).filename().string();
                        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "[Found]");
                        ImGui::SameLine();
                        ImGui::TextUnformatted(fname.c_str());
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", texSlot.c_str());
                        }
                    } else {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[None]");
                    }
                    ImGui::NextColumn();

                    if (ImGui::SmallButton("...")) {
                        ImGui::OpenPopup("PickTexturePopup");
                    }
                    if (!texSlot.empty()) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("X")) {
                            texSlot.clear();
                        }
                    }

                    if (ImGui::BeginPopup("PickTexturePopup")) {
                        ImGui::Text("Select Texture:");
                        ImGui::Separator();
                        if (ImGui::Selectable("(None)", texSlot.empty())) {
                            texSlot.clear();
                        }
                        for (const auto& te : projectTextures) {
                            bool isCur = (texSlot == te.fullPath || texSlot == te.filename);
                            if (ImGui::Selectable(te.filename.c_str(), isCur)) {
                                texSlot = te.fullPath;
                            }
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::Columns(1);
                    ImGui::PopID();
                };

                drawTextureRow("Base Color", pm.baseColorTexture);
                drawTextureRow("Normal Map", pm.normalTexture);
                drawTextureRow("Roughness", pm.roughnessTexture);
                drawTextureRow("Metallic", pm.metallicTexture);
                drawTextureRow("AO Map", pm.aoTexture);
                drawTextureRow("Emissive", pm.emissionTexture);
                drawTextureRow("Opacity", pm.opacityTexture);
            }

            ImGui::PopID();
            ImGui::Separator();
        }

        // Import Settings
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "IMPORT DESTINATION & SETTINGS");

        char folderBuf[260];
        strncpy_s(folderBuf, currentModelImport.targetFolder.c_str(), sizeof(folderBuf) - 1);
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::InputText("Materials Destination", folderBuf, sizeof(folderBuf))) {
            currentModelImport.targetFolder = folderBuf;
        }

        ImGui::Checkbox("Spawn Model in Viewport / Active Scene", &currentModelImport.spawnInLevel);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("If checked, places the imported model directly into the active scene with the generated materials applied.");
        }

        ImGui::EndChild();

        // ====================================================================
        // BOTTOM ACTIONS BAR
        // ====================================================================
        ImGui::Separator();

        if (pendingModelImportQueue.size() > 1) {
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Model 1 of %zu in queue: %s", 
                pendingModelImportQueue.size(), currentModelImport.modelName.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Model: %s", currentModelImport.modelName.c_str());
        }

        ImGui::SameLine(ImGui::GetWindowWidth() - 480.0f);

        // Import button (green)
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.55f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.70f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.45f, 0.22f, 1.0f));
        if (ImGui::Button("Import Model", ImVec2(130, 32))) {
            ExecuteModelImport(scene, currentModelImport.spawnInLevel, true);
        }
        ImGui::PopStyleColor(3);

        if (pendingModelImportQueue.size() > 1) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.40f, 0.75f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.50f, 0.90f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.32f, 0.65f, 1.0f));
            if (ImGui::Button("ÃƒÂ¢Ã…Â¡Ã‚Â¡ Import All", ImVec2(110, 32))) {
                ExecuteModelImport(scene, currentModelImport.spawnInLevel, false);
                while (pendingModelImportQueue.size() > 1) {
                    pendingModelImportQueue.erase(pendingModelImportQueue.begin());
                    OpenModelImportView(pendingModelImportQueue.front());
                    ExecuteModelImport(scene, currentModelImport.spawnInLevel, false);
                }
                pendingModelImportQueue.clear();
                showModelImportModal = false;
            }
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine();
        if (ImGui::Button("ÃƒÂ¢Ã‚ÂÃ‚Â­ Skip", ImVec2(80, 32))) {
            AdvanceModelImportQueue();
        }

        ImGui::SameLine();
        if (ImGui::Button("ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¢ Cancel All", ImVec2(100, 32))) {
            pendingModelImportQueue.clear();
            showModelImportModal = false;
        }
    }
    if (!open) {
        pendingModelImportQueue.clear();
        showModelImportModal = false;
    }
    ImGui::End();
}

// ============================================================================
// In-Editor Code Editor (dev.md & User Request: double click behaviour script)
// ============================================================================

void EngineUI::OpenScriptInCodeEditor(const std::string& path) {
    showCodeEditor = false;
    LaunchVSCodeWorkspace(path);
    AddLog("LogContent", "Opened Behaviour script in VS Code: " + std::filesystem::path(path).filename().string(), 0);
}

void EngineUI::RenderCodeEditor() {
    if (!showCodeEditor) return;

    std::string title = "Code Editor ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â " + activeCodeEditorFilename + (codeEditorDirty ? "*" : "") + "###CodeEditorWin";
    ImGui::SetNextWindowSize(ImVec2(740, 540), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(title.c_str(), &showCodeEditor, ImGuiWindowFlags_MenuBar)) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::MenuItem("Save File", "Ctrl+S")) {
                std::ofstream file(activeCodeEditorPath);
                if (file.is_open()) {
                    file << activeCodeEditorContent;
                    codeEditorDirty = false;
                    AddLog("LogContent", "Saved file: " + activeCodeEditorFilename, 2);
                }
            }
            if (ImGui::MenuItem("Open in External IDE (VS Code)")) {
                LaunchVSCodeWorkspace(activeCodeEditorPath);
                AddLog("LogContent", "Launched VS Code workspace for: " + activeCodeEditorFilename, 0);
            }
            if (ImGui::MenuItem("ÃƒÂ¢Ã…Â¡Ã‚Â¡ Compile Script", "F7", false, !isCompilingScripts)) {
                TriggerCompileScripts(nullptr);
            }
            ImGui::EndMenuBar();
        }

        // Action Toolbar
        if (ImGui::Button("Save File")) {
            std::ofstream file(activeCodeEditorPath);
            if (file.is_open()) {
                file << activeCodeEditorContent;
                codeEditorDirty = false;
                AddLog("LogContent", "Saved file: " + activeCodeEditorFilename, 2);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Open in External IDE (VS Code)")) {
            LaunchVSCodeWorkspace(activeCodeEditorPath);
            AddLog("LogContent", "Launched VS Code workspace for: " + activeCodeEditorFilename, 0);
        }
        ImGui::SameLine();
        if (isCompilingScripts) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "ÃƒÂ¢Ã‚ÂÃ‚Â³ Compiling...");
        } else {
            if (ImGui::Button("ÃƒÂ¢Ã…Â¡Ã‚Â¡ Compile")) {
                TriggerCompileScripts(nullptr);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Compile this behaviour script and rebuild standalone game");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("| %s", activeCodeEditorPath.c_str());

        ImGui::Separator();

        // Multiline text buffer
        static std::vector<char> buffer;
        if (buffer.size() < activeCodeEditorContent.size() + 65536) {
            buffer.resize(activeCodeEditorContent.size() + 65536);
            memcpy(buffer.data(), activeCodeEditorContent.c_str(), activeCodeEditorContent.size() + 1);
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (ImGui::InputTextMultiline("##CodeEditorText", buffer.data(), buffer.size(), avail,
            ImGuiInputTextFlags_AllowTabInput)) {
            activeCodeEditorContent = buffer.data();
            codeEditorDirty = true;
        }

        // Ctrl+S hotkey
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
            std::ofstream file(activeCodeEditorPath);
            if (file.is_open()) {
                file << activeCodeEditorContent;
                codeEditorDirty = false;
                AddLog("LogContent", "Saved file: " + activeCodeEditorFilename, 2);
            }
        }
    }
    ImGui::End();
}

// ============================================================================
// Details Panel Behaviours Section (dev.md Section 6, 7, 8, 9, 10, 11, 12, 13)
// ============================================================================

void EngineUI::RenderBehavioursSection(Scene& scene, GameObject* obj) {
    if (!obj) return;

    ImGui::Spacing();
    ImGui::Separator();
    char headerLabel[128];
    snprintf(headerLabel, sizeof(headerLabel), "Behaviour Scripts (%zu)###BehavioursHeader", obj->behaviours.size());
    if (ImGui::CollapsingHeader(headerLabel, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();

        // Button to add behaviour script
        if (ImGui::Button("+ Add Behaviour Script", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
            showAddBehaviourPopup = true;
            behaviourSearchBuf[0] = '\0';
        }

        if (!obj->behaviours.empty()) {
            bool isPlayActive = scene.isPlayMode;
            if (!isPlayActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.55f, 0.25f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.70f, 0.32f, 1.0f));
                if (ImGui::Button("ÃƒÂ¢Ã¢â‚¬â€œÃ‚Â¶ Play Mode (Full Screen Fit)", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                    EnterPlayMode(scene, currentCamera);
                }
                ImGui::PopStyleColor(2);
                ImGui::TextDisabled("Runs behaviour scripts in full screen play mode.");
                ImGui::Spacing();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.20f, 0.20f, 1.0f));
                if (ImGui::Button("ÃƒÂ¢Ã‚ÂÃ‚Â¹ Stop Play Mode", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                    ExitPlayMode(scene, currentCamera);
                }
                ImGui::PopStyleColor();
                ImGui::TextColored(ImVec4(0.2f, 0.95f, 0.4f, 1.0f), "ÃƒÂ¢Ã¢â‚¬â€œÃ‚Â¶ Simulation Active");
                ImGui::Spacing();
            }
        }

        if (showAddBehaviourPopup) {
            ImGui::OpenPopup("Add Behaviour Script##Modal");
            showAddBehaviourPopup = false;
        }

        if (ImGui::BeginPopupModal("Add Behaviour Script##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(ImVec4(0.12f, 0.68f, 1.00f, 1.0f), "Select Behaviour Script to Attach:");
            ImGui::SetNextItemWidth(300.0f);
            ImGui::InputTextWithHint("##BehSearch", "Search Behaviour Scripts...", behaviourSearchBuf, sizeof(behaviourSearchBuf));
            ImGui::Separator();

            std::string searchLower = behaviourSearchBuf;
            std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

            // Dynamically scan content folder for user-created .cpp script files so they show in the list
            std::error_code sEc;
            if (std::filesystem::exists(contentRootPath, sEc)) {
                for (auto it = std::filesystem::recursive_directory_iterator(contentRootPath, std::filesystem::directory_options::skip_permission_denied, sEc);
                     !sEc && it != std::filesystem::recursive_directory_iterator();
                     it.increment(sEc)) {
                    if (!it->is_directory(sEc)) {
                        if (it->path().extension() == ".cpp") {
                            std::string pStr = it->path().string();
                            std::string pLower = pStr;
                            std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::tolower);
                            if (pLower.find(".assetmeta") == std::string::npos && pLower.find(".meta") == std::string::npos && pLower.find(".internal") == std::string::npos) {
                                std::string stem = it->path().stem().string();
                                if (stem != "Cube" && stem != "EngineUI" && stem != "EunoiaBehaviour" && stem != "TextureManager" && stem != "AssetRegistry" && stem != "AssetManager") {
                                    BehaviourRegistry::Get().RegisterScriptFile(stem, it->path().string());
                                }
                            }
                        }
                    }
                }
            }

            std::error_code cNormEc;
            std::filesystem::path curContentNorm = std::filesystem::weakly_canonical(contentRootPath, cNormEc);
            std::string curContentStr = curContentNorm.generic_string();

            const auto& allBehaviors = BehaviourRegistry::Get().GetAll();
            int matchCount = 0;
            for (const auto& pair : allBehaviors) {
                const auto& info = pair.second;

                // Only show behaviours belonging to this project (or native engine behaviours)
                if (!info.isNative && !info.sourceCpp.empty()) {
                    std::error_code cEc;
                    std::filesystem::path srcNorm = std::filesystem::weakly_canonical(info.sourceCpp, cEc);
                    std::string srcStr = srcNorm.generic_string();
                    if (!curContentStr.empty() && srcStr.rfind(curContentStr, 0) != 0) {
                        continue; // Belongs to a different project! Skip it!
                    }
                    if (srcStr.find(".internal") != std::string::npos) {
                        continue; // Hidden internal Blueprint script! Skip it!
                    }
                }

                std::string nameLower = info.displayName;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                std::string classLower = info.className;
                std::transform(classLower.begin(), classLower.end(), classLower.begin(), ::tolower);

                if (!searchLower.empty() && nameLower.find(searchLower) == std::string::npos && classLower.find(searchLower) == std::string::npos) {
                    continue;
                }

                matchCount++;
                std::string itemText = info.displayName;
                if (ImGui::Selectable(itemText.c_str(), false)) {
                    UndoManager::Get().RecordSnapshot(scene, "Add Behaviour: " + info.className);
                    auto newB = BehaviourRegistry::Get().Create(info.className);
                    if (newB) {
                        newB->SetScene(&scene);
                        obj->AddBehaviour(std::move(newB));
                        obj->behaviours.back()->ResolveReferences(scene);
                        AddLog("LogBehaviour", "Attached " + info.displayName + " to " + obj->name, 2);
                    }
                    ImGui::CloseCurrentPopup();
                }
                if (!info.description.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", info.description.c_str());
                }
            }

            if (matchCount == 0) {
                ImGui::TextDisabled("No behaviours match search query.");
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.0f, 0.82f, 1.0f, 1.0f), "Node Behaviours (.enb):");
            std::error_code nbEc;
            if (std::filesystem::exists(contentRootPath, nbEc)) {
                for (auto it = std::filesystem::recursive_directory_iterator(contentRootPath, std::filesystem::directory_options::skip_permission_denied, nbEc);
                     !nbEc && it != std::filesystem::recursive_directory_iterator();
                     it.increment(nbEc)) {
                    if (!it->is_directory(nbEc) && it->path().extension() == ".enb") {
                        std::string stem = it->path().stem().string();
                        std::string stemLower = stem;
                        std::transform(stemLower.begin(), stemLower.end(), stemLower.begin(), ::tolower);
                        if (!searchLower.empty() && stemLower.find(searchLower) == std::string::npos) continue;

                        std::string label = "[NB] " + stem;
                        if (ImGui::Selectable(label.c_str(), false)) {
                            UndoManager::Get().RecordSnapshot(scene, "Add Node Behaviour: " + stem);
                            auto nbInstance = std::make_unique<Eunoia::NodeBehaviour::NodeBehaviourInstance>(it->path().string());
                            nbInstance->SetScene(&scene);
                            obj->AddBehaviour(std::move(nbInstance));
                            obj->behaviours.back()->ResolveReferences(scene);
                            AddLog("LogBehaviour", "Attached Node Behaviour " + stem + " to " + obj->name, 2);
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Create New Node Behaviour (.enb)...", ImVec2(240, 0))) {
                showNewNodeBehaviourPopup = true;
                snprintf(newNodeBehaviourNameBuf, sizeof(newNodeBehaviourNameBuf), "NB_NewBehaviour");
                popupTargetVirtualDir = currentVirtualDir;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Create New Behaviour Script...", ImVec2(240, 0))) {
                showNewBehaviourPopup = true;
                std::string defName = "NewBehaviour";
                int idx = 1;
                while (IsBehaviourNameAlreadyExists("NewBehaviour_" + std::to_string(idx))) idx++;
                defName = "NewBehaviour_" + std::to_string(idx);
                snprintf(newBehaviourNameBuf, sizeof(newBehaviourNameBuf), "%s", defName.c_str());
                popupTargetVirtualDir = currentVirtualDir;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Render each attached behaviour
        int removeIdx = -1;
        int moveUpIdx = -1;
        int moveDownIdx = -1;

        for (size_t i = 0; i < obj->behaviours.size(); ++i) {
            auto& b = obj->behaviours[i];
            if (!b) continue;

            // Ensure user script behaviours are upgraded to dynamic script reflection with latest properties from .cpp
            const auto& allRegistry = BehaviourRegistry::Get().GetAll();
            auto regIt = allRegistry.find(b->GetClassName());
            if (regIt != allRegistry.end() && !regIt->second.sourceCpp.empty()) {
                if (dynamic_cast<DynamicScriptBehaviour*>(b.get()) == nullptr || b->GetProperties().empty()) {
                    auto dynB = std::make_unique<DynamicScriptBehaviour>(b->GetClassName(), regIt->second.sourceCpp);
                    dynB->SetScene(&scene);
                    dynB->SetOwner(obj);
                    dynB->SetEnabled(b->IsEnabled());
                    dynB->CopyPropertiesFrom(*b);
                    dynB->ResolveReferences(scene);
                    b = std::move(dynB);
                } else {
                    b->SetScene(&scene);
                    b->RefreshPropertiesFromSource();
                    b->ResolveReferences(scene);
                }
            } else {
                b->SetScene(&scene);
                b->RefreshPropertiesFromSource();
                b->ResolveReferences(scene);
            }

            ImGui::PushID((int)i);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
            ImGui::BeginChild("BehaviourCard", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

            // Header line: Reorder, Class Title, Enable checkbox, Reset & Remove buttons
            if (i > 0) {
                if (ImGui::SmallButton("ÃƒÂ¢Ã¢â‚¬â€œÃ‚Â²")) moveUpIdx = (int)i;
                ImGui::SameLine();
            }
            if (i + 1 < obj->behaviours.size()) {
                if (ImGui::SmallButton("ÃƒÂ¢Ã¢â‚¬â€œÃ‚Â¼")) moveDownIdx = (int)i;
                ImGui::SameLine();
            }

            bool isEnabled = b->IsEnabled();
            if (ImGui::Checkbox("##Enabled", &isEnabled)) {
                UndoManager::Get().RecordSnapshot(scene, "Toggle Behaviour Enabled");
                b->SetEnabled(isEnabled);
            }
            ImGui::SameLine();

            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", b->GetDisplayName().c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", b->GetClassName().c_str());

            if (regIt != allRegistry.end() && !regIt->second.sourceCpp.empty()) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.75f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.85f, 1.0f));
                if (ImGui::SmallButton("Edit in VS Code")) {
                    OpenScriptInCodeEditor(regIt->second.sourceCpp);
                }
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Open script in VS Code: %s", regIt->second.sourceCpp.c_str());
                }
            }

            auto nbInst = dynamic_cast<Eunoia::NodeBehaviour::NodeBehaviourInstance*>(b.get());
            if (nbInst) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.45f, 0.75f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.55f, 0.90f, 1.0f));
                if (ImGui::SmallButton("Edit Graph")) {
                    OpenNodeBehaviourEditor(nbInst->GetAssetPath());
                }
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Open Node Behaviour visual scripting graph: %s", nbInst->GetAssetPath().c_str());
                }
            }

            // Right-aligned Reset and Remove buttons
            float availW = ImGui::GetContentRegionAvail().x;
            if (availW > 140.0f) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
                if (ImGui::SmallButton("Reset")) {
                    UndoManager::Get().RecordSnapshot(scene, "Reset Behaviour to Defaults");
                    b->SetScene(&scene);
                    b->ResetToDefaults();
                    b->ResolveReferences(scene);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Reset all properties and references to default values as defined in code.");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    removeIdx = (int)i;
                }
            } else if (availW > 70.0f) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 75.0f);
                if (ImGui::SmallButton("Remove")) {
                    removeIdx = (int)i;
                }
            }

            ImGui::Separator();

            // Properties list
            auto& props = b->GetProperties();
            if (props.empty()) {
                ImGui::TextDisabled("  (No exposed properties)");
            }
            for (size_t pi = 0; pi < props.size(); ++pi) {
                auto& prop = props[pi];
                ImGui::PushID((int)pi);

                switch (prop.type) {
                    case BehaviourPropertyType::Bool: {
                        if (prop.dataPtr) {
                            bool* val = reinterpret_cast<bool*>(prop.dataPtr);
                            ImGui::Checkbox(prop.displayName.c_str(), val);
                        }
                        break;
                    }
                    case BehaviourPropertyType::Int: {
                        if (prop.dataPtr) {
                            int* val = reinterpret_cast<int*>(prop.dataPtr);
                            if (prop.hasRange) {
                                ImGui::SliderInt(prop.displayName.c_str(), val, (int)prop.minVal, (int)prop.maxVal);
                            } else {
                                ImGui::DragInt(prop.displayName.c_str(), val);
                            }
                        }
                        break;
                    }
                    case BehaviourPropertyType::Float: {
                        if (prop.dataPtr) {
                            float* val = reinterpret_cast<float*>(prop.dataPtr);
                            if (prop.hasRange) {
                                ImGui::SliderFloat(prop.displayName.c_str(), val, prop.minVal, prop.maxVal);
                            } else {
                                ImGui::DragFloat(prop.displayName.c_str(), val, 0.1f);
                            }
                        }
                        break;
                    }
                    case BehaviourPropertyType::Double: {
                        if (prop.dataPtr) {
                            double* val = reinterpret_cast<double*>(prop.dataPtr);
                            float tempF = (float)*val;
                            if (ImGui::DragFloat(prop.displayName.c_str(), &tempF, 0.1f)) {
                                *val = (double)tempF;
                            }
                        }
                        break;
                    }
                    case BehaviourPropertyType::String: {
                        if (prop.dataPtr) {
                            std::string* val = reinterpret_cast<std::string*>(prop.dataPtr);
                            char strBuf[256] = {};
                            strncpy(strBuf, val->c_str(), sizeof(strBuf) - 1);
                            if (ImGui::InputText(prop.displayName.c_str(), strBuf, sizeof(strBuf))) {
                                *val = strBuf;
                            }
                        }
                        break;
                    }
                    case BehaviourPropertyType::Vec2: {
                        if (prop.dataPtr) {
                            glm::vec2* val = reinterpret_cast<glm::vec2*>(prop.dataPtr);
                            ImGui::DragFloat2(prop.displayName.c_str(), &val->x, 0.1f);
                        }
                        break;
                    }
                    case BehaviourPropertyType::Vec3: {
                        if (prop.dataPtr) {
                            glm::vec3* val = reinterpret_cast<glm::vec3*>(prop.dataPtr);
                            ImGui::DragFloat3(prop.displayName.c_str(), &val->x, 0.1f);
                        }
                        break;
                    }
                    case BehaviourPropertyType::Vec4: {
                        if (prop.dataPtr) {
                            glm::vec4* val = reinterpret_cast<glm::vec4*>(prop.dataPtr);
                            ImGui::DragFloat4(prop.displayName.c_str(), &val->x, 0.1f);
                        }
                        break;
                    }
                    case BehaviourPropertyType::Color3: {
                        if (prop.dataPtr) {
                            glm::vec3* val = reinterpret_cast<glm::vec3*>(prop.dataPtr);
                            ImGui::ColorEdit3(prop.displayName.c_str(), &val->x);
                        }
                        break;
                    }
                    case BehaviourPropertyType::ObjectRef: {
                        if (prop.refType == ObjectRefType::UserInterface) {
                            ImGui::Text("%s", prop.displayName.c_str());

                            std::string preview = "None (Select User Interface)";
                            if (!prop.assetPath.empty()) {
                                std::filesystem::path p(prop.assetPath);
                                preview = p.stem().string();
                            }

                            if (prop.isMissing) {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
                            }

                            std::string comboId = "##UIAssetCombo_" + std::to_string(obj->id) + "_" + prop.name;
                            if (ImGui::BeginCombo(comboId.c_str(), preview.c_str())) {
                                if (ImGui::Selectable("(None)", prop.assetPath.empty())) {
                                    UndoManager::Get().RecordSnapshot(scene, "Clear UI Reference: " + prop.name);
                                    prop.assetPath = "";
                                    b->ResolveReferences(scene);
                                }
                                ImGui::Separator();

                                // Scan project content directory for all .eui files
                                if (!contentRootPath.empty() && std::filesystem::exists(contentRootPath)) {
                                    std::error_code rec;
                                    for (auto it = std::filesystem::recursive_directory_iterator(contentRootPath, std::filesystem::directory_options::skip_permission_denied, rec);
                                         it != std::filesystem::recursive_directory_iterator(); it.increment(rec)) {
                                        if (rec) break;
                                        if (it->is_regular_file() && it->path().extension() == ".eui") {
                                            std::string uiPath = it->path().string();
                                            std::string uiStem = it->path().stem().string();
                                            bool isSel = (!prop.assetPath.empty() && (prop.assetPath == uiPath || std::filesystem::path(prop.assetPath).filename() == it->path().filename()));
                                            if (ImGui::Selectable(uiStem.c_str(), isSel)) {
                                                UndoManager::Get().RecordSnapshot(scene, "Assign UI: " + prop.name);
                                                prop.assetPath = uiPath;
                                                b->ResolveReferences(scene);
                                            }
                                        }
                                    }
                                }
                                ImGui::EndCombo();
                            }

                            if (prop.isMissing) {
                                ImGui::PopStyleColor();
                            }

                            // Accept Drag & Drop from Content Browser onto this field
                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                                    std::string payloadStr((const char*)payload->Data, payload->DataSize);
                                    std::stringstream pss(payloadStr);
                                    std::string dragItem;
                                    while (std::getline(pss, dragItem, '\n')) {
                                        if (dragItem.rfind("ASSET:", 0) == 0) {
                                            std::string srcRel = dragItem.substr(6);
                                            if (srcRel.find(".eui") != std::string::npos) {
                                                std::filesystem::path fullDisk = contentRootPath / srcRel;
                                                UndoManager::Get().RecordSnapshot(scene, "Drop UI Reference: " + prop.name);
                                                prop.assetPath = fullDisk.string();
                                                b->ResolveReferences(scene);
                                                AddLog("LogBehaviour", "Assigned UI " + fullDisk.stem().string() + " to " + prop.displayName, 2);
                                                break;
                                            }
                                        }
                                    }
                                } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                                    if (payload->DataSize == sizeof(AssetID)) {
                                        AssetID aid = *(const AssetID*)payload->Data;
                                        const AssetMetadata* meta = AssetRegistry::Get().Find(aid);
                                        if (meta && (meta->type == AssetType::UserInterface || meta->type == AssetType::UserInterface3D)) {
                                            std::filesystem::path fullDisk = contentRootPath / meta->sourcePath;
                                            UndoManager::Get().RecordSnapshot(scene, "Drop UI Reference: " + prop.name);
                                            prop.assetPath = fullDisk.string();
                                            b->ResolveReferences(scene);
                                            AddLog("LogBehaviour", "Assigned UI " + meta->objectName + " to " + prop.displayName, 2);
                                        }
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }

                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Select User Interface asset (.eui) or drag & drop from Content Browser onto this field.");
                            }
                            break;
                        }

                        // Object Reference with Typed Dropdown and Drag & Drop (dev.md Section 9, 10, 11, 12, 13)
                        ImGui::Text("%s", prop.displayName.c_str());

                        std::string preview = "None (Select " + std::string(ObjectRefTypeToString(prop.refType)) + ")";
                        if (prop.targetId != -1) {
                            GameObject* targetObj = scene.FindObject(prop.targetId);
                            if (targetObj) {
                                preview = targetObj->name;
                            } else {
                                preview = "Missing Reference (ID: " + std::to_string(prop.targetId) + ")";
                            }
                        }

                        if (prop.isMissing) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
                        }

                        if (ImGui::BeginCombo("##ObjRefCombo", preview.c_str())) {
                            if (ImGui::Selectable("(None)", prop.targetId == -1)) {
                                UndoManager::Get().RecordSnapshot(scene, "Clear Reference: " + prop.name);
                                prop.targetId = -1;
                                b->ResolveReferences(scene);
                            }
                            ImGui::Separator();

                            for (const auto& sceneObj : scene.objects) {
                                // Filter by requested reference type (dev.md Section 10)
                                if (prop.refType == ObjectRefType::Light && !sceneObj.isLight) continue;
                                if (prop.refType == ObjectRefType::Mesh && sceneObj.mesh.indices.empty()) continue;
                                if (prop.refType == ObjectRefType::Camera && !sceneObj.isCamera && sceneObj.type != PrimitiveType::Camera) continue;
                                if (prop.refType == ObjectRefType::AnimationGraph && !sceneObj.hasAnimator) continue;

                                bool isSelected = (prop.targetId == sceneObj.id);
                                std::string itemLabel = sceneObj.name + " (ID: " + std::to_string(sceneObj.id) + ")";
                                if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                                    UndoManager::Get().RecordSnapshot(scene, "Assign Reference: " + prop.name);
                                    prop.targetId = sceneObj.id;
                                    b->ResolveReferences(scene);
                                }
                            }
                            ImGui::EndCombo();
                        }

                        if (prop.isMissing) {
                            ImGui::PopStyleColor();
                        }

                        // Drag & Drop Target from Hierarchy (dev.md Section 12)
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OUTLINER_ACTOR")) {
                                int draggedId = *(const int*)payload->Data;
                                GameObject* draggedObj = scene.FindObject(draggedId);
                                if (draggedObj) {
                                    bool compatible = true;
                                    if (prop.refType == ObjectRefType::Light && !draggedObj->isLight) compatible = false;
                                    if (prop.refType == ObjectRefType::Mesh && draggedObj->mesh.indices.empty()) compatible = false;
                                    if (prop.refType == ObjectRefType::Camera && !draggedObj->isCamera && draggedObj->type != PrimitiveType::Camera) compatible = false;
                                    if (prop.refType == ObjectRefType::AnimationGraph && !draggedObj->hasAnimator) compatible = false;

                                    if (compatible) {
                                        UndoManager::Get().RecordSnapshot(scene, "Drop Reference: " + prop.name);
                                        prop.targetId = draggedId;
                                        b->ResolveReferences(scene);
                                        AddLog("LogBehaviour", "Assigned " + draggedObj->name + " to " + prop.displayName, 2);
                                    } else {
                                        AddLog("LogBehaviour", "Cannot assign " + draggedObj->name + " ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â incompatible type (" + ObjectRefTypeToString(prop.refType) + " expected).", 1);
                                    }
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Select %s or drag & drop actor from Outliner onto this field.", ObjectRefTypeToString(prop.refType));
                        }
                        break;
                    }
                }

                ImGui::PopID();
            }

            ImGui::Spacing();
            if (ImGui::Button("Reset to Code Defaults")) {
                UndoManager::Get().RecordSnapshot(scene, "Reset Behaviour to Defaults");
                b->SetScene(&scene);
                b->ResetToDefaults();
                b->ResolveReferences(scene);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Reset all properties and references to their initial default values as defined in code.");
            }

            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopID();
        }

        if (removeIdx >= 0) {
            UndoManager::Get().RecordSnapshot(scene, "Remove Behaviour");
            obj->RemoveBehaviour((size_t)removeIdx);
        } else if (moveUpIdx > 0) {
            UndoManager::Get().RecordSnapshot(scene, "Reorder Behaviour");
            obj->ReorderBehaviour((size_t)moveUpIdx, (size_t)(moveUpIdx - 1));
        } else if (moveDownIdx >= 0 && moveDownIdx + 1 < (int)obj->behaviours.size()) {
            UndoManager::Get().RecordSnapshot(scene, "Reorder Behaviour");
            obj->ReorderBehaviour((size_t)moveDownIdx, (size_t)(moveDownIdx + 1));
        }
    }
}

void EngineUI::RenderProjectBrowser(Scene& scene, OrbitCamera& camera) {
    if (!showProjectBrowser) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();

    // 1. Dark semi-transparent backdrop
    ImDrawList* bgDrawList = ImGui::GetForegroundDrawList();
    bgDrawList->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(8, 12, 18, 220));

    // 2. Centered Window
    float winW = 820.0f;
    float winH = 540.0f;
    ImVec2 centerPos(vp->Pos.x + (vp->Size.x - winW) * 0.5f, vp->Pos.y + (vp->Size.y - winH) * 0.5f);
    ImGui::SetNextWindowPos(centerPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 18.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.12f, 0.60f, 0.95f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.118f, 0.118f, 0.118f, 0.98f));

    bool open = true;
    bool hasActiveProject = !activeProjectRoot.empty();

    if (ImGui::Begin("Project Browser###ProjectBrowserModal", hasActiveProject ? &open : nullptr, flags)) {
        if (!open) {
            showProjectBrowser = false;
        }

        // Title Header
        ImGui::TextColored(ImVec4(0.12f, 0.68f, 1.00f, 1.0f), "EUNOIA ENGINE");
        ImGui::SameLine();
        ImGui::TextDisabled("ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â Project Browser");
        if (hasActiveProject) {
            ImGui::SameLine(winW - 140.0f);
            if (ImGui::SmallButton("ÃƒÂ¢Ã…â€œÃ¢â‚¬â€œ Return to Editor")) {
                showProjectBrowser = false;
            }
        }

        ImGui::TextWrapped("Select a previously created project, or configure and create a new empty game project.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Top Tabs: [ Recent Projects ] | [ New Project ]
        bool tab0Active = (projectBrowserTab == 0);
        if (tab0Active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.50f, 0.90f, 1.0f));
        std::string recentTabLabel = "Recent Projects (" + std::to_string(recentProjects.size()) + ")";
        if (ImGui::Button(recentTabLabel.c_str(), ImVec2(180, 32))) {
            projectBrowserTab = 0;
        }
        if (tab0Active) ImGui::PopStyleColor();

        ImGui::SameLine();
        bool tab1Active = (projectBrowserTab == 1);
        if (tab1Active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.50f, 0.90f, 1.0f));
        if (ImGui::Button("New Project", ImVec2(150, 32))) {
            projectBrowserTab = 1;
        }
        if (tab1Active) ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::Button("Browse for Project...", ImVec2(170, 32))) {
            std::string selectedFolder = ShowSelectFolderDialog(nullptr, "Select Existing Project Folder");
            if (!selectedFolder.empty()) {
                LoadProject(selectedFolder, scene, camera);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (projectBrowserTab == 0) {
            // =================================================================
            // TAB 0: RECENT PROJECTS (PREVIOUSLY CREATED PROJECTS)
            // =================================================================
            ImGui::SetNextItemWidth(winW - 220.0f);
            ImGui::InputTextWithHint("##ProjFilter", "Filter projects...", projectBrowserSearchBuf, sizeof(projectBrowserSearchBuf));
            ImGui::SameLine();
            if (ImGui::Button("Refresh List", ImVec2(140, 0))) {
                LoadRecentProjects();
            }

            ImGui::Spacing();

            // Projects List Child Window
            ImGui::BeginChild("RecentProjectsScroll", ImVec2(0, winH - 240.0f), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            if (recentProjects.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("No recent projects found.");
                ImGui::Text("Click 'New Project' above to create your first game project!");
            } else {
                for (size_t i = 0; i < recentProjects.size(); ++i) {
                    const auto& p = recentProjects[i];

                    // Filter search
                    if (strlen(projectBrowserSearchBuf) > 0) {
                        std::string searchLower = projectBrowserSearchBuf;
                        std::string nameLower = p.name;
                        for (auto& c : searchLower) c = tolower(c);
                        for (auto& c : nameLower) c = tolower(c);
                        if (nameLower.find(searchLower) == std::string::npos && p.rootPath.find(projectBrowserSearchBuf) == std::string::npos) {
                            continue;
                        }
                    }

                    ImGui::PushID((int)i);
                    bool isSelected = (selectedProjectIndex == (int)i);

                    ImVec2 curPos = ImGui::GetCursorPos();
                    ImVec2 itemSize(ImGui::GetContentRegionAvail().x, 48.0f);
                    ImVec2 screenPos = ImGui::GetCursorScreenPos();
                    ImVec2 pMin = screenPos;
                    ImVec2 pMax = ImVec2(screenPos.x + itemSize.x, screenPos.y + itemSize.y);

                    // Full-width invisible button to reliably capture clicks anywhere on the card
                    ImGui::InvisibleButton("##InEditorProjSelBtn", itemSize);
                    bool isHovered = ImGui::IsItemHovered();
                    if (ImGui::IsItemClicked()) {
                        selectedProjectIndex = (int)i;
                    }
                    if (isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        LoadProject(p.rootPath, scene, camera);
                    }

                    // Background styling
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 bgCol = isSelected ? IM_COL32(30, 95, 195, 180)
                                             : (isHovered ? IM_COL32(42, 52, 70, 160) : IM_COL32(24, 28, 38, 120));
                    ImU32 borderCol = isSelected ? IM_COL32(50, 160, 255, 230)
                                                 : (isHovered ? IM_COL32(70, 130, 220, 150) : IM_COL32(40, 45, 58, 100));
                    dl->AddRectFilled(pMin, pMax, bgCol, 4.0f);
                    dl->AddRect(pMin, pMax, borderCol, 4.0f, 0, isSelected ? 2.0f : 1.0f);

                    // Render text inside selectable card area using draw list (prevents SetCursorPos assertion)
                    dl->AddText(ImVec2(pMin.x + 12.0f, pMin.y + 6.0f), IM_COL32(50, 204, 255, 255), p.name.c_str());
                    std::string pathText = "Root: " + p.rootPath + "  |  Content: " + p.rootPath + "/Content";
                    dl->AddText(ImVec2(pMin.x + 12.0f, pMin.y + 24.0f), IM_COL32(150, 150, 160, 255), pathText.c_str());

                    if (!p.lastOpened.empty()) {
                        ImVec2 lastOpenedSize = ImGui::CalcTextSize(p.lastOpened.c_str());
                        dl->AddText(ImVec2(pMax.x - lastOpenedSize.x - 16.0f, pMin.y + 14.0f), IM_COL32(150, 150, 160, 255), p.lastOpened.c_str());
                    }

                    ImGui::PopID();
                }
            }
            ImGui::EndChild();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Bottom Actions for Tab 0
            bool hasSelection = (selectedProjectIndex >= 0 && selectedProjectIndex < (int)recentProjects.size());
            if (!hasSelection) ImGui::BeginDisabled();
            if (ImGui::Button("Open Selected Project", ImVec2(200, 34))) {
                if (hasSelection) {
                    LoadProject(recentProjects[selectedProjectIndex].rootPath, scene, camera);
                }
            }
            if (!hasSelection) ImGui::EndDisabled();

            ImGui::SameLine();
            if (!hasSelection) ImGui::BeginDisabled();
            if (ImGui::Button("Remove From List", ImVec2(140, 34))) {
                if (hasSelection) {
                    recentProjects.erase(recentProjects.begin() + selectedProjectIndex);
                    selectedProjectIndex = -1;
                    SaveRecentProjects();
                }
            }
            if (!hasSelection) ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::TextDisabled("(Double-click any project to open immediately)");

            if (hasActiveProject) {
                ImGui::SameLine(winW - 170.0f);
                if (ImGui::Button("Cancel", ImVec2(120, 34))) {
                    showProjectBrowser = false;
                }
            }
        } else {
            // =================================================================
            // TAB 1: NEW PROJECT (CREATE PROJECT SETUP)
            // =================================================================
            ImGui::TextColored(ImVec4(0.12f, 0.68f, 1.00f, 1.0f), "Create New Game Project");
            ImGui::TextDisabled("Set the project name and location. A new empty project structure with Content folder will be created.");
            ImGui::Spacing();

            // 1. Project Name
            ImGui::Text("Project Name:");
            ImGui::SetNextItemWidth(360.0f);
            ImGui::InputText("##NewProjNameInput", newProjectNameBuf, sizeof(newProjectNameBuf));

            ImGui::Spacing();

            // 2. Project Location
            ImGui::Text("Project Location (Parent Folder):");
            ImGui::SetNextItemWidth(560.0f);
            ImGui::InputText("##NewProjLocInput", newProjectPathBuf, sizeof(newProjectPathBuf));
            ImGui::SameLine();
            if (ImGui::Button("Browse Folder...", ImVec2(140, 0))) {
                std::string picked = ShowSelectFolderDialog(nullptr, "Select Folder for New Project");
                if (!picked.empty()) {
                    strncpy(newProjectPathBuf, picked.c_str(), sizeof(newProjectPathBuf) - 1);
                }
            }

            ImGui::Spacing();

            // 3. Computed Project Path & Content Structure Preview
            std::filesystem::path targetProjectDir = std::filesystem::path(newProjectPathBuf) / newProjectNameBuf;
            std::filesystem::path targetContentDir = targetProjectDir / "Content";
            std::filesystem::path targetRegistryDir = targetProjectDir / "Registry";
            std::filesystem::path targetBuildDir   = targetProjectDir / "Build";

            ImGui::BeginChild("NewProjPreview", ImVec2(0, 160), true);
            ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.25f, 1.0f), "Project Structure Summary:");
            ImGui::BulletText("Project Directory: %s", targetProjectDir.string().c_str());
            ImGui::BulletText("Content Root (Content Browser): %s", targetContentDir.string().c_str());
            ImGui::BulletText("Asset Registry Directory: %s", targetRegistryDir.string().c_str());
            ImGui::BulletText("Standalone Game Build Directory: %s", targetBuildDir.string().c_str());
            ImGui::BulletText("Starter Template: Eunoia Project Template (Default Level & Configs)");
            ImGui::EndChild();

            ImGui::Spacing();

            // Validation status
            std::error_code ec;
            bool nameValid = strlen(newProjectNameBuf) > 0;
            bool pathValid = strlen(newProjectPathBuf) > 0;
            bool targetExists = std::filesystem::exists(targetProjectDir, ec);

            if (!nameValid) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Please enter a project name.");
            } else if (!pathValid) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Please select a valid project directory.");
            } else if (targetExists) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Folder already exists on disk. Will initialize/load project.");
            } else {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦ Ready to create empty project in selected path.");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            bool canCreate = nameValid && pathValid;
            if (!canCreate) ImGui::BeginDisabled();
            if (ImGui::Button("ÃƒÂ¢Ã…â€œÃ‚Â¨ Create Project", ImVec2(180, 36))) {
                CreateNewProject(newProjectPathBuf, newProjectNameBuf, scene, camera);
            }
            if (!canCreate) ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 36))) {
                if (hasActiveProject) {
                    showProjectBrowser = false;
                } else {
                    projectBrowserTab = 0;
                }
            }
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}




// ============================================================================
// Standalone Game Build Pipeline Implementation
// ============================================================================

void EngineUI::OpenGameBuildModal(Scene& scene) {
    showGameBuildModal = true;
    if (isGameBuilding) return;

    std::filesystem::path projectBase = activeProjectRoot.empty() ? contentRootPath.parent_path() : activeProjectRoot;
    std::string projName = projectBase.filename().string();
    if (projName.empty()) projName = "Game";
    std::filesystem::path gameBuildDir = projectBase / "Build";

    std::lock_guard<std::mutex> lock(gameBuildMutex);
    gameBuildOutputDir = gameBuildDir;
    gameBuildExePath = (gameBuildDir / (projName + ".exe")).string();
    gameBuildProgress = 0.0f;
    gameBuildStage = "Ready to Build";
    gameBuildLog = "[SYSTEM] Ready to package standalone game project: " + projName + "\n";
    gameBuildSuccess = false;
}

void EngineUI::StartGameBuildAsync(Scene& scene) {
    if (isGameBuilding) return;

    if (gameBuildThread.joinable()) {
        gameBuildThread.join();
    }

    std::filesystem::path projectBase = activeProjectRoot.empty() ? contentRootPath.parent_path() : activeProjectRoot;
    std::string projName = projectBase.filename().string();
    if (projName.empty()) projName = "Game";
    std::filesystem::path gameBuildDir = projectBase / "Build";
    std::filesystem::path localContentRoot = contentRootPath;

    // Snapshot current active level on main thread before launching background thread
    std::error_code ec;
    std::string activeRelLevel = "Content/Levels/Main.elevel";
    std::filesystem::path activeLevelAbsPath;

    if (!currentLevelFilePath.empty() && std::filesystem::exists(currentLevelFilePath, ec)) {
        auto relP = std::filesystem::relative(currentLevelFilePath, projectBase, ec);
        if (!ec) activeRelLevel = relP.string();
        std::replace(activeRelLevel.begin(), activeRelLevel.end(), '\\', '/');
        activeLevelAbsPath = currentLevelFilePath;
    } else {
        std::filesystem::path saveP = localContentRoot / "Levels" / "Main.elevel";
        std::filesystem::create_directories(saveP.parent_path(), ec);
        currentLevelFilePath = saveP.string();
        activeLevelAbsPath = saveP;
        activeRelLevel = "Content/Levels/Main.elevel";
    }

    LevelSerializer::SaveLevel(scene, activeLevelAbsPath.string());
    AddLog("LogBuild", "Snapshotted active editor level: " + activeLevelAbsPath.string(), 1);

    isGameBuilding = true;
    gameBuildProgress = 0.05f;
    gameBuildStage = "Initializing standalone packaging pipeline...";
    gameBuildSuccess = false;
    gameBuildOutputDir = gameBuildDir;
    gameBuildExePath = (gameBuildDir / (projName + ".exe")).string();

    {
        std::lock_guard<std::mutex> lock(gameBuildMutex);
        gameBuildLog = "[BUILD START] Packaging standalone game: " + projName + "\n";
        gameBuildLog += "[BUILD] Target output directory: " + gameBuildDir.string() + "\n";
        gameBuildLog += "[BUILD] Active level snapshot: " + activeRelLevel + "\n";
    }

    gameBuildThread = std::thread([this, projectBase, projName, gameBuildDir, localContentRoot, activeRelLevel, activeLevelAbsPath]() {
        auto updateStatus = [this](float progress, const std::string& stage, const std::string& logLine) {
            std::lock_guard<std::mutex> lock(gameBuildMutex);
            gameBuildProgress = progress;
            gameBuildStage = stage;
            if (!logLine.empty()) {
                time_t rawtime;
                struct tm* timeinfo;
                char timeBuf[32] = {};
                time(&rawtime);
                timeinfo = localtime(&rawtime);
                if (timeinfo) {
                    strftime(timeBuf, sizeof(timeBuf), "[%H:%M:%S] ", timeinfo);
                }
                gameBuildLog += std::string(timeBuf) + logLine + "\n";
            }
        };

        std::error_code sec;

        try {
            updateStatus(0.10f, "Creating directory layout...", "Creating standalone distribution directories in: " + gameBuildDir.string());

            std::filesystem::path binariesDir    = gameBuildDir / "Binaries";
            std::filesystem::path dataDir        = gameBuildDir / "Data";
            std::filesystem::path depsBuildDir   = gameBuildDir / "Deps";
            std::filesystem::path buildContent   = gameBuildDir / "Content";
            std::filesystem::path buildShaders   = gameBuildDir / "Shaders";
            std::filesystem::path buildResources = gameBuildDir / "Resources";

            std::filesystem::create_directories(gameBuildDir, sec);
            std::filesystem::create_directories(binariesDir, sec);
            std::filesystem::create_directories(dataDir, sec);
            std::filesystem::create_directories(depsBuildDir, sec);
            std::filesystem::create_directories(buildContent, sec);
            std::filesystem::create_directories(buildShaders, sec);
            std::filesystem::create_directories(buildResources, sec);

            std::filesystem::create_directories(dataDir / "Content", sec);
            std::filesystem::create_directories(dataDir / "Shaders", sec);
            std::filesystem::create_directories(dataDir / "Resources", sec);

            // Copy template structure if available
            std::filesystem::path gameTemplateDir = "C:\\Projects\\Eunoia-Engine\\Eunoia-Engine\\Templates\\Game Build Template";
            if (!std::filesystem::exists(gameTemplateDir, sec)) {
                gameTemplateDir = std::filesystem::current_path() / "Templates" / "Game Build Template";
            }
            if (std::filesystem::exists(gameTemplateDir, sec)) {
                for (const auto& item : std::filesystem::directory_iterator(gameTemplateDir, sec)) {
                    if (item.is_directory(sec)) {
                        std::filesystem::create_directories(gameBuildDir / item.path().filename(), sec);
                    }
                }
            }

            // Stage 2: Deploy Active Level Snapshot
            updateStatus(0.25f, "Deploying active level snapshot...", "Deploying startup level: " + activeRelLevel);
            std::filesystem::path targetLevel1 = gameBuildDir / activeRelLevel;
            std::filesystem::path targetLevel2 = dataDir / activeRelLevel;
            std::filesystem::path targetLevelFallback = gameBuildDir / "Main.elevel";
            std::filesystem::path targetLevelFallbackData = dataDir / "Main.elevel";

            std::filesystem::create_directories(targetLevel1.parent_path(), sec);
            std::filesystem::create_directories(targetLevel2.parent_path(), sec);

            if (std::filesystem::exists(activeLevelAbsPath, sec)) {
                std::filesystem::copy_file(activeLevelAbsPath, targetLevel1, std::filesystem::copy_options::overwrite_existing, sec);
                std::filesystem::copy_file(activeLevelAbsPath, targetLevel2, std::filesystem::copy_options::overwrite_existing, sec);
                std::filesystem::copy_file(activeLevelAbsPath, targetLevelFallback, std::filesystem::copy_options::overwrite_existing, sec);
                std::filesystem::copy_file(activeLevelAbsPath, targetLevelFallbackData, std::filesystem::copy_options::overwrite_existing, sec);
                updateStatus(0.30f, "Active level deployed", "Active level deployed to standalone root and Data/ successfully.");
            } else {
                updateStatus(0.30f, "Warning: Level snapshot missing", "Warning: Could not read level snapshot at " + activeLevelAbsPath.string());
            }

            // Recursive copy lambda that filters out source code and developer files
            auto copyGameFiles = [](const std::filesystem::path& src, const std::filesystem::path& dst) {
                std::error_code ec;
                if (!std::filesystem::exists(src, ec)) return;
                std::filesystem::create_directories(dst, ec);
                for (const auto& entry : std::filesystem::recursive_directory_iterator(src, std::filesystem::directory_options::skip_permission_denied, ec)) {
                    const auto rel = std::filesystem::relative(entry.path(), src, ec);
                    const auto target = dst / rel;
                    if (entry.is_directory(ec)) {
                        std::filesystem::create_directories(target, ec);
                    } else if (entry.is_regular_file(ec)) {
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        std::string relStr = rel.string();
                        bool isBehaviourScript = (ext == ".cpp" && (relStr.find("Behaviours") != std::string::npos || relStr.find("behaviours") != std::string::npos));
                        // Filter out engine source code, intermediate compiler files, and git files (keep behaviour scripts for runtime)
                        if (!isBehaviourScript) {
                            if (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp" ||
                                ext == ".obj" || ext == ".o" || ext == ".pdb" || ext == ".idb" ||
                                ext == ".ilk" || ext == ".tlog" || ext == ".log" || ext == ".tmp" ||
                                ext == ".econfigs" || ext == ".git" || ext == ".gitignore") {
                                continue;
                        }
                        }
                        std::filesystem::create_directories(target.parent_path(), ec);
                        std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing, ec);
                    }
                }
            };

            // Stage 3: Packaging Project Content
            updateStatus(0.40f, "Packaging game content...", "Copying game levels, textures, materials, and audio from: " + localContentRoot.string());
            copyGameFiles(localContentRoot, buildContent);
            copyGameFiles(localContentRoot, dataDir / "Content");
            updateStatus(0.65f, "Game content packaged", "Project content copied directly without engine developer artifacts.");

            // Stage 5: Deploying Shaders and Engine Resources
            updateStatus(0.70f, "Deploying Shaders & Resources...", "Deploying DirectX 12 shaders and engine graphics resources...");
            std::filesystem::path engineRoot = "C:\\Projects\\Eunoia-Engine\\Eunoia-Engine";
            if (!std::filesystem::exists(engineRoot / "Shaders", sec)) {
                engineRoot = "C:\\Projects\\Eunoia-Engine";
            }
            if (!std::filesystem::exists(engineRoot / "Shaders", sec)) {
                engineRoot = std::filesystem::current_path();
            }

            if (std::filesystem::exists(engineRoot / "Shaders", sec)) {
                copyGameFiles(engineRoot / "Shaders", buildShaders);
                copyGameFiles(engineRoot / "Shaders", dataDir / "Shaders");
                updateStatus(0.75f, "Shaders deployed", "DirectX 12 Shaders deployed.");
            }
            if (std::filesystem::exists(engineRoot / "Resources", sec)) {
                copyGameFiles(engineRoot / "Resources", buildResources);
                copyGameFiles(engineRoot / "Resources", dataDir / "Resources");
                updateStatus(0.80f, "Resources deployed", "Engine graphics resources deployed.");
            }

            // Stage 6: Deploying Runtime Dependencies (DLLs)
            updateStatus(0.85f, "Deploying runtime dependencies...", "Deploying glfw3.dll and runtime libraries...");
            char curExeBuf[MAX_PATH] = {};
            GetModuleFileNameA(NULL, curExeBuf, MAX_PATH);
            std::filesystem::path curExePath(curExeBuf);
            std::filesystem::path curExeDir = curExePath.parent_path();

            std::vector<std::filesystem::path> dllCandidates = {
                curExeDir / "glfw3.dll",
                engineRoot / "deps" / "glfw-3.5.1.bin.WIN64" / "lib-mingw-w64" / "glfw3.dll",
                engineRoot / "Plugins" / "primitives" / "primitives.dll",
                curExeDir / "primitives.dll"
            };

            for (const auto& dllSrc : dllCandidates) {
                if (std::filesystem::exists(dllSrc, sec)) {
                    std::string fname = dllSrc.filename().string();
                    std::filesystem::copy_file(dllSrc, gameBuildDir / fname, std::filesystem::copy_options::overwrite_existing, sec);
                    std::filesystem::copy_file(dllSrc, binariesDir / fname, std::filesystem::copy_options::overwrite_existing, sec);
                    std::filesystem::copy_file(dllSrc, depsBuildDir / fname, std::filesystem::copy_options::overwrite_existing, sec);
                    updateStatus(0.88f, "Dependency deployed", "Deployed DLL: " + fname);
                }
            }

            // Stage 7: Writing GameConfig.json, Version.euversion, and Run.bat
            updateStatus(0.90f, "Configuring standalone game metadata...", "Generating GameConfig.json and Version.euversion...");

            auto writeGameConfig = [&](const std::filesystem::path& destCfg) {
                std::ofstream cf(destCfg);
                if (cf.is_open()) {
                    nlohmann::json gj;
                    gj["gameName"] = projName;
                    gj["startupLevel"] = activeRelLevel;
                    gj["defaultLevel"] = activeRelLevel;
                    gj["standalone"] = true;
                    gj["version"] = "1.0.0";
                    gj["renderer"] = {
                        {"backend", "DirectX12"},
                        {"preferDX12", true}
                    };
                    gj["window"] = {
                        {"title", projName},
                        {"width", 1920},
                        {"height", 1080},
                        {"fullscreen", false},
                        {"vsync", true}
                    };
                    cf << gj.dump(4);
                    cf.close();
                }
            };

            writeGameConfig(gameBuildDir / "GameConfig.json");
            writeGameConfig(dataDir / "GameConfig.json");

            {
                std::ofstream vf(gameBuildDir / "Version.euversion");
                if (vf.is_open()) {
                    nlohmann::json vj;
                    vj["engineVersion"] = "0.1.0";
                    vj["projectVersion"] = "1.0.0";
                    vj["gameName"] = projName;
                    vf << vj.dump(4);
                    vf.close();
                }
            }

            {
                std::ofstream runScript(gameBuildDir / "Run.bat");
                if (runScript.is_open()) {
                    runScript << "@echo off\n";
                    runScript << "cd /d \"%~dp0\"\n";
                    runScript << "echo Launching " << projName << " (DirectX 12 Standalone)...\n";
                    runScript << "start \"\" \"" << projName << ".exe\"\n";
                    runScript.close();
                }
            }

            // Stage 8: Building / Copying Standalone Executable
            updateStatus(0.95f, "Deploying standalone executable...", "Packaging " + projName + ".exe...");
            std::filesystem::path rootGameExe = gameBuildDir / (projName + ".exe");
            std::filesystem::path binGameExe  = binariesDir / (projName + ".exe");

            std::filesystem::path exeSource = curExePath;
            if (!std::filesystem::exists(exeSource, sec)) {
                exeSource = "C:\\Projects\\Eunoia-Engine\\Build\\Engine\\Eunoia-Editor.exe";
            }
            if (!std::filesystem::exists(exeSource, sec)) {
                exeSource = engineRoot / "Build" / "Runtime" / "Eunoia-Runtime.exe";
            }

            if (std::filesystem::exists(exeSource, sec)) {
                std::filesystem::copy_file(exeSource, rootGameExe, std::filesystem::copy_options::overwrite_existing, sec);
                std::filesystem::copy_file(exeSource, binGameExe, std::filesystem::copy_options::overwrite_existing, sec);
                updateStatus(0.98f, "Executable created", "Executable deployed: " + rootGameExe.string());
            } else {
                updateStatus(0.98f, "Executable error", "Warning: Could not find base executable source!");
            }

            // Stage 9: Completion
            {
                std::lock_guard<std::mutex> lock(gameBuildMutex);
                gameBuildProgress = 1.0f;
                gameBuildStage = "Build Succeeded";
                gameBuildSuccess = true;
                isGameBuilding = false;
                gameBuildExePath = rootGameExe.string();
                gameBuildOutputDir = gameBuildDir;
                gameBuildLog += "[BUILD SUCCESS] Standalone Game Package created successfully!\n";
                gameBuildLog += "[BUILD SUCCESS] Executable: " + rootGameExe.filename().string() + "\n";
                gameBuildLog += "[BUILD SUCCESS] Startup Level: " + activeRelLevel + "\n";
                gameBuildLog += "[BUILD SUCCESS] Standalone build complete in: " + gameBuildDir.string() + "\n";

                m_bottomProgress.active = true;
                m_bottomProgress.completionTimer = 3.5f;
                m_bottomProgress.title = "Standalone Game Package Built Successfully";
                m_bottomProgress.success = true;
            }

        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(gameBuildMutex);
            gameBuildProgress = 1.0f;
            gameBuildStage = "Build Failed";
            gameBuildSuccess = false;
            isGameBuilding = false;
            gameBuildLog += "[BUILD ERROR] Exception occurred during packaging: " + std::string(e.what()) + "\n";

            m_bottomProgress.active = true;
            m_bottomProgress.completionTimer = 4.0f;
            m_bottomProgress.title = "Standalone Game Build Failed";
            m_bottomProgress.success = false;
        } catch (...) {
            std::lock_guard<std::mutex> lock(gameBuildMutex);
            gameBuildProgress = 1.0f;
            gameBuildStage = "Build Failed";
            gameBuildSuccess = false;
            isGameBuilding = false;
            gameBuildLog += "[BUILD ERROR] Unknown exception occurred during packaging!\n";

            m_bottomProgress.active = true;
            m_bottomProgress.completionTimer = 4.0f;
            m_bottomProgress.title = "Standalone Game Build Failed";
            m_bottomProgress.success = false;
        }
    });
}

void EngineUI::RenderGameBuildModal(Scene& scene) {
    if (!showGameBuildModal) return;

    ImGui::SetNextWindowSize(ImVec2(680, 520), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (isGameBuilding) {
        flags |= ImGuiWindowFlags_NoMove;
    }

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.12f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.24f, 0.24f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.10f, 0.10f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.14f, 0.14f, 0.14f, 1.0f));

    if (ImGui::Begin("Standalone Game Project Build###GameBuildModal", &showGameBuildModal, flags)) {
        float progress;
        std::string stage;
        std::string logCopy;
        bool success;
        bool building;
        std::string exePath;
        std::filesystem::path outDir;

        {
            std::lock_guard<std::mutex> lock(gameBuildMutex);
            progress = gameBuildProgress;
            stage = gameBuildStage;
            logCopy = gameBuildLog;
            success = gameBuildSuccess;
            building = isGameBuilding;
            exePath = gameBuildExePath;
            outDir = gameBuildOutputDir;
        }

        // Header Title
        ImGui::TextColored(ImVec4(0.18f, 0.68f, 1.0f, 1.0f), "ÃƒÆ’Ã‚Â°Ãƒâ€¦Ã‚Â¸ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂºÃƒâ€šÃ‚Â  Standalone Game Build Pipeline");
        ImGui::Separator();
        ImGui::Spacing();

        // Project Info
        std::filesystem::path projectBase = activeProjectRoot.empty() ? contentRootPath.parent_path() : activeProjectRoot;
        std::string projName = projectBase.filename().string();
        ImGui::Text("Project: %s", projName.c_str());
        ImGui::SameLine(340);
        ImGui::Text("Output: %s", outDir.empty() ? "(default Build/)" : outDir.string().c_str());

        ImGui::Spacing();

        // Progress Bar
        char overlay[128];
        snprintf(overlay, sizeof(overlay), "%.0f%% - %s", progress * 100.0f, stage.c_str());
        ImVec4 barColor = building ? ImVec4(0.18f, 0.65f, 1.0f, 1.0f) : (success ? ImVec4(0.2f, 0.85f, 0.35f, 1.0f) : ImVec4(0.9f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 26.0f), overlay);
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // Current Stage Status text
        if (building) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "ÃƒÆ’Ã‚Â¢Ãƒâ€šÃ‚ÂÃƒâ€šÃ‚Â³ Building Standalone Package: %s", stage.c_str());
        } else if (success) {
            ImGui::TextColored(ImVec4(0.2f, 0.95f, 0.35f, 1.0f), "ÃƒÆ’Ã‚Â¢Ãƒâ€¦Ã¢â‚¬Å“ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ Standalone Build Succeeded! Ready for distribution.");
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "ÃƒÆ’Ã‚Â¢Ãƒâ€šÃ‚ÂÃƒâ€¦Ã¢â‚¬â„¢ %s", stage.c_str());
        }

        ImGui::Spacing();

        // Build Log Streaming Console
        ImGui::TextDisabled("Build Output & Diagnostics Log:");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.20f, 0.20f, 1.0f));
        if (ImGui::BeginChild("GameBuildLogChild", ImVec2(-1.0f, 240.0f), true, ImGuiWindowFlags_HorizontalScrollbar)) {
            ImGui::TextUnformatted(logCopy.c_str());
            if (building || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Action Buttons
        if (building) {
            ImGui::BeginDisabled();
            ImGui::Button("Packaging Standalone Game...", ImVec2(220, 32));
            ImGui::EndDisabled();
        } else {
            if (success) {
                if (ImGui::Button("ÃƒÆ’Ã‚Â°Ãƒâ€¦Ã‚Â¸Ãƒâ€¦Ã‚Â¡ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Launch Game (.exe)", ImVec2(170, 32))) {
                    if (!exePath.empty() && std::filesystem::exists(exePath)) {
                        ShellExecuteA(NULL, "open", exePath.c_str(), NULL, outDir.string().c_str(), SW_SHOW);
                    } else if (std::filesystem::exists(outDir / "Run.bat")) {
                        std::string batPath = (outDir / "Run.bat").string();
                        ShellExecuteA(NULL, "open", batPath.c_str(), NULL, outDir.string().c_str(), SW_SHOW);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("ÃƒÆ’Ã‚Â°Ãƒâ€¦Ã‚Â¸ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œÃƒâ€šÃ‚Â Open Build Folder", ImVec2(170, 32))) {
                    if (!outDir.empty() && std::filesystem::exists(outDir)) {
                        ShellExecuteA(NULL, "open", outDir.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
                    }
                }
                ImGui::SameLine();
            }

            if (ImGui::Button("Rebuild Project", ImVec2(130, 32))) {
                StartGameBuildAsync(scene);
            }

            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(100, 32))) {
                showGameBuildModal = false;
            }
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(4);
}
