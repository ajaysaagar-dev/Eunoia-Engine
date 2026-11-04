#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <cmath>
#include <random>
#include <typeindex>
#include <iostream>
#include <EngineScene/TransformTypes.h>

// Forward declarations
struct GameObject;
class EunoiaBehaviour;

// ============================================================================
// Space & Coordinate Enums
// ============================================================================
enum class Space {
    Local,
    World
};

// ============================================================================
// Color Type with Standard Static Presets (Docs/Behaviuor.md)
// ============================================================================
struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    constexpr Color() : r(1.0f), g(1.0f), b(1.0f), a(1.0f) {}
    constexpr Color(float inR, float inG, float inB, float inA = 1.0f) : r(inR), g(inG), b(inB), a(inA) {}
    Color(const glm::vec3& v, float inA = 1.0f) : r(v.x), g(v.y), b(v.z), a(inA) {}
    Color(const glm::vec4& v) : r(v.x), g(v.y), b(v.z), a(v.w) {}

    operator glm::vec4() const { return glm::vec4(r, g, b, a); }
    operator glm::vec3() const { return glm::vec3(r, g, b); }

    static const Color Red;
    static const Color Green;
    static const Color Blue;
    static const Color Yellow;
    static const Color Cyan;
    static const Color Magenta;
    static const Color White;
    static const Color Black;
    static const Color Gray;
    static const Color Orange;
    static const Color Clear;
};

inline const Color Color::Red     = Color(1.0f, 0.0f, 0.0f, 1.0f);
inline const Color Color::Green   = Color(0.0f, 1.0f, 0.0f, 1.0f);
inline const Color Color::Blue    = Color(0.0f, 0.0f, 1.0f, 1.0f);
inline const Color Color::Yellow  = Color(1.0f, 1.0f, 0.0f, 1.0f);
inline const Color Color::Cyan    = Color(0.0f, 1.0f, 1.0f, 1.0f);
inline const Color Color::Magenta = Color(1.0f, 0.0f, 1.0f, 1.0f);
inline const Color Color::White   = Color(1.0f, 1.0f, 1.0f, 1.0f);
inline const Color Color::Black   = Color(0.0f, 0.0f, 0.0f, 1.0f);
inline const Color Color::Gray    = Color(0.5f, 0.5f, 0.5f, 1.0f);
inline const Color Color::Orange  = Color(1.0f, 0.5f, 0.0f, 1.0f);
inline const Color Color::Clear   = Color(0.0f, 0.0f, 0.0f, 0.0f);

// ============================================================================
// Math Helpers (Docs/Behaviuor.md Section 16)
// ============================================================================
namespace Math {

inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

inline glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, float t) {
    return a + (b - a) * t;
}

inline float InverseLerp(float a, float b, float v) {
    return (std::abs(b - a) > 1e-6f) ? std::clamp((v - a) / (b - a), 0.0f, 1.0f) : 0.0f;
}

inline float Remap(float v, float inMin, float inMax, float outMin, float outMax) {
    float t = (std::abs(inMax - inMin) > 1e-6f) ? (v - inMin) / (inMax - inMin) : 0.0f;
    return outMin + t * (outMax - outMin);
}

inline float Clamp(float v, float lo, float hi) {
    return std::clamp(v, lo, hi);
}

inline int Clamp(int v, int lo, int hi) {
    return std::clamp(v, lo, hi);
}

inline float Clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

inline float MoveTowards(float current, float target, float maxDelta) {
    if (std::abs(target - current) <= maxDelta) return target;
    return current + std::copysign(maxDelta, target - current);
}

inline glm::vec3 MoveTowards(const glm::vec3& current, const glm::vec3& target, float maxDelta) {
    glm::vec3 toTarget = target - current;
    float dist = glm::length(toTarget);
    if (dist <= maxDelta || dist < 1e-6f) return target;
    return current + (toTarget / dist) * maxDelta;
}

inline float Repeat(float t, float length) {
    if (length <= 0.0f) return 0.0f;
    return std::clamp(t - std::floor(t / length) * length, 0.0f, length);
}

inline float PingPong(float t, float length) {
    if (length <= 0.0f) return 0.0f;
    t = Repeat(t, length * 2.0f);
    return length - std::abs(t - length);
}

inline float DeltaAngle(float current, float target) {
    float delta = Repeat((target - current), 360.0f);
    if (delta > 180.0f) delta -= 360.0f;
    return delta;
}

inline float LerpAngle(float a, float b, float t) {
    float delta = DeltaAngle(a, b);
    return a + delta * Clamp01(t);
}

inline bool Approximately(float a, float b, float epsilon = 1e-5f) {
    return std::abs(a - b) <= epsilon;
}

inline float Sign(float v) {
    return (v >= 0.0f) ? 1.0f : -1.0f;
}

inline float SmoothDamp(float current, float target, float& currentVelocity, float smoothTime, float maxSpeed = 1e9f, float deltaTime = 0.01667f) {
    smoothTime = std::max(0.0001f, smoothTime);
    float omega = 2.0f / smoothTime;
    float x = omega * deltaTime;
    float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = current - target;
    float originalTo = target;

    float maxChange = maxSpeed * smoothTime;
    change = std::clamp(change, -maxChange, maxChange);
    target = current - change;

    float temp = (currentVelocity + omega * change) * deltaTime;
    currentVelocity = (currentVelocity - omega * temp) * exp;
    float output = target + (change + temp) * exp;

    if (originalTo - current > 0.0f == output > originalTo) {
        output = originalTo;
        currentVelocity = (output - originalTo) / deltaTime;
    }
    return output;
}

inline glm::vec3 SmoothDamp(const glm::vec3& current, const glm::vec3& target, glm::vec3& currentVelocity, float smoothTime, float maxSpeed = 1e9f, float deltaTime = 0.01667f) {
    return glm::vec3(
        SmoothDamp(current.x, target.x, currentVelocity.x, smoothTime, maxSpeed, deltaTime),
        SmoothDamp(current.y, target.y, currentVelocity.y, smoothTime, maxSpeed, deltaTime),
        SmoothDamp(current.z, target.z, currentVelocity.z, smoothTime, maxSpeed, deltaTime)
    );
}

} // namespace Math

// ============================================================================
// Random Helpers (Docs/Behaviuor.md Section 16)
// ============================================================================
namespace Random {

inline std::mt19937& GetEngine() {
    static std::mt19937 engine(1337);
    return engine;
}

inline void SetSeed(unsigned int seed) {
    GetEngine().seed(seed);
}

inline float Range(float min, float max) {
    if (min > max) std::swap(min, max);
    std::uniform_real_distribution<float> dist(min, max);
    return dist(GetEngine());
}

inline int Range(int min, int max) {
    if (min > max) std::swap(min, max);
    std::uniform_int_distribution<int> dist(min, max);
    return dist(GetEngine());
}

inline bool Chance(float probability = 0.5f) {
    return Range(0.0f, 1.0f) < probability;
}

inline glm::vec3 InsideUnitSphere() {
    while (true) {
        glm::vec3 p(Range(-1.0f, 1.0f), Range(-1.0f, 1.0f), Range(-1.0f, 1.0f));
        if (glm::dot(p, p) <= 1.0f && glm::dot(p, p) > 1e-6f) return p;
    }
}

inline glm::vec3 OnUnitSphere() {
    return glm::normalize(InsideUnitSphere());
}

inline glm::vec2 InsideUnitCircle() {
    while (true) {
        glm::vec2 p(Range(-1.0f, 1.0f), Range(-1.0f, 1.0f));
        if (glm::dot(p, p) <= 1.0f && glm::dot(p, p) > 1e-6f) return p;
    }
}

inline glm::vec2 Direction2D() {
    float angle = Range(0.0f, 6.2831853f);
    return glm::vec2(std::cos(angle), std::sin(angle));
}

} // namespace Random

// ============================================================================
// Cached Parameter ID for Animation Graphs (Docs/Behaviuor.md Section 8)
// ============================================================================
struct ParamId {
    uint32_t hash = 0;
    std::string name;

    static constexpr uint32_t HashFNV1a(const char* str) {
        uint32_t h = 2166136261u;
        while (str && *str) {
            h ^= static_cast<uint8_t>(*str++);
            h *= 16777619u;
        }
        return h;
    }

    constexpr ParamId() : hash(0), name("") {}
    ParamId(const char* str) : hash(HashFNV1a(str)), name(str ? str : "") {}
    ParamId(const std::string& str) : hash(HashFNV1a(str.c_str())), name(str) {}

    bool operator==(const ParamId& o) const { return hash == o.hash; }
    bool operator!=(const ParamId& o) const { return hash != o.hash; }
    operator uint32_t() const { return hash; }
};

// ============================================================================
// Safe Object Reference (Docs/Behaviuor.md Section 9.4)
// Weak, generation-checked handle that avoids dangling pointers across frames.
// ============================================================================
class ObjectRef {
public:
    GameObject* m_ptr = nullptr;
    int m_id = -1;

    ObjectRef() = default;
    ObjectRef(GameObject* obj);
    ObjectRef(std::nullptr_t) : m_ptr(nullptr), m_id(-1) {}

    bool IsValid() const;
    GameObject* Get() const;
    GameObject* operator->() const { return Get(); }
    GameObject& operator*() const { return *Get(); }
    explicit operator bool() const { return IsValid(); }

    bool operator==(const ObjectRef& other) const {
        return m_ptr == other.m_ptr && m_id == other.m_id;
    }
    bool operator!=(const ObjectRef& other) const {
        return !(*this == other);
    }
    bool operator==(const GameObject* other) const;
    bool operator!=(const GameObject* other) const {
        return !(*this == other);
    }

    void Reset() {
        m_ptr = nullptr;
        m_id = -1;
    }
};

// ============================================================================
// Collision & Trigger Info (Docs/Behaviuor.md Section 11)
// ============================================================================
struct CollisionInfo {
    GameObject* other = nullptr;
    glm::vec3 point{0.0f, 0.0f, 0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 impulse{0.0f, 0.0f, 0.0f};
    float relativeSpeed = 0.0f;
};

// ============================================================================
// Typed Message Bus / Events System (Docs/Behaviuor.md Section 15)
// ============================================================================
namespace Events {

struct IEventHandler {
    virtual ~IEventHandler() = default;
};

template<typename T>
struct EventHandler : public IEventHandler {
    std::function<void(const T&)> callback;
    EventHandler(std::function<void(const T&)> cb) : callback(std::move(cb)) {}
};

inline std::unordered_map<std::type_index, std::unordered_map<void*, std::shared_ptr<IEventHandler>>>& GetSubscribers() {
    static std::unordered_map<std::type_index, std::unordered_map<void*, std::shared_ptr<IEventHandler>>> s_subs;
    return s_subs;
}

template<typename T>
inline void Subscribe(void* subscriber, std::function<void(const T&)> handler) {
    if (!subscriber || !handler) return;
    auto typeIdx = std::type_index(typeid(T));
    GetSubscribers()[typeIdx][subscriber] = std::make_shared<EventHandler<T>>(std::move(handler));
}

template<typename T>
inline void Unsubscribe(void* subscriber) {
    if (!subscriber) return;
    auto typeIdx = std::type_index(typeid(T));
    auto it = GetSubscribers().find(typeIdx);
    if (it != GetSubscribers().end()) {
        it->second.erase(subscriber);
    }
}

inline void UnsubscribeAll(void* subscriber) {
    if (!subscriber) return;
    for (auto& pair : GetSubscribers()) {
        pair.second.erase(subscriber);
    }
}

template<typename T>
inline void Publish(const T& event) {
    auto typeIdx = std::type_index(typeid(T));
    auto it = GetSubscribers().find(typeIdx);
    if (it != GetSubscribers().end()) {
        auto list = it->second;
        for (auto& pair : list) {
            if (auto h = std::dynamic_pointer_cast<EventHandler<T>>(pair.second)) {
                h->callback(event);
            }
        }
    }
}

inline std::vector<std::function<void()>>& GetDeferredQueue() {
    static std::vector<std::function<void()>> s_deferred;
    return s_deferred;
}

template<typename T>
inline void PublishDeferred(const T& event) {
    GetDeferredQueue().push_back([event]() {
        Publish<T>(event);
    });
}

inline void ProcessDeferred() {
    auto q = std::move(GetDeferredQueue());
    GetDeferredQueue().clear();
    for (auto& fn : q) {
        if (fn) fn();
    }
}

} // namespace Events

// ============================================================================
// Debug Draw System (Docs/Behaviuor.md Section 17)
// ============================================================================
namespace Debug {

struct DebugItem {
    enum class Type { Line, Arrow, Sphere, Box, Capsule, Text, Ray };
    Type type;
    glm::vec3 p1{0.0f};
    glm::vec3 p2{0.0f};
    float radius = 1.0f;
    Color color = Color::White;
    float remainingTime = 0.0f;
    std::string text;
};

inline std::vector<DebugItem>& GetQueue() {
    static std::vector<DebugItem> s_queue;
    return s_queue;
}

inline void DrawLine(const glm::vec3& a, const glm::vec3& b, const Color& color = Color::White, float seconds = 0.0f) {
    GetQueue().push_back({ DebugItem::Type::Line, a, b, 0.0f, color, seconds, "" });
}

inline void DrawArrow(const glm::vec3& from, const glm::vec3& direction, const Color& color = Color::Green, float seconds = 0.0f) {
    GetQueue().push_back({ DebugItem::Type::Arrow, from, from + direction, 0.0f, color, seconds, "" });
}

inline void DrawSphere(const glm::vec3& center, float radius, const Color& color = Color::Yellow, float seconds = 0.0f) {
    GetQueue().push_back({ DebugItem::Type::Sphere, center, glm::vec3(0.0f), radius, color, seconds, "" });
}

inline void DrawBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), const Color& color = Color::Cyan, float seconds = 0.0f) {
    GetQueue().push_back({ DebugItem::Type::Box, center, halfExtents, 0.0f, color, seconds, "" });
}

inline void DrawCapsule(const glm::vec3& a, const glm::vec3& b, float radius, const Color& color = Color::Magenta, float seconds = 0.0f) {
    GetQueue().push_back({ DebugItem::Type::Capsule, a, b, radius, color, seconds, "" });
}

inline void DrawText(const glm::vec3& worldPos, const std::string& text, const Color& color = Color::White, float seconds = 0.0f) {
    GetQueue().push_back({ DebugItem::Type::Text, worldPos, glm::vec3(0.0f), 0.0f, color, seconds, text });
}

inline void DrawRay(const glm::vec3& origin, const glm::vec3& dir, float length = 1.0f, const Color& color = Color::White, float seconds = 0.0f) {
    DrawLine(origin, origin + glm::normalize(dir) * length, color, seconds);
}

inline void Pause() {
    // Break into editor pause
}

inline void Update(float dt) {
    auto& q = GetQueue();
    for (auto it = q.begin(); it != q.end(); ) {
        it->remainingTime -= dt;
        if (it->remainingTime <= 0.0f) {
            it = q.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace Debug

// ============================================================================
// Audio API Wrappers (Docs/Behaviuor.md Section 18)
// ============================================================================
namespace Audio {

inline void Play(const std::string& clipName, float volume = 1.0f, float pitch = 1.0f) {
    (void)clipName; (void)volume; (void)pitch;
}

inline void PlayAt(const std::string& clipName, const glm::vec3& worldPos, float volume = 1.0f, float pitch = 1.0f) {
    (void)clipName; (void)worldPos; (void)volume; (void)pitch;
}

inline void PlayAt(const std::string& clipName, const TransformVector3& worldPos, float volume = 1.0f, float pitch = 1.0f) {
    PlayAt(clipName, glm::vec3(worldPos.x, worldPos.y, worldPos.z), volume, pitch);
}

inline void SetBusVolume(const std::string& busName, float volume) {
    (void)busName; (void)volume;
}

inline void StopAll(const std::string& busName = "") {
    (void)busName;
}

} // namespace Audio

// ============================================================================
// Level API Wrappers (Docs/Behaviuor.md Section 19)
// Note: Level is aliased to Scene in Scene.h: using Level = Scene;
// Level::Load, Level::Reload, Level::GetCurrentName are exposed on Scene.
// ============================================================================

// ============================================================================
// SaveGame API Wrappers (Docs/Behaviuor.md Section 19)
// ============================================================================
namespace SaveGame {

inline std::unordered_map<std::string, std::string>& GetStorage() {
    static std::unordered_map<std::string, std::string> s_data;
    return s_data;
}

inline void Set(const std::string& key, const std::string& val) { GetStorage()[key] = val; }
inline void Set(const std::string& key, int val) { GetStorage()[key] = std::to_string(val); }
inline void Set(const std::string& key, float val) { GetStorage()[key] = std::to_string(val); }
inline void Set(const std::string& key, bool val) { GetStorage()[key] = val ? "1" : "0"; }

template<typename T>
inline T Get(const std::string& key, const T& defaultVal = T()) {
    auto it = GetStorage().find(key);
    if (it == GetStorage().end()) return defaultVal;
    if constexpr (std::is_same_v<T, std::string>) {
        return it->second;
    } else if constexpr (std::is_same_v<T, int>) {
        return std::stoi(it->second);
    } else if constexpr (std::is_same_v<T, float>) {
        return std::stof(it->second);
    } else if constexpr (std::is_same_v<T, bool>) {
        return it->second == "1" || it->second == "true";
    }
    return defaultVal;
}

inline bool Save(const std::string& slotName = "default") { (void)slotName; return true; }
inline bool Load(const std::string& slotName = "default") { (void)slotName; return true; }
inline bool Exists(const std::string& slotName = "default") { (void)slotName; return true; }
inline bool Delete(const std::string& slotName = "default") { (void)slotName; return true; }

} // namespace SaveGame

// ============================================================================
// Coroutines & Tweens (Docs/Behaviuor.md Section 14)
// ============================================================================
struct CoroutineHandle {
    uint32_t id = 0;
    bool operator==(const CoroutineHandle& o) const { return id == o.id; }
    bool operator!=(const CoroutineHandle& o) const { return id != o.id; }
};

enum class Ease {
    Linear,
    InQuad,
    OutQuad,
    InOutQuad,
    InCubic,
    OutCubic,
    InOutCubic,
    InBack,
    OutBack,
    InElastic,
    OutElastic,
    OutBounce
};

namespace Tween {

struct TweenInstance {
    uint32_t id = 0;
    float elapsed = 0.0f;
    float duration = 1.0f;
    Ease ease = Ease::Linear;
    std::function<void(float)> onUpdate;
    std::function<void()> onComplete;
    bool finished = false;

    TweenInstance& OnComplete(std::function<void()> cb) {
        onComplete = std::move(cb);
        return *this;
    }
};

inline std::vector<TweenInstance>& GetActiveTweens() {
    static std::vector<TweenInstance> s_tweens;
    return s_tweens;
}

inline float EvaluateEase(Ease ease, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (ease) {
        case Ease::Linear: return t;
        case Ease::InQuad: return t * t;
        case Ease::OutQuad: return t * (2.0f - t);
        case Ease::InOutQuad: return (t < 0.5f) ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
        case Ease::InCubic: return t * t * t;
        case Ease::OutCubic: { float f = t - 1.0f; return f * f * f + 1.0f; }
        case Ease::InOutCubic: return (t < 0.5f) ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
        default: return t;
    }
}

inline TweenInstance& Float(float from, float to, float duration, Ease ease, std::function<void(float)> onUpdate) {
    static uint32_t s_id = 1;
    TweenInstance t;
    t.id = s_id++;
    t.duration = duration;
    t.ease = ease;
    t.onUpdate = [from, to, onUpdate, ease](float progress) {
        float factor = EvaluateEase(ease, progress);
        if (onUpdate) onUpdate(from + (to - from) * factor);
    };
    GetActiveTweens().push_back(std::move(t));
    return GetActiveTweens().back();
}

inline TweenInstance& Move(GameObject* target, const glm::vec3& targetPos, float duration, Ease ease = Ease::OutCubic);

} // namespace Tween

// ============================================================================
// Property Reflection and Registration Macros (Docs/Behaviuor.md Section 21)
// ============================================================================
struct Range {
    float min = 0.0f;
    float max = 1.0f;
    constexpr Range(float lo, float hi) : min(lo), max(hi) {}
};

struct Tooltip {
    const char* text = "";
    constexpr Tooltip(const char* t) : text(t) {}
};

#define EUNOIA_PROPERTY(type, name, ...) type name = type{__VA_ARGS__}
#define EUNOIA_FUNCTION(name)
#define EUNOIA_REGISTER_BEHAVIOUR(cls)
