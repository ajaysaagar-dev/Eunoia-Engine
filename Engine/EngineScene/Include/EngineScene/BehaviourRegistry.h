#pragma once
#include <EngineScene/EunoiaBehaviour.h>
#include <string>
#include <unordered_map>
#include <map>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>
#include <cmath>

struct BehaviourClassInfo {
    std::string className;
    std::string displayName;
    std::string description;
    std::string sourceHeader;
    std::string sourceCpp;
    bool isNative = false;
    std::function<std::unique_ptr<EunoiaBehaviour>()> factory;
};

// ============================================================================
// Built-in Behaviours (dev.md Section 1, 7, 37)
// ============================================================================

// 1. RotatorBehaviour — Smooth continuous rotation
class RotatorBehaviour : public EunoiaBehaviour {
public:
    float rotationSpeed = 45.0f;
    glm::vec3 rotationAxis = glm::vec3(0.0f, 1.0f, 0.0f);
    bool pingPong = false;

    RotatorBehaviour() {
        m_className = "RotatorBehaviour";
        m_displayName = "Rotator Behaviour";
        RegisterProperties();
    }

    void RegisterProperties() override {
        m_properties.clear();
        RegisterProperty("Rotation Speed", &rotationSpeed, "Movement", -360.0f, 360.0f);
        RegisterProperty("Rotation Axis", &rotationAxis, "Movement");
        RegisterProperty("Ping Pong", &pingPong, "Movement");
    }

    void ResetToDefaults() override {
        rotationSpeed = 45.0f;
        rotationAxis = glm::vec3(0.0f, 1.0f, 0.0f);
        pingPong = false;
        RegisterProperties();
        if (m_scene) ResolveReferences(*m_scene);
    }

    std::unique_ptr<EunoiaBehaviour> Clone() const override {
        auto clone = std::make_unique<RotatorBehaviour>(*this);
        clone->CopyPropertiesFrom(*this);
        return clone;
    }

    void Update(float deltaTime) override;
};

// 2. LightFlickerBehaviour — Dynamic lighting pulse/flicker
class LightFlickerBehaviour : public EunoiaBehaviour {
public:
    float minIntensity = 0.5f;
    float maxIntensity = 3.5f;
    float flickerFrequency = 8.0f;
    Light* targetLight = nullptr;

    LightFlickerBehaviour() {
        m_className = "LightFlickerBehaviour";
        m_displayName = "Light Flicker Behaviour";
        RegisterProperties();
    }

    void RegisterProperties() override {
        m_properties.clear();
        RegisterProperty("Min Intensity", &minIntensity, "Lighting", 0.0f, 20.0f);
        RegisterProperty("Max Intensity", &maxIntensity, "Lighting", 0.0f, 20.0f);
        RegisterProperty("Flicker Frequency", &flickerFrequency, "Lighting", 0.1f, 30.0f);
        RegisterReference("Target Light", &targetLight, ObjectRefType::Light, "References");
    }

    void ResetToDefaults() override {
        minIntensity = 0.5f;
        maxIntensity = 3.5f;
        flickerFrequency = 8.0f;
        targetLight = nullptr;
        RegisterProperties();
        if (m_scene) ResolveReferences(*m_scene);
    }

    std::unique_ptr<EunoiaBehaviour> Clone() const override {
        auto clone = std::make_unique<LightFlickerBehaviour>(*this);
        clone->CopyPropertiesFrom(*this);
        return clone;
    }

    void Update(float deltaTime) override;

private:
    float m_timeAccum = 0.0f;
};

// 3. DoorController (dev.md Section 37)
class DoorController : public EunoiaBehaviour {
public:
    float OpenSpeed = 2.5f;
    int RequiredKeys = 1;
    bool Locked = false;

    Light* WarningLight = nullptr;
    MeshRenderer* DoorMesh = nullptr;

    DoorController() {
        m_className = "DoorController";
        m_displayName = "Door Controller";
        RegisterProperties();
    }

    void RegisterProperties() override {
        m_properties.clear();
        RegisterProperty("Open Speed", &OpenSpeed, "Door Settings", 0.1f, 10.0f);
        RegisterProperty("Required Keys", &RequiredKeys, "Door Settings", 0, 10);
        RegisterProperty("Locked", &Locked, "Door Settings");
        RegisterReference("Warning Light", &WarningLight, ObjectRefType::Light, "References");
        RegisterReference("Door Mesh", &DoorMesh, ObjectRefType::Mesh, "References");
    }

    void ResetToDefaults() override {
        OpenSpeed = 2.5f;
        RequiredKeys = 1;
        Locked = false;
        WarningLight = nullptr;
        DoorMesh = nullptr;
        RegisterProperties();
        if (m_scene) ResolveReferences(*m_scene);
    }

    std::unique_ptr<EunoiaBehaviour> Clone() const override {
        auto clone = std::make_unique<DoorController>(*this);
        clone->CopyPropertiesFrom(*this);
        return clone;
    }

    void Start() override;
    void Update(float deltaTime) override;

private:
    float m_currentAngle = 0.0f;
};

// 4. PlayerController (dev.md Section 1, 7)
class PlayerController : public EunoiaBehaviour {
public:
    int Health = 100;
    float MoveSpeed = 5.0f;
    bool CanAttack = true;
    std::string PlayerName = "Player";
    Camera* TargetCamera = nullptr;

    PlayerController() {
        m_className = "PlayerController";
        m_displayName = "Player Controller";
        RegisterProperties();
    }

    void RegisterProperties() override {
        m_properties.clear();
        RegisterProperty("Health", &Health, "Combat", 0, 1000);
        RegisterProperty("Move Speed", &MoveSpeed, "Movement", 0.0f, 50.0f);
        RegisterProperty("Can Attack", &CanAttack, "Combat");
        RegisterProperty("Player Name", &PlayerName, "General");
        RegisterReference("Target Camera", &TargetCamera, ObjectRefType::Camera, "References");
    }

    void ResetToDefaults() override {
        Health = 100;
        MoveSpeed = 5.0f;
        CanAttack = true;
        PlayerName = "Player";
        TargetCamera = nullptr;
        RegisterProperties();
        if (m_scene) ResolveReferences(*m_scene);
    }

    std::unique_ptr<EunoiaBehaviour> Clone() const override {
        auto clone = std::make_unique<PlayerController>(*this);
        clone->CopyPropertiesFrom(*this);
        return clone;
    }

    void Start() override;
    void Update(float deltaTime) override;
};

// 5. EnemyController (dev.md Section 7)
class EnemyController : public EunoiaBehaviour {
public:
    int Health = 100;
    float MoveSpeed = 3.0f;
    bool CanAttack = true;
    std::string EnemyName = "Enemy";
    GameObject* TargetActor = nullptr;

    EnemyController() {
        m_className = "EnemyController";
        m_displayName = "Enemy Controller";
        RegisterProperties();
    }

    void RegisterProperties() override {
        m_properties.clear();
        RegisterProperty("Health", &Health, "Combat", 0, 1000);
        RegisterProperty("Move Speed", &MoveSpeed, "Movement", 0.0f, 50.0f);
        RegisterProperty("Can Attack", &CanAttack, "Combat");
        RegisterProperty("Enemy Name", &EnemyName, "General");
        RegisterReference("Target Actor", &TargetActor, ObjectRefType::Actor, "References");
    }

    void ResetToDefaults() override {
        Health = 100;
        MoveSpeed = 3.0f;
        CanAttack = true;
        EnemyName = "Enemy";
        TargetActor = nullptr;
        RegisterProperties();
        if (m_scene) ResolveReferences(*m_scene);
    }

    std::unique_ptr<EunoiaBehaviour> Clone() const override {
        auto clone = std::make_unique<EnemyController>(*this);
        clone->CopyPropertiesFrom(*this);
        return clone;
    }

    void Start() override;
    void Update(float deltaTime) override;
};

// 6. Dynamic Script Behaviour (for user-created .cpp scripts)
class DynamicScriptBehaviour : public EunoiaBehaviour {
public:
    DynamicScriptBehaviour(const std::string& className = "CustomBehaviour", const std::string& sourceCpp = "") {
        m_className = className;
        m_displayName = className;
        m_sourceCppPath = sourceCpp;
        if (!m_sourceCppPath.empty()) {
            ParsePropertiesFromCpp(m_sourceCppPath);
        } else {
            RegisterProperties();
        }
    }

    struct PropertyDefaultValue {
        BehaviourPropertyType type = BehaviourPropertyType::Float;
        bool boolVal = false;
        int intVal = 0;
        int64_t int64Val = 0;
        float floatVal = 0.0f;
        double doubleVal = 0.0;
        std::string stringVal;
        glm::vec2 vec2Val{0.0f};
        glm::vec3 vec3Val{0.0f};
        glm::vec4 vec4Val{0.0f};
        int targetId = -1;
    };

    std::unique_ptr<EunoiaBehaviour> Clone() const override {
        auto clone = std::make_unique<DynamicScriptBehaviour>(m_className, m_sourceCppPath);
        clone->m_displayName = m_displayName;
        clone->m_floatStorage = m_floatStorage;
        clone->m_intStorage = m_intStorage;
        clone->m_int64Storage = m_int64Storage;
        clone->m_boolStorage = m_boolStorage;
        clone->m_stringStorage = m_stringStorage;
        clone->m_doubleStorage = m_doubleStorage;
        clone->m_vec2Storage = m_vec2Storage;
        clone->m_vec3Storage = m_vec3Storage;
        clone->m_vec4Storage = m_vec4Storage;
        clone->m_refStorage = m_refStorage;
        clone->m_defaultPropertyValues = m_defaultPropertyValues;
        clone->m_sourceTimestamp = m_sourceTimestamp;
        clone->m_hasRotation = m_hasRotation;
        clone->m_rotateAxis = m_rotateAxis;
        clone->m_defaultSpeed = m_defaultSpeed;
        clone->m_hasTranslation = m_hasTranslation;
        clone->m_translateAxis = m_translateAxis;
        clone->m_defaultMoveSpeed = m_defaultMoveSpeed;
        clone->m_hasInput = m_hasInput;
        clone->m_startPrintMessages = m_startPrintMessages;
        clone->m_updatePrintMessages = m_updatePrintMessages;
        clone->m_transformActions = m_transformActions;
        clone->m_startLockCursor = m_startLockCursor;
        clone->RegisterProperties();
        clone->CopyPropertiesFrom(*this);
        clone->Transform.Init(clone.get());
        clone->GetRespectiveObject.behaviour = clone.get();
        clone->CursorState.behaviour = clone.get();
        clone->cursorState.behaviour = clone.get();
        return clone;
    }

    void RegisterProperties() override;
    void ParsePropertiesFromCpp(const std::string& cppPath, bool preserveExistingValues = true);
    void RefreshPropertiesFromSource() override;
    void ResetToDefaults() override;
    std::string GetSourceCppPath() const override { return m_sourceCppPath; }

    void Start() override;
    void Update(float deltaTime) override;

    // Stable storage pools for dynamically reflected properties
    std::map<std::string, float> m_floatStorage;
    std::map<std::string, int> m_intStorage;
    std::map<std::string, int64_t> m_int64Storage;
    std::map<std::string, bool> m_boolStorage;
    std::map<std::string, std::string> m_stringStorage;
    std::map<std::string, double> m_doubleStorage;
    std::map<std::string, glm::vec2> m_vec2Storage;
    std::map<std::string, glm::vec3> m_vec3Storage;
    std::map<std::string, glm::vec4> m_vec4Storage;
    std::map<std::string, void*> m_refStorage;
    std::map<std::string, PropertyDefaultValue> m_defaultPropertyValues;

    // Dynamic runtime script execution state
    bool m_hasRotation = false;
    glm::vec3 m_rotateAxis{0.0f, 1.0f, 0.0f};
    float m_defaultSpeed = 50.0f;

    bool m_hasTranslation = false;
    glm::vec3 m_translateAxis{0.0f, 0.0f, 0.0f};
    float m_defaultMoveSpeed = 5.0f;

    bool m_hasInput = false;
    bool m_startLockCursor = false;
    std::vector<std::string> m_startPrintMessages;

    struct DynamicUpdatePrint {
        std::string textPrefix;
        std::string textSuffix;
        bool appendDeltaTime = false;
        bool appendPosition = false;
        bool appendRotation = false;
        std::string varName;
        int printKey = -1;
    };
    std::vector<DynamicUpdatePrint> m_updatePrintMessages;

    struct DynamicKeyCondition {
        int key = -1;
        bool isPressed = false; // true: IsKeyPressed, false: IsKeyDown
        bool isNegated = false; // true: !IsKeyDown
    };

    struct DynamicTransformAction {
        enum class ActionType {
            TranslateRelative,
            TranslateWorld,
            RotateRelative,
            RotateWorld,
            LockCursor,
            UnlockCursor
        };
        ActionType type = ActionType::TranslateRelative;
        glm::vec3 constantDelta{0.0f};
        bool useDeltaTime[3] = {false, false, false};
        std::string varNames[3];
        float varMultiplier[3] = {1.0f, 1.0f, 1.0f};
        std::string scaleVarNames[3];
        bool activeAxis[3] = {false, false, false};
        std::string targetRefName;
        std::vector<DynamicKeyCondition> requiredKeys;
    };
    std::vector<DynamicTransformAction> m_transformActions;

    std::string m_sourceCppPath;
    std::filesystem::file_time_type m_sourceTimestamp{};
};

// ============================================================================
// BehaviourRegistry Singleton (dev.md Section 5)
// ============================================================================
class BehaviourRegistry {
public:
    static BehaviourRegistry& Get() {
        static BehaviourRegistry instance;
        return instance;
    }

    template<typename T>
    void Register(const std::string& className, const std::string& displayName = "", const std::string& description = "") {
        BehaviourClassInfo info;
        info.className = className;
        info.displayName = displayName.empty() ? className : displayName;
        info.description = description;
        info.isNative = true;
        info.factory = [className, displayName]() -> std::unique_ptr<EunoiaBehaviour> {
            auto b = std::make_unique<T>();
            b->SetClassName(className);
            if (!displayName.empty()) b->SetDisplayName(displayName);
            b->RegisterProperties();
            return b;
        };
        m_registry[className] = info;
    }

    void RegisterCustom(const BehaviourClassInfo& info) {
        m_registry[info.className] = info;
    }

    void RegisterScriptFile(const std::string& scriptName, const std::string& cppPath) {
        auto it = m_registry.find(scriptName);
        if (it != m_registry.end() && it->second.isNative) {
            // Already natively compiled into binary: retain factory, only record source path
            it->second.sourceCpp = cppPath;
            return;
        }

        BehaviourClassInfo info;
        info.className = scriptName;
        info.displayName = scriptName;
        info.description = "User C++ Script: " + cppPath;
        info.sourceCpp = cppPath;
        info.isNative = false;
        info.factory = [scriptName, cppPath]() -> std::unique_ptr<EunoiaBehaviour> {
            auto b = std::make_unique<DynamicScriptBehaviour>(scriptName, cppPath);
            b->SetClassName(scriptName);
            b->SetDisplayName(scriptName);
            b->ParsePropertiesFromCpp(cppPath);
            return b;
        };
        m_registry[scriptName] = info;
    }

    std::unique_ptr<EunoiaBehaviour> Create(const std::string& className) const {
        auto it = m_registry.find(className);
        if (it != m_registry.end() && it->second.factory) {
            return it->second.factory();
        }
        return nullptr;
    }

    bool Has(const std::string& className) const {
        return m_registry.find(className) != m_registry.end();
    }

    const std::unordered_map<std::string, BehaviourClassInfo>& GetAll() const {
        return m_registry;
    }

    std::vector<std::string> GetRegisteredClassNames() const {
        std::vector<std::string> names;
        for (const auto& pair : m_registry) {
            names.push_back(pair.first);
        }
        return names;
    }

private:
    BehaviourRegistry() {
        // Register built-in engine behaviours
        Register<RotatorBehaviour>("RotatorBehaviour", "Rotator Behaviour", "Smooth continuous rotation around an axis");
        Register<LightFlickerBehaviour>("LightFlickerBehaviour", "Light Flicker Behaviour", "Dynamic light pulse and flicker");
        Register<DoorController>("DoorController", "Door Controller", "Door swing and lock controller with light/mesh bindings");
        Register<PlayerController>("PlayerController", "Player Controller", "Player character controller with movement & combat stats");
        Register<EnemyController>("EnemyController", "Enemy Controller", "Enemy AI controller with target tracking");
    }

    std::unordered_map<std::string, BehaviourClassInfo> m_registry;
};

#define REGISTER_BEHAVIOUR(ClassName, DisplayName) \
    struct ClassName##_AutoRegister { \
        ClassName##_AutoRegister() { \
            BehaviourRegistry::Get().Register<ClassName>(#ClassName, DisplayName); \
        } \
    }; \
    static ClassName##_AutoRegister s_register_##ClassName;
