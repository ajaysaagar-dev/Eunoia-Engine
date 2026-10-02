#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

enum class CollisionShapeType {
    None = 0,
    Box = 1,
    Sphere = 2,
    Capsule = 3,
    Cylinder = 4,
    Cone = 5,
    Plane = 6,
    Mesh = 7
};

inline const char* GetCollisionShapeTypeName(CollisionShapeType shape) {
    switch (shape) {
        case CollisionShapeType::None:     return "None";
        case CollisionShapeType::Box:      return "Box Collision";
        case CollisionShapeType::Sphere:   return "Sphere Collision";
        case CollisionShapeType::Capsule:  return "Capsule Collision";
        case CollisionShapeType::Cylinder: return "Cylinder Collision";
        case CollisionShapeType::Cone:     return "Cone Collision";
        case CollisionShapeType::Plane:    return "Plane Collision";
        case CollisionShapeType::Mesh:     return "Mesh Collision";
        default:                           return "Custom Collision";
    }
}

inline CollisionShapeType StringToCollisionShapeType(const std::string& str) {
    if (str == "None") return CollisionShapeType::None;
    if (str == "Box" || str == "Box Collision") return CollisionShapeType::Box;
    if (str == "Sphere" || str == "Sphere Collision") return CollisionShapeType::Sphere;
    if (str == "Capsule" || str == "Capsule Collision") return CollisionShapeType::Capsule;
    if (str == "Cylinder" || str == "Cylinder Collision") return CollisionShapeType::Cylinder;
    if (str == "Cone" || str == "Cone Collision") return CollisionShapeType::Cone;
    if (str == "Plane" || str == "Plane Collision") return CollisionShapeType::Plane;
    if (str == "Mesh" || str == "Mesh Collision") return CollisionShapeType::Mesh;
    return CollisionShapeType::Box;
}

struct RaycastHitResult {
    bool hit = false;
    float distance = 0.0f;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    int objectId = -1;
    int modifierIndex = -1;
};

struct CollisionHitResult {
    bool hasCollision = false;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float penetrationDepth = 0.0f;
    glm::vec3 contactPoint{0.0f};
    int objectAId = -1;
    int objectBId = -1;
    int modifierAIndex = -1;
    int modifierBIndex = -1;
};

struct CollisionModifier {
    std::string name = "Box Collision";
    bool enabled = true;
    CollisionShapeType shapeType = CollisionShapeType::Box;
    bool isTrigger = false;

    // Dimensions (Local space)
    glm::vec3 boxExtents{0.5f, 0.5f, 0.5f}; // Half-extents for Box (size is 2 * boxExtents)
    float radius = 0.5f;                    // Radius for Sphere, Capsule, Cylinder, Cone
    float height = 1.0f;                    // Total height for Capsule, Cylinder, Cone
    glm::vec2 planeSize{2.5f, 2.5f};        // Width and length for Plane
    glm::vec3 centerOffset{0.0f, 0.0f, 0.0f}; // Offset relative to object pivot

    // Physical Material properties
    float friction = 0.5f;                  // Surface friction (0.0 = ice, 1.0 = rubber)
    float restitution = 0.0f;               // Bounciness (0.0 = inelastic, 1.0 = elastic)
    std::string preset = "Default";         // Preset ("Default", "BlockAll", "OverlapAll", "TriggerOnly", "NoCollision")

    // Visualization
    bool showWireframe = true;
    glm::vec4 wireframeColor{0.0f, 1.0f, 0.25f, 1.0f}; // Bright green by default
    bool isDefaultFit = true;               // Whether current dimensions match the mesh/primitive defaults

    CollisionModifier() = default;
    CollisionModifier(CollisionShapeType shape, const std::string& customName = "") {
        shapeType = shape;
        name = customName.empty() ? GetCollisionShapeTypeName(shape) : customName;
    }

    // Apply preset settings
    void ApplyPreset(const std::string& presetName) {
        preset = presetName;
        if (presetName == "NoCollision") {
            enabled = false;
        } else if (presetName == "TriggerOnly") {
            enabled = true;
            isTrigger = true;
            wireframeColor = glm::vec4(1.0f, 0.8f, 0.0f, 1.0f); // Orange-yellow for triggers
        } else if (presetName == "OverlapAll") {
            enabled = true;
            isTrigger = true;
            wireframeColor = glm::vec4(0.2f, 0.8f, 1.0f, 1.0f); // Cyan
        } else if (presetName == "BlockAll") {
            enabled = true;
            isTrigger = false;
            wireframeColor = glm::vec4(0.0f, 1.0f, 0.25f, 1.0f); // Bright green
        } else { // "Default"
            enabled = true;
            isTrigger = false;
            wireframeColor = glm::vec4(0.0f, 1.0f, 0.25f, 1.0f);
        }
    }
};

using CollisionComponent = CollisionModifier;
