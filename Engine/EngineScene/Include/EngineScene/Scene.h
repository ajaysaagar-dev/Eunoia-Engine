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
#include <EnginePlatform/InputSystem.h>
#include <Collisions/CollisionSystem.h>
#include <SFLOD/SFLODSystem.h>
#include <Physics/PhysicsSystem.h>
#include <EngineCore/JobSystem.h>
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
    float intensity = 1.0f;
    float range = 10.0f;
    float attenuation = 2.0f;
    float rangeSmoothing = 0.2f;
    bool enabled = true;
    bool castShadows = true;
    float shadowStrength = 0.85f;
    float shadowBias = 0.0012f;
    int shadowResolution = 1024;
    float shadowDistance = 250.0f;

    // Performance & Distance Culling
    bool enableDistanceCulling = false;
    float minVisibleDistance = 50.0f;
    float maxVisibleDistance = 100.0f;

    // Spot Light
    float innerConeAngle = 0.0f;
    float outerConeAngle = 45.0f;
    float coneFalloff = 1.0f;

    // Area / Rect Light
    int areaShape = 0;
    float width = 1.0f;
    float height = 1.0f;
    float radius = 0.025f;
    float length = 0.0f;
    bool twoSided = false;

    // Wicked Engine attributes
    bool volumetric = false;
    float volumetric_boost = 1.0f;
    float volumetricScattering = 0.66f;
    float volumetricIntensity = 1.0f;
    int volumetricSteps = 16;
    float volumetricMaxDistance = 120.0f;
    int forced_shadow_resolution = -1;
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
    bool skyboxAutoRotate = false;
    float skyboxAutoRotateSpeed = 1.0f; // degrees per second
    glm::vec3 skyboxTint{1.0f, 1.0f, 1.0f};
    float skyboxAmbientContribution = 0.5f;

    // Skybox Based Lighting Settings (default disabled)
    bool enableSkyboxLighting = false;
    float skyboxLightingIntensity = 1.0f;
    glm::vec3 skyboxLightingColor{1.0f, 1.0f, 1.0f};
    float skyboxLightingSpecular = 1.0f;
    float skyboxLightingDiffuse = 1.0f;

    // Post Processing Settings (Color Grading & Exposure)
    // Disabled by default for a level as requested
    bool enablePostProcessing = false;
    float postExposureEV = 0.0f;
    float postExposureMultiplier = 1.0f;
    int postTonemapper = 1; // 0=None/Linear, 1=ACES Filmic, 2=Reinhard
    float postSaturation = 1.0f;
    float postContrast = 1.0f;
    float postGamma = 1.0f;
    glm::vec3 postColorFilter{1.0f, 1.0f, 1.0f};
    float postColorTemperature = 6500.0f;
    float postColorTint = 0.0f;
    float postSharpenAmount = 0.0f;   // 0.0 = Disabled/Off, 0.0..1.0 sharpen filter intensity
    float postDenoiserAmount = 0.0f;  // 0.0 = Disabled/Off, 0.0..1.0 edge-preserving bilateral denoiser intensity

    // Screen Space Ambient Occlusion (SSAO / HBAO / MSAO / RTAO - WickedEngine Reference)
    // Disabled by default for new levels as requested
    bool enableAO = false;
    int aoType = 0;                   // 0=Disabled, 1=SSAO, 2=HBAO, 3=MSAO, 4=RTAO
    float aoPower = 1.0f;             // Intensity exponent (WickedEngine default 1.0, range 0.25 - 8.0)
    float aoRange = 1.0f;             // Sample distance / Ray length (WickedEngine default 1.0, range 0.1 - 20.0 m)
    int aoSampleCount = 16;           // Sample count (WickedEngine default 16, range 1 - 32)
    float aoBias = 0.025f;            // Angle/depth bias to eliminate self-occlusion artifacts
    bool aoBlur = true;               // Edge-preserving bilateral depth filter
    float aoBlurRadius = 2.0f;        // Bilateral filter radius
    bool aoTemporal = true;           // Temporal accumulation filter
    float aoTemporalBlend = 0.85f;

    // Volumetric Height Fog (Wicked Engine fogHF.hlsli reference)
    // Disabled by default for new levels as requested
    bool enableFog = false;
    float fogStart = 10.0f;           // Distance where fog begins
    float fogDensity = 0.005f;        // Global exponential fog density
    float fogHeightStart = 0.0f;      // Bottom altitude
    float fogHeightEnd = 50.0f;       // Top altitude
    glm::vec3 fogColor{0.7f, 0.75f, 0.85f}; // Fog scattering color

    // Directional Light Volumetric God Rays (Wicked Engine Technique)
    bool directionalVolumetric = false;
    float directionalVolumetricBoost = 1.0f;
    float directionalVolumetricScattering = 0.66f;
    float directionalVolumetricIntensity = 1.0f;
    int directionalVolumetricSteps = 16;
    float directionalVolumetricMaxDistance = 120.0f;

    // Shadow Mapping Parameters (Hardware D32_FLOAT 2048x2048 PCF)
    bool enableShadows = true;
    float shadowStrength = 0.85f;
    float shadowBias = 0.0012f;
    int shadowResolution = 2048;
    float shadowDistance = 250.0f;
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

    // Visualization Options (dev.md Viewport Visualize Menu)
    bool showAllCollisions = false;
    bool showAllBounds = false;

    // Animation
    bool playAnimations = true;

    // Hierarchy and Tree View State persistence (Outliner and Content Browser)
    std::unordered_map<std::string, bool> outlinerNodeOpenState;         // Actor identifier/name -> isExpanded
    std::unordered_map<std::string, bool> contentBrowserFolderOpenState; // Virtual folder path -> isExpanded

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

    std::vector<GameObject*> FindObjectsWithTag(const std::string& tag) {
        std::vector<GameObject*> result;
        for (auto& obj : objects) {
            if (obj.HasTag(tag)) result.push_back(&obj);
        }
        return result;
    }

    std::vector<const GameObject*> FindObjectsWithTag(const std::string& tag) const {
        std::vector<const GameObject*> result;
        for (const auto& obj : objects) {
            if (obj.HasTag(tag)) result.push_back(&obj);
        }
        return result;
    }

    GameObject* FindObjectWithTag(const std::string& tag) {
        for (auto& obj : objects) {
            if (obj.HasTag(tag)) return &obj;
        }
        return nullptr;
    }

    const GameObject* FindObjectWithTag(const std::string& tag) const {
        for (const auto& obj : objects) {
            if (obj.HasTag(tag)) return &obj;
        }
        return nullptr;
    }

    GameObject* FindWithTag(const std::string& tag) { return FindObjectWithTag(tag); }
    const GameObject* FindWithTag(const std::string& tag) const { return FindObjectWithTag(tag); }

    GameObject* FindObjectByName(const std::string& name) {
        for (auto& obj : objects) {
            if (obj.name == name) return &obj;
        }
        return nullptr;
    }

    const GameObject* FindObjectByName(const std::string& name) const {
        for (const auto& obj : objects) {
            if (obj.name == name) return &obj;
        }
        return nullptr;
    }

    std::string GetUniqueObjectName(const std::string& baseName, int excludeId = -1) const {
        std::unordered_set<std::string> existingNames;
        for (const auto& obj : objects) {
            if (obj.id != excludeId) {
                existingNames.insert(obj.name);
            }
        }

        // Clean base name: trim trailing numbers or " (Copy)", "(1)", etc.
        std::string root = baseName;
        // Strip " (Copy)" if present
        const std::string copySuffix = " (Copy)";
        while (root.size() >= copySuffix.size() &&
               root.compare(root.size() - copySuffix.size(), copySuffix.size(), copySuffix) == 0) {
            root = root.substr(0, root.size() - copySuffix.size());
        }

        // Strip trailing digits and whitespace: e.g. "Cube 1" -> "Cube"
        size_t endIdx = root.find_last_not_of("0123456789");
        if (endIdx != std::string::npos && endIdx + 1 < root.size()) {
            if (root[endIdx] == ' ' || root[endIdx] == '_') {
                root = root.substr(0, endIdx);
            }
        }

        // If root is empty, fallback to "Actor"
        if (root.empty()) root = "Actor";

        if (!existingNames.count(root)) {
            return root;
        }

        int suffix = 1;
        while (true) {
            std::string candidate = root + " " + std::to_string(suffix);
            if (!existingNames.count(candidate)) {
                return candidate;
            }
            suffix++;
        }
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
            if (!std::isfinite(obj.position.x) || !std::isfinite(obj.position.y) || !std::isfinite(obj.position.z)) continue;
            if (obj.position.y < -200.0f) continue; // Ignore out-of-bounds fallen objects

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
                if (std::isfinite(wp.x) && std::isfinite(wp.y) && std::isfinite(wp.z) && wp.y >= -200.0f) {
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

    // Add a standalone Collision Volume or Trigger Volume actor
    GameObject& AddCollisionActor(CollisionShapeType shapeType, glm::vec3 pos = {0.0f, 0.0f, 0.0f}, bool isTrigger = false, int parentId = -1) {
        std::string shapeName = GetCollisionShapeTypeName(shapeType);
        std::string baseName = isTrigger ? (std::string(shapeName).substr(0, shapeName.find(" Collision")) + " Trigger") : shapeName;
        if (onPreChange) onPreChange("Add " + baseName);
        int id = nextId++;
        std::string name = baseName + " " + std::to_string(id);
        objects.emplace_back(id, name, PrimitiveType::Empty, pos, glm::vec3{1.0f});
        GameObject& obj = objects.back();
        obj.mesh.vertices.clear();
        obj.mesh.indices.clear();
        obj.parentId = parentId;
        if (parentId != -1) {
            GameObject* parent = FindObject(parentId);
            if (parent) parent->childIds.push_back(id);
        }

        CollisionModifier mod(shapeType, isTrigger ? baseName : shapeName);
        mod.isTrigger = isTrigger;
        mod.showWireframe = true;
        if (isTrigger) {
            mod.preset = "TriggerOnly";
            mod.wireframeColor = glm::vec4(1.0f, 0.55f, 0.0f, 1.0f);
        } else {
            mod.preset = "BlockAll";
            mod.wireframeColor = glm::vec4(0.0f, 1.0f, 0.25f, 1.0f);
        }
        obj.modifiers.push_back(mod);

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

    GameObject& AddTerrain(glm::vec3 pos = {0.0f, 0.0f, 0.0f}, float width = 64.0f, float length = 64.0f, int resolution = 128, int parentId = -1) {
        if (onPreChange) onPreChange("Add Terrain");
        int id = nextId++;
        std::string finalName = "Terrain " + std::to_string(id);
        objects.emplace_back(id, finalName, PrimitiveType::Terrain, pos);
        GameObject& obj = objects.back();

        obj.isTerrain = true;
        obj.terrain.InitializeGrid(width, length, resolution);
        obj.terrain.RebuildTerrainMesh(obj.mesh);

        // Natural landscape base color & PBR defaults
        obj.color = glm::vec3(0.35f, 0.55f, 0.30f); // Default terrain meadow green
        obj.roughness = 0.85f;
        obj.metallic = 0.0f;
        obj.specular = 0.25f;
        obj.twoSided = false;
        obj.castShadows = true;
        obj.receiveShadows = true;

        obj.parentId = parentId;
        if (parentId != -1) {
            GameObject* parent = FindObject(parentId);
            if (parent) parent->childIds.push_back(id);
        }

        selectedId = id;
        return obj;
    }

    GameObject& AddUserInterface(glm::vec3 pos = {0.0f, 1.5f, 0.0f}, const std::string& assetPath = "", int parentId = -1) {
        if (onPreChange) onPreChange("Add User Interface");
        int id = nextId++;
        std::string name = "UserInterface_" + std::to_string(id);
        objects.emplace_back(id, name, PrimitiveType::UserInterface, pos);
        GameObject& obj = objects.back();

        obj.isUserInterface = true;
        obj.ui3d.enabled = true;
        obj.ui3d.uiAssetPath = assetPath;
        obj.ui3d.space = EunoiaUI::UI3DSpaceType::World;
        obj.ui3d.size = glm::vec2(2.0f, 1.125f);
        obj.twoSided = true;

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
            pl.rangeSmoothing = obj.light.rangeSmoothing;
            pl.enabled = true;
            pl.castShadows = true;
            pl.shadowStrength = obj.light.shadowStrength;
            pl.shadowBias = obj.light.shadowBias;
            pl.shadowResolution = obj.light.shadowResolution;
            pl.shadowDistance = obj.light.shadowDistance;
            pl.enableDistanceCulling = obj.light.enableDistanceCulling;
            pl.minVisibleDistance = obj.light.minVisibleDistance;
            pl.maxVisibleDistance = obj.light.maxVisibleDistance;
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
                glm::mat4 worldMat = GetWorldMatrix(obj);
                glm::vec3 worldPos = GetWorldPosition(obj);

                glm::vec3 localBaseDir = (obj.light.type == LightType::Directional) ? glm::vec3(0.0f, -1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, -1.0f);
                glm::vec3 unnormDir = glm::vec3(worldMat * glm::vec4(localBaseDir, 0.0f));
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
                    shadowDistance = obj.light.shadowDistance;
                    directionalVolumetric = obj.light.IsVolumetricsEnabled();
                    directionalVolumetricBoost = obj.light.volumetric_boost;
                    directionalVolumetricScattering = obj.light.volumetricScattering;
                    directionalVolumetricIntensity = obj.light.volumetricIntensity;
                    directionalVolumetricSteps = obj.light.volumetricSteps;
                    directionalVolumetricMaxDistance = obj.light.volumetricMaxDistance;
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
                    obj.light.type == LightType::Rectangle || obj.type == PrimitiveType::AreaLight) {
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
                        pl.rangeSmoothing = obj.light.rangeSmoothing;
                        pl.enabled = obj.light.enabled;
                        pl.castShadows = obj.light.castShadows;
                        pl.shadowStrength = obj.light.shadowStrength;
                        pl.shadowBias = obj.light.shadowBias;
                        pl.shadowResolution = obj.light.shadowResolution;
                        pl.shadowDistance = obj.light.shadowDistance;
                        pl.enableDistanceCulling = obj.light.enableDistanceCulling;
                        pl.minVisibleDistance = obj.light.minVisibleDistance;
                        pl.maxVisibleDistance = obj.light.maxVisibleDistance;
                        pl.innerConeAngle = obj.light.innerConeAngle;
                        pl.outerConeAngle = obj.light.outerConeAngle;
                        pl.coneFalloff = obj.light.coneFalloff;
                        pl.areaShape = obj.light.areaShape;
                        pl.width = obj.light.width;
                        pl.height = obj.light.height;
                        pl.radius = obj.light.radius;
                        pl.length = obj.light.length;
                        pl.twoSided = obj.light.twoSided;
                        pl.volumetric = obj.light.IsVolumetricsEnabled();
                        pl.volumetric_boost = obj.light.volumetric_boost;
                        pl.volumetricScattering = obj.light.volumetricScattering;
                        pl.volumetricIntensity = obj.light.volumetricIntensity;
                        pl.volumetricSteps = obj.light.volumetricSteps;
                        pl.volumetricMaxDistance = obj.light.volumetricMaxDistance;
                        pl.forced_shadow_resolution = obj.light.forced_shadow_resolution;
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
            // Compute overall bounding box of the entire composite model
            glm::vec3 modelMin(1e9f);
            glm::vec3 modelMax(-1e9f);
            bool anyValidV = false;
            for (const auto& im : model.meshes) {
                for (const auto& v : im.vertices) {
                    if (!std::isnan(v.pos.x) && !std::isnan(v.pos.y) && !std::isnan(v.pos.z) &&
                        !std::isinf(v.pos.x) && !std::isinf(v.pos.y) && !std::isinf(v.pos.z)) {
                        modelMin = glm::min(modelMin, v.pos);
                        modelMax = glm::max(modelMax, v.pos);
                        anyValidV = true;
                    }
                }
            }
            // Model base origin: align X and Z to center, Y to bottom
            glm::vec3 modelBaseOffset = anyValidV 
                ? glm::vec3((modelMin.x + modelMax.x) * 0.5f, modelMin.y, (modelMin.z + modelMax.z) * 0.5f)
                : glm::vec3(0.0f);

            // Root actor (pivot point at pos)
            int rootId = nextId++;
            std::filesystem::path p(filePath);
            std::string rootName = p.stem().string();
            objects.emplace_back(rootId, rootName, PrimitiveType::ImportedMesh, pos, glm::vec3{1.0f});
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

                // Child is located at local origin (0, 0, 0) relative to root, vertices are in unified model space
                objects.emplace_back(childId, childName, PrimitiveType::ImportedMesh, glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{1.0f});
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
                    child.materialName          = im.material.name;
                    child.baseColorTexture      = im.material.baseColorTexture;
                    child.normalTexture         = im.material.normalTexture;
                    child.roughnessTexture      = im.material.roughnessTexture;
                    child.metallicTexture       = im.material.metallicTexture;
                    child.aoTexture             = im.material.aoTexture;
                    child.emissionTexture       = im.material.emissionTexture;
                    child.opacityTexture        = im.material.opacityTexture;
                    child.color                 = im.material.baseColor;
                    child.metallic              = im.material.metallic;
                    child.roughness             = im.material.roughness;
                    child.normalStrength        = im.material.normalStrength;
                    child.normalMapYFlip        = im.material.normalMapYFlip;
                    child.metallicChannel       = im.material.metallicChannel;
                    child.roughnessChannel      = im.material.roughnessChannel;
                    child.aoChannel             = im.material.aoChannel;
                    child.blendMode             = im.material.blendMode;
                    child.opacity               = im.material.opacity;
                    child.opacityMaskClipValue  = im.material.opacityMaskClipValue;
                    child.emissiveColor         = im.material.emissiveColor;
                    child.emissiveIntensity     = im.material.emissiveIntensity;

                    AddEngineLog("LogImport",
                        "Auto-assigned material \"" + im.material.name + "\" to \"" + child.name + "\"" +
                        (im.material.baseColorTexture.empty() ? " (no base color texture found)" : ""), 0);
                }

                // Adjust vertices by model base offset so root position 'pos' aligns to model bottom-center
                child.mesh.vertices = im.vertices;
                child.mesh.indices  = im.indices;
                child.mesh.lodIndices = im.lodIndices;
                for (auto& v : child.mesh.vertices) {
                    if (std::isnan(v.pos.x)||std::isnan(v.pos.y)||std::isnan(v.pos.z)||
                        std::isinf(v.pos.x)||std::isinf(v.pos.y)||std::isinf(v.pos.z)) {
                        v.pos = glm::vec3(0.0f);
                    } else {
                        v.pos -= modelBaseOffset;
                    }
                }
                child.mesh.CalculateAABB();
                if (child.mesh.lodIndices.empty()) {
                    child.mesh.GenerateLODs();
                }
                uint32_t vCount = (uint32_t)child.mesh.vertices.size();
                for (uint32_t& idx : child.mesh.indices) { if (idx >= vCount) idx = 0; }

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
            obj.materialName          = mat.name;
            obj.baseColorTexture      = mat.baseColorTexture;
            obj.normalTexture         = mat.normalTexture;
            obj.roughnessTexture      = mat.roughnessTexture;
            obj.metallicTexture       = mat.metallicTexture;
            obj.aoTexture             = mat.aoTexture;
            obj.emissionTexture       = mat.emissionTexture;
            obj.opacityTexture        = mat.opacityTexture;
            obj.color                 = mat.baseColor;
            obj.metallic              = mat.metallic;
            obj.roughness             = mat.roughness;
            obj.normalStrength        = mat.normalStrength;
            obj.normalMapYFlip        = mat.normalMapYFlip;
            obj.metallicChannel       = mat.metallicChannel;
            obj.roughnessChannel      = mat.roughnessChannel;
            obj.aoChannel             = mat.aoChannel;
            obj.blendMode             = mat.blendMode;
            obj.opacity               = mat.opacity;
            obj.opacityMaskClipValue  = mat.opacityMaskClipValue;
            obj.emissiveColor         = mat.emissiveColor;
            obj.emissiveIntensity     = mat.emissiveIntensity;

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
                obj.mesh.CalculateAABB();
                obj.mesh.GenerateLODs();
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

    void SetMeshLODRecursive(int objId, bool enabled) {
        GameObject* obj = FindObject(objId);
        if (!obj) return;
        obj->enableLOD = enabled;
        for (int childId : obj->childIds) {
            SetMeshLODRecursive(childId, enabled);
        }
    }

    // When setting static to any mesh including empty actor, make all children static recursively
    void SetObjectMobility(int objId, Mobility newMobility, bool recursive = true) {
        GameObject* obj = FindObject(objId);
        if (!obj) return;
        obj->mobility = newMobility;
        if (recursive && newMobility == Mobility::Static) {
            for (int childId : obj->childIds) {
                SetObjectMobility(childId, newMobility, true);
            }
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
                copy.name = GetUniqueObjectName(srcObj->name);
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
                copy.name = GetUniqueObjectName(srcObj->name);
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
        skyboxAutoRotate = false;
        skyboxAutoRotateSpeed = 1.0f;
        skyboxTint = glm::vec3(1.0f, 1.0f, 1.0f);
        skyboxAmbientContribution = 0.5f;
        enableSkyboxLighting = false;
        skyboxLightingIntensity = 1.0f;
        skyboxLightingColor = glm::vec3(1.0f, 1.0f, 1.0f);
        skyboxLightingSpecular = 1.0f;
        skyboxLightingDiffuse = 1.0f;
        enablePostProcessing = false;
        postExposureEV = 0.0f;
        postExposureMultiplier = 1.0f;
        postTonemapper = 1;
        postSaturation = 1.0f;
        postContrast = 1.0f;
        postGamma = 1.0f;
        postColorFilter = glm::vec3(1.0f, 1.0f, 1.0f);
        postColorTemperature = 6500.0f;
        postColorTint = 0.0f;
        postSharpenAmount = 0.0f;
        postDenoiserAmount = 0.0f;
        enableAO = false;
        aoType = 0;
        aoPower = 1.0f;
        aoRange = 1.0f;
        aoSampleCount = 16;
        aoBias = 0.025f;
        aoBlur = true;
        aoBlurRadius = 2.0f;
        aoTemporal = true;
        aoTemporalBlend = 0.85f;
        outlinerNodeOpenState.clear();
        contentBrowserFolderOpenState.clear();
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
                    if (dynamic_cast<DynamicScriptBehaviour*>(b.get()) == nullptr || b->GetProperties().empty()) {
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

        Eunoia::Physics::PhysicsSystem::Get().ResetSimulation(*this);

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
        InputSystem::Get().SetCursorLocked(false);
        Eunoia::Physics::PhysicsSystem::Get().ResetSimulation(*this);

        // Cleanup runtime state
        for (auto& obj : objects) {
            for (auto& b : obj.behaviours) {
                if (!b) continue;
                b->ClearAllTimers();
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

        // Skybox automatic rotation (smooth continuous moving sky)
        if (enableSkybox && skyboxAutoRotate) {
            skyboxRotation = std::fmod(skyboxRotation + skyboxAutoRotateSpeed * dt, 360.0f);
            if (skyboxRotation < 0.0f) skyboxRotation += 360.0f;
        }

        if (isPlayMode) {
            // Update global World Delta Seconds (dev.md: "the global var called 'World Delta Seconds' use as a DeltaTime")
            WorldDeltaSeconds = dt;
            World_Delta_Seconds = dt;
            worldDeltaSeconds = dt;
            WorldDeltaTime = dt;

            for (auto& obj : objects) {
                obj.scene = this;
            }

            // 1. FixedUpdate & Physics Simulation
            fixedTimeAccumulator += dt;
            const float fixedStep = 1.0f / 60.0f;
            while (fixedTimeAccumulator >= fixedStep) {
                for (auto& obj : objects) {
                    for (auto& b : obj.behaviours) {
                        if (b && b->IsEnabled()) b->FixedUpdate(fixedStep);
                    }
                }
                // Step physics simulation
                Eunoia::Physics::PhysicsSystem::Get().StepSimulation(*this, fixedStep);
                fixedTimeAccumulator -= fixedStep;
            }

            // 2. Update
            for (auto& obj : objects) {
                for (auto& b : obj.behaviours) {
                    if (b && b->IsEnabled()) {
                        b->RefreshPropertiesFromSource();
                        b->UpdateTimers(dt);
                        b->Update(dt);
                    }
                }
                obj.Update(dt);
            }

            // 3. LateUpdate
            for (auto& obj : objects) {
                for (auto& b : obj.behaviours) {
                    if (b && b->IsEnabled()) b->LateUpdate(dt);
                }
            }
            SyncLightPositionsFromActors();
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
    float distSq = 0.0f;
    std::string albedoTex;
    std::string normalTex;
    std::string roughTex;
    std::string metalTex;
    std::string aoTex;
    std::string opacityTex;
    std::string emissionTex;
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
    bool twoSided = false;
    bool isVisible = true;
    bool isShadowVisible = true; // Wicked Engine: Directional Light frustum visibility flag
    int activeLOD = 0;           // Wicked Engine: Level of detail used for this batch
    bool disableStreaming = false; // Wicked Engine: Disable texture streaming / retain full resolution
    float useVertexAlpha = 0.0f;   // Continuous multi-layer terrain material alpha blending
};

    void BuildSceneMesh(
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        std::vector<RenderBatch>& outBatches,
        glm::vec3 cameraPos = {0.0f, 0.0f, 0.0f},
        const glm::mat4* pViewProj = nullptr)
    {
        outVertices.clear();
        outIndices.clear();
        outBatches.clear();
        SFLOD::SFLODSystem::Get().ResetFrameStats();

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
                b.twoSided = true;
                b.castShadows = false;
                b.receiveShadows = true;
                b.baseColor = {1.0f, 1.0f, 1.0f};
                b.isVisible = true;
                outBatches.push_back(b);
            }
        }

        // 2. All GameObjects (Rendered with Hardware GPU Shaders & Full 2K Texture Mapping)
        // WickedEngine Culling Architecture:
        // Keep all geometry in deterministic vertex/index buffer positions (preventing buffer shuffling & TAA ghosting).
        // Frustum culling marks each batch's isVisible flag so the GPU draw call can be skipped during viewport rasterization.
        struct CandidateEntry {
            GameObject* obj;
            glm::mat4 model;
            float distSq;
            bool isVisible = true;
            bool isShadowVisible = true;
            bool isTrans = false;
            bool isValid = false;
            uint32_t selectedLOD = 0;
            int sflodLOD = 0;
        };

        // Extract 6 frustum planes (Gribb & Hartmann) if viewProj is available
        glm::vec4 frustumPlanes[6];
        bool hasFrustum = (pViewProj != nullptr);
        if (hasFrustum) {
            const glm::mat4& m = *pViewProj;
            frustumPlanes[0] = glm::vec4(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0], m[3][3] + m[3][0]); // Left
            frustumPlanes[1] = glm::vec4(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0], m[3][3] - m[3][0]); // Right
            frustumPlanes[2] = glm::vec4(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1], m[3][3] + m[3][1]); // Bottom
            frustumPlanes[3] = glm::vec4(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1], m[3][3] - m[3][1]); // Top
            frustumPlanes[4] = glm::vec4(m[0][2],            m[1][2],            m[2][2],            m[3][2]);            // Near [0..1]
            frustumPlanes[5] = glm::vec4(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2], m[3][3] - m[3][2]); // Far

            for (int p = 0; p < 6; ++p) {
                float len = glm::length(glm::vec3(frustumPlanes[p]));
                if (len > 1e-6f) frustumPlanes[p] /= len;
            }
        }

        // Wicked Engine Exact 8-Corner OBB Frustum Test Helper
        auto TestOBBFrustum = [](const glm::vec3& bMin, const glm::vec3& bMax, const glm::mat4& model, const glm::vec4 planes[6]) -> bool {
            glm::vec3 corners[8] = {
                glm::vec3(model * glm::vec4(bMin.x, bMin.y, bMin.z, 1.0f)),
                glm::vec3(model * glm::vec4(bMax.x, bMin.y, bMin.z, 1.0f)),
                glm::vec3(model * glm::vec4(bMin.x, bMax.y, bMin.z, 1.0f)),
                glm::vec3(model * glm::vec4(bMax.x, bMax.y, bMin.z, 1.0f)),
                glm::vec3(model * glm::vec4(bMin.x, bMin.y, bMax.z, 1.0f)),
                glm::vec3(model * glm::vec4(bMax.x, bMin.y, bMax.z, 1.0f)),
                glm::vec3(model * glm::vec4(bMin.x, bMax.y, bMax.z, 1.0f)),
                glm::vec3(model * glm::vec4(bMax.x, bMax.y, bMax.z, 1.0f))
            };

            for (int p = 0; p < 6; ++p) {
                const glm::vec3 normal = glm::vec3(planes[p]);
                const float d = planes[p].w;
                int outsideCount = 0;
                for (int c = 0; c < 8; ++c) {
                    if (glm::dot(normal, corners[c]) + d < 0.0f) {
                        outsideCount++;
                    }
                }
                if (outsideCount == 8) {
                    return false; // All 8 corners are outside this plane
                }
            }
            return true;
        };

        // Wicked Engine Perspective-Safe Screen-Space Coverage LOD Evaluation (wiScene.cpp:8975)
        auto ComputeObjectLOD = [](const GameObject& obj, const glm::mat4& model, const glm::mat4* viewProj) -> uint32_t {
            if (!obj.enableLOD) return 0;
            uint32_t lodCount = obj.mesh.GetLODCount();
            if (lodCount <= 1) return 0;
            if (obj.forcedLOD >= 0) {
                return (uint32_t)glm::clamp(obj.forcedLOD, obj.minLOD, std::min(obj.maxLOD, (int)lodCount - 1));
            }
            if (!viewProj) return (uint32_t)glm::clamp(0, obj.minLOD, std::min(obj.maxLOD, (int)lodCount - 1));

            const glm::vec3& bMin = obj.mesh.aabbValid ? obj.mesh.aabbMin : glm::vec3(-0.5f);
            const glm::vec3& bMax = obj.mesh.aabbValid ? obj.mesh.aabbMax : glm::vec3(0.5f);
            glm::vec3 corners[8] = {
                glm::vec3(bMin.x, bMin.y, bMin.z), glm::vec3(bMax.x, bMin.y, bMin.z),
                glm::vec3(bMin.x, bMax.y, bMin.z), glm::vec3(bMax.x, bMax.y, bMin.z),
                glm::vec3(bMin.x, bMin.y, bMax.z), glm::vec3(bMax.x, bMin.y, bMax.z),
                glm::vec3(bMin.x, bMax.y, bMax.z), glm::vec3(bMax.x, bMax.y, bMax.z)
            };

            glm::vec2 screenMin(1e7f), screenMax(-1e7f);
            const glm::mat4 mvp = (*viewProj) * model;
            for (int i = 0; i < 8; ++i) {
                glm::vec4 clip = mvp * glm::vec4(corners[i], 1.0f);
                float w = std::max(clip.w, 1e-4f);
                glm::vec2 ndc = glm::vec2(clip.x / w, clip.y / w);
                glm::vec2 uv = ndc * glm::vec2(0.5f, -0.5f) + glm::vec2(0.5f, 0.5f);
                uv = glm::clamp(uv, glm::vec2(-1.0f), glm::vec2(2.0f)); // Bound near-plane explosion
                screenMin = glm::min(screenMin, uv);
                screenMax = glm::max(screenMax, uv);
            }

            float maxdim = std::max(screenMax.x - screenMin.x, screenMax.y - screenMin.y);
            maxdim = std::max(maxdim, 1e-5f);

            // Wicked Engine continuous screen-space coverage LOD scaling
            // Maps screen coverage smoothly across the available LOD range [0 .. lodCount - 1]
            float maxLodIdx = (float)(lodCount - 1);
            float coverageLod = (std::log2(1.0f / maxdim) + obj.lodBias) * (maxLodIdx / 6.0f);
            int selectedLod = (int)std::floor(coverageLod);
            selectedLod = glm::clamp(selectedLod, obj.minLOD, std::min(obj.maxLOD, (int)lodCount - 1));
            return (uint32_t)selectedLod;
        };

        // SFLOD Continuous 20-Level Screen-Space Coverage LOD Evaluation
        // Evaluates dynamically across 20 virtual LOD levels, completely independent of mesh LOD!
        auto ComputeSFLOD = [](const GameObject& obj, const glm::mat4& model, const glm::mat4* viewProj) -> int {
            if (!obj.enableSFLOD) return 0;
            if (obj.forcedLOD >= 0) {
                return glm::clamp(obj.forcedLOD, 0, 20);
            }
            if (!viewProj) return 0;

            const glm::vec3& bMin = obj.mesh.aabbValid ? obj.mesh.aabbMin : glm::vec3(-0.5f);
            const glm::vec3& bMax = obj.mesh.aabbValid ? obj.mesh.aabbMax : glm::vec3(0.5f);
            glm::vec3 corners[8] = {
                glm::vec3(bMin.x, bMin.y, bMin.z), glm::vec3(bMax.x, bMin.y, bMin.z),
                glm::vec3(bMin.x, bMax.y, bMin.z), glm::vec3(bMax.x, bMax.y, bMin.z),
                glm::vec3(bMin.x, bMin.y, bMax.z), glm::vec3(bMax.x, bMin.y, bMax.z),
                glm::vec3(bMin.x, bMax.y, bMax.z), glm::vec3(bMax.x, bMax.y, bMax.z)
            };

            glm::vec2 screenMin(1e7f), screenMax(-1e7f);
            const glm::mat4 mvp = (*viewProj) * model;
            for (int i = 0; i < 8; ++i) {
                glm::vec4 clip = mvp * glm::vec4(corners[i], 1.0f);
                float w = std::max(clip.w, 1e-4f);
                glm::vec2 ndc = glm::vec2(clip.x / w, clip.y / w);
                glm::vec2 uv = ndc * glm::vec2(0.5f, -0.5f) + glm::vec2(0.5f, 0.5f);
                uv = glm::clamp(uv, glm::vec2(-1.0f), glm::vec2(2.0f));
                screenMin = glm::min(screenMin, uv);
                screenMax = glm::max(screenMax, uv);
            }

            float maxdim = std::max(screenMax.x - screenMin.x, screenMax.y - screenMin.y);
            maxdim = std::max(maxdim, 1e-5f);

            // Continuous 20 LOD levels: log2(1.0 / maxdim) produces 0 at full screen, scaling smoothly up to 20
            float coverageLod = (std::log2(1.0f / maxdim) + obj.sflodLODBias) * (20.0f / 6.0f);
            int selectedLod = (int)std::floor(coverageLod);
            selectedLod = glm::clamp(selectedLod, 0, obj.sflodMaxLOD);
            return selectedLod;
        };

        std::vector<CandidateEntry> opaqueEntries;
        std::vector<CandidateEntry> transparentEntries;
        const size_t numObjects = objects.size();
        std::vector<CandidateEntry> candidateList(numObjects);

        if (numObjects >= 16) {
            EngineCore::JobContext jobCtx;
            uint32_t groupSize = std::max(8u, (uint32_t)(numObjects / (EngineCore::JobSystem::Get().WorkerCount() * 2 + 1)));
            EngineCore::JobSystem::Get().Dispatch(jobCtx, (uint32_t)numObjects, groupSize, [&](EngineCore::JobArgs args) {
                uint32_t i = args.jobIndex;
                auto& obj = objects[i];
                if (!obj.visible || obj.isCamera || IsCameraPrimitive(obj.type) || obj.isWaterBody || obj.type == PrimitiveType::WaterBody) return;
                if (obj.mesh.vertices.empty() || obj.mesh.indices.empty()) return;

                glm::mat4 model = GetWorldMatrix(obj);
                glm::vec3 objPos = glm::vec3(model[3]);
                glm::vec3 diff = cameraPos - objPos;
                float distSq = glm::dot(diff, diff);

                bool isBatchVisible = true;
                if (hasFrustum) {
                    const glm::vec3& bMin = obj.mesh.aabbValid ? obj.mesh.aabbMin : glm::vec3(-0.5f);
                    const glm::vec3& bMax = obj.mesh.aabbValid ? obj.mesh.aabbMax : glm::vec3(0.5f);
                    isBatchVisible = TestOBBFrustum(bMin, bMax, model, frustumPlanes);
                }

                uint32_t lod = ComputeObjectLOD(obj, model, pViewProj);
                obj.currentLOD = (int)lod;
                int sflodLod = ComputeSFLOD(obj, model, pViewProj);
                obj.sflodCurrentLOD = sflodLod;

                CandidateEntry& c = candidateList[i];
                c.obj = &obj;
                c.model = model;
                c.distSq = distSq;
                c.isVisible = isBatchVisible;
                c.isShadowVisible = true;
                c.selectedLOD = lod;
                c.sflodLOD = sflodLod;
                c.isTrans = (obj.blendMode != 0) || (obj.opacity < 0.999f);
                c.isValid = true;
            });
            EngineCore::JobSystem::Get().Wait(jobCtx);
        } else {
            for (size_t i = 0; i < numObjects; ++i) {
                auto& obj = objects[i];
                if (!obj.visible || obj.isCamera || IsCameraPrimitive(obj.type) || obj.isWaterBody || obj.type == PrimitiveType::WaterBody) continue;
                if (obj.mesh.vertices.empty() || obj.mesh.indices.empty()) continue;

                glm::mat4 model = GetWorldMatrix(obj);
                glm::vec3 objPos = glm::vec3(model[3]);
                glm::vec3 diff = cameraPos - objPos;
                float distSq = glm::dot(diff, diff);

                bool isBatchVisible = true;
                if (hasFrustum) {
                    const glm::vec3& bMin = obj.mesh.aabbValid ? obj.mesh.aabbMin : glm::vec3(-0.5f);
                    const glm::vec3& bMax = obj.mesh.aabbValid ? obj.mesh.aabbMax : glm::vec3(0.5f);
                    isBatchVisible = TestOBBFrustum(bMin, bMax, model, frustumPlanes);
                }

                uint32_t lod = ComputeObjectLOD(obj, model, pViewProj);
                obj.currentLOD = (int)lod;
                int sflodLod = ComputeSFLOD(obj, model, pViewProj);
                obj.sflodCurrentLOD = sflodLod;

                CandidateEntry& c = candidateList[i];
                c.obj = &obj;
                c.model = model;
                c.distSq = distSq;
                c.isVisible = isBatchVisible;
                c.isShadowVisible = true;
                c.selectedLOD = lod;
                c.sflodLOD = sflodLod;
                c.isTrans = (obj.blendMode != 0) || (obj.opacity < 0.999f);
                c.isValid = true;
            }
        }

        opaqueEntries.reserve(numObjects);
        for (const auto& c : candidateList) {
            if (!c.isValid) continue;
            if (c.isTrans) {
                transparentEntries.push_back(c);
            } else {
                opaqueEntries.push_back(c);
            }
        }

        // Opaque entries remain in stable, deterministic scene order to preserve vertex and
        // index buffer layout consistency across frames (eliminating temporal/TAA buffer jitter & ghosting).
        // Transparent: Back-to-front (descending distSq) -> correct alpha blending
        std::sort(transparentEntries.begin(), transparentEntries.end(), [](const CandidateEntry& a, const CandidateEntry& b) {
            return a.distSq > b.distSq;
        });

        // Combined ordered list: stable opaque first, sorted transparent back-to-front
        std::vector<CandidateEntry> sortedEntries;
        sortedEntries.reserve(opaqueEntries.size() + transparentEntries.size());
        sortedEntries.insert(sortedEntries.end(), opaqueEntries.begin(), opaqueEntries.end());
        sortedEntries.insert(sortedEntries.end(), transparentEntries.begin(), transparentEntries.end());

        for (const auto& entry : sortedEntries) {
            const auto& obj = *entry.obj;
            const glm::mat4& model = entry.model;
            float distSq = entry.distSq;

            // SFLOD (Single Face LOD / Imposter Cards System - Nacho Time!)
            // Works independently of whether mesh LOD is enabled or disabled!
            bool distanceTrigger = (obj.sflodDistanceThreshold > 0.0f) && 
                (distSq >= obj.sflodDistanceThreshold * obj.sflodDistanceThreshold);
            bool lodTrigger = (entry.sflodLOD >= obj.sflodMinLOD);
            bool previewTrigger = (obj.forcedLOD >= obj.sflodMinLOD);

            if (obj.enableSFLOD && !obj.mesh.vertices.empty() && 
                (distanceTrigger || lodTrigger || previewTrigger)) 
            {
                SFLOD::SFLODSystem::Get().AppendImposterCard(
                    obj, model, cameraPos, (uint32_t)entry.sflodLOD, distSq,
                    entry.isVisible, entry.isShadowVisible,
                    outVertices, outIndices, outBatches);
                continue;
            }

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

            for (size_t vi = 0; vi < obj.mesh.vertices.size(); ++vi) {
                const auto& mv = obj.mesh.vertices[vi];
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

                glm::vec3 vColor{1.0f, 1.0f, 1.0f};

                outVertices.push_back({ glm::vec3(worldPos), worldNormal, mv.uv, vColor, outTangent });
            }

            const auto& lodIndices = obj.mesh.GetIndicesForLOD(entry.selectedLOD);
            for (uint32_t idx : lodIndices) {
                if (idx < (uint32_t)obj.mesh.vertices.size()) {
                    outIndices.push_back(vertexOffset + idx);
                } else {
                    outIndices.push_back(vertexOffset);
                }
            }

            if (obj.isTerrain || obj.type == PrimitiveType::Terrain) {
                // Ensure base layer properties are initialized
                const auto* baseLayer = !obj.terrain.materialLayers.empty() ? &obj.terrain.materialLayers[0] : nullptr;

                // Base Batch: Layer 0 (Opaque terrain base)
                RenderBatch b0;
                b0.startIndex = indexStart;
                b0.indexCount = (uint32_t)lodIndices.size();
                b0.distSq = distSq;
                b0.isVisible = entry.isVisible;
                b0.isShadowVisible = entry.isShadowVisible;
                b0.activeLOD = (int)entry.selectedLOD;
                b0.albedoTex = (baseLayer && !baseLayer->baseColorTexture.empty()) ? baseLayer->baseColorTexture : obj.baseColorTexture;
                b0.normalTex = (baseLayer && !baseLayer->normalTexture.empty()) ? baseLayer->normalTexture : obj.normalTexture;
                b0.roughTex = (baseLayer && !baseLayer->roughnessTexture.empty()) ? baseLayer->roughnessTexture : obj.roughnessTexture;
                b0.metalTex = (baseLayer && !baseLayer->metallicTexture.empty()) ? baseLayer->metallicTexture : obj.metallicTexture;
                b0.aoTex = (baseLayer && !baseLayer->aoTexture.empty()) ? baseLayer->aoTexture : obj.aoTexture;
                b0.opacityTex = "";
                b0.emissionTex = obj.emissionTexture;
                b0.baseColor = baseLayer ? baseLayer->baseColor : obj.color;
                b0.metallic = baseLayer ? baseLayer->metallic : obj.metallic;
                b0.roughness = baseLayer ? baseLayer->roughness : obj.roughness;
                b0.normalStrength = baseLayer ? baseLayer->normalStrength : obj.normalStrength;
                b0.specular = baseLayer ? baseLayer->specular : obj.specular;
                b0.emissiveColor = obj.emissiveColor;
                b0.emissiveIntensity = obj.emissiveIntensity;
                b0.opacity = obj.opacity;
                b0.opacityMaskClipValue = obj.opacityMaskClipValue;
                b0.blendMode = 0; // Opaque base
                b0.shadingModel = obj.shadingModel;
                b0.isUnlit = (obj.shadingModel == 1);
                b0.castShadows = obj.castShadows;
                b0.receiveShadows = obj.receiveShadows;
                b0.meshClusterCulling = false;
                b0.objectId = obj.id;
                b0.vertexOffset = vertexOffset;
                b0.uvScale = baseLayer ? baseLayer->uvScale : obj.uvScale;
                b0.normalMapYFlip = obj.normalMapYFlip;
                b0.metallicChannel = obj.metallicChannel;
                b0.roughnessChannel = obj.roughnessChannel;
                b0.aoChannel = obj.aoChannel;
                b0.materialDebugMode = obj.materialDebugMode;
                b0.twoSided = obj.twoSided;
                b0.disableStreaming = obj.disableStreaming;
                b0.useVertexAlpha = 0.0f;
                outBatches.push_back(b0);

                // Overlay Batches: Layer 1..N-1 with continuous vertex alpha blending
                if (obj.terrain.materialLayers.size() > 1 && !obj.terrain.layerWeights.empty()) {
                    int res = obj.terrain.resolution;
                    int totalVerts = (res + 1) * (res + 1);

                    for (size_t m = 1; m < obj.terrain.materialLayers.size(); ++m) {
                        if (m >= obj.terrain.layerWeights.size() || obj.terrain.layerWeights[m].empty()) continue;

                        const auto& curLayerWeights = obj.terrain.layerWeights[m];
                        bool hasAnyWeight = false;
                        for (float w : curLayerWeights) {
                            if (w > 0.001f) {
                                hasAnyWeight = true;
                                break;
                            }
                        }
                        if (!hasAnyWeight) continue;

                        uint32_t overlayVertOffset = (uint32_t)outVertices.size();

                        // Duplicate transformed vertices for layer m with relative vertex alpha in color.r
                        for (int v = 0; v < totalVerts; ++v) {
                            float sumW = 0.0f;
                            for (size_t k = 0; k <= m; ++k) {
                                if (k < obj.terrain.layerWeights.size() && v < (int)obj.terrain.layerWeights[k].size()) {
                                    sumW += obj.terrain.layerWeights[k][v];
                                }
                            }
                            float wm = (v < (int)curLayerWeights.size()) ? curLayerWeights[v] : 0.0f;
                            float alphaM = (sumW > 0.0001f) ? glm::clamp(wm / sumW, 0.0f, 1.0f) : 0.0f;

                            Vertex ov = outVertices[vertexOffset + v];
                            ov.color = glm::vec4(alphaM, 0.0f, 0.0f, 1.0f);
                            outVertices.push_back(ov);
                        }

                        uint32_t overlayIndexStart = (uint32_t)outIndices.size();

                        // Add triangles only for quads where at least one vertex has weight > 0
                        for (int z = 0; z < res; ++z) {
                            for (int x = 0; x < res; ++x) {
                                int v00 = z * (res + 1) + x;
                                int v10 = z * (res + 1) + (x + 1);
                                int v01 = (z + 1) * (res + 1) + x;
                                int v11 = (z + 1) * (res + 1) + (x + 1);

                                float w00 = (v00 < (int)curLayerWeights.size()) ? curLayerWeights[v00] : 0.0f;
                                float w10 = (v10 < (int)curLayerWeights.size()) ? curLayerWeights[v10] : 0.0f;
                                float w01 = (v01 < (int)curLayerWeights.size()) ? curLayerWeights[v01] : 0.0f;
                                float w11 = (v11 < (int)curLayerWeights.size()) ? curLayerWeights[v11] : 0.0f;

                                if (w00 > 0.0005f || w10 > 0.0005f || w01 > 0.0005f || w11 > 0.0005f) {
                                    outIndices.push_back(overlayVertOffset + v00);
                                    outIndices.push_back(overlayVertOffset + v01);
                                    outIndices.push_back(overlayVertOffset + v10);

                                    outIndices.push_back(overlayVertOffset + v10);
                                    outIndices.push_back(overlayVertOffset + v01);
                                    outIndices.push_back(overlayVertOffset + v11);
                                }
                            }
                        }

                        uint32_t overlayIndexCount = (uint32_t)outIndices.size() - overlayIndexStart;
                        if (overlayIndexCount == 0) {
                            outVertices.resize(overlayVertOffset);
                            continue;
                        }

                        const auto& layer = obj.terrain.materialLayers[m];
                        RenderBatch ob;
                        ob.startIndex = overlayIndexStart;
                        ob.indexCount = overlayIndexCount;
                        ob.distSq = distSq;
                        ob.isVisible = entry.isVisible;
                        ob.isShadowVisible = false;
                        ob.activeLOD = (int)entry.selectedLOD;
                        ob.albedoTex = !layer.baseColorTexture.empty() ? layer.baseColorTexture : obj.baseColorTexture;
                        ob.normalTex = !layer.normalTexture.empty() ? layer.normalTexture : obj.normalTexture;
                        ob.roughTex = !layer.roughnessTexture.empty() ? layer.roughnessTexture : obj.roughnessTexture;
                        ob.metalTex = !layer.metallicTexture.empty() ? layer.metallicTexture : obj.metallicTexture;
                        ob.aoTex = !layer.aoTexture.empty() ? layer.aoTexture : obj.aoTexture;
                        ob.opacityTex = "";
                        ob.emissionTex = obj.emissionTexture;
                        ob.baseColor = layer.baseColor;
                        ob.metallic = layer.metallic;
                        ob.roughness = layer.roughness;
                        ob.normalStrength = layer.normalStrength;
                        ob.specular = layer.specular;
                        ob.emissiveColor = obj.emissiveColor;
                        ob.emissiveIntensity = obj.emissiveIntensity;
                        ob.opacity = 1.0f;
                        ob.opacityMaskClipValue = 0.333f;
                        ob.blendMode = 2; // Translucent hardware alpha blend
                        ob.shadingModel = obj.shadingModel;
                        ob.isUnlit = (obj.shadingModel == 1);
                        ob.castShadows = false;
                        ob.receiveShadows = obj.receiveShadows;
                        ob.meshClusterCulling = false;
                        ob.objectId = obj.id;
                        ob.vertexOffset = overlayVertOffset;
                        ob.uvScale = layer.uvScale;
                        ob.normalMapYFlip = obj.normalMapYFlip;
                        ob.metallicChannel = obj.metallicChannel;
                        ob.roughnessChannel = obj.roughnessChannel;
                        ob.aoChannel = obj.aoChannel;
                        ob.materialDebugMode = obj.materialDebugMode;
                        ob.twoSided = obj.twoSided;
                        ob.disableStreaming = obj.disableStreaming;
                        ob.useVertexAlpha = 1.0f;
                        outBatches.push_back(ob);
                    }
                }
            } else {
                RenderBatch b;
                b.startIndex = indexStart;
                b.indexCount = (uint32_t)lodIndices.size();
                b.distSq = distSq;
                b.isVisible = entry.isVisible;
                b.isShadowVisible = entry.isShadowVisible;
                b.activeLOD = (int)entry.selectedLOD;
                b.albedoTex = obj.baseColorTexture;
                b.normalTex = obj.normalTexture;
                b.roughTex = obj.roughnessTexture;
                b.metalTex = obj.metallicTexture;
                b.aoTex = obj.aoTexture;
                b.opacityTex = obj.opacityTexture;
                b.emissionTex = obj.emissionTexture;
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
                b.twoSided = obj.twoSided;
                b.disableStreaming = obj.disableStreaming;
                outBatches.push_back(b);
            }
        }

        // 3. Highlight Selected Object / Collider (Only in Editor Mode when clicked in viewport for testing)
        if (selectedId != -1 && !isPlayMode && !showAllCollisions) {
            GameObject* selObj = FindObject(selectedId);
            if (selObj && (selObj->visible || !selObj->modifiers.empty()) &&
                !selObj->isCamera && !IsCameraPrimitive(selObj->type) &&
                !selObj->isLight && !IsLightPrimitive(selObj->type) &&
                !selObj->isUserInterface && selObj->type != PrimitiveType::UserInterface)
            {
                if (selObj->modifiers.empty()) {
                    Collisions::CollisionSystem::Get().SetupDefaultModifiers(*selObj);
                }
                if (!selObj->modifiers.empty()) {
                    for (auto& mod : selObj->modifiers) {
                        mod.showWireframe = true;
                    }
                    uint32_t colStart = (uint32_t)outIndices.size();
                    Collisions::CollisionSystem::Get().AppendCollisionWireframe(outVertices, outIndices, *selObj, cameraPos, GetWorldMatrix(*selObj));
                    uint32_t colCount = (uint32_t)outIndices.size() - colStart;
                    if (colCount > 0) {
                        RenderBatch b;
                        b.startIndex = colStart;
                        b.indexCount = colCount;
                        b.isUnlit = true;
                        b.twoSided = true;
                        b.baseColor = {1.0f, 1.0f, 1.0f};
                        outBatches.push_back(b);
                    }
                }
            }
        }

        // 3b. Visualize All Scene Collisions (when selected in Viewport Visualize menu)
        if (showAllCollisions) {
            uint32_t colStart = (uint32_t)outIndices.size();
            for (auto& obj : objects) {
                if (obj.isCamera || IsCameraPrimitive(obj.type) || obj.isLight || IsLightPrimitive(obj.type) || obj.isUserInterface || obj.type == PrimitiveType::UserInterface) continue;
                if (!obj.visible && obj.modifiers.empty()) continue;
                if (obj.type == PrimitiveType::Empty && obj.modifiers.empty()) continue;
                if (obj.modifiers.empty()) {
                    Collisions::CollisionSystem::Get().SetupDefaultModifiers(obj);
                }
                if (!obj.modifiers.empty()) {
                    Collisions::CollisionSystem::Get().AppendCollisionWireframe(outVertices, outIndices, obj, cameraPos, GetWorldMatrix(obj));
                }
            }
            uint32_t colCount = (uint32_t)outIndices.size() - colStart;
            if (colCount > 0) {
                RenderBatch b;
                b.startIndex = colStart;
                b.indexCount = colCount;
                b.isUnlit = true;
                b.twoSided = true;
                b.baseColor = {1.0f, 1.0f, 1.0f};
                outBatches.push_back(b);
            }
        }

        // 3c. Visualize All Scene Object Bounds (when selected in Viewport Visualize menu)
        if (showAllBounds) {
            uint32_t boundsStart = (uint32_t)outIndices.size();
            glm::vec3 boundsColor{0.2f, 0.7f, 1.0f};
            for (const auto& obj : objects) {
                if (!obj.visible || obj.isCamera || IsCameraPrimitive(obj.type) || obj.isLight || IsLightPrimitive(obj.type) || obj.isUserInterface || obj.type == PrimitiveType::UserInterface || obj.type == PrimitiveType::Empty) continue;
                if (obj.mesh.vertices.empty()) continue;

                glm::vec3 minB(1e9f), maxB(-1e9f);
                for (const auto& v : obj.mesh.vertices) {
                    minB = glm::min(minB, v.pos);
                    maxB = glm::max(maxB, v.pos);
                }
                glm::vec3 center = (minB + maxB) * 0.5f;
                glm::vec3 extents = (maxB - minB) * 0.5f;
                std::vector<std::pair<glm::vec3, glm::vec3>> edges;
                Collisions::CollisionSystem::Get().GenerateBoxWireframe(edges, extents, center);
                glm::mat4 worldMat = GetWorldMatrix(obj);
                for (const auto& e : edges) {
                    glm::vec3 w0 = glm::vec3(worldMat * glm::vec4(e.first, 1.0f));
                    glm::vec3 w1 = glm::vec3(worldMat * glm::vec4(e.second, 1.0f));
                    GeometryBuilder::AppendCameraFacingLine(outVertices, outIndices, w0, w1, cameraPos, 1.2f, boundsColor);
                }
            }
            uint32_t boundsCount = (uint32_t)outIndices.size() - boundsStart;
            if (boundsCount > 0) {
                RenderBatch b;
                b.startIndex = boundsStart;
                b.indexCount = boundsCount;
                b.isUnlit = true;
                b.twoSided = true;
                b.baseColor = {1.0f, 1.0f, 1.0f};
                outBatches.push_back(b);
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
                float halfW = 0.0035f;
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
                b.twoSided = true;
                b.castShadows = false;
                b.receiveShadows = false;
                b.baseColor = {1.0f, 1.0f, 1.0f};
                outBatches.push_back(b);
            }
        }
    }

    void BuildSceneMesh(std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices, glm::vec3 cameraPos = {0.0f, 0.0f, 0.0f}, const glm::mat4* pViewProj = nullptr) {
        std::vector<RenderBatch> batches;
        BuildSceneMesh(outVertices, outIndices, batches, cameraPos, pViewProj);
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
        glm::mat4 invModel = glm::inverse(model);
        glm::vec3 localCam = glm::vec3(invModel * glm::vec4(cameraPos, 1.0f));
        std::vector<std::pair<glm::vec3, glm::vec3>> edges;

        switch (obj.type) {
            case PrimitiveType::Cube: {
                // 6 faces: +X (0), -X (1), +Y (2), -Y (3), +Z (4), -Z (5)
                const glm::vec3 normals[6] = {
                    { 1, 0, 0 }, { -1, 0, 0 },
                    { 0, 1, 0 }, { 0, -1, 0 },
                    { 0, 0, 1 }, { 0, 0, -1 }
                };
                const glm::vec3 centers[6] = {
                    { 0.5f, 0, 0 }, { -0.5f, 0, 0 },
                    { 0, 0.5f, 0 }, { 0, -0.5f, 0 },
                    { 0, 0, 0.5f }, { 0, 0, -0.5f }
                };
                bool faceVisible[6];
                for (int f = 0; f < 6; ++f) {
                    faceVisible[f] = glm::dot(localCam - centers[f], normals[f]) > 0.0f;
                }

                float s = 0.5f;
                const glm::vec3 c[8] = {
                    {-s, -s, -s}, { s, -s, -s}, { s, -s,  s}, {-s, -s,  s},
                    {-s,  s, -s}, { s,  s, -s}, { s,  s,  s}, {-s,  s,  s}
                };
                // 12 edges and their two adjacent faces
                struct CubeEdge { int v0, v1, f0, f1; };
                const CubeEdge cubeEdges[12] = {
                    { 0, 1, 3, 5 }, { 1, 2, 3, 0 }, { 2, 3, 3, 4 }, { 3, 0, 3, 1 }, // bottom
                    { 4, 5, 2, 5 }, { 5, 6, 2, 0 }, { 6, 7, 2, 4 }, { 7, 4, 2, 1 }, // top
                    { 0, 4, 1, 5 }, { 1, 5, 0, 5 }, { 2, 6, 0, 4 }, { 3, 7, 1, 4 }  // verticals
                };
                for (const auto& ce : cubeEdges) {
                    if (faceVisible[ce.f0] != faceVisible[ce.f1]) {
                        edges.push_back({ c[ce.v0], c[ce.v1] });
                    }
                }
                break;
            }
            case PrimitiveType::Plane: {
                // Plane silhouette is exclusively the 4 outer perimeter boundary edges (no internal grid lines)
                float hx = 1.25f, hz = 1.25f;
                glm::vec3 p0(-hx, 0.0f, -hz);
                glm::vec3 p1( hx, 0.0f, -hz);
                glm::vec3 p2( hx, 0.0f,  hz);
                glm::vec3 p3(-hx, 0.0f,  hz);
                edges.push_back({p0, p1}); edges.push_back({p1, p2});
                edges.push_back({p2, p3}); edges.push_back({p3, p0});
                break;
            }
            case PrimitiveType::Pyramid: {
                float s = 0.5f, h = 1.2f;
                glm::vec3 apex(0.0f, h, 0.0f);
                glm::vec3 c[4] = {
                    {-s, 0.0f,  s}, { s, 0.0f,  s}, { s, 0.0f, -s}, {-s, 0.0f, -s}
                };
                // Base face (face 0)
                bool baseVisible = (localCam.y < 0.0f);
                // 4 side faces
                bool sideVisible[4];
                for (int i = 0; i < 4; ++i) {
                    glm::vec3 v0 = c[i];
                    glm::vec3 v1 = c[(i + 1) % 4];
                    glm::vec3 fn = glm::normalize(glm::cross(v1 - v0, apex - v0));
                    sideVisible[i] = glm::dot(localCam - v0, fn) > 0.0f;
                }
                // Base perimeter edges
                for (int i = 0; i < 4; ++i) {
                    if (baseVisible != sideVisible[i]) {
                        edges.push_back({ c[i], c[(i + 1) % 4] });
                    }
                }
                // Sloped edges
                for (int i = 0; i < 4; ++i) {
                    int prevSide = (i + 3) % 4;
                    if (sideVisible[i] != sideVisible[prevSide]) {
                        edges.push_back({ c[i], apex });
                    }
                }
                break;
            }
            case PrimitiveType::Sphere: {
                // Exact camera horizon silhouette circle in screen space
                float r = 0.6f;
                float d = glm::length(localCam);
                if (d > r + 0.01f) {
                    glm::vec3 toCamDir = localCam / d;
                    float h = r * r / d;
                    float rSil = r * std::sqrt(std::max(0.0f, 1.0f - (r * r) / (d * d)));
                    glm::vec3 center = toCamDir * h;

                    glm::vec3 up = (std::abs(toCamDir.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                    glm::vec3 right = glm::normalize(glm::cross(toCamDir, up));
                    up = glm::cross(right, toCamDir);

                    const int segs = 48;
                    for (int i = 0; i < segs; ++i) {
                        float a0 = 2.0f * (float)M_PI * (float)i / (float)segs;
                        float a1 = 2.0f * (float)M_PI * (float)(i + 1) / (float)segs;
                        glm::vec3 p0 = center + (right * std::cos(a0) + up * std::sin(a0)) * rSil;
                        glm::vec3 p1 = center + (right * std::cos(a1) + up * std::sin(a1)) * rSil;
                        edges.push_back({ p0, p1 });
                    }
                }
                break;
            }
            case PrimitiveType::Cylinder: {
                float r = 0.5f, halfH = 0.6f;
                bool topVisible = (localCam.y > halfH);
                bool bottomVisible = (localCam.y < -halfH);

                const int segs = 36;
                // Side silhouette generator lines
                float camDistXZ = std::sqrt(localCam.x * localCam.x + localCam.z * localCam.z);
                if (camDistXZ > r + 0.01f) {
                    float phi = std::atan2(localCam.z, localCam.x);
                    float alpha = std::asin(std::clamp(r / camDistXZ, -1.0f, 1.0f));
                    for (float sgn : { -1.0f, 1.0f }) {
                        float ang = phi + sgn * (float(M_PI) * 0.5f - alpha);
                        glm::vec3 pBot(r * std::cos(ang), -halfH, r * std::sin(ang));
                        glm::vec3 pTop(r * std::cos(ang),  halfH, r * std::sin(ang));
                        edges.push_back({ pBot, pTop });
                    }
                }
                // Cap rims if visible
                if (topVisible) {
                    for (int i = 0; i < segs; ++i) {
                        float a0 = 2.0f * (float)M_PI * (float)i / (float)segs;
                        float a1 = 2.0f * (float)M_PI * (float)(i + 1) / (float)segs;
                        edges.push_back({ {r * std::cos(a0), halfH, r * std::sin(a0)},
                                          {r * std::cos(a1), halfH, r * std::sin(a1)} });
                    }
                }
                if (bottomVisible) {
                    for (int i = 0; i < segs; ++i) {
                        float a0 = 2.0f * (float)M_PI * (float)i / (float)segs;
                        float a1 = 2.0f * (float)M_PI * (float)(i + 1) / (float)segs;
                        edges.push_back({ {r * std::cos(a0), -halfH, r * std::sin(a0)},
                                          {r * std::cos(a1), -halfH, r * std::sin(a1)} });
                    }
                }
                break;
            }
            default: {
                // Arbitrary 3D Mesh / Model: True Screen-Space Silhouette Extraction
                // An edge is a silhouette edge iff (dot1 > 0) != (dot2 > 0) or it is a boundary edge facing camera.
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
                    if (kv.first.a >= vList.size() || kv.first.b >= vList.size()) continue;
                    glm::vec3 vA = vList[kv.first.a].pos;
                    glm::vec3 vB = vList[kv.first.b].pos;
                    glm::vec3 mid = (vA + vB) * 0.5f;
                    glm::vec3 toCam = localCam - mid;

                    float dot1 = glm::dot(kv.second.n1, toCam);
                    if (kv.second.count == 1) {
                        // Boundary open edge is a silhouette if front-facing
                        if (dot1 > 0.0f) {
                            edges.push_back({ vA, vB });
                        }
                    } else if (kv.second.count >= 2) {
                        float dot2 = glm::dot(kv.second.n2, toCam);
                        // True silhouette: one face faces camera, one face faces away
                        if ((dot1 > 0.0f && dot2 <= 0.0f) || (dot1 <= 0.0f && dot2 > 0.0f)) {
                            edges.push_back({ vA, vB });
                        }
                    }
                }
                break;
            }
        }

        // Draw camera-facing ribbons along strictly silhouette outline edges
        for (const auto& e : edges) {
            glm::vec3 wA = glm::vec3(model * glm::vec4(e.first, 1.0f));
            glm::vec3 wB = glm::vec3(model * glm::vec4(e.second, 1.0f));
            GeometryBuilder::AppendCameraFacingLine(outVertices, outIndices, wA, wB, cameraPos, 0.85f, outlineColor);
        }
    }

    void LoadDefaultLevel() { LoadDefaultScene(); }
    void BuildLevelMesh(std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices,
                        std::vector<RenderBatch>& outBatches, const glm::vec3& cameraPos = glm::vec3(0.0f)) {
        BuildSceneMesh(outVertices, outIndices, outBatches, cameraPos);
    }
};

using Level = Scene;

inline void GameObject::Destroy() {
    if (scene) {
        scene->RemoveObject(id);
    }
}

inline void GameObject::Destroy(GameObject* obj) {
    if (obj) obj->Destroy();
}

inline void GameObject::Destroy(GameObject& obj) {
    obj.Destroy();
}

inline void GameObject::Destroy(int objId) {
    // If called with ID and static method
}

inline void GameObject::SetMobility(Mobility m, bool recursive) {
    if (scene) {
        scene->SetObjectMobility(id, m, recursive);
    } else {
        mobility = m;
    }
}

