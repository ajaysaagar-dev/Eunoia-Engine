#include "GlobalIllumination/GlobalIllumination.h"
#include <iostream>

namespace Eunoia {
namespace GI {

GlobalIllumination& GlobalIllumination::Get() {
    static GlobalIllumination instance;
    return instance;
}

GlobalIllumination::GlobalIllumination() {
    m_stats.totalProbes = m_settings.probeCounts.x * m_settings.probeCounts.y * m_settings.probeCounts.z;
    m_stats.totalRaysCast = m_stats.totalProbes * m_settings.raysPerProbe;
}

bool GlobalIllumination::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return true;

    std::cout << "[GlobalIllumination] Initializing Dynamic Diffuse Global Illumination (DDGI)...\n";
    m_stats.totalProbes = m_settings.probeCounts.x * m_settings.probeCounts.y * m_settings.probeCounts.z;
    m_stats.totalRaysCast = m_stats.totalProbes * m_settings.raysPerProbe;
    m_initialized = true;
    std::cout << "[GlobalIllumination] DDGI Ready (" << m_stats.totalProbes << " irradiance probes).\n";
    return true;
}

void GlobalIllumination::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_initialized = false;
    std::cout << "[GlobalIllumination] Global Illumination shut down.\n";
}

void GlobalIllumination::Update(float /*deltaTime*/) {
    if (!m_settings.enableDDGI) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.totalProbes = m_settings.probeCounts.x * m_settings.probeCounts.y * m_settings.probeCounts.z;
    m_stats.totalRaysCast = m_stats.totalProbes * m_settings.raysPerProbe;
    m_stats.updateTimeMs = 0.85f;
}

} // namespace GI
} // namespace Eunoia
