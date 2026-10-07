#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace Eunoia {
namespace GI {

struct DDGIVolumeSettings {
    bool enableDDGI = false;
    glm::vec3 origin{0.0f, 0.0f, 0.0f};
    glm::vec3 extents{40.0f, 20.0f, 40.0f};
    glm::ivec3 probeCounts{16, 8, 16};
    int raysPerProbe = 128;
    float maxRayDistance = 30.0f;
    float hysteresis = 0.97f;          // temporal blend factor
    float energyPreservation = 0.85f;
    float normalBias = 0.1f;
    float viewBias = 0.2f;
    bool visualizeProbes = false;
    float probeVisualizationRadius = 0.15f;
};

struct DDGIStats {
    uint32_t totalProbes = 0;
    uint32_t totalRaysCast = 0;
    float updateTimeMs = 0.0f;
};

} // namespace GI
} // namespace Eunoia
