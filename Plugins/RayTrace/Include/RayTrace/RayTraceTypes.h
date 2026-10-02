#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>
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

struct GameObjectHitProxy {
    GameObject* ptr = nullptr;

    GameObjectHitProxy(GameObject* p = nullptr) : ptr(p) {}

    GameObjectHitProxy& operator=(GameObject* p) {
        ptr = p;
        return *this;
    }

    // Implicit conversion to GameObject* and GameObject&
    operator GameObject*() const { return ptr; }
    operator GameObject&() const;
    GameObject* operator->() const { return ptr; }
    GameObject& operator*() const;
    bool operator==(std::nullptr_t) const { return ptr == nullptr; }
    bool operator!=(std::nullptr_t) const { return ptr != nullptr; }
    explicit operator bool() const { return ptr != nullptr; }

    // Direct Name accessor supporting Hit.GameObject.Name
    struct NameAccessor {
        const GameObjectHitProxy* parent = nullptr;
        NameAccessor(const GameObjectHitProxy* p = nullptr) : parent(p) {}

        std::string str() const;
        operator std::string() const { return str(); }
        const char* c_str() const;
        bool empty() const { return str().empty(); }
        size_t size() const { return str().size(); }
        size_t length() const { return str().length(); }
        bool operator==(const std::string& other) const { return str() == other; }
        bool operator==(const char* other) const { return str() == (other ? other : ""); }
        bool operator!=(const std::string& other) const { return str() != other; }
        bool operator!=(const char* other) const { return str() != (other ? other : ""); }
        friend std::ostream& operator<<(std::ostream& os, const NameAccessor& na) {
            os << na.str();
            return os;
        }
    };

    NameAccessor Name{this};
    NameAccessor name{this};

    // Member forwards with dot syntax
    int GetId() const;
    glm::vec3 GetPosition() const;
    glm::vec3 GetRotation() const;
    glm::vec3 GetScale() const;
};

struct RayTraceHit {
    // Status flag (true if hit, false if nothing hit)
    bool Status = false;
    bool status = false;
    bool hasHit = false;

    // Hit GameObject accessors (supporting Hit.GameObject, Hit.GameObject.Name, Hit.gameObject->...)
    GameObjectHitProxy GameObject{nullptr};
    ::GameObject* gameObject = nullptr;

    // Hit location and geometry
    glm::vec3 Point{0.0f};
    glm::vec3 point{0.0f};
    glm::vec3 Normal{0.0f, 1.0f, 0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float Distance = 0.0f;
    float distance = 0.0f;
    float Fraction = 1.0f;
    float fraction = 1.0f;

    // Hit metadata
    int ObjectId = -1;
    int objectId = -1;
    int ModifierIndex = -1;
    int modifierIndex = -1;
    std::string ModifierName = "";
    std::string modifierName = "";
    CollisionShapeType ShapeType = CollisionShapeType::None;
    CollisionShapeType shapeType = CollisionShapeType::None;

    RayTraceHit() = default;

    void SetHit(bool hit, ::GameObject* obj, const glm::vec3& pt, const glm::vec3& n, float dist, float frac, int objId, int modIdx, const std::string& modName, CollisionShapeType shape);

    // Evaluates to true if an object was hit
    explicit operator bool() const {
        return Status && (gameObject != nullptr);
    }

    bool operator!() const {
        return !Status || (gameObject == nullptr);
    }

    // Direct pointer operator to hit GameObject
    ::GameObject* operator->() const {
        return gameObject;
    }

    ::GameObject& operator*() const;
    operator ::GameObject*() const {
        return gameObject;
    }
    operator ::GameObject&() const;

    ::GameObject* GetGameObject() const {
        return gameObject;
    }

    ::GameObject& Get() const;

    bool IsValid() const {
        return Status && (gameObject != nullptr);
    }
};

} // namespace RayTrace
} // namespace Eunoia

using RayTraceHit = Eunoia::RayTrace::RayTraceHit;
using RayTraceParams = Eunoia::RayTrace::RayTraceParams;
using GameObjectHitProxy = Eunoia::RayTrace::GameObjectHitProxy;
