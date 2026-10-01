#pragma once
// ============================================================================
// Visual-Behaviuors Plugin — Visual Behaviour Instance (Runtime & Details)
// Inherits from EunoiaBehaviour to seamlessly attach to any GameObject.
// Exposes variables in Details Panel and evaluates node graph during Play Mode.
// ============================================================================

#include <EngineScene/EunoiaBehaviour.h>
#include <EngineScene/BehaviourRegistry.h>
#include <EngineScene/ScreenPrint.h>
#include <EngineScene/GameObject.h>
#include "BehaviourGraph.h"
#include "BehaviourSerializer.h"
#include "NodeLibrary.h"

#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace VisualBehaviuors {

class VisualBehaviourInstance : public EunoiaBehaviour {
public:
    std::string m_assetPath;
    BehaviourGraph m_graph;

    // Stable storage pools for dynamically reflected properties from the graph
    std::map<std::string, bool>        m_bools;
    std::map<std::string, int>         m_ints;
    std::map<std::string, float>       m_floats;
    std::map<std::string, std::string> m_strings;
    std::map<std::string, glm::vec3>   m_vec3s;
    std::map<std::string, void*>       m_refs; // GameObject* references

    VisualBehaviourInstance(const std::string& assetPath = "")
        : m_assetPath(assetPath)
    {
        RegisterBuiltinNodes();
        if (!m_assetPath.empty()) {
            LoadAndRegister(false);
        } else {
            m_className = "VisualBehaviour";
            m_displayName = "Visual Behaviour";
        }
    }

    std::string GetAssetPath() const { return m_assetPath; }
    std::string GetSourceCppPath() const override { return m_assetPath; }

    void LoadAndRegister(bool preserveValues = true) {
        if (m_assetPath.empty()) return;
        std::filesystem::path p(m_assetPath);
        std::string stem = p.stem().string();
        m_className = "VisualBehaviour:" + stem;
        m_displayName = stem;

        LoadGraph(m_graph, m_assetPath);
        RegisterPropertiesFromGraph(preserveValues);
    }

    void RefreshPropertiesFromSource() override {
        LoadAndRegister(true);
    }

    void ResetToDefaults() override {
        LoadAndRegister(false);
    }

    void RegisterPropertiesFromGraph(bool preserveValues = true) {
        m_properties.clear();

        for (const auto& v : m_graph.variables) {
            switch (v.type) {
                case VarType::Bool: {
                    if (!preserveValues || m_bools.find(v.name) == m_bools.end()) {
                        m_bools[v.name] = (v.defaultValue == "true" || v.defaultValue == "1");
                    }
                    RegisterProperty(v.name, &m_bools[v.name], "Variables");
                    break;
                }
                case VarType::Int: {
                    if (!preserveValues || m_ints.find(v.name) == m_ints.end()) {
                        m_ints[v.name] = std::atoi(v.defaultValue.c_str());
                    }
                    RegisterProperty(v.name, &m_ints[v.name], "Variables");
                    break;
                }
                case VarType::Float: {
                    if (!preserveValues || m_floats.find(v.name) == m_floats.end()) {
                        m_floats[v.name] = (float)std::atof(v.defaultValue.c_str());
                    }
                    RegisterProperty(v.name, &m_floats[v.name], "Variables");
                    break;
                }
                case VarType::String: {
                    if (!preserveValues || m_strings.find(v.name) == m_strings.end()) {
                        m_strings[v.name] = v.defaultValue;
                    }
                    RegisterProperty(v.name, &m_strings[v.name], "Variables");
                    break;
                }
                case VarType::Vector3: {
                    if (!preserveValues || m_vec3s.find(v.name) == m_vec3s.end()) {
                        m_vec3s[v.name] = glm::vec3(0.0f);
                    }
                    RegisterProperty(v.name, &m_vec3s[v.name], "Variables");
                    break;
                }
                case VarType::GameObject: {
                    if (!preserveValues || m_refs.find(v.name) == m_refs.end()) {
                        m_refs[v.name] = nullptr;
                    }
                    // Registers as an Actor reference so any scene GameObject is selectable
                    RegisterReference(v.name, &m_refs[v.name], ObjectRefType::Actor, "Variables");
                    break;
                }
            }
        }
    }

    std::unique_ptr<EunoiaBehaviour> Clone() const override {
        auto clone = std::make_unique<VisualBehaviourInstance>(m_assetPath);
        clone->m_className = m_className;
        clone->m_displayName = m_displayName;
        clone->m_graph = m_graph;
        clone->m_bools = m_bools;
        clone->m_ints = m_ints;
        clone->m_floats = m_floats;
        clone->m_strings = m_strings;
        clone->m_vec3s = m_vec3s;
        clone->m_refs = m_refs;
        clone->RegisterPropertiesFromGraph(true);
        clone->CopyPropertiesFrom(*this);
        clone->Transform.Init(clone.get());
        clone->GetRespectiveObject.behaviour = clone.get();
        clone->CursorState.behaviour = clone.get();
        clone->cursorState.behaviour = clone.get();
        return clone;
    }

    // ─── Runtime Lifecycle ───────────────────────────────────────────────────
    void Start() override {
        ExecuteEvent("Event.Start", 0.0f);
        ExecuteEvent("Event.BeginPlay", 0.0f);
    }

    void Update(float deltaTime) override {
        ExecuteEvent("Event.Update", deltaTime);
        ExecuteEvent("Event.Tick", deltaTime);
    }

    void OnDestroy() override {
        ExecuteEvent("Event.OnDestroy", 0.0f);
        ExecuteEvent("Event.Destroy", 0.0f);
    }

private:
    void ExecuteEvent(const std::string& eventType, float dt) {
        for (const auto& node : m_graph.nodes) {
            if (node.typeId == eventType) {
                // Find outgoing Exec pin
                for (const auto& pin : node.pins) {
                    if (pin.kind == PinKind::Exec && pin.direction == PinDirection::Output) {
                        for (const auto& link : m_graph.links) {
                            if (link.fromPin == pin.id) {
                                ExecuteFlow(link.toNode, link.toPin, dt, 0);
                            }
                        }
                    }
                }
            }
        }
    }

    void ExecuteFlow(NodeId nodeId, PinId inPinId, float dt, int depth) {
        if (depth > 64) return; // Prevent infinite loop in cyclic graphs
        Node* node = m_graph.FindNode(nodeId);
        if (!node) return;

        if (node->typeId == "Debug.Print" || node->typeId == "Debug.PrintString" || node->typeId == "Print") {
            std::string text = EvaluateString(nodeId, "String", dt);
            if (text.empty()) {
                text = EvaluateString(nodeId, "Text", dt);
            }
            if (text.empty()) {
                auto pit = node->params.find("text");
                if (pit != node->params.end() && !pit->second.empty()) text = pit->second;
                else {
                    auto pit2 = node->params.find("String");
                    if (pit2 != node->params.end() && !pit2->second.empty()) text = pit2->second;
                    else text = "Hello from " + m_displayName + "!";
                }
            }

            float duration = EvaluateFloat(nodeId, "Duration", dt, 2.5f);
            if (duration <= 0.05f) duration = 2.5f;

            // Output to screen top-left overlay
            ScreenPrint::System::Get().AddMessage(text, duration);
            std::cout << "[VisualBehaviour:" << m_displayName << "] " << text << std::endl;

            TriggerNextExec(nodeId, dt, depth + 1);
        }
        else if (node->typeId == "Flow.Branch") {
            bool cond = EvaluateBool(nodeId, "Condition", dt, false);
            std::string outPinName = cond ? "True" : "False";
            for (const auto& pin : node->pins) {
                if (pin.kind == PinKind::Exec && pin.direction == PinDirection::Output && pin.name == outPinName) {
                    for (const auto& link : m_graph.links) {
                        if (link.fromPin == pin.id) {
                            ExecuteFlow(link.toNode, link.toPin, dt, depth + 1);
                        }
                    }
                }
            }
        }
        else if (node->typeId == "Flow.Sequence") {
            for (const auto& pin : node->pins) {
                if (pin.kind == PinKind::Exec && pin.direction == PinDirection::Output) {
                    for (const auto& link : m_graph.links) {
                        if (link.fromPin == pin.id) {
                            ExecuteFlow(link.toNode, link.toPin, dt, depth + 1);
                        }
                    }
                }
            }
        }
        else if (node->typeId == "Var.Set") {
            auto vit = node->params.find("varName");
            if (vit != node->params.end() && !vit->second.empty()) {
                std::string varName = vit->second;
                if (m_strings.find(varName) != m_strings.end()) {
                    m_strings[varName] = EvaluateString(nodeId, "Value", dt);
                } else if (m_floats.find(varName) != m_floats.end()) {
                    m_floats[varName] = EvaluateFloat(nodeId, "Value", dt, 0.0f);
                } else if (m_ints.find(varName) != m_ints.end()) {
                    m_ints[varName] = EvaluateInt(nodeId, "Value", dt, 0);
                } else if (m_bools.find(varName) != m_bools.end()) {
                    m_bools[varName] = EvaluateBool(nodeId, "Value", dt, false);
                }
            }
            TriggerNextExec(nodeId, dt, depth + 1);
        }
        else {
            TriggerNextExec(nodeId, dt, depth + 1);
        }
    }

    void TriggerNextExec(NodeId nodeId, float dt, int depth) {
        Node* node = m_graph.FindNode(nodeId);
        if (!node) return;
        for (const auto& pin : node->pins) {
            if (pin.kind == PinKind::Exec && pin.direction == PinDirection::Output) {
                for (const auto& link : m_graph.links) {
                    if (link.fromPin == pin.id) {
                        ExecuteFlow(link.toNode, link.toPin, dt, depth);
                    }
                }
            }
        }
    }

    // ─── Data Pin Evaluation ─────────────────────────────────────────────────
    std::string EvaluateString(NodeId targetNodeId, const std::string& pinName, float dt) {
        Node* node = m_graph.FindNode(targetNodeId);
        if (!node) return "";
        const Pin* inPin = nullptr;
        for (const auto& p : node->pins) {
            if (p.name == pinName && p.direction == PinDirection::Input) {
                inPin = &p;
                break;
            }
        }
        if (!inPin) return "";

        for (const auto& link : m_graph.links) {
            if (link.toPin == inPin->id) {
                Node* srcNode = m_graph.FindNode(link.fromNode);
                if (!srcNode) break;

                if (srcNode->typeId == "Var.Get") {
                    auto vit = srcNode->params.find("varName");
                    if (vit != srcNode->params.end()) {
                        std::string vn = vit->second;
                        if (m_refs.find(vn) != m_refs.end()) {
                            GameObject* go = reinterpret_cast<GameObject*>(m_refs[vn]);
                            return go ? go->name : "None";
                        }
                        if (m_strings.find(vn) != m_strings.end()) return m_strings[vn];
                        if (m_floats.find(vn) != m_floats.end()) return std::to_string(m_floats[vn]);
                        if (m_ints.find(vn) != m_ints.end()) return std::to_string(m_ints[vn]);
                        if (m_bools.find(vn) != m_bools.end()) return m_bools[vn] ? "true" : "false";
                        if (m_vec3s.find(vn) != m_vec3s.end()) {
                            glm::vec3 v = m_vec3s[vn];
                            return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
                        }
                    }
                } else if (srcNode->typeId == "Event.Update" || srcNode->typeId == "Event.Tick") {
                    return std::to_string(dt);
                } else if (srcNode->typeId == "Math.AddFloat" || srcNode->typeId == "Math.MulFloat") {
                    return std::to_string(EvaluateFloat(link.fromNode, "Result", dt, 0.0f));
                }
            }
        }

        auto pit = node->params.find(pinName);
        if (pit != node->params.end()) return pit->second;
        return "";
    }

    float EvaluateFloat(NodeId targetNodeId, const std::string& pinName, float dt, float defaultVal = 0.0f) {
        Node* node = m_graph.FindNode(targetNodeId);
        if (!node) return defaultVal;
        const Pin* inPin = nullptr;
        for (const auto& p : node->pins) {
            if (p.name == pinName && p.direction == PinDirection::Input) {
                inPin = &p;
                break;
            }
        }
        if (!inPin) return defaultVal;

        for (const auto& link : m_graph.links) {
            if (link.toPin == inPin->id) {
                Node* srcNode = m_graph.FindNode(link.fromNode);
                if (!srcNode) break;

                if (srcNode->typeId == "Var.Get") {
                    auto vit = srcNode->params.find("varName");
                    if (vit != srcNode->params.end()) {
                        std::string vn = vit->second;
                        if (m_floats.find(vn) != m_floats.end()) return m_floats[vn];
                        if (m_ints.find(vn) != m_ints.end()) return (float)m_ints[vn];
                    }
                } else if (srcNode->typeId == "Event.Update" || srcNode->typeId == "Event.Tick") {
                    return dt;
                } else if (srcNode->typeId == "Math.AddFloat") {
                    return EvaluateFloat(link.fromNode, "A", dt, 0.0f) + EvaluateFloat(link.fromNode, "B", dt, 0.0f);
                } else if (srcNode->typeId == "Math.MulFloat") {
                    return EvaluateFloat(link.fromNode, "A", dt, 0.0f) * EvaluateFloat(link.fromNode, "B", dt, 1.0f);
                }
            }
        }

        auto pit = node->params.find(pinName);
        if (pit != node->params.end() && !pit->second.empty()) return (float)std::atof(pit->second.c_str());
        return defaultVal;
    }

    int EvaluateInt(NodeId targetNodeId, const std::string& pinName, float dt, int defaultVal = 0) {
        Node* node = m_graph.FindNode(targetNodeId);
        if (!node) return defaultVal;
        const Pin* inPin = nullptr;
        for (const auto& p : node->pins) {
            if (p.name == pinName && p.direction == PinDirection::Input) {
                inPin = &p;
                break;
            }
        }
        if (!inPin) return defaultVal;

        for (const auto& link : m_graph.links) {
            if (link.toPin == inPin->id) {
                Node* srcNode = m_graph.FindNode(link.fromNode);
                if (!srcNode) break;

                if (srcNode->typeId == "Var.Get") {
                    auto vit = srcNode->params.find("varName");
                    if (vit != srcNode->params.end()) {
                        std::string vn = vit->second;
                        if (m_ints.find(vn) != m_ints.end()) return m_ints[vn];
                        if (m_floats.find(vn) != m_floats.end()) return (int)m_floats[vn];
                    }
                }
            }
        }

        auto pit = node->params.find(pinName);
        if (pit != node->params.end() && !pit->second.empty()) return std::atoi(pit->second.c_str());
        return defaultVal;
    }

    bool EvaluateBool(NodeId targetNodeId, const std::string& pinName, float dt, bool defaultVal = false) {
        Node* node = m_graph.FindNode(targetNodeId);
        if (!node) return defaultVal;
        const Pin* inPin = nullptr;
        for (const auto& p : node->pins) {
            if (p.name == pinName && p.direction == PinDirection::Input) {
                inPin = &p;
                break;
            }
        }
        if (!inPin) return defaultVal;

        for (const auto& link : m_graph.links) {
            if (link.toPin == inPin->id) {
                Node* srcNode = m_graph.FindNode(link.fromNode);
                if (!srcNode) break;

                if (srcNode->typeId == "Var.Get") {
                    auto vit = srcNode->params.find("varName");
                    if (vit != srcNode->params.end()) {
                        std::string vn = vit->second;
                        if (m_bools.find(vn) != m_bools.end()) return m_bools[vn];
                    }
                }
            }
        }

        auto pit = node->params.find(pinName);
        if (pit != node->params.end() && !pit->second.empty()) {
            return (pit->second == "true" || pit->second == "1");
        }
        return defaultVal;
    }
};

inline void RegisterVisualBehaviourAsset(const std::string& assetPath) {
    if (assetPath.empty()) return;
    std::filesystem::path p(assetPath);
    std::string stem = p.stem().string();
    std::string className = "VisualBehaviour:" + stem;

    BehaviourClassInfo info;
    info.className = className;
    info.displayName = stem + " (Visual Behaviour)";
    info.description = "Visual Behaviour Graph: " + assetPath;
    info.sourceCpp = assetPath;
    info.isNative = false;
    info.factory = [className, stem, assetPath]() -> std::unique_ptr<EunoiaBehaviour> {
        auto b = std::make_unique<VisualBehaviourInstance>(assetPath);
        b->SetClassName(className);
        b->SetDisplayName(stem);
        return b;
    };
    BehaviourRegistry::Get().RegisterCustom(info);
}

} // namespace VisualBehaviuors
