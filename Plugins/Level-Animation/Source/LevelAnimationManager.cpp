#include <Level-Animation/LevelAnimationManager.h>
#include <EngineScene/GameObject.h>
#include <EngineScene/Scene.h>
#include <filesystem>
#include <iostream>

namespace Eunoia {

LevelAnimationManager& LevelAnimationManager::Get() {
    static LevelAnimationManager s_instance;
    return s_instance;
}

std::shared_ptr<LevelAnimation> LevelAnimationManager::LoadLevelAnimation(const std::string& filePath) {
    if (filePath.empty()) return nullptr;

    auto it = m_loadedAnimations.find(filePath);
    if (it != m_loadedAnimations.end() && it->second) {
        return it->second;
    }

    auto anim = std::make_shared<LevelAnimation>();
    if (anim->LoadFromFile(filePath)) {
        m_loadedAnimations[filePath] = anim;
        return anim;
    }

    return nullptr;
}

std::string LevelAnimationManager::CreateNewLevelAnimation(const std::string& folderPath, const std::string& name) {
    std::filesystem::path dir(folderPath);
    std::string filename = name;
    if (filename.size() < 7 || filename.substr(filename.size() - 7) != ".elanim") {
        filename += ".elanim";
    }
    std::filesystem::path fullPath = dir / filename;

    auto anim = std::make_shared<LevelAnimation>();
    anim->name = name;
    anim->startTime = 0.0f;
    anim->duration = 5.0f;
    anim->loop = true;
    anim->SaveToFile(fullPath.string());

    m_loadedAnimations[fullPath.string()] = anim;
    m_activeAnimation = anim;
    return fullPath.string();
}

void LevelAnimationManager::RecordProperty(Scene& scene, int objectId, PropertyChannelType channel, float value) {
    if (!m_isRecording || !m_activeAnimation) return;

    AnimatedObjectTrack* ot = m_activeAnimation->FindObjectTrack(objectId);
    if (!ot) return; // Only record properties on objects that have been added to the animation

    PropertyTrack& pt = ot->FindOrCreateTrack(channel);
    pt.AddOrUpdateKey(m_currentPlayheadTime, value, InterpolationMode::Linear);

    if (!m_activeAnimation->assetPath.empty()) {
        m_activeAnimation->SaveToFile(m_activeAnimation->assetPath);
    }
}

void LevelAnimationManager::RecordTransform(Scene& scene, GameObject& obj) {
    if (!m_isRecording || !m_activeAnimation) return;

    AnimatedObjectTrack* ot = m_activeAnimation->FindObjectTrack(obj.id);
    if (!ot) return;

    ot->FindOrCreateTrack(PropertyChannelType::PositionX).AddOrUpdateKey(m_currentPlayheadTime, obj.position.x);
    ot->FindOrCreateTrack(PropertyChannelType::PositionY).AddOrUpdateKey(m_currentPlayheadTime, obj.position.y);
    ot->FindOrCreateTrack(PropertyChannelType::PositionZ).AddOrUpdateKey(m_currentPlayheadTime, obj.position.z);

    ot->FindOrCreateTrack(PropertyChannelType::RotationX).AddOrUpdateKey(m_currentPlayheadTime, obj.rotation.x);
    ot->FindOrCreateTrack(PropertyChannelType::RotationY).AddOrUpdateKey(m_currentPlayheadTime, obj.rotation.y);
    ot->FindOrCreateTrack(PropertyChannelType::RotationZ).AddOrUpdateKey(m_currentPlayheadTime, obj.rotation.z);

    ot->FindOrCreateTrack(PropertyChannelType::ScaleX).AddOrUpdateKey(m_currentPlayheadTime, obj.scale.x);
    ot->FindOrCreateTrack(PropertyChannelType::ScaleY).AddOrUpdateKey(m_currentPlayheadTime, obj.scale.y);
    ot->FindOrCreateTrack(PropertyChannelType::ScaleZ).AddOrUpdateKey(m_currentPlayheadTime, obj.scale.z);

    if (!m_activeAnimation->assetPath.empty()) {
        m_activeAnimation->SaveToFile(m_activeAnimation->assetPath);
    }
}

void LevelAnimationManager::RecordLight(Scene& scene, GameObject& obj) {
    if (!m_isRecording || !m_activeAnimation) return;

    AnimatedObjectTrack* ot = m_activeAnimation->FindObjectTrack(obj.id);
    if (!ot) return;

    ot->FindOrCreateTrack(PropertyChannelType::LightColorR).AddOrUpdateKey(m_currentPlayheadTime, obj.light.color.r);
    ot->FindOrCreateTrack(PropertyChannelType::LightColorG).AddOrUpdateKey(m_currentPlayheadTime, obj.light.color.g);
    ot->FindOrCreateTrack(PropertyChannelType::LightColorB).AddOrUpdateKey(m_currentPlayheadTime, obj.light.color.b);
    ot->FindOrCreateTrack(PropertyChannelType::LightIntensity).AddOrUpdateKey(m_currentPlayheadTime, obj.light.intensity);
    ot->FindOrCreateTrack(PropertyChannelType::LightRange).AddOrUpdateKey(m_currentPlayheadTime, obj.light.range);
    ot->FindOrCreateTrack(PropertyChannelType::LightOuterCone).AddOrUpdateKey(m_currentPlayheadTime, obj.light.outerConeAngle);
    ot->FindOrCreateTrack(PropertyChannelType::LightInnerCone).AddOrUpdateKey(m_currentPlayheadTime, obj.light.innerConeAngle);

    if (!m_activeAnimation->assetPath.empty()) {
        m_activeAnimation->SaveToFile(m_activeAnimation->assetPath);
    }
}

void LevelAnimationManager::RecordCamera(Scene& scene, GameObject& obj) {
    if (!m_isRecording || !m_activeAnimation) return;

    AnimatedObjectTrack* ot = m_activeAnimation->FindObjectTrack(obj.id);
    if (!ot) return;

    ot->FindOrCreateTrack(PropertyChannelType::CameraFOV).AddOrUpdateKey(m_currentPlayheadTime, obj.camera.fov);

    if (!m_activeAnimation->assetPath.empty()) {
        m_activeAnimation->SaveToFile(m_activeAnimation->assetPath);
    }
}

void LevelAnimationManager::RecordMaterial(Scene& scene, GameObject& obj) {
    if (!m_isRecording || !m_activeAnimation) return;

    AnimatedObjectTrack* ot = m_activeAnimation->FindObjectTrack(obj.id);
    if (!ot) return;

    ot->FindOrCreateTrack(PropertyChannelType::MaterialColorR).AddOrUpdateKey(m_currentPlayheadTime, obj.color.r);
    ot->FindOrCreateTrack(PropertyChannelType::MaterialColorG).AddOrUpdateKey(m_currentPlayheadTime, obj.color.g);
    ot->FindOrCreateTrack(PropertyChannelType::MaterialColorB).AddOrUpdateKey(m_currentPlayheadTime, obj.color.b);
    ot->FindOrCreateTrack(PropertyChannelType::MaterialRoughness).AddOrUpdateKey(m_currentPlayheadTime, obj.roughness);
    ot->FindOrCreateTrack(PropertyChannelType::MaterialMetallic).AddOrUpdateKey(m_currentPlayheadTime, obj.metallic);
    ot->FindOrCreateTrack(PropertyChannelType::MaterialOpacity).AddOrUpdateKey(m_currentPlayheadTime, obj.opacity);

    if (!m_activeAnimation->assetPath.empty()) {
        m_activeAnimation->SaveToFile(m_activeAnimation->assetPath);
    }
}

} // namespace Eunoia
