#pragma once
#include "PostProcessTypes.h"

namespace Eunoia { namespace PostProcess {

class PostProcessSystem {
public:
    static PostProcessSystem& Get() {
        static PostProcessSystem inst;
        return inst;
    }

    void Initialize();
    void Shutdown();
    void Apply(const PostProcessSettings& settings);
    void Update(float deltaTime);

    PostProcessSettings&       GetSettingsRef() { return m_settings; }
    const PostProcessSettings& GetSettings()    const { return m_settings; }

    // Bloom
    void SetBloomEnabled(bool v)           { m_settings.bloom.enabled = v; }
    bool GetBloomEnabled()           const { return m_settings.bloom.enabled; }

    // Motion Blur
    void SetMotionBlurEnabled(bool v)      { m_settings.motionBlur.enabled = v; }
    bool GetMotionBlurEnabled()      const { return m_settings.motionBlur.enabled; }

    // Depth of Field
    void SetDOFEnabled(bool v)             { m_settings.dof.enabled = v; }
    bool GetDOFEnabled()             const { return m_settings.dof.enabled; }

    // Optics & Lens
    void SetLensFlareEnabled(bool v)       { m_settings.lensFlare.enabled = v; }
    void SetLightShaftsEnabled(bool v)     { m_settings.lightShafts.enabled = v; }
    void SetChromaticAbEnabled(bool v)     { m_settings.chromaticAberration.enabled = v; }
    void SetEyeAdaptationEnabled(bool v)   { m_settings.eyeAdaptation.enabled = v; }
    void SetSharpenEnabled(bool v)         { m_settings.sharpen.enabled = v; }
    void SetEdgeOutlineEnabled(bool v)     { m_settings.edgeOutline.enabled = v; }

    // Screen Space Effects (SSAO, SSSR, Refractions)
    void SetAOMode(SSAOMode mode)          { m_settings.sse.ao.mode = mode; }
    SSAOMode GetAOMode()             const { return m_settings.sse.ao.mode; }

    void SetSSREnabled(bool v)             { m_settings.sse.ssr.enableSSR = v; }
    bool GetSSREnabled()             const { return m_settings.sse.ssr.enableSSR; }

    void SetRefractionsEnabled(bool v)     { m_settings.sse.refractions.enableRefractions = v; }
    bool GetRefractionsEnabled()     const { return m_settings.sse.refractions.enableRefractions; }

    // Presets
    void ApplyPreset(PostProcessPreset preset);
    void ApplySSEQualityPreset(int level); // 0=off,1=low,2=medium,3=high,4=ultra

private:
    PostProcessSystem() = default;
    PostProcessSettings m_settings;
};

// Aliases for backward compatibility
using AdvancedPostProcessSystem = PostProcessSystem;

}} // namespace Eunoia::PostProcess

// SSE compatibility accessor
namespace Eunoia { namespace SSE {
    class ScreenSpaceEffectsSystem {
    public:
        static ScreenSpaceEffectsSystem& Get() {
            static ScreenSpaceEffectsSystem inst;
            return inst;
        }
        void Initialize() { ::Eunoia::PostProcess::PostProcessSystem::Get().Initialize(); }
        void Shutdown()   { ::Eunoia::PostProcess::PostProcessSystem::Get().Shutdown(); }
        void SetAOMode(::Eunoia::PostProcess::SSAOMode m) { ::Eunoia::PostProcess::PostProcessSystem::Get().SetAOMode(m); }
        ::Eunoia::PostProcess::SSAOMode GetAOMode() const { return ::Eunoia::PostProcess::PostProcessSystem::Get().GetAOMode(); }
        void SetSSREnabled(bool v) { ::Eunoia::PostProcess::PostProcessSystem::Get().SetSSREnabled(v); }
        bool GetSSREnabled() const { return ::Eunoia::PostProcess::PostProcessSystem::Get().GetSSREnabled(); }
        void SetRefractionsEnabled(bool v) { ::Eunoia::PostProcess::PostProcessSystem::Get().SetRefractionsEnabled(v); }
        bool GetRefractionsEnabled() const { return ::Eunoia::PostProcess::PostProcessSystem::Get().GetRefractionsEnabled(); }
        ::Eunoia::PostProcess::ScreenSpaceEffectsSettings& GetSettingsRef() { return ::Eunoia::PostProcess::PostProcessSystem::Get().GetSettingsRef().sse; }
        const ::Eunoia::PostProcess::ScreenSpaceEffectsSettings& GetSettings() const { return ::Eunoia::PostProcess::PostProcessSystem::Get().GetSettings().sse; }
        void ApplyQualityPreset(int l) { ::Eunoia::PostProcess::PostProcessSystem::Get().ApplySSEQualityPreset(l); }
    };
}}
