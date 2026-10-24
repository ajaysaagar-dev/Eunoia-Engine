// ============================================================================
// Eunoia Engine — Texture Streaming Plugin
// Plugin implementation
// ============================================================================

#include <TextureStreaming/TextureStreamingPlugin.h>
#include <iostream>

namespace Eunoia {

void TextureStreamingPlugin::OnRegister(ServiceRegistry& /*registry*/) {
    // Registered in service registry
}

void TextureStreamingPlugin::OnInit() {
    std::cout << "[TextureStreamingPlugin] Wicked Engine DX12 Viewport Visibility Texture Streaming initialized.\n";
}

void TextureStreamingPlugin::OnUpdate(float /*deltaTime*/) {
    // Streaming updates run on render thread via TextureStreamingSystem::Get().Update()
}

void TextureStreamingPlugin::OnShutdown() {
    TextureStreamingSystem::Get().Shutdown();
}

} // namespace Eunoia
