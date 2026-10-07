#pragma once
#include <cstdint>
#include <string>
#include <glm/glm.hpp>

namespace Eunoia { namespace GPUParticles {

// ============================================================
//  EMITTER SOURCE TYPE (WickedEngine style)
// ============================================================
enum class ParticleEmitSource : uint8_t {
    Point = 0,        // Emit from a single point
    Mesh,             // Emit from mesh surface (random triangle)
    AnimatedMesh,     // Emit from animated mesh (with vertex velocity)
};

// ============================================================
//  PARTICLE BLEND MODE
// ============================================================
enum class ParticleBlendMode : uint8_t {
    Additive = 0,
    AlphaBlend,
    Premultiplied,
};

// ============================================================
//  EMITTER SETTINGS
// ============================================================
struct ParticleEmitterSettings {
    bool            enabled           = false;
    ParticleEmitSource emitSource     = ParticleEmitSource::Point;
    glm::vec3       position          = glm::vec3(0.0f);
    glm::vec3       gravity           = glm::vec3(0.0f, -9.8f, 0.0f);
    uint32_t        maxParticles      = 10000;
    float           emitRate          = 100.0f;   // Particles per second
    float           particleLifetime  = 2.0f;     // Seconds
    float           particleSize      = 0.05f;
    float           particleSizeVariance = 0.02f;
    float           velocityScale     = 1.0f;
    float           velocitySpread    = 0.2f;     // Cone spread angle
    glm::vec4       startColor        = glm::vec4(1.0f);
    glm::vec4       endColor          = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    // Soft particles
    bool            softParticles     = true;
    float           softParticleContrastPower = 2.0f;
    // GPU sort + collision
    bool            gpuSorting        = true;
    bool            collisionEnabled  = false;
    float           restitution       = 0.3f;
    // Mesh source
    std::string     meshAssetPath;
    // Blend
    ParticleBlendMode blendMode       = ParticleBlendMode::AlphaBlend;
    bool            useMotionVectors  = true;
};

// ============================================================
//  HAIR / GRASS PARTICLE SETTINGS (WickedEngine HairParticle)
// ============================================================
struct HairParticleSettings {
    bool            enabled           = false;
    std::string     meshAssetPath;           // Source mesh to grow hair on
    uint32_t        strandCount       = 10000;
    float           strandLength      = 0.3f;
    float           strandLengthVariance = 0.05f;
    float           strandWidth       = 0.002f;
    glm::vec4       rootColor         = glm::vec4(0.15f, 0.35f, 0.05f, 1.0f); // Grass green
    glm::vec4       tipColor          = glm::vec4(0.25f, 0.55f, 0.1f, 0.5f);
    float           windStrength      = 0.4f;
    float           windFrequency     = 1.0f;
    glm::vec2       windDirection     = glm::vec2(1.0f, 0.0f);
    bool            castShadows       = false;
    bool            receiveShadows    = true;
    float           lodDistance       = 50.0f;   // Fade out at this distance
};

// ============================================================
//  COMBINED GPU PARTICLES SETTINGS
// ============================================================
struct GPUParticlesSettings {
    ParticleEmitterSettings emitter;
    HairParticleSettings    hair;
    bool                    enableGPUParticles = false;
    bool                    enableHairParticles = false;
};

class GPUParticleSystem {
public:
    static GPUParticleSystem& Get() {
        static GPUParticleSystem inst;
        return inst;
    }

    void Initialize();
    void Shutdown();
    void Update(float deltaTime);

    GPUParticlesSettings&       GetSettingsRef()       { return m_settings; }
    const GPUParticlesSettings& GetSettings()    const { return m_settings; }

    bool IsGPUParticlesEnabled() const { return m_settings.enableGPUParticles; }
    bool IsHairParticlesEnabled() const { return m_settings.enableHairParticles; }

    void SetGPUParticlesEnabled(bool v) { m_settings.enableGPUParticles = v; }
    void SetHairParticlesEnabled(bool v) { m_settings.enableHairParticles = v; }

private:
    GPUParticleSystem() = default;
    GPUParticlesSettings m_settings;
};

}} // namespace Eunoia::GPUParticles
