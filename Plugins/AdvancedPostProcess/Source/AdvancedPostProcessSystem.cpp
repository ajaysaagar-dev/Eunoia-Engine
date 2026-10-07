#include <AdvancedPostProcess/AdvancedPostProcessSystem.h>

namespace Eunoia { namespace PostProcess {

void AdvancedPostProcessSystem::Initialize() {
    // WickedEngine defaults
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
}

void AdvancedPostProcessSystem::Shutdown() {
}

void AdvancedPostProcessSystem::Apply(const AdvancedPostProcessSettings& settings) {
    m_settings = settings;
}

void AdvancedPostProcessSystem::Update(float /*deltaTime*/) {
    // Eye adaptation temporal update would accumulate luminance histogram here
}

void AdvancedPostProcessSystem::ApplyPreset(PostProcessPreset preset) {
    switch (preset) {
        case PostProcessPreset::Off:
            m_settings = AdvancedPostProcessSettings{};
            m_settings.bloom.enabled = false;
            m_settings.lensFlare.enabled = false;
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

}} // namespace Eunoia::PostProcess
