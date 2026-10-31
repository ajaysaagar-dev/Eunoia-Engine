#include "Behaviour/BehaviourPlugin.h"
#include "Behaviour/BehaviourManager.h"
#include <iostream>

namespace Eunoia {

void BehaviourPlugin::OnRegister(ServiceRegistry& registry) {
}

void BehaviourPlugin::OnInit() {
    std::cout << "[Behaviour] Unreal-Engine Blueprint-style Behaviour Plugin initialized.\n";
}

void BehaviourPlugin::OnShutdown() {
    std::cout << "[Behaviour] Behaviour Plugin shutdown.\n";
}

static BehaviourPlugin s_behaviourPlugin;

} // namespace Eunoia
