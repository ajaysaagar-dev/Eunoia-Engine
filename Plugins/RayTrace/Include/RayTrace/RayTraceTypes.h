#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <Collisions/CollisionTypes.h>

struct GameObject;

namespace Eunoia {
namespace RayTrace {

struct RayTraceParams {
    int ignoreObjectId = -1;
    const GameObject* ignoreObject = nullptr;
    bool ignoreTriggers = false;
    bool ignoreInvisible = true;
    float maxDistance = 100000.0f;

    RayTraceParams() = default;
    RayTraceParams(int ignoreId) : ignoreObjectId(ignoreId) {}
    RayTraceParams(const GameObject* ignoreObj) : ignoreObject(ignoreObj) {}
};

struct RayTraceHit {
    bool hasHit = false;
    GameObject* gameObject = nullptr;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float distance = 0.0f;
    float fraction = 1.0f;
    int objectId = -1;
    int modifierIndex = -1;
    std::string modifierName = "";
    CollisionShapeType shapeType = CollisionShapeType::None;

    RayTraceHit() = default;

    // Evaluates to true if an object was hit
    explicit operator bool() const {
        return hasHit && (gameObject != nullptr);
    }

    bool operator!() const {
        return !hasHit || (gameObject == nullptr);
    }

    // Direct access to hit GameObject
    GameObject* operator->() const {
        return gameObject;
    }

    GameObject& operator*() const {
        return *gameObject;
    }

    operator GameObject*() const {
        return gameObject;
    }

    operator GameObject&() const {
        return *gameObject;
    }

    GameObject* GetGameObject() const {
        return gameObject;
    }

    GameObject& Get() const {
        return *gameObject;
    }

    bool IsValid() const {
        return hasHit && (gameObject != nullptr);
    }
};

} // namespace RayTrace
} // namespace Eunoia

using RayTraceHit = Eunoia::RayTrace::RayTraceHit;
using RayTraceParams = Eunoia::RayTrace::RayTraceParams;
