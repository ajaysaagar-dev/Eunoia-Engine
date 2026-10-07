#pragma once
#include "SSETypes.h"

namespace Eunoia { namespace SSE {

class ScreenSpaceEffectsSystem {
public:
    static ScreenSpaceEffectsSystem& Get() {
        static ScreenSpaceEffectsSystem inst;
        return inst;
    }

    void Initialize();
    void Shutdown();

    // AO
    void SetAOMode(SSAOMode mode)               { m_settings.ao.mode = mode; }
    SSAOMode GetAOMode()                 const  { return m_settings.ao.mode; }

    // SSR
    void SetSSREnabled(bool v)                  { m_settings.ssr.enableSSR = v; }
    bool GetSSREnabled()                 const  { return m_settings.ssr.enableSSR; }

    // Refractions
    void SetRefractionsEnabled(bool v)          { m_settings.refractions.enableRefractions = v; }
    bool GetRefractionsEnabled()         const  { return m_settings.refractions.enableRefractions; }

    ScreenSpaceEffectsSettings&       GetSettingsRef() { return m_settings; }
    const ScreenSpaceEffectsSettings& GetSettings()    const { return m_settings; }

    // Presets
    void ApplyQualityPreset(int level); // 0=off,1=low,2=medium,3=high,4=ultra

private:
    ScreenSpaceEffectsSystem() = default;
    ScreenSpaceEffectsSettings m_settings;
};

}} // namespace Eunoia::SSE
