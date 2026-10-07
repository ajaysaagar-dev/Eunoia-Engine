#pragma once
#include "RayTraceTypes.h"
#include <vector>
#include <glm/glm.hpp>

class Scene;

namespace Eunoia {
namespace RayTrace {

class IRayTraceService {
public:
    virtual ~IRayTraceService() = default;

    virtual RayTraceHit LineTrace(const Scene& scene, const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params = RayTraceParams()) = 0;
    virtual std::vector<RayTraceHit> LineTraceMulti(const Scene& scene, const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params = RayTraceParams()) = 0;
    virtual bool LineTraceTest(const Scene& scene, const glm::vec3& start, const glm::vec3& end, const RayTraceParams& params = RayTraceParams()) = 0;
};

} // namespace RayTrace
} // namespace Eunoia
