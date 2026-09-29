#pragma once
// ============================================================================
// Visual-Behaviuors Plugin — Public Service Interface
// Provides Create/Open/Close operations for .ebehaviour assets.
// ============================================================================

#include <string>

namespace VisualBehaviuors {

class IVisualBehaviuorService {
public:
    virtual ~IVisualBehaviuorService() = default;

    // Create a new .ebehaviour file at the given disk path
    virtual bool CreateAsset(const std::string& diskPath, const std::string& name) = 0;

    // Open the node-graph editor for a .ebehaviour file
    virtual void OpenEditor(const std::string& diskPath) = 0;

    // Close the editor for a file (save if dirty)
    virtual void CloseEditor(const std::string& diskPath) = 0;

    // Returns true if editor is currently open for this file
    virtual bool IsEditorOpen(const std::string& diskPath) const = 0;

    // Must be called each ImGui frame to render all open editors
    virtual void RenderEditors() = 0;
};

} // namespace VisualBehaviuors
