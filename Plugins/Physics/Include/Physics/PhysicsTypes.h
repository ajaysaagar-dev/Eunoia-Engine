#pragma once
#include <string>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace Eunoia {
namespace Physics {

struct PhysicsModifier {
    std::string name = "Physics Modifier";
    bool enabled = true;

    // Gravity & Mass properties
    bool useGravity = true;
    float gravityMultiplier = 1.0f;
    float mass = 1.0f; // Mass in kilograms (must be > 0)

    // Dynamics & Damping
    float linearDamping = 0.05f;  // Air resistance / linear drag (0.0 to 1.0)
    float angularDamping = 0.05f; // Rotational drag (0.0 to 1.0)
    float bounciness = 0.2f;      // Restitution coefficient (0.0 = inelastic, 1.0 = superball)
    float friction = 0.6f;        // Surface friction (0.0 = ice, 1.0 = high grip)
    bool isKinematic = false;     // If true, physics forces and gravity are ignored

    // Axis Constraints
    bool lockPositionX = false;
    bool lockPositionY = false;
    bool lockPositionZ = false;
    bool lockRotationX = false;
    bool lockRotationY = false;
    bool lockRotationZ = false;

    // Runtime state (simulation)
    glm::vec3 velocity{0.0f, 0.0f, 0.0f};
    glm::vec3 angularVelocity{0.0f, 0.0f, 0.0f};
    glm::vec3 accumulatedForce{0.0f, 0.0f, 0.0f};
    glm::vec3 accumulatedTorque{0.0f, 0.0f, 0.0f};
    bool isGrounded = false;
    int groundObjectId = -1;
    glm::vec3 contactNormal{0.0f, 1.0f, 0.0f};

    PhysicsModifier() = default;

    void AddForce(const glm::vec3& force) {
        if (!enabled || isKinematic) return;
        accumulatedForce += force;
    }

    void AddImpulse(const glm::vec3& impulse) {
        if (!enabled || isKinematic) return;
        float safeMass = std::max(0.001f, mass);
        velocity += impulse / safeMass;
    }

    void SetVelocity(const glm::vec3& v) {
        velocity = v;
    }

    void ResetVelocity() {
        velocity = glm::vec3(0.0f);
        angularVelocity = glm::vec3(0.0f);
        accumulatedForce = glm::vec3(0.0f);
        accumulatedTorque = glm::vec3(0.0f);
        isGrounded = false;
        groundObjectId = -1;
        contactNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    }
};

} // namespace Physics
} // namespace Eunoia

using PhysicsModifier = Eunoia::Physics::PhysicsModifier;
