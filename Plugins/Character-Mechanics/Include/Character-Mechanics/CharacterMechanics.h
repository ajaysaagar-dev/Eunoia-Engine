#pragma once
#include <Character-Mechanics/CharacterMechanicsSystem.h>
#include <Character-Mechanics/ICharacterMechanicsService.h>
#include <EngineScene/TransformTypes.h>

struct GameObject;
class EunoiaBehaviour;

namespace Eunoia {
namespace CharacterMechanics {

// Handle returned by AssumeAsCharacter()
struct CharacterMechanicsHandle {
    GameObject* target = nullptr;
    EunoiaBehaviour* behaviour = nullptr;

    void AddMovement(float speed, const glm::vec3& worldDir, bool negate = false);
    void AddMovement(float speed, const TransformVector3& worldDir, bool negate = false);
    void AddMovement(float speed, float dirX, float dirY, float dirZ, bool negate = false);

    // GetVelocity APIs on handle
    float GetVelocity(const glm::vec3& directionMask = glm::vec3(1.0f, 1.0f, 1.0f));
    float GetVelocity(const TransformVector3& directionMask);
    float GetVelocity(float dirX, float dirY, float dirZ);
    glm::vec3 GetVelocityVector(const glm::vec3& directionMask = glm::vec3(1.0f, 1.0f, 1.0f));
};

// Static & instance proxy for CharacterMechanics
struct CharacterMechanicsProxy {
    static void AddMovement(float speed, const glm::vec3& worldDir, bool negate = false) {
        CharacterMechanicsSystem::Get().AddMovement(speed, worldDir, negate);
    }
    static void AddMovement(float speed, const TransformVector3& worldDir, bool negate = false) {
        CharacterMechanicsSystem::Get().AddMovement(speed, worldDir, negate);
    }
    static void AddMovement(float speed, float dirX, float dirY, float dirZ, bool negate = false) {
        CharacterMechanicsSystem::Get().AddMovement(speed, dirX, dirY, dirZ, negate);
    }

    static void AddMovement(GameObject* target, float speed, const glm::vec3& worldDir, bool negate = false) {
        CharacterMechanicsSystem::Get().AddMovement(target, speed, worldDir, negate);
    }
    static void AddMovement(GameObject* target, float speed, const TransformVector3& worldDir, bool negate = false) {
        CharacterMechanicsSystem::Get().AddMovement(target, speed, worldDir, negate);
    }
    static void AddMovement(GameObject* target, float speed, float dirX, float dirY, float dirZ, bool negate = false) {
        CharacterMechanicsSystem::Get().AddMovement(target, speed, dirX, dirY, dirZ, negate);
    }

    // EngineSystem::CharacterMechanics::GetVelocity(vector3) APIs:
    static float GetVelocity(const glm::vec3& directionMask = glm::vec3(1.0f, 1.0f, 1.0f)) {
        return CharacterMechanicsSystem::Get().GetVelocity(directionMask);
    }
    static float GetVelocity(const TransformVector3& directionMask) {
        return CharacterMechanicsSystem::Get().GetVelocity(directionMask);
    }
    static float GetVelocity(float dirX, float dirY, float dirZ) {
        return CharacterMechanicsSystem::Get().GetVelocity(dirX, dirY, dirZ);
    }

    static float GetVelocity(GameObject* target, const glm::vec3& directionMask = glm::vec3(1.0f, 1.0f, 1.0f)) {
        return CharacterMechanicsSystem::Get().GetVelocity(target, directionMask);
    }
    static float GetVelocity(GameObject* target, const TransformVector3& directionMask) {
        return CharacterMechanicsSystem::Get().GetVelocity(target, directionMask);
    }
    static float GetVelocity(GameObject* target, float dirX, float dirY, float dirZ) {
        return CharacterMechanicsSystem::Get().GetVelocity(target, dirX, dirY, dirZ);
    }

    static glm::vec3 GetVelocityVector(const glm::vec3& directionMask = glm::vec3(1.0f, 1.0f, 1.0f)) {
        return CharacterMechanicsSystem::Get().GetVelocityVector(nullptr, directionMask);
    }
    static glm::vec3 GetVelocityVector(GameObject* target, const glm::vec3& directionMask = glm::vec3(1.0f, 1.0f, 1.0f)) {
        return CharacterMechanicsSystem::Get().GetVelocityVector(target, directionMask);
    }
};

} // namespace CharacterMechanics
} // namespace Eunoia

// Top-level EngineSystem struct and global aliases matching user request:
// EngineSystem::CharacterMechanics::AddMovement(...)
// CharacterMechanics::AddMovement(...)
// AssumeAsCharacter().AddMovement(...)
struct EngineSystem {
    using CharacterMechanics = Eunoia::CharacterMechanics::CharacterMechanicsProxy;
};

using CharacterMechanics = Eunoia::CharacterMechanics::CharacterMechanicsProxy;
