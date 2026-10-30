#include "CharacterMechanics/CharacterMechanicsSystem.h"
#include "CharacterMechanics/CharacterMechanics.h"
#include <EngineScene/GameObject.h>
#include <EngineScene/EunoiaBehaviour.h>
#include <iostream>

namespace Eunoia {
namespace CharacterMechanics {

CharacterMechanicsSystem& CharacterMechanicsSystem::Get() {
    static CharacterMechanicsSystem s_instance;
    return s_instance;
}

void CharacterMechanicsSystem::Initialize() {
    std::cout << "[CharacterMechanics] System initialized.\n";
}

void CharacterMechanicsSystem::Shutdown() {
    m_activeCharacter = nullptr;
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

} // namespace CharacterMechanics
} // namespace Eunoia
