#pragma once
// ============================================================================
// SceneCacheManager — High-performance versioned binary level loading cache
// 
// Features:
// - Versioned binary cache format (.bin) + metadata sidecar (.bin.meta)
// - Memory-mapped IO for near-instant disk access on Windows
// - Asset deduplication across all entities in the scene
// - Per-asset source file hashing & incremental invalidation
// - Asynchronous loading states (SceneLoadState) & real progress
// - Priority queues: Critical, High, Normal, Low, Background
// - Atomic file generation (.tmp -> rename)
// - Corrupt cache detection with automatic fallback
// - Integration with DX12 upload batching
// ============================================================================

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <chrono>
#include <filesystem>
#include <EngineAssets/AssetID.h>
#include <EngineAssets/AssetType.h>
#include <EngineAssets/Geometry.h>
#include <EngineScene/Scene.h>

namespace Eunoia {

using SceneProgressFn = std::function<void(float progress, const std::string& step, const std::string& detail)>;

// Magic: "ENCB" (Eunoia Cache Binary)
constexpr uint32_t SCENE_CACHE_MAGIC = 0x454E4342;
constexpr uint32_t SCENE_CACHE_VERSION = 2;

enum class AssetPriority : uint8_t {
    Critical = 0,
    High = 1,
    Normal = 2,
    Low = 3,
    Background = 4
};

inline const char* AssetPriorityToString(AssetPriority priority) {
    switch (priority) {
        case AssetPriority::Critical:   return "Critical";
        case AssetPriority::High:       return "High";
        case AssetPriority::Normal:     return "Normal";
        case AssetPriority::Low:        return "Low";
        case AssetPriority::Background: return "Background";
        default:                        return "Normal";
    }
}

enum class SceneLoadState : uint8_t {
    Unloaded = 0,
    ReadingCache,
    PreparingAssets,
    UploadingGPUResources,
    LoadingCriticalAssets,
    LoadingSecondaryAssets,
    Streaming,
    Ready
};

inline const char* SceneLoadStateToString(SceneLoadState state) {
    switch (state) {
        case SceneLoadState::Unloaded:               return "Unloaded";
        case SceneLoadState::ReadingCache:           return "Reading Cache";
        case SceneLoadState::PreparingAssets:        return "Preparing Assets";
        case SceneLoadState::UploadingGPUResources:  return "Uploading GPU Resources";
        case SceneLoadState::LoadingCriticalAssets:  return "Loading Critical Assets";
        case SceneLoadState::LoadingSecondaryAssets: return "Loading Secondary Assets";
        case SceneLoadState::Streaming:              return "Streaming";
        case SceneLoadState::Ready:                  return "Ready";
        default:                                     return "Unknown";
    }
}

#pragma pack(push, 1)
struct SceneCacheHeader {
    uint32_t magic = SCENE_CACHE_MAGIC;
    uint32_t version = SCENE_CACHE_VERSION;

    uint64_t sceneHash = 0;
    uint64_t assetDatabaseHash = 0;

    uint32_t entityCount = 0;
    uint32_t meshCount = 0;
    uint32_t textureCount = 0;
    uint32_t materialCount = 0;
    uint32_t shaderCount = 0;

    uint64_t entityDataOffset = 0;
    uint64_t meshIndexOffset = 0;
    uint64_t textureIndexOffset = 0;
    uint64_t materialIndexOffset = 0;
    uint64_t assetDataOffset = 0;
};

struct CachedMeshRecord {
    uint64_t assetIdHigh = 0;
    uint64_t assetIdLow = 0;
    uint64_t sourceHash = 0;
    uint32_t importerVersion = 1;
    uint32_t cookVersion = 1;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint64_t vertexDataOffset = 0;
    uint64_t indexDataOffset = 0;
    glm::vec3 aabbMin{0.0f};
    glm::vec3 aabbMax{0.0f};
    uint8_t priority = static_cast<uint8_t>(AssetPriority::Normal);
    char path[260] = {0};
};

struct CachedTextureRecord {
    uint64_t assetIdHigh = 0;
    uint64_t assetIdLow = 0;
    uint64_t sourceHash = 0;
    uint32_t cookVersion = 1;
    int32_t width = 0;
    int32_t height = 0;
    int32_t channels = 0;
    uint32_t format = 0;
    uint32_t mipLevels = 1;
    uint64_t dataOffset = 0;
    uint64_t dataSize = 0;
    glm::vec3 averageColor{0.5f};
    uint8_t isNormalMap = 0;
    uint8_t priority = static_cast<uint8_t>(AssetPriority::Normal);
    char path[260] = {0};
};

struct CachedEntityRecord {
    int32_t actorId = -1;
    uint32_t meshIndex = UINT32_MAX;
    uint8_t isImportedMesh = 0;
    uint8_t priority = static_cast<uint8_t>(AssetPriority::Normal);
};
#pragma pack(pop)

struct AssetDependencyInfo {
    AssetID id;
    AssetType type = AssetType::Unknown;
    std::string path;
    uint64_t sourceHash = 0;
    uint64_t fileSize = 0;
    uint64_t timestamp = 0;
    uint32_t cookVersion = 1;
    AssetPriority priority = AssetPriority::Normal;
};

struct CacheProfilingStats {
    float validationTimeMs = 0.0f;
    float diskIoTimeMs = 0.0f;
    float meshProcessTimeMs = 0.0f;
    float textureProcessTimeMs = 0.0f;
    float gpuUploadTimeMs = 0.0f;
    float totalLoadTimeMs = 0.0f;
    size_t cacheSizeBytes = 0;
    size_t uniqueMeshesLoaded = 0;
    size_t uniqueTexturesLoaded = 0;
};

enum class CacheValidationStatus {
    Valid,
    Missing,
    Corrupt,
    VersionMismatch,
    SceneModified,
    SourceAssetModified
};

class SceneCacheManager {
public:
    static SceneCacheManager& Get() {
        static SceneCacheManager instance;
        return instance;
    }

    uint32_t GetCacheVersion() const { return SCENE_CACHE_VERSION; }

    uint64_t GetSceneHash(const Scene& scene) const;
    uint64_t ComputeFileHash(const std::filesystem::path& path) const;

    void CollectAssetDependencies(const Scene& scene, std::vector<AssetDependencyInfo>& outDeps) const;

    CacheValidationStatus ValidateCache(const std::string& binPath, const Scene* scene = nullptr, std::string* outReason = nullptr) const;

    bool GenerateCache(const Scene& scene, const std::string& binPath, SceneProgressFn onProgress = nullptr);

    bool LoadCache(Scene& scene, const std::string& binPath, std::unordered_map<int, PrimitiveMesh>& outMeshMap, SceneProgressFn onProgress = nullptr);

    bool InvalidateCache(const std::string& binPath);

    bool RebuildCache(Scene& scene, const std::string& levelPath, const std::string& binPath, SceneProgressFn onProgress = nullptr);

    const CacheProfilingStats& GetLastProfilingStats() const { return m_lastStats; }

    SceneLoadState GetLoadState() const { return m_loadState; }
    void SetLoadState(SceneLoadState state) { m_loadState = state; }

private:
    SceneCacheManager() = default;

    mutable CacheProfilingStats m_lastStats;
    SceneLoadState m_loadState = SceneLoadState::Unloaded;

    bool WriteMetaFile(const std::string& metaPath, const SceneCacheHeader& header, const std::vector<AssetDependencyInfo>& deps);
    bool ReadMetaFile(const std::string& metaPath, SceneCacheHeader& outHeader, std::vector<AssetDependencyInfo>& outDeps) const;
};

} // namespace Eunoia
