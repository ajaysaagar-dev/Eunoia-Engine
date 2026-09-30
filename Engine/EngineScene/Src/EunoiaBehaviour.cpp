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
// CursorStateController (dev.md: CursorState.Locked(); CursorState.UnLocked();)
// ============================================================================

static struct CursorSyncInitializer {
    CursorSyncInitializer() {
        CursorStateController::s_cursorSyncCallback = []() {
            if (CursorStateController::s_activeBehaviour) {
                auto* beh = reinterpret_cast<EunoiaBehaviour*>(CursorStateController::s_activeBehaviour);
                if (beh && beh->GetOwner()) {
                    glm::vec3 rot = beh->GetOwner()->rotation;
                    InputSystem::Get().SetCursorPos(-(double)rot.y, (double)rot.x);
                }
            }
        };
    }
} s_cursorSyncInitializer;

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
    if (target == TargetType::RelativeRotation || target == TargetType::WorldRotation) {
        glm::vec3 current = GetValue();
        bool isCamera = false;
        if (parent && parent->gameObject) {
            isCamera = (parent->gameObject->isCamera || parent->gameObject->type == PrimitiveType::Camera);
        }
        if (inX != 0.0f) current.x = inX;
        if (inY != 0.0f) current.y = inY;
        if (inZ != 0.0f) current.z = inZ;
        if (isCamera) {
            current.x = std::clamp(current.x, -89.0f, 89.0f);
        }
        SetValue(current);
        return;
    }
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
    float rYaw = glm::radians(rot.y);
    glm::vec3 forward = glm::vec3(-std::sin(rYaw), 0.0f, -std::cos(rYaw));
    glm::vec3 right   = glm::vec3( std::cos(rYaw), 0.0f, -std::sin(rYaw));
    glm::vec3 up      = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 localDelta = delta.x * forward + delta.y * up + delta.z * right;
    SetRelativeLocationInternal(GetRelativeLocationInternal() + localDelta);
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
    if (target == TargetType::RelativeRotation || target == TargetType::WorldRotation) {
        glm::vec3 current = GetValue();
        bool isCamera = false;
        if (parent && parent->m_behaviour && parent->m_behaviour->GetOwner()) {
            auto* owner = parent->m_behaviour->GetOwner();
            isCamera = (owner->isCamera || owner->type == PrimitiveType::Camera);
        }
        if (inX != 0.0f) current.x = inX;
        if (inY != 0.0f) current.y = inY;
        if (inZ != 0.0f) current.z = inZ;
        if (isCamera) {
            current.x = std::clamp(current.x, -89.0f, 89.0f);
        }
        SetValue(current);
        return;
    }
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

void EunoiaBehaviour::RegisterReference(const std::string& name, void* ptrAddr, const std::string& category) {
    RegisterReference(name, ptrAddr, ObjectRefType::Actor, category);
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


void DynamicScriptBehaviour::Start() {
    for (const auto& msg : m_startPrintMessages) {
        Print(msg);
        AddEngineLog("LogBehaviour", "[" + m_className + "] " + msg, 0);
    }
    if (m_startLockCursor) {
        CursorState.Locked();
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
    auto& input = InputSystem::Get();
    for (const auto& action : m_transformActions) {
        if (!action.requiredKeys.empty()) {
            bool conditionMet = false;
            for (const auto& cond : action.requiredKeys) {
                if (cond.key == -2) {
                    if (input.GetMouseX() != 0.0) { conditionMet = true; break; }
                } else if (cond.key == -3) {
                    if (input.GetMouseY() != 0.0) { conditionMet = true; break; }
                } else if (cond.key != -1) {
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
                if (action.varNames[i] == "$__MouseDeltaX") {
                    val = (float)input.GetMouseDeltaX() * action.varMultiplier[i];
                } else if (action.varNames[i] == "$__MouseDeltaY") {
                    val = (float)input.GetMouseDeltaY() * action.varMultiplier[i];
                } else if (action.varNames[i] == "$__MouseX") {
                    val = (float)input.GetMouseX() * action.varMultiplier[i];
                } else if (action.varNames[i] == "$__MouseY") {
                    val = (float)input.GetMouseY() * action.varMultiplier[i];
                } else {
                    auto itF = m_floatStorage.find(action.varNames[i]);
                    if (itF != m_floatStorage.end()) {
                        val = itF->second * action.varMultiplier[i];
                    }
                }
            }
            if (!action.scaleVarNames[i].empty()) {
                auto itScale = m_floatStorage.find(action.scaleVarNames[i]);
                if (itScale != m_floatStorage.end()) {
                    val *= itScale->second;
                }
            }
            if (action.useDeltaTime[i]) {
                val *= dt;
            }
            delta[i] = val;
        }

        GameObject* targetObj = m_owner;
        if (!action.targetRefName.empty()) {
            auto itRef = m_refStorage.find(action.targetRefName);
            if (itRef == m_refStorage.end() && (action.targetRefName == "target" || action.targetRefName == "PlayerActor" || action.targetRefName == "player")) {
                itRef = m_refStorage.find("Player");
            }
            if (itRef != m_refStorage.end() && itRef->second) {
                targetObj = reinterpret_cast<GameObject*>(itRef->second);
            } else if (m_scene && m_owner) {
                std::string lowerRef = action.targetRefName;
                std::transform(lowerRef.begin(), lowerRef.end(), lowerRef.begin(), ::tolower);
                if (lowerRef.find("camera") != std::string::npos || lowerRef.find("cam") != std::string::npos || lowerRef.find("boom") != std::string::npos) {
                    for (auto& obj : m_scene->objects) {
                        if (obj.parentId == m_owner->id) {
                            targetObj = &obj;
                            break;
                        }
                    }
                }
                if (!targetObj) targetObj = m_owner;
            } else {
                targetObj = m_owner;
            }
        }
        if (!targetObj) continue;

        if (action.type == DynamicTransformAction::ActionType::TranslateRelative) {
            if (targetObj == m_owner) {
                Transform.Translate(delta.x, delta.y, delta.z);
            } else {
                targetObj->transform.RelativeTranslate(delta.x, delta.y, delta.z);
            }
        } else if (action.type == DynamicTransformAction::ActionType::TranslateWorld) {
            if (targetObj == m_owner) {
                Transform.WorldTranslate(delta.x, delta.y, delta.z);
            } else {
                targetObj->transform.WorldTranslate(delta.x, delta.y, delta.z);
            }
        } else if (action.type == DynamicTransformAction::ActionType::RotateRelative ||
                   action.type == DynamicTransformAction::ActionType::RotateWorld) {
            glm::vec3 cur = targetObj->rotation;
            bool isCamera = (targetObj->isCamera || targetObj->type == PrimitiveType::Camera);
            for (int i = 0; i < 3; ++i) {
                if (!action.activeAxis[i]) continue;
                bool isDelta = (action.varNames[i] == "$__MouseDeltaX" || action.varNames[i] == "$__MouseDeltaY" || action.useDeltaTime[i]);
                if (isDelta) {
                    if (i == 0) cur.x += delta.x;
                    else if (i == 1) cur.y += delta.y;
                    else if (i == 2) cur.z += delta.z;
                } else {
                    if (i == 0) cur.x = delta.x;
                    else if (i == 1) cur.y = delta.y;
                    else if (i == 2) cur.z = delta.z;
                }
            }
            if (isCamera) {
                cur.x = std::clamp(cur.x, -89.0f, 89.0f);
            }
            while (cur.y >= 360.0f) cur.y -= 360.0f;
            while (cur.y < 0.0f)    cur.y += 360.0f;
            targetObj->rotation = cur;
        } else if (action.type == DynamicTransformAction::ActionType::LockCursor) {
            CursorState.Locked();
        } else if (action.type == DynamicTransformAction::ActionType::UnlockCursor) {
            CursorState.UnLocked();
        }
    }
}

void DynamicScriptBehaviour::RegisterProperties() {
    if (!m_sourceCppPath.empty()) {
        ParsePropertiesFromCpp(m_sourceCppPath, true);
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
        ParsePropertiesFromCpp(m_sourceCppPath, true);
    }
}

void DynamicScriptBehaviour::ParsePropertiesFromCpp(const std::string& cppPath, bool preserveExistingValues) {
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

        // Check pointer declarations (handles "GameObject* Cam", "GameObject *Cam", "Actor *Cam", etc.)
        static const std::vector<std::pair<std::string, std::string>> ptrTypes = {
            { "GameObject", "GameObject*" },
            { "Actor", "GameObject*" },
            { "Camera", "Camera*" },
            { "LightComponent", "LightComponent*" },
            { "PrimitiveMesh", "PrimitiveMesh*" }
        };

        bool matchedPtr = false;
        for (const auto& pt : ptrTypes) {
            if (l.rfind(pt.first, 0) == 0) {
                size_t afterName = pt.first.length();
                while (afterName < l.size() && (l[afterName] == ' ' || l[afterName] == '\t')) afterName++;
                if (afterName < l.size() && l[afterName] == '*') {
                    matchedPtr = true;
                    std::string rest = l.substr(afterName + 1);
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
                        declaredVars[varName] = { pt.second, defVal };
                    }
                    break;
                }
            }
        }
        if (matchedPtr) continue;

        std::vector<std::string> types = {
            "float", "double", "int", "bool", "std::string", "string",
            "glm::vec3", "Vector3", "glm::vec2", "Vector2"
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
    struct SavedValue {
        BehaviourPropertyType type = BehaviourPropertyType::Float;
        bool boolVal = false;
        int intVal = 0;
        int64_t int64Val = 0;
        float floatVal = 0.0f;
        double doubleVal = 0.0;
        std::string stringVal;
        glm::vec2 vec2Val{0.0f};
        glm::vec3 vec3Val{0.0f};
        glm::vec4 vec4Val{0.0f};
        int targetId = -1;
        void* refPtr = nullptr;
        bool hasValue = false;
    };
    std::unordered_map<std::string, SavedValue> savedPropsByName;

    if (preserveExistingValues) {
        // Snapshot existing live properties
        for (const auto& p : m_properties) {
            SavedValue sv;
            sv.type = p.type;
            sv.targetId = p.targetId;
            sv.hasValue = true;
            if (p.dataPtr) {
                switch (p.type) {
                    case BehaviourPropertyType::Bool:
                        sv.boolVal = *reinterpret_cast<const bool*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::Int:
                        sv.intVal = *reinterpret_cast<const int*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::Int64:
                        sv.int64Val = *reinterpret_cast<const int64_t*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::Float:
                        sv.floatVal = *reinterpret_cast<const float*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::Double:
                        sv.doubleVal = *reinterpret_cast<const double*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::String:
                        sv.stringVal = *reinterpret_cast<const std::string*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::Vec2:
                        sv.vec2Val = *reinterpret_cast<const glm::vec2*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::Vec3:
                    case BehaviourPropertyType::Color3:
                        sv.vec3Val = *reinterpret_cast<const glm::vec3*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::Vec4:
                    case BehaviourPropertyType::Color4:
                        sv.vec4Val = *reinterpret_cast<const glm::vec4*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::ObjectRef:
                        sv.refPtr = *reinterpret_cast<void* const*>(p.dataPtr);
                        break;
                }
            }
            savedPropsByName[p.name] = sv;
        }

        // Also snapshot from storage maps in case not yet in m_properties
        for (const auto& pair : m_floatStorage) {
            auto& sv = savedPropsByName[pair.first];
            sv.floatVal = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_intStorage) {
            auto& sv = savedPropsByName[pair.first];
            sv.intVal = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_int64Storage) {
            auto& sv = savedPropsByName[pair.first];
            sv.int64Val = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_boolStorage) {
            auto& sv = savedPropsByName[pair.first];
            sv.boolVal = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_stringStorage) {
            auto& sv = savedPropsByName[pair.first];
            sv.stringVal = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_doubleStorage) {
            auto& sv = savedPropsByName[pair.first];
            sv.doubleVal = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_vec2Storage) {
            auto& sv = savedPropsByName[pair.first];
            sv.vec2Val = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_vec3Storage) {
            auto& sv = savedPropsByName[pair.first];
            sv.vec3Val = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_vec4Storage) {
            auto& sv = savedPropsByName[pair.first];
            sv.vec4Val = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_refStorage) {
            auto& sv = savedPropsByName[pair.first];
            sv.refPtr = pair.second;
            sv.hasValue = true;
        }
    }

    auto findSaved = [&](const std::string& name1, const std::string& name2 = "") -> const SavedValue* {
        if (!preserveExistingValues) return nullptr;
        if (!name1.empty()) {
            auto it = savedPropsByName.find(name1);
            if (it != savedPropsByName.end() && it->second.hasValue) return &it->second;
        }
        if (!name2.empty()) {
            auto it = savedPropsByName.find(name2);
            if (it != savedPropsByName.end() && it->second.hasValue) return &it->second;
        }
        return nullptr;
    };

    m_properties.clear();
    m_defaultPropertyValues.clear();
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
                PropertyDefaultValue def;
                def.type = BehaviourPropertyType::Int;
                def.intVal = defInt;
                m_defaultPropertyValues[propName] = def;
                if (!varRef.empty()) m_defaultPropertyValues[varRef] = def;

                const SavedValue* sv = findSaved(propName, varRef);
                int actualInt = (sv && sv->hasValue) ? sv->intVal : defInt;
                m_intStorage[propName] = actualInt;
                RegisterProperty(propName, &m_intStorage[propName], category, (int)minVal, (int)maxVal);
            } else if (type == "bool") {
                bool defBool = (defStr == "true" || defStr == "1");
                PropertyDefaultValue def;
                def.type = BehaviourPropertyType::Bool;
                def.boolVal = defBool;
                m_defaultPropertyValues[propName] = def;
                if (!varRef.empty()) m_defaultPropertyValues[varRef] = def;

                const SavedValue* sv = findSaved(propName, varRef);
                bool actualBool = (sv && sv->hasValue) ? sv->boolVal : defBool;
                m_boolStorage[propName] = actualBool;
                RegisterProperty(propName, &m_boolStorage[propName], category);
            } else if (type == "std::string" || type == "string") {
                std::string defS = trimArg(defStr);
                PropertyDefaultValue def;
                def.type = BehaviourPropertyType::String;
                def.stringVal = defS;
                m_defaultPropertyValues[propName] = def;
                if (!varRef.empty()) m_defaultPropertyValues[varRef] = def;

                const SavedValue* sv = findSaved(propName, varRef);
                std::string actualS = (sv && sv->hasValue) ? sv->stringVal : defS;
                m_stringStorage[propName] = actualS;
                RegisterProperty(propName, &m_stringStorage[propName], category);
            } else if (type == "glm::vec2" || type == "Vector2" || type == "vec2") {
                glm::vec2 defVec2(0.0f);
                PropertyDefaultValue def;
                def.type = BehaviourPropertyType::Vec2;
                def.vec2Val = defVec2;
                m_defaultPropertyValues[propName] = def;
                if (!varRef.empty()) m_defaultPropertyValues[varRef] = def;

                const SavedValue* sv = findSaved(propName, varRef);
                glm::vec2 actualVec2 = (sv && sv->hasValue) ? sv->vec2Val : defVec2;
                m_vec2Storage[propName] = actualVec2;
                RegisterProperty(propName, &m_vec2Storage[propName], category);
            } else if (type == "glm::vec3" || type == "Vector3" || type == "vec3") {
                glm::vec3 defVec(0.0f);
                PropertyDefaultValue def;
                def.type = BehaviourPropertyType::Vec3;
                def.vec3Val = defVec;
                m_defaultPropertyValues[propName] = def;
                if (!varRef.empty()) m_defaultPropertyValues[varRef] = def;

                const SavedValue* sv = findSaved(propName, varRef);
                glm::vec3 actualVec = (sv && sv->hasValue) ? sv->vec3Val : defVec;
                m_vec3Storage[propName] = actualVec;
                RegisterProperty(propName, &m_vec3Storage[propName], category);
            } else if (type == "glm::vec4" || type == "Vector4" || type == "vec4") {
                glm::vec4 defVec4(0.0f);
                PropertyDefaultValue def;
                def.type = BehaviourPropertyType::Vec4;
                def.vec4Val = defVec4;
                m_defaultPropertyValues[propName] = def;
                if (!varRef.empty()) m_defaultPropertyValues[varRef] = def;

                const SavedValue* sv = findSaved(propName, varRef);
                glm::vec4 actualVec4 = (sv && sv->hasValue) ? sv->vec4Val : defVec4;
                m_vec4Storage[propName] = actualVec4;
                RegisterProperty(propName, &m_vec4Storage[propName], category);
            } else if (type == "double") {
                double defD = 0.0;
                try {
                    std::string cleanDef = defStr;
                    if (!cleanDef.empty() && (cleanDef.back() == 'd' || cleanDef.back() == 'D')) cleanDef.pop_back();
                    if (!cleanDef.empty()) defD = std::stod(cleanDef);
                } catch (...) {}
                PropertyDefaultValue def;
                def.type = BehaviourPropertyType::Double;
                def.doubleVal = defD;
                m_defaultPropertyValues[propName] = def;
                if (!varRef.empty()) m_defaultPropertyValues[varRef] = def;

                const SavedValue* sv = findSaved(propName, varRef);
                double actualD = (sv && sv->hasValue) ? sv->doubleVal : defD;
                m_doubleStorage[propName] = actualD;
                RegisterProperty(propName, &m_doubleStorage[propName], category);
            } else {
                float defF = 0.0f;
                try {
                    std::string cleanDef = defStr;
                    if (!cleanDef.empty() && (cleanDef.back() == 'f' || cleanDef.back() == 'F')) cleanDef.pop_back();
                    if (!cleanDef.empty()) defF = std::stof(cleanDef);
                } catch (...) {}
                PropertyDefaultValue def;
                def.type = BehaviourPropertyType::Float;
                def.floatVal = defF;
                m_defaultPropertyValues[propName] = def;
                if (!varRef.empty()) m_defaultPropertyValues[varRef] = def;

                const SavedValue* sv = findSaved(propName, varRef);
                float actualF = (sv && sv->hasValue) ? sv->floatVal : defF;
                m_floatStorage[propName] = actualF;
                RegisterProperty(propName, &m_floatStorage[propName], category, minVal, maxVal);

                std::string pLower = propName;
                std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::tolower);
                if (pLower.find("speed") != std::string::npos || pLower.find("rot") != std::string::npos) {
                    m_defaultSpeed = actualF;
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

            if (args.size() < 2) continue;
            std::string propName = trimArg(args[0]);
            std::string refVar = trimArg(args[1]);
            size_t ampPos = refVar.find('&');
            if (ampPos != std::string::npos) refVar = refVar.substr(ampPos + 1);
            size_t arrowPos = refVar.find("->");
            if (arrowPos != std::string::npos) refVar = refVar.substr(arrowPos + 2);
            size_t rvStart = refVar.find_first_not_of(" \t");
            size_t rvEnd = refVar.find_last_not_of(" \t");
            if (rvStart != std::string::npos && rvEnd != std::string::npos) refVar = refVar.substr(rvStart, rvEnd - rvStart + 1);

            registeredVarNames.insert(propName);
            if (!refVar.empty()) registeredVarNames.insert(refVar);

            ObjectRefType refType = ObjectRefType::Actor;
            std::string category = "References";

            if (args.size() == 2) {
                // RegisterReference("Name", &var)
                refType = ObjectRefType::Actor;
                category = "References";
            } else if (args.size() == 3) {
                std::string arg2 = trimArg(args[2]);
                std::string arg2Lower = arg2;
                std::transform(arg2Lower.begin(), arg2Lower.end(), arg2Lower.begin(), ::tolower);
                if (arg2Lower.find("objectreftype") != std::string::npos ||
                    arg2Lower == "light" || arg2Lower == "mesh" || arg2Lower == "camera" || arg2Lower == "actor" || arg2Lower == "gameobject") {
                    if (arg2Lower.find("light") != std::string::npos) refType = ObjectRefType::Light;
                    else if (arg2Lower.find("mesh") != std::string::npos) refType = ObjectRefType::Mesh;
                    else if (arg2Lower.find("camera") != std::string::npos) refType = ObjectRefType::Camera;
                    else refType = ObjectRefType::Actor;
                    category = "References";
                } else {
                    // Third argument is category name: RegisterReference("Camera", &Camera, "Looke")
                    refType = ObjectRefType::Actor;
                    category = arg2;
                }
            } else if (args.size() >= 4) {
                // RegisterReference("Name", &var, ObjectRefType::X, "Category")
                std::string arg2 = trimArg(args[2]);
                std::string arg2Lower = arg2;
                std::transform(arg2Lower.begin(), arg2Lower.end(), arg2Lower.begin(), ::tolower);
                if (arg2Lower.find("light") != std::string::npos) refType = ObjectRefType::Light;
                else if (arg2Lower.find("mesh") != std::string::npos) refType = ObjectRefType::Mesh;
                else if (arg2Lower.find("camera") != std::string::npos) refType = ObjectRefType::Camera;
                else refType = ObjectRefType::Actor;

                category = trimArg(args[3]);
            }

            // If the variable was declared as GameObject*, all GameObjects from this scene are selectable
            auto itVar = declaredVars.find(refVar);
            if (itVar != declaredVars.end() && (itVar->second.type == "GameObject*" || itVar->second.type == "Actor*")) {
                refType = ObjectRefType::Actor;
            }

            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::ObjectRef;
            def.targetId = -1;
            m_defaultPropertyValues[propName] = def;
            if (!refVar.empty()) m_defaultPropertyValues[refVar] = def;

            const SavedValue* sv = findSaved(propName, refVar);
            void* actualPtr = (sv && sv->hasValue) ? sv->refPtr : nullptr;
            int actualTargetId = (sv && sv->hasValue) ? sv->targetId : -1;

            m_refStorage[propName] = actualPtr;
            if (!refVar.empty()) m_refStorage[refVar] = actualPtr;

            RegisterReference(propName, &m_refStorage[propName], refType, category);
            if (!m_properties.empty()) {
                m_properties.back().targetId = actualTargetId;
            }
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
            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::Float;
            def.floatVal = defF;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            float actualF = (sv && sv->hasValue) ? sv->floatVal : defF;
            m_floatStorage[varName] = actualF;
            RegisterProperty(varName, &m_floatStorage[varName], "General");
            std::string vLower = varName;
            std::transform(vLower.begin(), vLower.end(), vLower.begin(), ::tolower);
            if (vLower.find("speed") != std::string::npos) {
                m_defaultSpeed = actualF;
            }
        } else if (type == "bool") {
            bool defB = (defStr == "true" || defStr == "1");
            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::Bool;
            def.boolVal = defB;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            bool actualB = (sv && sv->hasValue) ? sv->boolVal : defB;
            m_boolStorage[varName] = actualB;
            RegisterProperty(varName, &m_boolStorage[varName], "General");
        } else if (type == "int" || type == "int32_t" || type == "int64_t" || type == "uint32_t" || type == "size_t") {
            int defI = 0;
            try { if (!defStr.empty()) defI = std::stoi(defStr); } catch (...) {}
            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::Int;
            def.intVal = defI;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            int actualI = (sv && sv->hasValue) ? sv->intVal : defI;
            m_intStorage[varName] = actualI;
            RegisterProperty(varName, &m_intStorage[varName], "General");
        } else if (type == "double") {
            double defD = 0.0;
            try {
                std::string clean = defStr;
                if (!clean.empty() && (clean.back() == 'd' || clean.back() == 'D')) clean.pop_back();
                if (!clean.empty()) defD = std::stod(clean);
            } catch (...) {}
            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::Double;
            def.doubleVal = defD;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            double actualD = (sv && sv->hasValue) ? sv->doubleVal : defD;
            m_doubleStorage[varName] = actualD;
            RegisterProperty(varName, &m_doubleStorage[varName], "General");
        } else if (type == "std::string" || type == "string") {
            std::string defS = trimArg(defStr);
            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::String;
            def.stringVal = defS;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            std::string actualS = (sv && sv->hasValue) ? sv->stringVal : defS;
            m_stringStorage[varName] = actualS;
            RegisterProperty(varName, &m_stringStorage[varName], "General");
        } else if (type == "glm::vec2" || type == "Vector2" || type == "vec2") {
            glm::vec2 defVec2(0.0f);
            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::Vec2;
            def.vec2Val = defVec2;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            glm::vec2 actualVec2 = (sv && sv->hasValue) ? sv->vec2Val : defVec2;
            m_vec2Storage[varName] = actualVec2;
            RegisterProperty(varName, &m_vec2Storage[varName], "General");
        } else if (type == "glm::vec3" || type == "Vector3" || type == "vec3") {
            glm::vec3 defVec(0.0f);
            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::Vec3;
            def.vec3Val = defVec;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            glm::vec3 actualVec = (sv && sv->hasValue) ? sv->vec3Val : defVec;
            m_vec3Storage[varName] = actualVec;
            RegisterProperty(varName, &m_vec3Storage[varName], "General");
        } else if (type == "glm::vec4" || type == "Vector4" || type == "vec4") {
            glm::vec4 defVec4(0.0f);
            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::Vec4;
            def.vec4Val = defVec4;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            glm::vec4 actualVec4 = (sv && sv->hasValue) ? sv->vec4Val : defVec4;
            m_vec4Storage[varName] = actualVec4;
            RegisterProperty(varName, &m_vec4Storage[varName], "General");
        } else if (type == "Camera*" || type == "LightComponent*" || type == "PrimitiveMesh*" || type == "GameObject*") {
            ObjectRefType refType = ObjectRefType::Actor;
            if (type == "Camera*") refType = ObjectRefType::Camera;
            else if (type == "LightComponent*") refType = ObjectRefType::Light;
            else if (type == "PrimitiveMesh*") refType = ObjectRefType::Mesh;

            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::ObjectRef;
            def.targetId = -1;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            void* actualPtr = (sv && sv->hasValue) ? sv->refPtr : nullptr;
            int actualTargetId = (sv && sv->hasValue) ? sv->targetId : -1;

            m_refStorage[varName] = actualPtr;
            RegisterReference(varName, &m_refStorage[varName], refType, "References");
            if (!m_properties.empty()) {
                m_properties.back().targetId = actualTargetId;
            }
        }
    }

    // 3. Third pass: scan Start() and Update() bodies for runtime actions
    m_startPrintMessages.clear();
    m_updatePrintMessages.clear();
    m_transformActions.clear();
    m_hasRotation = false;
    m_hasTranslation = false;
    m_rotateAxis = glm::vec3(0.0f, 1.0f, 0.0f);

    struct LocalVarExpr {
        float constantVal = 0.0f;
        bool useDt = false;
        std::string varName;
        float varMultiplier = 1.0f;
        std::string scaleVarName;
        bool isActive = false;
    };
    std::unordered_map<std::string, LocalVarExpr> localVars;

    auto parseArgExpr = [&](const std::string& rawExpr, float& outConst, bool& outUseDt, std::string& outVar, float& outMul, std::string& outScaleVar, bool& outActive) {
        outConst = 0.0f;
        outUseDt = false;
        outVar.clear();
        outMul = 1.0f;
        outScaleVar.clear();
        outActive = false;

        std::string s = rawExpr;
        size_t b = s.find_first_not_of(" \t\r\n");
        size_t e = s.find_last_not_of(" \t\r\n");
        if (b == std::string::npos || e == std::string::npos) return;
        s = s.substr(b, e - b + 1);

        auto itLoc = localVars.find(s);
        if (itLoc != localVars.end()) {
            outConst = itLoc->second.constantVal;
            outUseDt = itLoc->second.useDt;
            outVar = itLoc->second.varName;
            outMul = itLoc->second.varMultiplier;
            outScaleVar = itLoc->second.scaleVarName;
            outActive = itLoc->second.isActive;
            return;
        }

        std::string lowerS = s;
        std::transform(lowerS.begin(), lowerS.end(), lowerS.begin(), ::tolower);

        if (lowerS.find("deltatime") != std::string::npos ||
            lowerS.find("delta_time") != std::string::npos ||
            lowerS.find("worlddeltaseconds") != std::string::npos ||
            lowerS.find("dt") != std::string::npos) {
            outUseDt = true;
        }

        bool isMouseNegated = false;
        if (lowerS.find("getmousedeltax") != std::string::npos || lowerS.find("getmouseaxisx") != std::string::npos) {
            outVar = "$__MouseDeltaX";
            outActive = true;
            if (lowerS.find("(true)") != std::string::npos || lowerS.find("(1)") != std::string::npos || lowerS.find("-i.getmouse") != std::string::npos || lowerS.find("-input.getmouse") != std::string::npos || s.find('-') != std::string::npos) {
                isMouseNegated = true;
            }
        } else if (lowerS.find("getmousedeltay") != std::string::npos || lowerS.find("getmouseaxisy") != std::string::npos) {
            outVar = "$__MouseDeltaY";
            outActive = true;
            if (lowerS.find("(true)") != std::string::npos || lowerS.find("(1)") != std::string::npos || lowerS.find("-i.getmouse") != std::string::npos || lowerS.find("-input.getmouse") != std::string::npos || s.find('-') != std::string::npos) {
                isMouseNegated = true;
            }
        } else if (lowerS.find("getmousex") != std::string::npos) {
            outVar = "$__MouseX";
            outActive = true;
            if (lowerS.find("getmousex(true)") != std::string::npos || lowerS.find("getmousex(1)") != std::string::npos || s.find('-') != std::string::npos) {
                isMouseNegated = true;
            }
        } else if (lowerS.find("getmousey") != std::string::npos) {
            outVar = "$__MouseY";
            outActive = true;
            if (lowerS.find("getmousey(true)") != std::string::npos || lowerS.find("getmousey(1)") != std::string::npos || s.find('-') != std::string::npos) {
                isMouseNegated = true;
            }
        }

        for (const auto& v : declaredVars) {
            std::string vName = v.first;
            if (s.find(vName) != std::string::npos) {
                if (outVar.empty()) {
                    outVar = vName;
                    outActive = true;
                } else if (outVar != vName && outScaleVar.empty()) {
                    outScaleVar = vName;
                }
            }
        }

        bool hasMinusBeforeVar = false;
        if (!outVar.empty()) {
            size_t varPos = s.find(outVar);
            if (varPos != std::string::npos) {
                size_t minusPos = s.rfind('-', varPos);
                if (minusPos != std::string::npos) {
                    bool onlyWs = true;
                    for (size_t k = minusPos + 1; k < varPos; ++k) {
                        if (!std::isspace(static_cast<unsigned char>(s[k])) && s[k] != '(') {
                            onlyWs = false;
                            break;
                        }
                    }
                    if (onlyWs) hasMinusBeforeVar = true;
                }
            }
        }

        float sign = (isMouseNegated ^ hasMinusBeforeVar) ? -1.0f : 1.0f;
        outMul = sign;

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
                        if (std::abs(val) > 1e-6f) outActive = true;
                    } else {
                        outMul = std::abs(val) * sign * (val < 0.0f ? -1.0f : 1.0f);
                    }
                }
            } catch (...) {}
        }

        if (!outVar.empty() || std::abs(outConst) > 1e-6f || outUseDt) {
            outActive = true;
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

        if (str.find("GetMouseX(") != std::string::npos || str.find("GetMouseX()") != std::string::npos) {
            DynamicScriptBehaviour::DynamicKeyCondition cond;
            cond.key = -2;
            conds.push_back(cond);
        }
        if (str.find("GetMouseY(") != std::string::npos || str.find("GetMouseY()") != std::string::npos) {
            DynamicScriptBehaviour::DynamicKeyCondition cond;
            cond.key = -3;
            conds.push_back(cond);
        }

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
            if (l.find("CursorState.Locked") != std::string::npos || l.find("CursorState.Lock") != std::string::npos) {
                m_startLockCursor = true;
            }
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

            // Check for local variable declaration / assignment inside Update()
            size_t eqPos = l.find('=');
            if (eqPos != std::string::npos && l.find("==") == std::string::npos &&
                l.find("!=") == std::string::npos && l.find("<=") == std::string::npos &&
                l.find(">=") == std::string::npos && l.find("if ") == std::string::npos &&
                l.find("if(") == std::string::npos) {
                std::string left = l.substr(0, eqPos);
                std::string right = l.substr(eqPos + 1);
                size_t semi = right.find(';');
                if (semi != std::string::npos) right = right.substr(0, semi);

                size_t lb = left.find_last_not_of(" \t\r\n");
                if (lb != std::string::npos) {
                    size_t la = left.find_last_of(" \t\r\n*", lb);
                    std::string varName = (la == std::string::npos) ? left.substr(0, lb + 1) : left.substr(la + 1, lb - la);
                    LocalVarExpr expr;
                    parseArgExpr(right, expr.constantVal, expr.useDt, expr.varName, expr.varMultiplier, expr.scaleVarName, expr.isActive);
                    localVars[varName] = expr;
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
            bool hasRotate = (l.find("Rotate(") != std::string::npos || l.find("Rotation(") != std::string::npos) && l.find("AutoRotate") == std::string::npos;

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

                size_t arrowPos = l.find("->Transform.");
                if (arrowPos == std::string::npos) arrowPos = l.find("->transform.");
                if (arrowPos == std::string::npos) arrowPos = l.find(".Transform.");
                if (arrowPos == std::string::npos) arrowPos = l.find(".transform.");
                if (arrowPos != std::string::npos) {
                    std::string ref = l.substr(0, arrowPos);
                    size_t rb = ref.find_last_not_of(" \t\r\n;{}()");
                    if (rb != std::string::npos) {
                        size_t ra = ref.find_last_of(" \t\r\n;{}()", rb);
                        action.targetRefName = (ra == std::string::npos) ? ref.substr(0, rb + 1) : ref.substr(ra + 1, rb - ra);
                    }
                }

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
                            parseArgExpr(args[i], action.constantDelta[i], action.useDeltaTime[i], action.varNames[i], action.varMultiplier[i], action.scaleVarNames[i], action.activeAxis[i]);
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

            // 3. Scan for Transform.Rotate / RelativeRotate / WorldRotate / RelativeRotation / WorldRotation
            size_t rotPos = l.find("Rotate(");
            if (rotPos == std::string::npos) rotPos = l.find("Rotation(");
            if (rotPos != std::string::npos && l.find("AutoRotate") == std::string::npos) {
                DynamicTransformAction action;
                action.type = (l.find("WorldRotate(") != std::string::npos || l.find("WorldRotation(") != std::string::npos)
                              ? DynamicTransformAction::ActionType::RotateWorld
                              : DynamicTransformAction::ActionType::RotateRelative;

                size_t arrowPos = l.find("->Transform.");
                if (arrowPos == std::string::npos) arrowPos = l.find("->transform.");
                if (arrowPos == std::string::npos) arrowPos = l.find(".Transform.");
                if (arrowPos == std::string::npos) arrowPos = l.find(".transform.");
                if (arrowPos != std::string::npos) {
                    std::string ref = l.substr(0, arrowPos);
                    size_t rb = ref.find_last_not_of(" \t\r\n;{}()");
                    if (rb != std::string::npos) {
                        size_t ra = ref.find_last_of(" \t\r\n;{}()", rb);
                        action.targetRefName = (ra == std::string::npos) ? ref.substr(0, rb + 1) : ref.substr(ra + 1, rb - ra);
                    }
                }

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
                            parseArgExpr(args[i], action.constantDelta[i], action.useDeltaTime[i], action.varNames[i], action.varMultiplier[i], action.scaleVarNames[i], action.activeAxis[i]);
                        }
                        // If mouse Y is in Z slot and X (pitch) is unassigned, map mouse Y to pitch (X)
                        if (action.varNames[0].empty() && (action.varNames[2] == "$__MouseY" || action.varNames[2] == "$__MouseDeltaY")) {
                            action.varNames[0] = action.varNames[2];
                            action.varMultiplier[0] = action.varMultiplier[2];
                            action.useDeltaTime[0] = action.useDeltaTime[2];
                            action.scaleVarNames[0] = action.scaleVarNames[2];
                            action.activeAxis[0] = action.activeAxis[2];
                            action.activeAxis[2] = false;
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
            } else if (l.find("rotation.y") != std::string::npos || l.find("Rotation.y") != std::string::npos) {
                DynamicTransformAction action;
                action.type = DynamicTransformAction::ActionType::RotateRelative;
                action.activeAxis[1] = true;
                action.varNames[1] = "$__MouseDeltaX";
                action.varMultiplier[1] = (l.find("-=") != std::string::npos || l.find("- deltaX") != std::string::npos || l.find("-deltaX") != std::string::npos || l.find("- mouseDeltaX") != std::string::npos || l.find("-mouseDeltaX") != std::string::npos) ? -1.0f : 1.0f;
                for (const auto& v : declaredVars) {
                    if (l.find(v.first) != std::string::npos) {
                        action.scaleVarNames[1] = v.first;
                        break;
                    }
                }
                m_transformActions.push_back(action);
            } else if (l.find("rotation.x") != std::string::npos || l.find("Rotation.x") != std::string::npos) {
                DynamicTransformAction action;
                action.type = DynamicTransformAction::ActionType::RotateRelative;
                action.targetRefName = "Camera";
                action.activeAxis[0] = true;
                action.varNames[0] = "$__MouseDeltaY";
                action.varMultiplier[0] = (l.find("-=") != std::string::npos || l.find("- deltaY") != std::string::npos || l.find("-deltaY") != std::string::npos || l.find("- mouseDeltaY") != std::string::npos || l.find("-mouseDeltaY") != std::string::npos) ? -1.0f : 1.0f;
                for (const auto& v : declaredVars) {
                    if (l.find(v.first) != std::string::npos) {
                        action.scaleVarNames[0] = v.first;
                        break;
                    }
                }
                m_transformActions.push_back(action);
            }

            // 4. Scan for CursorState.Locked / CursorState.UnLocked
            if (l.find("CursorState.UnLocked") != std::string::npos || l.find("CursorState.Unlock") != std::string::npos) {
                DynamicTransformAction action;
                action.type = DynamicTransformAction::ActionType::UnlockCursor;
                if (!activeKeyConditions.empty()) {
                    action.requiredKeys = activeKeyConditions;
                    if (pendingSingleLineCondition) {
                        activeKeyConditions.clear();
                        pendingSingleLineCondition = false;
                    }
                }
                m_transformActions.push_back(action);
            } else if (l.find("CursorState.Locked") != std::string::npos || l.find("CursorState.Lock") != std::string::npos) {
                DynamicTransformAction action;
                action.type = DynamicTransformAction::ActionType::LockCursor;
                if (!activeKeyConditions.empty()) {
                    action.requiredKeys = activeKeyConditions;
                    if (pendingSingleLineCondition) {
                        activeKeyConditions.clear();
                        pendingSingleLineCondition = false;
                    }
                }
                m_transformActions.push_back(action);
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

    if (m_scene) {
        ResolveReferences(*m_scene);
    }
}

void EunoiaBehaviour::ResetToDefaults() {
    m_properties.clear();
    RegisterProperties();
    if (m_scene) {
        ResolveReferences(*m_scene);
    }
}

void DynamicScriptBehaviour::ResetToDefaults() {
    if (!m_sourceCppPath.empty()) {
        ParsePropertiesFromCpp(m_sourceCppPath, false);
    } else {
        for (auto& prop : m_properties) {
            auto it = m_defaultPropertyValues.find(prop.name);
            if (it == m_defaultPropertyValues.end() || !prop.dataPtr) continue;
            const auto& def = it->second;
            switch (prop.type) {
                case BehaviourPropertyType::Bool:
                    *reinterpret_cast<bool*>(prop.dataPtr) = def.boolVal;
                    break;
                case BehaviourPropertyType::Int:
                    *reinterpret_cast<int*>(prop.dataPtr) = def.intVal;
                    break;
                case BehaviourPropertyType::Int64:
                    *reinterpret_cast<int64_t*>(prop.dataPtr) = def.int64Val;
                    break;
                case BehaviourPropertyType::Float:
                    *reinterpret_cast<float*>(prop.dataPtr) = def.floatVal;
                    break;
                case BehaviourPropertyType::Double:
                    *reinterpret_cast<double*>(prop.dataPtr) = def.doubleVal;
                    break;
                case BehaviourPropertyType::String:
                    *reinterpret_cast<std::string*>(prop.dataPtr) = def.stringVal;
                    break;
                case BehaviourPropertyType::Vec2:
                    *reinterpret_cast<glm::vec2*>(prop.dataPtr) = def.vec2Val;
                    break;
                case BehaviourPropertyType::Vec3:
                case BehaviourPropertyType::Color3:
                    *reinterpret_cast<glm::vec3*>(prop.dataPtr) = def.vec3Val;
                    break;
                case BehaviourPropertyType::Vec4:
                case BehaviourPropertyType::Color4:
                    *reinterpret_cast<glm::vec4*>(prop.dataPtr) = def.vec4Val;
                    break;
                case BehaviourPropertyType::ObjectRef:
                    *reinterpret_cast<void**>(prop.dataPtr) = nullptr;
                    prop.targetId = -1;
                    prop.isMissing = false;
                    break;
            }
        }
        if (m_scene) {
            ResolveReferences(*m_scene);
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

// ============================================================================
// Mouse Input Helpers (Docs/Behaviours/InputSystem.md)
// ============================================================================

double EunoiaBehaviour::GetMouseX(bool negate) const {
    return InputSystem::Get().GetMouseX(negate);
}

double EunoiaBehaviour::GetMouseY(bool negate) const {
    return InputSystem::Get().GetMouseY(negate);
}

double EunoiaBehaviour::GetMouseDeltaX(bool negate) const {
    return InputSystem::Get().GetMouseDeltaX(negate);
}

double EunoiaBehaviour::GetMouseDeltaY(bool negate) const {
    return InputSystem::Get().GetMouseDeltaY(negate);
}

