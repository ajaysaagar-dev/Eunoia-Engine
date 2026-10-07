#include <iostream>
#include <cassert>
#include "EngineScene/Scene.h"
#include "EngineScene/SceneSerializer.h"
#include "EngineScene/BehaviourRegistry.h"

void AddEngineLog(const std::string&, const std::string&, int) {}

class LightFlickerBehaviour : public EunoiaBehaviour {
public:
    GameObject* targetLight = nullptr;

    LightFlickerBehaviour() {
        m_className = "LightFlickerBehaviour";
        RegisterProperties();
    }

    void RegisterProperties() override {
        m_properties.clear();
        RegisterReference("Target Light", &targetLight, ObjectRefType::Light);
    }

    std::unique_ptr<EunoiaBehaviour> Clone() const override {
        auto clone = std::make_unique<LightFlickerBehaviour>();
        clone->CopyPropertiesFrom(*this);
        clone->targetLight = targetLight;
        return clone;
    }
};

int main() {
    std::cout << "[RUNNING] EngineScene Tests...\n";

    Scene scene;
    int parentId = scene.AddObject(PrimitiveType::Cube, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f)).id;
    int childId = scene.AddObject(PrimitiveType::Sphere, glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f)).id;

    scene.SetParent(childId, parentId);
    auto* parent = scene.FindObject(parentId);
    auto* child = scene.FindObject(childId);

    assert(parent != nullptr);
    assert(child != nullptr);
    assert(child->parentId == parentId);
    assert(!parent->childIds.empty());
    assert(parent->childIds[0] == childId);

    // World matrix test with parent scale
    parent->scale = glm::vec3(2.0f);
    glm::mat4 childWorld = scene.GetWorldMatrix(*child);
    // Child local pos (0, 2, 0) scaled by parent 2 -> world pos (0, 4, 0)
    assert(std::abs(childWorld[3][1] - 4.0f) < 1e-4f);

    // Test: Behaviour Object Reference & Selection Persistence across Play Mode
    int lightId = scene.AddObject(PrimitiveType::PointLight, glm::vec3(5.0f, 5.0f, 5.0f)).id;
    auto flicker = std::make_shared<LightFlickerBehaviour>();
    parent->AddBehaviour(flicker);
    scene.selectedId = parentId;

    // Assign reference to target light
    auto& props = flicker->GetProperties();
    for (auto& p : props) {
        if (p.name == "Target Light") {
            p.targetId = lightId;
            break;
        }
    }
    flicker->ResolveReferences(scene);
    assert(flicker->targetLight != nullptr);

    // Enter Play Mode
    scene.StartPlayMode();
    assert(scene.isPlayMode);

    // Exit Play Mode
    scene.StopPlayMode();
    assert(!scene.isPlayMode);

    // Verify selection is preserved (not deselected)
    assert(scene.selectedId == parentId);

    // Verify assigned object reference is preserved (does not disappear)
    auto* restoredParent = scene.FindObject(parentId);
    assert(restoredParent != nullptr);
    assert(!restoredParent->behaviours.empty());
    auto* restoredFlicker = dynamic_cast<LightFlickerBehaviour*>(restoredParent->behaviours[0].get());
    assert(restoredFlicker != nullptr);
    assert(restoredFlicker->targetLight != nullptr);
    bool foundTarget = false;
    for (auto& p : restoredFlicker->GetProperties()) {
        if (p.name == "Target Light") {
            assert(p.targetId == lightId);
            foundTarget = true;
            break;
        }
    }
    assert(foundTarget);

    // Test: Outliner and Content Browser Tree Collapse & Expand Persistence
    scene.outlinerNodeOpenState["##LevelRootNode"] = false;
    scene.outlinerNodeOpenState["Parent Actor"] = false;
    scene.outlinerNodeOpenState["Child Actor"] = true;
    scene.contentBrowserFolderOpenState["/Game"] = true;
    scene.contentBrowserFolderOpenState["/Game/Meshes"] = false;
    scene.contentBrowserFolderOpenState["/Game/Materials"] = true;

    std::string testPath = "test_tree_state.elevel";
    bool saveOk = SceneSerializer::SaveScene(scene, testPath);
    assert(saveOk);

    Scene loadedScene;
    bool loadOk = SceneSerializer::LoadScene(loadedScene, testPath);
    assert(loadOk);

    assert(loadedScene.outlinerNodeOpenState.count("##LevelRootNode") > 0);
    assert(loadedScene.outlinerNodeOpenState["##LevelRootNode"] == false);

    assert(loadedScene.outlinerNodeOpenState.count("Parent Actor") > 0);
    assert(loadedScene.outlinerNodeOpenState["Parent Actor"] == false);

    assert(loadedScene.outlinerNodeOpenState.count("Child Actor") > 0);
    assert(loadedScene.outlinerNodeOpenState["Child Actor"] == true);

    assert(loadedScene.contentBrowserFolderOpenState.count("/Game") > 0);
    assert(loadedScene.contentBrowserFolderOpenState["/Game"] == true);

    assert(loadedScene.contentBrowserFolderOpenState.count("/Game/Meshes") > 0);
    assert(loadedScene.contentBrowserFolderOpenState["/Game/Meshes"] == false);

    assert(loadedScene.contentBrowserFolderOpenState.count("/Game/Materials") > 0);
    assert(loadedScene.contentBrowserFolderOpenState["/Game/Materials"] == true);

    std::filesystem::remove(testPath);
    std::filesystem::remove("test_tree_state.bin");

    std::cout << "[PASSED] Outliner & Content Browser tree collapse/expand persistence verified!\n";
    std::cout << "[PASSED] All EngineScene Tests passed successfully!\n";
    return 0;
}
