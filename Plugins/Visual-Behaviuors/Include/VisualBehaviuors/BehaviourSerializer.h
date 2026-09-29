#pragma once
// ============================================================================
// Visual-Behaviuors Plugin — JSON Serialization
// save/load BehaviourGraph to/from .ebehaviour files
// Uses nlohmann/json (single-header, already in deps/json)
// ============================================================================

#include "BehaviourGraph.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>

namespace VisualBehaviuors {

// ─── Pin data type helpers ───────────────────────────────────────────────────
inline const char* PinDataTypeToStr(PinDataType t) {
    switch (t) {
        case PinDataType::Exec:    return "Exec";
        case PinDataType::Bool:    return "Bool";
        case PinDataType::Int:     return "Int";
        case PinDataType::Float:   return "Float";
        case PinDataType::String:  return "String";
        case PinDataType::Vector3: return "Vector3";
        default:                   return "Float";
    }
}

inline PinDataType PinDataTypeFromStr(const std::string& s) {
    if (s == "Exec")    return PinDataType::Exec;
    if (s == "Bool")    return PinDataType::Bool;
    if (s == "Int")     return PinDataType::Int;
    if (s == "String")  return PinDataType::String;
    if (s == "Vector3") return PinDataType::Vector3;
    return PinDataType::Float;
}

// ─── Save ────────────────────────────────────────────────────────────────────
inline bool SaveGraph(const BehaviourGraph& g, const std::string& filePath) {
    using json = nlohmann::json;
    json j;
    j["version"] = g.version;
    j["name"]    = g.name;
    j["view"]["pan"]["x"] = g.panX;
    j["view"]["pan"]["y"] = g.panY;
    j["view"]["zoom"]     = g.zoom;

    // Variables
    json vars = json::array();
    for (const auto& v : g.variables) {
        json jv;
        jv["id"]           = v.id;
        jv["name"]         = v.name;
        jv["type"]         = VarTypeToString(v.type);
        jv["defaultValue"] = v.defaultValue;
        vars.push_back(jv);
    }
    j["variables"] = vars;

    // Nodes
    json nodes = json::array();
    for (const auto& n : g.nodes) {
        json jn;
        jn["id"]      = n.id;
        jn["type"]    = n.typeId;
        jn["comment"] = n.comment;
        jn["pos"]["x"] = n.posX;
        jn["pos"]["y"] = n.posY;
        jn["size"]["w"] = n.width;
        jn["size"]["h"] = n.height;
        json params = json::object();
        for (const auto& kv : n.params) params[kv.first] = kv.second;
        jn["params"] = params;
        nodes.push_back(jn);
    }
    j["nodes"] = nodes;

    // Links
    json links = json::array();
    for (const auto& l : g.links) {
        json jl;
        jl["id"]       = l.linkId;
        jl["fromNode"] = l.fromNode;
        jl["fromPin"]  = l.fromPin;
        jl["toNode"]   = l.toNode;
        jl["toPin"]    = l.toPin;
        links.push_back(jl);
    }
    j["links"] = links;

    std::ofstream f(filePath);
    if (!f.is_open()) {
        std::cerr << "[Visual-Behaviuors] Failed to open for writing: " << filePath << "\n";
        return false;
    }
    f << j.dump(2);
    return true;
}

// ─── Load ────────────────────────────────────────────────────────────────────
inline bool LoadGraph(BehaviourGraph& g, const std::string& filePath) {
    using json = nlohmann::json;
    std::ifstream f(filePath);
    if (!f.is_open()) {
        std::cerr << "[Visual-Behaviuors] Cannot open: " << filePath << "\n";
        return false;
    }
    json j;
    try { f >> j; }
    catch (const std::exception& e) {
        std::cerr << "[Visual-Behaviuors] JSON parse error in " << filePath << ": " << e.what() << "\n";
        return false;
    }

    g = BehaviourGraph{};
    g.version = j.value("version", 1);
    g.name    = j.value("name", "NewBehaviour");

    if (j.contains("view")) {
        if (j["view"].contains("pan")) {
            g.panX = j["view"]["pan"].value("x", 0.0f);
            g.panY = j["view"]["pan"].value("y", 0.0f);
        }
        g.zoom = j["view"].value("zoom", 1.0f);
    }

    // Variables
    if (j.contains("variables") && j["variables"].is_array()) {
        for (const auto& jv : j["variables"]) {
            Variable v;
            v.id           = jv.value("id", GenId());
            v.name         = jv.value("name", "Var");
            v.type         = VarTypeFromString(jv.value("type", "Float"));
            v.defaultValue = jv.value("defaultValue", "0");
            g.variables.push_back(v);
        }
    }

    // Nodes — skip unknown types with a warning, don't crash
    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& jn : j["nodes"]) {
            std::string typeId = jn.value("type", "");
            const NodeTypeDef* def = NodeRegistry::Get().Find(typeId);
            if (!def) {
                std::cerr << "[Visual-Behaviuors] WARNING: Unknown node type '" << typeId
                          << "' in " << filePath << " — skipped.\n";
                continue;
            }
            Node n;
            n.id      = jn.value("id", GenId());
            n.typeId  = typeId;
            n.comment = jn.value("comment", "");
            if (jn.contains("pos")) {
                n.posX = jn["pos"].value("x", 0.0f);
                n.posY = jn["pos"].value("y", 0.0f);
            }
            if (jn.contains("size")) {
                n.width  = jn["size"].value("w", 150.0f);
                n.height = jn["size"].value("h", 80.0f);
            }
            if (jn.contains("params") && jn["params"].is_object()) {
                for (auto it = jn["params"].begin(); it != jn["params"].end(); ++it)
                    n.params[it.key()] = it.value().get<std::string>();
            }
            n.RebuildPins(*def);
            g.nodes.push_back(std::move(n));
        }
    }

    // Links — skip if nodes/pins no longer exist
    if (j.contains("links") && j["links"].is_array()) {
        for (const auto& jl : j["links"]) {
            Link lk;
            lk.linkId   = jl.value("id", GenId());
            lk.fromNode = jl.value("fromNode", (NodeId)0);
            lk.fromPin  = jl.value("fromPin",  (PinId)0);
            lk.toNode   = jl.value("toNode",   (NodeId)0);
            lk.toPin    = jl.value("toPin",    (PinId)0);
            // Validate that nodes exist before adding
            bool fnOk = false, tnOk = false;
            for (const auto& n : g.nodes) {
                if (n.id == lk.fromNode) fnOk = true;
                if (n.id == lk.toNode)   tnOk = true;
            }
            if (fnOk && tnOk) g.links.push_back(lk);
            else std::cerr << "[Visual-Behaviuors] WARNING: Skipped orphan link " << lk.linkId << "\n";
        }
    }

    return true;
}

} // namespace VisualBehaviuors
