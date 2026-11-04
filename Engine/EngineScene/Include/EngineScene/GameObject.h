#pragma once
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <EngineAssets/Geometry.h>
#include <EngineAssets/AssetSystem.h>
#include <EngineScene/TransformTypes.h>
#include <EngineScene/EunoiaBehaviour.h>
#include <EngineScene/BehaviourRegistry.h>
#include <Collisions/CollisionTypes.h>
#include <Physics/PhysicsTypes.h>
#include <UISystem/UITypes.h>
#include <EngineScene/TerrainComponent.h>
#include <EngineScene/AnimationGraphModifier.h>
#include <EngineScene/LevelAnimationComponent.h>
#include <EunoiaUI/EunoiaUI.h>

// Wicked Engine Light System Alignment
enum class LightType : uint32_t {
    Directional = 0,
    Point       = 1,
    Spot        = 2,
    Rectangle   = 3,
    Area        = 3, // alias for Rectangle for backwards compatibility
    Sky         = 4,
    Count
};

inline const char* GetLightTypeName(LightType type) {
    switch (type) {
        case LightType::Directional: return "Directional Light";
        case LightType::Point:       return "Point Light";
        case LightType::Spot:        return "Spot Light";
        case LightType::Rectangle:   return "Rectangle Light";
        case LightType::Sky:         return "Sky Light";
        default:                     return "Light";
    }
}

inline glm::vec3 ColorTemperatureToRGB(float kelvin) {
    float temp = glm::clamp(kelvin, 1000.0f, 40000.0f) / 100.0f;
    float red, green, blue;
    if (temp <= 66.0f) {
        red = 255.0f;
        green = 99.4708025861f * std::log(temp) - 161.1195681661f;
        if (temp <= 19.0f) {
            blue = 0.0f;
        } else {
            blue = 138.5177312231f * std::log(temp - 10.0f) - 305.0447927307f;
        }
    } else {
        red = 329.698727446f * std::pow(temp - 60.0f, -0.1332047592f);
        green = 288.1221695283f * std::pow(temp - 60.0f, -0.0755148492f);
        blue = 255.0f;
    }
    return glm::vec3(glm::clamp(red / 255.0f, 0.0f, 1.0f),
                     glm::clamp(green / 255.0f, 0.0f, 1.0f),
                     glm::clamp(blue / 255.0f, 0.0f, 1.0f));
}

struct LightComponent {
    enum FLAGS : uint32_t {
        EMPTY               = 0,
        CAST_SHADOW         = 1 << 0,
        VOLUMETRICS         = 1 << 1,
        VISUALIZER          = 1 << 2,
        VOLUMETRICCLOUDS    = 1 << 3,
    };
    uint32_t _flags = CAST_SHADOW;

    LightType type = LightType::Point;
    bool enabled = true;

    // Wicked Engine Light properties
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f; // lux (directional) or candela (point/spot/rect)
    float range = 10.0f;
    float outerConeAngle = 45.0f; // degrees (Wicked Engine XM_PIDIV4)
    float innerConeAngle = 0.0f;  // degrees (0 = only outer cone)
    float radius = 0.025f;        // source radius (for specular softness / sphere)
    float length = 0.0f;          // point lights: line/capsule length; rect light: width
    float height = 0.0f;          // rect light only
    std::vector<float> cascade_distances = { 8.0f, 80.0f, 800.0f };
    int forced_shadow_resolution = -1; // -1: auto, greater: fixed shadow map resolution

    // Extended / convenient attributes
    float temperature = 6500.0f; // Kelvin (1000K - 12000K)
    bool useTemperature = false;
    float attenuation = 2.0f;
    float rangeSmoothing = 0.2f; // Smooth range dissolve ending (0.01 to 1.0)
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    float coneFalloff = 1.0f;
    int areaShape = 0; // 0 = Rectangle, 1 = Disk
    float width = 1.0f;
    bool twoSided = false;

    // Sky / Environment Light
    std::string envMapTexture = "";
    float envRotation = 0.0f;
    float diffuseContribution = 1.0f;
    float specularContribution = 1.0f;
    float ambientContribution = 0.25f;
    float mipLevel = 0.0f;
    glm::vec3 lowerHemisphereColor{0.15f, 0.12f, 0.10f};
    glm::vec3 skyColor{0.4f, 0.6f, 0.9f};
    glm::vec3 groundColor{0.3f, 0.2f, 0.1f};

    // Shadow properties
    bool castShadows = true;
    float shadowStrength = 0.85f;
    float shadowBias = 0.0012f;
    int shadowResolution = 2048;
    float shadowDistance = 250.0f;

    // Performance & Distance Culling
    bool enableDistanceCulling = false; // by default disabled
    float minVisibleDistance = 50.0f;   // light smoothly fades out starting at this distance
    float maxVisibleDistance = 100.0f;  // light completely ignored and not rendered beyond this distance

    // Volumetrics & God Rays (Wicked Engine Technique)
    bool volumetric = false;
    float volumetric_boost = 1.0f; // boost strength of volumetric fog for this light
    float volumetricScattering = 0.66f; // Mie Henyey-Greenstein anisotropy g factor
    float volumetricIntensity = 1.0f; // Overall god rays intensity multiplier
    int volumetricSteps = 16; // Raymarching sample steps (8, 16, 24, 32)
    float volumetricMaxDistance = 120.0f; // Max raymarch distance in meters

    // Light Channels / Layer
    uint32_t lightLayer = 1;

    // Non-serialized transform cache:
    glm::vec3 position{0.0f, 0.0f, 0.0f};

    // Wicked Engine helper methods
    void SetCastShadow(bool value) {
        if (value) _flags |= CAST_SHADOW; else _flags &= ~CAST_SHADOW;
        castShadows = value;
    }
    bool IsCastingShadow() const { return (_flags & CAST_SHADOW) != 0 || castShadows; }

    void SetVolumetricsEnabled(bool value) {
        if (value) _flags |= VOLUMETRICS; else _flags &= ~VOLUMETRICS;
        volumetric = value;
    }
    bool IsVolumetricsEnabled() const { return (_flags & VOLUMETRICS) != 0 || volumetric; }

    void SetVisualizerEnabled(bool value) {
        if (value) _flags |= VISUALIZER; else _flags &= ~VISUALIZER;
    }
    bool IsVisualizerEnabled() const { return (_flags & VISUALIZER) != 0; }

    void SetVolumetricCloudsEnabled(bool value) {
        if (value) _flags |= VOLUMETRICCLOUDS; else _flags &= ~VOLUMETRICCLOUDS;
    }
    bool IsVolumetricCloudsEnabled() const { return (_flags & VOLUMETRICCLOUDS) != 0; }

    bool IsInactive() const { return intensity < 0.0001f || range < 0.0001f; }

    float GetRange() const {
        float r = range;
        r = std::max(0.001f, r);
        r = std::min(r, 65504.0f);
        return r;
    }

    void SetType(LightType val) { type = val; }
    LightType GetType() const { return type; }

    struct LightSphere {
        glm::vec3 center{0.0f};
        float radius = 0.0f;
    };

    LightSphere GetSphere() const {
        LightSphere ret;
        switch (type) {
            case LightType::Directional:
                ret.center = glm::vec3(0.0f);
                ret.radius = 1e9f;
                break;
            case LightType::Point:
            case LightType::Rectangle:
                ret.center = position;
                ret.radius = GetRange();
                break;
            case LightType::Spot: {
                const float rad = glm::radians(outerConeAngle);
                const float outerCos = std::cos(rad);
                const float sRadius = GetRange() * 0.5f / std::max(0.0001f, (outerCos * outerCos));
                glm::vec3 dir = glm::length(direction) > 1e-4f ? glm::normalize(direction) : glm::vec3(0.0f, -1.0f, 0.0f);
                ret.center = position - dir * sRadius;
                ret.radius = sRadius;
                break;
            }
            default:
                ret.center = position;
                ret.radius = GetRange();
                break;
        }
        return ret;
    }
};

struct PrimitiveParams {
    float width = 1.0f;
    float height = 1.0f;
    float depth = 1.0f;
    float size = 1.0f;
    float radius = 0.5f;
    float radius2 = 0.2f;      // topRadius for Cone, minorRadius for Torus
    int segmentsX = 16;
    int segmentsY = 16;
    int segmentsZ = 16;
    int radialSegments = 24;
    int heightSegments = 1;
    int rings = 24;
    int sectors = 24;
    int subdivisions = 2;      // for Icosphere
    int sides = 4;             // for Pyramid / Prism
    bool capTop = true;
    bool capBottom = true;
};

enum class PrimitiveType {
    Cube,
    Plane,
    Box,
    Sphere,
    UVSphere,
    Icosphere,
    Cylinder,
    Cone,
    Capsule,
    Torus,
    Circle,
    Disc,
    Quad,
    Triangle,
    Pyramid,
    Prism,
    ImportedMesh,
    Empty,
    // Lights
    DirectionalLight,
    PointLight,
    SpotLight,
    AreaLight,
    SkyLight,
    Camera,
    WaterBody,
    UserInterface,
    Terrain,
    LevelAnimation
};

inline const char* GetPrimitiveTypeName(PrimitiveType type) {
    switch (type) {
        case PrimitiveType::Cube:             return "Cube";
        case PrimitiveType::Plane:            return "Plane";
        case PrimitiveType::Box:              return "Box";
        case PrimitiveType::Sphere:           return "Sphere";
        case PrimitiveType::UVSphere:         return "UV Sphere";
        case PrimitiveType::Icosphere:        return "Icosphere";
        case PrimitiveType::Cylinder:         return "Cylinder";
        case PrimitiveType::Cone:             return "Cone";
        case PrimitiveType::Capsule:          return "Capsule";
        case PrimitiveType::Torus:            return "Torus";
        case PrimitiveType::Circle:           return "Circle";
        case PrimitiveType::Disc:             return "Disc";
        case PrimitiveType::Quad:             return "Quad";
        case PrimitiveType::Triangle:         return "Triangle";
        case PrimitiveType::Pyramid:          return "Pyramid";
        case PrimitiveType::Prism:            return "Prism";
        case PrimitiveType::ImportedMesh:     return "Mesh";
        case PrimitiveType::Empty:            return "Empty Actor";
        case PrimitiveType::DirectionalLight: return "Directional Light";
        case PrimitiveType::PointLight:       return "Point Light";
        case PrimitiveType::SpotLight:        return "Spot Light";
        case PrimitiveType::AreaLight:        return "Area Light";
        case PrimitiveType::SkyLight:         return "Sky Light";
        case PrimitiveType::Camera:           return "Camera";
        case PrimitiveType::WaterBody:        return "Water Body";
        case PrimitiveType::UserInterface:    return "User Interface";
        case PrimitiveType::Terrain:          return "Terrain";
        case PrimitiveType::LevelAnimation:   return "Level Animation";
        default:                              return "Object";
    }
}

inline bool IsLightPrimitive(PrimitiveType type) {
    return type == PrimitiveType::DirectionalLight ||
           type == PrimitiveType::PointLight ||
           type == PrimitiveType::SpotLight ||
           type == PrimitiveType::AreaLight ||
           type == PrimitiveType::SkyLight;
}

inline bool IsCameraPrimitive(PrimitiveType type) {
    return type == PrimitiveType::Camera;
}

struct CameraComponent {
    bool isOrthographic = false;
    float fov = 60.0f;             // Vertical FOV in degrees
    float orthoSize = 5.0f;        // Orthographic half-height
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float aspectRatio = 16.0f / 9.0f;
};

enum class WaterBodyType {
    Ocean, River, Sea, Lake, Stream, Waterfall, Pond, Pool, WetSurface, Puddle, FloodWater, Underwater, Swamp, Custom
};

struct WaterBodyComponent {
    // Identity
    WaterBodyType type = WaterBodyType::Ocean;
    std::string name = "WaterBody";

    // Surface Appearance
    glm::vec4 waterColor = {0.0f, 0.3f, 0.5f, 1.0f};
    glm::vec4 shallowColor = {0.2f, 0.6f, 0.8f, 1.0f};
    float roughness = 0.1f;
    float metallic = 0.1f;
    float opacity = 0.8f;
    float refractionStrength = 0.5f;
    float reflectionStrength = 0.8f;
    float specularIntensity = 1.0f;

    // Normal Maps / Waves
    std::string normalMap1Path = "";
    AssetID normalMap1AssetId;
    std::string normalMap2Path = "";
    AssetID normalMap2AssetId;
    float normalScale = 1.0f;
    float waveSpeed = 1.0f;
    float waveScale = 1.0f;
    float waveHeight = 1.0f;

    // Foam
    bool enableFoam = false;
    glm::vec4 foamColor = {1.0f, 1.0f, 1.0f, 1.0f};
    float foamScale = 1.0f;
    float foamOpacity = 1.0f;
    std::string foamTexturePath = "";
    AssetID foamTextureAssetId;

    // Depth & Refraction
    bool enableDepth = true;
    float depthFadeDistance = 10.0f;
    bool enableRefraction = true;
    float distortionStrength = 0.1f;
    bool caustics = false;
    float causticsScale = 1.0f;
    float causticsSpeed = 1.0f;

    // Environment & Lighting
    bool receiveEnvironmentLight = true;
    float environmentLightIntensity = 1.0f;
    bool reflectSkybox = true;
    float subsurfaceScattering = 0.2f;
    glm::vec3 subsurfaceColor = {0.1f, 0.4f, 0.4f};

    // Flow (Rivers, Streams, Waterfalls)
    bool enableFlow = false;
    glm::vec2 flowDirection = {1.0f, 0.0f};
    float flowSpeed = 1.0f;
    std::string flowMapPath = "";
    AssetID flowMapAssetId;
    float flowTiling = 1.0f;

    // Physics (Ocean, Sea)
    bool enableWaves = true;
    float oceanWaveAmplitude = 1.0f;
    float oceanWavePeriod = 10.0f;
    float oceanWaveDirection = 0.0f;
    int tessellationLevel = 4;

    // Specific: Waterfall
    float waterfallFallHeight = 10.0f;
    float waterfallFlowRate = 1.0f;
    float waterfallSprayAmount = 0.5f;

    // Specific: Underwater
    glm::vec3 underwaterFogColor = {0.0f, 0.1f, 0.2f};
    float underwaterFogDensity = 1.0f;
    float underwaterVisibilityDistance = 20.0f;
    bool enableUnderwaterDistortion = true;
    float underwaterDistortionStrength = 1.0f;
    float underwaterDistortionSpeed = 1.0f;

    // Specific: WetSurface / Puddle
    float wetness = 0.5f;
    float puddleSize = 1.0f;
    float puddleDepth = 0.1f;
    bool animatePuddle = false;
    float rippleSpeed = 1.0f;
    float rippleScale = 1.0f;

    // Specific: SwampWater
    float swampDensity = 0.5f;
    glm::vec3 swampColor = {0.1f, 0.2f, 0.1f};
    float algaeAmount = 0.2f;

    // Geometry
    float width = 10.0f;
    float length = 10.0f;
    float elevation = 0.0f;
    int gridResolution = 32;

    // Simulation (AquaTerrain GPU heightfield simulation)
    bool simulationEnabled = true;
    int simulationResolution = 256;
    float waveDamping = 0.985f;
    float disturbanceStrength = 1.0f;
    float heightScale = 1.0f;

    // Advanced Shading & Depth
    float depthRange = 10.0f;
    float foamDistance = 1.5f;
    float foamStrength = 1.0f;
    float foamSpeed = 1.0f;
    float fresnelStrength = 1.0f;
    float specularPower = 128.0f;
    float sparklePower = 800.0f;

    // Ocean & Multi-Scale Waves
    float choppiness = 0.7f;
    float oceanExtent = 3000.0f;
    bool isCameraCentered = true;
    int debugMode = 0; // 0=Final, 1=Base, 2=Normals, 3=Height, 4=Foam, 5=Depth, 6=Wireframe

    // Rendering
    bool castShadows = false;
    bool receiveShadows = true;
    bool visible = true;
    int renderLayer = 0;
};

enum class Mobility {
    Static,
    Stationary,
    Movable
};

struct GameObject;

// ============================================================================
// GameObjectTransform
// Supports dev.md requirements:
//   <GameObject>.Transform.WorldLocation(new Vector3(0, 0, 0));
//   <GameObject>.Transform.WorldRotation(new Vector3(0, 0, 0));
//   <GameObject>.Transform.WorldScale(new Vector3(0, 0, 0));
//   <GameObject>.Transform.RelativeLocation(new Vector3(0, 0, 0));
//   <GameObject>.Transform.RelativeRotation(new Vector3(0, 0, 0));
//   <GameObject>.Transform.RelativeScale(new Vector3(0, 0, 0));
// ============================================================================
struct GameObjectTransform {
    struct AxisProxy {
        enum class TargetType {
            WorldLocation,
            RelativeLocation,
            WorldRotation,
            RelativeRotation,
            WorldScale,
            RelativeScale
        };

        GameObjectTransform* parent = nullptr;
        TargetType target = TargetType::WorldLocation;

        AxisProxy() = default;
        AxisProxy(GameObjectTransform* p, TargetType t) : parent(p), target(t) {}

        void operator()(float inX, float inY, float inZ);
        void operator()(const glm::vec3& v);
        void operator()(const glm::vec3* v);
        void operator()(const TransformVector3& v);
        void operator()(const TransformVector3* v);

        TransformVector3 operator()() const;

        operator glm::vec3() const { return GetValue(); }
        operator TransformVector3() const { return TransformVector3(GetValue()); }

        TransformVector3 operator+(const TransformVector3& v) const { return TransformVector3(GetValue()) + v; }
        TransformVector3 operator-(const TransformVector3& v) const { return TransformVector3(GetValue()) - v; }
        TransformVector3 operator+(const glm::vec3& v) const { return TransformVector3(GetValue()) + TransformVector3(v); }
        TransformVector3 operator-(const glm::vec3& v) const { return TransformVector3(GetValue()) - TransformVector3(v); }
        TransformVector3 operator*(float s) const { return TransformVector3(GetValue()) * s; }
        TransformVector3 operator*(double s) const { return TransformVector3(GetValue()) * (float)s; }
        TransformVector3 operator*(int s) const { return TransformVector3(GetValue()) * (float)s; }
        TransformVector3 operator/(float s) const { return TransformVector3(GetValue()) / s; }
        TransformVector3 operator/(double s) const { return TransformVector3(GetValue()) / (float)s; }
        TransformVector3 operator/(int s) const { return TransformVector3(GetValue()) / (float)s; }

        void X(float val);
        void Y(float val);
        void Z(float val);

        float GetX() const;
        float GetY() const;
        float GetZ() const;

        float x() const { return GetX(); }
        float y() const { return GetY(); }
        float z() const { return GetZ(); }

        float X() const { return GetX(); }
        float Y() const { return GetY(); }
        float Z() const { return GetZ(); }

        void SetX(float val) { X(val); }
        void SetY(float val) { Y(val); }
        void SetZ(float val) { Z(val); }

        glm::vec3 GetValue() const;
        void SetValue(const glm::vec3& v);
    };

    GameObject* gameObject = nullptr;

    AxisProxy WorldLocation{this, AxisProxy::TargetType::WorldLocation};
    AxisProxy RelativeLocation{this, AxisProxy::TargetType::RelativeLocation};
    AxisProxy WorldRotation{this, AxisProxy::TargetType::WorldRotation};
    AxisProxy RelativeRotation{this, AxisProxy::TargetType::RelativeRotation};
    AxisProxy WorldScale{this, AxisProxy::TargetType::WorldScale};
    AxisProxy RelativeScale{this, AxisProxy::TargetType::RelativeScale};

    AxisProxy WorldPosition{this, AxisProxy::TargetType::WorldLocation};
    AxisProxy RelativePosition{this, AxisProxy::TargetType::RelativeLocation};

    GameObjectTransform() : GameObjectTransform(nullptr) {}
    GameObjectTransform(GameObject* go) : gameObject(go) {
        InitProxies();
    }
    GameObjectTransform(const GameObjectTransform& other) : gameObject(other.gameObject) {
        InitProxies();
    }
    GameObjectTransform& operator=(const GameObjectTransform& other) {
        if (this != &other) {
            gameObject = other.gameObject;
            InitProxies();
        }
        return *this;
    }

    void InitProxies() {
        WorldLocation = AxisProxy(this, AxisProxy::TargetType::WorldLocation);
        RelativeLocation = AxisProxy(this, AxisProxy::TargetType::RelativeLocation);
        WorldRotation = AxisProxy(this, AxisProxy::TargetType::WorldRotation);
        RelativeRotation = AxisProxy(this, AxisProxy::TargetType::RelativeRotation);
        WorldScale = AxisProxy(this, AxisProxy::TargetType::WorldScale);
        RelativeScale = AxisProxy(this, AxisProxy::TargetType::RelativeScale);
        WorldPosition = AxisProxy(this, AxisProxy::TargetType::WorldLocation);
        RelativePosition = AxisProxy(this, AxisProxy::TargetType::RelativeLocation);
    }

    // Direct Getters (Docs/Behaviours/Transform.md: # GET)
    TransformVector3 GetWorldLocation() const { return WorldLocation(); }
    TransformVector3 GetRelativeLocation() const { return RelativeLocation(); }
    TransformVector3 GetWorldRotation() const { return WorldRotation(); }
    TransformVector3 GetRelativeRotation() const { return RelativeRotation(); }
    TransformVector3 GetWorldScale() const { return WorldScale(); }
    TransformVector3 GetRelativeScale() const { return RelativeScale(); }

    TransformVector3 GetWorldPosition() const { return WorldLocation(); }
    TransformVector3 GetRelativePosition() const { return RelativeLocation(); }

    // Transform State (Docs/Behaviours/Transform.md: # TRANSFORM)
    TransformState GetWorldTransform() const;
    TransformState GetRelativeTransform() const;

    // Directions (Docs/Behaviours/Transform.md: # DIRECTION & # DIRECTION AXIS)
    TransformVector3 GetForwardVector() const;
    TransformVector3 GetRightVector() const;
    TransformVector3 GetUpVector() const;

    glm::vec3 GetWorldLocationInternal() const;
    void SetWorldLocationInternal(const glm::vec3& v);
    glm::vec3 GetRelativeLocationInternal() const;
    void SetRelativeLocationInternal(const glm::vec3& v);
    glm::vec3 GetWorldRotationInternal() const;
    void SetWorldRotationInternal(const glm::vec3& v);
    glm::vec3 GetRelativeRotationInternal() const;
    void SetRelativeRotationInternal(const glm::vec3& v);
    glm::vec3 GetWorldScaleInternal() const;
    void SetWorldScaleInternal(const glm::vec3& v);
    glm::vec3 GetRelativeScaleInternal() const;
    void SetRelativeScaleInternal(const glm::vec3& v);

    // World Translate (dev.md: <GameObject>.Transform.WorldTranslate(0, 0, 0);)
    // 1. By delta (x, y, z) / (delta) / (new Vector3(...))
    void WorldTranslate(const glm::vec3& delta);
    void WorldTranslate(float x, float y, float z) { WorldTranslate(glm::vec3(x, y, z)); }
    void WorldTranslate(const glm::vec3* delta) { if (delta) { WorldTranslate(*delta); delete delta; } }

    // 2. From exact point to another location in world space:
    // Moves object such that point 'fromPoint' moves to 'toLocation' (delta = toLocation - fromPoint)
    void WorldTranslate(const glm::vec3& fromPoint, const glm::vec3& toLocation);
    void WorldTranslate(float fromX, float fromY, float fromZ, float toX, float toY, float toZ) {
        WorldTranslate(glm::vec3(fromX, fromY, fromZ), glm::vec3(toX, toY, toZ));
    }
    void WorldTranslate(const glm::vec3* fromPoint, const glm::vec3* toLocation) {
        if (fromPoint && toLocation) { WorldTranslate(*fromPoint, *toLocation); }
        if (fromPoint) delete fromPoint;
        if (toLocation) delete toLocation;
    }
    void WorldTranslateFromTo(const glm::vec3& fromPoint, const glm::vec3& toLocation) { WorldTranslate(fromPoint, toLocation); }
    void WorldTranslateFromTo(float fromX, float fromY, float fromZ, float toX, float toY, float toZ) {
        WorldTranslate(fromX, fromY, fromZ, toX, toY, toZ);
    }
    void WorldTranslateFromTo(const glm::vec3* fromPoint, const glm::vec3* toLocation) { WorldTranslate(fromPoint, toLocation); }
    void WorldTranslateTo(const glm::vec3& targetLocation) { WorldLocation(targetLocation); }
    void WorldTranslateTo(float x, float y, float z) { WorldLocation(x, y, z); }
    void WorldTranslateTo(const glm::vec3* targetLocation) { WorldLocation(targetLocation); }

    // Relative Translate (dev.md: <GameObject>.Transform.RelativeTranslate(0, 0, 0);)
    // 1. By delta (x, y, z) / (delta) / (new Vector3(...))
    void RelativeTranslate(const glm::vec3& delta);
    void RelativeTranslate(float x, float y, float z) { RelativeTranslate(glm::vec3(x, y, z)); }
    void RelativeTranslate(const glm::vec3* delta) { if (delta) { RelativeTranslate(*delta); delete delta; } }

    // 2. From exact point to another location in relative space
    void RelativeTranslate(const glm::vec3& fromPoint, const glm::vec3& toLocation);
    void RelativeTranslate(float fromX, float fromY, float fromZ, float toX, float toY, float toZ) {
        RelativeTranslate(glm::vec3(fromX, fromY, fromZ), glm::vec3(toX, toY, toZ));
    }
    void RelativeTranslate(const glm::vec3* fromPoint, const glm::vec3* toLocation) {
        if (fromPoint && toLocation) { RelativeTranslate(*fromPoint, *toLocation); }
        if (fromPoint) delete fromPoint;
        if (toLocation) delete toLocation;
    }
    void RelativeTranslateFromTo(const glm::vec3& fromPoint, const glm::vec3& toLocation) { RelativeTranslate(fromPoint, toLocation); }
    void RelativeTranslateFromTo(float fromX, float fromY, float fromZ, float toX, float toY, float toZ) {
        RelativeTranslate(fromX, fromY, fromZ, toX, toY, toZ);
    }
    void RelativeTranslateFromTo(const glm::vec3* fromPoint, const glm::vec3* toLocation) { RelativeTranslate(fromPoint, toLocation); }
    void RelativeTranslateTo(const glm::vec3& targetLocation) { RelativeLocation(targetLocation); }
    void RelativeTranslateTo(float x, float y, float z) { RelativeLocation(x, y, z); }
    void RelativeTranslateTo(const glm::vec3* targetLocation) { RelativeLocation(targetLocation); }

    // General Translate
    void Translate(const glm::vec3& delta) { RelativeTranslate(delta); }
    void Translate(float x, float y, float z) { RelativeTranslate(x, y, z); }
    void Translate(const glm::vec3* delta) { RelativeTranslate(delta); }
    void Translate(const glm::vec3& fromPoint, const glm::vec3& toLocation) { RelativeTranslate(fromPoint, toLocation); }
    void Translate(float fromX, float fromY, float fromZ, float toX, float toY, float toZ) {
        RelativeTranslate(fromX, fromY, fromZ, toX, toY, toZ);
    }
    void Translate(const glm::vec3* fromPoint, const glm::vec3* toLocation) { RelativeTranslate(fromPoint, toLocation); }
    void TranslateFromTo(const glm::vec3& fromPoint, const glm::vec3& toLocation) { RelativeTranslate(fromPoint, toLocation); }
    void TranslateTo(const glm::vec3& targetLocation) { RelativeTranslateTo(targetLocation); }

    // Directional convenience (Docs/Behaviuor.md Section 5)
    TransformVector3 GetBackwardVector() const { return -GetForwardVector(); }
    TransformVector3 GetLeftVector() const { return -GetRightVector(); }
    TransformVector3 GetDownVector() const { return -GetUpVector(); }

    // Rotation Helpers (Docs/Behaviuor.md Section 3.4)
    void LookAt(const glm::vec3& targetPos, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));
    void LookAt(const GameObject* otherObject, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));
    void LookAt(const ObjectRef& otherObject, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));
    void RotateTowards(const glm::vec3& targetRot, float degreesPerSecond, float dt = -1.0f);
    void RotateTowards(const TransformVector3& targetRot, float degreesPerSecond, float dt = -1.0f);
    void Rotate(const glm::vec3& axis, float degrees, Space space = Space::Local);
    void SetForward(const glm::vec3& direction);
    void SetForward(const TransformVector3& direction);
    glm::quat GetWorldQuaternion() const;
    void SetWorldQuaternion(const glm::quat& q);

    // Combined & Utility Calls (Docs/Behaviuor.md Section 3.5)
    void SetWorld(const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale);
    void SetRelative(const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale);
    void Teleport(const glm::vec3& pos, const glm::vec3& rot);
    void SetParent(GameObject* parentObject, bool keepWorld = true);
    GameObject* GetParent() const;

    glm::vec3 TransformPoint(const glm::vec3& localPoint) const;
    glm::vec3 InverseTransformPoint(const glm::vec3& worldPoint) const;
    glm::vec3 TransformDirection(const glm::vec3& localDir) const;
    glm::vec3 InverseTransformDirection(const glm::vec3& worldDir) const;

    float DistanceTo(const glm::vec3& point) const;
    float DistanceTo(const GameObject* otherObject) const;
    float DistanceTo(const ObjectRef& otherObject) const;
    glm::vec3 DirectionTo(const glm::vec3& point) const;
    glm::vec3 DirectionTo(const GameObject* otherObject) const;
    glm::vec3 DirectionTo(const ObjectRef& otherObject) const;
    glm::mat4 GetLocalToWorld() const;

    // Movement Helpers (Docs/Behaviuor.md Section 4.4)
    void MoveForward(float distance) { RelativeTranslate(distance, 0.0f, 0.0f); }
    void MoveRight(float distance) { RelativeTranslate(0.0f, 0.0f, distance); }
    void MoveUp(float distance) { RelativeTranslate(0.0f, distance, 0.0f); }
    bool MoveTowards(const glm::vec3& targetPos, float maxDelta);
};

struct GameObject {
    int id = 0;
    std::string name = "Object";
    PrimitiveType type = PrimitiveType::Cube;
    Mobility mobility = Mobility::Movable;
    void SetMobility(Mobility m, bool recursive = true);

    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotation{0.0f, 0.0f, 0.0f}; // Euler angles in degrees
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    glm::vec3 color{0.55f, 0.55f, 0.55f}; // Default gray

    // Surface Textures (from dev.md)
    std::string baseColorTexture = "";
    std::string normalTexture = "";
    std::string roughnessTexture = "";
    std::string metallicTexture = "";
    std::string aoTexture = "";
    std::string emissionTexture = "";
    std::string opacityTexture = "";

    // PBR Material properties
    std::string materialName = "Default_Material";
    float metallic = 0.0f;
    float roughness = 0.5f;
    float normalStrength = 1.0f;
    float specular = 0.5f;
    glm::vec3 emissiveColor{0.0f, 0.0f, 0.0f};
    float emissiveIntensity = 0.0f;
    float opacity = 1.0f;
    float opacityMaskClipValue = 0.333f;
    int shadingModel = 0; // 0 = Default Lit (PBR), 1 = Unlit, 2 = Subsurface
    int blendMode = 0;    // 0 = Opaque, 1 = Masked, 2 = Translucent
    bool twoSided = false;
    bool castShadows = true;
    bool receiveShadows = true;
    bool meshClusterCulling = false; // dev.md: Mesh Cluster Culling (OFF by default)
    bool smoothShading = true;       // Blender-style Smooth Shading (true = smooth, false = flat)
    float autoSmoothAngle = 180.0f;  // Crease angle in degrees for smooth normal calculation (default 180.0f = full smooth)
    bool enableLOD = false;          // Wicked Engine Mesh LOD: disabled by default (full quality)
    int forcedLOD = -1;              // -1 = Auto (distance & screen coverage), 0 = LOD0 .. 20 = LOD20
    float lodBias = 0.0f;            // Wicked Engine lod_bias (-20.0f to +20.0f)
    int minLOD = 0;                  // Minimum LOD clamp (0 to 20)
    int maxLOD = 20;                 // Maximum LOD clamp (0 to 20)
    int currentLOD = 0;              // Active runtime LOD selected for rendering (telemetry/debug)
    // SFLOD (Single Face LOD / Imposter Cards System - Nacho Time!)
    bool enableSFLOD = false;        // Master toggle for SFLOD
    int sflodMinLOD = 3;             // Trigger threshold: SFLOD LOD >= sflodMinLOD renders as imposter card (0 to 20)
    int sflodMaxLOD = 20;            // SFLOD maximum LOD clamp (0 to 20)
    float sflodLODBias = 0.0f;       // SFLOD continuous screen-space LOD bias (-20.0f to +20.0f, independent of mesh LOD)
    int sflodCurrentLOD = 0;         // Runtime calculated SFLOD LOD level (0 to 20, works even if mesh LOD is disabled)
    int sflodBillboardMode = 0;      // 0 = Camera-facing (spherical), 1 = Locked Y-axis (axial/Nacho Time), 2 = Fixed
    int sflodGeometryMode = 0;       // 0 = Single Plane (2 Tris), 1 = Tri-Planar Nacho Mode (6 Tris), 2 = 5-Face Box (10 Tris), 3 = 6-Face Cube (12 Tris), 4 = Custom Box
    int sflodOpenFace = 4;           // 0=Front, 1=Back, 2=Right, 3=Left, 4=Top (+Y, open crate), 5=Bottom
    uint8_t sflodFaceMask = 0x2F;    // Active faces bitmask (bits 0..5 = Front, Back, Right, Left, Top, Bottom; default 0x2F = 5 faces open top)
    int sflodShaderComplexity = 0;   // 0 = Low (Diffuse+Alpha), 1 = Medium (+Normal), 2 = High (+RMA)
    float sflodDistanceThreshold = 25.0f; // Distance threshold (imp_threshold) in meters (0 = disable distance trigger)
    bool sflodIsAtlas = false;       // True if current imposter texture is a 6-directional Nacho Time atlas
    float sflodPlaneWidth = 0.0f;    // 0 = Auto-calculate from mesh AABB
    float sflodPlaneHeight = 0.0f;   // 0 = Auto-calculate from mesh AABB
    glm::vec2 sflodPlaneOffset{0.0f, 0.0f}; // Fine-tuning card center offset
    std::string sflodCustomTexture = "";    // Optional custom imposter texture; empty uses baseColorTexture
    bool sflodCutout = true;         // Alpha cutout masking for foliage / cards
    glm::vec2 uvScale{1.0f, 1.0f};
    bool normalMapYFlip = false;     // Invert normal green/Y channel (OpenGL/Blender convention)
    int metallicChannel = 0;         // 0=R, 1=G, 2=B, 3=A
    int roughnessChannel = 1;        // 0=R, 1=G, 2=B, 3=A
    int aoChannel = 0;               // 0=R, 1=G, 2=B, 3=A
    int materialDebugMode = 0;       // 0=Final PBR, 1..10 diagnostic modes
    bool disableStreaming = false;   // Wicked Engine: Disable texture streaming for this object

    bool visible = true;
    bool autoRotate = false;
    glm::vec3 autoRotateSpeed{0.0f, 45.0f, 0.0f}; // deg/sec

    std::string meshFilePath = "";
    bool isImportedMesh = false;
    int submeshIndex = -1;           // -1 for root/group or single-mesh, >= 0 for submesh index

    // Hierarchy (parent-child)
    int parentId = -1;               // -1 = root
    std::vector<int> childIds;       // ordered child IDs

    // Scene & Transform accessors (dev.md)
    Scene* scene = nullptr;
    GameObjectTransform Transform{this};
    GameObjectTransform& transform = Transform;

    // Light actor proxy: when isLight==true this object represents a PointLight
    // Light actor proxy: when isLight==true this object represents a Light
    // and uses 'lightId' to index into scene.pointLights (if local light)
    bool isLight = false;
    int  lightId  = -1;              // index into Scene::pointLights
    LightComponent light;
    bool isCamera = false;
    CameraComponent camera;
    bool isWaterBody = false;
    WaterBodyComponent water;
    bool isTerrain = false;
    Eunoia::TerrainComponent terrain;
    bool isUserInterface = false;
    EunoiaUI::UserInterface3DComponent ui3d;
    PrimitiveParams params;

    PrimitiveMesh mesh;
    CollisionComponent collision;
    std::vector<CollisionModifier> modifiers;
    bool hasPhysics = false;
    PhysicsModifier physics;
    bool hasAnimator = false;
    AnimationGraphModifier animator;
    bool hasLevelAnimation = false;
    Eunoia::LevelAnimationComponent levelAnimation;

    void AddAnimatorModifier() {
        hasAnimator = true;
        animator.enabled = true;
        animator.name = "Animation Graph";
    }

    void RemoveAnimatorModifier() {
        hasAnimator = false;
        animator.enabled = false;
        animator.graph.reset();
        animator.instance.reset();
        animator.graphPath = "";
    }

    void AddLevelAnimation() {
        hasLevelAnimation = true;
        levelAnimation.enabled = true;
        levelAnimation.playOnStart = true;
        levelAnimation.loop = true;
        levelAnimation.playbackSpeed = 1.0f;
    }

    void RemoveLevelAnimation() {
        hasLevelAnimation = false;
        levelAnimation.enabled = false;
        levelAnimation.animation.reset();
        levelAnimation.animationPath = "";
        levelAnimation.isPlaying = false;
        levelAnimation.currentTime = 0.0f;
    }

    // Tags system (Support for all GameObjects, Actors, Empty Actors)
    std::vector<std::string> tags;

    const std::vector<std::string>& GetTags() const { return tags; }
    std::vector<std::string>& GetTags() { return tags; }
    std::vector<std::string> getTags() const { return tags; }

    bool HasTag(const std::string& tag) const {
        for (const auto& t : tags) {
            if (t == tag) return true;
        }
        return false;
    }
    bool hasTag(const std::string& tag) const { return HasTag(tag); }

    void AddTag(const std::string& tag) {
        if (tag.empty()) return;
        if (!HasTag(tag)) {
            tags.push_back(tag);
        }
    }
    void addTag(const std::string& tag) { AddTag(tag); }

    bool RemoveTag(const std::string& tag) {
        for (auto it = tags.begin(); it != tags.end(); ++it) {
            if (*it == tag) {
                tags.erase(it);
                return true;
            }
        }
        return false;
    }
    bool removeTag(const std::string& tag) { return RemoveTag(tag); }

    void RemoveAllTag() { tags.clear(); }
    void RemoveAllTags() { tags.clear(); }
    void removeAllTag() { tags.clear(); }
    void removeAllTags() { tags.clear(); }
    void ClearTags() { tags.clear(); }
    void clearTags() { tags.clear(); }

    void SetTags(const std::vector<std::string>& newTags) { tags = newTags; }
    void setTags(const std::vector<std::string>& newTags) { tags = newTags; }

    void SetTag(const std::string& tag) {
        tags.clear();
        if (!tag.empty()) tags.push_back(tag);
    }
    void setTag(const std::string& tag) { SetTag(tag); }

    // Activation & Hierarchy State (Docs/Behaviuor.md Section 9.6)
    void SetActive(bool active) { visible = active; }
    bool IsActive() const { return visible; }
    bool IsActiveInHierarchy() const;

    GameObject* FindChild(const std::string& childName, bool recursive = true) const;
    GameObject* GetParent() const;
    std::vector<GameObject*> GetChildren() const;

    void AddPhysicsModifier() {
        hasPhysics = true;
        physics.name = "Physics Modifier";
        physics.isCharacterPhysics = false;
        physics.enabled = true;
        physics.useGravity = true;
        physics.gravityMultiplier = 1.0f;
        physics.mass = 1.0f;
        physics.bounciness = 0.2f;
        physics.friction = 0.6f;
        physics.linearDamping = 0.05f;
        physics.angularDamping = 0.05f;
        physics.lockRotationX = false;
        physics.lockRotationZ = false;
        physics.ResetVelocity();
    }

    void AddCharacterPhysicsModifier() {
        hasPhysics = true;
        physics.name = "Character Physics";
        physics.isCharacterPhysics = true;
        physics.enableStepBackImpulse = false;
        physics.stepBackImpulseMultiplier = 1.0f;
        physics.enabled = true;
        physics.useGravity = true;
        physics.gravityMultiplier = 1.0f;
        physics.mass = 75.0f;
        physics.bounciness = 0.0f; // Zero bounce for realistic character dynamics
        physics.friction = 0.8f;
        physics.linearDamping = 0.1f;
        physics.angularDamping = 0.9f;
        physics.lockRotationX = true;
        physics.lockRotationZ = true;
        physics.ResetVelocity();

        // If no character collision modifier exists, add Character Collision
        bool hasCharCol = false;
        for (const auto& mod : modifiers) {
            if (mod.shapeType == CollisionShapeType::Character || mod.shapeType == CollisionShapeType::Capsule) {
                hasCharCol = true;
                break;
            }
        }
        if (!hasCharCol && modifiers.empty()) {
            AddModifier(CollisionShapeType::Character);
        }
    }

    void RemovePhysicsModifier() {
        hasPhysics = false;
        physics.enabled = false;
        physics.isCharacterPhysics = false;
        physics.ResetVelocity();
    }

    CollisionModifier& AddModifier(CollisionShapeType shape) {
        CollisionModifier mod(shape);
        float w = (params.width > 0.05f) ? params.width : 1.0f;
        float h = (params.height > 0.05f) ? params.height : 1.0f;
        float d = (params.depth > 0.05f) ? params.depth : 1.0f;
        float r = (params.radius > 0.05f) ? params.radius : 0.5f;

        switch (shape) {
            case CollisionShapeType::Box:
                mod.boxExtents = glm::vec3(w * 0.5f, h * 0.5f, d * 0.5f);
                break;
            case CollisionShapeType::Sphere:
                mod.radius = r;
                break;
            case CollisionShapeType::Capsule:
                mod.radius = r;
                mod.height = h;
                break;
            case CollisionShapeType::Character:
                mod.radius = 0.4f;
                mod.height = 1.8f;
                mod.centerOffset = glm::vec3(0.0f, 0.9f, 0.0f);
                mod.preventSideFall = true;
                mod.sideFall = false;
                break;
            case CollisionShapeType::Cylinder:
                mod.radius = r;
                mod.height = h;
                break;
            case CollisionShapeType::Cone:
                mod.radius = r;
                mod.height = h;
                break;
            case CollisionShapeType::Plane:
                mod.planeSize = glm::vec2(std::max(0.2f, w), std::max(0.2f, d > 0.05f ? d : 2.5f));
                break;
            case CollisionShapeType::Mesh:
                mod.boxExtents = glm::vec3(0.5f);
                break;
            default:
                break;
        }
        mod.name = std::string(GetCollisionShapeTypeName(shape)) + " " + std::to_string(modifiers.size() + 1);
        modifiers.push_back(mod);
        if (modifiers.size() == 1) collision = mod;
        return modifiers.back();
    }

    bool RemoveModifier(size_t index) {
        if (index >= modifiers.size()) return false;
        modifiers.erase(modifiers.begin() + index);
        if (!modifiers.empty()) collision = modifiers[0];
        else {
            collision.enabled = false;
            collision.shapeType = CollisionShapeType::None;
        }
        return true;
    }

    // Attached Behaviours (dev.md Section 1, 2)
    std::vector<std::shared_ptr<EunoiaBehaviour>> behaviours;

    GameObject() : Transform(this) {}

    GameObject(const GameObject& other) : Transform(this) {
        CopyFrom(other);
    }

    GameObject& operator=(const GameObject& other) {
        if (this != &other) {
            CopyFrom(other);
        }
        return *this;
    }

    GameObject(GameObject&& other) noexcept : Transform(this) {
        MoveFrom(std::move(other));
    }

    GameObject& operator=(GameObject&& other) noexcept {
        if (this != &other) {
            MoveFrom(std::move(other));
        }
        return *this;
    }

    void MoveFrom(GameObject&& other) noexcept {
        id = other.id;
        name = std::move(other.name);
        type = other.type;
        mobility = other.mobility;
        position = other.position;
        rotation = other.rotation;
        scale = other.scale;
        color = other.color;
        baseColorTexture = std::move(other.baseColorTexture);
        normalTexture = std::move(other.normalTexture);
        roughnessTexture = std::move(other.roughnessTexture);
        metallicTexture = std::move(other.metallicTexture);
        aoTexture = std::move(other.aoTexture);
        emissionTexture = std::move(other.emissionTexture);
        opacityTexture = std::move(other.opacityTexture);
        materialName = std::move(other.materialName);
        metallic = other.metallic;
        roughness = other.roughness;
        normalStrength = other.normalStrength;
        specular = other.specular;
        emissiveColor = other.emissiveColor;
        emissiveIntensity = other.emissiveIntensity;
        opacity = other.opacity;
        opacityMaskClipValue = other.opacityMaskClipValue;
        shadingModel = other.shadingModel;
        blendMode = other.blendMode;
        twoSided = other.twoSided;
        castShadows = other.castShadows;
        receiveShadows = other.receiveShadows;
        meshClusterCulling = other.meshClusterCulling;
        smoothShading = other.smoothShading;
        autoSmoothAngle = other.autoSmoothAngle;
        enableLOD = other.enableLOD;
        forcedLOD = other.forcedLOD;
        lodBias = other.lodBias;
        minLOD = other.minLOD;
        maxLOD = other.maxLOD;
        currentLOD = other.currentLOD;
        enableSFLOD = other.enableSFLOD;
        sflodMinLOD = other.sflodMinLOD;
        sflodMaxLOD = other.sflodMaxLOD;
        sflodLODBias = other.sflodLODBias;
        sflodCurrentLOD = other.sflodCurrentLOD;
        sflodBillboardMode = other.sflodBillboardMode;
        sflodGeometryMode = other.sflodGeometryMode;
        sflodOpenFace = other.sflodOpenFace;
        sflodFaceMask = other.sflodFaceMask;
        sflodShaderComplexity = other.sflodShaderComplexity;
        sflodDistanceThreshold = other.sflodDistanceThreshold;
        sflodIsAtlas = other.sflodIsAtlas;
        sflodPlaneWidth = other.sflodPlaneWidth;
        sflodPlaneHeight = other.sflodPlaneHeight;
        sflodPlaneOffset = other.sflodPlaneOffset;
        sflodCustomTexture = other.sflodCustomTexture;
        sflodCutout = other.sflodCutout;
        uvScale = other.uvScale;
        normalMapYFlip = other.normalMapYFlip;
        metallicChannel = other.metallicChannel;
        roughnessChannel = other.roughnessChannel;
        aoChannel = other.aoChannel;
        materialDebugMode = other.materialDebugMode;
        disableStreaming = other.disableStreaming;
        visible = other.visible;
        autoRotate = other.autoRotate;
        autoRotateSpeed = other.autoRotateSpeed;
        meshFilePath = std::move(other.meshFilePath);
        isImportedMesh = other.isImportedMesh;
        submeshIndex = other.submeshIndex;
        parentId = other.parentId;
        childIds = std::move(other.childIds);
        isLight = other.isLight;
        lightId = other.lightId;
        light = other.light;
        isCamera = other.isCamera;
        camera = other.camera;
        isWaterBody = other.isWaterBody;
        water = other.water;
        isTerrain = other.isTerrain;
        terrain = other.terrain;
        isUserInterface = other.isUserInterface;
        ui3d = std::move(other.ui3d);
        params = other.params;
        mesh = std::move(other.mesh);
        collision = std::move(other.collision);
        modifiers = std::move(other.modifiers);
        hasPhysics = other.hasPhysics;
        physics = std::move(other.physics);
        hasAnimator = other.hasAnimator;
        animator = std::move(other.animator);
        hasLevelAnimation = other.hasLevelAnimation;
        levelAnimation = std::move(other.levelAnimation);
        tags = std::move(other.tags);
        scene = other.scene;
        Transform.gameObject = this;
        behaviours = std::move(other.behaviours);
        for (auto& b : behaviours) {
            if (b) b->SetOwner(this);
        }
    }

    void CopyFrom(const GameObject& other) {
        id = other.id;
        name = other.name;
        type = other.type;
        mobility = other.mobility;
        tags = other.tags;
        position = other.position;
        rotation = other.rotation;
        scale = other.scale;
        color = other.color;
        baseColorTexture = other.baseColorTexture;
        normalTexture = other.normalTexture;
        roughnessTexture = other.roughnessTexture;
        metallicTexture = other.metallicTexture;
        aoTexture = other.aoTexture;
        emissionTexture = other.emissionTexture;
        opacityTexture = other.opacityTexture;
        materialName = other.materialName;
        metallic = other.metallic;
        roughness = other.roughness;
        normalStrength = other.normalStrength;
        specular = other.specular;
        emissiveColor = other.emissiveColor;
        emissiveIntensity = other.emissiveIntensity;
        opacity = other.opacity;
        opacityMaskClipValue = other.opacityMaskClipValue;
        shadingModel = other.shadingModel;
        blendMode = other.blendMode;
        twoSided = other.twoSided;
        castShadows = other.castShadows;
        receiveShadows = other.receiveShadows;
        meshClusterCulling = other.meshClusterCulling;
        smoothShading = other.smoothShading;
        autoSmoothAngle = other.autoSmoothAngle;
        enableLOD = other.enableLOD;
        forcedLOD = other.forcedLOD;
        lodBias = other.lodBias;
        minLOD = other.minLOD;
        maxLOD = other.maxLOD;
        currentLOD = other.currentLOD;
        enableSFLOD = other.enableSFLOD;
        sflodMinLOD = other.sflodMinLOD;
        sflodMaxLOD = other.sflodMaxLOD;
        sflodLODBias = other.sflodLODBias;
        sflodCurrentLOD = other.sflodCurrentLOD;
        sflodBillboardMode = other.sflodBillboardMode;
        sflodGeometryMode = other.sflodGeometryMode;
        sflodOpenFace = other.sflodOpenFace;
        sflodFaceMask = other.sflodFaceMask;
        sflodShaderComplexity = other.sflodShaderComplexity;
        sflodDistanceThreshold = other.sflodDistanceThreshold;
        sflodIsAtlas = other.sflodIsAtlas;
        sflodPlaneWidth = other.sflodPlaneWidth;
        sflodPlaneHeight = other.sflodPlaneHeight;
        sflodPlaneOffset = other.sflodPlaneOffset;
        sflodCustomTexture = other.sflodCustomTexture;
        sflodCutout = other.sflodCutout;
        uvScale = other.uvScale;
        normalMapYFlip = other.normalMapYFlip;
        metallicChannel = other.metallicChannel;
        roughnessChannel = other.roughnessChannel;
        aoChannel = other.aoChannel;
        materialDebugMode = other.materialDebugMode;
        disableStreaming = other.disableStreaming;
        visible = other.visible;
        autoRotate = other.autoRotate;
        autoRotateSpeed = other.autoRotateSpeed;
        meshFilePath = other.meshFilePath;
        isImportedMesh = other.isImportedMesh;
        submeshIndex = other.submeshIndex;
        parentId = other.parentId;
        childIds = other.childIds;
        isLight = other.isLight;
        lightId = other.lightId;
        light = other.light;
        isCamera = other.isCamera;
        camera = other.camera;
        isWaterBody = other.isWaterBody;
        water = other.water;
        isTerrain = other.isTerrain;
        terrain = other.terrain;
        isUserInterface = other.isUserInterface;
        ui3d = other.ui3d;
        params = other.params;
        mesh = other.mesh;
        collision = other.collision;
        modifiers = other.modifiers;
        hasPhysics = other.hasPhysics;
        physics = other.physics;
        hasAnimator = other.hasAnimator;
        animator = other.animator;
        hasLevelAnimation = other.hasLevelAnimation;
        levelAnimation = other.levelAnimation;
        scene = other.scene;
        Transform.gameObject = this;
        behaviours.clear();
        for (const auto& b : other.behaviours) {
            if (b) {
                auto clone = b->Clone();
                if (clone) {
                    clone->SetOwner(this);
                    behaviours.push_back(std::move(clone));
                } else {
                    auto bInst = BehaviourRegistry::Get().Create(b->GetClassName());
                    if (bInst) {
                        bInst->SetOwner(this);
                        bInst->CopyPropertiesFrom(*b);
                        bInst->SetEnabled(b->IsEnabled());
                        behaviours.push_back(std::move(bInst));
                    }
                }
            }
        }
    }

    void AddBehaviour(std::shared_ptr<EunoiaBehaviour> b) {
        if (!b) return;
        b->SetOwner(this);
        behaviours.push_back(b);
        b->OnCreate();
    }

    bool RemoveBehaviour(size_t index) {
        if (index >= behaviours.size()) return false;
        if (behaviours[index]) {
            behaviours[index]->OnDisable();
            behaviours[index]->OnDestroy();
        }
        behaviours.erase(behaviours.begin() + index);
        return true;
    }

    bool ReorderBehaviour(size_t fromIdx, size_t toIdx) {
        if (fromIdx >= behaviours.size() || toIdx >= behaviours.size() || fromIdx == toIdx) return false;
        std::swap(behaviours[fromIdx], behaviours[toIdx]);
        return true;
    }

    template<typename T>
    T* GetBehaviour() {
        for (auto& b : behaviours) {
            if (auto casted = dynamic_cast<T*>(b.get())) {
                return casted;
            }
        }
        return nullptr;
    }

    GameObject(int objId, const std::string& objName, PrimitiveType objType, glm::vec3 pos, glm::vec3 col = {0.55f, 0.55f, 0.55f})
        : id(objId), name(objName), type(objType), position(pos), color(col), Transform(this) {
        if (type == PrimitiveType::Camera) {
            isCamera = true;
        }
        if (type == PrimitiveType::LevelAnimation) {
            hasLevelAnimation = true;
            levelAnimation.enabled = true;
            levelAnimation.playOnStart = true;
            levelAnimation.loop = true;
            levelAnimation.playbackSpeed = 1.0f;
        }
        if (type == PrimitiveType::UserInterface) {
            isUserInterface = true;
            ui3d.enabled = true;
            params.width = ui3d.size.x;
            params.height = ui3d.size.y;
        }
        if (IsLightPrimitive(type)) {
            isLight = true;
            switch (type) {
                case PrimitiveType::DirectionalLight: light.type = LightType::Directional; light.castShadows = true; break;
                case PrimitiveType::PointLight:       light.type = LightType::Point;       light.castShadows = true; break;
                case PrimitiveType::SpotLight:        light.type = LightType::Spot;        light.castShadows = true; break;
                case PrimitiveType::AreaLight:        light.type = LightType::Area;        light.castShadows = true; break;
                case PrimitiveType::SkyLight:         light.type = LightType::Sky;         light.castShadows = false; light.intensity = 1.0f; break;
                default: break;
            }
            light.color = col;
        }

        // Set default primitive parameters
        if (type == PrimitiveType::Plane) { params.width = 2.5f; params.depth = 2.5f; params.segmentsX = 16; params.segmentsZ = 16; }
        else if (type == PrimitiveType::Box) { params.width = 1.0f; params.height = 1.0f; params.depth = 1.0f; params.segmentsX = 1; params.segmentsY = 1; params.segmentsZ = 1; }
        else if (type == PrimitiveType::Sphere || type == PrimitiveType::UVSphere) { params.radius = 0.6f; params.rings = 24; params.sectors = 24; }
        else if (type == PrimitiveType::Icosphere) { params.radius = 0.6f; params.subdivisions = 2; }
        else if (type == PrimitiveType::Cylinder) { params.radius = 0.5f; params.height = 1.2f; params.radialSegments = 24; params.heightSegments = 1; }
        else if (type == PrimitiveType::Cone) { params.radius = 0.5f; params.radius2 = 0.0f; params.height = 1.2f; params.radialSegments = 24; params.heightSegments = 1; }
        else if (type == PrimitiveType::Capsule) { params.radius = 0.35f; params.height = 0.8f; params.radialSegments = 20; params.heightSegments = 2; }
        else if (type == PrimitiveType::Torus) { params.radius = 0.6f; params.radius2 = 0.22f; params.radialSegments = 24; params.segmentsY = 16; }
        else if (type == PrimitiveType::Circle || type == PrimitiveType::Disc) { params.radius = 0.6f; params.radialSegments = 32; }
        else if (type == PrimitiveType::Quad) { params.width = 1.5f; params.height = 1.5f; }
        else if (type == PrimitiveType::Triangle) { params.width = 1.2f; params.height = 1.2f; }
        else if (type == PrimitiveType::Pyramid) { params.width = 1.0f; params.depth = 1.0f; params.height = 1.2f; params.sides = 4; }
        else if (type == PrimitiveType::Prism) { params.radius = 0.6f; params.height = 1.2f; params.sides = 3; }

        // By default, newly created meshes, primitives, imported meshes, lights, and cameras have NO colliders
        modifiers.clear();
        collision.enabled = false;
        collision.shapeType = CollisionShapeType::None;
        RebuildMesh();
    }

    void RebuildMesh() {
        switch (type) {
            case PrimitiveType::Cube:
                mesh = GeometryBuilder::CreateCube(params.size, params.segmentsX);
                break;
            case PrimitiveType::Plane:
                mesh = GeometryBuilder::CreatePlane(params.width, params.depth, params.segmentsX, params.segmentsZ);
                break;
            case PrimitiveType::Box:
                mesh = GeometryBuilder::CreateBox(params.width, params.height, params.depth, params.segmentsX, params.segmentsY, params.segmentsZ);
                break;
            case PrimitiveType::Sphere:
                mesh = GeometryBuilder::CreateSphere(params.radius, params.rings, params.sectors);
                break;
            case PrimitiveType::UVSphere:
                mesh = GeometryBuilder::CreateUVSphere(params.radius, params.sectors, params.rings);
                break;
            case PrimitiveType::Icosphere:
                mesh = GeometryBuilder::CreateIcosphere(params.radius, params.subdivisions);
                break;
            case PrimitiveType::Cylinder:
                mesh = GeometryBuilder::CreateCylinder(params.radius, params.height, params.radialSegments, params.heightSegments, params.capTop, params.capBottom);
                break;
            case PrimitiveType::Cone:
                mesh = GeometryBuilder::CreateCone(params.radius, params.radius2, params.height, params.radialSegments, params.heightSegments, params.capBottom, params.capTop);
                break;
            case PrimitiveType::Capsule:
                mesh = GeometryBuilder::CreateCapsule(params.radius, params.height, params.radialSegments, params.heightSegments, std::max(2, params.rings / 3));
                break;
            case PrimitiveType::Torus:
                mesh = GeometryBuilder::CreateTorus(params.radius, params.radius2 > 0.001f ? params.radius2 : 0.2f, params.radialSegments, params.segmentsY);
                break;
            case PrimitiveType::Circle:
                mesh = GeometryBuilder::CreateCircle(params.radius, params.radialSegments);
                break;
            case PrimitiveType::Disc:
                mesh = GeometryBuilder::CreateDisc(params.radius, params.radialSegments);
                break;
            case PrimitiveType::Quad:
                mesh = GeometryBuilder::CreateQuad(params.width, params.height);
                break;
            case PrimitiveType::Triangle:
                mesh = GeometryBuilder::CreateTriangle(params.width, params.height);
                break;
            case PrimitiveType::Pyramid:
                mesh = GeometryBuilder::CreatePyramid(params.width, params.depth, params.height, params.sides);
                break;
            case PrimitiveType::Prism:
                mesh = GeometryBuilder::CreatePrism(params.radius, params.height, params.sides);
                break;
            case PrimitiveType::Terrain:
                terrain.RebuildTerrainMesh(mesh);
                break;
            case PrimitiveType::UserInterface:
            case PrimitiveType::ImportedMesh:
            case PrimitiveType::Empty:
            default:
                if (IsLightPrimitive(type) || IsCameraPrimitive(type) || type == PrimitiveType::Empty || type == PrimitiveType::UserInterface || type == PrimitiveType::ImportedMesh) {
                    mesh.vertices.clear();
                    mesh.indices.clear();
                }
                break;
        }
    }

    void SetSmoothShading(bool smooth, float creaseAngle = 180.0f) {
        smoothShading = smooth;
        autoSmoothAngle = creaseAngle;
        if (mesh.vertices.empty()) return;
        if (smooth) {
            GeometryBuilder::ComputeSmoothNormals(mesh, creaseAngle);
        } else {
            GeometryBuilder::ComputeFlatNormals(mesh);
        }
    }

    glm::mat4 GetLocalMatrix() const {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        model = glm::scale(model, scale);
        return model;
    }

    glm::mat4 GetModelMatrix() const {
        return GetLocalMatrix();
    }

    // Direct Transform Translation helpers (dev.md)
    void WorldTranslate(float x, float y, float z) { Transform.WorldTranslate(x, y, z); }
    void WorldTranslate(const glm::vec3& delta) { Transform.WorldTranslate(delta); }
    void WorldTranslate(const glm::vec3* delta) { Transform.WorldTranslate(delta); }
    void WorldTranslate(const glm::vec3& fromPoint, const glm::vec3& toLocation) { Transform.WorldTranslate(fromPoint, toLocation); }
    void WorldTranslate(float fromX, float fromY, float fromZ, float toX, float toY, float toZ) { Transform.WorldTranslate(fromX, fromY, fromZ, toX, toY, toZ); }
    void WorldTranslate(const glm::vec3* fromPoint, const glm::vec3* toLocation) { Transform.WorldTranslate(fromPoint, toLocation); }
    void WorldTranslateFromTo(const glm::vec3& fromPoint, const glm::vec3& toLocation) { Transform.WorldTranslateFromTo(fromPoint, toLocation); }
    void WorldTranslateFromTo(float fromX, float fromY, float fromZ, float toX, float toY, float toZ) { Transform.WorldTranslateFromTo(fromX, fromY, fromZ, toX, toY, toZ); }
    void WorldTranslateFromTo(const glm::vec3* fromPoint, const glm::vec3* toLocation) { Transform.WorldTranslateFromTo(fromPoint, toLocation); }
    void WorldTranslateTo(const glm::vec3& targetLocation) { Transform.WorldTranslateTo(targetLocation); }
    void WorldTranslateTo(float x, float y, float z) { Transform.WorldTranslateTo(x, y, z); }
    void WorldTranslateTo(const glm::vec3* targetLocation) { Transform.WorldTranslateTo(targetLocation); }

    void RelativeTranslate(float x, float y, float z) { Transform.RelativeTranslate(x, y, z); }
    void RelativeTranslate(const glm::vec3& delta) { Transform.RelativeTranslate(delta); }
    void RelativeTranslate(const glm::vec3* delta) { Transform.RelativeTranslate(delta); }
    void RelativeTranslate(const glm::vec3& fromPoint, const glm::vec3& toLocation) { Transform.RelativeTranslate(fromPoint, toLocation); }
    void RelativeTranslate(float fromX, float fromY, float fromZ, float toX, float toY, float toZ) { Transform.RelativeTranslate(fromX, fromY, fromZ, toX, toY, toZ); }
    void RelativeTranslate(const glm::vec3* fromPoint, const glm::vec3* toLocation) { Transform.RelativeTranslate(fromPoint, toLocation); }
    void RelativeTranslateFromTo(const glm::vec3& fromPoint, const glm::vec3& toLocation) { Transform.RelativeTranslateFromTo(fromPoint, toLocation); }
    void RelativeTranslateFromTo(float fromX, float fromY, float fromZ, float toX, float toY, float toZ) { Transform.RelativeTranslateFromTo(fromX, fromY, fromZ, toX, toY, toZ); }
    void RelativeTranslateFromTo(const glm::vec3* fromPoint, const glm::vec3* toLocation) { Transform.RelativeTranslateFromTo(fromPoint, toLocation); }
    void RelativeTranslateTo(const glm::vec3& targetLocation) { Transform.RelativeTranslateTo(targetLocation); }
    void RelativeTranslateTo(float x, float y, float z) { Transform.RelativeTranslateTo(x, y, z); }
    void RelativeTranslateTo(const glm::vec3* targetLocation) { Transform.RelativeTranslateTo(targetLocation); }

    void Translate(float x, float y, float z) { Transform.Translate(x, y, z); }
    void Translate(const glm::vec3& delta) { Transform.Translate(delta); }
    void Translate(const glm::vec3* delta) { Transform.Translate(delta); }
    void Translate(const glm::vec3& fromPoint, const glm::vec3& toLocation) { Transform.Translate(fromPoint, toLocation); }
    void Translate(float fromX, float fromY, float fromZ, float toX, float toY, float toZ) { Transform.Translate(fromX, fromY, fromZ, toX, toY, toZ); }
    void Translate(const glm::vec3* fromPoint, const glm::vec3* toLocation) { Transform.Translate(fromPoint, toLocation); }
    void TranslateFromTo(const glm::vec3& fromPoint, const glm::vec3& toLocation) { Transform.TranslateFromTo(fromPoint, toLocation); }
    void TranslateTo(const glm::vec3& targetLocation) { Transform.TranslateTo(targetLocation); }

    // Direct Transform Getters (Docs/Behaviours/Transform.md: # GET, # TRANSFORM, # DIRECTION)
    TransformVector3 GetWorldLocation() const { return Transform.GetWorldLocation(); }
    TransformVector3 GetRelativeLocation() const { return Transform.GetRelativeLocation(); }
    TransformVector3 GetWorldRotation() const { return Transform.GetWorldRotation(); }
    TransformVector3 GetRelativeRotation() const { return Transform.GetRelativeRotation(); }
    TransformVector3 GetWorldScale() const { return Transform.GetWorldScale(); }
    TransformVector3 GetRelativeScale() const { return Transform.GetRelativeScale(); }

    TransformState GetWorldTransform() const { return Transform.GetWorldTransform(); }
    TransformState GetRelativeTransform() const { return Transform.GetRelativeTransform(); }

    TransformVector3 GetForwardVector() const { return Transform.GetForwardVector(); }
    TransformVector3 GetRightVector() const { return Transform.GetRightVector(); }
    TransformVector3 GetUpVector() const { return Transform.GetUpVector(); }

    // Character Mechanics
    Eunoia::CharacterMechanics::CharacterMechanicsHandle AssumeAsCharacter() {
        Eunoia::CharacterMechanics::CharacterMechanicsSystem::Get().SetActiveCharacter(this);
        return Eunoia::CharacterMechanics::CharacterMechanicsHandle{ this, nullptr };
    }

    float GetVelocity(const glm::vec3& directionMask = glm::vec3(1.0f, 1.0f, 1.0f)) {
        return Eunoia::CharacterMechanics::CharacterMechanicsSystem::Get().GetVelocity(this, directionMask);
    }
    float GetVelocity(const TransformVector3& directionMask) {
        return Eunoia::CharacterMechanics::CharacterMechanicsSystem::Get().GetVelocity(this, directionMask);
    }
    float GetVelocity(float dirX, float dirY, float dirZ) {
        return Eunoia::CharacterMechanics::CharacterMechanicsSystem::Get().GetVelocity(this, dirX, dirY, dirZ);
    }

    void SetOrientationToMovementDirection(bool enable, float rotationRate = 720.0f) {
        Eunoia::CharacterMechanics::CharacterMechanicsSystem::Get().SetOrientationToMovementDirection(this, enable, rotationRate);
        Eunoia::CharacterMechanics::CharacterMechanicsSystem::Get().SetBodyRotateToMovementDirection(enable, this, rotationRate);
    }
    bool IsOrientationToMovementDirectionEnabled() const {
        return Eunoia::CharacterMechanics::CharacterMechanicsSystem::Get().IsOrientationToMovementDirectionEnabled(const_cast<GameObject*>(this));
    }

    void SetBodyRotateToMovementDirection(bool enable, GameObject* body = nullptr, float rotationRate = 720.0f) {
        if (body) {
            Eunoia::CharacterMechanics::CharacterMechanicsSystem::Get().SetBodyObject(this, body);
            Eunoia::CharacterMechanics::CharacterMechanicsSystem::Get().SetBodyRotateToMovementDirection(enable, this, rotationRate);
            Eunoia::CharacterMechanics::CharacterMechanicsSystem::Get().SetBodyRotateToMovementDirection(enable, body, rotationRate);
            Eunoia::CharacterMechanics::CharacterMechanicsSystem::Get().SetOrientationToMovementDirection(this, enable, rotationRate);
            Eunoia::CharacterMechanics::CharacterMechanicsSystem::Get().SetOrientationToMovementDirection(body, enable, rotationRate);
        } else {
            Eunoia::CharacterMechanics::CharacterMechanicsSystem::Get().SetBodyRotateToMovementDirection(enable, this, rotationRate);
            Eunoia::CharacterMechanics::CharacterMechanicsSystem::Get().SetOrientationToMovementDirection(this, enable, rotationRate);
        }
    }
    void SetBodyRotateToMovementDirection(GameObject* body, bool enable, float rotationRate = 720.0f) {
        SetBodyRotateToMovementDirection(enable, body, rotationRate);
    }
    bool IsBodyRotateToMovementDirectionEnabled() const {
        return Eunoia::CharacterMechanics::CharacterMechanicsSystem::Get().IsBodyRotateToMovementDirectionEnabled(const_cast<GameObject*>(this));
    }

    // Unified Custom EunoiaUI Framework Accessor
    EunoiaUI::EunoiaUIManager& UI() { return EunoiaUI::EunoiaUIManager::Get(); }

    void Update(float dt);

    // Runtime Lifecycle & Destruction (dev.md & user request: GameObject.Destroy())
    void Destroy();
    static void Destroy(GameObject* obj);
    static void Destroy(GameObject& obj);
    static void Destroy(int objId);
};

// ============================================================================
// EunoiaBehaviour::GetBehaviour / HasBehaviour inline implementations
// (Defined here where GameObject is a complete type)
// ============================================================================
template<typename T>
inline T* EunoiaBehaviour::GetBehaviour() const {
    if (!m_owner) return nullptr;
    return m_owner->template GetBehaviour<T>();
}

template<typename T>
inline bool EunoiaBehaviour::HasBehaviour() const {
    return GetBehaviour<T>() != nullptr;
}

// ============================================================================
// ObjectRef and Hierarchy Inline Implementations
// ============================================================================
inline ObjectRef::ObjectRef(GameObject* obj) : m_ptr(obj), m_id(obj ? obj->id : -1) {}
inline bool ObjectRef::IsValid() const { return m_ptr && m_ptr->id == m_id; }
inline GameObject* ObjectRef::Get() const { return IsValid() ? m_ptr : nullptr; }
inline bool ObjectRef::operator==(const GameObject* other) const {
    if (!other) return !IsValid();
    return IsValid() && m_ptr == other;
}

// ============================================================================
// GameObjectTransform Extended Implementations (Docs/Behaviuor.md)
// ============================================================================
inline void GameObjectTransform::LookAt(const glm::vec3& targetPos, const glm::vec3& up) {
    (void)up;
    glm::vec3 curPos = GetWorldLocation().GetValue();
    glm::vec3 dir = targetPos - curPos;
    float len = glm::length(dir);
    if (len < 1e-5f) return;
    glm::vec3 norm = dir / len;
    float targetYaw = glm::degrees(std::atan2(norm.z, norm.x));
    float targetPitch = glm::degrees(std::asin(glm::clamp(-norm.y, -1.0f, 1.0f)));
    WorldRotation(targetPitch, targetYaw, 0.0f);
}

inline void GameObjectTransform::LookAt(const GameObject* otherObject, const glm::vec3& up) {
    if (otherObject) LookAt(otherObject->position, up);
}

inline void GameObjectTransform::LookAt(const ObjectRef& otherObject, const glm::vec3& up) {
    if (otherObject.IsValid()) LookAt(otherObject.Get(), up);
}

inline void GameObjectTransform::RotateTowards(const glm::vec3& targetRot, float degreesPerSecond, float dt) {
    if (dt <= 0.0f) dt = ::WorldDeltaSeconds > 0.0f ? ::WorldDeltaSeconds : 0.01667f;
    float maxStep = degreesPerSecond * dt;
    glm::vec3 cur = WorldRotation().GetValue();
    float newPitch = Math::MoveTowards(cur.x, targetRot.x, maxStep);
    float newYaw = Math::MoveTowards(cur.y, targetRot.y, maxStep);
    float newRoll = Math::MoveTowards(cur.z, targetRot.z, maxStep);
    WorldRotation(newPitch, newYaw, newRoll);
}

inline void GameObjectTransform::RotateTowards(const TransformVector3& targetRot, float degreesPerSecond, float dt) {
    RotateTowards(targetRot.GetValue(), degreesPerSecond, dt);
}

inline void GameObjectTransform::Rotate(const glm::vec3& axis, float degrees, Space space) {
    (void)space;
    glm::vec3 cur = WorldRotation().GetValue();
    glm::quat qCur = glm::quat(glm::radians(cur));
    glm::quat qDelta = glm::angleAxis(glm::radians(degrees), glm::normalize(axis));
    glm::quat qResult = qDelta * qCur;
    glm::vec3 euler = glm::degrees(glm::eulerAngles(qResult));
    WorldRotation(euler);
}

inline void GameObjectTransform::SetForward(const glm::vec3& direction) {
    float len = glm::length(direction);
    if (len < 1e-5f) return;
    glm::vec3 norm = direction / len;
    float targetYaw = glm::degrees(std::atan2(norm.z, norm.x));
    float targetPitch = glm::degrees(std::asin(glm::clamp(-norm.y, -1.0f, 1.0f)));
    WorldRotation(targetPitch, targetYaw, 0.0f);
}

inline void GameObjectTransform::SetForward(const TransformVector3& direction) {
    SetForward(direction.GetValue());
}

inline glm::quat GameObjectTransform::GetWorldQuaternion() const {
    glm::vec3 cur = WorldRotation().GetValue();
    return glm::quat(glm::radians(cur));
}

inline void GameObjectTransform::SetWorldQuaternion(const glm::quat& q) {
    glm::vec3 euler = glm::degrees(glm::eulerAngles(q));
    WorldRotation(euler);
}

inline void GameObjectTransform::SetWorld(const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale) {
    WorldLocation(pos);
    WorldRotation(rot);
    WorldScale(scale);
}

inline void GameObjectTransform::SetRelative(const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale) {
    RelativeLocation(pos);
    RelativeRotation(rot);
    RelativeScale(scale);
}

inline void GameObjectTransform::Teleport(const glm::vec3& pos, const glm::vec3& rot) {
    WorldLocation(pos);
    WorldRotation(rot);
}

inline GameObject* GameObjectTransform::GetParent() const {
    return gameObject ? gameObject->GetParent() : nullptr;
}

inline glm::mat4 GameObjectTransform::GetLocalToWorld() const {
    if (!gameObject) return glm::mat4(1.0f);
    glm::mat4 T = glm::translate(glm::mat4(1.0f), gameObject->position);
    glm::mat4 R = glm::rotate(glm::mat4(1.0f), glm::radians(gameObject->rotation.y), glm::vec3(0, 1, 0));
    R = glm::rotate(R, glm::radians(gameObject->rotation.x), glm::vec3(1, 0, 0));
    R = glm::rotate(R, glm::radians(gameObject->rotation.z), glm::vec3(0, 0, 1));
    glm::mat4 S = glm::scale(glm::mat4(1.0f), gameObject->scale);
    return T * R * S;
}

inline glm::vec3 GameObjectTransform::TransformPoint(const glm::vec3& localPoint) const {
    return glm::vec3(GetLocalToWorld() * glm::vec4(localPoint, 1.0f));
}

inline glm::vec3 GameObjectTransform::InverseTransformPoint(const glm::vec3& worldPoint) const {
    return glm::vec3(glm::inverse(GetLocalToWorld()) * glm::vec4(worldPoint, 1.0f));
}

inline glm::vec3 GameObjectTransform::TransformDirection(const glm::vec3& localDir) const {
    return glm::vec3(GetLocalToWorld() * glm::vec4(localDir, 0.0f));
}

inline glm::vec3 GameObjectTransform::InverseTransformDirection(const glm::vec3& worldDir) const {
    return glm::vec3(glm::inverse(GetLocalToWorld()) * glm::vec4(worldDir, 0.0f));
}

inline float GameObjectTransform::DistanceTo(const glm::vec3& point) const {
    return glm::distance(GetWorldLocation().GetValue(), point);
}

inline float GameObjectTransform::DistanceTo(const GameObject* otherObject) const {
    return otherObject ? DistanceTo(otherObject->position) : 0.0f;
}

inline float GameObjectTransform::DistanceTo(const ObjectRef& otherObject) const {
    return otherObject.IsValid() ? DistanceTo(otherObject.Get()) : 0.0f;
}

inline glm::vec3 GameObjectTransform::DirectionTo(const glm::vec3& point) const {
    glm::vec3 diff = point - GetWorldLocation().GetValue();
    float len = glm::length(diff);
    return (len > 1e-5f) ? (diff / len) : glm::vec3(0.0f);
}

inline glm::vec3 GameObjectTransform::DirectionTo(const GameObject* otherObject) const {
    return otherObject ? DirectionTo(otherObject->position) : glm::vec3(0.0f);
}

inline glm::vec3 GameObjectTransform::DirectionTo(const ObjectRef& otherObject) const {
    return otherObject.IsValid() ? DirectionTo(otherObject.Get()) : glm::vec3(0.0f);
}

inline bool GameObjectTransform::MoveTowards(const glm::vec3& targetPos, float maxDelta) {
    glm::vec3 cur = GetWorldLocation().GetValue();
    glm::vec3 next = Math::MoveTowards(cur, targetPos, maxDelta);
    WorldLocation(next);
    return glm::distance(next, targetPos) < 1e-4f;
}

inline Tween::TweenInstance& Tween::Move(GameObject* target, const glm::vec3& targetPos, float duration, Ease ease) {
    static uint32_t s_id = 1;
    TweenInstance t;
    t.id = s_id++;
    t.duration = duration;
    t.ease = ease;
    glm::vec3 startPos = target ? target->position : glm::vec3(0.0f);
    t.onUpdate = [target, startPos, targetPos, ease](float progress) {
        if (!target) return;
        float factor = EvaluateEase(ease, progress);
        target->position = startPos + (targetPos - startPos) * factor;
    };
    GetActiveTweens().push_back(std::move(t));
    return GetActiveTweens().back();
}

