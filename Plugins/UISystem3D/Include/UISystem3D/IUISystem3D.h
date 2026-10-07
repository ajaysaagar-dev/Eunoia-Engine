#pragma once

#include "UI3DTypes.h"

namespace EunoiaUI {

class IUISystem3D {
public:
    virtual ~IUISystem3D() = default;
    virtual void ClearAll() = 0;
};

} // namespace EunoiaUI
