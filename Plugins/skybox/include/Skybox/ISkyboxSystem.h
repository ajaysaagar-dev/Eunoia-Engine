#pragma once
#include <EunoiaPluginCore/IPlugin.h>

class ISkyboxSystem : public IPlugin {
public:
    virtual bool GetSkyboxEnabled() const = 0;
    virtual void SetSkyboxEnabled(bool enabled) = 0;
};
