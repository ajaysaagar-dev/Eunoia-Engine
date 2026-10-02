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

static glm::vec3 GetObjectHalfExtents(const GameObject& obj) {
    glm::vec3 h(0.5f);
    if (!obj.modifiers.empty()) {
        const auto& mod = obj.modifiers[0];
        if (mod.shapeType == CollisionShapeType::Box || mod.shapeType == CollisionShapeType::Mesh) {
            h = mod.boxExtents * glm::abs(obj.scale);
        } else if (mod.shapeType == CollisionShapeType::Sphere) {
            h = glm::vec3(mod.radius) * glm::abs(obj.scale);
        } else if (mod.shapeType == CollisionShapeType::Plane) {
            h = glm::vec3(mod.planeSize.x * 0.5f * std::abs(obj.scale.x), 0.05f, mod.planeSize.y * 0.5f * std::abs(obj.scale.z));
        } else {
            h = glm::vec3(mod.radius, mod.height * 0.5f, mod.radius) * glm::abs(obj.scale);
        }
    } else {
        if (obj.type == PrimitiveType::Sphere || obj.type == PrimitiveType::UVSphere || obj.type == PrimitiveType::Icosphere) {
            h = glm::vec3(obj.params.radius) * glm::abs(obj.scale);
        } else if (obj.type == PrimitiveType::Plane || obj.type == PrimitiveType::Quad) {
            h = glm::vec3(obj.params.width * 0.5f * std::abs(obj.scale.x), 0.05f, (obj.params.depth > 0.0f ? obj.params.depth : 2.5f) * 0.5f * std::abs(obj.scale.z));
        } else {
            h = glm::vec3(obj.params.width * 0.5f, obj.params.height * 0.5f, obj.params.depth * 0.5f) * glm::abs(obj.scale);
        }
        if (h.x < 0.05f) h.x = 0.5f * std::abs(obj.scale.x);
        if (h.y < 0.05f) h.y = 0.5f * std::abs(obj.scale.y);
        if (h.z < 0.05f) h.z = 0.5f * std::abs(obj.scale.z);
    }
    return h;
}

static glm::mat3 ComputeBodyInertiaInv(const GameObject& obj) {
    float m = std::max(0.001f, obj.physics.mass);
    glm::vec3 h = GetObjectHalfExtents(obj);
    glm::vec3 size = h * 2.0f;

    float ix = (1.0f / 12.0f) * m * (size.y * size.y + size.z * size.z);
    float iy = (1.0f / 12.0f) * m * (size.x * size.x + size.z * size.z);
    float iz = (1.0f / 12.0f) * m * (size.x * size.x + size.y * size.y);

    return glm::mat3(
        1.0f / std::max(1e-4f, ix), 0.0f, 0.0f,
        0.0f, 1.0f / std::max(1e-4f, iy), 0.0f,
        0.0f, 0.0f, 1.0f / std::max(1e-4f, iz)
    );
}

static std::vector<glm::vec3> GetBoxCornersWorld(const GameObject& obj) {
    std::vector<glm::vec3> pts;
    pts.reserve(8);
    glm::mat3 R = GetRotationMatrix(obj.rotation);
    glm::vec3 h = GetObjectHalfExtents(obj);

    pts.push_back(obj.position + R * glm::vec3(-h.x, -h.y, -h.z));
    pts.push_back(obj.position + R * glm::vec3( h.x, -h.y, -h.z));
    pts.push_back(obj.position + R * glm::vec3( h.x, -h.y,  h.z));
    pts.push_back(obj.position + R * glm::vec3(-h.x, -h.y,  h.z));
    pts.push_back(obj.position + R * glm::vec3(-h.x,  h.y, -h.z));
    pts.push_back(obj.position + R * glm::vec3( h.x,  h.y, -h.z));
    pts.push_back(obj.position + R * glm::vec3( h.x,  h.y,  h.z));
    pts.push_back(obj.position + R * glm::vec3(-h.x,  h.y,  h.z));
    return pts;
}

// SAT (Separating Axis Theorem) OBB vs OBB intersection
struct SATResult {
    bool intersecting = false;
    float penetration = 0.0f;
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; // Points from A to B
    glm::vec3 contactPoint{0.0f};
};

static bool TestAxis(const glm::vec3& axis, const glm::vec3& centerDelta,
                     const glm::mat3& RA, const glm::vec3& hA,
                     const glm::mat3& RB, const glm::vec3& hB,
                     float& outPen, glm::vec3& outNormal)
{
    float axisLen = glm::length(axis);
    if (axisLen < 1e-5f) return true; // Degenerate cross product, ignore

    glm::vec3 n = axis / axisLen;

    float rA = hA.x * std::abs(glm::dot(RA[0], n)) +
               hA.y * std::abs(glm::dot(RA[1], n)) +
               hA.z * std::abs(glm::dot(RA[2], n));

    float rB = hB.x * std::abs(glm::dot(RB[0], n)) +
               hB.y * std::abs(glm::dot(RB[1], n)) +
               hB.z * std::abs(glm::dot(RB[2], n));

    float dist = std::abs(glm::dot(centerDelta, n));
    float pen = (rA + rB) - dist;

    if (pen <= 0.0f) {
        return false; // Separating axis found, no intersection
    }

    if (pen < outPen) {
        outPen = pen;
        outNormal = (glm::dot(centerDelta, n) >= 0.0f) ? n : -n;
    }
    return true;
}

static SATResult TestOBBvsOBB(const GameObject& objA, const GameObject& objB) {
    SATResult res;
    glm::mat3 RA = GetRotationMatrix(objA.rotation);
    glm::mat3 RB = GetRotationMatrix(objB.rotation);
    glm::vec3 hA = GetObjectHalfExtents(objA);
    glm::vec3 hB = GetObjectHalfExtents(objB);

    glm::vec3 delta = objB.position - objA.position;
    float minPen = 1e9f;
    glm::vec3 bestNormal(0.0f);

    // 1. 3 Face axes of A
    for (int i = 0; i < 3; ++i) {
        if (!TestAxis(RA[i], delta, RA, hA, RB, hB, minPen, bestNormal)) return res;
    }

    // 2. 3 Face axes of B
    for (int i = 0; i < 3; ++i) {
        if (!TestAxis(RB[i], delta, RA, hA, RB, hB, minPen, bestNormal)) return res;
    }

    // 3. 9 Edge cross products
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            glm::vec3 edgeAxis = glm::cross(RA[i], RB[j]);
            if (!TestAxis(edgeAxis, delta, RA, hA, RB, hB, minPen, bestNormal)) return res;
        }
    }

    res.intersecting = true;
    res.penetration = minPen;
    res.normal = bestNormal; // Points from A to B

    // Find contact point on surface
    auto cornersA = GetBoxCornersWorld(objA);
    auto cornersB = GetBoxCornersWorld(objB);

    // Find deepest penetrating vertex
    float deepestDist = -1e9f;
    glm::vec3 contactPt = (objA.position + objB.position) * 0.5f;

    for (const auto& ptA : cornersA) {
        float d = glm::dot(ptA - objA.position, res.normal);
        if (d > deepestDist) {
            deepestDist = d;
            contactPt = ptA;
        }
    }

    for (const auto& ptB : cornersB) {
        float d = glm::dot(objB.position - ptB, res.normal);
        if (d > deepestDist) {
            deepestDist = d;
            contactPt = ptB;
        }
    }

    res.contactPoint = contactPt;
    return res;
}

void PhysicsSystem::StepSimulation(Scene& scene, float dt) {
    if (dt <= 0.0f) return;

    const int subSteps = 6;
    float subDt = dt / (float)subSteps;

    for (int step = 0; step < subSteps; ++step) {
        // 1. Integrate forces, gravity, and velocities
        for (auto& obj : scene.objects) {
            if (!obj.hasPhysics || !obj.physics.enabled || obj.physics.isKinematic) {
                continue;
            }
            IntegratePhysicsBody(obj, subDt);
        }

        // 2. Ground plane collision resolution & continuous toppling torque
        for (auto& obj : scene.objects) {
            if (!obj.hasPhysics || !obj.physics.enabled || obj.physics.isKinematic) {
                continue;
            }

            for (const auto& groundObj : scene.objects) {
                if (groundObj.id == obj.id || !groundObj.visible) continue;
                bool isGround = (groundObj.type == PrimitiveType::Plane || groundObj.type == PrimitiveType::Quad ||
                                 (!groundObj.modifiers.empty() && groundObj.modifiers[0].shapeType == CollisionShapeType::Plane));
                if (!isGround) continue;

                // Resolve box vs ground plane
                glm::mat3 RG = GetRotationMatrix(groundObj.rotation);
                glm::vec3 planeNormal = glm::normalize(RG * glm::vec3(0.0f, 1.0f, 0.0f));
                glm::vec3 planePoint = groundObj.position;
                glm::vec3 groundHalf = GetObjectHalfExtents(groundObj);

                auto corners = GetBoxCornersWorld(obj);
                auto& phys = obj.physics;
                float mass = std::max(0.001f, phys.mass);
                glm::mat3 R = GetRotationMatrix(obj.rotation);
                glm::mat3 I_inv_world = R * ComputeBodyInertiaInv(obj) * glm::transpose(R);

                int contactCount = 0;
                float maxPen = 0.0f;
                std::vector<glm::vec3> activeContactOffsets;

                for (const auto& cornerWorld : corners) {
                    glm::vec3 delta = cornerWorld - planePoint;
                    float projX = std::abs(glm::dot(delta, RG * glm::vec3(1.0f, 0.0f, 0.0f)));
                    float projZ = std::abs(glm::dot(delta, RG * glm::vec3(0.0f, 0.0f, 1.0f)));
                    if (projX > groundHalf.x + 2.0f || projZ > groundHalf.z + 2.0f) continue;

                    float distToPlane = glm::dot(delta, planeNormal);

                    if (distToPlane < 0.02f) { // Penetrating or resting on ground
                        float penetration = std::max(0.0f, -distToPlane);
                        maxPen = std::max(maxPen, penetration);
                        contactCount++;

                        glm::vec3 r_body = cornerWorld - obj.position;
                        activeContactOffsets.push_back(r_body);

                        glm::vec3 pointVel = phys.velocity + glm::cross(phys.angularVelocity, r_body);
                        float velAlongNormal = glm::dot(pointVel, planeNormal);

                        if (velAlongNormal < 0.0f) {
                            glm::vec3 rCrossN = glm::cross(r_body, planeNormal);
                            float effMassInv = (1.0f / mass) + glm::dot(rCrossN, I_inv_world * rCrossN);

                            float rest = (std::abs(velAlongNormal) < 0.25f) ? 0.0f : std::clamp(phys.bounciness, 0.0f, 0.9f);
                            float Jn = -(1.0f + rest) * velAlongNormal / std::max(1e-4f, effMassInv);
                            glm::vec3 impN = Jn * planeNormal;

                            phys.velocity += impN / mass;
                            phys.angularVelocity += I_inv_world * glm::cross(r_body, impN);

                            // Surface friction impulse
                            glm::vec3 tangentVel = pointVel - velAlongNormal * planeNormal;
                            float tSpeed = glm::length(tangentVel);
                            if (tSpeed > 1e-4f) {
                                glm::vec3 tDir = -tangentVel / tSpeed;
                                glm::vec3 rCrossT = glm::cross(r_body, tDir);
                                float tMassInv = (1.0f / mass) + glm::dot(rCrossT, I_inv_world * rCrossT);
                                float Jt = std::min(phys.friction * Jn, tSpeed / std::max(1e-4f, tMassInv));
                                glm::vec3 impT = Jt * tDir;
                                phys.velocity += impT / mass;
                                phys.angularVelocity += I_inv_world * glm::cross(r_body, impT);
                            }
                        }
                    }
                }

                if (contactCount > 0) {
                    obj.position += planeNormal * (maxPen * 0.85f);
                    phys.isGrounded = true;
                    phys.groundObjectId = groundObj.id;

                    // Continuous gravitational toppling torque:
                    // When resting on 1 corner or 1 edge (contactCount < 4), normal reaction force at contact point
                    // exerts torque that naturally topples the cube onto its full face!
                    if (contactCount < 4 && phys.useGravity) {
                        float supportWeight = mass * 9.81f * phys.gravityMultiplier / (float)contactCount;
                        for (const auto& r_c : activeContactOffsets) {
                            glm::vec3 normalForce = planeNormal * supportWeight;
                            glm::vec3 topplingTorque = glm::cross(r_c, normalForce);
                            phys.angularVelocity += (I_inv_world * topplingTorque) * subDt;
                        }
                    }

                    // Resting stabilization only when a full face (4 corners) is resting on ground
                    if (contactCount >= 4) {
                        if (glm::length(phys.velocity) < 0.5f && glm::length(phys.angularVelocity) < 0.8f) {
                            phys.velocity.y *= 0.05f;
                            phys.velocity.x *= std::clamp(1.0f - phys.friction * 25.0f * subDt, 0.0f, 1.0f);
                            phys.velocity.z *= std::clamp(1.0f - phys.friction * 25.0f * subDt, 0.0f, 1.0f);
                            phys.angularVelocity *= std::clamp(1.0f - 20.0f * subDt, 0.0f, 1.0f);

                            if (glm::length(phys.velocity) < 0.1f && glm::length(phys.angularVelocity) < 0.15f) {
                                phys.velocity = glm::vec3(0.0f);
                                phys.angularVelocity = glm::vec3(0.0f);
                            }
                        }
                    } else {
                        // Light damping while tumbling/toppling
                        phys.angularVelocity *= std::clamp(1.0f - phys.angularDamping * subDt, 0.0f, 1.0f);
                    }
                }
            }
        }

        // 3. Dynamic body vs body (SAT OBB vs OBB with unique unordered pairs)
        size_t numObjs = scene.objects.size();
        for (size_t i = 0; i < numObjs; ++i) {
            auto& objA = scene.objects[i];
            if (!objA.visible || objA.isLight || IsLightPrimitive(objA.type) || objA.isCamera || objA.type == PrimitiveType::Empty) continue;
            bool isGroundA = (objA.type == PrimitiveType::Plane || objA.type == PrimitiveType::Quad ||
                             (!objA.modifiers.empty() && objA.modifiers[0].shapeType == CollisionShapeType::Plane));
            if (isGroundA) continue;

            for (size_t j = i + 1; j < numObjs; ++j) {
                auto& objB = scene.objects[j];
                if (!objB.visible || objB.isLight || IsLightPrimitive(objB.type) || objB.isCamera || objB.type == PrimitiveType::Empty) continue;
                bool isGroundB = (objB.type == PrimitiveType::Plane || objB.type == PrimitiveType::Quad ||
                                 (!objB.modifiers.empty() && objB.modifiers[0].shapeType == CollisionShapeType::Plane));
                if (isGroundB) continue;

                // At least one object must have active dynamic physics
                bool dynA = (objA.hasPhysics && objA.physics.enabled && !objA.physics.isKinematic);
                bool dynB = (objB.hasPhysics && objB.physics.enabled && !objB.physics.isKinematic);
                if (!dynA && !dynB) continue;

                // Test SAT OBB intersection
                SATResult sat = TestOBBvsOBB(objA, objB);
                if (!sat.intersecting || sat.penetration <= 0.0f) continue;

                glm::vec3 normal = sat.normal; // Points from A to B
                glm::vec3 contactPt = sat.contactPoint;
                float pen = sat.penetration;

                glm::vec3 rA = contactPt - objA.position;
                glm::vec3 rB = contactPt - objB.position;

                float massA = dynA ? std::max(0.001f, objA.physics.mass) : 1e9f;
                float massB = dynB ? std::max(0.001f, objB.physics.mass) : 1e9f;

                glm::mat3 RA = GetRotationMatrix(objA.rotation);
                glm::mat3 RB = GetRotationMatrix(objB.rotation);
                glm::mat3 IA_inv = dynA ? (RA * ComputeBodyInertiaInv(objA) * glm::transpose(RA)) : glm::mat3(0.0f);
                glm::mat3 IB_inv = dynB ? (RB * ComputeBodyInertiaInv(objB) * glm::transpose(RB)) : glm::mat3(0.0f);

                glm::vec3 velPtA = dynA ? (objA.physics.velocity + glm::cross(objA.physics.angularVelocity, rA)) : glm::vec3(0.0f);
                glm::vec3 velPtB = dynB ? (objB.physics.velocity + glm::cross(objB.physics.angularVelocity, rB)) : glm::vec3(0.0f);
                glm::vec3 relVel = velPtA - velPtB;

                float velAlongNormal = glm::dot(relVel, normal);

                if (velAlongNormal > 0.0f) { // Moving towards each other (since normal points A -> B)
                    glm::vec3 rCrossNA = glm::cross(rA, normal);
                    glm::vec3 rCrossNB = glm::cross(rB, normal);

                    float effMassInv = (dynA ? (1.0f / massA + glm::dot(rCrossNA, IA_inv * rCrossNA)) : 0.0f) +
                                       (dynB ? (1.0f / massB + glm::dot(rCrossNB, IB_inv * rCrossNB)) : 0.0f);

                    float restA = dynA ? objA.physics.bounciness : 0.0f;
                    float restB = dynB ? objB.physics.bounciness : 0.0f;
                    float rest = std::min(restA, restB);
                    if (velAlongNormal < 0.25f) rest = 0.0f;

                    float Jn = (1.0f + rest) * velAlongNormal / std::max(1e-4f, effMassInv);
                    glm::vec3 impN = Jn * normal;

                    if (dynA) {
                        objA.physics.velocity -= impN / massA;
                        objA.physics.angularVelocity -= IA_inv * glm::cross(rA, impN);
                    }
                    if (dynB) {
                        objB.physics.velocity += impN / massB;
                        objB.physics.angularVelocity += IB_inv * glm::cross(rB, impN);
                    }

                    // Friction
                    glm::vec3 tangentVel = relVel - velAlongNormal * normal;
                    float tSpeed = glm::length(tangentVel);
                    if (tSpeed > 1e-4f) {
                        glm::vec3 tDir = tangentVel / tSpeed;
                        glm::vec3 rCrossTA = glm::cross(rA, tDir);
                        glm::vec3 rCrossTB = glm::cross(rB, tDir);

                        float tMassInv = (dynA ? (1.0f / massA + glm::dot(rCrossTA, IA_inv * rCrossTA)) : 0.0f) +
                                         (dynB ? (1.0f / massB + glm::dot(rCrossTB, IB_inv * rCrossTB)) : 0.0f);

                        float fricA = dynA ? objA.physics.friction : 0.5f;
                        float fricB = dynB ? objB.physics.friction : 0.5f;
                        float combFric = std::sqrt(fricA * fricB);

                        float Jt = std::min(combFric * Jn, tSpeed / std::max(1e-4f, tMassInv));
                        glm::vec3 impT = Jt * tDir;

                        if (dynA) {
                            objA.physics.velocity -= impT / massA;
                            objA.physics.angularVelocity -= IA_inv * glm::cross(rA, impT);
                        }
                        if (dynB) {
                            objB.physics.velocity += impT / massB;
                            objB.physics.angularVelocity += IB_inv * glm::cross(rB, impT);
                        }
                    }
                }

                // Continuous toppling torque for stacked/tilted dynamic bodies
                if (dynA && objA.physics.useGravity) {
                    glm::vec3 normalReaction = -normal * (massA * 9.81f * objA.physics.gravityMultiplier);
                    objA.physics.angularVelocity += (IA_inv * glm::cross(rA, normalReaction)) * (subDt * 0.5f);
                }
                if (dynB && objB.physics.useGravity) {
                    glm::vec3 normalReaction = normal * (massB * 9.81f * objB.physics.gravityMultiplier);
                    objB.physics.angularVelocity += (IB_inv * glm::cross(rB, normalReaction)) * (subDt * 0.5f);
                }

                // Positional separation (split proportionally by mass)
                float totalMass = (dynA ? massA : 0.0f) + (dynB ? massB : 0.0f);
                if (totalMass > 0.0f) {
                    if (dynA && dynB) {
                        float ratioA = massB / totalMass;
                        float ratioB = massA / totalMass;
                        objA.position -= normal * (pen * ratioA * 0.85f);
                        objB.position += normal * (pen * ratioB * 0.85f);
                    } else if (dynA) {
                        objA.position -= normal * (pen * 0.85f);
                    } else if (dynB) {
                        objB.position += normal * (pen * 0.85f);
                    }
                }
            }
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
    if (phys.lockPositionZ) phys.angularVelocity.z = 0.0f;

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

} // namespace Physics
} // namespace Eunoia
