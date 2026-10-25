#pragma once
#include <Node-Behaviour/NodeBehaviourTypes.h>
#include <memory>
#include <unordered_map>

// Forward declarations
struct GameObject;
class Scene;

namespace Eunoia {
namespace NodeBehaviour {

class NodeBehaviourGraph {
public:
    NodeBehaviourGraph();
    ~NodeBehaviourGraph() = default;

    std::string filePath;
    std::string name = "NewNodeBehaviour";

    // Graph elements
    std::vector<NodeItem> nodes;
    std::vector<NodeLink> links;
    std::vector<NodeVariable> variables;

    // ID allocation
    uint32_t GetNextId() { return m_nextId++; }
    void UpdateNextId(uint32_t id) { if (id >= m_nextId) m_nextId = id + 1; }

    // Graph initialization
    void InitializeDefaultEvents();

    // Node Factories
    NodeItem* CreateEventStartNode(const ImVec2& pos = ImVec2(50, 50));
    NodeItem* CreateEventUpdateNode(const ImVec2& pos = ImVec2(50, 200));
    NodeItem* CreateEventFixedUpdateNode(const ImVec2& pos = ImVec2(50, 370));
    NodeItem* CreatePrintNode(const ImVec2& pos = ImVec2(350, 100));
    NodeItem* CreateVariableGetNode(const std::string& varName, const ImVec2& pos = ImVec2(350, 250));
    NodeItem* CreateVariableSetNode(const std::string& varName, const ImVec2& pos = ImVec2(350, 350));

    // Transform Node Factories
    NodeItem* CreateGetRelativeLocationNode(const ImVec2& pos = ImVec2(350, 150));
    NodeItem* CreateSetRelativeLocationNode(const ImVec2& pos = ImVec2(350, 250));
    NodeItem* CreateGetRelativeRotationNode(const ImVec2& pos = ImVec2(350, 350));
    NodeItem* CreateSetRelativeRotationNode(const ImVec2& pos = ImVec2(350, 450));
    NodeItem* CreateGetRelativeScaleNode(const ImVec2& pos = ImVec2(350, 550));
    NodeItem* CreateSetRelativeScaleNode(const ImVec2& pos = ImVec2(350, 650));

    // Element Lookups
    NodeItem* FindNode(uint32_t nodeId);
    const NodeItem* FindNode(uint32_t nodeId) const;
    NodePin* FindPin(uint32_t pinId);
    const NodePin* FindPin(uint32_t pinId) const;
    NodeLink* FindLink(uint32_t linkId);
    const NodeLink* FindLink(uint32_t linkId) const;
    NodeVariable* FindVariable(const std::string& varName);
    const NodeVariable* FindVariable(const std::string& varName) const;
    NodeVariable* FindVariableById(uint32_t varId);

    // Link Management
    bool CanCreateLink(uint32_t pinA, uint32_t pinB) const;
    NodeLink* AddLink(uint32_t startPinId, uint32_t endPinId);
    void RemoveLink(uint32_t linkId);
    void RemoveNode(uint32_t nodeId);

    // Variable Management
    NodeVariable* AddVariable(const std::string& varName = "NewVar", NodePinType type = NodePinType::Float);
    void RemoveVariable(uint32_t varId);

    // Serialization
    bool SaveToFile(const std::string& path);
    bool LoadFromFile(const std::string& path);

    // Runtime Execution
    void ExecuteEvent(
        const std::string& eventName,
        float deltaTime,
        GameObject* owner,
        Scene* scene,
        std::unordered_map<std::string, NodeVariable>& runtimeVars
    );

private:
    uint32_t m_nextId = 1;

    void ExecuteChain(
        uint32_t startExecPinId,
        float deltaTime,
        GameObject* owner,
        Scene* scene,
        std::unordered_map<std::string, NodeVariable>& runtimeVars
    );

    // Resolves input pin value: checks if connected to output data pin or uses inline default
    std::string EvaluateStringPin(uint32_t pinId, std::unordered_map<std::string, NodeVariable>& runtimeVars);
    float EvaluateFloatPin(uint32_t pinId, std::unordered_map<std::string, NodeVariable>& runtimeVars);
    int EvaluateIntPin(uint32_t pinId, std::unordered_map<std::string, NodeVariable>& runtimeVars);
    bool EvaluateBoolPin(uint32_t pinId, std::unordered_map<std::string, NodeVariable>& runtimeVars);
    glm::vec3 EvaluateVector3Pin(
        uint32_t pinId,
        std::unordered_map<std::string, NodeVariable>& runtimeVars,
        GameObject* owner = nullptr,
        Scene* scene = nullptr
    );
    GameObject* EvaluateGameObjectPin(
        uint32_t pinId,
        GameObject* owner,
        Scene* scene,
        std::unordered_map<std::string, NodeVariable>& runtimeVars
    );
};

} // namespace NodeBehaviour
} // namespace Eunoia
