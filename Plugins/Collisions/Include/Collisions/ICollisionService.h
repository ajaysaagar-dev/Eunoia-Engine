#pragma once
#include "CollisionTypes.h"
#include <vector>

struct GameObject;
class Scene;

namespace Collisions {

class ICollisionService {
public:
    virtual ~ICollisionService() = default;

    virtual RaycastHitResult Raycast(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 1000.0f) = 0;
    virtual bool CheckOverlap(const GameObject& a, const GameObject& b, CollisionHitResult& outResult) = 0;
    virtual void AutoFitBounds(GameObject& obj) = 0;
    virtual void ResetToDefaultBounds(GameObject& obj) = 0;
};

} // namespace Collisions
