#pragma once

#include "BehaviourComponent.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>

class Scene;
struct GameObject;

namespace Eunoia {

struct ScriptPropertyOverride {
    std::string propertyName;
    std::string typeName = "float"; // float, int, bool, string, vec2, vec3, vec4, object_ref
    float floatVal = 0.0f;
    int intVal = 0;
    bool boolVal = false;
    std::string stringVal = "";
    glm::vec4 vecVal{0.0f, 0.0f, 0.0f, 0.0f};
    int targetComponentId = 0; // ID of component within THIS Behaviour asset
};

struct AttachedBehaviourScript {
    std::string id;
    std::string scriptName = "NewScript";
    std::string sourceCppPath = "";
    bool enabled = true;
    std::vector<ScriptPropertyOverride> propertyOverrides;
};

class BehaviourAsset {
public:
    std::string name = "NewBehaviour";
    std::string filePath = "";
    std::vector<BehaviourComponentDef> components;
    std::vector<AttachedBehaviourScript> attachedScripts;
    int nextComponentId = 1;

    BehaviourAsset();
    explicit BehaviourAsset(const std::string& inName);

    // Component Hierarchy Operations
    BehaviourComponentDef* AddComponent(PrimitiveType type, const std::string& name = "", int parentId = 0);
    bool RemoveComponent(int id);
    BehaviourComponentDef* FindComponent(int id);
    const BehaviourComponentDef* FindComponent(int id) const;
    bool SetParent(int childId, int newParentId);
    bool IsDescendant(int ancestorId, int childId) const;

    // Attached Behaviour Script Operations
    AttachedBehaviourScript* AddAttachedScript(const std::string& scriptName, const std::string& sourceCppPath);
    bool RemoveAttachedScript(const std::string& scriptId);
    AttachedBehaviourScript* FindAttachedScript(const std::string& scriptId);
    const AttachedBehaviourScript* FindAttachedScript(const std::string& scriptId) const;

    // JSON Persistence (.behaviour)
    bool SaveToFile(const std::string& path);
    bool LoadFromFile(const std::string& path);

    // Spawning into Level Scene
    GameObject* SpawnIntoScene(Scene& scene, const glm::vec3& worldPos, const glm::vec3& worldRot = glm::vec3(0.0f));
    void SyncInstancesInScene(Scene& scene);

    // Static Utility to Resolve Mesh Path across Project/Content/Asset hierarchies
    static std::filesystem::path ResolveAssetMeshPath(const std::string& meshPath, const std::string& assetFilePath = "");
};

} // namespace Eunoia
