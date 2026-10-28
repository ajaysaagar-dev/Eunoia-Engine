#include <Animation-Graph/AnimationGraphPlugin.h>
#include <Animation-Graph/AnimatorGraph.h>
#include <Animation-Graph/AnimatorGraphEditor.h>
#include <Animation-Graph/SkeletalAnimation.h>
#include <iostream>

namespace Eunoia {

void AnimationGraphPlugin::OnRegister(ServiceRegistry& registry) {
}

void AnimationGraphPlugin::OnInit() {
    std::cout << "[Animation-Graph] Animation Graph Plugin initialized.\n";
}

void AnimationGraphPlugin::OnShutdown() {
    std::cout << "[Animation-Graph] Animation Graph Plugin shutdown.\n";
}

static AnimationGraphPlugin s_animationGraphPlugin;

} // namespace Eunoia
