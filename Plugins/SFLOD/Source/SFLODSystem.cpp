#include "SFLOD/SFLODSystem.h"
#include <EngineScene/GameObject.h>
#include <EngineAssets/TextureManager.h>
#include <iostream>
#include <fstream>
#include <cmath>

namespace SFLOD {

static std::string CleanFilename(const std::string& name) {
    std::string clean = name;
    for (char& c : clean) {
        if (!isalnum((unsigned char)c) && c != '_' && c != '-') {
            c = '_';
        }
    }
    if (clean.empty()) clean = "Object";
    return clean;
}

bool SFLODSystem::IsSFLODActive(const GameObject& obj, uint32_t currentLod) const {
    if (!obj.enableSFLOD) return false;
    if (obj.mesh.vertices.empty()) return false;
    if (obj.forcedLOD >= 0) {
        return obj.forcedLOD >= obj.sflodMinLOD;
    }
    return (int)currentLod >= obj.sflodMinLOD;
}

void SFLODSystem::ComputeCardGeometry(
    const GameObject& obj,
    const glm::mat4& model,
    const glm::vec3& cameraPos,
    SFLODCardGeometry& outGeom) const
{
    // 1. Calculate local center and extents from mesh AABB
    glm::vec3 bMin = obj.mesh.aabbValid ? obj.mesh.aabbMin : glm::vec3(-0.5f);
    glm::vec3 bMax = obj.mesh.aabbValid ? obj.mesh.aabbMax : glm::vec3(0.5f);
    glm::vec3 localCenter = (bMin + bMax) * 0.5f;

    // Extract world scale from model matrix
    float scaleX = glm::length(glm::vec3(model[0]));
    float scaleY = glm::length(glm::vec3(model[1]));
    float scaleZ = glm::length(glm::vec3(model[2]));

    float extentX = (bMax.x - bMin.x) * (scaleX > 1e-4f ? scaleX : 1.0f);
    float extentY = (bMax.y - bMin.y) * (scaleY > 1e-4f ? scaleY : 1.0f);
    float extentZ = (bMax.z - bMin.z) * (scaleZ > 1e-4f ? scaleZ : 1.0f);

    float autoWidth = std::max(extentX, extentZ);
    if (autoWidth < 0.1f) autoWidth = 1.0f;
    float autoHeight = extentY;
    if (autoHeight < 0.1f) autoHeight = 1.0f;

    outGeom.halfWidth = (obj.sflodPlaneWidth > 0.001f) ? (obj.sflodPlaneWidth * 0.5f) : (autoWidth * 0.5f);
    outGeom.halfHeight = (obj.sflodPlaneHeight > 0.001f) ? (obj.sflodPlaneHeight * 0.5f) : (autoHeight * 0.5f);

    glm::vec3 worldCenter = glm::vec3(model * glm::vec4(localCenter, 1.0f));

    // 2. Compute orientation vectors based on mode
    glm::vec3 toCamera = cameraPos - worldCenter;
    float dist = glm::length(toCamera);
    glm::vec3 camDir = (dist > 1e-4f) ? (toCamera / dist) : glm::vec3(0.0f, 0.0f, 1.0f);

    SFLODBillboardMode mode = static_cast<SFLODBillboardMode>(obj.sflodBillboardMode);

    if (mode == SFLODBillboardMode::CameraFacingSpherical) {
        // Omnidirectional camera-facing billboard
        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(camDir, worldUp)) > 0.99f) {
            worldUp = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        outGeom.normal = camDir;
        outGeom.right = glm::normalize(glm::cross(worldUp, outGeom.normal));
        outGeom.up = glm::cross(outGeom.normal, outGeom.right);
    } else if (mode == SFLODBillboardMode::LockedYAxisAxial) {
        // "Nacho Time" foliage / tree axial billboard: lock rotation to World Y axis
        glm::vec3 camDirXZ(toCamera.x, 0.0f, toCamera.z);
        float distXZ = glm::length(camDirXZ);
        glm::vec3 normXZ = (distXZ > 1e-4f) ? (camDirXZ / distXZ) : glm::vec3(0.0f, 0.0f, 1.0f);

        outGeom.normal = normXZ;
        outGeom.up = glm::vec3(0.0f, 1.0f, 0.0f);
        outGeom.right = glm::normalize(glm::cross(outGeom.up, outGeom.normal));
    } else {
        // Fixed Plane: derived directly from model matrix
        outGeom.right = (scaleX > 1e-4f) ? (glm::vec3(model[0]) / scaleX) : glm::vec3(1.0f, 0.0f, 0.0f);
        outGeom.up = (scaleY > 1e-4f) ? (glm::vec3(model[1]) / scaleY) : glm::vec3(0.0f, 1.0f, 0.0f);
        outGeom.normal = (scaleZ > 1e-4f) ? (glm::vec3(model[2]) / scaleZ) : glm::vec3(0.0f, 0.0f, 1.0f);
    }

    // Apply offset
    worldCenter += outGeom.right * obj.sflodPlaneOffset.x + outGeom.up * obj.sflodPlaneOffset.y;
    outGeom.center = worldCenter;
}

int SFLODSystem::GetViewAngleIndex(
    const GameObject& obj,
    const glm::mat4& model,
    const glm::vec3& cameraPos,
    int numAngles) const
{
    glm::vec3 worldPos = glm::vec3(model[3]);
    glm::vec3 toCam = cameraPos - worldPos;
    float distXZ = std::sqrt(toCam.x * toCam.x + toCam.z * toCam.z);
    if (distXZ < 1e-4f) return 0;

    // Azimuthal angle in radians [-PI, PI]
    float theta = std::atan2(toCam.x, toCam.z);
    if (theta < 0.0f) {
        theta += 2.0f * 3.1415926535f;
    }

    float slice = (2.0f * 3.1415926535f) / (float)numAngles;
    int idx = (int)std::floor((theta / slice) + 0.5f) % numAngles;
    return (idx >= 0 && idx < numAngles) ? idx : 0;
}

SFLODOrthogonalFace SFLODSystem::GetDominantFace(
    const GameObject& obj,
    const glm::mat4& model,
    const glm::vec3& cameraPos,
    glm::vec3* outLocalDir) const
{
    glm::vec3 bMin = obj.mesh.aabbValid ? obj.mesh.aabbMin : glm::vec3(-0.5f);
    glm::vec3 bMax = obj.mesh.aabbValid ? obj.mesh.aabbMax : glm::vec3(0.5f);
    glm::vec3 localCenter = (bMin + bMax) * 0.5f;
    glm::vec3 worldCenter = glm::vec3(model * glm::vec4(localCenter, 1.0f));

    glm::vec3 toCam = cameraPos - worldCenter;
    float dist = glm::length(toCam);
    if (dist < 1e-5f) {
        toCam = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        toCam /= dist;
    }

    float scaleX = glm::length(glm::vec3(model[0]));
    float scaleY = glm::length(glm::vec3(model[1]));
    float scaleZ = glm::length(glm::vec3(model[2]));
    glm::vec3 axisX = (scaleX > 1e-4f) ? (glm::vec3(model[0]) / scaleX) : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 axisY = (scaleY > 1e-4f) ? (glm::vec3(model[1]) / scaleY) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 axisZ = (scaleZ > 1e-4f) ? (glm::vec3(model[2]) / scaleZ) : glm::vec3(0.0f, 0.0f, 1.0f);

    glm::vec3 localDir(
        glm::dot(toCam, axisX),
        glm::dot(toCam, axisY),
        glm::dot(toCam, axisZ)
    );

    if (outLocalDir) {
        *outLocalDir = localDir;
    }

    float absX = std::abs(localDir.x);
    float absY = std::abs(localDir.y);
    float absZ = std::abs(localDir.z);

    if (absZ >= absX && absZ >= absY) {
        return (localDir.z >= 0.0f) ? SFLODOrthogonalFace::Front : SFLODOrthogonalFace::Back;
    } else if (absX >= absZ && absX >= absY) {
        return (localDir.x >= 0.0f) ? SFLODOrthogonalFace::Right : SFLODOrthogonalFace::Left;
    } else {
        return (localDir.y >= 0.0f) ? SFLODOrthogonalFace::Top : SFLODOrthogonalFace::Bottom;
    }
}

SFLODAtlasTileRect SFLODSystem::GetAtlasTileUV(SFLODOrthogonalFace face) {
    int faceIdx = static_cast<int>(face);
    int col = faceIdx % 3;
    int row = faceIdx / 3;

    SFLODAtlasTileRect rect;
    rect.minUV = glm::vec2((float)col / 3.0f, (float)row / 2.0f);
    rect.maxUV = glm::vec2((float)(col + 1) / 3.0f, (float)(row + 1) / 2.0f);
    return rect;
}

bool SFLODSystem::RasterizeFace(
    const GameObject& obj,
    const glm::mat4& model,
    SFLODOrthogonalFace face,
    int tileResolution,
    std::vector<uint8_t>& outTileRgba,
    float cardW,
    float cardH,
    std::vector<uint8_t>* outTileNormal,
    std::vector<uint8_t>* outTileRMA)
{
    if (obj.mesh.vertices.empty() || obj.mesh.indices.empty() || tileResolution <= 0) {
        return false;
    }

    int S = tileResolution;
    outTileRgba.assign(S * S * 4, 0); // 100% transparent black background
    if (outTileNormal) {
        // Tangent-space default normal color (128, 128, 255) with 0 alpha
        outTileNormal->assign(S * S * 4, 0);
    }
    if (outTileRMA) {
        outTileRMA->assign(S * S * 4, 0);
    }

    std::vector<float> depthBuffer(S * S, -1e9f);

    // Extract axes and center
    glm::vec3 bMin = obj.mesh.aabbValid ? obj.mesh.aabbMin : glm::vec3(-0.5f);
    glm::vec3 bMax = obj.mesh.aabbValid ? obj.mesh.aabbMax : glm::vec3(0.5f);
    glm::vec3 localCenter = (bMin + bMax) * 0.5f;
    glm::vec3 worldCenter = glm::vec3(model * glm::vec4(localCenter, 1.0f));

    float scaleX = glm::length(glm::vec3(model[0]));
    float scaleY = glm::length(glm::vec3(model[1]));
    float scaleZ = glm::length(glm::vec3(model[2]));
    glm::vec3 axisX = (scaleX > 1e-4f) ? (glm::vec3(model[0]) / scaleX) : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 axisY = (scaleY > 1e-4f) ? (glm::vec3(model[1]) / scaleY) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 axisZ = (scaleZ > 1e-4f) ? (glm::vec3(model[2]) / scaleZ) : glm::vec3(0.0f, 0.0f, 1.0f);

    glm::vec3 norm(0.0f), right(0.0f), up(0.0f);
    float fWidth = cardW;
    float fHeight = cardH;

    switch (face) {
    case SFLODOrthogonalFace::Front: // +Z
        norm = axisZ; right = axisX; up = axisY;
        fWidth = cardW; fHeight = cardH;
        break;
    case SFLODOrthogonalFace::Back: // -Z
        norm = -axisZ; right = -axisX; up = axisY;
        fWidth = cardW; fHeight = cardH;
        break;
    case SFLODOrthogonalFace::Right: // +X
        norm = axisX; right = -axisZ; up = axisY;
        fWidth = cardW; fHeight = cardH;
        break;
    case SFLODOrthogonalFace::Left: // -X
        norm = -axisX; right = axisZ; up = axisY;
        fWidth = cardW; fHeight = cardH;
        break;
    case SFLODOrthogonalFace::Top: // +Y
        norm = axisY; right = axisX; up = -axisZ;
        fWidth = cardW; fHeight = cardW;
        break;
    case SFLODOrthogonalFace::Bottom: // -Y
        norm = -axisY; right = axisX; up = axisZ;
        fWidth = cardW; fHeight = cardW;
        break;
    }

    if (fWidth < 0.01f) fWidth = 1.0f;
    if (fHeight < 0.01f) fHeight = 1.0f;

    // Fetch base color and optional textures
    CachedTexture* baseTex = nullptr;
    if (!obj.baseColorTexture.empty() && obj.baseColorTexture != "none") {
        baseTex = TextureManager::Get().GetTexture(obj.baseColorTexture, TextureUsageSlot::BaseColor);
    }
    CachedTexture* normTex = nullptr;
    if (outTileNormal && !obj.normalTexture.empty() && obj.normalTexture != "none") {
        normTex = TextureManager::Get().GetTexture(obj.normalTexture, TextureUsageSlot::Normal);
    }

    const auto& vertices = obj.mesh.vertices;
    const auto& indices = obj.mesh.indices;

    glm::mat3 m3(model);
    glm::mat3 normalMatrix = glm::transpose(glm::inverse(m3));

    struct ProjectedVertex {
        float px = 0.0f;
        float py = 0.0f;
        float z = 0.0f;
        glm::vec2 uv{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
    };

    std::vector<ProjectedVertex> projVerts(vertices.size());
    for (size_t vi = 0; vi < vertices.size(); ++vi) {
        const auto& mv = vertices[vi];
        glm::vec4 wPos = model * glm::vec4(mv.pos, 1.0f);
        glm::vec3 delta = glm::vec3(wPos) - worldCenter;

        float xPlane = glm::dot(delta, right);
        float yPlane = glm::dot(delta, up);
        float zDepth = glm::dot(delta, norm);

        float uCard = (xPlane / fWidth) + 0.5f;
        float vCard = 0.5f - (yPlane / fHeight);

        projVerts[vi].px = uCard * (float)(S - 1);
        projVerts[vi].py = vCard * (float)(S - 1);
        projVerts[vi].z = zDepth;
        projVerts[vi].uv = mv.uv;
        projVerts[vi].normal = glm::normalize(normalMatrix * mv.normal);
    }

    float clipVal = (obj.opacityMaskClipValue > 0.0f) ? obj.opacityMaskClipValue : 0.333f;

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;

        const auto& p0 = projVerts[i0];
        const auto& p1 = projVerts[i1];
        const auto& p2 = projVerts[i2];

        float minX = std::min({ p0.px, p1.px, p2.px });
        float maxX = std::max({ p0.px, p1.px, p2.px });
        float minY = std::min({ p0.py, p1.py, p2.py });
        float maxY = std::max({ p0.py, p1.py, p2.py });

        int startX = std::max(0, (int)std::floor(minX));
        int endX   = std::min(S - 1, (int)std::ceil(maxX));
        int startY = std::max(0, (int)std::floor(minY));
        int endY   = std::min(S - 1, (int)std::ceil(maxY));

        if (startX > endX || startY > endY) continue;

        float det = (p1.py - p2.py) * (p0.px - p2.px) + (p2.px - p1.px) * (p0.py - p2.py);
        if (std::abs(det) < 1e-6f) continue;
        float invDet = 1.0f / det;

        for (int y = startY; y <= endY; ++y) {
            float py = (float)y;
            for (int x = startX; x <= endX; ++x) {
                float px = (float)x;

                float w0 = ((p1.py - p2.py) * (px - p2.px) + (p2.px - p1.px) * (py - p2.py)) * invDet;
                float w1 = ((p2.py - p0.py) * (px - p2.px) + (p0.px - p2.px) * (py - p2.py)) * invDet;
                float w2 = 1.0f - w0 - w1;

                if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;

                float z = w0 * p0.z + w1 * p1.z + w2 * p2.z;
                int pIdx = y * S + x;

                if (z <= depthBuffer[pIdx]) continue;

                glm::vec2 uv = w0 * p0.uv + w1 * p1.uv + w2 * p2.uv;
                glm::vec4 sampleColor(1.0f);
                if (baseTex) {
                    sampleColor = baseTex->Sample(uv.x, uv.y);
                }
                sampleColor.r *= obj.color.r;
                sampleColor.g *= obj.color.g;
                sampleColor.b *= obj.color.b;

                if (sampleColor.a < clipVal) continue;

                depthBuffer[pIdx] = z;
                int bIdx = pIdx * 4;
                outTileRgba[bIdx + 0] = (uint8_t)glm::clamp(sampleColor.r * 255.0f, 0.0f, 255.0f);
                outTileRgba[bIdx + 1] = (uint8_t)glm::clamp(sampleColor.g * 255.0f, 0.0f, 255.0f);
                outTileRgba[bIdx + 2] = (uint8_t)glm::clamp(sampleColor.b * 255.0f, 0.0f, 255.0f);
                outTileRgba[bIdx + 3] = (uint8_t)glm::clamp(sampleColor.a * 255.0f, 0.0f, 255.0f);

                if (outTileNormal) {
                    glm::vec3 interpNorm = glm::normalize(w0 * p0.normal + w1 * p1.normal + w2 * p2.normal);
                    glm::vec3 nColor = interpNorm * 0.5f + 0.5f;
                    (*outTileNormal)[bIdx + 0] = (uint8_t)glm::clamp(nColor.x * 255.0f, 0.0f, 255.0f);
                    (*outTileNormal)[bIdx + 1] = (uint8_t)glm::clamp(nColor.y * 255.0f, 0.0f, 255.0f);
                    (*outTileNormal)[bIdx + 2] = (uint8_t)glm::clamp(nColor.z * 255.0f, 0.0f, 255.0f);
                    (*outTileNormal)[bIdx + 3] = (uint8_t)glm::clamp(sampleColor.a * 255.0f, 0.0f, 255.0f);
                }
                if (outTileRMA) {
                    (*outTileRMA)[bIdx + 0] = (uint8_t)glm::clamp(obj.roughness * 255.0f, 0.0f, 255.0f);
                    (*outTileRMA)[bIdx + 1] = (uint8_t)glm::clamp(obj.metallic * 255.0f, 0.0f, 255.0f);
                    (*outTileRMA)[bIdx + 2] = 255; // 1.0 AO default
                    (*outTileRMA)[bIdx + 3] = (uint8_t)glm::clamp(sampleColor.a * 255.0f, 0.0f, 255.0f);
                }
            }
        }
    }

    return true;
}

bool SFLODSystem::RasterizeImposterTexture(
    const GameObject& obj,
    const glm::mat4& model,
    const glm::vec3& cameraPos,
    int resolution,
    std::vector<uint8_t>& outRgbaBuffer,
    float& outWidth,
    float& outHeight)
{
    if (obj.mesh.vertices.empty() || obj.mesh.indices.empty() || resolution <= 0) {
        return false;
    }

    SFLODCardGeometry geom;
    ComputeCardGeometry(obj, model, cameraPos, geom);

    float cardW = geom.halfWidth * 2.0f;
    float cardH = geom.halfHeight * 2.0f;
    if (cardW < 0.01f) cardW = 1.0f;
    if (cardH < 0.01f) cardH = 1.0f;
    outWidth = cardW;
    outHeight = cardH;

    int S = resolution;
    outRgbaBuffer.assign(S * S * 4, 0);

    std::vector<float> depthBuffer(S * S, -1e9f);

    CachedTexture* baseTex = nullptr;
    if (!obj.baseColorTexture.empty() && obj.baseColorTexture != "none") {
        baseTex = TextureManager::Get().GetTexture(obj.baseColorTexture, TextureUsageSlot::BaseColor);
    }

    const auto& vertices = obj.mesh.vertices;
    const auto& indices = obj.mesh.indices;

    struct ProjectedVertex {
        float px = 0.0f;
        float py = 0.0f;
        float z = 0.0f;
        glm::vec2 uv{0.0f};
    };

    std::vector<ProjectedVertex> projVerts(vertices.size());
    for (size_t vi = 0; vi < vertices.size(); ++vi) {
        const auto& mv = vertices[vi];
        glm::vec4 wPos = model * glm::vec4(mv.pos, 1.0f);
        glm::vec3 delta = glm::vec3(wPos) - geom.center;

        float xPlane = glm::dot(delta, geom.right);
        float yPlane = glm::dot(delta, geom.up);
        float zDepth = glm::dot(delta, geom.normal);

        float uCard = (xPlane / cardW) + 0.5f;
        float vCard = 0.5f - (yPlane / cardH);

        projVerts[vi].px = uCard * (float)(S - 1);
        projVerts[vi].py = vCard * (float)(S - 1);
        projVerts[vi].z = zDepth;
        projVerts[vi].uv = mv.uv;
    }

    float clipVal = (obj.opacityMaskClipValue > 0.0f) ? obj.opacityMaskClipValue : 0.333f;

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;

        const auto& p0 = projVerts[i0];
        const auto& p1 = projVerts[i1];
        const auto& p2 = projVerts[i2];

        float minX = std::min({ p0.px, p1.px, p2.px });
        float maxX = std::max({ p0.px, p1.px, p2.px });
        float minY = std::min({ p0.py, p1.py, p2.py });
        float maxY = std::max({ p0.py, p1.py, p2.py });

        int startX = std::max(0, (int)std::floor(minX));
        int endX   = std::min(S - 1, (int)std::ceil(maxX));
        int startY = std::max(0, (int)std::floor(minY));
        int endY   = std::min(S - 1, (int)std::ceil(maxY));

        if (startX > endX || startY > endY) continue;

        float det = (p1.py - p2.py) * (p0.px - p2.px) + (p2.px - p1.px) * (p0.py - p2.py);
        if (std::abs(det) < 1e-6f) continue;
        float invDet = 1.0f / det;

        for (int y = startY; y <= endY; ++y) {
            float py = (float)y;
            for (int x = startX; x <= endX; ++x) {
                float px = (float)x;

                float w0 = ((p1.py - p2.py) * (px - p2.px) + (p2.px - p1.px) * (py - p2.py)) * invDet;
                float w1 = ((p2.py - p0.py) * (px - p2.px) + (p0.px - p2.px) * (py - p2.py)) * invDet;
                float w2 = 1.0f - w0 - w1;

                if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;

                float z = w0 * p0.z + w1 * p1.z + w2 * p2.z;
                int pIdx = y * S + x;

                if (z <= depthBuffer[pIdx]) continue;

                glm::vec2 uv = w0 * p0.uv + w1 * p1.uv + w2 * p2.uv;
                glm::vec4 sampleColor(1.0f);
                if (baseTex) {
                    sampleColor = baseTex->Sample(uv.x, uv.y);
                }
                sampleColor.r *= obj.color.r;
                sampleColor.g *= obj.color.g;
                sampleColor.b *= obj.color.b;

                if (sampleColor.a < clipVal) continue;

                depthBuffer[pIdx] = z;
                int bIdx = pIdx * 4;
                outRgbaBuffer[bIdx + 0] = (uint8_t)glm::clamp(sampleColor.r * 255.0f, 0.0f, 255.0f);
                outRgbaBuffer[bIdx + 1] = (uint8_t)glm::clamp(sampleColor.g * 255.0f, 0.0f, 255.0f);
                outRgbaBuffer[bIdx + 2] = (uint8_t)glm::clamp(sampleColor.b * 255.0f, 0.0f, 255.0f);
                outRgbaBuffer[bIdx + 3] = (uint8_t)glm::clamp(sampleColor.a * 255.0f, 0.0f, 255.0f);
            }
        }
    }

    return true;
}

bool SFLODSystem::SaveTGA32(const std::string& filePath, int width, int height, const uint8_t* rgbaPixels) {
    if (!rgbaPixels || width <= 0 || height <= 0) return false;

    std::filesystem::path p(filePath);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    uint8_t header[18] = {
        0, 0, 2, // Uncompressed true-color image
        0, 0, 0, 0, 0,
        0, 0, 0, 0, // X and Y origin (0,0)
        (uint8_t)(width & 0xFF), (uint8_t)((width >> 8) & 0xFF),
        (uint8_t)(height & 0xFF), (uint8_t)((height >> 8) & 0xFF),
        32,      // 32 bpp
        0x28     // Descriptor: Top-to-bottom orientation (bit 5=1), 8-bit alpha (bits 0-3=8)
    };
    file.write(reinterpret_cast<const char*>(header), 18);

    // TGA stores in BGRA order
    std::vector<uint8_t> bgra(width * height * 4);
    for (int i = 0; i < width * height; ++i) {
        bgra[i * 4 + 0] = rgbaPixels[i * 4 + 2]; // B
        bgra[i * 4 + 1] = rgbaPixels[i * 4 + 1]; // G
        bgra[i * 4 + 2] = rgbaPixels[i * 4 + 0]; // R
        bgra[i * 4 + 3] = rgbaPixels[i * 4 + 3]; // A
    }
    file.write(reinterpret_cast<const char*>(bgra.data()), bgra.size());
    return file.good();
}

std::string SFLODSystem::BakeAndSaveImposterTexture(
    GameObject& obj,
    const glm::mat4& model,
    const glm::vec3& cameraPos,
    const std::filesystem::path& contentDir,
    int viewAngleIndex,
    int resolution)
{
    glm::vec3 bakeCamPos = cameraPos;
    glm::vec3 worldPos = glm::vec3(model[3]);

    if (viewAngleIndex >= 0) {
        float angleRad = (float)viewAngleIndex * (2.0f * 3.1415926535f / 8.0f);
        glm::vec3 angleDir(std::sin(angleRad), 0.25f, std::cos(angleRad));
        bakeCamPos = worldPos + glm::normalize(angleDir) * 10.0f;
    }

    std::vector<uint8_t> rgba;
    float cardW = 1.0f, cardH = 1.0f;
    if (!RasterizeImposterTexture(obj, model, bakeCamPos, resolution, rgba, cardW, cardH)) {
        return "";
    }

    std::filesystem::path impostersDir = contentDir / "Imposters";
    std::string safeName = CleanFilename(obj.name);

    std::string filename;
    if (viewAngleIndex >= 0) {
        filename = "SFLOD_" + safeName + "_angle" + std::to_string(viewAngleIndex) + ".tga";
    } else {
        filename = "SFLOD_" + safeName + "_view.tga";
    }

    std::filesystem::path fullPath = impostersDir / filename;
    std::string fullPathStr = fullPath.generic_string();

    if (!SaveTGA32(fullPathStr, resolution, resolution, rgba.data())) {
        std::cerr << "[SFLOD] Failed to save imposter texture to " << fullPathStr << "\n";
        return "";
    }

    TextureManager::Get().InvalidateTexture(fullPathStr);
    TextureManager::Get().GetTexture(fullPathStr, TextureUsageSlot::BaseColor);

    obj.sflodCustomTexture = fullPathStr;
    obj.sflodIsAtlas = false;
    if (obj.sflodPlaneWidth <= 0.001f || obj.sflodPlaneHeight <= 0.001f) {
        obj.sflodPlaneWidth = cardW;
        obj.sflodPlaneHeight = cardH;
    }

    std::cout << "[SFLOD] Successfully baked imposter texture: " << fullPathStr
              << " (" << resolution << "x" << resolution << ")\n";
    return fullPathStr;
}

bool SFLODSystem::BakeAll8Angles(
    GameObject& obj,
    const glm::mat4& model,
    const std::filesystem::path& contentDir,
    int resolution)
{
    std::string firstTexture = "";
    for (int a = 0; a < 8; ++a) {
        std::string resPath = BakeAndSaveImposterTexture(obj, model, glm::vec3(0.0f), contentDir, a, resolution);
        if (a == 0 && !resPath.empty()) {
            firstTexture = resPath;
        }
    }
    if (!firstTexture.empty()) {
        obj.sflodCustomTexture = firstTexture;
        obj.sflodIsAtlas = false;
        return true;
    }
    return false;
}

std::string SFLODSystem::BakeNachoTimeAtlas(
    GameObject& obj,
    const glm::mat4& model,
    const std::filesystem::path& contentDir,
    int tileResolution,
    SFLODShaderComplexity complexity)
{
    if (obj.mesh.vertices.empty() || obj.mesh.indices.empty()) {
        return "";
    }

    // Determine dimensions from mesh AABB
    glm::vec3 bMin = obj.mesh.aabbValid ? obj.mesh.aabbMin : glm::vec3(-0.5f);
    glm::vec3 bMax = obj.mesh.aabbValid ? obj.mesh.aabbMax : glm::vec3(0.5f);

    float scaleX = glm::length(glm::vec3(model[0]));
    float scaleY = glm::length(glm::vec3(model[1]));
    float scaleZ = glm::length(glm::vec3(model[2]));

    float extentX = (bMax.x - bMin.x) * (scaleX > 1e-4f ? scaleX : 1.0f);
    float extentY = (bMax.y - bMin.y) * (scaleY > 1e-4f ? scaleY : 1.0f);
    float extentZ = (bMax.z - bMin.z) * (scaleZ > 1e-4f ? scaleZ : 1.0f);

    float cardW = (obj.sflodPlaneWidth > 0.001f) ? obj.sflodPlaneWidth : std::max(extentX, extentZ);
    float cardH = (obj.sflodPlaneHeight > 0.001f) ? obj.sflodPlaneHeight : extentY;
    if (cardW < 0.1f) cardW = 1.0f;
    if (cardH < 0.1f) cardH = 1.0f;

    int S = tileResolution > 0 ? tileResolution : 512;
    int atlasW = S * 3;
    int atlasH = S * 2;

    std::vector<uint8_t> atlasRgba(atlasW * atlasH * 4, 0);

    bool needNormal = (complexity == SFLODShaderComplexity::Medium || complexity == SFLODShaderComplexity::High);
    bool needRMA = (complexity == SFLODShaderComplexity::High);

    std::vector<uint8_t> atlasNormal;
    if (needNormal) {
        atlasNormal.assign(atlasW * atlasH * 4, 0);
    }
    std::vector<uint8_t> atlasRMA;
    if (needRMA) {
        atlasRMA.assign(atlasW * atlasH * 4, 0);
    }

    // Bake each of the 6 orthogonal faces
    for (int f = 0; f < 6; ++f) {
        SFLODOrthogonalFace face = static_cast<SFLODOrthogonalFace>(f);
        std::vector<uint8_t> tileRgba;
        std::vector<uint8_t> tileNorm;
        std::vector<uint8_t> tileRMA;

        RasterizeFace(obj, model, face, S, tileRgba, cardW, cardH,
            needNormal ? &tileNorm : nullptr,
            needRMA ? &tileRMA : nullptr);

        int col = f % 3;
        int row = f / 3;
        int startX = col * S;
        int startY = row * S;

        for (int ty = 0; ty < S; ++ty) {
            int dstY = startY + ty;
            for (int tx = 0; tx < S; ++tx) {
                int dstX = startX + tx;
                int srcIdx = (ty * S + tx) * 4;
                int dstIdx = (dstY * atlasW + dstX) * 4;

                atlasRgba[dstIdx + 0] = tileRgba[srcIdx + 0];
                atlasRgba[dstIdx + 1] = tileRgba[srcIdx + 1];
                atlasRgba[dstIdx + 2] = tileRgba[srcIdx + 2];
                atlasRgba[dstIdx + 3] = tileRgba[srcIdx + 3];

                if (needNormal && !tileNorm.empty()) {
                    atlasNormal[dstIdx + 0] = tileNorm[srcIdx + 0];
                    atlasNormal[dstIdx + 1] = tileNorm[srcIdx + 1];
                    atlasNormal[dstIdx + 2] = tileNorm[srcIdx + 2];
                    atlasNormal[dstIdx + 3] = tileNorm[srcIdx + 3];
                }
                if (needRMA && !tileRMA.empty()) {
                    atlasRMA[dstIdx + 0] = tileRMA[srcIdx + 0];
                    atlasRMA[dstIdx + 1] = tileRMA[srcIdx + 1];
                    atlasRMA[dstIdx + 2] = tileRMA[srcIdx + 2];
                    atlasRMA[dstIdx + 3] = tileRMA[srcIdx + 3];
                }
            }
        }
    }

    // Save textures to Content/Imposters
    std::filesystem::path impostersDir = contentDir / "Imposters";
    std::string safeName = CleanFilename(obj.name);

    std::filesystem::path atlasPath = impostersDir / ("SFLOD_" + safeName + "_Atlas.tga");
    std::string atlasPathStr = atlasPath.generic_string();

    if (!SaveTGA32(atlasPathStr, atlasW, atlasH, atlasRgba.data())) {
        std::cerr << "[SFLOD] Failed to save Nacho Time atlas to " << atlasPathStr << "\n";
        return "";
    }

    TextureManager::Get().InvalidateTexture(atlasPathStr);
    TextureManager::Get().GetTexture(atlasPathStr, TextureUsageSlot::BaseColor);

    if (needNormal) {
        std::filesystem::path normPath = impostersDir / ("SFLOD_" + safeName + "_NormalAtlas.tga");
        std::string normPathStr = normPath.generic_string();
        if (SaveTGA32(normPathStr, atlasW, atlasH, atlasNormal.data())) {
            TextureManager::Get().InvalidateTexture(normPathStr);
            TextureManager::Get().GetTexture(normPathStr, TextureUsageSlot::Normal);
        }
    }

    if (needRMA) {
        std::filesystem::path rmaPath = impostersDir / ("SFLOD_" + safeName + "_RMAAtlas.tga");
        std::string rmaPathStr = rmaPath.generic_string();
        if (SaveTGA32(rmaPathStr, atlasW, atlasH, atlasRMA.data())) {
            TextureManager::Get().InvalidateTexture(rmaPathStr);
            TextureManager::Get().GetTexture(rmaPathStr, TextureUsageSlot::Roughness);
        }
    }

    // Update object parameters
    obj.sflodCustomTexture = atlasPathStr;
    obj.sflodIsAtlas = true;
    obj.enableSFLOD = true;
    if (obj.sflodPlaneWidth <= 0.001f || obj.sflodPlaneHeight <= 0.001f) {
        obj.sflodPlaneWidth = cardW;
        obj.sflodPlaneHeight = cardH;
    }

    std::cout << "[SFLOD] Successfully baked Nacho Time 6-way atlas: " << atlasPathStr
              << " (" << atlasW << "x" << atlasH << ")\n";
    return atlasPathStr;
}

} // namespace SFLOD
