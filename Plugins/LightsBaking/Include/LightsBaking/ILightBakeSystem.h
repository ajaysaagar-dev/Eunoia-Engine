#pragma once
// ============================================================================
// Eunoia Engine — Lights-Baking Plugin
// ILightBakeSystem.h: Plugin service interface for lights baking system
// ============================================================================

#include <cstdint>
#include <string>

namespace Eunoia {

class ILightBakeSystem {
public:
    virtual ~ILightBakeSystem() = default;
    virtual uint32_t GetBakedLightCount() const = 0;
    virtual bool HasBakedData(const std::string& lightGuid) const = 0;
};

} // namespace Eunoia
