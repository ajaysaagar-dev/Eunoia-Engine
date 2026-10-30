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
