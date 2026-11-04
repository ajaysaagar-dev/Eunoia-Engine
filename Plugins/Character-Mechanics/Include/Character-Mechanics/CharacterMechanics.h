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

    void SetOrientationToMovementDirection(bool enable, float rotationRate = 720.0f);
    void SetBodyRotateToMovementDirection(bool enable, GameObject* body = nullptr, float rotationRate = 720.0f);
    void SetBodyRotateToMovementDirection(GameObject* body, bool enable, float rotationRate = 720.0f);
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

    // Orientation to Movement Direction APIs:
    static void SetOrientationToMovementDirection(bool enable, float rotationRate = 720.0f) {
        CharacterMechanicsSystem::Get().SetOrientationToMovementDirection(enable, rotationRate);
    }
    static void SetOrientationToMovementDirection(GameObject* target, bool enable, float rotationRate = 720.0f) {
        CharacterMechanicsSystem::Get().SetOrientationToMovementDirection(target, enable, rotationRate);
    }
    static void SetOrientationToMovementDirection(bool enable, GameObject* target, float rotationRate = 720.0f) {
        CharacterMechanicsSystem::Get().SetOrientationToMovementDirection(target, enable, rotationRate);
    }
    static bool IsOrientationToMovementDirectionEnabled(GameObject* target = nullptr) {
        return CharacterMechanicsSystem::Get().IsOrientationToMovementDirectionEnabled(target);
    }

    // Body Rotate to Movement Direction APIs (Unreal Engine style bOrientRotationToMovement):
    // Supports:
    // EngineSystem::CharacterMechanics::SetBodyRotateToMovementDirection(false | true, gameobject);
    // EngineSystem::CharacterMechanics::SetBodyRotateToMovementDirection(gameobject, false | true);
    static void SetBodyRotateToMovementDirection(bool enable, GameObject* target = nullptr, float rotationRate = 720.0f) {
        CharacterMechanicsSystem::Get().SetBodyRotateToMovementDirection(enable, target, rotationRate);
    }
    static void SetBodyRotateToMovementDirection(GameObject* target, bool enable, float rotationRate = 720.0f) {
        CharacterMechanicsSystem::Get().SetBodyRotateToMovementDirection(target, enable, rotationRate);
    }
    static bool IsBodyRotateToMovementDirectionEnabled(GameObject* target = nullptr) {
        return CharacterMechanicsSystem::Get().IsBodyRotateToMovementDirectionEnabled(target);
    }
    static void SetBodyObject(GameObject* target, GameObject* body) {
        CharacterMechanicsSystem::Get().SetBodyObject(target, body);
    }
    static void SetBodyObject(GameObject* body) {
        CharacterMechanicsSystem::Get().SetBodyObject(nullptr, body);
    }
    static GameObject* GetBodyObject(GameObject* target = nullptr) {
        return CharacterMechanicsSystem::Get().GetBodyObject(target);
    }

    // Locomotion & State APIs (Docs/Behaviuor.md Section 7)
    static void Jump(GameObject* target = nullptr) {
        CharacterMechanicsSystem::Get().Jump(target);
    }
    static void SetJumpHeight(float height, GameObject* target = nullptr) {
        CharacterMechanicsSystem::Get().SetJumpHeight(height, target);
    }
    static void SetMaxWalkSpeed(float speed, GameObject* target = nullptr) {
        CharacterMechanicsSystem::Get().SetMaxWalkSpeed(speed, target);
    }
    static void SetMaxRunSpeed(float speed, GameObject* target = nullptr) {
        CharacterMechanicsSystem::Get().SetMaxRunSpeed(speed, target);
    }
    static void SetAcceleration(float accel, GameObject* target = nullptr) {
        CharacterMechanicsSystem::Get().SetAcceleration(accel, target);
    }
    static void SetDeceleration(float decel, GameObject* target = nullptr) {
        CharacterMechanicsSystem::Get().SetDeceleration(decel, target);
    }
    static void SetAirControl(float control, GameObject* target = nullptr) {
        CharacterMechanicsSystem::Get().SetAirControl(control, target);
    }
    static void SetGravityScale(float scale, GameObject* target = nullptr) {
        CharacterMechanicsSystem::Get().SetGravityScale(scale, target);
    }
    static void Launch(const glm::vec3& launchVel, bool overrideXY = true, bool overrideZ = true, GameObject* target = nullptr) {
        CharacterMechanicsSystem::Get().Launch(launchVel, overrideXY, overrideZ, target);
    }
    static void AddMovementInput(const glm::vec3& worldDirection, float scale = 1.0f, GameObject* target = nullptr) {
        CharacterMechanicsSystem::Get().AddMovementInput(worldDirection, scale, target);
    }
    static bool IsGrounded(GameObject* target = nullptr) {
        return CharacterMechanicsSystem::Get().IsGrounded(target);
    }
    static bool IsFalling(GameObject* target = nullptr) {
        return CharacterMechanicsSystem::Get().IsFalling(target);
    }
    static bool IsJumping(GameObject* target = nullptr) {
        return CharacterMechanicsSystem::Get().IsJumping(target);
    }
    static bool IsCrouching(GameObject* target = nullptr) {
        return CharacterMechanicsSystem::Get().IsCrouching(target);
    }
    static glm::vec3 GetGroundNormal(GameObject* target = nullptr) {
        return CharacterMechanicsSystem::Get().GetGroundNormal(target);
    }
    static GameObject* GetGroundObject(GameObject* target = nullptr) {
        return CharacterMechanicsSystem::Get().GetGroundObject(target);
    }
    static void SetCoyoteTime(float time, GameObject* target = nullptr) {
        CharacterMechanicsSystem::Get().SetCoyoteTime(time, target);
    }
    static void SetJumpBuffer(float buffer, GameObject* target = nullptr) {
        CharacterMechanicsSystem::Get().SetJumpBuffer(buffer, target);
    }
    static void SetStepHeight(float height, GameObject* target = nullptr) {
        CharacterMechanicsSystem::Get().SetStepHeight(height, target);
    }
    static void SetSlopeLimit(float limit, GameObject* target = nullptr) {
        CharacterMechanicsSystem::Get().SetSlopeLimit(limit, target);
    }
    static void Crouch(bool enable, GameObject* target = nullptr) {
        CharacterMechanicsSystem::Get().Crouch(enable, target);
    }
    static void SetCrouchHeight(float height, GameObject* target = nullptr) {
        CharacterMechanicsSystem::Get().SetCrouchHeight(height, target);
    }
    static float GetSpeed(GameObject* target = nullptr) {
        return CharacterMechanicsSystem::Get().GetSpeed(target);
    }
};

} // namespace CharacterMechanics
} // namespace Eunoia

// Top-level EngineSystem struct and global aliases matching user request:
// EngineSystem::CharacterMechanics::SetOrientationToMovementDirection(true | false)
// EngineSystem::CharacterMechanics::AddMovement(...)
// CharacterMechanics::AddMovement(...)
// AssumeAsCharacter().AddMovement(...)
// Char::...
struct EngineSystem {
    using CharacterMechanics = Eunoia::CharacterMechanics::CharacterMechanicsProxy;
};

using CharacterMechanics = Eunoia::CharacterMechanics::CharacterMechanicsProxy;
using Char = Eunoia::CharacterMechanics::CharacterMechanicsProxy;
