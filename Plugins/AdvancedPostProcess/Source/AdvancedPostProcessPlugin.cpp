#include <AdvancedPostProcess/AdvancedPostProcessSystem.h>

extern "C" void RegisterAdvancedPostProcessPlugin() {
    Eunoia::PostProcess::AdvancedPostProcessSystem::Get().Initialize();
}
