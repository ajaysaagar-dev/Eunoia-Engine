#include "CharacterMechanics/CharacterMechanicsSystem.h"
#include "CharacterMechanics/CharacterMechanics.h"
#include <EngineScene/GameObject.h>
#include <EngineScene/EunoiaBehaviour.h>
#include <iostream>
#include <cmath>

namespace Eunoia {
namespace CharacterMechanics {

CharacterMechanicsSystem& CharacterMechanicsSystem::Get() {
    static CharacterMechanicsSystem s_instance;
    return s_instance;
}

void CharacterMechanicsSystem::Initialize() {
    m_velocityTrackers.clear();
    std::cout << "[CharacterMechanics] System initialized.\n";
}

void CharacterMechanicsSystem::Shutdown() {
    m_activeCharacter = nullptr;
    m_velocityTrackers.clear();
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

} // namespace CharacterMechanics
} // namespace Eunoia
