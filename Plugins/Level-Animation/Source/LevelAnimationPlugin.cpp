#include <Level-Animation/LevelAnimationPlugin.h>
#include <Level-Animation/LevelAnimation.h>
#include <Level-Animation/LevelAnimationEditor.h>
#include <Level-Animation/LevelAnimationManager.h>
#include <iostream>

namespace Eunoia {

void LevelAnimationPlugin::OnRegister(ServiceRegistry& registry) {
}

void LevelAnimationPlugin::OnInit() {
    std::cout << "[Level-Animation] Level Animation Plugin initialized.\n";
}

void LevelAnimationPlugin::OnShutdown() {
    std::cout << "[Level-Animation] Level Animation Plugin shutdown.\n";
}

static LevelAnimationPlugin s_levelAnimationPlugin;

} // namespace Eunoia
