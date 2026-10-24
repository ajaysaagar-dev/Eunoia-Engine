#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>
#include <EngineAssets/Geometry.h>

namespace Eunoia {

enum class TerrainBrushType {
    Raise = 0,
    Lower = 1,
    Smooth = 2,
    Flatten = 3,
    Noise = 4
};

enum class TerrainFalloffType {
    Smooth = 0,   // Cosine falloff
    Linear = 1,   // Linear falloff
    Spherical = 2,// Spherical falloff
    Sharp = 3     // Sharp falloff
};

inline const char* GetTerrainBrushTypeName(TerrainBrushType type) {
    switch (type) {
        case TerrainBrushType::Raise:   return "Raise";
        case TerrainBrushType::Lower:   return "Lower";
        case TerrainBrushType::Smooth:  return "Smooth";
        case TerrainBrushType::Flatten: return "Flatten";
        case TerrainBrushType::Noise:   return "Noise";
        default:                        return "Sculpt";
    }
}

inline const char* GetTerrainFalloffTypeName(TerrainFalloffType falloff) {
    switch (falloff) {
        case TerrainFalloffType::Smooth:    return "Smooth (Cosine)";
        case TerrainFalloffType::Linear:    return "Linear";
        case TerrainFalloffType::Spherical: return "Spherical";
        case TerrainFalloffType::Sharp:     return "Sharp";
        default:                            return "Falloff";
    }
}

struct TerrainMaterialLayer {
    std::string name = "Default_Material";
    std::string path = ""; // relative or full path to .emat
    glm::vec3 baseColor{0.8f, 0.8f, 0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float normalStrength = 1.0f;
    float specular = 0.5f;
    glm::vec2 uvScale{1.0f, 1.0f};
    std::string baseColorTexture = "";
    std::string normalTexture = "";
    std::string roughnessTexture = "";
    std::string metallicTexture = "";
    std::string aoTexture = "";

    bool operator==(const TerrainMaterialLayer& o) const {
        return name == o.name && path == o.path &&
               baseColor == o.baseColor && metallic == o.metallic && roughness == o.roughness &&
               normalStrength == o.normalStrength && specular == o.specular && uvScale == o.uvScale &&
               baseColorTexture == o.baseColorTexture && normalTexture == o.normalTexture &&
               roughnessTexture == o.roughnessTexture && metallicTexture == o.metallicTexture &&
               aoTexture == o.aoTexture;
    }
};

struct TerrainSubRange {
    uint32_t startIndex = 0;
    uint32_t indexCount = 0;
    uint8_t materialIndex = 0;
};

struct TerrainComponent {
    // Dimensions
    float width = 64.0f;
    float length = 64.0f;
    int resolution = 128; // Grid segments across X and Z (e.g. 128 = 129x129 vertices)
    float heightScale = 1.0f;

    // Heightfield grid data: (resolution + 1) * (resolution + 1) entries
    std::vector<float> heightmap;

    // Sculpting & Brush controls
    TerrainBrushType brushType = TerrainBrushType::Raise;
    TerrainFalloffType brushFalloff = TerrainFalloffType::Smooth;
    float brushSize = 8.0f;        // Radius in world units
    float brushStrength = 5.0f;    // Speed / intensity per second
    float brushFalloffExp = 0.5f;  // Falloff shape factor
    float flattenTargetHeight = 0.0f;
    float noiseScale = 0.15f;      // Spatial frequency for procedural noise
    bool editModeActive = false;   // Details panel Sculpt/Edit tab toggle

    // Multi-Material Painting & Layer Controls
    std::vector<TerrainMaterialLayer> materialLayers;
    int activePaintMaterialIndex = 0;
    bool materialPaintModeActive = false; // Details panel Materials tab toggle
    float matBrushSize = 8.0f;
    float matBrushStrength = 2.0f;
    TerrainFalloffType matBrushFalloff = TerrainFalloffType::Smooth;

    // Continuous per-layer vertex blend weights (Unreal Engine style landscape weightmaps)
    // layerWeights[layerIdx][vIdx] from 0.0f to 1.0f (normalized: sum across layers == 1.0f at every vertex)
    std::vector<std::vector<float>> layerWeights;

    // Legacy compatibility container
    std::vector<uint8_t> quadMaterialIndices;

    // Runtime state
    bool isDirty = true;

    void EnsureLayerWeights() {
        int totalVerts = (resolution + 1) * (resolution + 1);
        if (materialLayers.empty()) {
            materialLayers.push_back(TerrainMaterialLayer());
        }
        if (layerWeights.size() != materialLayers.size()) {
            layerWeights.resize(materialLayers.size());
        }
        for (size_t i = 0; i < layerWeights.size(); ++i) {
            if (layerWeights[i].size() != (size_t)totalVerts) {
                layerWeights[i].assign(totalVerts, (i == 0) ? 1.0f : 0.0f);
            }
        }
    }

    void InitializeGrid(float w = 64.0f, float l = 64.0f, int res = 128) {
        width = w;
        length = l;
        resolution = std::max(4, std::min(res, 512));
        int totalVerts = (resolution + 1) * (resolution + 1);
        heightmap.assign(totalVerts, 0.0f);
        EnsureLayerWeights();
        for (size_t i = 0; i < layerWeights.size(); ++i) {
            layerWeights[i].assign(totalVerts, (i == 0) ? 1.0f : 0.0f);
        }
        isDirty = true;
    }

    // Resize or extend terrain dimensions and/or resolution without losing existing height/tilt and material paint data
    // anchorMode: 0 = Center (extends equally in all directions, keeping existing world positions),
    //             1 = Min Corner (-X, -Z fixed, extends towards +X, +Z)
    //             2 = Max Corner (+X, +Z fixed, extends towards -X, -Z)
    void ResizeGrid(float newW, float newL, int newRes, int anchorMode = 0) {
        newW = std::max(4.0f, newW);
        newL = std::max(4.0f, newL);
        newRes = std::clamp(newRes, 4, 512);

        // If heightmap is uninitialized, initialize directly
        if (heightmap.empty() || resolution <= 0) {
            InitializeGrid(newW, newL, newRes);
            return;
        }

        // Check if nothing changed
        if (std::abs(newW - width) < 0.001f &&
            std::abs(newL - length) < 0.001f &&
            newRes == resolution) {
            return;
        }

        EnsureLayerWeights();

        float oldW = width;
        float oldL = length;
        int oldRes = resolution;
        const auto oldHeightmap = heightmap;
        const auto oldLayerWeights = layerWeights;
        size_t numLayers = materialLayers.size();

        width = newW;
        length = newL;
        resolution = newRes;

        int newTotalVerts = (newRes + 1) * (newRes + 1);
        heightmap.assign(newTotalVerts, 0.0f);
        layerWeights.resize(numLayers);
        for (size_t l = 0; l < numLayers; ++l) {
            layerWeights[l].assign(newTotalVerts, (l == 0) ? 1.0f : 0.0f);
        }

        float hxNew = newW * 0.5f;
        float hzNew = newL * 0.5f;
        float stepXNew = newW / (float)newRes;
        float stepZNew = newL / (float)newRes;

        float hxOld = oldW * 0.5f;
        float hzOld = oldL * 0.5f;
        float stepXOld = oldW / (float)oldRes;
        float stepZOld = oldL / (float)oldRes;

        // Offset based on anchor mode
        float offX = 0.0f;
        float offZ = 0.0f;
        if (anchorMode == 1) { // Min Corner (-X, -Z fixed)
            offX = (oldW - newW) * 0.5f;
            offZ = (oldL - newL) * 0.5f;
        } else if (anchorMode == 2) { // Max Corner (+X, +Z fixed)
            offX = (newW - oldW) * 0.5f;
            offZ = (newL - oldL) * 0.5f;
        }

        for (int z = 0; z <= newRes; ++z) {
            float posZ = -hzNew + (float)z * stepZNew;
            float posZOld = posZ + offZ;

            for (int x = 0; x <= newRes; ++x) {
                float posX = -hxNew + (float)x * stepXNew;
                float posXOld = posX + offX;

                int vIdx = z * (newRes + 1) + x;

                // Calculate fractional grid coordinates in the old terrain
                float gx = (posXOld + hxOld) / stepXOld;
                float gz = (posZOld + hzOld) / stepZOld;

                // If within old bounds, bilinearly sample old height and layer weights
                if (gx >= 0.0f && gx <= (float)oldRes && gz >= 0.0f && gz <= (float)oldRes) {
                    int x0 = std::clamp((int)std::floor(gx), 0, oldRes - 1);
                    int z0 = std::clamp((int)std::floor(gz), 0, oldRes - 1);
                    int x1 = x0 + 1;
                    int z1 = z0 + 1;

                    float fx = std::clamp(gx - (float)x0, 0.0f, 1.0f);
                    float fz = std::clamp(gz - (float)z0, 0.0f, 1.0f);

                    int idx00 = z0 * (oldRes + 1) + x0;
                    int idx10 = z0 * (oldRes + 1) + x1;
                    int idx01 = z1 * (oldRes + 1) + x0;
                    int idx11 = z1 * (oldRes + 1) + x1;

                    // Bilinear height sample
                    float h00 = (idx00 < (int)oldHeightmap.size()) ? oldHeightmap[idx00] : 0.0f;
                    float h10 = (idx10 < (int)oldHeightmap.size()) ? oldHeightmap[idx10] : 0.0f;
                    float h01 = (idx01 < (int)oldHeightmap.size()) ? oldHeightmap[idx01] : 0.0f;
                    float h11 = (idx11 < (int)oldHeightmap.size()) ? oldHeightmap[idx11] : 0.0f;

                    float hTop = glm::mix(h00, h10, fx);
                    float hBottom = glm::mix(h01, h11, fx);
                    heightmap[vIdx] = glm::mix(hTop, hBottom, fz);

                    // Bilinear material layer weights sample
                    float sumW = 0.0f;
                    for (size_t l = 0; l < numLayers; ++l) {
                        float w00 = (l < oldLayerWeights.size() && idx00 < (int)oldLayerWeights[l].size()) ? oldLayerWeights[l][idx00] : (l == 0 ? 1.0f : 0.0f);
                        float w10 = (l < oldLayerWeights.size() && idx10 < (int)oldLayerWeights[l].size()) ? oldLayerWeights[l][idx10] : (l == 0 ? 1.0f : 0.0f);
                        float w01 = (l < oldLayerWeights.size() && idx01 < (int)oldLayerWeights[l].size()) ? oldLayerWeights[l][idx01] : (l == 0 ? 1.0f : 0.0f);
                        float w11 = (l < oldLayerWeights.size() && idx11 < (int)oldLayerWeights[l].size()) ? oldLayerWeights[l][idx11] : (l == 0 ? 1.0f : 0.0f);

                        float wTop = glm::mix(w00, w10, fx);
                        float wBottom = glm::mix(w01, w11, fx);
                        float w = glm::mix(wTop, wBottom, fz);

                        layerWeights[l][vIdx] = w;
                        sumW += w;
                    }

                    // Normalize weights
                    if (sumW > 0.0001f) {
                        for (size_t l = 0; l < numLayers; ++l) {
                            layerWeights[l][vIdx] = glm::clamp(layerWeights[l][vIdx] / sumW, 0.0f, 1.0f);
                        }
                    } else {
                        layerWeights[0][vIdx] = 1.0f;
                        for (size_t l = 1; l < numLayers; ++l) layerWeights[l][vIdx] = 0.0f;
                    }
                } else {
                    // Extended region outside previous bounds: flat base terrain
                    heightmap[vIdx] = 0.0f;
                    layerWeights[0][vIdx] = 1.0f;
                    for (size_t l = 1; l < numLayers; ++l) {
                        layerWeights[l][vIdx] = 0.0f;
                    }
                }
            }
        }

        isDirty = true;
    }

    void SyncBaseMaterial(const std::string& matName, const glm::vec3& color, float metallic, float roughness,
                          float normalStrength, float specular, const glm::vec2& uvScale,
                          const std::string& baseTex, const std::string& normalTex, const std::string& roughTex,
                          const std::string& metalTex, const std::string& aoTex) {
        if (materialLayers.empty()) {
            TerrainMaterialLayer baseLayer;
            baseLayer.name = !matName.empty() ? matName : "Default_Material";
            baseLayer.baseColor = color;
            baseLayer.metallic = metallic;
            baseLayer.roughness = roughness;
            baseLayer.normalStrength = normalStrength;
            baseLayer.specular = specular;
            baseLayer.uvScale = uvScale;
            baseLayer.baseColorTexture = baseTex;
            baseLayer.normalTexture = normalTex;
            baseLayer.roughnessTexture = roughTex;
            baseLayer.metallicTexture = metalTex;
            baseLayer.aoTexture = aoTex;
            materialLayers.push_back(baseLayer);
        } else {
            materialLayers[0].name = !matName.empty() ? matName : "Default_Material";
            materialLayers[0].baseColor = color;
            materialLayers[0].metallic = metallic;
            materialLayers[0].roughness = roughness;
            materialLayers[0].normalStrength = normalStrength;
            materialLayers[0].specular = specular;
            materialLayers[0].uvScale = uvScale;
            materialLayers[0].baseColorTexture = baseTex;
            materialLayers[0].normalTexture = normalTex;
            materialLayers[0].roughnessTexture = roughTex;
            materialLayers[0].metallicTexture = metalTex;
            materialLayers[0].aoTexture = aoTex;
        }
        EnsureLayerWeights();
    }

    void SwapMaterialLayers(int idxA, int idxB) {
        if (idxA < 0 || idxA >= (int)materialLayers.size() || idxB < 0 || idxB >= (int)materialLayers.size() || idxA == idxB) return;
        EnsureLayerWeights();
        std::swap(materialLayers[idxA], materialLayers[idxB]);
        std::swap(layerWeights[idxA], layerWeights[idxB]);
        if (activePaintMaterialIndex == idxA) activePaintMaterialIndex = idxB;
        else if (activePaintMaterialIndex == idxB) activePaintMaterialIndex = idxA;
        isDirty = true;
    }

    void RemoveMaterialLayer(int idx) {
        if (idx <= 0 || idx >= (int)materialLayers.size()) return;
        EnsureLayerWeights();
        int totalVerts = (resolution + 1) * (resolution + 1);
        for (int v = 0; v < totalVerts; ++v) {
            float rem = (v < (int)layerWeights[idx].size()) ? layerWeights[idx][v] : 0.0f;
            layerWeights[0][v] = glm::clamp(layerWeights[0][v] + rem, 0.0f, 1.0f);
        }
        materialLayers.erase(materialLayers.begin() + idx);
        layerWeights.erase(layerWeights.begin() + idx);
        if (activePaintMaterialIndex >= (int)materialLayers.size()) {
            activePaintMaterialIndex = (int)materialLayers.size() - 1;
        }
        isDirty = true;
    }

    void FillAllWithMaterial(int targetMatIndex) {
        if (materialLayers.empty() || targetMatIndex < 0 || targetMatIndex >= (int)materialLayers.size()) return;
        EnsureLayerWeights();
        int totalVerts = (resolution + 1) * (resolution + 1);
        for (size_t l = 0; l < layerWeights.size(); ++l) {
            float val = ((int)l == targetMatIndex) ? 1.0f : 0.0f;
            std::fill(layerWeights[l].begin(), layerWeights[l].end(), val);
        }
        isDirty = true;
    }

    inline int GetVertexIndex(int x, int z) const {
        return z * (resolution + 1) + x;
    }

    inline float GetHeight(int x, int z) const {
        if (x < 0 || x > resolution || z < 0 || z > resolution || heightmap.empty()) return 0.0f;
        return heightmap[GetVertexIndex(x, z)];
    }

    inline void SetHeight(int x, int z, float h) {
        if (x < 0 || x > resolution || z < 0 || z > resolution || heightmap.empty()) return;
        heightmap[GetVertexIndex(x, z)] = h;
        isDirty = true;
    }

    // Evaluate brush attenuation weight at distance d from hit center (0.0 to 1.0)
    float CalculateBrushWeight(float dist, float radius) const {
        if (dist >= radius) return 0.0f;
        float t = glm::clamp(dist / std::max(radius, 0.001f), 0.0f, 1.0f);
        switch (brushFalloff) {
            case TerrainFalloffType::Linear:
                return 1.0f - t;
            case TerrainFalloffType::Spherical:
                return std::sqrt(std::max(0.0f, 1.0f - t * t));
            case TerrainFalloffType::Sharp:
                return (1.0f - t) * (1.0f - t);
            case TerrainFalloffType::Smooth:
            default: {
                // Cosine ease-in/ease-out: (1 + cos(pi * t)) / 2
                return 0.5f * (1.0f + std::cos(glm::pi<float>() * t));
            }
        }
    }

    // Interactive Sculpting Brush Application
    // localHitPos: cursor hit position in terrain's local coordinates (Y is current height)
    void ApplyBrush(const glm::vec3& localHitPos, float deltaTime, bool invert = false) {
        if (heightmap.empty() || resolution <= 0) return;
        int res = resolution;
        int vertsPerSide = res + 1;
        float hx = width * 0.5f;
        float hz = length * 0.5f;
        float stepX = width / (float)res;
        float stepZ = length / (float)res;

        // Bounding box of brush circle in grid index coordinates
        float minXWorld = localHitPos.x - brushSize;
        float maxXWorld = localHitPos.x + brushSize;
        float minZWorld = localHitPos.z - brushSize;
        float maxZWorld = localHitPos.z + brushSize;

        int minGridX = glm::clamp((int)std::floor((minXWorld + hx) / stepX), 0, res);
        int maxGridX = glm::clamp((int)std::ceil((maxXWorld + hx) / stepX), 0, res);
        int minGridZ = glm::clamp((int)std::floor((minZWorld + hz) / stepZ), 0, res);
        int maxGridZ = glm::clamp((int)std::ceil((maxZWorld + hz) / stepZ), 0, res);

        float dtRate = glm::clamp(deltaTime, 0.001f, 0.1f);
        float baseDelta = brushStrength * dtRate * (invert ? -1.0f : 1.0f);

        for (int z = minGridZ; z <= maxGridZ; ++z) {
            float vZ = -hz + (float)z * stepZ;
            for (int x = minGridX; x <= maxGridX; ++x) {
                float vX = -hx + (float)x * stepX;
                float dist2D = std::hypot(vX - localHitPos.x, vZ - localHitPos.z);
                if (dist2D > brushSize) continue;

                float weight = CalculateBrushWeight(dist2D, brushSize);
                if (weight <= 0.0001f) continue;

                int vIdx = z * vertsPerSide + x;
                float currentH = heightmap[vIdx];

                switch (brushType) {
                    case TerrainBrushType::Raise: {
                        heightmap[vIdx] += baseDelta * weight;
                        break;
                    }
                    case TerrainBrushType::Lower: {
                        heightmap[vIdx] -= baseDelta * weight;
                        break;
                    }
                    case TerrainBrushType::Flatten: {
                        float targetH = flattenTargetHeight;
                        float diff = targetH - currentH;
                        heightmap[vIdx] += diff * glm::clamp(brushStrength * 2.0f * dtRate * weight, 0.0f, 1.0f);
                        break;
                    }
                    case TerrainBrushType::Smooth: {
                        // 4-neighborhood average
                        float sum = 0.0f;
                        int count = 0;
                        for (int dz = -1; dz <= 1; ++dz) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                int nx = x + dx;
                                int nz = z + dz;
                                if (nx >= 0 && nx <= res && nz >= 0 && nz <= res) {
                                    sum += heightmap[nz * vertsPerSide + nx];
                                    count++;
                                }
                            }
                        }
                        if (count > 0) {
                            float avgH = sum / (float)count;
                            float diff = avgH - currentH;
                            heightmap[vIdx] += diff * glm::clamp(brushStrength * 2.5f * dtRate * weight, 0.0f, 1.0f);
                        }
                        break;
                    }
                    case TerrainBrushType::Noise: {
                        float nVal = glm::perlin(glm::vec2(vX, vZ) * noiseScale);
                        heightmap[vIdx] += nVal * baseDelta * weight * 1.5f;
                        break;
                    }
                }
            }
        }
        isDirty = true;
    }

    // Interactive Material Painting Brush Application with Smooth Continuous Weights
    // localHitPos: cursor hit position in terrain's local coordinates
    // invert: when true (holding Shift), erases active layer and restores base layer
    void ApplyMaterialPaint(const glm::vec3& localHitPos, uint8_t targetMatIndex, float deltaTime, bool invert = false) {
        if (materialLayers.empty() || resolution <= 0) return;
        if (targetMatIndex >= materialLayers.size()) return;
        EnsureLayerWeights();

        int res = resolution;
        int vertsPerSide = res + 1;
        float hx = width * 0.5f;
        float hz = length * 0.5f;
        float stepX = width / (float)res;
        float stepZ = length / (float)res;

        float minXWorld = localHitPos.x - matBrushSize;
        float maxXWorld = localHitPos.x + matBrushSize;
        float minZWorld = localHitPos.z - matBrushSize;
        float maxZWorld = localHitPos.z + matBrushSize;

        int minGridX = glm::clamp((int)std::floor((minXWorld + hx) / stepX), 0, res);
        int maxGridX = glm::clamp((int)std::ceil((maxXWorld + hx) / stepX), 0, res);
        int minGridZ = glm::clamp((int)std::floor((minZWorld + hz) / stepZ), 0, res);
        int maxGridZ = glm::clamp((int)std::ceil((maxZWorld + hz) / stepZ), 0, res);

        float dtRate = glm::clamp(deltaTime, 0.001f, 0.05f);
        // Continuous flow rate scaled by strength (Unreal Engine style)
        float rate = glm::clamp(matBrushStrength, 0.01f, 20.0f) * dtRate * 3.5f;

        for (int z = minGridZ; z <= maxGridZ; ++z) {
            float vZ = -hz + (float)z * stepZ;
            for (int x = minGridX; x <= maxGridX; ++x) {
                float vX = -hx + (float)x * stepX;
                float dist2D = std::hypot(vX - localHitPos.x, vZ - localHitPos.z);
                if (dist2D > matBrushSize) continue;

                // Falloff calculation
                float t = glm::clamp(dist2D / std::max(matBrushSize, 0.001f), 0.0f, 1.0f);
                float falloffWeight = 0.0f;
                switch (matBrushFalloff) {
                    case TerrainFalloffType::Linear:
                        falloffWeight = 1.0f - t;
                        break;
                    case TerrainFalloffType::Spherical:
                        falloffWeight = std::sqrt(std::max(0.0f, 1.0f - t * t));
                        break;
                    case TerrainFalloffType::Sharp:
                        falloffWeight = (1.0f - t) * (1.0f - t);
                        break;
                    case TerrainFalloffType::Smooth:
                    default:
                        falloffWeight = 0.5f * (1.0f + std::cos(glm::pi<float>() * t));
                        break;
                }

                if (falloffWeight <= 0.0001f) continue;

                int vIdx = z * vertsPerSide + x;
                float delta = rate * falloffWeight;

                if (invert) {
                    // Erase active overlay layer (Shift+LMB) -> returns weight to base layer 0
                    if (targetMatIndex == 0) continue;
                    float oldW = layerWeights[targetMatIndex][vIdx];
                    float newW = glm::clamp(oldW - delta, 0.0f, 1.0f);
                    float removed = oldW - newW;
                    layerWeights[targetMatIndex][vIdx] = newW;
                    layerWeights[0][vIdx] = glm::clamp(layerWeights[0][vIdx] + removed, 0.0f, 1.0f);
                } else {
                    float oldW = layerWeights[targetMatIndex][vIdx];
                    float newW = glm::clamp(oldW + delta, 0.0f, 1.0f);
                    float diff = newW - oldW;
                    if (diff > 0.00001f) {
                        layerWeights[targetMatIndex][vIdx] = newW;
                        // Proportional normalization across remaining layers so sum is always 1.0
                        float otherSum = 0.0f;
                        for (size_t l = 0; l < layerWeights.size(); ++l) {
                            if (l != (size_t)targetMatIndex) otherSum += layerWeights[l][vIdx];
                        }
                        if (otherSum > 0.0001f) {
                            float factor = (1.0f - newW) / otherSum;
                            for (size_t l = 0; l < layerWeights.size(); ++l) {
                                if (l != (size_t)targetMatIndex) layerWeights[l][vIdx] = glm::clamp(layerWeights[l][vIdx] * factor, 0.0f, 1.0f);
                            }
                        } else {
                            for (size_t l = 0; l < layerWeights.size(); ++l) {
                                if (l != (size_t)targetMatIndex) layerWeights[l][vIdx] = 0.0f;
                            }
                        }
                    }
                }
            }
        }
        isDirty = true;
    }

    // Rebuild PrimitiveMesh vertices, indices, normals, UVs and tangents
    void RebuildTerrainMesh(PrimitiveMesh& mesh) {
        if (heightmap.empty()) {
            InitializeGrid(width, length, resolution);
        }
        EnsureLayerWeights();

        int res = resolution;
        int vertsPerSide = res + 1;
        float hx = width * 0.5f;
        float hz = length * 0.5f;
        float stepX = width / (float)res;
        float stepZ = length / (float)res;

        mesh.vertices.resize(vertsPerSide * vertsPerSide);

        // 1. Generate vertex positions and UVs
        for (int z = 0; z <= res; ++z) {
            float tz = (float)z / (float)res;
            float posZ = -hz + (float)z * stepZ;
            for (int x = 0; x <= res; ++x) {
                float tx = (float)x / (float)res;
                float posX = -hx + (float)x * stepX;
                int vIdx = z * vertsPerSide + x;
                float posY = (vIdx < (int)heightmap.size()) ? heightmap[vIdx] : 0.0f;

                mesh.vertices[vIdx].pos = glm::vec3(posX, posY, posZ);
                mesh.vertices[vIdx].uv = glm::vec2((posX / 8.0f) + 4.0f, (posZ / 8.0f) + 4.0f); // Invariant 8m texture tiling (never stretches on resize)
            }
        }

        // 2. Compute smooth normal and tangent vectors using central finite differences
        for (int z = 0; z <= res; ++z) {
            for (int x = 0; x <= res; ++x) {
                int vIdx = z * vertsPerSide + x;

                float hL = (x > 0) ? heightmap[z * vertsPerSide + (x - 1)] : heightmap[vIdx];
                float hR = (x < res) ? heightmap[z * vertsPerSide + (x + 1)] : heightmap[vIdx];
                float hD = (z > 0) ? heightmap[(z - 1) * vertsPerSide + x] : heightmap[vIdx];
                float hU = (z < res) ? heightmap[(z + 1) * vertsPerSide + x] : heightmap[vIdx];

                glm::vec3 normal = glm::normalize(glm::vec3(hL - hR, stepX + stepZ, hD - hU));
                mesh.vertices[vIdx].normal = normal;

                glm::vec3 tangent = glm::normalize(glm::vec3(1.0f, (hR - hL) / (2.0f * stepX), 0.0f));
                mesh.vertices[vIdx].tangent = glm::vec4(tangent, 1.0f);
            }
        }

        // 3. Build full index buffer with Counter-Clockwise (CCW) front-facing triangles
        mesh.indices.clear();
        mesh.indices.reserve(res * res * 6);
        for (int z = 0; z < res; ++z) {
            for (int x = 0; x < res; ++x) {
                uint32_t i0 = z * vertsPerSide + x;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = (z + 1) * vertsPerSide + x;
                uint32_t i3 = i2 + 1;

                // Tri 1: i0, i2, i1 (Upward normal +Y)
                mesh.indices.push_back(i0);
                mesh.indices.push_back(i2);
                mesh.indices.push_back(i1);

                // Tri 2: i1, i2, i3 (Upward normal +Y)
                mesh.indices.push_back(i1);
                mesh.indices.push_back(i2);
                mesh.indices.push_back(i3);
            }
        }

        mesh.CalculateAABB();
        mesh.GenerateLODs(0.5f, 0.2f);
        isDirty = false;
    }

    // Raycast a ray against the terrain heightfield grid
    // Returns true and sets outLocalHit if the ray intersects the terrain
    bool Raycast(const glm::vec3& rayLocalOrigin, const glm::vec3& rayLocalDir, glm::vec3& outLocalHit) const {
        if (heightmap.empty() || resolution <= 0) return false;

        float hx = width * 0.5f;
        float hz = length * 0.5f;
        // Check horizontal plane bounding box
        float minH = 1e9f, maxH = -1e9f;
        for (float h : heightmap) {
            minH = std::min(minH, h);
            maxH = std::max(maxH, h);
        }

        // AABB intersection test
        glm::vec3 bMin(-hx, minH - 1.0f, -hz);
        glm::vec3 bMax(hx, maxH + 1.0f, hz);

        float tmin = (bMin.x - rayLocalOrigin.x) / (std::abs(rayLocalDir.x) > 1e-6f ? rayLocalDir.x : 1e-6f);
        float tmax = (bMax.x - rayLocalOrigin.x) / (std::abs(rayLocalDir.x) > 1e-6f ? rayLocalDir.x : 1e-6f);
        if (tmin > tmax) std::swap(tmin, tmax);

        float tymin = (bMin.y - rayLocalOrigin.y) / (std::abs(rayLocalDir.y) > 1e-6f ? rayLocalDir.y : 1e-6f);
        float tymax = (bMax.y - rayLocalOrigin.y) / (std::abs(rayLocalDir.y) > 1e-6f ? rayLocalDir.y : 1e-6f);
        if (tymin > tymax) std::swap(tymin, tymax);

        if ((tmin > tymax) || (tymin > tmax)) return false;
        if (tymin > tmin) tmin = tymin;
        if (tymax < tmax) tmax = tymax;

        float tzmin = (bMin.z - rayLocalOrigin.z) / (std::abs(rayLocalDir.z) > 1e-6f ? rayLocalDir.z : 1e-6f);
        float tzmax = (bMax.z - rayLocalOrigin.z) / (std::abs(rayLocalDir.z) > 1e-6f ? rayLocalDir.z : 1e-6f);
        if (tzmin > tzmax) std::swap(tzmin, tzmax);

        if ((tmin > tzmax) || (tzmin > tmax)) return false;
        if (tzmin > tmin) tmin = tzmin;
        if (tzmax < tmax) tmax = tzmax;

        if (tmax < 0.0f) return false;
        float startT = std::max(0.0f, tmin);
        float endT = tmax;

        // March ray in fine steps across the heightfield grid
        float stepSize = std::min(width, length) / (float)resolution * 0.5f;
        stepSize = std::max(stepSize, 0.05f);

        int maxSteps = (int)std::ceil((endT - startT) / stepSize);
        maxSteps = std::min(maxSteps, 1000);

        float prevT = startT;
        glm::vec3 prevP = rayLocalOrigin + rayLocalDir * prevT;

        auto SampleHeightAt = [&](float px, float pz) -> float {
            float gx = (px + hx) / (width / (float)resolution);
            float gz = (pz + hz) / (length / (float)resolution);
            int x0 = glm::clamp((int)std::floor(gx), 0, resolution);
            int x1 = glm::clamp(x0 + 1, 0, resolution);
            int z0 = glm::clamp((int)std::floor(gz), 0, resolution);
            int z1 = glm::clamp(z0 + 1, 0, resolution);
            float fx = gx - (float)x0;
            float fz = gz - (float)z0;

            int v00 = z0 * (resolution + 1) + x0;
            int v10 = z0 * (resolution + 1) + x1;
            int v01 = z1 * (resolution + 1) + x0;
            int v11 = z1 * (resolution + 1) + x1;

            float h0 = glm::mix(heightmap[v00], heightmap[v10], fx);
            float h1 = glm::mix(heightmap[v01], heightmap[v11], fx);
            return glm::mix(h0, h1, fz);
        };

        for (int i = 1; i <= maxSteps; ++i) {
            float curT = startT + (float)i * stepSize;
            glm::vec3 curP = rayLocalOrigin + rayLocalDir * curT;

            if (curP.x < -hx || curP.x > hx || curP.z < -hz || curP.z > hz) {
                prevT = curT;
                prevP = curP;
                continue;
            }

            float terrH = SampleHeightAt(curP.x, curP.z);
            if (curP.y <= terrH) {
                // Linear interpolation to locate exact intersection
                float prevTerrH = SampleHeightAt(prevP.x, prevP.z);
                float frac = (prevP.y - prevTerrH) / ((prevP.y - prevTerrH) - (curP.y - terrH) + 1e-6f);
                frac = glm::clamp(frac, 0.0f, 1.0f);
                outLocalHit = glm::mix(prevP, curP, frac);
                outLocalHit.y = SampleHeightAt(outLocalHit.x, outLocalHit.z);
                return true;
            }
            prevT = curT;
            prevP = curP;
        }

        // Fallback: intersect with mean elevation plane Y = minH
        if (std::abs(rayLocalDir.y) > 1e-5f) {
            float tPlane = (0.0f - rayLocalOrigin.y) / rayLocalDir.y;
            if (tPlane >= 0.0f) {
                glm::vec3 hit = rayLocalOrigin + rayLocalDir * tPlane;
                if (hit.x >= -hx && hit.x <= hx && hit.z >= -hz && hit.z <= hz) {
                    outLocalHit = hit;
                    return true;
                }
            }
        }

        return false;
    }
};

} // namespace Eunoia
