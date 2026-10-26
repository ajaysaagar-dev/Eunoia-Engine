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

enum class SFLODGeometryMode : int {
    SinglePlane = 0,      // 1 quad / 2 triangles (dynamically samples dominant atlas tile)
    TriPlanarNacho = 1,   // 3 intersecting orthogonal quads / 6 triangles (XY, YZ, XZ quads)
    FiveFaceBox = 2,      // 5-face box / 10 triangles (open box: Front, Back, Left, Right, Bottom)
    SixFaceCube = 3,      // 6-face closed cube / 12 triangles (all 6 bounding box faces)
    CustomBox = 4         // Multi-face box / dynamic 2 to 12 triangles (varying active face mask)
};

enum class SFLODShaderComplexity : int {
    Low = 0,    // 1 Atlas: Diffuse + Alpha Cutout
    Medium = 1, // 2 Atlases: Diffuse + Normal
    High = 2    // 3 Atlases: Diffuse + Normal + RMA (Roughness, Metallic, AO)
};

enum class SFLODOrthogonalFace : int {
    Front = 0, // +Z
    Back = 1,  // -Z
    Right = 2, // +X
    Left = 3,  // -X
    Top = 4,   // +Y
    Bottom = 5 // -Y
};

struct SFLODAtlasTileRect {
    glm::vec2 minUV{0.0f, 0.0f};
    glm::vec2 maxUV{1.0f, 1.0f};
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
    int resolution = 512;          // Tile resolution (256, 512, 1024)
    int numAngles = 8;             // 1 for single view, 8 for 8-directional surrounding atlas
    bool autoBakeOnActivate = true;
    SFLODGeometryMode geometryMode = SFLODGeometryMode::SinglePlane;
    SFLODShaderComplexity shaderComplexity = SFLODShaderComplexity::Low;
};

struct SFLODTelemetry {
    uint32_t activeCards = 0;
    uint32_t totalSavedTriangles = 0;
};

} // namespace SFLOD
