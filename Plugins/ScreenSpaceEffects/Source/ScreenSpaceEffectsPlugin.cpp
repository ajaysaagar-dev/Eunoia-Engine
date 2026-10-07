#include <ScreenSpaceEffects/ScreenSpaceEffectsSystem.h>

// Plugin registration entry point
extern "C" void RegisterScreenSpaceEffectsPlugin() {
    Eunoia::SSE::ScreenSpaceEffectsSystem::Get().Initialize();
}
