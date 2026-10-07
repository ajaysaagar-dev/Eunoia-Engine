#pragma once
// ============================================================================
// Eunoia Engine — Lights-Baking Plugin
// LightBakeManager.h: Core coordinator for individual light baking
// ============================================================================

#include <LightsBaking/LightBakeTypes.h>
#include <LightsBaking/ILightBakeSystem.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <memory>
#include <functional>
#include <glm/glm.hpp>

class Scene;
struct GameObject;
struct PointLight;

namespace Eunoia {

class LightBakeManager : public ILightBakeSystem {
public:
    static LightBakeManager& Get();

    // ILightBakeSystem overrides
    uint32_t GetBakedLightCount() const override;
    bool HasBakedData(const std::string& lightGuid) const override;

    // Validation
    bool ValidateLight(const GameObject& obj, std::string& outError) const;

    // Geometry collection
    bool CollectBakeGeometry(const Scene& scene, std::vector<BakeStaticMesh>& outStaticMeshes, std::string& outError) const;

    // Core Bake operations (Section 1, 8, 16, 17 of dev.md)
    bool BakeLight(Scene& scene, GameObject& lightObj, const std::filesystem::path& projectRoot, std::string& outStatus);
    bool ClearBake(Scene& scene, GameObject& lightObj, const std::filesystem::path& projectRoot);

    // Stale / Dirty detection (Section 15 of dev.md)
    bool IsBakeValid(const GameObject& lightObj) const;
    void CheckBakeDirty(GameObject& lightObj) const;
    uint64_t ComputeBakeSignature(const GameObject& lightObj) const;

    // Asset paths & Cooked folder integration (dev.md & user prompt)
    std::filesystem::path GetCookedBakeDir(const std::filesystem::path& projectRoot, const std::string& lightGuid) const;
    bool SaveBakeAssets(
        const std::filesystem::path& bakeDir,
        const BakedLightMetadata& meta,
        const std::vector<glm::vec3>& lightMapHdr,
        const std::vector<float>& shadowMapHdr,
        const std::vector<MeshAtlasRect>& atlasRects,
        uint32_t res
    );

    // Persistence & Scene reloading (Section 18 of dev.md)
    bool LoadBakeForLight(GameObject& lightObj, const std::filesystem::path& projectRoot);
    void LoadAllBakesForScene(Scene& scene, const std::filesystem::path& projectRoot);

    // Asset cleanup on light deletion (Section 19 of dev.md)
    void OnLightDeleted(const std::string& lightGuid);

    // Runtime queries for static geometry vertex lighting
    const std::vector<glm::vec3>* GetStaticMeshVertexLighting(int objectId) const;
    glm::vec3 GetStaticVertexBakedLight(int objectId, size_t vertexIndex) const;
    bool HasStaticBakedLighting(int objectId) const;

    // Runtime queries for lightmap UVs & active baked lightmap texture
    bool HasMeshLightmapUV(int objectId) const;
    glm::vec2 GetMeshLightmapUV(int objectId, size_t vertexIndex) const;
    bool HasActiveLightmap() const;
    std::string GetActiveLightmapTexturePath() const;
    uint64_t GetLightmapRevision() const { return m_lightmapRevision; }

    // Progress / logging callback
    void SetProgressCallback(std::function<void(const std::string&, float)> cb) { m_progressCb = cb; }

private:
    LightBakeManager() = default;
    ~LightBakeManager() override = default;

    // Internal baking routines
    void GenerateMeshLightmapUvs(
        const std::vector<BakeStaticMesh>& staticMeshes,
        RuntimeBakedLightData& outData
    );

    void RaycastShadowsAndIllumination(
        const Scene& scene,
        const GameObject& lightObj,
        const std::vector<BakeStaticMesh>& staticMeshes,
        RuntimeBakedLightData& outData,
        std::vector<glm::vec3>& outLightMapHdr,
        std::vector<float>& outShadowMapHdr,
        uint32_t resolution
    );

    std::unordered_map<std::string, RuntimeBakedLightData> m_bakedLights; // Key: LightGUID
    // Combined accumulated lighting for static objects: objectId -> vector<glm::vec3>
    std::unordered_map<int, std::vector<glm::vec3>> m_accumulatedStaticLighting;
    std::unordered_map<int, std::vector<glm::vec2>> m_accumulatedMeshLightmapUvs;
    std::string m_activeLightmapPath;

    void RecomputeAccumulatedStaticLighting(const std::filesystem::path& projectRoot = {});

    std::function<void(const std::string&, float)> m_progressCb;
    uint64_t m_lightmapRevision = 1;
};

} // namespace Eunoia
