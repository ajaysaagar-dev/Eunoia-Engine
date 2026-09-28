#include <EngineScene/EunoiaBehaviour.h>
#include <EngineScene/BehaviourRegistry.h>
#include <EngineScene/GameObject.h>
#include <EngineScene/Scene.h>
#include <EnginePlatform/InputSystem.h>
#include <EngineRenderer/Camera.h>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>

// ============================================================================
// RespectiveObjectAccessor Implementations
// ============================================================================

LightComponent* RespectiveObjectAccessor::Light(void* objRef) const {
    if (!objRef) return nullptr;
    return reinterpret_cast<LightComponent*>(objRef);
}

PrimitiveMesh* RespectiveObjectAccessor::Mesh(void* objRef) const {
    if (!objRef) return nullptr;
    return reinterpret_cast<PrimitiveMesh*>(objRef);
}

GameObject* RespectiveObjectAccessor::Entity(void* objRef) const {
    if (!objRef) return nullptr;
    return reinterpret_cast<GameObject*>(objRef);
}

GameObject* RespectiveObjectAccessor::Actor(void* objRef) const {
    return Entity(objRef);
}

GameObject* RespectiveObjectAccessor::Shape(void* objRef) const {
    return Entity(objRef);
}

GameObject* RespectiveObjectAccessor::Transform(void* objRef) const {
    return Entity(objRef);
}

GameObject* RespectiveObjectAccessor::Camera(void* objRef) const {
    if (!objRef) return nullptr;
    return reinterpret_cast<GameObject*>(objRef);
}

GameObject* RespectiveObjectAccessor::CameraActor(void* objRef) const {
    if (!objRef) return nullptr;
    return reinterpret_cast<GameObject*>(objRef);
}

// ============================================================================
// BehaviourTransformProperty Implementations
// ============================================================================

glm::vec3 BehaviourTransformProperty::LocalSpace() const {
    if (!behaviour) return glm::vec3(0.0f);
    GameObject* owner = behaviour->GetOwner();
    if (!owner) return glm::vec3(0.0f);
    switch (type) {
        case Type::Location: return owner->position;
        case Type::Rotation: return owner->rotation;
        case Type::Scale:    return owner->scale;
    }
    return glm::vec3(0.0f);
}

void BehaviourTransformProperty::LocalSpace(const glm::vec3& v) {
    if (!behaviour) return;
    GameObject* owner = behaviour->GetOwner();
    if (!owner) return;
    switch (type) {
        case Type::Location: owner->position = v; break;
        case Type::Rotation: owner->rotation = v; break;
        case Type::Scale:    owner->scale = v; break;
    }
}

glm::vec3 BehaviourTransformProperty::WorldSpace() const {
    if (!behaviour) return glm::vec3(0.0f);
    GameObject* owner = behaviour->GetOwner();
    if (!owner) return glm::vec3(0.0f);
    Scene* scene = behaviour->GetScene();
    if (!scene || owner->parentId == -1) {
        return LocalSpace();
    }
    glm::mat4 worldMat = scene->GetWorldMatrix(*owner);
    switch (type) {
        case Type::Location:
            return glm::vec3(worldMat[3]);
        case Type::Rotation: {
            glm::vec3 c0(worldMat[0]), c1(worldMat[1]), c2(worldMat[2]);
            float sx = glm::length(c0);
            float sy = glm::length(c1);
            float sz = glm::length(c2);
            if (sx > 1e-6f && sy > 1e-6f && sz > 1e-6f) {
                glm::mat3 rotMat(c0 / sx, c1 / sy, c2 / sz);
                float pitch = glm::degrees(std::asin(glm::clamp(-rotMat[1][2], -1.0f, 1.0f)));
                float yaw = glm::degrees(std::atan2(rotMat[0][2], rotMat[2][2]));
                float roll = glm::degrees(std::atan2(rotMat[1][0], rotMat[1][1]));
                return glm::vec3(pitch, yaw, roll);
            }
            return owner->rotation;
        }
        case Type::Scale: {
            return glm::vec3(glm::length(glm::vec3(worldMat[0])),
                             glm::length(glm::vec3(worldMat[1])),
                             glm::length(glm::vec3(worldMat[2])));
        }
    }
    return LocalSpace();
}

void BehaviourTransformProperty::WorldSpace(const glm::vec3& v) {
    if (!behaviour) return;
    GameObject* owner = behaviour->GetOwner();
    if (!owner) return;
    Scene* scene = behaviour->GetScene();
    if (!scene || owner->parentId == -1) {
        LocalSpace(v);
        return;
    }
    const GameObject* parent = scene->FindObjectConst(owner->parentId);
    if (!parent) {
        LocalSpace(v);
        return;
    }
    glm::mat4 pMat = scene->GetWorldMatrix(*parent);
    glm::mat4 invP = glm::inverse(pMat);
    switch (type) {
        case Type::Location: {
            owner->position = glm::vec3(invP * glm::vec4(v, 1.0f));
            break;
        }
        case Type::Rotation: {
            owner->rotation = v - parent->rotation;
            break;
        }
        case Type::Scale: {
            glm::vec3 pScale(glm::length(glm::vec3(pMat[0])),
                             glm::length(glm::vec3(pMat[1])),
                             glm::length(glm::vec3(pMat[2])));
            if (pScale.x > 1e-6f && pScale.y > 1e-6f && pScale.z > 1e-6f) {
                owner->scale = v / pScale;
            } else {
                owner->scale = v;
            }
            break;
        }
    }
}

// ============================================================================
// GameObjectTransform Implementations (dev.md)
// ============================================================================

static Scene* GetSceneForGameObject(const GameObject* go) {
    if (!go) return nullptr;
    if (go->scene) return go->scene;
    for (const auto& b : go->behaviours) {
        if (b && b->GetScene()) return b->GetScene();
    }
    return nullptr;
}

glm::vec3 GameObjectTransform::GetRelativeLocationInternal() const {
    if (!gameObject) return glm::vec3(0.0f);
    return gameObject->position;
}

void GameObjectTransform::SetRelativeLocationInternal(const glm::vec3& v) {
    if (!gameObject) return;
    gameObject->position = v;
}

glm::vec3 GameObjectTransform::GetRelativeRotationInternal() const {
    if (!gameObject) return glm::vec3(0.0f);
    return gameObject->rotation;
}

void GameObjectTransform::SetRelativeRotationInternal(const glm::vec3& v) {
    if (!gameObject) return;
    gameObject->rotation = v;
}

glm::vec3 GameObjectTransform::GetRelativeScaleInternal() const {
    if (!gameObject) return glm::vec3(1.0f);
    return gameObject->scale;
}

void GameObjectTransform::SetRelativeScaleInternal(const glm::vec3& v) {
    if (!gameObject) return;
    gameObject->scale = v;
}

glm::vec3 GameObjectTransform::GetWorldLocationInternal() const {
    if (!gameObject) return glm::vec3(0.0f);
    Scene* scene = GetSceneForGameObject(gameObject);
    if (!scene || gameObject->parentId == -1) {
        return gameObject->position;
    }
    return scene->GetWorldPosition(*gameObject);
}

void GameObjectTransform::SetWorldLocationInternal(const glm::vec3& v) {
    if (!gameObject) return;
    Scene* scene = GetSceneForGameObject(gameObject);
    if (!scene || gameObject->parentId == -1) {
        gameObject->position = v;
        return;
    }
    const GameObject* parent = scene->FindObjectConst(gameObject->parentId);
    if (!parent) {
        gameObject->position = v;
        return;
    }
    glm::mat4 pMat = scene->GetWorldMatrix(*parent);
    glm::mat4 invP = glm::inverse(pMat);
    gameObject->position = glm::vec3(invP * glm::vec4(v, 1.0f));
}

glm::vec3 GameObjectTransform::GetWorldRotationInternal() const {
    if (!gameObject) return glm::vec3(0.0f);
    Scene* scene = GetSceneForGameObject(gameObject);
    if (!scene || gameObject->parentId == -1) {
        return gameObject->rotation;
    }
    glm::mat4 worldMat = scene->GetWorldMatrix(*gameObject);
    glm::vec3 c0(worldMat[0]), c1(worldMat[1]), c2(worldMat[2]);
    float sx = glm::length(c0);
    float sy = glm::length(c1);
    float sz = glm::length(c2);
    if (sx > 1e-6f && sy > 1e-6f && sz > 1e-6f) {
        glm::mat3 rotMat(c0 / sx, c1 / sy, c2 / sz);
        float pitch = glm::degrees(std::asin(glm::clamp(-rotMat[1][2], -1.0f, 1.0f)));
        float yaw = glm::degrees(std::atan2(rotMat[0][2], rotMat[2][2]));
        float roll = glm::degrees(std::atan2(rotMat[1][0], rotMat[1][1]));
        return glm::vec3(pitch, yaw, roll);
    }
    return gameObject->rotation;
}

void GameObjectTransform::SetWorldRotationInternal(const glm::vec3& v) {
    if (!gameObject) return;
    Scene* scene = GetSceneForGameObject(gameObject);
    if (!scene || gameObject->parentId == -1) {
        gameObject->rotation = v;
        return;
    }
    const GameObject* parent = scene->FindObjectConst(gameObject->parentId);
    if (!parent) {
        gameObject->rotation = v;
        return;
    }
    gameObject->rotation = v - parent->rotation;
}

glm::vec3 GameObjectTransform::GetWorldScaleInternal() const {
    if (!gameObject) return glm::vec3(1.0f);
    Scene* scene = GetSceneForGameObject(gameObject);
    if (!scene || gameObject->parentId == -1) {
        return gameObject->scale;
    }
    glm::mat4 worldMat = scene->GetWorldMatrix(*gameObject);
    return glm::vec3(glm::length(glm::vec3(worldMat[0])),
                     glm::length(glm::vec3(worldMat[1])),
                     glm::length(glm::vec3(worldMat[2])));
}

void GameObjectTransform::SetWorldScaleInternal(const glm::vec3& v) {
    if (!gameObject) return;
    Scene* scene = GetSceneForGameObject(gameObject);
    if (!scene || gameObject->parentId == -1) {
        gameObject->scale = v;
        return;
    }
    const GameObject* parent = scene->FindObjectConst(gameObject->parentId);
    if (!parent) {
        gameObject->scale = v;
        return;
    }
    glm::mat4 pMat = scene->GetWorldMatrix(*parent);
    glm::vec3 pScale(glm::length(glm::vec3(pMat[0])),
                     glm::length(glm::vec3(pMat[1])),
                     glm::length(glm::vec3(pMat[2])));
    if (pScale.x > 1e-6f && pScale.y > 1e-6f && pScale.z > 1e-6f) {
        gameObject->scale = v / pScale;
    } else {
        gameObject->scale = v;
    }
}

// GameObjectTransform::AxisProxy implementations
glm::vec3 GameObjectTransform::AxisProxy::GetValue() const {
    if (!parent) return glm::vec3(0.0f);
    switch (target) {
        case TargetType::WorldLocation:    return parent->GetWorldLocationInternal();
        case TargetType::RelativeLocation: return parent->GetRelativeLocationInternal();
        case TargetType::WorldRotation:    return parent->GetWorldRotationInternal();
        case TargetType::RelativeRotation: return parent->GetRelativeRotationInternal();
        case TargetType::WorldScale:       return parent->GetWorldScaleInternal();
        case TargetType::RelativeScale:    return parent->GetRelativeScaleInternal();
    }
    return glm::vec3(0.0f);
}

void GameObjectTransform::AxisProxy::SetValue(const glm::vec3& v) {
    if (!parent) return;
    switch (target) {
        case TargetType::WorldLocation:    parent->SetWorldLocationInternal(v); break;
        case TargetType::RelativeLocation: parent->SetRelativeLocationInternal(v); break;
        case TargetType::WorldRotation:    parent->SetWorldRotationInternal(v); break;
        case TargetType::RelativeRotation: parent->SetRelativeRotationInternal(v); break;
        case TargetType::WorldScale:       parent->SetWorldScaleInternal(v); break;
        case TargetType::RelativeScale:    parent->SetRelativeScaleInternal(v); break;
    }
}

void GameObjectTransform::AxisProxy::operator()(float inX, float inY, float inZ) {
    SetValue(glm::vec3(inX, inY, inZ));
}

void GameObjectTransform::AxisProxy::operator()(const glm::vec3& v) {
    SetValue(v);
}

void GameObjectTransform::AxisProxy::operator()(const glm::vec3* v) {
    if (v) {
        SetValue(*v);
        delete v;
    }
}

void GameObjectTransform::AxisProxy::operator()(const TransformVector3& v) {
    SetValue(glm::vec3(v.x, v.y, v.z));
}

void GameObjectTransform::AxisProxy::operator()(const TransformVector3* v) {
    if (v) {
        SetValue(glm::vec3(v->x, v->y, v->z));
        delete v;
    }
}

TransformVector3 GameObjectTransform::AxisProxy::operator()() const {
    return TransformVector3(GetValue());
}

void GameObjectTransform::AxisProxy::X(float val) {
    glm::vec3 v = GetValue();
    v.x = val;
    SetValue(v);
}

void GameObjectTransform::AxisProxy::Y(float val) {
    glm::vec3 v = GetValue();
    v.y = val;
    SetValue(v);
}

void GameObjectTransform::AxisProxy::Z(float val) {
    glm::vec3 v = GetValue();
    v.z = val;
    SetValue(v);
}

TransformState GameObjectTransform::GetWorldTransform() const {
    TransformState state;
    state.Location = TransformVector3(GetWorldLocationInternal());
    state.Rotation = TransformVector3(GetWorldRotationInternal());
    state.Scale    = TransformVector3(GetWorldScaleInternal());
    if (gameObject) {
        Scene* scene = GetSceneForGameObject(gameObject);
        if (scene) {
            state.Matrix = scene->GetWorldMatrix(*gameObject);
        } else {
            state.Matrix = gameObject->GetLocalMatrix();
        }
    }
    return state;
}

TransformState GameObjectTransform::GetRelativeTransform() const {
    TransformState state;
    state.Location = TransformVector3(GetRelativeLocationInternal());
    state.Rotation = TransformVector3(GetRelativeRotationInternal());
    state.Scale    = TransformVector3(GetRelativeScaleInternal());
    if (gameObject) {
        state.Matrix = gameObject->GetLocalMatrix();
    }
    return state;
}

TransformVector3 GameObjectTransform::GetForwardVector() const {
    glm::vec3 rot = GetWorldRotationInternal();
    glm::mat4 rotMat = glm::mat4(1.0f);
    rotMat = glm::rotate(rotMat, glm::radians(rot.y), glm::vec3(0, 1, 0));
    rotMat = glm::rotate(rotMat, glm::radians(rot.x), glm::vec3(1, 0, 0));
    rotMat = glm::rotate(rotMat, glm::radians(rot.z), glm::vec3(0, 0, 1));
    return TransformVector3(glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f))));
}

TransformVector3 GameObjectTransform::GetRightVector() const {
    glm::vec3 rot = GetWorldRotationInternal();
    glm::mat4 rotMat = glm::mat4(1.0f);
    rotMat = glm::rotate(rotMat, glm::radians(rot.y), glm::vec3(0, 1, 0));
    rotMat = glm::rotate(rotMat, glm::radians(rot.x), glm::vec3(1, 0, 0));
    rotMat = glm::rotate(rotMat, glm::radians(rot.z), glm::vec3(0, 0, 1));
    return TransformVector3(glm::normalize(glm::vec3(rotMat * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f))));
}

TransformVector3 GameObjectTransform::GetUpVector() const {
    glm::vec3 rot = GetWorldRotationInternal();
    glm::mat4 rotMat = glm::mat4(1.0f);
    rotMat = glm::rotate(rotMat, glm::radians(rot.y), glm::vec3(0, 1, 0));
    rotMat = glm::rotate(rotMat, glm::radians(rot.x), glm::vec3(1, 0, 0));
    rotMat = glm::rotate(rotMat, glm::radians(rot.z), glm::vec3(0, 0, 1));
    return TransformVector3(glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f))));
}

void GameObjectTransform::WorldTranslate(const glm::vec3& delta) {
    if (!gameObject) return;
    SetWorldLocationInternal(GetWorldLocationInternal() + delta);
}

void GameObjectTransform::WorldTranslate(const glm::vec3& fromPoint, const glm::vec3& toLocation) {
    if (!gameObject) return;
    WorldTranslate(toLocation - fromPoint);
}

void GameObjectTransform::RelativeTranslate(const glm::vec3& delta) {
    if (!gameObject) return;
    glm::vec3 rot = GetRelativeRotationInternal();
    glm::mat4 rotMat = glm::mat4(1.0f);
    rotMat = glm::rotate(rotMat, glm::radians(rot.y), glm::vec3(0, 1, 0));
    rotMat = glm::rotate(rotMat, glm::radians(rot.x), glm::vec3(1, 0, 0));
    rotMat = glm::rotate(rotMat, glm::radians(rot.z), glm::vec3(0, 0, 1));
    glm::vec3 rotatedDelta = glm::vec3(rotMat * glm::vec4(delta, 0.0f));
    SetRelativeLocationInternal(GetRelativeLocationInternal() + rotatedDelta);
}

void GameObjectTransform::RelativeTranslate(const glm::vec3& fromPoint, const glm::vec3& toLocation) {
    if (!gameObject) return;
    RelativeTranslate(toLocation - fromPoint);
}

// BehaviourTransform::AxisProxy implementations
glm::vec3 BehaviourTransform::AxisProxy::GetValue() const {
    if (!parent) return glm::vec3(0.0f);
    switch (target) {
        case TargetType::WorldLocation:    return parent->Location.WorldSpace();
        case TargetType::RelativeLocation: return parent->Location.LocalSpace();
        case TargetType::WorldRotation:    return parent->Rotation.WorldSpace();
        case TargetType::RelativeRotation: return parent->Rotation.LocalSpace();
        case TargetType::WorldScale:       return parent->Scale.WorldSpace();
        case TargetType::RelativeScale:    return parent->Scale.LocalSpace();
    }
    return glm::vec3(0.0f);
}

void BehaviourTransform::AxisProxy::SetValue(const glm::vec3& v) {
    if (!parent) return;
    switch (target) {
        case TargetType::WorldLocation:    parent->Location.WorldSpace(v); break;
        case TargetType::RelativeLocation: parent->Location.LocalSpace(v); break;
        case TargetType::WorldRotation:    parent->Rotation.WorldSpace(v); break;
        case TargetType::RelativeRotation: parent->Rotation.LocalSpace(v); break;
        case TargetType::WorldScale:       parent->Scale.WorldSpace(v); break;
        case TargetType::RelativeScale:    parent->Scale.LocalSpace(v); break;
    }
}

void BehaviourTransform::AxisProxy::operator()(float inX, float inY, float inZ) {
    SetValue(glm::vec3(inX, inY, inZ));
}

void BehaviourTransform::AxisProxy::operator()(const glm::vec3& v) {
    SetValue(v);
}

void BehaviourTransform::AxisProxy::operator()(const glm::vec3* v) {
    if (v) {
        SetValue(*v);
        delete v;
    }
}

void BehaviourTransform::AxisProxy::operator()(const TransformVector3& v) {
    SetValue(glm::vec3(v.x, v.y, v.z));
}

void BehaviourTransform::AxisProxy::operator()(const TransformVector3* v) {
    if (v) {
        SetValue(glm::vec3(v->x, v->y, v->z));
        delete v;
    }
}

TransformVector3 BehaviourTransform::AxisProxy::operator()() const {
    return TransformVector3(GetValue());
}

void BehaviourTransform::AxisProxy::X(float val) {
    glm::vec3 v = GetValue();
    v.x = val;
    SetValue(v);
}

void BehaviourTransform::AxisProxy::Y(float val) {
    glm::vec3 v = GetValue();
    v.y = val;
    SetValue(v);
}

void BehaviourTransform::AxisProxy::Z(float val) {
    glm::vec3 v = GetValue();
    v.z = val;
    SetValue(v);
}

TransformState BehaviourTransform::GetWorldTransform() const {
    TransformState state;
    state.Location = TransformVector3(WorldLocation());
    state.Rotation = TransformVector3(WorldRotation());
    state.Scale    = TransformVector3(WorldScale());
    return state;
}

TransformState BehaviourTransform::GetRelativeTransform() const {
    TransformState state;
    state.Location = TransformVector3(RelativeLocation());
    state.Rotation = TransformVector3(RelativeRotation());
    state.Scale    = TransformVector3(RelativeScale());
    return state;
}

TransformVector3 BehaviourTransform::GetForwardVector() const {
    glm::vec3 rot = WorldRotation();
    glm::mat4 rotMat = glm::mat4(1.0f);
    rotMat = glm::rotate(rotMat, glm::radians(rot.y), glm::vec3(0, 1, 0));
    rotMat = glm::rotate(rotMat, glm::radians(rot.x), glm::vec3(1, 0, 0));
    rotMat = glm::rotate(rotMat, glm::radians(rot.z), glm::vec3(0, 0, 1));
    return TransformVector3(glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f))));
}

TransformVector3 BehaviourTransform::GetRightVector() const {
    glm::vec3 rot = WorldRotation();
    glm::mat4 rotMat = glm::mat4(1.0f);
    rotMat = glm::rotate(rotMat, glm::radians(rot.y), glm::vec3(0, 1, 0));
    rotMat = glm::rotate(rotMat, glm::radians(rot.x), glm::vec3(1, 0, 0));
    rotMat = glm::rotate(rotMat, glm::radians(rot.z), glm::vec3(0, 0, 1));
    return TransformVector3(glm::normalize(glm::vec3(rotMat * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f))));
}

TransformVector3 BehaviourTransform::GetUpVector() const {
    glm::vec3 rot = WorldRotation();
    glm::mat4 rotMat = glm::mat4(1.0f);
    rotMat = glm::rotate(rotMat, glm::radians(rot.y), glm::vec3(0, 1, 0));
    rotMat = glm::rotate(rotMat, glm::radians(rot.x), glm::vec3(1, 0, 0));
    rotMat = glm::rotate(rotMat, glm::radians(rot.z), glm::vec3(0, 0, 1));
    return TransformVector3(glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f))));
}

// ============================================================================
// EunoiaBehaviour Property Registration Implementations
// ============================================================================

void EunoiaBehaviour::RegisterProperty(const std::string& name, bool* val, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Bool;
    prop.dataPtr = val;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterProperty(const std::string& name, int* val, const std::string& category, int min, int max) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Int;
    prop.dataPtr = val;
    if (min != max) {
        prop.hasRange = true;
        prop.minVal = (float)min;
        prop.maxVal = (float)max;
    }
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterProperty(const std::string& name, int64_t* val, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Int64;
    prop.dataPtr = val;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterProperty(const std::string& name, float* val, const std::string& category, float min, float max) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Float;
    prop.dataPtr = val;
    if (min != max) {
        prop.hasRange = true;
        prop.minVal = min;
        prop.maxVal = max;
    }
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterProperty(const std::string& name, double* val, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Double;
    prop.dataPtr = val;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterProperty(const std::string& name, std::string* val, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::String;
    prop.dataPtr = val;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterProperty(const std::string& name, glm::vec2* val, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Vec2;
    prop.dataPtr = val;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterProperty(const std::string& name, glm::vec3* val, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Vec3;
    prop.dataPtr = val;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterProperty(const std::string& name, glm::vec4* val, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Vec4;
    prop.dataPtr = val;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterColor(const std::string& name, glm::vec3* val, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Color3;
    prop.dataPtr = val;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterReference(const std::string& name, void* ptrAddr, ObjectRefType refType, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::ObjectRef;
    prop.refType = refType;
    prop.dataPtr = ptrAddr;
    prop.targetId = -1;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::ResolveReferences(Scene& scene) {
    m_scene = &scene;
    for (auto& prop : m_properties) {
        if (prop.type != BehaviourPropertyType::ObjectRef || !prop.dataPtr) continue;

        if (prop.targetId == -1) {
            *reinterpret_cast<void**>(prop.dataPtr) = nullptr;
            prop.isMissing = false;
            continue;
        }

        GameObject* targetObj = scene.FindObject(prop.targetId);
        if (!targetObj) {
            *reinterpret_cast<void**>(prop.dataPtr) = nullptr;
            prop.isMissing = true;
            continue;
        }

        prop.isMissing = false;
        switch (prop.refType) {
            case ObjectRefType::Actor:
                *reinterpret_cast<GameObject**>(prop.dataPtr) = targetObj;
                break;
            case ObjectRefType::Light:
                *reinterpret_cast<LightComponent**>(prop.dataPtr) = targetObj->isLight ? &targetObj->light : nullptr;
                if (!targetObj->isLight) prop.isMissing = true;
                break;
            case ObjectRefType::Mesh:
                *reinterpret_cast<PrimitiveMesh**>(prop.dataPtr) = &targetObj->mesh;
                break;
            case ObjectRefType::Camera:
                if (targetObj && (targetObj->isCamera || targetObj->type == PrimitiveType::Camera)) {
                    *reinterpret_cast<GameObject**>(prop.dataPtr) = targetObj;
                    prop.isMissing = false;
                } else {
                    *reinterpret_cast<void**>(prop.dataPtr) = nullptr;
                    prop.isMissing = true;
                }
                break;
            default:
                *reinterpret_cast<void**>(prop.dataPtr) = targetObj;
                break;
        }
    }
}

// ============================================================================
// Built-in Behaviour Logic
// ============================================================================

void RotatorBehaviour::Update(float deltaTime) {
    if (!m_owner) return;
    glm::vec3 normAxis = glm::length(rotationAxis) > 0.001f ? glm::normalize(rotationAxis) : glm::vec3(0, 1, 0);
    m_owner->rotation += normAxis * (rotationSpeed * deltaTime);
    if (m_owner->rotation.x >= 360.0f) m_owner->rotation.x -= 360.0f;
    if (m_owner->rotation.y >= 360.0f) m_owner->rotation.y -= 360.0f;
    if (m_owner->rotation.z >= 360.0f) m_owner->rotation.z -= 360.0f;
}

void LightFlickerBehaviour::Update(float deltaTime) {
    m_timeAccum += deltaTime * flickerFrequency;
    float noise = (std::sin(m_timeAccum) + std::sin(m_timeAccum * 2.3f) + std::cos(m_timeAccum * 3.7f)) / 3.0f; // [-1, 1]
    float norm = (noise + 1.0f) * 0.5f; // [0, 1]
    float currentIntensity = minIntensity + norm * (maxIntensity - minIntensity);

    if (targetLight) {
        targetLight->intensity = currentIntensity;
    } else if (m_owner && m_owner->isLight) {
        m_owner->light.intensity = currentIntensity;
    }
}

void DoorController::Start() {
    auto* light = GetRespectiveObject.Light(WarningLight);
    if (light) {
        light->color = Locked ? glm::vec3(1.0f, 0.1f, 0.1f) : glm::vec3(0.1f, 1.0f, 0.1f);
    }
    AddEngineLog("LogBehaviour", "DoorController::Start initialized on " + (m_owner ? m_owner->name : "Unknown"), 0);
}

void DoorController::Update(float deltaTime) {
    if (!m_owner) return;
    float targetAngle = Locked ? 0.0f : 90.0f;
    m_currentAngle += (targetAngle - m_currentAngle) * std::min(1.0f, OpenSpeed * deltaTime);
    m_owner->rotation.y = m_currentAngle;

    auto* light = GetRespectiveObject.Light(WarningLight);
    if (light) {
        light->color = Locked ? glm::vec3(1.0f, 0.1f, 0.1f) : glm::vec3(0.1f, 1.0f, 0.1f);
    }
}

void PlayerController::Start() {
    AddEngineLog("LogBehaviour", "PlayerController::Start — Player \"" + PlayerName + "\" entered level.", 0);
}

void PlayerController::Update(float deltaTime) {
    if (!m_owner) return;
    auto& input = InputSystem::Get();

    glm::vec3 moveDir(0.0f);
    if (input.IsKeyDown(Key::W) || input.IsKeyDown(Key::Up))    moveDir.z -= 1.0f;
    if (input.IsKeyDown(Key::S) || input.IsKeyDown(Key::Down))  moveDir.z += 1.0f;
    if (input.IsKeyDown(Key::A) || input.IsKeyDown(Key::Left))  moveDir.x -= 1.0f;
    if (input.IsKeyDown(Key::D) || input.IsKeyDown(Key::Right)) moveDir.x += 1.0f;

    if (glm::length(moveDir) > 0.001f) {
        moveDir = glm::normalize(moveDir);
        m_owner->position += moveDir * (MoveSpeed * deltaTime);
    }
}

void EnemyController::Start() {
    AddEngineLog("LogBehaviour", "EnemyController::Start — " + EnemyName + " searching for target.", 0);
}

void EnemyController::Update(float deltaTime) {
    if (!m_owner) return;
    GameObject* target = GetRespectiveObject.Actor(TargetActor);
    if (target) {
        glm::vec3 dir = target->position - m_owner->position;
        float dist = glm::length(dir);
        if (dist > 1.5f) {
            dir = glm::normalize(dir);
            m_owner->position += dir * (MoveSpeed * deltaTime);
            float targetYaw = glm::degrees(std::atan2(dir.x, -dir.z));
            m_owner->rotation.y = targetYaw;
        }
    }
}

void DynamicScriptBehaviour::Start() {
    for (const auto& msg : m_startPrintMessages) {
        Print(msg);
        AddEngineLog("LogBehaviour", "[" + m_className + "] " + msg, 0);
    }
}

void DynamicScriptBehaviour::Update(float deltaTime) {
    if (!m_owner || !IsEnabled()) return;

    // Check if script has an 'enabled' or 'active' boolean flag in storage
    bool isScriptEnabled = true;
    for (const auto& pair : m_boolStorage) {
        std::string k = pair.first;
        std::transform(k.begin(), k.end(), k.begin(), ::tolower);
        if (k.find("enable") != std::string::npos || k.find("active") != std::string::npos) {
            isScriptEnabled = pair.second;
            break;
        }
    }
    if (!isScriptEnabled) return;

    float dt = (::WorldDeltaSeconds > 0.0f) ? ::WorldDeltaSeconds : deltaTime;


    // Execute real-time dynamic Print statements inside Update()
    for (const auto& item : m_updatePrintMessages) {
        std::string msg = item.textPrefix;
        if (item.appendDeltaTime) {
            msg += std::to_string(dt);
        }
        if (!item.varName.empty()) {
            auto itF = m_floatStorage.find(item.varName);
            if (itF != m_floatStorage.end()) msg += std::to_string(itF->second);
            auto itI = m_intStorage.find(item.varName);
            if (itI != m_intStorage.end()) msg += std::to_string(itI->second);
            auto itB = m_boolStorage.find(item.varName);
            if (itB != m_boolStorage.end()) msg += (itB->second ? "true" : "false");
            auto itS = m_stringStorage.find(item.varName);
            if (itS != m_stringStorage.end()) msg += itS->second;
        }
        if (item.appendPosition && m_owner) {
            msg += " Pos: (" + std::to_string(m_owner->position.x) + ", " +
                               std::to_string(m_owner->position.y) + ", " +
                               std::to_string(m_owner->position.z) + ")";
        }
        if (item.appendRotation && m_owner) {
            msg += " Rot: (" + std::to_string(m_owner->rotation.x) + ", " +
                               std::to_string(m_owner->rotation.y) + ", " +
                               std::to_string(m_owner->rotation.z) + ")";
        }
        if (!item.textSuffix.empty()) {
            msg += item.textSuffix;
        }

        PrintWithKey(item.printKey, msg, 2.5f);
    }

    // Execute real-time dynamic Transform actions inside Update() (dev.md: Transform.Translate, Transform.Rotate)
    Transform.Init(this);
    for (const auto& action : m_transformActions) {
        if (!action.requiredKeys.empty()) {
            auto& input = InputSystem::Get();
            bool conditionMet = false;
            for (const auto& cond : action.requiredKeys) {
                if (cond.key != -1) {
                    bool keyState = cond.isPressed ? input.IsKeyPressed(cond.key) : input.IsKeyDown(cond.key);
                    if (cond.isNegated) keyState = !keyState;
                    if (keyState) {
                        conditionMet = true;
                        break;
                    }
                }
            }
            if (!conditionMet) continue;
        }

        glm::vec3 delta(0.0f);
        for (int i = 0; i < 3; ++i) {
            float val = action.constantDelta[i];
            if (!action.varNames[i].empty()) {
                auto itF = m_floatStorage.find(action.varNames[i]);
                if (itF != m_floatStorage.end()) {
                    val = itF->second * action.varMultiplier[i];
                }
            }
            if (action.useDeltaTime[i]) {
                val *= dt;
            }
            delta[i] = val;
        }

        if (action.type == DynamicTransformAction::ActionType::TranslateRelative) {
            Transform.Translate(delta.x, delta.y, delta.z);
        } else if (action.type == DynamicTransformAction::ActionType::TranslateWorld) {
            Transform.WorldTranslate(delta.x, delta.y, delta.z);
        } else if (action.type == DynamicTransformAction::ActionType::RotateRelative) {
            Transform.Rotation += delta;
        } else if (action.type == DynamicTransformAction::ActionType::RotateWorld) {
            Transform.Rotation.WorldSpace(Transform.Rotation.WorldSpace() + delta);
        }
    }
}

void DynamicScriptBehaviour::RegisterProperties() {
    if (!m_sourceCppPath.empty()) {
        ParsePropertiesFromCpp(m_sourceCppPath);
    } else {
        m_properties.clear();
    }
}

void DynamicScriptBehaviour::RefreshPropertiesFromSource() {
    if (m_sourceCppPath.empty()) return;
    std::error_code ec;
    if (!std::filesystem::exists(m_sourceCppPath, ec)) return;
    auto lwt = std::filesystem::last_write_time(m_sourceCppPath, ec);
    if (!ec && (lwt > m_sourceTimestamp || m_properties.empty())) {
        ParsePropertiesFromCpp(m_sourceCppPath);
    }
}

void DynamicScriptBehaviour::ParsePropertiesFromCpp(const std::string& cppPath) {
    m_sourceCppPath = cppPath;
    std::error_code ec;
    if (!std::filesystem::exists(cppPath, ec)) return;
    m_sourceTimestamp = std::filesystem::last_write_time(cppPath, ec);

    std::ifstream file(cppPath);
    if (!file.is_open()) return;

    std::string line;
    std::vector<std::string> lines;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    file.close();

    // 1. First pass: find declared member variables and their types & defaults
    struct VarDecl {
        std::string type;
        std::string defaultVal;
    };
    std::unordered_map<std::string, VarDecl> declaredVars;

    for (const auto& rawLine : lines) {
        std::string l = rawLine;
        size_t start = l.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        l = l.substr(start);

        // Ignore comments
        if (l.rfind("//", 0) == 0 || l.rfind("/*", 0) == 0 || l.rfind("*", 0) == 0) continue;

        // Ignore methods
        if (l.find("(") != std::string::npos && l.find(")") != std::string::npos) {
            if (l.find("void ") != std::string::npos || l.find("override") != std::string::npos) continue;
        }

        std::vector<std::string> types = {
            "float", "double", "int", "bool", "std::string", "string",
            "glm::vec3", "Vector3", "glm::vec2", "Vector2",
            "Camera*", "LightComponent*", "PrimitiveMesh*", "GameObject*"
        };

        for (const auto& t : types) {
            if (l.rfind(t + " ", 0) == 0 || l.rfind(t + "\t", 0) == 0) {
                std::string rest = l.substr(t.length());
                size_t semi = rest.find(';');
                if (semi != std::string::npos) rest = rest.substr(0, semi);

                size_t eq = rest.find('=');
                std::string varName = (eq != std::string::npos) ? rest.substr(0, eq) : rest;
                std::string defVal = (eq != std::string::npos) ? rest.substr(eq + 1) : "";

                size_t vnStart = varName.find_first_not_of(" \t*&");
                size_t vnEnd = varName.find_last_not_of(" \t*&");
                if (vnStart != std::string::npos && vnEnd != std::string::npos) {
                    varName = varName.substr(vnStart, vnEnd - vnStart + 1);
                }

                size_t dvStart = defVal.find_first_not_of(" \t");
                size_t dvEnd = defVal.find_last_not_of(" \t");
                if (dvStart != std::string::npos && dvEnd != std::string::npos) {
                    defVal = defVal.substr(dvStart, dvEnd - dvStart + 1);
                }

                if (!varName.empty()) {
                    declaredVars[varName] = { t, defVal };
                }
                break;
            }
        }
    }

    // 2. Second pass: scan for RegisterProperty(...) and RegisterReference(...)
    std::unordered_map<std::string, float> existingFloats;
    for (const auto& pair : m_floatStorage) existingFloats[pair.first] = pair.second;
    std::unordered_map<std::string, int> existingInts;
    for (const auto& pair : m_intStorage) existingInts[pair.first] = pair.second;
    std::unordered_map<std::string, bool> existingBools;
    for (const auto& pair : m_boolStorage) existingBools[pair.first] = pair.second;
    std::unordered_map<std::string, std::string> existingStrings;
    for (const auto& pair : m_stringStorage) existingStrings[pair.first] = pair.second;
    std::unordered_map<std::string, glm::vec3> existingVec3s;
    for (const auto& pair : m_vec3Storage) existingVec3s[pair.first] = pair.second;

    m_properties.clear();
    std::unordered_set<std::string> registeredVarNames;

    auto trimArg = [](std::string s) -> std::string {
        size_t b = s.find_first_not_of(" \t\r\n");
        size_t e = s.find_last_not_of(" \t\r\n");
        if (b == std::string::npos || e == std::string::npos) return "";
        s = s.substr(b, e - b + 1);
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
            s = s.substr(1, s.size() - 2);
        }
        return s;
    };

    for (const auto& rawLine : lines) {
        std::string l = rawLine;
        size_t start = l.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        l = l.substr(start);

        if (l.rfind("//", 0) == 0 || l.rfind("/*", 0) == 0) continue;

        size_t regPropPos = l.find("RegisterProperty(");
        size_t regRefPos = l.find("RegisterReference(");

        if (regPropPos != std::string::npos) {
            size_t openP = l.find('(', regPropPos);
            size_t closeP = l.find(')', openP);
            if (openP == std::string::npos || closeP == std::string::npos) continue;
            std::string argsStr = l.substr(openP + 1, closeP - openP - 1);

            std::vector<std::string> args;
            bool inQuote = false;
            std::string curArg;
            for (char c : argsStr) {
                if (c == '"') inQuote = !inQuote;
                else if (c == ',' && !inQuote) {
                    args.push_back(curArg);
                    curArg.clear();
                    continue;
                }
                curArg += c;
            }
            if (!curArg.empty()) args.push_back(curArg);

            if (args.size() < 2) continue;
            std::string propName = trimArg(args[0]);
            std::string varRef = trimArg(args[1]);
            std::string category = (args.size() >= 3) ? trimArg(args[2]) : "General";

            size_t ampPos = varRef.find('&');
            if (ampPos != std::string::npos) varRef = varRef.substr(ampPos + 1);
            size_t arrowPos = varRef.find("->");
            if (arrowPos != std::string::npos) varRef = varRef.substr(arrowPos + 2);
            size_t vStart = varRef.find_first_not_of(" \t");
            size_t vEnd = varRef.find_last_not_of(" \t");
            if (vStart != std::string::npos && vEnd != std::string::npos) varRef = varRef.substr(vStart, vEnd - vStart + 1);

            registeredVarNames.insert(varRef);
            registeredVarNames.insert(propName);

            std::string type = "float";
            std::string defStr = "0";
            auto varIt = declaredVars.find(varRef);
            if (varIt != declaredVars.end()) {
                type = varIt->second.type;
                defStr = varIt->second.defaultVal;
            }

            float minVal = 0.0f, maxVal = 0.0f;
            if (args.size() >= 5) {
                try {
                    minVal = std::stof(trimArg(args[3]));
                    maxVal = std::stof(trimArg(args[4]));
                } catch (...) {}
            }

            if (type == "int" || type == "int32_t" || type == "int64_t" || type == "uint32_t" || type == "size_t") {
                int defInt = 0;
                try { if (!defStr.empty()) defInt = std::stoi(defStr); } catch (...) {}
                if (existingInts.find(propName) != existingInts.end()) defInt = existingInts[propName];
                m_intStorage[propName] = defInt;
                RegisterProperty(propName, &m_intStorage[propName], category, (int)minVal, (int)maxVal);
            } else if (type == "bool") {
                bool defBool = (defStr == "true" || defStr == "1");
                if (existingBools.find(propName) != existingBools.end()) defBool = existingBools[propName];
                m_boolStorage[propName] = defBool;
                RegisterProperty(propName, &m_boolStorage[propName], category);
            } else if (type == "std::string" || type == "string") {
                std::string defS = trimArg(defStr);
                if (existingStrings.find(propName) != existingStrings.end()) defS = existingStrings[propName];
                m_stringStorage[propName] = defS;
                RegisterProperty(propName, &m_stringStorage[propName], category);
            } else if (type == "glm::vec3" || type == "Vector3" || type == "vec3") {
                glm::vec3 defVec(0.0f);
                if (existingVec3s.find(propName) != existingVec3s.end()) defVec = existingVec3s[propName];
                m_vec3Storage[propName] = defVec;
                RegisterProperty(propName, &m_vec3Storage[propName], category);
            } else {
                float defF = 0.0f;
                try {
                    std::string cleanDef = defStr;
                    if (!cleanDef.empty() && (cleanDef.back() == 'f' || cleanDef.back() == 'F')) cleanDef.pop_back();
                    if (!cleanDef.empty()) defF = std::stof(cleanDef);
                } catch (...) {}
                if (existingFloats.find(propName) != existingFloats.end()) defF = existingFloats[propName];
                m_floatStorage[propName] = defF;
                RegisterProperty(propName, &m_floatStorage[propName], category, minVal, maxVal);

                std::string pLower = propName;
                std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::tolower);
                if (pLower.find("speed") != std::string::npos || pLower.find("rot") != std::string::npos) {
                    m_defaultSpeed = defF;
                }
            }
        } else if (regRefPos != std::string::npos) {
            size_t openP = l.find('(', regRefPos);
            size_t closeP = l.find(')', openP);
            if (openP == std::string::npos || closeP == std::string::npos) continue;
            std::string argsStr = l.substr(openP + 1, closeP - openP - 1);

            std::vector<std::string> args;
            bool inQuote = false;
            std::string curArg;
            for (char c : argsStr) {
                if (c == '"') inQuote = !inQuote;
                else if (c == ',' && !inQuote) {
                    args.push_back(curArg);
                    curArg.clear();
                    continue;
                }
                curArg += c;
            }
            if (!curArg.empty()) args.push_back(curArg);

            if (args.size() < 3) continue;
            std::string propName = trimArg(args[0]);
            std::string refTypeStr = trimArg(args[2]);
            std::string category = (args.size() >= 4) ? trimArg(args[3]) : "References";

            registeredVarNames.insert(propName);

            ObjectRefType refType = ObjectRefType::Actor;
            if (refTypeStr.find("Light") != std::string::npos) refType = ObjectRefType::Light;
            else if (refTypeStr.find("Mesh") != std::string::npos) refType = ObjectRefType::Mesh;
            else if (refTypeStr.find("Camera") != std::string::npos) refType = ObjectRefType::Camera;

            m_refStorage[propName] = nullptr;
            RegisterReference(propName, &m_refStorage[propName], refType, category);
        }
    }

    // Auto-register any declared member variables that were not in RegisterProperty
    for (const auto& pair : declaredVars) {
        const std::string& varName = pair.first;
        if (registeredVarNames.find(varName) != registeredVarNames.end()) continue;

        const std::string& type = pair.second.type;
        const std::string& defStr = pair.second.defaultVal;

        if (type == "float") {
            float defF = 0.0f;
            try {
                std::string clean = defStr;
                if (!clean.empty() && (clean.back() == 'f' || clean.back() == 'F')) clean.pop_back();
                if (!clean.empty()) defF = std::stof(clean);
            } catch (...) {}
            if (existingFloats.find(varName) != existingFloats.end()) defF = existingFloats[varName];
            m_floatStorage[varName] = defF;
            RegisterProperty(varName, &m_floatStorage[varName], "General");
            std::string vLower = varName;
            std::transform(vLower.begin(), vLower.end(), vLower.begin(), ::tolower);
            if (vLower.find("speed") != std::string::npos) {
                m_defaultSpeed = defF;
            }
        } else if (type == "bool") {
            bool defB = (defStr == "true" || defStr == "1");
            if (existingBools.find(varName) != existingBools.end()) defB = existingBools[varName];
            m_boolStorage[varName] = defB;
            RegisterProperty(varName, &m_boolStorage[varName], "General");
        } else if (type == "int") {
            int defI = 0;
            try { if (!defStr.empty()) defI = std::stoi(defStr); } catch (...) {}
            if (existingInts.find(varName) != existingInts.end()) defI = existingInts[varName];
            m_intStorage[varName] = defI;
            RegisterProperty(varName, &m_intStorage[varName], "General");
        } else if (type == "std::string" || type == "string") {
            std::string defS = trimArg(defStr);
            if (existingStrings.find(varName) != existingStrings.end()) defS = existingStrings[varName];
            m_stringStorage[varName] = defS;
            RegisterProperty(varName, &m_stringStorage[varName], "General");
        } else if (type == "glm::vec3" || type == "Vector3" || type == "vec3") {
            glm::vec3 defVec(0.0f);
            if (existingVec3s.find(varName) != existingVec3s.end()) defVec = existingVec3s[varName];
            m_vec3Storage[varName] = defVec;
            RegisterProperty(varName, &m_vec3Storage[varName], "General");
        }
    }

    // 3. Third pass: scan Start() and Update() bodies for runtime actions
    m_startPrintMessages.clear();
    m_updatePrintMessages.clear();
    m_transformActions.clear();
    m_hasRotation = false;
    m_hasTranslation = false;
    m_rotateAxis = glm::vec3(0.0f, 1.0f, 0.0f);

    auto parseArgExpr = [&](const std::string& rawExpr, float& outConst, bool& outUseDt, std::string& outVar, float& outMul) {
        std::string s = rawExpr;
        size_t b = s.find_first_not_of(" \t\r\n");
        size_t e = s.find_last_not_of(" \t\r\n");
        if (b == std::string::npos || e == std::string::npos) return;
        s = s.substr(b, e - b + 1);

        std::string lowerS = s;
        std::transform(lowerS.begin(), lowerS.end(), lowerS.begin(), ::tolower);

        if (lowerS.find("deltatime") != std::string::npos ||
            lowerS.find("delta_time") != std::string::npos ||
            lowerS.find("worlddeltaseconds") != std::string::npos ||
            lowerS.find("dt") != std::string::npos) {
            outUseDt = true;
        }

        for (const auto& v : declaredVars) {
            std::string vName = v.first;
            if (s.find(vName) != std::string::npos) {
                outVar = vName;
                break;
            }
        }

        outConst = 0.0f;
        outMul = 1.0f;

        size_t numStart = s.find_first_of("-+0123456789.");
        if (numStart != std::string::npos) {
            size_t numEnd = numStart;
            if (s[numStart] == '-' || s[numStart] == '+') numEnd++;
            while (numEnd < s.size() && (std::isdigit(s[numEnd]) || s[numEnd] == '.' || s[numEnd] == 'f' || s[numEnd] == 'F')) {
                numEnd++;
            }
            std::string numStr = s.substr(numStart, numEnd - numStart);
            if (!numStr.empty() && (numStr.back() == 'f' || numStr.back() == 'F')) numStr.pop_back();
            try {
                if (!numStr.empty() && numStr != "-" && numStr != "+") {
                    float val = std::stof(numStr);
                    if (outVar.empty()) {
                        outConst = val;
                    } else {
                        outMul = val;
                    }
                }
            } catch (...) {}
        }
    };

    auto extractKeyConditions = [](const std::string& str) -> std::vector<DynamicScriptBehaviour::DynamicKeyCondition> {
        std::vector<DynamicScriptBehaviour::DynamicKeyCondition> conds;
        static const std::unordered_map<std::string, int> keyMap = {
            {"w", Key::W}, {"a", Key::A}, {"s", Key::S}, {"d", Key::D},
            {"q", Key::Q}, {"e", Key::E}, {"r", Key::R}, {"f", Key::F},
            {"c", Key::C}, {"x", Key::X}, {"z", Key::Z}, {"v", Key::V},
            {"b", Key::B}, {"g", Key::G}, {"h", Key::H}, {"i", Key::I},
            {"j", Key::J}, {"k", Key::K}, {"l", Key::L}, {"m", Key::M},
            {"n", Key::N}, {"o", Key::O}, {"p", Key::P}, {"t", Key::T},
            {"u", Key::U}, {"y", Key::Y},
            {"space", Key::Space}, {"lshift", Key::LShift}, {"rshift", Key::RShift},
            {"lctrl", Key::LCtrl}, {"rctrl", Key::RCtrl}, {"lalt", Key::LAlt}, {"ralt", Key::RAlt},
            {"up", Key::Up}, {"down", Key::Down}, {"left", Key::Left}, {"right", Key::Right},
            {"escape", Key::Escape}, {"enter", Key::Enter}, {"tab", Key::Tab},
            {"delete", Key::Delete}, {"backspace", Key::Backspace}
        };

        size_t searchPos = 0;
        while (searchPos < str.size()) {
            size_t kdPos = str.find("IsKeyDown(", searchPos);
            size_t kpPos = str.find("IsKeyPressed(", searchPos);
            size_t nextPos = std::min(kdPos, kpPos);
            if (nextPos == std::string::npos) break;

            bool isPress = (nextPos == kpPos);
            bool isNegated = false;
            if (nextPos > 0 && str[nextPos - 1] == '!') isNegated = true;
            else if (nextPos >= 2 && str[nextPos - 2] == '!' && str[nextPos - 1] == ' ') isNegated = true;

            size_t openP = str.find('(', nextPos);
            size_t closeP = str.find(')', openP);
            if (openP == std::string::npos || closeP == std::string::npos) break;

            std::string arg = str.substr(openP + 1, closeP - openP - 1);
            std::string lowerArg = arg;
            std::transform(lowerArg.begin(), lowerArg.end(), lowerArg.begin(), ::tolower);

            size_t colon = lowerArg.rfind("::");
            if (colon != std::string::npos) lowerArg = lowerArg.substr(colon + 2);
            size_t glfwPrefix = lowerArg.find("glfw_key_");
            if (glfwPrefix != std::string::npos) lowerArg = lowerArg.substr(glfwPrefix + 9);

            size_t b = lowerArg.find_first_not_of(" \t\r\n'\"");
            size_t e = lowerArg.find_last_not_of(" \t\r\n'\"");
            if (b != std::string::npos && e != std::string::npos) {
                lowerArg = lowerArg.substr(b, e - b + 1);
            }

            auto it = keyMap.find(lowerArg);
            if (it != keyMap.end()) {
                DynamicScriptBehaviour::DynamicKeyCondition cond;
                cond.key = it->second;
                cond.isPressed = isPress;
                cond.isNegated = isNegated;
                conds.push_back(cond);
            }

            searchPos = closeP + 1;
        }
        return conds;
    };

    bool inStartMethod = false;
    bool inUpdateMethod = false;
    std::vector<DynamicScriptBehaviour::DynamicKeyCondition> activeKeyConditions;
    bool pendingSingleLineCondition = false;
    int conditionBraceLevel = 0;

    for (const auto& rawLine : lines) {
        std::string l = rawLine;
        size_t start = l.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        l = l.substr(start);

        if (l.rfind("//", 0) == 0 || l.rfind("/*", 0) == 0) continue;

        if (l.find("void Start(") != std::string::npos || l.find("Start()") != std::string::npos) {
            inStartMethod = true;
            inUpdateMethod = false;
        } else if (l.find("void Update(") != std::string::npos || l.find("Update(float") != std::string::npos) {
            inUpdateMethod = true;
            inStartMethod = false;
            activeKeyConditions.clear();
            pendingSingleLineCondition = false;
            conditionBraceLevel = 0;
        }

        if (inStartMethod) {
            size_t prPos = l.find("Print(");
            if (prPos != std::string::npos) {
                size_t q1 = l.find('"', prPos);
                if (q1 != std::string::npos) {
                    size_t q2 = l.find('"', q1 + 1);
                    if (q2 != std::string::npos) {
                        m_startPrintMessages.push_back(l.substr(q1 + 1, q2 - q1 - 1));
                    }
                }
            }
            if (l.find('}') != std::string::npos && l.find('{') == std::string::npos) {
                inStartMethod = false;
            }
        }

        if (inUpdateMethod) {
            // Track block braces for active condition
            if (conditionBraceLevel > 0) {
                for (char c : l) {
                    if (c == '{') conditionBraceLevel++;
                    else if (c == '}') conditionBraceLevel--;
                }
                if (conditionBraceLevel <= 0) {
                    conditionBraceLevel = 0;
                    activeKeyConditions.clear();
                }
            }

            // Check if this line introduces an if (...) with key checks
            size_t ifPos = l.find("if (");
            if (ifPos == std::string::npos) ifPos = l.find("if(");
            std::vector<DynamicScriptBehaviour::DynamicKeyCondition> lineConditions;
            if (ifPos != std::string::npos) {
                lineConditions = extractKeyConditions(l);
            }

            bool hasTranslate = (l.find("Translate(") != std::string::npos);
            bool hasRotate = (l.find("Rotate(") != std::string::npos && l.find("AutoRotate") == std::string::npos);

            if (!lineConditions.empty()) {
                if (hasTranslate || hasRotate) {
                    activeKeyConditions = lineConditions;
                    pendingSingleLineCondition = true;
                } else if (l.find('{') != std::string::npos) {
                    activeKeyConditions = lineConditions;
                    conditionBraceLevel = 1;
                    pendingSingleLineCondition = false;
                } else {
                    activeKeyConditions = lineConditions;
                    pendingSingleLineCondition = true;
                }
            }

            // 1. Scan for Print statements
            size_t prPos = l.find("Print(");
            if (prPos != std::string::npos) {
                size_t openP = l.find('(', prPos);
                size_t closeP = l.rfind(')');
                if (openP != std::string::npos && closeP != std::string::npos && closeP > openP) {
                    std::string inner = l.substr(openP + 1, closeP - openP - 1);
                    std::string lowerInner = inner;
                    std::transform(lowerInner.begin(), lowerInner.end(), lowerInner.begin(), ::tolower);

                    DynamicScriptBehaviour::DynamicUpdatePrint item;
                    if (lowerInner.find("deltatime") != std::string::npos ||
                        lowerInner.find("delta_time") != std::string::npos ||
                        lowerInner.find("worlddeltaseconds") != std::string::npos ||
                        lowerInner.find("dt") != std::string::npos) {
                        item.appendDeltaTime = true;
                    }

                    if (lowerInner.find("position") != std::string::npos || lowerInner.find("location") != std::string::npos) {
                        item.appendPosition = true;
                    }
                    if (lowerInner.find("rotation") != std::string::npos) {
                        item.appendRotation = true;
                    }

                    for (const auto& v : declaredVars) {
                        std::string vLow = v.first;
                        std::transform(vLow.begin(), vLow.end(), vLow.begin(), ::tolower);
                        if (lowerInner.find(vLow) != std::string::npos) {
                            item.varName = v.first;
                            break;
                        }
                    }

                    size_t q1 = inner.find('"');
                    if (q1 != std::string::npos) {
                        size_t q2 = inner.find('"', q1 + 1);
                        if (q2 != std::string::npos) {
                            item.textPrefix = inner.substr(q1 + 1, q2 - q1 - 1);
                            size_t q3 = inner.find('"', q2 + 1);
                            if (q3 != std::string::npos) {
                                size_t q4 = inner.find('"', q3 + 1);
                                if (q4 != std::string::npos) {
                                    item.textSuffix = inner.substr(q3 + 1, q4 - q3 - 1);
                                }
                            }
                        } else {
                            item.textPrefix = inner.substr(q1 + 1);
                        }
                    } else {
                        if (item.appendDeltaTime) item.textPrefix = "DeltaTime: ";
                        else if (!item.varName.empty()) item.textPrefix = item.varName + ": ";
                        else item.textPrefix = inner;
                    }

                    item.printKey = (int)m_updatePrintMessages.size() + 100;
                    m_updatePrintMessages.push_back(item);
                }
            }

            // 2. Scan for Transform.Translate / RelativeTranslate / WorldTranslate
            size_t transPos = l.find("Translate(");
            if (transPos != std::string::npos) {
                DynamicTransformAction action;
                action.type = (l.find("WorldTranslate(") != std::string::npos)
                              ? DynamicTransformAction::ActionType::TranslateWorld
                              : DynamicTransformAction::ActionType::TranslateRelative;

                size_t openP = l.find('(', transPos);
                size_t closeP = l.rfind(')');
                if (openP != std::string::npos && closeP != std::string::npos && closeP > openP) {
                    std::string argsStr = l.substr(openP + 1, closeP - openP - 1);
                    std::vector<std::string> args;
                    std::string cur;
                    int parenDepth = 0;
                    for (char c : argsStr) {
                        if (c == '(') parenDepth++;
                        else if (c == ')') parenDepth--;
                        else if (c == ',' && parenDepth == 0) {
                            args.push_back(cur);
                            cur.clear();
                            continue;
                        }
                        cur += c;
                    }
                    if (!cur.empty()) args.push_back(cur);

                    if (args.size() == 3) {
                        for (int i = 0; i < 3; ++i) {
                            parseArgExpr(args[i], action.constantDelta[i], action.useDeltaTime[i], action.varNames[i], action.varMultiplier[i]);
                        }
                        if (!activeKeyConditions.empty()) {
                            action.requiredKeys = activeKeyConditions;
                            if (pendingSingleLineCondition) {
                                activeKeyConditions.clear();
                                pendingSingleLineCondition = false;
                            }
                        }
                        m_transformActions.push_back(action);
                    }
                }
            }

            // 3. Scan for Transform.Rotate / RelativeRotate / WorldRotate
            size_t rotPos = l.find("Rotate(");
            if (rotPos != std::string::npos && l.find("AutoRotate") == std::string::npos) {
                DynamicTransformAction action;
                action.type = (l.find("WorldRotate(") != std::string::npos)
                              ? DynamicTransformAction::ActionType::RotateWorld
                              : DynamicTransformAction::ActionType::RotateRelative;

                size_t openP = l.find('(', rotPos);
                size_t closeP = l.rfind(')');
                if (openP != std::string::npos && closeP != std::string::npos && closeP > openP) {
                    std::string argsStr = l.substr(openP + 1, closeP - openP - 1);
                    std::vector<std::string> args;
                    std::string cur;
                    int parenDepth = 0;
                    for (char c : argsStr) {
                        if (c == '(') parenDepth++;
                        else if (c == ')') parenDepth--;
                        else if (c == ',' && parenDepth == 0) {
                            args.push_back(cur);
                            cur.clear();
                            continue;
                        }
                        cur += c;
                    }
                    if (!cur.empty()) args.push_back(cur);

                    if (args.size() == 3) {
                        for (int i = 0; i < 3; ++i) {
                            parseArgExpr(args[i], action.constantDelta[i], action.useDeltaTime[i], action.varNames[i], action.varMultiplier[i]);
                        }
                        if (!activeKeyConditions.empty()) {
                            action.requiredKeys = activeKeyConditions;
                            if (pendingSingleLineCondition) {
                                activeKeyConditions.clear();
                                pendingSingleLineCondition = false;
                            }
                        }
                        m_transformActions.push_back(action);
                    }
                }
            }

            if (!hasTranslate && !hasRotate && lineConditions.empty() && pendingSingleLineCondition) {
                if (l.find(';') != std::string::npos || l.find('{') != std::string::npos) {
                    activeKeyConditions.clear();
                    pendingSingleLineCondition = false;
                }
            }

            if (conditionBraceLevel == 0 && l.find('}') != std::string::npos && l.find('{') == std::string::npos) {
                inUpdateMethod = false;
                activeKeyConditions.clear();
                pendingSingleLineCondition = false;
            }
        }

    }
}

// ============================================================================
// GetComponent<T> Specialisations (dev.md Section 35)
// Implementations live here to avoid circular includes between
// EunoiaBehaviour.h ↔ GameObject.h.
// ============================================================================

template<>
LightComponent* EunoiaBehaviour::GetComponent<LightComponent>() const {
    if (!m_owner || !m_owner->isLight) return nullptr;
    return &(m_owner->light);
}

template<>
PrimitiveMesh* EunoiaBehaviour::GetComponent<PrimitiveMesh>() const {
    if (!m_owner || m_owner->isLight) return nullptr;
    return &(m_owner->mesh);
}

// ============================================================================
// SpawnGameObject / DestroyGameObject implementations (dev.md Section 45)
// ============================================================================

GameObject* EunoiaBehaviour::SpawnGameObject(const std::string& name, const glm::vec3& position) {
    if (!m_scene) {
        AddEngineLog("LogBehaviour", "[Behaviour] SpawnGameObject: no active scene - call only during Play Mode", 1);
        return nullptr;
    }
    GameObject& obj = m_scene->AddObject(PrimitiveType::Empty, position, glm::vec3(0.55f));
    obj.name = name;
    AddEngineLog("LogBehaviour", "[Behaviour] SpawnGameObject: created '" + name + "' (ID " + std::to_string(obj.id) + ")", 0);
    return &obj;
}

void EunoiaBehaviour::DestroyGameObject(int objectId) {
    if (!m_scene) return;
    m_scene->RemoveObject(objectId);
    AddEngineLog("LogBehaviour", "[Behaviour] DestroyGameObject: removed object ID " + std::to_string(objectId), 0);
}
