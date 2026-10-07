#pragma once

#include "UI3DTypes.h"
#include <UISystem/UserInterface.h>
#include <memory>
#include <string>

namespace EunoiaUI {

class UserInterface3D {
public:
    std::string name = "New User Interface 3D";
    std::string filePath = "";
    UserInterface3DComponent component;
    std::shared_ptr<UserInterface> ui2d;

    UserInterface3D() {
        ui2d = std::make_shared<UserInterface>(name);
    }

    explicit UserInterface3D(const std::string& inName, const std::string& inPath = "")
        : name(inName), filePath(inPath) {
        ui2d = std::make_shared<UserInterface>(inName);
        if (!inPath.empty()) {
            ui2d->filePath = inPath;
        }
    }

    bool LoadFromFile(const std::string& path) {
        filePath = path;
        if (!ui2d) {
            ui2d = std::make_shared<UserInterface>(name);
        }
        bool ok = ui2d->LoadFromFile(path);
        if (ok) {
            name = ui2d->name;
            component.canvasResolution = ui2d->canvasResolution;
            component.enabled = ui2d->isVisible;
        }
        return ok;
    }

    bool SaveToFile(const std::string& path) {
        filePath = path;
        if (!ui2d) {
            ui2d = std::make_shared<UserInterface>(name);
        }
        ui2d->name = name;
        ui2d->filePath = path;
        ui2d->canvasResolution = component.canvasResolution;
        ui2d->isVisible = component.enabled;
        return ui2d->SaveToFile(path);
    }
};

} // namespace EunoiaUI
