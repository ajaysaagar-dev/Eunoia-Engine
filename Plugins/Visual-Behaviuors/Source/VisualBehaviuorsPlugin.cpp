// ============================================================================
// Eunoia Engine — Visual-Behaviuors Plugin Implementation
// Node-graph visual scripting system for .ebehaviour assets.
//
// Uses RegisterReference API without objectRefType per dev.md:
//   RegisterReference("Camera Boom", &Camera, "Look");
// ============================================================================

#include "VisualBehaviuors/IVisualBehaviuorService.h"
#include "VisualBehaviuors/BehaviourGraph.h"
#include "VisualBehaviuors/BehaviourSerializer.h"
#include "VisualBehaviuors/NodeLibrary.h"
#include "VisualBehaviuors/BehaviourEditorWindow.h"

#include <filesystem>
#include <fstream>
#include <iostream>

// Plugin API (uses engine plugin loader headers)
#include <EunoiaPluginCore/IPlugin.h>
#include <EunoiaPluginCore/ServiceRegistry.h>

namespace VisualBehaviuors {

// ─────────────────────────────────────────────────────────────────────────────
// Service implementation
// ─────────────────────────────────────────────────────────────────────────────
class VisualBehaviuorServiceImpl : public IVisualBehaviuorService {
public:
    VisualBehaviuorServiceImpl() {
        // Register all built-in node types once
        RegisterBuiltinNodes();
    }

    bool CreateAsset(const std::string& diskPath, const std::string& name) override {
        // Create a minimal valid .ebehaviour graph file
        BehaviourGraph g;
        g.name = name;
        // Seed with BeginPlay and Tick event stubs for convenience
        g.AddNode("Event.BeginPlay", 50.0f, 100.0f);
        g.AddNode("Event.Tick",      50.0f, 220.0f);
        if (SaveGraph(g, diskPath)) {
            std::cout << "[Visual-Behaviuors] Created: " << diskPath << "\n";
            return true;
        }
        std::cerr << "[Visual-Behaviuors] Failed to create: " << diskPath << "\n";
        return false;
    }

    void OpenEditor(const std::string& diskPath) override {
        BehaviourEditorManager::Get().Open(diskPath);
    }

    void CloseEditor(const std::string& diskPath) override {
        // BehaviourEditorManager handles auto-save on close via isOpen flag
    }

    bool IsEditorOpen(const std::string& diskPath) const override {
        return BehaviourEditorManager::Get().IsOpen(diskPath);
    }

    void RenderEditors() override {
        BehaviourEditorManager::Get().RenderAll();
    }
};

} // namespace VisualBehaviuors

// ─────────────────────────────────────────────────────────────────────────────
// Plugin class
// ─────────────────────────────────────────────────────────────────────────────
class VisualBehaviuorsPlugin : public IPlugin {
public:
    VisualBehaviuorsPlugin() = default;
    ~VisualBehaviuorsPlugin() override = default;

    const char* GetName() const override { return "Visual-Behaviuors"; }
    bool SupportsHotReload() const override { return false; }

    void OnRegister(ServiceRegistry& registry) override {
        std::cout << "[Visual-Behaviuors] Registering IVisualBehaviuorService...\n";
        registry.Register<VisualBehaviuors::IVisualBehaviuorService>(
            "IVisualBehaviuorService", &m_service);
    }

    void OnInit() override {
        std::cout << "[Visual-Behaviuors] Plugin initialized.\n";
        std::cout << "[Visual-Behaviuors] Node types registered: "
                  << VisualBehaviuors::NodeRegistry::Get().All().size() << "\n";
    }

    void OnShutdown() override {
        std::cout << "[Visual-Behaviuors] Plugin shutdown.\n";
    }

private:
    VisualBehaviuors::VisualBehaviuorServiceImpl m_service;
};

