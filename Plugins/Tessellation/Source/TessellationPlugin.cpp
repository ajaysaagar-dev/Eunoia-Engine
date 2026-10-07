#include <Tessellation/TessellationSystem.h>

extern "C" void RegisterTessellationPlugin() {
    Eunoia::Tessellation::TessellationSystem::Get().Initialize();
}
