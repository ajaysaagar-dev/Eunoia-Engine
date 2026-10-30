#include "CharacterMechanics/CharacterMechanicsPlugin.h"
#include "CharacterMechanics/CharacterMechanicsSystem.h"
#include "CharacterMechanics/ICharacterMechanicsService.h"
#include <iostream>

namespace Eunoia {
namespace CharacterMechanics {

class CharacterMechanicsServiceImpl : public ICharacterMechanicsService {
public:
    void AddMovement(GameObject* target, float speed, const glm::vec3& worldDirection, bool negate = false) override {
        CharacterMechanicsSystem::Get().AddMovement(target, speed, worldDirection, negate);
    }
    void SetActiveCharacter(GameObject* character) override {
        CharacterMechanicsSystem::Get().SetActiveCharacter(character);
    }
    GameObject* GetActiveCharacter() const override {
        return CharacterMechanicsSystem::Get().GetActiveCharacter();
    }
};

} // namespace CharacterMechanics

void CharacterMechanicsPlugin::OnRegister(ServiceRegistry& registry) {
    static CharacterMechanics::CharacterMechanicsServiceImpl service;
    std::cout << "[Character Mechanics] Registering ICharacterMechanicsService...\n";
    registry.Register<CharacterMechanics::ICharacterMechanicsService>("ICharacterMechanicsService", &service);
}

void CharacterMechanicsPlugin::OnInit() {
    CharacterMechanics::CharacterMechanicsSystem::Get().Initialize();
    std::cout << "[Character Mechanics] Plugin initialized.\n";
}

void CharacterMechanicsPlugin::OnShutdown() {
    CharacterMechanics::CharacterMechanicsSystem::Get().Shutdown();
    std::cout << "[Character Mechanics] Plugin shutdown.\n";
}

static CharacterMechanicsPlugin s_characterMechanicsPlugin;

} // namespace Eunoia
