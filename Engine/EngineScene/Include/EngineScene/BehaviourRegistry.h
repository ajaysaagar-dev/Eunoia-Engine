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
// Dynamic Script Behaviour (for user-created .cpp scripts)
// ============================================================================

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
        clone->m_destroyActions = m_destroyActions;
        clone->m_transformActions = m_transformActions;
        clone->m_startLockCursor = m_startLockCursor;
        clone->m_startUnlockCursor = m_startUnlockCursor;
        clone->m_startUIAddTargets = m_startUIAddTargets;
        clone->m_startUIRemoveTargets = m_startUIRemoveTargets;
        clone->m_startUIRemoveAll = m_startUIRemoveAll;
        clone->m_methods = m_methods;
        clone->m_properties = m_properties;
        for (auto& p : clone->m_properties) {
            switch (p.type) {
                case BehaviourPropertyType::Bool:
                    p.dataPtr = &clone->m_boolStorage[p.name];
                    break;
                case BehaviourPropertyType::Int:
                    p.dataPtr = &clone->m_intStorage[p.name];
                    break;
                case BehaviourPropertyType::Int64:
                    p.dataPtr = &clone->m_int64Storage[p.name];
                    break;
                case BehaviourPropertyType::Float:
                    p.dataPtr = &clone->m_floatStorage[p.name];
                    break;
                case BehaviourPropertyType::Double:
                    p.dataPtr = &clone->m_doubleStorage[p.name];
                    break;
                case BehaviourPropertyType::String:
                    p.dataPtr = &clone->m_stringStorage[p.name];
                    break;
                case BehaviourPropertyType::Vec2:
                    p.dataPtr = &clone->m_vec2Storage[p.name];
                    break;
                case BehaviourPropertyType::Vec3:
                case BehaviourPropertyType::Color3:
                    p.dataPtr = &clone->m_vec3Storage[p.name];
                    break;
                case BehaviourPropertyType::Vec4:
                case BehaviourPropertyType::Color4:
                    p.dataPtr = &clone->m_vec4Storage[p.name];
                    break;
                case BehaviourPropertyType::ObjectRef:
                    p.dataPtr = &clone->m_refStorage[p.name];
                    break;
            }
        }
        clone->Transform.Init(clone.get());
        clone->GetRespectiveObject.behaviour = clone.get();
        clone->CursorState.behaviour = clone.get();
        clone->cursorState.behaviour = clone.get();
        return clone;
    }

    void CopyPropertiesFrom(const EunoiaBehaviour& other) override;
    void RegisterProperties() override;
    void ParsePropertiesFromCpp(const std::string& cppPath, bool preserveExistingValues = true);
    void RefreshPropertiesFromSource() override;
    void ResetToDefaults() override;
    std::string GetSourceCppPath() const override { return m_sourceCppPath; }

    void OnCreate() override;
    void OnEnable() override;
    void Start() override;
    void Update(float deltaTime) override;
    void InvokeFunction(const std::string& functionName) override;

    enum class ScriptStatementType {
        Assignment,
        Toggle,
        Print,
        UnlockCursor,
        LockCursor,
        AddUI,
        RemoveUI,
        RemoveAllUI,
        CallMethod,
        StartTimer,
        StartInterval,
        StopTimer,
        ClearAllTimers
    };

    struct ScriptAction {
        ScriptStatementType type = ScriptStatementType::Assignment;
        std::string varName;
        std::string valueStr;
        std::string target;
        float duration = 0.0f;
    };

    struct ScriptBranch {
        bool hasCondition = false;
        std::string conditionVar;
        bool expectedBool = true;
        std::vector<ScriptAction> thenActions;
        std::vector<ScriptAction> elseActions;
    };

    struct ScriptMethod {
        std::string name;
        std::vector<ScriptBranch> branches;
        std::vector<std::string> calledMethods;
        std::vector<std::string> printMessages;
        std::vector<std::string> uiAddTargets;
        std::vector<std::string> uiRemoveTargets;
        bool unlockCursor = false;
        bool lockCursor = false;
        bool removeAllUI = false;
    };
    std::unordered_map<std::string, ScriptMethod> m_methods;
    void ExecuteMethod(const std::string& methodName, std::unordered_set<std::string>& visited);

    // Direct typed getters for robust runtime reflection
    float GetFloat(const std::string& name, float defaultVal = 0.0f) const;
    bool GetBool(const std::string& name, bool defaultVal = false) const;
    int GetInt(const std::string& name, int defaultVal = 0) const;
    int64_t GetInt64(const std::string& name, int64_t defaultVal = 0) const;
    double GetDouble(const std::string& name, double defaultVal = 0.0) const;
    std::string GetString(const std::string& name, const std::string& defaultVal = "") const;
    glm::vec2 GetVec2(const std::string& name, const glm::vec2& defaultVal = glm::vec2(0.0f)) const;
    glm::vec3 GetVec3(const std::string& name, const glm::vec3& defaultVal = glm::vec3(0.0f)) const;
    glm::vec4 GetVec4(const std::string& name, const glm::vec4& defaultVal = glm::vec4(0.0f)) const;
    void* GetRef(const std::string& name) const;
    void SyncStorageFromProperties();

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
    bool m_startUnlockCursor = false;
    std::vector<std::string> m_startPrintMessages;
    std::vector<std::string> m_startUIAddTargets;
    std::vector<std::string> m_startUIRemoveTargets;
    bool m_startUIRemoveAll = false;

    struct DynamicKeyCondition {
        int key = -1;
        bool isPressed = false; // true: IsKeyPressed, false: IsKeyDown
        bool isNegated = false; // true: !IsKeyDown
    };

    struct DynamicBoolCondition {
        std::string varName;
        bool expectedVal = true;
    };

    struct DynamicUpdatePrint {
        std::string textPrefix;
        std::string textSuffix;
        bool appendDeltaTime = false;
        bool appendPosition = false;
        bool appendRotation = false;
        bool appendHitName = false;
        bool appendHitId = false;
        bool appendHitPoint = false;
        bool appendHitNormal = false;
        bool appendHitDistance = false;
        bool appendHitStatus = false;
        bool appendHitTags = false;
        bool requireHitStatus = false;
        bool requireNoHit = false;
        std::string requireHitTag;
        std::vector<DynamicKeyCondition> requiredKeys;
        std::vector<DynamicBoolCondition> requiredBools;
        std::string varName;
        int printKey = -1;
    };
    std::vector<DynamicUpdatePrint> m_updatePrintMessages;

    struct DynamicDestroyAction {
        bool enabled = true;
        bool targetIsHitObject = false;
        std::string targetVar;
        std::vector<DynamicKeyCondition> requiredKeys;
        std::vector<DynamicBoolCondition> requiredBools;
        bool requireHitStatus = false;
        bool requireNoHit = false;
        std::string requireHitTag;
    };
    std::vector<DynamicDestroyAction> m_destroyActions;

    struct DynamicLineTraceAction {
        bool enabled = true;
        std::string startVar;
        std::string endVar;
        glm::vec3 constantStart{0.0f};
        glm::vec3 constantEnd{0.0f};
        bool autoOriginFromOwner = false;
        bool autoDirectionForward = false;
        float traceDistance = 100.0f;
    };
    std::vector<DynamicLineTraceAction> m_lineTraceActions;
    RayTraceHit m_lastDynamicHit;

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
        std::vector<DynamicBoolCondition> requiredBools;
    };
    std::vector<DynamicTransformAction> m_transformActions;

    struct DynamicUIAction {
        enum class ActionType {
            Add,
            Remove,
            RemoveAll
        };
        ActionType type = ActionType::Add;
        std::string targetVar;
        bool requireHitStatus = false;
        bool requireNoHit = false;
        std::string requireHitTag;
        std::vector<DynamicKeyCondition> requiredKeys;
        std::vector<DynamicBoolCondition> requiredBools;
    };
    std::vector<DynamicUIAction> m_uiActions;

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
            b->SetDisplayName(displayName.empty() ? className : displayName);
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
        if (it != m_registry.end()) {
            it->second.sourceCpp = cppPath;
            it->second.factory = [scriptName, cppPath]() -> std::unique_ptr<EunoiaBehaviour> {
                auto b = std::make_unique<DynamicScriptBehaviour>(scriptName, cppPath);
                b->SetClassName(scriptName);
                b->SetDisplayName(scriptName);
                b->ParsePropertiesFromCpp(cppPath);
                return b;
            };
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
        // No hardcoded built-in behaviours
    }

    std::unordered_map<std::string, BehaviourClassInfo> m_registry;
};

#define REGISTER_BEHAVIOUR(ClassName, ...) \
    struct ClassName##_AutoRegister { \
        ClassName##_AutoRegister() { \
            std::string dName = #ClassName; \
            const char* explicitName = "" __VA_ARGS__; \
            if (explicitName && explicitName[0] != '\0') dName = explicitName; \
            BehaviourRegistry::Get().Register<ClassName>(#ClassName, dName); \
        } \
    }; \
    static ClassName##_AutoRegister s_register_##ClassName;
