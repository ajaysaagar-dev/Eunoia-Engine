#pragma once
#include "DDGITypes.h"

namespace Eunoia {
namespace GI {

class IGlobalIllumination {
public:
    virtual ~IGlobalIllumination() = default;

    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual bool IsEnabled() const = 0;
    virtual void SetEnabled(bool enabled) = 0;

    virtual const DDGIVolumeSettings& GetSettings() const = 0;
    virtual DDGIVolumeSettings& GetSettingsRef() = 0;
    virtual void SetSettings(const DDGIVolumeSettings& settings) = 0;

    virtual const DDGIStats& GetStats() const = 0;
    virtual void Update(float deltaTime) = 0;
};

} // namespace GI
} // namespace Eunoia
