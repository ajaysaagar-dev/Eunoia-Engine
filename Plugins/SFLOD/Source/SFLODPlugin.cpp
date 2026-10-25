#include "SFLOD/ISFLODService.h"
#include "SFLOD/SFLODSystem.h"
#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>
#include <iostream>

namespace SFLOD {

class SFLODServiceImpl : public ISFLODService {
public:
    SFLODServiceImpl() = default;
    ~SFLODServiceImpl() override = default;

    bool IsSFLODActive(const GameObject& obj, uint32_t currentLod) const override {
        return SFLODSystem::Get().IsSFLODActive(obj, currentLod);
    }

    void ComputeCardBounds(const GameObject& obj, float& outWidth, float& outHeight, glm::vec3& outOffset) const override {
        SFLODCardGeometry geom;
        SFLODSystem::Get().ComputeCardGeometry(obj, glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 5.0f), geom);
        outWidth = geom.halfWidth * 2.0f;
        outHeight = geom.halfHeight * 2.0f;
        outOffset = geom.center;
    }

    uint32_t GetActiveCardCount() const override {
        return SFLODSystem::Get().GetActiveCardCount();
    }

    uint32_t GetTrianglesSaved() const override {
        return SFLODSystem::Get().GetTrianglesSaved();
    }

    void ResetFrameStats() override {
        SFLODSystem::Get().ResetFrameStats();
    }
};

} // namespace SFLOD

class SFLODPlugin : public IPlugin {
public:
    SFLODPlugin() = default;
    ~SFLODPlugin() override = default;

    const char* GetName() const override { return "SFLOD"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override {
        std::cout << "[SFLOD] Registering ISFLODService...\n";
        registry.Register<SFLOD::ISFLODService>("ISFLODService", &m_service);
    }

    void OnInit() override {
        std::cout << "[SFLOD] Single Face LOD / Imposter Cards plugin initialized.\n";
    }

    void OnShutdown() override {
        std::cout << "[SFLOD] SFLOD plugin shutdown.\n";
    }

private:
    SFLOD::SFLODServiceImpl m_service;
};
