#pragma once
#include "IVolumetricAtmosphere.h"
#include <mutex>

namespace Eunoia {
namespace Atmosphere {

class VolumetricAtmosphere : public IVolumetricAtmosphere {
public:
    static VolumetricAtmosphere& Get();

    bool Initialize() override;
    void Shutdown() override;

    bool IsEnabled() const override { return m_settings.enableAtmosphere; }
    void SetEnabled(bool enabled) override { m_settings.enableAtmosphere = enabled; }

    const VolumetricAtmosphereSettings& GetSettings() const override { return m_settings; }
    VolumetricAtmosphereSettings& GetSettingsRef() override { return m_settings; }
    void SetSettings(const VolumetricAtmosphereSettings& settings) override { m_settings = settings; }

    void Update(float deltaTime) override;

private:
    VolumetricAtmosphere() = default;
    ~VolumetricAtmosphere() override = default;

    VolumetricAtmosphereSettings m_settings;
    float m_windOffset = 0.0f;
    mutable std::mutex m_mutex;
    bool m_initialized = false;
};

} // namespace Atmosphere
} // namespace Eunoia
