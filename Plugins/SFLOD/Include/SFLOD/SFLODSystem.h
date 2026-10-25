#pragma once
#include "SFLODTypes.h"
#include <EngineAssets/Geometry.h>
#include <EngineScene/GameObject.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace SFLOD {

class SFLODSystem {
public:
    static SFLODSystem& Get() {
        static SFLODSystem instance;
        return instance;
    }

    bool IsSFLODActive(const GameObject& obj, uint32_t currentLod) const;

    void ComputeCardGeometry(
        const GameObject& obj,
        const glm::mat4& model,
        const glm::vec3& cameraPos,
        SFLODCardGeometry& outGeom) const;

    void ComputeCardBounds(const GameObject& obj, float& outWidth, float& outHeight, glm::vec3& outOffset) const {
        SFLODCardGeometry geom;
        ComputeCardGeometry(obj, glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 5.0f), geom);
        outWidth = geom.halfWidth * 2.0f;
        outHeight = geom.halfHeight * 2.0f;
        outOffset = geom.center;
    }

    template<typename BatchType>
    void AppendImposterCard(
        const GameObject& obj,
        const glm::mat4& model,
        const glm::vec3& cameraPos,
        uint32_t selectedLod,
        float distSq,
        bool isVisible,
        bool isShadowVisible,
        std::vector<Vertex>& outVertices,
        std::vector<uint32_t>& outIndices,
        std::vector<BatchType>& outBatches)
    {
        SFLODCardGeometry geom;
        ComputeCardGeometry(obj, model, cameraPos, geom);

        uint32_t vOffset = static_cast<uint32_t>(outVertices.size());
        uint32_t iStart = static_cast<uint32_t>(outIndices.size());

        // 4 Quad Vertices
        glm::vec3 p0 = geom.center - geom.right * geom.halfWidth - geom.up * geom.halfHeight; // bottom-left
        glm::vec3 p1 = geom.center + geom.right * geom.halfWidth - geom.up * geom.halfHeight; // bottom-right
        glm::vec3 p2 = geom.center + geom.right * geom.halfWidth + geom.up * geom.halfHeight; // top-right
        glm::vec3 p3 = geom.center - geom.right * geom.halfWidth + geom.up * geom.halfHeight; // top-left

        glm::vec2 uv0(0.0f, 1.0f);
        glm::vec2 uv1(1.0f, 1.0f);
        glm::vec2 uv2(1.0f, 0.0f);
        glm::vec2 uv3(0.0f, 0.0f);

        glm::vec3 vColor(1.0f, 1.0f, 1.0f);
        glm::vec4 tangent(geom.right, 1.0f);

        outVertices.push_back({ p0, geom.normal, uv0, vColor, tangent });
        outVertices.push_back({ p1, geom.normal, uv1, vColor, tangent });
        outVertices.push_back({ p2, geom.normal, uv2, vColor, tangent });
        outVertices.push_back({ p3, geom.normal, uv3, vColor, tangent });

        // 2 Triangles (6 indices)
        outIndices.push_back(vOffset + 0);
        outIndices.push_back(vOffset + 1);
        outIndices.push_back(vOffset + 2);

        outIndices.push_back(vOffset + 0);
        outIndices.push_back(vOffset + 2);
        outIndices.push_back(vOffset + 3);

        // RenderBatch for Imposter Card
        BatchType b;
        b.startIndex = iStart;
        b.indexCount = 6;
        b.distSq = distSq;
        b.isVisible = isVisible;
        b.isShadowVisible = isShadowVisible;
        b.activeLOD = (int)selectedLod;
        b.albedoTex = !obj.sflodCustomTexture.empty() ? obj.sflodCustomTexture : obj.baseColorTexture;
        b.normalTex = obj.normalTexture;
        b.roughTex = obj.roughnessTexture;
        b.metalTex = obj.metallicTexture;
        b.aoTex = obj.aoTexture;
        b.opacityTex = obj.opacityTexture;
        b.emissionTex = obj.emissionTexture;
        b.baseColor = obj.color;
        b.metallic = obj.metallic;
        b.roughness = obj.roughness;
        b.normalStrength = obj.normalStrength;
        b.specular = obj.specular;
        b.emissiveColor = obj.emissiveColor;
        b.emissiveIntensity = obj.emissiveIntensity;
        b.opacity = obj.opacity;
        b.opacityMaskClipValue = 0.333f;
        b.blendMode = obj.sflodCutout ? 1 : obj.blendMode; // Masked (cutout) for cards or user blendMode
        b.shadingModel = obj.shadingModel;
        b.isUnlit = (obj.shadingModel == 1);
        b.castShadows = obj.castShadows;
        b.receiveShadows = obj.receiveShadows;
        b.meshClusterCulling = false;
        b.objectId = obj.id;
        b.vertexOffset = vOffset;
        b.uvScale = obj.uvScale;
        b.normalMapYFlip = obj.normalMapYFlip;
        b.metallicChannel = obj.metallicChannel;
        b.roughnessChannel = obj.roughnessChannel;
        b.aoChannel = obj.aoChannel;
        b.materialDebugMode = obj.materialDebugMode;
        b.twoSided = true; // Imposter cards are always two-sided
        b.disableStreaming = obj.disableStreaming;
        b.useVertexAlpha = 0.0f;

        outBatches.push_back(b);

        // Track telemetry
        m_telemetry.activeCards++;
        uint32_t originalTris = static_cast<uint32_t>(obj.mesh.indices.size() / 3);
        if (originalTris > 2) {
            m_telemetry.totalSavedTriangles += (originalTris - 2);
        }
    }

    void ResetFrameStats() {
        m_telemetry.activeCards = 0;
        m_telemetry.totalSavedTriangles = 0;
    }

    uint32_t GetActiveCardCount() const { return m_telemetry.activeCards; }
    uint32_t GetTrianglesSaved() const { return m_telemetry.totalSavedTriangles; }

private:
    SFLODSystem() = default;
    ~SFLODSystem() = default;
    SFLODSystem(const SFLODSystem&) = delete;
    SFLODSystem& operator=(const SFLODSystem&) = delete;

    SFLODTelemetry m_telemetry;
};

} // namespace SFLOD
