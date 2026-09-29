#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>
#include <Graphics/GraphicsSystem.h>
#include <iostream>

class GraphicsPlugin : public IPlugin {
public:
    GraphicsPlugin() = default;
    ~GraphicsPlugin() override = default;

    const char* GetName() const override { return "Graphics"; }

    void OnRegister(ServiceRegistry& registry) override {
        registry.Register<Eunoia::IGraphicsSystem>("IGraphicsSystem", &Eunoia::GraphicsSystem::Get());
    }

    void OnInit() override {
        Eunoia::GraphicsSystem::Get().Initialize();
        std::cout << "[GraphicsPlugin] Graphics system and real-time Anti-Aliasing pipeline initialized.\n";
    }

    void OnUpdate(float /*deltaTime*/) override {
        // Real-time graphics updates if needed
    }

    void OnShutdown() override {
        Eunoia::GraphicsSystem::Get().Shutdown();
        std::cout << "[GraphicsPlugin] Graphics system shut down.\n";
    }
};
