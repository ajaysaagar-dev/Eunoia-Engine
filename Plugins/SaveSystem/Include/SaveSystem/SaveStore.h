#pragma once
#include "SaveValue.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>
#include <functional>

namespace Eunoia {
namespace SaveSystem {

class SaveStore {
public:
    SaveStore() = default;
    ~SaveStore() = default;

    SaveStore(const SaveStore& other) {
        std::lock_guard<std::mutex> lock(other.m_mutex);
        m_items = other.m_items;
    }

    SaveStore& operator=(const SaveStore& other) {
        if (this != &other) {
            std::unique_lock<std::mutex> lockA(m_mutex, std::defer_lock);
            std::unique_lock<std::mutex> lockB(other.m_mutex, std::defer_lock);
            std::lock(lockA, lockB);
            m_items = other.m_items;
        }
        return *this;
    }

    void SetItem(const std::string& key, const SaveValue& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_items[key] = value;
    }

    template<typename T>
    void SetItem(const std::string& key, const T& value) {
        SetItem(key, SaveValue(value));
    }

    SaveValue GetItem(const std::string& key, const SaveValue& defaultValue = SaveValue()) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_items.find(key);
        if (it != m_items.end()) {
            return it->second;
        }
        return defaultValue;
    }

    template<typename T>
    T GetItem(const std::string& key, const T& defaultValue) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_items.find(key);
        if (it != m_items.end()) {
            return (T)it->second;
        }
        return defaultValue;
    }

    std::unordered_map<std::string, SaveValue> GetItems() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items;
    }

    bool HasItem(const std::string& key) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.find(key) != m_items.end();
    }

    bool Contains(const std::string& key) const {
        return HasItem(key);
    }

    bool RemoveItem(const std::string& key) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.erase(key) > 0;
    }

    void RemoveAll() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_items.clear();
    }

    void Clear() {
        RemoveAll();
    }

    size_t Count() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.size();
    }

    bool IsEmpty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.empty();
    }

    std::vector<std::string> GetKeys() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::string> keys;
        keys.reserve(m_items.size());
        for (const auto& pair : m_items) {
            keys.push_back(pair.first);
        }
        return keys;
    }

    SaveValue& operator[](const std::string& key) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items[key];
    }

    bool SaveToFile(const std::string& filePath) const;
    bool LoadFromFile(const std::string& filePath);

    std::string ToJSON() const;
    bool FromJSON(const std::string& jsonStr);

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, SaveValue> m_items;
};

} // namespace SaveSystem
} // namespace Eunoia
