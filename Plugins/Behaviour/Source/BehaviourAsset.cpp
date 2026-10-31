#include "Behaviour/BehaviourAsset.h"
#include <EngineScene/Scene.h>
#include <EngineScene/GameObject.h>
#include <EngineScene/BehaviourRegistry.h>
#include <EngineAssets/AssetManager.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <cmath>

namespace Eunoia {

BehaviourAsset::BehaviourAsset() {
    // Add default root scene component
    BehaviourComponentDef root;
    root.id = nextComponentId++;
    root.name = "DefaultSceneRoot";
    root.parentId = 0;
    root.type = PrimitiveType::Empty;
    components.push_back(root);
}

BehaviourAsset::BehaviourAsset(const std::string& inName) : name(inName) {
    BehaviourComponentDef root;
    root.id = nextComponentId++;
    root.name = "DefaultSceneRoot";
    root.parentId = 0;
    root.type = PrimitiveType::Empty;
    components.push_back(root);
}

BehaviourComponentDef* BehaviourAsset::AddComponent(PrimitiveType type, const std::string& compName, int parentId) {
    BehaviourComponentDef comp;
    comp.id = nextComponentId++;
    if (!compName.empty()) {
        comp.name = compName;
    } else {
        comp.name = "Component_" + std::to_string(comp.id);
    }
    comp.type = type;
    comp.parentId = parentId;

    if (parentId == 0 && !components.empty()) {
        comp.parentId = components.front().id;
    }

    components.push_back(comp);

    if (comp.parentId > 0) {
        BehaviourComponentDef* parent = FindComponent(comp.parentId);
        if (parent) {
            parent->childIds.push_back(comp.id);
        }
    }

    return &components.back();
}

bool BehaviourAsset::RemoveComponent(int id) {
    if (components.empty() || components.front().id == id) {
        // Cannot delete root component
        return false;
    }

    auto it = std::find_if(components.begin(), components.end(), [id](const BehaviourComponentDef& c) {
        return c.id == id;
    });

    if (it != components.end()) {
        int parentId = it->parentId;
        if (parentId > 0) {
            BehaviourComponentDef* parent = FindComponent(parentId);
            if (parent) {
                parent->childIds.erase(std::remove(parent->childIds.begin(), parent->childIds.end(), id), parent->childIds.end());
            }
        }
        components.erase(it);
        return true;
    }

    return false;
}

BehaviourComponentDef* BehaviourAsset::FindComponent(int id) {
    for (auto& c : components) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

const BehaviourComponentDef* BehaviourAsset::FindComponent(int id) const {
    for (const auto& c : components) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

bool BehaviourAsset::SetParent(int childId, int newParentId) {
    if (childId == newParentId || childId <= 0) return false;
    BehaviourComponentDef* child = FindComponent(childId);
    if (!child) return false;

    // Remove from old parent
    if (child->parentId > 0) {
        BehaviourComponentDef* oldParent = FindComponent(child->parentId);
        if (oldParent) {
            oldParent->childIds.erase(std::remove(oldParent->childIds.begin(), oldParent->childIds.end(), childId), oldParent->childIds.end());
        }
    }

    child->parentId = newParentId;
    if (newParentId > 0) {
        BehaviourComponentDef* newParent = FindComponent(newParentId);
        if (newParent) {
            newParent->childIds.push_back(childId);
        }
    }

    return true;
}

AttachedBehaviourScript* BehaviourAsset::AddAttachedScript(const std::string& scriptName, const std::string& sourceCppPath) {
    static uint64_t s_scriptCounter = 1;
    AttachedBehaviourScript s;
    s.id = "beh_script_" + std::to_string(s_scriptCounter++);
    s.scriptName = scriptName;
    s.sourceCppPath = sourceCppPath;
    s.enabled = true;
    attachedScripts.push_back(s);
    return &attachedScripts.back();
}

bool BehaviourAsset::RemoveAttachedScript(const std::string& scriptId) {
    auto it = std::find_if(attachedScripts.begin(), attachedScripts.end(), [&](const AttachedBehaviourScript& s) {
        return s.id == scriptId;
    });
    if (it != attachedScripts.end()) {
        attachedScripts.erase(it);
        return true;
    }
    return false;
}

AttachedBehaviourScript* BehaviourAsset::FindAttachedScript(const std::string& scriptId) {
    for (auto& s : attachedScripts) {
        if (s.id == scriptId) return &s;
    }
    return nullptr;
}

const AttachedBehaviourScript* BehaviourAsset::FindAttachedScript(const std::string& scriptId) const {
    for (const auto& s : attachedScripts) {
        if (s.id == scriptId) return &s;
    }
    return nullptr;
}

bool BehaviourAsset::SaveToFile(const std::string& path) {
    try {
        nlohmann::json root;
        root["name"] = name;
        root["nextComponentId"] = nextComponentId;

        // Components
        nlohmann::json compArray = nlohmann::json::array();
        for (const auto& c : components) {
            nlohmann::json cj;
            cj["id"] = c.id;
            cj["name"] = c.name;
            cj["parentId"] = c.parentId;
            cj["childIds"] = c.childIds;
            cj["type"] = (int)c.type;
            cj["pos"] = { c.localPosition.x, c.localPosition.y, c.localPosition.z };
            cj["rot"] = { c.localRotation.x, c.localRotation.y, c.localRotation.z };
            cj["scale"] = { c.localScale.x, c.localScale.y, c.localScale.z };
            cj["meshPath"] = c.meshPath;
            cj["materialPath"] = c.materialPath;
            cj["color"] = { c.color.r, c.color.g, c.color.b, c.color.a };
            cj["lightColor"] = { c.lightColor.r, c.lightColor.g, c.lightColor.b };
            cj["lightIntensity"] = c.lightIntensity;
            cj["lightRange"] = c.lightRange;
            cj["visible"] = c.visible;
            compArray.push_back(cj);
        }
        root["components"] = compArray;

        // Attached Behaviour Scripts
        nlohmann::json scriptArray = nlohmann::json::array();
        for (const auto& s : attachedScripts) {
            nlohmann::json sj;
            sj["id"] = s.id;
            sj["scriptName"] = s.scriptName;
            sj["sourceCppPath"] = s.sourceCppPath;
            sj["enabled"] = s.enabled;

            nlohmann::json propArray = nlohmann::json::array();
            for (const auto& p : s.propertyOverrides) {
                nlohmann::json pj;
                pj["name"] = p.propertyName;
                pj["type"] = p.typeName;
                pj["floatVal"] = p.floatVal;
                pj["intVal"] = p.intVal;
                pj["boolVal"] = p.boolVal;
                pj["stringVal"] = p.stringVal;
                pj["vecVal"] = { p.vecVal.x, p.vecVal.y, p.vecVal.z, p.vecVal.w };
                pj["targetComponentId"] = p.targetComponentId;
                propArray.push_back(pj);
            }
            sj["propertyOverrides"] = propArray;
            scriptArray.push_back(sj);
        }
        root["attachedScripts"] = scriptArray;

        std::filesystem::path dir = std::filesystem::path(path).parent_path();
        if (!dir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
        }

        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << root.dump(4);
        filePath = path;
        return true;
    } catch (...) {
        return false;
    }
}

bool BehaviourAsset::LoadFromFile(const std::string& path) {
    try {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return false;

        std::ifstream file(path);
        if (!file.is_open()) return false;

        nlohmann::json root;
        file >> root;

        if (root.contains("name")) name = root["name"].get<std::string>();
        if (root.contains("nextComponentId")) nextComponentId = root["nextComponentId"].get<int>();

        components.clear();
        if (root.contains("components") && root["components"].is_array()) {
            for (const auto& cj : root["components"]) {
                BehaviourComponentDef c;
                if (cj.contains("id")) c.id = cj["id"].get<int>();
                if (cj.contains("name")) c.name = cj["name"].get<std::string>();
                if (cj.contains("parentId")) c.parentId = cj["parentId"].get<int>();
                if (cj.contains("childIds") && cj["childIds"].is_array()) {
                    for (const auto& ch : cj["childIds"]) c.childIds.push_back(ch.get<int>());
                }
                if (cj.contains("type")) c.type = (PrimitiveType)cj["type"].get<int>();
                if (cj.contains("pos") && cj["pos"].size() >= 3) {
                    c.localPosition = glm::vec3(cj["pos"][0].get<float>(), cj["pos"][1].get<float>(), cj["pos"][2].get<float>());
                }
                if (cj.contains("rot") && cj["rot"].size() >= 3) {
                    c.localRotation = glm::vec3(cj["rot"][0].get<float>(), cj["rot"][1].get<float>(), cj["rot"][2].get<float>());
                }
                if (cj.contains("scale") && cj["scale"].size() >= 3) {
                    c.localScale = glm::vec3(cj["scale"][0].get<float>(), cj["scale"][1].get<float>(), cj["scale"][2].get<float>());
                }
                if (cj.contains("meshPath")) c.meshPath = cj["meshPath"].get<std::string>();
                if (cj.contains("materialPath")) c.materialPath = cj["materialPath"].get<std::string>();
                if (cj.contains("color") && cj["color"].size() >= 4) {
                    c.color = glm::vec4(cj["color"][0].get<float>(), cj["color"][1].get<float>(), cj["color"][2].get<float>(), cj["color"][3].get<float>());
                }
                if (cj.contains("lightColor") && cj["lightColor"].size() >= 3) {
                    c.lightColor = glm::vec3(cj["lightColor"][0].get<float>(), cj["lightColor"][1].get<float>(), cj["lightColor"][2].get<float>());
                }
                if (cj.contains("lightIntensity")) c.lightIntensity = cj["lightIntensity"].get<float>();
                if (cj.contains("lightRange")) c.lightRange = cj["lightRange"].get<float>();
                if (cj.contains("visible")) c.visible = cj["visible"].get<bool>();

                components.push_back(c);
            }
        }

        if (components.empty()) {
            BehaviourComponentDef rootComp;
            rootComp.id = 1;
            rootComp.name = "DefaultSceneRoot";
            components.push_back(rootComp);
        }

        attachedScripts.clear();
        if (root.contains("attachedScripts") && root["attachedScripts"].is_array()) {
            for (const auto& sj : root["attachedScripts"]) {
                AttachedBehaviourScript s;
                if (sj.contains("id")) s.id = sj["id"].get<std::string>();
                if (sj.contains("scriptName")) s.scriptName = sj["scriptName"].get<std::string>();
                if (sj.contains("sourceCppPath")) s.sourceCppPath = sj["sourceCppPath"].get<std::string>();
                if (sj.contains("enabled")) s.enabled = sj["enabled"].get<bool>();

                if (sj.contains("propertyOverrides") && sj["propertyOverrides"].is_array()) {
                    for (const auto& pj : sj["propertyOverrides"]) {
                        ScriptPropertyOverride p;
                        if (pj.contains("name")) p.propertyName = pj["name"].get<std::string>();
                        if (pj.contains("type")) p.typeName = pj["type"].get<std::string>();
                        if (pj.contains("floatVal")) p.floatVal = pj["floatVal"].get<float>();
                        if (pj.contains("intVal")) p.intVal = pj["intVal"].get<int>();
                        if (pj.contains("boolVal")) p.boolVal = pj["boolVal"].get<bool>();
                        if (pj.contains("stringVal")) p.stringVal = pj["stringVal"].get<std::string>();
                        if (pj.contains("vecVal") && pj["vecVal"].size() >= 4) {
                            p.vecVal = glm::vec4(pj["vecVal"][0].get<float>(), pj["vecVal"][1].get<float>(), pj["vecVal"][2].get<float>(), pj["vecVal"][3].get<float>());
                        }
                        if (pj.contains("targetComponentId")) p.targetComponentId = pj["targetComponentId"].get<int>();
                        s.propertyOverrides.push_back(p);
                    }
                }
                attachedScripts.push_back(s);
            }
        }

        filePath = path;
        return true;
    } catch (...) {
        return false;
    }
}

GameObject* BehaviourAsset::SpawnIntoScene(Scene& scene, const glm::vec3& worldPos, const glm::vec3& worldRot) {
    if (components.empty()) return nullptr;

    std::unordered_map<int, int> compIdToObjId;
    GameObject* rootObj = nullptr;

    // 1. Spawn root actor
    const auto& rootComp = components.front();
    GameObject& spawnedRoot = scene.AddEmptyActor(worldPos + rootComp.localPosition);
    spawnedRoot.name = name.empty() ? "BP_Actor" : name;
    spawnedRoot.rotation = worldRot + rootComp.localRotation;
    spawnedRoot.scale = rootComp.localScale;
    rootObj = &spawnedRoot;
    compIdToObjId[rootComp.id] = spawnedRoot.id;

    // 2. Spawn remaining components as children
    for (size_t i = 1; i < components.size(); ++i) {
        const auto& c = components[i];
        glm::vec3 cWorldPos = worldPos + c.localPosition;

        GameObject* spawnedChild = nullptr;
        if (!c.meshPath.empty()) {
            std::error_code ec;
            std::filesystem::path resolvedMesh = AssetManager::Get().ResolvePhysicalOrCookedPath(c.meshPath);
            if (resolvedMesh.empty() || !std::filesystem::exists(resolvedMesh, ec)) {
                resolvedMesh = c.meshPath;
            }
            GameObject& mObj = scene.AddImportedMesh(resolvedMesh.string(), cWorldPos);
            spawnedChild = &mObj;
        } else if (c.type == PrimitiveType::Empty) {
            GameObject& eObj = scene.AddEmptyActor(cWorldPos);
            spawnedChild = &eObj;
        } else {
            GameObject& pObj = scene.AddObject(c.type, cWorldPos);
            spawnedChild = &pObj;
        }

        if (spawnedChild) {
            spawnedChild->name = c.name;
            spawnedChild->rotation = c.localRotation;
            spawnedChild->scale = c.localScale;
            spawnedChild->color = c.color;
            compIdToObjId[c.id] = spawnedChild->id;

            // Wire parenting
            int pId = c.parentId;
            if (compIdToObjId.find(pId) != compIdToObjId.end()) {
                int parentObjId = compIdToObjId[pId];
                GameObject* pObj = scene.FindObject(parentObjId);
                if (pObj) {
                    spawnedChild->parentId = parentObjId;
                    pObj->childIds.push_back(spawnedChild->id);
                }
            } else if (rootObj) {
                spawnedChild->parentId = rootObj->id;
                rootObj->childIds.push_back(spawnedChild->id);
            }
        }
    }

    // 3. Attach Behaviour Scripts to root actor
    for (const auto& s : attachedScripts) {
        if (!s.enabled) continue;

        std::string scriptClassName = s.scriptName;
        // Strip file extension if present
        size_t dotPos = scriptClassName.rfind('.');
        if (dotPos != std::string::npos) scriptClassName = scriptClassName.substr(0, dotPos);

        auto beh = BehaviourRegistry::Get().Create(scriptClassName);
        if (beh && rootObj) {
            beh->SetOwner(rootObj);
            beh->SetScene(&scene);

            // Apply property overrides and remap component references
            for (const auto& prop : s.propertyOverrides) {
                for (auto& propDesc : beh->GetProperties()) {
                    if (propDesc.name == prop.propertyName) {
                        if (prop.typeName == "object_ref" && prop.targetComponentId > 0) {
                            auto mapIt = compIdToObjId.find(prop.targetComponentId);
                            if (mapIt != compIdToObjId.end()) {
                                propDesc.targetId = mapIt->second;
                            }
                        } else if (propDesc.dataPtr) {
                            switch (propDesc.type) {
                                case BehaviourPropertyType::Float:
                                    *reinterpret_cast<float*>(propDesc.dataPtr) = prop.floatVal;
                                    break;
                                case BehaviourPropertyType::Int:
                                    *reinterpret_cast<int*>(propDesc.dataPtr) = prop.intVal;
                                    break;
                                case BehaviourPropertyType::Bool:
                                    *reinterpret_cast<bool*>(propDesc.dataPtr) = prop.boolVal;
                                    break;
                                case BehaviourPropertyType::String:
                                    *reinterpret_cast<std::string*>(propDesc.dataPtr) = prop.stringVal;
                                    break;
                                default: break;
                            }
                        }
                        break;
                    }
                }
            }

            rootObj->AddBehaviour(std::move(beh));
        }
    }

    scene.selectedId = rootObj->id;
    return rootObj;
}

} // namespace Eunoia
