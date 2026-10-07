#include "Physics/IPhysicsService.h"
#include "Physics/PhysicsSystem.h"
#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>
#include <iostream>

namespace Eunoia {
namespace Physics {

class PhysicsServiceImpl : public IPhysicsService {
public:
    PhysicsServiceImpl() = default;
    ~PhysicsServiceImpl() override = default;

    void StepSimulation(Scene& scene, float dt) override {
        PhysicsSystem::Get().StepSimulation(scene, dt);
    }

    void ResetSimulation(Scene& scene) override {
        PhysicsSystem::Get().ResetSimulation(scene);
    }

    void SetGlobalGravity(const glm::vec3& g) override {
        PhysicsSystem::Get().SetGlobalGravity(g);
    }

    glm::vec3 GetGlobalGravity() const override {
        return PhysicsSystem::Get().GetGlobalGravity();
    }
};

} // namespace Physics
} // namespace Eunoia

class PhysicsPlugin : public IPlugin {
public:
    PhysicsPlugin() = default;
    ~PhysicsPlugin() override = default;

    const char* GetName() const override { return "Physics"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override {
        std::cout << "[Physics] Registering IPhysicsService...\n";
        registry.Register<Eunoia::Physics::IPhysicsService>("IPhysicsService", &m_service);
    }

    void OnInit() override {
        Eunoia::Physics::PhysicsSystem::Get().Initialize();
        std::cout << "[Physics] Plugin initialized.\n";
    }

    void OnShutdown() override {
        Eunoia::Physics::PhysicsSystem::Get().Shutdown();
        std::cout << "[Physics] Plugin shutdown.\n";
    }

private:
    Eunoia::Physics::PhysicsServiceImpl m_service;
};
