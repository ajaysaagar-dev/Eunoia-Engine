#include <EngineScene/SceneCacheManager.h>
#include <EngineCore/EngineLogger.h>
#include <EngineAssets/AssetRegistry.h>
#include <EngineAssets/TextureManager.h>
#include <EngineAssets/MeshImporter.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <windows.h>

namespace Eunoia {

// RAII wrapper for Windows Memory Mapped File access
class MappedFileView {
public:
    MappedFileView() = default;
    ~MappedFileView() { Close(); }

    MappedFileView(const MappedFileView&) = delete;
    MappedFileView& operator=(const MappedFileView&) = delete;

    bool Open(const std::wstring& path) {
        Close();

        m_file = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr
        );

        if (m_file == INVALID_HANDLE_VALUE) {
            return false;
        }

        LARGE_INTEGER size;
        if (!GetFileSizeEx(m_file, &size) || size.QuadPart == 0) {
            Close();
            return false;
        }
        m_size = static_cast<size_t>(size.QuadPart);

        m_mapping = CreateFileMappingW(
            m_file,
            nullptr,
            PAGE_READONLY,
            0,
            0,
            nullptr
        );

        if (!m_mapping) {
            Close();
            return false;
        }

        m_data = static_cast<const uint8_t*>(MapViewOfFile(
            m_mapping,
            FILE_MAP_READ,
            0,
            0,
            0
        ));

        if (!m_data) {
            Close();
            return false;
        }

        return true;
    }

    void Close() {
        if (m_data) {
            UnmapViewOfFile(m_data);
            m_data = nullptr;
        }
        if (m_mapping) {
            CloseHandle(m_mapping);
            m_mapping = NULL;
        }
        if (m_file != INVALID_HANDLE_VALUE) {
            CloseHandle(m_file);
            m_file = INVALID_HANDLE_VALUE;
        }
        m_size = 0;
    }

    const uint8_t* Data() const { return m_data; }
    size_t Size() const { return m_size; }
    bool IsValid() const { return m_data != nullptr; }

private:
    HANDLE m_file = INVALID_HANDLE_VALUE;
    HANDLE m_mapping = NULL;
    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
};

// Fast 64-bit FNV-1a Hash
static inline uint64_t FNV1a64(const void* data, size_t size, uint64_t seed = 14695981039346656037ULL) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    uint64_t hash = seed;
    for (size_t i = 0; i < size; ++i) {
        hash ^= static_cast<uint64_t>(ptr[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static inline uint64_t HashString(const std::string& str, uint64_t seed = 14695981039346656037ULL) {
    return FNV1a64(str.data(), str.size(), seed);
}

uint64_t SceneCacheManager::ComputeFileHash(const std::filesystem::path& path) const {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || std::filesystem::is_directory(path, ec)) {
        return 0;
    }

    uint64_t fileSize = std::filesystem::file_size(path, ec);
    auto lastTime = std::filesystem::last_write_time(path, ec).time_since_epoch().count();

    uint64_t hash = 14695981039346656037ULL;
    hash = FNV1a64(&fileSize, sizeof(fileSize), hash);
    hash = FNV1a64(&lastTime, sizeof(lastTime), hash);

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return hash;
    }

    char buffer[65536];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        hash = FNV1a64(buffer, static_cast<size_t>(file.gcount()), hash);
    }
    return hash;
}

uint64_t SceneCacheManager::GetSceneHash(const Scene& scene) const {
    uint64_t hash = 14695981039346656037ULL;
    size_t objCount = scene.objects.size();
    hash = FNV1a64(&objCount, sizeof(objCount), hash);

    for (const auto& obj : scene.objects) {
        hash = FNV1a64(&obj.id, sizeof(obj.id), hash);
        hash = HashString(obj.name, hash);
        int typeVal = static_cast<int>(obj.type);
        hash = FNV1a64(&typeVal, sizeof(typeVal), hash);
        hash = FNV1a64(&obj.position, sizeof(obj.position), hash);
        hash = FNV1a64(&obj.rotation, sizeof(obj.rotation), hash);
        hash = FNV1a64(&obj.scale, sizeof(obj.scale), hash);
        hash = HashString(obj.meshFilePath, hash);
        hash = FNV1a64(&obj.submeshIndex, sizeof(obj.submeshIndex), hash);
        hash = HashString(obj.materialName, hash);
        hash = HashString(obj.baseColorTexture, hash);
        hash = HashString(obj.normalTexture, hash);
        hash = HashString(obj.roughnessTexture, hash);
        hash = HashString(obj.metallicTexture, hash);
        hash = HashString(obj.aoTexture, hash);
        hash = HashString(obj.emissionTexture, hash);
        hash = HashString(obj.opacityTexture, hash);
        hash = FNV1a64(&obj.metallic, sizeof(obj.metallic), hash);
        hash = FNV1a64(&obj.roughness, sizeof(obj.roughness), hash);
    }

    size_t lightCount = scene.pointLights.size();
    hash = FNV1a64(&lightCount, sizeof(lightCount), hash);
    for (const auto& light : scene.pointLights) {
        hash = FNV1a64(&light.id, sizeof(light.id), hash);
        hash = FNV1a64(&light.position, sizeof(light.position), hash);
        hash = FNV1a64(&light.color, sizeof(light.color), hash);
        hash = FNV1a64(&light.intensity, sizeof(light.intensity), hash);
    }

    hash = FNV1a64(&scene.lightDirection, sizeof(scene.lightDirection), hash);
    hash = FNV1a64(&scene.lightIntensity, sizeof(scene.lightIntensity), hash);
    hash = HashString(scene.skyboxTexturePath, hash);

    return hash;
}

void SceneCacheManager::CollectAssetDependencies(const Scene& scene, std::vector<AssetDependencyInfo>& outDeps) const {
    outDeps.clear();
    std::unordered_set<std::string> seenPaths;

    for (const auto& obj : scene.objects) {
        // Collect mesh dependency
        if (obj.isImportedMesh && !obj.meshFilePath.empty()) {
            std::string meshKey = obj.meshFilePath;
            if (seenPaths.find(meshKey) == seenPaths.end()) {
                seenPaths.insert(meshKey);
                AssetDependencyInfo dep;
                dep.type = AssetType::Mesh;
                dep.path = meshKey;
                dep.sourceHash = ComputeFileHash(meshKey);
                std::error_code ec;
                if (std::filesystem::exists(meshKey, ec)) {
                    dep.fileSize = std::filesystem::file_size(meshKey, ec);
                    dep.timestamp = std::filesystem::last_write_time(meshKey, ec).time_since_epoch().count();
                }
                const auto* meta = AssetRegistry::Get().FindByPath(meshKey);
                if (meta) dep.id = meta->id;
                else dep.id = AssetID::CreateRandom();
                dep.priority = obj.visible ? AssetPriority::High : AssetPriority::Normal;
                outDeps.push_back(dep);
            }
        }

        // Helper for textures
        auto checkTex = [&](const std::string& path, AssetPriority prio) {
            if (path.empty() || path == "none") return;
            std::string resolved = TextureManager::Get().ResolvePath(path);
            if (resolved.empty()) resolved = path;
            if (seenPaths.find(resolved) == seenPaths.end()) {
                seenPaths.insert(resolved);
                AssetDependencyInfo dep;
                dep.type = AssetType::Texture;
                dep.path = resolved;
                dep.sourceHash = ComputeFileHash(resolved);
                std::error_code ec;
                if (std::filesystem::exists(resolved, ec)) {
                    dep.fileSize = std::filesystem::file_size(resolved, ec);
                    dep.timestamp = std::filesystem::last_write_time(resolved, ec).time_since_epoch().count();
                }
                const auto* meta = AssetRegistry::Get().FindByPath(resolved);
                if (meta) dep.id = meta->id;
                else dep.id = AssetID::CreateRandom();
                dep.priority = prio;
                outDeps.push_back(dep);
            }
        };

        checkTex(obj.baseColorTexture, AssetPriority::High);
        checkTex(obj.normalTexture, AssetPriority::Normal);
        checkTex(obj.roughnessTexture, AssetPriority::Normal);
        checkTex(obj.metallicTexture, AssetPriority::Normal);
        checkTex(obj.aoTexture, AssetPriority::Low);
        checkTex(obj.emissionTexture, AssetPriority::Low);
        checkTex(obj.opacityTexture, AssetPriority::Low);
    }

    if (scene.enableSkybox && !scene.skyboxTexturePath.empty()) {
        std::string resolved = TextureManager::Get().ResolvePath(scene.skyboxTexturePath);
        if (resolved.empty()) resolved = scene.skyboxTexturePath;
        if (seenPaths.find(resolved) == seenPaths.end()) {
            seenPaths.insert(resolved);
            AssetDependencyInfo dep;
            dep.type = AssetType::Skybox;
            dep.path = resolved;
            dep.sourceHash = ComputeFileHash(resolved);
            dep.priority = AssetPriority::High;
            outDeps.push_back(dep);
        }
    }
}

bool SceneCacheManager::WriteMetaFile(const std::string& metaPath, const SceneCacheHeader& header, const std::vector<AssetDependencyInfo>& deps) {
    std::ofstream out(metaPath);
    if (!out.is_open()) return false;

    out << "{\n";
    out << "  \"magic\": \"ENCB_META\",\n";
    out << "  \"version\": " << header.version << ",\n";
    out << "  \"sceneHash\": " << header.sceneHash << ",\n";
    out << "  \"assetDatabaseHash\": " << header.assetDatabaseHash << ",\n";
    out << "  \"entityCount\": " << header.entityCount << ",\n";
    out << "  \"meshCount\": " << header.meshCount << ",\n";
    out << "  \"textureCount\": " << header.textureCount << ",\n";
    out << "  \"assets\": [\n";

    for (size_t i = 0; i < deps.size(); ++i) {
        const auto& d = deps[i];
        out << "    {\n";
        out << "      \"id\": \"" << d.id.ToString() << "\",\n";
        out << "      \"type\": \"" << AssetTypeToString(d.type) << "\",\n";
        out << "      \"path\": \"" << d.path << "\",\n";
        out << "      \"sourceHash\": " << d.sourceHash << ",\n";
        out << "      \"fileSize\": " << d.fileSize << ",\n";
        out << "      \"timestamp\": " << d.timestamp << ",\n";
        out << "      \"priority\": " << static_cast<int>(d.priority) << "\n";
        out << "    }" << (i + 1 < deps.size() ? ",\n" : "\n");
    }

    out << "  ]\n";
    out << "}\n";
    return true;
}

bool SceneCacheManager::ReadMetaFile(const std::string& metaPath, SceneCacheHeader& outHeader, std::vector<AssetDependencyInfo>& outDeps) const {
    outDeps.clear();
    std::ifstream in(metaPath);
    if (!in.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    if (content.find("\"magic\": \"ENCB_META\"") == std::string::npos) {
        return false;
    }

    auto parseUint64 = [&](const std::string& key) -> uint64_t {
        size_t pos = content.find("\"" + key + "\":");
        if (pos == std::string::npos) return 0;
        pos = content.find_first_not_of(" \t\r\n", pos + key.length() + 3);
        if (pos == std::string::npos) return 0;
        try {
            return std::stoull(content.substr(pos));
        } catch (...) { return 0; }
    };

    auto parseUint32 = [&](const std::string& key) -> uint32_t {
        return static_cast<uint32_t>(parseUint64(key));
    };

    outHeader.version = parseUint32("version");
    outHeader.sceneHash = parseUint64("sceneHash");
    outHeader.assetDatabaseHash = parseUint64("assetDatabaseHash");
    outHeader.entityCount = parseUint32("entityCount");
    outHeader.meshCount = parseUint32("meshCount");
    outHeader.textureCount = parseUint32("textureCount");

    // Parse simple assets array
    size_t arrPos = content.find("\"assets\":");
    if (arrPos != std::string::npos) {
        size_t cur = content.find('{', arrPos);
        while (cur != std::string::npos) {
            size_t endObj = content.find('}', cur);
            if (endObj == std::string::npos) break;
            std::string block = content.substr(cur, endObj - cur + 1);

            AssetDependencyInfo dep;
            size_t p = block.find("\"path\": \"");
            if (p != std::string::npos) {
                p += 9;
                size_t pEnd = block.find('"', p);
                if (pEnd != std::string::npos) dep.path = block.substr(p, pEnd - p);
            }

            p = block.find("\"sourceHash\":");
            if (p != std::string::npos) {
                p = block.find_first_not_of(" \t\r\n", p + 13);
                try { dep.sourceHash = std::stoull(block.substr(p)); } catch (...) {}
            }

            p = block.find("\"timestamp\":");
            if (p != std::string::npos) {
                p = block.find_first_not_of(" \t\r\n", p + 12);
                try { dep.timestamp = std::stoull(block.substr(p)); } catch (...) {}
            }

            p = block.find("\"type\": \"");
            if (p != std::string::npos) {
                p += 9;
                size_t pEnd = block.find('"', p);
                if (pEnd != std::string::npos) dep.type = StringToAssetType(block.substr(p, pEnd - p));
            }

            if (!dep.path.empty()) {
                outDeps.push_back(dep);
            }
            cur = content.find('{', endObj);
        }
    }

    return true;
}

CacheValidationStatus SceneCacheManager::ValidateCache(const std::string& binPath, const Scene* scene, std::string* outReason) const {
    auto tStart = std::chrono::high_resolution_clock::now();

    if (binPath.empty() || !std::filesystem::exists(binPath)) {
        if (outReason) *outReason = "Binary cache file does not exist";
        return CacheValidationStatus::Missing;
    }

    std::ifstream in(binPath, std::ios::binary);
    if (!in.is_open()) {
        if (outReason) *outReason = "Failed to open binary cache file";
        return CacheValidationStatus::Corrupt;
    }

    SceneCacheHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    in.close();

    if (header.magic != SCENE_CACHE_MAGIC) {
        if (outReason) *outReason = "Invalid cache magic header";
        return CacheValidationStatus::Corrupt;
    }

    if (header.version != SCENE_CACHE_VERSION) {
        if (outReason) *outReason = "Cache version mismatch (got " + std::to_string(header.version) + ", expected " + std::to_string(SCENE_CACHE_VERSION) + ")";
        return CacheValidationStatus::VersionMismatch;
    }

    if (scene && header.sceneHash != 0) {
        uint64_t currentSceneHash = GetSceneHash(*scene);
        if (currentSceneHash != header.sceneHash) {
            if (outReason) *outReason = "Scene hierarchy or configuration has changed";
            return CacheValidationStatus::SceneModified;
        }
    }

    // Check .bin.meta sidecar
    std::string metaPath = binPath + ".meta";
    if (std::filesystem::exists(metaPath)) {
        SceneCacheHeader metaHeader{};
        std::vector<AssetDependencyInfo> metaDeps;
        if (ReadMetaFile(metaPath, metaHeader, metaDeps)) {
            for (const auto& dep : metaDeps) {
                if (dep.path.empty()) continue;
                std::error_code ec;
                if (!std::filesystem::exists(dep.path, ec)) {
                    // Source asset might be at external/unmounted dev drive (e.g. D:\xnormal\...);
                    // the baked binary cache already contains full geometry, do not invalidate
                    continue;
                }
                auto lastTime = std::filesystem::last_write_time(dep.path, ec).time_since_epoch().count();
                if (dep.timestamp != 0 && static_cast<uint64_t>(lastTime) != dep.timestamp) {
                    // Check if content hash actually changed
                    uint64_t currentHash = ComputeFileHash(dep.path);
                    if (currentHash != dep.sourceHash) {
                        if (outReason) *outReason = "Source asset content modified: " + dep.path;
                        return CacheValidationStatus::SourceAssetModified;
                    }
                }
            }
        }
    }

    auto tEnd = std::chrono::high_resolution_clock::now();
    m_lastStats.validationTimeMs = std::chrono::duration<float, std::milli>(tEnd - tStart).count();

    if (outReason) *outReason = "Cache is valid and fully synchronized";
    return CacheValidationStatus::Valid;
}

bool SceneCacheManager::GenerateCache(const Scene& scene, const std::string& binPath, SceneProgressFn onProgress) {
    if (binPath.empty()) return false;

    auto tStart = std::chrono::high_resolution_clock::now();
    m_loadState = SceneLoadState::PreparingAssets;

    if (onProgress) onProgress(0.05f, "Preparing Scene Cache", "Deduplicating scene assets...");

    // 1. Collect and deduplicate unique meshes
    struct UniqueMesh {
        std::string identifier;
        PrimitiveMesh mesh;
        CachedMeshRecord record;
    };

    std::vector<UniqueMesh> uniqueMeshes;
    std::unordered_map<std::string, uint32_t> meshIndexMap;
    std::vector<CachedEntityRecord> entityRecords;

    entityRecords.reserve(scene.objects.size());

    for (const auto& obj : scene.objects) {
        CachedEntityRecord ent;
        ent.actorId = obj.id;
        ent.isImportedMesh = obj.isImportedMesh ? 1 : 0;
        ent.priority = static_cast<uint8_t>(obj.visible ? AssetPriority::Normal : AssetPriority::Low);

        if (!obj.mesh.vertices.empty()) {
            std::string meshKey;
            if (obj.isImportedMesh && !obj.meshFilePath.empty()) {
                meshKey = obj.meshFilePath + "#" + std::to_string(obj.submeshIndex);
            } else {
                meshKey = "Primitive_" + std::to_string(static_cast<int>(obj.type)) + "_" +
                          std::to_string(obj.mesh.vertices.size()) + "_" +
                          std::to_string(obj.mesh.indices.size());
            }

            auto it = meshIndexMap.find(meshKey);
            if (it == meshIndexMap.end()) {
                uint32_t newIdx = static_cast<uint32_t>(uniqueMeshes.size());
                meshIndexMap[meshKey] = newIdx;

                UniqueMesh um;
                um.identifier = meshKey;
                um.mesh = obj.mesh;
                um.mesh.CalculateAABB();

                um.record.vertexCount = static_cast<uint32_t>(obj.mesh.vertices.size());
                um.record.indexCount = static_cast<uint32_t>(obj.mesh.indices.size());
                um.record.aabbMin = um.mesh.aabbMin;
                um.record.aabbMax = um.mesh.aabbMax;
                um.record.priority = ent.priority;

                if (obj.isImportedMesh && !obj.meshFilePath.empty()) {
                    snprintf(um.record.path, sizeof(um.record.path), "%s", obj.meshFilePath.c_str());
                    um.record.sourceHash = ComputeFileHash(obj.meshFilePath);
                } else {
                    snprintf(um.record.path, sizeof(um.record.path), "%s", meshKey.c_str());
                    um.record.sourceHash = HashString(meshKey);
                }

                uniqueMeshes.push_back(std::move(um));
                ent.meshIndex = newIdx;
            } else {
                ent.meshIndex = it->second;
            }
        }

        entityRecords.push_back(ent);
    }

    if (onProgress) onProgress(0.35f, "Writing Cache", "Serializing cooked mesh streams...");

    // 2. Collect dependencies for sidecar
    std::vector<AssetDependencyInfo> dependencies;
    CollectAssetDependencies(scene, dependencies);

    // 3. Prepare Header and calculate offsets
    SceneCacheHeader header{};
    header.magic = SCENE_CACHE_MAGIC;
    header.version = SCENE_CACHE_VERSION;
    header.sceneHash = GetSceneHash(scene);

    uint64_t dbHash = 14695981039346656037ULL;
    for (const auto& dep : dependencies) {
        dbHash = FNV1a64(&dep.sourceHash, sizeof(dep.sourceHash), dbHash);
    }
    header.assetDatabaseHash = dbHash;

    header.entityCount = static_cast<uint32_t>(entityRecords.size());
    header.meshCount = static_cast<uint32_t>(uniqueMeshes.size());
    header.textureCount = 0;
    header.materialCount = 0;
    header.shaderCount = 0;

    uint64_t curOffset = sizeof(SceneCacheHeader);
    header.entityDataOffset = curOffset;
    curOffset += header.entityCount * sizeof(CachedEntityRecord);

    header.meshIndexOffset = curOffset;
    curOffset += header.meshCount * sizeof(CachedMeshRecord);

    header.textureIndexOffset = curOffset;
    header.materialIndexOffset = curOffset;
    header.assetDataOffset = curOffset;

    // Calculate per-mesh data offsets
    uint64_t assetDataCursor = header.assetDataOffset;
    for (auto& um : uniqueMeshes) {
        um.record.vertexDataOffset = assetDataCursor;
        assetDataCursor += um.record.vertexCount * sizeof(MeshVertex);

        um.record.indexDataOffset = assetDataCursor;
        assetDataCursor += um.record.indexCount * sizeof(uint32_t);
    }

    // 4. Atomic Write: write to .bin.tmp first
    std::string tmpBinPath = binPath + ".tmp";
    std::ofstream outBin(tmpBinPath, std::ios::binary);
    if (!outBin.is_open()) {
        EngineLogger::Get().LogError("SceneCacheManager", 0x101, "Failed to open temporary cache file for writing", tmpBinPath);
        return false;
    }

    // Write header
    outBin.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Write entity records
    if (!entityRecords.empty()) {
        outBin.write(reinterpret_cast<const char*>(entityRecords.data()), entityRecords.size() * sizeof(CachedEntityRecord));
    }

    // Write mesh index records
    for (const auto& um : uniqueMeshes) {
        outBin.write(reinterpret_cast<const char*>(&um.record), sizeof(CachedMeshRecord));
    }

    // Write mesh asset data (vertices & indices)
    for (const auto& um : uniqueMeshes) {
        if (!um.mesh.vertices.empty()) {
            outBin.write(reinterpret_cast<const char*>(um.mesh.vertices.data()), um.mesh.vertices.size() * sizeof(MeshVertex));
        }
        if (!um.mesh.indices.empty()) {
            outBin.write(reinterpret_cast<const char*>(um.mesh.indices.data()), um.mesh.indices.size() * sizeof(uint32_t));
        }
    }

    outBin.flush();
    outBin.close();

    // 5. Write .bin.meta.tmp
    std::string tmpMetaPath = binPath + ".meta.tmp";
    WriteMetaFile(tmpMetaPath, header, dependencies);

    if (onProgress) onProgress(0.85f, "Finalizing Cache", "Committing atomic cache files...");

    // 6. Atomic Commit: rename .tmp -> target files
    std::error_code ec;
    std::string finalMetaPath = binPath + ".meta";

    if (std::filesystem::exists(binPath, ec)) {
        std::filesystem::remove(binPath, ec);
    }
    std::filesystem::rename(tmpBinPath, binPath, ec);
    if (ec) {
        EngineLogger::Get().LogError("SceneCacheManager", 0x102, "Failed to rename tmp cache file", ec.message());
        return false;
    }

    if (std::filesystem::exists(finalMetaPath, ec)) {
        std::filesystem::remove(finalMetaPath, ec);
    }
    std::filesystem::rename(tmpMetaPath, finalMetaPath, ec);

    auto tEnd = std::chrono::high_resolution_clock::now();
    float elapsedMs = std::chrono::duration<float, std::milli>(tEnd - tStart).count();

    m_lastStats.cacheSizeBytes = std::filesystem::file_size(binPath, ec);
    m_lastStats.uniqueMeshesLoaded = uniqueMeshes.size();

    EngineLogger::Get().LogAction(
        "SCENE_CACHE_GENERATED",
        binPath,
        "Generated binary cache with " + std::to_string(uniqueMeshes.size()) + " deduplicated meshes (" +
        std::to_string(m_lastStats.cacheSizeBytes / 1024) + " KB in " + std::to_string(elapsedMs) + "ms)"
    );

    if (onProgress) onProgress(1.0f, "Cache Generated", "Ready");
    m_loadState = SceneLoadState::Ready;
    return true;
}

bool SceneCacheManager::LoadCache(Scene& scene, const std::string& binPath, std::unordered_map<int, PrimitiveMesh>& outMeshMap, SceneProgressFn onProgress) {
    outMeshMap.clear();
    if (binPath.empty()) return false;

    auto tStart = std::chrono::high_resolution_clock::now();
    m_loadState = SceneLoadState::ReadingCache;

    if (onProgress) onProgress(0.05f, "Validating Cache", "Validating binary format and dependencies...");

    std::string reason;
    CacheValidationStatus status = ValidateCache(binPath, nullptr, &reason);
    if (status != CacheValidationStatus::Valid) {
        EngineLogger::Get().LogAction("SCENE_CACHE_INVALID", binPath, reason);
        return false;
    }

    if (onProgress) onProgress(0.20f, "Memory-Mapping Cache", "Mapping binary stream into address space...");

    auto tIoStart = std::chrono::high_resolution_clock::now();

    // Memory-map the file
    std::wstring wpath(binPath.begin(), binPath.end());
    MappedFileView mappedFile;
    if (!mappedFile.Open(wpath)) {
        EngineLogger::Get().LogError("SceneCacheManager", 0x103, "Memory-mapping failed for binary cache", binPath);
        return false;
    }

    auto tIoEnd = std::chrono::high_resolution_clock::now();
    m_lastStats.diskIoTimeMs = std::chrono::duration<float, std::milli>(tIoEnd - tIoStart).count();

    const uint8_t* rawData = mappedFile.Data();
    size_t fileSize = mappedFile.Size();

    if (fileSize < sizeof(SceneCacheHeader)) {
        return false;
    }

    const auto* header = reinterpret_cast<const SceneCacheHeader*>(rawData);
    if (header->magic != SCENE_CACHE_MAGIC || header->version != SCENE_CACHE_VERSION) {
        return false;
    }

    m_loadState = SceneLoadState::PreparingAssets;
    if (onProgress) onProgress(0.40f, "Loading Deduplicated Meshes", "Unpacking geometry buffers...");

    auto tMeshStart = std::chrono::high_resolution_clock::now();

    // Read mesh index records
    const auto* meshRecords = reinterpret_cast<const CachedMeshRecord*>(rawData + header->meshIndexOffset);
    std::vector<PrimitiveMesh> deduplicatedMeshes;
    deduplicatedMeshes.resize(header->meshCount);

    for (uint32_t m = 0; m < header->meshCount; ++m) {
        const auto& rec = meshRecords[m];
        PrimitiveMesh& pm = deduplicatedMeshes[m];
        pm.aabbMin = rec.aabbMin;
        pm.aabbMax = rec.aabbMax;
        pm.aabbValid = true;

        if (rec.vertexCount > 0 && rec.vertexDataOffset + rec.vertexCount * sizeof(MeshVertex) <= fileSize) {
            pm.vertices.resize(rec.vertexCount);
            memcpy(pm.vertices.data(), rawData + rec.vertexDataOffset, rec.vertexCount * sizeof(MeshVertex));
        }

        if (rec.indexCount > 0 && rec.indexDataOffset + rec.indexCount * sizeof(uint32_t) <= fileSize) {
            pm.indices.resize(rec.indexCount);
            memcpy(pm.indices.data(), rawData + rec.indexDataOffset, rec.indexCount * sizeof(uint32_t));
        }
    }

    // Read entity records and link meshes to actors
    const auto* entityRecords = reinterpret_cast<const CachedEntityRecord*>(rawData + header->entityDataOffset);
    for (uint32_t e = 0; e < header->entityCount; ++e) {
        const auto& ent = entityRecords[e];
        if (ent.meshIndex < deduplicatedMeshes.size()) {
            outMeshMap[ent.actorId] = deduplicatedMeshes[ent.meshIndex];
        }
    }

    auto tMeshEnd = std::chrono::high_resolution_clock::now();
    m_lastStats.meshProcessTimeMs = std::chrono::duration<float, std::milli>(tMeshEnd - tMeshStart).count();

    auto tEnd = std::chrono::high_resolution_clock::now();
    m_lastStats.totalLoadTimeMs = std::chrono::duration<float, std::milli>(tEnd - tStart).count();
    m_lastStats.uniqueMeshesLoaded = header->meshCount;
    m_lastStats.cacheSizeBytes = fileSize;

    EngineLogger::Get().LogAction(
        "SCENE_CACHE_LOADED",
        binPath,
        "Loaded " + std::to_string(header->entityCount) + " actors (" +
        std::to_string(header->meshCount) + " deduplicated meshes) in " +
        std::to_string(m_lastStats.totalLoadTimeMs) + "ms"
    );

    if (onProgress) onProgress(0.80f, "Mesh Preparation Done", "Ready for GPU resource dispatch");
    m_loadState = SceneLoadState::UploadingGPUResources;
    return true;
}

bool SceneCacheManager::InvalidateCache(const std::string& binPath) {
    if (binPath.empty()) return false;
    std::error_code ec;
    std::filesystem::remove(binPath, ec);
    std::filesystem::remove(binPath + ".meta", ec);
    std::filesystem::remove(binPath + ".tmp", ec);
    std::filesystem::remove(binPath + ".meta.tmp", ec);
    EngineLogger::Get().LogAction("SCENE_CACHE_INVALIDATED", binPath, "Cache deleted");
    return true;
}

bool SceneCacheManager::RebuildCache(Scene& scene, const std::string& levelPath, const std::string& binPath, SceneProgressFn onProgress) {
    InvalidateCache(binPath);
    return GenerateCache(scene, binPath, onProgress);
}

} // namespace Eunoia
