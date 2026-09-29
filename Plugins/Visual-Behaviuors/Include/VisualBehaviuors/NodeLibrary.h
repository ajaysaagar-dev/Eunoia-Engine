#pragma once
// ============================================================================
// Visual-Behaviuors Plugin — Built-in Node Library Registration
// Registers all placeholder node types into NodeRegistry.
// Categories: Events, Flow, Debug, Math, Variables, Utility
// ============================================================================

#include "BehaviourGraph.h"

namespace VisualBehaviuors {

// ─── Helpers ─────────────────────────────────────────────────────────────────
inline PinDef ExecOut(const std::string& name = "►") {
    return { name, PinKind::Exec, PinDataType::Exec, PinDirection::Output };
}
inline PinDef ExecIn(const std::string& name = "►") {
    return { name, PinKind::Exec, PinDataType::Exec, PinDirection::Input };
}
inline PinDef DataOut(const std::string& name, PinDataType dt) {
    return { name, PinKind::Data, dt, PinDirection::Output };
}
inline PinDef DataIn(const std::string& name, PinDataType dt) {
    return { name, PinKind::Data, dt, PinDirection::Input };
}

// ─── Registration ─────────────────────────────────────────────────────────────
inline void RegisterBuiltinNodes() {
    NodeRegistry& reg = NodeRegistry::Get();

    // ── Events ──────────────────────────────────────────────────────────────
    reg.Register({
        "Event.BeginPlay", "Event BeginPlay", "Events",
        { ExecOut() }
    });
    reg.Register({
        "Event.Tick", "Event Tick", "Events",
        { ExecOut(), DataOut("DeltaTime", PinDataType::Float) }
    });
    reg.Register({
        "Event.Destroy", "Event Destroy", "Events",
        { ExecOut() }
    });

    // ── Flow ─────────────────────────────────────────────────────────────────
    reg.Register({
        "Flow.Branch", "Branch", "Flow",
        { ExecIn(), DataIn("Condition", PinDataType::Bool),
          ExecOut("True"), ExecOut("False") }
    });
    reg.Register({
        "Flow.Sequence", "Sequence", "Flow",
        { ExecIn(), ExecOut("Then 0"), ExecOut("Then 1"), ExecOut("Then 2") }
    });

    // ── Debug ────────────────────────────────────────────────────────────────
    reg.Register({
        "Debug.PrintString", "Print String", "Debug",
        { ExecIn(), DataIn("String", PinDataType::String), ExecOut() }
    });

    // ── Math ─────────────────────────────────────────────────────────────────
    reg.Register({
        "Math.AddFloat", "Add (Float)", "Math",
        { DataIn("A", PinDataType::Float), DataIn("B", PinDataType::Float),
          DataOut("Result", PinDataType::Float) }
    });
    reg.Register({
        "Math.MulFloat", "Multiply (Float)", "Math",
        { DataIn("A", PinDataType::Float), DataIn("B", PinDataType::Float),
          DataOut("Result", PinDataType::Float) }
    });

    // ── Variables: Get/Set are generated dynamically per variable name ───────
    // Template definitions so the node type IDs are stable and can be serialized.
    // Actual display names are overridden per-node using params["varName"].
    reg.Register({
        "Var.Get", "Get Variable", "Variables",
        { DataOut("Value", PinDataType::Float) }
    });
    reg.Register({
        "Var.Set", "Set Variable", "Variables",
        { ExecIn(), DataIn("Value", PinDataType::Float), ExecOut() }
    });

    // ── Utility: Comment box ─────────────────────────────────────────────────
    reg.Register({
        "Utility.Comment", "Comment", "Utility",
        {}  // no pins; dragging nodes inside handled in canvas UI
    });
}

} // namespace VisualBehaviuors
