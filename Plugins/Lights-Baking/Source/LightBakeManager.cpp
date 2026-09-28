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

    for (const auto& obj : scene.objects) {
        if (!obj.visible) continue;
        if (obj.isCamera || IsCameraPrimitive(obj.type)) continue;
        if (obj.isLight || IsLightPrimitive(obj.type)) continue;
        if (obj.isWaterBody || obj.type == PrimitiveType::WaterBody) continue;
        if (obj.mesh.vertices.empty() || obj.mesh.indices.empty()) continue;

        // Static scene geometry requirement (Section 7 of dev.md)
        if (obj.mobility != Mobility::Static) continue;

        BakeStaticMesh sm;
        sm.objectId = obj.id;
        sm.name = obj.name;
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
        outStaticMeshes.push_back(std::move(sm));
    }

    if (outStaticMeshes.empty()) {
        outError = "No valid static geometry found. Set object Mobility to Static to participate in light baking.";
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

    if (std::abs(a) < EPSILON) return false;

    float f = 1.0f / a;
    glm::vec3 s = orig - v0;
    float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;

    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = f * glm::dot(edge2, q);
    return (t > EPSILON && t < maxDist);
}

void LightBakeManager::RaycastShadowsAndIllumination(
    const GameObject& lightObj,
    const std::vector<BakeStaticMesh>& staticMeshes,
    RuntimeBakedLightData& outData,
    std::vector<uint8_t>& outLightMapRgba,
    std::vector<uint8_t>& outShadowMapRgba,
    uint32_t resolution)
{
    glm::vec3 lightPos = lightObj.position;
    glm::vec3 lightDir = glm::normalize(lightObj.light.direction);
    glm::vec3 lightColor = lightObj.light.color;
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

    outData.staticMeshVertexLighting.clear();

    // 1. Calculate per-vertex direct lighting and static shadow occlusion
    for (const auto& mesh : staticMeshes) {
        std::vector<glm::vec3> vLight(mesh.worldVertices.size(), glm::vec3(0.0f));

        for (size_t vi = 0; vi < mesh.worldVertices.size(); ++vi) {
            glm::vec3 P = mesh.worldVertices[vi];
            glm::vec3 N = mesh.worldNormals[vi];

            glm::vec3 L;
            float dist = 0.0f;
            float atten = 1.0f;
            float maxRayDist = 1000.0f;

            if (lType == LightType::Directional) {
                L = -lightDir;
                dist = 1000.0f;
                maxRayDist = 500.0f;
            } else {
                glm::vec3 toLight = lightPos - P;
                dist = glm::length(toLight);
                if (dist >= lightRange || dist <= 1e-4f) continue;
                L = toLight / dist;
                maxRayDist = dist - 0.015f;

                float normDist = dist / lightRange;
                float distAtten = std::clamp(1.0f - normDist, 0.0f, 1.0f);
                distAtten = std::pow(distAtten, std::max(lightAtten, 0.2f)) / (dist * dist + 1.0f);

                if (lType == LightType::Spot) {
                    float cosDir = glm::dot(lightDir, -L);
                    float spotAtten = std::clamp((cosDir - cosOuter) / std::max(cosInner - cosOuter, 0.001f), 0.0f, 1.0f);
                    atten = distAtten * spotAtten * spotAtten;
                } else {
                    atten = distAtten;
                }
            }

            float NdotL = std::max(glm::dot(N, L), 0.0f);
            if (NdotL <= 1e-4f || atten <= 1e-5f) continue;

            // Cast shadow ray
            float shadowFactor = 1.0f;
            if (castShadows) {
                glm::vec3 rayOrig = P + N * 0.02f;
                bool occluded = false;

                for (const auto& occMesh : staticMeshes) {
                    if (!RayBoxIntersect(rayOrig, L, maxRayDist, occMesh.aabbMin - glm::vec3(0.05f), occMesh.aabbMax + glm::vec3(0.05f))) {
                        continue;
                    }

                    for (size_t tri = 0; tri + 2 < occMesh.indices.size(); tri += 3) {
                        uint32_t i0 = occMesh.indices[tri];
                        uint32_t i1 = occMesh.indices[tri + 1];
                        uint32_t i2 = occMesh.indices[tri + 2];
                        if (i0 >= occMesh.worldVertices.size() || i1 >= occMesh.worldVertices.size() || i2 >= occMesh.worldVertices.size()) continue;

                        if (RayTriangleIntersect(rayOrig, L, maxRayDist, occMesh.worldVertices[i0], occMesh.worldVertices[i1], occMesh.worldVertices[i2])) {
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

            glm::vec3 directIllum = lightColor * (lightIntensity * NdotL * atten * shadowFactor);
            vLight[vi] = directIllum;
        }

        outData.staticMeshVertexLighting[mesh.objectId] = std::move(vLight);
    }

    // 2. Generate 2D LightMap and ShadowMap textures (RGBA8)
    outLightMapRgba.resize(resolution * resolution * 4, 0);
    outShadowMapRgba.resize(resolution * resolution * 4, 255);

    // Fill textures with baseline representation
    for (uint32_t y = 0; y < resolution; ++y) {
        for (uint32_t x = 0; x < resolution; ++x) {
            uint32_t idx = (y * resolution + x) * 4;

            float u = (x + 0.5f) / (float)resolution;
            float v = (y + 0.5f) / (float)resolution;

            // Generate falloff gradient representation
            float dx = u - 0.5f;
            float dy = v - 0.5f;
            float r = std::sqrt(dx * dx + dy * dy) * 2.0f;
            float falloff = std::clamp(1.0f - r, 0.0f, 1.0f);

            glm::vec3 rgb = lightColor * (lightIntensity * falloff);
            outLightMapRgba[idx + 0] = (uint8_t)std::clamp(rgb.r * 255.0f, 0.0f, 255.0f);
            outLightMapRgba[idx + 1] = (uint8_t)std::clamp(rgb.g * 255.0f, 0.0f, 255.0f);
            outLightMapRgba[idx + 2] = (uint8_t)std::clamp(rgb.b * 255.0f, 0.0f, 255.0f);
            outLightMapRgba[idx + 3] = 255;

            // Shadow map stores shadow attenuation / depth
            uint8_t sVal = (uint8_t)(falloff * 255.0f);
            outShadowMapRgba[idx + 0] = sVal;
            outShadowMapRgba[idx + 1] = sVal;
            outShadowMapRgba[idx + 2] = sVal;
            outShadowMapRgba[idx + 3] = 255;
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
    return projectRoot / "Cooked" / "Lighting" / "Baked" / lightGuid;
}

bool LightBakeManager::SaveBakeAssets(
    const std::filesystem::path& bakeDir,
    const BakedLightMetadata& meta,
    const std::vector<uint8_t>& lightMapRgba,
    const std::vector<uint8_t>& shadowMapRgba,
    uint32_t res)
{
    std::error_code ec;
    std::filesystem::create_directories(bakeDir, ec);
    if (ec) {
        std::cerr << "[LightBakeManager] Failed to create directory: " << bakeDir.string() << " (" << ec.message() << ")\n";
        return false;
    }

    // 1. Save LightMap.dds using DirectXTex
    std::filesystem::path lmPath = bakeDir / "LightMap.dds";
    {
        DirectX::ScratchImage img;
        if (img.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, res, res, 1, 1)) {
            memcpy(img.GetPixels(), lightMapRgba.data(), lightMapRgba.size());
            DirectX::SaveToDDSFile(img.GetImages(), img.GetImageCount(), img.GetMetadata(), lmPath.string().c_str());
        }
    }

    // 2. Save ShadowMap.dds using DirectXTex
    std::filesystem::path smPath = bakeDir / "ShadowMap.dds";
    {
        DirectX::ScratchImage img;
        if (img.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, res, res, 1, 1)) {
            memcpy(img.GetPixels(), shadowMapRgba.data(), shadowMapRgba.size());
            DirectX::SaveToDDSFile(img.GetImages(), img.GetImageCount(), img.GetMetadata(), smPath.string().c_str());
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
        ofs << "  \"bakeDate\": \"" << meta.bakeDate << "\"\n";
        ofs << "}\n";
    }

    return true;
}

bool LightBakeManager::BakeLight(Scene& scene, GameObject& lightObj, const std::filesystem::path& projectRoot, std::string& outStatus) {
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

    // Ensure stable light GUID
    if (lightObj.light.lightGuid.empty()) {
        lightObj.light.lightGuid = EngineCore::UUID{}.ToString();
    }

    uint32_t res = (uint32_t)lightObj.light.bakeResolution;
    std::filesystem::path bakeDir = GetCookedBakeDir(projectRoot, lightObj.light.lightGuid);

    outStatus = "Baking Light: " + lightObj.name + "...";
    if (m_progressCb) m_progressCb("Calculating illumination and static shadows...", 0.3f);

    RuntimeBakedLightData runtimeData;
    runtimeData.lightGuid = lightObj.light.lightGuid;
    runtimeData.resolution = res;
    runtimeData.lightMapPath = bakeDir / "LightMap.dds";
    runtimeData.shadowMapPath = bakeDir / "ShadowMap.dds";

    std::vector<uint8_t> lmRgba, smRgba;
    RaycastShadowsAndIllumination(lightObj, staticMeshes, runtimeData, lmRgba, smRgba, res);

    if (m_progressCb) m_progressCb("Saving baked assets to Cooked folder...", 0.7f);

    BakedLightMetadata meta;
    meta.lightGuid = lightObj.light.lightGuid;
    meta.resolution = res;
    meta.lightMapAsset = (std::filesystem::path("Cooked") / "Lighting" / "Baked" / lightObj.light.lightGuid / "LightMap.dds").generic_string();
    meta.shadowMapAsset = (std::filesystem::path("Cooked") / "Lighting" / "Baked" / lightObj.light.lightGuid / "ShadowMap.dds").generic_string();
    meta.bakedPosition = lightObj.position;
    meta.bakedRotation = lightObj.rotation;
    meta.bakedIntensity = lightObj.light.intensity;
    meta.bakedColor = lightObj.light.color;
    meta.bakedRange = lightObj.light.range;
    meta.bakedInnerCone = lightObj.light.innerConeAngle;
    meta.bakedOuterCone = lightObj.light.outerConeAngle;
    meta.bakedShadowBias = lightObj.light.shadowBias;
    meta.bakeSignature = ComputeBakeSignature(lightObj);

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    meta.bakeDate = timeBuf;

    if (!SaveBakeAssets(bakeDir, meta, lmRgba, smRgba, res)) {
        outStatus = "Failed to save baked assets to Cooked folder.";
        return false;
    }

    // Mark light as Baked
    lightObj.light.bakeState = LightBakeState::Baked;
    lightObj.light.isBakeDirty = false;
    lightObj.light.bakeSignature = meta.bakeSignature;
    lightObj.light.bakedPosition = meta.bakedPosition;
    lightObj.light.bakedRotation = meta.bakedRotation;
    lightObj.light.bakedIntensity = meta.bakedIntensity;
    lightObj.light.bakedColor = meta.bakedColor;
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
    RecomputeAccumulatedStaticLighting();

    if (m_progressCb) m_progressCb("Bake complete.", 1.0f);
    outStatus = "Bake complete for " + lightObj.name + ". Assets saved to Cooked folder.";
    return true;
}

bool LightBakeManager::ClearBake(Scene& scene, GameObject& lightObj, const std::filesystem::path& projectRoot) {
    if (lightObj.light.lightGuid.empty()) return false;

    std::filesystem::path bakeDir = GetCookedBakeDir(projectRoot, lightObj.light.lightGuid);
    std::error_code ec;
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
    RecomputeAccumulatedStaticLighting();

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
        // Assets were deleted or missing
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

    // Reconstruct static lighting across all loaded bakes
    std::vector<BakeStaticMesh> staticMeshes;
    std::string err;
    if (CollectBakeGeometry(scene, staticMeshes, err)) {
        for (auto& obj : scene.objects) {
            if ((obj.isLight || IsLightPrimitive(obj.type)) && obj.light.bakeState == LightBakeState::Baked) {
                auto it = m_bakedLights.find(obj.light.lightGuid);
                if (it != m_bakedLights.end()) {
                    std::vector<uint8_t> dummyLm, dummySm;
                    RaycastShadowsAndIllumination(obj, staticMeshes, it->second, dummyLm, dummySm, (uint32_t)obj.light.bakeResolution);
                }
            }
        }
    }

    RecomputeAccumulatedStaticLighting();
}

void LightBakeManager::OnLightDeleted(const std::string& lightGuid) {
    if (lightGuid.empty()) return;
    m_bakedLights.erase(lightGuid);
    RecomputeAccumulatedStaticLighting();
}

void LightBakeManager::RecomputeAccumulatedStaticLighting() {
    m_accumulatedStaticLighting.clear();

    for (const auto& kv : m_bakedLights) {
        const auto& lightData = kv.second;
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

} // namespace Eunoia
