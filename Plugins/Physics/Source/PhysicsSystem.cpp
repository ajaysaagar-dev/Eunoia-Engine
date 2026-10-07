#define GLM_ENABLE_EXPERIMENTAL
#include "Physics/PhysicsSystem.h"
#include "Collisions/CollisionSystem.h"
#include <EngineScene/Scene.h>
#include <EngineScene/GameObject.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
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

static bool ShouldLockCharacterUpright(const GameObject& obj) {
    if (obj.hasPhysics && obj.physics.isCharacterPhysics) {
        return true;
    }
    for (const auto& mod : obj.modifiers) {
        if (mod.enabled && mod.shapeType == CollisionShapeType::Character) {
            return true;
        }
    }
    for (const auto& b : obj.behaviours) {
        if (b && b->IsEnabled()) {
            return true;
        }
    }
    return false;
}

static glm::mat3 ComputeBodyInertiaInv(const GameObject& obj) {
    if (ShouldLockCharacterUpright(obj)) {
        return glm::mat3(0.0f);
    }

    float m = std::max(0.001f, obj.physics.mass);
    glm::vec3 h = GetObjectHalfExtents(obj);

    bool isSphere = (obj.type == PrimitiveType::Sphere || obj.type == PrimitiveType::UVSphere || obj.type == PrimitiveType::Icosphere ||
                    (!obj.modifiers.empty() && obj.modifiers[0].shapeType == CollisionShapeType::Sphere));
    if (isSphere) {
        float r = h.x;
        float i = 0.4f * m * r * r;
        float invI = 1.0f / std::max(1e-4f, i);
        return glm::mat3(
            invI, 0.0f, 0.0f,
            0.0f, invI, 0.0f,
            0.0f, 0.0f, invI
        );
    }

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

static glm::vec3 GetObjectCenterOffset(const GameObject& obj) {
    if (!obj.modifiers.empty()) {
        return obj.modifiers[0].centerOffset * obj.scale;
    }
    return glm::vec3(0.0f);
}

static std::vector<glm::vec3> GetBoxCornersWorld(const GameObject& obj) {
    std::vector<glm::vec3> pts;
    pts.reserve(8);
    glm::mat3 R = GetRotationMatrix(obj.rotation);
    glm::vec3 h = GetObjectHalfExtents(obj);
    glm::vec3 c = GetObjectCenterOffset(obj);
    glm::vec3 centerWorld = obj.position + R * c;

    pts.push_back(centerWorld + R * glm::vec3(-h.x, -h.y, -h.z));
    pts.push_back(centerWorld + R * glm::vec3( h.x, -h.y, -h.z));
    pts.push_back(centerWorld + R * glm::vec3( h.x, -h.y,  h.z));
    pts.push_back(centerWorld + R * glm::vec3(-h.x, -h.y,  h.z));
    pts.push_back(centerWorld + R * glm::vec3(-h.x,  h.y, -h.z));
    pts.push_back(centerWorld + R * glm::vec3( h.x,  h.y, -h.z));
    pts.push_back(centerWorld + R * glm::vec3( h.x,  h.y,  h.z));
    pts.push_back(centerWorld + R * glm::vec3(-h.x,  h.y,  h.z));
    return pts;
}

// Contact Constraint structure for Sequential Impulse Solver
struct ContactConstraint {
    int objAIndex = -1;
    int objBIndex = -1; // -1 for ground plane
    glm::vec3 contactPoint{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; // Points from B into A (lifting A)
    glm::vec3 tangent1{1.0f, 0.0f, 0.0f};
    glm::vec3 tangent2{0.0f, 0.0f, 1.0f};
    glm::vec3 rA{0.0f};
    glm::vec3 rB{0.0f};
    float penetration = 0.0f;
    float restitution = 0.0f;
    float friction = 0.5f;

    // Effective masses
    float effNormalMass = 0.0f;
    float effTangent1Mass = 0.0f;
    float effTangent2Mass = 0.0f;

    // Accumulated clamped impulses
    float accNormalImpulse = 0.0f;
    float accTangent1Impulse = 0.0f;
    float accTangent2Impulse = 0.0f;

    // Baumgarte velocity bias
    float velocityBias = 0.0f;
};

// SAT test helper
static bool TestAxisSAT(const glm::vec3& axis, const glm::vec3& delta,
                        const glm::mat3& RA, const glm::vec3& hA,
                        const glm::mat3& RB, const glm::vec3& hB,
                        float& outPen, glm::vec3& outNormal)
{
    float len = glm::length(axis);
    if (len < 1e-5f) return true;

    glm::vec3 n = axis / len;

    float rA = hA.x * std::abs(glm::dot(RA[0], n)) +
               hA.y * std::abs(glm::dot(RA[1], n)) +
               hA.z * std::abs(glm::dot(RA[2], n));

    float rB = hB.x * std::abs(glm::dot(RB[0], n)) +
               hB.y * std::abs(glm::dot(RB[1], n)) +
               hB.z * std::abs(glm::dot(RB[2], n));

    float dist = std::abs(glm::dot(delta, n));
    float pen = (rA + rB) - dist;

    if (pen <= 0.0f) return false;

    if (pen < outPen) {
        outPen = pen;
        outNormal = (glm::dot(delta, n) >= 0.0f) ? n : -n;
    }
    return true;
}

void PhysicsSystem::StepSimulation(Scene& scene, float dt) {
    if (dt <= 0.0f) return;

    // Clamp dt to avoid physics spiral of death
    dt = std::min(dt, 0.0333f);

    const int subSteps = 8;
    float subDt = dt / (float)subSteps;

    for (int step = 0; step < subSteps; ++step) {
        // 1. Numerical Integration: Gravity, External Forces, Velocity Prediction
        for (auto& obj : scene.objects) {
            if (!obj.hasPhysics || !obj.physics.enabled || obj.physics.isKinematic) {
                continue;
            }
            auto& phys = obj.physics;
            float mass = std::max(0.001f, phys.mass);

            // Gravity
            glm::vec3 grav(0.0f);
            if (phys.useGravity) {
                grav = m_globalGravity * phys.gravityMultiplier;
            }

            glm::vec3 linearAcc = grav + (phys.accumulatedForce / mass);
            phys.velocity += linearAcc * subDt;

            // Unconditionally stable damping (matches D3D12Renderer)
            phys.velocity *= 1.0f / (1.0f + subDt * std::max(0.0f, phys.linearDamping));

            // Constraints
            if (phys.lockPositionX) phys.velocity.x = 0.0f;
            if (phys.lockPositionY) phys.velocity.y = 0.0f;
            if (phys.lockPositionZ) phys.velocity.z = 0.0f;

            // Angular damping
            phys.angularVelocity *= 1.0f / (1.0f + subDt * std::max(0.0f, phys.angularDamping));

            bool isCharUpright = ShouldLockCharacterUpright(obj);
            if (phys.lockRotationX || isCharUpright) phys.angularVelocity.x = 0.0f;
            if (phys.lockRotationY || isCharUpright) phys.angularVelocity.y = 0.0f;
            if (phys.lockRotationZ || isCharUpright) phys.angularVelocity.z = 0.0f;
            if (isCharUpright) {
                phys.accumulatedTorque = glm::vec3(0.0f);
                obj.rotation.x = 0.0f;
                obj.rotation.z = 0.0f;
            }

            phys.accumulatedForce = glm::vec3(0.0f);
            phys.accumulatedTorque = glm::vec3(0.0f);
            phys.isGrounded = false;
        }

        // 2. Contact Generation
        std::vector<ContactConstraint> constraints;
        size_t numObjs = scene.objects.size();

        // 2a. Ground Plane Contacts
        for (size_t i = 0; i < numObjs; ++i) {
            auto& obj = scene.objects[i];
            if (!obj.hasPhysics || !obj.physics.enabled || obj.physics.isKinematic) continue;
            if (!obj.visible && obj.modifiers.empty() && !obj.hasPhysics) continue;

            for (size_t g = 0; g < numObjs; ++g) {
                if (g == i) continue;
                auto& groundObj = scene.objects[g];
                if (groundObj.isUserInterface || groundObj.type == PrimitiveType::UserInterface) continue;
                if (!groundObj.visible && groundObj.modifiers.empty()) continue;
                bool isGround = (groundObj.type == PrimitiveType::Plane || groundObj.type == PrimitiveType::Quad ||
                                 (!groundObj.modifiers.empty() && groundObj.modifiers[0].shapeType == CollisionShapeType::Plane));
                if (!isGround) continue;

                glm::mat3 RG = GetRotationMatrix(groundObj.rotation);
                glm::vec3 planeNormal = glm::normalize(RG * glm::vec3(0.0f, 1.0f, 0.0f));
                glm::vec3 planePoint = groundObj.position;
                glm::vec3 groundHalf = GetObjectHalfExtents(groundObj);

                auto corners = GetBoxCornersWorld(obj);
                float massA = std::max(0.001f, obj.physics.mass);
                glm::mat3 RA = GetRotationMatrix(obj.rotation);
                glm::mat3 IA_inv = RA * ComputeBodyInertiaInv(obj) * glm::transpose(RA);

                for (const auto& cornerWorld : corners) {
                    glm::vec3 delta = cornerWorld - planePoint;
                    float projX = std::abs(glm::dot(delta, RG * glm::vec3(1.0f, 0.0f, 0.0f)));
                    float projZ = std::abs(glm::dot(delta, RG * glm::vec3(0.0f, 0.0f, 1.0f)));
                    if (projX > groundHalf.x + 2.0f || projZ > groundHalf.z + 2.0f) continue;

                    float distToPlane = glm::dot(delta, planeNormal);

                    if (distToPlane < 0.015f) { // Contacting or penetrating
                        float penetration = std::max(0.0f, -distToPlane);
                        obj.physics.isGrounded = true;
                        obj.physics.groundObjectId = groundObj.id;

                        ContactConstraint cc;
                        cc.objAIndex = (int)i;
                        cc.objBIndex = -1;
                        cc.contactPoint = cornerWorld;
                        cc.normal = planeNormal; // Upward normal
                        cc.rA = cornerWorld - obj.position;
                        cc.rB = glm::vec3(0.0f);
                        cc.penetration = penetration;
                        bool isCharA = obj.physics.isCharacterPhysics || (!obj.modifiers.empty() && obj.modifiers[0].shapeType == CollisionShapeType::Character);
                        cc.restitution = isCharA ? 0.0f : std::clamp(obj.physics.bounciness, 0.0f, 0.9f);
                        cc.friction = std::clamp(obj.physics.friction, 0.0f, 1.0f);

                        // Tangent vectors
                        glm::vec3 tangent1 = glm::cross(cc.normal, glm::vec3(0.0f, 0.0f, 1.0f));
                        if (glm::length(tangent1) < 0.1f) tangent1 = glm::cross(cc.normal, glm::vec3(1.0f, 0.0f, 0.0f));
                        cc.tangent1 = glm::normalize(tangent1);
                        cc.tangent2 = glm::normalize(glm::cross(cc.normal, cc.tangent1));

                        // Effective normal mass
                        glm::vec3 rCrossN = glm::cross(cc.rA, cc.normal);
                        float Kn = (1.0f / massA) + glm::dot(rCrossN, IA_inv * rCrossN);
                        cc.effNormalMass = 1.0f / std::max(1e-4f, Kn);

                        // Effective tangent masses
                        glm::vec3 rCrossT1 = glm::cross(cc.rA, cc.tangent1);
                        float Kt1 = (1.0f / massA) + glm::dot(rCrossT1, IA_inv * rCrossT1);
                        cc.effTangent1Mass = 1.0f / std::max(1e-4f, Kt1);

                        glm::vec3 rCrossT2 = glm::cross(cc.rA, cc.tangent2);
                        float Kt2 = (1.0f / massA) + glm::dot(rCrossT2, IA_inv * rCrossT2);
                        cc.effTangent2Mass = 1.0f / std::max(1e-4f, Kt2);

                        // Baumgarte stabilization velocity bias & restitution
                        const float baumgarte = 0.15f;
                        const float slop = 0.005f;
                        float penBias = (baumgarte / subDt) * std::max(0.0f, penetration - slop);
                        penBias = std::min(penBias, 2.0f);

                        // Relative velocity along normal before impulse
                        glm::vec3 vPoint = obj.physics.velocity + glm::cross(obj.physics.angularVelocity, cc.rA);
                        float vRelN = glm::dot(vPoint, cc.normal);
                        float bounceBias = 0.0f;
                        if (!isCharA && vRelN < -0.3f && cc.restitution > 0.01f) {
                            bounceBias = -cc.restitution * vRelN;
                        }
                        cc.velocityBias = penBias + bounceBias;

                        constraints.push_back(cc);
                    }
                }
            }
        }

        // 2b. Dynamic Body vs Body SAT Contacts
        for (size_t i = 0; i < numObjs; ++i) {
            auto& objA = scene.objects[i];
            if (objA.isLight || IsLightPrimitive(objA.type) || objA.isCamera || objA.isUserInterface || objA.type == PrimitiveType::UserInterface) continue;
            if (!objA.visible && objA.modifiers.empty() && !objA.hasPhysics) continue;
            if (objA.type == PrimitiveType::Empty && objA.modifiers.empty() && !objA.hasPhysics) continue;
            bool isGroundA = (objA.type == PrimitiveType::Plane || objA.type == PrimitiveType::Quad ||
                             (!objA.modifiers.empty() && objA.modifiers[0].shapeType == CollisionShapeType::Plane));
            if (isGroundA) continue;

            for (size_t j = i + 1; j < numObjs; ++j) {
                auto& objB = scene.objects[j];
                if (objB.isLight || IsLightPrimitive(objB.type) || objB.isCamera || objB.isUserInterface || objB.type == PrimitiveType::UserInterface) continue;
                if (!objB.visible && objB.modifiers.empty() && !objB.hasPhysics) continue;
                if (objB.type == PrimitiveType::Empty && objB.modifiers.empty() && !objB.hasPhysics) continue;
                bool isGroundB = (objB.type == PrimitiveType::Plane || objB.type == PrimitiveType::Quad ||
                                 (!objB.modifiers.empty() && objB.modifiers[0].shapeType == CollisionShapeType::Plane));
                if (isGroundB) continue;

                bool dynA = (objA.hasPhysics && objA.physics.enabled && !objA.physics.isKinematic);
                bool dynB = (objB.hasPhysics && objB.physics.enabled && !objB.physics.isKinematic);
                if (!dynA && !dynB) continue;

                glm::mat3 RA = GetRotationMatrix(objA.rotation);
                glm::mat3 RB = GetRotationMatrix(objB.rotation);
                glm::vec3 hA = GetObjectHalfExtents(objA);
                glm::vec3 hB = GetObjectHalfExtents(objB);
                glm::vec3 delta = objB.position - objA.position;
                float minPen = 1e9f;
                glm::vec3 bestNormal(0.0f);

                bool intersect = true;
                for (int a = 0; a < 3 && intersect; ++a) {
                    if (!TestAxisSAT(RA[a], delta, RA, hA, RB, hB, minPen, bestNormal)) intersect = false;
                }
                for (int b = 0; b < 3 && intersect; ++b) {
                    if (!TestAxisSAT(RB[b], delta, RA, hA, RB, hB, minPen, bestNormal)) intersect = false;
                }
                for (int a = 0; a < 3 && intersect; ++a) {
                    for (int b = 0; b < 3 && intersect; ++b) {
                        glm::vec3 edgeAxis = glm::cross(RA[a], RB[b]);
                        if (!TestAxisSAT(edgeAxis, delta, RA, hA, RB, hB, minPen, bestNormal)) intersect = false;
                    }
                }

                if (!intersect || minPen <= 0.0f) continue;

                // Find contact point
                auto cornersA = GetBoxCornersWorld(objA);
                auto cornersB = GetBoxCornersWorld(objB);
                float deepestDist = -1e9f;
                glm::vec3 contactPt = (objA.position + objB.position) * 0.5f;

                for (const auto& ptA : cornersA) {
                    float d = glm::dot(ptA - objA.position, bestNormal);
                    if (d > deepestDist) { deepestDist = d; contactPt = ptA; }
                }
                for (const auto& ptB : cornersB) {
                    float d = glm::dot(objB.position - ptB, bestNormal);
                    if (d > deepestDist) { deepestDist = d; contactPt = ptB; }
                }

                ContactConstraint cc;
                cc.objAIndex = (int)i;
                cc.objBIndex = (int)j;
                cc.contactPoint = contactPt;
                cc.normal = -bestNormal; // Points from B into A
                cc.rA = contactPt - objA.position;
                cc.rB = contactPt - objB.position;
                cc.penetration = minPen;
                bool isCharA = dynA && (objA.physics.isCharacterPhysics || (!objA.modifiers.empty() && objA.modifiers[0].shapeType == CollisionShapeType::Character));
                bool isCharB = dynB && (objB.physics.isCharacterPhysics || (!objB.modifiers.empty() && objB.modifiers[0].shapeType == CollisionShapeType::Character));
                cc.restitution = (isCharA || isCharB) ? 0.0f : std::min(dynA ? objA.physics.bounciness : 0.0f, dynB ? objB.physics.bounciness : 0.0f);
                cc.friction = std::sqrt((dynA ? objA.physics.friction : 0.5f) * (dynB ? objB.physics.friction : 0.5f));

                glm::vec3 tangent1 = glm::cross(cc.normal, glm::vec3(0.0f, 0.0f, 1.0f));
                if (glm::length(tangent1) < 0.1f) tangent1 = glm::cross(cc.normal, glm::vec3(1.0f, 0.0f, 0.0f));
                cc.tangent1 = glm::normalize(tangent1);
                cc.tangent2 = glm::normalize(glm::cross(cc.normal, cc.tangent1));

                float massA = dynA ? std::max(0.001f, objA.physics.mass) : 1e9f;
                float massB = dynB ? std::max(0.001f, objB.physics.mass) : 1e9f;
                glm::mat3 IA_inv = dynA ? (RA * ComputeBodyInertiaInv(objA) * glm::transpose(RA)) : glm::mat3(0.0f);
                glm::mat3 IB_inv = dynB ? (RB * ComputeBodyInertiaInv(objB) * glm::transpose(RB)) : glm::mat3(0.0f);

                glm::vec3 rCrossNA = glm::cross(cc.rA, cc.normal);
                glm::vec3 rCrossNB = glm::cross(cc.rB, cc.normal);
                float Kn = (dynA ? (1.0f / massA + glm::dot(rCrossNA, IA_inv * rCrossNA)) : 0.0f) +
                           (dynB ? (1.0f / massB + glm::dot(rCrossNB, IB_inv * rCrossNB)) : 0.0f);
                cc.effNormalMass = 1.0f / std::max(1e-4f, Kn);

                glm::vec3 rCrossT1A = glm::cross(cc.rA, cc.tangent1);
                glm::vec3 rCrossT1B = glm::cross(cc.rB, cc.tangent1);
                float Kt1 = (dynA ? (1.0f / massA + glm::dot(rCrossT1A, IA_inv * rCrossT1A)) : 0.0f) +
                            (dynB ? (1.0f / massB + glm::dot(rCrossT1B, IB_inv * rCrossT1B)) : 0.0f);
                cc.effTangent1Mass = 1.0f / std::max(1e-4f, Kt1);

                glm::vec3 rCrossT2A = glm::cross(cc.rA, cc.tangent2);
                glm::vec3 rCrossT2B = glm::cross(cc.rB, cc.tangent2);
                float Kt2 = (dynA ? (1.0f / massA + glm::dot(rCrossT2A, IA_inv * rCrossT2A)) : 0.0f) +
                            (dynB ? (1.0f / massB + glm::dot(rCrossT2B, IB_inv * rCrossT2B)) : 0.0f);
                cc.effTangent2Mass = 1.0f / std::max(1e-4f, Kt2);

                const float baumgarte = 0.15f;
                const float slop = 0.005f;
                float penBias = (baumgarte / subDt) * std::max(0.0f, minPen - slop);
                penBias = std::min(penBias, 2.0f);

                // Relative velocity along normal before impulse
                glm::vec3 vPtA = dynA ? (objA.physics.velocity + glm::cross(objA.physics.angularVelocity, cc.rA)) : glm::vec3(0.0f);
                glm::vec3 vPtB = dynB ? (objB.physics.velocity + glm::cross(objB.physics.angularVelocity, cc.rB)) : glm::vec3(0.0f);
                float vRelN = glm::dot(vPtA - vPtB, cc.normal);
                float bounceBias = 0.0f;
                if (!isCharA && !isCharB && vRelN < -0.3f && cc.restitution > 0.01f) {
                    bounceBias = -cc.restitution * vRelN;
                }
                cc.velocityBias = penBias + bounceBias;

                constraints.push_back(cc);
            }
        }

        // 3. Sequential Impulse Solver (Projected Gauss-Seidel Iterations)
        const int solverIterations = 10;
        for (int iter = 0; iter < solverIterations; ++iter) {
            for (auto& cc : constraints) {
                auto& objA = scene.objects[cc.objAIndex];
                GameObject* objBPtr = (cc.objBIndex >= 0) ? &scene.objects[cc.objBIndex] : nullptr;

                bool dynA = (objA.hasPhysics && objA.physics.enabled && !objA.physics.isKinematic);
                bool dynB = (objBPtr && objBPtr->hasPhysics && objBPtr->physics.enabled && !objBPtr->physics.isKinematic);

                float massA = dynA ? std::max(0.001f, objA.physics.mass) : 1e9f;
                float massB = dynB ? std::max(0.001f, objBPtr->physics.mass) : 1e9f;

                glm::mat3 RA = GetRotationMatrix(objA.rotation);
                glm::mat3 IA_inv = dynA ? (RA * ComputeBodyInertiaInv(objA) * glm::transpose(RA)) : glm::mat3(0.0f);
                glm::mat3 RB = objBPtr ? GetRotationMatrix(objBPtr->rotation) : glm::mat3(1.0f);
                glm::mat3 IB_inv = dynB ? (RB * ComputeBodyInertiaInv(*objBPtr) * glm::transpose(RB)) : glm::mat3(0.0f);

                // Relative contact velocity
                glm::vec3 vA = dynA ? (objA.physics.velocity + glm::cross(objA.physics.angularVelocity, cc.rA)) : glm::vec3(0.0f);
                glm::vec3 vB = dynB ? (objBPtr->physics.velocity + glm::cross(objBPtr->physics.angularVelocity, cc.rB)) : glm::vec3(0.0f);
                glm::vec3 vRel = vA - vB;

                // --- 3a. Friction Constraint Resolution ---
                // Tangent 1
                float vT1 = glm::dot(vRel, cc.tangent1);
                float deltaLambdaT1 = -vT1 * cc.effTangent1Mass;
                float maxFriction = cc.friction * cc.accNormalImpulse;
                float oldAccT1 = cc.accTangent1Impulse;
                cc.accTangent1Impulse = std::clamp(oldAccT1 + deltaLambdaT1, -maxFriction, maxFriction);
                deltaLambdaT1 = cc.accTangent1Impulse - oldAccT1;
                glm::vec3 impT1 = deltaLambdaT1 * cc.tangent1;

                if (dynA) {
                    objA.physics.velocity += impT1 / massA;
                    objA.physics.angularVelocity += IA_inv * glm::cross(cc.rA, impT1);
                }
                if (dynB) {
                    objBPtr->physics.velocity -= impT1 / massB;
                    objBPtr->physics.angularVelocity -= IB_inv * glm::cross(cc.rB, impT1);
                }

                // Tangent 2
                vA = dynA ? (objA.physics.velocity + glm::cross(objA.physics.angularVelocity, cc.rA)) : glm::vec3(0.0f);
                vB = dynB ? (objBPtr->physics.velocity + glm::cross(objBPtr->physics.angularVelocity, cc.rB)) : glm::vec3(0.0f);
                vRel = vA - vB;
                float vT2 = glm::dot(vRel, cc.tangent2);
                float deltaLambdaT2 = -vT2 * cc.effTangent2Mass;
                float oldAccT2 = cc.accTangent2Impulse;
                cc.accTangent2Impulse = std::clamp(oldAccT2 + deltaLambdaT2, -maxFriction, maxFriction);
                deltaLambdaT2 = cc.accTangent2Impulse - oldAccT2;
                glm::vec3 impT2 = deltaLambdaT2 * cc.tangent2;

                if (dynA) {
                    objA.physics.velocity += impT2 / massA;
                    objA.physics.angularVelocity += IA_inv * glm::cross(cc.rA, impT2);
                }
                if (dynB) {
                    objBPtr->physics.velocity -= impT2 / massB;
                    objBPtr->physics.angularVelocity -= IB_inv * glm::cross(cc.rB, impT2);
                }

                // --- 3b. Normal Non-Penetration Constraint Resolution ---
                vA = dynA ? (objA.physics.velocity + glm::cross(objA.physics.angularVelocity, cc.rA)) : glm::vec3(0.0f);
                vB = dynB ? (objBPtr->physics.velocity + glm::cross(objBPtr->physics.angularVelocity, cc.rB)) : glm::vec3(0.0f);
                vRel = vA - vB;

                float vN = glm::dot(vRel, cc.normal);
                float deltaLambdaN = -(vN - cc.velocityBias) * cc.effNormalMass;
                float oldAccN = cc.accNormalImpulse;
                cc.accNormalImpulse = std::max(0.0f, oldAccN + deltaLambdaN);
                deltaLambdaN = cc.accNormalImpulse - oldAccN;
                glm::vec3 impN = deltaLambdaN * cc.normal;

                if (dynA) {
                    objA.physics.velocity += impN / massA;
                    objA.physics.angularVelocity += IA_inv * glm::cross(cc.rA, impN);
                }
                if (dynB) {
                    objBPtr->physics.velocity -= impN / massB;
                    objBPtr->physics.angularVelocity -= IB_inv * glm::cross(cc.rB, impN);
                }
            }
        }

        // 4. Position & 3D Quaternion Orientation Integration
        for (auto& obj : scene.objects) {
            if (!obj.hasPhysics || !obj.physics.enabled || obj.physics.isKinematic) {
                continue;
            }

            auto& phys = obj.physics;

            // Void limit check to prevent out-of-bounds objects from accelerating to negative infinity
            if (obj.position.y < -300.0f) {
                phys.velocity = glm::vec3(0.0f);
                phys.angularVelocity = glm::vec3(0.0f);
                continue;
            }

            // Rest thresholding for stable resting state (zero jitter)
            if (phys.isGrounded && glm::length(phys.velocity) < 0.04f && glm::length(phys.angularVelocity) < 0.05f) {
                phys.velocity = glm::vec3(0.0f);
                phys.angularVelocity = glm::vec3(0.0f);
                continue;
            }

            // Integrate position
            obj.position += phys.velocity * subDt;

            // Integrate 3D orientation smoothly with quaternion
            bool isCharUpright = ShouldLockCharacterUpright(obj);
            if (isCharUpright) {
                obj.rotation.x = 0.0f;
                obj.rotation.z = 0.0f;
                phys.angularVelocity = glm::vec3(0.0f);
            } else if (glm::length(phys.angularVelocity) > 1e-6f) {
                glm::quat currentQuat = glm::quat(glm::radians(obj.rotation));
                glm::quat deltaQuat(0.0f, phys.angularVelocity.x * subDt * 0.5f,
                                          phys.angularVelocity.y * subDt * 0.5f,
                                          phys.angularVelocity.z * subDt * 0.5f);
                glm::quat nextQuat = glm::normalize(currentQuat + deltaQuat * currentQuat);
                glm::vec3 nextEulerDeg = glm::degrees(glm::eulerAngles(nextQuat));

                if (!phys.lockRotationX) obj.rotation.x = nextEulerDeg.x;
                if (!phys.lockRotationY) obj.rotation.y = nextEulerDeg.y;
                if (!phys.lockRotationZ) obj.rotation.z = nextEulerDeg.z;

                // Keep angles normalized between -180 and 180
                for (int a = 0; a < 3; ++a) {
                    float& ang = (a == 0 ? obj.rotation.x : (a == 1 ? obj.rotation.y : obj.rotation.z));
                    if (!std::isnan(ang) && !std::isinf(ang)) {
                        ang = std::fmod(ang + 180.0f, 360.0f);
                        if (ang < 0.0f) ang += 360.0f;
                        ang -= 180.0f;
                    }
                }
            }
        }
    }
}

void PhysicsSystem::IntegratePhysicsBody(GameObject& obj, float dt) {
    // Handled in StepSimulation sub-step solver
}

} // namespace Physics
} // namespace Eunoia
