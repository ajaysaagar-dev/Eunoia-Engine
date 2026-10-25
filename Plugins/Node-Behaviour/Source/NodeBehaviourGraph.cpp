#include <Node-Behaviour/NodeBehaviourGraph.h>
#include <EngineCore/EngineLogger.h>
#include <EngineScene/ScreenPrint.h>
#include <EngineScene/GameObject.h>
#include <EngineScene/Scene.h>
#include <json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace Eunoia {
namespace NodeBehaviour {

NodeBehaviourGraph::NodeBehaviourGraph() {
}

void NodeBehaviourGraph::InitializeDefaultEvents() {
    nodes.clear();
    links.clear();
    m_nextId = 1;

    CreateEventStartNode(ImVec2(100.0f, 100.0f));
    CreateEventUpdateNode(ImVec2(100.0f, 260.0f));
    CreateEventFixedUpdateNode(ImVec2(100.0f, 440.0f));
}

NodeItem* NodeBehaviourGraph::CreateEventStartNode(const ImVec2& pos) {
    NodeItem node;
    node.id = GetNextId();
    node.name = "Event Start";
    node.subtitle = "Called when play mode begins";
    node.category = NodeCategory::Event;
    node.headerColor = ImVec4(0.78f, 0.12f, 0.12f, 1.0f); // Unreal Event Red
    node.position = pos;
    node.hasInitializedPos = true;

    // Outputs
    node.outputs.emplace_back(GetNextId(), node.id, "Exec", NodePinKind::Output, NodePinType::Exec);

    nodes.push_back(node);
    return &nodes.back();
}

NodeItem* NodeBehaviourGraph::CreateEventUpdateNode(const ImVec2& pos) {
    NodeItem node;
    node.id = GetNextId();
    node.name = "Event Update";
    node.subtitle = "Called every frame";
    node.category = NodeCategory::Event;
    node.headerColor = ImVec4(0.78f, 0.12f, 0.12f, 1.0f);
    node.position = pos;
    node.hasInitializedPos = true;

    // Outputs
    node.outputs.emplace_back(GetNextId(), node.id, "Exec", NodePinKind::Output, NodePinType::Exec);
    node.outputs.emplace_back(GetNextId(), node.id, "Delta Seconds", NodePinKind::Output, NodePinType::Float);

    nodes.push_back(node);
    return &nodes.back();
}

NodeItem* NodeBehaviourGraph::CreateEventFixedUpdateNode(const ImVec2& pos) {
    NodeItem node;
    node.id = GetNextId();
    node.name = "Event FixedUpdate";
    node.subtitle = "Called at fixed physics timestep";
    node.category = NodeCategory::Event;
    node.headerColor = ImVec4(0.78f, 0.12f, 0.12f, 1.0f);
    node.position = pos;
    node.hasInitializedPos = true;

    // Outputs
    node.outputs.emplace_back(GetNextId(), node.id, "Exec", NodePinKind::Output, NodePinType::Exec);
    node.outputs.emplace_back(GetNextId(), node.id, "Fixed Delta", NodePinKind::Output, NodePinType::Float);

    nodes.push_back(node);
    return &nodes.back();
}

NodeItem* NodeBehaviourGraph::CreatePrintNode(const ImVec2& pos) {
    NodeItem node;
    node.id = GetNextId();
    node.name = "Print String";
    node.subtitle = "Development";
    node.category = NodeCategory::Function;
    node.functionName = "Print";
    node.headerColor = ImVec4(0.12f, 0.45f, 0.88f, 1.0f); // Unreal Function Blue
    node.position = pos;
    node.hasInitializedPos = true;

    // Inputs
    node.inputs.emplace_back(GetNextId(), node.id, "Exec", NodePinKind::Input, NodePinType::Exec);
    
    NodePin strPin(GetNextId(), node.id, "In String", NodePinKind::Input, NodePinType::String);
    strPin.stringValue = "Hello from Node Behaviour!";
    node.inputs.push_back(strPin);

    NodePin screenPin(GetNextId(), node.id, "Print to Screen", NodePinKind::Input, NodePinType::Bool);
    screenPin.boolValue = true;
    node.inputs.push_back(screenPin);

    NodePin logPin(GetNextId(), node.id, "Print to Log", NodePinKind::Input, NodePinType::Bool);
    logPin.boolValue = true;
    node.inputs.push_back(logPin);

    NodePin durPin(GetNextId(), node.id, "Duration", NodePinKind::Input, NodePinType::Float);
    durPin.floatValue = 2.0f;
    node.inputs.push_back(durPin);

    // Outputs
    node.outputs.emplace_back(GetNextId(), node.id, "Exec", NodePinKind::Output, NodePinType::Exec);

    nodes.push_back(node);
    return &nodes.back();
}

NodeItem* NodeBehaviourGraph::CreateVariableGetNode(const std::string& varName, const ImVec2& pos) {
    NodeVariable* var = FindVariable(varName);
    NodePinType vType = var ? var->type : NodePinType::Float;

    NodeItem node;
    node.id = GetNextId();
    node.name = "Get " + varName;
    node.subtitle = "Variable";
    node.category = NodeCategory::VariableGet;
    node.variableName = varName;
    node.headerColor = ImVec4(0.15f, 0.55f, 0.45f, 1.0f); // Teal/Cyan
    node.position = pos;
    node.hasInitializedPos = true;

    // Outputs
    node.outputs.emplace_back(GetNextId(), node.id, varName, NodePinKind::Output, vType);

    nodes.push_back(node);
    return &nodes.back();
}

NodeItem* NodeBehaviourGraph::CreateVariableSetNode(const std::string& varName, const ImVec2& pos) {
    NodeVariable* var = FindVariable(varName);
    NodePinType vType = var ? var->type : NodePinType::Float;

    NodeItem node;
    node.id = GetNextId();
    node.name = "Set " + varName;
    node.subtitle = "Variable";
    node.category = NodeCategory::VariableSet;
    node.variableName = varName;
    node.headerColor = ImVec4(0.15f, 0.55f, 0.55f, 1.0f);
    node.position = pos;
    node.hasInitializedPos = true;

    // Inputs
    node.inputs.emplace_back(GetNextId(), node.id, "Exec", NodePinKind::Input, NodePinType::Exec);
    node.inputs.emplace_back(GetNextId(), node.id, varName, NodePinKind::Input, vType);

    // Outputs
    node.outputs.emplace_back(GetNextId(), node.id, "Exec", NodePinKind::Output, NodePinType::Exec);
    node.outputs.emplace_back(GetNextId(), node.id, "Value", NodePinKind::Output, vType);

    nodes.push_back(node);
    return &nodes.back();
}

NodeItem* NodeBehaviourGraph::CreateGetRelativeLocationNode(const ImVec2& pos) {
    NodeItem node;
    node.id = GetNextId();
    node.name = "Get Relative Location";
    node.subtitle = "Transform";
    node.category = NodeCategory::Function;
    node.functionName = "GetRelativeLocation";
    node.headerColor = ImVec4(0.08f, 0.45f, 0.55f, 1.0f);
    node.position = pos;
    node.hasInitializedPos = false;

    node.inputs.emplace_back(GetNextId(), node.id, "Target", NodePinKind::Input, NodePinType::GameObject);
    node.outputs.emplace_back(GetNextId(), node.id, "Relative Location", NodePinKind::Output, NodePinType::Vector3);

    nodes.push_back(node);
    return &nodes.back();
}

NodeItem* NodeBehaviourGraph::CreateSetRelativeLocationNode(const ImVec2& pos) {
    NodeItem node;
    node.id = GetNextId();
    node.name = "Set Relative Location";
    node.subtitle = "Transform";
    node.category = NodeCategory::Function;
    node.functionName = "SetRelativeLocation";
    node.headerColor = ImVec4(0.12f, 0.45f, 0.88f, 1.0f);
    node.position = pos;
    node.hasInitializedPos = false;

    node.inputs.emplace_back(GetNextId(), node.id, "Exec", NodePinKind::Input, NodePinType::Exec);
    node.inputs.emplace_back(GetNextId(), node.id, "Target", NodePinKind::Input, NodePinType::GameObject);
    node.inputs.emplace_back(GetNextId(), node.id, "New Location", NodePinKind::Input, NodePinType::Vector3);
    node.outputs.emplace_back(GetNextId(), node.id, "Exec", NodePinKind::Output, NodePinType::Exec);

    nodes.push_back(node);
    return &nodes.back();
}

NodeItem* NodeBehaviourGraph::CreateGetRelativeRotationNode(const ImVec2& pos) {
    NodeItem node;
    node.id = GetNextId();
    node.name = "Get Relative Rotation";
    node.subtitle = "Transform";
    node.category = NodeCategory::Function;
    node.functionName = "GetRelativeRotation";
    node.headerColor = ImVec4(0.08f, 0.45f, 0.55f, 1.0f);
    node.position = pos;
    node.hasInitializedPos = false;

    node.inputs.emplace_back(GetNextId(), node.id, "Target", NodePinKind::Input, NodePinType::GameObject);
    node.outputs.emplace_back(GetNextId(), node.id, "Relative Rotation", NodePinKind::Output, NodePinType::Vector3);

    nodes.push_back(node);
    return &nodes.back();
}

NodeItem* NodeBehaviourGraph::CreateSetRelativeRotationNode(const ImVec2& pos) {
    NodeItem node;
    node.id = GetNextId();
    node.name = "Set Relative Rotation";
    node.subtitle = "Transform";
    node.category = NodeCategory::Function;
    node.functionName = "SetRelativeRotation";
    node.headerColor = ImVec4(0.12f, 0.45f, 0.88f, 1.0f);
    node.position = pos;
    node.hasInitializedPos = false;

    node.inputs.emplace_back(GetNextId(), node.id, "Exec", NodePinKind::Input, NodePinType::Exec);
    node.inputs.emplace_back(GetNextId(), node.id, "Target", NodePinKind::Input, NodePinType::GameObject);
    node.inputs.emplace_back(GetNextId(), node.id, "New Rotation", NodePinKind::Input, NodePinType::Vector3);
    node.outputs.emplace_back(GetNextId(), node.id, "Exec", NodePinKind::Output, NodePinType::Exec);

    nodes.push_back(node);
    return &nodes.back();
}

NodeItem* NodeBehaviourGraph::CreateGetRelativeScaleNode(const ImVec2& pos) {
    NodeItem node;
    node.id = GetNextId();
    node.name = "Get Relative Scale";
    node.subtitle = "Transform";
    node.category = NodeCategory::Function;
    node.functionName = "GetRelativeScale";
    node.headerColor = ImVec4(0.08f, 0.45f, 0.55f, 1.0f);
    node.position = pos;
    node.hasInitializedPos = false;

    node.inputs.emplace_back(GetNextId(), node.id, "Target", NodePinKind::Input, NodePinType::GameObject);
    node.outputs.emplace_back(GetNextId(), node.id, "Relative Scale", NodePinKind::Output, NodePinType::Vector3);

    nodes.push_back(node);
    return &nodes.back();
}

NodeItem* NodeBehaviourGraph::CreateSetRelativeScaleNode(const ImVec2& pos) {
    NodeItem node;
    node.id = GetNextId();
    node.name = "Set Relative Scale";
    node.subtitle = "Transform";
    node.category = NodeCategory::Function;
    node.functionName = "SetRelativeScale";
    node.headerColor = ImVec4(0.12f, 0.45f, 0.88f, 1.0f);
    node.position = pos;
    node.hasInitializedPos = false;

    node.inputs.emplace_back(GetNextId(), node.id, "Exec", NodePinKind::Input, NodePinType::Exec);
    node.inputs.emplace_back(GetNextId(), node.id, "Target", NodePinKind::Input, NodePinType::GameObject);
    node.inputs.emplace_back(GetNextId(), node.id, "New Scale", NodePinKind::Input, NodePinType::Vector3);
    node.outputs.emplace_back(GetNextId(), node.id, "Exec", NodePinKind::Output, NodePinType::Exec);

    nodes.push_back(node);
    return &nodes.back();
}

NodeItem* NodeBehaviourGraph::FindNode(uint32_t nodeId) {
    for (auto& n : nodes) if (n.id == nodeId) return &n;
    return nullptr;
}

const NodeItem* NodeBehaviourGraph::FindNode(uint32_t nodeId) const {
    for (const auto& n : nodes) if (n.id == nodeId) return &n;
    return nullptr;
}

NodePin* NodeBehaviourGraph::FindPin(uint32_t pinId) {
    for (auto& n : nodes) {
        for (auto& p : n.inputs) if (p.id == pinId) return &p;
        for (auto& p : n.outputs) if (p.id == pinId) return &p;
    }
    return nullptr;
}

const NodePin* NodeBehaviourGraph::FindPin(uint32_t pinId) const {
    for (const auto& n : nodes) {
        for (const auto& p : n.inputs) if (p.id == pinId) return &p;
        for (const auto& p : n.outputs) if (p.id == pinId) return &p;
    }
    return nullptr;
}

NodeLink* NodeBehaviourGraph::FindLink(uint32_t linkId) {
    for (auto& l : links) if (l.id == linkId) return &l;
    return nullptr;
}

const NodeLink* NodeBehaviourGraph::FindLink(uint32_t linkId) const {
    for (const auto& l : links) if (l.id == linkId) return &l;
    return nullptr;
}

NodeVariable* NodeBehaviourGraph::FindVariable(const std::string& varName) {
    for (auto& v : variables) if (v.name == varName) return &v;
    return nullptr;
}

const NodeVariable* NodeBehaviourGraph::FindVariable(const std::string& varName) const {
    for (const auto& v : variables) if (v.name == varName) return &v;
    return nullptr;
}

NodeVariable* NodeBehaviourGraph::FindVariableById(uint32_t varId) {
    for (auto& v : variables) if (v.id == varId) return &v;
    return nullptr;
}

bool NodeBehaviourGraph::CanCreateLink(uint32_t pinA, uint32_t pinB) const {
    if (pinA == pinB) return false;
    const NodePin* pA = FindPin(pinA);
    const NodePin* pB = FindPin(pinB);
    if (!pA || !pB) return false;
    if (pA->nodeId == pB->nodeId) return false;
    if (pA->kind == pB->kind) return false; // Must connect Input to Output

    // Type compatibility
    if (pA->type != pB->type) return false;

    return true;
}

NodeLink* NodeBehaviourGraph::AddLink(uint32_t startPinId, uint32_t endPinId) {
    if (!CanCreateLink(startPinId, endPinId)) return nullptr;

    const NodePin* pA = FindPin(startPinId);
    const NodePin* pB = FindPin(endPinId);
    if (!pA || !pB) return nullptr;

    uint32_t fromPin = (pA->kind == NodePinKind::Output) ? startPinId : endPinId;
    uint32_t toPin   = (pA->kind == NodePinKind::Input) ? startPinId : endPinId;

    // If target input pin already has a link, remove old link
    for (size_t i = 0; i < links.size(); ++i) {
        if (links[i].endPinId == toPin) {
            links.erase(links.begin() + i);
            break;
        }
    }

    NodeLink link;
    link.id = GetNextId();
    link.startPinId = fromPin;
    link.endPinId = toPin;
    link.color = GetPinColor(pA->type);

    links.push_back(link);
    return &links.back();
}

void NodeBehaviourGraph::RemoveLink(uint32_t linkId) {
    for (size_t i = 0; i < links.size(); ++i) {
        if (links[i].id == linkId) {
            links.erase(links.begin() + i);
            return;
        }
    }
}

void NodeBehaviourGraph::RemoveNode(uint32_t nodeId) {
    // Remove links connected to any pin of this node
    NodeItem* node = FindNode(nodeId);
    if (!node) return;

    std::vector<uint32_t> pinIds;
    for (const auto& p : node->inputs) pinIds.push_back(p.id);
    for (const auto& p : node->outputs) pinIds.push_back(p.id);

    links.erase(std::remove_if(links.begin(), links.end(), [&](const NodeLink& l) {
        for (uint32_t pid : pinIds) {
            if (l.startPinId == pid || l.endPinId == pid) return true;
        }
        return false;
    }), links.end());

    // Remove node
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const NodeItem& n) {
        return n.id == nodeId;
    }), nodes.end());
}

NodeVariable* NodeBehaviourGraph::AddVariable(const std::string& varName, NodePinType type) {
    std::string finalName = varName;
    int suffix = 1;
    while (FindVariable(finalName) != nullptr) {
        finalName = varName + "_" + std::to_string(suffix++);
    }

    NodeVariable var;
    var.id = GetNextId();
    var.name = finalName;
    var.type = type;
    var.isPublic = true;

    variables.push_back(var);
    return &variables.back();
}

void NodeBehaviourGraph::RemoveVariable(uint32_t varId) {
    variables.erase(std::remove_if(variables.begin(), variables.end(), [&](const NodeVariable& v) {
        return v.id == varId;
    }), variables.end());
}

bool NodeBehaviourGraph::SaveToFile(const std::string& path) {
    json j;
    j["name"] = name;
    j["nextId"] = m_nextId;

    // Variables
    json varsJson = json::array();
    for (const auto& v : variables) {
        json vj;
        vj["id"] = v.id;
        vj["name"] = v.name;
        vj["type"] = (int)v.type;
        vj["floatValue"] = v.floatValue;
        vj["intValue"] = v.intValue;
        vj["boolValue"] = v.boolValue;
        vj["stringValue"] = v.stringValue;
        vj["vectorValue"] = { v.vectorValue.x, v.vectorValue.y, v.vectorValue.z };
        vj["tooltip"] = v.tooltip;
        vj["isPublic"] = v.isPublic;
        vj["targetObjectId"] = v.targetObjectId;
        varsJson.push_back(vj);
    }
    j["variables"] = varsJson;

    // Nodes
    json nodesJson = json::array();
    for (const auto& n : nodes) {
        json nj;
        nj["id"] = n.id;
        nj["name"] = n.name;
        nj["subtitle"] = n.subtitle;
        nj["category"] = (int)n.category;
        nj["headerColor"] = { n.headerColor.x, n.headerColor.y, n.headerColor.z, n.headerColor.w };
        nj["position"] = { n.position.x, n.position.y };
        nj["functionName"] = n.functionName;
        nj["variableName"] = n.variableName;

        json inPins = json::array();
        for (const auto& p : n.inputs) {
            json pj;
            pj["id"] = p.id;
            pj["name"] = p.name;
            pj["kind"] = (int)p.kind;
            pj["type"] = (int)p.type;
            pj["floatValue"] = p.floatValue;
            pj["intValue"] = p.intValue;
            pj["boolValue"] = p.boolValue;
            pj["stringValue"] = p.stringValue;
            pj["vectorValue"] = { p.vectorValue.x, p.vectorValue.y, p.vectorValue.z };
            pj["targetObjectId"] = p.targetObjectId;
            inPins.push_back(pj);
        }
        nj["inputs"] = inPins;

        json outPins = json::array();
        for (const auto& p : n.outputs) {
            json pj;
            pj["id"] = p.id;
            pj["name"] = p.name;
            pj["kind"] = (int)p.kind;
            pj["type"] = (int)p.type;
            outPins.push_back(pj);
        }
        nj["outputs"] = outPins;

        nodesJson.push_back(nj);
    }
    j["nodes"] = nodesJson;

    // Links
    json linksJson = json::array();
    for (const auto& l : links) {
        json lj;
        lj["id"] = l.id;
        lj["startPinId"] = l.startPinId;
        lj["endPinId"] = l.endPinId;
        lj["color"] = (uint32_t)l.color;
        linksJson.push_back(lj);
    }
    j["links"] = linksJson;

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << j.dump(4);
    filePath = path;
    return true;
}

bool NodeBehaviourGraph::LoadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    json j;
    try {
        file >> j;
    } catch (...) {
        return false;
    }

    nodes.clear();
    links.clear();
    variables.clear();

    if (j.contains("name")) name = j["name"].get<std::string>();
    if (j.contains("nextId")) m_nextId = j["nextId"].get<uint32_t>();

    // Variables
    if (j.contains("variables") && j["variables"].is_array()) {
        for (const auto& vj : j["variables"]) {
            NodeVariable v;
            v.id = vj.value("id", 0);
            v.name = vj.value("name", "Var");
            v.type = (NodePinType)vj.value("type", 0);
            v.floatValue = vj.value("floatValue", 0.0f);
            v.intValue = vj.value("intValue", 0);
            v.boolValue = vj.value("boolValue", false);
            v.stringValue = vj.value("stringValue", "");
            if (vj.contains("vectorValue") && vj["vectorValue"].is_array() && vj["vectorValue"].size() == 3) {
                v.vectorValue = glm::vec3(vj["vectorValue"][0], vj["vectorValue"][1], vj["vectorValue"][2]);
            }
            v.tooltip = vj.value("tooltip", "");
            v.isPublic = vj.value("isPublic", true);
            v.targetObjectId = vj.value("targetObjectId", -1);
            UpdateNextId(v.id);
            variables.push_back(v);
        }
    }

    // Nodes
    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& nj : j["nodes"]) {
            NodeItem n;
            n.id = nj.value("id", 0);
            n.name = nj.value("name", "Node");
            n.subtitle = nj.value("subtitle", "");
            n.category = (NodeCategory)nj.value("category", 0);
            if (nj.contains("headerColor") && nj["headerColor"].is_array() && nj["headerColor"].size() == 4) {
                n.headerColor = ImVec4(nj["headerColor"][0], nj["headerColor"][1], nj["headerColor"][2], nj["headerColor"][3]);
            }
            if (nj.contains("position") && nj["position"].is_array() && nj["position"].size() == 2) {
                n.position = ImVec2(nj["position"][0], nj["position"][1]);
            }
            n.hasInitializedPos = false; // Always allow canvas to position at loaded coordinates!
            n.functionName = nj.value("functionName", "");
            n.variableName = nj.value("variableName", "");
            UpdateNextId(n.id);

            // Inputs
            if (nj.contains("inputs") && nj["inputs"].is_array()) {
                for (const auto& pj : nj["inputs"]) {
                    NodePin p;
                    p.id = pj.value("id", 0);
                    p.nodeId = n.id;
                    p.name = pj.value("name", "");
                    p.kind = NodePinKind::Input;
                    p.type = (NodePinType)pj.value("type", 0);
                    p.floatValue = pj.value("floatValue", 0.0f);
                    p.intValue = pj.value("intValue", 0);
                    p.boolValue = pj.value("boolValue", false);
                    p.stringValue = pj.value("stringValue", "");
                    if (pj.contains("vectorValue") && pj["vectorValue"].is_array() && pj["vectorValue"].size() == 3) {
                        p.vectorValue = glm::vec3(pj["vectorValue"][0], pj["vectorValue"][1], pj["vectorValue"][2]);
                    }
                    p.targetObjectId = pj.value("targetObjectId", -1);
                    UpdateNextId(p.id);
                    n.inputs.push_back(p);
                }
            }

            // Outputs
            if (nj.contains("outputs") && nj["outputs"].is_array()) {
                for (const auto& pj : nj["outputs"]) {
                    NodePin p;
                    p.id = pj.value("id", 0);
                    p.nodeId = n.id;
                    p.name = pj.value("name", "");
                    p.kind = NodePinKind::Output;
                    p.type = (NodePinType)pj.value("type", 0);
                    UpdateNextId(p.id);
                    n.outputs.push_back(p);
                }
            }

            nodes.push_back(n);
        }
    }

    // Links
    if (j.contains("links") && j["links"].is_array()) {
        for (const auto& lj : j["links"]) {
            NodeLink l;
            l.id = lj.value("id", 0);
            l.startPinId = lj.value("startPinId", 0);
            l.endPinId = lj.value("endPinId", 0);
            l.color = lj.value("color", (uint32_t)IM_COL32(255, 255, 255, 255));
            UpdateNextId(l.id);
            links.push_back(l);
        }
    }

    filePath = path;
    return true;
}

void NodeBehaviourGraph::ExecuteEvent(
    const std::string& eventName,
    float deltaTime,
    GameObject* owner,
    Scene* scene,
    std::unordered_map<std::string, NodeVariable>& runtimeVars)
{
    for (auto& node : nodes) {
        if (node.category == NodeCategory::Event && node.name == eventName) {
            // Populate delta time pin if present
            for (auto& outPin : node.outputs) {
                if (outPin.type == NodePinType::Float) {
                    outPin.floatValue = deltaTime;
                }
            }

            // Trigger Exec pin
            for (auto& outPin : node.outputs) {
                if (outPin.type == NodePinType::Exec) {
                    ExecuteChain(outPin.id, deltaTime, owner, scene, runtimeVars);
                }
            }
        }
    }
}

void NodeBehaviourGraph::ExecuteChain(
    uint32_t startExecPinId,
    float deltaTime,
    GameObject* owner,
    Scene* scene,
    std::unordered_map<std::string, NodeVariable>& runtimeVars)
{
    uint32_t curExecPin = startExecPinId;
    int maxSteps = 1000; // Loop guard

    while (curExecPin != 0 && maxSteps-- > 0) {
        // Find link leaving curExecPin
        uint32_t targetInputPinId = 0;
        for (const auto& l : links) {
            if (l.startPinId == curExecPin) {
                targetInputPinId = l.endPinId;
                break;
            }
        }
        if (targetInputPinId == 0) break;

        NodePin* targetPin = FindPin(targetInputPinId);
        if (!targetPin) break;

        NodeItem* targetNode = FindNode(targetPin->nodeId);
        if (!targetNode) break;

        // Execute targetNode
        curExecPin = 0; // Default: stop unless next Exec is found

        if (targetNode->category == NodeCategory::Function) {
            if (targetNode->functionName == "Print") {
                // Read print parameters
                std::string printMsg = "Hello";
                float duration = 2.0f;
                bool printToScreen = true;
                bool printToLog = true;

                for (const auto& pin : targetNode->inputs) {
                    if (pin.name == "In String") {
                        printMsg = EvaluateStringPin(pin.id, runtimeVars);
                    } else if (pin.name == "Duration") {
                        duration = EvaluateFloatPin(pin.id, runtimeVars);
                    } else if (pin.name == "Print to Screen") {
                        printToScreen = EvaluateBoolPin(pin.id, runtimeVars);
                    } else if (pin.name == "Print to Log") {
                        printToLog = EvaluateBoolPin(pin.id, runtimeVars);
                    }
                }

                // Execute print
                if (printToScreen) {
                    ScreenPrint::System::Get().AddMessage(printMsg, duration, glm::vec4(0.0f, 1.0f, 1.0f, 1.0f));
                }
                if (printToLog) {
                    std::cout << "[NodeBehaviour] " << printMsg << std::endl;
                }

                // Proceed to next Exec output
                for (const auto& p : targetNode->outputs) {
                    if (p.type == NodePinType::Exec) {
                        curExecPin = p.id;
                        break;
                    }
                }
            } else if (targetNode->functionName == "SetRelativeLocation") {
                GameObject* target = nullptr;
                glm::vec3 newLoc(0.0f);
                for (const auto& pin : targetNode->inputs) {
                    if (pin.name == "Target") {
                        target = EvaluateGameObjectPin(pin.id, owner, scene, runtimeVars);
                    } else if (pin.name == "New Location") {
                        newLoc = EvaluateVector3Pin(pin.id, runtimeVars, owner, scene);
                    }
                }
                if (target) {
                    target->position = newLoc;
                    target->transform.RelativeLocation.SetValue(newLoc);
                }
                for (const auto& p : targetNode->outputs) {
                    if (p.type == NodePinType::Exec) {
                        curExecPin = p.id;
                        break;
                    }
                }
            } else if (targetNode->functionName == "SetRelativeRotation") {
                GameObject* target = nullptr;
                glm::vec3 newRot(0.0f);
                for (const auto& pin : targetNode->inputs) {
                    if (pin.name == "Target") {
                        target = EvaluateGameObjectPin(pin.id, owner, scene, runtimeVars);
                    } else if (pin.name == "New Rotation") {
                        newRot = EvaluateVector3Pin(pin.id, runtimeVars, owner, scene);
                    }
                }
                if (target) {
                    target->rotation = newRot;
                    target->transform.RelativeRotation.SetValue(newRot);
                }
                for (const auto& p : targetNode->outputs) {
                    if (p.type == NodePinType::Exec) {
                        curExecPin = p.id;
                        break;
                    }
                }
            } else if (targetNode->functionName == "SetRelativeScale") {
                GameObject* target = nullptr;
                glm::vec3 newScale(1.0f);
                for (const auto& pin : targetNode->inputs) {
                    if (pin.name == "Target") {
                        target = EvaluateGameObjectPin(pin.id, owner, scene, runtimeVars);
                    } else if (pin.name == "New Scale") {
                        newScale = EvaluateVector3Pin(pin.id, runtimeVars, owner, scene);
                    }
                }
                if (target) {
                    target->scale = newScale;
                    target->transform.RelativeScale.SetValue(newScale);
                }
                for (const auto& p : targetNode->outputs) {
                    if (p.type == NodePinType::Exec) {
                        curExecPin = p.id;
                        break;
                    }
                }
            }
        } else if (targetNode->category == NodeCategory::VariableSet) {
            std::string varName = targetNode->variableName;
            for (const auto& p : targetNode->inputs) {
                if (p.name == varName) {
                    if (p.type == NodePinType::Float) {
                        runtimeVars[varName].floatValue = EvaluateFloatPin(p.id, runtimeVars);
                    } else if (p.type == NodePinType::Int) {
                        runtimeVars[varName].intValue = EvaluateIntPin(p.id, runtimeVars);
                    } else if (p.type == NodePinType::Bool) {
                        runtimeVars[varName].boolValue = EvaluateBoolPin(p.id, runtimeVars);
                    } else if (p.type == NodePinType::String) {
                        runtimeVars[varName].stringValue = EvaluateStringPin(p.id, runtimeVars);
                    } else if (p.type == NodePinType::Vector3) {
                        runtimeVars[varName].vectorValue = EvaluateVector3Pin(p.id, runtimeVars);
                    }
                }
            }

            // Proceed to next Exec output
            for (const auto& p : targetNode->outputs) {
                if (p.type == NodePinType::Exec) {
                    curExecPin = p.id;
                    break;
                }
            }
        }
    }
}

std::string NodeBehaviourGraph::EvaluateStringPin(uint32_t pinId, std::unordered_map<std::string, NodeVariable>& runtimeVars) {
    const NodePin* pin = FindPin(pinId);
    if (!pin) return "";

    // Check if connected to an output pin
    for (const auto& l : links) {
        if (l.endPinId == pinId) {
            const NodePin* srcPin = FindPin(l.startPinId);
            if (srcPin) {
                const NodeItem* srcNode = FindNode(srcPin->nodeId);
                if (srcNode && srcNode->category == NodeCategory::VariableGet) {
                    auto it = runtimeVars.find(srcNode->variableName);
                    if (it != runtimeVars.end()) return it->second.stringValue;
                }
                return srcPin->stringValue;
            }
        }
    }
    return pin->stringValue;
}

float NodeBehaviourGraph::EvaluateFloatPin(uint32_t pinId, std::unordered_map<std::string, NodeVariable>& runtimeVars) {
    const NodePin* pin = FindPin(pinId);
    if (!pin) return 0.0f;

    for (const auto& l : links) {
        if (l.endPinId == pinId) {
            const NodePin* srcPin = FindPin(l.startPinId);
            if (srcPin) {
                const NodeItem* srcNode = FindNode(srcPin->nodeId);
                if (srcNode && srcNode->category == NodeCategory::VariableGet) {
                    auto it = runtimeVars.find(srcNode->variableName);
                    if (it != runtimeVars.end()) return it->second.floatValue;
                }
                return srcPin->floatValue;
            }
        }
    }
    return pin->floatValue;
}

int NodeBehaviourGraph::EvaluateIntPin(uint32_t pinId, std::unordered_map<std::string, NodeVariable>& runtimeVars) {
    const NodePin* pin = FindPin(pinId);
    if (!pin) return 0;

    for (const auto& l : links) {
        if (l.endPinId == pinId) {
            const NodePin* srcPin = FindPin(l.startPinId);
            if (srcPin) {
                const NodeItem* srcNode = FindNode(srcPin->nodeId);
                if (srcNode && srcNode->category == NodeCategory::VariableGet) {
                    auto it = runtimeVars.find(srcNode->variableName);
                    if (it != runtimeVars.end()) return it->second.intValue;
                }
                return srcPin->intValue;
            }
        }
    }
    return pin->intValue;
}

bool NodeBehaviourGraph::EvaluateBoolPin(uint32_t pinId, std::unordered_map<std::string, NodeVariable>& runtimeVars) {
    const NodePin* pin = FindPin(pinId);
    if (!pin) return false;

    for (const auto& l : links) {
        if (l.endPinId == pinId) {
            const NodePin* srcPin = FindPin(l.startPinId);
            if (srcPin) {
                const NodeItem* srcNode = FindNode(srcPin->nodeId);
                if (srcNode && srcNode->category == NodeCategory::VariableGet) {
                    auto it = runtimeVars.find(srcNode->variableName);
                    if (it != runtimeVars.end()) return it->second.boolValue;
                }
                return srcPin->boolValue;
            }
        }
    }
    return pin->boolValue;
}

glm::vec3 NodeBehaviourGraph::EvaluateVector3Pin(
    uint32_t pinId,
    std::unordered_map<std::string, NodeVariable>& runtimeVars,
    GameObject* owner,
    Scene* scene)
{
    const NodePin* pin = FindPin(pinId);
    if (!pin) return glm::vec3(0.0f);

    for (const auto& l : links) {
        if (l.endPinId == pinId) {
            const NodePin* srcPin = FindPin(l.startPinId);
            if (srcPin) {
                const NodeItem* srcNode = FindNode(srcPin->nodeId);
                if (srcNode) {
                    if (srcNode->category == NodeCategory::VariableGet) {
                        auto it = runtimeVars.find(srcNode->variableName);
                        if (it != runtimeVars.end()) return it->second.vectorValue;
                    } else if (srcNode->category == NodeCategory::Function) {
                        if (srcNode->functionName == "GetRelativeLocation") {
                            GameObject* target = nullptr;
                            for (const auto& inp : srcNode->inputs) {
                                if (inp.name == "Target") {
                                    target = EvaluateGameObjectPin(inp.id, owner, scene, runtimeVars);
                                    break;
                                }
                            }
                            return target ? target->position : (owner ? owner->position : glm::vec3(0.0f));
                        } else if (srcNode->functionName == "GetRelativeRotation") {
                            GameObject* target = nullptr;
                            for (const auto& inp : srcNode->inputs) {
                                if (inp.name == "Target") {
                                    target = EvaluateGameObjectPin(inp.id, owner, scene, runtimeVars);
                                    break;
                                }
                            }
                            return target ? target->rotation : (owner ? owner->rotation : glm::vec3(0.0f));
                        } else if (srcNode->functionName == "GetRelativeScale") {
                            GameObject* target = nullptr;
                            for (const auto& inp : srcNode->inputs) {
                                if (inp.name == "Target") {
                                    target = EvaluateGameObjectPin(inp.id, owner, scene, runtimeVars);
                                    break;
                                }
                            }
                            return target ? target->scale : (owner ? owner->scale : glm::vec3(1.0f));
                        }
                    }
                }
                return srcPin->vectorValue;
            }
        }
    }
    return pin->vectorValue;
}

GameObject* NodeBehaviourGraph::EvaluateGameObjectPin(
    uint32_t pinId,
    GameObject* owner,
    Scene* scene,
    std::unordered_map<std::string, NodeVariable>& runtimeVars)
{
    const NodePin* pin = FindPin(pinId);
    if (!pin) return owner;

    for (const auto& l : links) {
        if (l.endPinId == pinId) {
            const NodePin* srcPin = FindPin(l.startPinId);
            if (srcPin) {
                const NodeItem* srcNode = FindNode(srcPin->nodeId);
                if (srcNode && srcNode->category == NodeCategory::VariableGet) {
                    auto it = runtimeVars.find(srcNode->variableName);
                    if (it != runtimeVars.end() && it->second.targetObjectId != -1 && scene) {
                        return scene->FindObject(it->second.targetObjectId);
                    }
                }
                if (srcPin->targetObjectId != -1 && scene) {
                    return scene->FindObject(srcPin->targetObjectId);
                }
            }
        }
    }

    if (pin->targetObjectId != -1 && scene) {
        return scene->FindObject(pin->targetObjectId);
    }

    return owner; // Defaults to Self / owner
}

} // namespace NodeBehaviour
} // namespace Eunoia
