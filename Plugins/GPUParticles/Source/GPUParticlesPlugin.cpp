#include <GPUParticles/GPUParticleSystem.h>

extern "C" void RegisterGPUParticlesPlugin() {
    Eunoia::GPUParticles::GPUParticleSystem::Get().Initialize();
}
