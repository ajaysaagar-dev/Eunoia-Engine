#include "CharacterMechanics/CharacterMechanicsSystem.h"
#include "CharacterMechanics/CharacterMechanics.h"
#include <EngineScene/GameObject.h>
#include <EngineScene/EunoiaBehaviour.h>
#include <EngineScene/Scene.h>
#include <iostream>
#include <cmath>
#include <algorithm>

namespace Eunoia {
namespace CharacterMechanics {

CharacterMechanicsSystem& CharacterMechanicsSystem::Get() {
    static CharacterMechanicsSystem s_instance;
    return s_instance;
}

void CharacterMechanicsSystem::Initialize() {
    m_velocityTrackers.clear();
    m_settings.clear();
    std::cout << "[CharacterMechanics] System initialized.\n";
}

void CharacterMechanicsSystem::Shutdown() {
    m_activeCharacter = nullptr;
    m_velocityTrackers.clear();
    m_settings.clear();
    std::cout << "[CharacterMechanics] System shutdown.\n";
}

void CharacterMechanicsSystem::SetActiveCharacter(GameObject* character) {
    m_activeCharacter = character;
}

GameObject* CharacterMechanicsSystem::GetActiveCharacter() const {
    return m_activeCharacter;
}

void CharacterMechanicsSystem::AddMovement(GameObject* target, float speed, const glm::vec3& worldDirection, bool negate) {
    GameObject* character = target ? target : m_activeCharacter;
    if (!character) return;

    float len = glm::length(worldDirection);
    if (len < 1e-6f) return;

    glm::vec3 normDir = worldDirection / len;
    float sign = negate ? -1.0f : 1.0f;
    glm::vec3 delta = normDir * (speed * sign);

    character->Transform.WorldTranslate(delta);
}

void CharacterMechanicsSystem::AddMovement(GameObject* target, float speed, const TransformVector3& worldDirection, bool negate) {
    AddMovement(target, speed, glm::vec3(worldDirection.x, worldDirection.y, worldDirection.z), negate);
}

void CharacterMechanicsSystem::AddMovement(GameObject* target, float speed, float dirX, float dirY, float dirZ, bool negate) {
    AddMovement(target, speed, glm::vec3(dirX, dirY, dirZ), negate);
}

void CharacterMechanicsSystem::AddMovement(float speed, const glm::vec3& worldDirection, bool negate) {
    AddMovement(nullptr, speed, worldDirection, negate);
}

void CharacterMechanicsSystem::AddMovement(float speed, const TransformVector3& worldDirection, bool negate) {
    AddMovement(nullptr, speed, worldDirection, negate);
}

void CharacterMechanicsSystem::AddMovement(float speed, float dirX, float dirY, float dirZ, bool negate) {
    AddMovement(nullptr, speed, dirX, dirY, dirZ, negate);
}

void CharacterMechanicsSystem::UpdateObjectVelocity(GameObject* target, float dt) {
    GameObject* character = target ? target : m_activeCharacter;
    if (!character) return;

    int id = (character->id != 0) ? character->id : static_cast<int>(reinterpret_cast<uintptr_t>(character) & 0x7FFFFFFF);
    auto& tracker = m_velocityTrackers[id];

    glm::vec3 curPos = character->GetWorldLocation();
    auto now = std::chrono::steady_clock::now();

    if (!tracker.initialized) {
        tracker.lastWorldPosition = curPos;
        tracker.currentWorldPosition = curPos;
        tracker.currentVelocity = glm::vec3(0.0f);
        tracker.lastSampleTime = now;
        tracker.lastDeltaTime = (dt > 1e-5f) ? dt : ((::WorldDeltaSeconds > 1e-5f) ? ::WorldDeltaSeconds : 0.01667f);
        tracker.initialized = true;
        return;
    }

    float elapsed = std::chrono::duration<float>(now - tracker.lastSampleTime).count();
    float effectiveDt = (dt > 1e-5f) ? dt : ((::WorldDeltaSeconds > 1e-5f) ? ::WorldDeltaSeconds : elapsed);
    if (effectiveDt < 1e-5f) effectiveDt = (tracker.lastDeltaTime > 1e-5f) ? tracker.lastDeltaTime : 0.01667f;

    glm::vec3 displacement = curPos - tracker.lastWorldPosition;
    tracker.currentVelocity = displacement / effectiveDt;
    tracker.lastWorldPosition = curPos;
    tracker.currentWorldPosition = curPos;
    tracker.lastSampleTime = now;
    tracker.lastDeltaTime = effectiveDt;
}

void CharacterMechanicsSystem::ClearTrackers() {
    m_velocityTrackers.clear();
}

float CharacterMechanicsSystem::GetVelocity(GameObject* target, const glm::vec3& directionMask) {
    GameObject* character = target ? target : m_activeCharacter;
    if (!character) return 0.0f;

    int id = (character->id != 0) ? character->id : static_cast<int>(reinterpret_cast<uintptr_t>(character) & 0x7FFFFFFF);
    auto& tracker = m_velocityTrackers[id];

    glm::vec3 curPos = character->GetWorldLocation();
    auto now = std::chrono::steady_clock::now();

    if (!tracker.initialized) {
        tracker.lastWorldPosition = curPos;
        tracker.currentWorldPosition = curPos;
        tracker.currentVelocity = glm::vec3(0.0f);
        tracker.lastSampleTime = now;
        tracker.lastDeltaTime = (::WorldDeltaSeconds > 1e-5f) ? ::WorldDeltaSeconds : 0.01667f;
        tracker.initialized = true;
        return 0.0f;
    }

    float elapsed = std::chrono::duration<float>(now - tracker.lastSampleTime).count();
    glm::vec3 posDelta = curPos - tracker.currentWorldPosition;

    if (elapsed >= 0.002f || glm::length(posDelta) > 1e-6f) {
        float effectiveDt = (::WorldDeltaSeconds > 1e-5f) ? ::WorldDeltaSeconds : ((elapsed > 1e-5f) ? elapsed : tracker.lastDeltaTime);
        if (effectiveDt < 1e-5f) effectiveDt = 0.01667f;

        glm::vec3 displacement = curPos - tracker.lastWorldPosition;
        tracker.currentVelocity = displacement / effectiveDt;
        tracker.lastWorldPosition = curPos;
        tracker.currentWorldPosition = curPos;
        tracker.lastSampleTime = now;
        tracker.lastDeltaTime = effectiveDt;
    }

    glm::vec3 filtered(0.0f);
    if (std::abs(directionMask.x) > 0.001f) filtered.x = tracker.currentVelocity.x;
    if (std::abs(directionMask.y) > 0.001f) filtered.y = tracker.currentVelocity.y;
    if (std::abs(directionMask.z) > 0.001f) filtered.z = tracker.currentVelocity.z;

    return glm::length(filtered);
}

float CharacterMechanicsSystem::GetVelocity(GameObject* target, const TransformVector3& directionMask) {
    return GetVelocity(target, glm::vec3(directionMask.x, directionMask.y, directionMask.z));
}

float CharacterMechanicsSystem::GetVelocity(GameObject* target, float dirX, float dirY, float dirZ) {
    return GetVelocity(target, glm::vec3(dirX, dirY, dirZ));
}

float CharacterMechanicsSystem::GetVelocity(const glm::vec3& directionMask) {
    return GetVelocity(nullptr, directionMask);
}

float CharacterMechanicsSystem::GetVelocity(const TransformVector3& directionMask) {
    return GetVelocity(nullptr, glm::vec3(directionMask.x, directionMask.y, directionMask.z));
}

float CharacterMechanicsSystem::GetVelocity(float dirX, float dirY, float dirZ) {
    return GetVelocity(nullptr, glm::vec3(dirX, dirY, dirZ));
}

glm::vec3 CharacterMechanicsSystem::GetVelocityVector(GameObject* target, const glm::vec3& directionMask) {
    GameObject* character = target ? target : m_activeCharacter;
    if (!character) return glm::vec3(0.0f);

    GetVelocity(character, directionMask);

    int id = (character->id != 0) ? character->id : static_cast<int>(reinterpret_cast<uintptr_t>(character) & 0x7FFFFFFF);
    auto it = m_velocityTrackers.find(id);
    if (it == m_velocityTrackers.end()) return glm::vec3(0.0f);

    glm::vec3 filtered(0.0f);
    if (std::abs(directionMask.x) > 0.001f) filtered.x = it->second.currentVelocity.x;
    if (std::abs(directionMask.y) > 0.001f) filtered.y = it->second.currentVelocity.y;
    if (std::abs(directionMask.z) > 0.001f) filtered.z = it->second.currentVelocity.z;

    return filtered;
}

// Orientation to Movement Direction APIs
void CharacterMechanicsSystem::SetOrientationToMovementDirection(GameObject* target, bool enable, float rotationRate) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) {
        int id = (character->id != 0) ? character->id : static_cast<int>(reinterpret_cast<uintptr_t>(character) & 0x7FFFFFFF);
        auto& s = m_settings[id];
        s.orientToMovement = enable;
        s.bodyRotateToMovement = enable;
        s.rotationRate = rotationRate;
        s.bodyRotationRate = rotationRate;
        if (!s.hasBaseOffset && s.bodyObject) {
            s.baseYawOffset = s.bodyObject->rotation.y;
            s.hasBaseOffset = true;
        }
    }
    // Also save in m_settings[0] as global/default fallback
    auto& fallback = m_settings[0];
    fallback.orientToMovement = enable;
    fallback.bodyRotateToMovement = enable;
    fallback.rotationRate = rotationRate;
    fallback.bodyRotationRate = rotationRate;
}

void CharacterMechanicsSystem::SetOrientationToMovementDirection(bool enable, float rotationRate) {
    SetOrientationToMovementDirection(nullptr, enable, rotationRate);
}

bool CharacterMechanicsSystem::IsOrientationToMovementDirectionEnabled(GameObject* target) const {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) {
        int id = (character->id != 0) ? character->id : static_cast<int>(reinterpret_cast<uintptr_t>(character) & 0x7FFFFFFF);
        auto it = m_settings.find(id);
        if (it != m_settings.end()) {
            return it->second.orientToMovement || it->second.bodyRotateToMovement;
        }
    }
    auto it0 = m_settings.find(0);
    if (it0 != m_settings.end()) {
        return it0->second.orientToMovement || it0->second.bodyRotateToMovement;
    }
    return false;
}

float CharacterMechanicsSystem::GetRotationRate(GameObject* target) const {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) {
        int id = (character->id != 0) ? character->id : static_cast<int>(reinterpret_cast<uintptr_t>(character) & 0x7FFFFFFF);
        auto it = m_settings.find(id);
        if (it != m_settings.end()) {
            return it->second.rotationRate;
        }
    }
    auto it0 = m_settings.find(0);
    if (it0 != m_settings.end()) {
        return it0->second.rotationRate;
    }
    return 720.0f;
}

void CharacterMechanicsSystem::OrientToDirection(GameObject* target, const glm::vec3& worldDirection, float dt) {
    GameObject* character = target ? target : m_activeCharacter;
    if (!character) return;

    if (std::abs(worldDirection.x) < 1e-4f && std::abs(worldDirection.z) < 1e-4f) {
        return;
    }

    // Check if orientation is enabled on character, global fallback, or active character
    bool isEnabled = IsOrientationToMovementDirectionEnabled(character) ||
                     IsBodyRotateToMovementDirectionEnabled(character) ||
                     IsOrientationToMovementDirectionEnabled(nullptr) ||
                     IsBodyRotateToMovementDirectionEnabled(nullptr);

    // Resolve designated body object
    GameObject* body = GetBodyObject(character);
    if (!body || body == character) {
        // Auto-detect body object from child objects (e.g. Remy, PlayerBody, mesh child)
        if (character->scene) {
            for (int childId : character->childIds) {
                GameObject* child = character->scene->FindObject(childId);
                if (child) {
                    std::string cName = child->name;
                    std::transform(cName.begin(), cName.end(), cName.begin(), ::tolower);
                    if (cName.find("body") != std::string::npos || cName.find("remy") != std::string::npos ||
                        child->isImportedMesh || !child->meshFilePath.empty()) {
                        body = child;
                        SetBodyObject(character, body);
                        break;
                    }
                }
            }
            if (!body || body == character) {
                for (auto& obj : character->scene->objects) {
                    if (obj.parentId == character->id) {
                        std::string oName = obj.name;
                        std::transform(oName.begin(), oName.end(), oName.begin(), ::tolower);
                        if (oName.find("body") != std::string::npos || oName.find("remy") != std::string::npos ||
                            obj.isImportedMesh || !obj.meshFilePath.empty()) {
                            body = &obj;
                            SetBodyObject(character, body);
                            break;
                        }
                    }
                }
            }
        }
    }

    if (body && body != character) {
        if (IsOrientationToMovementDirectionEnabled(body) || IsBodyRotateToMovementDirectionEnabled(body)) {
            isEnabled = true;
        }
    }

    if (!isEnabled) return;

    GameObject* targetObj = body ? body : character;
    if (!targetObj) return;

    // Standard forward vector in Eunoia is (0, 0, -1) at yaw 0
    float moveYaw = glm::degrees(std::atan2(-worldDirection.x, -worldDirection.z));

    // Base yaw offset (e.g. Mixamo characters modeled facing +Z having initial rotation 180)
    int cId = (character->id != 0) ? character->id : static_cast<int>(reinterpret_cast<uintptr_t>(character) & 0x7FFFFFFF);
    auto& s = m_settings[cId];
    if (!s.hasBaseOffset) {
        s.baseYawOffset = targetObj->rotation.y;
        s.hasBaseOffset = true;
    }
    float baseYaw = s.baseYawOffset;
    float targetWorldYaw = moveYaw + baseYaw;

    // Calculate parent's accumulated world yaw
    float parentWorldYaw = 0.0f;
    Scene* scene = targetObj->scene ? targetObj->scene : character->scene;
    if (scene && targetObj->parentId != -1) {
        const GameObject* p = scene->FindObjectConst(targetObj->parentId);
        while (p) {
            parentWorldYaw += p->rotation.y;
            p = (p->parentId != -1) ? scene->FindObjectConst(p->parentId) : nullptr;
        }
    }

    float targetLocalYaw = targetWorldYaw - parentWorldYaw;
    float currentLocalYaw = targetObj->rotation.y;
    float diff = std::fmod(targetLocalYaw - currentLocalYaw + 540.0f, 360.0f) - 180.0f;

    float rate = GetBodyRotationRate(character);
    if (rate <= 0.0f) rate = GetRotationRate(character);
    if (rate <= 0.0f && body && body != character) rate = GetBodyRotationRate(body);
    if (rate <= 0.0f) rate = 720.0f;

    float effectiveDt = (dt > 1e-5f) ? dt : ((::WorldDeltaSeconds > 1e-5f) ? ::WorldDeltaSeconds : 0.01667f);
    float maxStep = rate * effectiveDt;

    if (rate <= 0.0f || std::abs(diff) <= maxStep) {
        targetObj->rotation.y = targetLocalYaw;
    } else {
        targetObj->rotation.y += (diff > 0.0f ? maxStep : -maxStep);
    }

    targetObj->rotation.y = std::fmod(targetObj->rotation.y + 180.0f, 360.0f);
    if (targetObj->rotation.y < 0.0f) targetObj->rotation.y += 360.0f;
    targetObj->rotation.y -= 180.0f;

    // Lock pitch and roll strictly to 0 for body orientation
    targetObj->rotation.x = 0.0f;
    targetObj->rotation.z = 0.0f;

    if (targetObj->hasPhysics && targetObj->physics.isCharacterPhysics) {
        targetObj->rotation.x = 0.0f;
        targetObj->rotation.z = 0.0f;
    }
}

void CharacterMechanicsSystem::SetBodyRotateToMovementDirection(bool enable, GameObject* target, float rotationRate) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) {
        int id = (character->id != 0) ? character->id : static_cast<int>(reinterpret_cast<uintptr_t>(character) & 0x7FFFFFFF);
        auto& s = m_settings[id];
        s.bodyRotateToMovement = enable;
        s.orientToMovement = enable;
        s.bodyRotationRate = rotationRate;
        s.rotationRate = rotationRate;
        if (!s.bodyObject) {
            s.bodyObject = character;
        }
        if (!s.hasBaseOffset && s.bodyObject) {
            s.baseYawOffset = s.bodyObject->rotation.y;
            s.hasBaseOffset = true;
        }
    }
    // Also save in m_settings[0] as global/default fallback
    auto& fallback = m_settings[0];
    fallback.bodyRotateToMovement = enable;
    fallback.orientToMovement = enable;
    fallback.bodyRotationRate = rotationRate;
    fallback.rotationRate = rotationRate;
    if (character && !fallback.bodyObject) {
        fallback.bodyObject = character;
    }
    if (!fallback.hasBaseOffset && fallback.bodyObject) {
        fallback.baseYawOffset = fallback.bodyObject->rotation.y;
        fallback.hasBaseOffset = true;
    }
}

void CharacterMechanicsSystem::SetBodyRotateToMovementDirection(GameObject* target, bool enable, float rotationRate) {
    SetBodyRotateToMovementDirection(enable, target, rotationRate);
}

bool CharacterMechanicsSystem::IsBodyRotateToMovementDirectionEnabled(GameObject* target) const {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) {
        int id = (character->id != 0) ? character->id : static_cast<int>(reinterpret_cast<uintptr_t>(character) & 0x7FFFFFFF);
        auto it = m_settings.find(id);
        if (it != m_settings.end()) {
            return it->second.bodyRotateToMovement || it->second.orientToMovement;
        }
    }
    auto it0 = m_settings.find(0);
    if (it0 != m_settings.end()) {
        return it0->second.bodyRotateToMovement || it0->second.orientToMovement;
    }
    return false;
}

float CharacterMechanicsSystem::GetBodyRotationRate(GameObject* target) const {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) {
        int id = (character->id != 0) ? character->id : static_cast<int>(reinterpret_cast<uintptr_t>(character) & 0x7FFFFFFF);
        auto it = m_settings.find(id);
        if (it != m_settings.end()) {
            return it->second.bodyRotationRate;
        }
    }
    auto it0 = m_settings.find(0);
    if (it0 != m_settings.end()) {
        return it0->second.bodyRotationRate;
    }
    return 720.0f;
}

GameObject* CharacterMechanicsSystem::GetBodyObject(GameObject* target) const {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) {
        int id = (character->id != 0) ? character->id : static_cast<int>(reinterpret_cast<uintptr_t>(character) & 0x7FFFFFFF);
        auto it = m_settings.find(id);
        if (it != m_settings.end() && it->second.bodyObject) {
            return it->second.bodyObject;
        }
    }
    auto it0 = m_settings.find(0);
    if (it0 != m_settings.end() && it0->second.bodyObject) {
        return it0->second.bodyObject;
    }
    return character;
}

void CharacterMechanicsSystem::SetBodyObject(GameObject* target, GameObject* body) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) {
        int id = (character->id != 0) ? character->id : static_cast<int>(reinterpret_cast<uintptr_t>(character) & 0x7FFFFFFF);
        m_settings[id].bodyObject = body;
        if (body && !m_settings[id].hasBaseOffset) {
            m_settings[id].baseYawOffset = body->rotation.y;
            m_settings[id].hasBaseOffset = true;
        }
    }
    m_settings[0].bodyObject = body;
    if (body && !m_settings[0].hasBaseOffset) {
        m_settings[0].baseYawOffset = body->rotation.y;
        m_settings[0].hasBaseOffset = true;
    }
    if (body) {
        int bId = (body->id != 0) ? body->id : static_cast<int>(reinterpret_cast<uintptr_t>(body) & 0x7FFFFFFF);
        m_settings[bId].bodyObject = body;
        if (!m_settings[bId].hasBaseOffset) {
            m_settings[bId].baseYawOffset = body->rotation.y;
            m_settings[bId].hasBaseOffset = true;
        }
    }
}

void CharacterMechanicsSystem::SetBodyObject(GameObject* body) {
    SetBodyObject(nullptr, body);
}

float CharacterMechanicsSystem::GetBaseYawOffset(GameObject* target) const {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) {
        int id = (character->id != 0) ? character->id : static_cast<int>(reinterpret_cast<uintptr_t>(character) & 0x7FFFFFFF);
        auto it = m_settings.find(id);
        if (it != m_settings.end()) {
            return it->second.baseYawOffset;
        }
    }
    auto it0 = m_settings.find(0);
    if (it0 != m_settings.end()) {
        return it0->second.baseYawOffset;
    }
    return 0.0f;
}

void CharacterMechanicsSystem::SetBaseYawOffset(GameObject* target, float offset) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) {
        int id = (character->id != 0) ? character->id : static_cast<int>(reinterpret_cast<uintptr_t>(character) & 0x7FFFFFFF);
        m_settings[id].baseYawOffset = offset;
        m_settings[id].hasBaseOffset = true;
    }
    m_settings[0].baseYawOffset = offset;
    m_settings[0].hasBaseOffset = true;
}

// CharacterMechanicsHandle implementations
void CharacterMechanicsHandle::AddMovement(float speed, const glm::vec3& worldDir, bool negate) {
    if (behaviour && !target) {
        target = behaviour->GetOwner();
    }
    CharacterMechanicsSystem::Get().AddMovement(target, speed, worldDir, negate);
}

void CharacterMechanicsHandle::AddMovement(float speed, const TransformVector3& worldDir, bool negate) {
    AddMovement(speed, glm::vec3(worldDir.x, worldDir.y, worldDir.z), negate);
}

void CharacterMechanicsHandle::AddMovement(float speed, float dirX, float dirY, float dirZ, bool negate) {
    AddMovement(speed, glm::vec3(dirX, dirY, dirZ), negate);
}

float CharacterMechanicsHandle::GetVelocity(const glm::vec3& directionMask) {
    if (behaviour && !target) {
        target = behaviour->GetOwner();
    }
    return CharacterMechanicsSystem::Get().GetVelocity(target, directionMask);
}

float CharacterMechanicsHandle::GetVelocity(const TransformVector3& directionMask) {
    return GetVelocity(glm::vec3(directionMask.x, directionMask.y, directionMask.z));
}

float CharacterMechanicsHandle::GetVelocity(float dirX, float dirY, float dirZ) {
    return GetVelocity(glm::vec3(dirX, dirY, dirZ));
}

glm::vec3 CharacterMechanicsHandle::GetVelocityVector(const glm::vec3& directionMask) {
    if (behaviour && !target) {
        target = behaviour->GetOwner();
    }
    return CharacterMechanicsSystem::Get().GetVelocityVector(target, directionMask);
}

void CharacterMechanicsHandle::SetOrientationToMovementDirection(bool enable, float rotationRate) {
    if (behaviour && !target) {
        target = behaviour->GetOwner();
    }
    CharacterMechanicsSystem::Get().SetOrientationToMovementDirection(target, enable, rotationRate);
}

void CharacterMechanicsHandle::SetBodyRotateToMovementDirection(bool enable, GameObject* body, float rotationRate) {
    if (behaviour && !target) {
        target = behaviour->GetOwner();
    }
    if (body) {
        CharacterMechanicsSystem::Get().SetBodyObject(target, body);
        CharacterMechanicsSystem::Get().SetBodyRotateToMovementDirection(enable, target, rotationRate);
        CharacterMechanicsSystem::Get().SetBodyRotateToMovementDirection(enable, body, rotationRate);
    } else {
        CharacterMechanicsSystem::Get().SetBodyRotateToMovementDirection(enable, target, rotationRate);
    }
}

void CharacterMechanicsHandle::SetBodyRotateToMovementDirection(GameObject* body, bool enable, float rotationRate) {
    SetBodyRotateToMovementDirection(enable, body, rotationRate);
}

// ============================================================================
// Locomotion & State APIs (Docs/Behaviuor.md Section 7)
// ============================================================================

void CharacterMechanicsSystem::Jump(GameObject* target) {
    GameObject* character = target ? target : m_activeCharacter;
    if (!character) return;
    auto& s = m_settings[character->id];
    if (s.isGrounded || s.coyoteTime > 0.0f) {
        s.isJumping = true;
        s.isGrounded = false;
        s.isFalling = false;
        // Launch upwards according to jumpHeight (v = sqrt(2 * g * h))
        float jumpVel = std::sqrt(2.0f * 9.81f * s.gravityScale * s.jumpHeight);
        character->Transform.WorldTranslate(0.0f, jumpVel * (1.0f / 60.0f), 0.0f);
    }
}

void CharacterMechanicsSystem::SetJumpHeight(float height, GameObject* target) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) m_settings[character->id].jumpHeight = height;
}

void CharacterMechanicsSystem::SetMaxWalkSpeed(float speed, GameObject* target) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) m_settings[character->id].maxWalkSpeed = speed;
}

void CharacterMechanicsSystem::SetMaxRunSpeed(float speed, GameObject* target) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) m_settings[character->id].maxRunSpeed = speed;
}

void CharacterMechanicsSystem::SetAcceleration(float accel, GameObject* target) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) m_settings[character->id].acceleration = accel;
}

void CharacterMechanicsSystem::SetDeceleration(float decel, GameObject* target) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) m_settings[character->id].deceleration = decel;
}

void CharacterMechanicsSystem::SetAirControl(float control, GameObject* target) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) m_settings[character->id].airControl = std::clamp(control, 0.0f, 1.0f);
}

void CharacterMechanicsSystem::SetGravityScale(float scale, GameObject* target) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) m_settings[character->id].gravityScale = scale;
}

void CharacterMechanicsSystem::Launch(const glm::vec3& launchVel, bool overrideXY, bool overrideZ, GameObject* target) {
    GameObject* character = target ? target : m_activeCharacter;
    if (!character) return;
    (void)overrideXY; (void)overrideZ;
    character->Transform.WorldTranslate(launchVel * (1.0f / 60.0f));
}

void CharacterMechanicsSystem::AddMovementInput(const glm::vec3& worldDirection, float scale, GameObject* target) {
    GameObject* character = target ? target : m_activeCharacter;
    if (!character) return;
    auto& s = m_settings[character->id];
    float speed = (s.maxWalkSpeed > 0.0f ? s.maxWalkSpeed : 6.0f) * scale;
    AddMovement(character, speed * (1.0f / 60.0f), worldDirection, false);
}

bool CharacterMechanicsSystem::IsGrounded(GameObject* target) const {
    GameObject* character = target ? target : m_activeCharacter;
    if (!character) return true;
    auto it = m_settings.find(character->id);
    return (it != m_settings.end()) ? it->second.isGrounded : true;
}

bool CharacterMechanicsSystem::IsFalling(GameObject* target) const {
    GameObject* character = target ? target : m_activeCharacter;
    if (!character) return false;
    auto it = m_settings.find(character->id);
    return (it != m_settings.end()) ? it->second.isFalling : false;
}

bool CharacterMechanicsSystem::IsJumping(GameObject* target) const {
    GameObject* character = target ? target : m_activeCharacter;
    if (!character) return false;
    auto it = m_settings.find(character->id);
    return (it != m_settings.end()) ? it->second.isJumping : false;
}

bool CharacterMechanicsSystem::IsCrouching(GameObject* target) const {
    GameObject* character = target ? target : m_activeCharacter;
    if (!character) return false;
    auto it = m_settings.find(character->id);
    return (it != m_settings.end()) ? it->second.isCrouching : false;
}

glm::vec3 CharacterMechanicsSystem::GetGroundNormal(GameObject* target) const {
    GameObject* character = target ? target : m_activeCharacter;
    if (!character) return glm::vec3(0.0f, 1.0f, 0.0f);
    auto it = m_settings.find(character->id);
    return (it != m_settings.end()) ? it->second.groundNormal : glm::vec3(0.0f, 1.0f, 0.0f);
}

GameObject* CharacterMechanicsSystem::GetGroundObject(GameObject* target) const {
    GameObject* character = target ? target : m_activeCharacter;
    if (!character) return nullptr;
    auto it = m_settings.find(character->id);
    return (it != m_settings.end()) ? it->second.groundObject : nullptr;
}

void CharacterMechanicsSystem::SetCoyoteTime(float time, GameObject* target) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) m_settings[character->id].coyoteTime = time;
}

void CharacterMechanicsSystem::SetJumpBuffer(float buffer, GameObject* target) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) m_settings[character->id].jumpBuffer = buffer;
}

void CharacterMechanicsSystem::SetStepHeight(float height, GameObject* target) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) m_settings[character->id].stepHeight = height;
}

void CharacterMechanicsSystem::SetSlopeLimit(float limit, GameObject* target) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) m_settings[character->id].slopeLimit = limit;
}

void CharacterMechanicsSystem::Crouch(bool enable, GameObject* target) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) m_settings[character->id].isCrouching = enable;
}

void CharacterMechanicsSystem::SetCrouchHeight(float height, GameObject* target) {
    GameObject* character = target ? target : m_activeCharacter;
    if (character) m_settings[character->id].crouchHeight = height;
}

float CharacterMechanicsSystem::GetSpeed(GameObject* target) {
    return GetVelocity(target, glm::vec3(1.0f, 1.0f, 1.0f));
}

} // namespace CharacterMechanics
} // namespace Eunoia
