#pragma once
#include <glm/glm.hpp>

class Scene;

namespace Eunoia {
namespace Physics {

class IPhysicsService {
public:
    virtual ~IPhysicsService() = default;
    virtual void StepSimulation(Scene& scene, float dt) = 0;
    virtual void ResetSimulation(Scene& scene) = 0;
    virtual void SetGlobalGravity(const glm::vec3& g) = 0;
    virtual glm::vec3 GetGlobalGravity() const = 0;
};

} // namespace Physics
} // namespace Eunoia
