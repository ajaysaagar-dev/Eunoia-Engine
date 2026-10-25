#pragma once
#include <EngineScene/EunoiaBehaviour.h>
#include <Node-Behaviour/NodeBehaviourGraph.h>
#include <unordered_map>
#include <memory>

namespace Eunoia {
namespace NodeBehaviour {

class NodeBehaviourInstance : public EunoiaBehaviour {
public:
    NodeBehaviourInstance(const std::string& scriptAssetPath = "");
    virtual ~NodeBehaviourInstance() = default;

    std::unique_ptr<EunoiaBehaviour> Clone() const override;

    void OnCreate() override;
    void Start() override;
    void Update(float deltaTime) override;
    void FixedUpdate(float fixedDeltaTime) override;
    void OnDestroy() override;

    std::string GetClassName() const override { return "NodeBehaviourInstance"; }
    std::string GetDisplayName() const override { return m_displayName; }
    std::string GetScriptAssetPath() const { return m_scriptAssetPath; }
    std::string GetAssetPath() const { return m_scriptAssetPath; }
    void SetScriptAssetPath(const std::string& path);
    std::string GetSourceCppPath() const override { return m_scriptAssetPath; }
    void SetSourceCppPath(const std::string& path) override { SetScriptAssetPath(path); }

    std::shared_ptr<NodeBehaviourGraph> GetGraph() const { return m_graph; }

    void RefreshPropertiesFromSource() override;
    void ReloadGraph(std::shared_ptr<NodeBehaviourGraph> newGraph = nullptr);
    void SyncVariablesFromGraph();
    void ResolveReferences(Scene& scene) override;

    // Variable Accessors
    std::unordered_map<std::string, NodeVariable>& GetInstanceVars() { return m_instanceVars; }
    const std::unordered_map<std::string, NodeVariable>& GetInstanceVars() const { return m_instanceVars; }

private:
    std::string m_scriptAssetPath;
    std::string m_displayName = "Node Behaviour";
    std::shared_ptr<NodeBehaviourGraph> m_graph;
    std::unordered_map<std::string, NodeVariable> m_instanceVars;

    void SetupExposedProperties();
};

} // namespace NodeBehaviour
} // namespace Eunoia
