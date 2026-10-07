#pragma once
#include "IGlobalIllumination.h"
#include <mutex>

namespace Eunoia {
namespace GI {

class GlobalIllumination : public IGlobalIllumination {
public:
    static GlobalIllumination& Get();

    bool Initialize() override;
    void Shutdown() override;

    bool IsEnabled() const override { return m_settings.enableDDGI; }
    void SetEnabled(bool enabled) override { m_settings.enableDDGI = enabled; }

    const DDGIVolumeSettings& GetSettings() const override { return m_settings; }
    DDGIVolumeSettings& GetSettingsRef() override { return m_settings; }
    void SetSettings(const DDGIVolumeSettings& settings) override { m_settings = settings; }

    const DDGIStats& GetStats() const override { return m_stats; }
    void Update(float deltaTime) override;

private:
    GlobalIllumination();
    ~GlobalIllumination() override = default;

    DDGIVolumeSettings m_settings;
    DDGIStats m_stats;
    mutable std::mutex m_mutex;
    bool m_initialized = false;
};

} // namespace GI
} // namespace Eunoia
