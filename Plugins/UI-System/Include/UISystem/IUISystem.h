#pragma once

#include "UserInterface.h"

namespace EunoiaUI {

class IUISystem {
public:
    virtual ~IUISystem() = default;
    virtual void Add(UserInterface* ui) = 0;
    virtual void Remove(UserInterface* ui) = 0;
    virtual void RemoveAll() = 0;
    virtual bool IsActive(UserInterface* ui) const = 0;
};

} // namespace EunoiaUI
