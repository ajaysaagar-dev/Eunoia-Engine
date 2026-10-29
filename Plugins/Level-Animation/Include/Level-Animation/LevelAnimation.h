#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <cmath>

struct GameObject;
class Scene;

namespace Eunoia {

enum class PropertyChannelType {
    PositionX = 0,
    PositionY = 1,
    PositionZ = 2,
    RotationX = 3,
    RotationY = 4,
    RotationZ = 5,
    ScaleX = 6,
    ScaleY = 7,
    ScaleZ = 8,
    Visible = 9,
    LightColorR = 10,
    LightColorG = 11,
    LightColorB = 12,
    LightIntensity = 13,
    LightRange = 14,
    LightOuterCone = 15,
    LightInnerCone = 16,
    MaterialColorR = 17,
    MaterialColorG = 18,
    MaterialColorB = 19,
    MaterialRoughness = 20,
    MaterialMetallic = 21,
    MaterialOpacity = 22,
    CameraFOV = 23,
    Count
};

inline const char* ChannelTypeToString(PropertyChannelType type) {
    switch (type) {
        case PropertyChannelType::PositionX: return "Location.X";
        case PropertyChannelType::PositionY: return "Location.Y";
        case PropertyChannelType::PositionZ: return "Location.Z";
        case PropertyChannelType::RotationX: return "Rotation.X";
        case PropertyChannelType::RotationY: return "Rotation.Y";
        case PropertyChannelType::RotationZ: return "Rotation.Z";
        case PropertyChannelType::ScaleX: return "Scale.X";
        case PropertyChannelType::ScaleY: return "Scale.Y";
        case PropertyChannelType::ScaleZ: return "Scale.Z";
        case PropertyChannelType::Visible: return "Visibility";
        case PropertyChannelType::LightColorR: return "Light.Color.R";
        case PropertyChannelType::LightColorG: return "Light.Color.G";
        case PropertyChannelType::LightColorB: return "Light.Color.B";
        case PropertyChannelType::LightIntensity: return "Light.Intensity";
        case PropertyChannelType::LightRange: return "Light.Range";
        case PropertyChannelType::LightOuterCone: return "Light.OuterCone";
        case PropertyChannelType::LightInnerCone: return "Light.InnerCone";
        case PropertyChannelType::MaterialColorR: return "Material.Color.R";
        case PropertyChannelType::MaterialColorG: return "Material.Color.G";
        case PropertyChannelType::MaterialColorB: return "Material.Color.B";
        case PropertyChannelType::MaterialRoughness: return "Material.Roughness";
        case PropertyChannelType::MaterialMetallic: return "Material.Metallic";
        case PropertyChannelType::MaterialOpacity: return "Material.Opacity";
        case PropertyChannelType::CameraFOV: return "Camera.FOV";
        default: return "Unknown";
    }
}

inline PropertyChannelType StringToChannelType(const std::string& str) {
    if (str == "Location.X" || str == "PositionX") return PropertyChannelType::PositionX;
    if (str == "Location.Y" || str == "PositionY") return PropertyChannelType::PositionY;
    if (str == "Location.Z" || str == "PositionZ") return PropertyChannelType::PositionZ;
    if (str == "Rotation.X" || str == "RotationX") return PropertyChannelType::RotationX;
    if (str == "Rotation.Y" || str == "RotationY") return PropertyChannelType::RotationY;
    if (str == "Rotation.Z" || str == "RotationZ") return PropertyChannelType::RotationZ;
    if (str == "Scale.X" || str == "ScaleX") return PropertyChannelType::ScaleX;
    if (str == "Scale.Y" || str == "ScaleY") return PropertyChannelType::ScaleY;
    if (str == "Scale.Z" || str == "ScaleZ") return PropertyChannelType::ScaleZ;
    if (str == "Visibility" || str == "Visible") return PropertyChannelType::Visible;
    if (str == "Light.Color.R" || str == "LightColorR") return PropertyChannelType::LightColorR;
    if (str == "Light.Color.G" || str == "LightColorG") return PropertyChannelType::LightColorG;
    if (str == "Light.Color.B" || str == "LightColorB") return PropertyChannelType::LightColorB;
    if (str == "Light.Intensity" || str == "LightIntensity") return PropertyChannelType::LightIntensity;
    if (str == "Light.Range" || str == "LightRange") return PropertyChannelType::LightRange;
    if (str == "Light.OuterCone" || str == "LightOuterCone") return PropertyChannelType::LightOuterCone;
    if (str == "Light.InnerCone" || str == "LightInnerCone") return PropertyChannelType::LightInnerCone;
    if (str == "Material.Color.R" || str == "MaterialColorR") return PropertyChannelType::MaterialColorR;
    if (str == "Material.Color.G" || str == "MaterialColorG") return PropertyChannelType::MaterialColorG;
    if (str == "Material.Color.B" || str == "MaterialColorB") return PropertyChannelType::MaterialColorB;
    if (str == "Material.Roughness" || str == "MaterialRoughness") return PropertyChannelType::MaterialRoughness;
    if (str == "Material.Metallic" || str == "MaterialMetallic") return PropertyChannelType::MaterialMetallic;
    if (str == "Material.Opacity" || str == "MaterialOpacity") return PropertyChannelType::MaterialOpacity;
    if (str == "Camera.FOV" || str == "CameraFOV") return PropertyChannelType::CameraFOV;
    return PropertyChannelType::PositionX;
}

enum class InterpolationMode {
    Linear = 0,
    Step = 1,
    Cubic = 2
};

struct AnimationKeyframe {
    float time = 0.0f;
    float value = 0.0f;
    InterpolationMode interp = InterpolationMode::Linear;

    AnimationKeyframe() = default;
    AnimationKeyframe(float t, float v, InterpolationMode m = InterpolationMode::Linear)
        : time(t), value(v), interp(m) {}
};

struct PropertyTrack {
    PropertyChannelType channelType = PropertyChannelType::PositionX;
    std::string channelName = "Location.X";
    std::vector<AnimationKeyframe> keyframes;

    float Evaluate(float time) const;
    void AddOrUpdateKey(float time, float value, InterpolationMode interp = InterpolationMode::Linear);
    bool RemoveKeyAt(size_t index);
    bool RemoveKeyNear(float time, float tolerance = 0.02f);
    void SortKeys();
};

struct AnimatedObjectTrack {
    int targetObjectId = -1;
    std::string targetObjectName = "Actor";
    bool isExpanded = true;
    std::vector<PropertyTrack> propertyTracks;

    PropertyTrack* FindTrack(PropertyChannelType type);
    const PropertyTrack* FindTrack(PropertyChannelType type) const;
    PropertyTrack& FindOrCreateTrack(PropertyChannelType type, const std::string& name = "");
    void RemoveTrack(PropertyChannelType type);
    void Evaluate(float time, GameObject* target) const;
};

class LevelAnimation {
public:
    std::string name = "NewLevelAnimation";
    std::string assetPath = "";
    float startTime = 0.0f;
    float duration = 5.0f; // customizable end time
    bool loop = true;
    std::vector<AnimatedObjectTrack> objectTracks;

    LevelAnimation() = default;

    AnimatedObjectTrack* AddObject(int objectId, const std::string& objectName);
    void RemoveObject(int objectId);
    AnimatedObjectTrack* FindObjectTrack(int objectId);
    const AnimatedObjectTrack* FindObjectTrack(int objectId) const;

    void Evaluate(float time, Scene& scene) const;

    bool SaveToFile(const std::string& filePath);
    bool LoadFromFile(const std::string& filePath);
};

} // namespace Eunoia
