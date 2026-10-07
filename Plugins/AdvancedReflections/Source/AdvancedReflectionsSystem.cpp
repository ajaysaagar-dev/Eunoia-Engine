#include <AdvancedReflections/AdvancedReflectionsSystem.h>

namespace Eunoia { namespace Reflections {

void AdvancedReflectionsSystem::Initialize() {
    m_settings.planar.enabled               = false;
    m_settings.planar.resolution            = 512;
    m_settings.cubemap.enabled              = true;
    m_settings.cubemap.type                 = CubemapType::Dynamic;
    m_settings.cubemap.resolution           = 128;
    m_settings.cubemap.updateEveryNFrames   = 8;
    m_settings.cubemap.useParallaxCorrection= true;
    m_settings.ssr.enabled                  = false;
    m_settings.ssr.quality                  = SSRQuality::Medium;
    m_settings.ssr.roughnessThreshold       = 0.6f;
    m_settings.ssr.maxRaySteps             = 64;
    m_settings.ssr.enableDenoiser           = true;
    m_settings.ssr.useHierarchicalZ         = true;
}

void AdvancedReflectionsSystem::Shutdown() {}

}} // namespace Eunoia::Reflections
