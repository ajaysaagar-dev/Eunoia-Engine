#include <Node-Behaviour/NodeBehaviourInstance.h>
#include <EngineScene/GameObject.h>
#include <EngineScene/Scene.h>
#include <EngineScene/BehaviourRegistry.h>
#include <filesystem>

namespace Eunoia {
namespace NodeBehaviour {

struct AutoRegisterNodeBehaviour {
    AutoRegisterNodeBehaviour() {
        BehaviourRegistry::Get().Register<NodeBehaviourInstance>(
            "NodeBehaviourInstance", "Node Behaviour", "Unreal Engine-style visual scripting behaviour"
        );
    }
};
static AutoRegisterNodeBehaviour s_autoRegisterNodeBehaviour;

NodeBehaviourInstance::NodeBehaviourInstance(const std::string& scriptAssetPath)
    : m_scriptAssetPath(scriptAssetPath)
{
    SetScriptAssetPath(scriptAssetPath);
}

void NodeBehaviourInstance::SetScriptAssetPath(const std::string& path) {
    m_scriptAssetPath = path;
    if (!path.empty()) {
        std::error_code ec;
        std::filesystem::path p(path);
        m_displayName = p.stem().string();

        m_graph = std::make_shared<NodeBehaviourGraph>();
        if (m_graph->LoadFromFile(path)) {
            SyncVariablesFromGraph();
        }
    }
}

void NodeBehaviourInstance::RefreshPropertiesFromSource() {
    if (!m_scriptAssetPath.empty()) {
        if (!m_graph) {
            m_graph = std::make_shared<NodeBehaviourGraph>();
            m_graph->LoadFromFile(m_scriptAssetPath);
        }
        SyncVariablesFromGraph();
    }
}

void NodeBehaviourInstance::ReloadGraph(std::shared_ptr<NodeBehaviourGraph> newGraph) {
    if (newGraph) {
        m_graph = newGraph;
    } else if (!m_scriptAssetPath.empty()) {
        m_graph = std::make_shared<NodeBehaviourGraph>();
        m_graph->LoadFromFile(m_scriptAssetPath);
    }
    SyncVariablesFromGraph();
}

void NodeBehaviourInstance::SyncVariablesFromGraph() {
    if (!m_graph) return;

    std::unordered_map<std::string, NodeVariable> updatedVars;
    for (const auto& gv : m_graph->variables) {
        auto it = m_instanceVars.find(gv.name);
        if (it != m_instanceVars.end() && it->second.type == gv.type) {
            NodeVariable v = gv;
            v.floatValue = it->second.floatValue;
            v.intValue = it->second.intValue;
            v.boolValue = it->second.boolValue;
            v.stringValue = it->second.stringValue;
            v.vectorValue = it->second.vectorValue;
            v.targetObjectId = it->second.targetObjectId;
            updatedVars[gv.name] = v;
        } else {
            updatedVars[gv.name] = gv;
        }
    }
    m_instanceVars = std::move(updatedVars);
    SetupExposedProperties();
}

void NodeBehaviourInstance::SetupExposedProperties() {
    m_properties.clear();

    for (auto& pair : m_instanceVars) {
        auto& v = pair.second;
        if (!v.isPublic) continue;

        BehaviourProperty prop;
        prop.name = v.name;
        prop.displayName = v.name;
        prop.category = "Variables";

        switch (v.type) {
            case NodePinType::Float:
                prop.type = BehaviourPropertyType::Float;
                prop.dataPtr = &v.floatValue;
                break;
            case NodePinType::Int:
                prop.type = BehaviourPropertyType::Int;
                prop.dataPtr = &v.intValue;
                break;
            case NodePinType::Bool:
                prop.type = BehaviourPropertyType::Bool;
                prop.dataPtr = &v.boolValue;
                break;
            case NodePinType::String:
                prop.type = BehaviourPropertyType::String;
                prop.dataPtr = &v.stringValue;
                break;
            case NodePinType::Vector3:
                prop.type = BehaviourPropertyType::Vec3;
                prop.dataPtr = &v.vectorValue;
                break;
            case NodePinType::GameObject:
                prop.type = BehaviourPropertyType::ObjectRef;
                prop.refType = ObjectRefType::Actor;
                prop.targetId = v.targetObjectId;
                prop.dataPtr = nullptr;
                break;
            default:
                continue;
        }

        m_properties.push_back(prop);
    }
}

std::unique_ptr<EunoiaBehaviour> NodeBehaviourInstance::Clone() const {
    auto clone = std::make_unique<NodeBehaviourInstance>(m_scriptAssetPath);
    clone->m_displayName = m_displayName;
    clone->m_instanceVars = m_instanceVars;
    clone->SetupExposedProperties();
    return clone;
}

void NodeBehaviourInstance::ResolveReferences(Scene& scene) {
    EunoiaBehaviour::ResolveReferences(scene);
    for (auto& prop : m_properties) {
        if (prop.type == BehaviourPropertyType::ObjectRef) {
            auto it = m_instanceVars.find(prop.name);
            if (it != m_instanceVars.end()) {
                it->second.targetObjectId = prop.targetId;
            }
        }
    }
}

void NodeBehaviourInstance::OnCreate() {
    if (!m_graph && !m_scriptAssetPath.empty()) {
        SetScriptAssetPath(m_scriptAssetPath);
    }
}

void NodeBehaviourInstance::Start() {
    if (m_graph) {
        for (const auto& prop : m_properties) {
            if (prop.type == BehaviourPropertyType::ObjectRef) {
                auto it = m_instanceVars.find(prop.name);
                if (it != m_instanceVars.end()) {
                    it->second.targetObjectId = prop.targetId;
                }
            }
        }
        m_graph->ExecuteEvent("Event Start", 0.0f, GetOwner(), GetScene(), m_instanceVars);
    }
}

void NodeBehaviourInstance::Update(float deltaTime) {
    if (m_graph) {
        m_graph->ExecuteEvent("Event Update", deltaTime, GetOwner(), GetScene(), m_instanceVars);
    }
}

void NodeBehaviourInstance::FixedUpdate(float fixedDeltaTime) {
    if (m_graph) {
        m_graph->ExecuteEvent("Event FixedUpdate", fixedDeltaTime, GetOwner(), GetScene(), m_instanceVars);
    }
}

void NodeBehaviourInstance::OnDestroy() {
}

} // namespace NodeBehaviour
} // namespace Eunoia
