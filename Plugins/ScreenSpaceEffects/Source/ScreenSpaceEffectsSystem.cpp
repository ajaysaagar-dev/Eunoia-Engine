#include <ScreenSpaceEffects/ScreenSpaceEffectsSystem.h>
#include <EngineCore/EngineLogger.h>

namespace Eunoia { namespace SSE {

void ScreenSpaceEffectsSystem::Initialize() {
    // Defaults: SSAO medium quality, SSR disabled, refractions disabled
    m_settings.ao.mode        = SSAOMode::SSAO;
    m_settings.ao.radius      = 0.5f;
    m_settings.ao.power       = 1.5f;
    m_settings.ao.sampleCount = 16;
    m_settings.ao.enableBlur  = true;
    m_settings.ssr.enableSSR  = false;
    m_settings.refractions.enableRefractions = false;
}

void ScreenSpaceEffectsSystem::Shutdown() {
}

void ScreenSpaceEffectsSystem::ApplyQualityPreset(int level) {
    switch (level) {
        case 0: // Off
            m_settings.ao.mode       = SSAOMode::Disabled;
            m_settings.ssr.enableSSR = false;
            break;
        case 1: // Low
            m_settings.ao.mode        = SSAOMode::SSAO;
            m_settings.ao.sampleCount = 8;
            m_settings.ao.radius      = 0.3f;
            m_settings.ssr.enableSSR  = false;
            break;
        case 2: // Medium
            m_settings.ao.mode        = SSAOMode::SSAO;
            m_settings.ao.sampleCount = 16;
            m_settings.ao.radius      = 0.5f;
            m_settings.ssr.enableSSR  = true;
            m_settings.ssr.maxRaySteps= 32;
            break;
        case 3: // High
            m_settings.ao.mode        = SSAOMode::HBAO;
            m_settings.ao.radius      = 0.6f;
            m_settings.ssr.enableSSR  = true;
            m_settings.ssr.maxRaySteps= 64;
            m_settings.refractions.enableRefractions = true;
            break;
        case 4: // Ultra
            m_settings.ao.mode        = SSAOMode::MSAO;
            m_settings.ao.radius      = 0.7f;
            m_settings.ssr.enableSSR  = true;
            m_settings.ssr.maxRaySteps= 128;
            m_settings.ssr.enableDenoiser = true;
            m_settings.refractions.enableRefractions = true;
            break;
    }
}

}} // namespace Eunoia::SSE
