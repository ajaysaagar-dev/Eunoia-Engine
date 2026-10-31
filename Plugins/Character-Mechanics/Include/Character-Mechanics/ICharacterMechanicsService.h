#pragma once
#include <glm/glm.hpp>

struct GameObject;

namespace Eunoia {
namespace CharacterMechanics {

class ICharacterMechanicsService {
public:
    virtual ~ICharacterMechanicsService() = default;

    virtual void AddMovement(GameObject* target, float speed, const glm::vec3& worldDirection, bool negate = false) = 0;
    virtual void SetActiveCharacter(GameObject* character) = 0;
    virtual GameObject* GetActiveCharacter() const = 0;

    virtual float GetVelocity(GameObject* target, const glm::vec3& directionMask = glm::vec3(1.0f, 1.0f, 1.0f)) = 0;
    virtual float GetVelocity(const glm::vec3& directionMask = glm::vec3(1.0f, 1.0f, 1.0f)) = 0;
};

} // namespace CharacterMechanics
} // namespace Eunoia
