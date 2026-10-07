#pragma once
#include "SaveValue.h"
#include "SaveStore.h"
#include <string>

namespace Eunoia {
namespace SaveSystem {

class ISaveSystemService {
public:
    virtual ~ISaveSystemService() = default;

    virtual SaveStore& GetGlobalStore() = 0;
    virtual SaveStore& GetLocalStore() = 0;

    virtual void OnLevelLoaded(const std::string& levelName) = 0;
    virtual void OnLevelUnloaded(const std::string& levelName) = 0;

    virtual bool SaveAll(const std::string& slotName = "DefaultSave") = 0;
    virtual bool LoadAll(const std::string& slotName = "DefaultSave") = 0;
};

} // namespace SaveSystem
} // namespace Eunoia
