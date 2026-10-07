#pragma once
#include "RayTraceTypes.h"
#include <glm/glm.hpp>
#include <vector>

class Scene;
struct GameObject;

namespace Eunoia {
namespace RayTrace {

class RayTraceSystem {
public:
    static RayTraceSystem& Get();

    void Initialize();
    void Shutdown();

    void SetActiveScene(Scene* scene) { m_activeScene = scene; }
    Scene* GetActiveScene() const { return m_activeScene; }

    // Line Trace against collision shapes in the active scene
    const RayTraceHit& LineTrace(const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params = RayTraceParams());
    const RayTraceHit& LineTrace(const glm::vec3& start, const glm::vec3& end, const GameObject* ignoreObject);
    const RayTraceHit& LineTrace(const glm::vec3& start, const glm::vec3& end, int ignoreObjectId);

    // Line Trace explicitly providing Scene
    RayTraceHit LineTrace(const Scene& scene, const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params = RayTraceParams());

    // Multi-hit line trace (returns all intersections ordered by distance)
    std::vector<RayTraceHit> LineTraceMulti(const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params = RayTraceParams());
    std::vector<RayTraceHit> LineTraceMulti(const Scene& scene, const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params = RayTraceParams());

    // Fast boolean test whether ray hits anything
    bool LineTraceTest(const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params = RayTraceParams());
    bool LineTraceTest(const Scene& scene, const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params = RayTraceParams());

    // Raycast against a single GameObject's collision modifiers
    bool RaycastGameObject(const GameObject& obj, const glm::vec3& start, const glm::vec3& dirNorm, float maxDist, RayTraceHit& outHit, bool ignoreTriggers = false, const Scene* scene = nullptr);

private:
    RayTraceSystem() = default;
    ~RayTraceSystem() = default;

    Scene* m_activeScene = nullptr;
    RayTraceHit m_lastHit;
};

} // namespace RayTrace
} // namespace Eunoia

using RayTraceSystem = Eunoia::RayTrace::RayTraceSystem;
