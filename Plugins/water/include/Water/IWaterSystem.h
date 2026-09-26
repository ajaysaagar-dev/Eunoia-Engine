#pragma once
#include <EunoiaPluginCore/IPlugin.h>
#include <EngineScene/GameObject.h>

class IWaterSystem : public IPlugin {
public:
    virtual int GetActiveWaterBodyCount() const = 0;
    virtual void EnableWaterSystem(bool enable) = 0;
};
