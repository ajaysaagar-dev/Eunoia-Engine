#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include <imgui.h>

namespace Eunoia {
namespace NodeBehaviour {

enum class NodePinKind {
    Input,
    Output
};

enum class NodePinType {
    Exec,
    Float,
    Int,
    Bool,
    String,
    Vector3,
    GameObject
};

inline const char* NodePinTypeToString(NodePinType type) {
    switch (type) {
        case NodePinType::Exec:       return "Exec";
        case NodePinType::Float:      return "Float";
        case NodePinType::Int:        return "Integer";
        case NodePinType::Bool:       return "Boolean";
        case NodePinType::String:     return "String";
        case NodePinType::Vector3:    return "Vector3";
        case NodePinType::GameObject: return "GameObject";
        default:                      return "Unknown";
    }
}

inline ImU32 GetPinColor(NodePinType type) {
    switch (type) {
        case NodePinType::Exec:       return IM_COL32(255, 255, 255, 255); // White flow
        case NodePinType::Float:      return IM_COL32(154, 239, 63, 255);  // Lime Green
        case NodePinType::Int:        return IM_COL32(68, 201, 156, 255);  // Cyan / Teal
        case NodePinType::Bool:       return IM_COL32(234, 45, 45, 255);   // Crimson Red
        case NodePinType::String:     return IM_COL32(255, 99, 177, 255);  // Magenta Pink
        case NodePinType::Vector3:    return IM_COL32(245, 197, 44, 255);  // Gold Yellow
        case NodePinType::GameObject: return IM_COL32(0, 168, 240, 255);   // Sky Blue
        default:                      return IM_COL32(200, 200, 200, 255);
    }
}

struct NodePin {
    uint32_t id = 0;
    uint32_t nodeId = 0;
    std::string name;
    NodePinKind kind = NodePinKind::Input;
    NodePinType type = NodePinType::Exec;

    // Inline editable default values when no link is connected
    float floatValue = 0.0f;
    int intValue = 0;
    bool boolValue = false;
    std::string stringValue = "";
    glm::vec3 vectorValue{0.0f, 0.0f, 0.0f};

    NodePin() = default;
    NodePin(uint32_t inId, uint32_t inNodeId, const std::string& inName, NodePinKind inKind, NodePinType inType)
        : id(inId), nodeId(inNodeId), name(inName), kind(inKind), type(inType) {}
};

struct NodeLink {
    uint32_t id = 0;
    uint32_t startPinId = 0;
    uint32_t endPinId = 0;
    ImU32 color = IM_COL32(255, 255, 255, 255);

    NodeLink() = default;
    NodeLink(uint32_t inId, uint32_t inStart, uint32_t inEnd, ImU32 inColor = IM_COL32(255, 255, 255, 255))
        : id(inId), startPinId(inStart), endPinId(inEnd), color(inColor) {}
};

enum class NodeCategory {
    Event,
    Function,
    VariableGet,
    VariableSet,
    Math
};

struct NodeItem {
    uint32_t id = 0;
    std::string name;
    std::string subtitle;
    NodeCategory category = NodeCategory::Event;
    ImVec4 headerColor = ImVec4(0.8f, 0.15f, 0.15f, 1.0f); // Default Unreal red for events

    std::vector<NodePin> inputs;
    std::vector<NodePin> outputs;

    ImVec2 position{0.0f, 0.0f};
    bool hasInitializedPos = false;

    // Custom properties / metadata
    std::string variableName; // For VariableGet / VariableSet
    std::string functionName; // For Function nodes (e.g. "Print")
};

struct NodeVariable {
    uint32_t id = 0;
    std::string name = "NewVar";
    NodePinType type = NodePinType::Float;

    // Default value storage
    float floatValue = 0.0f;
    int intValue = 0;
    bool boolValue = false;
    std::string stringValue = "";
    glm::vec3 vectorValue{0.0f, 0.0f, 0.0f};

    std::string tooltip = "";
    bool isPublic = true; // Instance editable on Actor in Details panel
};

} // namespace NodeBehaviour
} // namespace Eunoia
