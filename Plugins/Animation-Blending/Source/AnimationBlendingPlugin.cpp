#include <Animation-Blending/AnimationBlendingPlugin.h>
#include <Animation-Blending/AnimationBlend.h>
#include <Animation-Blending/AnimationBlendEditor.h>
#include <Animation-Blending/AnimationBlendManager.h>
#include <iostream>

namespace Eunoia {

void AnimationBlendingPlugin::OnRegister(ServiceRegistry& registry) {
}

void AnimationBlendingPlugin::OnInit() {
    std::cout << "[Animation-Blending] Animation Blending Plugin initialized.\n";
}

void AnimationBlendingPlugin::OnShutdown() {
    std::cout << "[Animation-Blending] Animation Blending Plugin shutdown.\n";
}

static AnimationBlendingPlugin s_animationBlendingPlugin;

} // namespace Eunoia
