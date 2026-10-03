#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <iostream>
#include <sstream>
#include <string_view>
#include <glm/glm.hpp>

namespace Eunoia {
namespace SaveSystem {

enum class SaveValueType {
    Null,
    Bool,
    Int64,
    Double,
    String,
    Vec2,
    Vec3,
    Vec4,
    Array,
    Object
};

class SaveValue {
public:
    using ArrayType = std::vector<SaveValue>;
    using ObjectType = std::unordered_map<std::string, SaveValue>;

    SaveValue() : m_type(SaveValueType::Null), m_data(std::monostate{}) {}
    SaveValue(std::nullptr_t) : m_type(SaveValueType::Null), m_data(std::monostate{}) {}

    SaveValue(bool val) : m_type(SaveValueType::Bool), m_data(val) {}

    SaveValue(int val) : m_type(SaveValueType::Int64), m_data((int64_t)val) {}
    SaveValue(int64_t val) : m_type(SaveValueType::Int64), m_data(val) {}
    SaveValue(uint32_t val) : m_type(SaveValueType::Int64), m_data((int64_t)val) {}
    SaveValue(uint64_t val) : m_type(SaveValueType::Int64), m_data((int64_t)val) {}

    SaveValue(float val) : m_type(SaveValueType::Double), m_data((double)val) {}
    SaveValue(double val) : m_type(SaveValueType::Double), m_data(val) {}

    SaveValue(const char* val) : m_type(SaveValueType::String), m_data(val ? std::string(val) : std::string()) {}
    SaveValue(const std::string& val) : m_type(SaveValueType::String), m_data(val) {}
    SaveValue(std::string_view val) : m_type(SaveValueType::String), m_data(std::string(val)) {}

    SaveValue(const glm::vec2& val) : m_type(SaveValueType::Vec2), m_data(val) {}
    SaveValue(const glm::vec3& val) : m_type(SaveValueType::Vec3), m_data(val) {}
    SaveValue(const glm::vec4& val) : m_type(SaveValueType::Vec4), m_data(val) {}

    SaveValue(const ArrayType& val) : m_type(SaveValueType::Array), m_data(val) {}
    SaveValue(const ObjectType& val) : m_type(SaveValueType::Object), m_data(val) {}

    template<typename T>
    SaveValue(const std::vector<T>& vec) : m_type(SaveValueType::Array) {
        ArrayType arr;
        arr.reserve(vec.size());
        for (const auto& item : vec) arr.push_back(SaveValue(item));
        m_data = arr;
    }

    SaveValueType GetType() const { return m_type; }
    bool IsNull() const { return m_type == SaveValueType::Null; }
    bool IsBool() const { return m_type == SaveValueType::Bool; }
    bool IsInt() const { return m_type == SaveValueType::Int64; }
    bool IsNumber() const { return m_type == SaveValueType::Int64 || m_type == SaveValueType::Double; }
    bool IsDouble() const { return m_type == SaveValueType::Double; }
    bool IsString() const { return m_type == SaveValueType::String; }
    bool IsVec2() const { return m_type == SaveValueType::Vec2; }
    bool IsVec3() const { return m_type == SaveValueType::Vec3; }
    bool IsVec4() const { return m_type == SaveValueType::Vec4; }
    bool IsArray() const { return m_type == SaveValueType::Array; }
    bool IsObject() const { return m_type == SaveValueType::Object; }

    int AsInt(int defaultVal = 0) const {
        if (m_type == SaveValueType::Int64) return (int)std::get<int64_t>(m_data);
        if (m_type == SaveValueType::Double) return (int)std::get<double>(m_data);
        if (m_type == SaveValueType::Bool) return std::get<bool>(m_data) ? 1 : 0;
        if (m_type == SaveValueType::String) {
            try { return std::stoi(std::get<std::string>(m_data)); } catch (...) { return defaultVal; }
        }
        return defaultVal;
    }

    int64_t AsInt64(int64_t defaultVal = 0) const {
        if (m_type == SaveValueType::Int64) return std::get<int64_t>(m_data);
        if (m_type == SaveValueType::Double) return (int64_t)std::get<double>(m_data);
        if (m_type == SaveValueType::Bool) return std::get<bool>(m_data) ? 1 : 0;
        if (m_type == SaveValueType::String) {
            try { return std::stoll(std::get<std::string>(m_data)); } catch (...) { return defaultVal; }
        }
        return defaultVal;
    }

    float AsFloat(float defaultVal = 0.0f) const {
        if (m_type == SaveValueType::Double) return (float)std::get<double>(m_data);
        if (m_type == SaveValueType::Int64) return (float)std::get<int64_t>(m_data);
        if (m_type == SaveValueType::Bool) return std::get<bool>(m_data) ? 1.0f : 0.0f;
        if (m_type == SaveValueType::String) {
            try { return std::stof(std::get<std::string>(m_data)); } catch (...) { return defaultVal; }
        }
        return defaultVal;
    }

    double AsDouble(double defaultVal = 0.0) const {
        if (m_type == SaveValueType::Double) return std::get<double>(m_data);
        if (m_type == SaveValueType::Int64) return (double)std::get<int64_t>(m_data);
        if (m_type == SaveValueType::Bool) return std::get<bool>(m_data) ? 1.0 : 0.0;
        if (m_type == SaveValueType::String) {
            try { return std::stod(std::get<std::string>(m_data)); } catch (...) { return defaultVal; }
        }
        return defaultVal;
    }

    bool AsBool(bool defaultVal = false) const {
        if (m_type == SaveValueType::Bool) return std::get<bool>(m_data);
        if (m_type == SaveValueType::Int64) return std::get<int64_t>(m_data) != 0;
        if (m_type == SaveValueType::Double) return std::get<double>(m_data) != 0.0;
        if (m_type == SaveValueType::String) {
            const std::string& s = std::get<std::string>(m_data);
            if (s == "true" || s == "1" || s == "True" || s == "TRUE") return true;
            if (s == "false" || s == "0" || s == "False" || s == "FALSE") return false;
            return !s.empty();
        }
        return defaultVal;
    }

    std::string AsString(const std::string& defaultVal = "") const {
        if (m_type == SaveValueType::String) return std::get<std::string>(m_data);
        if (m_type == SaveValueType::Int64) return std::to_string(std::get<int64_t>(m_data));
        if (m_type == SaveValueType::Double) {
            std::string s = std::to_string(std::get<double>(m_data));
            while (s.size() > 1 && s.find('.') != std::string::npos && (s.back() == '0' || s.back() == '.')) {
                bool wasDot = (s.back() == '.');
                s.pop_back();
                if (wasDot) break;
            }
            return s;
        }
        if (m_type == SaveValueType::Bool) return std::get<bool>(m_data) ? "true" : "false";
        if (m_type == SaveValueType::Vec2) {
            glm::vec2 v = std::get<glm::vec2>(m_data);
            return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
        }
        if (m_type == SaveValueType::Vec3) {
            glm::vec3 v = std::get<glm::vec3>(m_data);
            return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
        }
        if (m_type == SaveValueType::Vec4) {
            glm::vec4 v = std::get<glm::vec4>(m_data);
            return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ", " + std::to_string(v.w) + ")";
        }
        if (m_type == SaveValueType::Null) return "";
        return defaultVal;
    }

    glm::vec2 AsVec2(const glm::vec2& defaultVal = glm::vec2(0.0f)) const {
        if (m_type == SaveValueType::Vec2) return std::get<glm::vec2>(m_data);
        return defaultVal;
    }

    glm::vec3 AsVec3(const glm::vec3& defaultVal = glm::vec3(0.0f)) const {
        if (m_type == SaveValueType::Vec3) return std::get<glm::vec3>(m_data);
        return defaultVal;
    }

    glm::vec4 AsVec4(const glm::vec4& defaultVal = glm::vec4(0.0f)) const {
        if (m_type == SaveValueType::Vec4) return std::get<glm::vec4>(m_data);
        return defaultVal;
    }

    const ArrayType& AsArray() const {
        static const ArrayType s_emptyArray;
        if (m_type == SaveValueType::Array) return std::get<ArrayType>(m_data);
        return s_emptyArray;
    }

    const ObjectType& AsObject() const {
        static const ObjectType s_emptyObject;
        if (m_type == SaveValueType::Object) return std::get<ObjectType>(m_data);
        return s_emptyObject;
    }

    // Automatic implicit conversions
    operator int() const { return AsInt(); }
    operator int64_t() const { return AsInt64(); }
    operator float() const { return AsFloat(); }
    operator double() const { return AsDouble(); }
    operator bool() const { return AsBool(); }
    operator std::string() const { return AsString(); }
    operator glm::vec2() const { return AsVec2(); }
    operator glm::vec3() const { return AsVec3(); }
    operator glm::vec4() const { return AsVec4(); }

    std::string ToString() const { return AsString(); }

    bool operator==(const SaveValue& other) const {
        if (m_type != other.m_type) return false;
        return m_data == other.m_data;
    }

    bool operator!=(const SaveValue& other) const {
        return !(*this == other);
    }

    // JSON serialization
    std::string ToJSON() const;
    static SaveValue FromJSON(const std::string& jsonStr);

private:
    SaveValueType m_type = SaveValueType::Null;
    std::variant<
        std::monostate,
        bool,
        int64_t,
        double,
        std::string,
        glm::vec2,
        glm::vec3,
        glm::vec4,
        ArrayType,
        ObjectType
    > m_data;
};

inline std::ostream& operator<<(std::ostream& os, const SaveValue& val) {
    os << val.AsString();
    return os;
}

} // namespace SaveSystem
} // namespace Eunoia
