#pragma once
#include <glm/glm.hpp>
#include <EngineScene/TransformTypes.h>

struct GameObject;
class EunoiaBehaviour;

namespace Eunoia {
namespace CharacterMechanics {

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

private:
    CharacterMechanicsSystem() = default;
    ~CharacterMechanicsSystem() = default;

    GameObject* m_activeCharacter = nullptr;
};

} // namespace CharacterMechanics
} // namespace Eunoia
