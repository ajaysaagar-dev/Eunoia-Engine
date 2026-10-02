#include "Collisions/CollisionSystem.h"
#include <EngineScene/GameObject.h>
#include <EngineScene/Scene.h>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace Collisions {

void CollisionSystem::SetupDefaultModifiers(GameObject& obj) {
    obj.modifiers.clear();
    if (obj.isLight || IsLightPrimitive(obj.type) || obj.isCamera || obj.type == PrimitiveType::Camera || obj.type == PrimitiveType::Empty || obj.type == PrimitiveType::ImportedMesh || obj.isImportedMesh) {
        obj.collision.enabled = false;
        obj.collision.shapeType = CollisionShapeType::None;
        return;
    }

    CollisionModifier mod;
    mod.enabled = true;
    mod.centerOffset = glm::vec3(0.0f);
    mod.isDefaultFit = true;

    switch (obj.type) {
        case PrimitiveType::Cube:
            mod.name = "Box Collision";
            mod.shapeType = CollisionShapeType::Box;
            mod.boxExtents = glm::vec3(0.5f * (obj.params.size > 0.0f ? obj.params.size : 1.0f));
            break;

        case PrimitiveType::Box:
            mod.name = "Box Collision";
            mod.shapeType = CollisionShapeType::Box;
            mod.boxExtents = glm::vec3(
                std::max(0.05f, obj.params.width * 0.5f),
                std::max(0.05f, obj.params.height * 0.5f),
                std::max(0.05f, obj.params.depth * 0.5f)
            );
            break;

        case PrimitiveType::Sphere:
        case PrimitiveType::UVSphere:
        case PrimitiveType::Icosphere:
            mod.name = "Sphere Collision";
            mod.shapeType = CollisionShapeType::Sphere;
            mod.radius = std::max(0.05f, obj.params.radius > 0.0f ? obj.params.radius : 0.6f);
            break;

        case PrimitiveType::Capsule:
            mod.name = "Capsule Collision";
            mod.shapeType = CollisionShapeType::Capsule;
            mod.radius = std::max(0.05f, obj.params.radius > 0.0f ? obj.params.radius : 0.35f);
            mod.height = std::max(0.1f, obj.params.height > 0.0f ? obj.params.height : 0.8f);
            break;

        case PrimitiveType::Cylinder:
            mod.name = "Cylinder Collision";
            mod.shapeType = CollisionShapeType::Cylinder;
            mod.radius = std::max(0.05f, obj.params.radius > 0.0f ? obj.params.radius : 0.5f);
            mod.height = std::max(0.1f, obj.params.height > 0.0f ? obj.params.height : 1.2f);
            break;

        case PrimitiveType::Cone:
            mod.name = "Cone Collision";
            mod.shapeType = CollisionShapeType::Cone;
            mod.radius = std::max(0.05f, obj.params.radius > 0.0f ? obj.params.radius : 0.5f);
            mod.height = std::max(0.1f, obj.params.height > 0.0f ? obj.params.height : 1.2f);
            break;

        case PrimitiveType::Plane:
        case PrimitiveType::Quad:
            mod.name = "Plane Collision";
            mod.shapeType = CollisionShapeType::Plane;
            mod.planeSize = glm::vec2(
                std::max(0.1f, obj.params.width > 0.0f ? obj.params.width : 2.5f),
                std::max(0.1f, obj.params.depth > 0.0f ? obj.params.depth : 2.5f)
            );
            break;

        case PrimitiveType::WaterBody:
            mod.name = "Plane Collision";
            mod.shapeType = CollisionShapeType::Plane;
            mod.planeSize = glm::vec2(
                std::max(0.1f, obj.water.width > 0.0f ? obj.water.width : 25.0f),
                std::max(0.1f, obj.water.length > 0.0f ? obj.water.length : 25.0f)
            );
            mod.isTrigger = true;
            break;

        case PrimitiveType::Pyramid:
        case PrimitiveType::Prism:
            mod.name = "Box Collision";
            mod.shapeType = CollisionShapeType::Box;
            mod.boxExtents = glm::vec3(
                std::max(0.05f, obj.params.width * 0.5f),
                std::max(0.05f, obj.params.height * 0.5f),
                std::max(0.05f, obj.params.depth * 0.5f)
            );
            break;

        default:
            mod.name = "Box Collision";
            mod.shapeType = CollisionShapeType::Box;
            mod.boxExtents = glm::vec3(0.5f);
            break;
    }

    obj.modifiers.push_back(mod);
    obj.collision = mod;
}

CollisionModifier& CollisionSystem::AddModifier(GameObject& obj, CollisionShapeType shapeType) {
    return obj.AddModifier(shapeType);
}

bool CollisionSystem::RemoveModifier(GameObject& obj, size_t index) {
    return obj.RemoveModifier(index);
}

void CollisionSystem::AutoFitModifier(GameObject& obj, CollisionModifier& mod) {
    if (obj.mesh.vertices.empty()) {
        ResetModifierToDefault(obj, mod);
        return;
    }

    glm::vec3 minB(1e9f);
    glm::vec3 maxB(-1e9f);
    bool valid = false;

    for (const auto& v : obj.mesh.vertices) {
        if (!std::isfinite(v.pos.x) || !std::isfinite(v.pos.y) || !std::isfinite(v.pos.z)) continue;
        minB = glm::min(minB, v.pos);
        maxB = glm::max(maxB, v.pos);
        valid = true;
    }

    if (!valid) {
        ResetModifierToDefault(obj, mod);
        return;
    }

    glm::vec3 center = (minB + maxB) * 0.5f;
    glm::vec3 halfSize = (maxB - minB) * 0.5f;

    halfSize.x = std::max(0.05f, halfSize.x);
    halfSize.y = std::max(0.05f, halfSize.y);
    halfSize.z = std::max(0.05f, halfSize.z);

    mod.centerOffset = center;
    mod.isDefaultFit = false;

    switch (mod.shapeType) {
        case CollisionShapeType::Box:
        case CollisionShapeType::Mesh:
            mod.boxExtents = halfSize;
            break;

        case CollisionShapeType::Sphere:
            mod.radius = std::max({ halfSize.x, halfSize.y, halfSize.z });
            break;

        case CollisionShapeType::Capsule:
        case CollisionShapeType::Cylinder:
        case CollisionShapeType::Cone:
            mod.radius = std::max(halfSize.x, halfSize.z);
            mod.height = halfSize.y * 2.0f;
            break;

        case CollisionShapeType::Plane:
            mod.planeSize = glm::vec2(halfSize.x * 2.0f, halfSize.z * 2.0f);
            break;

        default:
            break;
    }
}

void CollisionSystem::AutoFitModifier(GameObject& obj, size_t index) {
    if (index < obj.modifiers.size()) {
        AutoFitModifier(obj, obj.modifiers[index]);
    }
}

void CollisionSystem::ResetModifierToDefault(GameObject& obj, CollisionModifier& mod) {
    mod.centerOffset = glm::vec3(0.0f);
    mod.isDefaultFit = true;

    switch (mod.shapeType) {
        case CollisionShapeType::Box:
            mod.boxExtents = glm::vec3(
                std::max(0.05f, obj.params.width * 0.5f),
                std::max(0.05f, obj.params.height * 0.5f),
                std::max(0.05f, obj.params.depth * 0.5f)
            );
            break;
        case CollisionShapeType::Sphere:
            mod.radius = std::max(0.05f, obj.params.radius > 0.0f ? obj.params.radius : 0.6f);
            break;
        case CollisionShapeType::Capsule:
            mod.radius = std::max(0.05f, obj.params.radius > 0.0f ? obj.params.radius : 0.35f);
            mod.height = std::max(0.1f, obj.params.height > 0.0f ? obj.params.height : 0.8f);
            break;
        case CollisionShapeType::Cylinder:
            mod.radius = std::max(0.05f, obj.params.radius > 0.0f ? obj.params.radius : 0.5f);
            mod.height = std::max(0.1f, obj.params.height > 0.0f ? obj.params.height : 1.2f);
            break;
        case CollisionShapeType::Cone:
            mod.radius = std::max(0.05f, obj.params.radius > 0.0f ? obj.params.radius : 0.5f);
            mod.height = std::max(0.1f, obj.params.height > 0.0f ? obj.params.height : 1.2f);
            break;
        case CollisionShapeType::Plane:
            mod.planeSize = glm::vec2(
                std::max(0.1f, obj.params.width > 0.0f ? obj.params.width : 2.5f),
                std::max(0.1f, obj.params.depth > 0.0f ? obj.params.depth : 2.5f)
            );
            break;
        case CollisionShapeType::Mesh:
            mod.boxExtents = glm::vec3(0.5f);
            break;
        default:
            break;
    }
}

void CollisionSystem::ResetModifierToDefault(GameObject& obj, size_t index) {
    if (index < obj.modifiers.size()) {
        ResetModifierToDefault(obj, obj.modifiers[index]);
    }
}

void CollisionSystem::AutoFitBounds(GameObject& obj) {
    for (auto& mod : obj.modifiers) {
        AutoFitModifier(obj, mod);
    }
    if (!obj.modifiers.empty()) {
        obj.collision = obj.modifiers[0];
    }
}

void CollisionSystem::ResetToDefaultBounds(GameObject& obj) {
    SetupDefaultModifiers(obj);
}

void CollisionSystem::GenerateBoxWireframe(std::vector<std::pair<glm::vec3, glm::vec3>>& outEdges, const glm::vec3& extents, const glm::vec3& center) {
    glm::vec3 c[8] = {
        center + glm::vec3(-extents.x, -extents.y, -extents.z),
        center + glm::vec3( extents.x, -extents.y, -extents.z),
        center + glm::vec3( extents.x, -extents.y,  extents.z),
        center + glm::vec3(-extents.x, -extents.y,  extents.z),
        center + glm::vec3(-extents.x,  extents.y, -extents.z),
        center + glm::vec3( extents.x,  extents.y, -extents.z),
        center + glm::vec3( extents.x,  extents.y,  extents.z),
        center + glm::vec3(-extents.x,  extents.y,  extents.z)
    };

    // Bottom 4 edges
    outEdges.push_back({c[0], c[1]});
    outEdges.push_back({c[1], c[2]});
    outEdges.push_back({c[2], c[3]});
    outEdges.push_back({c[3], c[0]});

    // Top 4 edges
    outEdges.push_back({c[4], c[5]});
    outEdges.push_back({c[5], c[6]});
    outEdges.push_back({c[6], c[7]});
    outEdges.push_back({c[7], c[4]});

    // Vertical 4 pillars
    outEdges.push_back({c[0], c[4]});
    outEdges.push_back({c[1], c[5]});
    outEdges.push_back({c[2], c[6]});
    outEdges.push_back({c[3], c[7]});
}

void CollisionSystem::GenerateSphereWireframe(std::vector<std::pair<glm::vec3, glm::vec3>>& outEdges, float radius, const glm::vec3& center, int segments) {
    segments = std::max(12, segments);

    for (int i = 0; i < segments; ++i) {
        float a0 = 2.0f * (float)M_PI * ((float)i / (float)segments);
        float a1 = 2.0f * (float)M_PI * ((float)(i + 1) / (float)segments);
        outEdges.push_back({
            center + glm::vec3(radius * std::cos(a0), 0.0f, radius * std::sin(a0)),
            center + glm::vec3(radius * std::cos(a1), 0.0f, radius * std::sin(a1))
        });
    }

    for (int i = 0; i < segments; ++i) {
        float a0 = 2.0f * (float)M_PI * ((float)i / (float)segments);
        float a1 = 2.0f * (float)M_PI * ((float)(i + 1) / (float)segments);
        outEdges.push_back({
            center + glm::vec3(radius * std::cos(a0), radius * std::sin(a0), 0.0f),
            center + glm::vec3(radius * std::cos(a1), radius * std::sin(a1), 0.0f)
        });
    }

    for (int i = 0; i < segments; ++i) {
        float a0 = 2.0f * (float)M_PI * ((float)i / (float)segments);
        float a1 = 2.0f * (float)M_PI * ((float)(i + 1) / (float)segments);
        outEdges.push_back({
            center + glm::vec3(0.0f, radius * std::cos(a0), radius * std::sin(a0)),
            center + glm::vec3(0.0f, radius * std::cos(a1), radius * std::sin(a1))
        });
    }
}

void CollisionSystem::GenerateCapsuleWireframe(std::vector<std::pair<glm::vec3, glm::vec3>>& outEdges, float radius, float height, const glm::vec3& center, int segments) {
    segments = std::max(12, segments);
    float cylinderHalfH = std::max(0.0f, (height - 2.0f * radius) * 0.5f);
    glm::vec3 topCenter = center + glm::vec3(0.0f, cylinderHalfH, 0.0f);
    glm::vec3 botCenter = center - glm::vec3(0.0f, cylinderHalfH, 0.0f);

    // Top and bottom rings
    for (int i = 0; i < segments; ++i) {
        float a0 = 2.0f * (float)M_PI * ((float)i / (float)segments);
        float a1 = 2.0f * (float)M_PI * ((float)(i + 1) / (float)segments);
        outEdges.push_back({
            topCenter + glm::vec3(radius * std::cos(a0), 0.0f, radius * std::sin(a0)),
            topCenter + glm::vec3(radius * std::cos(a1), 0.0f, radius * std::sin(a1))
        });
        outEdges.push_back({
            botCenter + glm::vec3(radius * std::cos(a0), 0.0f, radius * std::sin(a0)),
            botCenter + glm::vec3(radius * std::cos(a1), 0.0f, radius * std::sin(a1))
        });
    }

    // 4 cylinder connecting lines
    for (int i = 0; i < 4; ++i) {
        float a = (float)M_PI * 0.5f * (float)i;
        glm::vec3 offset(radius * std::cos(a), 0.0f, radius * std::sin(a));
        outEdges.push_back({ botCenter + offset, topCenter + offset });
    }

    // Top and bottom hemisphere arcs
    int halfSegs = segments / 2;
    for (int i = 0; i < halfSegs; ++i) {
        float a0 = (float)M_PI * ((float)i / (float)halfSegs);
        float a1 = (float)M_PI * ((float)(i + 1) / (float)halfSegs);
        outEdges.push_back({
            topCenter + glm::vec3(radius * std::cos(a0), radius * std::sin(a0), 0.0f),
            topCenter + glm::vec3(radius * std::cos(a1), radius * std::sin(a1), 0.0f)
        });
        outEdges.push_back({
            topCenter + glm::vec3(0.0f, radius * std::sin(a0), radius * std::cos(a0)),
            topCenter + glm::vec3(0.0f, radius * std::sin(a1), radius * std::cos(a0))
        });

        float b0 = (float)M_PI + a0;
        float b1 = (float)M_PI + a1;
        outEdges.push_back({
            botCenter + glm::vec3(radius * std::cos(b0), radius * std::sin(b0), 0.0f),
            botCenter + glm::vec3(radius * std::cos(b1), radius * std::sin(b1), 0.0f)
        });
        outEdges.push_back({
            botCenter + glm::vec3(0.0f, radius * std::sin(b0), radius * std::cos(b0)),
            botCenter + glm::vec3(0.0f, radius * std::sin(b1), radius * std::cos(b1))
        });
    }
}

void CollisionSystem::GenerateCylinderWireframe(std::vector<std::pair<glm::vec3, glm::vec3>>& outEdges, float radius, float height, const glm::vec3& center, int segments) {
    segments = std::max(12, segments);
    float halfH = height * 0.5f;
    glm::vec3 topCenter = center + glm::vec3(0.0f, halfH, 0.0f);
    glm::vec3 botCenter = center - glm::vec3(0.0f, halfH, 0.0f);

    for (int i = 0; i < segments; ++i) {
        float a0 = 2.0f * (float)M_PI * ((float)i / (float)segments);
        float a1 = 2.0f * (float)M_PI * ((float)(i + 1) / (float)segments);
        outEdges.push_back({
            topCenter + glm::vec3(radius * std::cos(a0), 0.0f, radius * std::sin(a0)),
            topCenter + glm::vec3(radius * std::cos(a1), 0.0f, radius * std::sin(a1))
        });
        outEdges.push_back({
            botCenter + glm::vec3(radius * std::cos(a0), 0.0f, radius * std::sin(a0)),
            botCenter + glm::vec3(radius * std::cos(a1), 0.0f, radius * std::sin(a1))
        });
    }

    for (int i = 0; i < 4; ++i) {
        float a = (float)M_PI * 0.5f * (float)i;
        glm::vec3 offset(radius * std::cos(a), 0.0f, radius * std::sin(a));
        outEdges.push_back({ botCenter + offset, topCenter + offset });
    }
}

void CollisionSystem::GenerateConeWireframe(std::vector<std::pair<glm::vec3, glm::vec3>>& outEdges, float radius, float height, const glm::vec3& center, int segments) {
    segments = std::max(12, segments);
    float halfH = height * 0.5f;
    glm::vec3 apex = center + glm::vec3(0.0f, halfH, 0.0f);
    glm::vec3 botCenter = center - glm::vec3(0.0f, halfH, 0.0f);

    for (int i = 0; i < segments; ++i) {
        float a0 = 2.0f * (float)M_PI * ((float)i / (float)segments);
        float a1 = 2.0f * (float)M_PI * ((float)(i + 1) / (float)segments);
        outEdges.push_back({
            botCenter + glm::vec3(radius * std::cos(a0), 0.0f, radius * std::sin(a0)),
            botCenter + glm::vec3(radius * std::cos(a1), 0.0f, radius * std::sin(a1))
        });
    }

    for (int i = 0; i < 4; ++i) {
        float a = (float)M_PI * 0.5f * (float)i;
        glm::vec3 basePt = botCenter + glm::vec3(radius * std::cos(a), 0.0f, radius * std::sin(a));
        outEdges.push_back({ basePt, apex });
    }
}

void CollisionSystem::GeneratePlaneWireframe(std::vector<std::pair<glm::vec3, glm::vec3>>& outEdges, const glm::vec2& size, const glm::vec3& center) {
    float hx = size.x * 0.5f;
    float hz = size.y * 0.5f;

    glm::vec3 p0 = center + glm::vec3(-hx, 0.0f, -hz);
    glm::vec3 p1 = center + glm::vec3( hx, 0.0f, -hz);
    glm::vec3 p2 = center + glm::vec3( hx, 0.0f,  hz);
    glm::vec3 p3 = center + glm::vec3(-hx, 0.0f,  hz);

    outEdges.push_back({p0, p1});
    outEdges.push_back({p1, p2});
    outEdges.push_back({p2, p3});
    outEdges.push_back({p3, p0});

    outEdges.push_back({p0, p2});
    outEdges.push_back({p1, p3});
}

void CollisionSystem::GenerateMeshWireframe(std::vector<std::pair<glm::vec3, glm::vec3>>& outEdges, const GameObject& obj, const CollisionModifier& mod) {
    GenerateBoxWireframe(outEdges, mod.boxExtents, mod.centerOffset);
}

void CollisionSystem::AppendCollisionWireframe(
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    const GameObject& obj,
    const glm::vec3& cameraPos,
    const glm::mat4& worldMat)
{
    // Render all active modifiers
    for (const auto& mod : obj.modifiers) {
        if (!mod.enabled || !mod.showWireframe || mod.shapeType == CollisionShapeType::None) {
            continue;
        }

        std::vector<std::pair<glm::vec3, glm::vec3>> edges;

        switch (mod.shapeType) {
            case CollisionShapeType::Box:
                GenerateBoxWireframe(edges, mod.boxExtents, mod.centerOffset);
                break;

            case CollisionShapeType::Sphere:
                GenerateSphereWireframe(edges, mod.radius, mod.centerOffset);
                break;

            case CollisionShapeType::Capsule:
                GenerateCapsuleWireframe(edges, mod.radius, mod.height, mod.centerOffset);
                break;

            case CollisionShapeType::Cylinder:
                GenerateCylinderWireframe(edges, mod.radius, mod.height, mod.centerOffset);
                break;

            case CollisionShapeType::Cone:
                GenerateConeWireframe(edges, mod.radius, mod.height, mod.centerOffset);
                break;

            case CollisionShapeType::Plane:
                GeneratePlaneWireframe(edges, mod.planeSize, mod.centerOffset);
                break;

            case CollisionShapeType::Mesh:
                GenerateMeshWireframe(edges, obj, mod);
                break;

            default:
                break;
        }

        glm::vec3 color = glm::vec3(mod.wireframeColor);

        for (const auto& e : edges) {
            glm::vec3 w0 = glm::vec3(worldMat * glm::vec4(e.first, 1.0f));
            glm::vec3 w1 = glm::vec3(worldMat * glm::vec4(e.second, 1.0f));
            GeometryBuilder::AppendCameraFacingLine(outVertices, outIndices, w0, w1, cameraPos, 1.35f, color);
        }
    }
}

RaycastHitResult CollisionSystem::Raycast(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float maxDistance) {
    RaycastHitResult closestHit;
    closestHit.distance = maxDistance;
    glm::vec3 dirNorm = glm::normalize(direction);

    for (const auto& obj : scene.objects) {
        if (!obj.visible) continue;

        glm::mat4 worldMat = scene.GetWorldMatrix(obj);
        glm::mat4 invWorld = glm::inverse(worldMat);

        glm::vec3 localOrigin = glm::vec3(invWorld * glm::vec4(origin, 1.0f));
        glm::vec3 localDir = glm::normalize(glm::vec3(invWorld * glm::vec4(dirNorm, 0.0f)));

        for (int mi = 0; mi < (int)obj.modifiers.size(); ++mi) {
            const auto& mod = obj.modifiers[mi];
            if (!mod.enabled || mod.shapeType == CollisionShapeType::None) continue;

            if (mod.shapeType == CollisionShapeType::Box || mod.shapeType == CollisionShapeType::Mesh) {
                glm::vec3 minB = mod.centerOffset - mod.boxExtents;
                glm::vec3 maxB = mod.centerOffset + mod.boxExtents;

                float tmin = (minB.x - localOrigin.x) / (std::abs(localDir.x) > 1e-6f ? localDir.x : 1e-6f);
                float tmax = (maxB.x - localOrigin.x) / (std::abs(localDir.x) > 1e-6f ? localDir.x : 1e-6f);
                if (tmin > tmax) std::swap(tmin, tmax);

                float tymin = (minB.y - localOrigin.y) / (std::abs(localDir.y) > 1e-6f ? localDir.y : 1e-6f);
                float tymax = (maxB.y - localOrigin.y) / (std::abs(localDir.y) > 1e-6f ? localDir.y : 1e-6f);
                if (tymin > tymax) std::swap(tymin, tymax);

                if ((tmin > tymax) || (tymin > tmax)) continue;
                if (tymin > tmin) tmin = tymin;
                if (tymax < tmax) tmax = tymax;

                float tzmin = (minB.z - localOrigin.z) / (std::abs(localDir.z) > 1e-6f ? localDir.z : 1e-6f);
                float tzmax = (maxB.z - localOrigin.z) / (std::abs(localDir.z) > 1e-6f ? localDir.z : 1e-6f);
                if (tzmin > tzmax) std::swap(tzmin, tzmax);

                if ((tmin > tzmax) || (tzmin > tmax)) continue;
                if (tzmin > tmin) tmin = tzmin;

                if (tmin > 0.0f) {
                    glm::vec3 localHitPt = localOrigin + localDir * tmin;
                    glm::vec3 worldHitPt = glm::vec3(worldMat * glm::vec4(localHitPt, 1.0f));
                    float dist = glm::length(worldHitPt - origin);
                    if (dist < closestHit.distance) {
                        closestHit.hit = true;
                        closestHit.distance = dist;
                        closestHit.point = worldHitPt;
                        closestHit.objectId = obj.id;
                        closestHit.modifierIndex = mi;
                    }
                }
            }
            else if (mod.shapeType == CollisionShapeType::Sphere) {
                glm::vec3 oc = localOrigin - mod.centerOffset;
                float b = glm::dot(oc, localDir);
                float c = glm::dot(oc, oc) - mod.radius * mod.radius;
                float disc = b * b - c;
                if (disc >= 0.0f) {
                    float t = -b - std::sqrt(disc);
                    if (t > 0.0f) {
                        glm::vec3 localHitPt = localOrigin + localDir * t;
                        glm::vec3 worldHitPt = glm::vec3(worldMat * glm::vec4(localHitPt, 1.0f));
                        float dist = glm::length(worldHitPt - origin);
                        if (dist < closestHit.distance) {
                            closestHit.hit = true;
                            closestHit.distance = dist;
                            closestHit.point = worldHitPt;
                            closestHit.objectId = obj.id;
                            closestHit.modifierIndex = mi;
                        }
                    }
                }
            }
        }
    }

    return closestHit;
}

bool CollisionSystem::CheckOverlap(const GameObject& a, const glm::mat4& worldA, const GameObject& b, const glm::mat4& worldB, CollisionHitResult& outResult) {
    for (int i = 0; i < (int)a.modifiers.size(); ++i) {
        const auto& modA = a.modifiers[i];
        if (!modA.enabled || modA.shapeType == CollisionShapeType::None) continue;
        glm::vec3 posA = glm::vec3(worldA * glm::vec4(modA.centerOffset, 1.0f));

        for (int j = 0; j < (int)b.modifiers.size(); ++j) {
            const auto& modB = b.modifiers[j];
            if (!modB.enabled || modB.shapeType == CollisionShapeType::None) continue;
            glm::vec3 posB = glm::vec3(worldB * glm::vec4(modB.centerOffset, 1.0f));

            if (modA.shapeType == CollisionShapeType::Sphere && modB.shapeType == CollisionShapeType::Sphere) {
                float rA = modA.radius * glm::length(glm::vec3(worldA[0]));
                float rB = modB.radius * glm::length(glm::vec3(worldB[0]));
                float dist = glm::length(posA - posB);
                if (dist < (rA + rB)) {
                    outResult.hasCollision = true;
                    outResult.normal = (dist > 1e-4f) ? glm::normalize(posA - posB) : glm::vec3(0, 1, 0);
                    outResult.penetrationDepth = (rA + rB) - dist;
                    outResult.contactPoint = posB + outResult.normal * rB;
                    outResult.objectAId = a.id;
                    outResult.objectBId = b.id;
                    outResult.modifierAIndex = i;
                    outResult.modifierBIndex = j;
                    return true;
                }
            } else {
                glm::vec3 extA = modA.boxExtents * glm::vec3(glm::length(glm::vec3(worldA[0])), glm::length(glm::vec3(worldA[1])), glm::length(glm::vec3(worldA[2])));
                glm::vec3 extB = modB.boxExtents * glm::vec3(glm::length(glm::vec3(worldB[0])), glm::length(glm::vec3(worldB[1])), glm::length(glm::vec3(worldB[2])));
                glm::vec3 delta = glm::abs(posA - posB);

                if (delta.x <= (extA.x + extB.x) && delta.y <= (extA.y + extB.y) && delta.z <= (extA.z + extB.z)) {
                    outResult.hasCollision = true;
                    outResult.objectAId = a.id;
                    outResult.objectBId = b.id;
                    outResult.modifierAIndex = i;
                    outResult.modifierBIndex = j;
                    outResult.contactPoint = (posA + posB) * 0.5f;
                    return true;
                }
            }
        }
    }

    return false;
}

} // namespace Collisions
