#include "SFLOD/SFLODSystem.h"
#include <EngineScene/GameObject.h>
#include <iostream>

namespace SFLOD {

bool SFLODSystem::IsSFLODActive(const GameObject& obj, uint32_t currentLod) const {
    if (!obj.enableSFLOD) return false;
    if (obj.mesh.vertices.empty()) return false;
    if (obj.forcedLOD >= 0) {
        return obj.forcedLOD >= obj.sflodMinLOD;
    }
    return (int)currentLod >= obj.sflodMinLOD;
}

void SFLODSystem::ComputeCardGeometry(
    const GameObject& obj,
    const glm::mat4& model,
    const glm::vec3& cameraPos,
    SFLODCardGeometry& outGeom) const
{
    // 1. Calculate local center and extents from mesh AABB
    glm::vec3 bMin = obj.mesh.aabbValid ? obj.mesh.aabbMin : glm::vec3(-0.5f);
    glm::vec3 bMax = obj.mesh.aabbValid ? obj.mesh.aabbMax : glm::vec3(0.5f);
    glm::vec3 localCenter = (bMin + bMax) * 0.5f;

    // Extract world scale from model matrix
    float scaleX = glm::length(glm::vec3(model[0]));
    float scaleY = glm::length(glm::vec3(model[1]));
    float scaleZ = glm::length(glm::vec3(model[2]));

    float extentX = (bMax.x - bMin.x) * (scaleX > 1e-4f ? scaleX : 1.0f);
    float extentY = (bMax.y - bMin.y) * (scaleY > 1e-4f ? scaleY : 1.0f);
    float extentZ = (bMax.z - bMin.z) * (scaleZ > 1e-4f ? scaleZ : 1.0f);

    float autoWidth = std::max(extentX, extentZ);
    if (autoWidth < 0.1f) autoWidth = 1.0f;
    float autoHeight = extentY;
    if (autoHeight < 0.1f) autoHeight = 1.0f;

    outGeom.halfWidth = (obj.sflodPlaneWidth > 0.001f) ? (obj.sflodPlaneWidth * 0.5f) : (autoWidth * 0.5f);
    outGeom.halfHeight = (obj.sflodPlaneHeight > 0.001f) ? (obj.sflodPlaneHeight * 0.5f) : (autoHeight * 0.5f);

    glm::vec3 worldCenter = glm::vec3(model * glm::vec4(localCenter, 1.0f));

    // 2. Compute orientation vectors based on mode
    glm::vec3 toCamera = cameraPos - worldCenter;
    float dist = glm::length(toCamera);
    glm::vec3 camDir = (dist > 1e-4f) ? (toCamera / dist) : glm::vec3(0.0f, 0.0f, 1.0f);

    SFLODBillboardMode mode = static_cast<SFLODBillboardMode>(obj.sflodBillboardMode);

    if (mode == SFLODBillboardMode::CameraFacingSpherical) {
        // Omnidirectional camera-facing billboard
        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(camDir, worldUp)) > 0.99f) {
            worldUp = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        outGeom.normal = camDir;
        outGeom.right = glm::normalize(glm::cross(worldUp, outGeom.normal));
        outGeom.up = glm::cross(outGeom.normal, outGeom.right);
    } else if (mode == SFLODBillboardMode::LockedYAxisAxial) {
        // "Nacho Time" foliage / tree axial billboard: lock rotation to World Y axis
        glm::vec3 camDirXZ(toCamera.x, 0.0f, toCamera.z);
        float distXZ = glm::length(camDirXZ);
        glm::vec3 normXZ = (distXZ > 1e-4f) ? (camDirXZ / distXZ) : glm::vec3(0.0f, 0.0f, 1.0f);

        outGeom.normal = normXZ;
        outGeom.up = glm::vec3(0.0f, 1.0f, 0.0f);
        outGeom.right = glm::normalize(glm::cross(outGeom.up, outGeom.normal));
    } else {
        // Fixed Plane: derived directly from model matrix
        outGeom.right = (scaleX > 1e-4f) ? (glm::vec3(model[0]) / scaleX) : glm::vec3(1.0f, 0.0f, 0.0f);
        outGeom.up = (scaleY > 1e-4f) ? (glm::vec3(model[1]) / scaleY) : glm::vec3(0.0f, 1.0f, 0.0f);
        outGeom.normal = (scaleZ > 1e-4f) ? (glm::vec3(model[2]) / scaleZ) : glm::vec3(0.0f, 0.0f, 1.0f);
    }

    // Apply offset
    worldCenter += outGeom.right * obj.sflodPlaneOffset.x + outGeom.up * obj.sflodPlaneOffset.y;
    outGeom.center = worldCenter;
}

} // namespace SFLOD
