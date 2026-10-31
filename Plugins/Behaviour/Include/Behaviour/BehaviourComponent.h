#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <EngineScene/GameObject.h>

namespace Eunoia {

struct BehaviourComponentDef {
    int id = 1;
    std::string name = "Root";
    int parentId = 0;
    std::vector<int> childIds;

    // Transform
    glm::vec3 localPosition{0.0f, 0.0f, 0.0f};
    glm::vec3 localRotation{0.0f, 0.0f, 0.0f}; // Euler angles in degrees
    glm::vec3 localScale{1.0f, 1.0f, 1.0f};

    // Component Type & Visuals
    PrimitiveType type = PrimitiveType::Empty;
    std::string meshPath = "";
    std::string materialPath = "";
    glm::vec4 color{0.85f, 0.85f, 0.85f, 1.0f};

    // Light Attributes
    glm::vec3 lightColor{1.0f, 1.0f, 1.0f};
    float lightIntensity = 10.0f;
    float lightRange = 10.0f;
    float innerConeAngle = 15.0f;
    float outerConeAngle = 45.0f;
    float coneFalloff = 1.0f;
    float rangeSmoothing = 0.5f;
    float attenuation = 1.0f;
    float radius = 0.025f;
    float length = 0.0f;
    bool useTemperature = false;
    float temperature = 6500.0f;
    bool castShadows = true;

    // Visuals & PBR Attributes
    float metallic = 0.0f;
    float roughness = 0.5f;
    float specular = 0.5f;
    float normalStrength = 1.0f;
    bool smoothShading = false;
    float autoSmoothAngle = 30.0f;
    glm::vec2 uvScale{1.0f, 1.0f};

    // Primitive shape parameters (Cube, Plane, Box, Sphere, Cylinder, Cone, Capsule, etc.)
    PrimitiveParams params;

    // Camera Attributes
    float cameraFov = 45.0f;
    float cameraNear = 0.1f;
    float cameraFar = 1000.0f;
    bool isOrthographic = false;
    float orthoSize = 5.0f;

    bool visible = true;
};

} // namespace Eunoia
