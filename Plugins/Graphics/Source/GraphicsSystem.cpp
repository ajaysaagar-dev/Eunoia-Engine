#include <Graphics/GraphicsSystem.h>
#include <iostream>
#include <algorithm>

namespace Eunoia {

GraphicsSystem& GraphicsSystem::Get() {
    static GraphicsSystem s_instance;
    return s_instance;
}

GraphicsSystem::GraphicsSystem() {
    m_settings.antiAliasing = AntiAliasingMethod::TAA; // Default to primary AA from dev.md
}

bool GraphicsSystem::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_initialized = true;
    std::cout << "[GraphicsSystem] Initialized with Anti-Aliasing: "
              << MethodToString(m_settings.antiAliasing) << std::endl;
    return true;
}

void GraphicsSystem::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callbacks.clear();
    m_initialized = false;
    std::cout << "[GraphicsSystem] Shutdown complete." << std::endl;
}

AntiAliasingMethod GraphicsSystem::GetAntiAliasingMethod() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_settings.antiAliasing;
}

void GraphicsSystem::SetAntiAliasingMethod(AntiAliasingMethod method) {
    AntiAliasingMethod oldMethod;
    std::vector<AntiAliasingCallback> callbacksToInvoke;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_settings.antiAliasing == method) return;

        oldMethod = m_settings.antiAliasing;
        m_settings.antiAliasing = method;

        for (const auto& pair : m_callbacks) {
            if (pair.second) {
                callbacksToInvoke.push_back(pair.second);
            }
        }
    }

    std::cout << "[GraphicsSystem] Anti-Aliasing changed in real time: "
              << MethodToString(oldMethod) << " -> " << MethodToString(method) << std::endl;

    // Dispatch callbacks outside mutex lock to avoid deadlocks
    for (auto& cb : callbacksToInvoke) {
        cb(oldMethod, method);
    }
}

const char* GraphicsSystem::GetAntiAliasingName(AntiAliasingMethod method) const {
    return MethodToString(method);
}

const AntiAliasingInfo& GraphicsSystem::GetCurrentAntiAliasingInfo() const {
    AntiAliasingMethod cur = GetAntiAliasingMethod();
    const auto& methods = GetAllMethods();
    for (const auto& m : methods) {
        if (m.method == cur) return m;
    }
    return methods[0];
}

const std::vector<AntiAliasingInfo>& GraphicsSystem::GetAvailableAntiAliasingMethods() const {
    return GetAllMethods();
}

const GraphicsQualitySettings& GraphicsSystem::GetQualitySettings() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_settings;
}

void GraphicsSystem::SetQualitySettings(const GraphicsQualitySettings& settings) {
    AntiAliasingMethod oldAA;
    AntiAliasingMethod newAA;
    bool aaChanged = false;
    std::vector<AntiAliasingCallback> callbacksToInvoke;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        oldAA = m_settings.antiAliasing;
        newAA = settings.antiAliasing;
        aaChanged = (oldAA != newAA);
        m_settings = settings;

        if (aaChanged) {
            for (const auto& pair : m_callbacks) {
                if (pair.second) callbacksToInvoke.push_back(pair.second);
            }
        }
    }

    if (aaChanged) {
        for (auto& cb : callbacksToInvoke) {
            cb(oldAA, newAA);
        }
    }
}

void GraphicsSystem::ApplyPreset(int presetLevel) {
    GraphicsQualitySettings s;
    switch (presetLevel) {
        case 0: // Low
            s.antiAliasing = AntiAliasingMethod::FXAA;
            s.shadowQuality = 0;
            s.textureQuality = 0;
            s.postProcessQuality = 0;
            s.resolutionScale = 0.75f;
            s.sharpness = 0.4f;
            break;
        case 1: // Medium
            s.antiAliasing = AntiAliasingMethod::SMAA;
            s.shadowQuality = 1;
            s.textureQuality = 1;
            s.postProcessQuality = 1;
            s.resolutionScale = 0.85f;
            s.sharpness = 0.5f;
            break;
        case 2: // High (Default)
            s.antiAliasing = AntiAliasingMethod::TAA;
            s.shadowQuality = 2;
            s.textureQuality = 2;
            s.postProcessQuality = 2;
            s.resolutionScale = 1.0f;
            s.sharpness = 0.5f;
            break;
        case 3: // Ultra
            s.antiAliasing = AntiAliasingMethod::TSR;
            s.shadowQuality = 3;
            s.textureQuality = 3;
            s.postProcessQuality = 3;
            s.resolutionScale = 1.0f;
            s.sharpness = 0.6f;
            break;
        case 4: // Cinematic
            s.antiAliasing = AntiAliasingMethod::SSAA;
            s.shadowQuality = 3;
            s.textureQuality = 3;
            s.postProcessQuality = 3;
            s.resolutionScale = 1.0f;
            s.sharpness = 0.7f;
            break;
        default:
            s.antiAliasing = AntiAliasingMethod::TAA;
            break;
    }
    SetQualitySettings(s);
}

glm::vec2 GraphicsSystem::GetTemporalJitter(uint64_t frameIndex, uint32_t width, uint32_t height) const {
    if (width == 0 || height == 0) return glm::vec2(0.0f);

    AntiAliasingMethod aa = GetAntiAliasingMethod();
    if (aa != AntiAliasingMethod::TAA && aa != AntiAliasingMethod::TSR &&
        aa != AntiAliasingMethod::DLSS && aa != AntiAliasingMethod::FSR && aa != AntiAliasingMethod::XeSS) {
        return glm::vec2(0.0f);
    }

    // Standard Halton sequence (bases 2 and 3) 8-sample cycle
    auto halton = [](uint64_t index, uint32_t base) -> float {
        float result = 0.0f;
        float f = 1.0f / (float)base;
        uint64_t i = index;
        while (i > 0) {
            result += (float)(i % base) * f;
            i /= base;
            f /= (float)base;
        }
        return result;
    };

    uint64_t phase = (frameIndex % 8) + 1;
    float jitterX = halton(phase, 2) - 0.5f;
    float jitterY = halton(phase, 3) - 0.5f;

    // Convert subpixel offset (-0.5 to +0.5 pixels) into normalized projection coordinates
    return glm::vec2((2.0f * jitterX) / (float)width, (2.0f * jitterY) / (float)height);
}

void GraphicsSystem::RegisterAntiAliasingChangedCallback(const std::string& listenerId, AntiAliasingCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callbacks[listenerId] = callback;
}

void GraphicsSystem::UnregisterAntiAliasingChangedCallback(const std::string& listenerId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callbacks.erase(listenerId);
}

const std::vector<AntiAliasingInfo>& GraphicsSystem::GetAllMethods() {
    static const std::vector<AntiAliasingInfo> s_methods = {
        { AntiAliasingMethod::None,       "None",                    "Off",                   "Zero",      "Disabled",                  "Off" },
        { AntiAliasingMethod::TAA,        "TAA",                     "Very High",             "Medium",    "Primary AA",                "Primary" },
        { AntiAliasingMethod::TSR,        "TSR / Temporal Upscaler", "Excellent",             "Med-High",  "High-end renderer",         "Upscaler" },
        { AntiAliasingMethod::SMAA,       "SMAA",                    "High",                  "Low",       "Non-temporal fallback",     "Fallback" },
        { AntiAliasingMethod::FXAA,       "FXAA",                    "Medium",                "Very Low",  "Low-end fallback",          "Fast" },
        { AntiAliasingMethod::MSAA_2x_4x, "MSAA 2x/4x",              "Very High geometry AA", "High",      "Forward renderer",          "Geometry" },
        { AntiAliasingMethod::SSAA,       "SSAA",                    "Excellent",             "Very High", "Screenshot/cinematic mode", "Cinematic" },
        { AntiAliasingMethod::DLSS,       "DLSS",                    "Excellent",             "Medium",    "NVIDIA optional",           "NVIDIA" },
        { AntiAliasingMethod::FSR,        "FSR",                     "Excellent",             "Medium",    "AMD/NVIDIA optional",       "AMD/All" },
        { AntiAliasingMethod::XeSS,       "XeSS",                    "Excellent",             "Medium",    "Intel/compatible hardware", "Intel/All" },
    };
    return s_methods;
}

const char* GraphicsSystem::MethodToString(AntiAliasingMethod method) {
    const auto& methods = GetAllMethods();
    for (const auto& m : methods) {
        if (m.method == method) return m.name;
    }
    return "None";
}

AntiAliasingMethod GraphicsSystem::StringToMethod(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "none" || lower == "off") return AntiAliasingMethod::None;
    if (lower.find("taa") != std::string::npos) return AntiAliasingMethod::TAA;
    if (lower.find("tsr") != std::string::npos || lower.find("temporal upscaler") != std::string::npos) return AntiAliasingMethod::TSR;
    if (lower.find("smaa") != std::string::npos) return AntiAliasingMethod::SMAA;
    if (lower.find("fxaa") != std::string::npos) return AntiAliasingMethod::FXAA;
    if (lower.find("msaa") != std::string::npos) return AntiAliasingMethod::MSAA_2x_4x;
    if (lower.find("ssaa") != std::string::npos) return AntiAliasingMethod::SSAA;
    if (lower.find("dlss") != std::string::npos) return AntiAliasingMethod::DLSS;
    if (lower.find("fsr") != std::string::npos) return AntiAliasingMethod::FSR;
    if (lower.find("xess") != std::string::npos) return AntiAliasingMethod::XeSS;

    return AntiAliasingMethod::TAA;
}

} // namespace Eunoia
