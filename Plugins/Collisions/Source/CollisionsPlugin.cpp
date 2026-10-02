#include "Collisions/ICollisionService.h"
#include "Collisions/CollisionSystem.h"
#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>
#include <iostream>

namespace Collisions {

class CollisionServiceImpl : public ICollisionService {
public:
    CollisionServiceImpl() = default;
    ~CollisionServiceImpl() override = default;

    RaycastHitResult Raycast(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float maxDistance) override {
        return CollisionSystem::Get().Raycast(scene, origin, direction, maxDistance);
    }

    bool CheckOverlap(const GameObject& a, const GameObject& b, CollisionHitResult& outResult) override {
        return false;
    }

    void AutoFitBounds(GameObject& obj) override {
        CollisionSystem::Get().AutoFitBounds(obj);
    }

    void ResetToDefaultBounds(GameObject& obj) override {
        CollisionSystem::Get().ResetToDefaultBounds(obj);
    }
};

} // namespace Collisions

class CollisionsPlugin : public IPlugin {
public:
    CollisionsPlugin() = default;
    ~CollisionsPlugin() override = default;

    const char* GetName() const override { return "Collisions"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override {
        std::cout << "[Collisions] Registering ICollisionService...\n";
        registry.Register<Collisions::ICollisionService>("ICollisionService", &m_service);
    }

    void OnInit() override {
        std::cout << "[Collisions] Plugin initialized.\n";
    }

    void OnShutdown() override {
        std::cout << "[Collisions] Plugin shutdown.\n";
    }

private:
    Collisions::CollisionServiceImpl m_service;
};
