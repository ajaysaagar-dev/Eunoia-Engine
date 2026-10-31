#include "Behaviour/BehaviourAsset.h"
#include <Editor/EngineUI.h>
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

bool BehaviourAsset::IsDescendant(int ancestorId, int childId) const {
    const BehaviourComponentDef* child = FindComponent(childId);
    while (child && child->parentId > 0) {
        if (child->parentId == ancestorId) return true;
        child = FindComponent(child->parentId);
    }
    return false;
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
            cj["innerConeAngle"] = c.innerConeAngle;
            cj["outerConeAngle"] = c.outerConeAngle;
            cj["coneFalloff"] = c.coneFalloff;
            cj["rangeSmoothing"] = c.rangeSmoothing;
            cj["attenuation"] = c.attenuation;
            cj["radius"] = c.radius;
            cj["length"] = c.length;
            cj["useTemperature"] = c.useTemperature;
            cj["temperature"] = c.temperature;
            cj["castShadows"] = c.castShadows;

            cj["metallic"] = c.metallic;
            cj["roughness"] = c.roughness;
            cj["specular"] = c.specular;
            cj["normalStrength"] = c.normalStrength;
            cj["smoothShading"] = c.smoothShading;
            cj["autoSmoothAngle"] = c.autoSmoothAngle;
            cj["uvScale"] = { c.uvScale.x, c.uvScale.y };

            cj["cameraFov"] = c.cameraFov;
            cj["cameraNear"] = c.cameraNear;
            cj["cameraFar"] = c.cameraFar;
            cj["isOrthographic"] = c.isOrthographic;
            cj["orthoSize"] = c.orthoSize;

            nlohmann::json pj;
            pj["size"] = c.params.size;
            pj["width"] = c.params.width;
            pj["height"] = c.params.height;
            pj["depth"] = c.params.depth;
            pj["radius"] = c.params.radius;
            pj["radius2"] = c.params.radius2;
            pj["segmentsX"] = c.params.segmentsX;
            pj["segmentsY"] = c.params.segmentsY;
            pj["segmentsZ"] = c.params.segmentsZ;
            pj["radialSegments"] = c.params.radialSegments;
            pj["rings"] = c.params.rings;
            pj["sectors"] = c.params.sectors;
            pj["subdivisions"] = c.params.subdivisions;
            pj["sides"] = c.params.sides;
            pj["capTop"] = c.params.capTop;
            pj["capBottom"] = c.params.capBottom;
            cj["params"] = pj;

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
                if (cj.contains("innerConeAngle")) c.innerConeAngle = cj["innerConeAngle"].get<float>();
                if (cj.contains("outerConeAngle")) c.outerConeAngle = cj["outerConeAngle"].get<float>();
                if (cj.contains("coneFalloff")) c.coneFalloff = cj["coneFalloff"].get<float>();
                if (cj.contains("rangeSmoothing")) c.rangeSmoothing = cj["rangeSmoothing"].get<float>();
                if (cj.contains("attenuation")) c.attenuation = cj["attenuation"].get<float>();
                if (cj.contains("radius")) c.radius = cj["radius"].get<float>();
                if (cj.contains("length")) c.length = cj["length"].get<float>();
                if (cj.contains("useTemperature")) c.useTemperature = cj["useTemperature"].get<bool>();
                if (cj.contains("temperature")) c.temperature = cj["temperature"].get<float>();
                if (cj.contains("castShadows")) c.castShadows = cj["castShadows"].get<bool>();

                if (cj.contains("metallic")) c.metallic = cj["metallic"].get<float>();
                if (cj.contains("roughness")) c.roughness = cj["roughness"].get<float>();
                if (cj.contains("specular")) c.specular = cj["specular"].get<float>();
                if (cj.contains("normalStrength")) c.normalStrength = cj["normalStrength"].get<float>();
                if (cj.contains("smoothShading")) c.smoothShading = cj["smoothShading"].get<bool>();
                if (cj.contains("autoSmoothAngle")) c.autoSmoothAngle = cj["autoSmoothAngle"].get<float>();
                if (cj.contains("uvScale") && cj["uvScale"].size() >= 2) {
                    c.uvScale = glm::vec2(cj["uvScale"][0].get<float>(), cj["uvScale"][1].get<float>());
                }

                if (cj.contains("cameraFov")) c.cameraFov = cj["cameraFov"].get<float>();
                if (cj.contains("cameraNear")) c.cameraNear = cj["cameraNear"].get<float>();
                if (cj.contains("cameraFar")) c.cameraFar = cj["cameraFar"].get<float>();
                if (cj.contains("isOrthographic")) c.isOrthographic = cj["isOrthographic"].get<bool>();
                if (cj.contains("orthoSize")) c.orthoSize = cj["orthoSize"].get<float>();

                if (cj.contains("params") && cj["params"].is_object()) {
                    const auto& pj = cj["params"];
                    if (pj.contains("size")) c.params.size = pj["size"].get<float>();
                    if (pj.contains("width")) c.params.width = pj["width"].get<float>();
                    if (pj.contains("height")) c.params.height = pj["height"].get<float>();
                    if (pj.contains("depth")) c.params.depth = pj["depth"].get<float>();
                    if (pj.contains("radius")) c.params.radius = pj["radius"].get<float>();
                    if (pj.contains("radius2")) c.params.radius2 = pj["radius2"].get<float>();
                    if (pj.contains("segmentsX")) c.params.segmentsX = pj["segmentsX"].get<int>();
                    if (pj.contains("segmentsY")) c.params.segmentsY = pj["segmentsY"].get<int>();
                    if (pj.contains("segmentsZ")) c.params.segmentsZ = pj["segmentsZ"].get<int>();
                    if (pj.contains("radialSegments")) c.params.radialSegments = pj["radialSegments"].get<int>();
                    if (pj.contains("rings")) c.params.rings = pj["rings"].get<int>();
                    if (pj.contains("sectors")) c.params.sectors = pj["sectors"].get<int>();
                    if (pj.contains("subdivisions")) c.params.subdivisions = pj["subdivisions"].get<int>();
                    if (pj.contains("sides")) c.params.sides = pj["sides"].get<int>();
                    if (pj.contains("capTop")) c.params.capTop = pj["capTop"].get<bool>();
                    if (pj.contains("capBottom")) c.params.capBottom = pj["capBottom"].get<bool>();
                }

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

std::filesystem::path BehaviourAsset::ResolveAssetMeshPath(const std::string& meshPath, const std::string& assetFilePath) {
    if (meshPath.empty()) return {};
    std::error_code ec;
    std::filesystem::path p(meshPath);
    if (std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec)) {
        return p;
    }

    // 1. Try AssetManager resolution
    std::filesystem::path resolved = AssetManager::Get().ResolvePhysicalOrCookedPath(meshPath);
    if (!resolved.empty() && std::filesystem::exists(resolved, ec) && !std::filesystem::is_directory(resolved, ec)) {
        return resolved;
    }

    // 2. Try EngineUI content root
    if (EngineUI::GetInstance()) {
        std::filesystem::path cRoot = EngineUI::GetInstance()->contentRootPath / meshPath;
        if (std::filesystem::exists(cRoot, ec) && !std::filesystem::is_directory(cRoot, ec)) {
            return cRoot;
        }
    }

    // 3. Try relative to the behaviour asset's directory and project Content directory
    if (!assetFilePath.empty()) {
        std::filesystem::path assetDir = std::filesystem::path(assetFilePath).parent_path();
        std::filesystem::path cand1 = assetDir / meshPath;
        if (std::filesystem::exists(cand1, ec) && !std::filesystem::is_directory(cand1, ec)) {
            return cand1;
        }
        std::filesystem::path contentDir = assetDir.parent_path();
        std::filesystem::path cand2 = contentDir / meshPath;
        if (std::filesystem::exists(cand2, ec) && !std::filesystem::is_directory(cand2, ec)) {
            return cand2;
        }
        std::filesystem::path cand3 = contentDir / "Content" / meshPath;
        if (std::filesystem::exists(cand3, ec) && !std::filesystem::is_directory(cand3, ec)) {
            return cand3;
        }
    }

    return meshPath;
}

static void TagDescendantsBehaviourRoot(Scene& scene, int actorId, int rootActorId) {
    GameObject* obj = scene.FindObject(actorId);
    if (!obj) return;
    obj->behaviourRootId = rootActorId;
    std::vector<int> children = obj->childIds;
    for (int chId : children) {
        TagDescendantsBehaviourRoot(scene, chId, rootActorId);
    }
}

GameObject* BehaviourAsset::SpawnIntoScene(Scene& scene, const glm::vec3& worldPos, const glm::vec3& worldRot) {
    if (components.empty()) return nullptr;

    std::unordered_map<int, int> compIdToObjId;

    // 1. Spawn root actor
    const auto& rootComp = components.front();
    GameObject& spawnedRoot = scene.AddEmptyActor(worldPos + rootComp.localPosition);
    int rootActorId = spawnedRoot.id;
    spawnedRoot.name = name.empty() ? "BP_Actor" : name;
    spawnedRoot.rotation = worldRot + rootComp.localRotation;
    spawnedRoot.scale = rootComp.localScale;
    spawnedRoot.isBehaviourInstance = true;
    spawnedRoot.behaviourAssetPath = filePath;
    compIdToObjId[rootComp.id] = rootActorId;

    // 2. Spawn remaining components as children
    for (size_t i = 1; i < components.size(); ++i) {
        const auto& c = components[i];

        int spawnedChildId = -1;
        if (!c.meshPath.empty()) {
            std::filesystem::path resolvedMesh = ResolveAssetMeshPath(c.meshPath, filePath);
            GameObject& mObj = scene.AddImportedMesh(resolvedMesh.string(), c.localPosition);
            spawnedChildId = mObj.id;
        } else if (c.type == PrimitiveType::Empty) {
            GameObject& eObj = scene.AddEmptyActor(c.localPosition);
            spawnedChildId = eObj.id;
        } else {
            GameObject& pObj = scene.AddObject(c.type, c.localPosition);
            spawnedChildId = pObj.id;
        }

        if (spawnedChildId != -1) {
            compIdToObjId[c.id] = spawnedChildId;

            GameObject* childObj = scene.FindObject(spawnedChildId);
            if (childObj) {
                childObj->name = c.name;
                childObj->rotation = c.localRotation;
                childObj->scale = c.localScale;
                childObj->color = c.color;
                childObj->visible = c.visible;
                if (!c.materialPath.empty()) {
                    childObj->materialName = c.materialPath;
                }
                childObj->metallic = c.metallic;
                childObj->roughness = c.roughness;
                childObj->specular = c.specular;
                childObj->normalStrength = c.normalStrength;
                childObj->smoothShading = c.smoothShading;
                childObj->autoSmoothAngle = c.autoSmoothAngle;
                childObj->uvScale = c.uvScale;
                childObj->params = c.params;

                if (c.type == PrimitiveType::PointLight || c.type == PrimitiveType::SpotLight) {
                    childObj->isLight = true;
                    childObj->light.type = (c.type == PrimitiveType::SpotLight) ? LightType::Spot : LightType::Point;
                    childObj->light.color = c.lightColor;
                    childObj->light.intensity = c.lightIntensity;
                    childObj->light.range = c.lightRange;
                    childObj->light.enabled = c.visible;
                    childObj->light.innerConeAngle = c.innerConeAngle;
                    childObj->light.outerConeAngle = c.outerConeAngle;
                    childObj->light.coneFalloff = c.coneFalloff;
                    childObj->light.rangeSmoothing = c.rangeSmoothing;
                    childObj->light.attenuation = c.attenuation;
                    childObj->light.radius = c.radius;
                    childObj->light.length = c.length;
                    childObj->light.useTemperature = c.useTemperature;
                    childObj->light.temperature = c.temperature;
                    childObj->light.castShadows = c.castShadows;

                    PointLight pl;
                    pl.id = spawnedChildId;
                    pl.name = c.name;
                    pl.type = childObj->light.type;
                    pl.position = worldPos + c.localPosition;
                    pl.color = c.useTemperature ? (c.lightColor * ColorTemperatureToRGB(c.temperature)) : c.lightColor;
                    pl.intensity = c.lightIntensity;
                    pl.range = c.lightRange;
                    pl.enabled = c.visible;
                    pl.innerConeAngle = c.innerConeAngle;
                    pl.outerConeAngle = c.outerConeAngle;
                    pl.castShadows = c.castShadows;
                    scene.pointLights.push_back(pl);
                }

                childObj->behaviourRootId = rootActorId;
            }

            TagDescendantsBehaviourRoot(scene, spawnedChildId, rootActorId);

            int pId = c.parentId;
            int parentObjId = rootActorId;
            if (pId != 0 && compIdToObjId.find(pId) != compIdToObjId.end()) {
                parentObjId = compIdToObjId[pId];
            }

            GameObject* pObj = scene.FindObject(parentObjId);
            childObj = scene.FindObject(spawnedChildId);
            if (pObj && childObj) {
                childObj->parentId = parentObjId;
                if (std::find(pObj->childIds.begin(), pObj->childIds.end(), spawnedChildId) == pObj->childIds.end()) {
                    pObj->childIds.push_back(spawnedChildId);
                }
            }
        }
    }

    // 3. Attach Behaviour Scripts to root actor
    GameObject* rootObj = scene.FindObject(rootActorId);
    if (!rootObj) return nullptr;

    for (const auto& s : attachedScripts) {
        if (!s.enabled) continue;

        std::string scriptClassName = s.scriptName;
        size_t dotPos = scriptClassName.rfind('.');
        if (dotPos != std::string::npos) scriptClassName = scriptClassName.substr(0, dotPos);

        auto beh = BehaviourRegistry::Get().Create(scriptClassName);
        if (beh) {
            rootObj = scene.FindObject(rootActorId);
            if (!rootObj) break;

            beh->SetOwner(rootObj);
            beh->SetScene(&scene);
            if (!s.sourceCppPath.empty()) {
                beh->SetSourceCppPath(s.sourceCppPath);
            }

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

    scene.selectedId = rootActorId;
    return scene.FindObject(rootActorId);
}

void BehaviourAsset::SyncInstancesInScene(Scene& scene) {
    if (filePath.empty()) return;

    std::vector<int> instanceRootIds;
    for (const auto& obj : scene.objects) {
        if (obj.isBehaviourInstance && obj.behaviourAssetPath == filePath) {
            instanceRootIds.push_back(obj.id);
        }
    }

    for (int rootId : instanceRootIds) {
        GameObject* rootObj = scene.FindObject(rootId);
        if (!rootObj) continue;

        std::vector<int> oldChildIds;
        for (const auto& obj : scene.objects) {
            if (obj.behaviourRootId == rootId) {
                oldChildIds.push_back(obj.id);
            }
        }
        for (int cid : oldChildIds) {
            scene.RemoveObject(cid);
        }

        rootObj = scene.FindObject(rootId);
        if (!rootObj) continue;
        rootObj->childIds.clear();

        std::unordered_map<int, int> compIdToObjId;
        if (!components.empty()) {
            compIdToObjId[components.front().id] = rootId;
        }

        for (size_t i = 1; i < components.size(); ++i) {
            const auto& c = components[i];
            if (!c.visible) continue;

            int spawnedChildId = -1;
            if (!c.meshPath.empty()) {
                std::filesystem::path resolvedMesh = ResolveAssetMeshPath(c.meshPath, filePath);
                GameObject& mObj = scene.AddImportedMesh(resolvedMesh.string(), c.localPosition);
                spawnedChildId = mObj.id;
            } else if (c.type == PrimitiveType::Empty) {
                GameObject& eObj = scene.AddEmptyActor(c.localPosition);
                spawnedChildId = eObj.id;
            } else {
                GameObject& pObj = scene.AddObject(c.type, c.localPosition);
                spawnedChildId = pObj.id;
            }

            if (spawnedChildId != -1) {
                compIdToObjId[c.id] = spawnedChildId;

                GameObject* childObj = scene.FindObject(spawnedChildId);
                if (childObj) {
                    childObj->name = c.name;
                    childObj->rotation = c.localRotation;
                    childObj->scale = c.localScale;
                    childObj->color = c.color;
                    childObj->visible = c.visible;
                    if (!c.materialPath.empty()) {
                        childObj->materialName = c.materialPath;
                    }
                    childObj->metallic = c.metallic;
                    childObj->roughness = c.roughness;
                    childObj->specular = c.specular;
                    childObj->normalStrength = c.normalStrength;
                    childObj->smoothShading = c.smoothShading;
                    childObj->autoSmoothAngle = c.autoSmoothAngle;
                    childObj->uvScale = c.uvScale;
                    childObj->params = c.params;

                    if (c.type == PrimitiveType::PointLight || c.type == PrimitiveType::SpotLight) {
                        childObj->isLight = true;
                        childObj->light.type = (c.type == PrimitiveType::SpotLight) ? LightType::Spot : LightType::Point;
                        childObj->light.color = c.lightColor;
                        childObj->light.intensity = c.lightIntensity;
                        childObj->light.range = c.lightRange;
                        childObj->light.enabled = c.visible;
                        childObj->light.innerConeAngle = c.innerConeAngle;
                        childObj->light.outerConeAngle = c.outerConeAngle;
                        childObj->light.coneFalloff = c.coneFalloff;
                        childObj->light.rangeSmoothing = c.rangeSmoothing;
                        childObj->light.attenuation = c.attenuation;
                        childObj->light.radius = c.radius;
                        childObj->light.length = c.length;
                        childObj->light.useTemperature = c.useTemperature;
                        childObj->light.temperature = c.temperature;
                        childObj->light.castShadows = c.castShadows;

                        PointLight pl;
                        pl.id = spawnedChildId;
                        pl.name = c.name;
                        pl.type = childObj->light.type;
                        pl.position = rootObj ? (rootObj->position + c.localPosition) : c.localPosition;
                        pl.color = c.useTemperature ? (c.lightColor * ColorTemperatureToRGB(c.temperature)) : c.lightColor;
                        pl.intensity = c.lightIntensity;
                        pl.range = c.lightRange;
                        pl.enabled = c.visible;
                        pl.innerConeAngle = c.innerConeAngle;
                        pl.outerConeAngle = c.outerConeAngle;
                        pl.castShadows = c.castShadows;
                        scene.pointLights.push_back(pl);
                    }

                    childObj->behaviourRootId = rootId;
                }

                TagDescendantsBehaviourRoot(scene, spawnedChildId, rootId);

                int pId = c.parentId;
                int parentObjId = rootId;
                if (pId != 0 && compIdToObjId.find(pId) != compIdToObjId.end()) {
                    parentObjId = compIdToObjId[pId];
                }

                GameObject* pObj = scene.FindObject(parentObjId);
                childObj = scene.FindObject(spawnedChildId);
                if (pObj && childObj) {
                    childObj->parentId = parentObjId;
                    if (std::find(pObj->childIds.begin(), pObj->childIds.end(), spawnedChildId) == pObj->childIds.end()) {
                        pObj->childIds.push_back(spawnedChildId);
                    }
                }
            }
        }

        rootObj = scene.FindObject(rootId);
        if (!rootObj) continue;
        rootObj->behaviours.clear();

        for (const auto& s : attachedScripts) {
            if (!s.enabled) continue;

            std::string scriptClassName = s.scriptName;
            size_t dotPos = scriptClassName.rfind('.');
            if (dotPos != std::string::npos) scriptClassName = scriptClassName.substr(0, dotPos);

            auto beh = BehaviourRegistry::Get().Create(scriptClassName);
            if (beh) {
                rootObj = scene.FindObject(rootId);
                if (!rootObj) break;

                beh->SetOwner(rootObj);
                beh->SetScene(&scene);

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
    }
}
} // namespace Eunoia
