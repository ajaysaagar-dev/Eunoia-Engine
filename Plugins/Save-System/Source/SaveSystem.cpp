#include "SaveSystem/SaveSystem.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <iostream>

using json = nlohmann::json;

namespace Eunoia {
namespace SaveSystem {

static json SaveValueToJson(const SaveValue& val) {
    switch (val.GetType()) {
        case SaveValueType::Null:
            return nullptr;
        case SaveValueType::Bool:
            return val.AsBool();
        case SaveValueType::Int64:
            return val.AsInt64();
        case SaveValueType::Double:
            return val.AsDouble();
        case SaveValueType::String:
            return val.AsString();
        case SaveValueType::Vec2: {
            glm::vec2 v = val.AsVec2();
            return json::array({ v.x, v.y });
        }
        case SaveValueType::Vec3: {
            glm::vec3 v = val.AsVec3();
            return json::array({ v.x, v.y, v.z });
        }
        case SaveValueType::Vec4: {
            glm::vec4 v = val.AsVec4();
            return json::array({ v.x, v.y, v.z, v.w });
        }
        case SaveValueType::Array: {
            json jArr = json::array();
            for (const auto& item : val.AsArray()) {
                jArr.push_back(SaveValueToJson(item));
            }
            return jArr;
        }
        case SaveValueType::Object: {
            json jObj = json::object();
            for (const auto& pair : val.AsObject()) {
                jObj[pair.first] = SaveValueToJson(pair.second);
            }
            return jObj;
        }
    }
    return nullptr;
}

static SaveValue JsonToSaveValue(const json& j) {
    if (j.is_null()) {
        return SaveValue();
    } else if (j.is_boolean()) {
        return SaveValue(j.get<bool>());
    } else if (j.is_number_integer()) {
        return SaveValue(j.get<int64_t>());
    } else if (j.is_number_unsigned()) {
        return SaveValue((int64_t)j.get<uint64_t>());
    } else if (j.is_number_float()) {
        return SaveValue(j.get<double>());
    } else if (j.is_string()) {
        return SaveValue(j.get<std::string>());
    } else if (j.is_array()) {
        if (j.size() == 2 && j[0].is_number() && j[1].is_number()) {
            return SaveValue(glm::vec2(j[0].get<float>(), j[1].get<float>()));
        } else if (j.size() == 3 && j[0].is_number() && j[1].is_number() && j[2].is_number()) {
            return SaveValue(glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>()));
        } else if (j.size() == 4 && j[0].is_number() && j[1].is_number() && j[2].is_number() && j[3].is_number()) {
            return SaveValue(glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()));
        }
        SaveValue::ArrayType arr;
        arr.reserve(j.size());
        for (const auto& item : j) {
            arr.push_back(JsonToSaveValue(item));
        }
        return SaveValue(arr);
    } else if (j.is_object()) {
        SaveValue::ObjectType obj;
        for (auto it = j.begin(); it != j.end(); ++it) {
            obj[it.key()] = JsonToSaveValue(it.value());
        }
        return SaveValue(obj);
    }
    return SaveValue();
}

std::string SaveValue::ToJSON() const {
    try {
        json j = SaveValueToJson(*this);
        return j.dump(2);
    } catch (...) {
        return "null";
    }
}

SaveValue SaveValue::FromJSON(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        return JsonToSaveValue(j);
    } catch (...) {
        return SaveValue();
    }
}

std::string SaveStore::ToJSON() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    json j = json::object();
    for (const auto& pair : m_items) {
        j[pair.first] = SaveValueToJson(pair.second);
    }
    return j.dump(2);
}

bool SaveStore::FromJSON(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        if (!j.is_object()) return false;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_items.clear();
        for (auto it = j.begin(); it != j.end(); ++it) {
            m_items[it.key()] = JsonToSaveValue(it.value());
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool SaveStore::SaveToFile(const std::string& filePath) const {
    std::error_code ec;
    std::filesystem::path p(filePath);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    std::ofstream file(filePath);
    if (!file.is_open()) return false;

    file << ToJSON();
    return true;
}

bool SaveStore::LoadFromFile(const std::string& filePath) {
    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec)) return false;

    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    std::stringstream ss;
    ss << file.rdbuf();
    return FromJSON(ss.str());
}

SaveSystemClass::SaveSystemClass() {
    m_storageDir = "Saved/SaveGames";
}

SaveSystemClass& SaveSystemClass::Get() {
    static SaveSystemClass s_instance;
    return s_instance;
}

void SaveSystemClass::Initialize() {
    std::error_code ec;
    std::filesystem::create_directories(m_storageDir, ec);
}

void SaveSystemClass::Shutdown() {
}

void SaveSystemClass::SetStorageDirectory(const std::string& dirPath) {
    m_storageDir = dirPath;
    std::error_code ec;
    std::filesystem::create_directories(m_storageDir, ec);
}

const std::string& SaveSystemClass::GetStorageDirectory() const {
    return m_storageDir;
}

void SaveSystemClass::OnLevelLoaded(const std::string& levelName) {
    m_currentLevelName = levelName;
    // Current level data is cleared upon loading new level
    Local.RemoveAll();
}

void SaveSystemClass::OnLevelUnloaded(const std::string& levelName) {
    Local.RemoveAll();
}

bool SaveSystemClass::SaveGlobal(const std::string& slotName) {
    std::filesystem::path p = std::filesystem::path(m_storageDir) / (slotName + "_Global.json");
    return Global.SaveToFile(p.string());
}

bool SaveSystemClass::LoadGlobal(const std::string& slotName) {
    std::filesystem::path p = std::filesystem::path(m_storageDir) / (slotName + "_Global.json");
    return Global.LoadFromFile(p.string());
}

bool SaveSystemClass::SaveLocal(const std::string& levelName, const std::string& slotName) {
    std::string lvl = levelName.empty() ? m_currentLevelName : levelName;
    if (lvl.empty()) lvl = "CurrentLevel";
    std::string sanitizedLvl;
    for (char c : lvl) {
        if (isalnum((unsigned char)c) || c == '_' || c == '-') sanitizedLvl += c;
        else sanitizedLvl += '_';
    }

    std::filesystem::path p = std::filesystem::path(m_storageDir) / (slotName + "_Local_" + sanitizedLvl + ".json");
    return Local.SaveToFile(p.string());
}

bool SaveSystemClass::LoadLocal(const std::string& levelName, const std::string& slotName) {
    std::string lvl = levelName.empty() ? m_currentLevelName : levelName;
    if (lvl.empty()) lvl = "CurrentLevel";
    std::string sanitizedLvl;
    for (char c : lvl) {
        if (isalnum((unsigned char)c) || c == '_' || c == '-') sanitizedLvl += c;
        else sanitizedLvl += '_';
    }

    std::filesystem::path p = std::filesystem::path(m_storageDir) / (slotName + "_Local_" + sanitizedLvl + ".json");
    return Local.LoadFromFile(p.string());
}

bool SaveSystemClass::SaveAll(const std::string& slotName) {
    bool g = SaveGlobal(slotName);
    bool l = SaveLocal(m_currentLevelName, slotName);
    return g && l;
}

bool SaveSystemClass::LoadAll(const std::string& slotName) {
    bool g = LoadGlobal(slotName);
    bool l = LoadLocal(m_currentLevelName, slotName);
    return g || l;
}

} // namespace SaveSystem
} // namespace Eunoia
