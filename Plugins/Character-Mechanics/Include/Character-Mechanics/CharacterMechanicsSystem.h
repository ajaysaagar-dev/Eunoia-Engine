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

private:
    CharacterMechanicsSystem() = default;
    ~CharacterMechanicsSystem() = default;

    GameObject* m_activeCharacter = nullptr;
    std::unordered_map<int, ObjectVelocityTracker> m_velocityTrackers;
    std::unordered_map<int, CharacterMechanicsSettings> m_settings;
};

} // namespace CharacterMechanics
} // namespace Eunoia
