#pragma once
#include "SaveValue.h"
#include "SaveStore.h"
#include "ISaveSystemService.h"
#include <string>
#include <memory>

namespace Eunoia {
namespace SaveSystem {

class SaveSystemClass {
public:
    static SaveSystemClass& Get();

    SaveStore Global;
    SaveStore Local;

    void OnLevelLoaded(const std::string& levelName = "");
    void OnLevelUnloaded(const std::string& levelName = "");

    void SetStorageDirectory(const std::string& dirPath);
    const std::string& GetStorageDirectory() const;

    bool SaveGlobal(const std::string& slotName = "DefaultGlobal");
    bool LoadGlobal(const std::string& slotName = "DefaultGlobal");

    bool SaveLocal(const std::string& levelName = "", const std::string& slotName = "DefaultLocal");
    bool LoadLocal(const std::string& levelName = "", const std::string& slotName = "DefaultLocal");

    bool SaveAll(const std::string& slotName = "DefaultSave");
    bool LoadAll(const std::string& slotName = "DefaultSave");

    void Initialize();
    void Shutdown();

private:
    SaveSystemClass();
    ~SaveSystemClass() = default;

    std::string m_storageDir;
    std::string m_currentLevelName;
};

// Static proxy supporting SaveSystem::Global and SaveSystem::Local
struct SaveSystemScopeProxy {
    static SaveStore& Global() { return SaveSystemClass::Get().Global; }
    static SaveStore& Local() { return SaveSystemClass::Get().Local; }
};

} // namespace SaveSystem
} // namespace Eunoia

// Global instances for direct syntax: SaveSystem.Global and SaveSystem.Local
inline Eunoia::SaveSystem::SaveSystemClass& SaveSystem = Eunoia::SaveSystem::SaveSystemClass::Get();
inline Eunoia::SaveSystem::SaveSystemClass& saveSystem = Eunoia::SaveSystem::SaveSystemClass::Get();
