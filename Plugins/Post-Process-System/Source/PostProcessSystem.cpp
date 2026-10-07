#include <PostProcess/PostProcessSystem.h>

namespace Eunoia { namespace PostProcess {

void PostProcessSystem::Initialize() {
    // Post Processing Defaults
    m_settings.bloom.enabled          = true;
    m_settings.bloom.threshold        = 1.0f;
    m_settings.bloom.strength         = 0.5f;
    m_settings.bloom.mipChainLength   = 6;

    m_settings.motionBlur.enabled     = false;
    m_settings.motionBlur.strength    = 1.0f;
    m_settings.motionBlur.sampleCount = 16;
    m_settings.motionBlur.shutterAngle= 180.0f;

    m_settings.dof.enabled            = false;
    m_settings.dof.focusDistance      = 10.0f;
    m_settings.dof.focusRange         = 3.0f;
    m_settings.dof.bokehRadius        = 8.0f;

    m_settings.lensFlare.enabled      = true;
    m_settings.lensFlare.intensity    = 0.3f;

    m_settings.lightShafts.enabled    = false;
    m_settings.lightShafts.numSamples = 64;

    m_settings.chromaticAberration.enabled = false;
    m_settings.chromaticAberration.amount  = 0.003f;

    m_settings.eyeAdaptation.enabled  = false;
    m_settings.eyeAdaptation.minEV    = -4.0f;
    m_settings.eyeAdaptation.maxEV    = 4.0f;
    m_settings.eyeAdaptation.adaptSpeed= 2.0f;

    m_settings.sharpen.enabled        = false;
    m_settings.sharpen.amount         = 0.28f;

    m_settings.edgeOutline.enabled    = false;
    m_settings.edgeOutline.thickness  = 1.0f;

    // Screen Space Effects Defaults
    m_settings.sse.ao.mode        = SSAOMode::SSAO;
    m_settings.sse.ao.radius      = 0.5f;
    m_settings.sse.ao.power       = 1.5f;
    m_settings.sse.ao.sampleCount = 16;
    m_settings.sse.ao.enableBlur  = true;
    m_settings.sse.ssr.enableSSR  = false;
    m_settings.sse.refractions.enableRefractions = false;
}

void PostProcessSystem::Shutdown() {
}

void PostProcessSystem::Apply(const PostProcessSettings& settings) {
    m_settings = settings;
}

void PostProcessSystem::Update(float /*deltaTime*/) {
}

void PostProcessSystem::ApplyPreset(PostProcessPreset preset) {
    switch (preset) {
        case PostProcessPreset::Off:
            m_settings.bloom.enabled = false;
            m_settings.lensFlare.enabled = false;
            m_settings.motionBlur.enabled = false;
            m_settings.dof.enabled = false;
            m_settings.lightShafts.enabled = false;
            m_settings.chromaticAberration.enabled = false;
            m_settings.eyeAdaptation.enabled = false;
            m_settings.sharpen.enabled = false;
            m_settings.edgeOutline.enabled = false;
            break;
        case PostProcessPreset::GameBalanced:
            m_settings.bloom.enabled          = true;
            m_settings.bloom.strength         = 0.4f;
            m_settings.motionBlur.enabled     = false;
            m_settings.dof.enabled            = false;
            m_settings.eyeAdaptation.enabled  = true;
            m_settings.lensFlare.enabled      = false;
            m_settings.sharpen.enabled        = true;
            m_settings.sharpen.amount         = 0.28f;
            m_settings.chromaticAberration.enabled = false;
            break;
        case PostProcessPreset::Cinematic:
            m_settings.bloom.enabled          = true;
            m_settings.bloom.strength         = 0.7f;
            m_settings.motionBlur.enabled     = true;
            m_settings.motionBlur.strength    = 1.0f;
            m_settings.dof.enabled            = true;
            m_settings.dof.bokehRadius        = 10.0f;
            m_settings.eyeAdaptation.enabled  = true;
            m_settings.lensFlare.enabled      = true;
            m_settings.lightShafts.enabled    = true;
            m_settings.chromaticAberration.enabled = true;
            m_settings.chromaticAberration.amount  = 0.002f;
            m_settings.edgeOutline.enabled    = false;
            break;
        case PostProcessPreset::Photorealistic:
            m_settings.bloom.enabled          = true;
            m_settings.bloom.strength         = 0.6f;
            m_settings.bloom.useHDR           = true;
            m_settings.motionBlur.enabled     = true;
            m_settings.dof.enabled            = true;
            m_settings.eyeAdaptation.enabled  = true;
            m_settings.eyeAdaptation.useHistogram = true;
            m_settings.lensFlare.enabled      = true;
            m_settings.lightShafts.enabled    = true;
            m_settings.sharpen.enabled        = true;
            m_settings.sharpen.amount         = 0.5f;
            m_settings.chromaticAberration.enabled = false;
            break;
        case PostProcessPreset::Stylized:
            m_settings.edgeOutline.enabled    = true;
            m_settings.edgeOutline.thickness  = 2.0f;
            m_settings.bloom.enabled          = true;
            m_settings.bloom.strength         = 0.9f;
            m_settings.chromaticAberration.enabled = true;
            m_settings.chromaticAberration.amount  = 0.005f;
            m_settings.eyeAdaptation.enabled  = false;
            break;
    }
}

void PostProcessSystem::ApplySSEQualityPreset(int level) {
    switch (level) {
        case 0: // Off
            m_settings.sse.ao.mode       = SSAOMode::Disabled;
            m_settings.sse.ssr.enableSSR = false;
            break;
        case 1: // Low
            m_settings.sse.ao.mode        = SSAOMode::SSAO;
            m_settings.sse.ao.sampleCount = 8;
            m_settings.sse.ao.radius      = 0.3f;
            m_settings.sse.ssr.enableSSR  = false;
            break;
        case 2: // Medium
            m_settings.sse.ao.mode        = SSAOMode::SSAO;
            m_settings.sse.ao.sampleCount = 16;
            m_settings.sse.ao.radius      = 0.5f;
            m_settings.sse.ssr.enableSSR  = true;
            m_settings.sse.ssr.maxRaySteps= 32;
            break;
        case 3: // High
            m_settings.sse.ao.mode        = SSAOMode::HBAO;
            m_settings.sse.ao.radius      = 0.6f;
            m_settings.sse.ssr.enableSSR  = true;
            m_settings.sse.ssr.maxRaySteps= 64;
            m_settings.sse.refractions.enableRefractions = true;
            break;
        case 4: // Ultra
            m_settings.sse.ao.mode        = SSAOMode::MSAO;
            m_settings.sse.ao.radius      = 0.7f;
            m_settings.sse.ssr.enableSSR  = true;
            m_settings.sse.ssr.maxRaySteps= 128;
            m_settings.sse.ssr.enableDenoiser = true;
            m_settings.sse.refractions.enableRefractions = true;
            break;
    }
}

}} // namespace Eunoia::PostProcess
