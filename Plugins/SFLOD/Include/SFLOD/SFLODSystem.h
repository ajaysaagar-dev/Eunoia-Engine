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

    // Bakes an individual orthogonal face (+Z, -Z, +X, -X, +Y, -Y) into an RGBA tile buffer
    bool RasterizeFace(
        const GameObject& obj,
        const glm::mat4& model,
        SFLODOrthogonalFace face,
        int tileResolution,
        std::vector<uint8_t>& outTileRgba,
        float cardW,
        float cardH,
        std::vector<uint8_t>* outTileNormal = nullptr,
        std::vector<uint8_t>* outTileRMA = nullptr);

    // Writes an uncompressed 32-bit TGA image with full 8-bit alpha channel to disk
    static bool SaveTGA32(const std::string& filePath, int width, int height, const uint8_t* rgbaPixels);

    // Bakes a single view-dependent imposter texture to disk
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

    // Imposter Cards: Nacho Time! - 6-directional orthogonal texture atlas baking (+X, -X, +Y, -Y, +Z, -Z)
    // Packs all 6 orthogonal shots into a single 3x2 grid texture atlas
    std::string BakeNachoTimeAtlas(
        GameObject& obj,
        const glm::mat4& model,
        const std::filesystem::path& contentDir,
        int tileResolution = 512,
        SFLODShaderComplexity complexity = SFLODShaderComplexity::Low);

    // Determines the dominant orthogonal face based on incoming view vector relative to object
    SFLODOrthogonalFace GetDominantFace(
        const GameObject& obj,
        const glm::mat4& model,
        const glm::vec3& cameraPos,
        glm::vec3* outLocalDir = nullptr) const;

    // Returns the UV bounds (minUV, maxUV) for a given orthogonal face in the 3x2 atlas grid
    static SFLODAtlasTileRect GetAtlasTileUV(SFLODOrthogonalFace face);

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

        // Check if object is using 6-directional Nacho Time atlas
        bool isAtlas = obj.sflodIsAtlas || (obj.sflodCustomTexture.find("_Atlas") != std::string::npos);
        glm::vec3 localDir(0.0f);
        SFLODOrthogonalFace domFace = GetDominantFace(obj, model, cameraPos, &localDir);

        bool isTriPlanar = (obj.sflodGeometryMode == 1); // 1 = TriPlanarNacho
        uint32_t numIndicesGenerated = 0;

        if (isTriPlanar) {
            // =========================================================================
            // Mode B: Tri-Planar Nacho Mode (3 Intersecting Orthogonal Planes, 6 Tris)
            // =========================================================================
            // Extract orthonormal axes from model matrix
            float scaleX = glm::length(glm::vec3(model[0]));
            float scaleY = glm::length(glm::vec3(model[1]));
            float scaleZ = glm::length(glm::vec3(model[2]));
            glm::vec3 axisX = (scaleX > 1e-4f) ? (glm::vec3(model[0]) / scaleX) : glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 axisY = (scaleY > 1e-4f) ? (glm::vec3(model[1]) / scaleY) : glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 axisZ = (scaleZ > 1e-4f) ? (glm::vec3(model[2]) / scaleZ) : glm::vec3(0.0f, 0.0f, 1.0f);

            glm::vec3 center = geom.center;
            float hw = geom.halfWidth;
            float hh = geom.halfHeight;

            glm::vec3 vColor(1.0f, 1.0f, 1.0f);

            // Plane 1: XY Quad (Front/Back)
            SFLODOrthogonalFace faceXY = (localDir.z >= 0.0f) ? SFLODOrthogonalFace::Front : SFLODOrthogonalFace::Back;
            SFLODAtlasTileRect tileXY = isAtlas ? GetAtlasTileUV(faceXY) : SFLODAtlasTileRect{ glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f) };
            glm::vec3 normXY = (localDir.z >= 0.0f) ? axisZ : -axisZ;
            glm::vec3 rightXY = (localDir.z >= 0.0f) ? axisX : -axisX;
            glm::vec3 upXY = axisY;

            glm::vec3 xy0 = center - rightXY * hw - upXY * hh;
            glm::vec3 xy1 = center + rightXY * hw - upXY * hh;
            glm::vec3 xy2 = center + rightXY * hw + upXY * hh;
            glm::vec3 xy3 = center - rightXY * hw + upXY * hh;

            uint32_t idxXY = static_cast<uint32_t>(outVertices.size());
            outVertices.push_back({ xy0, normXY, glm::vec2(tileXY.minUV.x, tileXY.maxUV.y), vColor, glm::vec4(rightXY, 1.0f) });
            outVertices.push_back({ xy1, normXY, glm::vec2(tileXY.maxUV.x, tileXY.maxUV.y), vColor, glm::vec4(rightXY, 1.0f) });
            outVertices.push_back({ xy2, normXY, glm::vec2(tileXY.maxUV.x, tileXY.minUV.y), vColor, glm::vec4(rightXY, 1.0f) });
            outVertices.push_back({ xy3, normXY, glm::vec2(tileXY.minUV.x, tileXY.minUV.y), vColor, glm::vec4(rightXY, 1.0f) });

            outIndices.push_back(idxXY + 0); outIndices.push_back(idxXY + 1); outIndices.push_back(idxXY + 2);
            outIndices.push_back(idxXY + 0); outIndices.push_back(idxXY + 2); outIndices.push_back(idxXY + 3);

            // Plane 2: YZ Quad (Sides: Right/Left)
            SFLODOrthogonalFace faceYZ = (localDir.x >= 0.0f) ? SFLODOrthogonalFace::Right : SFLODOrthogonalFace::Left;
            SFLODAtlasTileRect tileYZ = isAtlas ? GetAtlasTileUV(faceYZ) : SFLODAtlasTileRect{ glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f) };
            glm::vec3 normYZ = (localDir.x >= 0.0f) ? axisX : -axisX;
            glm::vec3 rightYZ = (localDir.x >= 0.0f) ? -axisZ : axisZ;
            glm::vec3 upYZ = axisY;

            glm::vec3 yz0 = center - rightYZ * hw - upYZ * hh;
            glm::vec3 yz1 = center + rightYZ * hw - upYZ * hh;
            glm::vec3 yz2 = center + rightYZ * hw + upYZ * hh;
            glm::vec3 yz3 = center - rightYZ * hw + upYZ * hh;

            uint32_t idxYZ = static_cast<uint32_t>(outVertices.size());
            outVertices.push_back({ yz0, normYZ, glm::vec2(tileYZ.minUV.x, tileYZ.maxUV.y), vColor, glm::vec4(rightYZ, 1.0f) });
            outVertices.push_back({ yz1, normYZ, glm::vec2(tileYZ.maxUV.x, tileYZ.maxUV.y), vColor, glm::vec4(rightYZ, 1.0f) });
            outVertices.push_back({ yz2, normYZ, glm::vec2(tileYZ.maxUV.x, tileYZ.minUV.y), vColor, glm::vec4(rightYZ, 1.0f) });
            outVertices.push_back({ yz3, normYZ, glm::vec2(tileYZ.minUV.x, tileYZ.minUV.y), vColor, glm::vec4(rightYZ, 1.0f) });

            outIndices.push_back(idxYZ + 0); outIndices.push_back(idxYZ + 1); outIndices.push_back(idxYZ + 2);
            outIndices.push_back(idxYZ + 0); outIndices.push_back(idxYZ + 2); outIndices.push_back(idxYZ + 3);

            // Plane 3: XZ Quad (Top/Bottom)
            SFLODOrthogonalFace faceXZ = (localDir.y >= 0.0f) ? SFLODOrthogonalFace::Top : SFLODOrthogonalFace::Bottom;
            SFLODAtlasTileRect tileXZ = isAtlas ? GetAtlasTileUV(faceXZ) : SFLODAtlasTileRect{ glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 1.0f) };
            glm::vec3 normXZ = (localDir.y >= 0.0f) ? axisY : -axisY;
            glm::vec3 rightXZ = axisX;
            glm::vec3 upXZ = (localDir.y >= 0.0f) ? -axisZ : axisZ;

            glm::vec3 xz0 = center - rightXZ * hw - upXZ * hw;
            glm::vec3 xz1 = center + rightXZ * hw - upXZ * hw;
            glm::vec3 xz2 = center + rightXZ * hw + upXZ * hw;
            glm::vec3 xz3 = center - rightXZ * hw + upXZ * hw;

            uint32_t idxXZ = static_cast<uint32_t>(outVertices.size());
            outVertices.push_back({ xz0, normXZ, glm::vec2(tileXZ.minUV.x, tileXZ.maxUV.y), vColor, glm::vec4(rightXZ, 1.0f) });
            outVertices.push_back({ xz1, normXZ, glm::vec2(tileXZ.maxUV.x, tileXZ.maxUV.y), vColor, glm::vec4(rightXZ, 1.0f) });
            outVertices.push_back({ xz2, normXZ, glm::vec2(tileXZ.maxUV.x, tileXZ.minUV.y), vColor, glm::vec4(rightXZ, 1.0f) });
            outVertices.push_back({ xz3, normXZ, glm::vec2(tileXZ.minUV.x, tileXZ.minUV.y), vColor, glm::vec4(rightXZ, 1.0f) });

            outIndices.push_back(idxXZ + 0); outIndices.push_back(idxXZ + 1); outIndices.push_back(idxXZ + 2);
            outIndices.push_back(idxXZ + 0); outIndices.push_back(idxXZ + 2); outIndices.push_back(idxXZ + 3);

            numIndicesGenerated = 18;
        } else {
            // =========================================================================
            // Mode A: Single Plane (1 Quad Billboard, 2 Tris)
            // =========================================================================
            glm::vec3 p0 = geom.center - geom.right * geom.halfWidth - geom.up * geom.halfHeight; // bottom-left
            glm::vec3 p1 = geom.center + geom.right * geom.halfWidth - geom.up * geom.halfHeight; // bottom-right
            glm::vec3 p2 = geom.center + geom.right * geom.halfWidth + geom.up * geom.halfHeight; // top-right
            glm::vec3 p3 = geom.center - geom.right * geom.halfWidth + geom.up * geom.halfHeight; // top-left

            glm::vec2 uv0(0.0f, 1.0f);
            glm::vec2 uv1(1.0f, 1.0f);
            glm::vec2 uv2(1.0f, 0.0f);
            glm::vec2 uv3(0.0f, 0.0f);

            if (isAtlas) {
                // Dynamically map incoming view vector onto dominant atlas tile
                SFLODAtlasTileRect tile = GetAtlasTileUV(domFace);
                uv0 = glm::vec2(tile.minUV.x, tile.maxUV.y);
                uv1 = glm::vec2(tile.maxUV.x, tile.maxUV.y);
                uv2 = glm::vec2(tile.maxUV.x, tile.minUV.y);
                uv3 = glm::vec2(tile.minUV.x, tile.minUV.y);
            }

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

            numIndicesGenerated = 6;
        }

        // Determine active imposter texture: check if multi-angle series exists
        std::string activeTexture = obj.sflodCustomTexture;
        if (!activeTexture.empty() && !isAtlas) {
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
        b.indexCount = numIndicesGenerated;
        b.distSq = distSq;
        b.isVisible = isVisible;
        b.isShadowVisible = isShadowVisible;
        b.activeLOD = (int)selectedLod;
        b.albedoTex = activeTexture;

        // Normal map detection for Medium and High shader complexity
        std::string normalTex = obj.normalTexture;
        if (isAtlas) {
            size_t atlasPos = activeTexture.find("_Atlas.tga");
            if (atlasPos != std::string::npos) {
                std::string candidateNormalAtlas = activeTexture.substr(0, atlasPos) + "_NormalAtlas.tga";
                std::error_code ec;
                if (std::filesystem::exists(candidateNormalAtlas, ec)) {
                    normalTex = candidateNormalAtlas;
                }
            }
        }
        b.normalTex = normalTex;

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
        uint32_t cardTris = numIndicesGenerated / 3;
        if (originalTris > cardTris) {
            m_telemetry.totalSavedTriangles += (originalTris - cardTris);
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
