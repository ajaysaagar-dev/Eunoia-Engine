#pragma once
#include "VolumetricAtmosphereTypes.h"

namespace Eunoia {
namespace Atmosphere {

class IVolumetricAtmosphere {
public:
    virtual ~IVolumetricAtmosphere() = default;

    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual bool IsEnabled() const = 0;
    virtual void SetEnabled(bool enabled) = 0;

    virtual const VolumetricAtmosphereSettings& GetSettings() const = 0;
    virtual void SetSettings(const VolumetricAtmosphereSettings& settings) = 0;
    virtual VolumetricAtmosphereSettings& GetSettingsRef() = 0;

    virtual void Update(float deltaTime) = 0;
};

} // namespace Atmosphere
} // namespace Eunoia
