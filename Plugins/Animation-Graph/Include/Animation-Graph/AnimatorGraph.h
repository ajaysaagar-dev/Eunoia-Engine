#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace Eunoia {

enum class AnimatorParamType {
    Boolean,
    Float,
    Int,
    String
};

inline const char* AnimatorParamTypeToString(AnimatorParamType type) {
    switch (type) {
        case AnimatorParamType::Boolean: return "Boolean";
        case AnimatorParamType::Float:   return "Float";
        case AnimatorParamType::Int:     return "Int";
        case AnimatorParamType::String:  return "String";
        default:                         return "Unknown";
    }
}

inline AnimatorParamType StringToAnimatorParamType(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "bool" || s == "boolean") return AnimatorParamType::Boolean;
    if (s == "float")                  return AnimatorParamType::Float;
    if (s == "int" || s == "integer")  return AnimatorParamType::Int;
    if (s == "string" || s == "str")   return AnimatorParamType::String;
    return AnimatorParamType::Boolean;
}

struct AnimatorParam {
    std::string name = "Param";
    AnimatorParamType type = AnimatorParamType::Boolean;
    bool boolValue = false;
    float floatValue = 0.0f;
    int intValue = 0;
    std::string stringValue = "";

    AnimatorParam() = default;
    AnimatorParam(const std::string& n, AnimatorParamType t) : name(n), type(t) {}
};

enum class ConditionOp {
    Greater,        // >
    Less,           // <
    GreaterEqual,   // >=
    LessEqual,      // <=
    Equal,          // ==
    NotEqual,       // !=
    IsTrue,         // Boolean == true
    IsFalse         // Boolean == false
};

inline const char* ConditionOpToSerializeString(ConditionOp op) {
    switch (op) {
        case ConditionOp::Greater:      return "Greater";
        case ConditionOp::Less:         return "Less";
        case ConditionOp::GreaterEqual: return "GreaterEqual";
        case ConditionOp::LessEqual:    return "LessEqual";
        case ConditionOp::Equal:        return "Equal";
        case ConditionOp::NotEqual:     return "NotEqual";
        case ConditionOp::IsTrue:       return "True";
        case ConditionOp::IsFalse:      return "False";
        default:                        return "Equal";
    }
}

inline const char* ConditionOpToString(ConditionOp op) {
    switch (op) {
        case ConditionOp::Greater:      return "> (Greater)";
        case ConditionOp::Less:         return "< (Less)";
        case ConditionOp::GreaterEqual: return ">= (Greater/Equal)";
        case ConditionOp::LessEqual:    return "<= (Less/Equal)";
        case ConditionOp::Equal:        return "== (Equal)";
        case ConditionOp::NotEqual:     return "!= (Not Equal)";
        case ConditionOp::IsTrue:       return "True";
        case ConditionOp::IsFalse:      return "False";
        default:                        return "==";
    }
}

inline ConditionOp StringToConditionOp(const std::string& s) {
    if (s == "GreaterEqual" || s == ">=" || s.find(">=") != std::string::npos || s.find("Greater/Equal") != std::string::npos) return ConditionOp::GreaterEqual;
    if (s == "LessEqual" || s == "<=" || s.find("<=") != std::string::npos || s.find("Less/Equal") != std::string::npos)       return ConditionOp::LessEqual;
    if (s == "Greater" || s == ">" || (s.find("Greater") != std::string::npos && s.find("Equal") == std::string::npos)) return ConditionOp::Greater;
    if (s == "Less" || s == "<" || (s.find("Less") != std::string::npos && s.find("Equal") == std::string::npos))       return ConditionOp::Less;
    if (s == "Equal" || s == "==" || s.find("==") != std::string::npos) return ConditionOp::Equal;
    if (s == "NotEqual" || s == "!=" || s.find("!=") != std::string::npos || s.find("Not Equal") != std::string::npos) return ConditionOp::NotEqual;
    if (s == "IsTrue" || s == "True" || s == "true") return ConditionOp::IsTrue;
    if (s == "IsFalse" || s == "False" || s == "false") return ConditionOp::IsFalse;
    return ConditionOp::Equal;
}

struct TransitionCondition {
    std::string paramName = "";
    ConditionOp op = ConditionOp::Equal;
    bool targetBool = true;
    float targetFloat = 0.0f;
    int targetInt = 0;
    std::string targetString = "";

    bool Evaluate(const std::unordered_map<std::string, AnimatorParam>& params) const {
        auto it = params.find(paramName);
        if (it == params.end()) return false;

        const auto& p = it->second;
        switch (p.type) {
            case AnimatorParamType::Boolean: {
                if (op == ConditionOp::IsTrue) return p.boolValue == true;
                if (op == ConditionOp::IsFalse) return p.boolValue == false;
                if (op == ConditionOp::Equal) return p.boolValue == targetBool;
                if (op == ConditionOp::NotEqual) return p.boolValue != targetBool;
                return p.boolValue == targetBool;
            }
            case AnimatorParamType::Float: {
                if (op == ConditionOp::Greater)      return p.floatValue > targetFloat;
                if (op == ConditionOp::Less)         return p.floatValue < targetFloat;
                if (op == ConditionOp::GreaterEqual) return p.floatValue >= targetFloat;
                if (op == ConditionOp::LessEqual)    return p.floatValue <= targetFloat;
                if (op == ConditionOp::Equal)        return std::abs(p.floatValue - targetFloat) < 0.001f;
                if (op == ConditionOp::NotEqual)     return std::abs(p.floatValue - targetFloat) >= 0.001f;
                return false;
            }
            case AnimatorParamType::Int: {
                if (op == ConditionOp::Greater)      return p.intValue > targetInt;
                if (op == ConditionOp::Less)         return p.intValue < targetInt;
                if (op == ConditionOp::GreaterEqual) return p.intValue >= targetInt;
                if (op == ConditionOp::LessEqual)    return p.intValue <= targetInt;
                if (op == ConditionOp::Equal)        return p.intValue == targetInt;
                if (op == ConditionOp::NotEqual)     return p.intValue != targetInt;
                return false;
            }
            case AnimatorParamType::String: {
                if (op == ConditionOp::Equal)    return p.stringValue == targetString;
                if (op == ConditionOp::NotEqual) return p.stringValue != targetString;
                return false;
            }
        }
        return false;
    }
};

struct StateTransition {
    uint32_t id = 0;
    uint32_t sourceStateId = 0;
    uint32_t targetStateId = 0;
    float smoothingTime = 0.20f; // Crossfade duration in seconds
    std::vector<TransitionCondition> conditions;

    bool CanTransition(const std::unordered_map<std::string, AnimatorParam>& params, bool isSourceEntry = false) const {
        if (conditions.empty()) {
            // Transitions from Entry / Begin node transition immediately to target state.
            // Transitions between normal animation states do not auto-transition without conditions,
            // preventing the state machine from immediately bypassing the state before conditions are configured.
            return isSourceEntry;
        }
        for (const auto& cond : conditions) {
            if (!cond.Evaluate(params)) return false;
        }
        return true;
    }
};

struct AnimationStateNode {
    uint32_t id = 0;
    std::string name = "State";
    bool isEntry = false;
    std::string clipPath = "";
    float speed = 1.0f;
    bool loop = true;
    glm::vec2 position{100.0f, 100.0f};

    AnimationStateNode() = default;
    AnimationStateNode(uint32_t nid, const std::string& n, bool entry = false)
        : id(nid), name(n), isEntry(entry) {}
};

class AnimatorGraph {
public:
    std::string name = "NewAnimator";
    std::string assetPath = "";
    std::string skeletonMeshPath = "";
    uint32_t entryStateId = 0;
    uint32_t nextId = 1;

    std::vector<AnimatorParam> parameters;
    std::vector<AnimationStateNode> states;
    std::vector<StateTransition> transitions;

    AnimatorGraph() {
        ResetToDefault();
    }

    void ResetToDefault() {
        parameters.clear();
        states.clear();
        transitions.clear();
        skeletonMeshPath = "";
        nextId = 1;

        // Default "Begin" node as required by the user specifications
        AnimationStateNode beginNode;
        beginNode.id = nextId++;
        beginNode.name = "Begin";
        beginNode.isEntry = true;
        beginNode.clipPath = "";
        beginNode.speed = 1.0f;
        beginNode.loop = true;
        beginNode.position = glm::vec2(200.0f, 250.0f);
        states.push_back(beginNode);
        entryStateId = beginNode.id;
    }

    AnimationStateNode* FindState(uint32_t id) {
        for (auto& s : states) {
            if (s.id == id) return &s;
        }
        return nullptr;
    }

    const AnimationStateNode* FindState(uint32_t id) const {
        for (const auto& s : states) {
            if (s.id == id) return &s;
        }
        return nullptr;
    }

    AnimationStateNode* FindEntryState() {
        for (auto& s : states) {
            if (s.isEntry) return &s;
        }
        if (!states.empty()) return &states.front();
        return nullptr;
    }

    StateTransition* FindTransition(uint32_t id) {
        for (auto& t : transitions) {
            if (t.id == id) return &t;
        }
        return nullptr;
    }

    const StateTransition* FindTransition(uint32_t id) const {
        for (const auto& t : transitions) {
            if (t.id == id) return &t;
        }
        return nullptr;
    }

    StateTransition* FindTransitionBetween(uint32_t srcId, uint32_t dstId) {
        for (auto& t : transitions) {
            if (t.sourceStateId == srcId && t.targetStateId == dstId) return &t;
        }
        return nullptr;
    }

    AnimatorParam* FindParameter(const std::string& paramName) {
        for (auto& p : parameters) {
            if (p.name == paramName) return &p;
        }
        return nullptr;
    }

    void AddParameter(const std::string& paramName, AnimatorParamType type) {
        if (FindParameter(paramName)) return;
        AnimatorParam p(paramName, type);
        parameters.push_back(p);
    }

    void RemoveParameter(const std::string& paramName) {
        parameters.erase(std::remove_if(parameters.begin(), parameters.end(),
            [&](const AnimatorParam& p) { return p.name == paramName; }), parameters.end());

        // Clean up conditions referencing this parameter
        for (auto& t : transitions) {
            t.conditions.erase(std::remove_if(t.conditions.begin(), t.conditions.end(),
                [&](const TransitionCondition& c) { return c.paramName == paramName; }), t.conditions.end());
        }
    }

    AnimationStateNode& AddState(const std::string& stateName, const glm::vec2& pos) {
        AnimationStateNode node;
        node.id = nextId++;
        node.name = stateName;
        node.isEntry = states.empty();
        node.position = pos;
        states.push_back(node);
        if (node.isEntry) entryStateId = node.id;
        return states.back();
    }

    void RemoveState(uint32_t id) {
        states.erase(std::remove_if(states.begin(), states.end(),
            [&](const AnimationStateNode& s) { return s.id == id; }), states.end());

        // Remove transitions referencing this state
        transitions.erase(std::remove_if(transitions.begin(), transitions.end(),
            [&](const StateTransition& t) { return t.sourceStateId == id || t.targetStateId == id; }), transitions.end());

        if (entryStateId == id) {
            if (!states.empty()) {
                states.front().isEntry = true;
                entryStateId = states.front().id;
            } else {
                entryStateId = 0;
            }
        }
    }

    StateTransition& AddTransition(uint32_t srcId, uint32_t dstId, float smoothing = 0.20f) {
        StateTransition* existing = FindTransitionBetween(srcId, dstId);
        if (existing) return *existing;

        StateTransition t;
        t.id = nextId++;
        t.sourceStateId = srcId;
        t.targetStateId = dstId;
        t.smoothingTime = smoothing;
        transitions.push_back(t);
        return transitions.back();
    }

    void RemoveTransition(uint32_t id) {
        transitions.erase(std::remove_if(transitions.begin(), transitions.end(),
            [&](const StateTransition& t) { return t.id == id; }), transitions.end());
    }

    bool SaveToFile(const std::string& filePath) {
        std::ofstream out(filePath);
        if (!out.is_open()) return false;

        out << "{\n";
        out << "  \"name\": \"" << name << "\",\n";
        out << "  \"skeletonMesh\": \"" << skeletonMeshPath << "\",\n";
        out << "  \"entryStateId\": " << entryStateId << ",\n";
        out << "  \"nextId\": " << nextId << ",\n";

        // Parameters
        out << "  \"parameters\": [\n";
        for (size_t i = 0; i < parameters.size(); ++i) {
            const auto& p = parameters[i];
            out << "    {\n";
            out << "      \"name\": \"" << p.name << "\",\n";
            out << "      \"type\": \"" << AnimatorParamTypeToString(p.type) << "\",\n";
            out << "      \"boolValue\": " << (p.boolValue ? "true" : "false") << ",\n";
            out << "      \"floatValue\": " << p.floatValue << ",\n";
            out << "      \"intValue\": " << p.intValue << ",\n";
            out << "      \"stringValue\": \"" << p.stringValue << "\"\n";
            out << "    }" << (i + 1 < parameters.size() ? "," : "") << "\n";
        }
        out << "  ],\n";

        // States
        out << "  \"states\": [\n";
        for (size_t i = 0; i < states.size(); ++i) {
            const auto& s = states[i];
            out << "    {\n";
            out << "      \"id\": " << s.id << ",\n";
            out << "      \"name\": \"" << s.name << "\",\n";
            out << "      \"isEntry\": " << (s.isEntry ? "true" : "false") << ",\n";
            out << "      \"clipPath\": \"" << s.clipPath << "\",\n";
            out << "      \"speed\": " << s.speed << ",\n";
            out << "      \"loop\": " << (s.loop ? "true" : "false") << ",\n";
            out << "      \"posX\": " << s.position.x << ",\n";
            out << "      \"posY\": " << s.position.y << "\n";
            out << "    }" << (i + 1 < states.size() ? "," : "") << "\n";
        }
        out << "  ],\n";

        // Transitions
        out << "  \"transitions\": [\n";
        for (size_t i = 0; i < transitions.size(); ++i) {
            const auto& t = transitions[i];
            out << "    {\n";
            out << "      \"id\": " << t.id << ",\n";
            out << "      \"sourceId\": " << t.sourceStateId << ",\n";
            out << "      \"targetId\": " << t.targetStateId << ",\n";
            out << "      \"smoothingTime\": " << t.smoothingTime << ",\n";
            out << "      \"conditions\": [\n";
            for (size_t j = 0; j < t.conditions.size(); ++j) {
                const auto& c = t.conditions[j];
                out << "        {\n";
                out << "          \"paramName\": \"" << c.paramName << "\",\n";
                out << "          \"op\": \"" << ConditionOpToSerializeString(c.op) << "\",\n";
                out << "          \"targetBool\": " << (c.targetBool ? "true" : "false") << ",\n";
                out << "          \"targetFloat\": " << c.targetFloat << ",\n";
                out << "          \"targetInt\": " << c.targetInt << ",\n";
                out << "          \"targetString\": \"" << c.targetString << "\"\n";
                out << "        }" << (j + 1 < t.conditions.size() ? "," : "") << "\n";
            }
            out << "      ]\n";
            out << "    }" << (i + 1 < transitions.size() ? "," : "") << "\n";
        }
        out << "  ]\n";
        out << "}\n";

        assetPath = filePath;
        return true;
    }

    bool LoadFromFile(const std::string& filePath) {
        std::ifstream in(filePath);
        if (!in.is_open()) return false;

        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();

        ResetToDefault();
        states.clear(); // Will load from file
        assetPath = filePath;

        auto extractString = [](const std::string& json, const std::string& key) -> std::string {
            std::string search = "\"" + key + "\":";
            size_t pos = json.find(search);
            if (pos == std::string::npos) return "";
            size_t start = json.find("\"", pos + search.length());
            if (start == std::string::npos) return "";
            size_t end = json.find("\"", start + 1);
            if (end == std::string::npos) return "";
            return json.substr(start + 1, end - start - 1);
        };

        auto extractNumber = [](const std::string& json, const std::string& key, double defaultVal = 0.0) -> double {
            std::string search = "\"" + key + "\":";
            size_t pos = json.find(search);
            if (pos == std::string::npos) return defaultVal;
            size_t start = pos + search.length();
            while (start < json.length() && (json[start] == ' ' || json[start] == '\t' || json[start] == '\n')) start++;
            size_t end = start;
            while (end < json.length() && (isdigit(json[end]) || json[end] == '.' || json[end] == '-' || json[end] == '+')) end++;
            if (start == end) return defaultVal;
            try { return std::stod(json.substr(start, end - start)); } catch (...) { return defaultVal; }
        };

        auto extractBool = [](const std::string& json, const std::string& key, bool defaultVal = false) -> bool {
            std::string search = "\"" + key + "\":";
            size_t pos = json.find(search);
            if (pos == std::string::npos) return defaultVal;
            size_t truePos = json.find("true", pos + search.length());
            size_t falsePos = json.find("false", pos + search.length());
            if (truePos != std::string::npos && (falsePos == std::string::npos || truePos < falsePos)) {
                return (truePos - (pos + search.length()) < 10);
            }
            return false;
        };

        name = extractString(content, "name");
        skeletonMeshPath = extractString(content, "skeletonMesh");
        entryStateId = (uint32_t)extractNumber(content, "entryStateId", 1);
        nextId = (uint32_t)extractNumber(content, "nextId", 1);

        // Parse Parameters
        size_t paramsPos = content.find("\"parameters\":");
        if (paramsPos != std::string::npos) {
            size_t arrStart = content.find('[', paramsPos);
            size_t arrEnd = content.find(']', arrStart);
            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string arrContent = content.substr(arrStart, arrEnd - arrStart);
                size_t objStart = 0;
                while ((objStart = arrContent.find('{', objStart)) != std::string::npos) {
                    size_t objEnd = arrContent.find('}', objStart);
                    if (objEnd == std::string::npos) break;
                    std::string objStr = arrContent.substr(objStart, objEnd - objStart + 1);

                    AnimatorParam p;
                    p.name = extractString(objStr, "name");
                    p.type = StringToAnimatorParamType(extractString(objStr, "type"));
                    p.boolValue = extractBool(objStr, "boolValue", false);
                    p.floatValue = (float)extractNumber(objStr, "floatValue", 0.0);
                    p.intValue = (int)extractNumber(objStr, "intValue", 0);
                    p.stringValue = extractString(objStr, "stringValue");
                    if (!p.name.empty()) parameters.push_back(p);
                    objStart = objEnd + 1;
                }
            }
        }

        // Parse States
        size_t statesPos = content.find("\"states\":");
        if (statesPos != std::string::npos) {
            size_t arrStart = content.find('[', statesPos);
            size_t arrEnd = content.find(']', arrStart);
            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string arrContent = content.substr(arrStart, arrEnd - arrStart);
                size_t objStart = 0;
                while ((objStart = arrContent.find('{', objStart)) != std::string::npos) {
                    size_t objEnd = arrContent.find('}', objStart);
                    if (objEnd == std::string::npos) break;
                    std::string objStr = arrContent.substr(objStart, objEnd - objStart + 1);

                    AnimationStateNode s;
                    s.id = (uint32_t)extractNumber(objStr, "id", 0);
                    s.name = extractString(objStr, "name");
                    s.isEntry = extractBool(objStr, "isEntry", false);
                    s.clipPath = extractString(objStr, "clipPath");
                    s.speed = (float)extractNumber(objStr, "speed", 1.0);
                    s.loop = extractBool(objStr, "loop", true);
                    s.position.x = (float)extractNumber(objStr, "posX", 100.0);
                    s.position.y = (float)extractNumber(objStr, "posY", 100.0);
                    if (s.id != 0) states.push_back(s);
                    objStart = objEnd + 1;
                }
            }
        }

        // If states empty, restore Begin node
        if (states.empty()) {
            AnimationStateNode beginNode;
            beginNode.id = 1;
            beginNode.name = "Begin";
            beginNode.isEntry = true;
            beginNode.position = glm::vec2(200.0f, 250.0f);
            states.push_back(beginNode);
            entryStateId = 1;
            nextId = std::max(nextId, 2u);
        }

        // Parse Transitions
        size_t transPos = content.find("\"transitions\":");
        if (transPos != std::string::npos) {
            size_t arrStart = content.find('[', transPos);
            size_t arrEnd = content.rfind(']');
            if (arrStart != std::string::npos && arrEnd != std::string::npos && arrEnd > arrStart) {
                std::string arrContent = content.substr(arrStart, arrEnd - arrStart + 1);
                size_t pos = 0;
                while ((pos = arrContent.find('{', pos)) != std::string::npos) {
                    // Match closing brace using depth counter
                    int depth = 0;
                    size_t objStart = pos;
                    size_t objEnd = std::string::npos;
                    for (size_t p = objStart; p < arrContent.length(); ++p) {
                        if (arrContent[p] == '{') depth++;
                        else if (arrContent[p] == '}') {
                            depth--;
                            if (depth == 0) {
                                objEnd = p;
                                break;
                            }
                        }
                    }
                    if (objEnd == std::string::npos) break;

                    std::string objStr = arrContent.substr(objStart, objEnd - objStart + 1);

                    StateTransition t;
                    t.id = (uint32_t)extractNumber(objStr, "id", 0);
                    t.sourceStateId = (uint32_t)extractNumber(objStr, "sourceId", 0);
                    t.targetStateId = (uint32_t)extractNumber(objStr, "targetId", 0);
                    t.smoothingTime = (float)extractNumber(objStr, "smoothingTime", 0.20);

                    // Parse nested conditions
                    size_t cArrStart = objStr.find("\"conditions\":");
                    if (cArrStart != std::string::npos) {
                        size_t cStart = objStr.find('[', cArrStart);
                        size_t cEnd = objStr.find(']', cStart);
                        if (cStart != std::string::npos && cEnd != std::string::npos) {
                            std::string cContent = objStr.substr(cStart, cEnd - cStart + 1);
                            size_t cPos = 0;
                            while ((cPos = cContent.find('{', cPos)) != std::string::npos) {
                                int cDepth = 0;
                                size_t cObjStart = cPos;
                                size_t cObjEnd = std::string::npos;
                                for (size_t cp = cObjStart; cp < cContent.length(); ++cp) {
                                    if (cContent[cp] == '{') cDepth++;
                                    else if (cContent[cp] == '}') {
                                        cDepth--;
                                        if (cDepth == 0) {
                                            cObjEnd = cp;
                                            break;
                                        }
                                    }
                                }
                                if (cObjEnd == std::string::npos) break;
                                std::string cStr = cContent.substr(cObjStart, cObjEnd - cObjStart + 1);

                                TransitionCondition c;
                                c.paramName = extractString(cStr, "paramName");
                                c.op = StringToConditionOp(extractString(cStr, "op"));
                                c.targetBool = extractBool(cStr, "targetBool", true);
                                c.targetFloat = (float)extractNumber(cStr, "targetFloat", 0.0);
                                c.targetInt = (int)extractNumber(cStr, "targetInt", 0);
                                c.targetString = extractString(cStr, "targetString");
                                if (!c.paramName.empty()) t.conditions.push_back(c);
                                cPos = cObjEnd + 1;
                            }
                        }
                    }

                    if (t.id != 0 && t.sourceStateId != 0 && t.targetStateId != 0) {
                        transitions.push_back(t);
                    }
                    pos = objEnd + 1;
                }
            }
        }

        return true;
    }
};

class AnimatorInstance {
public:
    std::shared_ptr<AnimatorGraph> graph;
    uint32_t activeStateId = 0;
    uint32_t previousStateId = 0;

    bool isTransitioning = false;
    float transitionProgress = 0.0f; // 0.0 to 1.0
    float transitionDuration = 0.20f;

    float timeInCurrentState = 0.0f;
    float timeInPreviousState = 0.0f;

    std::unordered_map<std::string, AnimatorParam> runtimeParams;

    AnimatorInstance() = default;
    explicit AnimatorInstance(std::shared_ptr<AnimatorGraph> g) : graph(g) {
        Reset();
    }

    void Reset() {
        if (!graph) return;
        runtimeParams.clear();
        for (const auto& p : graph->parameters) {
            runtimeParams[p.name] = p;
        }
        auto* entry = graph->FindEntryState();
        activeStateId = entry ? entry->id : 0;
        previousStateId = 0;
        isTransitioning = false;
        transitionProgress = 0.0f;
        transitionDuration = 0.20f;
        timeInCurrentState = 0.0f;
        timeInPreviousState = 0.0f;
    }

    void SetBool(const std::string& name, bool value) {
        auto it = runtimeParams.find(name);
        if (it != runtimeParams.end()) {
            it->second.boolValue = value;
        } else {
            AnimatorParam p(name, AnimatorParamType::Boolean);
            p.boolValue = value;
            runtimeParams[name] = p;
        }
    }

    void SetFloat(const std::string& name, float value) {
        auto it = runtimeParams.find(name);
        if (it != runtimeParams.end()) {
            it->second.floatValue = value;
        } else {
            AnimatorParam p(name, AnimatorParamType::Float);
            p.floatValue = value;
            runtimeParams[name] = p;
        }
    }

    void SetInt(const std::string& name, int value) {
        auto it = runtimeParams.find(name);
        if (it != runtimeParams.end()) {
            it->second.intValue = value;
        } else {
            AnimatorParam p(name, AnimatorParamType::Int);
            p.intValue = value;
            runtimeParams[name] = p;
        }
    }

    void SetString(const std::string& name, const std::string& value) {
        auto it = runtimeParams.find(name);
        if (it != runtimeParams.end()) {
            it->second.stringValue = value;
        } else {
            AnimatorParam p(name, AnimatorParamType::String);
            p.stringValue = value;
            runtimeParams[name] = p;
        }
    }

    bool GetBool(const std::string& name, bool def = false) const {
        auto it = runtimeParams.find(name);
        return it != runtimeParams.end() ? it->second.boolValue : def;
    }

    float GetFloat(const std::string& name, float def = 0.0f) const {
        auto it = runtimeParams.find(name);
        return it != runtimeParams.end() ? it->second.floatValue : def;
    }

    int GetInt(const std::string& name, int def = 0) const {
        auto it = runtimeParams.find(name);
        return it != runtimeParams.end() ? it->second.intValue : def;
    }

    std::string GetString(const std::string& name, const std::string& def = "") const {
        auto it = runtimeParams.find(name);
        return it != runtimeParams.end() ? it->second.stringValue : def;
    }

    void Update(float deltaTime) {
        if (!graph) return;

        // Synchronize graph parameters to runtimeParams
        for (const auto& p : graph->parameters) {
            if (runtimeParams.find(p.name) == runtimeParams.end()) {
                runtimeParams[p.name] = p;
            }
        }

        // If no active state, pick entry
        if (activeStateId == 0) {
            auto* entry = graph->FindEntryState();
            if (entry) activeStateId = entry->id;
            else return;
        }

        const auto* currentState = graph->FindState(activeStateId);
        if (!currentState) return;

        // Advance playback time
        float curSpeed = currentState->speed;
        timeInCurrentState += deltaTime * curSpeed;

        if (isTransitioning) {
            const auto* prevState = graph->FindState(previousStateId);
            float prevSpeed = prevState ? prevState->speed : 1.0f;
            timeInPreviousState += deltaTime * prevSpeed;

            if (transitionDuration > 0.001f) {
                transitionProgress += deltaTime / transitionDuration;
            } else {
                transitionProgress = 1.0f;
            }

            if (transitionProgress >= 1.0f) {
                isTransitioning = false;
                transitionProgress = 1.0f;
                previousStateId = 0;
                timeInPreviousState = 0.0f;
            }
        } else {
            // Check outgoing transitions from active state
            bool transitioned = false;
            for (const auto& t : graph->transitions) {
                if (t.sourceStateId == activeStateId) {
                    if (t.CanTransition(runtimeParams, currentState->isEntry)) {
                        // Start transition to target state
                        previousStateId = activeStateId;
                        activeStateId = t.targetStateId;
                        isTransitioning = true;
                        transitionProgress = 0.0f;
                        transitionDuration = std::max(0.01f, t.smoothingTime);
                        timeInPreviousState = timeInCurrentState;
                        timeInCurrentState = 0.0f;
                        transitioned = true;
                        break;
                    }
                }
            }

            // Real-time Preview: If no outgoing transition was triggered,
            // check if the active state was reached via a conditional transition whose conditions
            // are NO LONGER satisfied (e.g. IsMoving became false, or Speed dropped below threshold).
            // Smoothly return to the source state so preview dynamically reacts to live parameter edits.
            if (!transitioned && !currentState->isEntry) {
                for (const auto& t : graph->transitions) {
                    if (t.targetStateId == activeStateId && !t.conditions.empty()) {
                        bool stillValid = true;
                        for (const auto& cond : t.conditions) {
                            if (!cond.Evaluate(runtimeParams)) {
                                stillValid = false;
                                break;
                            }
                        }
                        if (!stillValid) {
                            previousStateId = activeStateId;
                            activeStateId = t.sourceStateId;
                            isTransitioning = true;
                            transitionProgress = 0.0f;
                            transitionDuration = std::max(0.01f, t.smoothingTime);
                            timeInPreviousState = timeInCurrentState;
                            timeInCurrentState = 0.0f;
                            break;
                        }
                    }
                }
            }
        }
    }
};

} // namespace Eunoia
