#pragma once
// ============================================================================
// Eunoia Engine — Lights-Baking Plugin
// LightBakeTypes.h: Core enums, metadata, and asset structures for baking
// ============================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>

namespace Eunoia {

enum class LightBakeState {
    Realtime = 0,
    Baking   = 1,
    Baked    = 2
};

inline const char* GetLightBakeStateName(LightBakeState state) {
    switch (state) {
        case LightBakeState::Realtime: return "Realtime";
        case LightBakeState::Baking:   return "Baking...";
        case LightBakeState::Baked:    return "Baked";
        default:                       return "Unknown";
    }
}

struct BakedLightMetadata {
    std::string lightGuid;
    uint32_t resolution = 1024;
    std::string lightMapAsset;
    std::string shadowMapAsset;
    glm::vec3 bakedPosition{0.0f};
    glm::vec3 bakedRotation{0.0f};
    float bakedIntensity = 2.0f;
    glm::vec3 bakedColor{1.0f, 1.0f, 1.0f};
    float bakedRange = 10.0f;
    float bakedInnerCone = 20.0f;
    float bakedOuterCone = 45.0f;
    float bakedShadowBias = 0.0012f;
    uint64_t bakeSignature = 0;
    std::string bakeDate;
};

struct BakeStaticMesh {
    int objectId = -1;
    std::string name;
    glm::mat4 worldTransform{1.0f};
    std::vector<glm::vec3> worldVertices;
    std::vector<glm::vec3> worldNormals;
    std::vector<glm::vec2> uvs;
    std::vector<uint32_t> indices;
    glm::vec3 aabbMin{1e9f};
    glm::vec3 aabbMax{-1e9f};
};

struct RuntimeBakedLightData {
    std::string lightGuid;
    uint32_t resolution = 1024;
    std::filesystem::path lightMapPath;
    std::filesystem::path shadowMapPath;
    std::vector<uint8_t> lightMapPixels;
    std::vector<uint8_t> shadowMapPixels;
    // Map from objectId -> per-vertex baked irradiance color (RGB float)
    std::unordered_map<int, std::vector<glm::vec3>> staticMeshVertexLighting;
};

} // namespace Eunoia
