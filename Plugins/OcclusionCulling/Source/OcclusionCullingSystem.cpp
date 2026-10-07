#include <OcclusionCulling/OcclusionCullingSystem.h>

namespace Eunoia { namespace OcclusionCulling {

void OcclusionCullingSystem::Initialize() {
    m_settings.occlusion.enabled           = true;
    m_settings.occlusion.twoPassOcclusion  = true;
    m_settings.occlusion.meshletOcclusion  = true;
    m_settings.occlusion.maxOcclusionQueries = 4096;
    m_settings.forwardPlus.lightingMode    = LightingMode::ForwardPlus;
    m_settings.forwardPlus.tileSize        = glm::uvec2(16, 16);
    m_settings.forwardPlus.maxLightsPerTile= 256;
    m_settings.forwardPlus.use25DCulling   = true;
    m_settings.forwardPlus.clustered       = true;
    m_settings.forwardPlus.clusterSlices   = 32;
}

void OcclusionCullingSystem::Shutdown() {}

}} // namespace Eunoia::OcclusionCulling
