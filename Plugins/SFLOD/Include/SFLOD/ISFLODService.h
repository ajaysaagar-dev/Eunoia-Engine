#pragma once
#include "SFLODTypes.h"

struct GameObject;

namespace SFLOD {

class ISFLODService {
public:
    virtual ~ISFLODService() = default;

    virtual bool IsSFLODActive(const GameObject& obj, uint32_t currentLod) const = 0;
    virtual void ComputeCardBounds(const GameObject& obj, float& outWidth, float& outHeight, glm::vec3& outOffset) const = 0;
    virtual uint32_t GetActiveCardCount() const = 0;
    virtual uint32_t GetTrianglesSaved() const = 0;
    virtual void ResetFrameStats() = 0;
};

} // namespace SFLOD
