#pragma once
#include "RayTracingTypes.h"
#include <functional>

namespace Eunoia {
namespace RayTracing {

class IRayTracingSystem {
public:
    virtual ~IRayTracingSystem() = default;

    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual bool IsHardwareDxrSupported() const = 0;
    virtual bool IsEnabled() const = 0;
    virtual void SetEnabled(bool enabled) = 0;

    virtual const RayTracingSettings& GetSettings() const = 0;
    virtual void SetSettings(const RayTracingSettings& settings) = 0;

    virtual const RayTracingStats& GetStats() const = 0;
    virtual void ResetStats() = 0;

    // Apply preset
    virtual void ApplyPreset(RayTracingQuality quality) = 0;
};

} // namespace RayTracing
} // namespace Eunoia
