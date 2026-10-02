#include "Physics/PhysicsSystem.h"
#include "Collisions/CollisionSystem.h"
#include <EngineScene/Scene.h>
#include <EngineScene/GameObject.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

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

static glm::mat3 GetRotationMatrix(const glm::vec3& eulerDeg) {
    glm::mat4 m(1.0f);
    m = glm::rotate(m, glm::radians(eulerDeg.x), glm::vec3(1, 0, 0));
    m = glm::rotate(m, glm::radians(eulerDeg.y), glm::vec3(0, 1, 0));
    m = glm::rotate(m, glm::radians(eulerDeg.z), glm::vec3(0, 0, 1));
    return glm::mat3(m);
}

static glm::mat3 ComputeBodyInertiaInv(const GameObject& obj) {
    float m = std::max(0.001f, obj.physics.mass);
    glm::vec3 halfExtents(0.5f);

    if (!obj.modifiers.empty()) {
        const auto& mod = obj.modifiers[0];
        if (mod.shapeType == CollisionShapeType::Box || mod.shapeType == CollisionShapeType::Mesh) {
            halfExtents = mod.boxExtents * glm::abs(obj.scale);
        } else if (mod.shapeType == CollisionShapeType::Sphere) {
            halfExtents = glm::vec3(mod.radius) * glm::abs(obj.scale);
        } else {
            halfExtents = glm::vec3(mod.radius, mod.height * 0.5f, mod.radius) * glm::abs(obj.scale);
        }
    } else {
        halfExtents = glm::vec3(obj.params.width * 0.5f, obj.params.height * 0.5f, obj.params.depth * 0.5f) * glm::abs(obj.scale);
        if (halfExtents.x < 0.05f) halfExtents.x = 0.5f * std::abs(obj.scale.x);
        if (halfExtents.y < 0.05f) halfExtents.y = 0.5f * std::abs(obj.scale.y);
        if (halfExtents.z < 0.05f) halfExtents.z = 0.5f * std::abs(obj.scale.z);
    }

    glm::vec3 size = halfExtents * 2.0f;
    float ix = (1.0f / 12.0f) * m * (size.y * size.y + size.z * size.z);
    float iy = (1.0f / 12.0f) * m * (size.x * size.x + size.z * size.z);
    float iz = (1.0f / 12.0f) * m * (size.x * size.x + size.y * size.y);

    return glm::mat3(
        1.0f / std::max(1e-4f, ix), 0.0f, 0.0f,
        0.0f, 1.0f / std::max(1e-4f, iy), 0.0f,
        0.0f, 0.0f, 1.0f / std::max(1e-4f, iz)
    );
}

static std::vector<glm::vec3> GetObjectContactSamplePoints(const GameObject& obj) {
    std::vector<glm::vec3> pts;
    glm::mat3 R = GetRotationMatrix(obj.rotation);
    glm::vec3 h(0.5f);

    if (!obj.modifiers.empty()) {
        const auto& mod = obj.modifiers[0];
        if (mod.shapeType == CollisionShapeType::Sphere) {
            float r = mod.radius * std::max({std::abs(obj.scale.x), std::abs(obj.scale.y), std::abs(obj.scale.z)});
            // 6 poles + 8 diagonal points
            pts.push_back(R * glm::vec3(0, -r, 0));
            pts.push_back(R * glm::vec3(0, r, 0));
            pts.push_back(R * glm::vec3(-r, 0, 0));
            pts.push_back(R * glm::vec3(r, 0, 0));
            pts.push_back(R * glm::vec3(0, 0, -r));
            pts.push_back(R * glm::vec3(0, 0, r));
            return pts;
        } else if (mod.shapeType == CollisionShapeType::Box || mod.shapeType == CollisionShapeType::Mesh) {
            h = mod.boxExtents * glm::abs(obj.scale);
        } else {
            h = glm::vec3(mod.radius, mod.height * 0.5f, mod.radius) * glm::abs(obj.scale);
        }
    } else {
        if (obj.type == PrimitiveType::Sphere || obj.type == PrimitiveType::UVSphere || obj.type == PrimitiveType::Icosphere) {
            float r = obj.params.radius * std::max({std::abs(obj.scale.x), std::abs(obj.scale.y), std::abs(obj.scale.z)});
            pts.push_back(R * glm::vec3(0, -r, 0));
            pts.push_back(R * glm::vec3(0, r, 0));
            pts.push_back(R * glm::vec3(-r, 0, 0));
            pts.push_back(R * glm::vec3(r, 0, 0));
            pts.push_back(R * glm::vec3(0, 0, -r));
            pts.push_back(R * glm::vec3(0, 0, r));
            return pts;
        }
        h = glm::vec3(obj.params.width * 0.5f, obj.params.height * 0.5f, obj.params.depth * 0.5f) * glm::abs(obj.scale);
        if (h.x < 0.05f) h.x = 0.5f * std::abs(obj.scale.x);
        if (h.y < 0.05f) h.y = 0.5f * std::abs(obj.scale.y);
        if (h.z < 0.05f) h.z = 0.5f * std::abs(obj.scale.z);
    }

    // 8 box corners (in body space, transformed by rotation R)
    pts.push_back(R * glm::vec3(-h.x, -h.y, -h.z));
    pts.push_back(R * glm::vec3( h.x, -h.y, -h.z));
    pts.push_back(R * glm::vec3( h.x, -h.y,  h.z));
    pts.push_back(R * glm::vec3(-h.x, -h.y,  h.z));
    pts.push_back(R * glm::vec3(-h.x,  h.y, -h.z));
    pts.push_back(R * glm::vec3( h.x,  h.y, -h.z));
    pts.push_back(R * glm::vec3( h.x,  h.y,  h.z));
    pts.push_back(R * glm::vec3(-h.x,  h.y,  h.z));
    return pts;
}

void PhysicsSystem::StepSimulation(Scene& scene, float dt) {
    if (dt <= 0.0f) return;

    const int subSteps = 4;
    float subDt = dt / (float)subSteps;

    for (int step = 0; step < subSteps; ++step) {
        // 1. Integrate forces, gravity, and velocities
        for (auto& obj : scene.objects) {
            if (!obj.hasPhysics || !obj.physics.enabled || obj.physics.isKinematic) {
                continue;
            }
            IntegratePhysicsBody(obj, subDt);
        }

        // 2. Resolve realistic 3D contact collisions & torques
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

    // Linear force & acceleration
    glm::vec3 linearAcc = gravityAcc + (phys.accumulatedForce / mass);
    phys.velocity += linearAcc * dt;

    // Linear damping (air drag)
    float linearDampFactor = std::clamp(1.0f - phys.linearDamping * dt, 0.0f, 1.0f);
    phys.velocity *= linearDampFactor;

    // Axis position locking
    if (phys.lockPositionX) phys.velocity.x = 0.0f;
    if (phys.lockPositionY) phys.velocity.y = 0.0f;
    if (phys.lockPositionZ) phys.velocity.z = 0.0f;

    // Position integration
    obj.position += phys.velocity * dt;

    // Angular torque & acceleration
    glm::mat3 R = GetRotationMatrix(obj.rotation);
    glm::mat3 I_inv_body = ComputeBodyInertiaInv(obj);
    glm::mat3 I_inv_world = R * I_inv_body * glm::transpose(R);

    glm::vec3 angularAcc = I_inv_world * phys.accumulatedTorque;
    phys.angularVelocity += angularAcc * dt;

    // Angular damping
    float angularDampFactor = std::clamp(1.0f - phys.angularDamping * dt, 0.0f, 1.0f);
    phys.angularVelocity *= angularDampFactor;

    // Axis rotation locking
    if (phys.lockRotationX) phys.angularVelocity.x = 0.0f;
    if (phys.lockRotationY) phys.angularVelocity.y = 0.0f;
    if (phys.lockRotationZ) phys.angularVelocity.z = 0.0f;

    // Rotation integration (degrees)
    if (glm::length(phys.angularVelocity) > 1e-6f) {
        glm::vec3 dRotDeg = glm::degrees(phys.angularVelocity * dt);
        if (phys.lockRotationX) dRotDeg.x = 0.0f;
        if (phys.lockRotationY) dRotDeg.y = 0.0f;
        if (phys.lockRotationZ) dRotDeg.z = 0.0f;
        obj.rotation += dRotDeg;

        // Keep angles normalized
        for (int a = 0; a < 3; ++a) {
            float& ang = (a == 0 ? obj.rotation.x : (a == 1 ? obj.rotation.y : obj.rotation.z));
            ang = std::fmod(ang + 180.0f, 360.0f);
            if (ang < 0.0f) ang += 360.0f;
            ang -= 180.0f;
        }
    }

    // Reset accumulated external forces/torques
    phys.accumulatedForce = glm::vec3(0.0f);
    phys.accumulatedTorque = glm::vec3(0.0f);
    phys.isGrounded = false;
    phys.groundObjectId = -1;
}

void PhysicsSystem::ResolveObjectCollisions(Scene& scene, GameObject& objA, float dt) {
    auto& physA = objA.physics;
    float massA = std::max(0.001f, physA.mass);
    glm::mat3 RA = GetRotationMatrix(objA.rotation);
    glm::mat3 I_inv_worldA = RA * ComputeBodyInertiaInv(objA) * glm::transpose(RA);

    std::vector<glm::vec3> localOffsets = GetObjectContactSamplePoints(objA);
    float restitution = std::clamp(physA.bounciness, 0.0f, 0.95f);
    float friction = std::clamp(physA.friction, 0.0f, 1.0f);

    int groundedContactCount = 0;

    for (auto& objB : scene.objects) {
        if (objB.id == objA.id) continue;
        if (!objB.visible) continue;
        if (objB.isLight || IsLightPrimitive(objB.type) || objB.isCamera || IsCameraPrimitive(objB.type) || objB.type == PrimitiveType::Empty) {
            continue;
        }

        // Check if B has collision enabled
        if (!objB.modifiers.empty()) {
            bool anySolid = false;
            for (const auto& m : objB.modifiers) {
                if (m.enabled && !m.isTrigger) { anySolid = true; break; }
            }
            if (!anySolid) continue;
        }

        bool isPlaneB = (objB.type == PrimitiveType::Plane || objB.type == PrimitiveType::Quad ||
                         (!objB.modifiers.empty() && objB.modifiers[0].shapeType == CollisionShapeType::Plane));

        // 1. COLLISION WITH GROUND PLANE
        if (isPlaneB) {
            glm::mat3 RB = GetRotationMatrix(objB.rotation);
            glm::vec3 planeNormal = glm::normalize(RB * glm::vec3(0.0f, 1.0f, 0.0f));
            glm::vec3 planePoint = objB.position;

            glm::vec2 planeSize(2.5f);
            if (!objB.modifiers.empty() && objB.modifiers[0].shapeType == CollisionShapeType::Plane) {
                planeSize = objB.modifiers[0].planeSize * glm::vec2(std::abs(objB.scale.x), std::abs(objB.scale.z));
            } else {
                planeSize = glm::vec2(objB.params.width * std::abs(objB.scale.x), (objB.params.depth > 0.0f ? objB.params.depth : 2.5f) * std::abs(objB.scale.z));
            }
            float halfW = planeSize.x * 0.5f;
            float halfD = planeSize.y * 0.5f;

            for (const auto& r_body : localOffsets) {
                glm::vec3 worldContactPos = objA.position + r_body;
                glm::vec3 delta = worldContactPos - planePoint;

                // Check horizontal bounds relative to plane
                float projX = std::abs(glm::dot(delta, RB * glm::vec3(1.0f, 0.0f, 0.0f)));
                float projZ = std::abs(glm::dot(delta, RB * glm::vec3(0.0f, 0.0f, 1.0f)));
                if (projX > halfW + 1.0f || projZ > halfD + 1.0f) continue;

                float distToPlane = glm::dot(delta, planeNormal);

                if (distToPlane < 0.0f) {
                    float penetration = -distToPlane;
                    groundedContactCount++;

                    // Velocity of the object at this specific corner contact point (linear + angular cross product)
                    glm::vec3 pointVel = physA.velocity + glm::cross(physA.angularVelocity, r_body);
                    float velAlongNormal = glm::dot(pointVel, planeNormal);

                    if (velAlongNormal < 0.0f) {
                        // Effective inertia at contact point along normal
                        glm::vec3 rCrossN = glm::cross(r_body, planeNormal);
                        float effectiveMassInv = (1.0f / massA) + glm::dot(rCrossN, I_inv_worldA * rCrossN);

                        // Normal contact impulse
                        float J_normal = -(1.0f + restitution) * velAlongNormal / std::max(1e-4f, effectiveMassInv);
                        glm::vec3 impulseNormal = J_normal * planeNormal;

                        // Apply normal impulse to linear and angular velocities (generates realistic rotation/toppling!)
                        physA.velocity += impulseNormal / massA;
                        physA.angularVelocity += I_inv_worldA * glm::cross(r_body, impulseNormal);

                        // Tangential Friction Impulse
                        glm::vec3 tangentVel = pointVel - velAlongNormal * planeNormal;
                        float tangentSpeed = glm::length(tangentVel);

                        if (tangentSpeed > 1e-4f) {
                            glm::vec3 tangentDir = -tangentVel / tangentSpeed;
                            glm::vec3 rCrossT = glm::cross(r_body, tangentDir);
                            float tangentMassInv = (1.0f / massA) + glm::dot(rCrossT, I_inv_worldA * rCrossT);

                            float J_tangent = std::min(friction * J_normal, tangentSpeed / std::max(1e-4f, tangentMassInv));
                            glm::vec3 impulseTangent = J_tangent * tangentDir;

                            physA.velocity += impulseTangent / massA;
                            physA.angularVelocity += I_inv_worldA * glm::cross(r_body, impulseTangent);
                        }
                    }

                    // Positional separation to prevent sinking into ground
                    objA.position += planeNormal * (penetration * 0.7f);
                }
            }
        } else {
            // 2. COLLISION WITH OTHER 3D OBJECTS (OBB / Box / Sphere)
            glm::mat3 RB = GetRotationMatrix(objB.rotation);
            glm::vec3 hB(0.5f);
            if (!objB.modifiers.empty()) {
                const auto& modB = objB.modifiers[0];
                hB = modB.boxExtents * glm::abs(objB.scale);
            } else {
                hB = glm::vec3(objB.params.width * 0.5f, objB.params.height * 0.5f, objB.params.depth * 0.5f) * glm::abs(objB.scale);
                if (hB.x < 0.05f) hB.x = 0.5f * std::abs(objB.scale.x);
                if (hB.y < 0.05f) hB.y = 0.5f * std::abs(objB.scale.y);
                if (hB.z < 0.05f) hB.z = 0.5f * std::abs(objB.scale.z);
            }

            for (const auto& r_body : localOffsets) {
                glm::vec3 worldContactPos = objA.position + r_body;
                glm::vec3 localToB = glm::transpose(RB) * (worldContactPos - objB.position);

                // Check if vertex is inside box B
                if (std::abs(localToB.x) < hB.x && std::abs(localToB.y) < hB.y && std::abs(localToB.z) < hB.z) {
                    // Find minimum penetration face
                    float dx = hB.x - std::abs(localToB.x);
                    float dy = hB.y - std::abs(localToB.y);
                    float dz = hB.z - std::abs(localToB.z);

                    glm::vec3 localNormal(0.0f);
                    float pen = 0.0f;
                    if (dy <= dx && dy <= dz) {
                        pen = dy;
                        localNormal = glm::vec3(0.0f, localToB.y > 0.0f ? 1.0f : -1.0f, 0.0f);
                    } else if (dx <= dz) {
                        pen = dx;
                        localNormal = glm::vec3(localToB.x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
                    } else {
                        pen = dz;
                        localNormal = glm::vec3(0.0f, 0.0f, localToB.z > 0.0f ? 1.0f : -1.0f);
                    }

                    glm::vec3 worldNormal = RB * localNormal;
                    groundedContactCount++;

                    glm::vec3 velB = (objB.hasPhysics && objB.physics.enabled) ? objB.physics.velocity : glm::vec3(0.0f);
                    glm::vec3 pointVel = (physA.velocity + glm::cross(physA.angularVelocity, r_body)) - velB;
                    float velAlongNormal = glm::dot(pointVel, worldNormal);

                    if (velAlongNormal < 0.0f) {
                        glm::vec3 rCrossN = glm::cross(r_body, worldNormal);
                        float effectiveMassInv = (1.0f / massA) + glm::dot(rCrossN, I_inv_worldA * rCrossN);

                        float J_normal = -(1.0f + restitution) * velAlongNormal / std::max(1e-4f, effectiveMassInv);
                        glm::vec3 impulseNormal = J_normal * worldNormal;

                        physA.velocity += impulseNormal / massA;
                        physA.angularVelocity += I_inv_worldA * glm::cross(r_body, impulseNormal);

                        glm::vec3 tangentVel = pointVel - velAlongNormal * worldNormal;
                        float tangentSpeed = glm::length(tangentVel);
                        if (tangentSpeed > 1e-4f) {
                            glm::vec3 tangentDir = -tangentVel / tangentSpeed;
                            glm::vec3 rCrossT = glm::cross(r_body, tangentDir);
                            float tangentMassInv = (1.0f / massA) + glm::dot(rCrossT, I_inv_worldA * rCrossT);
                            float J_tangent = std::min(friction * J_normal, tangentSpeed / std::max(1e-4f, tangentMassInv));
                            glm::vec3 impulseTangent = J_tangent * tangentDir;

                            physA.velocity += impulseTangent / massA;
                            physA.angularVelocity += I_inv_worldA * glm::cross(r_body, impulseTangent);
                        }

                        if (objB.hasPhysics && objB.physics.enabled && !objB.physics.isKinematic) {
                            objB.physics.velocity -= impulseNormal / std::max(0.001f, objB.physics.mass);
                        }
                    }

                    objA.position += worldNormal * (pen * 0.7f);
                }
            }
        }
    }

    // 3. Grounded State & Resting Stability
    if (groundedContactCount > 0) {
        physA.isGrounded = true;
        // When low velocity and resting on multiple contact points, damp micro-jitter to settle flat
        if (glm::length(physA.velocity) < 0.35f && glm::length(physA.angularVelocity) < 0.6f) {
            physA.velocity.y *= 0.1f;
            physA.velocity.x *= std::clamp(1.0f - friction * 15.0f * dt, 0.0f, 1.0f);
            physA.velocity.z *= std::clamp(1.0f - friction * 15.0f * dt, 0.0f, 1.0f);
            physA.angularVelocity *= std::clamp(1.0f - 12.0f * dt, 0.0f, 1.0f);
            if (groundedContactCount >= 3 && glm::length(physA.velocity) < 0.08f && glm::length(physA.angularVelocity) < 0.15f) {
                physA.velocity = glm::vec3(0.0f);
                physA.angularVelocity = glm::vec3(0.0f);
            }
        }
    }
}

} // namespace Physics
} // namespace Eunoia
