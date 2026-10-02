#pragma once
#include "CollisionTypes.h"
#include <EngineAssets/Geometry.h>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct GameObject;
class Scene;

namespace Collisions {

class CollisionSystem {
public:
    static CollisionSystem& Get() {
        static CollisionSystem s_instance;
        return s_instance;
    }

    // Default modifier setup for any GameObject depending on its PrimitiveType
    void SetupDefaultModifiers(GameObject& obj);

    // Add a new collision modifier
    CollisionModifier& AddModifier(GameObject& obj, CollisionShapeType shapeType);

    // Remove modifier at index
    bool RemoveModifier(GameObject& obj, size_t index);

    // Compute bounding dimensions matching the mesh geometry for a specific modifier
    void AutoFitModifier(GameObject& obj, CollisionModifier& mod);
    void AutoFitModifier(GameObject& obj, size_t index);

    // Reset modifier shape and size to factory defaults
    void ResetModifierToDefault(GameObject& obj, CollisionModifier& mod);
    void ResetModifierToDefault(GameObject& obj, size_t index);

    // Legacy wrappers
    void SetupDefaultCollision(GameObject& obj) { SetupDefaultModifiers(obj); }
    void AutoFitBounds(GameObject& obj);
    void ResetToDefaultBounds(GameObject& obj);

    // Viewport wireframe visualization for all active modifiers
    void AppendCollisionWireframe(
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        const GameObject& obj,
        const glm::vec3& cameraPos,
        const glm::mat4& worldMat);

    // Queries
    RaycastHitResult Raycast(const Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 1000.0f);
    bool CheckOverlap(const GameObject& a, const glm::mat4& worldA, const GameObject& b, const glm::mat4& worldB, CollisionHitResult& outResult);

    // Shape wireframe generators
    void GenerateBoxWireframe(std::vector<std::pair<glm::vec3, glm::vec3>>& outEdges, const glm::vec3& extents, const glm::vec3& center);
    void GenerateSphereWireframe(std::vector<std::pair<glm::vec3, glm::vec3>>& outEdges, float radius, const glm::vec3& center, int segments = 24);
    void GenerateCapsuleWireframe(std::vector<std::pair<glm::vec3, glm::vec3>>& outEdges, float radius, float height, const glm::vec3& center, int segments = 20);
    void GenerateCylinderWireframe(std::vector<std::pair<glm::vec3, glm::vec3>>& outEdges, float radius, float height, const glm::vec3& center, int segments = 24);
    void GenerateConeWireframe(std::vector<std::pair<glm::vec3, glm::vec3>>& outEdges, float radius, float height, const glm::vec3& center, int segments = 24);
    void GeneratePlaneWireframe(std::vector<std::pair<glm::vec3, glm::vec3>>& outEdges, const glm::vec2& size, const glm::vec3& center);
    void GenerateMeshWireframe(std::vector<std::pair<glm::vec3, glm::vec3>>& outEdges, const GameObject& obj, const CollisionModifier& mod);

private:
    CollisionSystem() = default;
    ~CollisionSystem() = default;
};

} // namespace Collisions
