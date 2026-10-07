#pragma once
#include "AdvancedPostProcessTypes.h"

namespace Eunoia { namespace PostProcess {

class AdvancedPostProcessSystem {
public:
    static AdvancedPostProcessSystem& Get() {
        static AdvancedPostProcessSystem inst;
        return inst;
    }

    void Initialize();
    void Shutdown();
    void Apply(const AdvancedPostProcessSettings& settings);
    void Update(float deltaTime); // For eye adaptation, animated lens effects

    AdvancedPostProcessSettings&       GetSettingsRef() { return m_settings; }
    const AdvancedPostProcessSettings& GetSettings()    const { return m_settings; }

    // Individual feature toggles
    void SetBloomEnabled(bool v)           { m_settings.bloom.enabled = v; }
    void SetMotionBlurEnabled(bool v)      { m_settings.motionBlur.enabled = v; }
    void SetDOFEnabled(bool v)             { m_settings.dof.enabled = v; }
    void SetLensFlareEnabled(bool v)       { m_settings.lensFlare.enabled = v; }
    void SetLightShaftsEnabled(bool v)     { m_settings.lightShafts.enabled = v; }
    void SetChromaticAbEnabled(bool v)     { m_settings.chromaticAberration.enabled = v; }
    void SetEyeAdaptationEnabled(bool v)   { m_settings.eyeAdaptation.enabled = v; }
    void SetSharpenEnabled(bool v)         { m_settings.sharpen.enabled = v; }
    void SetEdgeOutlineEnabled(bool v)     { m_settings.edgeOutline.enabled = v; }

    // Presets
    void ApplyPreset(PostProcessPreset preset);

private:
    AdvancedPostProcessSystem() = default;
    AdvancedPostProcessSettings m_settings;
};

}} // namespace Eunoia::PostProcess
