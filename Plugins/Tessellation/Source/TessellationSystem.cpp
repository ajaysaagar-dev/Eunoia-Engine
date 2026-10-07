#include <Tessellation/TessellationSystem.h>

namespace Eunoia { namespace Tessellation {

void TessellationSystem::Initialize() {
    m_settings.mode               = TessellationMode::Disabled;
    m_settings.tessellationFactor = 4.0f;
    m_settings.adaptiveLOD        = true;
    m_settings.lodDistanceMin     = 2.0f;
    m_settings.lodDistanceMax     = 50.0f;
    m_settings.displacementScale  = 0.1f;
    m_settings.pnBlend            = 1.0f;
}

void TessellationSystem::Shutdown() {}

}} // namespace Eunoia::Tessellation
