#pragma once

#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <unordered_map>
#include <Animation-Graph/AnimatorGraph.h>
#include <Animation-Graph/SkeletalAnimation.h>
#include <Animation-Blending/AnimationBlend.h>

struct GameObject;

namespace Eunoia {

struct AnimationGraphModifier {
    bool enabled = true;
    std::string name = "Animation Graph";
    std::string graphPath = "";
    std::shared_ptr<AnimatorGraph> graph;
    std::unique_ptr<AnimatorInstance> instance;

    // Runtime loaded assets and caches
    std::unordered_map<std::string, std::shared_ptr<AnimationClip>> loadedClips;
    std::unordered_map<std::string, std::shared_ptr<AnimationBlend>> loadedBlends;
    std::shared_ptr<SkeletalMesh> skeletalMesh;
    std::string cachedMeshPath = "";
    std::unordered_map<uint32_t, glm::vec2> smoothedBlendCoords;

    AnimationGraphModifier() = default;

    // Copy constructor: clones graph pointer and creates a new runtime instance
    AnimationGraphModifier(const AnimationGraphModifier& other) {
        enabled = other.enabled;
        name = other.name;
        graphPath = other.graphPath;
        graph = other.graph;
        cachedMeshPath = other.cachedMeshPath;
        skeletalMesh = other.skeletalMesh;
        loadedClips = other.loadedClips;
        loadedBlends = other.loadedBlends;
        smoothedBlendCoords = other.smoothedBlendCoords;
        if (!graphPath.empty()) {
            LoadGraph(graphPath);
        }
    }

    // Copy assignment operator
    AnimationGraphModifier& operator=(const AnimationGraphModifier& other) {
        if (this != &other) {
            enabled = other.enabled;
            name = other.name;
            graphPath = other.graphPath;
            graph = other.graph;
            cachedMeshPath = other.cachedMeshPath;
            skeletalMesh = other.skeletalMesh;
            loadedClips = other.loadedClips;
            loadedBlends = other.loadedBlends;
            smoothedBlendCoords = other.smoothedBlendCoords;
            if (!graphPath.empty()) {
                LoadGraph(graphPath);
            } else {
                instance.reset();
            }
        }
        return *this;
    }

    // Move operations
    AnimationGraphModifier(AnimationGraphModifier&& other) noexcept = default;
    AnimationGraphModifier& operator=(AnimationGraphModifier&& other) noexcept = default;

    void LoadGraph(const std::string& path) {
        graphPath = path;
        if (graphPath.empty()) {
            graph.reset();
            instance.reset();
            return;
        }

        graph = std::make_shared<AnimatorGraph>();
        if (graph->LoadFromFile(path)) {
            instance = std::make_unique<AnimatorInstance>(graph);
            return;
        }

        // Try relative to project Content directory
        std::string contentCandidate = "Content/" + path;
        if (graph->LoadFromFile(contentCandidate)) {
            graphPath = contentCandidate;
            instance = std::make_unique<AnimatorInstance>(graph);
            return;
        }

        std::string projectsCandidate = "Projects/GameTest/Content/" + path;
        if (graph->LoadFromFile(projectsCandidate)) {
            graphPath = projectsCandidate;
            instance = std::make_unique<AnimatorInstance>(graph);
            return;
        }
    }

    // Forward declaration of Update; defined inline in Scene.h after GameObject and Scene are complete
    void Update(float dt, ::GameObject* owner = nullptr);

    // Parameter Setters
    void SetBoolean(const std::string& paramName, bool value) {
        if (instance) instance->SetBool(paramName, value);
    }
    void SetBool(const std::string& paramName, bool value) {
        SetBoolean(paramName, value);
    }

    void SetFloat(const std::string& paramName, float value) {
        if (instance) instance->SetFloat(paramName, value);
    }

    void SetInteger(const std::string& paramName, int value) {
        if (instance) instance->SetInt(paramName, value);
    }
    void SetInt(const std::string& paramName, int value) {
        SetInteger(paramName, value);
    }

    void SetString(const std::string& paramName, const std::string& value) {
        if (instance) instance->SetString(paramName, value);
    }

    // Parameter Getters
    bool GetBoolean(const std::string& paramName, bool defaultVal = false) const {
        return instance ? instance->GetBool(paramName, defaultVal) : defaultVal;
    }
    bool GetBool(const std::string& paramName, bool defaultVal = false) const {
        return GetBoolean(paramName, defaultVal);
    }

    float GetFloat(const std::string& paramName, float defaultVal = 0.0f) const {
        return instance ? instance->GetFloat(paramName, defaultVal) : defaultVal;
    }

    int GetInteger(const std::string& paramName, int defaultVal = 0) const {
        return instance ? instance->GetInt(paramName, defaultVal) : defaultVal;
    }
    int GetInt(const std::string& paramName, int defaultVal = 0) const {
        return GetInteger(paramName, defaultVal);
    }

    std::string GetString(const std::string& paramName, const std::string& defaultVal = "") const {
        return instance ? instance->GetString(paramName, defaultVal) : defaultVal;
    }

    // Playback and State Control
    void Play(const std::string& stateName) {
        if (!instance || !graph) return;
        for (const auto& s : graph->states) {
            if (s.name == stateName) {
                instance->activeStateId = s.id;
                instance->isTransitioning = false;
                instance->transitionProgress = 0.0f;
                instance->timeInCurrentState = 0.0f;
                break;
            }
        }
    }

    void Reset() {
        if (instance) instance->Reset();
    }

    std::string GetCurrentStateName() const {
        if (!instance || !graph) return "None";
        const auto* s = graph->FindState(instance->activeStateId);
        return s ? s->name : "None";
    }

    bool IsTransitioning() const {
        return instance ? instance->isTransitioning : false;
    }
};

} // namespace Eunoia

// Aliases for user behaviour scripts and engine systems
using AnimationGraphModifier = Eunoia::AnimationGraphModifier;
using AnimationGraph = Eunoia::AnimationGraphModifier;
using AnimatorGraph = Eunoia::AnimationGraphModifier;
using AnimatorComponent = Eunoia::AnimationGraphModifier;
