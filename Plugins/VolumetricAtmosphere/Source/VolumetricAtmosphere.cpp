#include "VolumetricAtmosphere/VolumetricAtmosphere.h"
#include <iostream>

namespace Eunoia {
namespace Atmosphere {

VolumetricAtmosphere& VolumetricAtmosphere::Get() {
    static VolumetricAtmosphere instance;
    return instance;
}

bool VolumetricAtmosphere::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return true;

    std::cout << "[VolumetricAtmosphere] Initializing Raymarched Volumetric Clouds & Atmospheric Scattering...\n";
    m_initialized = true;
    return true;
}

void VolumetricAtmosphere::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_initialized = false;
    std::cout << "[VolumetricAtmosphere] Volumetric Atmosphere shut down.\n";
}

void VolumetricAtmosphere::Update(float deltaTime) {
    if (!m_settings.enableAtmosphere || !m_settings.clouds.enableClouds) return;
    m_windOffset += (m_settings.clouds.windSpeed * 0.2777f) * deltaTime; // km/h to m/s
}

} // namespace Atmosphere
} // namespace Eunoia
