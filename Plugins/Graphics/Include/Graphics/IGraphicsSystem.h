#pragma once
// ============================================================================
// Eunoia Engine — Graphics Plugin
// IGraphicsSystem: Interface for Graphics System service in Plugin Architecture
// ============================================================================

#include "GraphicsTypes.h"
#include <EunoiaPluginCore/IPlugin.h>
#include <functional>

namespace Eunoia {

class IGraphicsSystem {
public:
    virtual ~IGraphicsSystem() = default;

    // Global Anti-Aliasing controls
    virtual AntiAliasingMethod GetAntiAliasingMethod() const = 0;
    virtual void SetAntiAliasingMethod(AntiAliasingMethod method) = 0;
    virtual const char* GetAntiAliasingName(AntiAliasingMethod method) const = 0;
    virtual const AntiAliasingInfo& GetCurrentAntiAliasingInfo() const = 0;
    virtual const std::vector<AntiAliasingInfo>& GetAvailableAntiAliasingMethods() const = 0;

    // Quality Settings
    virtual const GraphicsQualitySettings& GetQualitySettings() const = 0;
    virtual void SetQualitySettings(const GraphicsQualitySettings& settings) = 0;

    // Temporal Jitter for TAA / TSR / Temporal Upscalers
    virtual glm::vec2 GetTemporalJitter(uint64_t frameIndex, uint32_t width, uint32_t height) const = 0;

    // Real-time listener callback
    using AntiAliasingCallback = std::function<void(AntiAliasingMethod oldMethod, AntiAliasingMethod newMethod)>;
    virtual void RegisterAntiAliasingChangedCallback(const std::string& listenerId, AntiAliasingCallback callback) = 0;
    virtual void UnregisterAntiAliasingChangedCallback(const std::string& listenerId) = 0;
};

} // namespace Eunoia
