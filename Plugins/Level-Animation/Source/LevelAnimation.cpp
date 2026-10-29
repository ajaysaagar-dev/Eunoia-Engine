#include <Level-Animation/LevelAnimation.h>
#include <EngineScene/GameObject.h>
#include <EngineScene/Scene.h>
#include <fstream>
#include <iostream>
#include <cmath>
#include <nlohmann/json.hpp>

namespace Eunoia {

// --- PropertyTrack ---

void PropertyTrack::SortKeys() {
    std::sort(keyframes.begin(), keyframes.end(), [](const AnimationKeyframe& a, const AnimationKeyframe& b) {
        return a.time < b.time;
    });
}

float PropertyTrack::Evaluate(float time) const {
    if (keyframes.empty()) return 0.0f;
    if (keyframes.size() == 1) return keyframes[0].value;
    if (time <= keyframes.front().time) return keyframes.front().value;
    if (time >= keyframes.back().time) return keyframes.back().value;

    // Binary search to find keyframe segment
    auto it = std::upper_bound(keyframes.begin(), keyframes.end(), time,
        [](float t, const AnimationKeyframe& k) {
            return t < k.time;
        });

    if (it == keyframes.end()) return keyframes.back().value;
    if (it == keyframes.begin()) return keyframes.front().value;

    const AnimationKeyframe& k1 = *it;
    const AnimationKeyframe& k0 = *(it - 1);

    float dt = k1.time - k0.time;
    if (dt <= 1e-6f) return k0.value;

    float tNorm = (time - k0.time) / dt;

    if (k0.interp == InterpolationMode::Step) {
        return k0.value;
    }
    else if (k0.interp == InterpolationMode::Cubic) {
        // Catmull-Rom style tangents
        size_t idx = it - keyframes.begin();
        float m0 = 0.0f;
        float m1 = 0.0f;

        if (idx >= 2) {
            float dtPrev = k0.time - keyframes[idx - 2].time;
            if (dtPrev > 1e-6f) {
                m0 = 0.5f * ((k1.value - k0.value) / dt + (k0.value - keyframes[idx - 2].value) / dtPrev);
            } else {
                m0 = (k1.value - k0.value) / dt;
            }
        } else {
            m0 = (k1.value - k0.value) / dt;
        }

        if (idx + 1 < keyframes.size()) {
            float dtNext = keyframes[idx + 1].time - k1.time;
            if (dtNext > 1e-6f) {
                m1 = 0.5f * ((keyframes[idx + 1].value - k1.value) / dtNext + (k1.value - k0.value) / dt);
            } else {
                m1 = (k1.value - k0.value) / dt;
            }
        } else {
            m1 = (k1.value - k0.value) / dt;
        }

        // Hermite basis functions
        float t2 = tNorm * tNorm;
        float t3 = t2 * tNorm;
        float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        float h10 = t3 - 2.0f * t2 + tNorm;
        float h01 = -2.0f * t3 + 3.0f * t2;
        float h11 = t3 - t2;

        return h00 * k0.value + h10 * dt * m0 + h01 * k1.value + h11 * dt * m1;
    }

    // Default: Linear
    return k0.value + (k1.value - k0.value) * tNorm;
}

void PropertyTrack::AddOrUpdateKey(float time, float value, InterpolationMode interp) {
    for (auto& k : keyframes) {
        if (std::abs(k.time - time) <= 0.015f) {
            k.time = time;
            k.value = value;
            k.interp = interp;
            return;
        }
    }
    keyframes.emplace_back(time, value, interp);
    SortKeys();
}

bool PropertyTrack::RemoveKeyAt(size_t index) {
    if (index < keyframes.size()) {
        keyframes.erase(keyframes.begin() + index);
        return true;
    }
    return false;
}

bool PropertyTrack::RemoveKeyNear(float time, float tolerance) {
    for (auto it = keyframes.begin(); it != keyframes.end(); ++it) {
        if (std::abs(it->time - time) <= tolerance) {
            keyframes.erase(it);
            return true;
        }
    }
    return false;
}

// --- AnimatedObjectTrack ---

PropertyTrack* AnimatedObjectTrack::FindTrack(PropertyChannelType type) {
    for (auto& tr : propertyTracks) {
        if (tr.channelType == type) return &tr;
    }
    return nullptr;
}

const PropertyTrack* AnimatedObjectTrack::FindTrack(PropertyChannelType type) const {
    for (const auto& tr : propertyTracks) {
        if (tr.channelType == type) return &tr;
    }
    return nullptr;
}

PropertyTrack& AnimatedObjectTrack::FindOrCreateTrack(PropertyChannelType type, const std::string& name) {
    for (auto& tr : propertyTracks) {
        if (tr.channelType == type) return tr;
    }
    propertyTracks.emplace_back();
    PropertyTrack& tr = propertyTracks.back();
    tr.channelType = type;
    tr.channelName = name.empty() ? ChannelTypeToString(type) : name;
    return tr;
}

void AnimatedObjectTrack::RemoveTrack(PropertyChannelType type) {
    for (auto it = propertyTracks.begin(); it != propertyTracks.end(); ++it) {
        if (it->channelType == type) {
            propertyTracks.erase(it);
            return;
        }
    }
}

void AnimatedObjectTrack::Evaluate(float time, GameObject* target) const {
    if (!target) return;

    for (const auto& tr : propertyTracks) {
        if (tr.keyframes.empty()) continue;
        float val = tr.Evaluate(time);

        switch (tr.channelType) {
            case PropertyChannelType::PositionX: target->position.x = val; break;
            case PropertyChannelType::PositionY: target->position.y = val; break;
            case PropertyChannelType::PositionZ: target->position.z = val; break;
            case PropertyChannelType::RotationX: target->rotation.x = val; break;
            case PropertyChannelType::RotationY: target->rotation.y = val; break;
            case PropertyChannelType::RotationZ: target->rotation.z = val; break;
            case PropertyChannelType::ScaleX:    target->scale.x = val; break;
            case PropertyChannelType::ScaleY:    target->scale.y = val; break;
            case PropertyChannelType::ScaleZ:    target->scale.z = val; break;
            case PropertyChannelType::Visible:   target->visible = (val > 0.5f); break;

            case PropertyChannelType::LightColorR:
                target->light.color.r = val;
                break;
            case PropertyChannelType::LightColorG:
                target->light.color.g = val;
                break;
            case PropertyChannelType::LightColorB:
                target->light.color.b = val;
                break;
            case PropertyChannelType::LightIntensity:
                target->light.intensity = val;
                break;
            case PropertyChannelType::LightRange:
                target->light.range = val;
                break;
            case PropertyChannelType::LightOuterCone:
                target->light.outerConeAngle = val;
                break;
            case PropertyChannelType::LightInnerCone:
                target->light.innerConeAngle = val;
                break;

            case PropertyChannelType::MaterialColorR:
                target->color.r = val;
                break;
            case PropertyChannelType::MaterialColorG:
                target->color.g = val;
                break;
            case PropertyChannelType::MaterialColorB:
                target->color.b = val;
                break;
            case PropertyChannelType::MaterialRoughness:
                target->roughness = val;
                break;
            case PropertyChannelType::MaterialMetallic:
                target->metallic = val;
                break;
            case PropertyChannelType::MaterialOpacity:
                target->opacity = val;
                break;

            case PropertyChannelType::CameraFOV:
                target->camera.fov = val;
                break;

            default:
                break;
        }
    }
}

// --- LevelAnimation ---

AnimatedObjectTrack* LevelAnimation::AddObject(int objectId, const std::string& objectName) {
    for (auto& ot : objectTracks) {
        if (ot.targetObjectId == objectId) return &ot;
    }
    objectTracks.emplace_back();
    AnimatedObjectTrack& ot = objectTracks.back();
    ot.targetObjectId = objectId;
    ot.targetObjectName = objectName;
    ot.isExpanded = true;
    return &ot;
}

void LevelAnimation::RemoveObject(int objectId) {
    for (auto it = objectTracks.begin(); it != objectTracks.end(); ++it) {
        if (it->targetObjectId == objectId) {
            objectTracks.erase(it);
            return;
        }
    }
}

AnimatedObjectTrack* LevelAnimation::FindObjectTrack(int objectId) {
    for (auto& ot : objectTracks) {
        if (ot.targetObjectId == objectId) return &ot;
    }
    return nullptr;
}

const AnimatedObjectTrack* LevelAnimation::FindObjectTrack(int objectId) const {
    for (const auto& ot : objectTracks) {
        if (ot.targetObjectId == objectId) return &ot;
    }
    return nullptr;
}

void LevelAnimation::Evaluate(float time, Scene& scene) const {
    float evalTime = time;
    if (loop && duration > 0.001f) {
        evalTime = std::fmod(evalTime, duration);
        if (evalTime < 0.0f) evalTime += duration;
    } else {
        evalTime = std::clamp(evalTime, 0.0f, duration);
    }

    bool hasLightChanged = false;
    for (const auto& ot : objectTracks) {
        GameObject* target = scene.FindObject(ot.targetObjectId);
        if (!target && !ot.targetObjectName.empty()) {
            // Fallback search by object name
            for (auto& obj : scene.objects) {
                if (obj.name == ot.targetObjectName) {
                    target = &obj;
                    break;
                }
            }
        }
        if (target) {
            ot.Evaluate(evalTime, target);
            if (target->isLight || IsLightPrimitive(target->type)) {
                hasLightChanged = true;
            }
        }
    }

    if (hasLightChanged) {
        scene.SyncLightPositionsFromActors();
    }
}

bool LevelAnimation::SaveToFile(const std::string& filePath) {
    using json = nlohmann::json;
    json j;
    j["name"] = name;
    j["startTime"] = startTime;
    j["duration"] = duration;
    j["loop"] = loop;

    json jTracks = json::array();
    for (const auto& ot : objectTracks) {
        json jObj;
        jObj["targetObjectId"] = ot.targetObjectId;
        jObj["targetObjectName"] = ot.targetObjectName;
        jObj["isExpanded"] = ot.isExpanded;

        json jPropTracks = json::array();
        for (const auto& pt : ot.propertyTracks) {
            json jPt;
            jPt["channelType"] = static_cast<int>(pt.channelType);
            jPt["channelName"] = pt.channelName;

            json jKeys = json::array();
            for (const auto& k : pt.keyframes) {
                json jK;
                jK["time"] = k.time;
                jK["value"] = k.value;
                jK["interp"] = static_cast<int>(k.interp);
                jKeys.push_back(jK);
            }
            jPt["keyframes"] = jKeys;
            jPropTracks.push_back(jPt);
        }
        jObj["propertyTracks"] = jPropTracks;
        jTracks.push_back(jObj);
    }
    j["objectTracks"] = jTracks;

    std::ofstream file(filePath);
    if (!file.is_open()) return false;
    file << j.dump(4);
    assetPath = filePath;
    return true;
}

bool LevelAnimation::LoadFromFile(const std::string& filePath) {
    using json = nlohmann::json;
    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    try {
        json j;
        file >> j;

        if (j.contains("name") && j["name"].is_string()) name = j["name"].get<std::string>();
        if (j.contains("startTime") && j["startTime"].is_number()) startTime = j["startTime"].get<float>();
        if (j.contains("duration") && j["duration"].is_number()) duration = j["duration"].get<float>();
        if (j.contains("loop") && j["loop"].is_boolean()) loop = j["loop"].get<bool>();

        objectTracks.clear();
        if (j.contains("objectTracks") && j["objectTracks"].is_array()) {
            for (const auto& jObj : j["objectTracks"]) {
                AnimatedObjectTrack ot;
                if (jObj.contains("targetObjectId") && jObj["targetObjectId"].is_number_integer())
                    ot.targetObjectId = jObj["targetObjectId"].get<int>();
                if (jObj.contains("targetObjectName") && jObj["targetObjectName"].is_string())
                    ot.targetObjectName = jObj["targetObjectName"].get<std::string>();
                if (jObj.contains("isExpanded") && jObj["isExpanded"].is_boolean())
                    ot.isExpanded = jObj["isExpanded"].get<bool>();

                if (jObj.contains("propertyTracks") && jObj["propertyTracks"].is_array()) {
                    for (const auto& jPt : jObj["propertyTracks"]) {
                        PropertyTrack pt;
                        if (jPt.contains("channelType") && jPt["channelType"].is_number_integer())
                            pt.channelType = static_cast<PropertyChannelType>(jPt["channelType"].get<int>());
                        if (jPt.contains("channelName") && jPt["channelName"].is_string())
                            pt.channelName = jPt["channelName"].get<std::string>();

                        if (jPt.contains("keyframes") && jPt["keyframes"].is_array()) {
                            for (const auto& jK : jPt["keyframes"]) {
                                AnimationKeyframe k;
                                if (jK.contains("time") && jK["time"].is_number())
                                    k.time = jK["time"].get<float>();
                                if (jK.contains("value") && jK["value"].is_number())
                                    k.value = jK["value"].get<float>();
                                if (jK.contains("interp") && jK["interp"].is_number_integer())
                                    k.interp = static_cast<InterpolationMode>(jK["interp"].get<int>());
                                pt.keyframes.push_back(k);
                            }
                        }
                        pt.SortKeys();
                        ot.propertyTracks.push_back(pt);
                    }
                }
                objectTracks.push_back(ot);
            }
        }
        assetPath = filePath;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace Eunoia
