// ============================================================================
// Eunoia Engine — Lights-Baking Plugin
// LightBakeManager.cpp: Implementation of per-light baking system
// ============================================================================

#include <LightsBaking/LightBakeManager.h>
#include <EngineScene/Scene.h>
#include <EngineScene/GameObject.h>
#include <EngineCore/UUID.h>
#include <EngineAssets/DirectXTex.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace Eunoia {

LightBakeManager& LightBakeManager::Get() {
    static LightBakeManager instance;
    return instance;
}

uint32_t LightBakeManager::GetBakedLightCount() const {
    return (uint32_t)m_bakedLights.size();
}

bool LightBakeManager::HasBakedData(const std::string& lightGuid) const {
    if (lightGuid.empty()) return false;
    return m_bakedLights.find(lightGuid) != m_bakedLights.end();
}

bool LightBakeManager::ValidateLight(const GameObject& obj, std::string& outError) const {
    if (!obj.isLight && !IsLightPrimitive(obj.type)) {
        outError = "Selected object is not a light.";
        return false;
    }

    if (obj.light.type == LightType::Sky) {
        outError = "Sky light is an ambient environment light and does not participate in direct baking.";
        return false;
    }

    int res = obj.light.bakeResolution;
    if (res != 256 && res != 512 && res != 1024 && res != 2048 && res != 4096) {
        outError = "Invalid bake resolution: " + std::to_string(res) + ". Supported: 256, 512, 1024, 2048, 4096.";
        return false;
    }

    return true;
}

bool LightBakeManager::CollectBakeGeometry(const Scene& scene, std::vector<BakeStaticMesh>& outStaticMeshes, std::string& outError) const {
    outStaticMeshes.clear();

    bool hasExplicitStatic = false;
    for (const auto& obj : scene.objects) {
        if (!obj.visible) continue;
        if (obj.isCamera || IsCameraPrimitive(obj.type)) continue;
        if (obj.isLight || IsLightPrimitive(obj.type)) continue;
        if (obj.isWaterBody || obj.type == PrimitiveType::WaterBody) continue;
        if (obj.mesh.vertices.empty() || obj.mesh.indices.empty()) continue;
        if (obj.mobility == Mobility::Static) {
            hasExplicitStatic = true;
            break;
        }
    }

    for (const auto& obj : scene.objects) {
        if (!obj.visible) continue;
        if (obj.isCamera || IsCameraPrimitive(obj.type)) continue;
        if (obj.isLight || IsLightPrimitive(obj.type)) continue;
        if (obj.isWaterBody || obj.type == PrimitiveType::WaterBody) continue;
        if (obj.mesh.vertices.empty() || obj.mesh.indices.empty()) continue;

        // If explicit static objects exist in the scene, only bake those; otherwise include all eligible geometry
        if (hasExplicitStatic && obj.mobility != Mobility::Static) continue;

        BakeStaticMesh sm;
        sm.objectId = obj.id;
        sm.name = obj.name;
        sm.primitiveType = (int)obj.type;
        sm.worldTransform = scene.GetWorldMatrix(obj);

        glm::mat3 normMat = glm::transpose(glm::inverse(glm::mat3(sm.worldTransform)));

        sm.worldVertices.reserve(obj.mesh.vertices.size());
        sm.worldNormals.reserve(obj.mesh.vertices.size());
        sm.uvs.reserve(obj.mesh.vertices.size());

        for (const auto& mv : obj.mesh.vertices) {
            glm::vec4 wp = sm.worldTransform * glm::vec4(mv.pos, 1.0f);
            glm::vec3 worldPos(wp);
            glm::vec3 worldNormal = glm::normalize(normMat * mv.normal);

            sm.worldVertices.push_back(worldPos);
            sm.worldNormals.push_back(worldNormal);
            sm.uvs.push_back(mv.uv);

            sm.aabbMin = glm::min(sm.aabbMin, worldPos);
            sm.aabbMax = glm::max(sm.aabbMax, worldPos);
        }

        sm.indices = obj.mesh.indices;
        sm.castShadows = obj.castShadows;
        outStaticMeshes.push_back(std::move(sm));
    }

    if (outStaticMeshes.empty()) {
        outError = "No valid geometry found in the scene to bake light onto.";
        return false;
    }

    return true;
}

static inline bool RayBoxIntersect(const glm::vec3& orig, const glm::vec3& dir, float maxDist, const glm::vec3& bmin, const glm::vec3& bmax) {
    float tmin = 0.0f;
    float tmax = maxDist;

    for (int i = 0; i < 3; ++i) {
        if (std::abs(dir[i]) < 1e-8f) {
            if (orig[i] < bmin[i] || orig[i] > bmax[i]) return false;
        } else {
            float ood = 1.0f / dir[i];
            float t1 = (bmin[i] - orig[i]) * ood;
            float t2 = (bmax[i] - orig[i]) * ood;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    return true;
}

static inline bool RayTriangleIntersect(
    const glm::vec3& orig, const glm::vec3& dir, float maxDist,
    const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2)
{
    const float EPSILON = 1e-6f;
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 h = glm::cross(dir, edge2);
    float a = glm::dot(edge1, h);

    // Cull backfaces relative to ray direction (matching DX12 rasterizer CULL_MODE_BACK)
    if (a <= EPSILON) return false;

    float f = 1.0f / a;
    glm::vec3 s = orig - v0;
    float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;

    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = f * glm::dot(edge2, q);
    return (t > 0.005f && t < maxDist);
}

static inline uint16_t FloatToHalf(float val) {
    uint32_t x = *reinterpret_cast<uint32_t*>(&val);
    uint32_t sign = (x >> 16) & 0x8000;
    int32_t exp = ((x >> 23) & 0xff) - 127 + 15;
    uint32_t mant = x & 0x007fffff;
    if (exp <= 0) return (uint16_t)sign;
    if (exp >= 31) return (uint16_t)(sign | 0x7c00);
    return (uint16_t)(sign | (exp << 10) | (mant >> 13));
}

static inline float HalfToFloat(uint16_t val) {
    uint32_t sign = (uint32_t)(val & 0x8000) << 16;
    uint32_t exp = (val & 0x7c00) >> 10;
    uint32_t mant = (val & 0x03ff) << 13;
    if (exp == 0) {
        if (mant == 0) {
            uint32_t res = sign;
            return *reinterpret_cast<float*>(&res);
        }
        while (!(mant & 0x00800000)) {
            mant <<= 1;
            exp -= 1;
        }
        exp += 1;
        mant &= ~0x00800000;
    } else if (exp == 31) {
        exp = 255;
    } else {
        exp = exp - 15 + 127;
    }
    uint32_t res = sign | (exp << 23) | mant;
    return *reinterpret_cast<float*>(&res);
}

void LightBakeManager::GenerateMeshLightmapUvs(
    const std::vector<BakeStaticMesh>& staticMeshes,
    RuntimeBakedLightData& outData)
{
    outData.atlasRects.clear();
    outData.meshLightmapUvs.clear();

    size_t meshCount = staticMeshes.size();
    if (meshCount == 0) return;

    int cols = (int)std::ceil(std::sqrt((double)meshCount));
    if (cols < 1) cols = 1;
    int rows = (int)std::ceil((double)meshCount / (double)cols);
    if (rows < 1) rows = 1;

    float cellW = 1.0f / (float)cols;
    float cellH = 1.0f / (float)rows;
    float margin = 0.002f;

    for (size_t k = 0; k < meshCount; ++k) {
        const auto& sm = staticMeshes[k];
        int col = (int)(k % cols);
        int row = (int)(k / cols);

        MeshAtlasRect rect;
        rect.objectId = sm.objectId;
        rect.uMin = col * cellW + margin;
        rect.vMin = row * cellH + margin;
        rect.uMax = (col + 1) * cellW - margin;
        rect.vMax = (row + 1) * cellH - margin;
        outData.atlasRects.push_back(rect);

        size_t vertCount = sm.worldVertices.size();
        if (vertCount == 0) continue;

        // Disjoint Set Union (DSU) to group connected coplanar vertices into non-overlapping chart islands
        std::vector<uint32_t> dsu(vertCount);
        for (uint32_t i = 0; i < vertCount; ++i) dsu[i] = i;

        auto Find = [&](uint32_t i) -> uint32_t {
            uint32_t root = i;
            while (root != dsu[root]) root = dsu[root];
            uint32_t curr = i;
            while (curr != root) {
                uint32_t next = dsu[curr];
                dsu[curr] = root;
                curr = next;
            }
            return root;
        };

        auto Union = [&](uint32_t i, uint32_t j) {
            uint32_t ri = Find(i);
            uint32_t rj = Find(j);
            if (ri != rj) dsu[ri] = rj;
        };

        for (size_t tri = 0; tri + 2 < sm.indices.size(); tri += 3) {
            uint32_t i0 = sm.indices[tri];
            uint32_t i1 = sm.indices[tri + 1];
            uint32_t i2 = sm.indices[tri + 2];
            if (i0 < vertCount && i1 < vertCount && i2 < vertCount) {
                Union(i0, i1);
                Union(i1, i2);
            }
        }

        std::unordered_map<uint32_t, std::vector<uint32_t>> islandMap;
        for (uint32_t vi = 0; vi < vertCount; ++vi) {
            islandMap[Find(vi)].push_back(vi);
        }

        struct IslandInfo {
            std::vector<uint32_t> vlist;
            std::vector<float> pu;
            std::vector<float> pv;
            float uMin = 0.0f;
            float vMin = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
            float packX = 0.0f;
            float packY = 0.0f;
        };

        std::vector<IslandInfo> islands;
        islands.reserve(islandMap.size());
        float totalArea = 0.0f;
        float maxIslandW = 0.0f;

        for (auto& kv : islandMap) {
            IslandInfo isl;
            isl.vlist = std::move(kv.second);
            size_t n = isl.vlist.size();
            isl.pu.resize(n);
            isl.pv.resize(n);

            glm::vec3 avgN(0.0f);
            for (uint32_t vi : isl.vlist) {
                if (vi < sm.worldNormals.size()) avgN += sm.worldNormals[vi];
            }
            float nLen = glm::length(avgN);
            if (nLen > 1e-4f) avgN /= nLen;
            else avgN = glm::vec3(0, 1, 0);

            glm::vec3 up = (std::abs(avgN.y) > 0.7f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            glm::vec3 tangent = glm::cross(avgN, up);
            float tLen = glm::length(tangent);
            if (tLen > 1e-4f) tangent /= tLen;
            else tangent = glm::vec3(1, 0, 0);
            glm::vec3 bitangent = glm::cross(avgN, tangent);

            glm::vec3 origin = sm.worldVertices[isl.vlist[0]];
            float minU = 1e9f, maxU = -1e9f;
            float minV = 1e9f, maxV = -1e9f;

            for (size_t i = 0; i < n; ++i) {
                uint32_t vi = isl.vlist[i];
                glm::vec3 dP = sm.worldVertices[vi] - origin;
                float u = glm::dot(dP, tangent);
                float v = glm::dot(dP, bitangent);
                isl.pu[i] = u;
                isl.pv[i] = v;
                minU = std::min(minU, u); maxU = std::max(maxU, u);
                minV = std::min(minV, v); maxV = std::max(maxV, v);
            }

            isl.uMin = minU;
            isl.vMin = minV;
            isl.w = std::max(maxU - minU, 0.001f);
            isl.h = std::max(maxV - minV, 0.001f);

            totalArea += isl.w * isl.h;
            maxIslandW = std::max(maxIslandW, isl.w);
            islands.push_back(std::move(isl));
        }

        // Sort islands by height descending for optimal shelf packing
        std::sort(islands.begin(), islands.end(), [](const IslandInfo& a, const IslandInfo& b) {
            return a.h > b.h;
        });

        float targetPackW = std::max(std::sqrt(totalArea * 1.3f), maxIslandW);
        if (targetPackW < 0.001f) targetPackW = 0.001f;

        float currX = 0.0f;
        float currY = 0.0f;
        float currShelfH = 0.0f;

        for (auto& isl : islands) {
            float pad = 0.01f * std::max(isl.w, isl.h);
            float itemW = isl.w + 2.0f * pad;
            float itemH = isl.h + 2.0f * pad;

            if (currX + itemW > targetPackW && currX > 0.0f) {
                currY += currShelfH;
                currX = 0.0f;
                currShelfH = 0.0f;
            }

            isl.packX = currX + pad;
            isl.packY = currY + pad;
            currX += itemW;
            currShelfH = std::max(currShelfH, itemH);
        }

        float totalPackH = std::max(currY + currShelfH, 0.001f);

        std::vector<glm::vec2> atlasUvs(vertCount);
        float rectW = rect.uMax - rect.uMin;
        float rectH = rect.vMax - rect.vMin;

        for (const auto& isl : islands) {
            for (size_t i = 0; i < isl.vlist.size(); ++i) {
                uint32_t vi = isl.vlist[i];
                float uNorm = (isl.pu[i] - isl.uMin) / isl.w;
                float vNorm = (isl.pv[i] - isl.vMin) / isl.h;
                float localU = (isl.packX + uNorm * isl.w) / targetPackW;
                float localV = (isl.packY + vNorm * isl.h) / totalPackH;

                float au = rect.uMin + localU * rectW;
                float av = rect.vMin + localV * rectH;
                atlasUvs[vi] = glm::vec2(au, av);
            }
        }

        outData.meshLightmapUvs[sm.objectId] = std::move(atlasUvs);
    }
}

void LightBakeManager::RaycastShadowsAndIllumination(
    const Scene& scene,
    const GameObject& lightObj,
    const std::vector<BakeStaticMesh>& staticMeshes,
    RuntimeBakedLightData& outData,
    std::vector<glm::vec3>& outLightMapHdr,
    std::vector<float>& outShadowMapHdr,
    uint32_t resolution)
{
    glm::mat4 lightWorldMat = scene.GetWorldMatrix(lightObj);
    glm::vec3 lightPos = scene.GetWorldPosition(lightObj);
    glm::vec3 localBaseDir = (lightObj.light.type == LightType::Directional) ? glm::vec3(0.0f, -1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 unnormDir = glm::vec3(lightWorldMat * glm::vec4(localBaseDir, 0.0f));
    float dirLen = glm::length(unnormDir);
    glm::vec3 lightDir = (dirLen > 0.0001f) ? (unnormDir / dirLen) : localBaseDir;

    glm::vec3 lightColor = lightObj.light.useTemperature ? (lightObj.light.color * ColorTemperatureToRGB(lightObj.light.temperature)) : lightObj.light.color;
    float lightIntensity = lightObj.light.intensity;
    float lightRange = std::max(lightObj.light.range, 0.1f);
    float lightAtten = lightObj.light.attenuation;
    float shadowStrength = std::clamp(lightObj.light.shadowStrength, 0.0f, 1.0f);
    bool castShadows = lightObj.light.castShadows;
    LightType lType = lightObj.light.type;

    float cosInner = 1.0f, cosOuter = 0.0f;
    if (lType == LightType::Spot) {
        cosInner = std::cos(glm::radians(std::min(lightObj.light.innerConeAngle, lightObj.light.outerConeAngle)));
        cosOuter = std::cos(glm::radians(std::max(lightObj.light.innerConeAngle, lightObj.light.outerConeAngle)));
    }

    outData.atlasRects.clear();
    outData.meshLightmapUvs.clear();
    outData.staticMeshVertexLighting.clear();

    size_t meshCount = staticMeshes.size();
    if (meshCount == 0) return;

    // 1. Generate dedicated Lightmap UV atlas coordinates for all static meshes
    GenerateMeshLightmapUvs(staticMeshes, outData);

    // 2. Rasterize triangles into LightMap and ShadowMap
    uint32_t W = resolution;
    uint32_t H = resolution;
    outLightMapHdr.assign(W * H, glm::vec3(0.0f));
    outShadowMapHdr.assign(W * H, 1.0f);
    std::vector<bool> coverage(W * H, false);

    for (size_t k = 0; k < meshCount; ++k) {
        const auto& sm = staticMeshes[k];
        const auto& atlasUvs = outData.meshLightmapUvs[sm.objectId];
        if (atlasUvs.size() != sm.worldVertices.size()) continue;

        for (size_t tri = 0; tri + 2 < sm.indices.size(); tri += 3) {
            uint32_t i0 = sm.indices[tri];
            uint32_t i1 = sm.indices[tri + 1];
            uint32_t i2 = sm.indices[tri + 2];
            if (i0 >= sm.worldVertices.size() || i1 >= sm.worldVertices.size() || i2 >= sm.worldVertices.size()) continue;

            const glm::vec3& P0 = sm.worldVertices[i0];
            const glm::vec3& P1 = sm.worldVertices[i1];
            const glm::vec3& P2 = sm.worldVertices[i2];

            const glm::vec3& N0 = sm.worldNormals[i0];
            const glm::vec3& N1 = sm.worldNormals[i1];
            const glm::vec3& N2 = sm.worldNormals[i2];

            glm::vec2 p0 = atlasUvs[i0] * glm::vec2((float)W, (float)H);
            glm::vec2 p1 = atlasUvs[i1] * glm::vec2((float)W, (float)H);
            glm::vec2 p2 = atlasUvs[i2] * glm::vec2((float)W, (float)H);

            float denom = (p1.y - p2.y) * (p0.x - p2.x) + (p2.x - p1.x) * (p0.y - p2.y);
            if (std::abs(denom) < 1e-6f) continue;
            float invDenom = 1.0f / denom;

            int minX = std::max(0, (int)std::floor(std::min({p0.x, p1.x, p2.x})));
            int maxX = std::min((int)W - 1, (int)std::ceil(std::max({p0.x, p1.x, p2.x})));
            int minY = std::max(0, (int)std::floor(std::min({p0.y, p1.y, p2.y})));
            int maxY = std::min((int)H - 1, (int)std::ceil(std::max({p0.y, p1.y, p2.y})));

            for (int y = minY; y <= maxY; ++y) {
                float py = (float)y + 0.5f;
                for (int x = minX; x <= maxX; ++x) {
                    float px = (float)x + 0.5f;

                    float w0 = ((p1.y - p2.y) * (px - p2.x) + (p2.x - p1.x) * (py - p2.y)) * invDenom;
                    float w1 = ((p2.y - p0.y) * (px - p2.x) + (p0.x - p2.x) * (py - p2.y)) * invDenom;
                    float w2 = 1.0f - w0 - w1;

                    if (w0 < -0.01f || w1 < -0.01f || w2 < -0.01f) continue;

                    glm::vec3 P = w0 * P0 + w1 * P1 + w2 * P2;
                    glm::vec3 N = glm::normalize(w0 * N0 + w1 * N1 + w2 * N2);

                    glm::vec3 L;
                    float distAtten = 1.0f;
                    float maxDist = 1000.0f;

                    if (lType == LightType::Directional) {
                        glm::vec3 dir = lightDir;
                        float dLen = glm::length(dir);
                        L = (dLen > 1e-4f) ? (-dir / dLen) : glm::vec3(0.0f, 1.0f, 0.0f);
                        distAtten = 1.0f;
                        maxDist = 1000.0f;
                    } else {
                        glm::vec3 toLight = lightPos - P;
                        float dist = glm::length(toLight);
                        if (dist >= lightRange || dist <= 1e-4f) continue;
                        L = toLight / dist;

                        float normDist = dist / lightRange;
                        float da = std::clamp(1.0f - normDist, 0.0f, 1.0f);
                        float attenExp = std::max(lightAtten, 0.2f);
                        distAtten = std::pow(da, attenExp) / (dist * dist + 1.0f);
                        maxDist = dist - 0.02f;
                    }

                    uint32_t pIdx = y * W + x;
                    coverage[pIdx] = true;

                    float NdotL = std::max(glm::dot(N, L), 0.0f);
                    if (NdotL <= 1e-5f) continue;

                    float spotAngleAtten = 1.0f;
                    if (lType == LightType::Spot) {
                        float cosDir = glm::dot(lightDir, -L);
                        spotAngleAtten = std::clamp((cosDir - cosOuter) / std::max(cosInner - cosOuter, 0.001f), 0.0f, 1.0f);
                        spotAngleAtten = spotAngleAtten * spotAngleAtten;
                    }
                    if (distAtten * spotAngleAtten <= 1e-5f) continue;

                    float shadowFactor = 1.0f;
                    if (castShadows) {
                        glm::vec3 rayOrig = P + N * std::max(lightObj.light.shadowBias * 0.5f, 0.002f);
                        bool occluded = false;

                        for (const auto& occMesh : staticMeshes) {
                            if (!occMesh.castShadows) continue;
                            if (!RayBoxIntersect(rayOrig, L, maxDist, occMesh.aabbMin - glm::vec3(0.05f), occMesh.aabbMax + glm::vec3(0.05f))) continue;
                            for (size_t oTri = 0; oTri + 2 < occMesh.indices.size(); oTri += 3) {
                                uint32_t o0 = occMesh.indices[oTri];
                                uint32_t o1 = occMesh.indices[oTri + 1];
                                uint32_t o2 = occMesh.indices[oTri + 2];
                                if (RayTriangleIntersect(rayOrig, L, maxDist, occMesh.worldVertices[o0], occMesh.worldVertices[o1], occMesh.worldVertices[o2])) {
                                    occluded = true;
                                    break;
                                }
                            }
                            if (occluded) break;
                        }

                        if (occluded) {
                            shadowFactor = 1.0f - shadowStrength;
                        }
                    }

                    glm::vec3 illum = lightColor * (lightIntensity * NdotL * distAtten * spotAngleAtten * shadowFactor);
                    outLightMapHdr[pIdx] = illum;
                    outShadowMapHdr[pIdx] = shadowFactor;
                }
            }
        }
    }

    // 3. Dilation pass (4 passes, 8-connected) to prevent bilinear edge seam bleeding
    for (int pass = 0; pass < 4; ++pass) {
        auto copyLm = outLightMapHdr;
        auto copySm = outShadowMapHdr;
        auto copyCov = coverage;
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                uint32_t idx = y * W + x;
                if (!copyCov[idx]) {
                    glm::vec3 sumLm(0.0f);
                    float sumSm = 0.0f;
                    int count = 0;
                    for (int dy = -1; dy <= 1; ++dy) {
                        int ny = (int)y + dy;
                        if (ny < 0 || ny >= (int)H) continue;
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            int nx = (int)x + dx;
                            if (nx < 0 || nx >= (int)W) continue;
                            uint32_t nIdx = (uint32_t)ny * W + (uint32_t)nx;
                            if (copyCov[nIdx]) {
                                sumLm += copyLm[nIdx];
                                sumSm += copySm[nIdx];
                                count++;
                            }
                        }
                    }
                    if (count > 0) {
                        outLightMapHdr[idx] = sumLm / (float)count;
                        outShadowMapHdr[idx] = sumSm / (float)count;
                        coverage[idx] = true;
                    }
                }
            }
        }
    }

    // 4. Compute per-vertex lighting fallback for every participating static mesh
    for (const auto& sm : staticMeshes) {
        auto& vLight = outData.staticMeshVertexLighting[sm.objectId];
        vLight.resize(sm.worldVertices.size(), glm::vec3(0.0f));

        for (size_t vi = 0; vi < sm.worldVertices.size(); ++vi) {
            const glm::vec3& P = sm.worldVertices[vi];
            const glm::vec3& N = sm.worldNormals[vi];

            glm::vec3 L;
            float distAtten = 1.0f;
            float maxDist = 1000.0f;

            if (lType == LightType::Directional) {
                float dLen = glm::length(lightDir);
                L = (dLen > 1e-4f) ? (-lightDir / dLen) : glm::vec3(0.0f, 1.0f, 0.0f);
            } else {
                glm::vec3 toLight = lightPos - P;
                float dist = glm::length(toLight);
                if (dist >= lightRange || dist <= 1e-4f) continue;
                L = toLight / dist;

                float normDist = dist / lightRange;
                float da = std::clamp(1.0f - normDist, 0.0f, 1.0f);
                float attenExp = std::max(lightAtten, 0.2f);
                distAtten = std::pow(da, attenExp) / (dist * dist + 1.0f);
                maxDist = dist - 0.02f;
            }

            float NdotL = std::max(glm::dot(N, L), 0.0f);
            if (NdotL <= 1e-5f) continue;

            float spotAngleAtten = 1.0f;
            if (lType == LightType::Spot) {
                float cosDir = glm::dot(lightDir, -L);
                spotAngleAtten = std::clamp((cosDir - cosOuter) / std::max(cosInner - cosOuter, 0.001f), 0.0f, 1.0f);
                spotAngleAtten = spotAngleAtten * spotAngleAtten;
            }
            if (distAtten * spotAngleAtten <= 1e-5f) continue;

            float shadowFactor = 1.0f;
            if (castShadows) {
                glm::vec3 rayOrig = P + N * std::max(lightObj.light.shadowBias * 0.5f, 0.002f);
                bool occluded = false;

                for (const auto& occMesh : staticMeshes) {
                    if (!occMesh.castShadows) continue;
                    if (!RayBoxIntersect(rayOrig, L, maxDist, occMesh.aabbMin - glm::vec3(0.05f), occMesh.aabbMax + glm::vec3(0.05f))) continue;
                    for (size_t oTri = 0; oTri + 2 < occMesh.indices.size(); oTri += 3) {
                        uint32_t o0 = occMesh.indices[oTri];
                        uint32_t o1 = occMesh.indices[oTri + 1];
                        uint32_t o2 = occMesh.indices[oTri + 2];
                        if (RayTriangleIntersect(rayOrig, L, maxDist, occMesh.worldVertices[o0], occMesh.worldVertices[o1], occMesh.worldVertices[o2])) {
                            occluded = true;
                            break;
                        }
                    }
                    if (occluded) break;
                }

                if (occluded) {
                    shadowFactor = 1.0f - shadowStrength;
                }
            }

            vLight[vi] = lightColor * (lightIntensity * NdotL * distAtten * spotAngleAtten * shadowFactor);
        }
    }
}

uint64_t LightBakeManager::ComputeBakeSignature(const GameObject& lightObj) const {
    uint64_t hash = 14695981039346656037ULL;
    auto HashBytes = [&hash](const void* data, size_t size) {
        const uint8_t* p = (const uint8_t*)data;
        for (size_t i = 0; i < size; ++i) {
            hash ^= p[i];
            hash *= 1099511628211ULL;
        }
    };

    HashBytes(&lightObj.position, sizeof(glm::vec3));
    HashBytes(&lightObj.rotation, sizeof(glm::vec3));
    int lType = (int)lightObj.light.type;
    HashBytes(&lType, sizeof(int));
    HashBytes(&lightObj.light.intensity, sizeof(float));
    HashBytes(&lightObj.light.color, sizeof(glm::vec3));
    HashBytes(&lightObj.light.range, sizeof(float));
    HashBytes(&lightObj.light.attenuation, sizeof(float));
    HashBytes(&lightObj.light.innerConeAngle, sizeof(float));
    HashBytes(&lightObj.light.outerConeAngle, sizeof(float));
    HashBytes(&lightObj.light.shadowBias, sizeof(float));
    HashBytes(&lightObj.light.shadowStrength, sizeof(float));
    HashBytes(&lightObj.light.bakeResolution, sizeof(int));

    return hash;
}

bool LightBakeManager::IsBakeValid(const GameObject& lightObj) const {
    if (lightObj.light.bakeState != LightBakeState::Baked) return false;
    if (lightObj.light.isBakeDirty) return false;
    return (ComputeBakeSignature(lightObj) == lightObj.light.bakeSignature);
}

void LightBakeManager::CheckBakeDirty(GameObject& lightObj) const {
    if (lightObj.light.bakeState == LightBakeState::Baked) {
        if (ComputeBakeSignature(lightObj) != lightObj.light.bakeSignature) {
            lightObj.light.isBakeDirty = true;
        }
    }
}

std::filesystem::path LightBakeManager::GetCookedBakeDir(const std::filesystem::path& projectRoot, const std::string& lightGuid) const {
    std::filesystem::path root = projectRoot;
    if (root.empty()) {
        root = "C:/Projects/Eunoia-Engine/Projects/Unknown-13-Test";
    }
    return root / "Cooked" / "Lighting" / "Baked" / lightGuid;
}

bool LightBakeManager::SaveBakeAssets(
    const std::filesystem::path& bakeDir,
    const BakedLightMetadata& meta,
    const std::vector<glm::vec3>& lightMapHdr,
    const std::vector<float>& shadowMapHdr,
    const std::vector<MeshAtlasRect>& atlasRects,
    uint32_t res)
{
    std::error_code ec;
    std::filesystem::create_directories(bakeDir, ec);
    if (ec) {
        std::cerr << "[LightBakeManager] Failed to create directory: " << bakeDir.string() << " (" << ec.message() << ")\n";
        return false;
    }

    // 1. Save LightMap.dds using DirectXTex (DXGI_FORMAT_R16G16B16A16_FLOAT HDR)
    std::filesystem::path lmPath = bakeDir / "LightMap.dds";
    {
        DirectX::ScratchImage img;
        if (SUCCEEDED(img.Initialize2D(DXGI_FORMAT_R16G16B16A16_FLOAT, res, res, 1, 1))) {
            uint16_t* pixels = reinterpret_cast<uint16_t*>(img.GetPixels());
            for (size_t i = 0; i < lightMapHdr.size(); ++i) {
                const auto& col = lightMapHdr[i];
                pixels[i * 4 + 0] = FloatToHalf(col.r);
                pixels[i * 4 + 1] = FloatToHalf(col.g);
                pixels[i * 4 + 2] = FloatToHalf(col.b);
                pixels[i * 4 + 3] = FloatToHalf(1.0f);
            }
            std::string lmPathStr = lmPath.string();
            DirectX::SaveToDDSFile(img.GetImages(), img.GetImageCount(), img.GetMetadata(), lmPathStr.c_str());
        }
    }

    // 2. Save ShadowMap.dds using DirectXTex (DXGI_FORMAT_R8G8B8A8_UNORM)
    std::filesystem::path smPath = bakeDir / "ShadowMap.dds";
    {
        DirectX::ScratchImage img;
        if (SUCCEEDED(img.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, res, res, 1, 1))) {
            uint8_t* pixels = img.GetPixels();
            for (size_t i = 0; i < shadowMapHdr.size(); ++i) {
                uint8_t sVal = (uint8_t)std::clamp(shadowMapHdr[i] * 255.0f, 0.0f, 255.0f);
                pixels[i * 4 + 0] = sVal;
                pixels[i * 4 + 1] = sVal;
                pixels[i * 4 + 2] = sVal;
                pixels[i * 4 + 3] = 255;
            }
            std::string smPathStr = smPath.string();
            DirectX::SaveToDDSFile(img.GetImages(), img.GetImageCount(), img.GetMetadata(), smPathStr.c_str());
        }
    }

    // 3. Save BakeMetadata.json
    std::filesystem::path metaPath = bakeDir / "BakeMetadata.json";
    std::ofstream ofs(metaPath);
    if (ofs.is_open()) {
        ofs << "{\n";
        ofs << "  \"lightGuid\": \"" << meta.lightGuid << "\",\n";
        ofs << "  \"resolution\": " << meta.resolution << ",\n";
        ofs << "  \"lightMapAsset\": \"" << meta.lightMapAsset << "\",\n";
        ofs << "  \"shadowMapAsset\": \"" << meta.shadowMapAsset << "\",\n";
        ofs << "  \"bakedPosition\": [" << meta.bakedPosition.x << ", " << meta.bakedPosition.y << ", " << meta.bakedPosition.z << "],\n";
        ofs << "  \"bakedRotation\": [" << meta.bakedRotation.x << ", " << meta.bakedRotation.y << ", " << meta.bakedRotation.z << "],\n";
        ofs << "  \"bakedIntensity\": " << meta.bakedIntensity << ",\n";
        ofs << "  \"bakedColor\": [" << meta.bakedColor.r << ", " << meta.bakedColor.g << ", " << meta.bakedColor.b << "],\n";
        ofs << "  \"bakedRange\": " << meta.bakedRange << ",\n";
        ofs << "  \"bakedInnerCone\": " << meta.bakedInnerCone << ",\n";
        ofs << "  \"bakedOuterCone\": " << meta.bakedOuterCone << ",\n";
        ofs << "  \"bakedShadowBias\": " << meta.bakedShadowBias << ",\n";
        ofs << "  \"bakeSignature\": \"" << std::to_string(meta.bakeSignature) << "\",\n";
        ofs << "  \"bakeDate\": \"" << meta.bakeDate << "\",\n";
        ofs << "  \"meshAtlas\": [\n";
        for (size_t i = 0; i < atlasRects.size(); ++i) {
            const auto& ar = atlasRects[i];
            ofs << "    { \"objectId\": " << ar.objectId
                << ", \"uMin\": " << ar.uMin << ", \"vMin\": " << ar.vMin
                << ", \"uMax\": " << ar.uMax << ", \"vMax\": " << ar.vMax << " }"
                << (i + 1 < atlasRects.size() ? ",\n" : "\n");
        }
        ofs << "  ]\n";
        ofs << "}\n";
    }

    return true;
}

bool LightBakeManager::BakeLight(Scene& scene, GameObject& lightObj, const std::filesystem::path& projectRoot, std::string& outStatus) {
    // 0. Ensure lights and transforms are completely synchronized across the scene
    scene.SyncLightPositionsFromActors();

    std::string err;
    if (!ValidateLight(lightObj, err)) {
        outStatus = "Validation failed: " + err;
        return false;
    }

    std::vector<BakeStaticMesh> staticMeshes;
    if (!CollectBakeGeometry(scene, staticMeshes, err)) {
        outStatus = "Geometry check failed: " + err;
        return false;
    }

    // Ensure all participating objects are set to Static mobility
    for (const auto& sm : staticMeshes) {
        if (GameObject* mObj = scene.FindObject(sm.objectId)) {
            mObj->mobility = Mobility::Static;
        }
    }

    // Ensure stable light GUID
    if (lightObj.light.lightGuid.empty()) {
        lightObj.light.lightGuid = EngineCore::UUID{}.ToString();
    }

    uint32_t res = (uint32_t)lightObj.light.bakeResolution;
    std::filesystem::path bakeDir = GetCookedBakeDir(projectRoot, lightObj.light.lightGuid);

    // Delete any previous baked map files for this particular light from Cooked folder
    std::error_code ec;
    std::filesystem::path root = projectRoot.empty() ? "C:/Projects/Eunoia-Engine/Projects/Unknown-13-Test" : projectRoot;
    if (!lightObj.light.bakedLightmapPath.empty()) {
        std::filesystem::remove(root / lightObj.light.bakedLightmapPath, ec);
    }
    if (!lightObj.light.bakedShadowmapPath.empty()) {
        std::filesystem::remove(root / lightObj.light.bakedShadowmapPath, ec);
    }
    if (std::filesystem::exists(bakeDir, ec)) {
        std::filesystem::remove_all(bakeDir, ec);
    }

    outStatus = "Baking Light: " + lightObj.name + "...";
    if (m_progressCb) m_progressCb("Calculating illumination and static shadows...", 0.3f);

    RuntimeBakedLightData runtimeData;
    runtimeData.lightGuid = lightObj.light.lightGuid;
    runtimeData.resolution = res;
    runtimeData.lightMapPath = bakeDir / "LightMap.dds";
    runtimeData.shadowMapPath = bakeDir / "ShadowMap.dds";

    std::vector<glm::vec3> lmHdr;
    std::vector<float> smHdr;
    RaycastShadowsAndIllumination(scene, lightObj, staticMeshes, runtimeData, lmHdr, smHdr, res);

    if (m_progressCb) m_progressCb("Saving baked assets to Cooked folder...", 0.7f);

    glm::mat4 lightWorldMat = scene.GetWorldMatrix(lightObj);
    glm::vec3 worldLightPos = scene.GetWorldPosition(lightObj);
    glm::vec3 localBaseDir = (lightObj.light.type == LightType::Directional) ? glm::vec3(0.0f, -1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 unnormDir = glm::vec3(lightWorldMat * glm::vec4(localBaseDir, 0.0f));
    float dirLen = glm::length(unnormDir);
    glm::vec3 worldLightDir = (dirLen > 0.0001f) ? (unnormDir / dirLen) : localBaseDir;
    glm::vec3 worldLightColor = lightObj.light.useTemperature ? (lightObj.light.color * ColorTemperatureToRGB(lightObj.light.temperature)) : lightObj.light.color;

    BakedLightMetadata meta;
    meta.lightGuid = lightObj.light.lightGuid;
    meta.resolution = res;
    meta.lightMapAsset = (std::filesystem::path("Cooked") / "Lighting" / "Baked" / lightObj.light.lightGuid / "LightMap.dds").generic_string();
    meta.shadowMapAsset = (std::filesystem::path("Cooked") / "Lighting" / "Baked" / lightObj.light.lightGuid / "ShadowMap.dds").generic_string();
    meta.bakedPosition = worldLightPos;
    meta.bakedRotation = lightObj.rotation;
    meta.bakedIntensity = lightObj.light.intensity;
    meta.bakedColor = worldLightColor;
    meta.bakedRange = lightObj.light.range;
    meta.bakedInnerCone = lightObj.light.innerConeAngle;
    meta.bakedOuterCone = lightObj.light.outerConeAngle;
    meta.bakedShadowBias = lightObj.light.shadowBias;
    meta.bakeSignature = ComputeBakeSignature(lightObj);

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    meta.bakeDate = timeBuf;

    if (!SaveBakeAssets(bakeDir, meta, lmHdr, smHdr, runtimeData.atlasRects, res)) {
        outStatus = "Failed to save baked assets to Cooked folder.";
        return false;
    }

    // Mark light as Baked
    lightObj.light.bakeState = LightBakeState::Baked;
    lightObj.light.isBakeDirty = false;
    lightObj.light.bakeSignature = meta.bakeSignature;
    lightObj.light.bakedPosition = worldLightPos;
    lightObj.light.bakedRotation = meta.bakedRotation;
    lightObj.light.bakedIntensity = meta.bakedIntensity;
    lightObj.light.bakedColor = worldLightColor;
    lightObj.light.bakedRange = meta.bakedRange;
    lightObj.light.bakedInnerCone = meta.bakedInnerCone;
    lightObj.light.bakedOuterCone = meta.bakedOuterCone;
    lightObj.light.bakedShadowBias = meta.bakedShadowBias;
    lightObj.light.bakedResolution = (int)res;
    lightObj.light.bakedLightmapPath = meta.lightMapAsset;
    lightObj.light.bakedShadowmapPath = meta.shadowMapAsset;

    // Sync to mirrored PointLight proxy if applicable
    if (lightObj.lightId >= 0 && lightObj.lightId < (int)scene.pointLights.size()) {
        scene.pointLights[lightObj.lightId].bakeState = LightBakeState::Baked;
    }

    m_bakedLights[lightObj.light.lightGuid] = std::move(runtimeData);
    RecomputeAccumulatedStaticLighting(projectRoot);

    float minHdrVal = 1e9f, maxHdrVal = 0.0f;
    for (const auto& c : lmHdr) {
        float lum = glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
        minHdrVal = std::min(minHdrVal, lum);
        maxHdrVal = std::max(maxHdrVal, lum);
    }
    if (minHdrVal > 1e8f) minHdrVal = 0.0f;

    // Specific logging requirements (dev.md Section 293-327)
    std::cout << "\n========================================\n";
    std::cout << "[Bake] Light: " << lightObj.name << "\n";
    std::cout << "[Bake] Resolution: " << res << "\n";
    std::cout << "[Bake] Participating meshes: " << staticMeshes.size() << "\n";
    std::cout << "[Bake] Light contribution generated\n";
    std::cout << "[Bake] Shadow contribution generated\n";
    std::cout << "[Bake] HDR range: min=" << minHdrVal << ", max=" << maxHdrVal << "\n";
    std::cout << "[Bake] Lightmap asset: " << meta.lightMapAsset << "\n";
    std::cout << "[Bake] Realtime contribution disabled\n";
    std::cout << "[Bake] Baked contribution enabled\n";
    std::cout << "========================================\n\n";

    if (m_progressCb) m_progressCb("Bake complete.", 1.0f);
    outStatus = "Bake complete for " + lightObj.name + ". Assets saved to Cooked folder.";
    return true;
}

bool LightBakeManager::ClearBake(Scene& scene, GameObject& lightObj, const std::filesystem::path& projectRoot) {
    if (lightObj.light.lightGuid.empty()) return false;

    std::filesystem::path bakeDir = GetCookedBakeDir(projectRoot, lightObj.light.lightGuid);
    std::error_code ec;
    std::filesystem::path root = projectRoot.empty() ? "C:/Projects/Eunoia-Engine/Projects/Unknown-13-Test" : projectRoot;
    if (!lightObj.light.bakedLightmapPath.empty()) {
        std::filesystem::remove(root / lightObj.light.bakedLightmapPath, ec);
    }
    if (!lightObj.light.bakedShadowmapPath.empty()) {
        std::filesystem::remove(root / lightObj.light.bakedShadowmapPath, ec);
    }
    std::filesystem::remove_all(bakeDir, ec);

    lightObj.light.bakeState = LightBakeState::Realtime;
    lightObj.light.isBakeDirty = false;
    lightObj.light.bakeSignature = 0;
    lightObj.light.bakedLightmapPath.clear();
    lightObj.light.bakedShadowmapPath.clear();

    if (lightObj.lightId >= 0 && lightObj.lightId < (int)scene.pointLights.size()) {
        scene.pointLights[lightObj.lightId].bakeState = LightBakeState::Realtime;
    }

    m_bakedLights.erase(lightObj.light.lightGuid);
    RecomputeAccumulatedStaticLighting(projectRoot);

    std::cout << "[LightBakeManager] Cleared bake for " << lightObj.name << ". Returned to Realtime.\n";
    return true;
}

bool LightBakeManager::LoadBakeForLight(GameObject& lightObj, const std::filesystem::path& projectRoot) {
    if (lightObj.light.lightGuid.empty()) return false;
    if (lightObj.light.bakeState != LightBakeState::Baked) return false;

    std::filesystem::path bakeDir = GetCookedBakeDir(projectRoot, lightObj.light.lightGuid);
    std::filesystem::path metaFile = bakeDir / "BakeMetadata.json";

    std::error_code ec;
    if (!std::filesystem::exists(metaFile, ec)) {
        lightObj.light.isBakeDirty = true;
        return false;
    }

    RuntimeBakedLightData rbd;
    rbd.lightGuid = lightObj.light.lightGuid;
    rbd.resolution = (uint32_t)lightObj.light.bakeResolution;
    rbd.lightMapPath = bakeDir / "LightMap.dds";
    rbd.shadowMapPath = bakeDir / "ShadowMap.dds";

    m_bakedLights[lightObj.light.lightGuid] = std::move(rbd);
    return true;
}

void LightBakeManager::LoadAllBakesForScene(Scene& scene, const std::filesystem::path& projectRoot) {
    m_bakedLights.clear();
    for (auto& obj : scene.objects) {
        if ((obj.isLight || IsLightPrimitive(obj.type)) && obj.light.bakeState == LightBakeState::Baked) {
            LoadBakeForLight(obj, projectRoot);
            CheckBakeDirty(obj);
        }
    }

    // Reconstruct lightmap UVs across participating geometry for all loaded bakes
    if (!m_bakedLights.empty()) {
        std::vector<BakeStaticMesh> staticMeshes;
        std::string err;
        if (CollectBakeGeometry(scene, staticMeshes, err)) {
            RuntimeBakedLightData dummyData;
            GenerateMeshLightmapUvs(staticMeshes, dummyData);
            for (auto& kv : m_bakedLights) {
                kv.second.atlasRects = dummyData.atlasRects;
                kv.second.meshLightmapUvs = dummyData.meshLightmapUvs;
            }
        }
    }

    RecomputeAccumulatedStaticLighting(projectRoot);
}

void LightBakeManager::OnLightDeleted(const std::string& lightGuid) {
    if (lightGuid.empty()) return;
    m_bakedLights.erase(lightGuid);
    RecomputeAccumulatedStaticLighting();
}

void LightBakeManager::RecomputeAccumulatedStaticLighting(const std::filesystem::path& projectRoot) {
    m_lightmapRevision++;
    m_accumulatedStaticLighting.clear();
    m_accumulatedMeshLightmapUvs.clear();
    m_activeLightmapPath.clear();

    if (m_bakedLights.empty()) return;

    std::vector<std::string> validLightmapPaths;
    for (const auto& kv : m_bakedLights) {
        const auto& lightData = kv.second;
        if (!lightData.lightMapPath.empty() && std::filesystem::exists(lightData.lightMapPath)) {
            validLightmapPaths.push_back(lightData.lightMapPath.generic_string());
        }

        for (const auto& uvsKv : lightData.meshLightmapUvs) {
            m_accumulatedMeshLightmapUvs[uvsKv.first] = uvsKv.second;
        }

        for (const auto& meshKv : lightData.staticMeshVertexLighting) {
            int objId = meshKv.first;
            const auto& vCols = meshKv.second;

            auto& accum = m_accumulatedStaticLighting[objId];
            if (accum.size() < vCols.size()) {
                accum.resize(vCols.size(), glm::vec3(0.0f));
            }

            for (size_t i = 0; i < vCols.size(); ++i) {
                accum[i] += vCols[i];
            }
        }
    }

    if (validLightmapPaths.empty()) {
        m_activeLightmapPath.clear();
        return;
    }

    if (validLightmapPaths.size() == 1) {
        m_activeLightmapPath = validLightmapPaths[0];
    } else {
        // Multi-light accumulation: composite multiple baked lightmaps into CombinedLightMap.dds
        std::filesystem::path root = projectRoot.empty() ? std::filesystem::path("C:/Projects/Eunoia-Engine/Projects/Unknown-13-Test") : projectRoot;
        std::filesystem::path combinedDir = root / "Cooked" / "Lighting" / "Baked";
        std::error_code ec;
        std::filesystem::create_directories(combinedDir, ec);
        std::filesystem::path combinedPath = combinedDir / "CombinedLightMap.dds";

        DirectX::ScratchImage baseImg;
        DirectX::TexMetadata baseMeta;
        if (DirectX::LoadFromDDSFile(validLightmapPaths[0].c_str(), baseMeta, baseImg)) {
            const auto& meta = baseImg.GetMetadata();
            size_t pixelCount = meta.width * meta.height;
            uint16_t* basePixels = reinterpret_cast<uint16_t*>(baseImg.GetPixels());

            for (size_t p = 1; p < validLightmapPaths.size(); ++p) {
                DirectX::ScratchImage nextImg;
                DirectX::TexMetadata nextMeta;
                if (DirectX::LoadFromDDSFile(validLightmapPaths[p].c_str(), nextMeta, nextImg)) {
                    const uint16_t* nextPixels = reinterpret_cast<const uint16_t*>(nextImg.GetPixels());
                    for (size_t px = 0; px < pixelCount; ++px) {
                        float r1 = HalfToFloat(basePixels[px * 4 + 0]);
                        float g1 = HalfToFloat(basePixels[px * 4 + 1]);
                        float b1 = HalfToFloat(basePixels[px * 4 + 2]);

                        float r2 = HalfToFloat(nextPixels[px * 4 + 0]);
                        float g2 = HalfToFloat(nextPixels[px * 4 + 1]);
                        float b2 = HalfToFloat(nextPixels[px * 4 + 2]);

                        basePixels[px * 4 + 0] = FloatToHalf(r1 + r2);
                        basePixels[px * 4 + 1] = FloatToHalf(g1 + g2);
                        basePixels[px * 4 + 2] = FloatToHalf(b1 + b2);
                    }
                }
            }

            std::string combinedPathStr = combinedPath.string();
            DirectX::SaveToDDSFile(baseImg.GetImages(), baseImg.GetImageCount(), baseImg.GetMetadata(), combinedPathStr.c_str());
            m_activeLightmapPath = combinedPath.generic_string();
        } else {
            m_activeLightmapPath = validLightmapPaths[0];
        }
    }
}

const std::vector<glm::vec3>* LightBakeManager::GetStaticMeshVertexLighting(int objectId) const {
    auto it = m_accumulatedStaticLighting.find(objectId);
    if (it != m_accumulatedStaticLighting.end()) {
        return &it->second;
    }
    return nullptr;
}

glm::vec3 LightBakeManager::GetStaticVertexBakedLight(int objectId, size_t vertexIndex) const {
    auto it = m_accumulatedStaticLighting.find(objectId);
    if (it != m_accumulatedStaticLighting.end()) {
        if (vertexIndex < it->second.size()) {
            return it->second[vertexIndex];
        }
    }
    return glm::vec3(0.0f);
}

bool LightBakeManager::HasStaticBakedLighting(int objectId) const {
    return m_accumulatedStaticLighting.find(objectId) != m_accumulatedStaticLighting.end();
}

bool LightBakeManager::HasMeshLightmapUV(int objectId) const {
    return m_accumulatedMeshLightmapUvs.find(objectId) != m_accumulatedMeshLightmapUvs.end();
}

glm::vec2 LightBakeManager::GetMeshLightmapUV(int objectId, size_t vertexIndex) const {
    auto it = m_accumulatedMeshLightmapUvs.find(objectId);
    if (it != m_accumulatedMeshLightmapUvs.end()) {
        if (vertexIndex < it->second.size()) {
            return it->second[vertexIndex];
        }
    }
    return glm::vec2(0.0f, 0.0f);
}

bool LightBakeManager::HasActiveLightmap() const {
    return !m_activeLightmapPath.empty() && std::filesystem::exists(m_activeLightmapPath);
}

std::string LightBakeManager::GetActiveLightmapTexturePath() const {
    return m_activeLightmapPath;
}

} // namespace Eunoia
