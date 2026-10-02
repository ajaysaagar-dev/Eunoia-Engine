#include "RayTrace/IRayTraceService.h"
#include "RayTrace/RayTraceSystem.h"
#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>
#include <iostream>

namespace Eunoia {
namespace RayTrace {

class RayTraceServiceImpl : public IRayTraceService {
public:
    RayTraceServiceImpl() = default;
    ~RayTraceServiceImpl() override = default;

    RayTraceHit LineTrace(const Scene& scene, const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params) override {
        return RayTraceSystem::Get().LineTrace(scene, start, end, params);
    }

    std::vector<RayTraceHit> LineTraceMulti(const Scene& scene, const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params) override {
        return RayTraceSystem::Get().LineTraceMulti(scene, start, end, params);
    }

    bool LineTraceTest(const Scene& scene, const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params) override {
        return RayTraceSystem::Get().LineTraceTest(scene, start, end, params);
    }
};

} // namespace RayTrace
} // namespace Eunoia

class RayTracePlugin : public IPlugin {
public:
    RayTracePlugin() = default;
    ~RayTracePlugin() override = default;

    const char* GetName() const override { return "RayTrace"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override {
        std::cout << "[RayTrace] Registering IRayTraceService...\n";
        registry.Register<Eunoia::RayTrace::IRayTraceService>("IRayTraceService", &m_service);
    }

    void OnInit() override {
        Eunoia::RayTrace::RayTraceSystem::Get().Initialize();
        std::cout << "[RayTrace] Plugin initialized.\n";
    }

    void OnShutdown() override {
        Eunoia::RayTrace::RayTraceSystem::Get().Shutdown();
        std::cout << "[RayTrace] Plugin shutdown.\n";
    }

private:
    Eunoia::RayTrace::RayTraceServiceImpl m_service;
};
