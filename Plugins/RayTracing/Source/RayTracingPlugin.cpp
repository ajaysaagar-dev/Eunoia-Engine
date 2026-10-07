// ============================================================================
// Eunoia Engine — Ray-Tracing Plugin
// RayTracingPlugin.cpp: Plugin lifecycle and service registration
// ============================================================================

#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>
#include "RayTracing/RayTracingSystem.h"
#include <iostream>

namespace Eunoia {
namespace RayTracing {

class RayTracingPlugin : public IPlugin {
public:
    RayTracingPlugin() = default;
    ~RayTracingPlugin() override = default;

    const char* GetName() const override { return "Ray-Tracing"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override {
        std::cout << "[RayTracingPlugin] Registering IRayTracingSystem service...\n";
        registry.Register<IRayTracingSystem>("IRayTracingSystem", &RayTracingSystem::Get());
    }

    void OnInit() override {
        RayTracingSystem::Get().Initialize();
        std::cout << "[RayTracingPlugin] Ray-Tracing plugin initialized.\n";
    }

    void OnShutdown() override {
        RayTracingSystem::Get().Shutdown();
        std::cout << "[RayTracingPlugin] Ray-Tracing plugin shutdown.\n";
    }
};

} // namespace RayTracing
} // namespace Eunoia
