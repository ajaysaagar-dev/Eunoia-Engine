#include <AdvancedReflections/AdvancedReflectionsSystem.h>

extern "C" void RegisterAdvancedReflectionsPlugin() {
    Eunoia::Reflections::AdvancedReflectionsSystem::Get().Initialize();
}
