#pragma once
#include "IRayTracingSystem.h"
#include <mutex>
#include <vector>

namespace Eunoia {
namespace RayTracing {

class RayTracingSystem : public IRayTracingSystem {
public:
    static RayTracingSystem& Get();

    bool Initialize() override;
    void Shutdown() override;

    bool IsHardwareDxrSupported() const override { return m_stats.isHardwareDxrAvailable; }
    bool IsEnabled() const override { return m_settings.enableRayTracing; }
    void SetEnabled(bool enabled) override { m_settings.enableRayTracing = enabled; }

    const RayTracingSettings& GetSettings() const override { return m_settings; }
    RayTracingSettings& GetSettingsRef() { return m_settings; }
    void SetSettings(const RayTracingSettings& settings) override { m_settings = settings; }

    const RayTracingStats& GetStats() const override { return m_stats; }
    void ResetStats() override;

    void ApplyPreset(RayTracingQuality quality) override;

    void CheckHardwareSupport();
    void RecordFrameRays(uint32_t reflections, uint32_t shadows, uint32_t ao, float timeMs);

private:
    RayTracingSystem();
    ~RayTracingSystem() override = default;

    RayTracingSettings m_settings;
    RayTracingStats m_stats;
    mutable std::mutex m_mutex;
    bool m_initialized = false;
};

} // namespace RayTracing
} // namespace Eunoia
