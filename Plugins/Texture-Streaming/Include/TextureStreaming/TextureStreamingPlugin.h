#pragma once
// ============================================================================
// Eunoia Engine — Texture Streaming Plugin
// Plugin registration header
// ============================================================================

#include <EunoiaPluginCore/IPlugin.h>
#include "TextureStreamingSystem.h"

namespace Eunoia {

class TextureStreamingPlugin : public IPlugin {
public:
    TextureStreamingPlugin() = default;
    ~TextureStreamingPlugin() override = default;

    void OnRegister(ServiceRegistry& registry) override;
    void OnInit() override;
    void OnUpdate(float deltaTime) override;
    void OnShutdown() override;
};

} // namespace Eunoia
