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
    // Calculates the float current velocity of the object based on world movement comparing last transform and current transform.
    // The vector3 parameter is an axis mask: 1 (or true/non-zero) selects that direction, 0 (false) neglects it.
    float GetVelocity(GameObject* target, const glm::vec3& directionMask = glm::vec3(1.0f, 1.0f, 1.0f));
    float GetVelocity(GameObject* target, const TransformVector3& directionMask);
    float GetVelocity(GameObject* target, float dirX, float dirY, float dirZ);

    float GetVelocity(const glm::vec3& directionMask = glm::vec3(1.0f, 1.0f, 1.0f));
    float GetVelocity(const TransformVector3& directionMask);
    float GetVelocity(float dirX, float dirY, float dirZ);

    glm::vec3 GetVelocityVector(GameObject* target = nullptr, const glm::vec3& directionMask = glm::vec3(1.0f, 1.0f, 1.0f));

    void UpdateObjectVelocity(GameObject* target, float dt = 0.0f);
    void ClearTrackers();

private:
    CharacterMechanicsSystem() = default;
    ~CharacterMechanicsSystem() = default;

    GameObject* m_activeCharacter = nullptr;
    std::unordered_map<int, ObjectVelocityTracker> m_velocityTrackers;
};

} // namespace CharacterMechanics
} // namespace Eunoia
