#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace SFLOD {

enum class SFLODBillboardMode : int {
    CameraFacingSpherical = 0, // Faces the camera on all axes (omnidirectional)
    LockedYAxisAxial = 1,      // Faces the camera rotated only around the world Y axis (Nacho Time / foliage style)
    FixedPlane = 2             // Uses the object's own local rotation (no camera billboard)
};

struct SFLODCardGeometry {
    glm::vec3 center{0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float halfWidth = 1.0f;
    float halfHeight = 1.0f;
};

struct SFLODBakeSettings {
    int resolution = 512;          // 256, 512, 1024
    int numAngles = 8;             // 1 for single view, 8 for 8-directional surrounding atlas
    bool autoBakeOnActivate = true;
};

struct SFLODTelemetry {
    uint32_t activeCards = 0;
    uint32_t totalSavedTriangles = 0;
};

} // namespace SFLOD
