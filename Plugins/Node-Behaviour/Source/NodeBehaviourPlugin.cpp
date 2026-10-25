#include <Node-Behaviour/NodeBehaviourInstance.h>
#include <EngineScene/BehaviourRegistry.h>
#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>
#include <iostream>

namespace Eunoia {
namespace NodeBehaviour {

class NodeBehaviourPlugin : public IPlugin {
public:
    NodeBehaviourPlugin() = default;
    ~NodeBehaviourPlugin() override = default;

    const char* GetName() const override { return "Node-Behaviour"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override {
    }

    void OnInit() override {
        BehaviourRegistry::Get().Register<NodeBehaviourInstance>(
            "NodeBehaviourInstance", "Node Behaviour", "Unreal Engine-style visual scripting behaviour"
        );
        std::cout << "[Node-Behaviour] Plugin initialized.\n";
    }

    void OnShutdown() override {
        std::cout << "[Node-Behaviour] Plugin shutdown.\n";
    }
};

static NodeBehaviourPlugin s_nodeBehaviourPlugin;

} // namespace NodeBehaviour
} // namespace Eunoia
