#include <OcclusionCulling/OcclusionCullingSystem.h>

extern "C" void RegisterOcclusionCullingPlugin() {
    Eunoia::OcclusionCulling::OcclusionCullingSystem::Get().Initialize();
}
