#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <utility>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <EngineScene/GameObject.h>
#include <EngineAssets/Geometry.h>
#include <EngineAssets/TextureManager.h>
#include <EngineAssets/MeshImporter.h>
#include <filesystem>
#include <EngineScene/BehaviourRegistry.h>
#ifdef GetClassName
#undef GetClassName
#endif

void AddEngineLog(const std::string& category, const std::string& message, int level);

struct PointLight {
    int id = 0;
    std::string name = "Point Light";
    LightType type = LightType::Point;
    glm::vec3 position{0.0f, 2.5f, 0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    glm::vec3 color{1.0f, 0.95f, 0.85f};
    float intensity = 2.0f;
    float range = 10.0f;
    float attenuation = 2.0f;
    bool enabled = true;
    bool castShadows = true;
    float shadowStrength = 0.85f;
    float shadowBias = 0.0012f;
    int shadowResolution = 1024;

    // Spot Light
    float innerConeAngle = 20.0f;
    float outerConeAngle = 45.0f;
    float coneFalloff = 1.0f;

    // Area Light
    int areaShape = 0;
    float width = 1.0f;
    float height = 1.0f;
    float radius = 0.5f;
    bool twoSided = false;
};

class Scene {
public:
    std::vector<GameObject> objects;
    int nextId = 1;
    int selectedId = -1;
    int activeLevelCameraId = -1;
    bool isInteracting = false;
    std::function<void(const std::string&)> onPreChange = nullptr;

    // Environment & Directional Sun Light
    glm::vec3 lightDirection = glm::normalize(glm::vec3(0.6f, 1.0f, 0.8f));
    glm::vec3 lightColor{1.0f, 0.98f, 0.92f};
    float lightIntensity = 1.0f;
    float ambientIntensity = 0.0f;
    glm::vec3 ambientColor{0.15f, 0.15f, 0.18f};
    glm::vec4 clearColor{0.07f, 0.07f, 0.08f, 1.0f};

    // Skybox Environment Settings
    bool enableSkybox = true;
    std::string skyboxAssetPath = "";
    std::string skyboxTexturePath = "";
    float skyboxExposure = 1.0f;
    float skyboxRotation = 0.0f; // degrees 0..360
    glm::vec3 skyboxTint{1.0f, 1.0f, 1.0f};
    float skyboxAmbientContribution = 0.5f;

    // Skybox Based Lighting Settings (default disabled)
    bool enableSkyboxLighting = false;
    float skyboxLightingIntensity = 1.0f;
    glm::vec3 skyboxLightingColor{1.0f, 1.0f, 1.0f};
    float skyboxLightingSpecular = 1.0f;
    float skyboxLightingDiffuse = 1.0f;

    // Shadow Mapping Parameters (Hardware D32_FLOAT 2048x2048 PCF)
    bool enableShadows = true;
    float shadowStrength = 0.85f;
    float shadowBias = 0.0012f;
    int shadowResolution = 2048;
    float pcfRadius = 1.2f;
    bool showLightFrustum = false;
    bool hasLightFrustumCorners = false;
    glm::vec3 lightFrustumCorners[8] = {};

    // Multiple Point Lights Support
    std::vector<PointLight> pointLights;

    // Water Bodies Support
    std::vector<WaterBodyComponent> waterBodies;

    // Grid
    bool showGrid = true;
    float gridSize = 16.0f;
    int gridDivisions = 16;
    glm::vec3 gridColor{0.20f, 0.21f, 0.24f};

    // Animation
    bool playAnimations = true;

    Scene() {
        Clear();
    }

    void LoadDefaultScene() {
        Clear();
    }

    GameObject* FindObject(int id) {
        for (auto& obj : objects) {
            if (obj.id == id) return &obj;
        }
        return nullptr;
    }

    const GameObject* FindObject(int id) const {
        for (const auto& obj : objects) {
            if (obj.id == id) return &obj;
        }
        return nullptr;
    }

    const GameObject* FindObjectConst(int id) const {
        return FindObject(id);
    }

    glm::mat4 GetWorldMatrix(const GameObject& obj, int depth = 0) const {
        glm::mat4 local = obj.GetLocalMatrix();
        if (depth > 64) return local;
        if (obj.parentId != -1) {
            const GameObject* parent = FindObjectConst(obj.parentId);
            if (parent && parent->id != obj.id) {
                return GetWorldMatrix(*parent, depth + 1) * local;
            }
        }
        return local;
    }

    glm::mat4 GetWorldMatrix(int objId) const {
        const GameObject* obj = FindObjectConst(objId);
        if (!obj) return glm::mat4(1.0f);
        return GetWorldMatrix(*obj);
    }

    glm::vec3 GetWorldPosition(const GameObject& obj) const {
        return glm::vec3(GetWorldMatrix(obj)[3]);
    }

    glm::vec3 GetWorldPosition(int objId) const {
        const GameObject* obj = FindObjectConst(objId);
        if (!obj) return glm::vec3(0.0f);
        return GetWorldPosition(*obj);
    }

    glm::vec3 GetWorldCenter(const GameObject& obj) const {
        if (obj.mesh.vertices.empty()) return GetWorldPosition(obj);
        glm::vec3 minB(1e9f), maxB(-1e9f);
        bool anyValid = false;
        for (const auto& v : obj.mesh.vertices) {
            if (std::isnan(v.pos.x) || std::isnan(v.pos.y) || std::isnan(v.pos.z) ||
                !std::isfinite(v.pos.x) || !std::isfinite(v.pos.y) || !std::isfinite(v.pos.z)) {
                continue; // skip corrupted vertex rather than let it poison the bounds
            }
            minB = glm::min(minB, v.pos);
            maxB = glm::max(maxB, v.pos);
            anyValid = true;
        }
        if (!anyValid) return GetWorldPosition(obj); // every vertex was bad — fall back to origin/position
        glm::vec3 localCenter = (minB + maxB) * 0.5f;
        glm::mat4 worldMat = GetWorldMatrix(obj);
        glm::vec3 worldCenter = glm::vec3(worldMat * glm::vec4(localCenter, 1.0f));
        if (std::isnan(worldCenter.x) || std::isnan(worldCenter.y) || std::isnan(worldCenter.z) ||
            !std::isfinite(worldCenter.x) || !std::isfinite(worldCenter.y) || !std::isfinite(worldCenter.z)) {
            return GetWorldPosition(obj); // worldMat itself was bad — fall back
        }
        return worldCenter;
    }

    bool GetSceneAABB(glm::vec3& outMin, glm::vec3& outMax) const {
        glm::vec3 minB(1e9f), maxB(-1e9f);
        bool hasValidGeom = false;
        for (const auto& obj : objects) {
            if (!obj.visible || obj.isLight || obj.type == PrimitiveType::Empty) continue;
            if (obj.mesh.vertices.empty()) continue;

            glm::mat4 worldMat = GetWorldMatrix(obj);
            glm::vec3 objMin(1e9f), objMax(-1e9f);
            bool objValid = false;
            for (const auto& v : obj.mesh.vertices) {
                if (std::isnan(v.pos.x) || std::isnan(v.pos.y) || std::isnan(v.pos.z) ||
                    !std::isfinite(v.pos.x) || !std::isfinite(v.pos.y) || !std::isfinite(v.pos.z)) {
                    continue;
                }
                objMin = glm::min(objMin, v.pos);
                objMax = glm::max(objMax, v.pos);
                objValid = true;
            }
            if (!objValid) continue;

            glm::vec3 corners[8] = {
                {objMin.x, objMin.y, objMin.z},
                {objMax.x, objMin.y, objMin.z},
                {objMin.x, objMax.y, objMin.z},
                {objMax.x, objMax.y, objMin.z},
                {objMin.x, objMin.y, objMax.z},
                {objMax.x, objMin.y, objMax.z},
                {objMin.x, objMax.y, objMax.z},
                {objMax.x, objMax.y, objMax.z}
            };
            for (int i = 0; i < 8; ++i) {
                glm::vec4 wp = worldMat * glm::vec4(corners[i], 1.0f);
                if (std::isfinite(wp.x) && std::isfinite(wp.y) && std::isfinite(wp.z)) {
                    minB = glm::min(minB, glm::vec3(wp));
                    maxB = glm::max(maxB, glm::vec3(wp));
                    hasValidGeom = true;
                }
            }
        }
        if (!hasValidGeom) {
            outMin = glm::vec3(-10.0f, -1.0f, -10.0f);
            outMax = glm::vec3(10.0f, 10.0f, 10.0f);
            return false;
        }
        outMin = minB;
        outMax = maxB;
        return true;
    }

    static void DecomposeMatrix(const glm::mat4& m, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale) {
        translation = glm::vec3(m[3]);
        scale.x = glm::length(glm::vec3(m[0]));
        scale.y = glm::length(glm::vec3(m[1]));
        scale.z = glm::length(glm::vec3(m[2]));

        glm::mat3 rotM(1.0f);
        if (scale.x > 1e-6f) rotM[0] = glm::vec3(m[0]) / scale.x;
        if (scale.y > 1e-6f) rotM[1] = glm::vec3(m[1]) / scale.y;
        if (scale.z > 1e-6f) rotM[2] = glm::vec3(m[2]) / scale.z;

        const float RAD2DEG = 180.0f / 3.14159265358979323846f;
        float sinY = glm::clamp(rotM[2][0], -1.0f, 1.0f);
        float cosY = std::sqrt(std::max(0.0f, 1.0f - sinY * sinY));
        if (cosY > 1e-4f) {
            rotation.x = RAD2DEG * std::atan2(-rotM[2][1], rotM[2][2]);
            rotation.y = RAD2DEG * std::atan2(sinY, cosY);
            rotation.z = RAD2DEG * std::atan2(-rotM[1][0], rotM[0][0]);
        } else {
            rotation.x = RAD2DEG * std::atan2(rotM[1][2], rotM[1][1]);
            rotation.y = (sinY > 0.0f) ? 90.0f : -90.0f;
            rotation.z = 0.0f;
        }

        if (std::isnan(translation.x) || std::isinf(translation.x)) translation.x = 0.0f;
        if (std::isnan(translation.y) || std::isinf(translation.y)) translation.y = 0.0f;
        if (std::isnan(translation.z) || std::isinf(translation.z)) translation.z = 0.0f;
        if (std::isnan(rotation.x) || std::isinf(rotation.x)) rotation.x = 0.0f;
        if (std::isnan(rotation.y) || std::isinf(rotation.y)) rotation.y = 0.0f;
        if (std::isnan(rotation.z) || std::isinf(rotation.z)) rotation.z = 0.0f;
        if (std::isnan(scale.x) || std::isinf(scale.x) || scale.x < 1e-4f) scale.x = 1.0f;
        if (std::isnan(scale.y) || std::isinf(scale.y) || scale.y < 1e-4f) scale.y = 1.0f;
        if (std::isnan(scale.z) || std::isinf(scale.z) || scale.z < 1e-4f) scale.z = 1.0f;
    }

    GameObject& AddObject(PrimitiveType type, glm::vec3 pos = {0.0f, 0.5f, 0.0f}, glm::vec3 color = {0.55f, 0.55f, 0.55f}, int parentId = -1) {
        if (onPreChange) onPreChange("Add " + std::string(GetPrimitiveTypeName(type)));
        int id = nextId++;
        std::string name = std::string(GetPrimitiveTypeName(type)) + " " + std::to_string(id);
        objects.emplace_back(id, name, type, pos, color);
        GameObject& obj = objects.back();
        obj.materialName = "Default_Material";
        obj.metallic = 0.0f;
        obj.roughness = 0.5f;
        obj.specular = 0.5f;
        obj.parentId = parentId;
        if (parentId != -1) {
            GameObject* parent = FindObject(parentId);
            if (parent) parent->childIds.push_back(id);
        }
        selectedId = id;
        return obj;
    }

    GameObject& AddEmptyActor(glm::vec3 pos = {0.0f, 0.0f, 0.0f}, int parentId = -1) {
        if (onPreChange) onPreChange("Add Empty Actor");
        int id = nextId++;
        std::string name = "Empty Actor " + std::to_string(id);
        objects.emplace_back(id, name, PrimitiveType::Empty, pos, glm::vec3{1.0f});
        GameObject& obj = objects.back();
        obj.mesh.vertices.clear();
        obj.mesh.indices.clear();
        obj.parentId = parentId;
        if (parentId != -1) {
            GameObject* parent = FindObject(parentId);
            if (parent) parent->childIds.push_back(id);
        }
        selectedId = id;
        return obj;
    }

    // Add a PointLight proxy actor to the outliner (mirrors scene.pointLights[lightIndex])
    GameObject& AddLightActor(int lightIndex, int parentId = -1) {
        PointLight& pl = pointLights[lightIndex];
        int id = nextId++;
        objects.emplace_back(id, pl.name, PrimitiveType::PointLight, pl.position, glm::vec3{pl.color.r, pl.color.g, pl.color.b});
        GameObject& obj = objects.back();
        obj.isLight = true;
        obj.lightId = lightIndex;
        obj.light.castShadows = pl.castShadows;
        obj.parentId = parentId;
        obj.mesh.vertices.clear();
        obj.mesh.indices.clear();
        if (parentId != -1) {
            GameObject* parent = FindObject(parentId);
            if (parent) parent->childIds.push_back(id);
        }
        selectedId = id;
        return obj;
    }

    // Apply type-specific default preset to a WaterBodyComponent
    static void ApplyWaterBodyPreset(WaterBodyComponent& w, WaterBodyType wbType) {
        w.type = wbType;
        // Global baseline defaults for AquaTerrain simulation & shading (dev.md Section 30)
        w.simulationEnabled = true;
        w.simulationResolution = 256;
        w.waveDamping = 0.985f;
        w.disturbanceStrength = 0.3f;
        w.heightScale = 0.03f; // Subtle local ripple displacement (Section 7)
        w.depthRange = 12.0f;
        w.foamDistance = 0.6f;
        w.foamStrength = 0.15f; // Conservative broken shoreline foam (Section 23)
        w.foamSpeed = 0.5f;
        w.fresnelStrength = 0.8f;
        w.specularPower = 90.0f;
        w.sparklePower = 300.0f;
        w.choppiness = 0.7f;
        w.oceanExtent = 3000.0f;
        w.debugMode = 0;
        w.isCameraCentered = (wbType == WaterBodyType::Ocean || wbType == WaterBodyType::Sea);

        switch (wbType) {

        case WaterBodyType::Ocean:
            // Continuous Open-World Ocean (dev.md Sections 1-38)
            w.width = 2000.0f;  w.length = 2000.0f;  w.gridResolution = 256;
            w.waterColor   = {0.02f, 0.08f, 0.14f, 0.90f}; // Dark blue-green deep water (Section 17)
            w.shallowColor = {0.04f, 0.20f, 0.28f, 0.80f}; // Dark teal shallow water
            w.roughness = 0.12f;  w.metallic = 0.0f;  w.opacity = 0.85f;
            w.reflectionStrength = 0.75f;  w.refractionStrength = 0.4f;  w.specularIntensity = 0.8f;
            w.normalScale = 0.35f;  w.waveSpeed = 1.0f;  w.waveScale = 0.20f;  w.waveHeight = 0.18f;
            w.choppiness = 0.7f;
            w.enableFoam = true;
            w.foamColor = {0.92f, 0.96f, 1.0f, 0.70f};  w.foamScale = 1.0f;  w.foamOpacity = 0.6f;
            w.enableDepth = true;  w.depthFadeDistance = 30.0f;
            w.enableRefraction = true;  w.distortionStrength = 0.15f;
            w.caustics = false;
            w.enableWaves = true;
            w.oceanWaveAmplitude = 0.2f;  w.oceanWavePeriod = 15.0f;
            w.oceanWaveDirection = 45.0f;  w.tessellationLevel = 8;
            w.reflectSkybox = true;  w.receiveEnvironmentLight = true;
            w.environmentLightIntensity = 1.2f;
            w.subsurfaceScattering = 0.25f;
            w.subsurfaceColor = {0.02f, 0.15f, 0.25f};
            break;

        case WaterBodyType::Sea:
            // Enclosed sea: calm rolling waves
            w.width = 1000.0f;  w.length = 1000.0f;  w.gridResolution = 128;
            w.waterColor   = {0.03f, 0.10f, 0.16f, 0.88f};
            w.shallowColor = {0.06f, 0.24f, 0.30f, 0.78f};
            w.roughness = 0.14f;  w.metallic = 0.0f;  w.opacity = 0.85f;
            w.reflectionStrength = 0.70f;  w.refractionStrength = 0.4f;  w.specularIntensity = 0.75f;
            w.normalScale = 0.30f;  w.waveSpeed = 0.8f;  w.waveScale = 0.25f;  w.waveHeight = 0.12f;
            w.choppiness = 0.6f;
            w.enableFoam = true;
            w.foamColor = {0.92f, 0.95f, 0.98f, 0.65f};  w.foamScale = 1.2f;  w.foamOpacity = 0.5f;
            w.enableDepth = true;  w.depthFadeDistance = 25.0f;
            w.enableRefraction = true;  w.distortionStrength = 0.12f;
            w.enableWaves = true;
            w.oceanWaveAmplitude = 0.15f;  w.oceanWavePeriod = 12.0f;
            w.oceanWaveDirection = 30.0f;  w.tessellationLevel = 6;
            w.reflectSkybox = true;  w.receiveEnvironmentLight = true;
            w.subsurfaceScattering = 0.20f;
            w.subsurfaceColor = {0.03f, 0.12f, 0.22f};
            break;

        case WaterBodyType::Lake:
            // Calm inland lake: medium scale, clear blue-green, still surface
            w.width = 60.0f;  w.length = 60.0f;  w.gridResolution = 48;
            w.waterColor   = {0.04f, 0.18f, 0.28f, 0.88f};
            w.shallowColor = {0.12f, 0.38f, 0.45f, 0.72f};
            w.roughness = 0.04f;  w.metallic = 0.0f;  w.opacity = 0.82f;
            w.reflectionStrength = 0.92f;  w.refractionStrength = 0.3f;  w.specularIntensity = 1.6f;
            w.normalScale = 0.5f;  w.waveSpeed = 0.3f;  w.waveScale = 1.2f;  w.waveHeight = 0.15f;
            w.enableFoam = false;
            w.enableDepth = true;  w.depthFadeDistance = 15.0f;
            w.enableRefraction = true;  w.distortionStrength = 0.08f;
            w.caustics = true;  w.causticsScale = 1.5f;  w.causticsSpeed = 0.4f;
            w.enableWaves = false;
            w.reflectSkybox = true;  w.receiveEnvironmentLight = true;
            w.environmentLightIntensity = 1.2f;
            w.subsurfaceScattering = 0.2f;
            w.subsurfaceColor = {0.05f, 0.18f, 0.22f};
            break;

        case WaterBodyType::River:
            // Flowing river: medium-wide, flowing current, slight turbulence
            w.width = 8.0f;  w.length = 80.0f;  w.gridResolution = 48;
            w.waterColor   = {0.06f, 0.20f, 0.22f, 0.85f};
            w.shallowColor = {0.15f, 0.40f, 0.35f, 0.70f};
            w.roughness = 0.25f;  w.metallic = 0.0f;  w.opacity = 0.80f;
            w.reflectionStrength = 0.6f;  w.refractionStrength = 0.4f;  w.specularIntensity = 1.1f;
            w.normalScale = 1.5f;  w.waveSpeed = 1.8f;  w.waveScale = 1.8f;  w.waveHeight = 0.4f;
            w.enableFoam = true;
            w.foamColor = {0.90f, 0.93f, 0.95f, 0.65f};  w.foamScale = 2.5f;  w.foamOpacity = 0.5f;
            w.enableDepth = true;  w.depthFadeDistance = 6.0f;
            w.enableRefraction = true;  w.distortionStrength = 0.3f;
            w.enableFlow = true;
            w.flowDirection = {0.0f, 1.0f};  w.flowSpeed = 1.5f;  w.flowTiling = 2.0f;
            w.enableWaves = false;
            w.reflectSkybox = true;  w.receiveEnvironmentLight = true;
            w.subsurfaceScattering = 0.15f;
            w.subsurfaceColor = {0.08f, 0.20f, 0.18f};
            break;

        case WaterBodyType::Stream:
            // Narrow fast stream: small scale, fast flow, clear, rocky
            w.width = 2.5f;  w.length = 25.0f;  w.gridResolution = 32;
            w.waterColor   = {0.10f, 0.28f, 0.25f, 0.78f};
            w.shallowColor = {0.20f, 0.50f, 0.40f, 0.60f};
            w.roughness = 0.35f;  w.metallic = 0.0f;  w.opacity = 0.72f;
            w.reflectionStrength = 0.5f;  w.refractionStrength = 0.5f;  w.specularIntensity = 0.9f;
            w.normalScale = 2.0f;  w.waveSpeed = 2.5f;  w.waveScale = 2.5f;  w.waveHeight = 0.25f;
            w.enableFoam = true;
            w.foamColor = {1.0f, 1.0f, 1.0f, 0.8f};  w.foamScale = 3.0f;  w.foamOpacity = 0.65f;
            w.enableDepth = true;  w.depthFadeDistance = 2.0f;
            w.enableRefraction = true;  w.distortionStrength = 0.4f;
            w.caustics = true;  w.causticsScale = 2.0f;  w.causticsSpeed = 1.2f;
            w.enableFlow = true;
            w.flowDirection = {0.0f, 1.0f};  w.flowSpeed = 2.5f;  w.flowTiling = 3.0f;
            w.enableWaves = false;
            w.reflectSkybox = false;  w.receiveEnvironmentLight = true;
            w.subsurfaceScattering = 0.1f;
            w.subsurfaceColor = {0.12f, 0.25f, 0.20f};
            break;

        case WaterBodyType::Waterfall:
            // Vertical cascade: tall and narrow, heavy foam, fast flow
            w.width = 4.0f;  w.length = 12.0f;  w.gridResolution = 32;
            w.waterColor   = {0.30f, 0.45f, 0.50f, 0.75f};
            w.shallowColor = {0.55f, 0.70f, 0.72f, 0.60f};
            w.roughness = 0.55f;  w.metallic = 0.0f;  w.opacity = 0.70f;
            w.reflectionStrength = 0.35f;  w.refractionStrength = 0.6f;  w.specularIntensity = 0.8f;
            w.normalScale = 2.5f;  w.waveSpeed = 3.5f;  w.waveScale = 3.0f;  w.waveHeight = 0.6f;
            w.enableFoam = true;
            w.foamColor = {1.0f, 1.0f, 1.0f, 0.92f};  w.foamScale = 2.0f;  w.foamOpacity = 0.85f;
            w.enableDepth = false;
            w.enableRefraction = true;  w.distortionStrength = 0.7f;
            w.enableFlow = true;
            w.flowDirection = {0.0f, 1.0f};  w.flowSpeed = 4.0f;  w.flowTiling = 4.0f;
            w.enableWaves = false;
            w.waterfallFallHeight = 12.0f;  w.waterfallFlowRate = 4.0f;  w.waterfallSprayAmount = 1.5f;
            w.reflectSkybox = false;  w.receiveEnvironmentLight = true;
            w.subsurfaceScattering = 0.05f;
            break;

        case WaterBodyType::Pond:
            // Small still pond: small, dark-green tint, very calm
            w.width = 12.0f;  w.length = 12.0f;  w.gridResolution = 24;
            w.waterColor   = {0.03f, 0.12f, 0.10f, 0.90f};
            w.shallowColor = {0.08f, 0.28f, 0.22f, 0.75f};
            w.roughness = 0.06f;  w.metallic = 0.0f;  w.opacity = 0.85f;
            w.reflectionStrength = 0.88f;  w.refractionStrength = 0.2f;  w.specularIntensity = 1.4f;
            w.normalScale = 0.3f;  w.waveSpeed = 0.2f;  w.waveScale = 0.8f;  w.waveHeight = 0.05f;
            w.enableFoam = false;
            w.enableDepth = true;  w.depthFadeDistance = 5.0f;
            w.enableRefraction = true;  w.distortionStrength = 0.05f;
            w.caustics = true;  w.causticsScale = 1.0f;  w.causticsSpeed = 0.3f;
            w.enableWaves = false;
            w.reflectSkybox = true;  w.receiveEnvironmentLight = true;
            w.subsurfaceScattering = 0.12f;
            w.subsurfaceColor = {0.04f, 0.14f, 0.12f};
            break;

        case WaterBodyType::Pool:
            // Clean swimming pool: rectangular, bright turquoise, very clear
            w.width = 10.0f;  w.length = 20.0f;  w.gridResolution = 24;
            w.waterColor   = {0.05f, 0.45f, 0.55f, 0.88f};
            w.shallowColor = {0.15f, 0.65f, 0.72f, 0.70f};
            w.roughness = 0.03f;  w.metallic = 0.0f;  w.opacity = 0.82f;
            w.reflectionStrength = 0.95f;  w.refractionStrength = 0.35f;  w.specularIntensity = 2.0f;
            w.normalScale = 0.4f;  w.waveSpeed = 0.4f;  w.waveScale = 0.8f;  w.waveHeight = 0.08f;
            w.enableFoam = false;
            w.enableDepth = true;  w.depthFadeDistance = 4.0f;
            w.enableRefraction = true;  w.distortionStrength = 0.12f;
            w.caustics = true;  w.causticsScale = 1.2f;  w.causticsSpeed = 0.5f;
            w.enableWaves = false;
            w.reflectSkybox = true;  w.receiveEnvironmentLight = true;
            w.environmentLightIntensity = 1.3f;
            w.subsurfaceScattering = 0.25f;
            w.subsurfaceColor = {0.08f, 0.40f, 0.50f};
            break;

        case WaterBodyType::WetSurface:
            // Rain-wet ground: thin film, very reflective, dark/glossy
            w.width = 8.0f;  w.length = 8.0f;  w.gridResolution = 16;
            w.waterColor   = {0.05f, 0.06f, 0.08f, 0.60f};
            w.shallowColor = {0.08f, 0.10f, 0.12f, 0.40f};
            w.roughness = 0.02f;  w.metallic = 0.0f;  w.opacity = 0.55f;
            w.reflectionStrength = 0.98f;  w.refractionStrength = 0.05f;  w.specularIntensity = 2.0f;
            w.normalScale = 0.2f;  w.waveSpeed = 0.5f;  w.waveScale = 0.5f;  w.waveHeight = 0.01f;
            w.enableFoam = false;
            w.enableDepth = false;
            w.enableRefraction = false;
            w.wetness = 0.85f;
            w.animatePuddle = true;  w.rippleSpeed = 1.0f;  w.rippleScale = 1.5f;
            w.enableWaves = false;
            w.reflectSkybox = true;  w.receiveEnvironmentLight = true;
            break;

        case WaterBodyType::Puddle:
            // Small rain puddle: tiny, shallow, rippling
            w.width = 1.5f;  w.length = 1.5f;  w.gridResolution = 12;
            w.waterColor   = {0.06f, 0.07f, 0.09f, 0.70f};
            w.shallowColor = {0.10f, 0.12f, 0.15f, 0.50f};
            w.roughness = 0.04f;  w.metallic = 0.0f;  w.opacity = 0.65f;
            w.reflectionStrength = 0.90f;  w.refractionStrength = 0.1f;  w.specularIntensity = 1.8f;
            w.normalScale = 0.5f;  w.waveSpeed = 1.0f;  w.waveScale = 0.8f;  w.waveHeight = 0.02f;
            w.enableFoam = false;
            w.enableDepth = true;  w.depthFadeDistance = 0.2f;
            w.enableRefraction = true;  w.distortionStrength = 0.06f;
            w.wetness = 1.0f;
            w.puddleSize = 1.5f;  w.puddleDepth = 0.04f;
            w.animatePuddle = true;  w.rippleSpeed = 2.0f;  w.rippleScale = 1.0f;
            w.enableWaves = false;
            w.reflectSkybox = true;  w.receiveEnvironmentLight = true;
            break;

        case WaterBodyType::FloodWater:
            // Rising flood: large, murky brown-green, opaque
            w.width = 150.0f;  w.length = 150.0f;  w.gridResolution = 64;
            w.waterColor   = {0.15f, 0.14f, 0.05f, 0.95f};
            w.shallowColor = {0.22f, 0.20f, 0.08f, 0.88f};
            w.roughness = 0.45f;  w.metallic = 0.0f;  w.opacity = 0.92f;
            w.reflectionStrength = 0.3f;  w.refractionStrength = 0.1f;  w.specularIntensity = 0.6f;
            w.normalScale = 1.0f;  w.waveSpeed = 0.8f;  w.waveScale = 3.0f;  w.waveHeight = 0.3f;
            w.enableFoam = true;
            w.foamColor = {0.70f, 0.65f, 0.50f, 0.60f};  w.foamScale = 4.0f;  w.foamOpacity = 0.45f;
            w.enableDepth = true;  w.depthFadeDistance = 1.5f;
            w.enableRefraction = false;
            w.enableWaves = false;
            w.reflectSkybox = false;  w.receiveEnvironmentLight = true;
            w.subsurfaceScattering = 0.0f;
            break;

        case WaterBodyType::Underwater:
            // Underwater volume: large, heavy fog, distortion
            w.width = 60.0f;  w.length = 60.0f;  w.gridResolution = 16;
            w.waterColor   = {0.00f, 0.08f, 0.18f, 0.98f};
            w.shallowColor = {0.02f, 0.12f, 0.22f, 0.95f};
            w.roughness = 0.8f;  w.metallic = 0.0f;  w.opacity = 0.96f;
            w.reflectionStrength = 0.1f;  w.refractionStrength = 0.0f;  w.specularIntensity = 0.3f;
            w.normalScale = 0.5f;  w.waveSpeed = 0.2f;  w.waveScale = 1.0f;  w.waveHeight = 0.0f;
            w.enableFoam = false;
            w.enableDepth = true;  w.depthFadeDistance = 25.0f;
            w.enableRefraction = false;
            w.caustics = true;  w.causticsScale = 2.0f;  w.causticsSpeed = 0.5f;
            w.underwaterFogColor = {0.00f, 0.08f, 0.18f};
            w.underwaterFogDensity = 1.8f;
            w.underwaterVisibilityDistance = 18.0f;
            w.enableUnderwaterDistortion = true;
            w.underwaterDistortionStrength = 0.8f;
            w.underwaterDistortionSpeed = 1.2f;
            w.enableWaves = false;
            w.reflectSkybox = false;  w.receiveEnvironmentLight = true;
            w.environmentLightIntensity = 0.4f;
            w.subsurfaceScattering = 0.0f;
            break;

        case WaterBodyType::Swamp:
            // Dark swamp: murky, algae-covered, dense fog
            w.width = 40.0f;  w.length = 40.0f;  w.gridResolution = 32;
            w.waterColor   = {0.06f, 0.10f, 0.04f, 0.95f};
            w.shallowColor = {0.12f, 0.18f, 0.06f, 0.88f};
            w.roughness = 0.65f;  w.metallic = 0.0f;  w.opacity = 0.92f;
            w.reflectionStrength = 0.15f;  w.refractionStrength = 0.05f;  w.specularIntensity = 0.4f;
            w.normalScale = 0.8f;  w.waveSpeed = 0.15f;  w.waveScale = 1.5f;  w.waveHeight = 0.05f;
            w.enableFoam = false;
            w.enableDepth = true;  w.depthFadeDistance = 1.0f;
            w.enableRefraction = false;
            w.swampDensity = 0.85f;
            w.swampColor = {0.06f, 0.12f, 0.04f};
            w.algaeAmount = 0.65f;
            w.enableWaves = false;
            w.reflectSkybox = false;  w.receiveEnvironmentLight = true;
            w.environmentLightIntensity = 0.6f;
            w.subsurfaceScattering = 0.05f;
            w.subsurfaceColor = {0.05f, 0.10f, 0.03f};
            break;

        case WaterBodyType::Custom:
            w.width = 25.0f; w.length = 25.0f; w.gridResolution = 32;
            w.waterColor = {0.03f, 0.20f, 0.35f, 0.85f};
            w.shallowColor = {0.10f, 0.45f, 0.60f, 0.70f};
            w.roughness = 0.1f; w.metallic = 0.0f; w.opacity = 0.80f;
            w.reflectionStrength = 0.9f; w.refractionStrength = 0.5f; w.specularIntensity = 1.4f;
            w.normalScale = 1.0f; w.waveSpeed = 1.0f; w.waveScale = 1.5f; w.waveHeight = 0.5f;
            w.enableFoam = true; w.foamColor = {1.0f, 1.0f, 1.0f, 0.75f}; w.foamScale = 3.0f; w.foamOpacity = 0.6f;
            w.depthRange = 10.0f; w.foamDistance = 1.5f; w.foamStrength = 1.0f; w.fresnelStrength = 1.0f;
            w.specularPower = 128.0f; w.sparklePower = 800.0f;
            w.simulationEnabled = true; w.waveDamping = 0.985f;
            break;

        default: break;
        }
    }

    GameObject& AddWaterBody(WaterBodyType wbType, glm::vec3 pos, const std::string& name = "", int parentId = -1) {
        if (onPreChange) onPreChange("Add Water Body");
        int id = nextId++;
        std::string finalName = name.empty() ? ("Water_" + std::to_string(id)) : name;
        objects.emplace_back(id, finalName, PrimitiveType::WaterBody, pos);
        GameObject& obj = objects.back();

        obj.isWaterBody = true;
        obj.blendMode = 2; // Translucent
        obj.twoSided = true;

        // Apply type-specific preset defaults
        ApplyWaterBodyPreset(obj.water, wbType);

        // Build mesh from preset geometry
        obj.mesh = GeometryBuilder::CreatePlane(
            obj.water.width, obj.water.length,
            obj.water.gridResolution, obj.water.gridResolution);
        obj.position.y = obj.water.elevation;

        obj.params.width    = obj.water.width;
        obj.params.depth    = obj.water.length;
        obj.params.segmentsX = obj.water.gridResolution;
        obj.params.segmentsZ = obj.water.gridResolution;

        obj.parentId = parentId;
        if (parentId != -1) {
            GameObject* parent = FindObject(parentId);
            if (parent) parent->childIds.push_back(id);
        }

        selectedId = id;
        return obj;
    }


    // Create a brand-new Light + its actor
    GameObject& AddNewLight(PrimitiveType lightType = PrimitiveType::PointLight, glm::vec3 pos = {0.0f, 2.5f, 0.0f}, int parentId = -1) {
        if (onPreChange) onPreChange("Add " + std::string(GetPrimitiveTypeName(lightType)));
        int id = nextId++;
        std::string name = std::string(GetPrimitiveTypeName(lightType)) + " " + std::to_string(id);
        glm::vec3 col(1.0f, 0.95f, 0.85f);
        if (lightType == PrimitiveType::DirectionalLight) col = glm::vec3(1.0f, 0.98f, 0.92f);
        else if (lightType == PrimitiveType::SkyLight) col = glm::vec3(0.65f, 0.8f, 1.0f);

        objects.emplace_back(id, name, lightType, pos, col);
        GameObject& obj = objects.back();
        if (lightType == PrimitiveType::SkyLight) {
            obj.light.type = LightType::Sky;
            obj.light.castShadows = false;
            obj.light.intensity = 1.0f;
        } else {
            obj.light.castShadows = true;
        }
        obj.parentId = parentId;
        if (parentId != -1) {
            GameObject* parent = FindObject(parentId);
            if (parent) parent->childIds.push_back(id);
        }

        if (lightType == PrimitiveType::PointLight || lightType == PrimitiveType::SpotLight ||
            lightType == PrimitiveType::AreaLight) {
            PointLight pl;
            pl.id = id;
            pl.name = name;
            pl.type = obj.light.type;
            pl.position = pos;
            pl.direction = obj.light.direction;
            pl.color = col;
            pl.intensity = obj.light.intensity;
            pl.range = obj.light.range;
            pl.attenuation = obj.light.attenuation;
            pl.enabled = true;
            pl.castShadows = true;
            pl.shadowStrength = obj.light.shadowStrength;
            pl.shadowBias = obj.light.shadowBias;
            pl.shadowResolution = obj.light.shadowResolution;
            pl.innerConeAngle = obj.light.innerConeAngle;
            pl.outerConeAngle = obj.light.outerConeAngle;
            pl.coneFalloff = obj.light.coneFalloff;
            pl.areaShape = obj.light.areaShape;
            pl.width = obj.light.width;
            pl.height = obj.light.height;
            pl.radius = obj.light.radius;
            pl.twoSided = obj.light.twoSided;
            pointLights.push_back(pl);
            obj.lightId = (int)pointLights.size() - 1;
        }

        selectedId = id;
        return obj;
    }

    // After loading a level: create proxy GameObjects for each PointLight that doesn't already have one
    void SyncLightActors() {
        for (int i = 0; i < (int)pointLights.size(); ++i) {
            bool found = false;
            for (auto& obj : objects) {
                if (obj.isLight && obj.lightId == i) { found = true; break; }
            }
            if (!found) {
                AddLightActor(i);
            }
        }
    }

    // Keep all lights synced from their GameObjects (called each frame)
    void SyncLightPositionsFromActors() {
        ambientIntensity = 0.0f;
        ambientColor = glm::vec3(0.0f);
        bool hasDirectional = false;
        bool hasSkyLight = false;
        pointLights.clear();
        for (auto& obj : objects) {
            if (obj.isLight || IsLightPrimitive(obj.type)) {
                obj.isLight = true;
                glm::mat4 worldMat = GetWorldMatrix(obj);
                glm::vec3 worldPos = GetWorldPosition(obj);

                glm::vec3 unnormDir = glm::vec3(worldMat * glm::vec4(0.0f, -1.0f, 0.0f, 0.0f));
                float dirLen = glm::length(unnormDir);
                if (dirLen > 0.0001f && !std::isnan(dirLen)) {
                    obj.light.direction = unnormDir / dirLen;
                }

                if (obj.light.type == LightType::Directional && obj.light.enabled) {
                    lightDirection = -obj.light.direction;
                    lightColor = obj.light.useTemperature ? (obj.light.color * ColorTemperatureToRGB(obj.light.temperature)) : obj.light.color;
                    lightIntensity = obj.light.intensity;
                    enableShadows = obj.light.castShadows;
                    shadowStrength = obj.light.shadowStrength;
                    shadowBias = obj.light.shadowBias;
                    shadowResolution = obj.light.shadowResolution;
                    hasDirectional = true;
                }
                else if (obj.light.type == LightType::Sky && obj.light.enabled) {
                    glm::vec3 sCol = obj.light.useTemperature ? (obj.light.color * ColorTemperatureToRGB(obj.light.temperature)) : obj.light.color;
                    ambientColor += sCol * obj.light.intensity;
                    ambientIntensity += obj.light.intensity;
                    hasSkyLight = true;
                }
                else if (obj.light.type == LightType::Point || obj.type == PrimitiveType::PointLight ||
                    obj.light.type == LightType::Spot || obj.type == PrimitiveType::SpotLight ||
                    obj.light.type == LightType::Area || obj.type == PrimitiveType::AreaLight) {
                    if (obj.light.enabled) {
                        PointLight pl;
                        pl.id = obj.id;
                        pl.name = obj.name;
                        pl.type = obj.light.type;
                        pl.position = worldPos;
                        pl.direction = obj.light.direction;
                        pl.color = obj.light.useTemperature ? (obj.light.color * ColorTemperatureToRGB(obj.light.temperature)) : obj.light.color;
                        pl.intensity = obj.light.intensity;
                        pl.range = obj.light.range;
                        pl.attenuation = obj.light.attenuation;
                        pl.enabled = obj.light.enabled;
                        pl.castShadows = obj.light.castShadows;
                        pl.shadowStrength = obj.light.shadowStrength;
                        pl.shadowBias = obj.light.shadowBias;
                        pl.shadowResolution = obj.light.shadowResolution;
                        pl.innerConeAngle = obj.light.innerConeAngle;
                        pl.outerConeAngle = obj.light.outerConeAngle;
                        pl.coneFalloff = obj.light.coneFalloff;
                        pl.areaShape = obj.light.areaShape;
                        pl.width = obj.light.width;
                        pl.height = obj.light.height;
                        pl.radius = obj.light.radius;
                        pl.twoSided = obj.light.twoSided;
                        pointLights.push_back(pl);
                        obj.lightId = (int)pointLights.size() - 1;
                    }
                }
            }
        }
        if (!hasDirectional) {
            lightIntensity = 0.0f;
        }
        if (hasSkyLight) {
            if (ambientIntensity > 0.0001f) {
                ambientColor /= ambientIntensity;
            }
        } else {
            ambientColor = glm::vec3(0.0f);
            ambientIntensity = 0.0f;
        }
    }

    GameObject& AddImportedMesh(const std::string& filePath, glm::vec3 pos = {0.0f, 0.0f, 0.0f}) {
        if (onPreChange) onPreChange("Import 3D Mesh: " + filePath);
        ImportedModel model;
        try { model = MeshImporter::Load(filePath); } catch (...) {}

        // If model has multiple meshes (hierarchy), spawn them as children of a root actor
        if (model.valid && model.meshes.size() > 1) {
            // Root actor (no geometry, just a transform pivot)
            int rootId = nextId++;
            std::filesystem::path p(filePath);
            std::string rootName = p.stem().string();
            objects.emplace_back(rootId, rootName, PrimitiveType::ImportedMesh, pos, glm::vec3{0.55f});
            GameObject& root = objects.back();
            root.meshFilePath = filePath;
            root.isImportedMesh = true;
            root.submeshIndex = -1;
            root.materialName = "Default_Material";
            root.parentId = -1;
            root.mesh.vertices.clear();
            root.mesh.indices.clear();
            selectedId = rootId;

            // Child actors for each sub-mesh
            int subIdx = 0;
            for (auto& im : model.meshes) {
                if (!im.valid || im.vertices.empty()) { subIdx++; continue; }
                int childId = nextId++;
                std::string childName = im.name.empty() ? (rootName + "_" + std::to_string(subIdx)) : im.name;

                // Compute bounding-box center / pivot point of the sub-mesh
                glm::vec3 minBound(1e9f);
                glm::vec3 maxBound(-1e9f);
                for (const auto& v : im.vertices) {
                    if (!std::isnan(v.pos.x) && !std::isnan(v.pos.y) && !std::isnan(v.pos.z) &&
                        !std::isinf(v.pos.x) && !std::isinf(v.pos.y) && !std::isinf(v.pos.z)) {
                        minBound = glm::min(minBound, v.pos);
                        maxBound = glm::max(maxBound, v.pos);
                    }
                }
                glm::vec3 pivot = (minBound + maxBound) * 0.5f;

                objects.emplace_back(childId, childName, PrimitiveType::ImportedMesh, pivot, glm::vec3{0.55f});
                GameObject& child = objects.back();
                child.meshFilePath = filePath;
                child.isImportedMesh = true;
                child.submeshIndex = subIdx++;
                child.materialName = "Default_Material";
                child.metallic = 0.0f;
                child.roughness = 0.5f;
                child.specular = 0.5f;
                child.parentId = rootId;

                if (im.material.hasMaterial) {
                    child.materialName       = im.material.name;
                    child.baseColorTexture   = im.material.baseColorTexture;
                    child.normalTexture      = im.material.normalTexture;
                    child.roughnessTexture   = im.material.roughnessTexture;
                    child.metallicTexture    = im.material.metallicTexture;
                    child.aoTexture          = im.material.aoTexture;
                    child.emissionTexture    = im.material.emissionTexture;
                    child.opacityTexture     = im.material.opacityTexture;
                    child.metallic           = im.material.metallic;
                    child.roughness          = im.material.roughness;
                    child.emissiveColor      = im.material.emissiveColor;
                    child.emissiveIntensity  = im.material.emissiveIntensity;

                    AddEngineLog("LogImport",
                        "Auto-assigned material \"" + im.material.name + "\" to \"" + child.name + "\"" +
                        (im.material.baseColorTexture.empty() ? " (no base color texture found)" : ""), 0);
                }

                // Center mesh vertices relative to its pivot point
                child.mesh.vertices = im.vertices;
                child.mesh.indices  = im.indices;
                uint32_t vCount = (uint32_t)child.mesh.vertices.size();
                for (uint32_t& idx : child.mesh.indices) { if (idx >= vCount) idx = 0; }
                for (auto& v : child.mesh.vertices) {
                    if (std::isnan(v.pos.x)||std::isnan(v.pos.y)||std::isnan(v.pos.z)||
                        std::isinf(v.pos.x)||std::isinf(v.pos.y)||std::isinf(v.pos.z)) {
                        v.pos = glm::vec3(0.0f);
                    } else {
                        v.pos -= pivot;
                    }
                }
                // Register child with root
                if (GameObject* rootPtr = FindObject(rootId)) rootPtr->childIds.push_back(childId);
            }
            return *FindObject(rootId);
        }

        // Single-mesh fallback (original behavior)
        int id = nextId++;
        std::filesystem::path p(filePath);
        std::string meshName = p.stem().string();
        if (meshName.empty()) meshName = "Mesh";

        objects.emplace_back(id, meshName, PrimitiveType::ImportedMesh, pos, glm::vec3{0.55f, 0.55f, 0.55f});
        GameObject& obj = objects.back();
        obj.meshFilePath = filePath;
        obj.isImportedMesh = true;
        obj.submeshIndex = 0;
        obj.materialName = "Default_Material";
        obj.metallic = 0.0f;
        obj.roughness = 0.5f;
        obj.specular = 0.5f;
        obj.parentId = -1;

        if (!model.meshes.empty() && model.meshes[0].material.hasMaterial) {
            const auto& mat = model.meshes[0].material;
            obj.materialName       = mat.name;
            obj.baseColorTexture   = mat.baseColorTexture;
            obj.normalTexture      = mat.normalTexture;
            obj.roughnessTexture   = mat.roughnessTexture;
            obj.metallicTexture    = mat.metallicTexture;
            obj.aoTexture          = mat.aoTexture;
            obj.emissionTexture    = mat.emissionTexture;
            obj.opacityTexture     = mat.opacityTexture;
            obj.metallic           = mat.metallic;
            obj.roughness          = mat.roughness;
            obj.emissiveColor      = mat.emissiveColor;
            obj.emissiveIntensity  = mat.emissiveIntensity;

            AddEngineLog("LogImport",
                "Auto-assigned material \"" + mat.name + "\" to \"" + obj.name + "\"" +
                (mat.baseColorTexture.empty() ? " (no base color texture found)" : ""), 0);
        }

        if (model.valid && !model.meshes.empty()) {
            obj.mesh = model.GetMergedMesh();
            if (obj.mesh.vertices.empty()) {
                obj.mesh = GeometryBuilder::CreateCube(1.0f, 18);
            } else {
                uint32_t vCount = (uint32_t)obj.mesh.vertices.size();
                for (uint32_t& idx : obj.mesh.indices) { if (idx >= vCount) idx = 0; }
                glm::vec3 minBound(1e9f);
                glm::vec3 maxBound(-1e9f);
                for (auto& v : obj.mesh.vertices) {
                    if (std::isnan(v.pos.x)||std::isnan(v.pos.y)||std::isnan(v.pos.z)||
                        std::isinf(v.pos.x)||std::isinf(v.pos.y)||std::isinf(v.pos.z)) {
                        v.pos = glm::vec3(0.0f);
                    } else {
                        minBound = glm::min(minBound, v.pos);
                        maxBound = glm::max(maxBound, v.pos);
                    }
                }
                glm::vec3 pivot = (minBound + maxBound) * 0.5f;
                if (glm::length(pivot) > 0.0001f) {
                    obj.position += pivot;
                    for (auto& v : obj.mesh.vertices) {
                        v.pos -= pivot;
                    }
                }
            }
        } else {
            obj.mesh = GeometryBuilder::CreateCube(1.0f, 18);
        }

        selectedId = id;
        return obj;
    }

    // Set parentId of 'childId' to 'newParentId'; updates both old and new parent's childIds list and preserves world transform
    void ReparentObject(int childId, int newParentId) {
        GameObject* child = FindObject(childId);
        if (!child) return;
        if (child->parentId == newParentId) return;
        if (childId == newParentId) return;
        // Don't allow parenting to a descendant (cycle check)
        if (newParentId != -1 && IsDescendantOf(newParentId, childId)) return;

        glm::mat4 childWorld = GetWorldMatrix(*child);

        // Remove from all parents' childIds list to prevent any stale references
        for (auto& o : objects) {
            auto& ch = o.childIds;
            ch.erase(std::remove(ch.begin(), ch.end(), childId), ch.end());
        }

        child->parentId = newParentId;
        if (newParentId != -1) {
            GameObject* newParent = FindObject(newParentId);
            if (newParent) {
                if (std::find(newParent->childIds.begin(), newParent->childIds.end(), childId) == newParent->childIds.end()) {
                    newParent->childIds.push_back(childId);
                }
                glm::mat4 parentWorld = GetWorldMatrix(*newParent);
                float det = glm::determinant(parentWorld);
                if (std::abs(det) > 1e-6f && !std::isnan(det)) {
                    glm::mat4 newLocal = glm::inverse(parentWorld) * childWorld;
                    DecomposeMatrix(newLocal, child->position, child->rotation, child->scale);
                } else {
                    DecomposeMatrix(childWorld, child->position, child->rotation, child->scale);
                }
            }
        } else {
            DecomposeMatrix(childWorld, child->position, child->rotation, child->scale);
        }
    }

    void SetParent(int childId, int newParentId) {
        ReparentObject(childId, newParentId);
    }

    bool IsDescendantOf(int candidateId, int ancestorId) const {
        if (candidateId == -1 || ancestorId == -1) return false;
        if (candidateId == ancestorId) return true;
        const GameObject* node = FindObjectConst(candidateId);
        int depth = 0;
        while (node && depth++ < 256) {
            if (node->parentId == ancestorId) return true;
            if (node->parentId == -1) break;
            node = FindObjectConst(node->parentId);
        }
        return false;
    }

    // Dev.md Section 3: When enabling on a parent object, automatically propagate to all descendants/children recursively
    void SetMeshClusterCullingRecursive(int objId, bool enabled) {
        GameObject* obj = FindObject(objId);
        if (!obj) return;
        obj->meshClusterCulling = enabled;
        for (int childId : obj->childIds) {
            SetMeshClusterCullingRecursive(childId, enabled);
        }
    }

    void RemoveObject(int id) {
        static bool s_inRemove = false;
        bool isRootRemove = !s_inRemove;
        if (isRootRemove) {
            s_inRemove = true;
            if (onPreChange) onPreChange("Delete Actor");
        }

        // First recursively remove children
        GameObject* obj = FindObject(id);
        if (obj) {
            std::vector<int> children = obj->childIds; // copy before mutating
            for (int cid : children) RemoveObject(cid);
            // Remove from parent's childIds
            if (obj->parentId != -1) {
                GameObject* parent = FindObject(obj->parentId);
                if (parent) {
                    auto& ch = parent->childIds;
                    ch.erase(std::remove(ch.begin(), ch.end(), id), ch.end());
                }
            }
            // If it's a light proxy, remove the actual PointLight too
            if (obj->isLight && obj->lightId >= 0 && obj->lightId < (int)pointLights.size()) {
                int li = obj->lightId;
                pointLights.erase(pointLights.begin() + li);
                // Fix lightId references in remaining objects
                for (auto& o : objects) {
                    if (o.isLight && o.lightId > li) o.lightId--;
                }
            }
        }
        auto it = std::remove_if(objects.begin(), objects.end(), [id](const GameObject& o) { return o.id == id; });
        if (it != objects.end()) {
            objects.erase(it, objects.end());
            OnObjectRemoved(id);
            if (selectedId == id) {
                selectedId = objects.empty() ? -1 : objects.front().id;
            }
        }

        if (isRootRemove) {
            s_inRemove = false;
        }
    }

    GameObject* DuplicateObject(int id) {
        if (onPreChange) onPreChange("Duplicate Actor");
        GameObject* orig = FindObject(id);
        if (!orig) return nullptr;

        // 1. Collect all descendant IDs in pre-order traversal (starting from 'id')
        std::vector<int> subtreeIds;
        std::unordered_set<int> visited;
        std::function<void(int)> collectSubtree = [&](int curId) {
            if (visited.count(curId)) return;
            visited.insert(curId);
            subtreeIds.push_back(curId);
            const GameObject* curObj = FindObject(curId);
            if (curObj) {
                for (int childId : curObj->childIds) {
                    collectSubtree(childId);
                }
            }
        };
        collectSubtree(id);

        // 2. Pre-allocate new unique IDs for each node in the subtree
        std::unordered_map<int, int> oldToNewId;
        for (int oldId : subtreeIds) {
            oldToNewId[oldId] = nextId++;
        }

        int rootNewId = oldToNewId[id];
        int originalParentId = orig->parentId;

        // 3. Duplicate all objects in the subtree
        // Store in temporary list first to avoid pointer/iterator invalidation during push_back
        std::vector<GameObject> newObjects;
        newObjects.reserve(subtreeIds.size());

        for (int oldId : subtreeIds) {
            const GameObject* srcObj = FindObject(oldId);
            if (!srcObj) continue;

            GameObject copy = *srcObj;
            copy.id = oldToNewId[oldId];

            if (oldId == id) {
                copy.name = srcObj->name + " (Copy)";
                // Duplicated object is positioned exactly at the main object (no offset)
                copy.position = srcObj->position;
                copy.parentId = originalParentId;
            } else {
                // For children in the duplicated subtree:
                // Reparent to the corresponding newly duplicated parent copy
                if (oldToNewId.count(srcObj->parentId)) {
                    copy.parentId = oldToNewId[srcObj->parentId];
                } else {
                    copy.parentId = -1;
                }
                // Keep the exact same local position
                copy.position = srcObj->position;
            }

            // Remap childIds to point to the newly duplicated children
            std::vector<int> remappedChildIds;
            for (int chId : srcObj->childIds) {
                if (oldToNewId.count(chId)) {
                    remappedChildIds.push_back(oldToNewId[chId]);
                }
            }
            copy.childIds = remappedChildIds;

            // If it's a light, reset lightId so SyncLightPositionsFromActors allocates a unique PointLight
            if (copy.isLight) {
                copy.lightId = -1;
            }

            newObjects.push_back(copy);
        }

        // Append all duplicated objects to the scene
        for (auto& newObj : newObjects) {
            objects.push_back(newObj);
        }

        // 4. If the root object being duplicated had a parent outside the subtree,
        // register the new root copy in that parent's childIds list
        if (originalParentId != -1 && !oldToNewId.count(originalParentId)) {
            GameObject* parentObj = FindObject(originalParentId);
            if (parentObj) {
                parentObj->childIds.push_back(rootNewId);
            }
        }

        // 5. Select the newly duplicated root object in the viewport
        selectedId = rootNewId;

        // 6. Ensure lights and transforms are synchronized immediately
        SyncLightPositionsFromActors();

        return FindObject(rootNewId);
    }

    GameObject* GetSelected() {
        return FindObject(selectedId);
    }

    void Clear() {
        objects.clear();
        pointLights.clear();
        waterBodies.clear();
        selectedId = -1;
        activeLevelCameraId = -1;
        nextId = 1;
        lightIntensity = 0.0f;
        ambientIntensity = 0.0f;
        playModePreObjects.clear();
        playModePreSelectedId = -1;
        enableSkybox = true;
        skyboxAssetPath = "";
        skyboxTexturePath = "";
        skyboxExposure = 1.0f;
        skyboxRotation = 0.0f;
        skyboxTint = glm::vec3(1.0f, 1.0f, 1.0f);
        skyboxAmbientContribution = 0.5f;
        enableSkyboxLighting = false;
        skyboxLightingIntensity = 1.0f;
        skyboxLightingColor = glm::vec3(1.0f, 1.0f, 1.0f);
        skyboxLightingSpecular = 1.0f;
        skyboxLightingDiffuse = 1.0f;
    }

    // Play Mode State & Runtime Lifecycle (dev.md Section 30-36)
    bool isPlayMode = false;
    float fixedTimeAccumulator = 0.0f;
    std::vector<GameObject> playModePreObjects;
    int playModePreSelectedId = -1;

    void StartPlayMode() {
        if (isPlayMode) return;

        // Ensure all registered script behaviours are upgraded to DynamicScriptBehaviour and properties refreshed from source
        const auto& allRegistry = BehaviourRegistry::Get().GetAll();
        for (auto& obj : objects) {
            for (auto& b : obj.behaviours) {
                if (!b) continue;
                auto regIt = allRegistry.find(b->GetClassName());
                if (regIt != allRegistry.end() && !regIt->second.sourceCpp.empty()) {
                    bool sourceModifiedAfterBuild = false;
                    std::error_code ec;
                    if (std::filesystem::exists(regIt->second.sourceCpp, ec)) {
                        char exePathBuf[MAX_PATH] = {};
                        GetModuleFileNameA(NULL, exePathBuf, MAX_PATH);
                        if (std::filesystem::exists(exePathBuf, ec)) {
                            auto cppTime = std::filesystem::last_write_time(regIt->second.sourceCpp, ec);
                            auto exeTime = std::filesystem::last_write_time(exePathBuf, ec);
                            if (!ec && cppTime > exeTime) {
                                sourceModifiedAfterBuild = true;
                            }
                        }
                    }

                    if (regIt->second.isNative && !sourceModifiedAfterBuild) {
                        // Native compiled behaviour
                        if (dynamic_cast<DynamicScriptBehaviour*>(b.get()) != nullptr && regIt->second.factory) {
                            auto nativeB = regIt->second.factory();
                            nativeB->SetScene(this);
                            nativeB->SetOwner(&obj);
                            nativeB->SetEnabled(b->IsEnabled());
                            nativeB->CopyPropertiesFrom(*b);
                            nativeB->ResolveReferences(*this);
                            b = std::move(nativeB);
                        } else {
                            b->RefreshPropertiesFromSource();
                        }
                    } else if (dynamic_cast<DynamicScriptBehaviour*>(b.get()) == nullptr || b->GetProperties().empty()) {
                        auto dynB = std::make_unique<DynamicScriptBehaviour>(b->GetClassName(), regIt->second.sourceCpp);
                        dynB->SetScene(this);
                        dynB->SetOwner(&obj);
                        dynB->SetEnabled(b->IsEnabled());
                        dynB->CopyPropertiesFrom(*b);
                        dynB->ResolveReferences(*this);
                        b = std::move(dynB);
                    } else {
                        b->RefreshPropertiesFromSource();
                    }
                }
            }
        }

        // Ensure all editor behaviour references are resolved before cloning
        ResolveAllBehaviourReferences();

        // Deep copy editor scene actors to restore cleanly on Stop
        playModePreObjects = objects;
        playModePreSelectedId = selectedId;
        isPlayMode = true;
        fixedTimeAccumulator = 0.0f;

        ResolveAllBehaviourReferences();

        for (auto& obj : objects) {
            for (auto& b : obj.behaviours) {
                if (!b) continue;
                b->SetScene(this);
                b->SetOwner(&obj);
                b->OnCreate();
                if (b->IsEnabled()) {
                    b->OnEnable();
                    if (!b->HasStarted()) {
                        b->Start();
                        b->SetStarted(true);
                    }
                }
            }
        }
        AddEngineLog("LogPlayLevel", "PIE: Play Mode Started (All Behaviours Initialized)", 2);
    }

    void StopPlayMode() {
        if (!isPlayMode) return;

        // Cleanup runtime state
        for (auto& obj : objects) {
            for (auto& b : obj.behaviours) {
                if (!b) continue;
                if (b->IsEnabled()) {
                    b->OnDisable();
                }
                b->OnDestroy();
                b->SetStarted(false);
            }
        }

        isPlayMode = false;

        // Restore unmodified editor scene
        objects = std::move(playModePreObjects);
        playModePreObjects.clear();
        selectedId = playModePreSelectedId;

        ResolveAllBehaviourReferences();
        SyncLightPositionsFromActors();

        AddEngineLog("LogPlayLevel", "PIE: Play Mode Stopped. Restored Editor Mode.", 1);
    }

    void ResolveAllBehaviourReferences() {
        for (auto& obj : objects) {
            for (auto& b : obj.behaviours) {
                if (b) {
                    b->SetScene(this);
                    b->SetOwner(&obj);
                    b->ResolveReferences(*this);
                }
            }
        }
    }

    void OnObjectRemoved(int removedId) {
        for (auto& obj : objects) {
            for (auto& b : obj.behaviours) {
                if (!b) continue;
                for (auto& prop : b->GetProperties()) {
                    if (prop.type == BehaviourPropertyType::ObjectRef && prop.targetId == removedId) {
                        prop.targetId = -1;
                        prop.isMissing = true;
                        if (prop.dataPtr) {
                            *reinterpret_cast<void**>(prop.dataPtr) = nullptr;
                        }
                    }
                }
            }
        }
    }

    void Update(float dt) {
        SyncLightPositionsFromActors();

        if (isPlayMode) {
            // Update global World Delta Seconds (dev.md: "the global var called 'World Delta Seconds' use as a DeltaTime")
            WorldDeltaSeconds = dt;
            World_Delta_Seconds = dt;
            worldDeltaSeconds = dt;
            WorldDeltaTime = dt;

            for (auto& obj : objects) {
                obj.scene = this;
            }

            // 1. FixedUpdate
            fixedTimeAccumulator += dt;
            const float fixedStep = 1.0f / 60.0f;
            while (fixedTimeAccumulator >= fixedStep) {
                for (auto& obj : objects) {
                    for (auto& b : obj.behaviours) {
                        if (b && b->IsEnabled()) b->FixedUpdate(fixedStep);
                    }
                }
                fixedTimeAccumulator -= fixedStep;
            }

            // 2. Update
            for (auto& obj : objects) {
                for (auto& b : obj.behaviours) {
                    if (b && b->IsEnabled()) b->Update(dt);
                }
                obj.Update(dt);
            }

            // 3. LateUpdate
            for (auto& obj : objects) {
                for (auto& b : obj.behaviours) {
                    if (b && b->IsEnabled()) b->LateUpdate(dt);
                }
            }
            return;
        }

        WorldDeltaSeconds = dt;
        World_Delta_Seconds = dt;
        worldDeltaSeconds = dt;
        WorldDeltaTime = dt;
        for (auto& obj : objects) {
            obj.scene = this;
        }

        if (!playAnimations) return;
        for (auto& obj : objects) {
            if (obj.id == selectedId && isInteracting) continue;
            obj.Update(dt);
        }
    }

struct RenderBatch {
    uint32_t startIndex = 0;
    uint32_t indexCount = 0;
    std::string albedoTex;
    std::string normalTex;
    std::string roughTex;
    std::string metalTex;
    std::string aoTex;
    std::string opacityTex;
    glm::vec3 baseColor{1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float normalStrength = 1.0f;
    float specular = 0.5f;
    glm::vec3 emissiveColor{0.0f, 0.0f, 0.0f};
    float emissiveIntensity = 0.0f;
    float opacity = 1.0f;
    float opacityMaskClipValue = 0.333f;
    int blendMode = 0;
    int shadingModel = 0;
    bool isUnlit = false;
    bool castShadows = true;
    bool receiveShadows = true;
    bool meshClusterCulling = false;
    int objectId = -1;
    uint32_t vertexOffset = 0;
    glm::vec2 uvScale{1.0f, 1.0f};
    bool normalMapYFlip = false;
    int metallicChannel = 0;
    int roughnessChannel = 1;
    int aoChannel = 0;
    int materialDebugMode = 0;
};

    void BuildSceneMesh(
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        std::vector<RenderBatch>& outBatches,
        glm::vec3 cameraPos = {0.0f, 0.0f, 0.0f})
    {
        outVertices.clear();
        outIndices.clear();
        outBatches.clear();

        // 1. Optional Grid
        if (showGrid) {
            uint32_t gridStart = (uint32_t)outIndices.size();
            GeometryBuilder::AppendGrid(outVertices, outIndices, gridSize, gridDivisions, gridColor);
            uint32_t gridCount = (uint32_t)outIndices.size() - gridStart;
            if (gridCount > 0) {
                RenderBatch b;
                b.startIndex = gridStart;
                b.indexCount = gridCount;
                b.isUnlit = true;
                b.castShadows = false;
                b.receiveShadows = true;
                b.baseColor = {1.0f, 1.0f, 1.0f};
                outBatches.push_back(b);
            }
        }

        // 2. All GameObjects (Rendered with Hardware GPU Shaders & Full 2K Texture Mapping)
        for (const auto& obj : objects) {
            if (!obj.visible || obj.isCamera || IsCameraPrimitive(obj.type) || obj.isWaterBody || obj.type == PrimitiveType::WaterBody) continue;
            if (obj.mesh.vertices.empty() || obj.mesh.indices.empty()) continue;

            glm::mat4 model = GetWorldMatrix(obj);
            glm::mat3 m3(model);
            glm::mat3 normalMatrix(1.0f);
            float det = glm::determinant(m3);
            if (std::abs(det) > 1e-6f && !std::isnan(det)) {
                normalMatrix = glm::transpose(glm::inverse(m3));
            } else {
                // Robust orthonormal fallback for degenerate scales (e.g. 0-scale axes)
                glm::vec3 c0 = glm::length(m3[0]) > 1e-5f ? glm::normalize(m3[0]) : glm::vec3(1.0f, 0.0f, 0.0f);
                glm::vec3 c1 = glm::length(m3[1]) > 1e-5f ? glm::normalize(m3[1]) : glm::vec3(0.0f, 1.0f, 0.0f);
                glm::vec3 c2 = glm::length(m3[2]) > 1e-5f ? glm::normalize(m3[2]) : glm::vec3(0.0f, 0.0f, 1.0f);
                normalMatrix = glm::mat3(c0, c1, c2);
            }

            uint32_t vertexOffset = (uint32_t)outVertices.size();
            uint32_t indexStart = (uint32_t)outIndices.size();

            for (const auto& mv : obj.mesh.vertices) {
                glm::vec4 worldPos = model * glm::vec4(mv.pos, 1.0f);
                // Task 3 – defense in depth: if the model matrix is corrupted (NaN/Inf),
                // fall back to local-space position rather than sending NaN to the GPU.
                if (std::isnan(worldPos.x) || std::isnan(worldPos.y) || std::isnan(worldPos.z) ||
                    !std::isfinite(worldPos.x) || !std::isfinite(worldPos.y) || !std::isfinite(worldPos.z)) {
                    worldPos = glm::vec4(mv.pos, 1.0f);
                }
                glm::vec3 n = normalMatrix * mv.normal;
                float nLen = glm::length(n);
                glm::vec3 worldNormal = (nLen > 1e-6f && !std::isnan(nLen)) ? (n / nLen) : glm::vec3(0.0f, 1.0f, 0.0f);

                glm::vec3 t = glm::mat3(model) * glm::vec3(mv.tangent);
                float tLen = glm::length(t);
                glm::vec3 worldTangent = (tLen > 1e-6f && !std::isnan(tLen)) ? (t / tLen) : glm::vec3(1.0f, 0.0f, 0.0f);
                glm::vec4 outTangent = glm::vec4(worldTangent, mv.tangent.w);

                outVertices.push_back({ glm::vec3(worldPos), worldNormal, mv.uv, glm::vec3(1.0f, 1.0f, 1.0f), outTangent });
            }

            for (uint32_t idx : obj.mesh.indices) {
                if (idx < (uint32_t)obj.mesh.vertices.size()) {
                    outIndices.push_back(vertexOffset + idx);
                } else {
                    outIndices.push_back(vertexOffset);
                }
            }

            RenderBatch b;
            b.startIndex = indexStart;
            b.indexCount = (uint32_t)obj.mesh.indices.size();
            b.albedoTex = obj.baseColorTexture;
            b.normalTex = obj.normalTexture;
            b.roughTex = obj.roughnessTexture;
            b.metalTex = obj.metallicTexture;
            b.aoTex = obj.aoTexture;
            b.opacityTex = obj.opacityTexture;
            b.baseColor = obj.color;
            b.metallic = obj.metallic;
            b.roughness = obj.roughness;
            b.normalStrength = obj.normalStrength;
            b.specular = obj.specular;
            b.emissiveColor = obj.emissiveColor;
            b.emissiveIntensity = obj.emissiveIntensity;
            b.opacity = obj.opacity;
            b.opacityMaskClipValue = obj.opacityMaskClipValue;
            b.blendMode = obj.blendMode;
            b.shadingModel = obj.shadingModel;
            b.isUnlit = (obj.shadingModel == 1);
            b.castShadows = obj.castShadows;
            b.receiveShadows = obj.receiveShadows;
            b.meshClusterCulling = obj.meshClusterCulling;
            b.objectId = obj.id;
            b.vertexOffset = vertexOffset;
            b.uvScale = obj.uvScale;
            b.normalMapYFlip = obj.normalMapYFlip;
            b.metallicChannel = obj.metallicChannel;
            b.roughnessChannel = obj.roughnessChannel;
            b.aoChannel = obj.aoChannel;
            b.materialDebugMode = obj.materialDebugMode;
            outBatches.push_back(b);
        }

        // 3. Highlight Selected Object: ONLY on the meshes edge as a vivid green outline
        if (selectedId != -1) {
            GameObject* selObj = FindObject(selectedId);
            if (selObj && selObj->visible) {
                uint32_t outlineStart = (uint32_t)outIndices.size();
                glm::vec3 greenOutlineColor{0.0f, 1.0f, 0.25f};
                AppendObjectEdgeOutline(outVertices, outIndices, *selObj, cameraPos, greenOutlineColor);
                uint32_t outlineCount = (uint32_t)outIndices.size() - outlineStart;
                if (outlineCount > 0) {
                    RenderBatch b;
                    b.startIndex = outlineStart;
                    b.indexCount = outlineCount;
                    b.isUnlit = true;
                    b.baseColor = {1.0f, 1.0f, 1.0f};
                    outBatches.push_back(b);
                }
            }
        }

        // 4. Debug Visualization of Light Frustum (dev.md Step 2)
        if (showLightFrustum && hasLightFrustumCorners) {
            uint32_t frustumStart = (uint32_t)outIndices.size();
            glm::vec3 frustumColor{1.0f, 0.85f, 0.1f}; // bright yellow

            auto AppendLine = [&](const glm::vec3& p0, const glm::vec3& p1) {
                uint32_t baseIdx = (uint32_t)outVertices.size();
                glm::vec3 dir = p1 - p0;
                float len = glm::length(dir);
                if (len < 1e-4f) return;
                glm::vec3 side = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
                if (glm::length(side) < 1e-3f) side = glm::vec3(1, 0, 0);
                float halfW = 0.035f;
                outVertices.push_back({ p0 - side * halfW, glm::vec3(0, 1, 0), glm::vec2(0, 0), frustumColor });
                outVertices.push_back({ p0 + side * halfW, glm::vec3(0, 1, 0), glm::vec2(1, 0), frustumColor });
                outVertices.push_back({ p1 + side * halfW, glm::vec3(0, 1, 0), glm::vec2(1, 1), frustumColor });
                outVertices.push_back({ p1 - side * halfW, glm::vec3(0, 1, 0), glm::vec2(0, 1), frustumColor });

                outIndices.push_back(baseIdx + 0);
                outIndices.push_back(baseIdx + 1);
                outIndices.push_back(baseIdx + 2);
                outIndices.push_back(baseIdx + 0);
                outIndices.push_back(baseIdx + 2);
                outIndices.push_back(baseIdx + 3);
            };

            // Near plane (0, 1, 2, 3)
            AppendLine(lightFrustumCorners[0], lightFrustumCorners[1]);
            AppendLine(lightFrustumCorners[1], lightFrustumCorners[2]);
            AppendLine(lightFrustumCorners[2], lightFrustumCorners[3]);
            AppendLine(lightFrustumCorners[3], lightFrustumCorners[0]);

            // Far plane (4, 5, 6, 7)
            AppendLine(lightFrustumCorners[4], lightFrustumCorners[5]);
            AppendLine(lightFrustumCorners[5], lightFrustumCorners[6]);
            AppendLine(lightFrustumCorners[6], lightFrustumCorners[7]);
            AppendLine(lightFrustumCorners[7], lightFrustumCorners[4]);

            // Connecting edges (0-4, 1-5, 2-6, 3-7)
            AppendLine(lightFrustumCorners[0], lightFrustumCorners[4]);
            AppendLine(lightFrustumCorners[1], lightFrustumCorners[5]);
            AppendLine(lightFrustumCorners[2], lightFrustumCorners[6]);
            AppendLine(lightFrustumCorners[3], lightFrustumCorners[7]);

            uint32_t frustumCount = (uint32_t)outIndices.size() - frustumStart;
            if (frustumCount > 0) {
                RenderBatch b;
                b.startIndex = frustumStart;
                b.indexCount = frustumCount;
                b.isUnlit = true;
                b.castShadows = false;
                b.receiveShadows = false;
                b.baseColor = {1.0f, 1.0f, 1.0f};
                outBatches.push_back(b);
            }
        }
    }

    void BuildSceneMesh(std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices, glm::vec3 cameraPos = {0.0f, 0.0f, 0.0f}) {
        std::vector<RenderBatch> batches;
        BuildSceneMesh(outVertices, outIndices, batches, cameraPos);
    }

    void AppendObjectEdgeOutline(
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        const GameObject& obj,
        const glm::vec3& cameraPos,
        const glm::vec3& outlineColor)
    {
        if (obj.mesh.vertices.empty() || obj.type == PrimitiveType::Empty || obj.isLight || IsLightPrimitive(obj.type) || obj.isCamera || IsCameraPrimitive(obj.type)) {
            return;
        }
        glm::mat4 model = GetWorldMatrix(obj);
        std::vector<std::pair<glm::vec3, glm::vec3>> edges;

        switch (obj.type) {
            case PrimitiveType::Cube: {
                float s = 0.5f;
                glm::vec3 c[8] = {
                    {-s, -s, -s}, { s, -s, -s}, { s, -s,  s}, {-s, -s,  s},
                    {-s,  s, -s}, { s,  s, -s}, { s,  s,  s}, {-s,  s,  s}
                };
                // 12 edges of cube
                edges.push_back({c[0], c[1]}); edges.push_back({c[1], c[2]});
                edges.push_back({c[2], c[3]}); edges.push_back({c[3], c[0]});
                edges.push_back({c[4], c[5]}); edges.push_back({c[5], c[6]});
                edges.push_back({c[6], c[7]}); edges.push_back({c[7], c[4]});
                edges.push_back({c[0], c[4]}); edges.push_back({c[1], c[5]});
                edges.push_back({c[2], c[6]}); edges.push_back({c[3], c[7]});
                break;
            }
            case PrimitiveType::Plane: {
                float hx = 1.25f, hz = 1.25f;
                glm::vec3 p0(-hx, 0.0f, -hz);
                glm::vec3 p1( hx, 0.0f, -hz);
                glm::vec3 p2( hx, 0.0f,  hz);
                glm::vec3 p3(-hx, 0.0f,  hz);
                edges.push_back({p0, p1}); edges.push_back({p1, p2});
                edges.push_back({p2, p3}); edges.push_back({p3, p0});
                int subX = 6, subZ = 6;
                for (int z = 1; z < subZ; ++z) {
                    float tz = (float)z / (float)subZ;
                    float posZ = -hz + tz * 2.5f;
                    edges.push_back({ {-hx, 0.0f, posZ}, {hx, 0.0f, posZ} });
                }
                for (int x = 1; x < subX; ++x) {
                    float tx = (float)x / (float)subX;
                    float posX = -hx + tx * 2.5f;
                    edges.push_back({ {posX, 0.0f, -hz}, {posX, 0.0f, hz} });
                }
                break;
            }
            case PrimitiveType::Pyramid: {
                float s = 0.5f, h = 1.2f;
                glm::vec3 apex(0.0f, h, 0.0f);
                glm::vec3 c[4] = {
                    {-s, 0.0f,  s},
                    { s, 0.0f,  s},
                    { s, 0.0f, -s},
                    {-s, 0.0f, -s}
                };
                edges.push_back({c[0], c[1]}); edges.push_back({c[1], c[2]});
                edges.push_back({c[2], c[3]}); edges.push_back({c[3], c[0]});
                edges.push_back({c[0], apex}); edges.push_back({c[1], apex});
                edges.push_back({c[2], apex}); edges.push_back({c[3], apex});
                break;
            }
            case PrimitiveType::Cylinder: {
                float r = 0.5f, halfH = 0.6f;
                const int segs = 32;
                for (int i = 0; i < segs; ++i) {
                    float a0 = 2.0f * (float)M_PI * (float)i / (float)segs;
                    float a1 = 2.0f * (float)M_PI * (float)(i + 1) / (float)segs;
                    edges.push_back({ {r * std::cos(a0),  halfH, r * std::sin(a0)},
                                      {r * std::cos(a1),  halfH, r * std::sin(a1)} });
                    edges.push_back({ {r * std::cos(a0), -halfH, r * std::sin(a0)},
                                      {r * std::cos(a1), -halfH, r * std::sin(a1)} });
                }
                for (int i = 0; i < 4; ++i) {
                    float a = (float)M_PI * 0.5f * (float)i;
                    edges.push_back({ {r * std::cos(a), -halfH, r * std::sin(a)},
                                      {r * std::cos(a),  halfH, r * std::sin(a)} });
                }
                break;
            }
            case PrimitiveType::Sphere: {
                float r = 0.6f;
                const int segs = 36;
                // Equator (XZ plane)
                for (int i = 0; i < segs; ++i) {
                    float a0 = 2.0f * (float)M_PI * (float)i / (float)segs;
                    float a1 = 2.0f * (float)M_PI * (float)(i + 1) / (float)segs;
                    edges.push_back({ {r * std::cos(a0), 0.0f, r * std::sin(a0)},
                                      {r * std::cos(a1), 0.0f, r * std::sin(a1)} });
                }
                // Meridian 1 (XY plane)
                for (int i = 0; i < segs; ++i) {
                    float a0 = 2.0f * (float)M_PI * (float)i / (float)segs;
                    float a1 = 2.0f * (float)M_PI * (float)(i + 1) / (float)segs;
                    edges.push_back({ {r * std::cos(a0), r * std::sin(a0), 0.0f},
                                      {r * std::cos(a1), r * std::sin(a1), 0.0f} });
                }
                // Meridian 2 (YZ plane)
                for (int i = 0; i < segs; ++i) {
                    float a0 = 2.0f * (float)M_PI * (float)i / (float)segs;
                    float a1 = 2.0f * (float)M_PI * (float)(i + 1) / (float)segs;
                    edges.push_back({ {0.0f, r * std::sin(a0), r * std::cos(a0)},
                                      {0.0f, r * std::sin(a1), r * std::cos(a1)} });
                }
                for (float ySign : {-0.3f, 0.3f}) {
                    float rLat = std::sqrt(std::max(0.0f, r * r - ySign * ySign));
                    for (int i = 0; i < segs; ++i) {
                        float a0 = 2.0f * (float)M_PI * (float)i / (float)segs;
                        float a1 = 2.0f * (float)M_PI * (float)(i + 1) / (float)segs;
                        edges.push_back({ {rLat * std::cos(a0), ySign, rLat * std::sin(a0)},
                                          {rLat * std::cos(a1), ySign, rLat * std::sin(a1)} });
                    }
                }
                break;
            }
            case PrimitiveType::Torus: {
                float rMain = 0.6f, rTube = 0.22f;
                const int mainSegs = 36;
                const int tubeSegs = 16;
                for (int i = 0; i < mainSegs; ++i) {
                    float a0 = 2.0f * (float)M_PI * (float)i / (float)mainSegs;
                    float a1 = 2.0f * (float)M_PI * (float)(i + 1) / (float)mainSegs;
                    float c0 = std::cos(a0), s0 = std::sin(a0);
                    float c1 = std::cos(a1), s1 = std::sin(a1);
                    edges.push_back({ {(rMain + rTube) * c0, 0.0f, (rMain + rTube) * s0},
                                      {(rMain + rTube) * c1, 0.0f, (rMain + rTube) * s1} });
                    edges.push_back({ {(rMain - rTube) * c0, 0.0f, (rMain - rTube) * s0},
                                      {(rMain - rTube) * c1, 0.0f, (rMain - rTube) * s1} });
                    edges.push_back({ {rMain * c0,  rTube, rMain * s0},
                                      {rMain * c1,  rTube, rMain * s1} });
                    edges.push_back({ {rMain * c0, -rTube, rMain * s0},
                                      {rMain * c1, -rTube, rMain * s1} });
                }
                for (int k = 0; k < 8; ++k) {
                    float u = 2.0f * (float)M_PI * (float)k / 8.0f;
                    float cosU = std::cos(u);
                    float sinU = std::sin(u);
                    for (int j = 0; j < tubeSegs; ++j) {
                        float v0 = 2.0f * (float)M_PI * (float)j / (float)tubeSegs;
                        float v1 = 2.0f * (float)M_PI * (float)(j + 1) / (float)tubeSegs;
                        glm::vec3 pA((rMain + rTube * std::cos(v0)) * cosU, rTube * std::sin(v0), (rMain + rTube * std::cos(v0)) * sinU);
                        glm::vec3 pB((rMain + rTube * std::cos(v1)) * cosU, rTube * std::sin(v1), (rMain + rTube * std::cos(v1)) * sinU);
                        edges.push_back({pA, pB});
                    }
                }
                break;
            }
            default: {
                struct EdgeKey {
                    uint32_t a, b;
                    bool operator==(const EdgeKey& o) const { return a == o.a && b == o.b; }
                };
                struct EdgeKeyHash {
                    size_t operator()(const EdgeKey& k) const { return ((size_t)k.a * 397) ^ (size_t)k.b; }
                };
                struct EdgeInfo {
                    glm::vec3 n1{0.0f};
                    glm::vec3 n2{0.0f};
                    int count = 0;
                };
                std::unordered_map<EdgeKey, EdgeInfo, EdgeKeyHash> edgeMap;
                const auto& vList = obj.mesh.vertices;
                const auto& iList = obj.mesh.indices;
                for (size_t i = 0; i + 2 < iList.size(); i += 3) {
                    uint32_t i0 = iList[i], i1 = iList[i + 1], i2 = iList[i + 2];
                    if (i0 >= vList.size() || i1 >= vList.size() || i2 >= vList.size()) continue;
                    glm::vec3 v0 = vList[i0].pos, v1 = vList[i1].pos, v2 = vList[i2].pos;
                    glm::vec3 c = glm::cross(v1 - v0, v2 - v0);
                    float cLen = glm::length(c);
                    if (cLen < 1e-6f || std::isnan(cLen)) continue;
                    glm::vec3 fn = c / cLen;
                    uint32_t tri[3] = { i0, i1, i2 };
                    for (int e = 0; e < 3; ++e) {
                        uint32_t ea = tri[e];
                        uint32_t eb = tri[(e + 1) % 3];
                        if (ea > eb) std::swap(ea, eb);
                        EdgeKey key{ ea, eb };
                        auto& info = edgeMap[key];
                        if (info.count == 0) info.n1 = fn;
                        else if (info.count == 1) info.n2 = fn;
                        info.count++;
                    }
                }
                for (const auto& kv : edgeMap) {
                    if (kv.second.count == 1 || glm::dot(kv.second.n1, kv.second.n2) < 0.96f) {
                        if (kv.first.a < vList.size() && kv.first.b < vList.size()) {
                            edges.push_back({ vList[kv.first.a].pos, vList[kv.first.b].pos });
                        }
                    }
                }
                break;
            }
        }

        // Draw camera-facing ribbons along every edge transformed to world space
        for (const auto& e : edges) {
            glm::vec3 wA = glm::vec3(model * glm::vec4(e.first, 1.0f));
            glm::vec3 wB = glm::vec3(model * glm::vec4(e.second, 1.0f));
            GeometryBuilder::AppendCameraFacingLine(outVertices, outIndices, wA, wB, cameraPos, 1.25f, outlineColor);
        }
    }

    void LoadDefaultLevel() { LoadDefaultScene(); }
    void BuildLevelMesh(std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices,
                        std::vector<RenderBatch>& outBatches, const glm::vec3& cameraPos = glm::vec3(0.0f)) {
        BuildSceneMesh(outVertices, outIndices, outBatches, cameraPos);
    }
};

using Level = Scene;
