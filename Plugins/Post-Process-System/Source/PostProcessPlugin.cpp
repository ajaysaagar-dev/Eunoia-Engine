#include <PostProcess/PostProcessSystem.h>

extern "C" void RegisterPostProcessSystemPlugin() {
    Eunoia::PostProcess::PostProcessSystem::Get().Initialize();
}

// Backward compatibility symbols
extern "C" void RegisterAdvancedPostProcessPlugin() {
    Eunoia::PostProcess::PostProcessSystem::Get().Initialize();
}

extern "C" void RegisterScreenSpaceEffectsPlugin() {
    Eunoia::PostProcess::PostProcessSystem::Get().Initialize();
}
