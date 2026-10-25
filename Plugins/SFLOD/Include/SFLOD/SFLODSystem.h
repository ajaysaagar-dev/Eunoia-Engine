#pragma once
#include "SFLODTypes.h"
#include <EngineAssets/Geometry.h>
#include <EngineScene/GameObject.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <filesystem>
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

    // Mathematical software rasterizer: projects 3D mesh triangles onto the card plane
    // with Z-buffering and alpha cutouts, generating a high-quality RGBA texture map.
    bool RasterizeImposterTexture(
        const GameObject& obj,
        const glm::mat4& model,
        const glm::vec3& cameraPos,
        int resolution,
        std::vector<uint8_t>& outRgbaBuffer,
        float& outWidth,
        float& outHeight);

    // Writes an uncompressed 32-bit TGA image with full 8-bit alpha channel to disk
    static bool SaveTGA32(const std::string& filePath, int width, int height, const uint8_t* rgbaPixels);

    // Bakes and saves the imposter texture map to disk (Content/Imposters/) and registers it
    std::string BakeAndSaveImposterTexture(
        GameObject& obj,
        const glm::mat4& model,
        const glm::vec3& cameraPos,
        const std::filesystem::path& contentDir,
        int viewAngleIndex = -1,
        int resolution = 512);

    // Bakes all 8 surrounding horizontal angles (0..7) for multi-angle view rendering
    bool BakeAll8Angles(
        GameObject& obj,
        const glm::mat4& model,
        const std::filesystem::path& contentDir,
        int resolution = 512);

    // Calculates the discrete azimuthal view angle index (0..numAngles-1)
    int GetViewAngleIndex(
        const GameObject& obj,
        const glm::mat4& model,
        const glm::vec3& cameraPos,
        int numAngles = 8) const;

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

        // Determine active imposter texture: check if multi-angle series exists
        std::string activeTexture = obj.sflodCustomTexture;
        if (!activeTexture.empty()) {
            size_t anglePos = activeTexture.find("_angle");
            if (anglePos != std::string::npos) {
                int angleIdx = GetViewAngleIndex(obj, model, cameraPos, 8);
                std::string basePrefix = activeTexture.substr(0, anglePos);
                std::string ext = std::filesystem::path(activeTexture).extension().string();
                std::string candidateAngleTex = basePrefix + "_angle" + std::to_string(angleIdx) + ext;
                std::error_code ec;
                if (std::filesystem::exists(candidateAngleTex, ec)) {
                    activeTexture = candidateAngleTex;
                }
            }
        }

        // If no custom imposter texture set, fallback to object baseColorTexture
        if (activeTexture.empty()) {
            activeTexture = obj.baseColorTexture;
        }

        // RenderBatch for Imposter Card
        BatchType b;
        b.startIndex = iStart;
        b.indexCount = 6;
        b.distSq = distSq;
        b.isVisible = isVisible;
        b.isShadowVisible = isShadowVisible;
        b.activeLOD = (int)selectedLod;
        b.albedoTex = activeTexture;
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
        b.opacityMaskClipValue = (obj.opacityMaskClipValue > 0.0f) ? obj.opacityMaskClipValue : 0.333f;
        b.blendMode = 1; // Always use Masked alpha cutout for imposter cards
        b.shadingModel = obj.shadingModel;
        b.isUnlit = (obj.shadingModel == 1);
        b.castShadows = obj.castShadows;
        b.receiveShadows = obj.receiveShadows;
        b.meshClusterCulling = false;
        b.objectId = obj.id;
        b.vertexOffset = vOffset;
        b.uvScale = glm::vec2(1.0f, 1.0f);
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
