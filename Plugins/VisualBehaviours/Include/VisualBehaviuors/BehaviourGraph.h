#pragma once
// ============================================================================
// Visual-Behaviuors Plugin — Graph Data Model
// Pure data structures. No ImGui or D3D12 dependency.
// Saved as JSON with extension .ebehaviour
// ============================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <functional>
#include <algorithm>
#include <memory>

namespace VisualBehaviuors {

// ─────────────────────────────────────────────────────────────────────────────
// Stable IDs
// ─────────────────────────────────────────────────────────────────────────────
using NodeId  = uint64_t;
using PinId   = uint64_t;
using LinkId  = uint64_t;

static uint64_t s_nextId = 1;
inline uint64_t GenId() { return s_nextId++; }

// ─────────────────────────────────────────────────────────────────────────────
// Pin kinds and data types
// ─────────────────────────────────────────────────────────────────────────────
enum class PinKind { Exec, Data };

enum class PinDataType {
    Exec,       // white triangle
    Bool,       // red
    Int,        // cyan
    Float,      // green
    String,     // magenta
    Vector3,    // yellow
    GameObject  // orange
};

enum class PinDirection { Input, Output };

struct Pin {
    PinId        id        = 0;
    NodeId       ownerNode = 0;
    std::string  name;
    PinKind      kind      = PinKind::Data;
    PinDataType  dataType  = PinDataType::Float;
    PinDirection direction = PinDirection::Input;
};

// ─────────────────────────────────────────────────────────────────────────────
// Node type registry
// ─────────────────────────────────────────────────────────────────────────────
struct PinDef {
    std::string  name;
    PinKind      kind;
    PinDataType  dataType;
    PinDirection direction;
};

struct NodeTypeDef {
    std::string           typeId;       // stable string key, e.g. "Event.BeginPlay"
    std::string           displayName;
    std::string           category;
    std::vector<PinDef>   pins;
};

class NodeRegistry {
public:
    static NodeRegistry& Get() {
        static NodeRegistry inst;
        return inst;
    }

    void Register(const NodeTypeDef& def) {
        m_defs[def.typeId] = def;
    }

    const NodeTypeDef* Find(const std::string& typeId) const {
        auto it = m_defs.find(typeId);
        return (it != m_defs.end()) ? &it->second : nullptr;
    }

    const std::unordered_map<std::string, NodeTypeDef>& All() const { return m_defs; }

private:
    std::unordered_map<std::string, NodeTypeDef> m_defs;
};

// ─────────────────────────────────────────────────────────────────────────────
// Variable
// ─────────────────────────────────────────────────────────────────────────────
enum class VarType { Bool, Int, Float, String, Vector3, GameObject };

inline const char* VarTypeToString(VarType t) {
    switch (t) {
        case VarType::Bool:       return "Bool";
        case VarType::Int:        return "Int";
        case VarType::Float:      return "Float";
        case VarType::String:     return "String";
        case VarType::Vector3:    return "Vector3";
        case VarType::GameObject: return "GameObject";
        default:                  return "Float";
    }
}

inline VarType VarTypeFromString(const std::string& s) {
    if (s == "Bool")       return VarType::Bool;
    if (s == "Int")        return VarType::Int;
    if (s == "String")     return VarType::String;
    if (s == "Vector3")    return VarType::Vector3;
    if (s == "GameObject") return VarType::GameObject;
    return VarType::Float;
}

inline PinDataType VarTypeToPinDataType(VarType t) {
    switch (t) {
        case VarType::Bool:       return PinDataType::Bool;
        case VarType::Int:        return PinDataType::Int;
        case VarType::Float:      return PinDataType::Float;
        case VarType::String:     return PinDataType::String;
        case VarType::Vector3:    return PinDataType::Vector3;
        case VarType::GameObject: return PinDataType::GameObject;
        default:                  return PinDataType::Float;
    }
}

struct Variable {
    uint64_t    id           = 0;
    std::string name;
    VarType     type         = VarType::Float;
    std::string defaultValue = "0";   // stored as string for simplicity
};

// ─────────────────────────────────────────────────────────────────────────────
// Node instance
// ─────────────────────────────────────────────────────────────────────────────
struct Node {
    NodeId      id       = 0;
    std::string typeId;          // references NodeTypeDef::typeId
    std::string comment;
    float       posX     = 0.0f;
    float       posY     = 0.0f;
    float       width    = 150.0f;
    float       height   = 80.0f;
    // params: per-node custom data (e.g. variable name for Get/Set)
    std::unordered_map<std::string, std::string> params;
    // runtime pins (rebuilt from NodeTypeDef on load)
    std::vector<Pin> pins;

    void RebuildPins(const NodeTypeDef& def) {
        pins.clear();
        uint32_t idx = 0;
        for (const auto& pd : def.pins) {
            Pin p;
            p.id        = (id > 0) ? ((id << 16) | (++idx)) : GenId();
            p.ownerNode = id;
            p.name      = pd.name;
            p.kind      = pd.kind;
            p.dataType  = pd.dataType;
            p.direction = pd.direction;
            pins.push_back(p);
        }
    }

    Pin* FindPin(PinId pid) {
        for (auto& p : pins) if (p.id == pid) return &p;
        return nullptr;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Link
// ─────────────────────────────────────────────────────────────────────────────
struct Link {
    LinkId linkId    = 0;
    NodeId fromNode  = 0;
    PinId  fromPin   = 0;
    NodeId toNode    = 0;
    PinId  toPin     = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Graph = full .ebehaviour asset data
// ─────────────────────────────────────────────────────────────────────────────
struct BehaviourGraph {
    int                    version   = 1;
    std::string            name      = "NewBehaviour";
    std::vector<Variable>  variables;
    std::vector<Node>      nodes;
    std::vector<Link>      links;
    float                  panX      = 0.0f;
    float                  panY      = 0.0f;
    float                  zoom      = 1.0f;

    // ─── Mutation helpers ────────────────────────────────────────────────────
    NodeId AddNode(const std::string& typeId, float x = 0.0f, float y = 0.0f) {
        const NodeTypeDef* def = NodeRegistry::Get().Find(typeId);
        if (!def) return 0;
        Node n;
        n.id    = GenId();
        n.typeId = typeId;
        n.posX  = x;
        n.posY  = y;
        n.RebuildPins(*def);
        nodes.push_back(std::move(n));
        return nodes.back().id;
    }

    bool RemoveNode(NodeId nid) {
        // remove all connected links first
        links.erase(std::remove_if(links.begin(), links.end(),
            [&](const Link& l){ return l.fromNode == nid || l.toNode == nid; }),
            links.end());
        auto it = std::find_if(nodes.begin(), nodes.end(), [&](const Node& n){ return n.id == nid; });
        if (it == nodes.end()) return false;
        nodes.erase(it);
        return true;
    }

    Node* FindNode(NodeId nid) {
        for (auto& n : nodes) if (n.id == nid) return &n;
        return nullptr;
    }

    Link* FindLink(LinkId lid) {
        for (auto& l : links) if (l.linkId == lid) return &l;
        return nullptr;
    }

    // ─── Link validation ─────────────────────────────────────────────────────
    enum class LinkResult { Ok, SameDirection, SelfLink, TypeMismatch, ExecMultiInput, DataMultiInput };

    LinkResult CanLink(NodeId fromNode, PinId fromPin, NodeId toNode, PinId toPin) const {
        if (fromNode == toNode) return LinkResult::SelfLink;
        const Node* fn = nullptr; const Node* tn = nullptr;
        for (const auto& n : nodes) {
            if (n.id == fromNode) fn = &n;
            if (n.id == toNode)   tn = &n;
        }
        if (!fn || !tn) return LinkResult::TypeMismatch;
        const Pin* fp = nullptr; const Pin* tp = nullptr;
        for (const auto& p : fn->pins) if (p.id == fromPin) fp = &p;
        for (const auto& p : tn->pins) if (p.id == toPin)   tp = &p;
        if (!fp || !tp) return LinkResult::TypeMismatch;
        if (fp->direction == tp->direction) return LinkResult::SameDirection;
        bool typeMatch = (fp->dataType == tp->dataType);
        // Convenience: allow any Data pin to connect to a String input pin (auto-stringification like in Blueprint)
        if (!typeMatch && fp->kind == PinKind::Data && tp->kind == PinKind::Data && tp->dataType == PinDataType::String) {
            typeMatch = true;
        }
        if (!typeMatch) return LinkResult::TypeMismatch;
        // Exec inputs accept only one link
        if (tp->kind == PinKind::Exec) {
            for (const auto& l : links) if (l.toPin == toPin) return LinkResult::ExecMultiInput;
        }
        // Data inputs accept only one link
        if (tp->kind == PinKind::Data && tp->direction == PinDirection::Input) {
            for (const auto& l : links) if (l.toPin == toPin) return LinkResult::DataMultiInput;
        }
        return LinkResult::Ok;
    }

    LinkId AddLink(NodeId fromNode, PinId fromPin, NodeId toNode, PinId toPin) {
        if (CanLink(fromNode, fromPin, toNode, toPin) != LinkResult::Ok) return 0;
        // Remove existing data-input link to allow reconnect
        links.erase(std::remove_if(links.begin(), links.end(),
            [&](const Link& l){ return l.toPin == toPin; }), links.end());
        Link lk;
        lk.linkId   = GenId();
        lk.fromNode = fromNode;
        lk.fromPin  = fromPin;
        lk.toNode   = toNode;
        lk.toPin    = toPin;
        links.push_back(lk);
        return lk.linkId;
    }

    bool RemoveLink(LinkId lid) {
        auto it = std::find_if(links.begin(), links.end(), [&](const Link& l){ return l.linkId == lid; });
        if (it == links.end()) return false;
        links.erase(it);
        return true;
    }

    // Remove all links on a given pin
    void BreakPinLinks(PinId pid) {
        links.erase(std::remove_if(links.begin(), links.end(),
            [&](const Link& l){ return l.fromPin == pid || l.toPin == pid; }), links.end());
    }

    // ─── Variables ───────────────────────────────────────────────────────────
    uint64_t AddVariable(const std::string& name, VarType type) {
        Variable v;
        v.id   = GenId();
        v.name = name;
        v.type = type;
        variables.push_back(v);
        return v.id;
    }

    bool RemoveVariable(uint64_t vid) {
        auto it = std::find_if(variables.begin(), variables.end(), [&](const Variable& v){ return v.id == vid; });
        if (it == variables.end()) return false;
        // Remove Get/Set nodes for this variable
        std::string vname = it->name;
        std::vector<NodeId> toRemove;
        for (const auto& n : nodes) {
            auto pit = n.params.find("varName");
            if (pit != n.params.end() && pit->second == vname) toRemove.push_back(n.id);
        }
        for (NodeId nid : toRemove) RemoveNode(nid);
        variables.erase(it);
        return true;
    }

    void RenameVariable(uint64_t vid, const std::string& newName) {
        auto it = std::find_if(variables.begin(), variables.end(), [&](const Variable& v){ return v.id == vid; });
        if (it == variables.end()) return;
        std::string oldName = it->name;
        it->name = newName;
        // Update Get/Set nodes that reference oldName
        for (auto& n : nodes) {
            auto pit = n.params.find("varName");
            if (pit != n.params.end() && pit->second == oldName) pit->second = newName;
        }
    }

    Variable* FindVariable(uint64_t vid) {
        for (auto& v : variables) if (v.id == vid) return &v;
        return nullptr;
    }

    const Variable* FindVariableByName(const std::string& name) const {
        for (const auto& v : variables) if (v.name == name) return &v;
        return nullptr;
    }

    Variable* FindVariableByName(const std::string& name) {
        for (auto& v : variables) if (v.name == name) return &v;
        return nullptr;
    }

    bool IsVariableNameUnique(const std::string& name, uint64_t excludeId = 0) const {
        for (const auto& v : variables)
            if (v.name == name && v.id != excludeId) return false;
        return true;
    }
};

} // namespace VisualBehaviuors
