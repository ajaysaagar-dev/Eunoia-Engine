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
#include <Animation-Graph/AnimatorGraphEditor.h>
#include <Animation-Blending/AnimationBlendManager.h>
#include <EngineAssets/SkeletalAnimation.h>
#include <Level-Animation/LevelAnimation.h>
#include <Level-Animation/LevelAnimationEditor.h>
#include <Level-Animation/LevelAnimationManager.h>

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

EngineUI* EngineUI::GetInstance() {
    return g_pEngineUI;
}
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

#include <Editor/ContentHelpers.h>
BackgroundSyncState s_bgSync;

void TriggerBackgroundRegistrySync(const std::filesystem::path& path) {
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

void SyncRegistryWithUIProgress(const std::filesystem::path& path) {
    TriggerBackgroundRegistrySync(path);
}

bool MoveAssetToDirectory(const AssetID& id, const std::string& targetVirtualDir, const std::filesystem::path& contentRoot) {
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

bool MoveFolderToDirectory(const std::string& srcVirtualDir, const std::string& targetVirtualDir, const std::filesystem::path& contentRoot) {
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
    } else if (meta->type == AssetType::Script) {
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
    if (!curPayload || (!curPayload->IsDataType("ASSET_ID") && !curPayload->IsDataType("CONTENT_DRAG"))) return;

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
            if (const ImGuiPayload* cPayload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                std::string payloadData((const char*)cPayload->Data);
                std::stringstream pss(payloadData);
                std::string itemKey;
                while (std::getline(pss, itemKey, '\n')) {
                    if (itemKey.empty() || itemKey.rfind("DIR:", 0) == 0) continue;
                    if (itemKey.rfind("FILE:", 0) == 0) itemKey = itemKey.substr(5);
                    std::string cleanKey = itemKey;
                    if (cleanKey.rfind("FILE:", 0) == 0) cleanKey = cleanKey.substr(5);
                    const AssetMetadata* meta = nullptr;
                    if (cleanKey.rfind("ASSET:", 0) == 0) {
                        meta = AssetRegistry::Get().FindByVirtualPath(cleanKey.substr(6));
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
                                                !Eunoia::NodeBehaviour::NodeBehaviourManager::Get().IsActive() &&
                        !Eunoia::AnimatorGraphManager::Get().IsActive() &&
                        !Eunoia::AnimationBlendManager::Get().IsActive() &&
                        (s_activePromotedTabIndex < 0 || s_activePromotedTabIndex >= (int)s_promotedTabs.size());

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

        if (isMainEditor) {
            ImGuiDockNode* centralNode = ImGui::DockBuilderGetCentralNode(dockId);
            if (centralNode && centralNode->Size.x > 20.0f && centralNode->Size.y > 20.0f) {
                m_dynamicViewportRect.x = centralNode->Pos.x - mainVp->Pos.x;
                m_dynamicViewportRect.y = centralNode->Pos.y - mainVp->Pos.y;
                m_dynamicViewportRect.width = centralNode->Size.x;
                m_dynamicViewportRect.height = centralNode->Size.y;
                m_hasDynamicViewportRect = true;
            }
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
        if (showMaterialEditor && !IsWindowPromotedTab("Material Editor")) {
            RenderMaterialEditor(scene);
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

        if (Eunoia::AnimatorGraphManager::Get().IsActive()) {
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
                } else if (pName == "Material Editor") {
                    RenderMaterialEditor(scene);
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
                    ImGui::SameLine();
                    if (scene.enableShadows) {
                        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "[ACTIVE]");
                    } else {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "[OFF]");
                    }
                    ImGui::Separator();

                    // --- 1. SHADOW TYPE (WickedEngine shadowTypeComboBox) ---
                    ImGui::TextDisabled("Shadow Architecture:");
                    const char* shadowTypes[] = { "Shadowmaps (Hardware D32)", "Ray Traced Shadows (DXR / Fallback)" };
                    ImGui::SetNextItemWidth(260.0f);
                    if (ImGui::Combo("Shadow Type##ShadowType", &scene.shadowType, shadowTypes, IM_ARRAYSIZE(shadowTypes))) {
                        AddLog("LogRenderer", scene.shadowType == 1 ? "Shadow Type: Ray Traced" : "Shadow Type: Shadowmaps", 0);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Choose between shadowmaps and ray traced shadows.\nNote that ray traced shadows utilize hardware ray tracing acceleration when supported.");
                    }
                    ImGui::Separator();

                    // --- 2. 2D SHADOWMAP RESOLUTION (WickedEngine shadowProps2DComboBox) ---
                    ImGui::TextDisabled("2D Shadowmap Resolution (Directional / Spot):");
                    const int res2DVals[] = { 0, 128, 256, 512, 1024, 2048, 4096 };
                    const char* res2DLabels[] = { "Off (Disabled)", "128 (Ultra Low)", "256 (Very Low)", "512 (Low)", "1024 (Medium)", "2048 (High - Default)", "4096 (Cinematic / Ultra)" };
                    int cur2DIdx = 5; // default 2048
                    for (int i = 0; i < 7; ++i) {
                        if (scene.shadowResolution2D == res2DVals[i]) { cur2DIdx = i; break; }
                    }
                    ImGui::SetNextItemWidth(260.0f);
                    if (ImGui::Combo("2D Shadowmap Res##2DRes", &cur2DIdx, res2DLabels, IM_ARRAYSIZE(res2DLabels))) {
                        scene.shadowResolution2D = res2DVals[cur2DIdx];
                        scene.shadowResolution = scene.shadowResolution2D;
                        for (auto& obj : scene.objects) {
                            if ((obj.isLight || IsLightPrimitive(obj.type)) && obj.light.type == LightType::Directional) {
                                obj.light.shadowResolution = scene.shadowResolution2D;
                            }
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Choose a shadow quality preset for 2D shadow maps (spotlights, directional lights)...\nThis specifies the maximum shadow resolution for these light types.");
                    }

                    // --- 3. CUBE SHADOWMAP RESOLUTION (WickedEngine shadowPropsCubeComboBox) ---
                    ImGui::TextDisabled("Cube Shadowmap Resolution (Point / Area):");
                    const int resCubeVals[] = { 0, 128, 256, 512, 1024, 2048 };
                    const char* resCubeLabels[] = { "Off (Disabled)", "128 (Low)", "256 (Medium Low)", "512 (Balanced - Default)", "1024 (High)", "2048 (Cinematic)" };
                    int curCubeIdx = 3; // default 512
                    for (int i = 0; i < 6; ++i) {
                        if (scene.shadowResolutionCube == resCubeVals[i]) { curCubeIdx = i; break; }
                    }
                    ImGui::SetNextItemWidth(260.0f);
                    if (ImGui::Combo("Cube Shadowmap Res##CubeRes", &curCubeIdx, resCubeLabels, IM_ARRAYSIZE(resCubeLabels))) {
                        scene.shadowResolutionCube = resCubeVals[curCubeIdx];
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Choose a shadow quality preset for cube shadow maps (pointlights, area lights)...\nThis specifies the maximum shadow resolution for these light types.");
                    }
                    ImGui::Separator();

                    // --- 4. SHADOW FILTERING (WickedEngine Vogel Disk shadowHF.hlsli) ---
                    ImGui::TextDisabled("Shadow Filtering & Soft Penumbra (WickedEngine Vogel Disk):");
                    const int sampleCounts[] = { 4, 8, 16, 32 };
                    const char* sampleLabels[] = { "4 Samples (Performance)", "8 Samples (Medium)", "16 Samples (High - Vogel Disk Reference)", "32 Samples (Ultra / Smooth Penumbra)" };
                    int curSampleIdx = 2; // default 16
                    for (int i = 0; i < 4; ++i) {
                        if (scene.shadowSampleCount == sampleCounts[i]) { curSampleIdx = i; break; }
                    }
                    ImGui::SetNextItemWidth(260.0f);
                    if (ImGui::Combo("Vogel Disk Samples##VogelSamples", &curSampleIdx, sampleLabels, IM_ARRAYSIZE(sampleLabels))) {
                        scene.shadowSampleCount = sampleCounts[curSampleIdx];
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("WickedEngine Vogel disk Poisson sampling kernel count.\nBaked Poisson distribution remapped via [-1, 1] range in shadowHF.hlsli.");
                    }

                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::SliderFloat("Filter Radius / Spread", &scene.pcfRadius, 0.2f, 5.0f, "%.2f");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Vogel disk soft shadow spread factor. Remapped dynamically via:\nspread = inv_shadow_res * mad(radius, 8, 2) matching WickedEngine.");
                    }

                    ImGui::Checkbox("PCSS Blocker Search (Soft Shadows)", &scene.shadowPCSS);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Percentage-Closer Soft Shadows (PCSS):\nSearches for blockers to dynamically widen shadow penumbra spread farther from occluders.");
                    }

                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::SliderFloat("Depth / Normal Bias", &scene.shadowBias, 0.0001f, 0.01f, "%.4f");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Slope-scale and normal offset depth bias to eliminate shadow acne without detaching contact shadows.");
                    }

                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::SliderFloat("Shadow Strength", &scene.shadowStrength, 0.0f, 1.0f, "%.2f");
                    ImGui::Separator();

                    // --- 5. CASCADED SHADOW MAPS (CSM - WickedEngine Directional Cascades) ---
                    ImGui::TextDisabled("Cascaded Shadow Maps (CSM - Directional Light):");
                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::SliderInt("Cascade Count", &scene.cascadeCount, 1, 4);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Number of shadow cascades for directional sun lighting (1 to 4).\nEach cascade covers a progressively larger distance range.");
                    }

                    for (int c = 0; c < scene.cascadeCount; ++c) {
                        char cLabel[64];
                        snprintf(cLabel, sizeof(cLabel), "Cascade %d Split##Casc%d", c + 1, c);
                        ImGui::SetNextItemWidth(260.0f);
                        float maxSplit = scene.shadowDistance;
                        ImGui::SliderFloat(cLabel, &scene.cascadeDistances[c], 1.0f, maxSplit, "%.1f m");
                    }

                    if (ImGui::Button("Auto-Calculate Practical Splits (Logarithmic)")) {
                        float nearZ = 0.5f;
                        float farZ = scene.shadowDistance;
                        float lambda = 0.75f; // WickedEngine practical split ratio
                        int numCascades = scene.cascadeCount;
                        for (int i = 0; i < numCascades; ++i) {
                            float p = float(i + 1) / float(numCascades);
                            float logSplit = nearZ * std::pow(farZ / nearZ, p);
                            float uniSplit = nearZ + (farZ - nearZ) * p;
                            scene.cascadeDistances[i] = lambda * logSplit + (1.0f - lambda) * uniSplit;
                        }
                        AddLog("LogRenderer", "Cascade split distances recalculated.", 0);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Automatically calculates optimal split distances blending logarithmic and uniform distributions.");
                    }

                    ImGui::Checkbox("Cascade Border Blending", &scene.cascadeBlending);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Smoothly blends between cascade boundaries across the outer 10% transition region to prevent pop-in seams.");
                    }

                    ImGui::Checkbox("Visualize Cascades (Color Overlay)", &scene.visualizeCascades);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Colorizes scene pixels to visualize directional light cascade splits:\nRed = Cascade 1 (Near)\nGreen = Cascade 2 (Mid)\nBlue = Cascade 3 (Far)\nYellow = Cascade 4 (Extreme)");
                    }
                    ImGui::Separator();

                    // --- 6. SCREEN SPACE SHADOWS (Contact Shadows - WickedEngine screenspaceshadowCS) ---
                    ImGui::TextDisabled("Screen Space Shadows (Contact Shadows):");
                    ImGui::Checkbox("Enable Contact Shadows", &scene.screenSpaceShadows);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Enable screen space contact shadows (WickedEngine screenspaceshadowCS).\nAdds ultra-fine contact shadows on hair, wrinkles, teeth, foliage, and small geometry.");
                    }
                    if (scene.screenSpaceShadows) {
                        ImGui::SetNextItemWidth(260.0f);
                        ImGui::SliderFloat("Contact Shadow Range", &scene.screenSpaceShadowRange, 0.01f, 10.0f, "%.2f m");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Range / ray length of screen space contact shadows.");
                        }

                        ImGui::SetNextItemWidth(260.0f);
                        ImGui::SliderInt("Contact Shadow Samples", &scene.screenSpaceShadowSamples, 4, 128);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Sample count / raymarch steps for contact shadows. Higher values are cleaner but have a slight GPU cost.");
                        }
                    }
                    ImGui::Separator();

                    // --- 7. CAPSULE SHADOWS (WickedEngine capsuleShadowHF) ---
                    ImGui::TextDisabled("Capsule Shadows (Ambient Occlusion Occluders):");
                    ImGui::Checkbox("Enable Capsule Shadows", &scene.capsuleShadows);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Enable ambient occlusion capsule shadows (WickedEngine capsuleShadowHF).\nGenerates soft contact ground shadows under characters and capsule colliders.");
                    }
                    if (scene.capsuleShadows) {
                        ImGui::SetNextItemWidth(260.0f);
                        ImGui::SliderFloat("Capsule Shadow Angle", &scene.capsuleShadowAngle, 0.0f, 90.0f, "%.1f deg");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Set capsule shadow spread angle in degrees (WickedEngine SetCapsuleShadowAngle).");
                        }

                        ImGui::SetNextItemWidth(260.0f);
                        ImGui::SliderFloat("Capsule Shadow Fade", &scene.capsuleShadowFade, 0.0f, 1.0f, "%.2f");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Set capsule shadow fading intensity (WickedEngine SetCapsuleShadowFade).");
                        }
                    }
                    ImGui::Separator();

                    // --- 8. PERFORMANCE, LOD & DISTANCE CULLING (WickedEngine shadowLODCheckBox) ---
                    ImGui::TextDisabled("Performance, LOD & Distance Range:");
                    ImGui::SetNextItemWidth(260.0f);
                    if (ImGui::SliderFloat("Max Shadow Distance", &scene.shadowDistance, 5.0f, 1000.0f, "%.1f m")) {
                        for (auto& obj : scene.objects) {
                            if ((obj.isLight || IsLightPrimitive(obj.type)) && obj.light.type == LightType::Directional) {
                                obj.light.shadowDistance = scene.shadowDistance;
                            }
                        }
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Maximum distance from camera to render shadows (%.1fm).\nShadows beyond this distance are completely culled with a smooth outer 15% fade.", scene.shadowDistance);
                    }

                    // Quick Distance Presets
                    auto SetShadowDist = [&](float d) {
                        scene.shadowDistance = d;
                        for (auto& obj : scene.objects) {
                            if ((obj.isLight || IsLightPrimitive(obj.type)) && obj.light.type == LightType::Directional) {
                                obj.light.shadowDistance = d;
                            }
                        }
                    };
                    if (ImGui::BeginMenu("Quick Distance Presets")) {
                        if (ImGui::MenuItem("Near (25 m)", nullptr, scene.shadowDistance == 25.0f)) SetShadowDist(25.0f);
                        if (ImGui::MenuItem("Medium / Balanced (60 m)", nullptr, scene.shadowDistance == 60.0f)) SetShadowDist(60.0f);
                        if (ImGui::MenuItem("Far (120 m)", nullptr, scene.shadowDistance == 120.0f)) SetShadowDist(120.0f);
                        if (ImGui::MenuItem("Very Far (250 m)", nullptr, scene.shadowDistance == 250.0f)) SetShadowDist(250.0f);
                        if (ImGui::MenuItem("Extreme (500 m)", nullptr, scene.shadowDistance == 500.0f)) SetShadowDist(500.0f);
                        if (ImGui::MenuItem("Ultra Cinematic (1000 m)", nullptr, scene.shadowDistance == 1000.0f)) SetShadowDist(1000.0f);
                        ImGui::EndMenu();
                    }

                    ImGui::Checkbox("Shadow LOD Override", &scene.shadowLODOverride);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Enable custom LOD selection for shadow maps (WickedEngine SetShadowLODOverrideEnabled).\nCan improve GPU performance by rendering simplified LOD geometry into shadow maps.");
                    }

                    ImGui::Checkbox("Show Light Frustum Wireframe", &scene.showLightFrustum);

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
        bool isLevelActive = (uiMgr.IsMainEditorActive() && !Eunoia::NodeBehaviour::NodeBehaviourManager::Get().IsActive() && !Eunoia::AnimatorGraphManager::Get().IsActive() && !Eunoia::AnimationBlendManager::Get().IsActive() && (s_activePromotedTabIndex < 0 || s_activePromotedTabIndex >= (int)s_promotedTabs.size()));
        std::string levelTabName = "Level Editor";
        if (levelUnsaved) levelTabName += "*";
        levelTabName += "##Tab_Level";

        if (RenderTabWithSaveProgress(levelTabName.c_str(), isLevelActive, m_levelSaveState, tabBtnH)) {
            materialWorkspaceActive = false;
            Eunoia::NodeBehaviour::NodeBehaviourManager::Get().SwitchToMainEditor();
            Eunoia::AnimatorGraphManager::Get().SwitchToMainEditor();
            Eunoia::AnimationBlendManager::Get().SwitchToMainEditor();
            uiMgr.SwitchToMainEditor();
            s_activePromotedTabIndex = -1;
            if (activeMaterialDocIdx >= 0 && activeMaterialDocIdx < (int)openMaterialDocs.size()) {
                PropagateMaterialChangesToScene(scene, openMaterialDocs[activeMaterialDocIdx].material);
            }
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

                if (ImGui::MenuItem("New Behaviour Script (.cpp)...")) {
                    showNewBehaviourPopup = true;
                    std::string defName = "NewBehaviour";
                    if (IsBehaviourNameAlreadyExists(defName)) {
                        int bIdx = 1;
                        while (IsBehaviourNameAlreadyExists("NewBehaviour_" + std::to_string(bIdx))) bIdx++;
                        defName = "NewBehaviour_" + std::to_string(bIdx);
                    }
                    snprintf(newBehaviourNameBuf, sizeof(newBehaviourNameBuf), "%s", defName.c_str());
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

// Outliner panel implementation moved to Panels/OutlinerPanel.cpp

// DetailsPanel implementation moved to Panels/DetailsPanel.cpp
// MaterialEditorPanel implementation moved to Panels/MaterialEditorPanel.cpp
// EnvironmentPanel implementation moved to Panels/EnvironmentPanel.cpp
// ContentBrowserPanel implementation moved to Panels/ContentBrowserPanel.cpp
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

// RenderBehavioursSection moved to Panels/DetailsPanel.cpp
// ProjectBrowserPanel implementation moved to Panels/ProjectBrowserPanel.cpp
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
