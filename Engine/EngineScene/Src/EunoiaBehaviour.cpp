#include <EngineScene/EunoiaBehaviour.h>
#include <EngineScene/BehaviourRegistry.h>
#include <EngineScene/GameObject.h>
#include <EngineScene/Scene.h>
#include <EnginePlatform/InputSystem.h>
#include <EngineRenderer/Camera.h>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>

// ============================================================================
// RespectiveObjectAccessor Implementations
// ============================================================================

LightComponent* RespectiveObjectAccessor::Light(void* objRef) const {
    if (!objRef) return nullptr;
    return reinterpret_cast<LightComponent*>(objRef);
}

PrimitiveMesh* RespectiveObjectAccessor::Mesh(void* objRef) const {
    if (!objRef) return nullptr;
    return reinterpret_cast<PrimitiveMesh*>(objRef);
}

GameObject* RespectiveObjectAccessor::Entity(void* objRef) const {
    if (!objRef) return nullptr;
    return reinterpret_cast<GameObject*>(objRef);
}

GameObject* RespectiveObjectAccessor::Actor(void* objRef) const {
    return Entity(objRef);
}

GameObject* RespectiveObjectAccessor::Shape(void* objRef) const {
    return Entity(objRef);
}

GameObject* RespectiveObjectAccessor::Transform(void* objRef) const {
    return Entity(objRef);
}

GameObject* RespectiveObjectAccessor::Camera(void* objRef) const {
    if (!objRef) return nullptr;
    return reinterpret_cast<GameObject*>(objRef);
}

GameObject* RespectiveObjectAccessor::CameraActor(void* objRef) const {
    if (!objRef) return nullptr;
    return reinterpret_cast<GameObject*>(objRef);
}

// ============================================================================
// CursorStateController (dev.md: CursorState.Locked(); CursorState.UnLocked();)
// ============================================================================

static struct CursorSyncInitializer {
    CursorSyncInitializer() {
        CursorStateController::s_cursorSyncCallback = []() {
            if (CursorStateController::s_activeBehaviour) {
                auto* beh = reinterpret_cast<EunoiaBehaviour*>(CursorStateController::s_activeBehaviour);
                if (beh && beh->GetOwner()) {
                    glm::vec3 rot = beh->GetOwner()->rotation;
                    InputSystem::Get().SetCursorPos(-(double)rot.y, (double)rot.x);
                }
            }
        };
    }
} s_cursorSyncInitializer;

// ============================================================================
// BehaviourTransformProperty Implementations
// ============================================================================

glm::vec3 BehaviourTransformProperty::LocalSpace() const {
    if (!behaviour) return glm::vec3(0.0f);
    GameObject* owner = behaviour->GetOwner();
    if (!owner) return glm::vec3(0.0f);
    switch (type) {
        case Type::Location: return owner->position;
        case Type::Rotation: return owner->rotation;
        case Type::Scale:    return owner->scale;
    }
    return glm::vec3(0.0f);
}

void BehaviourTransformProperty::LocalSpace(const glm::vec3& v) {
    if (!behaviour) return;
    GameObject* owner = behaviour->GetOwner();
    if (!owner) return;
    switch (type) {
        case Type::Location: owner->position = v; break;
        case Type::Rotation: owner->rotation = v; break;
        case Type::Scale:    owner->scale = v; break;
    }
}

glm::vec3 BehaviourTransformProperty::WorldSpace() const {
    if (!behaviour) return glm::vec3(0.0f);
    GameObject* owner = behaviour->GetOwner();
    if (!owner) return glm::vec3(0.0f);
    Scene* scene = behaviour->GetScene();
    if (!scene || owner->parentId == -1) {
        return LocalSpace();
    }
    glm::mat4 worldMat = scene->GetWorldMatrix(*owner);
    switch (type) {
        case Type::Location:
            return glm::vec3(worldMat[3]);
        case Type::Rotation: {
            glm::vec3 worldRot = owner->rotation;
            const GameObject* p = scene->FindObjectConst(owner->parentId);
            while (p) {
                worldRot += p->rotation;
                p = (p->parentId != -1) ? scene->FindObjectConst(p->parentId) : nullptr;
            }
            return worldRot;
        }
        case Type::Scale: {
            return glm::vec3(glm::length(glm::vec3(worldMat[0])),
                             glm::length(glm::vec3(worldMat[1])),
                             glm::length(glm::vec3(worldMat[2])));
        }
    }
    return LocalSpace();
}

void BehaviourTransformProperty::WorldSpace(const glm::vec3& v) {
    if (!behaviour) return;
    GameObject* owner = behaviour->GetOwner();
    if (!owner) return;
    Scene* scene = behaviour->GetScene();
    if (!scene || owner->parentId == -1) {
        LocalSpace(v);
        return;
    }
    const GameObject* parent = scene->FindObjectConst(owner->parentId);
    if (!parent) {
        LocalSpace(v);
        return;
    }
    glm::mat4 pMat = scene->GetWorldMatrix(*parent);
    glm::mat4 invP = glm::inverse(pMat);
    switch (type) {
        case Type::Location: {
            owner->position = glm::vec3(invP * glm::vec4(v, 1.0f));
            break;
        }
        case Type::Rotation: {
            glm::vec3 parentRot(0.0f);
            const GameObject* p = scene->FindObjectConst(owner->parentId);
            while (p) {
                parentRot += p->rotation;
                p = (p->parentId != -1) ? scene->FindObjectConst(p->parentId) : nullptr;
            }
            owner->rotation = v - parentRot;
            break;
        }
        case Type::Scale: {
            glm::vec3 pScale(glm::length(glm::vec3(pMat[0])),
                             glm::length(glm::vec3(pMat[1])),
                             glm::length(glm::vec3(pMat[2])));
            if (pScale.x > 1e-6f && pScale.y > 1e-6f && pScale.z > 1e-6f) {
                owner->scale = v / pScale;
            } else {
                owner->scale = v;
            }
            break;
        }
    }
}

// ============================================================================
// GameObjectTransform Implementations (dev.md)
// ============================================================================

static Scene* GetSceneForGameObject(const GameObject* go) {
    if (!go) return nullptr;
    if (go->scene) return go->scene;
    for (const auto& b : go->behaviours) {
        if (b && b->GetScene()) return b->GetScene();
    }
    return nullptr;
}

// ----------------------------------------------------------------------------
// High-Precision 3D Capsule-to-Obstacle Geometric Collision Routines
// ----------------------------------------------------------------------------

static float ClosestPtSegmentSegment(
    const glm::vec3& p1, const glm::vec3& q1,
    const glm::vec3& p2, const glm::vec3& q2,
    glm::vec3& c1, glm::vec3& c2)
{
    glm::vec3 d1 = q1 - p1;
    glm::vec3 d2 = q2 - p2;
    glm::vec3 r  = p1 - p2;
    float a = glm::dot(d1, d1);
    float e = glm::dot(d2, d2);
    float f = glm::dot(d2, r);

    float s = 0.0f, t = 0.0f;

    if (a <= 1e-6f && e <= 1e-6f) {
        c1 = p1;
        c2 = p2;
        return glm::dot(c1 - c2, c1 - c2);
    }
    if (a <= 1e-6f) {
        s = 0.0f;
        t = std::clamp(f / e, 0.0f, 1.0f);
    } else {
        float c = glm::dot(d1, r);
        if (e <= 1e-6f) {
            t = 0.0f;
            s = std::clamp(-c / a, 0.0f, 1.0f);
        } else {
            float b = glm::dot(d1, d2);
            float denom = a * e - b * b;
            if (std::abs(denom) > 1e-6f) {
                s = std::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
            } else {
                s = 0.0f;
            }
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
    return glm::dot(c1 - c2, c1 - c2);
}

static glm::vec3 ClosestPtSegmentPoint(const glm::vec3& p, const glm::vec3& q, const glm::vec3& pt) {
    glm::vec3 ab = q - p;
    float lenSq = glm::dot(ab, ab);
    if (lenSq <= 1e-6f) return p;
    float t = std::clamp(glm::dot(pt - p, ab) / lenSq, 0.0f, 1.0f);
    return p + ab * t;
}

static void ClosestPtSegmentAABB(
    const glm::vec3& p, const glm::vec3& q,
    const glm::vec3& bMin, const glm::vec3& bMax,
    glm::vec3& outSegPt, glm::vec3& outBoxPt, float& outDistSq)
{
    float bestDistSq = 1e20f;
    glm::vec3 bestSegPt = p;
    glm::vec3 bestBoxPt = glm::clamp(p, bMin, bMax);

    float tValues[8];
    int numT = 0;
    tValues[numT++] = 0.0f;
    tValues[numT++] = 1.0f;

    glm::vec3 d = q - p;
    if (std::abs(d.x) > 1e-6f) {
        float t1 = (bMin.x - p.x) / d.x;
        if (t1 >= 0.0f && t1 <= 1.0f) tValues[numT++] = t1;
        float t2 = (bMax.x - p.x) / d.x;
        if (t2 >= 0.0f && t2 <= 1.0f) tValues[numT++] = t2;
    }
    if (std::abs(d.y) > 1e-6f) {
        float t1 = (bMin.y - p.y) / d.y;
        if (t1 >= 0.0f && t1 <= 1.0f) tValues[numT++] = t1;
        float t2 = (bMax.y - p.y) / d.y;
        if (t2 >= 0.0f && t2 <= 1.0f) tValues[numT++] = t2;
    }
    if (std::abs(d.z) > 1e-6f) {
        float t1 = (bMin.z - p.z) / d.z;
        if (t1 >= 0.0f && t1 <= 1.0f) tValues[numT++] = t1;
        float t2 = (bMax.z - p.z) / d.z;
        if (t2 >= 0.0f && t2 <= 1.0f) tValues[numT++] = t2;
    }

    for (int i = 0; i < numT; ++i) {
        glm::vec3 sPt = p + d * tValues[i];
        glm::vec3 bPt = glm::clamp(sPt, bMin, bMax);
        float distSq = glm::dot(sPt - bPt, sPt - bPt);
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            bestSegPt = sPt;
            bestBoxPt = bPt;
        }
    }

    outSegPt = bestSegPt;
    outBoxPt = bestBoxPt;
    outDistSq = bestDistSq;
}

struct CharCollider {
    float radius = 0.35f;
    float height = 1.75f;
    glm::vec3 centerOffset{0.0f, 0.875f, 0.0f};
    float stepHeight = 0.35f;
};

static bool TestCharAgainstObstacle(
    const glm::vec3& charWorldPos,
    const CharCollider& col,
    const GameObject& other,
    const Scene& scene,
    glm::vec3& outPushNormal,
    float& outPenetration)
{
    if (other.isLight || IsLightPrimitive(other.type) || other.isCamera || other.type == PrimitiveType::Camera || other.isUserInterface || other.type == PrimitiveType::UserInterface) {
        return false;
    }
    if (!other.visible && other.modifiers.empty()) {
        return false;
    }
    if (other.type == PrimitiveType::Empty && other.modifiers.empty()) return false;

    bool anyActive = false;
    for (const auto& m : other.modifiers) {
        if (m.enabled && m.shapeType != CollisionShapeType::None && !m.isTrigger) {
            anyActive = true;
            break;
        }
    }
    if (!other.modifiers.empty() && !anyActive) return false;

    glm::mat4 worldMat = scene.GetWorldMatrix(other);
    glm::mat4 invWorld = glm::inverse(worldMat);

    glm::vec3 scale(
        glm::length(glm::vec3(worldMat[0])),
        glm::length(glm::vec3(worldMat[1])),
        glm::length(glm::vec3(worldMat[2]))
    );
    if (scale.x < 1e-5f || scale.y < 1e-5f || scale.z < 1e-5f) return false;

    float charHalfH = col.height * 0.5f;
    float charBottomY = std::min(charWorldPos.y, charWorldPos.y + col.centerOffset.y - charHalfH);
    float charTopY = std::max(charWorldPos.y + col.height, charWorldPos.y + col.centerOffset.y + charHalfH);
    float charSpanH = charTopY - charBottomY;
    float segHalfLen = std::max(0.0f, (charSpanH * 0.5f) - col.radius);
    glm::vec3 charCenterWorld = glm::vec3(charWorldPos.x + col.centerOffset.x, (charBottomY + charTopY) * 0.5f, charWorldPos.z + col.centerOffset.z);
    glm::vec3 charSegP0 = charCenterWorld - glm::vec3(0.0f, segHalfLen, 0.0f);
    glm::vec3 charSegP1 = charCenterWorld + glm::vec3(0.0f, segHalfLen, 0.0f);

    std::vector<CollisionModifier> effectiveMods;
    if (!other.modifiers.empty()) {
        for (const auto& m : other.modifiers) {
            if (m.enabled && m.shapeType != CollisionShapeType::None && !m.isTrigger) {
                effectiveMods.push_back(m);
            }
        }
    } else {
        CollisionModifier m;
        m.enabled = true;
        m.centerOffset = glm::vec3(0.0f);
        if (other.type == PrimitiveType::Plane || other.type == PrimitiveType::Quad) {
            m.shapeType = CollisionShapeType::Plane;
            m.planeSize = glm::vec2(
                std::max(0.1f, other.params.width > 0.0f ? other.params.width : 2.5f),
                std::max(0.1f, other.params.depth > 0.0f ? other.params.depth : 2.5f)
            );
        } else if (other.type == PrimitiveType::Sphere || other.type == PrimitiveType::UVSphere || other.type == PrimitiveType::Icosphere) {
            m.shapeType = CollisionShapeType::Sphere;
            m.radius = std::max(0.05f, other.params.radius > 0.0f ? other.params.radius : 0.6f);
        } else if (other.type == PrimitiveType::Capsule) {
            m.shapeType = CollisionShapeType::Capsule;
            m.radius = std::max(0.05f, other.params.radius > 0.0f ? other.params.radius : 0.35f);
            m.height = std::max(0.1f, other.params.height > 0.0f ? other.params.height : 0.8f);
        } else if (other.type == PrimitiveType::Cylinder) {
            m.shapeType = CollisionShapeType::Cylinder;
            m.radius = std::max(0.05f, other.params.radius > 0.0f ? other.params.radius : 0.5f);
            m.height = std::max(0.1f, other.params.height > 0.0f ? other.params.height : 1.2f);
        } else if (other.type == PrimitiveType::ImportedMesh || other.isImportedMesh) {
            m.shapeType = CollisionShapeType::Box;
            if (!other.mesh.vertices.empty()) {
                glm::vec3 minB(1e9f), maxB(-1e9f);
                for (const auto& v : other.mesh.vertices) {
                    if (std::isfinite(v.pos.x) && std::isfinite(v.pos.y) && std::isfinite(v.pos.z)) {
                        minB = glm::min(minB, v.pos);
                        maxB = glm::max(maxB, v.pos);
                    }
                }
                m.centerOffset = (minB + maxB) * 0.5f;
                m.boxExtents = glm::max((maxB - minB) * 0.5f, glm::vec3(0.05f));
            } else {
                m.boxExtents = glm::vec3(0.5f);
            }
        } else {
            m.shapeType = CollisionShapeType::Box;
            m.boxExtents = glm::vec3(
                std::max(0.05f, other.params.width * 0.5f),
                std::max(0.05f, other.params.height * 0.5f),
                std::max(0.05f, (other.params.depth > 0.0f ? other.params.depth : 1.0f) * 0.5f)
            );
        }
        effectiveMods.push_back(m);
    }

    bool hit = false;
    outPenetration = 0.0f;
    outPushNormal = glm::vec3(0.0f);

    for (const auto& mod : effectiveMods) {
        if (mod.shapeType == CollisionShapeType::Plane) {
            glm::vec3 nPlane = glm::normalize(glm::vec3(worldMat[1]));
            if (nPlane.y > 0.65f) {
                // Ground / floor plane: provides vertical support, does not block horizontal movement
                continue;
            }
            glm::vec3 planePoint = glm::vec3(worldMat * glm::vec4(mod.centerOffset, 1.0f));
            glm::vec3 closestSegPt = ClosestPtSegmentPoint(charSegP0, charSegP1, planePoint);
            float distToPlane = glm::dot(closestSegPt - planePoint, nPlane);
            if (std::abs(distToPlane) < col.radius) {
                glm::vec2 halfSize = mod.planeSize * 0.5f;
                glm::vec3 localPt = glm::vec3(invWorld * glm::vec4(closestSegPt, 1.0f));
                if (std::abs(localPt.x - mod.centerOffset.x) <= (halfSize.x + col.radius / scale.x) &&
                    std::abs(localPt.z - mod.centerOffset.z) <= (halfSize.y + col.radius / scale.z)) {
                    float pen = col.radius - std::abs(distToPlane);
                    if (pen > outPenetration) {
                        outPenetration = pen;
                        outPushNormal = (distToPlane >= 0.0f ? 1.0f : -1.0f) * nPlane;
                        outPushNormal.y = 0.0f;
                        if (glm::length(outPushNormal) > 1e-4f) outPushNormal = glm::normalize(outPushNormal);
                        else outPushNormal = glm::vec3(0, 0, 1);
                        hit = true;
                    }
                }
            }
            continue;
        }

        if (mod.shapeType == CollisionShapeType::Sphere) {
            glm::vec3 worldCenter = glm::vec3(worldMat * glm::vec4(mod.centerOffset, 1.0f));
            float worldRadius = mod.radius * std::max({scale.x, scale.y, scale.z});
            float worldHalfH = mod.radius * scale.y;

            if (worldCenter.y + worldHalfH <= (charBottomY - 0.05f) || worldCenter.y - worldHalfH >= (charTopY + 0.05f)) {
                continue;
            }

            glm::vec3 closestSegPt = ClosestPtSegmentPoint(charSegP0, charSegP1, worldCenter);
            glm::vec3 diffWorld = closestSegPt - worldCenter;
            float hDist = glm::length(diffWorld);
            float minAllowed = col.radius + worldRadius;
            if (hDist < minAllowed) {
                float pen = minAllowed - hDist;
                if (pen > outPenetration) {
                    outPenetration = pen;
                    glm::vec3 norm = (hDist > 1e-4f) ? (diffWorld / hDist) : glm::vec3(0, 0, 1);
                    norm.y = 0.0f;
                    if (glm::length(norm) > 1e-4f) norm = glm::normalize(norm);
                    else norm = glm::vec3(0, 0, 1);
                    outPushNormal = norm;
                    hit = true;
                }
            }
            continue;
        }

        if (mod.shapeType == CollisionShapeType::Cylinder || mod.shapeType == CollisionShapeType::Capsule || mod.shapeType == CollisionShapeType::Cone || mod.shapeType == CollisionShapeType::Character) {
            float obsHalfH = mod.height * 0.5f;
            float obsSegLen = (mod.shapeType == CollisionShapeType::Capsule || mod.shapeType == CollisionShapeType::Character) ? std::max(0.0f, obsHalfH - mod.radius) : obsHalfH;
            glm::vec3 obsSeg0 = mod.centerOffset - glm::vec3(0.0f, obsSegLen, 0.0f);
            glm::vec3 obsSeg1 = mod.centerOffset + glm::vec3(0.0f, obsSegLen, 0.0f);
            glm::vec3 worldObsP0 = glm::vec3(worldMat * glm::vec4(obsSeg0, 1.0f));
            glm::vec3 worldObsP1 = glm::vec3(worldMat * glm::vec4(obsSeg1, 1.0f));

            float worldCenterY = (worldObsP0.y + worldObsP1.y) * 0.5f;
            float worldTotalHalfH = (obsHalfH * scale.y);
            if (worldCenterY + worldTotalHalfH <= (charBottomY - 0.05f) || worldCenterY - worldTotalHalfH >= (charTopY + 0.05f)) {
                continue;
            }

            glm::vec3 c1, c2;
            ClosestPtSegmentSegment(charSegP0, charSegP1, worldObsP0, worldObsP1, c1, c2);
            glm::vec3 diffWorld = c1 - c2;
            float hDist = glm::length(diffWorld);
            float worldRadius = mod.radius * std::max(scale.x, scale.z);
            float minAllowed = col.radius + worldRadius;
            if (hDist < minAllowed) {
                float pen = minAllowed - hDist;
                if (pen > outPenetration) {
                    outPenetration = pen;
                    glm::vec3 norm = (hDist > 1e-4f) ? (diffWorld / hDist) : glm::vec3(0, 0, 1);
                    norm.y = 0.0f;
                    if (glm::length(norm) > 1e-4f) norm = glm::normalize(norm);
                    else norm = glm::vec3(0, 0, 1);
                    outPushNormal = norm;
                    hit = true;
                }
            }
            continue;
        }

        // Box / Mesh OBB
        glm::vec3 C = mod.centerOffset;
        glm::vec3 E = mod.boxExtents;
        glm::vec3 bMin = C - E;
        glm::vec3 bMax = C + E;

        float worldMinY = 1e9f, worldMaxY = -1e9f;
        for (int cx = -1; cx <= 1; cx += 2) {
            for (int cy = -1; cy <= 1; cy += 2) {
                for (int cz = -1; cz <= 1; cz += 2) {
                    glm::vec3 corner = C + glm::vec3(cx * E.x, cy * E.y, cz * E.z);
                    float wy = (worldMat * glm::vec4(corner, 1.0f)).y;
                    worldMinY = std::min(worldMinY, wy);
                    worldMaxY = std::max(worldMaxY, wy);
                }
            }
        }

        if (worldMaxY <= (charBottomY - 0.05f) || worldMinY >= (charTopY + 0.05f)) {
            continue;
        }

        glm::vec3 locSegP0 = glm::vec3(invWorld * glm::vec4(charSegP0, 1.0f));
        glm::vec3 locSegP1 = glm::vec3(invWorld * glm::vec4(charSegP1, 1.0f));

        glm::vec3 outSegPt, outBoxPt;
        float distSq = 0.0f;
        ClosestPtSegmentAABB(locSegP0, locSegP1, bMin, bMax, outSegPt, outBoxPt, distSq);

        if (distSq > 1e-7f) {
            glm::vec3 worldSegPt = glm::vec3(worldMat * glm::vec4(outSegPt, 1.0f));
            glm::vec3 worldBoxPt = glm::vec3(worldMat * glm::vec4(outBoxPt, 1.0f));
            glm::vec3 diffWorld = worldSegPt - worldBoxPt;
            float worldDist = glm::length(diffWorld);
            if (worldDist < col.radius) {
                float pen = col.radius - worldDist;
                if (pen > outPenetration) {
                    outPenetration = pen;
                    glm::vec3 worldNorm = (worldDist > 1e-4f) ? (diffWorld / worldDist) : glm::vec3(0, 0, 1);
                    worldNorm.y = 0.0f;
                    if (glm::length(worldNorm) > 1e-4f) worldNorm = glm::normalize(worldNorm);
                    else worldNorm = glm::vec3(0, 0, 1);
                    outPushNormal = worldNorm;
                    hit = true;
                }
            }
        } else {
            // Segment intersects or penetrates box
            float dLeft  = (outSegPt.x - bMin.x) * scale.x;
            float dRight = (bMax.x - outSegPt.x) * scale.x;
            float dBack  = (outSegPt.z - bMin.z) * scale.z;
            float dFront = (bMax.z - outSegPt.z) * scale.z;

            float minD = std::min({dLeft, dRight, dBack, dFront});
            glm::vec3 localNorm(0.0f);
            if (minD == dLeft) localNorm = glm::vec3(-1, 0, 0);
            else if (minD == dRight) localNorm = glm::vec3(1, 0, 0);
            else if (minD == dBack) localNorm = glm::vec3(0, 0, -1);
            else localNorm = glm::vec3(0, 0, 1);

            glm::vec3 worldNorm = glm::vec3(worldMat * glm::vec4(localNorm, 0.0f));
            worldNorm.y = 0.0f;
            if (glm::length(worldNorm) > 1e-4f) worldNorm = glm::normalize(worldNorm);
            else worldNorm = glm::vec3(0, 0, 1);

            float pen = col.radius + minD;
            if (pen > outPenetration) {
                outPenetration = pen;
                outPushNormal = worldNorm;
                hit = true;
            }
        }
    }

    return hit;
}

static glm::vec3 ResolveCharacterMovement(GameObject* go, const glm::vec3& targetPos) {
    if (!go) return targetPos;

    bool isChar = (go->hasPhysics && go->physics.isCharacterPhysics);
    if (!isChar) {
        for (const auto& mod : go->modifiers) {
            if (mod.enabled && (mod.shapeType == CollisionShapeType::Character || mod.shapeType == CollisionShapeType::Capsule || mod.shapeType == CollisionShapeType::Box)) {
                isChar = true;
                break;
            }
        }
    }
    if (!isChar) {
        for (const auto& b : go->behaviours) {
            if (b && b->IsEnabled()) { isChar = true; break; }
        }
    }
    if (!isChar) return targetPos;

    Scene* scene = GetSceneForGameObject(go);
    if (!scene) return targetPos;

    CharCollider col;
    col.radius = std::max(0.05f, 0.35f * std::max(std::abs(go->scale.x), std::abs(go->scale.z)));
    col.height = std::max(0.1f, 1.75f * std::abs(go->scale.y));
    col.centerOffset = glm::vec3(0.0f, col.height * 0.5f, 0.0f);
    col.stepHeight = 0.35f * std::abs(go->scale.y);

    if (!go->modifiers.empty()) {
        for (const auto& mod : go->modifiers) {
            if (mod.enabled && (mod.shapeType == CollisionShapeType::Character || mod.shapeType == CollisionShapeType::Capsule || mod.shapeType == CollisionShapeType::Box || mod.shapeType == CollisionShapeType::Cylinder || mod.shapeType == CollisionShapeType::Sphere)) {
                if (mod.shapeType == CollisionShapeType::Box) {
                    col.radius = std::max(0.05f, std::max(mod.boxExtents.x * std::abs(go->scale.x), mod.boxExtents.z * std::abs(go->scale.z)));
                    col.height = std::max(0.1f, mod.boxExtents.y * 2.0f * std::abs(go->scale.y));
                } else if (mod.shapeType == CollisionShapeType::Sphere) {
                    col.radius = std::max(0.05f, mod.radius * std::max(std::abs(go->scale.x), std::abs(go->scale.z)));
                    col.height = std::max(0.1f, mod.radius * 2.0f * std::abs(go->scale.y));
                } else {
                    col.radius = std::max(0.05f, mod.radius * std::max(std::abs(go->scale.x), std::abs(go->scale.z)));
                    col.height = std::max(0.1f, mod.height * std::abs(go->scale.y));
                }
                col.centerOffset = mod.centerOffset * go->scale;
                break;
            }
        }
    }

    glm::vec3 curPos = go->position;
    glm::vec3 moveDelta = targetPos - curPos;
    if (glm::length(moveDelta) < 1e-6f) return curPos;

    // 1. Initial de-penetration: if already inside any obstacle, push out
    for (int iter = 0; iter < 4; ++iter) {
        float maxPen = 0.0f;
        glm::vec3 bestNorm(0.0f);
        for (const auto& other : scene->objects) {
            if (&other == go || other.id == go->id || other.parentId == go->id) continue;
            float pen = 0.0f;
            glm::vec3 norm(0.0f);
            if (TestCharAgainstObstacle(curPos, col, other, *scene, norm, pen)) {
                if (pen > maxPen) {
                    maxPen = pen;
                    bestNorm = norm;
                }
            }
        }
        if (maxPen > 0.001f) {
            curPos += bestNorm * (maxPen + 0.002f);
        } else {
            break;
        }
    }

    // 2. Continuous sub-stepped sweep with surface sliding & step-up
    float totalDist = glm::length(moveDelta);
    int numSubSteps = std::clamp((int)std::ceil(totalDist / 0.035f), 1, 10);
    glm::vec3 subDelta = moveDelta / (float)numSubSteps;

    glm::vec3 resolvedPos = curPos;

    for (int step = 0; step < numSubSteps; ++step) {
        glm::vec3 candidate = resolvedPos + subDelta;

        float maxPen = 0.0f;
        glm::vec3 bestNorm(0.0f);
        GameObject* hitObj = nullptr;

        for (auto& other : scene->objects) {
            if (&other == go || other.id == go->id || other.parentId == go->id) continue;
            float pen = 0.0f;
            glm::vec3 norm(0.0f);
            if (TestCharAgainstObstacle(candidate, col, other, *scene, norm, pen)) {
                if (pen > maxPen) {
                    maxPen = pen;
                    bestNorm = norm;
                    hitObj = &other;
                }
            }
        }

        if (maxPen <= 0.0001f) {
            resolvedPos = candidate;
        } else {
            // Push dynamic physics obstacle
            if (hitObj && hitObj->hasPhysics && hitObj->physics.enabled && !hitObj->physics.isKinematic) {
                float charMass = (go->hasPhysics) ? go->physics.mass : 75.0f;
                hitObj->physics.accumulatedForce += -bestNorm * (charMass * 25.0f);
                hitObj->physics.velocity += -bestNorm * 0.3f;
            }

            // A. Try Step-Up ONLY for low stairs / small ledges (<= stepHeight)
            bool steppedUp = false;
            float stepRise = 0.0f;
            if (hitObj) {
                glm::mat4 hWorld = scene->GetWorldMatrix(*hitObj);
                float hWorldMaxY = -1e9f;
                if (!hitObj->modifiers.empty()) {
                    for (const auto& hm : hitObj->modifiers) {
                        if (hm.enabled && (hm.shapeType == CollisionShapeType::Box || hm.shapeType == CollisionShapeType::Mesh)) {
                            for (int cx = -1; cx <= 1; cx += 2) {
                                for (int cy = -1; cy <= 1; cy += 2) {
                                    for (int cz = -1; cz <= 1; cz += 2) {
                                        glm::vec3 cnr = hm.centerOffset + glm::vec3(cx * hm.boxExtents.x, cy * hm.boxExtents.y, cz * hm.boxExtents.z);
                                        hWorldMaxY = std::max(hWorldMaxY, (hWorld * glm::vec4(cnr, 1.0f)).y);
                                    }
                                }
                            }
                        } else if (hm.enabled && (hm.shapeType == CollisionShapeType::Cylinder || hm.shapeType == CollisionShapeType::Capsule)) {
                            glm::vec3 cnr = hm.centerOffset + glm::vec3(0.0f, hm.height * 0.5f, 0.0f);
                            hWorldMaxY = std::max(hWorldMaxY, (hWorld * glm::vec4(cnr, 1.0f)).y);
                        }
                    }
                }
                if (hWorldMaxY > -1e8f) {
                    float charCurBottomY = std::min(resolvedPos.y, resolvedPos.y + col.centerOffset.y - col.height * 0.5f);
                    stepRise = hWorldMaxY - charCurBottomY;
                }
            }

            bool canAttemptStep = (col.stepHeight > 0.05f && stepRise > 0.02f && stepRise <= col.stepHeight);
            if (canAttemptStep) {
                glm::vec3 elevatedPos = resolvedPos + glm::vec3(0.0f, stepRise + 0.02f, 0.0f) + subDelta;
                bool elevatedBlocked = false;
                for (const auto& other : scene->objects) {
                    if (&other == go || other.id == go->id || other.parentId == go->id) continue;
                    float pen = 0.0f; glm::vec3 norm(0.0f);
                    if (TestCharAgainstObstacle(elevatedPos, col, other, *scene, norm, pen)) {
                        elevatedBlocked = true;
                        break;
                    }
                }
                if (!elevatedBlocked) {
                    resolvedPos = elevatedPos;
                    steppedUp = true;
                }
            }

            if (!steppedUp) {
                // B. Surface tangent sliding
                glm::vec3 slideDelta = subDelta - glm::dot(subDelta, bestNorm) * bestNorm;
                slideDelta.y = subDelta.y;

                glm::vec3 slideCandidate = resolvedPos + slideDelta;
                float slidePen = 0.0f;
                glm::vec3 slideNorm(0.0f);
                for (const auto& other : scene->objects) {
                    if (&other == go || other.id == go->id || other.parentId == go->id) continue;
                    float pen = 0.0f;
                    glm::vec3 norm(0.0f);
                    if (TestCharAgainstObstacle(slideCandidate, col, other, *scene, norm, pen)) {
                        if (pen > slidePen) {
                            slidePen = pen;
                            slideNorm = norm;
                        }
                    }
                }

                if (slidePen <= 0.0001f) {
                    resolvedPos = slideCandidate;
                } else {
                    // C. Double-Plane Crease (Corner) Sliding
                    glm::vec3 crease = glm::cross(bestNorm, slideNorm);
                    float creaseLen = glm::length(crease);
                    bool creaseResolved = false;

                    if (creaseLen > 1e-3f) {
                        crease /= creaseLen;
                        glm::vec3 creaseDelta = glm::dot(subDelta, crease) * crease;
                        creaseDelta.y = subDelta.y;
                        glm::vec3 creaseCandidate = resolvedPos + creaseDelta;

                        bool creaseBlocked = false;
                        for (const auto& other : scene->objects) {
                            if (&other == go || other.id == go->id || other.parentId == go->id) continue;
                            float pen = 0.0f; glm::vec3 norm(0.0f);
                            if (TestCharAgainstObstacle(creaseCandidate, col, other, *scene, norm, pen)) {
                                creaseBlocked = true;
                                break;
                            }
                        }
                        if (!creaseBlocked) {
                            resolvedPos = creaseCandidate;
                            creaseResolved = true;
                        }
                    }

                    if (!creaseResolved) {
                        // D. Fallback individual axis sliding
                        glm::vec3 tryX = resolvedPos + glm::vec3(subDelta.x, subDelta.y, 0.0f);
                        bool xBlocked = false;
                        for (const auto& other : scene->objects) {
                            if (&other == go || other.id == go->id || other.parentId == go->id) continue;
                            float pen = 0.0f; glm::vec3 norm(0.0f);
                            if (TestCharAgainstObstacle(tryX, col, other, *scene, norm, pen)) { xBlocked = true; break; }
                        }
                        if (!xBlocked) {
                            resolvedPos = tryX;
                        } else {
                            glm::vec3 tryZ = resolvedPos + glm::vec3(0.0f, subDelta.y, subDelta.z);
                            bool zBlocked = false;
                            for (const auto& other : scene->objects) {
                                if (&other == go || other.id == go->id || other.parentId == go->id) continue;
                                float pen = 0.0f; glm::vec3 norm(0.0f);
                                if (TestCharAgainstObstacle(tryZ, col, other, *scene, norm, pen)) { zBlocked = true; break; }
                            }
                            if (!zBlocked) {
                                resolvedPos = tryZ;
                            }
                        }
                    }
                }
            }
        }
    }

    // Strict upright locking & zero unwanted collision torque
    if (go->hasPhysics) {
        go->physics.angularVelocity = glm::vec3(0.0f);
        if (go->physics.isCharacterPhysics) {
            go->rotation.x = 0.0f;
            go->rotation.z = 0.0f;
        }
    }

    return resolvedPos;
}

glm::vec3 GameObjectTransform::GetRelativeLocationInternal() const {
    if (!gameObject) return glm::vec3(0.0f);
    return gameObject->position;
}

void GameObjectTransform::SetRelativeLocationInternal(const glm::vec3& v) {
    if (!gameObject) return;
    gameObject->position = ResolveCharacterMovement(gameObject, v);
}

glm::vec3 GameObjectTransform::GetRelativeRotationInternal() const {
    if (!gameObject) return glm::vec3(0.0f);
    return gameObject->rotation;
}

void GameObjectTransform::SetRelativeRotationInternal(const glm::vec3& v) {
    if (!gameObject) return;
    glm::vec3 targetRot = v;
    if (gameObject->hasPhysics && gameObject->physics.isCharacterPhysics) {
        targetRot.x = 0.0f;
        targetRot.z = 0.0f;
    }
    gameObject->rotation = targetRot;
}

glm::vec3 GameObjectTransform::GetRelativeScaleInternal() const {
    if (!gameObject) return glm::vec3(1.0f);
    return gameObject->scale;
}

void GameObjectTransform::SetRelativeScaleInternal(const glm::vec3& v) {
    if (!gameObject) return;
    gameObject->scale = v;
}

glm::vec3 GameObjectTransform::GetWorldLocationInternal() const {
    if (!gameObject) return glm::vec3(0.0f);
    Scene* scene = GetSceneForGameObject(gameObject);
    if (!scene || gameObject->parentId == -1) {
        return gameObject->position;
    }
    return scene->GetWorldPosition(*gameObject);
}

void GameObjectTransform::SetWorldLocationInternal(const glm::vec3& v) {
    if (!gameObject) return;
    Scene* scene = GetSceneForGameObject(gameObject);
    if (!scene || gameObject->parentId == -1) {
        gameObject->position = ResolveCharacterMovement(gameObject, v);
        return;
    }
    const GameObject* parent = scene->FindObjectConst(gameObject->parentId);
    if (!parent) {
        gameObject->position = ResolveCharacterMovement(gameObject, v);
        return;
    }
    glm::mat4 pMat = scene->GetWorldMatrix(*parent);
    glm::mat4 invP = glm::inverse(pMat);
    glm::vec3 localPos = glm::vec3(invP * glm::vec4(v, 1.0f));
    gameObject->position = ResolveCharacterMovement(gameObject, localPos);
}

glm::vec3 GameObjectTransform::GetWorldRotationInternal() const {
    if (!gameObject) return glm::vec3(0.0f);
    Scene* scene = GetSceneForGameObject(gameObject);
    if (!scene || gameObject->parentId == -1) {
        return gameObject->rotation;
    }
    glm::vec3 worldRot = gameObject->rotation;
    const GameObject* p = scene->FindObjectConst(gameObject->parentId);
    while (p) {
        worldRot += p->rotation;
        p = (p->parentId != -1) ? scene->FindObjectConst(p->parentId) : nullptr;
    }
    return worldRot;
}

void GameObjectTransform::SetWorldRotationInternal(const glm::vec3& v) {
    if (!gameObject) return;
    Scene* scene = GetSceneForGameObject(gameObject);
    if (!scene || gameObject->parentId == -1) {
        gameObject->rotation = v;
        return;
    }
    glm::vec3 parentRot(0.0f);
    const GameObject* p = scene->FindObjectConst(gameObject->parentId);
    while (p) {
        parentRot += p->rotation;
        p = (p->parentId != -1) ? scene->FindObjectConst(p->parentId) : nullptr;
    }
    gameObject->rotation = v - parentRot;
}

glm::vec3 GameObjectTransform::GetWorldScaleInternal() const {
    if (!gameObject) return glm::vec3(1.0f);
    Scene* scene = GetSceneForGameObject(gameObject);
    if (!scene || gameObject->parentId == -1) {
        return gameObject->scale;
    }
    glm::mat4 worldMat = scene->GetWorldMatrix(*gameObject);
    return glm::vec3(glm::length(glm::vec3(worldMat[0])),
                     glm::length(glm::vec3(worldMat[1])),
                     glm::length(glm::vec3(worldMat[2])));
}

void GameObjectTransform::SetWorldScaleInternal(const glm::vec3& v) {
    if (!gameObject) return;
    Scene* scene = GetSceneForGameObject(gameObject);
    if (!scene || gameObject->parentId == -1) {
        gameObject->scale = v;
        return;
    }
    const GameObject* parent = scene->FindObjectConst(gameObject->parentId);
    if (!parent) {
        gameObject->scale = v;
        return;
    }
    glm::mat4 pMat = scene->GetWorldMatrix(*parent);
    glm::vec3 pScale(glm::length(glm::vec3(pMat[0])),
                     glm::length(glm::vec3(pMat[1])),
                     glm::length(glm::vec3(pMat[2])));
    if (pScale.x > 1e-6f && pScale.y > 1e-6f && pScale.z > 1e-6f) {
        gameObject->scale = v / pScale;
    } else {
        gameObject->scale = v;
    }
}

// GameObjectTransform::AxisProxy implementations
glm::vec3 GameObjectTransform::AxisProxy::GetValue() const {
    if (!parent) return glm::vec3(0.0f);
    switch (target) {
        case TargetType::WorldLocation:    return parent->GetWorldLocationInternal();
        case TargetType::RelativeLocation: return parent->GetRelativeLocationInternal();
        case TargetType::WorldRotation:    return parent->GetWorldRotationInternal();
        case TargetType::RelativeRotation: return parent->GetRelativeRotationInternal();
        case TargetType::WorldScale:       return parent->GetWorldScaleInternal();
        case TargetType::RelativeScale:    return parent->GetRelativeScaleInternal();
    }
    return glm::vec3(0.0f);
}

void GameObjectTransform::AxisProxy::SetValue(const glm::vec3& v) {
    if (!parent) return;
    switch (target) {
        case TargetType::WorldLocation:    parent->SetWorldLocationInternal(v); break;
        case TargetType::RelativeLocation: parent->SetRelativeLocationInternal(v); break;
        case TargetType::WorldRotation:    parent->SetWorldRotationInternal(v); break;
        case TargetType::RelativeRotation: parent->SetRelativeRotationInternal(v); break;
        case TargetType::WorldScale:       parent->SetWorldScaleInternal(v); break;
        case TargetType::RelativeScale:    parent->SetRelativeScaleInternal(v); break;
    }
}

void GameObjectTransform::AxisProxy::operator()(float inX, float inY, float inZ) {
    if (target == TargetType::RelativeRotation || target == TargetType::WorldRotation) {
        glm::vec3 current = GetValue();
        bool isCamera = false;
        if (parent && parent->gameObject) {
            isCamera = (parent->gameObject->isCamera || parent->gameObject->type == PrimitiveType::Camera);
        }
        if (inX != 0.0f) current.x = inX;
        if (inY != 0.0f) current.y = inY;
        if (inZ != 0.0f) current.z = inZ;
        if (isCamera) {
            current.x = std::clamp(current.x, -89.0f, 89.0f);
        }
        SetValue(current);
        return;
    }
    SetValue(glm::vec3(inX, inY, inZ));
}

void GameObjectTransform::AxisProxy::operator()(const glm::vec3& v) {
    SetValue(v);
}

void GameObjectTransform::AxisProxy::operator()(const glm::vec3* v) {
    if (v) {
        SetValue(*v);
        delete v;
    }
}

void GameObjectTransform::AxisProxy::operator()(const TransformVector3& v) {
    SetValue(glm::vec3(v.x, v.y, v.z));
}

void GameObjectTransform::AxisProxy::operator()(const TransformVector3* v) {
    if (v) {
        SetValue(glm::vec3(v->x, v->y, v->z));
        delete v;
    }
}

TransformVector3 GameObjectTransform::AxisProxy::operator()() const {
    return TransformVector3(GetValue());
}

void GameObjectTransform::AxisProxy::X(float val) {
    glm::vec3 v = GetValue();
    v.x = val;
    SetValue(v);
}

void GameObjectTransform::AxisProxy::Y(float val) {
    glm::vec3 v = GetValue();
    v.y = val;
    SetValue(v);
}

void GameObjectTransform::AxisProxy::Z(float val) {
    glm::vec3 v = GetValue();
    v.z = val;
    SetValue(v);
}

TransformState GameObjectTransform::GetWorldTransform() const {
    TransformState state;
    state.Location = TransformVector3(GetWorldLocationInternal());
    state.Rotation = TransformVector3(GetWorldRotationInternal());
    state.Scale    = TransformVector3(GetWorldScaleInternal());
    if (gameObject) {
        Scene* scene = GetSceneForGameObject(gameObject);
        if (scene) {
            state.Matrix = scene->GetWorldMatrix(*gameObject);
        } else {
            state.Matrix = gameObject->GetLocalMatrix();
        }
    }
    return state;
}

TransformState GameObjectTransform::GetRelativeTransform() const {
    TransformState state;
    state.Location = TransformVector3(GetRelativeLocationInternal());
    state.Rotation = TransformVector3(GetRelativeRotationInternal());
    state.Scale    = TransformVector3(GetRelativeScaleInternal());
    if (gameObject) {
        state.Matrix = gameObject->GetLocalMatrix();
    }
    return state;
}

TransformVector3 GameObjectTransform::GetForwardVector() const {
    glm::vec3 rot = GetWorldRotationInternal();
    float rYaw = glm::radians(rot.y);
    float rPitch = glm::radians(rot.x);
    float x = -std::cos(rPitch) * std::sin(rYaw);
    float y = std::sin(rPitch);
    float z = -std::cos(rPitch) * std::cos(rYaw);
    return TransformVector3(glm::normalize(glm::vec3(x, y, z)));
}

TransformVector3 GameObjectTransform::GetRightVector() const {
    glm::vec3 rot = GetWorldRotationInternal();
    float rYaw = glm::radians(rot.y);
    return TransformVector3(glm::normalize(glm::vec3(std::cos(rYaw), 0.0f, -std::sin(rYaw))));
}

TransformVector3 GameObjectTransform::GetUpVector() const {
    glm::vec3 fwd = GetForwardVector();
    glm::vec3 right = GetRightVector();
    return TransformVector3(glm::normalize(glm::cross(glm::vec3(right.x, right.y, right.z), glm::vec3(fwd.x, fwd.y, fwd.z))));
}

void GameObjectTransform::WorldTranslate(const glm::vec3& delta) {
    if (!gameObject) return;
    SetWorldLocationInternal(GetWorldLocationInternal() + delta);
}

void GameObjectTransform::WorldTranslate(const glm::vec3& fromPoint, const glm::vec3& toLocation) {
    if (!gameObject) return;
    WorldTranslate(toLocation - fromPoint);
}

void GameObjectTransform::RelativeTranslate(const glm::vec3& delta) {
    if (!gameObject) return;
    glm::vec3 rot = GetRelativeRotationInternal();
    float rYaw = glm::radians(rot.y);
    glm::vec3 forward = glm::vec3(-std::sin(rYaw), 0.0f, -std::cos(rYaw));
    glm::vec3 right   = glm::vec3( std::cos(rYaw), 0.0f, -std::sin(rYaw));
    glm::vec3 up      = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 localDelta = delta.x * forward + delta.y * up + delta.z * right;
    SetRelativeLocationInternal(GetRelativeLocationInternal() + localDelta);
}

void GameObjectTransform::RelativeTranslate(const glm::vec3& fromPoint, const glm::vec3& toLocation) {
    if (!gameObject) return;
    RelativeTranslate(toLocation - fromPoint);
}

// BehaviourTransform::AxisProxy implementations
glm::vec3 BehaviourTransform::AxisProxy::GetValue() const {
    if (!parent) return glm::vec3(0.0f);
    switch (target) {
        case TargetType::WorldLocation:    return parent->Location.WorldSpace();
        case TargetType::RelativeLocation: return parent->Location.LocalSpace();
        case TargetType::WorldRotation:    return parent->Rotation.WorldSpace();
        case TargetType::RelativeRotation: return parent->Rotation.LocalSpace();
        case TargetType::WorldScale:       return parent->Scale.WorldSpace();
        case TargetType::RelativeScale:    return parent->Scale.LocalSpace();
    }
    return glm::vec3(0.0f);
}

void BehaviourTransform::AxisProxy::SetValue(const glm::vec3& v) {
    if (!parent) return;
    switch (target) {
        case TargetType::WorldLocation:    parent->Location.WorldSpace(v); break;
        case TargetType::RelativeLocation: parent->Location.LocalSpace(v); break;
        case TargetType::WorldRotation:    parent->Rotation.WorldSpace(v); break;
        case TargetType::RelativeRotation: parent->Rotation.LocalSpace(v); break;
        case TargetType::WorldScale:       parent->Scale.WorldSpace(v); break;
        case TargetType::RelativeScale:    parent->Scale.LocalSpace(v); break;
    }
}

void BehaviourTransform::AxisProxy::operator()(float inX, float inY, float inZ) {
    if (target == TargetType::RelativeRotation || target == TargetType::WorldRotation) {
        glm::vec3 current = GetValue();
        bool isCamera = false;
        if (parent && parent->m_behaviour && parent->m_behaviour->GetOwner()) {
            auto* owner = parent->m_behaviour->GetOwner();
            isCamera = (owner->isCamera || owner->type == PrimitiveType::Camera);
        }
        if (inX != 0.0f) current.x = inX;
        if (inY != 0.0f) current.y = inY;
        if (inZ != 0.0f) current.z = inZ;
        if (isCamera) {
            current.x = std::clamp(current.x, -89.0f, 89.0f);
        }
        SetValue(current);
        return;
    }
    SetValue(glm::vec3(inX, inY, inZ));
}

void BehaviourTransform::AxisProxy::operator()(const glm::vec3& v) {
    SetValue(v);
}

void BehaviourTransform::AxisProxy::operator()(const glm::vec3* v) {
    if (v) {
        SetValue(*v);
        delete v;
    }
}

void BehaviourTransform::AxisProxy::operator()(const TransformVector3& v) {
    SetValue(glm::vec3(v.x, v.y, v.z));
}

void BehaviourTransform::AxisProxy::operator()(const TransformVector3* v) {
    if (v) {
        SetValue(glm::vec3(v->x, v->y, v->z));
        delete v;
    }
}

TransformVector3 BehaviourTransform::AxisProxy::operator()() const {
    return TransformVector3(GetValue());
}

void BehaviourTransform::AxisProxy::X(float val) {
    glm::vec3 v = GetValue();
    v.x = val;
    SetValue(v);
}

void BehaviourTransform::AxisProxy::Y(float val) {
    glm::vec3 v = GetValue();
    v.y = val;
    SetValue(v);
}

void BehaviourTransform::AxisProxy::Z(float val) {
    glm::vec3 v = GetValue();
    v.z = val;
    SetValue(v);
}

TransformState BehaviourTransform::GetWorldTransform() const {
    TransformState state;
    state.Location = TransformVector3(WorldLocation());
    state.Rotation = TransformVector3(WorldRotation());
    state.Scale    = TransformVector3(WorldScale());
    return state;
}

TransformState BehaviourTransform::GetRelativeTransform() const {
    TransformState state;
    state.Location = TransformVector3(RelativeLocation());
    state.Rotation = TransformVector3(RelativeRotation());
    state.Scale    = TransformVector3(RelativeScale());
    return state;
}

TransformVector3 BehaviourTransform::GetForwardVector() const {
    glm::vec3 rot = WorldRotation();
    float rYaw = glm::radians(rot.y);
    float rPitch = glm::radians(rot.x);
    float x = -std::cos(rPitch) * std::sin(rYaw);
    float y = std::sin(rPitch);
    float z = -std::cos(rPitch) * std::cos(rYaw);
    return TransformVector3(glm::normalize(glm::vec3(x, y, z)));
}

TransformVector3 BehaviourTransform::GetRightVector() const {
    glm::vec3 rot = WorldRotation();
    float rYaw = glm::radians(rot.y);
    return TransformVector3(glm::normalize(glm::vec3(std::cos(rYaw), 0.0f, -std::sin(rYaw))));
}

TransformVector3 BehaviourTransform::GetUpVector() const {
    glm::vec3 fwd = GetForwardVector();
    glm::vec3 right = GetRightVector();
    return TransformVector3(glm::normalize(glm::cross(glm::vec3(right.x, right.y, right.z), glm::vec3(fwd.x, fwd.y, fwd.z))));
}

// ============================================================================
// EunoiaBehaviour Property Registration Implementations
// ============================================================================

void EunoiaBehaviour::RegisterProperty(const std::string& name, bool* val, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Bool;
    prop.dataPtr = val;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterProperty(const std::string& name, int* val, const std::string& category, int min, int max) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Int;
    prop.dataPtr = val;
    if (min != max) {
        prop.hasRange = true;
        prop.minVal = (float)min;
        prop.maxVal = (float)max;
    }
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterProperty(const std::string& name, int64_t* val, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Int64;
    prop.dataPtr = val;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterProperty(const std::string& name, float* val, const std::string& category, float min, float max) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Float;
    prop.dataPtr = val;
    if (min != max) {
        prop.hasRange = true;
        prop.minVal = min;
        prop.maxVal = max;
    }
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterProperty(const std::string& name, double* val, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Double;
    prop.dataPtr = val;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterProperty(const std::string& name, std::string* val, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::String;
    prop.dataPtr = val;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterProperty(const std::string& name, glm::vec2* val, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Vec2;
    prop.dataPtr = val;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterProperty(const std::string& name, glm::vec3* val, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Vec3;
    prop.dataPtr = val;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterProperty(const std::string& name, glm::vec4* val, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Vec4;
    prop.dataPtr = val;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterColor(const std::string& name, glm::vec3* val, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::Color3;
    prop.dataPtr = val;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::RegisterReference(const std::string& name, void* ptrAddr, const std::string& category) {
    RegisterReference(name, ptrAddr, ObjectRefType::Actor, category);
}

void EunoiaBehaviour::RegisterReference(const std::string& name, void* ptrAddr, ObjectRefType refType, const std::string& category) {
    BehaviourProperty prop;
    prop.name = name;
    prop.displayName = name;
    prop.category = category;
    prop.type = BehaviourPropertyType::ObjectRef;
    prop.refType = refType;
    prop.dataPtr = ptrAddr;
    prop.targetId = -1;
    m_properties.push_back(prop);
}

void EunoiaBehaviour::ResolveReferences(Scene& scene) {
    m_scene = &scene;
    for (auto& prop : m_properties) {
        if (prop.type != BehaviourPropertyType::ObjectRef || !prop.dataPtr) continue;

        if (prop.refType == ObjectRefType::UserInterface) {
            UserInterface* resolvedUI = nullptr;
            if (!prop.assetPath.empty()) {
                resolvedUI = EunoiaUI::UISystemManager::Get().GetOrLoadUI(prop.assetPath);
            }
            if (!resolvedUI) {
                resolvedUI = EunoiaUI::UISystemManager::Get().GetOrLoadUI(prop.name);
            }
            if (!resolvedUI) {
                for (const auto& pair : EunoiaUI::UISystemManager::Get().GetLoadedUIs()) {
                    if (pair.second && (pair.second->name == prop.name || pair.first.find(prop.name) != std::string::npos)) {
                        resolvedUI = pair.second.get();
                        break;
                    }
                }
            }
            *reinterpret_cast<UserInterface**>(prop.dataPtr) = resolvedUI;
            prop.isMissing = false;
            continue;
        }

        if (prop.targetId == -1) {
            if (m_owner && (prop.name == "Camera" || prop.name == "camera" || prop.name == "cam" || prop.name == "Cam")) {
                GameObject* foundCam = nullptr;
                for (auto& obj : scene.objects) {
                    if (obj.parentId == m_owner->id && (obj.isCamera || obj.type == PrimitiveType::Camera || obj.name.find("Camera") != std::string::npos || obj.name.find("Cam") != std::string::npos)) {
                        foundCam = &obj;
                        break;
                    }
                }
                if (!foundCam) foundCam = m_owner;
                *reinterpret_cast<GameObject**>(prop.dataPtr) = foundCam;
            } else {
                *reinterpret_cast<void**>(prop.dataPtr) = nullptr;
            }
            prop.isMissing = false;
            continue;
        }

        GameObject* targetObj = scene.FindObject(prop.targetId);
        if (!targetObj) {
            *reinterpret_cast<void**>(prop.dataPtr) = nullptr;
            prop.isMissing = true;
            continue;
        }

        prop.isMissing = false;
        switch (prop.refType) {
            case ObjectRefType::Actor:
                *reinterpret_cast<GameObject**>(prop.dataPtr) = targetObj;
                break;
            case ObjectRefType::Light:
                *reinterpret_cast<LightComponent**>(prop.dataPtr) = targetObj->isLight ? &targetObj->light : nullptr;
                if (!targetObj->isLight) prop.isMissing = true;
                break;
            case ObjectRefType::Mesh:
                *reinterpret_cast<PrimitiveMesh**>(prop.dataPtr) = &targetObj->mesh;
                break;
            case ObjectRefType::Camera:
                if (targetObj && (targetObj->isCamera || targetObj->type == PrimitiveType::Camera)) {
                    *reinterpret_cast<GameObject**>(prop.dataPtr) = targetObj;
                    prop.isMissing = false;
                } else {
                    *reinterpret_cast<void**>(prop.dataPtr) = nullptr;
                    prop.isMissing = true;
                }
                break;
            default:
                *reinterpret_cast<void**>(prop.dataPtr) = targetObj;
                break;
        }
    }
}


static std::string NormalizePropName(const std::string& name) {
    std::string res;
    res.reserve(name.size());
    for (char c : name) {
        if (c != ' ' && c != '_' && c != '-' && c != '\t' && c != '\r' && c != '\n') {
            res += (char)::tolower(static_cast<unsigned char>(c));
        }
    }
    return res;
}

float DynamicScriptBehaviour::GetFloat(const std::string& name, float defaultVal) const {
    if (name.empty()) return defaultVal;
    auto it = m_floatStorage.find(name);
    if (it != m_floatStorage.end()) return it->second;

    std::string norm = NormalizePropName(name);
    for (const auto& pair : m_floatStorage) {
        if (NormalizePropName(pair.first) == norm) return pair.second;
    }
    for (const auto& prop : m_properties) {
        if ((NormalizePropName(prop.name) == norm || NormalizePropName(prop.displayName) == norm) && prop.dataPtr) {
            if (prop.type == BehaviourPropertyType::Float) return *reinterpret_cast<const float*>(prop.dataPtr);
            if (prop.type == BehaviourPropertyType::Double) return (float)*reinterpret_cast<const double*>(prop.dataPtr);
            if (prop.type == BehaviourPropertyType::Int) return (float)*reinterpret_cast<const int*>(prop.dataPtr);
        }
    }
    return defaultVal;
}

bool DynamicScriptBehaviour::GetBool(const std::string& name, bool defaultVal) const {
    if (name.empty()) return defaultVal;
    auto it = m_boolStorage.find(name);
    if (it != m_boolStorage.end()) return it->second;

    std::string norm = NormalizePropName(name);
    for (const auto& pair : m_boolStorage) {
        if (NormalizePropName(pair.first) == norm) return pair.second;
    }
    for (const auto& prop : m_properties) {
        if ((NormalizePropName(prop.name) == norm || NormalizePropName(prop.displayName) == norm) && prop.dataPtr) {
            if (prop.type == BehaviourPropertyType::Bool) return *reinterpret_cast<const bool*>(prop.dataPtr);
        }
    }
    return defaultVal;
}

int DynamicScriptBehaviour::GetInt(const std::string& name, int defaultVal) const {
    if (name.empty()) return defaultVal;
    auto it = m_intStorage.find(name);
    if (it != m_intStorage.end()) return it->second;

    std::string norm = NormalizePropName(name);
    for (const auto& pair : m_intStorage) {
        if (NormalizePropName(pair.first) == norm) return pair.second;
    }
    for (const auto& prop : m_properties) {
        if ((NormalizePropName(prop.name) == norm || NormalizePropName(prop.displayName) == norm) && prop.dataPtr) {
            if (prop.type == BehaviourPropertyType::Int) return *reinterpret_cast<const int*>(prop.dataPtr);
            if (prop.type == BehaviourPropertyType::Int64) return (int)*reinterpret_cast<const int64_t*>(prop.dataPtr);
            if (prop.type == BehaviourPropertyType::Float) return (int)*reinterpret_cast<const float*>(prop.dataPtr);
        }
    }
    return defaultVal;
}

int64_t DynamicScriptBehaviour::GetInt64(const std::string& name, int64_t defaultVal) const {
    if (name.empty()) return defaultVal;
    auto it = m_int64Storage.find(name);
    if (it != m_int64Storage.end()) return it->second;

    std::string norm = NormalizePropName(name);
    for (const auto& pair : m_int64Storage) {
        if (NormalizePropName(pair.first) == norm) return pair.second;
    }
    for (const auto& prop : m_properties) {
        if ((NormalizePropName(prop.name) == norm || NormalizePropName(prop.displayName) == norm) && prop.dataPtr) {
            if (prop.type == BehaviourPropertyType::Int64) return *reinterpret_cast<const int64_t*>(prop.dataPtr);
            if (prop.type == BehaviourPropertyType::Int) return (int64_t)*reinterpret_cast<const int*>(prop.dataPtr);
        }
    }
    return defaultVal;
}

double DynamicScriptBehaviour::GetDouble(const std::string& name, double defaultVal) const {
    if (name.empty()) return defaultVal;
    auto it = m_doubleStorage.find(name);
    if (it != m_doubleStorage.end()) return it->second;

    std::string norm = NormalizePropName(name);
    for (const auto& pair : m_doubleStorage) {
        if (NormalizePropName(pair.first) == norm) return pair.second;
    }
    for (const auto& prop : m_properties) {
        if ((NormalizePropName(prop.name) == norm || NormalizePropName(prop.displayName) == norm) && prop.dataPtr) {
            if (prop.type == BehaviourPropertyType::Double) return *reinterpret_cast<const double*>(prop.dataPtr);
            if (prop.type == BehaviourPropertyType::Float) return (double)*reinterpret_cast<const float*>(prop.dataPtr);
        }
    }
    return defaultVal;
}

std::string DynamicScriptBehaviour::GetString(const std::string& name, const std::string& defaultVal) const {
    if (name.empty()) return defaultVal;
    auto it = m_stringStorage.find(name);
    if (it != m_stringStorage.end()) return it->second;

    std::string norm = NormalizePropName(name);
    for (const auto& pair : m_stringStorage) {
        if (NormalizePropName(pair.first) == norm) return pair.second;
    }
    for (const auto& prop : m_properties) {
        if ((NormalizePropName(prop.name) == norm || NormalizePropName(prop.displayName) == norm) && prop.dataPtr) {
            if (prop.type == BehaviourPropertyType::String) return *reinterpret_cast<const std::string*>(prop.dataPtr);
        }
    }
    return defaultVal;
}

glm::vec2 DynamicScriptBehaviour::GetVec2(const std::string& name, const glm::vec2& defaultVal) const {
    if (name.empty()) return defaultVal;
    auto it = m_vec2Storage.find(name);
    if (it != m_vec2Storage.end()) return it->second;

    std::string norm = NormalizePropName(name);
    for (const auto& pair : m_vec2Storage) {
        if (NormalizePropName(pair.first) == norm) return pair.second;
    }
    for (const auto& prop : m_properties) {
        if ((NormalizePropName(prop.name) == norm || NormalizePropName(prop.displayName) == norm) && prop.dataPtr) {
            if (prop.type == BehaviourPropertyType::Vec2) return *reinterpret_cast<const glm::vec2*>(prop.dataPtr);
        }
    }
    return defaultVal;
}

glm::vec3 DynamicScriptBehaviour::GetVec3(const std::string& name, const glm::vec3& defaultVal) const {
    if (name.empty()) return defaultVal;
    auto it = m_vec3Storage.find(name);
    if (it != m_vec3Storage.end()) return it->second;

    std::string norm = NormalizePropName(name);
    for (const auto& pair : m_vec3Storage) {
        if (NormalizePropName(pair.first) == norm) return pair.second;
    }
    for (const auto& prop : m_properties) {
        if ((NormalizePropName(prop.name) == norm || NormalizePropName(prop.displayName) == norm) && prop.dataPtr) {
            if (prop.type == BehaviourPropertyType::Vec3 || prop.type == BehaviourPropertyType::Color3) {
                return *reinterpret_cast<const glm::vec3*>(prop.dataPtr);
            }
        }
    }
    return defaultVal;
}

glm::vec4 DynamicScriptBehaviour::GetVec4(const std::string& name, const glm::vec4& defaultVal) const {
    if (name.empty()) return defaultVal;
    auto it = m_vec4Storage.find(name);
    if (it != m_vec4Storage.end()) return it->second;

    std::string norm = NormalizePropName(name);
    for (const auto& pair : m_vec4Storage) {
        if (NormalizePropName(pair.first) == norm) return pair.second;
    }
    for (const auto& prop : m_properties) {
        if ((NormalizePropName(prop.name) == norm || NormalizePropName(prop.displayName) == norm) && prop.dataPtr) {
            if (prop.type == BehaviourPropertyType::Vec4 || prop.type == BehaviourPropertyType::Color4) {
                return *reinterpret_cast<const glm::vec4*>(prop.dataPtr);
            }
        }
    }
    return defaultVal;
}

void* DynamicScriptBehaviour::GetRef(const std::string& name) const {
    if (name.empty()) return nullptr;
    auto it = m_refStorage.find(name);
    if (it != m_refStorage.end()) return it->second;

    std::string norm = NormalizePropName(name);
    for (const auto& pair : m_refStorage) {
        if (NormalizePropName(pair.first) == norm) return pair.second;
    }
    for (const auto& prop : m_properties) {
        if ((NormalizePropName(prop.name) == norm || NormalizePropName(prop.displayName) == norm) && prop.dataPtr) {
            if (prop.type == BehaviourPropertyType::ObjectRef) return *reinterpret_cast<void* const*>(prop.dataPtr);
        }
    }
    return nullptr;
}

void DynamicScriptBehaviour::SyncStorageFromProperties() {
    for (auto& prop : m_properties) {
        if (!prop.dataPtr) continue;
        switch (prop.type) {
            case BehaviourPropertyType::Bool: {
                bool val = *reinterpret_cast<const bool*>(prop.dataPtr);
                m_boolStorage[prop.name] = val;
                break;
            }
            case BehaviourPropertyType::Int: {
                int val = *reinterpret_cast<const int*>(prop.dataPtr);
                m_intStorage[prop.name] = val;
                break;
            }
            case BehaviourPropertyType::Int64: {
                int64_t val = *reinterpret_cast<const int64_t*>(prop.dataPtr);
                m_int64Storage[prop.name] = val;
                break;
            }
            case BehaviourPropertyType::Float: {
                float val = *reinterpret_cast<const float*>(prop.dataPtr);
                m_floatStorage[prop.name] = val;
                std::string pLower = prop.name;
                std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::tolower);
                if (pLower.find("speed") != std::string::npos) m_defaultSpeed = val;
                break;
            }
            case BehaviourPropertyType::Double: {
                double val = *reinterpret_cast<const double*>(prop.dataPtr);
                m_doubleStorage[prop.name] = val;
                break;
            }
            case BehaviourPropertyType::String: {
                std::string val = *reinterpret_cast<const std::string*>(prop.dataPtr);
                m_stringStorage[prop.name] = val;
                break;
            }
            case BehaviourPropertyType::Vec2: {
                glm::vec2 val = *reinterpret_cast<const glm::vec2*>(prop.dataPtr);
                m_vec2Storage[prop.name] = val;
                break;
            }
            case BehaviourPropertyType::Vec3:
            case BehaviourPropertyType::Color3: {
                glm::vec3 val = *reinterpret_cast<const glm::vec3*>(prop.dataPtr);
                m_vec3Storage[prop.name] = val;
                break;
            }
            case BehaviourPropertyType::Vec4:
            case BehaviourPropertyType::Color4: {
                glm::vec4 val = *reinterpret_cast<const glm::vec4*>(prop.dataPtr);
                m_vec4Storage[prop.name] = val;
                break;
            }
            case BehaviourPropertyType::ObjectRef: {
                void* val = *reinterpret_cast<void* const*>(prop.dataPtr);
                m_refStorage[prop.name] = val;
                break;
            }
        }
    }
}

void DynamicScriptBehaviour::CopyPropertiesFrom(const EunoiaBehaviour& other) {
    const auto* dynOther = dynamic_cast<const DynamicScriptBehaviour*>(&other);
    if (dynOther) {
        m_floatStorage = dynOther->m_floatStorage;
        m_intStorage = dynOther->m_intStorage;
        m_int64Storage = dynOther->m_int64Storage;
        m_boolStorage = dynOther->m_boolStorage;
        m_stringStorage = dynOther->m_stringStorage;
        m_doubleStorage = dynOther->m_doubleStorage;
        m_vec2Storage = dynOther->m_vec2Storage;
        m_vec3Storage = dynOther->m_vec3Storage;
        m_vec4Storage = dynOther->m_vec4Storage;
        m_refStorage = dynOther->m_refStorage;
        m_defaultPropertyValues = dynOther->m_defaultPropertyValues;
        m_methods = dynOther->m_methods;
    }
    EunoiaBehaviour::CopyPropertiesFrom(other);
    SyncStorageFromProperties();
}

void DynamicScriptBehaviour::ExecuteMethod(const std::string& methodName, std::unordered_set<std::string>& visited) {
    std::string cleanMethod = methodName;
    size_t paren = cleanMethod.find('(');
    if (paren != std::string::npos) cleanMethod = cleanMethod.substr(0, paren);
    size_t nb = cleanMethod.find_first_not_of(" \t\r\n");
    size_t ne = cleanMethod.find_last_not_of(" \t\r\n");
    if (nb == std::string::npos) return;
    cleanMethod = cleanMethod.substr(nb, ne - nb + 1);

    if (visited.find(cleanMethod) != visited.end()) return;
    visited.insert(cleanMethod);

    auto it = m_methods.find(cleanMethod);
    if (it == m_methods.end()) {
        std::string lowerMethod = cleanMethod;
        std::transform(lowerMethod.begin(), lowerMethod.end(), lowerMethod.begin(), ::tolower);
        for (auto& pair : m_methods) {
            std::string k = pair.first;
            std::transform(k.begin(), k.end(), k.begin(), ::tolower);
            if (k == lowerMethod) {
                it = m_methods.find(pair.first);
                break;
            }
        }
    }
    if (it == m_methods.end()) return;

    const auto& m = it->second;

    auto executeAction = [&](const ScriptAction& act) {
        switch (act.type) {
            case ScriptStatementType::Toggle: {
                bool cur = GetBool(act.varName, false);
                bool nextVal = !cur;
                m_boolStorage[act.varName] = nextVal;
                for (auto& prop : m_properties) {
                    if (prop.name == act.varName || prop.displayName == act.varName || NormalizePropName(prop.name) == NormalizePropName(act.varName)) {
                        if (prop.dataPtr && prop.type == BehaviourPropertyType::Bool) {
                            *reinterpret_cast<bool*>(prop.dataPtr) = nextVal;
                        }
                    }
                }
                break;
            }
            case ScriptStatementType::Assignment: {
                std::string vName = act.varName;
                std::string valStr = act.valueStr;
                std::string valLower = valStr;
                std::transform(valLower.begin(), valLower.end(), valLower.begin(), ::tolower);

                if (valLower.rfind("!" + NormalizePropName(vName), 0) == 0 || valLower.rfind("!", 0) == 0) {
                    bool cur = GetBool(vName, false);
                    bool nextVal = !cur;
                    m_boolStorage[vName] = nextVal;
                    for (auto& prop : m_properties) {
                        if (prop.name == vName || prop.displayName == vName || NormalizePropName(prop.name) == NormalizePropName(vName)) {
                            if (prop.dataPtr && prop.type == BehaviourPropertyType::Bool) {
                                *reinterpret_cast<bool*>(prop.dataPtr) = nextVal;
                            }
                        }
                    }
                } else if (valLower == "true" || valLower == "false" || valLower == "1" || valLower == "0") {
                    bool bVal = (valLower == "true" || valLower == "1");
                    m_boolStorage[vName] = bVal;
                    for (auto& prop : m_properties) {
                        if (prop.name == vName || prop.displayName == vName || NormalizePropName(prop.name) == NormalizePropName(vName)) {
                            if (prop.dataPtr && prop.type == BehaviourPropertyType::Bool) {
                                *reinterpret_cast<bool*>(prop.dataPtr) = bVal;
                            }
                        }
                    }
                } else {
                    try {
                        std::string clean = valStr;
                        if (!clean.empty() && (clean.back() == 'f' || clean.back() == 'F' || clean.back() == 'd' || clean.back() == 'D')) {
                            clean.pop_back();
                        }
                        float fVal = std::stof(clean);
                        m_floatStorage[vName] = fVal;
                        for (auto& prop : m_properties) {
                            if (prop.name == vName || prop.displayName == vName || NormalizePropName(prop.name) == NormalizePropName(vName)) {
                                if (prop.dataPtr && prop.type == BehaviourPropertyType::Float) {
                                    *reinterpret_cast<float*>(prop.dataPtr) = fVal;
                                }
                            }
                        }
                    } catch (...) {
                        std::string sVal = valStr;
                        if (sVal.size() >= 2 && sVal.front() == '"' && sVal.back() == '"') {
                            sVal = sVal.substr(1, sVal.size() - 2);
                        }
                        m_stringStorage[vName] = sVal;
                        for (auto& prop : m_properties) {
                            if (prop.name == vName || prop.displayName == vName || NormalizePropName(prop.name) == NormalizePropName(vName)) {
                                if (prop.dataPtr && prop.type == BehaviourPropertyType::String) {
                                    *reinterpret_cast<std::string*>(prop.dataPtr) = sVal;
                                }
                            }
                        }
                    }
                }
                break;
            }
            case ScriptStatementType::Print: {
                Print(act.target);
                AddEngineLog("LogBehaviour", "[" + m_className + "] " + act.target, 0);
                break;
            }
            case ScriptStatementType::UnlockCursor: {
                CursorState.UnLocked();
                break;
            }
            case ScriptStatementType::LockCursor: {
                CursorState.Locked();
                break;
            }
            case ScriptStatementType::RemoveAllUI: {
                EunoiaUI::UISystemManager::Get().RemoveAll();
                break;
            }
            case ScriptStatementType::AddUI: {
                UserInterface* uiPtr = nullptr;
                for (auto& p : m_properties) {
                    if (p.name == act.target && p.refType == ObjectRefType::UserInterface) {
                        if (!p.assetPath.empty()) {
                            uiPtr = EunoiaUI::UISystemManager::Get().GetOrLoadUI(p.assetPath);
                            if (p.dataPtr) *reinterpret_cast<UserInterface**>(p.dataPtr) = uiPtr;
                        } else if (p.dataPtr) {
                            uiPtr = *reinterpret_cast<UserInterface**>(p.dataPtr);
                        }
                        break;
                    }
                }
                if (uiPtr) {
                    EunoiaUI::UISystemManager::Get().Add(uiPtr);
                    AddEngineLog("LogUI", "Added UserInterface to viewport: " + uiPtr->name, 2);
                }
                break;
            }
            case ScriptStatementType::RemoveUI: {
                UserInterface* uiPtr = nullptr;
                for (auto& p : m_properties) {
                    if (p.name == act.target && p.refType == ObjectRefType::UserInterface && p.dataPtr) {
                        uiPtr = *reinterpret_cast<UserInterface**>(p.dataPtr);
                        break;
                    }
                }
                if (uiPtr) {
                    EunoiaUI::UISystemManager::Get().Remove(uiPtr);
                }
                break;
            }
            case ScriptStatementType::CallMethod: {
                ExecuteMethod(act.target, visited);
                break;
            }
            case ScriptStatementType::StartTimer: {
                float dur = act.duration;
                if (dur <= 0.0f && !act.valueStr.empty()) {
                    dur = GetFloat(act.valueStr, 0.0f);
                }
                StartTimer(act.target, dur);
                break;
            }
            case ScriptStatementType::StartInterval: {
                float dur = act.duration;
                if (dur <= 0.0f && !act.valueStr.empty()) {
                    dur = GetFloat(act.valueStr, 0.0f);
                }
                StartInterval(act.target, dur);
                break;
            }
            case ScriptStatementType::StopTimer: {
                for (auto& t : m_timers) {
                    if (t.functionName == act.target) {
                        t.markedForRemoval = true;
                    }
                }
                break;
            }
            case ScriptStatementType::ClearAllTimers: {
                ClearAllTimers();
                break;
            }
        }
    };

    if (m.unlockCursor) CursorState.UnLocked();
    if (m.lockCursor) CursorState.Locked();
    if (m.removeAllUI) EunoiaUI::UISystemManager::Get().RemoveAll();

    for (const auto& msg : m.printMessages) {
        Print(msg);
        AddEngineLog("LogBehaviour", "[" + m_className + "] " + msg, 0);
    }

    for (const auto& uiVar : m.uiAddTargets) {
        UserInterface* uiPtr = nullptr;
        for (auto& p : m_properties) {
            if (p.name == uiVar && p.refType == ObjectRefType::UserInterface) {
                if (!p.assetPath.empty()) {
                    uiPtr = EunoiaUI::UISystemManager::Get().GetOrLoadUI(p.assetPath);
                    if (p.dataPtr) *reinterpret_cast<UserInterface**>(p.dataPtr) = uiPtr;
                } else if (p.dataPtr) {
                    uiPtr = *reinterpret_cast<UserInterface**>(p.dataPtr);
                }
                break;
            }
        }
        if (uiPtr) {
            EunoiaUI::UISystemManager::Get().Add(uiPtr);
            AddEngineLog("LogUI", "Added UserInterface to viewport: " + uiPtr->name, 2);
        }
    }

    for (const auto& uiVar : m.uiRemoveTargets) {
        UserInterface* uiPtr = nullptr;
        for (auto& p : m_properties) {
            if (p.name == uiVar && p.refType == ObjectRefType::UserInterface && p.dataPtr) {
                uiPtr = *reinterpret_cast<UserInterface**>(p.dataPtr);
                break;
            }
        }
        if (uiPtr) {
            EunoiaUI::UISystemManager::Get().Remove(uiPtr);
        }
    }

    for (const auto& branch : m.branches) {
        if (branch.hasCondition) {
            bool condVal = GetBool(branch.conditionVar, false);
            if (condVal == branch.expectedBool) {
                for (const auto& act : branch.thenActions) {
                    executeAction(act);
                }
            } else {
                for (const auto& act : branch.elseActions) {
                    executeAction(act);
                }
            }
        } else {
            for (const auto& act : branch.thenActions) {
                executeAction(act);
            }
        }
    }

    for (const auto& childMethod : m.calledMethods) {
        ExecuteMethod(childMethod, visited);
    }
}

void DynamicScriptBehaviour::OnCreate() {
    std::unordered_set<std::string> visited;
    ExecuteMethod("OnCreate", visited);
}

void DynamicScriptBehaviour::OnEnable() {
    std::unordered_set<std::string> visited;
    ExecuteMethod("OnEnable", visited);
}

void DynamicScriptBehaviour::InvokeFunction(const std::string& functionName) {
    std::unordered_set<std::string> visited;
    ExecuteMethod(functionName, visited);
}

void DynamicScriptBehaviour::Start() {
    std::unordered_set<std::string> visited;
    ExecuteMethod("Start", visited);

    for (const auto& msg : m_startPrintMessages) {
        Print(msg);
        AddEngineLog("LogBehaviour", "[" + m_className + "] " + msg, 0);
    }
    if (m_startUnlockCursor) {
        CursorState.UnLocked();
    } else if (m_startLockCursor) {
        CursorState.Locked();
    }
    if (m_startUIRemoveAll) {
        EunoiaUI::UISystemManager::Get().RemoveAll();
    }
    for (const auto& uiVar : m_startUIAddTargets) {
        UserInterface* uiPtr = nullptr;
        for (auto& p : m_properties) {
            if (p.name == uiVar && p.refType == ObjectRefType::UserInterface) {
                if (!p.assetPath.empty()) {
                    uiPtr = EunoiaUI::UISystemManager::Get().GetOrLoadUI(p.assetPath);
                    if (p.dataPtr) *reinterpret_cast<UserInterface**>(p.dataPtr) = uiPtr;
                } else if (p.dataPtr) {
                    uiPtr = *reinterpret_cast<UserInterface**>(p.dataPtr);
                }
                break;
            }
        }
        if (uiPtr) {
            EunoiaUI::UISystemManager::Get().Add(uiPtr);
            AddEngineLog("LogUI", "Added UserInterface to viewport: " + uiPtr->name, 2);
        }
    }
    for (const auto& uiVar : m_startUIRemoveTargets) {
        UserInterface* uiPtr = nullptr;
        for (auto& p : m_properties) {
            if (p.name == uiVar && p.refType == ObjectRefType::UserInterface && p.dataPtr) {
                uiPtr = *reinterpret_cast<UserInterface**>(p.dataPtr);
                break;
            }
        }
        if (uiPtr) {
            EunoiaUI::UISystemManager::Get().Remove(uiPtr);
        }
    }

    // Execute dynamic save actions in Start()
    for (const auto& act : m_startSaveActions) {
        auto& store = (act.scope == DynamicSaveAction::Scope::Global) ? SaveSystem.Global : SaveSystem.Local;
        if (act.type == DynamicSaveAction::ActionType::SetItem) {
            if (act.isConstString) {
                store.SetItem(act.itemName, act.valueStr);
            } else if (!act.varName.empty()) {
                if (m_floatStorage.find(act.varName) != m_floatStorage.end()) store.SetItem(act.itemName, GetFloat(act.varName));
                else if (m_intStorage.find(act.varName) != m_intStorage.end()) store.SetItem(act.itemName, GetInt(act.varName));
                else if (m_boolStorage.find(act.varName) != m_boolStorage.end()) store.SetItem(act.itemName, GetBool(act.varName));
                else if (m_stringStorage.find(act.varName) != m_stringStorage.end()) store.SetItem(act.itemName, GetString(act.varName));
                else if (m_doubleStorage.find(act.varName) != m_doubleStorage.end()) store.SetItem(act.itemName, GetDouble(act.varName));
                else if (m_int64Storage.find(act.varName) != m_int64Storage.end()) store.SetItem(act.itemName, GetInt64(act.varName));
                else if (m_vec2Storage.find(act.varName) != m_vec2Storage.end()) store.SetItem(act.itemName, GetVec2(act.varName));
                else if (m_vec3Storage.find(act.varName) != m_vec3Storage.end()) store.SetItem(act.itemName, GetVec3(act.varName));
                else if (m_vec4Storage.find(act.varName) != m_vec4Storage.end()) store.SetItem(act.itemName, GetVec4(act.varName));
                else store.SetItem(act.itemName, act.valueStr);
            } else {
                std::string v = act.valueStr;
                std::string vLow = v;
                std::transform(vLow.begin(), vLow.end(), vLow.begin(), ::tolower);
                if (vLow == "true") store.SetItem(act.itemName, true);
                else if (vLow == "false") store.SetItem(act.itemName, false);
                else {
                    try {
                        if (v.find('.') != std::string::npos) {
                            if (v.back() == 'f' || v.back() == 'F') v.pop_back();
                            store.SetItem(act.itemName, std::stof(v));
                        } else {
                            store.SetItem(act.itemName, std::stoi(v));
                        }
                    } catch (...) {
                        store.SetItem(act.itemName, act.valueStr);
                    }
                }
            }
        } else if (act.type == DynamicSaveAction::ActionType::RemoveItem) {
            store.RemoveItem(act.itemName);
        } else if (act.type == DynamicSaveAction::ActionType::RemoveAll) {
            store.RemoveAll();
        }
    }
}

void DynamicScriptBehaviour::Update(float deltaTime) {
    if (!m_owner || !IsEnabled()) return;

    // Check if script has an 'enabled' or 'active' boolean flag in storage
    bool isScriptEnabled = true;
    for (const auto& pair : m_boolStorage) {
        std::string k = pair.first;
        std::transform(k.begin(), k.end(), k.begin(), ::tolower);
        if (k == "enabled" || k == "isenabled" || k == "is_enabled" || k == "scriptenabled" || k == "script_enabled" || k == "bisenabled") {
            isScriptEnabled = pair.second;
            break;
        }
    }
    if (!isScriptEnabled) return;

    SyncStorageFromProperties();

    float dt = (::WorldDeltaSeconds > 0.0f) ? ::WorldDeltaSeconds : deltaTime;
    UpdateTimers(dt);

    // Execute dynamic RayTrace actions
    if (m_scene && m_owner) {
        RayTrace.behaviour = this;
        RayTraceSystem::Get().SetActiveScene(m_scene);
        for (const auto& trace : m_lineTraceActions) {
            glm::vec3 startPos = m_owner->GetWorldLocation();
            glm::vec3 forwardDir = m_owner->GetForwardVector();

            std::string sLow = trace.startVar;
            std::transform(sLow.begin(), sLow.end(), sLow.begin(), ::tolower);
            if (sLow.find("camera") != std::string::npos || sLow.find("cam") != std::string::npos) {
                GameObject* camObj = nullptr;
                void* refPtr = GetRef("Camera");
                if (refPtr) camObj = reinterpret_cast<GameObject*>(refPtr);
                else {
                    for (auto& obj : m_scene->objects) {
                        if (obj.parentId == m_owner->id && (obj.isCamera || obj.type == PrimitiveType::Camera || obj.name.find("Camera") != std::string::npos || obj.name.find("Cam") != std::string::npos)) {
                            camObj = &obj;
                            break;
                        }
                    }
                }
                if (!camObj) camObj = m_owner;
                if (camObj) {
                    startPos = camObj->GetWorldLocation();
                    forwardDir = camObj->GetForwardVector();
                }
            } else if (sLow.find("transform.location") != std::string::npos || sLow.find("transform.position") != std::string::npos || sLow.find("getworldlocation") != std::string::npos) {
                startPos = m_owner->GetWorldLocation();
            }

            glm::vec3 endPos = startPos + forwardDir * trace.traceDistance;

            std::string eLow = trace.endVar;
            std::transform(eLow.begin(), eLow.end(), eLow.begin(), ::tolower);
            if (eLow.find("camera") != std::string::npos || eLow.find("cam") != std::string::npos) {
                GameObject* camObj = nullptr;
                void* refPtr = GetRef("Camera");
                if (refPtr) camObj = reinterpret_cast<GameObject*>(refPtr);
                else {
                    for (auto& obj : m_scene->objects) {
                        if (obj.parentId == m_owner->id && (obj.isCamera || obj.type == PrimitiveType::Camera || obj.name.find("Camera") != std::string::npos || obj.name.find("Cam") != std::string::npos)) {
                            camObj = &obj;
                            break;
                        }
                    }
                }
                if (!camObj) camObj = m_owner;
                if (camObj) {
                    forwardDir = camObj->GetForwardVector();
                    endPos = startPos + forwardDir * trace.traceDistance;
                }
            } else if (eLow.find("forward") != std::string::npos) {
                endPos = startPos + forwardDir * trace.traceDistance;
            } else if (eLow.find("down") != std::string::npos) {
                endPos = startPos + glm::vec3(0.0f, -trace.traceDistance, 0.0f);
            } else if (eLow.find("up") != std::string::npos) {
                endPos = startPos + glm::vec3(0.0f, trace.traceDistance, 0.0f);
            }

            m_lastDynamicHit = RayTrace.LineTrace(startPos, endPos, m_owner);
        }
    }

    // Execute real-time dynamic Print statements inside Update()
    for (const auto& item : m_updatePrintMessages) {
        // 1. Check boolean condition variables
        bool boolConditionMet = true;
        for (const auto& bCond : item.requiredBools) {
            bool curVal = GetBool(bCond.varName, bCond.expectedVal);
            if (curVal != bCond.expectedVal) {
                boolConditionMet = false;
                break;
            }
        }
        if (!boolConditionMet) continue;

        // 2. Check hit conditions
        if (item.requireHitStatus) {
            if (!m_lastDynamicHit.Status) continue;
            if (!item.requireHitTag.empty()) {
                if (!m_lastDynamicHit.GameObject || !m_lastDynamicHit.GameObject.HasTag(item.requireHitTag)) {
                    continue;
                }
            }
        }
        if (item.requireNoHit && m_lastDynamicHit.Status) continue;

        // 3. Check key conditions if any
        if (!item.requiredKeys.empty()) {
            bool keyMet = false;
            for (const auto& cond : item.requiredKeys) {
                if (cond.key >= 0) {
                    bool down = cond.isPressed ? InputSystem::Get().IsKeyPressed(cond.key) : InputSystem::Get().IsKeyDown(cond.key);
                    if (cond.isNegated) down = !down;
                    if (down) { keyMet = true; break; }
                }
            }
            if (!keyMet) continue;
        }

        std::string msg = item.textPrefix;
        if (item.appendDeltaTime) {
            msg += std::to_string(dt);
        }
        if (item.appendHitName) {
            if (m_lastDynamicHit.Status && m_lastDynamicHit.GameObject) {
                msg += m_lastDynamicHit.GameObject.Name.str();
            } else {
                msg += "None";
            }
        }
        if (item.appendHitId) {
            msg += m_lastDynamicHit.Status ? std::to_string(m_lastDynamicHit.ObjectId) : "-1";
        }
        if (item.appendHitPoint) {
            msg += m_lastDynamicHit.Status ? ("(" + std::to_string(m_lastDynamicHit.Point.x) + ", " +
                                                    std::to_string(m_lastDynamicHit.Point.y) + ", " +
                                                    std::to_string(m_lastDynamicHit.Point.z) + ")") : "(0, 0, 0)";
        }
        if (item.appendHitStatus) {
            msg += (m_lastDynamicHit.Status ? "true" : "false");
        }
        if (item.appendHitDistance) {
            msg += std::to_string(m_lastDynamicHit.Distance);
        }
        if (item.appendHitNormal) {
            msg += ("(" + std::to_string(m_lastDynamicHit.Normal.x) + ", " +
                          std::to_string(m_lastDynamicHit.Normal.y) + ", " +
                          std::to_string(m_lastDynamicHit.Normal.z) + ")");
        }
        if (item.appendHitTags) {
            if (m_lastDynamicHit.Status && m_lastDynamicHit.GameObject) {
                const auto& tags = m_lastDynamicHit.GameObject.GetTags();
                msg += "[";
                for (size_t ti = 0; ti < tags.size(); ++ti) {
                    msg += "\"" + tags[ti] + "\"";
                    if (ti + 1 < tags.size()) msg += ", ";
                }
                msg += "]";
            } else {
                msg += "[]";
            }
        }
        if (!item.varName.empty()) {
            if (m_floatStorage.find(item.varName) != m_floatStorage.end()) msg += std::to_string(GetFloat(item.varName));
            else if (m_intStorage.find(item.varName) != m_intStorage.end()) msg += std::to_string(GetInt(item.varName));
            else if (m_boolStorage.find(item.varName) != m_boolStorage.end()) msg += (GetBool(item.varName) ? "true" : "false");
            else if (m_stringStorage.find(item.varName) != m_stringStorage.end()) msg += GetString(item.varName);
            else {
                float f = GetFloat(item.varName, -999999.0f);
                if (f != -999999.0f) msg += std::to_string(f);
                else msg += (GetBool(item.varName, false) ? "true" : "false");
            }
        }
        if (!item.saveItemName.empty()) {
            auto& store = item.saveItemIsGlobal ? SaveSystem.Global : SaveSystem.Local;
            if (store.HasItem(item.saveItemName)) {
                msg += store.GetItem(item.saveItemName).ToString();
            } else {
                msg += "null";
            }
        }
        if (item.appendPosition && m_owner) {
            msg += " Pos: (" + std::to_string(m_owner->position.x) + ", " +
                               std::to_string(m_owner->position.y) + ", " +
                               std::to_string(m_owner->position.z) + ")";
        }
        if (item.appendRotation && m_owner) {
            msg += " Rot: (" + std::to_string(m_owner->rotation.x) + ", " +
                               std::to_string(m_owner->rotation.y) + ", " +
                               std::to_string(m_owner->rotation.z) + ")";
        }
        if (!item.textSuffix.empty()) {
            msg += item.textSuffix;
        }

        if (!item.requiredKeys.empty()) {
            Print(msg, 2.5f);
        } else {
            PrintWithKey(item.printKey, msg, 2.5f);
        }
    }

    // Execute real-time dynamic Destroy actions inside Update()
    for (const auto& action : m_destroyActions) {
        if (!action.enabled) continue;
        bool boolConditionMet = true;
        for (const auto& bCond : action.requiredBools) {
            bool curVal = GetBool(bCond.varName, bCond.expectedVal);
            if (curVal != bCond.expectedVal) {
                boolConditionMet = false;
                break;
            }
        }
        if (!boolConditionMet) continue;

        if (action.requireHitStatus) {
            if (!m_lastDynamicHit.Status) continue;
            if (!action.requireHitTag.empty()) {
                if (!m_lastDynamicHit.GameObject || !m_lastDynamicHit.GameObject.HasTag(action.requireHitTag)) {
                    continue;
                }
            }
        }
        if (action.requireNoHit && m_lastDynamicHit.Status) continue;

        if (!action.requiredKeys.empty()) {
            bool keyMet = false;
            for (const auto& cond : action.requiredKeys) {
                if (cond.key >= 0) {
                    bool down = cond.isPressed ? InputSystem::Get().IsKeyPressed(cond.key) : InputSystem::Get().IsKeyDown(cond.key);
                    if (cond.isNegated) down = !down;
                    if (down) { keyMet = true; break; }
                }
            }
            if (!keyMet) continue;
        }

        if (action.targetIsHitObject) {
            if (m_lastDynamicHit.Status && m_lastDynamicHit.GameObject && m_lastDynamicHit.GameObject.ptr) {
                m_lastDynamicHit.GameObject.ptr->Destroy();
            }
        } else if (!action.targetVar.empty()) {
            int objId = GetInt(action.targetVar, -1);
            if (objId >= 0 && m_scene) {
                m_scene->RemoveObject(objId);
            } else if (m_owner && (action.targetVar == "this" || action.targetVar == "gameobject" || action.targetVar == "owner")) {
                m_owner->Destroy();
            }
        } else if (m_owner) {
            m_owner->Destroy();
        }
    }

    // Execute real-time dynamic Transform actions inside Update() (dev.md: Transform.Translate, Transform.Rotate)
    Transform.Init(this);
    auto& input = InputSystem::Get();
    for (const auto& action : m_transformActions) {
        // 1. Check boolean condition variables (e.g. CanMove == true / canMove == false)
        bool boolConditionMet = true;
        for (const auto& bCond : action.requiredBools) {
            bool curVal = GetBool(bCond.varName, bCond.expectedVal);
            if (curVal != bCond.expectedVal) {
                boolConditionMet = false;
                break;
            }
        }
        if (!boolConditionMet) continue;

        if (!action.requiredKeys.empty()) {
            bool conditionMet = false;
            for (const auto& cond : action.requiredKeys) {
                if (cond.key == -2) {
                    if (input.GetMouseX() != 0.0) { conditionMet = true; break; }
                } else if (cond.key == -3) {
                    if (input.GetMouseY() != 0.0) { conditionMet = true; break; }
                } else if (cond.key != -1) {
                    bool keyState = cond.isPressed ? input.IsKeyPressed(cond.key) : input.IsKeyDown(cond.key);
                    if (cond.isNegated) keyState = !keyState;
                    if (keyState) {
                        conditionMet = true;
                        break;
                    }
                }
            }
            if (!conditionMet) continue;
        }

        glm::vec3 delta(0.0f);
        for (int i = 0; i < 3; ++i) {
            float val = action.constantDelta[i];
            if (!action.varNames[i].empty()) {
                if (action.varNames[i] == "$__MouseDeltaX") {
                    val = (float)input.GetMouseDeltaX() * action.varMultiplier[i];
                } else if (action.varNames[i] == "$__MouseDeltaY") {
                    val = (float)input.GetMouseDeltaY() * action.varMultiplier[i];
                } else if (action.varNames[i] == "$__MouseX") {
                    val = (float)input.GetMouseX() * action.varMultiplier[i];
                } else if (action.varNames[i] == "$__MouseY") {
                    val = (float)input.GetMouseY() * action.varMultiplier[i];
                } else {
                    val = GetFloat(action.varNames[i], action.constantDelta[i]) * action.varMultiplier[i];
                }
            }
            if (!action.scaleVarNames[i].empty()) {
                val *= GetFloat(action.scaleVarNames[i], 1.0f);
            }
            if (action.useDeltaTime[i]) {
                val *= dt;
            }
            delta[i] = val;
        }

        GameObject* targetObj = m_owner;
        if (!action.targetRefName.empty()) {
            void* refPtr = GetRef(action.targetRefName);
            if (!refPtr && (action.targetRefName == "target" || action.targetRefName == "PlayerActor" || action.targetRefName == "player")) {
                refPtr = GetRef("Player");
            }
            if (refPtr) {
                targetObj = reinterpret_cast<GameObject*>(refPtr);
            } else if (m_scene && m_owner) {
                std::string lowerRef = action.targetRefName;
                std::transform(lowerRef.begin(), lowerRef.end(), lowerRef.begin(), ::tolower);
                if (lowerRef.find("camera") != std::string::npos || lowerRef.find("cam") != std::string::npos || lowerRef.find("boom") != std::string::npos) {
                    for (auto& obj : m_scene->objects) {
                        if (obj.parentId == m_owner->id) {
                            targetObj = &obj;
                            break;
                        }
                    }
                }
                if (!targetObj) targetObj = m_owner;
            } else {
                targetObj = m_owner;
            }
        }
        if (!targetObj) continue;

        if (action.type == DynamicTransformAction::ActionType::TranslateRelative) {
            if (targetObj == m_owner) {
                Transform.Translate(delta.x, delta.y, delta.z);
            } else {
                targetObj->transform.RelativeTranslate(delta.x, delta.y, delta.z);
            }
        } else if (action.type == DynamicTransformAction::ActionType::TranslateWorld) {
            if (targetObj == m_owner) {
                Transform.WorldTranslate(delta.x, delta.y, delta.z);
            } else {
                targetObj->transform.WorldTranslate(delta.x, delta.y, delta.z);
            }
        } else if (action.type == DynamicTransformAction::ActionType::RotateRelative ||
                   action.type == DynamicTransformAction::ActionType::RotateWorld) {
            glm::vec3 cur = targetObj->rotation;
            bool isCamera = (targetObj->isCamera || targetObj->type == PrimitiveType::Camera);
            for (int i = 0; i < 3; ++i) {
                if (!action.activeAxis[i]) continue;
                bool isDelta = (action.varNames[i] == "$__MouseDeltaX" || action.varNames[i] == "$__MouseDeltaY" || action.useDeltaTime[i]);
                if (isDelta) {
                    if (i == 0) cur.x += delta.x;
                    else if (i == 1) cur.y += delta.y;
                    else if (i == 2) cur.z += delta.z;
                } else {
                    if (i == 0) cur.x = delta.x;
                    else if (i == 1) cur.y = delta.y;
                    else if (i == 2) cur.z = delta.z;
                }
            }
            if (isCamera) {
                cur.x = std::clamp(cur.x, -89.0f, 89.0f);
            }
            while (cur.y >= 360.0f) cur.y -= 360.0f;
            while (cur.y < 0.0f)    cur.y += 360.0f;
            targetObj->rotation = cur;
        } else if (action.type == DynamicTransformAction::ActionType::LockCursor) {
            CursorState.Locked();
        } else if (action.type == DynamicTransformAction::ActionType::UnlockCursor) {
            CursorState.UnLocked();
        }
    }

    // Execute real-time dynamic UI actions inside Update()
    for (const auto& action : m_uiActions) {
        // 1. Check boolean condition variables
        bool boolConditionMet = true;
        for (const auto& bCond : action.requiredBools) {
            bool curVal = GetBool(bCond.varName, bCond.expectedVal);
            if (curVal != bCond.expectedVal) {
                boolConditionMet = false;
                break;
            }
        }
        if (!boolConditionMet) continue;

        // 2. Check hit conditions
        if (action.requireHitStatus) {
            if (!m_lastDynamicHit.Status) continue;
            if (!action.requireHitTag.empty()) {
                if (!m_lastDynamicHit.GameObject || !m_lastDynamicHit.GameObject.HasTag(action.requireHitTag)) {
                    continue;
                }
            }
        }
        if (action.requireNoHit) {
            if (m_lastDynamicHit.Status && !action.requireHitTag.empty() && m_lastDynamicHit.GameObject && m_lastDynamicHit.GameObject.HasTag(action.requireHitTag)) {
                continue;
            }
            if (m_lastDynamicHit.Status && action.requireHitTag.empty()) {
                continue;
            }
        }

        // 3. Check key conditions if any
        if (!action.requiredKeys.empty()) {
            bool keyMet = false;
            for (const auto& cond : action.requiredKeys) {
                if (cond.key >= 0) {
                    bool down = cond.isPressed ? input.IsKeyPressed(cond.key) : input.IsKeyDown(cond.key);
                    if (cond.isNegated) down = !down;
                    if (down) { keyMet = true; break; }
                }
            }
            if (!keyMet) continue;
        }

        // 4. Perform UI action
        if (action.type == DynamicScriptBehaviour::DynamicUIAction::ActionType::RemoveAll) {
            EunoiaUI::UISystemManager::Get().RemoveAll();
        } else {
            UserInterface* uiPtr = nullptr;
            for (auto& p : m_properties) {
                if (p.name == action.targetVar && p.refType == ObjectRefType::UserInterface) {
                    if (p.dataPtr) uiPtr = *reinterpret_cast<UserInterface**>(p.dataPtr);
                    if (!uiPtr && !p.assetPath.empty()) {
                        uiPtr = EunoiaUI::UISystemManager::Get().GetOrLoadUI(p.assetPath);
                        if (p.dataPtr) *reinterpret_cast<UserInterface**>(p.dataPtr) = uiPtr;
                    }
                    break;
                }
            }
            if (!uiPtr) {
                uiPtr = EunoiaUI::UISystemManager::Get().GetOrLoadUI(action.targetVar);
            }
            if (!uiPtr) {
                for (const auto& pair : EunoiaUI::UISystemManager::Get().GetLoadedUIs()) {
                    if (pair.second && (pair.second->name == action.targetVar || pair.first.find(action.targetVar) != std::string::npos)) {
                        uiPtr = pair.second.get();
                        break;
                    }
                }
            }

            if (uiPtr) {
                if (action.type == DynamicScriptBehaviour::DynamicUIAction::ActionType::Add) {
                    EunoiaUI::UISystemManager::Get().Add(uiPtr);
                    for (auto& pair : m_boolStorage) {
                        std::string kLow = pair.first;
                        std::transform(kLow.begin(), kLow.end(), kLow.begin(), ::tolower);
                        std::string targetLow = action.targetVar;
                        std::transform(targetLow.begin(), targetLow.end(), targetLow.begin(), ::tolower);
                        if (kLow.find(targetLow) != std::string::npos && kLow.find("added") != std::string::npos) {
                            pair.second = true;
                        }
                    }
                } else if (action.type == DynamicScriptBehaviour::DynamicUIAction::ActionType::Remove) {
                    EunoiaUI::UISystemManager::Get().Remove(uiPtr);
                    for (auto& pair : m_boolStorage) {
                        std::string kLow = pair.first;
                        std::transform(kLow.begin(), kLow.end(), kLow.begin(), ::tolower);
                        std::string targetLow = action.targetVar;
                        std::transform(targetLow.begin(), targetLow.end(), targetLow.begin(), ::tolower);
                        if (kLow.find(targetLow) != std::string::npos && kLow.find("added") != std::string::npos) {
                            pair.second = false;
                        }
                    }
                }
            }
        }
    }

    // Execute real-time dynamic SaveSystem actions inside Update()
    for (const auto& action : m_updateSaveActions) {
        bool boolConditionMet = true;
        for (const auto& bCond : action.requiredBools) {
            bool curVal = GetBool(bCond.varName, bCond.expectedVal);
            if (curVal != bCond.expectedVal) {
                boolConditionMet = false;
                break;
            }
        }
        if (!boolConditionMet) continue;

        if (action.requireHitStatus) {
            if (!m_lastDynamicHit.Status) continue;
            if (!action.requireHitTag.empty()) {
                if (!m_lastDynamicHit.GameObject || !m_lastDynamicHit.GameObject.HasTag(action.requireHitTag)) {
                    continue;
                }
            }
        }
        if (action.requireNoHit && m_lastDynamicHit.Status) continue;

        if (!action.requiredKeys.empty()) {
            bool keyMet = false;
            for (const auto& cond : action.requiredKeys) {
                if (cond.key >= 0) {
                    bool down = cond.isPressed ? input.IsKeyPressed(cond.key) : input.IsKeyDown(cond.key);
                    if (cond.isNegated) down = !down;
                    if (down) { keyMet = true; break; }
                }
            }
            if (!keyMet) continue;
        }

        auto& store = (action.scope == DynamicSaveAction::Scope::Global) ? SaveSystem.Global : SaveSystem.Local;
        if (action.type == DynamicSaveAction::ActionType::SetItem) {
            if (action.isConstString) {
                store.SetItem(action.itemName, action.valueStr);
            } else if (!action.varName.empty()) {
                if (m_floatStorage.find(action.varName) != m_floatStorage.end()) store.SetItem(action.itemName, GetFloat(action.varName));
                else if (m_intStorage.find(action.varName) != m_intStorage.end()) store.SetItem(action.itemName, GetInt(action.varName));
                else if (m_boolStorage.find(action.varName) != m_boolStorage.end()) store.SetItem(action.itemName, GetBool(action.varName));
                else if (m_stringStorage.find(action.varName) != m_stringStorage.end()) store.SetItem(action.itemName, GetString(action.varName));
                else if (m_doubleStorage.find(action.varName) != m_doubleStorage.end()) store.SetItem(action.itemName, GetDouble(action.varName));
                else if (m_int64Storage.find(action.varName) != m_int64Storage.end()) store.SetItem(action.itemName, GetInt64(action.varName));
                else if (m_vec2Storage.find(action.varName) != m_vec2Storage.end()) store.SetItem(action.itemName, GetVec2(action.varName));
                else if (m_vec3Storage.find(action.varName) != m_vec3Storage.end()) store.SetItem(action.itemName, GetVec3(action.varName));
                else if (m_vec4Storage.find(action.varName) != m_vec4Storage.end()) store.SetItem(action.itemName, GetVec4(action.varName));
                else store.SetItem(action.itemName, action.valueStr);
            } else {
                std::string v = action.valueStr;
                std::string vLow = v;
                std::transform(vLow.begin(), vLow.end(), vLow.begin(), ::tolower);
                if (vLow == "true") store.SetItem(action.itemName, true);
                else if (vLow == "false") store.SetItem(action.itemName, false);
                else {
                    try {
                        if (v.find('.') != std::string::npos) {
                            if (v.back() == 'f' || v.back() == 'F') v.pop_back();
                            store.SetItem(action.itemName, std::stof(v));
                        } else {
                            store.SetItem(action.itemName, std::stoi(v));
                        }
                    } catch (...) {
                        store.SetItem(action.itemName, action.valueStr);
                    }
                }
            }
        } else if (action.type == DynamicSaveAction::ActionType::RemoveItem) {
            store.RemoveItem(action.itemName);
        } else if (action.type == DynamicSaveAction::ActionType::RemoveAll) {
            store.RemoveAll();
        }
    }
}

void DynamicScriptBehaviour::RegisterProperties() {
    if (!m_sourceCppPath.empty()) {
        ParsePropertiesFromCpp(m_sourceCppPath, true);
    } else {
        m_properties.clear();
    }
}

void DynamicScriptBehaviour::RefreshPropertiesFromSource() {
    if (m_sourceCppPath.empty()) return;
    std::error_code ec;
    if (!std::filesystem::exists(m_sourceCppPath, ec)) return;
    auto lwt = std::filesystem::last_write_time(m_sourceCppPath, ec);
    if (!ec && (lwt > m_sourceTimestamp || m_properties.empty())) {
        ParsePropertiesFromCpp(m_sourceCppPath, true);
    }
}

void DynamicScriptBehaviour::ParsePropertiesFromCpp(const std::string& cppPath, bool preserveExistingValues) {
    m_sourceCppPath = cppPath;
    std::error_code ec;
    if (!std::filesystem::exists(cppPath, ec)) return;
    m_sourceTimestamp = std::filesystem::last_write_time(cppPath, ec);

    std::ifstream file(cppPath);
    if (!file.is_open()) return;

    std::string line;
    std::vector<std::string> lines;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    file.close();

    // 1. First pass: find declared public member variables and their types & defaults
    struct VarDecl {
        std::string type;
        std::string defaultVal;
    };
    std::unordered_map<std::string, VarDecl> declaredVars;

    int braceDepth = 0;
    bool inClass = false;
    bool classBraceStarted = false;
    bool inPublic = false;

    for (const auto& rawLine : lines) {
        std::string l = rawLine;
        size_t start = l.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        l = l.substr(start);

        // Strip single-line comments
        size_t commentPos = l.find("//");
        if (commentPos != std::string::npos) {
            l = l.substr(0, commentPos);
        }
        size_t endTrim = l.find_last_not_of(" \t\r\n");
        if (endTrim == std::string::npos) continue;
        l = l.substr(0, endTrim + 1);

        // Ignore block comments
        if (l.rfind("/*", 0) == 0 || l.rfind("*", 0) == 0) continue;

        // Detect class / struct definition
        if (!inClass) {
            if (l.find("class ") != std::string::npos || l.find("struct ") != std::string::npos) {
                if (l.find(';') == std::string::npos || l.find('{') != std::string::npos) {
                    inClass = true;
                    classBraceStarted = (l.find('{') != std::string::npos);
                    inPublic = (l.find("struct ") != std::string::npos);

                    size_t cPos = l.find("class ");
                    size_t sPos = l.find("struct ");
                    size_t kwPos = (cPos != std::string::npos) ? cPos + 6 : ((sPos != std::string::npos) ? sPos + 7 : std::string::npos);
                    if (kwPos != std::string::npos) {
                        size_t nameStart = l.find_first_not_of(" \t", kwPos);
                        if (nameStart != std::string::npos) {
                            size_t nameEnd = l.find_first_of(" \t:{;", nameStart);
                            std::string parsedClassName = (nameEnd != std::string::npos) ? l.substr(nameStart, nameEnd - nameStart) : l.substr(nameStart);
                            if (!parsedClassName.empty() && (m_className.empty() || m_className == "CustomBehaviour")) {
                                m_className = parsedClassName;
                                if (m_displayName.empty() || m_displayName == "CustomBehaviour") {
                                    m_displayName = parsedClassName;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Count braces on this line (excluding string literals)
        int openBracesOnLine = 0;
        int closeBracesOnLine = 0;
        bool inStr = false;
        for (char c : l) {
            if (c == '"') inStr = !inStr;
            if (!inStr) {
                if (c == '{') openBracesOnLine++;
                else if (c == '}') closeBracesOnLine++;
            }
        }

        if (inClass && openBracesOnLine > 0) {
            classBraceStarted = true;
        }

        int effectiveDepth = braceDepth;

        // Check for access specifiers at class scope
        if (effectiveDepth == 1 || (effectiveDepth == 0 && inClass && openBracesOnLine > 0)) {
            if (l == "public:" || l.rfind("public:", 0) == 0) {
                inPublic = true;
                braceDepth += openBracesOnLine - closeBracesOnLine;
                continue;
            } else if (l == "private:" || l.rfind("private:", 0) == 0 ||
                       l == "protected:" || l.rfind("protected:", 0) == 0) {
                inPublic = false;
                braceDepth += openBracesOnLine - closeBracesOnLine;
                continue;
            }
        }

        bool isClassPublic = (effectiveDepth == 1 && inClass && inPublic);

        braceDepth += openBracesOnLine - closeBracesOnLine;
        if (classBraceStarted && braceDepth <= 0) {
            braceDepth = 0;
            inClass = false;
            inPublic = false;
            classBraceStarted = false;
        }

        if (!isClassPublic) continue;

        // Skip destructors and methods
        if (l.find('~') != std::string::npos) continue;
        size_t openParen = l.find('(');
        if (openParen != std::string::npos) {
            size_t eqPos = l.find('=');
            if (eqPos == std::string::npos || openParen < eqPos ||
                l.find("override") != std::string::npos || l.find("void ") != std::string::npos ||
                l.find("virtual ") != std::string::npos) {
                continue;
            }
        }
        if (l.find(';') == std::string::npos) continue;

        // Check pointer declarations (handles "GameObject* Cam", "GameObject *Cam", "Actor *Cam", etc.)
        static const std::vector<std::pair<std::string, std::string>> ptrTypes = {
            { "GameObject", "GameObject*" },
            { "Actor", "GameObject*" },
            { "Camera", "Camera*" },
            { "LightComponent", "LightComponent*" },
            { "Light", "LightComponent*" },
            { "PrimitiveMesh", "PrimitiveMesh*" },
            { "Mesh", "PrimitiveMesh*" },
            { "UserInterface", "UserInterface*" }
        };

        bool matchedPtr = false;
        for (const auto& pt : ptrTypes) {
            if (l.rfind(pt.first, 0) == 0) {
                size_t afterName = pt.first.length();
                while (afterName < l.size() && (l[afterName] == ' ' || l[afterName] == '\t')) afterName++;
                if (afterName < l.size() && l[afterName] == '*') {
                    matchedPtr = true;
                    std::string rest = l.substr(afterName + 1);
                    size_t semi = rest.find(';');
                    if (semi != std::string::npos) rest = rest.substr(0, semi);

                    size_t eq = rest.find('=');
                    std::string varName = (eq != std::string::npos) ? rest.substr(0, eq) : rest;
                    std::string defVal = (eq != std::string::npos) ? rest.substr(eq + 1) : "";

                    size_t vnStart = varName.find_first_not_of(" \t*&");
                    size_t vnEnd = varName.find_last_not_of(" \t*&");
                    if (vnStart != std::string::npos && vnEnd != std::string::npos) {
                        varName = varName.substr(vnStart, vnEnd - vnStart + 1);
                    }

                    size_t dvStart = defVal.find_first_not_of(" \t");
                    size_t dvEnd = defVal.find_last_not_of(" \t");
                    if (dvStart != std::string::npos && dvEnd != std::string::npos) {
                        defVal = defVal.substr(dvStart, dvEnd - dvStart + 1);
                    }

                    if (!varName.empty()) {
                        declaredVars[varName] = { pt.second, defVal };
                    }
                    break;
                }
            }
        }
        if (matchedPtr) continue;

        static const std::vector<std::string> types = {
            "float", "double", "int", "bool", "std::string", "string",
            "glm::vec4", "Vector4", "vec4",
            "glm::vec3", "Vector3", "vec3",
            "glm::vec2", "Vector2", "vec2",
            "int32_t", "int64_t", "uint32_t", "size_t", "unsigned int"
        };

        for (const auto& t : types) {
            if (l.rfind(t + " ", 0) == 0 || l.rfind(t + "\t", 0) == 0) {
                std::string rest = l.substr(t.length());
                size_t semi = rest.find(';');
                if (semi != std::string::npos) rest = rest.substr(0, semi);

                size_t eq = rest.find('=');
                std::string varName = (eq != std::string::npos) ? rest.substr(0, eq) : rest;
                std::string defVal = (eq != std::string::npos) ? rest.substr(eq + 1) : "";

                size_t vnStart = varName.find_first_not_of(" \t*&");
                size_t vnEnd = varName.find_last_not_of(" \t*&");
                if (vnStart != std::string::npos && vnEnd != std::string::npos) {
                    varName = varName.substr(vnStart, vnEnd - vnStart + 1);
                }

                size_t dvStart = defVal.find_first_not_of(" \t");
                size_t dvEnd = defVal.find_last_not_of(" \t");
                if (dvStart != std::string::npos && dvEnd != std::string::npos) {
                    defVal = defVal.substr(dvStart, dvEnd - dvStart + 1);
                }

                if (!varName.empty()) {
                    declaredVars[varName] = { t, defVal };
                }
                break;
            }
        }
    }

    // 2. Second pass: scan for RegisterProperty(...) and RegisterReference(...)
    struct SavedValue {
        BehaviourPropertyType type = BehaviourPropertyType::Float;
        bool boolVal = false;
        int intVal = 0;
        int64_t int64Val = 0;
        float floatVal = 0.0f;
        double doubleVal = 0.0;
        std::string stringVal;
        glm::vec2 vec2Val{0.0f};
        glm::vec3 vec3Val{0.0f};
        glm::vec4 vec4Val{0.0f};
        int targetId = -1;
        std::string assetPath;
        void* refPtr = nullptr;
        bool hasValue = false;
    };
    std::unordered_map<std::string, SavedValue> savedPropsByName;

    if (preserveExistingValues) {
        // Snapshot existing live properties
        for (const auto& p : m_properties) {
            SavedValue sv;
            sv.type = p.type;
            sv.targetId = p.targetId;
            sv.assetPath = p.assetPath;
            sv.hasValue = true;
            if (p.dataPtr) {
                switch (p.type) {
                    case BehaviourPropertyType::Bool:
                        sv.boolVal = *reinterpret_cast<const bool*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::Int:
                        sv.intVal = *reinterpret_cast<const int*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::Int64:
                        sv.int64Val = *reinterpret_cast<const int64_t*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::Float:
                        sv.floatVal = *reinterpret_cast<const float*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::Double:
                        sv.doubleVal = *reinterpret_cast<const double*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::String:
                        sv.stringVal = *reinterpret_cast<const std::string*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::Vec2:
                        sv.vec2Val = *reinterpret_cast<const glm::vec2*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::Vec3:
                    case BehaviourPropertyType::Color3:
                        sv.vec3Val = *reinterpret_cast<const glm::vec3*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::Vec4:
                    case BehaviourPropertyType::Color4:
                        sv.vec4Val = *reinterpret_cast<const glm::vec4*>(p.dataPtr);
                        break;
                    case BehaviourPropertyType::ObjectRef:
                        sv.refPtr = *reinterpret_cast<void* const*>(p.dataPtr);
                        break;
                }
            }
            savedPropsByName[p.name] = sv;
        }

        // Also snapshot from storage maps in case not yet in m_properties
        for (const auto& pair : m_floatStorage) {
            auto& sv = savedPropsByName[pair.first];
            sv.floatVal = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_intStorage) {
            auto& sv = savedPropsByName[pair.first];
            sv.intVal = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_int64Storage) {
            auto& sv = savedPropsByName[pair.first];
            sv.int64Val = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_boolStorage) {
            auto& sv = savedPropsByName[pair.first];
            sv.boolVal = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_stringStorage) {
            auto& sv = savedPropsByName[pair.first];
            sv.stringVal = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_doubleStorage) {
            auto& sv = savedPropsByName[pair.first];
            sv.doubleVal = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_vec2Storage) {
            auto& sv = savedPropsByName[pair.first];
            sv.vec2Val = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_vec3Storage) {
            auto& sv = savedPropsByName[pair.first];
            sv.vec3Val = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_vec4Storage) {
            auto& sv = savedPropsByName[pair.first];
            sv.vec4Val = pair.second;
            sv.hasValue = true;
        }
        for (const auto& pair : m_refStorage) {
            auto& sv = savedPropsByName[pair.first];
            sv.refPtr = pair.second;
            sv.hasValue = true;
        }
    }

    auto findSaved = [&](const std::string& name1, const std::string& name2 = "") -> const SavedValue* {
        if (!preserveExistingValues) return nullptr;
        if (!name1.empty()) {
            auto it = savedPropsByName.find(name1);
            if (it != savedPropsByName.end() && it->second.hasValue) return &it->second;
        }
        if (!name2.empty()) {
            auto it = savedPropsByName.find(name2);
            if (it != savedPropsByName.end() && it->second.hasValue) return &it->second;
        }
        return nullptr;
    };

    std::map<std::string, PropertyDefaultValue> prevDefaults = m_defaultPropertyValues;
    m_properties.clear();
    m_defaultPropertyValues.clear();
    std::unordered_set<std::string> registeredVarNames;

    auto trimArg = [](std::string s) -> std::string {
        size_t b = s.find_first_not_of(" \t\r\n");
        size_t e = s.find_last_not_of(" \t\r\n");
        if (b == std::string::npos || e == std::string::npos) return "";
        s = s.substr(b, e - b + 1);
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
            s = s.substr(1, s.size() - 2);
        }
        return s;
    };

    for (const auto& rawLine : lines) {
        std::string l = rawLine;
        size_t start = l.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        l = l.substr(start);

        if (l.rfind("//", 0) == 0 || l.rfind("/*", 0) == 0) continue;

        size_t regPropPos = l.find("RegisterProperty(");
        size_t regRefPos = l.find("RegisterReference(");

        if (regPropPos != std::string::npos) {
            size_t openP = l.find('(', regPropPos);
            size_t closeP = l.find(')', openP);
            if (openP == std::string::npos || closeP == std::string::npos) continue;
            std::string argsStr = l.substr(openP + 1, closeP - openP - 1);

            std::vector<std::string> args;
            bool inQuote = false;
            std::string curArg;
            for (char c : argsStr) {
                if (c == '"') inQuote = !inQuote;
                else if (c == ',' && !inQuote) {
                    args.push_back(curArg);
                    curArg.clear();
                    continue;
                }
                curArg += c;
            }
            if (!curArg.empty()) args.push_back(curArg);

            if (args.size() < 2) continue;
            std::string propName = trimArg(args[0]);
            std::string varRef = trimArg(args[1]);
            std::string category = (args.size() >= 3) ? trimArg(args[2]) : "General";

            size_t ampPos = varRef.find('&');
            if (ampPos != std::string::npos) varRef = varRef.substr(ampPos + 1);
            size_t arrowPos = varRef.find("->");
            if (arrowPos != std::string::npos) varRef = varRef.substr(arrowPos + 2);
            size_t vStart = varRef.find_first_not_of(" \t");
            size_t vEnd = varRef.find_last_not_of(" \t");
            if (vStart != std::string::npos && vEnd != std::string::npos) varRef = varRef.substr(vStart, vEnd - vStart + 1);

            registeredVarNames.insert(varRef);
            registeredVarNames.insert(propName);

            std::string type = "float";
            std::string defStr = "0";
            auto varIt = declaredVars.find(varRef);
            if (varIt != declaredVars.end()) {
                type = varIt->second.type;
                defStr = varIt->second.defaultVal;
            }

            float minVal = 0.0f, maxVal = 0.0f;
            if (args.size() >= 5) {
                try {
                    minVal = std::stof(trimArg(args[3]));
                    maxVal = std::stof(trimArg(args[4]));
                } catch (...) {}
            }

            if (type == "int" || type == "int32_t" || type == "int64_t" || type == "uint32_t" || type == "size_t") {
                int defInt = 0;
                try { if (!defStr.empty()) defInt = std::stoi(defStr); } catch (...) {}
                bool defaultChangedInCpp = false;
                auto prevDefIt = prevDefaults.find(varRef.empty() ? propName : varRef);
                if (prevDefIt == prevDefaults.end()) prevDefIt = prevDefaults.find(propName);
                if (prevDefIt != prevDefaults.end() && prevDefIt->second.type == BehaviourPropertyType::Int) {
                    if (prevDefIt->second.intVal != defInt) defaultChangedInCpp = true;
                }

                PropertyDefaultValue def;
                def.type = BehaviourPropertyType::Int;
                def.intVal = defInt;
                m_defaultPropertyValues[propName] = def;
                if (!varRef.empty()) m_defaultPropertyValues[varRef] = def;

                const SavedValue* sv = findSaved(propName, varRef);
                int actualInt = (sv && sv->hasValue && !defaultChangedInCpp) ? sv->intVal : defInt;
                m_intStorage[propName] = actualInt;
                if (!varRef.empty()) m_intStorage[varRef] = actualInt;
                RegisterProperty(propName, &m_intStorage[propName], category, (int)minVal, (int)maxVal);
            } else if (type == "bool") {
                bool defBool = (defStr == "true" || defStr == "1");
                bool defaultChangedInCpp = false;
                auto prevDefIt = prevDefaults.find(varRef.empty() ? propName : varRef);
                if (prevDefIt == prevDefaults.end()) prevDefIt = prevDefaults.find(propName);
                if (prevDefIt != prevDefaults.end() && prevDefIt->second.type == BehaviourPropertyType::Bool) {
                    if (prevDefIt->second.boolVal != defBool) defaultChangedInCpp = true;
                }

                PropertyDefaultValue def;
                def.type = BehaviourPropertyType::Bool;
                def.boolVal = defBool;
                m_defaultPropertyValues[propName] = def;
                if (!varRef.empty()) m_defaultPropertyValues[varRef] = def;

                const SavedValue* sv = findSaved(propName, varRef);
                bool actualBool = (sv && sv->hasValue && !defaultChangedInCpp) ? sv->boolVal : defBool;
                m_boolStorage[propName] = actualBool;
                if (!varRef.empty()) m_boolStorage[varRef] = actualBool;
                RegisterProperty(propName, &m_boolStorage[propName], category);
            } else if (type == "std::string" || type == "string") {
                std::string defS = trimArg(defStr);
                bool defaultChangedInCpp = false;
                auto prevDefIt = prevDefaults.find(varRef.empty() ? propName : varRef);
                if (prevDefIt == prevDefaults.end()) prevDefIt = prevDefaults.find(propName);
                if (prevDefIt != prevDefaults.end() && prevDefIt->second.type == BehaviourPropertyType::String) {
                    if (prevDefIt->second.stringVal != defS) defaultChangedInCpp = true;
                }

                PropertyDefaultValue def;
                def.type = BehaviourPropertyType::String;
                def.stringVal = defS;
                m_defaultPropertyValues[propName] = def;
                if (!varRef.empty()) m_defaultPropertyValues[varRef] = def;

                const SavedValue* sv = findSaved(propName, varRef);
                std::string actualS = (sv && sv->hasValue && !defaultChangedInCpp) ? sv->stringVal : defS;
                m_stringStorage[propName] = actualS;
                if (!varRef.empty()) m_stringStorage[varRef] = actualS;
                RegisterProperty(propName, &m_stringStorage[propName], category);
            } else if (type == "glm::vec2" || type == "Vector2" || type == "vec2") {
                glm::vec2 defVec2(0.0f);
                PropertyDefaultValue def;
                def.type = BehaviourPropertyType::Vec2;
                def.vec2Val = defVec2;
                m_defaultPropertyValues[propName] = def;
                if (!varRef.empty()) m_defaultPropertyValues[varRef] = def;

                const SavedValue* sv = findSaved(propName, varRef);
                glm::vec2 actualVec2 = (sv && sv->hasValue) ? sv->vec2Val : defVec2;
                m_vec2Storage[propName] = actualVec2;
                if (!varRef.empty()) m_vec2Storage[varRef] = actualVec2;
                RegisterProperty(propName, &m_vec2Storage[propName], category);
            } else if (type == "glm::vec3" || type == "Vector3" || type == "vec3") {
                glm::vec3 defVec(0.0f);
                PropertyDefaultValue def;
                def.type = BehaviourPropertyType::Vec3;
                def.vec3Val = defVec;
                m_defaultPropertyValues[propName] = def;
                if (!varRef.empty()) m_defaultPropertyValues[varRef] = def;

                const SavedValue* sv = findSaved(propName, varRef);
                glm::vec3 actualVec = (sv && sv->hasValue) ? sv->vec3Val : defVec;
                m_vec3Storage[propName] = actualVec;
                if (!varRef.empty()) m_vec3Storage[varRef] = actualVec;
                RegisterProperty(propName, &m_vec3Storage[propName], category);
            } else if (type == "glm::vec4" || type == "Vector4" || type == "vec4") {
                glm::vec4 defVec4(0.0f);
                PropertyDefaultValue def;
                def.type = BehaviourPropertyType::Vec4;
                def.vec4Val = defVec4;
                m_defaultPropertyValues[propName] = def;
                if (!varRef.empty()) m_defaultPropertyValues[varRef] = def;

                const SavedValue* sv = findSaved(propName, varRef);
                glm::vec4 actualVec4 = (sv && sv->hasValue) ? sv->vec4Val : defVec4;
                m_vec4Storage[propName] = actualVec4;
                if (!varRef.empty()) m_vec4Storage[varRef] = actualVec4;
                RegisterProperty(propName, &m_vec4Storage[propName], category);
            } else if (type == "double") {
                double defD = 0.0;
                try {
                    std::string cleanDef = defStr;
                    if (!cleanDef.empty() && (cleanDef.back() == 'd' || cleanDef.back() == 'D')) cleanDef.pop_back();
                    if (!cleanDef.empty()) defD = std::stod(cleanDef);
                } catch (...) {}
                bool defaultChangedInCpp = false;
                auto prevDefIt = prevDefaults.find(varRef.empty() ? propName : varRef);
                if (prevDefIt == prevDefaults.end()) prevDefIt = prevDefaults.find(propName);
                if (prevDefIt != prevDefaults.end() && prevDefIt->second.type == BehaviourPropertyType::Double) {
                    if (std::abs(prevDefIt->second.doubleVal - defD) > 1e-5) defaultChangedInCpp = true;
                }

                PropertyDefaultValue def;
                def.type = BehaviourPropertyType::Double;
                def.doubleVal = defD;
                m_defaultPropertyValues[propName] = def;
                if (!varRef.empty()) m_defaultPropertyValues[varRef] = def;

                const SavedValue* sv = findSaved(propName, varRef);
                double actualD = (sv && sv->hasValue && !defaultChangedInCpp) ? sv->doubleVal : defD;
                m_doubleStorage[propName] = actualD;
                if (!varRef.empty()) m_doubleStorage[varRef] = actualD;
                RegisterProperty(propName, &m_doubleStorage[propName], category);
            } else {
                float defF = 0.0f;
                try {
                    std::string cleanDef = defStr;
                    if (!cleanDef.empty() && (cleanDef.back() == 'f' || cleanDef.back() == 'F')) cleanDef.pop_back();
                    if (!cleanDef.empty()) defF = std::stof(cleanDef);
                } catch (...) {}
                bool defaultChangedInCpp = false;
                auto prevDefIt = prevDefaults.find(varRef.empty() ? propName : varRef);
                if (prevDefIt == prevDefaults.end()) prevDefIt = prevDefaults.find(propName);
                if (prevDefIt != prevDefaults.end() && prevDefIt->second.type == BehaviourPropertyType::Float) {
                    if (std::abs(prevDefIt->second.floatVal - defF) > 1e-5f) defaultChangedInCpp = true;
                }

                PropertyDefaultValue def;
                def.type = BehaviourPropertyType::Float;
                def.floatVal = defF;
                m_defaultPropertyValues[propName] = def;
                if (!varRef.empty()) m_defaultPropertyValues[varRef] = def;

                const SavedValue* sv = findSaved(propName, varRef);
                float actualF = (sv && sv->hasValue && !defaultChangedInCpp) ? sv->floatVal : defF;
                m_floatStorage[propName] = actualF;
                if (!varRef.empty()) m_floatStorage[varRef] = actualF;
                RegisterProperty(propName, &m_floatStorage[propName], category, minVal, maxVal);

                std::string pLower = propName;
                std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::tolower);
                if (pLower.find("speed") != std::string::npos || pLower.find("rot") != std::string::npos) {
                    m_defaultSpeed = actualF;
                }
            }
        } else if (regRefPos != std::string::npos) {
            size_t openP = l.find('(', regRefPos);
            size_t closeP = l.find(')', openP);
            if (openP == std::string::npos || closeP == std::string::npos) continue;
            std::string argsStr = l.substr(openP + 1, closeP - openP - 1);

            std::vector<std::string> args;
            bool inQuote = false;
            std::string curArg;
            for (char c : argsStr) {
                if (c == '"') inQuote = !inQuote;
                else if (c == ',' && !inQuote) {
                    args.push_back(curArg);
                    curArg.clear();
                    continue;
                }
                curArg += c;
            }
            if (!curArg.empty()) args.push_back(curArg);

            if (args.size() < 2) continue;
            std::string propName = trimArg(args[0]);
            std::string refVar = trimArg(args[1]);
            size_t ampPos = refVar.find('&');
            if (ampPos != std::string::npos) refVar = refVar.substr(ampPos + 1);
            size_t arrowPos = refVar.find("->");
            if (arrowPos != std::string::npos) refVar = refVar.substr(arrowPos + 2);
            size_t rvStart = refVar.find_first_not_of(" \t");
            size_t rvEnd = refVar.find_last_not_of(" \t");
            if (rvStart != std::string::npos && rvEnd != std::string::npos) refVar = refVar.substr(rvStart, rvEnd - rvStart + 1);

            registeredVarNames.insert(propName);
            if (!refVar.empty()) registeredVarNames.insert(refVar);

            ObjectRefType refType = ObjectRefType::Actor;
            std::string category = "References";

            if (args.size() == 2) {
                refType = ObjectRefType::Actor;
                category = "References";
            } else if (args.size() == 3) {
                std::string arg2 = trimArg(args[2]);
                std::string arg2Lower = arg2;
                std::transform(arg2Lower.begin(), arg2Lower.end(), arg2Lower.begin(), ::tolower);
                if (arg2Lower.find("objectreftype") != std::string::npos ||
                    arg2Lower == "light" || arg2Lower == "mesh" || arg2Lower == "camera" || arg2Lower == "actor" || arg2Lower == "gameobject") {
                    if (arg2Lower.find("light") != std::string::npos) refType = ObjectRefType::Light;
                    else if (arg2Lower.find("mesh") != std::string::npos) refType = ObjectRefType::Mesh;
                    else if (arg2Lower.find("camera") != std::string::npos) refType = ObjectRefType::Camera;
                    else refType = ObjectRefType::Actor;
                    category = "References";
                } else {
                    refType = ObjectRefType::Actor;
                    category = arg2;
                }
            } else if (args.size() >= 4) {
                std::string arg2 = trimArg(args[2]);
                std::string arg2Lower = arg2;
                std::transform(arg2Lower.begin(), arg2Lower.end(), arg2Lower.begin(), ::tolower);
                if (arg2Lower.find("light") != std::string::npos) refType = ObjectRefType::Light;
                else if (arg2Lower.find("mesh") != std::string::npos) refType = ObjectRefType::Mesh;
                else if (arg2Lower.find("camera") != std::string::npos) refType = ObjectRefType::Camera;
                else refType = ObjectRefType::Actor;

                category = trimArg(args[3]);
            }

            auto itVar = declaredVars.find(refVar);
            if (itVar != declaredVars.end() && (itVar->second.type == "GameObject*" || itVar->second.type == "Actor*")) {
                refType = ObjectRefType::Actor;
            }

            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::ObjectRef;
            def.targetId = -1;
            m_defaultPropertyValues[propName] = def;
            if (!refVar.empty()) m_defaultPropertyValues[refVar] = def;

            const SavedValue* sv = findSaved(propName, refVar);
            void* actualPtr = (sv && sv->hasValue) ? sv->refPtr : nullptr;
            int actualTargetId = (sv && sv->hasValue) ? sv->targetId : -1;
            std::string actualAssetPath = (sv && sv->hasValue) ? sv->assetPath : "";

            m_refStorage[propName] = actualPtr;
            if (!refVar.empty()) m_refStorage[refVar] = actualPtr;

            RegisterReference(propName, &m_refStorage[propName], refType, category);
            if (!m_properties.empty()) {
                m_properties.back().targetId = actualTargetId;
                m_properties.back().assetPath = actualAssetPath;
            }
        }
    }

    // Auto-register any declared member variables that were not in RegisterProperty
    for (const auto& pair : declaredVars) {
        const std::string& varName = pair.first;
        if (registeredVarNames.find(varName) != registeredVarNames.end()) continue;

        const std::string& type = pair.second.type;
        const std::string& defStr = pair.second.defaultVal;

        if (type == "float") {
            float defF = 0.0f;
            try {
                std::string clean = defStr;
                if (!clean.empty() && (clean.back() == 'f' || clean.back() == 'F')) clean.pop_back();
                if (!clean.empty()) defF = std::stof(clean);
            } catch (...) {}
            bool defaultChangedInCpp = false;
            auto prevDefIt = prevDefaults.find(varName);
            if (prevDefIt != prevDefaults.end() && prevDefIt->second.type == BehaviourPropertyType::Float) {
                if (std::abs(prevDefIt->second.floatVal - defF) > 1e-5f) defaultChangedInCpp = true;
            }

            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::Float;
            def.floatVal = defF;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            float actualF = (sv && sv->hasValue && !defaultChangedInCpp) ? sv->floatVal : defF;
            m_floatStorage[varName] = actualF;
            RegisterProperty(varName, &m_floatStorage[varName], "General");
            std::string vLower = varName;
            std::transform(vLower.begin(), vLower.end(), vLower.begin(), ::tolower);
            if (vLower.find("speed") != std::string::npos) {
                m_defaultSpeed = actualF;
            }
        } else if (type == "bool") {
            bool defB = (defStr == "true" || defStr == "1");
            bool defaultChangedInCpp = false;
            auto prevDefIt = prevDefaults.find(varName);
            if (prevDefIt != prevDefaults.end() && prevDefIt->second.type == BehaviourPropertyType::Bool) {
                if (prevDefIt->second.boolVal != defB) defaultChangedInCpp = true;
            }

            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::Bool;
            def.boolVal = defB;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            bool actualB = (sv && sv->hasValue && !defaultChangedInCpp) ? sv->boolVal : defB;
            m_boolStorage[varName] = actualB;
            RegisterProperty(varName, &m_boolStorage[varName], "General");
        } else if (type == "int" || type == "int32_t" || type == "int64_t" || type == "uint32_t" || type == "size_t") {
            int defI = 0;
            try { if (!defStr.empty()) defI = std::stoi(defStr); } catch (...) {}
            bool defaultChangedInCpp = false;
            auto prevDefIt = prevDefaults.find(varName);
            if (prevDefIt != prevDefaults.end() && prevDefIt->second.type == BehaviourPropertyType::Int) {
                if (prevDefIt->second.intVal != defI) defaultChangedInCpp = true;
            }

            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::Int;
            def.intVal = defI;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            int actualI = (sv && sv->hasValue && !defaultChangedInCpp) ? sv->intVal : defI;
            m_intStorage[varName] = actualI;
            RegisterProperty(varName, &m_intStorage[varName], "General");
        } else if (type == "double") {
            double defD = 0.0;
            try {
                std::string cleanDef = defStr;
                if (!cleanDef.empty() && (cleanDef.back() == 'd' || cleanDef.back() == 'D')) cleanDef.pop_back();
                if (!cleanDef.empty()) defD = std::stod(cleanDef);
            } catch (...) {}
            bool defaultChangedInCpp = false;
            auto prevDefIt = prevDefaults.find(varName);
            if (prevDefIt != prevDefaults.end() && prevDefIt->second.type == BehaviourPropertyType::Double) {
                if (std::abs(prevDefIt->second.doubleVal - defD) > 1e-5) defaultChangedInCpp = true;
            }

            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::Double;
            def.doubleVal = defD;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            double actualD = (sv && sv->hasValue && !defaultChangedInCpp) ? sv->doubleVal : defD;
            m_doubleStorage[varName] = actualD;
            RegisterProperty(varName, &m_doubleStorage[varName], "General");
        } else if (type == "std::string" || type == "string") {
            std::string defS = trimArg(defStr);
            bool defaultChangedInCpp = false;
            auto prevDefIt = prevDefaults.find(varName);
            if (prevDefIt != prevDefaults.end() && prevDefIt->second.type == BehaviourPropertyType::String) {
                if (prevDefIt->second.stringVal != defS) defaultChangedInCpp = true;
            }

            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::String;
            def.stringVal = defS;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            std::string actualS = (sv && sv->hasValue && !defaultChangedInCpp) ? sv->stringVal : defS;
            m_stringStorage[varName] = actualS;
            RegisterProperty(varName, &m_stringStorage[varName], "General");
        } else if (type == "glm::vec2" || type == "Vector2" || type == "vec2") {
            glm::vec2 defVec2(0.0f);
            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::Vec2;
            def.vec2Val = defVec2;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            glm::vec2 actualVec2 = (sv && sv->hasValue) ? sv->vec2Val : defVec2;
            m_vec2Storage[varName] = actualVec2;
            RegisterProperty(varName, &m_vec2Storage[varName], "General");
        } else if (type == "glm::vec3" || type == "Vector3" || type == "vec3") {
            glm::vec3 defVec(0.0f);
            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::Vec3;
            def.vec3Val = defVec;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            glm::vec3 actualVec = (sv && sv->hasValue) ? sv->vec3Val : defVec;
            m_vec3Storage[varName] = actualVec;
            RegisterProperty(varName, &m_vec3Storage[varName], "General");
        } else if (type == "glm::vec4" || type == "Vector4" || type == "vec4") {
            glm::vec4 defVec4(0.0f);
            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::Vec4;
            def.vec4Val = defVec4;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            glm::vec4 actualVec4 = (sv && sv->hasValue) ? sv->vec4Val : defVec4;
            m_vec4Storage[varName] = actualVec4;
            RegisterProperty(varName, &m_vec4Storage[varName], "General");
        } else if (type == "Camera*" || type == "LightComponent*" || type == "Light*" ||
                   type == "PrimitiveMesh*" || type == "Mesh*" || type == "GameObject*" || type == "Actor*" ||
                   type == "UserInterface*") {
            ObjectRefType refType = ObjectRefType::Actor;
            if (type == "Camera*") refType = ObjectRefType::Camera;
            else if (type == "LightComponent*" || type == "Light*") refType = ObjectRefType::Light;
            else if (type == "PrimitiveMesh*" || type == "Mesh*") refType = ObjectRefType::Mesh;
            else if (type == "UserInterface*") refType = ObjectRefType::UserInterface;

            PropertyDefaultValue def;
            def.type = BehaviourPropertyType::ObjectRef;
            def.targetId = -1;
            m_defaultPropertyValues[varName] = def;

            const SavedValue* sv = findSaved(varName);
            void* actualPtr = (sv && sv->hasValue) ? sv->refPtr : nullptr;
            int actualTargetId = (sv && sv->hasValue) ? sv->targetId : -1;
            std::string actualAssetPath = (sv && sv->hasValue) ? sv->assetPath : "";

            m_refStorage[varName] = actualPtr;
            RegisterReference(varName, &m_refStorage[varName], refType, "References");
            if (!m_properties.empty()) {
                m_properties.back().targetId = actualTargetId;
                m_properties.back().assetPath = actualAssetPath;
            }
        }
    }

    // 3. Third pass: scan Start() and Update() bodies for runtime actions
    m_startPrintMessages.clear();
    m_startUIAddTargets.clear();
    m_startUIRemoveTargets.clear();
    m_startUIRemoveAll = false;
    m_startSaveActions.clear();
    m_updatePrintMessages.clear();
    m_updateSaveActions.clear();
    m_destroyActions.clear();
    m_transformActions.clear();
    m_lineTraceActions.clear();
    m_uiActions.clear();
    m_hasRotation = false;
    m_hasTranslation = false;
    m_rotateAxis = glm::vec3(0.0f, 1.0f, 0.0f);

    struct LocalVarExpr {
        float constantVal = 0.0f;
        bool useDt = false;
        std::string varName;
        float varMultiplier = 1.0f;
        std::string scaleVarName;
        bool isActive = false;
    };
    std::unordered_map<std::string, LocalVarExpr> localVars;

    auto parseArgExpr = [&](const std::string& rawExpr, float& outConst, bool& outUseDt, std::string& outVar, float& outMul, std::string& outScaleVar, bool& outActive) {
        outConst = 0.0f;
        outUseDt = false;
        outVar.clear();
        outMul = 1.0f;
        outScaleVar.clear();
        outActive = false;

        std::string s = rawExpr;
        size_t b = s.find_first_not_of(" \t\r\n");
        size_t e = s.find_last_not_of(" \t\r\n");
        if (b == std::string::npos || e == std::string::npos) return;
        s = s.substr(b, e - b + 1);

        auto itLoc = localVars.find(s);
        if (itLoc != localVars.end()) {
            outConst = itLoc->second.constantVal;
            outUseDt = itLoc->second.useDt;
            outVar = itLoc->second.varName;
            outMul = itLoc->second.varMultiplier;
            outScaleVar = itLoc->second.scaleVarName;
            outActive = itLoc->second.isActive;
            return;
        }

        std::string lowerS = s;
        std::transform(lowerS.begin(), lowerS.end(), lowerS.begin(), ::tolower);

        if (lowerS.find("deltatime") != std::string::npos ||
            lowerS.find("delta_time") != std::string::npos ||
            lowerS.find("worlddeltaseconds") != std::string::npos ||
            lowerS.find("dt") != std::string::npos) {
            outUseDt = true;
        }

        bool isMouseNegated = false;
        if (lowerS.find("getmousedeltax") != std::string::npos || lowerS.find("getmouseaxisx") != std::string::npos) {
            outVar = "$__MouseDeltaX";
            outActive = true;
            if (lowerS.find("(true)") != std::string::npos || lowerS.find("(1)") != std::string::npos || lowerS.find("-i.getmouse") != std::string::npos || lowerS.find("-input.getmouse") != std::string::npos || s.find('-') != std::string::npos) {
                isMouseNegated = true;
            }
        } else if (lowerS.find("getmousedeltay") != std::string::npos || lowerS.find("getmouseaxisy") != std::string::npos) {
            outVar = "$__MouseDeltaY";
            outActive = true;
            if (lowerS.find("(true)") != std::string::npos || lowerS.find("(1)") != std::string::npos || lowerS.find("-i.getmouse") != std::string::npos || lowerS.find("-input.getmouse") != std::string::npos || s.find('-') != std::string::npos) {
                isMouseNegated = true;
            }
        } else if (lowerS.find("getmousex") != std::string::npos) {
            outVar = "$__MouseX";
            outActive = true;
            if (lowerS.find("getmousex(true)") != std::string::npos || lowerS.find("getmousex(1)") != std::string::npos || s.find('-') != std::string::npos) {
                isMouseNegated = true;
            }
        } else if (lowerS.find("getmousey") != std::string::npos) {
            outVar = "$__MouseY";
            outActive = true;
            if (lowerS.find("getmousey(true)") != std::string::npos || lowerS.find("getmousey(1)") != std::string::npos || s.find('-') != std::string::npos) {
                isMouseNegated = true;
            }
        }

        for (const auto& v : declaredVars) {
            std::string vName = v.first;
            if (s.find(vName) != std::string::npos) {
                if (outVar.empty()) {
                    outVar = vName;
                    outActive = true;
                } else if (outVar != vName && outScaleVar.empty()) {
                    outScaleVar = vName;
                }
            }
        }

        bool hasMinusBeforeVar = false;
        if (!outVar.empty()) {
            size_t varPos = s.find(outVar);
            if (varPos != std::string::npos) {
                size_t minusPos = s.rfind('-', varPos);
                if (minusPos != std::string::npos) {
                    bool onlyWs = true;
                    for (size_t k = minusPos + 1; k < varPos; ++k) {
                        if (!std::isspace(static_cast<unsigned char>(s[k])) && s[k] != '(') {
                            onlyWs = false;
                            break;
                        }
                    }
                    if (onlyWs) hasMinusBeforeVar = true;
                }
            }
        }

        float sign = (isMouseNegated ^ hasMinusBeforeVar) ? -1.0f : 1.0f;
        outMul = sign;

        size_t numStart = s.find_first_of("-+0123456789.");
        if (numStart != std::string::npos) {
            size_t numEnd = numStart;
            if (s[numStart] == '-' || s[numStart] == '+') numEnd++;
            while (numEnd < s.size() && (std::isdigit(s[numEnd]) || s[numEnd] == '.' || s[numEnd] == 'f' || s[numEnd] == 'F')) {
                numEnd++;
            }
            std::string numStr = s.substr(numStart, numEnd - numStart);
            if (!numStr.empty() && (numStr.back() == 'f' || numStr.back() == 'F')) numStr.pop_back();
            try {
                if (!numStr.empty() && numStr != "-" && numStr != "+") {
                    float val = std::stof(numStr);
                    if (outVar.empty()) {
                        outConst = val;
                        if (std::abs(val) > 1e-6f) outActive = true;
                    } else {
                        outMul = std::abs(val) * sign * (val < 0.0f ? -1.0f : 1.0f);
                    }
                }
            } catch (...) {}
        }

        if (!outVar.empty() || std::abs(outConst) > 1e-6f || outUseDt) {
            outActive = true;
        }
    };

    auto extractKeyConditions = [](const std::string& str) -> std::vector<DynamicScriptBehaviour::DynamicKeyCondition> {
        std::vector<DynamicScriptBehaviour::DynamicKeyCondition> conds;
        static const std::unordered_map<std::string, int> keyMap = {
            {"w", Key::W}, {"a", Key::A}, {"s", Key::S}, {"d", Key::D},
            {"q", Key::Q}, {"e", Key::E}, {"r", Key::R}, {"f", Key::F},
            {"c", Key::C}, {"x", Key::X}, {"z", Key::Z}, {"v", Key::V},
            {"b", Key::B}, {"g", Key::G}, {"h", Key::H}, {"i", Key::I},
            {"j", Key::J}, {"k", Key::K}, {"l", Key::L}, {"m", Key::M},
            {"n", Key::N}, {"o", Key::O}, {"p", Key::P}, {"t", Key::T},
            {"u", Key::U}, {"y", Key::Y},
            {"space", Key::Space}, {"lshift", Key::LShift}, {"rshift", Key::RShift},
            {"lctrl", Key::LCtrl}, {"rctrl", Key::RCtrl}, {"lalt", Key::LAlt}, {"ralt", Key::RAlt},
            {"up", Key::Up}, {"down", Key::Down}, {"left", Key::Left}, {"right", Key::Right},
            {"escape", Key::Escape}, {"enter", Key::Enter}, {"tab", Key::Tab},
            {"delete", Key::Delete}, {"backspace", Key::Backspace}
        };

        if (str.find("GetMouseX(") != std::string::npos || str.find("GetMouseX()") != std::string::npos) {
            DynamicScriptBehaviour::DynamicKeyCondition cond;
            cond.key = -2;
            conds.push_back(cond);
        }
        if (str.find("GetMouseY(") != std::string::npos || str.find("GetMouseY()") != std::string::npos) {
            DynamicScriptBehaviour::DynamicKeyCondition cond;
            cond.key = -3;
            conds.push_back(cond);
        }

        size_t searchPos = 0;
        while (searchPos < str.size()) {
            size_t kdPos = str.find("IsKeyDown(", searchPos);
            size_t kpPos = str.find("IsKeyPressed(", searchPos);
            size_t nextPos = std::min(kdPos, kpPos);
            if (nextPos == std::string::npos) break;

            bool isPress = (nextPos == kpPos);
            bool isNegated = false;
            if (nextPos > 0 && str[nextPos - 1] == '!') isNegated = true;
            else if (nextPos >= 2 && str[nextPos - 2] == '!' && str[nextPos - 1] == ' ') isNegated = true;

            size_t openP = str.find('(', nextPos);
            size_t closeP = str.find(')', openP);
            if (openP == std::string::npos || closeP == std::string::npos) break;

            std::string arg = str.substr(openP + 1, closeP - openP - 1);
            std::string lowerArg = arg;
            std::transform(lowerArg.begin(), lowerArg.end(), lowerArg.begin(), ::tolower);

            size_t colon = lowerArg.rfind("::");
            if (colon != std::string::npos) lowerArg = lowerArg.substr(colon + 2);
            size_t glfwPrefix = lowerArg.find("glfw_key_");
            if (glfwPrefix != std::string::npos) lowerArg = lowerArg.substr(glfwPrefix + 9);

            size_t b = lowerArg.find_first_not_of(" \t\r\n'\"");
            size_t e = lowerArg.find_last_not_of(" \t\r\n'\"");
            if (b != std::string::npos && e != std::string::npos) {
                lowerArg = lowerArg.substr(b, e - b + 1);
            }

            auto it = keyMap.find(lowerArg);
            if (it != keyMap.end()) {
                DynamicScriptBehaviour::DynamicKeyCondition cond;
                cond.key = it->second;
                cond.isPressed = isPress;
                cond.isNegated = isNegated;
                conds.push_back(cond);
            }

            searchPos = closeP + 1;
        }
        return conds;
    };

    auto extractBoolConditions = [&](const std::string& str) -> std::vector<DynamicScriptBehaviour::DynamicBoolCondition> {
        std::vector<DynamicScriptBehaviour::DynamicBoolCondition> conds;
        for (const auto& v : declaredVars) {
            if (v.second.type != "bool") continue;
            const std::string& varName = v.first;
            size_t p = str.find(varName);
            if (p == std::string::npos) continue;

            if (p > 0 && (std::isalnum((unsigned char)str[p - 1]) || str[p - 1] == '_')) continue;
            size_t after = p + varName.size();
            if (after < str.size() && (std::isalnum((unsigned char)str[after]) || str[after] == '_')) continue;

            DynamicScriptBehaviour::DynamicBoolCondition bc;
            bc.varName = varName;
            bc.expectedVal = true;

            std::string sub = str.substr(0, p);
            size_t lastNonSpace = sub.find_last_not_of(" \t(");
            bool isNegated = (lastNonSpace != std::string::npos && sub[lastNonSpace] == '!');

            std::string rest = str.substr(after);
            size_t eqPos = rest.find("==");
            if (eqPos != std::string::npos) {
                std::string afterEq = rest.substr(eqPos + 2);
                size_t fPos = afterEq.find("false");
                size_t tPos = afterEq.find("true");
                if (fPos != std::string::npos && (tPos == std::string::npos || fPos < tPos)) {
                    bc.expectedVal = false;
                } else if (tPos != std::string::npos) {
                    bc.expectedVal = true;
                }
            } else if (rest.find("!=") != std::string::npos) {
                std::string afterNeq = rest.substr(rest.find("!=") + 2);
                size_t tPos = afterNeq.find("true");
                if (tPos != std::string::npos) {
                    bc.expectedVal = false;
                }
            } else if (isNegated) {
                bc.expectedVal = false;
            }

            conds.push_back(bc);
        }
        return conds;
    };

    struct ConditionScope {
        int braceDepth = 0;
        std::vector<DynamicScriptBehaviour::DynamicKeyCondition> keys;
        std::vector<DynamicScriptBehaviour::DynamicBoolCondition> bools;
        bool requireHitStatus = false;
        bool requireNoHit = false;
        std::string requireHitTag;
    };
    std::vector<ConditionScope> conditionStack;
    std::vector<DynamicScriptBehaviour::DynamicKeyCondition> pendingSingleLineKeys;
    std::vector<DynamicScriptBehaviour::DynamicBoolCondition> pendingSingleLineBools;
    bool pendingSingleLineRequireHitStatus = false;
    bool pendingSingleLineRequireNoHit = false;
    std::string pendingSingleLineRequireHitTag;
    bool hasPendingSingleLineCondition = false;
    bool lastIfWasHitCondition = false;
    bool lastIfHitConditionNegated = false;
    std::string lastIfHitTag;
    int updateMethodBraceDepth = 0;

    m_methods.clear();
    std::string currentMethodName;
    int currentMethodBraceDepth = 0;

    struct MethodParseState {
        DynamicScriptBehaviour::ScriptBranch currentBranch;
        bool inIf = false;
        bool inElse = false;
        int ifBraceDepth = 0;
        int elseBraceDepth = 0;
        bool singleLineIf = false;
        bool singleLineElse = false;
    };
    MethodParseState methodState;

    bool inStartMethod = false;
    bool inUpdateMethod = false;

    for (const auto& rawLine : lines) {
        std::string l = rawLine;
        size_t start = l.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        l = l.substr(start);

        if (l.rfind("//", 0) == 0 || l.rfind("/*", 0) == 0) continue;

        // Count braces on this line
        int openBracesOnLine = 0;
        int closeBracesOnLine = 0;
        bool inStr = false;
        for (char c : l) {
            if (c == '"') inStr = !inStr;
            if (!inStr) {
                if (c == '{') openBracesOnLine++;
                else if (c == '}') closeBracesOnLine++;
            }
        }

        // Detect start of any method (e.g. void Start(), void Update(float dt), void PrintName(), etc.)
        size_t openParen = l.find('(');
        if (openParen != std::string::npos && l.find(';') == std::string::npos && currentMethodName.empty()) {
            std::string beforeParen = l.substr(0, openParen);
            size_t nb = beforeParen.find_last_not_of(" \t");
            if (nb != std::string::npos) {
                size_t na = beforeParen.find_last_of(" \t:*&", nb);
                std::string mName = (na == std::string::npos) ? beforeParen.substr(0, nb + 1) : beforeParen.substr(na + 1, nb - na);
                if (!mName.empty() && mName != "if" && mName != "for" && mName != "while" && mName != "switch" && mName != "catch" && mName != "REGISTER_BEHAVIOUR") {
                    currentMethodName = mName;
                    currentMethodBraceDepth = openBracesOnLine - closeBracesOnLine;
                    methodState = MethodParseState();
                    if (mName == "Update") {
                        inUpdateMethod = true;
                        inStartMethod = false;
                        conditionStack.clear();
                        pendingSingleLineKeys.clear();
                        pendingSingleLineBools.clear();
                        pendingSingleLineRequireHitStatus = false;
                        pendingSingleLineRequireNoHit = false;
                        pendingSingleLineRequireHitTag.clear();
                        hasPendingSingleLineCondition = false;
                        lastIfWasHitCondition = false;
                        lastIfHitConditionNegated = false;
                        lastIfHitTag.clear();
                        updateMethodBraceDepth = currentMethodBraceDepth;
                    } else if (mName == "Start") {
                        inStartMethod = true;
                        inUpdateMethod = false;
                    } else {
                        inStartMethod = false;
                        inUpdateMethod = false;
                    }
                }
            }
        }

        if (!currentMethodName.empty() && !inUpdateMethod) {
            auto& sm = m_methods[currentMethodName];
            sm.name = currentMethodName;

            std::string lTrim = l;
            std::string lLower = lTrim;
            std::transform(lLower.begin(), lLower.end(), lLower.begin(), ::tolower);

            auto parseActionFromStr = [&](const std::string& str, DynamicScriptBehaviour::ScriptAction& outAct) -> bool {
                std::string s = str;
                size_t b = s.find_first_not_of(" \t\r\n;{}");
                size_t e = s.find_last_not_of(" \t\r\n;{}");
                if (b == std::string::npos || e == std::string::npos) return false;
                s = s.substr(b, e - b + 1);

                std::string sLow = s;
                std::transform(sLow.begin(), sLow.end(), sLow.begin(), ::tolower);

                if (sLow.find("cursorstate.unlocked") != std::string::npos || sLow.find("cursorstate.unlock") != std::string::npos) {
                    outAct.type = DynamicScriptBehaviour::ScriptStatementType::UnlockCursor;
                    sm.unlockCursor = true;
                    if (currentMethodName == "Start") m_startUnlockCursor = true;
                    return true;
                }
                if (sLow.find("cursorstate.locked") != std::string::npos || sLow.find("cursorstate.lock") != std::string::npos) {
                    outAct.type = DynamicScriptBehaviour::ScriptStatementType::LockCursor;
                    sm.lockCursor = true;
                    if (currentMethodName == "Start") m_startLockCursor = true;
                    return true;
                }
                if (sLow.find("uisystem.removeall()") != std::string::npos || sLow.find("removeallui()") != std::string::npos) {
                    outAct.type = DynamicScriptBehaviour::ScriptStatementType::RemoveAllUI;
                    sm.removeAllUI = true;
                    if (currentMethodName == "Start") m_startUIRemoveAll = true;
                    return true;
                }
                if (sLow.find("uisystem.add(") != std::string::npos || sLow.find("uisystem::add(") != std::string::npos || sLow.find("addui(") != std::string::npos) {
                    size_t p1 = s.find('(');
                    size_t p2 = s.find(')', p1);
                    if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1 + 1) {
                        std::string varName = s.substr(p1 + 1, p2 - p1 - 1);
                        size_t vn1 = varName.find_first_not_of(" \t&");
                        size_t vn2 = varName.find_last_not_of(" \t&");
                        if (vn1 != std::string::npos && vn2 != std::string::npos) {
                            outAct.type = DynamicScriptBehaviour::ScriptStatementType::AddUI;
                            outAct.target = varName.substr(vn1, vn2 - vn1 + 1);
                            sm.uiAddTargets.push_back(outAct.target);
                            if (currentMethodName == "Start") m_startUIAddTargets.push_back(outAct.target);
                            return true;
                        }
                    }
                }
                if (sLow.find("uisystem.remove(") != std::string::npos || sLow.find("uisystem::remove(") != std::string::npos || sLow.find("removeui(") != std::string::npos) {
                    size_t p1 = s.find('(');
                    size_t p2 = s.find(')', p1);
                    if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1 + 1) {
                        std::string varName = s.substr(p1 + 1, p2 - p1 - 1);
                        size_t vn1 = varName.find_first_not_of(" \t&");
                        size_t vn2 = varName.find_last_not_of(" \t&");
                        if (vn1 != std::string::npos && vn2 != std::string::npos) {
                            outAct.type = DynamicScriptBehaviour::ScriptStatementType::RemoveUI;
                            outAct.target = varName.substr(vn1, vn2 - vn1 + 1);
                            sm.uiRemoveTargets.push_back(outAct.target);
                            if (currentMethodName == "Start") m_startUIRemoveTargets.push_back(outAct.target);
                            return true;
                        }
                    }
                }
                if (sLow.find("print(") != std::string::npos) {
                    size_t q1 = s.find('"');
                    if (q1 != std::string::npos) {
                        size_t q2 = s.find('"', q1 + 1);
                        if (q2 != std::string::npos) {
                            outAct.type = DynamicScriptBehaviour::ScriptStatementType::Print;
                            outAct.target = s.substr(q1 + 1, q2 - q1 - 1);
                            sm.printMessages.push_back(outAct.target);
                            if (currentMethodName == "Start") m_startPrintMessages.push_back(outAct.target);
                            return true;
                        }
                    }
                }

                // SaveSystem calls (SaveSystem.Global.SetItem, SaveSystem.Local.SetItem, RemoveItem, RemoveAll)
                if (sLow.find("savesystem.") != std::string::npos || sLow.find("savesystem::") != std::string::npos) {
                    bool isGlobal = (sLow.find(".local") == std::string::npos && sLow.find("::local") == std::string::npos);
                    DynamicScriptBehaviour::DynamicSaveAction saveAct;
                    saveAct.scope = isGlobal ? DynamicScriptBehaviour::DynamicSaveAction::Scope::Global : DynamicScriptBehaviour::DynamicSaveAction::Scope::Local;

                    if (sLow.find("removeall()") != std::string::npos || sLow.find("removeall( )") != std::string::npos) {
                        saveAct.type = DynamicScriptBehaviour::DynamicSaveAction::ActionType::RemoveAll;
                        if (currentMethodName == "Start") m_startSaveActions.push_back(saveAct);
                        return true;
                    } else if (sLow.find("removeitem(") != std::string::npos) {
                        size_t p1 = s.find("RemoveItem(");
                        if (p1 == std::string::npos) p1 = s.find("removeitem(");
                        if (p1 == std::string::npos) p1 = s.find("Removeitem(");
                        size_t pOpen = s.find('(', p1);
                        size_t pClose = s.rfind(')');
                        if (pOpen != std::string::npos && pClose != std::string::npos && pClose > pOpen + 1) {
                            std::string arg = s.substr(pOpen + 1, pClose - pOpen - 1);
                            size_t q1 = arg.find('"');
                            if (q1 != std::string::npos) {
                                size_t q2 = arg.find('"', q1 + 1);
                                if (q2 != std::string::npos) saveAct.itemName = arg.substr(q1 + 1, q2 - q1 - 1);
                            } else {
                                size_t a1 = arg.find_first_not_of(" \t");
                                size_t a2 = arg.find_last_not_of(" \t");
                                if (a1 != std::string::npos && a2 != std::string::npos) saveAct.itemName = arg.substr(a1, a2 - a1 + 1);
                            }
                            saveAct.type = DynamicScriptBehaviour::DynamicSaveAction::ActionType::RemoveItem;
                            if (currentMethodName == "Start") m_startSaveActions.push_back(saveAct);
                            return true;
                        }
                    } else if (sLow.find("setitem(") != std::string::npos) {
                        size_t p1 = s.find("SetItem(");
                        if (p1 == std::string::npos) p1 = s.find("setitem(");
                        if (p1 == std::string::npos) p1 = s.find("Setitem(");
                        size_t pOpen = s.find('(', p1);
                        size_t pClose = s.rfind(')');
                        if (pOpen != std::string::npos && pClose != std::string::npos && pClose > pOpen + 1) {
                            std::string argsStr = s.substr(pOpen + 1, pClose - pOpen - 1);
                            std::vector<std::string> args;
                            std::string cur;
                            bool inQ = false;
                            for (char c : argsStr) {
                                if (c == '"') inQ = !inQ;
                                if (c == ',' && !inQ) {
                                    args.push_back(cur);
                                    cur.clear();
                                } else {
                                    cur += c;
                                }
                            }
                            if (!cur.empty()) args.push_back(cur);

                            if (args.size() >= 2) {
                                std::string itemArg = args[0];
                                std::string valArg = args[1];

                                size_t q1 = itemArg.find('"');
                                if (q1 != std::string::npos) {
                                    size_t q2 = itemArg.find('"', q1 + 1);
                                    if (q2 != std::string::npos) saveAct.itemName = itemArg.substr(q1 + 1, q2 - q1 - 1);
                                } else {
                                    size_t a1 = itemArg.find_first_not_of(" \t");
                                    size_t a2 = itemArg.find_last_not_of(" \t");
                                    if (a1 != std::string::npos && a2 != std::string::npos) saveAct.itemName = itemArg.substr(a1, a2 - a1 + 1);
                                }

                                size_t vq1 = valArg.find('"');
                                if (vq1 != std::string::npos) {
                                    size_t vq2 = valArg.find('"', vq1 + 1);
                                    if (vq2 != std::string::npos) {
                                        saveAct.valueStr = valArg.substr(vq1 + 1, vq2 - vq1 - 1);
                                        saveAct.isConstString = true;
                                    }
                                } else {
                                    size_t va1 = valArg.find_first_not_of(" \t");
                                    size_t va2 = valArg.find_last_not_of(" \t");
                                    if (va1 != std::string::npos && va2 != std::string::npos) {
                                        std::string rawVal = valArg.substr(va1, va2 - va1 + 1);
                                        saveAct.valueStr = rawVal;
                                        saveAct.varName = rawVal;
                                    }
                                }
                                saveAct.type = DynamicScriptBehaviour::DynamicSaveAction::ActionType::SetItem;
                                if (currentMethodName == "Start") m_startSaveActions.push_back(saveAct);
                                return true;
                            }
                        }
                    }
                }

                // Timer functions (StartTimer, StartInterval, SetTimer, SetInterval)
                if (sLow.find("starttimer(") != std::string::npos || sLow.find("startinterval(") != std::string::npos ||
                    sLow.find("settimer(") != std::string::npos || sLow.find("setinterval(") != std::string::npos) {
                    bool isInterval = (sLow.find("interval") != std::string::npos);
                    size_t p1 = s.find('(');
                    size_t p2 = s.rfind(')');
                    if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1 + 1) {
                        std::string argsStr = s.substr(p1 + 1, p2 - p1 - 1);
                        size_t comma = argsStr.find(',');
                        if (comma != std::string::npos) {
                            std::string funcArg = argsStr.substr(0, comma);
                            std::string timeArg = argsStr.substr(comma + 1);

                            size_t fa1 = funcArg.find_first_not_of(" \t&'\"");
                            size_t fa2 = funcArg.find_last_not_of(" \t'\"()");
                            std::string cleanFunc = (fa1 != std::string::npos && fa2 != std::string::npos) ? funcArg.substr(fa1, fa2 - fa1 + 1) : "";
                            size_t colons = cleanFunc.rfind("::");
                            if (colons != std::string::npos) cleanFunc = cleanFunc.substr(colons + 2);
                            size_t arrow = cleanFunc.rfind("->");
                            if (arrow != std::string::npos) cleanFunc = cleanFunc.substr(arrow + 2);
                            size_t dot = cleanFunc.rfind(".");
                            if (dot != std::string::npos) cleanFunc = cleanFunc.substr(dot + 1);

                            size_t ta1 = timeArg.find_first_not_of(" \t");
                            size_t ta2 = timeArg.find_last_not_of(" \t");
                            std::string cleanTime = (ta1 != std::string::npos && ta2 != std::string::npos) ? timeArg.substr(ta1, ta2 - ta1 + 1) : "";

                            float seconds = 0.0f;
                            try {
                                std::string numClean = cleanTime;
                                if (!numClean.empty() && (numClean.back() == 'f' || numClean.back() == 'F')) numClean.pop_back();
                                seconds = std::stof(numClean);
                            } catch (...) {
                                seconds = 0.0f;
                            }

                            outAct.type = isInterval ? DynamicScriptBehaviour::ScriptStatementType::StartInterval : DynamicScriptBehaviour::ScriptStatementType::StartTimer;
                            outAct.target = cleanFunc;
                            outAct.valueStr = cleanTime;
                            outAct.duration = seconds;
                            return true;
                        }
                    }
                }

                if (sLow.find("clearalltimers()") != std::string::npos) {
                    outAct.type = DynamicScriptBehaviour::ScriptStatementType::ClearAllTimers;
                    return true;
                }

                if (sLow.find("stoptimer(") != std::string::npos || sLow.find("cleartimer(") != std::string::npos ||
                    sLow.find("stopinterval(") != std::string::npos || sLow.find("clearinterval(") != std::string::npos) {
                    size_t p1 = s.find('(');
                    size_t p2 = s.rfind(')');
                    if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1 + 1) {
                        std::string arg = s.substr(p1 + 1, p2 - p1 - 1);
                        size_t a1 = arg.find_first_not_of(" \t&'\"");
                        size_t a2 = arg.find_last_not_of(" \t'\"()");
                        outAct.type = DynamicScriptBehaviour::ScriptStatementType::StopTimer;
                        outAct.target = (a1 != std::string::npos && a2 != std::string::npos) ? arg.substr(a1, a2 - a1 + 1) : "";
                        return true;
                    }
                }

                // Variable assignment or toggle (e.g. CanMove = false; CanMove = !CanMove;)
                size_t eqPos = s.find('=');
                if (eqPos != std::string::npos && s.find("==") == std::string::npos &&
                    s.find("!=") == std::string::npos && s.find("<=") == std::string::npos &&
                    s.find(">=") == std::string::npos) {
                    std::string left = s.substr(0, eqPos);
                    std::string right = s.substr(eqPos + 1);
                    size_t semi = right.find(';');
                    if (semi != std::string::npos) right = right.substr(0, semi);

                    size_t lb = left.find_last_not_of(" \t\r\n");
                    size_t la = left.find_first_not_of(" \t\r\n");
                    if (lb != std::string::npos && la != std::string::npos) {
                        size_t typeSpace = left.find_last_of(" \t*&", lb);
                        std::string varName = (typeSpace == std::string::npos) ? left.substr(la, lb - la + 1) : left.substr(typeSpace + 1, lb - typeSpace);
                        size_t rb = right.find_last_not_of(" \t\r\n");
                        size_t ra = right.find_first_not_of(" \t\r\n");
                        std::string valStr = (rb != std::string::npos && ra != std::string::npos) ? right.substr(ra, rb - ra + 1) : "";

                        if (!varName.empty() && !valStr.empty()) {
                            if (valStr.rfind("!" + varName, 0) == 0 || valStr.rfind("! " + varName, 0) == 0 || valStr.rfind("!", 0) == 0) {
                                outAct.type = DynamicScriptBehaviour::ScriptStatementType::Toggle;
                                outAct.varName = varName;
                                outAct.valueStr = valStr;
                            } else {
                                outAct.type = DynamicScriptBehaviour::ScriptStatementType::Assignment;
                                outAct.varName = varName;
                                outAct.valueStr = valStr;
                            }
                            return true;
                        }
                    }
                }

                // Method call (e.g. PrintName();)
                size_t cp = s.find("()");
                if (cp == std::string::npos) cp = s.find("( )");
                if (cp != std::string::npos && s.find("void ") == std::string::npos && s.find("bool ") == std::string::npos && s.find("int ") == std::string::npos && s.find("float ") == std::string::npos) {
                    std::string beforeCall = s.substr(0, cp);
                    size_t cb = beforeCall.find_last_not_of(" \t");
                    if (cb != std::string::npos) {
                        size_t ca = beforeCall.find_last_of(" \t;{}.", cb);
                        std::string calledName = (ca == std::string::npos) ? beforeCall.substr(0, cb + 1) : beforeCall.substr(ca + 1, cb - ca);
                        if (!calledName.empty() && calledName != currentMethodName &&
                            calledName != "if" && calledName != "for" && calledName != "while" &&
                            calledName != "Print" && calledName != "Locked" && calledName != "UnLocked" &&
                            calledName != "RemoveAll") {
                            outAct.type = DynamicScriptBehaviour::ScriptStatementType::CallMethod;
                            outAct.target = calledName;
                            sm.calledMethods.push_back(calledName);
                            return true;
                        }
                    }
                }
                return false;
            };

            auto addAction = [&](const DynamicScriptBehaviour::ScriptAction& act) {
                if (methodState.inIf) {
                    methodState.currentBranch.thenActions.push_back(act);
                    if (methodState.singleLineIf && methodState.ifBraceDepth <= 0) {
                        methodState.inIf = false;
                        methodState.singleLineIf = false;
                    }
                } else if (methodState.inElse) {
                    methodState.currentBranch.elseActions.push_back(act);
                    if (methodState.singleLineElse && methodState.elseBraceDepth <= 0) {
                        methodState.inElse = false;
                        methodState.singleLineElse = false;
                    }
                } else {
                    if (methodState.currentBranch.hasCondition) {
                        sm.branches.push_back(methodState.currentBranch);
                        methodState.currentBranch = DynamicScriptBehaviour::ScriptBranch();
                    }
                    methodState.currentBranch.thenActions.push_back(act);
                }
            };

            // Detect if-statements: if (condition)
            size_t ifPos = lTrim.find("if (");
            if (ifPos == std::string::npos) ifPos = lTrim.find("if(");
            if (ifPos != std::string::npos && (ifPos == 0 || lTrim[ifPos - 1] == ' ' || lTrim[ifPos - 1] == '\t' || lTrim[ifPos - 1] == '}' || lTrim[ifPos - 1] == ';')) {
                size_t pOpen = lTrim.find('(', ifPos);
                size_t pClose = lTrim.find(')', pOpen);
                if (pOpen != std::string::npos && pClose != std::string::npos && pClose > pOpen + 1) {
                    std::string condRaw = lTrim.substr(pOpen + 1, pClose - pOpen - 1);
                    std::string condVar;
                    bool expectedBool = true;

                    size_t cb = condRaw.find_first_not_of(" \t");
                    size_t ce = condRaw.find_last_not_of(" \t");
                    if (cb != std::string::npos && ce != std::string::npos) {
                        std::string cClean = condRaw.substr(cb, ce - cb + 1);
                        if (cClean.rfind("!", 0) == 0) {
                            expectedBool = false;
                            size_t vb = cClean.find_first_not_of(" \t!", 0);
                            if (vb != std::string::npos) condVar = cClean.substr(vb);
                        } else {
                            size_t eqEq = cClean.find("==");
                            if (eqEq != std::string::npos) {
                                std::string leftSide = cClean.substr(0, eqEq);
                                std::string rightSide = cClean.substr(eqEq + 2);
                                size_t lb = leftSide.find_first_not_of(" \t");
                                size_t le = leftSide.find_last_not_of(" \t");
                                if (lb != std::string::npos && le != std::string::npos) condVar = leftSide.substr(lb, le - lb + 1);

                                std::string rLower = rightSide;
                                std::transform(rLower.begin(), rLower.end(), rLower.begin(), ::tolower);
                                if (rLower.find("false") != std::string::npos || rLower.find("0") != std::string::npos) {
                                    expectedBool = false;
                                } else {
                                    expectedBool = true;
                                }
                            } else {
                                condVar = cClean;
                                expectedBool = true;
                            }
                        }
                    }

                    if (!condVar.empty()) {
                        if (methodState.currentBranch.hasCondition || !methodState.currentBranch.thenActions.empty()) {
                            sm.branches.push_back(methodState.currentBranch);
                            methodState.currentBranch = DynamicScriptBehaviour::ScriptBranch();
                        }
                        methodState.currentBranch.hasCondition = true;
                        methodState.currentBranch.conditionVar = condVar;
                        methodState.currentBranch.expectedBool = expectedBool;
                        methodState.inIf = true;
                        methodState.inElse = false;

                        std::string afterParen = lTrim.substr(pClose + 1);
                        if (afterParen.find('{') != std::string::npos) {
                            methodState.ifBraceDepth = 1;
                            methodState.singleLineIf = false;
                        } else {
                            DynamicScriptBehaviour::ScriptAction act;
                            if (parseActionFromStr(afterParen, act)) {
                                addAction(act);
                            } else {
                                methodState.singleLineIf = true;
                            }
                        }
                    }
                }
            } else if (lTrim.find("else") != std::string::npos && (lTrim.rfind("else", 0) == 0 || lTrim.find(" else") != std::string::npos || lTrim.find("}else") != std::string::npos || lTrim.find("} else") != std::string::npos)) {
                methodState.inIf = false;
                methodState.inElse = true;
                size_t elsePos = lTrim.find("else");
                std::string afterElse = lTrim.substr(elsePos + 4);
                if (afterElse.find('{') != std::string::npos) {
                    methodState.elseBraceDepth = 1;
                    methodState.singleLineElse = false;
                } else {
                    DynamicScriptBehaviour::ScriptAction act;
                    if (parseActionFromStr(afterElse, act)) {
                        addAction(act);
                    } else {
                        methodState.singleLineElse = true;
                    }
                }
            } else {
                DynamicScriptBehaviour::ScriptAction act;
                if (parseActionFromStr(lTrim, act)) {
                    addAction(act);
                }
            }

            if (methodState.ifBraceDepth > 0) {
                methodState.ifBraceDepth += openBracesOnLine - closeBracesOnLine;
                if (methodState.ifBraceDepth <= 0) {
                    methodState.inIf = false;
                    methodState.ifBraceDepth = 0;
                }
            }
            if (methodState.elseBraceDepth > 0) {
                methodState.elseBraceDepth += openBracesOnLine - closeBracesOnLine;
                if (methodState.elseBraceDepth <= 0) {
                    methodState.inElse = false;
                    methodState.elseBraceDepth = 0;
                }
            }

            if (l.find('{') == std::string::npos && currentMethodBraceDepth <= 0 && l.find('}') != std::string::npos) {
                if (methodState.currentBranch.hasCondition || !methodState.currentBranch.thenActions.empty() || !methodState.currentBranch.elseActions.empty()) {
                    sm.branches.push_back(methodState.currentBranch);
                    methodState.currentBranch = DynamicScriptBehaviour::ScriptBranch();
                }
                currentMethodName.clear();
                inStartMethod = false;
            } else if (l.find('{') != std::string::npos || currentMethodBraceDepth > 0) {
                currentMethodBraceDepth += openBracesOnLine - closeBracesOnLine;
                if (currentMethodBraceDepth <= 0) {
                    if (methodState.currentBranch.hasCondition || !methodState.currentBranch.thenActions.empty() || !methodState.currentBranch.elseActions.empty()) {
                        sm.branches.push_back(methodState.currentBranch);
                        methodState.currentBranch = DynamicScriptBehaviour::ScriptBranch();
                    }
                    currentMethodName.clear();
                    inStartMethod = false;
                }
            }
        }

        if (inUpdateMethod) {
            int openBracesOnLine = 0;
            int closeBracesOnLine = 0;
            for (char c : l) {
                if (c == '{') openBracesOnLine++;
                else if (c == '}') closeBracesOnLine++;
            }

            if (openBracesOnLine > 0 && hasPendingSingleLineCondition) {
                ConditionScope scope;
                scope.braceDepth = updateMethodBraceDepth + openBracesOnLine;
                scope.keys = pendingSingleLineKeys;
                scope.bools = pendingSingleLineBools;
                scope.requireHitStatus = pendingSingleLineRequireHitStatus;
                scope.requireNoHit = pendingSingleLineRequireNoHit;
                scope.requireHitTag = pendingSingleLineRequireHitTag;
                conditionStack.push_back(scope);
                pendingSingleLineKeys.clear();
                pendingSingleLineBools.clear();
                pendingSingleLineRequireHitStatus = false;
                pendingSingleLineRequireNoHit = false;
                pendingSingleLineRequireHitTag.clear();
                hasPendingSingleLineCondition = false;
            }

            updateMethodBraceDepth += openBracesOnLine;

            // Check for local variable declaration / assignment inside Update()
            size_t eqPos = l.find('=');
            if (eqPos != std::string::npos && l.find("==") == std::string::npos &&
                l.find("!=") == std::string::npos && l.find("<=") == std::string::npos &&
                l.find(">=") == std::string::npos && l.find("if ") == std::string::npos &&
                l.find("if(") == std::string::npos) {
                std::string left = l.substr(0, eqPos);
                std::string right = l.substr(eqPos + 1);
                size_t semi = right.find(';');
                if (semi != std::string::npos) right = right.substr(0, semi);

                size_t lb = left.find_last_not_of(" \t\r\n");
                if (lb != std::string::npos) {
                    size_t la = left.find_last_of(" \t\r\n*", lb);
                    std::string varName = (la == std::string::npos) ? left.substr(0, lb + 1) : left.substr(la + 1, lb - la);
                    LocalVarExpr expr;
                    parseArgExpr(right, expr.constantVal, expr.useDt, expr.varName, expr.varMultiplier, expr.scaleVarName, expr.isActive);
                    localVars[varName] = expr;
                }
            }

            std::string lLower = l;
            std::transform(lLower.begin(), lLower.end(), lLower.begin(), ::tolower);

            // Check if this line introduces an if (...) with key or bool checks, Hit.Status, or HasTag
            size_t ifPos = l.find("if (");
            if (ifPos == std::string::npos) ifPos = l.find("if(");
            if (ifPos != std::string::npos && (ifPos == 0 || l[ifPos - 1] == ' ' || l[ifPos - 1] == '\t' || l[ifPos - 1] == '}' || l[ifPos - 1] == ';')) {
                auto lineKeys = extractKeyConditions(l);
                auto lineBools = extractBoolConditions(l);
                std::string hitTag;
                size_t htPos = l.find("HasTag(");
                if (htPos == std::string::npos) htPos = l.find("hasTag(");
                if (htPos == std::string::npos) htPos = l.find("hastag(");
                if (htPos != std::string::npos) {
                    size_t q1 = l.find('"', htPos);
                    if (q1 != std::string::npos) {
                        size_t q2 = l.find('"', q1 + 1);
                        if (q2 != std::string::npos) {
                            hitTag = l.substr(q1 + 1, q2 - q1 - 1);
                        }
                    }
                }
                bool isHitCheck = (lLower.find("hit.status") != std::string::npos || lLower.find("hit.gameobject") != std::string::npos || !hitTag.empty());
                bool isNegatedHit = (lLower.find("!hit.status") != std::string::npos || lLower.find("hit.status == false") != std::string::npos || lLower.find("!hit.gameobject") != std::string::npos);

                if (!lineKeys.empty() || !lineBools.empty() || isHitCheck) {
                    if (isHitCheck) {
                        lastIfWasHitCondition = true;
                        lastIfHitConditionNegated = isNegatedHit;
                        lastIfHitTag = hitTag;
                    } else {
                        lastIfWasHitCondition = false;
                        lastIfHitTag.clear();
                    }

                    if (openBracesOnLine > 0) {
                        ConditionScope scope;
                        scope.braceDepth = updateMethodBraceDepth;
                        scope.keys = lineKeys;
                        scope.bools = lineBools;
                        if (isHitCheck) {
                            if (isNegatedHit) scope.requireNoHit = true;
                            else {
                                scope.requireHitStatus = true;
                                scope.requireHitTag = hitTag;
                            }
                        }
                        conditionStack.push_back(scope);
                    } else {
                        pendingSingleLineKeys.insert(pendingSingleLineKeys.end(), lineKeys.begin(), lineKeys.end());
                        pendingSingleLineBools.insert(pendingSingleLineBools.end(), lineBools.begin(), lineBools.end());
                        if (isHitCheck) {
                            if (isNegatedHit) pendingSingleLineRequireNoHit = true;
                            else {
                                pendingSingleLineRequireHitStatus = true;
                                pendingSingleLineRequireHitTag = hitTag;
                            }
                        }
                        hasPendingSingleLineCondition = true;
                    }
                }
            } else if (lLower.find("else") != std::string::npos && lLower.find("else if") == std::string::npos && lLower.find("else  if") == std::string::npos) {
                size_t elsePos = lLower.find("else");
                if (elsePos == 0 || l[elsePos - 1] == ' ' || l[elsePos - 1] == '\t' || l[elsePos - 1] == '}' || l[elsePos - 1] == ';') {
                    if (lastIfWasHitCondition) {
                        if (openBracesOnLine > 0) {
                            ConditionScope scope;
                            scope.braceDepth = updateMethodBraceDepth;
                            if (lastIfHitConditionNegated) {
                                scope.requireHitStatus = true;
                                scope.requireHitTag = lastIfHitTag;
                            } else {
                                scope.requireNoHit = true;
                                scope.requireHitTag = lastIfHitTag;
                            }
                            conditionStack.push_back(scope);
                        } else {
                            if (lastIfHitConditionNegated) {
                                pendingSingleLineRequireHitStatus = true;
                                pendingSingleLineRequireHitTag = lastIfHitTag;
                            } else {
                                pendingSingleLineRequireNoHit = true;
                                pendingSingleLineRequireHitTag = lastIfHitTag;
                            }
                            hasPendingSingleLineCondition = true;
                        }
                    }
                }
            }

            auto applyConditionsToAction = [&](DynamicTransformAction& act) {
                for (const auto& sc : conditionStack) {
                    act.requiredKeys.insert(act.requiredKeys.end(), sc.keys.begin(), sc.keys.end());
                    act.requiredBools.insert(act.requiredBools.end(), sc.bools.begin(), sc.bools.end());
                }
                act.requiredKeys.insert(act.requiredKeys.end(), pendingSingleLineKeys.begin(), pendingSingleLineKeys.end());
                act.requiredBools.insert(act.requiredBools.end(), pendingSingleLineBools.begin(), pendingSingleLineBools.end());
                if (hasPendingSingleLineCondition) {
                    pendingSingleLineKeys.clear();
                    pendingSingleLineBools.clear();
                    pendingSingleLineRequireHitStatus = false;
                    pendingSingleLineRequireNoHit = false;
                    pendingSingleLineRequireHitTag.clear();
                    hasPendingSingleLineCondition = false;
                }
            };

            auto applyConditionsToUIAction = [&](DynamicScriptBehaviour::DynamicUIAction& act) {
                for (const auto& sc : conditionStack) {
                    act.requiredKeys.insert(act.requiredKeys.end(), sc.keys.begin(), sc.keys.end());
                    act.requiredBools.insert(act.requiredBools.end(), sc.bools.begin(), sc.bools.end());
                    if (sc.requireHitStatus) act.requireHitStatus = true;
                    if (sc.requireNoHit) act.requireNoHit = true;
                    if (!sc.requireHitTag.empty()) act.requireHitTag = sc.requireHitTag;
                }
                act.requiredKeys.insert(act.requiredKeys.end(), pendingSingleLineKeys.begin(), pendingSingleLineKeys.end());
                act.requiredBools.insert(act.requiredBools.end(), pendingSingleLineBools.begin(), pendingSingleLineBools.end());
                if (pendingSingleLineRequireHitStatus) act.requireHitStatus = true;
                if (pendingSingleLineRequireNoHit) act.requireNoHit = true;
                if (!pendingSingleLineRequireHitTag.empty()) act.requireHitTag = pendingSingleLineRequireHitTag;
                if (hasPendingSingleLineCondition) {
                    pendingSingleLineKeys.clear();
                    pendingSingleLineBools.clear();
                    pendingSingleLineRequireHitStatus = false;
                    pendingSingleLineRequireNoHit = false;
                    pendingSingleLineRequireHitTag.clear();
                    hasPendingSingleLineCondition = false;
                }
            };

            auto applyConditionsToPrint = [&](DynamicScriptBehaviour::DynamicUpdatePrint& item) {
                for (const auto& sc : conditionStack) {
                    item.requiredKeys.insert(item.requiredKeys.end(), sc.keys.begin(), sc.keys.end());
                    item.requiredBools.insert(item.requiredBools.end(), sc.bools.begin(), sc.bools.end());
                    if (sc.requireHitStatus) item.requireHitStatus = true;
                    if (sc.requireNoHit) item.requireNoHit = true;
                    if (!sc.requireHitTag.empty()) item.requireHitTag = sc.requireHitTag;
                }
                item.requiredKeys.insert(item.requiredKeys.end(), pendingSingleLineKeys.begin(), pendingSingleLineKeys.end());
                item.requiredBools.insert(item.requiredBools.end(), pendingSingleLineBools.begin(), pendingSingleLineBools.end());
                if (pendingSingleLineRequireHitStatus) item.requireHitStatus = true;
                if (pendingSingleLineRequireNoHit) item.requireNoHit = true;
                if (!pendingSingleLineRequireHitTag.empty()) item.requireHitTag = pendingSingleLineRequireHitTag;
                if (hasPendingSingleLineCondition) {
                    pendingSingleLineKeys.clear();
                    pendingSingleLineBools.clear();
                    pendingSingleLineRequireHitStatus = false;
                    pendingSingleLineRequireNoHit = false;
                    pendingSingleLineRequireHitTag.clear();
                    hasPendingSingleLineCondition = false;
                }
            };

            auto applyConditionsToDestroy = [&](DynamicDestroyAction& act) {
                for (const auto& sc : conditionStack) {
                    act.requiredKeys.insert(act.requiredKeys.end(), sc.keys.begin(), sc.keys.end());
                    act.requiredBools.insert(act.requiredBools.end(), sc.bools.begin(), sc.bools.end());
                    if (sc.requireHitStatus) act.requireHitStatus = true;
                    if (sc.requireNoHit) act.requireNoHit = true;
                    if (!sc.requireHitTag.empty()) act.requireHitTag = sc.requireHitTag;
                }
                act.requiredKeys.insert(act.requiredKeys.end(), pendingSingleLineKeys.begin(), pendingSingleLineKeys.end());
                act.requiredBools.insert(act.requiredBools.end(), pendingSingleLineBools.begin(), pendingSingleLineBools.end());
                if (pendingSingleLineRequireHitStatus) act.requireHitStatus = true;
                if (pendingSingleLineRequireNoHit) act.requireNoHit = true;
                if (!pendingSingleLineRequireHitTag.empty()) act.requireHitTag = pendingSingleLineRequireHitTag;
                if (hasPendingSingleLineCondition) {
                    pendingSingleLineKeys.clear();
                    pendingSingleLineBools.clear();
                    pendingSingleLineRequireHitStatus = false;
                    pendingSingleLineRequireNoHit = false;
                    pendingSingleLineRequireHitTag.clear();
                    hasPendingSingleLineCondition = false;
                }
            };

            auto applyConditionsToSaveAction = [&](DynamicSaveAction& act) {
                for (const auto& sc : conditionStack) {
                    act.requiredKeys.insert(act.requiredKeys.end(), sc.keys.begin(), sc.keys.end());
                    act.requiredBools.insert(act.requiredBools.end(), sc.bools.begin(), sc.bools.end());
                    if (sc.requireHitStatus) act.requireHitStatus = true;
                    if (sc.requireNoHit) act.requireNoHit = true;
                    if (!sc.requireHitTag.empty()) act.requireHitTag = sc.requireHitTag;
                }
                act.requiredKeys.insert(act.requiredKeys.end(), pendingSingleLineKeys.begin(), pendingSingleLineKeys.end());
                act.requiredBools.insert(act.requiredBools.end(), pendingSingleLineBools.begin(), pendingSingleLineBools.end());
                if (pendingSingleLineRequireHitStatus) act.requireHitStatus = true;
                if (pendingSingleLineRequireNoHit) act.requireNoHit = true;
                if (!pendingSingleLineRequireHitTag.empty()) act.requireHitTag = pendingSingleLineRequireHitTag;
                if (hasPendingSingleLineCondition) {
                    pendingSingleLineKeys.clear();
                    pendingSingleLineBools.clear();
                    pendingSingleLineRequireHitStatus = false;
                    pendingSingleLineRequireNoHit = false;
                    pendingSingleLineRequireHitTag.clear();
                    hasPendingSingleLineCondition = false;
                }
            };

            // 1. Scan for Print statements
            size_t prPos = l.find("Print(");
            if (prPos != std::string::npos) {
                size_t openP = l.find('(', prPos);
                size_t closeP = l.rfind(')');
                if (openP != std::string::npos && closeP != std::string::npos && closeP > openP) {
                    std::string inner = l.substr(openP + 1, closeP - openP - 1);
                    std::string lowerInner = inner;
                    std::transform(lowerInner.begin(), lowerInner.end(), lowerInner.begin(), ::tolower);

                    DynamicScriptBehaviour::DynamicUpdatePrint item;
                    applyConditionsToPrint(item);

                    if (lowerInner.find("deltatime") != std::string::npos ||
                        lowerInner.find("delta_time") != std::string::npos ||
                        lowerInner.find("worlddeltaseconds") != std::string::npos ||
                        lowerInner.find("dt") != std::string::npos) {
                        item.appendDeltaTime = true;
                    }

                    if (lowerInner.find("hit.gameobject.name") != std::string::npos ||
                        lowerInner.find("gameobject.name") != std::string::npos ||
                        (lowerInner.find("hit") != std::string::npos && lowerInner.find(".name") != std::string::npos)) {
                        item.appendHitName = true;
                    } else if (lowerInner.find("hit.gameobject.objectid") != std::string::npos ||
                               lowerInner.find("hit.objectid") != std::string::npos ||
                               (lowerInner.find("hit") != std::string::npos && lowerInner.find(".objectid") != std::string::npos) ||
                               (lowerInner.find("hit") != std::string::npos && lowerInner.find(".id") != std::string::npos)) {
                        item.appendHitId = true;
                    } else if (lowerInner.find("hit.point") != std::string::npos ||
                               lowerInner.find("hit.location") != std::string::npos ||
                               lowerInner.find("hit.position") != std::string::npos) {
                        item.appendHitPoint = true;
                    } else if (lowerInner.find("hit.status") != std::string::npos) {
                        item.appendHitStatus = true;
                    } else if (lowerInner.find("hit.distance") != std::string::npos) {
                        item.appendHitDistance = true;
                    } else if (lowerInner.find("hit.normal") != std::string::npos) {
                        item.appendHitNormal = true;
                    } else if (lowerInner.find("hit.gameobject.gettags") != std::string::npos ||
                               lowerInner.find("hit.gameobject.tags") != std::string::npos ||
                               lowerInner.find("gameobject.gettags") != std::string::npos ||
                               lowerInner.find("hit.gettags") != std::string::npos ||
                               lowerInner.find("hit.tags") != std::string::npos) {
                        item.appendHitTags = true;
                    } else if (lowerInner.find("hit.gameobject") != std::string::npos ||
                               lowerInner.find("hit.actor") != std::string::npos) {
                        item.appendHitName = true;
                    }

                    if (lowerInner.find("savesystem.") != std::string::npos || lowerInner.find("savesystem::") != std::string::npos) {
                        bool isGlobal = (lowerInner.find(".local") == std::string::npos && lowerInner.find("::local") == std::string::npos);
                        item.saveItemIsGlobal = isGlobal;
                        size_t giPos = lowerInner.find("getitem(");
                        if (giPos != std::string::npos) {
                            size_t p1 = inner.find('(', giPos);
                            size_t p2 = inner.find(')', p1);
                            if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1 + 1) {
                                std::string arg = inner.substr(p1 + 1, p2 - p1 - 1);
                                size_t q1 = arg.find('"');
                                if (q1 != std::string::npos) {
                                    size_t q2 = arg.find('"', q1 + 1);
                                    if (q2 != std::string::npos) item.saveItemName = arg.substr(q1 + 1, q2 - q1 - 1);
                                } else {
                                    size_t a1 = arg.find_first_not_of(" \t");
                                    size_t a2 = arg.find_last_not_of(" \t");
                                    if (a1 != std::string::npos && a2 != std::string::npos) item.saveItemName = arg.substr(a1, a2 - a1 + 1);
                                }
                            }
                        }
                    }

                    if (lowerInner.find("position") != std::string::npos || lowerInner.find("location") != std::string::npos) {
                        if (!item.appendHitPoint) item.appendPosition = true;
                    }
                    if (lowerInner.find("rotation") != std::string::npos) {
                        item.appendRotation = true;
                    }

                    for (const auto& v : declaredVars) {
                        std::string vLow = v.first;
                        std::transform(vLow.begin(), vLow.end(), vLow.begin(), ::tolower);
                        if (lowerInner.find(vLow) != std::string::npos) {
                            item.varName = v.first;
                            break;
                        }
                    }

                    size_t q1 = inner.find('"');
                    if (q1 != std::string::npos) {
                        size_t q2 = inner.find('"', q1 + 1);
                        if (q2 != std::string::npos) {
                            item.textPrefix = inner.substr(q1 + 1, q2 - q1 - 1);
                            size_t q3 = inner.find('"', q2 + 1);
                            if (q3 != std::string::npos) {
                                size_t q4 = inner.find('"', q3 + 1);
                                if (q4 != std::string::npos) {
                                    item.textSuffix = inner.substr(q3 + 1, q4 - q3 - 1);
                                }
                            }
                        } else {
                            item.textPrefix = inner.substr(q1 + 1);
                        }
                    } else {
                        if (item.appendDeltaTime) item.textPrefix = "DeltaTime: ";
                        else if (item.appendHitName || item.appendHitId || item.appendHitPoint || item.appendHitStatus || item.appendHitDistance || item.appendHitNormal || item.appendHitTags) item.textPrefix = "";
                        else if (!item.saveItemName.empty()) item.textPrefix = "";
                        else if (!item.varName.empty()) item.textPrefix = item.varName + ": ";
                        else item.textPrefix = inner;
                    }

                    item.printKey = (int)m_updatePrintMessages.size() + 100;
                    m_updatePrintMessages.push_back(item);
                }
            }

            // 1b. Scan for Destroy statements (e.g. Hit.GameObject.Destroy(), GameObject.Destroy(), Destroy(Hit.GameObject), etc.)
            if (lLower.find(".destroy()") != std::string::npos || lLower.find("->destroy()") != std::string::npos ||
                lLower.find("gameobject.destroy(") != std::string::npos || lLower.find("gameobject::destroy(") != std::string::npos ||
                lLower.find("destroy(") != std::string::npos || lLower.find("destroy();") != std::string::npos) {
                DynamicDestroyAction act;
                act.enabled = true;
                if (lLower.find("hit") != std::string::npos && (lLower.find("gameobject") != std::string::npos || lLower.find(".destroy") != std::string::npos)) {
                    act.targetIsHitObject = true;
                } else if (lLower.find("gameobject.destroy()") != std::string::npos || lLower.find("this->destroy()") != std::string::npos || lLower.find("destroy()") != std::string::npos) {
                    act.targetIsHitObject = false;
                    act.targetVar = "this";
                } else {
                    size_t dPos = lLower.find("destroy(");
                    if (dPos != std::string::npos) {
                        size_t p1 = l.find('(', dPos);
                        size_t p2 = l.rfind(')');
                        if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1 + 1) {
                            std::string arg = l.substr(p1 + 1, p2 - p1 - 1);
                            std::string aLow = arg;
                            std::transform(aLow.begin(), aLow.end(), aLow.begin(), ::tolower);
                            if (aLow.find("hit") != std::string::npos) {
                                act.targetIsHitObject = true;
                            } else {
                                size_t a1 = arg.find_first_not_of(" \t&*");
                                size_t a2 = arg.find_last_not_of(" \t&*");
                                if (a1 != std::string::npos && a2 != std::string::npos) {
                                    act.targetVar = arg.substr(a1, a2 - a1 + 1);
                                }
                            }
                        }
                    }
                }
                applyConditionsToDestroy(act);
                m_destroyActions.push_back(act);
            }

            // 2. Scan for Transform.Translate / RelativeTranslate / WorldTranslate
            size_t transPos = l.find("Translate(");
            if (transPos != std::string::npos) {
                DynamicTransformAction action;
                action.type = (l.find("WorldTranslate(") != std::string::npos)
                              ? DynamicTransformAction::ActionType::TranslateWorld
                              : DynamicTransformAction::ActionType::TranslateRelative;

                size_t arrowPos = l.find("->Transform.");
                if (arrowPos == std::string::npos) arrowPos = l.find("->transform.");
                if (arrowPos == std::string::npos) arrowPos = l.find(".Transform.");
                if (arrowPos == std::string::npos) arrowPos = l.find(".transform.");
                if (arrowPos != std::string::npos) {
                    std::string ref = l.substr(0, arrowPos);
                    size_t rb = ref.find_last_not_of(" \t\r\n;{}()");
                    if (rb != std::string::npos) {
                        size_t ra = ref.find_last_of(" \t\r\n;{}()", rb);
                        action.targetRefName = (ra == std::string::npos) ? ref.substr(0, rb + 1) : ref.substr(ra + 1, rb - ra);
                    }
                }

                size_t openP = l.find('(', transPos);
                size_t closeP = l.rfind(')');
                if (openP != std::string::npos && closeP != std::string::npos && closeP > openP) {
                    std::string argsStr = l.substr(openP + 1, closeP - openP - 1);
                    std::vector<std::string> args;
                    std::string cur;
                    int parenDepth = 0;
                    for (char c : argsStr) {
                        if (c == '(') parenDepth++;
                        else if (c == ')') parenDepth--;
                        else if (c == ',' && parenDepth == 0) {
                            args.push_back(cur);
                            cur.clear();
                            continue;
                        }
                        cur += c;
                    }
                    if (!cur.empty()) args.push_back(cur);

                    if (args.size() == 3) {
                        for (int i = 0; i < 3; ++i) {
                            parseArgExpr(args[i], action.constantDelta[i], action.useDeltaTime[i], action.varNames[i], action.varMultiplier[i], action.scaleVarNames[i], action.activeAxis[i]);
                        }
                        applyConditionsToAction(action);
                        m_transformActions.push_back(action);
                    }
                }
            }

            // 3. Scan for Transform.Rotate / RelativeRotate / WorldRotate / RelativeRotation / WorldRotation
            size_t rotPos = l.find("Rotate(");
            if (rotPos == std::string::npos) rotPos = l.find("Rotation(");
            if (rotPos != std::string::npos && l.find("AutoRotate") == std::string::npos) {
                DynamicTransformAction action;
                action.type = (l.find("WorldRotate(") != std::string::npos || l.find("WorldRotation(") != std::string::npos)
                              ? DynamicTransformAction::ActionType::RotateWorld
                              : DynamicTransformAction::ActionType::RotateRelative;

                size_t arrowPos = l.find("->Transform.");
                if (arrowPos == std::string::npos) arrowPos = l.find("->transform.");
                if (arrowPos == std::string::npos) arrowPos = l.find(".Transform.");
                if (arrowPos == std::string::npos) arrowPos = l.find(".transform.");
                if (arrowPos != std::string::npos) {
                    std::string ref = l.substr(0, arrowPos);
                    size_t rb = ref.find_last_not_of(" \t\r\n;{}()");
                    if (rb != std::string::npos) {
                        size_t ra = ref.find_last_of(" \t\r\n;{}()", rb);
                        action.targetRefName = (ra == std::string::npos) ? ref.substr(0, rb + 1) : ref.substr(ra + 1, rb - ra);
                    }
                }

                size_t openP = l.find('(', rotPos);
                size_t closeP = l.rfind(')');
                if (openP != std::string::npos && closeP != std::string::npos && closeP > openP) {
                    std::string argsStr = l.substr(openP + 1, closeP - openP - 1);
                    std::vector<std::string> args;
                    std::string cur;
                    int parenDepth = 0;
                    for (char c : argsStr) {
                        if (c == '(') parenDepth++;
                        else if (c == ')') parenDepth--;
                        else if (c == ',' && parenDepth == 0) {
                            args.push_back(cur);
                            cur.clear();
                            continue;
                        }
                        cur += c;
                    }
                    if (!cur.empty()) args.push_back(cur);

                    if (args.size() == 3) {
                        for (int i = 0; i < 3; ++i) {
                            parseArgExpr(args[i], action.constantDelta[i], action.useDeltaTime[i], action.varNames[i], action.varMultiplier[i], action.scaleVarNames[i], action.activeAxis[i]);
                        }
                        // If mouse Y is in Z slot and X (pitch) is unassigned, map mouse Y to pitch (X)
                        if (action.varNames[0].empty() && (action.varNames[2] == "$__MouseY" || action.varNames[2] == "$__MouseDeltaY")) {
                            action.varNames[0] = action.varNames[2];
                            action.varMultiplier[0] = action.varMultiplier[2];
                            action.useDeltaTime[0] = action.useDeltaTime[2];
                            action.scaleVarNames[0] = action.scaleVarNames[2];
                            action.activeAxis[0] = action.activeAxis[2];
                            action.activeAxis[2] = false;
                        }
                        applyConditionsToAction(action);
                        m_transformActions.push_back(action);
                    }
                }
            } else if (l.find("rotation.y") != std::string::npos || l.find("Rotation.y") != std::string::npos) {
                DynamicTransformAction action;
                action.type = DynamicTransformAction::ActionType::RotateRelative;
                action.activeAxis[1] = true;
                action.varNames[1] = "$__MouseDeltaX";
                action.varMultiplier[1] = (l.find("-=") != std::string::npos || l.find("- deltaX") != std::string::npos || l.find("-deltaX") != std::string::npos || l.find("- mouseDeltaX") != std::string::npos || l.find("-mouseDeltaX") != std::string::npos) ? -1.0f : 1.0f;
                for (const auto& v : declaredVars) {
                    if (l.find(v.first) != std::string::npos) {
                        action.scaleVarNames[1] = v.first;
                        break;
                    }
                }
                applyConditionsToAction(action);
                m_transformActions.push_back(action);
            } else if (l.find("rotation.x") != std::string::npos || l.find("Rotation.x") != std::string::npos) {
                DynamicTransformAction action;
                action.type = DynamicTransformAction::ActionType::RotateRelative;
                action.targetRefName = "Camera";
                action.activeAxis[0] = true;
                action.varNames[0] = "$__MouseDeltaY";
                action.varMultiplier[0] = (l.find("-=") != std::string::npos || l.find("- deltaY") != std::string::npos || l.find("-deltaY") != std::string::npos || l.find("- mouseDeltaY") != std::string::npos || l.find("-mouseDeltaY") != std::string::npos) ? -1.0f : 1.0f;
                for (const auto& v : declaredVars) {
                    if (l.find(v.first) != std::string::npos) {
                        action.scaleVarNames[0] = v.first;
                        break;
                    }
                }
                applyConditionsToAction(action);
                m_transformActions.push_back(action);
            }

            // 4. Scan for CursorState.Locked / CursorState.UnLocked
            if (l.find("CursorState.UnLocked") != std::string::npos || l.find("CursorState.Unlock") != std::string::npos) {
                DynamicTransformAction action;
                action.type = DynamicTransformAction::ActionType::UnlockCursor;
                applyConditionsToAction(action);
                m_transformActions.push_back(action);
            } else if (l.find("CursorState.Locked") != std::string::npos || l.find("CursorState.Lock") != std::string::npos) {
                DynamicTransformAction action;
                action.type = DynamicTransformAction::ActionType::LockCursor;
                applyConditionsToAction(action);
                m_transformActions.push_back(action);
            }

            // 5. Scan for RayTrace.LineTrace or LineTrace
            size_t ltPos = l.find("LineTrace(");
            if (ltPos != std::string::npos) {
                DynamicLineTraceAction action;
                size_t openP = l.find('(', ltPos);
                size_t closeP = l.rfind(')');
                if (openP != std::string::npos && closeP != std::string::npos && closeP > openP) {
                    std::string argsStr = l.substr(openP + 1, closeP - openP - 1);
                    std::vector<std::string> args;
                    std::string cur;
                    int parenDepth = 0;
                    for (char c : argsStr) {
                        if (c == '(') parenDepth++;
                        else if (c == ')') parenDepth--;
                        else if (c == ',' && parenDepth == 0) {
                            args.push_back(cur);
                            cur.clear();
                            continue;
                        }
                        cur += c;
                    }
                    if (!cur.empty()) args.push_back(cur);

                    if (!args.empty()) {
                        action.startVar = args[0];
                    }
                    if (args.size() > 1) {
                        action.endVar = args[1];
                        size_t numStart = args[1].find_first_of("0123456789");
                        if (numStart != std::string::npos) {
                            size_t numEnd = numStart;
                            while (numEnd < args[1].size() && (std::isdigit(args[1][numEnd]) || args[1][numEnd] == '.' || args[1][numEnd] == 'f' || args[1][numEnd] == 'F')) {
                                numEnd++;
                            }
                            std::string nStr = args[1].substr(numStart, numEnd - numStart);
                            if (!nStr.empty() && (nStr.back() == 'f' || nStr.back() == 'F')) nStr.pop_back();
                            try {
                                float val = std::stof(nStr);
                                if (val > 0.0f) action.traceDistance = val;
                            } catch (...) {}
                        }
                    }
                    if (args.size() > 2) {
                        try {
                            float val = std::stof(args[2]);
                            if (val > 0.0f) action.traceDistance = val;
                        } catch (...) {}
                    }
                }
                m_lineTraceActions.push_back(action);
            }

            // 6. Scan for UISystem.Add / UISystem.Remove / UISystem.RemoveAll
            size_t uiAddPos = lLower.find("uisystem.add(");
            if (uiAddPos == std::string::npos) uiAddPos = lLower.find("uisystem::add(");
            if (uiAddPos == std::string::npos) uiAddPos = lLower.find("addui(");
            if (uiAddPos != std::string::npos) {
                size_t p1 = l.find('(', uiAddPos);
                size_t p2 = l.find(')', p1);
                if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1 + 1) {
                    std::string varName = l.substr(p1 + 1, p2 - p1 - 1);
                    size_t v1 = varName.find_first_not_of(" \t&");
                    size_t v2 = varName.find_last_not_of(" \t&");
                    if (v1 != std::string::npos && v2 != std::string::npos) {
                        DynamicScriptBehaviour::DynamicUIAction act;
                        act.type = DynamicScriptBehaviour::DynamicUIAction::ActionType::Add;
                        act.targetVar = varName.substr(v1, v2 - v1 + 1);
                        applyConditionsToUIAction(act);
                        m_uiActions.push_back(act);
                    }
                }
            }

            size_t uiRemPos = lLower.find("uisystem.remove(");
            if (uiRemPos == std::string::npos) uiRemPos = lLower.find("uisystem::remove(");
            if (uiRemPos == std::string::npos) uiRemPos = lLower.find("removeui(");
            if (uiRemPos != std::string::npos && lLower.find("removeall") == std::string::npos) {
                size_t p1 = l.find('(', uiRemPos);
                size_t p2 = l.find(')', p1);
                if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1 + 1) {
                    std::string varName = l.substr(p1 + 1, p2 - p1 - 1);
                    size_t v1 = varName.find_first_not_of(" \t&");
                    size_t v2 = varName.find_last_not_of(" \t&");
                    if (v1 != std::string::npos && v2 != std::string::npos) {
                        DynamicScriptBehaviour::DynamicUIAction act;
                        act.type = DynamicScriptBehaviour::DynamicUIAction::ActionType::Remove;
                        act.targetVar = varName.substr(v1, v2 - v1 + 1);
                        applyConditionsToUIAction(act);
                        m_uiActions.push_back(act);
                    }
                }
            }

            if (lLower.find("uisystem.removeall()") != std::string::npos || lLower.find("removeallui()") != std::string::npos) {
                DynamicScriptBehaviour::DynamicUIAction act;
                act.type = DynamicScriptBehaviour::DynamicUIAction::ActionType::RemoveAll;
                applyConditionsToUIAction(act);
                m_uiActions.push_back(act);
            }

            // 7. Scan for SaveSystem (SetItem / RemoveItem / RemoveAll)
            if (lLower.find("savesystem.") != std::string::npos || lLower.find("savesystem::") != std::string::npos) {
                bool isGlobal = (lLower.find(".local") == std::string::npos && lLower.find("::local") == std::string::npos);
                DynamicScriptBehaviour::DynamicSaveAction saveAct;
                saveAct.scope = isGlobal ? DynamicScriptBehaviour::DynamicSaveAction::Scope::Global : DynamicScriptBehaviour::DynamicSaveAction::Scope::Local;

                if (lLower.find("removeall()") != std::string::npos || lLower.find("removeall( )") != std::string::npos) {
                    saveAct.type = DynamicScriptBehaviour::DynamicSaveAction::ActionType::RemoveAll;
                    applyConditionsToSaveAction(saveAct);
                    m_updateSaveActions.push_back(saveAct);
                } else if (lLower.find("removeitem(") != std::string::npos) {
                    size_t p1 = l.find("RemoveItem(");
                    if (p1 == std::string::npos) p1 = l.find("removeitem(");
                    if (p1 == std::string::npos) p1 = l.find("Removeitem(");
                    size_t pOpen = l.find('(', p1);
                    size_t pClose = l.rfind(')');
                    if (pOpen != std::string::npos && pClose != std::string::npos && pClose > pOpen + 1) {
                        std::string arg = l.substr(pOpen + 1, pClose - pOpen - 1);
                        size_t q1 = arg.find('"');
                        if (q1 != std::string::npos) {
                            size_t q2 = arg.find('"', q1 + 1);
                            if (q2 != std::string::npos) saveAct.itemName = arg.substr(q1 + 1, q2 - q1 - 1);
                        } else {
                            size_t a1 = arg.find_first_not_of(" \t");
                            size_t a2 = arg.find_last_not_of(" \t");
                            if (a1 != std::string::npos && a2 != std::string::npos) saveAct.itemName = arg.substr(a1, a2 - a1 + 1);
                        }
                        saveAct.type = DynamicScriptBehaviour::DynamicSaveAction::ActionType::RemoveItem;
                        applyConditionsToSaveAction(saveAct);
                        m_updateSaveActions.push_back(saveAct);
                    }
                } else if (lLower.find("setitem(") != std::string::npos) {
                    size_t p1 = l.find("SetItem(");
                    if (p1 == std::string::npos) p1 = l.find("setitem(");
                    if (p1 == std::string::npos) p1 = l.find("Setitem(");
                    size_t pOpen = l.find('(', p1);
                    size_t pClose = l.rfind(')');
                    if (pOpen != std::string::npos && pClose != std::string::npos && pClose > pOpen + 1) {
                        std::string argsStr = l.substr(pOpen + 1, pClose - pOpen - 1);
                        std::vector<std::string> args;
                        std::string cur;
                        bool inQ = false;
                        for (char c : argsStr) {
                            if (c == '"') inQ = !inQ;
                            if (c == ',' && !inQ) {
                                args.push_back(cur);
                                cur.clear();
                            } else {
                                cur += c;
                            }
                        }
                        if (!cur.empty()) args.push_back(cur);

                        if (args.size() >= 2) {
                            std::string itemArg = args[0];
                            std::string valArg = args[1];

                            size_t q1 = itemArg.find('"');
                            if (q1 != std::string::npos) {
                                size_t q2 = itemArg.find('"', q1 + 1);
                                if (q2 != std::string::npos) saveAct.itemName = itemArg.substr(q1 + 1, q2 - q1 - 1);
                            } else {
                                size_t a1 = itemArg.find_first_not_of(" \t");
                                size_t a2 = itemArg.find_last_not_of(" \t");
                                if (a1 != std::string::npos && a2 != std::string::npos) saveAct.itemName = itemArg.substr(a1, a2 - a1 + 1);
                            }

                            size_t vq1 = valArg.find('"');
                            if (vq1 != std::string::npos) {
                                size_t vq2 = valArg.find('"', vq1 + 1);
                                if (vq2 != std::string::npos) {
                                    saveAct.valueStr = valArg.substr(vq1 + 1, vq2 - vq1 - 1);
                                    saveAct.isConstString = true;
                                }
                            } else {
                                size_t va1 = valArg.find_first_not_of(" \t");
                                size_t va2 = valArg.find_last_not_of(" \t");
                                if (va1 != std::string::npos && va2 != std::string::npos) {
                                    std::string rawVal = valArg.substr(va1, va2 - va1 + 1);
                                    saveAct.valueStr = rawVal;
                                    saveAct.varName = rawVal;
                                }
                            }
                            saveAct.type = DynamicScriptBehaviour::DynamicSaveAction::ActionType::SetItem;
                            applyConditionsToSaveAction(saveAct);
                            m_updateSaveActions.push_back(saveAct);
                        }
                    }
                }
            }

            // Handle brace cleanup at end of line
            updateMethodBraceDepth -= closeBracesOnLine;
            while (!conditionStack.empty() && conditionStack.back().braceDepth > updateMethodBraceDepth) {
                conditionStack.pop_back();
            }

            if (closeBracesOnLine > 0 && updateMethodBraceDepth <= 0) {
                inUpdateMethod = false;
                conditionStack.clear();
                pendingSingleLineKeys.clear();
                pendingSingleLineBools.clear();
                pendingSingleLineRequireHitStatus = false;
                pendingSingleLineRequireNoHit = false;
                pendingSingleLineRequireHitTag.clear();
                hasPendingSingleLineCondition = false;
                lastIfWasHitCondition = false;
                lastIfHitTag.clear();
            }
        }

    }

    if (m_lineTraceActions.empty()) {
        bool needsHit = false;
        for (const auto& pr : m_updatePrintMessages) {
            if (pr.appendHitName || pr.appendHitId || pr.appendHitPoint || pr.appendHitStatus || pr.appendHitDistance || pr.appendHitNormal || pr.appendHitTags || pr.requireHitStatus || pr.requireNoHit || !pr.requireHitTag.empty()) {
                needsHit = true;
                break;
            }
        }
        if (!needsHit) {
            for (const auto& uia : m_uiActions) {
                if (uia.requireHitStatus || uia.requireNoHit || !uia.requireHitTag.empty()) {
                    needsHit = true;
                    break;
                }
            }
        }
        if (!needsHit) {
            for (const auto& da : m_destroyActions) {
                if (da.targetIsHitObject || da.requireHitStatus || da.requireNoHit || !da.requireHitTag.empty()) {
                    needsHit = true;
                    break;
                }
            }
        }
        if (needsHit) {
            DynamicLineTraceAction action;
            action.traceDistance = 1000.0f;
            m_lineTraceActions.push_back(action);
        }
    }

    if (m_scene) {
        ResolveReferences(*m_scene);
    }
}

void EunoiaBehaviour::ResetToDefaults() {
    m_properties.clear();
    RegisterProperties();
    if (m_scene) {
        ResolveReferences(*m_scene);
    }
}

void DynamicScriptBehaviour::ResetToDefaults() {
    if (!m_sourceCppPath.empty()) {
        ParsePropertiesFromCpp(m_sourceCppPath, false);
    } else {
        for (auto& prop : m_properties) {
            auto it = m_defaultPropertyValues.find(prop.name);
            if (it == m_defaultPropertyValues.end() || !prop.dataPtr) continue;
            const auto& def = it->second;
            switch (prop.type) {
                case BehaviourPropertyType::Bool:
                    *reinterpret_cast<bool*>(prop.dataPtr) = def.boolVal;
                    break;
                case BehaviourPropertyType::Int:
                    *reinterpret_cast<int*>(prop.dataPtr) = def.intVal;
                    break;
                case BehaviourPropertyType::Int64:
                    *reinterpret_cast<int64_t*>(prop.dataPtr) = def.int64Val;
                    break;
                case BehaviourPropertyType::Float:
                    *reinterpret_cast<float*>(prop.dataPtr) = def.floatVal;
                    break;
                case BehaviourPropertyType::Double:
                    *reinterpret_cast<double*>(prop.dataPtr) = def.doubleVal;
                    break;
                case BehaviourPropertyType::String:
                    *reinterpret_cast<std::string*>(prop.dataPtr) = def.stringVal;
                    break;
                case BehaviourPropertyType::Vec2:
                    *reinterpret_cast<glm::vec2*>(prop.dataPtr) = def.vec2Val;
                    break;
                case BehaviourPropertyType::Vec3:
                case BehaviourPropertyType::Color3:
                    *reinterpret_cast<glm::vec3*>(prop.dataPtr) = def.vec3Val;
                    break;
                case BehaviourPropertyType::Vec4:
                case BehaviourPropertyType::Color4:
                    *reinterpret_cast<glm::vec4*>(prop.dataPtr) = def.vec4Val;
                    break;
                case BehaviourPropertyType::ObjectRef:
                    *reinterpret_cast<void**>(prop.dataPtr) = nullptr;
                    prop.targetId = -1;
                    prop.isMissing = false;
                    break;
            }
        }
        if (m_scene) {
            ResolveReferences(*m_scene);
        }
    }
}

// ============================================================================
// GetComponent<T> Specialisations (dev.md Section 35)
// Implementations live here to avoid circular includes between
// EunoiaBehaviour.h ↔ GameObject.h.
// ============================================================================

template<>
LightComponent* EunoiaBehaviour::GetComponent<LightComponent>() const {
    if (!m_owner || !m_owner->isLight) return nullptr;
    return &(m_owner->light);
}

template<>
PrimitiveMesh* EunoiaBehaviour::GetComponent<PrimitiveMesh>() const {
    if (!m_owner || m_owner->isLight) return nullptr;
    return &(m_owner->mesh);
}

// ============================================================================
// SpawnGameObject / DestroyGameObject implementations (dev.md Section 45)
// ============================================================================

GameObject* EunoiaBehaviour::SpawnGameObject(const std::string& name, const glm::vec3& position) {
    if (!m_scene) {
        AddEngineLog("LogBehaviour", "[Behaviour] SpawnGameObject: no active scene - call only during Play Mode", 1);
        return nullptr;
    }
    GameObject& obj = m_scene->AddObject(PrimitiveType::Empty, position, glm::vec3(0.55f));
    obj.name = name;
    AddEngineLog("LogBehaviour", "[Behaviour] SpawnGameObject: created '" + name + "' (ID " + std::to_string(obj.id) + ")", 0);
    return &obj;
}

void EunoiaBehaviour::DestroyGameObject(int objectId) {
    if (!m_scene) return;
    m_scene->RemoveObject(objectId);
    AddEngineLog("LogBehaviour", "[Behaviour] DestroyGameObject: removed object ID " + std::to_string(objectId), 0);
}

void EunoiaBehaviour::Destroy(GameObject* obj) {
    if (obj) obj->Destroy();
}

void EunoiaBehaviour::Destroy(GameObject& obj) {
    obj.Destroy();
}

void EunoiaBehaviour::Destroy(const Eunoia::RayTrace::GameObjectHitProxy& hitProxy) {
    if (hitProxy.ptr) hitProxy.ptr->Destroy();
}

void EunoiaBehaviour::Destroy() {
    if (m_owner) m_owner->Destroy();
}

// ============================================================================
// Mouse Input Helpers (Docs/Behaviours/InputSystem.md)
// ============================================================================

double EunoiaBehaviour::GetMouseX(bool negate) const {
    return InputSystem::Get().GetMouseX(negate);
}

double EunoiaBehaviour::GetMouseY(bool negate) const {
    return InputSystem::Get().GetMouseY(negate);
}

double EunoiaBehaviour::GetMouseDeltaX(bool negate) const {
    return InputSystem::Get().GetMouseDeltaX(negate);
}

double EunoiaBehaviour::GetMouseDeltaY(bool negate) const {
    return InputSystem::Get().GetMouseDeltaY(negate);
}

// ============================================================================
// Tag System Helpers on Behaviour
// ============================================================================

std::vector<std::string> EunoiaBehaviour::GetTags() const {
    return m_owner ? m_owner->GetTags() : std::vector<std::string>{};
}

bool EunoiaBehaviour::HasTag(const std::string& tag) const {
    return m_owner ? m_owner->HasTag(tag) : false;
}

void EunoiaBehaviour::AddTag(const std::string& tag) {
    if (m_owner) m_owner->AddTag(tag);
}

bool EunoiaBehaviour::RemoveTag(const std::string& tag) {
    return m_owner ? m_owner->RemoveTag(tag) : false;
}

void EunoiaBehaviour::RemoveAllTag() {
    if (m_owner) m_owner->RemoveAllTag();
}

std::vector<GameObject*> EunoiaBehaviour::FindObjectsWithTag(const std::string& tag) const {
    return m_scene ? m_scene->FindObjectsWithTag(tag) : std::vector<GameObject*>{};
}

GameObject* EunoiaBehaviour::FindObjectWithTag(const std::string& tag) const {
    return m_scene ? m_scene->FindObjectWithTag(tag) : nullptr;
}


