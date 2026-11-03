#pragma once
#include <filesystem>
#include <string>
#include <atomic>
#include <mutex>
#include <EngineAssets/AssetRegistry.h>

struct BackgroundSyncState {
    std::atomic<bool> isRunning{false};
    std::atomic<float> progress{0.0f};
    std::string currentDetail = "";
    float completionTimer = 0.0f;
    std::mutex mutex;
};
extern BackgroundSyncState s_bgSync;

void TriggerBackgroundRegistrySync(const std::filesystem::path& path);
void SyncRegistryWithUIProgress(const std::filesystem::path& path);
bool MoveAssetToDirectory(const AssetID& id, const std::string& targetVirtualDir, const std::filesystem::path& contentRoot);
bool MoveFolderToDirectory(const std::string& srcVirtualDir, const std::string& targetVirtualDir, const std::filesystem::path& contentRoot);
