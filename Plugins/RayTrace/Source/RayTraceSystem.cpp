#include "RayTrace/RayTraceSystem.h"
#include <EngineScene/Scene.h>
#include <EngineScene/GameObject.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Eunoia {
namespace RayTrace {

GameObjectHitProxy::operator GameObject&() const {
    return *ptr;
}

GameObject& GameObjectHitProxy::operator*() const {
    return *ptr;
}

std::string GameObjectHitProxy::NameAccessor::str() const {
    return (parent && parent->ptr) ? parent->ptr->name : "";
}

const char* GameObjectHitProxy::NameAccessor::c_str() const {
    return (parent && parent->ptr) ? parent->ptr->name.c_str() : "";
}

int GameObjectHitProxy::GetId() const {
    return ptr ? ptr->id : -1;
}

glm::vec3 GameObjectHitProxy::GetPosition() const {
    return ptr ? ptr->position : glm::vec3(0.0f);
}

glm::vec3 GameObjectHitProxy::GetRotation() const {
    return ptr ? ptr->rotation : glm::vec3(0.0f);
}

glm::vec3 GameObjectHitProxy::GetScale() const {
    return ptr ? ptr->scale : glm::vec3(1.0f);
}

void RayTraceHit::SetHit(bool hit, ::GameObject* obj, const glm::vec3& pt, const glm::vec3& n, float dist, float frac, int objId, int modIdx, const std::string& modName, CollisionShapeType shape) {
    Status = hit;
    status = hit;
    hasHit = hit;
    GameObject = GameObjectHitProxy(obj);
    gameObject = obj;
    Point = pt;
    point = pt;
    Normal = n;
    normal = n;
    Distance = dist;
    distance = dist;
    Fraction = frac;
    fraction = frac;
    ObjectId = objId;
    objectId = objId;
    ModifierIndex = modIdx;
    modifierIndex = modIdx;
    ModifierName = modName;
    modifierName = modName;
    ShapeType = shape;
    shapeType = shape;
}

::GameObject& RayTraceHit::operator*() const {
    return *gameObject;
}

RayTraceHit::operator ::GameObject&() const {
    return *gameObject;
}

::GameObject& RayTraceHit::Get() const {
    return *gameObject;
}

RayTraceSystem& RayTraceSystem::Get() {
    static RayTraceSystem instance;
    return instance;
}

void RayTraceSystem::Initialize() {
    m_activeScene = nullptr;
    m_lastHit = RayTraceHit();
}

void RayTraceSystem::Shutdown() {
    m_activeScene = nullptr;
    m_lastHit = RayTraceHit();
}

static glm::mat4 GetObjectWorldMatrix(const GameObject& obj) {
    glm::mat4 m(1.0f);
    m = glm::translate(m, obj.position);
    m = glm::rotate(m, glm::radians(obj.rotation.x), glm::vec3(1, 0, 0));
    m = glm::rotate(m, glm::radians(obj.rotation.y), glm::vec3(0, 1, 0));
    m = glm::rotate(m, glm::radians(obj.rotation.z), glm::vec3(0, 0, 1));
    m = glm::scale(m, obj.scale);
    return m;
}

static bool IntersectRayBox(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& boxMin, const glm::vec3& boxMax, float& outT, glm::vec3& outNormal) {
    float tmin = -1e30f;
    float tmax =  1e30f;
    glm::vec3 nmin(0.0f);
    glm::vec3 nmax(0.0f);

    for (int i = 0; i < 3; ++i) {
        if (std::abs(rd[i]) < 1e-7f) {
            if (ro[i] < boxMin[i] || ro[i] > boxMax[i]) return false;
        } else {
            float invD = 1.0f / rd[i];
            float t1 = (boxMin[i] - ro[i]) * invD;
            float t2 = (boxMax[i] - ro[i]) * invD;
            glm::vec3 n1(0.0f);
            glm::vec3 n2(0.0f);
            n1[i] = -1.0f;
            n2[i] =  1.0f;

            if (t1 > t2) {
                std::swap(t1, t2);
                std::swap(n1, n2);
            }

            if (t1 > tmin) {
                tmin = t1;
                nmin = n1;
            }
            if (t2 < tmax) {
                tmax = t2;
                nmax = n2;
            }

            if (tmin > tmax) return false;
        }
    }

    if (tmax < 0.0f) return false;

    if (tmin >= 0.0f) {
        outT = tmin;
        outNormal = nmin;
    } else {
        outT = tmax;
        outNormal = nmax;
    }
    return true;
}

static bool IntersectRaySphere(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& center, float radius, float& outT, glm::vec3& outNormal) {
    glm::vec3 oc = ro - center;
    float b = glm::dot(oc, rd);
    float c = glm::dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0f) return false;

    float sqrtDisc = std::sqrt(disc);
    float t = -b - sqrtDisc;
    if (t < 0.0f) {
        t = -b + sqrtDisc;
    }
    if (t < 0.0f) return false;

    outT = t;
    glm::vec3 hitP = ro + rd * t;
    outNormal = glm::normalize(hitP - center);
    return true;
}

static bool IntersectRayPlane(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& center, const glm::vec2& size, float& outT, glm::vec3& outNormal) {
    if (std::abs(rd.y) < 1e-7f) return false;

    float t = (center.y - ro.y) / rd.y;
    if (t < 0.0f) return false;

    glm::vec3 hitP = ro + rd * t;
    float halfW = size.x * 0.5f;
    float halfD = size.y * 0.5f;

    if (hitP.x < center.x - halfW || hitP.x > center.x + halfW ||
        hitP.z < center.z - halfD || hitP.z > center.z + halfD) {
        return false;
    }

    outT = t;
    outNormal = (rd.y < 0.0f) ? glm::vec3(0, 1, 0) : glm::vec3(0, -1, 0);
    return true;
}

static bool IntersectRayCylinder(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& center, float radius, float height, float& outT, glm::vec3& outNormal) {
    glm::vec3 relRo = ro - center;
    float halfH = height * 0.5f;
    float closestT = 1e30f;
    glm::vec3 bestN(0.0f);
    bool hit = false;

    // Body: x^2 + z^2 = r^2
    float a = rd.x * rd.x + rd.z * rd.z;
    float b = 2.0f * (relRo.x * rd.x + relRo.z * rd.z);
    float c = relRo.x * relRo.x + relRo.z * relRo.z - radius * radius;

    if (a > 1e-7f) {
        float disc = b * b - 4.0f * a * c;
        if (disc >= 0.0f) {
            float sqrtDisc = std::sqrt(disc);
            float t1 = (-b - sqrtDisc) / (2.0f * a);
            float t2 = (-b + sqrtDisc) / (2.0f * a);

            for (float t : { t1, t2 }) {
                if (t > 0.0f && t < closestT) {
                    float y = relRo.y + rd.y * t;
                    if (y >= -halfH && y <= halfH) {
                        closestT = t;
                        bestN = glm::normalize(glm::vec3(relRo.x + rd.x * t, 0.0f, relRo.z + rd.z * t));
                        hit = true;
                    }
                }
            }
        }
    }

    // Top and Bottom Caps
    if (std::abs(rd.y) > 1e-7f) {
        for (float capY : { halfH, -halfH }) {
            float t = (capY - relRo.y) / rd.y;
            if (t > 0.0f && t < closestT) {
                float x = relRo.x + rd.x * t;
                float z = relRo.z + rd.z * t;
                if (x * x + z * z <= radius * radius) {
                    closestT = t;
                    bestN = glm::vec3(0.0f, capY > 0.0f ? 1.0f : -1.0f, 0.0f);
                    hit = true;
                }
            }
        }
    }

    if (hit) {
        outT = closestT;
        outNormal = bestN;
        return true;
    }
    return false;
}

static bool IntersectRayCapsule(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& center, float radius, float height, float& outT, glm::vec3& outNormal) {
    float halfH = std::max(0.0f, (height - radius * 2.0f) * 0.5f);
    float closestT = 1e30f;
    glm::vec3 bestN(0.0f);
    bool hit = false;

    // Middle Cylinder
    if (halfH > 0.001f) {
        float tCyl = 0.0f;
        glm::vec3 nCyl(0.0f);
        if (IntersectRayCylinder(ro, rd, center, radius, halfH * 2.0f, tCyl, nCyl)) {
            if (tCyl > 0.0f && tCyl < closestT) {
                closestT = tCyl;
                bestN = nCyl;
                hit = true;
            }
        }
    }

    // Top and bottom hemispheres
    for (float sign : { 1.0f, -1.0f }) {
        glm::vec3 capCenter = center + glm::vec3(0.0f, sign * halfH, 0.0f);
        float tSph = 0.0f;
        glm::vec3 nSph(0.0f);
        if (IntersectRaySphere(ro, rd, capCenter, radius, tSph, nSph)) {
            if (tSph > 0.0f && tSph < closestT) {
                closestT = tSph;
                bestN = nSph;
                hit = true;
            }
        }
    }

    if (hit) {
        outT = closestT;
        outNormal = bestN;
        return true;
    }
    return false;
}

static bool IntersectRayCone(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& center, float radius, float height, float& outT, glm::vec3& outNormal) {
    glm::vec3 relRo = ro - center;
    float halfH = height * 0.5f;
    float tipY = halfH;
    float baseY = -halfH;
    float k = radius / height;
    float k2 = k * k;

    float closestT = 1e30f;
    glm::vec3 bestN(0.0f);
    bool hit = false;

    // Cone surface: x^2 + z^2 = k^2 * (tipY - y)^2
    float dy = tipY - relRo.y;
    float a = rd.x * rd.x + rd.z * rd.z - k2 * rd.y * rd.y;
    float b = 2.0f * (relRo.x * rd.x + relRo.z * rd.z + k2 * dy * rd.y);
    float c = relRo.x * relRo.x + relRo.z * relRo.z - k2 * dy * dy;

    if (std::abs(a) > 1e-7f) {
        float disc = b * b - 4.0f * a * c;
        if (disc >= 0.0f) {
            float sqrtDisc = std::sqrt(disc);
            float t1 = (-b - sqrtDisc) / (2.0f * a);
            float t2 = (-b + sqrtDisc) / (2.0f * a);

            for (float t : { t1, t2 }) {
                if (t > 0.0f && t < closestT) {
                    float y = relRo.y + rd.y * t;
                    if (y >= baseY && y <= tipY) {
                        closestT = t;
                        glm::vec3 hitP = relRo + rd * t;
                        float r = std::sqrt(hitP.x * hitP.x + hitP.z * hitP.z);
                        bestN = glm::normalize(glm::vec3(hitP.x, r * k, hitP.z));
                        hit = true;
                    }
                }
            }
        }
    }

    // Base disc
    if (std::abs(rd.y) > 1e-7f) {
        float t = (baseY - relRo.y) / rd.y;
        if (t > 0.0f && t < closestT) {
            float x = relRo.x + rd.x * t;
            float z = relRo.z + rd.z * t;
            if (x * x + z * z <= radius * radius) {
                closestT = t;
                bestN = glm::vec3(0.0f, -1.0f, 0.0f);
                hit = true;
            }
        }
    }

    if (hit) {
        outT = closestT;
        outNormal = bestN;
        return true;
    }
    return false;
}

bool RayTraceSystem::RaycastGameObject(const GameObject& obj, const glm::vec3& start, const glm::vec3& dirNorm, float maxDist, RayTraceHit& outHit, bool ignoreTriggers) {
    if (obj.isLight || IsLightPrimitive(obj.type) || obj.isCamera || IsCameraPrimitive(obj.type)) {
        return false;
    }

    glm::mat4 worldMat = GetObjectWorldMatrix(obj);
    glm::mat4 invWorld = glm::inverse(worldMat);
    glm::mat3 normalMat = glm::transpose(glm::mat3(invWorld));

    glm::vec3 localOrigin = glm::vec3(invWorld * glm::vec4(start, 1.0f));
    glm::vec3 localDir = glm::vec3(invWorld * glm::vec4(dirNorm, 0.0f));
    float localDirLen = glm::length(localDir);
    if (localDirLen < 1e-7f) return false;
    glm::vec3 localDirNorm = localDir / localDirLen;

    float closestLocalT = 1e30f;
    glm::vec3 bestLocalNormal(0.0f);
    int bestModIdx = -1;
    std::string bestModName = "";
    CollisionShapeType bestShape = CollisionShapeType::None;

    // 1. Check user-defined modifiers
    if (!obj.modifiers.empty()) {
        for (int i = 0; i < (int)obj.modifiers.size(); ++i) {
            const auto& mod = obj.modifiers[i];
            if (!mod.enabled || mod.shapeType == CollisionShapeType::None) continue;
            if (ignoreTriggers && mod.isTrigger) continue;

            float t = 0.0f;
            glm::vec3 n(0.0f);
            bool hitMod = false;

            switch (mod.shapeType) {
                case CollisionShapeType::Box:
                case CollisionShapeType::Mesh:
                    hitMod = IntersectRayBox(localOrigin, localDirNorm, mod.centerOffset - mod.boxExtents, mod.centerOffset + mod.boxExtents, t, n);
                    break;
                case CollisionShapeType::Sphere:
                    hitMod = IntersectRaySphere(localOrigin, localDirNorm, mod.centerOffset, mod.radius, t, n);
                    break;
                case CollisionShapeType::Plane:
                    hitMod = IntersectRayPlane(localOrigin, localDirNorm, mod.centerOffset, mod.planeSize, t, n);
                    break;
                case CollisionShapeType::Cylinder:
                    hitMod = IntersectRayCylinder(localOrigin, localDirNorm, mod.centerOffset, mod.radius, mod.height, t, n);
                    break;
                case CollisionShapeType::Capsule:
                    hitMod = IntersectRayCapsule(localOrigin, localDirNorm, mod.centerOffset, mod.radius, mod.height, t, n);
                    break;
                case CollisionShapeType::Cone:
                    hitMod = IntersectRayCone(localOrigin, localDirNorm, mod.centerOffset, mod.radius, mod.height, t, n);
                    break;
                default:
                    break;
            }

            if (hitMod && t > 0.0f && t < closestLocalT) {
                closestLocalT = t;
                bestLocalNormal = n;
                bestModIdx = i;
                bestModName = mod.name;
                bestShape = mod.shapeType;
            }
        }
    } else if (obj.type != PrimitiveType::Empty && obj.type != PrimitiveType::ImportedMesh) {
        // Fallback default shape matching primitive dimensions
        float t = 0.0f;
        glm::vec3 n(0.0f);
        bool hitMod = false;

        if (obj.type == PrimitiveType::Sphere || obj.type == PrimitiveType::UVSphere || obj.type == PrimitiveType::Icosphere) {
            float r = (obj.params.radius > 0.0f ? obj.params.radius : 0.6f);
            hitMod = IntersectRaySphere(localOrigin, localDirNorm, glm::vec3(0.0f), r, t, n);
            bestShape = CollisionShapeType::Sphere;
            bestModName = "Default Sphere Collision";
        } else if (obj.type == PrimitiveType::Plane || obj.type == PrimitiveType::Quad) {
            glm::vec2 sz(obj.params.width > 0.0f ? obj.params.width : 2.5f, obj.params.depth > 0.0f ? obj.params.depth : 2.5f);
            hitMod = IntersectRayPlane(localOrigin, localDirNorm, glm::vec3(0.0f), sz, t, n);
            bestShape = CollisionShapeType::Plane;
            bestModName = "Default Plane Collision";
        } else if (obj.type == PrimitiveType::Capsule) {
            float r = (obj.params.radius > 0.0f ? obj.params.radius : 0.35f);
            float h = (obj.params.height > 0.0f ? obj.params.height : 0.8f);
            hitMod = IntersectRayCapsule(localOrigin, localDirNorm, glm::vec3(0.0f), r, h, t, n);
            bestShape = CollisionShapeType::Capsule;
            bestModName = "Default Capsule Collision";
        } else if (obj.type == PrimitiveType::Cylinder) {
            float r = (obj.params.radius > 0.0f ? obj.params.radius : 0.5f);
            float h = (obj.params.height > 0.0f ? obj.params.height : 1.2f);
            hitMod = IntersectRayCylinder(localOrigin, localDirNorm, glm::vec3(0.0f), r, h, t, n);
            bestShape = CollisionShapeType::Cylinder;
            bestModName = "Default Cylinder Collision";
        } else if (obj.type == PrimitiveType::Cone) {
            float r = (obj.params.radius > 0.0f ? obj.params.radius : 0.5f);
            float h = (obj.params.height > 0.0f ? obj.params.height : 1.2f);
            hitMod = IntersectRayCone(localOrigin, localDirNorm, glm::vec3(0.0f), r, h, t, n);
            bestShape = CollisionShapeType::Cone;
            bestModName = "Default Cone Collision";
        } else {
            glm::vec3 ext(std::max(0.05f, obj.params.width * 0.5f), std::max(0.05f, obj.params.height * 0.5f), std::max(0.05f, obj.params.depth * 0.5f));
            hitMod = IntersectRayBox(localOrigin, localDirNorm, -ext, ext, t, n);
            bestShape = CollisionShapeType::Box;
            bestModName = "Default Box Collision";
        }

        if (hitMod && t > 0.0f && t < closestLocalT) {
            closestLocalT = t;
            bestLocalNormal = n;
            bestModIdx = 0;
        }
    }

    if (bestModIdx >= 0 && closestLocalT < 1e29f) {
        glm::vec3 localHitPt = localOrigin + localDirNorm * closestLocalT;
        glm::vec3 worldHitPt = glm::vec3(worldMat * glm::vec4(localHitPt, 1.0f));
        float worldDist = glm::length(worldHitPt - start);

        if (worldDist <= maxDist) {
            outHit.SetHit(
                true,
                const_cast<GameObject*>(&obj),
                worldHitPt,
                glm::normalize(normalMat * bestLocalNormal),
                worldDist,
                worldDist / maxDist,
                obj.id,
                bestModIdx,
                bestModName,
                bestShape
            );
            return true;
        }
    }

    return false;
}

const RayTraceHit& RayTraceSystem::LineTrace(const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params) {
    if (m_activeScene) {
        m_lastHit = LineTrace(*m_activeScene, start, end, params);
    } else {
        m_lastHit = RayTraceHit();
    }
    return m_lastHit;
}

const RayTraceHit& RayTraceSystem::LineTrace(const glm::vec3& start, const glm::vec3& end, const GameObject* ignoreObject) {
    RayTraceParams params;
    params.ignoreObject = ignoreObject;
    return LineTrace(start, end, params);
}

const RayTraceHit& RayTraceSystem::LineTrace(const glm::vec3& start, const glm::vec3& end, int ignoreObjectId) {
    RayTraceParams params;
    params.ignoreObjectId = ignoreObjectId;
    return LineTrace(start, end, params);
}

RayTraceHit RayTraceSystem::LineTrace(const Scene& scene, const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params) {
    glm::vec3 delta = end - start;
    float maxDist = glm::length(delta);
    if (maxDist < 1e-6f) return RayTraceHit();

    glm::vec3 dirNorm = delta / maxDist;
    float searchDist = std::min(maxDist, params.maxDistance);

    RayTraceHit closestHit;
    float closestDist = 1e30f;

    for (const auto& obj : scene.objects) {
        if (params.ignoreInvisible && !obj.visible) continue;
        if (params.ignoreObjectId >= 0 && obj.id == params.ignoreObjectId) continue;
        if (params.ignoreObject != nullptr && &obj == params.ignoreObject) continue;

        RayTraceHit hit;
        if (RaycastGameObject(obj, start, dirNorm, searchDist, hit, params.ignoreTriggers)) {
            if (hit.distance < closestDist) {
                closestDist = hit.distance;
                closestHit = hit;
                closestHit.fraction = hit.distance / maxDist;
                closestHit.Fraction = closestHit.fraction;
            }
        }
    }

    return closestHit;
}

std::vector<RayTraceHit> RayTraceSystem::LineTraceMulti(const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params) {
    if (m_activeScene) {
        return LineTraceMulti(*m_activeScene, start, end, params);
    }
    return {};
}

std::vector<RayTraceHit> RayTraceSystem::LineTraceMulti(const Scene& scene, const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params) {
    glm::vec3 delta = end - start;
    float maxDist = glm::length(delta);
    if (maxDist < 1e-6f) return {};

    glm::vec3 dirNorm = delta / maxDist;
    float searchDist = std::min(maxDist, params.maxDistance);

    std::vector<RayTraceHit> hits;

    for (const auto& obj : scene.objects) {
        if (params.ignoreInvisible && !obj.visible) continue;
        if (params.ignoreObjectId >= 0 && obj.id == params.ignoreObjectId) continue;
        if (params.ignoreObject != nullptr && &obj == params.ignoreObject) continue;

        RayTraceHit hit;
        if (RaycastGameObject(obj, start, dirNorm, searchDist, hit, params.ignoreTriggers)) {
            hit.fraction = hit.distance / maxDist;
            hits.push_back(hit);
        }
    }

    std::sort(hits.begin(), hits.end(), [](const RayTraceHit& a, const RayTraceHit& b) {
        return a.distance < b.distance;
    });

    return hits;
}

bool RayTraceSystem::LineTraceTest(const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params) {
    if (m_activeScene) {
        return LineTraceTest(*m_activeScene, start, end, params);
    }
    return false;
}

bool RayTraceSystem::LineTraceTest(const Scene& scene, const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params) {
    RayTraceHit hit = LineTrace(scene, start, end, params);
    return hit.hasHit;
}

} // namespace RayTrace
} // namespace Eunoia
