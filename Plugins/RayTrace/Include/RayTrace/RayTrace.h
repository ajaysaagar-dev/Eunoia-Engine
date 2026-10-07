#pragma once
#include "RayTraceTypes.h"
#include "RayTraceSystem.h"
#include "IRayTraceService.h"

namespace Eunoia {
namespace RayTrace {

struct RayTraceProxy {
    const RayTraceHit& LineTrace(const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params = RayTraceParams()) const {
        return RayTraceSystem::Get().LineTrace(start, end, params);
    }

    const RayTraceHit& LineTrace(const glm::vec3& start, const glm::vec3& end, const GameObject* ignoreObject) const {
        return RayTraceSystem::Get().LineTrace(start, end, ignoreObject);
    }

    const RayTraceHit& LineTrace(const glm::vec3& start, const glm::vec3& end, int ignoreObjectId) const {
        return RayTraceSystem::Get().LineTrace(start, end, ignoreObjectId);
    }

    std::vector<RayTraceHit> LineTraceMulti(const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params = RayTraceParams()) const {
        return RayTraceSystem::Get().LineTraceMulti(start, end, params);
    }

    bool LineTraceTest(const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params = RayTraceParams()) const {
        return RayTraceSystem::Get().LineTraceTest(start, end, params);
    }
};

} // namespace RayTrace
} // namespace Eunoia

inline Eunoia::RayTrace::RayTraceProxy RayTrace;
inline Eunoia::RayTrace::RayTraceProxy rayTrace;
