#pragma once
#include "PhysicsTypes.h"
#include "IPhysicsService.h"
#include <vector>
#include <memory>
#include <glm/glm.hpp>

class Scene;
struct GameObject;

namespace Eunoia {
namespace Physics {

class PhysicsSystem : public IPhysicsService {
public:
    static PhysicsSystem& Get();

    void Initialize();
    void Shutdown();

    void StepSimulation(Scene& scene, float dt) override;
    void ResetSimulation(Scene& scene) override;

    void SetGlobalGravity(const glm::vec3& g) override { m_globalGravity = g; }
    glm::vec3 GetGlobalGravity() const override { return m_globalGravity; }

private:
    PhysicsSystem() = default;
    ~PhysicsSystem() override = default;

    glm::vec3 m_globalGravity{0.0f, -9.81f, 0.0f};

    void IntegratePhysicsBody(GameObject& obj, float dt);
    void ResolveObjectCollisions(Scene& scene, GameObject& obj, float dt);
};

} // namespace Physics
} // namespace Eunoia
