#pragma once
// ============================================================================
// Eunoia Engine — Graphics Plugin
// GraphicsSystem: Global singleton managing real-time Anti-Aliasing and Quality
// ============================================================================

#include "IGraphicsSystem.h"
#include <unordered_map>
#include <mutex>
#include <iostream>

namespace Eunoia {

class GraphicsSystem : public IGraphicsSystem {
public:
    static GraphicsSystem& Get();

    bool Initialize();
    void Shutdown();

    // Global Anti-Aliasing controls (real-time, thread-safe)
    AntiAliasingMethod GetAntiAliasingMethod() const override;
    void SetAntiAliasingMethod(AntiAliasingMethod method) override;
    const char* GetAntiAliasingName(AntiAliasingMethod method) const override;
    const AntiAliasingInfo& GetCurrentAntiAliasingInfo() const override;
    const std::vector<AntiAliasingInfo>& GetAvailableAntiAliasingMethods() const override;

    // Quality Settings & Presets
    const GraphicsQualitySettings& GetQualitySettings() const override;
    void SetQualitySettings(const GraphicsQualitySettings& settings) override;
    void ApplyPreset(int presetLevel); // 0=Low, 1=Medium, 2=High, 3=Ultra, 4=Cinematic

    // Temporal jitter calculation (Halton 2,3 sequence for TAA/TSR subpixel jitter)
    glm::vec2 GetTemporalJitter(uint64_t frameIndex, uint32_t width, uint32_t height) const override;

    // Real-time listener callback system
    void RegisterAntiAliasingChangedCallback(const std::string& listenerId, AntiAliasingCallback callback) override;
    void UnregisterAntiAliasingChangedCallback(const std::string& listenerId) override;

    // Static utilities
    static const std::vector<AntiAliasingInfo>& GetAllMethods();
    static const char* MethodToString(AntiAliasingMethod method);
    static AntiAliasingMethod StringToMethod(const std::string& name);

private:
    GraphicsSystem();
    ~GraphicsSystem() override = default;
    GraphicsSystem(const GraphicsSystem&) = delete;
    GraphicsSystem& operator=(const GraphicsSystem&) = delete;

    mutable std::mutex m_mutex;
    GraphicsQualitySettings m_settings;
    std::unordered_map<std::string, AntiAliasingCallback> m_callbacks;
    bool m_initialized = false;
};

} // namespace Eunoia
