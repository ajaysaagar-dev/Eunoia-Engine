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
};

} // namespace CharacterMechanics
} // namespace Eunoia
