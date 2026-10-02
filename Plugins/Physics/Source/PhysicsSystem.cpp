#include "Physics/PhysicsSystem.h"
#include "Collisions/CollisionSystem.h"
#include <EngineScene/Scene.h>
#include <EngineScene/GameObject.h>
#include <algorithm>
#include <cmath>

namespace Eunoia {
namespace Physics {

PhysicsSystem& PhysicsSystem::Get() {
    static PhysicsSystem instance;
    return instance;
}

void PhysicsSystem::Initialize() {
    m_globalGravity = glm::vec3(0.0f, -9.81f, 0.0f);
}

void PhysicsSystem::Shutdown() {
}

void PhysicsSystem::ResetSimulation(Scene& scene) {
    for (auto& obj : scene.objects) {
        if (obj.hasPhysics) {
            obj.physics.ResetVelocity();
        }
    }
}

void PhysicsSystem::StepSimulation(Scene& scene, float dt) {
    if (dt <= 0.0f) return;

    // Sub-stepping for stability
    const int subSteps = 2;
    float subDt = dt / (float)subSteps;

    for (int step = 0; step < subSteps; ++step) {
        // 1. Integrate forces, gravity, and velocities for all dynamic physics objects
        for (auto& obj : scene.objects) {
            if (!obj.hasPhysics || !obj.physics.enabled || obj.physics.isKinematic) {
                continue;
            }
            IntegratePhysicsBody(obj, subDt);
        }

        // 2. Resolve collisions against all scene colliders
        for (auto& obj : scene.objects) {
            if (!obj.hasPhysics || !obj.physics.enabled || obj.physics.isKinematic) {
                continue;
            }
            ResolveObjectCollisions(scene, obj, subDt);
        }
    }
}

void PhysicsSystem::IntegratePhysicsBody(GameObject& obj, float dt) {
    auto& phys = obj.physics;
    float mass = std::max(0.001f, phys.mass);

    // Gravity acceleration
    glm::vec3 gravityAcc(0.0f);
    if (phys.useGravity) {
        gravityAcc = m_globalGravity * phys.gravityMultiplier;
    }

    // Force acceleration
    glm::vec3 forceAcc = phys.accumulatedForce / mass;
    glm::vec3 totalAcc = gravityAcc + forceAcc;

    // Integrate velocity
    phys.velocity += totalAcc * dt;

    // Apply linear damping (air resistance)
    float dampFactor = std::clamp(1.0f - phys.linearDamping * dt, 0.0f, 1.0f);
    phys.velocity *= dampFactor;

    // Axis position locking
    if (phys.lockPositionX) phys.velocity.x = 0.0f;
    if (phys.lockPositionY) phys.velocity.y = 0.0f;
    if (phys.lockPositionZ) phys.velocity.z = 0.0f;

    // Integrate position
    obj.position += phys.velocity * dt;

    // Clear forces
    phys.accumulatedForce = glm::vec3(0.0f);
    phys.accumulatedTorque = glm::vec3(0.0f);
    phys.isGrounded = false;
    phys.groundObjectId = -1;
}

// Helper struct for bounding box
struct WorldAABB {
    glm::vec3 minPoint{0.0f};
    glm::vec3 maxPoint{0.0f};
    glm::vec3 center{0.0f};
    glm::vec3 halfExtents{0.0f};
};

static WorldAABB ComputeObjectAABB(const GameObject& obj) {
    WorldAABB aabb;
    glm::vec3 halfSize(0.5f);

    if (!obj.modifiers.empty()) {
        const auto& mod = obj.modifiers[0];
        switch (mod.shapeType) {
            case CollisionShapeType::Box:
                halfSize = mod.boxExtents * glm::abs(obj.scale);
                break;
            case CollisionShapeType::Sphere:
                halfSize = glm::vec3(mod.radius) * glm::abs(obj.scale);
                break;
            case CollisionShapeType::Capsule:
            case CollisionShapeType::Cylinder:
            case CollisionShapeType::Cone:
                halfSize = glm::vec3(mod.radius * std::max(std::abs(obj.scale.x), std::abs(obj.scale.z)),
                                     mod.height * 0.5f * std::abs(obj.scale.y),
                                     mod.radius * std::max(std::abs(obj.scale.x), std::abs(obj.scale.z)));
                break;
            case CollisionShapeType::Plane:
                halfSize = glm::vec3(mod.planeSize.x * 0.5f * std::abs(obj.scale.x),
                                     0.05f * std::abs(obj.scale.y),
                                     mod.planeSize.y * 0.5f * std::abs(obj.scale.z));
                break;
            default:
                halfSize = glm::vec3(0.5f) * glm::abs(obj.scale);
                break;
        }
        glm::vec3 offsetWorld = glm::mat3(obj.GetLocalMatrix()) * mod.centerOffset;
        aabb.center = obj.position + offsetWorld;
    } else {
        if (obj.type == PrimitiveType::Sphere || obj.type == PrimitiveType::UVSphere || obj.type == PrimitiveType::Icosphere) {
            halfSize = glm::vec3(obj.params.radius) * glm::abs(obj.scale);
        } else if (obj.type == PrimitiveType::Plane || obj.type == PrimitiveType::Quad) {
            halfSize = glm::vec3(obj.params.width * 0.5f * std::abs(obj.scale.x), 0.05f, obj.params.depth * 0.5f * std::abs(obj.scale.z));
        } else {
            halfSize = glm::vec3(obj.params.width * 0.5f * std::abs(obj.scale.x),
                                 obj.params.height * 0.5f * std::abs(obj.scale.y),
                                 obj.params.depth * 0.5f * std::abs(obj.scale.z));
        }
        if (halfSize.x < 0.05f) halfSize.x = 0.5f * std::abs(obj.scale.x);
        if (halfSize.y < 0.05f) halfSize.y = 0.5f * std::abs(obj.scale.y);
        if (halfSize.z < 0.05f) halfSize.z = 0.5f * std::abs(obj.scale.z);
        aabb.center = obj.position;
    }

    aabb.halfExtents = halfSize;
    aabb.minPoint = aabb.center - halfSize;
    aabb.maxPoint = aabb.center + halfSize;
    return aabb;
}

void PhysicsSystem::ResolveObjectCollisions(Scene& scene, GameObject& objA, float dt) {
    WorldAABB aabbA = ComputeObjectAABB(objA);
    auto& physA = objA.physics;

    for (auto& objB : scene.objects) {
        if (objB.id == objA.id) continue;
        if (!objB.visible) continue;
        if (objB.isLight || IsLightPrimitive(objB.type) || objB.isCamera || IsCameraPrimitive(objB.type) || objB.type == PrimitiveType::Empty) {
            continue;
        }

        // If B has modifiers and all are triggers/disabled, skip physical collision
        bool bHasSolidCollision = true;
        if (!objB.modifiers.empty()) {
            bool anyEnabled = false;
            for (const auto& m : objB.modifiers) {
                if (m.enabled && !m.isTrigger) { anyEnabled = true; break; }
            }
            if (!anyEnabled) bHasSolidCollision = false;
        }
        if (!bHasSolidCollision) continue;

        WorldAABB aabbB = ComputeObjectAABB(objB);

        // Check AABB overlap
        bool overlapX = (aabbA.minPoint.x <= aabbB.maxPoint.x && aabbA.maxPoint.x >= aabbB.minPoint.x);
        bool overlapY = (aabbA.minPoint.y <= aabbB.maxPoint.y && aabbA.maxPoint.y >= aabbB.minPoint.y);
        bool overlapZ = (aabbA.minPoint.z <= aabbB.maxPoint.z && aabbA.maxPoint.z >= aabbB.minPoint.z);

        if (!overlapX || !overlapY || !overlapZ) {
            continue;
        }

        // Calculate overlap depths along X, Y, Z
        float penX1 = aabbB.maxPoint.x - aabbA.minPoint.x;
        float penX2 = aabbA.maxPoint.x - aabbB.minPoint.x;
        float penX = std::min(penX1, penX2);

        float penY1 = aabbB.maxPoint.y - aabbA.minPoint.y;
        float penY2 = aabbA.maxPoint.y - aabbB.minPoint.y;
        float penY = std::min(penY1, penY2);

        float penZ1 = aabbB.maxPoint.z - aabbA.minPoint.z;
        float penZ2 = aabbA.maxPoint.z - aabbB.minPoint.z;
        float penZ = std::min(penZ1, penZ2);

        // Plane collision special handling (prefer Y normal for landing on flat ground/planes)
        bool isPlaneB = (objB.type == PrimitiveType::Plane || objB.type == PrimitiveType::Quad ||
                         (!objB.modifiers.empty() && objB.modifiers[0].shapeType == CollisionShapeType::Plane));

        glm::vec3 normal(0.0f);
        float penDepth = 0.0f;

        if (isPlaneB || (penY <= penX && penY <= penZ)) {
            penDepth = penY;
            normal = (aabbA.center.y > aabbB.center.y) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, -1.0f, 0.0f);
        } else if (penX <= penZ) {
            penDepth = penX;
            normal = (aabbA.center.x > aabbB.center.x) ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(-1.0f, 0.0f, 0.0f);
        } else {
            penDepth = penZ;
            normal = (aabbA.center.z > aabbB.center.z) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 0.0f, -1.0f);
        }

        if (penDepth <= 0.0f) continue;

        // Position separation
        if (objB.hasPhysics && objB.physics.enabled && !objB.physics.isKinematic) {
            float totalMass = physA.mass + objB.physics.mass;
            float ratioA = objB.physics.mass / totalMass;
            float ratioB = physA.mass / totalMass;
            objA.position += normal * (penDepth * ratioA);
            objB.position -= normal * (penDepth * ratioB);
        } else {
            objA.position += normal * penDepth;
        }

        // Recompute AABB after position shift
        aabbA = ComputeObjectAABB(objA);

        // Velocity resolution
        glm::vec3 velB = (objB.hasPhysics && objB.physics.enabled) ? objB.physics.velocity : glm::vec3(0.0f);
        glm::vec3 relVel = physA.velocity - velB;
        float velAlongNormal = glm::dot(relVel, normal);

        if (velAlongNormal < 0.0f) {
            float restitution = std::clamp(physA.bounciness, 0.0f, 1.0f);
            float friction = std::clamp(physA.friction, 0.0f, 1.0f);

            // Check if landing on floor/ground
            if (normal.y > 0.5f) {
                physA.isGrounded = true;
                physA.groundObjectId = objB.id;
                physA.contactNormal = normal;

                if (std::abs(velAlongNormal) < 0.35f) {
                    // Resting on surface: zero out vertical velocity to prevent micro-bouncing
                    physA.velocity.y = 0.0f;
                    // Apply ground friction to horizontal motion
                    float fDamp = std::clamp(1.0f - friction * 12.0f * dt, 0.0f, 1.0f);
                    physA.velocity.x *= fDamp;
                    physA.velocity.z *= fDamp;
                } else {
                    // Bouncing off floor
                    physA.velocity -= (1.0f + restitution) * velAlongNormal * normal;
                    float fDamp = std::clamp(1.0f - friction * 4.0f * dt, 0.0f, 1.0f);
                    physA.velocity.x *= fDamp;
                    physA.velocity.z *= fDamp;
                }
            } else {
                // Wall or ceiling bounce
                physA.velocity -= (1.0f + restitution) * velAlongNormal * normal;
                float fDamp = std::clamp(1.0f - friction * 4.0f * dt, 0.0f, 1.0f);
                physA.velocity *= fDamp;
            }

            // If B also has physics, apply reaction impulse
            if (objB.hasPhysics && objB.physics.enabled && !objB.physics.isKinematic) {
                float impulse = -(1.0f + restitution) * velAlongNormal / (1.0f / physA.mass + 1.0f / objB.physics.mass);
                glm::vec3 impulseVec = impulse * normal;
                physA.velocity += impulseVec / physA.mass;
                objB.physics.velocity -= impulseVec / objB.physics.mass;
            }
        }
    }
}

} // namespace Physics
} // namespace Eunoia
