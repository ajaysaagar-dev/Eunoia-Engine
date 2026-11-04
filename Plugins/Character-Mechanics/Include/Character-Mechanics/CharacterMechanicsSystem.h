#pragma once
#include <glm/glm.hpp>
#include <EngineScene/TransformTypes.h>
#include <unordered_map>
#include <chrono>

struct GameObject;
class EunoiaBehaviour;

namespace Eunoia {
namespace CharacterMechanics {

struct ObjectVelocityTracker {
    glm::vec3 lastWorldPosition{0.0f, 0.0f, 0.0f};
    glm::vec3 currentWorldPosition{0.0f, 0.0f, 0.0f};
    glm::vec3 currentVelocity{0.0f, 0.0f, 0.0f};
    std::chrono::steady_clock::time_point lastSampleTime{};
    float lastDeltaTime = 0.01667f;
    bool initialized = false;
};

struct CharacterMechanicsSettings {
    bool orientToMovement = false;
    float rotationRate = 720.0f; // degrees per second (0 = instant snap)
    bool bodyRotateToMovement = false;
    GameObject* bodyObject = nullptr;
    float bodyRotationRate = 720.0f;
    float baseYawOffset = 0.0f;
    bool hasBaseOffset = false;

    // Locomotion & Physics Properties
    float jumpHeight = 1.2f;
    float maxWalkSpeed = 6.0f;
    float maxRunSpeed = 9.0f;
    float acceleration = 40.0f;
    float deceleration = 60.0f;
    float airControl = 0.3f;
    float gravityScale = 1.0f;

    // States
    bool isGrounded = true;
    bool isFalling = false;
    bool isJumping = false;
    bool isCrouching = false;
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
    GameObject* groundObject = nullptr;

    // Game-feel Helpers
    float coyoteTime = 0.10f;
    float jumpBuffer = 0.12f;
    float stepHeight = 0.35f;
    float slopeLimit = 45.0f;
    float crouchHeight = 1.0f;
    float standingHeight = 1.8f;
};

class CharacterMechanicsSystem {
public:
    static CharacterMechanicsSystem& Get();

    void Initialize();
    void Shutdown();

    void SetActiveCharacter(GameObject* character);
    GameObject* GetActiveCharacter() const;

    // Movement APIs
    void AddMovement(GameObject* target, float speed, const glm::vec3& worldDirection, bool negate = false);
    void AddMovement(GameObject* target, float speed, const TransformVector3& worldDirection, bool negate = false);
    void AddMovement(GameObject* target, float speed, float dirX, float dirY, float dirZ, bool negate = false);

    // Global AddMovement targeting active character
    void AddMovement(float speed, const glm::vec3& worldDirection, bool negate = false);
    void AddMovement(float speed, const TransformVector3& worldDirection, bool negate = false);
    void AddMovement(float speed, float dirX, float dirY, float dirZ, bool negate = false);

    // Velocity APIs:
    float GetVelocity(GameObject* target, const glm::vec3& directionMask = glm::vec3(1.0f, 1.0f, 1.0f));
    float GetVelocity(GameObject* target, const TransformVector3& directionMask);
    float GetVelocity(GameObject* target, float dirX, float dirY, float dirZ);

    float GetVelocity(const glm::vec3& directionMask = glm::vec3(1.0f, 1.0f, 1.0f));
    float GetVelocity(const TransformVector3& directionMask);
    float GetVelocity(float dirX, float dirY, float dirZ);

    glm::vec3 GetVelocityVector(GameObject* target = nullptr, const glm::vec3& directionMask = glm::vec3(1.0f, 1.0f, 1.0f));

    void UpdateObjectVelocity(GameObject* target, float dt = 0.0f);
    void ClearTrackers();

    // Orientation to Movement Direction APIs
    void SetOrientationToMovementDirection(GameObject* target, bool enable, float rotationRate = 720.0f);
    void SetOrientationToMovementDirection(bool enable, float rotationRate = 720.0f);
    bool IsOrientationToMovementDirectionEnabled(GameObject* target = nullptr) const;
    float GetRotationRate(GameObject* target = nullptr) const;
    void OrientToDirection(GameObject* target, const glm::vec3& worldDirection, float dt = 0.0f);

    // Body Rotate to Movement Direction APIs (Unreal Engine style bOrientRotationToMovement)
    void SetBodyRotateToMovementDirection(bool enable, GameObject* target = nullptr, float rotationRate = 720.0f);
    void SetBodyRotateToMovementDirection(GameObject* target, bool enable, float rotationRate = 720.0f);
    bool IsBodyRotateToMovementDirectionEnabled(GameObject* target = nullptr) const;
    float GetBodyRotationRate(GameObject* target = nullptr) const;
    GameObject* GetBodyObject(GameObject* target = nullptr) const;
    void SetBodyObject(GameObject* target, GameObject* body);
    void SetBodyObject(GameObject* body);
    float GetBaseYawOffset(GameObject* target = nullptr) const;
    void SetBaseYawOffset(GameObject* target, float offset);

    // Locomotion & State APIs (Docs/Behaviuor.md Section 7)
    void Jump(GameObject* target = nullptr);
    void SetJumpHeight(float height, GameObject* target = nullptr);
    void SetMaxWalkSpeed(float speed, GameObject* target = nullptr);
    void SetMaxRunSpeed(float speed, GameObject* target = nullptr);
    void SetAcceleration(float accel, GameObject* target = nullptr);
    void SetDeceleration(float decel, GameObject* target = nullptr);
    void SetAirControl(float control, GameObject* target = nullptr);
    void SetGravityScale(float scale, GameObject* target = nullptr);
    void Launch(const glm::vec3& launchVel, bool overrideXY = true, bool overrideZ = true, GameObject* target = nullptr);
    void AddMovementInput(const glm::vec3& worldDirection, float scale = 1.0f, GameObject* target = nullptr);

    bool IsGrounded(GameObject* target = nullptr) const;
    bool IsFalling(GameObject* target = nullptr) const;
    bool IsJumping(GameObject* target = nullptr) const;
    bool IsCrouching(GameObject* target = nullptr) const;
    glm::vec3 GetGroundNormal(GameObject* target = nullptr) const;
    GameObject* GetGroundObject(GameObject* target = nullptr) const;

    void SetCoyoteTime(float time, GameObject* target = nullptr);
    void SetJumpBuffer(float buffer, GameObject* target = nullptr);
    void SetStepHeight(float height, GameObject* target = nullptr);
    void SetSlopeLimit(float limit, GameObject* target = nullptr);
    void Crouch(bool enable, GameObject* target = nullptr);
    void SetCrouchHeight(float height, GameObject* target = nullptr);
    float GetSpeed(GameObject* target = nullptr);

private:
    CharacterMechanicsSystem() = default;
    ~CharacterMechanicsSystem() = default;

    GameObject* m_activeCharacter = nullptr;
    std::unordered_map<int, ObjectVelocityTracker> m_velocityTrackers;
    std::unordered_map<int, CharacterMechanicsSettings> m_settings;
};

} // namespace CharacterMechanics
} // namespace Eunoia
