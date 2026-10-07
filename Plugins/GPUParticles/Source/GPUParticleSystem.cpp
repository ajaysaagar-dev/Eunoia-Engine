#include <GPUParticles/GPUParticleSystem.h>

namespace Eunoia { namespace GPUParticles {

void GPUParticleSystem::Initialize() {
    m_settings.enableGPUParticles  = false;
    m_settings.enableHairParticles = false;
    m_settings.emitter.maxParticles = 10000;
    m_settings.emitter.emitRate     = 100.0f;
    m_settings.emitter.particleLifetime = 2.0f;
    m_settings.emitter.softParticles = true;
    m_settings.emitter.gpuSorting   = true;
    m_settings.hair.strandCount     = 10000;
    m_settings.hair.strandLength    = 0.3f;
    m_settings.hair.windStrength    = 0.4f;
}

void GPUParticleSystem::Shutdown() {}

void GPUParticleSystem::Update(float /*deltaTime*/) {
    // GPU CS dispatch for particle simulation would go here
}

}} // namespace Eunoia::GPUParticles
