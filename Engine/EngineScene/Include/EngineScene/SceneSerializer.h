#pragma once
#include <EngineScene/Scene.h>
#include <EngineScene/BehaviourRegistry.h>
#include <SaveSystem/SaveSystem.h>
#include <EngineCore/EngineLogger.h>
#include <EngineScene/SceneCacheManager.h>
#include <EngineAssets/Addressables.h>
#ifdef GetClassName
#undef GetClassName
#endif
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
#include <filesystem>
#include <functional>
#include <thread>
#include <mutex>
#include <deque>
#include <atomic>
#include <chrono>

// ============================================================================
// SceneSerializer — JSON-based Scene Save/Load System
// Saves and loads complete scene state including actors, lights, and environment.
// Format: .escene (Eunoia Scene) JSON files
// ============================================================================

using SceneProgressFn = std::function<void(float progress, const std::string& step, const std::string& detail)>;

class SceneSerializer {
public:

    // Fast binary cache for pre-parsed mesh geometries (delegates to versioned SceneCacheManager)
    static bool SaveFastMeshCache(const Scene& scene, const std::string& binPath) {
        return Eunoia::SceneCacheManager::Get().GenerateCache(scene, binPath);
    }

    static std::unordered_map<int, PrimitiveMesh> LoadFastMeshMap(const std::string& binPath) {
        std::unordered_map<int, PrimitiveMesh> meshMap;
        if (binPath.empty() || !std::filesystem::exists(binPath)) return meshMap;

        // Try high-performance versioned SceneCacheManager first
        Scene dummyScene;
        if (Eunoia::SceneCacheManager::Get().LoadCache(dummyScene, binPath, meshMap)) {
            return meshMap;
        }

        // Backward compatibility fallback for legacy EMSH caches
        std::ifstream in(binPath, std::ios::binary);
        if (!in.is_open()) return meshMap;

        uint32_t magic = 0;
        in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        if (magic != 0x454D5348) return meshMap; // "EMSH"

        uint32_t count = 0;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));

        for (uint32_t i = 0; i < count; ++i) {
            int id = -1;
            in.read(reinterpret_cast<char*>(&id), sizeof(id));
            uint32_t vCount = 0;
            in.read(reinterpret_cast<char*>(&vCount), sizeof(vCount));
            PrimitiveMesh pm;
            pm.vertices.resize(vCount);
            if (vCount > 0) {
                in.read(reinterpret_cast<char*>(pm.vertices.data()), vCount * sizeof(MeshVertex));
            }
            uint32_t iCount = 0;
            in.read(reinterpret_cast<char*>(&iCount), sizeof(iCount));
            pm.indices.resize(iCount);
            if (iCount > 0) {
                in.read(reinterpret_cast<char*>(pm.indices.data()), iCount * sizeof(uint32_t));
            }
            if (vCount > 0) {
                meshMap[id] = std::move(pm);
            }
        }
        return meshMap;
    }

    // Save the entire scene to a JSON file
    static bool SaveScene(const Scene& scene, const std::string& filePath, SceneProgressFn onProgress = nullptr) {
        if (onProgress) onProgress(0.1f, "Opening file for writing", filePath);
        std::string tempPath = filePath + ".tmp";
        std::ofstream file(tempPath);
        if (!file.is_open()) return false;

        file << "{\n";
        file << "  \"formatVersion\": 1,\n";

        // Extract scene name from file path
        std::filesystem::path p(filePath);
        std::string sceneName = p.stem().string();
        file << "  \"sceneName\": " << QuoteStr(sceneName) << ",\n";

        // Environment
        file << "  \"environment\": {\n";
        file << "    \"clearColor\": " << WriteVec4(scene.clearColor) << ",\n";
        file << "    \"ambientIntensity\": " << scene.ambientIntensity << ",\n";
        file << "    \"sunLight\": {\n";
        file << "      \"direction\": " << WriteVec3(scene.lightDirection) << ",\n";
        file << "      \"color\": " << WriteVec3(scene.lightColor) << ",\n";
        file << "      \"intensity\": " << scene.lightIntensity << "\n";
        file << "    },\n";
        file << "    \"shadows\": {\n";
        file << "      \"enabled\": " << BoolStr(scene.enableShadows) << ",\n";
        file << "      \"type\": " << scene.shadowType << ",\n";
        file << "      \"resolution2D\": " << scene.shadowResolution2D << ",\n";
        file << "      \"resolutionCube\": " << scene.shadowResolutionCube << ",\n";
        file << "      \"sampleCount\": " << scene.shadowSampleCount << ",\n";
        file << "      \"pcss\": " << BoolStr(scene.shadowPCSS) << ",\n";
        file << "      \"distance\": " << scene.shadowDistance << ",\n";
        file << "      \"strength\": " << scene.shadowStrength << ",\n";
        file << "      \"bias\": " << scene.shadowBias << ",\n";
        file << "      \"pcfRadius\": " << scene.pcfRadius << ",\n";
        file << "      \"softness\": " << scene.shadowSoftness << ",\n";
        file << "      \"cascadeCount\": " << scene.cascadeCount << ",\n";
        file << "      \"cascade0\": " << scene.cascadeDistances[0] << ",\n";
        file << "      \"cascade1\": " << scene.cascadeDistances[1] << ",\n";
        file << "      \"cascade2\": " << scene.cascadeDistances[2] << ",\n";
        file << "      \"cascade3\": " << scene.cascadeDistances[3] << ",\n";
        file << "      \"cascadeBlending\": " << BoolStr(scene.cascadeBlending) << ",\n";
        file << "      \"visualizeCascades\": " << BoolStr(scene.visualizeCascades) << ",\n";
        file << "      \"screenSpaceShadows\": " << BoolStr(scene.screenSpaceShadows) << ",\n";
        file << "      \"screenSpaceRange\": " << scene.screenSpaceShadowRange << ",\n";
        file << "      \"screenSpaceSamples\": " << scene.screenSpaceShadowSamples << ",\n";
        file << "      \"capsuleShadows\": " << BoolStr(scene.capsuleShadows) << ",\n";
        file << "      \"capsuleAngle\": " << scene.capsuleShadowAngle << ",\n";
        file << "      \"capsuleFade\": " << scene.capsuleShadowFade << ",\n";
        file << "      \"shadowLODOverride\": " << BoolStr(scene.shadowLODOverride) << "\n";
        file << "    },\n";
        file << "    \"grid\": {\n";
        file << "      \"show\": " << BoolStr(scene.showGrid) << ",\n";
        file << "      \"size\": " << scene.gridSize << ",\n";
        file << "      \"divisions\": " << scene.gridDivisions << ",\n";
        file << "      \"color\": " << WriteVec3(scene.gridColor) << "\n";
        file << "    },\n";
        file << "    \"skybox\": {\n";
        file << "      \"enabled\": " << BoolStr(scene.enableSkybox) << ",\n";
        file << "      \"assetPath\": " << QuoteStr(scene.skyboxAssetPath) << ",\n";
        file << "      \"texturePath\": " << QuoteStr(scene.skyboxTexturePath) << ",\n";
        file << "      \"exposure\": " << scene.skyboxExposure << ",\n";
        file << "      \"rotation\": " << scene.skyboxRotation << ",\n";
        file << "      \"autoRotate\": " << BoolStr(scene.skyboxAutoRotate) << ",\n";
        file << "      \"autoRotateSpeed\": " << scene.skyboxAutoRotateSpeed << ",\n";
        file << "      \"tint\": " << WriteVec3(scene.skyboxTint) << ",\n";
        file << "      \"ambientContribution\": " << scene.skyboxAmbientContribution << ",\n";
        file << "      \"enableLighting\": " << BoolStr(scene.enableSkyboxLighting) << ",\n";
        file << "      \"lightingIntensity\": " << scene.skyboxLightingIntensity << ",\n";
        file << "      \"lightingColor\": " << WriteVec3(scene.skyboxLightingColor) << ",\n";
        file << "      \"lightingSpecular\": " << scene.skyboxLightingSpecular << ",\n";
        file << "      \"lightingDiffuse\": " << scene.skyboxLightingDiffuse << "\n";
        file << "    },\n";
        file << "    \"postProcessing\": {\n";
        file << "      \"enabled\": " << BoolStr(scene.enablePostProcessing) << ",\n";
        file << "      \"exposureEV\": " << scene.postExposureEV << ",\n";
        file << "      \"exposureMultiplier\": " << scene.postExposureMultiplier << ",\n";
        file << "      \"tonemapper\": " << scene.postTonemapper << ",\n";
        file << "      \"saturation\": " << scene.postSaturation << ",\n";
        file << "      \"contrast\": " << scene.postContrast << ",\n";
        file << "      \"gamma\": " << scene.postGamma << ",\n";
        file << "      \"colorFilter\": " << WriteVec3(scene.postColorFilter) << ",\n";
        file << "      \"colorTemperature\": " << scene.postColorTemperature << ",\n";
        file << "      \"colorTint\": " << scene.postColorTint << ",\n";
        file << "      \"sharpenAmount\": " << scene.postSharpenAmount << ",\n";
        file << "      \"denoiserAmount\": " << scene.postDenoiserAmount << "\n";
        file << "    },\n";
        file << "    \"ambientOcclusion\": {\n";
        file << "      \"enabled\": " << BoolStr(scene.enableAO) << ",\n";
        file << "      \"type\": " << scene.aoType << ",\n";
        file << "      \"power\": " << scene.aoPower << ",\n";
        file << "      \"range\": " << scene.aoRange << ",\n";
        file << "      \"sampleCount\": " << scene.aoSampleCount << ",\n";
        file << "      \"bias\": " << scene.aoBias << ",\n";
        file << "      \"blur\": " << BoolStr(scene.aoBlur) << ",\n";
        file << "      \"blurRadius\": " << scene.aoBlurRadius << ",\n";
        file << "      \"temporal\": " << BoolStr(scene.aoTemporal) << ",\n";
        file << "      \"temporalBlend\": " << scene.aoTemporalBlend << "\n";
        file << "    },\n";
        file << "    \"volumetricFog\": {\n";
        file << "      \"enabled\": " << BoolStr(scene.enableFog) << ",\n";
        file << "      \"start\": " << scene.fogStart << ",\n";
        file << "      \"density\": " << scene.fogDensity << ",\n";
        file << "      \"heightStart\": " << scene.fogHeightStart << ",\n";
        file << "      \"heightEnd\": " << scene.fogHeightEnd << ",\n";
        file << "      \"color\": [" << scene.fogColor.r << ", " << scene.fogColor.g << ", " << scene.fogColor.b << "]\n";
        file << "    },\n";
        file << "    \"graphics\": {\n";
        file << "      \"vsync\": " << BoolStr(scene.vsync) << ",\n";
        file << "      \"displayOutput\": " << scene.displayOutput << ",\n";
        file << "      \"resolutionScale\": " << scene.resolutionScale << ",\n";
        file << "      \"streamingThreshold\": " << scene.streamingThreshold << ",\n";
        file << "      \"hdrCalibration\": " << scene.hdrCalibration << ",\n";
        file << "      \"hdrUiCalibration\": " << scene.hdrUiCalibration << ",\n";
        file << "      \"renderPath\": " << scene.renderPath << ",\n";
        file << "      \"pathTraceTargetSamples\": " << scene.pathTraceTargetSamples << ",\n";
        file << "      \"occlusionCulling\": " << BoolStr(scene.occlusionCulling) << ",\n";
        file << "      \"visibilityComputeShading\": " << BoolStr(scene.visibilityComputeShading) << ",\n";
        file << "      \"allowMeshShader\": " << BoolStr(scene.allowMeshShader) << ",\n";
        file << "      \"meshletOcclusionCulling\": " << BoolStr(scene.meshletOcclusionCulling) << ",\n";
        file << "      \"meshBlend\": " << BoolStr(scene.meshBlend) << ",\n";
        file << "      \"tessellation\": " << BoolStr(scene.tessellation) << ",\n";
        file << "      \"gameSpeed\": " << scene.gameSpeed << ",\n";
        file << "      \"wireframeMode\": " << scene.wireframeMode << ",\n";
        file << "      \"giBoost\": " << scene.giBoost << ",\n";
        file << "      \"surfelGI\": " << BoolStr(scene.surfelGI) << ",\n";
        file << "      \"surfelGIDebug\": " << scene.surfelGIDebug << ",\n";
        file << "      \"ddgi\": " << BoolStr(scene.ddgi) << ",\n";
        file << "      \"ddgiDebug\": " << BoolStr(scene.ddgiDebug) << ",\n";
        file << "      \"ddgiGridRes\": [" << scene.ddgiGridRes.x << ", " << scene.ddgiGridRes.y << ", " << scene.ddgiGridRes.z << "],\n";
        file << "      \"ddgiRayCount\": " << scene.ddgiRayCount << ",\n";
        file << "      \"ddgiBlendSpeed\": " << scene.ddgiBlendSpeed << ",\n";
        file << "      \"ddgiSmoothBackface\": " << scene.ddgiSmoothBackface << ",\n";
        file << "      \"vxgi\": " << BoolStr(scene.vxgi) << ",\n";
        file << "      \"vxgiDebug\": " << scene.vxgiDebug << ",\n";
        file << "      \"vxgiReflections\": " << BoolStr(scene.vxgiReflections) << ",\n";
        file << "      \"vxgiVoxelSize\": " << scene.vxgiVoxelSize << ",\n";
        file << "      \"vxgiRayStepSize\": " << scene.vxgiRayStepSize << ",\n";
        file << "      \"vxgiMaxDistance\": " << scene.vxgiMaxDistance << ",\n";
        file << "      \"ssgi\": " << BoolStr(scene.ssgi) << ",\n";
        file << "      \"ssgiDepthRejection\": " << scene.ssgiDepthRejection << ",\n";
        file << "      \"rtDiffuse\": " << BoolStr(scene.rtDiffuse) << ",\n";
        file << "      \"rtDiffuseRange\": " << scene.rtDiffuseRange << ",\n";
        file << "      \"rtDiffuseQuality\": " << scene.rtDiffuseQuality << ",\n";
        file << "      \"raytraceBounceCount\": " << scene.raytraceBounceCount << ",\n";
        file << "      \"msaa\": " << scene.msaa << ",\n";
        file << "      \"temporalAA\": " << BoolStr(scene.temporalAA) << ",\n";
        file << "      \"temporalAADebug\": " << BoolStr(scene.temporalAADebug) << ",\n";
        file << "      \"textureQuality\": " << scene.textureQuality << ",\n";
        file << "      \"mipLodBias\": " << scene.mipLodBias << ",\n";
        file << "      \"fsr\": " << BoolStr(scene.fsr) << ",\n";
        file << "      \"fsrSharpness\": " << scene.fsrSharpness << ",\n";
        file << "      \"fsr2\": " << BoolStr(scene.fsr2) << ",\n";
        file << "      \"fsr2Preset\": " << scene.fsr2Preset << ",\n";
        file << "      \"fsr2Sharpness\": " << scene.fsr2Sharpness << ",\n";
        file << "      \"vrsClassification\": " << BoolStr(scene.vrsClassification) << ",\n";
        file << "      \"vrsDebug\": " << BoolStr(scene.vrsDebug) << ",\n";
        file << "      \"planarReflections\": " << BoolStr(scene.planarReflections) << ",\n";
        file << "      \"planarReflectionResolutionScale\": " << scene.planarReflectionResolutionScale << ",\n";
        file << "      \"planarReflectionMSAA\": " << scene.planarReflectionMSAA << ",\n";
        file << "      \"ssr\": " << BoolStr(scene.ssr) << ",\n";
        file << "      \"ssrQuality\": " << scene.ssrQuality << ",\n";
        file << "      \"reflectionRoughnessCutoff\": " << scene.reflectionRoughnessCutoff << ",\n";
        file << "      \"rtReflections\": " << BoolStr(scene.rtReflections) << ",\n";
        file << "      \"rtReflectionsRange\": " << scene.rtReflectionsRange << ",\n";
        file << "      \"rtReflectionsQuality\": " << scene.rtReflectionsQuality << ",\n";
        file << "      \"colorGrading\": " << BoolStr(scene.colorGrading) << ",\n";
        file << "      \"dither\": " << BoolStr(scene.dither) << ",\n";
        file << "      \"bloom\": " << BoolStr(scene.bloom) << ",\n";
        file << "      \"bloomStrength\": " << scene.bloomStrength << ",\n";
        file << "      \"lightShafts\": " << BoolStr(scene.lightShafts) << ",\n";
        file << "      \"lightShaftsStrength\": " << scene.lightShaftsStrength << ",\n";
        file << "      \"lightShaftsFadeSpeed\": " << scene.lightShaftsFadeSpeed << ",\n";
        file << "      \"lensFlare\": " << BoolStr(scene.lensFlare) << ",\n";
        file << "      \"eyeAdaption\": " << BoolStr(scene.eyeAdaption) << ",\n";
        file << "      \"eyeAdaptionKey\": " << scene.eyeAdaptionKey << ",\n";
        file << "      \"eyeAdaptionRate\": " << scene.eyeAdaptionRate << ",\n";
        file << "      \"motionBlur\": " << BoolStr(scene.motionBlur) << ",\n";
        file << "      \"motionBlurStrength\": " << scene.motionBlurStrength << ",\n";
        file << "      \"depthOfField\": " << BoolStr(scene.depthOfField) << ",\n";
        file << "      \"depthOfFieldScale\": " << scene.depthOfFieldScale << ",\n";
        file << "      \"fxaa\": " << BoolStr(scene.fxaa) << ",\n";
        file << "      \"crtFilter\": " << BoolStr(scene.crtFilter) << ",\n";
        file << "      \"sharpenFilter\": " << BoolStr(scene.sharpenFilter) << ",\n";
        file << "      \"sharpenFilterAmount\": " << scene.sharpenFilterAmount << ",\n";
        file << "      \"outline\": " << BoolStr(scene.outline) << ",\n";
        file << "      \"outlineThreshold\": " << scene.outlineThreshold << ",\n";
        file << "      \"outlineThickness\": " << scene.outlineThickness << ",\n";
        file << "      \"chromaticAberration\": " << BoolStr(scene.chromaticAberration) << ",\n";
        file << "      \"chromaticAberrationAmount\": " << scene.chromaticAberrationAmount << ",\n";
        file << "      \"advancedLightCulling\": " << BoolStr(scene.advancedLightCulling) << ",\n";
        file << "      \"debugLightCulling\": " << BoolStr(scene.debugLightCulling) << ",\n";
        file << "      \"freezeCullingCamera\": " << BoolStr(scene.freezeCullingCamera) << ",\n";
        file << "      \"disableAlbedoMaps\": " << BoolStr(scene.disableAlbedoMaps) << ",\n";
        file << "      \"forceDiffuseLighting\": " << BoolStr(scene.forceDiffuseLighting) << ",\n";
        file << "      \"forceUnlit\": " << BoolStr(scene.forceUnlit) << ",\n";
        file << "      \"raytraceDebugBVH\": " << BoolStr(scene.raytraceDebugBVH) << "\n";
        file << "    }\n";
        file << "  },\n";

        // Point Lights
        file << "  \"pointLights\": [\n";
        for (size_t i = 0; i < scene.pointLights.size(); ++i) {
            const auto& pl = scene.pointLights[i];
            file << "    {\n";
            file << "      \"id\": " << pl.id << ",\n";
            file << "      \"name\": " << QuoteStr(pl.name) << ",\n";
            file << "      \"position\": " << WriteVec3(pl.position) << ",\n";
            file << "      \"color\": " << WriteVec3(pl.color) << ",\n";
            file << "      \"intensity\": " << pl.intensity << ",\n";
            file << "      \"range\": " << pl.range << ",\n";
            file << "      \"rangeSmoothing\": " << pl.rangeSmoothing << ",\n";
            file << "      \"enabled\": " << BoolStr(pl.enabled) << "\n";
            file << "    }" << (i + 1 < scene.pointLights.size() ? "," : "") << "\n";
        }
        file << "  ],\n";

        // Actors
        file << "  \"actors\": [\n";
        for (size_t i = 0; i < scene.objects.size(); ++i) {
            const auto& obj = scene.objects[i];
            file << "    {\n";
            file << "      \"id\": " << obj.id << ",\n";
            file << "      \"name\": " << QuoteStr(obj.name) << ",\n";
            file << "      \"type\": " << QuoteStr(GetPrimitiveTypeName(obj.type)) << ",\n";
            file << "      \"mobility\": " << QuoteStr(MobilityToStr(obj.mobility)) << ",\n";

            // Transform
            file << "      \"transform\": {\n";
            file << "        \"position\": " << WriteVec3(obj.position) << ",\n";
            file << "        \"rotation\": " << WriteVec3(obj.rotation) << ",\n";
            file << "        \"scale\": " << WriteVec3(obj.scale) << "\n";
            file << "      },\n";

            // Material
            file << "      \"material\": {\n";
            file << "        \"materialName\": " << QuoteStr(obj.materialName) << ",\n";
            file << "        \"color\": " << WriteVec3(obj.color) << ",\n";
            file << "        \"metallic\": " << obj.metallic << ",\n";
            file << "        \"roughness\": " << obj.roughness << ",\n";
            file << "        \"normalStrength\": " << obj.normalStrength << ",\n";
            file << "        \"specular\": " << obj.specular << ",\n";
            file << "        \"emissiveColor\": " << WriteVec3(obj.emissiveColor) << ",\n";
            file << "        \"emissiveIntensity\": " << obj.emissiveIntensity << ",\n";
            file << "        \"shadingModel\": " << obj.shadingModel << ",\n";
            file << "        \"blendMode\": " << obj.blendMode << ",\n";
            file << "        \"twoSided\": " << BoolStr(obj.twoSided) << ",\n";
            file << "        \"castShadows\": " << BoolStr(obj.castShadows) << ",\n";
            file << "        \"receiveShadows\": " << BoolStr(obj.receiveShadows) << ",\n";
            file << "        \"meshClusterCulling\": " << BoolStr(obj.meshClusterCulling) << ",\n";
            file << "        \"smoothShading\": " << BoolStr(obj.smoothShading) << ",\n";
            file << "        \"autoSmoothAngle\": " << obj.autoSmoothAngle << ",\n";
            file << "        \"enableLOD\": " << BoolStr(obj.enableLOD) << ",\n";
            file << "        \"forcedLOD\": " << obj.forcedLOD << ",\n";
            file << "        \"lodBias\": " << obj.lodBias << ",\n";
            file << "        \"minLOD\": " << obj.minLOD << ",\n";
            file << "        \"maxLOD\": " << obj.maxLOD << ",\n";
            file << "        \"enableSFLOD\": " << BoolStr(obj.enableSFLOD) << ",\n";
            file << "        \"sflodMinLOD\": " << obj.sflodMinLOD << ",\n";
            file << "        \"sflodMaxLOD\": " << obj.sflodMaxLOD << ",\n";
            file << "        \"sflodLODBias\": " << obj.sflodLODBias << ",\n";
            file << "        \"sflodBillboardMode\": " << obj.sflodBillboardMode << ",\n";
            file << "        \"sflodGeometryMode\": " << obj.sflodGeometryMode << ",\n";
            file << "        \"sflodOpenFace\": " << obj.sflodOpenFace << ",\n";
            file << "        \"sflodFaceMask\": " << (int)obj.sflodFaceMask << ",\n";
            file << "        \"sflodShaderComplexity\": " << obj.sflodShaderComplexity << ",\n";
            file << "        \"sflodDistanceThreshold\": " << obj.sflodDistanceThreshold << ",\n";
            file << "        \"sflodIsAtlas\": " << BoolStr(obj.sflodIsAtlas) << ",\n";
            file << "        \"sflodPlaneWidth\": " << obj.sflodPlaneWidth << ",\n";
            file << "        \"sflodPlaneHeight\": " << obj.sflodPlaneHeight << ",\n";
            file << "        \"sflodPlaneOffset\": [" << obj.sflodPlaneOffset.x << ", " << obj.sflodPlaneOffset.y << "],\n";
            file << "        \"sflodCustomTexture\": " << QuoteStr(obj.sflodCustomTexture) << ",\n";
            file << "        \"sflodCutout\": " << BoolStr(obj.sflodCutout) << ",\n";
            file << "        \"opacity\": " << obj.opacity << ",\n";
            file << "        \"opacityMaskClipValue\": " << obj.opacityMaskClipValue << ",\n";
            file << "        \"normalMapYFlip\": " << BoolStr(obj.normalMapYFlip) << ",\n";
            file << "        \"disableStreaming\": " << BoolStr(obj.disableStreaming) << ",\n";
            file << "        \"metallicChannel\": " << obj.metallicChannel << ",\n";
            file << "        \"roughnessChannel\": " << obj.roughnessChannel << ",\n";
            file << "        \"aoChannel\": " << obj.aoChannel << ",\n";
            file << "        \"materialDebugMode\": " << obj.materialDebugMode << ",\n";
            file << "        \"uvScale\": [" << obj.uvScale.x << ", " << obj.uvScale.y << "],\n";
            file << "        \"textures\": {\n";
            file << "          \"baseColor\": " << QuoteStr(obj.baseColorTexture) << ",\n";
            file << "          \"normal\": " << QuoteStr(obj.normalTexture) << ",\n";
            file << "          \"roughness\": " << QuoteStr(obj.roughnessTexture) << ",\n";
            file << "          \"metallic\": " << QuoteStr(obj.metallicTexture) << ",\n";
            file << "          \"ao\": " << QuoteStr(obj.aoTexture) << ",\n";
            file << "          \"emission\": " << QuoteStr(obj.emissionTexture) << ",\n";
            file << "          \"opacity\": " << QuoteStr(obj.opacityTexture) << "\n";
            file << "        }\n";
            file << "      },\n";

            // Visibility & Animation
            file << "      \"visible\": " << BoolStr(obj.visible) << ",\n";
            file << "      \"autoRotate\": " << BoolStr(obj.autoRotate) << ",\n";
            file << "      \"autoRotateSpeed\": " << WriteVec3(obj.autoRotateSpeed) << ",\n";

            // Tags
            file << "      \"tags\": [\n";
            for (size_t ti = 0; ti < obj.tags.size(); ++ti) {
                file << "        " << QuoteStr(obj.tags[ti]) << (ti + 1 < obj.tags.size() ? "," : "") << "\n";
            }
            file << "      ],\n";

            // Hierarchy, Imported Mesh & Light Proxy
            file << "      \"isImportedMesh\": " << BoolStr(obj.isImportedMesh) << ",\n";
            file << "      \"meshFilePath\": " << QuoteStr(obj.meshFilePath) << ",\n";
            file << "      \"submeshIndex\": " << obj.submeshIndex << ",\n";
            file << "      \"parentId\": " << obj.parentId << ",\n";
            file << "      \"isLight\": " << BoolStr(obj.isLight) << ",\n";
            file << "      \"lightId\": " << obj.lightId << ",\n";

            // Primitive Parameters (dev.md)
            file << "      \"paramWidth\": " << obj.params.width << ",\n";
            file << "      \"paramHeight\": " << obj.params.height << ",\n";
            file << "      \"paramDepth\": " << obj.params.depth << ",\n";
            file << "      \"paramSize\": " << obj.params.size << ",\n";
            file << "      \"paramRadius\": " << obj.params.radius << ",\n";
            file << "      \"paramRadius2\": " << obj.params.radius2 << ",\n";
            file << "      \"paramSegX\": " << obj.params.segmentsX << ",\n";
            file << "      \"paramSegY\": " << obj.params.segmentsY << ",\n";
            file << "      \"paramSegZ\": " << obj.params.segmentsZ << ",\n";
            file << "      \"paramRadialSeg\": " << obj.params.radialSegments << ",\n";
            file << "      \"paramHeightSeg\": " << obj.params.heightSegments << ",\n";
            file << "      \"paramRings\": " << obj.params.rings << ",\n";
            file << "      \"paramSectors\": " << obj.params.sectors << ",\n";
            file << "      \"paramSubdiv\": " << obj.params.subdivisions << ",\n";
            file << "      \"paramSides\": " << obj.params.sides << ",\n";
            file << "      \"paramCapTop\": " << BoolStr(obj.params.capTop) << ",\n";
            file << "      \"paramCapBottom\": " << BoolStr(obj.params.capBottom) << ",\n";

            // Wicked Engine Light Component Properties
            file << "      \"lightFlags\": " << obj.light._flags << ",\n";
            file << "      \"lightType\": " << (int)obj.light.type << ",\n";
            file << "      \"lightEnabled\": " << BoolStr(obj.light.enabled) << ",\n";
            file << "      \"lightColor\": " << WriteVec3(obj.light.color) << ",\n";
            file << "      \"lightIntensity\": " << obj.light.intensity << ",\n";
            file << "      \"lightTemperature\": " << obj.light.temperature << ",\n";
            file << "      \"lightUseTemp\": " << BoolStr(obj.light.useTemperature) << ",\n";
            file << "      \"lightRange\": " << obj.light.range << ",\n";
            file << "      \"lightRangeSmoothing\": " << obj.light.rangeSmoothing << ",\n";
            file << "      \"lightAttenuation\": " << obj.light.attenuation << ",\n";
            file << "      \"lightInnerCone\": " << obj.light.innerConeAngle << ",\n";
            file << "      \"lightOuterCone\": " << obj.light.outerConeAngle << ",\n";
            file << "      \"lightConeFalloff\": " << obj.light.coneFalloff << ",\n";
            file << "      \"lightRadius\": " << obj.light.radius << ",\n";
            file << "      \"lightLength\": " << obj.light.length << ",\n";
            file << "      \"lightHeight\": " << obj.light.height << ",\n";
            file << "      \"lightVolumetricBoost\": " << obj.light.volumetric_boost << ",\n";
            file << "      \"lightForcedShadowRes\": " << obj.light.forced_shadow_resolution << ",\n";
            file << "      \"lightAreaShape\": " << obj.light.areaShape << ",\n";
            file << "      \"lightAreaWidth\": " << obj.light.width << ",\n";
            file << "      \"lightAreaHeight\": " << obj.light.height << ",\n";
            file << "      \"lightTwoSided\": " << BoolStr(obj.light.twoSided) << ",\n";
            file << "      \"lightSkyColor\": " << WriteVec3(obj.light.skyColor) << ",\n";
            file << "      \"lightGroundColor\": " << WriteVec3(obj.light.groundColor) << ",\n";
            file << "      \"lightEnvMap\": " << QuoteStr(obj.light.envMapTexture) << ",\n";
            file << "      \"lightCastShadows\": " << BoolStr(obj.light.castShadows) << ",\n";
            file << "      \"lightShadowStrength\": " << obj.light.shadowStrength << ",\n";
            file << "      \"lightShadowBias\": " << obj.light.shadowBias << ",\n";
            file << "      \"lightShadowRes\": " << obj.light.shadowResolution << ",\n";
            file << "      \"lightShadowDist\": " << obj.light.shadowDistance << ",\n";
            file << "      \"lightVolumetric\": " << BoolStr(obj.light.volumetric) << ",\n";
            file << "      \"lightVolScattering\": " << obj.light.volumetricScattering << ",\n";
            file << "      \"lightVolIntensity\": " << obj.light.volumetricIntensity << ",\n";
            file << "      \"lightVolSteps\": " << obj.light.volumetricSteps << ",\n";
            file << "      \"lightVolMaxDist\": " << obj.light.volumetricMaxDistance << ",\n";
            file << "      \"lightEnableDistCulling\": " << BoolStr(obj.light.enableDistanceCulling) << ",\n";
            file << "      \"lightMinVisibleDist\": " << obj.light.minVisibleDistance << ",\n";
            file << "      \"lightMaxVisibleDist\": " << obj.light.maxVisibleDistance << ",\n";
            file << "      \"lightLayer\": " << obj.light.lightLayer << ",\n";

            // Camera Component Properties
            file << "      \"isCamera\": " << BoolStr(obj.isCamera) << ",\n";
            file << "      \"camOrthographic\": " << BoolStr(obj.camera.isOrthographic) << ",\n";
            file << "      \"camFov\": " << obj.camera.fov << ",\n";
            file << "      \"camOrthoSize\": " << obj.camera.orthoSize << ",\n";
            file << "      \"camNearPlane\": " << obj.camera.nearPlane << ",\n";
            file << "      \"camFarPlane\": " << obj.camera.farPlane << ",\n";
            file << "      \"camAspectRatio\": " << obj.camera.aspectRatio << ",\n";

            file << "      \"isWaterBody\": " << BoolStr(obj.isWaterBody) << ",\n";
            file << "      \"waterType\": " << (int)obj.water.type << ",\n";
            file << "      \"waterName\": " << QuoteStr(obj.water.name) << ",\n";
            file << "      \"waterRoughness\": " << obj.water.roughness << ",\n";
            file << "      \"waterMetallic\": " << obj.water.metallic << ",\n";
            file << "      \"waterOpacity\": " << obj.water.opacity << ",\n";
            file << "      \"waterEnableDepth\": " << BoolStr(obj.water.enableDepth) << ",\n";
            file << "      \"waterWaveSpeed\": " << obj.water.waveSpeed << ",\n";
            file << "      \"waterWaveScale\": " << obj.water.waveScale << ",\n";
            file << "      \"waterWaveHeight\": " << obj.water.waveHeight << ",\n";
            file << "      \"waterNormalScale\": " << obj.water.normalScale << ",\n";
            file << "      \"waterDepthRange\": " << obj.water.depthRange << ",\n";
            file << "      \"waterFoamDistance\": " << obj.water.foamDistance << ",\n";
            file << "      \"waterFoamStrength\": " << obj.water.foamStrength << ",\n";
            file << "      \"waterFoamSpeed\": " << obj.water.foamSpeed << ",\n";
            file << "      \"waterFresnelStrength\": " << obj.water.fresnelStrength << ",\n";
            file << "      \"waterSpecularPower\": " << obj.water.specularPower << ",\n";
            file << "      \"waterSparklePower\": " << obj.water.sparklePower << ",\n";
            file << "      \"waterSimEnabled\": " << BoolStr(obj.water.simulationEnabled) << ",\n";
            file << "      \"waterWaveDamping\": " << obj.water.waveDamping << ",\n";
            file << "      \"waterDisturbanceStrength\": " << obj.water.disturbanceStrength << ",\n";
            file << "      \"waterHeightScale\": " << obj.water.heightScale << ",\n";
            file << "      \"waterColor\": [" << obj.water.waterColor.r << ", " << obj.water.waterColor.g << ", " << obj.water.waterColor.b << ", " << obj.water.waterColor.a << "],\n";
            file << "      \"waterShallowColor\": [" << obj.water.shallowColor.r << ", " << obj.water.shallowColor.g << ", " << obj.water.shallowColor.b << ", " << obj.water.shallowColor.a << "],\n";
            file << "      \"waterChoppiness\": " << obj.water.choppiness << ",\n";
            file << "      \"waterOceanExtent\": " << obj.water.oceanExtent << ",\n";
            file << "      \"waterIsCameraCentered\": " << BoolStr(obj.water.isCameraCentered) << ",\n";
            file << "      \"waterDebugMode\": " << obj.water.debugMode << ",\n";

            file << "      \"isTerrain\": " << BoolStr(obj.isTerrain || obj.type == PrimitiveType::Terrain) << ",\n";
            if (obj.isTerrain || obj.type == PrimitiveType::Terrain) {
                file << "      \"terrainWidth\": " << obj.terrain.width << ",\n";
                file << "      \"terrainLength\": " << obj.terrain.length << ",\n";
                file << "      \"terrainResolution\": " << obj.terrain.resolution << ",\n";
                file << "      \"terrainHeightScale\": " << obj.terrain.heightScale << ",\n";
                file << "      \"terrainBrushType\": " << (int)obj.terrain.brushType << ",\n";
                file << "      \"terrainBrushFalloff\": " << (int)obj.terrain.brushFalloff << ",\n";
                file << "      \"terrainBrushSize\": " << obj.terrain.brushSize << ",\n";
                file << "      \"terrainBrushStrength\": " << obj.terrain.brushStrength << ",\n";
                file << "      \"terrainFlattenTarget\": " << obj.terrain.flattenTargetHeight << ",\n";
                file << "      \"terrainNoiseScale\": " << obj.terrain.noiseScale << ",\n";
                file << "      \"terrainHeights\": [";
                for (size_t hi = 0; hi < obj.terrain.heightmap.size(); ++hi) {
                    file << obj.terrain.heightmap[hi];
                    if (hi + 1 < obj.terrain.heightmap.size()) file << ",";
                }
                file << "],\n";

                file << "      \"terrainMaterialLayers\": [\n";
                for (size_t li = 0; li < obj.terrain.materialLayers.size(); ++li) {
                    const auto& l = obj.terrain.materialLayers[li];
                    file << "        {\n";
                    file << "          \"name\": " << QuoteStr(l.name) << ",\n";
                    file << "          \"path\": " << QuoteStr(l.path) << ",\n";
                    file << "          \"baseColor\": [" << l.baseColor.r << ", " << l.baseColor.g << ", " << l.baseColor.b << "],\n";
                    file << "          \"metallic\": " << l.metallic << ",\n";
                    file << "          \"roughness\": " << l.roughness << ",\n";
                    file << "          \"normalStrength\": " << l.normalStrength << ",\n";
                    file << "          \"specular\": " << l.specular << ",\n";
                    file << "          \"uvScale\": [" << l.uvScale.x << ", " << l.uvScale.y << "],\n";
                    file << "          \"baseColorTexture\": " << QuoteStr(l.baseColorTexture) << ",\n";
                    file << "          \"normalTexture\": " << QuoteStr(l.normalTexture) << ",\n";
                    file << "          \"roughnessTexture\": " << QuoteStr(l.roughnessTexture) << ",\n";
                    file << "          \"metallicTexture\": " << QuoteStr(l.metallicTexture) << ",\n";
                    file << "          \"aoTexture\": " << QuoteStr(l.aoTexture) << "\n";
                    file << "        }" << (li + 1 < obj.terrain.materialLayers.size() ? "," : "") << "\n";
                }
                file << "      ],\n";

                file << "      \"terrainLayerWeights\": [\n";
                for (size_t l = 0; l < obj.terrain.layerWeights.size(); ++l) {
                    file << "        [";
                    const auto& wList = obj.terrain.layerWeights[l];
                    for (size_t wi = 0; wi < wList.size(); ++wi) {
                        file << wList[wi];
                        if (wi + 1 < wList.size()) file << ",";
                    }
                    file << "]" << (l + 1 < obj.terrain.layerWeights.size() ? "," : "") << "\n";
                }
                file << "      ],\n";
            }

            file << "      \"isUserInterface\": " << BoolStr(obj.isUserInterface || obj.type == PrimitiveType::UserInterface) << ",\n";
            file << "      \"uiAssetPath\": " << QuoteStr(obj.ui3d.uiAssetPath) << ",\n";
            file << "      \"uiSpace\": " << (int)obj.ui3d.space << ",\n";
            file << "      \"uiSize\": [" << obj.ui3d.size.x << ", " << obj.ui3d.size.y << "],\n";
            file << "      \"uiCanvasResolution\": [" << obj.ui3d.canvasResolution.x << ", " << obj.ui3d.canvasResolution.y << "],\n";
            file << "      \"uiTwoSided\": " << BoolStr(obj.ui3d.twoSided) << ",\n";
            file << "      \"uiOpacity\": " << obj.ui3d.opacity << ",\n";
            file << "      \"uiInteractable\": " << BoolStr(obj.ui3d.interactable) << ",\n";
            file << "      \"uiBackgroundColor\": [" << obj.ui3d.backgroundColor.r << ", " << obj.ui3d.backgroundColor.g << ", " << obj.ui3d.backgroundColor.b << ", " << obj.ui3d.backgroundColor.a << "],\n";
            file << "      \"uiBorderColor\": [" << obj.ui3d.borderColor.r << ", " << obj.ui3d.borderColor.g << ", " << obj.ui3d.borderColor.b << ", " << obj.ui3d.borderColor.a << "],\n";
            file << "      \"uiBorderWidth\": " << obj.ui3d.borderWidth << ",\n";

            file << "      \"hasLevelAnimation\": " << BoolStr(obj.hasLevelAnimation) << ",\n";
            if (obj.hasLevelAnimation) {
                file << "      \"levelAnimationEnabled\": " << BoolStr(obj.levelAnimation.enabled) << ",\n";
                file << "      \"levelAnimationPath\": " << QuoteStr(obj.levelAnimation.animationPath) << ",\n";
                file << "      \"levelAnimationPlayOnStart\": " << BoolStr(obj.levelAnimation.playOnStart) << ",\n";
                file << "      \"levelAnimationLoop\": " << BoolStr(obj.levelAnimation.loop) << ",\n";
                file << "      \"levelAnimationPlaybackSpeed\": " << obj.levelAnimation.playbackSpeed << ",\n";
            }
            file << "      \"hasAnimator\": " << BoolStr(obj.hasAnimator) << ",\n";
            if (obj.hasAnimator) {
                file << "      \"animatorEnabled\": " << BoolStr(obj.animator.enabled) << ",\n";
                file << "      \"animatorGraphPath\": " << QuoteStr(obj.animator.graphPath) << ",\n";
            }
            file << "      \"hasPhysics\": " << BoolStr(obj.hasPhysics) << ",\n";
            if (obj.hasPhysics) {
                file << "      \"physicsEnabled\": " << BoolStr(obj.physics.enabled) << ",\n";
                file << "      \"physicsIsCharacterPhysics\": " << BoolStr(obj.physics.isCharacterPhysics) << ",\n";
                file << "      \"physicsEnableStepBackImpulse\": " << BoolStr(obj.physics.enableStepBackImpulse) << ",\n";
                file << "      \"physicsStepBackImpulseMultiplier\": " << obj.physics.stepBackImpulseMultiplier << ",\n";
                file << "      \"physicsUseGravity\": " << BoolStr(obj.physics.useGravity) << ",\n";
                file << "      \"physicsGravityMultiplier\": " << obj.physics.gravityMultiplier << ",\n";
                file << "      \"physicsMass\": " << obj.physics.mass << ",\n";
                file << "      \"physicsLinearDamping\": " << obj.physics.linearDamping << ",\n";
                file << "      \"physicsAngularDamping\": " << obj.physics.angularDamping << ",\n";
                file << "      \"physicsBounciness\": " << obj.physics.bounciness << ",\n";
                file << "      \"physicsFriction\": " << obj.physics.friction << ",\n";
                file << "      \"physicsIsKinematic\": " << BoolStr(obj.physics.isKinematic) << ",\n";
                file << "      \"physicsLockPosX\": " << BoolStr(obj.physics.lockPositionX) << ",\n";
                file << "      \"physicsLockPosY\": " << BoolStr(obj.physics.lockPositionY) << ",\n";
                file << "      \"physicsLockPosZ\": " << BoolStr(obj.physics.lockPositionZ) << ",\n";
                file << "      \"physicsLockRotX\": " << BoolStr(obj.physics.lockRotationX) << ",\n";
                file << "      \"physicsLockRotY\": " << BoolStr(obj.physics.lockRotationY) << ",\n";
                file << "      \"physicsLockRotZ\": " << BoolStr(obj.physics.lockRotationZ) << ",\n";
            }
            file << "      \"modifiers\": [\n";
            for (size_t mi = 0; mi < obj.modifiers.size(); ++mi) {
                const auto& mod = obj.modifiers[mi];
                file << "        {\n";
                file << "          \"name\": " << QuoteStr(mod.name) << ",\n";
                file << "          \"enabled\": " << BoolStr(mod.enabled) << ",\n";
                file << "          \"shape\": " << (int)mod.shapeType << ",\n";
                file << "          \"isTrigger\": " << BoolStr(mod.isTrigger) << ",\n";
                file << "          \"boxExtents\": " << WriteVec3(mod.boxExtents) << ",\n";
                file << "          \"radius\": " << mod.radius << ",\n";
                file << "          \"height\": " << mod.height << ",\n";
                file << "          \"planeSize\": [" << mod.planeSize.x << ", " << mod.planeSize.y << "],\n";
                file << "          \"centerOffset\": " << WriteVec3(mod.centerOffset) << ",\n";
                file << "          \"friction\": " << mod.friction << ",\n";
                file << "          \"restitution\": " << mod.restitution << ",\n";
                file << "          \"preset\": " << QuoteStr(mod.preset) << ",\n";
                file << "          \"showWireframe\": " << BoolStr(mod.showWireframe) << ",\n";
                file << "          \"wireframeColor\": [" << mod.wireframeColor.r << ", " << mod.wireframeColor.g << ", " << mod.wireframeColor.b << ", " << mod.wireframeColor.a << "],\n";
                file << "          \"isDefaultFit\": " << BoolStr(mod.isDefaultFit) << ",\n";
                file << "          \"preventSideFall\": " << BoolStr(mod.preventSideFall) << ",\n";
                file << "          \"sideFall\": " << BoolStr(mod.sideFall) << "\n";
                file << "        }" << (mi + 1 < obj.modifiers.size() ? "," : "") << "\n";
            }
            file << "      ],\n";

            // Attached Behaviours (dev.md Section 14)
            file << "      \"behaviours\": [\n";
            for (size_t bi = 0; bi < obj.behaviours.size(); ++bi) {
                const auto& b = obj.behaviours[bi];
                if (!b) continue;
                file << "        {\n";
                file << "          \"className\": " << QuoteStr(b->GetClassName()) << ",\n";
                file << "          \"enabled\": " << BoolStr(b->IsEnabled()) << ",\n";
                if (!b->GetSourceCppPath().empty()) {
                    file << "          \"assetPath\": " << QuoteStr(b->GetSourceCppPath()) << ",\n";
                }
                file << "          \"properties\": [\n";
                const auto& props = b->GetProperties();
                for (size_t pi = 0; pi < props.size(); ++pi) {
                    const auto& p = props[pi];
                    file << "            {\n";
                    file << "              \"name\": " << QuoteStr(p.name) << ",\n";
                    file << "              \"type\": " << (int)p.type << ",\n";
                    if (p.type == BehaviourPropertyType::Bool && p.dataPtr) {
                        file << "              \"value\": " << BoolStr(*reinterpret_cast<bool*>(p.dataPtr)) << "\n";
                    } else if (p.type == BehaviourPropertyType::Int && p.dataPtr) {
                        file << "              \"value\": " << *reinterpret_cast<int*>(p.dataPtr) << "\n";
                    } else if (p.type == BehaviourPropertyType::Float && p.dataPtr) {
                        file << "              \"value\": " << *reinterpret_cast<float*>(p.dataPtr) << "\n";
                    } else if (p.type == BehaviourPropertyType::Double && p.dataPtr) {
                        file << "              \"value\": " << *reinterpret_cast<double*>(p.dataPtr) << "\n";
                    } else if (p.type == BehaviourPropertyType::String && p.dataPtr) {
                        file << "              \"value\": " << QuoteStr(*reinterpret_cast<std::string*>(p.dataPtr)) << "\n";
                    } else if (p.type == BehaviourPropertyType::Vec2 && p.dataPtr) {
                        glm::vec2 v = *reinterpret_cast<glm::vec2*>(p.dataPtr);
                        file << "              \"value\": [" << v.x << ", " << v.y << "]\n";
                    } else if ((p.type == BehaviourPropertyType::Vec3 || p.type == BehaviourPropertyType::Color3) && p.dataPtr) {
                        glm::vec3 v = *reinterpret_cast<glm::vec3*>(p.dataPtr);
                        file << "              \"value\": [" << v.x << ", " << v.y << ", " << v.z << "]\n";
                    } else if (p.type == BehaviourPropertyType::ObjectRef) {
                        file << "              \"targetId\": " << p.targetId << ",\n";
                        file << "              \"refType\": " << (int)p.refType << ",\n";
                        file << "              \"assetPath\": " << QuoteStr(p.assetPath) << "\n";
                    } else {
                        file << "              \"value\": 0\n";
                    }
                    file << "            }" << (pi + 1 < props.size() ? "," : "") << "\n";
                }
                file << "          ]\n";
                file << "        }" << (bi + 1 < obj.behaviours.size() ? "," : "") << "\n";
            }
            file << "      ]\n";
            file << "    }" << (i + 1 < scene.objects.size() ? "," : "") << "\n";
        }
        file << "  ],\n";

        // Scene metadata
        file << "  \"nextId\": " << scene.nextId << ",\n";
        file << "  \"activeLevelCameraId\": " << scene.activeLevelCameraId << ",\n";
        file << "  \"playAnimations\": " << BoolStr(scene.playAnimations) << ",\n";

        // Tree view states (Outliner and Content Browser)
        file << "  \"outlinerExpandedNodes\": [\n";
        {
            size_t count = 0;
            for (const auto& kv : scene.outlinerNodeOpenState) {
                if (kv.second) count++;
            }
            size_t idx = 0;
            for (const auto& kv : scene.outlinerNodeOpenState) {
                if (!kv.second) continue;
                file << "    " << QuoteStr(kv.first) << (idx + 1 < count ? "," : "") << "\n";
                idx++;
            }
        }
        file << "  ],\n";

        file << "  \"outlinerCollapsedNodes\": [\n";
        {
            size_t count = 0;
            for (const auto& kv : scene.outlinerNodeOpenState) {
                if (!kv.second) count++;
            }
            size_t idx = 0;
            for (const auto& kv : scene.outlinerNodeOpenState) {
                if (kv.second) continue;
                file << "    " << QuoteStr(kv.first) << (idx + 1 < count ? "," : "") << "\n";
                idx++;
            }
        }
        file << "  ],\n";

        file << "  \"contentBrowserExpandedFolders\": [\n";
        {
            size_t count = 0;
            for (const auto& kv : scene.contentBrowserFolderOpenState) {
                if (kv.second) count++;
            }
            size_t idx = 0;
            for (const auto& kv : scene.contentBrowserFolderOpenState) {
                if (!kv.second) continue;
                file << "    " << QuoteStr(kv.first) << (idx + 1 < count ? "," : "") << "\n";
                idx++;
            }
        }
        file << "  ],\n";

        file << "  \"contentBrowserCollapsedFolders\": [\n";
        {
            size_t count = 0;
            for (const auto& kv : scene.contentBrowserFolderOpenState) {
                if (!kv.second) count++;
            }
            size_t idx = 0;
            for (const auto& kv : scene.contentBrowserFolderOpenState) {
                if (kv.second) continue;
                file << "    " << QuoteStr(kv.first) << (idx + 1 < count ? "," : "") << "\n";
                idx++;
            }
        }
        file << "  ]\n";

        file << "}\n";
        file.close();

        // Atomically replace target JSON file from temporary file to prevent level data corruption/loss
        std::error_code ec;
        std::filesystem::remove(filePath, ec);
        std::filesystem::rename(tempPath, filePath, ec);
        if (ec) {
            std::filesystem::copy_file(tempPath, filePath, std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::remove(tempPath, ec);
        }

        // WickedEngine Fast Binary Archive Technique:
        // Automatically save pre-parsed binary cache side-by-side (.bin) for instant loading
        std::filesystem::path binP(filePath);
        std::string autoBin = (binP.parent_path() / (binP.stem().string() + ".bin")).string();
        std::string autoBinTmp = autoBin + ".tmp";
        SaveFastMeshCache(scene, autoBinTmp);
        std::filesystem::remove(autoBin, ec);
        std::filesystem::rename(autoBinTmp, autoBin, ec);
        if (ec) {
            std::filesystem::copy_file(autoBinTmp, autoBin, std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::remove(autoBinTmp, ec);
        }

        return true;
    }

    // Save level alias
    static bool SaveLevel(const Scene& level, const std::string& filePath, SceneProgressFn onProgress = nullptr) {
        return SaveScene(level, filePath, onProgress);
    }

    // Load a scene from a JSON file
    static bool LoadScene(Scene& scene, const std::string& filePath, SceneProgressFn onProgress = nullptr, const std::string& binPath = "") {
        if (onProgress) onProgress(0.1f, "Opening level file", filePath);
        std::string actualPath = filePath;
        std::error_code ec;
        if (!std::filesystem::exists(actualPath, ec) || std::filesystem::file_size(actualPath, ec) == 0) {
            std::filesystem::path resolved = AssetManager::Get().ResolvePhysicalOrCookedPath(filePath);
            if (!resolved.empty() && std::filesystem::exists(resolved, ec) && std::filesystem::file_size(resolved, ec) > 0) {
                actualPath = resolved.string();
            }
        }
        std::ifstream file(actualPath);
        if (!file.is_open()) return false;

        // Read entire file
        std::stringstream ss;
        ss << file.rdbuf();
        file.close();
        std::string content = ss.str();

        if (onProgress) onProgress(0.20f, "Resetting scene graph", "Preparing container");
        // Clear the scene first
        scene.objects.clear();
        scene.pointLights.clear();
        scene.selectedId = -1;

        // Reset Local Save Store for the newly loaded level
        SaveSystem.OnLevelLoaded(filePath);

        // Addressables Asynchronous Preload (Models & Textures)
        std::vector<std::string> modelAddresses;
        std::vector<std::pair<std::string, TextureUsageSlot>> textureAddresses;
        ExtractLevelAddressableAssets(content, modelAddresses, textureAddresses);
        if (!modelAddresses.empty() || !textureAddresses.empty()) {
            if (onProgress) onProgress(0.25f, "Addressables Preload", "Pre-decoding models & textures in parallel");
            Eunoia::Addressables::PreloadLevelAssetsAsync(
                modelAddresses,
                textureAddresses,
                [&](float subProg, const std::string& item, const std::string& cat) {
                    if (onProgress) {
                        onProgress(0.25f + 0.08f * subProg, "Preloading Addressable " + cat, std::filesystem::path(item).filename().string());
                    }
                }
            );
        }

        if (onProgress) onProgress(0.35f, "Loading Skybox & Environment", "Ambiance & clear color");
        ParseSkyboxAndEnvironment(content, scene);

        if (onProgress) onProgress(0.50f, "Loading Lights", "Sun light & point lights");
        ParseLights(content, scene);

        if (onProgress) onProgress(0.70f, "Loading Meshes", "Loading geometry, materials & hierarchy");
        std::string effectiveBin = binPath;
        if (effectiveBin.empty()) {
            std::filesystem::path p(filePath);
            std::filesystem::path cand = p.parent_path() / (p.stem().string() + ".bin");
            if (std::filesystem::exists(cand)) {
                effectiveBin = cand.string();
            }
        }
        ParseActors(content, scene, effectiveBin);

        // WickedEngine Fast Binary Archive Technique:
        // If pre-parsed binary cache was missing, generate it now so all subsequent loads are instant
        if (effectiveBin.empty() || !std::filesystem::exists(effectiveBin)) {
            std::filesystem::path p(filePath);
            std::string autoBin = (p.parent_path() / (p.stem().string() + ".bin")).string();
            SaveFastMeshCache(scene, autoBin);
        }

        if (onProgress) onProgress(0.85f, "Applying Post-Processing", "Color grading & camera");
        ParsePostProcessing(content, scene);

        if (onProgress) onProgress(0.95f, "Finalizing scene metadata", std::to_string(scene.objects.size()) + " actors loaded");
        ParseMetadata(content, scene);

        // Select first actor if available
        if (!scene.objects.empty()) {
            scene.selectedId = scene.objects.front().id;
        }
        if (onProgress) onProgress(1.0f, "Level loaded successfully", filePath);

        return true;
    }

private:
    // ---- JSON Writing Helpers ----
    static std::string QuoteStr(const std::string& s) {
        std::string escaped;
        escaped += '"';
        for (char c : s) {
            if (c == '"') escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else if (c == '\n') escaped += "\\n";
            else escaped += c;
        }
        escaped += '"';
        return escaped;
    }

    static std::string BoolStr(bool b) { return b ? "true" : "false"; }

    static std::string WriteVec3(const glm::vec3& v) {
        std::ostringstream os;
        os << "[" << v.x << ", " << v.y << ", " << v.z << "]";
        return os.str();
    }

    static std::string WriteVec4(const glm::vec4& v) {
        std::ostringstream os;
        os << "[" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << "]";
        return os.str();
    }

    static std::string MobilityToStr(Mobility m) {
        switch (m) {
            case Mobility::Static: return "Static";
            case Mobility::Stationary: return "Stationary";
            case Mobility::Movable: return "Movable";
            default: return "Movable";
        }
    }

    static Mobility StrToMobility(const std::string& s) {
        if (s == "Static") return Mobility::Static;
        if (s == "Stationary") return Mobility::Stationary;
        return Mobility::Movable;
    }

    static PrimitiveType StrToPrimitive(const std::string& s) {
        if (s == "Cube") return PrimitiveType::Cube;
        if (s == "Plane") return PrimitiveType::Plane;
        if (s == "Box") return PrimitiveType::Box;
        if (s == "Sphere") return PrimitiveType::Sphere;
        if (s == "UVSphere" || s == "UV Sphere") return PrimitiveType::UVSphere;
        if (s == "Icosphere") return PrimitiveType::Icosphere;
        if (s == "Cylinder") return PrimitiveType::Cylinder;
        if (s == "Cone") return PrimitiveType::Cone;
        if (s == "Capsule") return PrimitiveType::Capsule;
        if (s == "Torus") return PrimitiveType::Torus;
        if (s == "Circle") return PrimitiveType::Circle;
        if (s == "Disc") return PrimitiveType::Disc;
        if (s == "Quad") return PrimitiveType::Quad;
        if (s == "Triangle") return PrimitiveType::Triangle;
        if (s == "Pyramid") return PrimitiveType::Pyramid;
        if (s == "Prism") return PrimitiveType::Prism;
        if (s == "Mesh" || s == "ImportedMesh") return PrimitiveType::ImportedMesh;
        if (s == "Empty" || s == "Empty Actor") return PrimitiveType::Empty;
        if (s == "DirectionalLight" || s == "Directional Light") return PrimitiveType::DirectionalLight;
        if (s == "PointLight" || s == "Point Light") return PrimitiveType::PointLight;
        if (s == "SpotLight" || s == "Spot Light") return PrimitiveType::SpotLight;
        if (s == "AreaLight" || s == "Area Light") return PrimitiveType::AreaLight;
        if (s == "SkyLight" || s == "Sky Light" || s == "AmbientLight" || s == "Ambient Light" || s == "HemisphereLight" || s == "Hemisphere Light") return PrimitiveType::SkyLight;
        if (s == "TubeLight" || s == "Tube Light" || s == "DiscLight" || s == "Disc Light") return PrimitiveType::AreaLight;
        if (s == "Camera") return PrimitiveType::Camera;
        if (s == "UserInterface" || s == "User Interface" || s == "UI" || s == "UserInterface3D" || s == "UI3D") return PrimitiveType::UserInterface;
        if (s == "LevelAnimation" || s == "Level Animation") return PrimitiveType::LevelAnimation;
        return PrimitiveType::Cube;
    }

    // ---- JSON Parsing Helpers ----

    // Extract a quoted string value for a given key
    static std::string ParseString(const std::string& json, const std::string& key, const std::string& def = "") {
        size_t pos = json.find(key);
        if (pos == std::string::npos) return def;
        pos = json.find(':', pos + key.size());
        if (pos == std::string::npos) return def;
        pos = json.find('"', pos + 1);
        if (pos == std::string::npos) return def;
        pos++; // skip opening quote
        std::string result;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                pos++;
                if (json[pos] == '"') result += '"';
                else if (json[pos] == '\\') result += '\\';
                else if (json[pos] == 'n') result += '\n';
                else result += json[pos];
            } else {
                result += json[pos];
            }
            pos++;
        }
        return result;
    }

    // Extract a float value for a given key
    static float ParseFloat(const std::string& json, const std::string& key, float def = 0.0f) {
        size_t pos = json.find(key);
        if (pos == std::string::npos) return def;
        pos = json.find(':', pos + key.size());
        if (pos == std::string::npos) return def;
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        try { return std::stof(json.substr(pos, 30)); }
        catch (...) { return def; }
    }

    // Extract an int value for a given key
    static int ParseInt(const std::string& json, const std::string& key, int def = 0) {
        size_t pos = json.find(key);
        if (pos == std::string::npos) return def;
        pos = json.find(':', pos + key.size());
        if (pos == std::string::npos) return def;
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        try { return std::stoi(json.substr(pos, 20)); }
        catch (...) { return def; }
    }

    // Extract a bool value for a given key
    static bool ParseBool(const std::string& json, const std::string& key, bool def = false) {
        size_t pos = json.find(key);
        if (pos == std::string::npos) return def;
        pos = json.find(':', pos + key.size());
        if (pos == std::string::npos) return def;
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        if (pos < json.size() && json[pos] == 't') return true;
        if (pos < json.size() && json[pos] == 'f') return false;
        return def;
    }

    // Parse a JSON array of numbers: [x, y, z] or [x, y, z, w]
    static glm::vec3 ParseVec3(const std::string& json, size_t startPos) {
        glm::vec3 v(0.0f);
        size_t bracket = json.find('[', startPos);
        if (bracket == std::string::npos) return v;
        bracket++;
        try {
            size_t next;
            v.x = std::stof(json.substr(bracket), &next); bracket += next;
            while (bracket < json.size() && (json[bracket] == ',' || json[bracket] == ' ')) bracket++;
            v.y = std::stof(json.substr(bracket), &next); bracket += next;
            while (bracket < json.size() && (json[bracket] == ',' || json[bracket] == ' ')) bracket++;
            v.z = std::stof(json.substr(bracket), &next);
        } catch (...) {}
        return v;
    }

    static glm::vec4 ParseVec4(const std::string& json, size_t startPos) {
        glm::vec4 v(0.0f);
        size_t bracket = json.find('[', startPos);
        if (bracket == std::string::npos) return v;
        bracket++;
        try {
            size_t next;
            v.x = std::stof(json.substr(bracket), &next); bracket += next;
            while (bracket < json.size() && (json[bracket] == ',' || json[bracket] == ' ')) bracket++;
            v.y = std::stof(json.substr(bracket), &next); bracket += next;
            while (bracket < json.size() && (json[bracket] == ',' || json[bracket] == ' ')) bracket++;
            v.z = std::stof(json.substr(bracket), &next); bracket += next;
            while (bracket < json.size() && (json[bracket] == ',' || json[bracket] == ' ')) bracket++;
            v.w = std::stof(json.substr(bracket), &next);
        } catch (...) {}
        return v;
    }

    // Find key position within a specific region of json
    static size_t FindKeyInRegion(const std::string& json, const std::string& key, size_t regionStart, size_t regionEnd) {
        size_t pos = json.find(key, regionStart);
        if (pos == std::string::npos || pos > regionEnd) return std::string::npos;
        return pos;
    }

    // Parse a string value at a specific position (after key)
    static std::string ParseStringAt(const std::string& json, size_t keyPos, const std::string& def = "") {
        if (keyPos == std::string::npos) return def;
        size_t pos = json.find(':', keyPos);
        if (pos == std::string::npos) return def;
        pos = json.find('"', pos + 1);
        if (pos == std::string::npos) return def;
        pos++;
        std::string result;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                pos++;
                if (json[pos] == '"') result += '"';
                else if (json[pos] == '\\') result += '\\';
                else if (json[pos] == 'n') result += '\n';
                else result += json[pos];
            } else {
                result += json[pos];
            }
            pos++;
        }
        return result;
    }

    static float ParseFloatAt(const std::string& json, size_t keyPos, float def = 0.0f) {
        if (keyPos == std::string::npos) return def;
        size_t pos = json.find(':', keyPos);
        if (pos == std::string::npos) return def;
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        try { return std::stof(json.substr(pos, 30)); }
        catch (...) { return def; }
    }

    static int ParseIntAt(const std::string& json, size_t keyPos, int def = 0) {
        if (keyPos == std::string::npos) return def;
        size_t pos = json.find(':', keyPos);
        if (pos == std::string::npos) return def;
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        try { return std::stoi(json.substr(pos, 20)); }
        catch (...) { return def; }
    }

    static bool ParseBoolAt(const std::string& json, size_t keyPos, bool def = false) {
        if (keyPos == std::string::npos) return def;
        size_t pos = json.find(':', keyPos);
        if (pos == std::string::npos) return def;
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        if (pos < json.size() && json[pos] == 't') return true;
        if (pos < json.size() && json[pos] == 'f') return false;
        return def;
    }

public:
    // ---- Brace/Bracket Matching ----

    static size_t FindMatchingBrace(const std::string& json, size_t openPos) {
        if (openPos >= json.size() || json[openPos] != '{') return std::string::npos;
        int depth = 1;
        bool inString = false;
        for (size_t i = openPos + 1; i < json.size(); ++i) {
            if (json[i] == '\\' && inString) { i++; continue; }
            if (json[i] == '"') inString = !inString;
            if (!inString) {
                if (json[i] == '{') depth++;
                else if (json[i] == '}') { depth--; if (depth == 0) return i; }
            }
        }
        return std::string::npos;
    }

    static size_t FindMatchingBracket(const std::string& json, size_t openPos) {
        if (openPos >= json.size() || json[openPos] != '[') return std::string::npos;
        int depth = 1;
        bool inString = false;
        for (size_t i = openPos + 1; i < json.size(); ++i) {
            if (json[i] == '\\' && inString) { i++; continue; }
            if (json[i] == '"') inString = !inString;
            if (!inString) {
                if (json[i] == '[') depth++;
                else if (json[i] == ']') { depth--; if (depth == 0) return i; }
            }
        }
        return std::string::npos;
    }

    // ---- Addressables Level Asset Extractor ----

    static void ExtractLevelAddressableAssets(
        const std::string& json,
        std::vector<std::string>& outModels,
        std::vector<std::pair<std::string, TextureUsageSlot>>& outTextures
    ) {
        outModels.clear();
        outTextures.clear();
        if (json.empty()) return;

        // 1. Skybox texture & asset path
        size_t envPos = json.find("\"environment\"");
        if (envPos != std::string::npos) {
            size_t envStart = json.find('{', envPos);
            if (envStart != std::string::npos) {
                size_t envEnd = FindMatchingBrace(json, envStart);
                if (envEnd != std::string::npos) {
                    std::string envBlock = json.substr(envStart, envEnd - envStart + 1);
                    size_t sPos = envBlock.find("\"skybox\"");
                    if (sPos != std::string::npos) {
                        size_t sStart = envBlock.find('{', sPos);
                        if (sStart != std::string::npos) {
                            size_t sEnd = FindMatchingBrace(envBlock, sStart);
                            if (sEnd != std::string::npos) {
                                std::string sBlock = envBlock.substr(sStart, sEnd - sStart + 1);
                                std::string tex = ParseString(sBlock, "\"texturePath\"", "");
                                if (!tex.empty() && tex != "none" && tex != "None") {
                                    outTextures.emplace_back(tex, TextureUsageSlot::Cubemap);
                                }
                                std::string asset = ParseString(sBlock, "\"assetPath\"", "");
                                if (!asset.empty() && asset != "none" && asset != "None") {
                                    outTextures.emplace_back(asset, TextureUsageSlot::Cubemap);
                                }
                            }
                        }
                    }
                }
            }
        }

        // 2. Scan for all meshFilePath occurrences
        size_t curPos = 0;
        while ((curPos = json.find("\"meshFilePath\"", curPos)) != std::string::npos) {
            std::string meshPath = ParseStringAt(json, curPos, "");
            if (!meshPath.empty() && meshPath != "none" && meshPath != "None") {
                outModels.push_back(meshPath);
            }
            curPos += 14;
        }

        // 3. Scan for all material texture slots
        static const std::vector<std::pair<std::string, TextureUsageSlot>> textureSlotKeys = {
            {"\"baseColor\"", TextureUsageSlot::BaseColor},
            {"\"normal\"", TextureUsageSlot::Normal},
            {"\"roughness\"", TextureUsageSlot::Roughness},
            {"\"metallic\"", TextureUsageSlot::Metallic},
            {"\"ao\"", TextureUsageSlot::AO},
            {"\"emission\"", TextureUsageSlot::Emission},
            {"\"opacity\"", TextureUsageSlot::Opacity}
        };

        for (const auto& slotDef : textureSlotKeys) {
            curPos = 0;
            while ((curPos = json.find(slotDef.first, curPos)) != std::string::npos) {
                std::string texPath = ParseStringAt(json, curPos, "");
                if (!texPath.empty() && texPath != "none" && texPath != "None") {
                    outTextures.emplace_back(texPath, slotDef.second);
                }
                curPos += slotDef.first.length();
            }
        }
    }

    // ---- Section Parsers ----

    static void ParseSkyboxAndEnvironment(const std::string& json, Scene& scene) {
        size_t envPos = json.find("\"environment\"");
        if (envPos == std::string::npos) return;

        // Find the environment object boundaries
        size_t envStart = json.find('{', envPos);
        if (envStart == std::string::npos) return;
        size_t envEnd = FindMatchingBrace(json, envStart);

        std::string envBlock = json.substr(envStart, envEnd - envStart + 1);

        // Clear color
        size_t ccPos = envBlock.find("\"clearColor\"");
        if (ccPos != std::string::npos) {
            scene.clearColor = ParseVec4(envBlock, ccPos);
        }

        // Ambient
        size_t aiPos = envBlock.find("\"ambientIntensity\"");
        if (aiPos != std::string::npos) {
            scene.ambientIntensity = ParseFloatAt(envBlock, aiPos, 0.0f);
        }

        // Grid
        size_t gridPos = envBlock.find("\"grid\"");
        if (gridPos != std::string::npos) {
            size_t gStart = envBlock.find('{', gridPos);
            size_t gEnd = FindMatchingBrace(envBlock, gStart);
            std::string gBlock = envBlock.substr(gStart, gEnd - gStart + 1);

            size_t p;
            p = gBlock.find("\"show\""); if (p != std::string::npos) scene.showGrid = ParseBoolAt(gBlock, p, true);
            p = gBlock.find("\"size\""); if (p != std::string::npos) scene.gridSize = ParseFloatAt(gBlock, p, 16.0f);
            p = gBlock.find("\"divisions\""); if (p != std::string::npos) scene.gridDivisions = ParseIntAt(gBlock, p, 16);
            p = gBlock.find("\"color\""); if (p != std::string::npos) scene.gridColor = ParseVec3(gBlock, p);
        }

        // Skybox
        size_t skyPos = envBlock.find("\"skybox\"");
        if (skyPos != std::string::npos) {
            size_t sStart = envBlock.find('{', skyPos);
            size_t sEnd = FindMatchingBrace(envBlock, sStart);
            std::string sBlock = envBlock.substr(sStart, sEnd - sStart + 1);

            size_t p;
            p = sBlock.find("\"enabled\""); if (p != std::string::npos) scene.enableSkybox = ParseBoolAt(sBlock, p, true);
            p = sBlock.find("\"assetPath\""); if (p != std::string::npos) scene.skyboxAssetPath = ParseString(sBlock, "\"assetPath\"", "");
            p = sBlock.find("\"texturePath\""); if (p != std::string::npos) scene.skyboxTexturePath = ParseString(sBlock, "\"texturePath\"", "");
            p = sBlock.find("\"exposure\""); if (p != std::string::npos) scene.skyboxExposure = ParseFloatAt(sBlock, p, 1.0f);
            p = sBlock.find("\"rotation\""); if (p != std::string::npos) scene.skyboxRotation = ParseFloatAt(sBlock, p, 0.0f);
            p = sBlock.find("\"autoRotate\""); if (p != std::string::npos) scene.skyboxAutoRotate = ParseBoolAt(sBlock, p, false);
            p = sBlock.find("\"autoRotateSpeed\""); if (p != std::string::npos) scene.skyboxAutoRotateSpeed = ParseFloatAt(sBlock, p, 1.0f);
            p = sBlock.find("\"tint\""); if (p != std::string::npos) scene.skyboxTint = ParseVec3(sBlock, p);
            p = sBlock.find("\"ambientContribution\""); if (p != std::string::npos) scene.skyboxAmbientContribution = ParseFloatAt(sBlock, p, 0.5f);
            p = sBlock.find("\"enableLighting\""); if (p != std::string::npos) scene.enableSkyboxLighting = ParseBoolAt(sBlock, p, false);
            p = sBlock.find("\"lightingIntensity\""); if (p != std::string::npos) scene.skyboxLightingIntensity = ParseFloatAt(sBlock, p, 1.0f);
            p = sBlock.find("\"lightingColor\""); if (p != std::string::npos) scene.skyboxLightingColor = ParseVec3(sBlock, p);
            p = sBlock.find("\"lightingSpecular\""); if (p != std::string::npos) scene.skyboxLightingSpecular = ParseFloatAt(sBlock, p, 1.0f);
            p = sBlock.find("\"lightingDiffuse\""); if (p != std::string::npos) scene.skyboxLightingDiffuse = ParseFloatAt(sBlock, p, 1.0f);
        }
    }

    static void ParseLights(const std::string& json, Scene& scene) {
        size_t envPos = json.find("\"environment\"");
        if (envPos != std::string::npos) {
            size_t envStart = json.find('{', envPos);
            if (envStart != std::string::npos) {
                size_t envEnd = FindMatchingBrace(json, envStart);
                std::string envBlock = json.substr(envStart, envEnd - envStart + 1);

                // Sun light
                size_t sunPos = envBlock.find("\"sunLight\"");
                if (sunPos != std::string::npos) {
                    size_t sunStart = envBlock.find('{', sunPos);
                    size_t sunEnd = FindMatchingBrace(envBlock, sunStart);
                    std::string sunBlock = envBlock.substr(sunStart, sunEnd - sunStart + 1);

                    size_t dPos = sunBlock.find("\"direction\"");
                    if (dPos != std::string::npos) scene.lightDirection = glm::normalize(ParseVec3(sunBlock, dPos));
                    size_t cPos = sunBlock.find("\"color\"");
                    if (cPos != std::string::npos) scene.lightColor = ParseVec3(sunBlock, cPos);
                    size_t iPos = sunBlock.find("\"intensity\"");
                    if (iPos != std::string::npos) scene.lightIntensity = ParseFloatAt(sunBlock, iPos, 1.0f);
                }

                // Shadows (WickedEngine Shadow Architecture Alignment)
                size_t shadowPos = envBlock.find("\"shadows\"");
                if (shadowPos != std::string::npos) {
                    size_t sStart = envBlock.find('{', shadowPos);
                    size_t sEnd = FindMatchingBrace(envBlock, sStart);
                    std::string sBlock = envBlock.substr(sStart, sEnd - sStart + 1);

                    size_t p;
                    p = sBlock.find("\"enabled\""); if (p != std::string::npos) scene.enableShadows = ParseBoolAt(sBlock, p, true);
                    p = sBlock.find("\"type\""); if (p != std::string::npos) scene.shadowType = ParseIntAt(sBlock, p, 0);
                    p = sBlock.find("\"resolution2D\""); if (p != std::string::npos) { scene.shadowResolution2D = ParseIntAt(sBlock, p, 2048); scene.shadowResolution = scene.shadowResolution2D; }
                    p = sBlock.find("\"resolutionCube\""); if (p != std::string::npos) scene.shadowResolutionCube = ParseIntAt(sBlock, p, 512);
                    p = sBlock.find("\"sampleCount\""); if (p != std::string::npos) scene.shadowSampleCount = ParseIntAt(sBlock, p, 16);
                    p = sBlock.find("\"pcss\""); if (p != std::string::npos) scene.shadowPCSS = ParseBoolAt(sBlock, p, false);
                    p = sBlock.find("\"distance\""); if (p != std::string::npos) scene.shadowDistance = ParseFloatAt(sBlock, p, 250.0f);
                    p = sBlock.find("\"strength\""); if (p != std::string::npos) scene.shadowStrength = ParseFloatAt(sBlock, p, 0.85f);
                    p = sBlock.find("\"bias\""); if (p != std::string::npos) scene.shadowBias = ParseFloatAt(sBlock, p, 0.0012f);
                    p = sBlock.find("\"pcfRadius\""); if (p != std::string::npos) scene.pcfRadius = ParseFloatAt(sBlock, p, 1.2f);
                    p = sBlock.find("\"softness\""); if (p != std::string::npos) scene.shadowSoftness = ParseFloatAt(sBlock, p, 1.0f);
                    p = sBlock.find("\"cascadeCount\""); if (p != std::string::npos) scene.cascadeCount = ParseIntAt(sBlock, p, 3);
                    p = sBlock.find("\"cascade0\""); if (p != std::string::npos) scene.cascadeDistances[0] = ParseFloatAt(sBlock, p, 10.0f);
                    p = sBlock.find("\"cascade1\""); if (p != std::string::npos) scene.cascadeDistances[1] = ParseFloatAt(sBlock, p, 35.0f);
                    p = sBlock.find("\"cascade2\""); if (p != std::string::npos) scene.cascadeDistances[2] = ParseFloatAt(sBlock, p, 100.0f);
                    p = sBlock.find("\"cascade3\""); if (p != std::string::npos) scene.cascadeDistances[3] = ParseFloatAt(sBlock, p, 250.0f);
                    p = sBlock.find("\"cascadeBlending\""); if (p != std::string::npos) scene.cascadeBlending = ParseBoolAt(sBlock, p, true);
                    p = sBlock.find("\"visualizeCascades\""); if (p != std::string::npos) scene.visualizeCascades = ParseBoolAt(sBlock, p, false);
                    p = sBlock.find("\"screenSpaceShadows\""); if (p != std::string::npos) scene.screenSpaceShadows = ParseBoolAt(sBlock, p, true);
                    p = sBlock.find("\"screenSpaceRange\""); if (p != std::string::npos) scene.screenSpaceShadowRange = ParseFloatAt(sBlock, p, 1.0f);
                    p = sBlock.find("\"screenSpaceSamples\""); if (p != std::string::npos) scene.screenSpaceShadowSamples = ParseIntAt(sBlock, p, 16);
                    p = sBlock.find("\"capsuleShadows\""); if (p != std::string::npos) scene.capsuleShadows = ParseBoolAt(sBlock, p, false);
                    p = sBlock.find("\"capsuleAngle\""); if (p != std::string::npos) scene.capsuleShadowAngle = ParseFloatAt(sBlock, p, 45.0f);
                    p = sBlock.find("\"capsuleFade\""); if (p != std::string::npos) scene.capsuleShadowFade = ParseFloatAt(sBlock, p, 0.2f);
                    p = sBlock.find("\"shadowLODOverride\""); if (p != std::string::npos) scene.shadowLODOverride = ParseBoolAt(sBlock, p, false);
                }
            }
        }

        // Point lights
        ParsePointLights(json, scene);
    }

    static void ParsePostProcessing(const std::string& json, Scene& scene) {
        size_t envPos = json.find("\"environment\"");
        if (envPos == std::string::npos) return;

        size_t envStart = json.find('{', envPos);
        if (envStart == std::string::npos) return;
        size_t envEnd = FindMatchingBrace(json, envStart);

        std::string envBlock = json.substr(envStart, envEnd - envStart + 1);

        // Post Processing (Color Grading & Exposure)
        size_t ppPos = envBlock.find("\"postProcessing\"");
        if (ppPos != std::string::npos) {
            size_t pStart = envBlock.find('{', ppPos);
            size_t pEnd = FindMatchingBrace(envBlock, pStart);
            std::string pBlock = envBlock.substr(pStart, pEnd - pStart + 1);

            size_t p;
            p = pBlock.find("\"enabled\""); if (p != std::string::npos) scene.enablePostProcessing = ParseBoolAt(pBlock, p, false);
            p = pBlock.find("\"exposureEV\""); if (p != std::string::npos) scene.postExposureEV = ParseFloatAt(pBlock, p, 0.0f);
            p = pBlock.find("\"exposureMultiplier\""); if (p != std::string::npos) scene.postExposureMultiplier = ParseFloatAt(pBlock, p, 1.0f);
            p = pBlock.find("\"tonemapper\""); if (p != std::string::npos) scene.postTonemapper = (int)ParseFloatAt(pBlock, p, 1.0f);
            p = pBlock.find("\"saturation\""); if (p != std::string::npos) scene.postSaturation = ParseFloatAt(pBlock, p, 1.0f);
            p = pBlock.find("\"contrast\""); if (p != std::string::npos) scene.postContrast = ParseFloatAt(pBlock, p, 1.0f);
            p = pBlock.find("\"gamma\""); if (p != std::string::npos) scene.postGamma = ParseFloatAt(pBlock, p, 1.0f);
            p = pBlock.find("\"colorFilter\""); if (p != std::string::npos) scene.postColorFilter = ParseVec3(pBlock, p);
            p = pBlock.find("\"colorTemperature\""); if (p != std::string::npos) scene.postColorTemperature = ParseFloatAt(pBlock, p, 6500.0f);
            p = pBlock.find("\"colorTint\""); if (p != std::string::npos) scene.postColorTint = ParseFloatAt(pBlock, p, 0.0f);
            p = pBlock.find("\"sharpenAmount\""); if (p != std::string::npos) scene.postSharpenAmount = ParseFloatAt(pBlock, p, 0.0f);
            p = pBlock.find("\"denoiserAmount\""); if (p != std::string::npos) scene.postDenoiserAmount = ParseFloatAt(pBlock, p, 0.0f);
        } else {
            scene.enablePostProcessing = false;
        }

        size_t aoPos = envBlock.find("\"ambientOcclusion\"");
        if (aoPos != std::string::npos) {
            size_t aStart = envBlock.find('{', aoPos);
            size_t aEnd = FindMatchingBrace(envBlock, aStart);
            std::string aBlock = envBlock.substr(aStart, aEnd - aStart + 1);

            size_t p;
            p = aBlock.find("\"enabled\""); if (p != std::string::npos) scene.enableAO = ParseBoolAt(aBlock, p, true);
            p = aBlock.find("\"type\""); if (p != std::string::npos) scene.aoType = (int)ParseFloatAt(aBlock, p, 1.0f);
            p = aBlock.find("\"power\""); if (p != std::string::npos) scene.aoPower = ParseFloatAt(aBlock, p, 1.0f);
            p = aBlock.find("\"range\""); if (p != std::string::npos) scene.aoRange = ParseFloatAt(aBlock, p, 1.0f);
            p = aBlock.find("\"sampleCount\""); if (p != std::string::npos) scene.aoSampleCount = (int)ParseFloatAt(aBlock, p, 16.0f);
            p = aBlock.find("\"bias\""); if (p != std::string::npos) scene.aoBias = ParseFloatAt(aBlock, p, 0.025f);
            p = aBlock.find("\"blur\""); if (p != std::string::npos) scene.aoBlur = ParseBoolAt(aBlock, p, true);
            p = aBlock.find("\"blurRadius\""); if (p != std::string::npos) scene.aoBlurRadius = ParseFloatAt(aBlock, p, 2.0f);
            p = aBlock.find("\"temporal\""); if (p != std::string::npos) scene.aoTemporal = ParseBoolAt(aBlock, p, true);
            p = aBlock.find("\"temporalBlend\""); if (p != std::string::npos) scene.aoTemporalBlend = ParseFloatAt(aBlock, p, 0.85f);
        }

        size_t fogPos = envBlock.find("\"volumetricFog\"");
        if (fogPos != std::string::npos) {
            size_t fStart = envBlock.find('{', fogPos);
            size_t fEnd = FindMatchingBrace(envBlock, fStart);
            std::string fBlock = envBlock.substr(fStart, fEnd - fStart + 1);

            size_t p;
            p = fBlock.find("\"enabled\""); if (p != std::string::npos) scene.enableFog = ParseBoolAt(fBlock, p, false);
            p = fBlock.find("\"start\""); if (p != std::string::npos) scene.fogStart = ParseFloatAt(fBlock, p, 10.0f);
            p = fBlock.find("\"density\""); if (p != std::string::npos) scene.fogDensity = ParseFloatAt(fBlock, p, 0.005f);
            p = fBlock.find("\"heightStart\""); if (p != std::string::npos) scene.fogHeightStart = ParseFloatAt(fBlock, p, 0.0f);
            p = fBlock.find("\"heightEnd\""); if (p != std::string::npos) scene.fogHeightEnd = ParseFloatAt(fBlock, p, 50.0f);
            p = fBlock.find("\"color\""); if (p != std::string::npos) scene.fogColor = ParseVec3(fBlock, p);
        } else {
            scene.enableFog = false;

        size_t gfxPos = envBlock.find("\"graphics\"");
        if (gfxPos != std::string::npos) {
            size_t gStart = envBlock.find('{', gfxPos);
            size_t gEnd = FindMatchingBrace(envBlock, gStart);
            std::string gBlock = envBlock.substr(gStart, gEnd - gStart + 1);

            size_t p;
            p = gBlock.find("\"vsync\""); if (p != std::string::npos) scene.vsync = ParseBoolAt(gBlock, p, true);
            p = gBlock.find("\"displayOutput\""); if (p != std::string::npos) scene.displayOutput = (int)ParseFloatAt(gBlock, p, 0.0f);
            p = gBlock.find("\"resolutionScale\""); if (p != std::string::npos) scene.resolutionScale = ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"streamingThreshold\""); if (p != std::string::npos) scene.streamingThreshold = ParseFloatAt(gBlock, p, 0.8f);
            p = gBlock.find("\"hdrCalibration\""); if (p != std::string::npos) scene.hdrCalibration = ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"hdrUiCalibration\""); if (p != std::string::npos) scene.hdrUiCalibration = ParseFloatAt(gBlock, p, 1.0f);

            p = gBlock.find("\"renderPath\""); if (p != std::string::npos) scene.renderPath = (int)ParseFloatAt(gBlock, p, 0.0f);
            p = gBlock.find("\"pathTraceTargetSamples\""); if (p != std::string::npos) scene.pathTraceTargetSamples = (int)ParseFloatAt(gBlock, p, 1024.0f);
            p = gBlock.find("\"occlusionCulling\""); if (p != std::string::npos) scene.occlusionCulling = ParseBoolAt(gBlock, p, true);
            p = gBlock.find("\"visibilityComputeShading\""); if (p != std::string::npos) scene.visibilityComputeShading = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"allowMeshShader\""); if (p != std::string::npos) scene.allowMeshShader = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"meshletOcclusionCulling\""); if (p != std::string::npos) scene.meshletOcclusionCulling = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"meshBlend\""); if (p != std::string::npos) scene.meshBlend = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"tessellation\""); if (p != std::string::npos) scene.tessellation = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"gameSpeed\""); if (p != std::string::npos) scene.gameSpeed = ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"wireframeMode\""); if (p != std::string::npos) scene.wireframeMode = (int)ParseFloatAt(gBlock, p, 0.0f);

            p = gBlock.find("\"giBoost\""); if (p != std::string::npos) scene.giBoost = ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"surfelGI\""); if (p != std::string::npos) scene.surfelGI = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"surfelGIDebug\""); if (p != std::string::npos) scene.surfelGIDebug = (int)ParseFloatAt(gBlock, p, 0.0f);
            p = gBlock.find("\"ddgi\""); if (p != std::string::npos) scene.ddgi = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"ddgiDebug\""); if (p != std::string::npos) scene.ddgiDebug = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"ddgiGridRes\""); if (p != std::string::npos) {
                glm::vec3 r = ParseVec3(gBlock, p);
                scene.ddgiGridRes = glm::ivec3((int)r.x, (int)r.y, (int)r.z);
            }
            p = gBlock.find("\"ddgiRayCount\""); if (p != std::string::npos) scene.ddgiRayCount = (int)ParseFloatAt(gBlock, p, 64.0f);
            p = gBlock.find("\"ddgiBlendSpeed\""); if (p != std::string::npos) scene.ddgiBlendSpeed = ParseFloatAt(gBlock, p, 0.1f);
            p = gBlock.find("\"ddgiSmoothBackface\""); if (p != std::string::npos) scene.ddgiSmoothBackface = ParseFloatAt(gBlock, p, 0.0f);
            p = gBlock.find("\"vxgi\""); if (p != std::string::npos) scene.vxgi = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"vxgiDebug\""); if (p != std::string::npos) scene.vxgiDebug = (int)ParseFloatAt(gBlock, p, 0.0f);
            p = gBlock.find("\"vxgiReflections\""); if (p != std::string::npos) scene.vxgiReflections = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"vxgiVoxelSize\""); if (p != std::string::npos) scene.vxgiVoxelSize = ParseFloatAt(gBlock, p, 0.125f);
            p = gBlock.find("\"vxgiRayStepSize\""); if (p != std::string::npos) scene.vxgiRayStepSize = ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"vxgiMaxDistance\""); if (p != std::string::npos) scene.vxgiMaxDistance = ParseFloatAt(gBlock, p, 10.0f);
            p = gBlock.find("\"ssgi\""); if (p != std::string::npos) scene.ssgi = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"ssgiDepthRejection\""); if (p != std::string::npos) scene.ssgiDepthRejection = ParseFloatAt(gBlock, p, 8.0f);
            p = gBlock.find("\"rtDiffuse\""); if (p != std::string::npos) scene.rtDiffuse = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"rtDiffuseRange\""); if (p != std::string::npos) scene.rtDiffuseRange = ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"rtDiffuseQuality\""); if (p != std::string::npos) scene.rtDiffuseQuality = (int)ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"raytraceBounceCount\""); if (p != std::string::npos) scene.raytraceBounceCount = (int)ParseFloatAt(gBlock, p, 1.0f);

            p = gBlock.find("\"msaa\""); if (p != std::string::npos) scene.msaa = (int)ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"temporalAA\""); if (p != std::string::npos) scene.temporalAA = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"temporalAADebug\""); if (p != std::string::npos) scene.temporalAADebug = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"textureQuality\""); if (p != std::string::npos) scene.textureQuality = (int)ParseFloatAt(gBlock, p, 3.0f);
            p = gBlock.find("\"mipLodBias\""); if (p != std::string::npos) scene.mipLodBias = ParseFloatAt(gBlock, p, 0.0f);
            p = gBlock.find("\"fsr\""); if (p != std::string::npos) scene.fsr = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"fsrSharpness\""); if (p != std::string::npos) scene.fsrSharpness = ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"fsr2\""); if (p != std::string::npos) scene.fsr2 = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"fsr2Preset\""); if (p != std::string::npos) scene.fsr2Preset = (int)ParseFloatAt(gBlock, p, 0.0f);
            p = gBlock.find("\"fsr2Sharpness\""); if (p != std::string::npos) scene.fsr2Sharpness = ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"vrsClassification\""); if (p != std::string::npos) scene.vrsClassification = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"vrsDebug\""); if (p != std::string::npos) scene.vrsDebug = ParseBoolAt(gBlock, p, false);

            p = gBlock.find("\"planarReflections\""); if (p != std::string::npos) scene.planarReflections = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"planarReflectionResolutionScale\""); if (p != std::string::npos) scene.planarReflectionResolutionScale = ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"planarReflectionMSAA\""); if (p != std::string::npos) scene.planarReflectionMSAA = (int)ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"ssr\""); if (p != std::string::npos) scene.ssr = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"ssrQuality\""); if (p != std::string::npos) scene.ssrQuality = (int)ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"reflectionRoughnessCutoff\""); if (p != std::string::npos) scene.reflectionRoughnessCutoff = ParseFloatAt(gBlock, p, 0.6f);
            p = gBlock.find("\"rtReflections\""); if (p != std::string::npos) scene.rtReflections = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"rtReflectionsRange\""); if (p != std::string::npos) scene.rtReflectionsRange = ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"rtReflectionsQuality\""); if (p != std::string::npos) scene.rtReflectionsQuality = (int)ParseFloatAt(gBlock, p, 1.0f);

            p = gBlock.find("\"colorGrading\""); if (p != std::string::npos) scene.colorGrading = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"dither\""); if (p != std::string::npos) scene.dither = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"bloom\""); if (p != std::string::npos) scene.bloom = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"bloomStrength\""); if (p != std::string::npos) scene.bloomStrength = ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"lightShafts\""); if (p != std::string::npos) scene.lightShafts = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"lightShaftsStrength\""); if (p != std::string::npos) scene.lightShaftsStrength = ParseFloatAt(gBlock, p, 0.05f);
            p = gBlock.find("\"lightShaftsFadeSpeed\""); if (p != std::string::npos) scene.lightShaftsFadeSpeed = ParseFloatAt(gBlock, p, 3.0f);
            p = gBlock.find("\"lensFlare\""); if (p != std::string::npos) scene.lensFlare = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"eyeAdaption\""); if (p != std::string::npos) scene.eyeAdaption = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"eyeAdaptionKey\""); if (p != std::string::npos) scene.eyeAdaptionKey = ParseFloatAt(gBlock, p, 0.1f);
            p = gBlock.find("\"eyeAdaptionRate\""); if (p != std::string::npos) scene.eyeAdaptionRate = ParseFloatAt(gBlock, p, 0.5f);
            p = gBlock.find("\"motionBlur\""); if (p != std::string::npos) scene.motionBlur = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"motionBlurStrength\""); if (p != std::string::npos) scene.motionBlurStrength = ParseFloatAt(gBlock, p, 100.0f);
            p = gBlock.find("\"depthOfField\""); if (p != std::string::npos) scene.depthOfField = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"depthOfFieldScale\""); if (p != std::string::npos) scene.depthOfFieldScale = ParseFloatAt(gBlock, p, 10.0f);
            p = gBlock.find("\"fxaa\""); if (p != std::string::npos) scene.fxaa = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"crtFilter\""); if (p != std::string::npos) scene.crtFilter = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"sharpenFilter\""); if (p != std::string::npos) scene.sharpenFilter = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"sharpenFilterAmount\""); if (p != std::string::npos) scene.sharpenFilterAmount = ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"outline\""); if (p != std::string::npos) scene.outline = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"outlineThreshold\""); if (p != std::string::npos) scene.outlineThreshold = ParseFloatAt(gBlock, p, 0.1f);
            p = gBlock.find("\"outlineThickness\""); if (p != std::string::npos) scene.outlineThickness = ParseFloatAt(gBlock, p, 1.0f);
            p = gBlock.find("\"chromaticAberration\""); if (p != std::string::npos) scene.chromaticAberration = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"chromaticAberrationAmount\""); if (p != std::string::npos) scene.chromaticAberrationAmount = ParseFloatAt(gBlock, p, 1.0f);

            p = gBlock.find("\"advancedLightCulling\""); if (p != std::string::npos) scene.advancedLightCulling = ParseBoolAt(gBlock, p, true);
            p = gBlock.find("\"debugLightCulling\""); if (p != std::string::npos) scene.debugLightCulling = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"freezeCullingCamera\""); if (p != std::string::npos) scene.freezeCullingCamera = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"disableAlbedoMaps\""); if (p != std::string::npos) scene.disableAlbedoMaps = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"forceDiffuseLighting\""); if (p != std::string::npos) scene.forceDiffuseLighting = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"forceUnlit\""); if (p != std::string::npos) scene.forceUnlit = ParseBoolAt(gBlock, p, false);
            p = gBlock.find("\"raytraceDebugBVH\""); if (p != std::string::npos) scene.raytraceDebugBVH = ParseBoolAt(gBlock, p, false);
        }
        }
    }

    static void ParseMetadata(const std::string& content, Scene& scene) {
        scene.nextId = ParseInt(content, "\"nextId\"", 1);
        scene.activeLevelCameraId = ParseInt(content, "\"activeLevelCameraId\"", -1);
        scene.playAnimations = ParseBool(content, "\"playAnimations\"", true);

        // Ensure nextId is higher than any existing actor/light ID
        for (const auto& obj : scene.objects) {
            if (obj.id >= scene.nextId) scene.nextId = obj.id + 1;
        }
        for (const auto& pl : scene.pointLights) {
            if (pl.id >= scene.nextId) scene.nextId = pl.id + 1;
        }

        // Parse tree view states (Outliner and Content Browser)
        auto ParseJsonStringList = [](const std::string& json, const std::string& key) -> std::vector<std::string> {
            std::vector<std::string> result;
            size_t kPos = json.find(key);
            if (kPos == std::string::npos) return result;
            size_t bOpen = json.find('[', kPos);
            if (bOpen == std::string::npos) return result;
            size_t bClose = json.find(']', bOpen);
            if (bClose == std::string::npos) return result;

            size_t cur = bOpen + 1;
            while (cur < bClose) {
                size_t qOpen = json.find('"', cur);
                if (qOpen == std::string::npos || qOpen >= bClose) break;
                size_t qClose = json.find('"', qOpen + 1);
                if (qClose == std::string::npos || qClose > bClose) break;
                std::string item = json.substr(qOpen + 1, qClose - qOpen - 1);
                if (!item.empty()) {
                    result.push_back(item);
                }
                cur = qClose + 1;
            }
            return result;
        };

        auto outlinerExpanded = ParseJsonStringList(content, "\"outlinerExpandedNodes\"");
        auto outlinerCollapsed = ParseJsonStringList(content, "\"outlinerCollapsedNodes\"");
        if (!outlinerExpanded.empty() || !outlinerCollapsed.empty() || content.find("\"outlinerExpandedNodes\"") != std::string::npos) {
            scene.outlinerNodeOpenState.clear();
            for (const auto& node : outlinerExpanded) {
                scene.outlinerNodeOpenState[node] = true;
            }
            for (const auto& node : outlinerCollapsed) {
                scene.outlinerNodeOpenState[node] = false;
            }
        }

        auto cbExpanded = ParseJsonStringList(content, "\"contentBrowserExpandedFolders\"");
        auto cbCollapsed = ParseJsonStringList(content, "\"contentBrowserCollapsedFolders\"");
        if (!cbExpanded.empty() || !cbCollapsed.empty() || content.find("\"contentBrowserExpandedFolders\"") != std::string::npos) {
            scene.contentBrowserFolderOpenState.clear();
            for (const auto& folder : cbExpanded) {
                scene.contentBrowserFolderOpenState[folder] = true;
            }
            for (const auto& folder : cbCollapsed) {
                scene.contentBrowserFolderOpenState[folder] = false;
            }
        }
    }

    static void ParseEnvironment(const std::string& json, Scene& scene) {
        ParseSkyboxAndEnvironment(json, scene);
        ParseLights(json, scene);
        ParsePostProcessing(json, scene);
    }

    static void ParsePointLights(const std::string& json, Scene& scene) {
        size_t plPos = json.find("\"pointLights\"");
        if (plPos == std::string::npos) return;

        size_t arrStart = json.find('[', plPos);
        if (arrStart == std::string::npos) return;
        size_t arrEnd = FindMatchingBracket(json, arrStart);

        std::string arrBlock = json.substr(arrStart, arrEnd - arrStart + 1);

        // Find each light object
        size_t searchPos = 0;
        while (true) {
            size_t objStart = arrBlock.find('{', searchPos);
            if (objStart == std::string::npos) break;
            size_t objEnd = FindMatchingBrace(arrBlock, objStart);
            if (objEnd == std::string::npos) break;

            std::string lightBlock = arrBlock.substr(objStart, objEnd - objStart + 1);
            PointLight pl;
            size_t p;
            p = lightBlock.find("\"id\""); if (p != std::string::npos) pl.id = ParseIntAt(lightBlock, p, 0);
            p = lightBlock.find("\"name\""); if (p != std::string::npos) pl.name = ParseStringAt(lightBlock, p, "Point Light");
            p = lightBlock.find("\"position\""); if (p != std::string::npos) pl.position = ParseVec3(lightBlock, p);
            p = lightBlock.find("\"color\""); if (p != std::string::npos) pl.color = ParseVec3(lightBlock, p);
            p = lightBlock.find("\"intensity\""); if (p != std::string::npos) pl.intensity = ParseFloatAt(lightBlock, p, 2.0f);
            p = lightBlock.find("\"range\""); if (p != std::string::npos) pl.range = ParseFloatAt(lightBlock, p, 10.0f);
            p = lightBlock.find("\"rangeSmoothing\""); if (p != std::string::npos) pl.rangeSmoothing = ParseFloatAt(lightBlock, p, 0.2f);
            p = lightBlock.find("\"enabled\""); if (p != std::string::npos) pl.enabled = ParseBoolAt(lightBlock, p, true);

            scene.pointLights.push_back(pl);
            searchPos = objEnd + 1;
        }
    }

    static std::vector<std::string> ExtractActorBlocks(const std::string& json) {
        std::vector<std::string> blocks;
        size_t actorsPos = json.find("\"actors\"");
        if (actorsPos == std::string::npos) return blocks;

        size_t arrStart = json.find('[', actorsPos);
        if (arrStart == std::string::npos) return blocks;
        size_t arrEnd = FindMatchingBracket(json, arrStart);
        if (arrEnd == std::string::npos) return blocks;

        std::string arrBlock = json.substr(arrStart, arrEnd - arrStart + 1);

        size_t searchPos = 0;
        while (true) {
            size_t objStart = arrBlock.find('{', searchPos);
            if (objStart == std::string::npos) break;
            size_t objEnd = FindMatchingBrace(arrBlock, objStart);
            if (objEnd == std::string::npos) break;

            blocks.push_back(arrBlock.substr(objStart, objEnd - objStart + 1));
            searchPos = objEnd + 1;
        }
        return blocks;
    }

    static GameObject ParseSingleActor(const std::string& actorBlock, std::unordered_map<int, PrimitiveMesh>& fastMeshes) {
        // Parse identity
        size_t p;
        int id = 0;
        std::string name = "Object";
        PrimitiveType type = PrimitiveType::Cube;
        glm::vec3 pos(0.0f);

        p = actorBlock.find("\"id\""); if (p != std::string::npos) id = ParseIntAt(actorBlock, p, 0);
        p = actorBlock.find("\"name\""); if (p != std::string::npos) name = ParseStringAt(actorBlock, p, "Object");
        p = actorBlock.find("\"type\""); if (p != std::string::npos) type = StrToPrimitive(ParseStringAt(actorBlock, p, "Cube"));

        // Parse transform block
        size_t tfPos = actorBlock.find("\"transform\"");
        if (tfPos != std::string::npos) {
            size_t tfStart = actorBlock.find('{', tfPos);
            size_t tfEnd = FindMatchingBrace(actorBlock, tfStart);
            std::string tfBlock = actorBlock.substr(tfStart, tfEnd - tfStart + 1);

            p = tfBlock.find("\"position\""); if (p != std::string::npos) pos = ParseVec3(tfBlock, p);
        }

        // Create the object
        glm::vec3 color(0.55f);
        GameObject obj(id, name, type, pos, color);

        // Parse mobility
        p = actorBlock.find("\"mobility\""); if (p != std::string::npos) obj.mobility = StrToMobility(ParseStringAt(actorBlock, p, "Movable"));

        // Parse remaining transform
        if (tfPos != std::string::npos) {
            size_t tfStart = actorBlock.find('{', tfPos);
            size_t tfEnd = FindMatchingBrace(actorBlock, tfStart);
            std::string tfBlock = actorBlock.substr(tfStart, tfEnd - tfStart + 1);

            p = tfBlock.find("\"rotation\""); if (p != std::string::npos) obj.rotation = ParseVec3(tfBlock, p);
            p = tfBlock.find("\"scale\""); if (p != std::string::npos) obj.scale = ParseVec3(tfBlock, p);
        }

        // Parse material block
        size_t matPos = actorBlock.find("\"material\"");
        if (matPos != std::string::npos) {
            size_t matStart = actorBlock.find('{', matPos);
            size_t matEnd = FindMatchingBrace(actorBlock, matStart);
            std::string matBlock = actorBlock.substr(matStart, matEnd - matStart + 1);

            p = matBlock.find("\"materialName\""); if (p != std::string::npos) obj.materialName = ParseStringAt(matBlock, p, "Default_Material");
            p = matBlock.find("\"color\""); if (p != std::string::npos) obj.color = ParseVec3(matBlock, p);
            p = matBlock.find("\"metallic\""); if (p != std::string::npos) obj.metallic = ParseFloatAt(matBlock, p, 0.0f);
            p = matBlock.find("\"roughness\""); if (p != std::string::npos) obj.roughness = ParseFloatAt(matBlock, p, 0.5f);
            p = matBlock.find("\"normalStrength\""); if (p != std::string::npos) obj.normalStrength = ParseFloatAt(matBlock, p, 1.0f);
            p = matBlock.find("\"specular\""); if (p != std::string::npos) obj.specular = ParseFloatAt(matBlock, p, 0.5f);
            p = matBlock.find("\"emissiveColor\""); if (p != std::string::npos) obj.emissiveColor = ParseVec3(matBlock, p);
            p = matBlock.find("\"emissiveIntensity\""); if (p != std::string::npos) obj.emissiveIntensity = ParseFloatAt(matBlock, p, 0.0f);
            p = matBlock.find("\"shadingModel\""); if (p != std::string::npos) obj.shadingModel = ParseIntAt(matBlock, p, 0);
            p = matBlock.find("\"blendMode\""); if (p != std::string::npos) obj.blendMode = ParseIntAt(matBlock, p, 0);
            p = matBlock.find("\"twoSided\""); if (p != std::string::npos) obj.twoSided = ParseBoolAt(matBlock, p, false);
            p = matBlock.find("\"castShadows\""); if (p != std::string::npos) obj.castShadows = ParseBoolAt(matBlock, p, true);
            p = matBlock.find("\"receiveShadows\""); if (p != std::string::npos) obj.receiveShadows = ParseBoolAt(matBlock, p, true);
            p = matBlock.find("\"meshClusterCulling\""); if (p != std::string::npos) obj.meshClusterCulling = ParseBoolAt(matBlock, p, false);
            p = matBlock.find("\"smoothShading\""); if (p != std::string::npos) obj.smoothShading = ParseBoolAt(matBlock, p, true);
            p = matBlock.find("\"autoSmoothAngle\""); if (p != std::string::npos) obj.autoSmoothAngle = ParseFloatAt(matBlock, p, 180.0f);
            p = matBlock.find("\"enableLOD\""); if (p != std::string::npos) obj.enableLOD = ParseBoolAt(matBlock, p, false);
            p = matBlock.find("\"forcedLOD\""); if (p != std::string::npos) obj.forcedLOD = ParseIntAt(matBlock, p, -1);
            p = matBlock.find("\"lodBias\""); if (p != std::string::npos) obj.lodBias = ParseFloatAt(matBlock, p, 0.0f);
            p = matBlock.find("\"minLOD\""); if (p != std::string::npos) obj.minLOD = ParseIntAt(matBlock, p, 0);
            p = matBlock.find("\"maxLOD\""); if (p != std::string::npos) obj.maxLOD = ParseIntAt(matBlock, p, 20);
            p = matBlock.find("\"enableSFLOD\""); if (p != std::string::npos) obj.enableSFLOD = ParseBoolAt(matBlock, p, false);
            p = matBlock.find("\"sflodMinLOD\""); if (p != std::string::npos) obj.sflodMinLOD = ParseIntAt(matBlock, p, 3);
            p = matBlock.find("\"sflodMaxLOD\""); if (p != std::string::npos) obj.sflodMaxLOD = ParseIntAt(matBlock, p, 20);
            p = matBlock.find("\"sflodLODBias\""); if (p != std::string::npos) obj.sflodLODBias = ParseFloatAt(matBlock, p, 0.0f);
            p = matBlock.find("\"sflodBillboardMode\""); if (p != std::string::npos) obj.sflodBillboardMode = ParseIntAt(matBlock, p, 0);
            p = matBlock.find("\"sflodGeometryMode\""); if (p != std::string::npos) obj.sflodGeometryMode = ParseIntAt(matBlock, p, 0);
            p = matBlock.find("\"sflodOpenFace\""); if (p != std::string::npos) obj.sflodOpenFace = ParseIntAt(matBlock, p, 4);
            p = matBlock.find("\"sflodFaceMask\""); if (p != std::string::npos) obj.sflodFaceMask = (uint8_t)ParseIntAt(matBlock, p, 0x2F);
            p = matBlock.find("\"sflodShaderComplexity\""); if (p != std::string::npos) obj.sflodShaderComplexity = ParseIntAt(matBlock, p, 0);
            p = matBlock.find("\"sflodDistanceThreshold\""); if (p != std::string::npos) obj.sflodDistanceThreshold = ParseFloatAt(matBlock, p, 25.0f);
            p = matBlock.find("\"sflodIsAtlas\""); if (p != std::string::npos) obj.sflodIsAtlas = ParseBoolAt(matBlock, p, false);
            p = matBlock.find("\"sflodPlaneWidth\""); if (p != std::string::npos) obj.sflodPlaneWidth = ParseFloatAt(matBlock, p, 0.0f);
            p = matBlock.find("\"sflodPlaneHeight\""); if (p != std::string::npos) obj.sflodPlaneHeight = ParseFloatAt(matBlock, p, 0.0f);
            p = matBlock.find("\"sflodPlaneOffset\"");
            if (p != std::string::npos) {
                size_t lb = matBlock.find('[', p);
                size_t rb = matBlock.find(']', lb);
                if (lb != std::string::npos && rb != std::string::npos) {
                    std::string arr = matBlock.substr(lb + 1, rb - lb - 1);
                    std::stringstream ss(arr);
                    char comma;
                    ss >> obj.sflodPlaneOffset.x >> comma >> obj.sflodPlaneOffset.y;
                }
            }
            p = matBlock.find("\"sflodCustomTexture\""); if (p != std::string::npos) obj.sflodCustomTexture = ParseStringAt(matBlock, p, "");
            p = matBlock.find("\"sflodCutout\""); if (p != std::string::npos) obj.sflodCutout = ParseBoolAt(matBlock, p, true);
            p = matBlock.find("\"opacity\""); if (p != std::string::npos) obj.opacity = ParseFloatAt(matBlock, p, 1.0f);
            p = matBlock.find("\"opacityMaskClipValue\""); if (p != std::string::npos) obj.opacityMaskClipValue = ParseFloatAt(matBlock, p, 0.333f);
            p = matBlock.find("\"normalMapYFlip\""); if (p != std::string::npos) obj.normalMapYFlip = ParseBoolAt(matBlock, p, false);
            p = matBlock.find("\"disableStreaming\""); if (p != std::string::npos) obj.disableStreaming = ParseBoolAt(matBlock, p, false);
            p = matBlock.find("\"metallicChannel\""); if (p != std::string::npos) obj.metallicChannel = ParseIntAt(matBlock, p, 0);
            p = matBlock.find("\"roughnessChannel\""); if (p != std::string::npos) obj.roughnessChannel = ParseIntAt(matBlock, p, 1);
            p = matBlock.find("\"aoChannel\""); if (p != std::string::npos) obj.aoChannel = ParseIntAt(matBlock, p, 0);
            p = matBlock.find("\"materialDebugMode\""); if (p != std::string::npos) obj.materialDebugMode = ParseIntAt(matBlock, p, 0);
            p = matBlock.find("\"uvScale\"");
            if (p != std::string::npos) {
                size_t lb = matBlock.find('[', p);
                size_t rb = matBlock.find(']', lb);
                if (lb != std::string::npos && rb != std::string::npos) {
                    std::string arr = matBlock.substr(lb + 1, rb - lb - 1);
                    std::stringstream ss(arr);
                    char comma;
                    ss >> obj.uvScale.x >> comma >> obj.uvScale.y;
                }
            }

            // Textures sub-block
            size_t texPos = matBlock.find("\"textures\"");
            if (texPos != std::string::npos) {
                size_t texStart = matBlock.find('{', texPos);
                size_t texEnd = FindMatchingBrace(matBlock, texStart);
                std::string texBlock = matBlock.substr(texStart, texEnd - texStart + 1);

                p = texBlock.find("\"baseColor\""); if (p != std::string::npos) obj.baseColorTexture = ParseStringAt(texBlock, p);
                p = texBlock.find("\"normal\""); if (p != std::string::npos) obj.normalTexture = ParseStringAt(texBlock, p);
                p = texBlock.find("\"roughness\""); if (p != std::string::npos) obj.roughnessTexture = ParseStringAt(texBlock, p);
                p = texBlock.find("\"metallic\""); if (p != std::string::npos) obj.metallicTexture = ParseStringAt(texBlock, p);
                p = texBlock.find("\"ao\""); if (p != std::string::npos) obj.aoTexture = ParseStringAt(texBlock, p);
                p = texBlock.find("\"emission\""); if (p != std::string::npos) obj.emissionTexture = ParseStringAt(texBlock, p);
                p = texBlock.find("\"opacity\""); if (p != std::string::npos) obj.opacityTexture = ParseStringAt(texBlock, p);
            }
        }

        // Parse visibility & animation
        p = actorBlock.find("\"visible\""); if (p != std::string::npos) obj.visible = ParseBoolAt(actorBlock, p, true);
        p = actorBlock.find("\"autoRotate\""); if (p != std::string::npos) obj.autoRotate = ParseBoolAt(actorBlock, p, false);
        p = actorBlock.find("\"autoRotateSpeed\""); if (p != std::string::npos) obj.autoRotateSpeed = ParseVec3(actorBlock, p);

        // Parse tags
        size_t tagsPos = actorBlock.find("\"tags\"");
        if (tagsPos != std::string::npos) {
            size_t arrStart = actorBlock.find('[', tagsPos);
            size_t arrEnd = actorBlock.find(']', arrStart);
            if (arrStart != std::string::npos && arrEnd != std::string::npos && arrEnd > arrStart) {
                std::string tagsSub = actorBlock.substr(arrStart + 1, arrEnd - arrStart - 1);
                size_t q1 = 0;
                while ((q1 = tagsSub.find('"', q1)) != std::string::npos) {
                    size_t q2 = tagsSub.find('"', q1 + 1);
                    if (q2 == std::string::npos) break;
                    std::string tStr = tagsSub.substr(q1 + 1, q2 - q1 - 1);
                    if (!tStr.empty()) {
                        obj.AddTag(tStr);
                    }
                    q1 = q2 + 1;
                }
            }
        }

        // Parse hierarchy, imported mesh & light proxy
        p = actorBlock.find("\"isImportedMesh\""); if (p != std::string::npos) obj.isImportedMesh = ParseBoolAt(actorBlock, p, false);
        p = actorBlock.find("\"meshFilePath\""); if (p != std::string::npos) obj.meshFilePath = ParseStringAt(actorBlock, p, "");
        p = actorBlock.find("\"submeshIndex\""); if (p != std::string::npos) obj.submeshIndex = ParseIntAt(actorBlock, p, -1);
        p = actorBlock.find("\"parentId\""); if (p != std::string::npos) obj.parentId = ParseIntAt(actorBlock, p, -1);
        p = actorBlock.find("\"isLight\""); if (p != std::string::npos) obj.isLight = ParseBoolAt(actorBlock, p, false);
        p = actorBlock.find("\"lightId\""); if (p != std::string::npos) obj.lightId = ParseIntAt(actorBlock, p, -1);

        // Parse Primitive Parameters
        p = actorBlock.find("\"paramWidth\""); if (p != std::string::npos) obj.params.width = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"paramHeight\""); if (p != std::string::npos) obj.params.height = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"paramDepth\""); if (p != std::string::npos) obj.params.depth = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"paramSize\""); if (p != std::string::npos) obj.params.size = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"paramRadius\""); if (p != std::string::npos) obj.params.radius = ParseFloatAt(actorBlock, p, 0.5f);
        p = actorBlock.find("\"paramRadius2\""); if (p != std::string::npos) obj.params.radius2 = ParseFloatAt(actorBlock, p, 0.2f);
        p = actorBlock.find("\"paramSegX\""); if (p != std::string::npos) obj.params.segmentsX = ParseIntAt(actorBlock, p, 16);
        p = actorBlock.find("\"paramSegY\""); if (p != std::string::npos) obj.params.segmentsY = ParseIntAt(actorBlock, p, 16);
        p = actorBlock.find("\"paramSegZ\""); if (p != std::string::npos) obj.params.segmentsZ = ParseIntAt(actorBlock, p, 16);
        p = actorBlock.find("\"paramRadialSeg\""); if (p != std::string::npos) obj.params.radialSegments = ParseIntAt(actorBlock, p, 24);
        p = actorBlock.find("\"paramHeightSeg\""); if (p != std::string::npos) obj.params.heightSegments = ParseIntAt(actorBlock, p, 1);
        p = actorBlock.find("\"paramRings\""); if (p != std::string::npos) obj.params.rings = ParseIntAt(actorBlock, p, 24);
        p = actorBlock.find("\"paramSectors\""); if (p != std::string::npos) obj.params.sectors = ParseIntAt(actorBlock, p, 24);
        p = actorBlock.find("\"paramSubdiv\""); if (p != std::string::npos) obj.params.subdivisions = ParseIntAt(actorBlock, p, 2);
        p = actorBlock.find("\"paramSides\""); if (p != std::string::npos) obj.params.sides = ParseIntAt(actorBlock, p, 4);
        p = actorBlock.find("\"paramCapTop\""); if (p != std::string::npos) obj.params.capTop = ParseBoolAt(actorBlock, p, true);
        p = actorBlock.find("\"paramCapBottom\""); if (p != std::string::npos) obj.params.capBottom = ParseBoolAt(actorBlock, p, true);

        // Parse Light Properties
        p = actorBlock.find("\"lightType\""); if (p != std::string::npos) obj.light.type = (LightType)ParseIntAt(actorBlock, p, 0);
        p = actorBlock.find("\"lightFlags\""); if (p != std::string::npos) obj.light._flags = (uint32_t)ParseIntAt(actorBlock, p, LightComponent::CAST_SHADOW);
        p = actorBlock.find("\"lightEnabled\""); if (p != std::string::npos) obj.light.enabled = ParseBoolAt(actorBlock, p, true);
        p = actorBlock.find("\"lightColor\""); if (p != std::string::npos) obj.light.color = ParseVec3(actorBlock, p);
        p = actorBlock.find("\"lightIntensity\""); if (p != std::string::npos) obj.light.intensity = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"lightTemperature\""); if (p != std::string::npos) obj.light.temperature = ParseFloatAt(actorBlock, p, 6500.0f);
        p = actorBlock.find("\"lightUseTemp\""); if (p != std::string::npos) obj.light.useTemperature = ParseBoolAt(actorBlock, p, false);
        p = actorBlock.find("\"lightRange\""); if (p != std::string::npos) obj.light.range = ParseFloatAt(actorBlock, p, 10.0f);
        p = actorBlock.find("\"lightRangeSmoothing\""); if (p != std::string::npos) obj.light.rangeSmoothing = ParseFloatAt(actorBlock, p, 0.2f);
        p = actorBlock.find("\"lightAttenuation\""); if (p != std::string::npos) obj.light.attenuation = ParseFloatAt(actorBlock, p, 2.0f);
        p = actorBlock.find("\"lightInnerCone\""); if (p != std::string::npos) obj.light.innerConeAngle = ParseFloatAt(actorBlock, p, 0.0f);
        p = actorBlock.find("\"lightOuterCone\""); if (p != std::string::npos) obj.light.outerConeAngle = ParseFloatAt(actorBlock, p, 45.0f);
        p = actorBlock.find("\"lightConeFalloff\""); if (p != std::string::npos) obj.light.coneFalloff = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"lightRadius\""); if (p != std::string::npos) obj.light.radius = ParseFloatAt(actorBlock, p, 0.025f);
        p = actorBlock.find("\"lightLength\""); if (p != std::string::npos) obj.light.length = ParseFloatAt(actorBlock, p, 0.0f);
        p = actorBlock.find("\"lightHeight\""); if (p != std::string::npos) obj.light.height = ParseFloatAt(actorBlock, p, 0.0f);
        p = actorBlock.find("\"lightVolumetricBoost\""); if (p != std::string::npos) obj.light.volumetric_boost = ParseFloatAt(actorBlock, p, 0.0f);
        p = actorBlock.find("\"lightForcedShadowRes\""); if (p != std::string::npos) obj.light.forced_shadow_resolution = ParseIntAt(actorBlock, p, -1);
        p = actorBlock.find("\"lightAreaShape\""); if (p != std::string::npos) obj.light.areaShape = ParseIntAt(actorBlock, p, 0);
        p = actorBlock.find("\"lightAreaWidth\""); if (p != std::string::npos) obj.light.width = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"lightAreaHeight\""); if (p != std::string::npos) obj.light.height = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"lightAreaRadius\""); if (p != std::string::npos) obj.light.radius = ParseFloatAt(actorBlock, p, 0.025f);
        p = actorBlock.find("\"lightAreaLength\""); if (p != std::string::npos) obj.light.length = ParseFloatAt(actorBlock, p, 0.0f);
        p = actorBlock.find("\"lightTwoSided\""); if (p != std::string::npos) obj.light.twoSided = ParseBoolAt(actorBlock, p, false);
        p = actorBlock.find("\"lightSkyColor\""); if (p != std::string::npos) obj.light.skyColor = ParseVec3(actorBlock, p);
        p = actorBlock.find("\"lightGroundColor\""); if (p != std::string::npos) obj.light.groundColor = ParseVec3(actorBlock, p);
        p = actorBlock.find("\"lightEnvMap\""); if (p != std::string::npos) obj.light.envMapTexture = ParseStringAt(actorBlock, p, "");
        p = actorBlock.find("\"lightCastShadows\""); if (p != std::string::npos) {
            obj.light.castShadows = ParseBoolAt(actorBlock, p, true);
            obj.light.SetCastShadow(obj.light.castShadows);
        }
        p = actorBlock.find("\"lightShadowStrength\""); if (p != std::string::npos) obj.light.shadowStrength = ParseFloatAt(actorBlock, p, 0.85f);
        p = actorBlock.find("\"lightShadowBias\""); if (p != std::string::npos) obj.light.shadowBias = ParseFloatAt(actorBlock, p, 0.0012f);
        p = actorBlock.find("\"lightShadowRes\""); if (p != std::string::npos) obj.light.shadowResolution = ParseIntAt(actorBlock, p, 2048);
        p = actorBlock.find("\"lightShadowDist\""); if (p != std::string::npos) obj.light.shadowDistance = ParseFloatAt(actorBlock, p, 50.0f);
        p = actorBlock.find("\"lightVolumetric\""); if (p != std::string::npos) {
            obj.light.volumetric = ParseBoolAt(actorBlock, p, false);
            obj.light.SetVolumetricsEnabled(obj.light.volumetric);
        }
        p = actorBlock.find("\"lightVolScattering\""); if (p != std::string::npos) obj.light.volumetricScattering = ParseFloatAt(actorBlock, p, 0.66f);
        p = actorBlock.find("\"lightVolIntensity\""); if (p != std::string::npos) obj.light.volumetricIntensity = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"lightVolSteps\""); if (p != std::string::npos) obj.light.volumetricSteps = ParseIntAt(actorBlock, p, 16);
        p = actorBlock.find("\"lightVolMaxDist\""); if (p != std::string::npos) obj.light.volumetricMaxDistance = ParseFloatAt(actorBlock, p, 120.0f);
        p = actorBlock.find("\"lightEnableDistCulling\""); if (p != std::string::npos) obj.light.enableDistanceCulling = ParseBoolAt(actorBlock, p, false);
        p = actorBlock.find("\"lightMinVisibleDist\""); if (p != std::string::npos) obj.light.minVisibleDistance = ParseFloatAt(actorBlock, p, 50.0f);
        p = actorBlock.find("\"lightMaxVisibleDist\""); if (p != std::string::npos) obj.light.maxVisibleDistance = ParseFloatAt(actorBlock, p, 100.0f);
        p = actorBlock.find("\"lightLayer\""); if (p != std::string::npos) obj.light.lightLayer = (uint32_t)ParseIntAt(actorBlock, p, 1);

        // Parse Camera Properties
        p = actorBlock.find("\"isCamera\""); if (p != std::string::npos) obj.isCamera = ParseBoolAt(actorBlock, p, false);
        if (obj.type == PrimitiveType::Camera) obj.isCamera = true;
        p = actorBlock.find("\"camOrthographic\""); if (p != std::string::npos) obj.camera.isOrthographic = ParseBoolAt(actorBlock, p, false);
        p = actorBlock.find("\"camFov\""); if (p != std::string::npos) obj.camera.fov = ParseFloatAt(actorBlock, p, 60.0f);
        p = actorBlock.find("\"camOrthoSize\""); if (p != std::string::npos) obj.camera.orthoSize = ParseFloatAt(actorBlock, p, 5.0f);
        p = actorBlock.find("\"camNearPlane\""); if (p != std::string::npos) obj.camera.nearPlane = ParseFloatAt(actorBlock, p, 0.1f);
        p = actorBlock.find("\"camFarPlane\""); if (p != std::string::npos) obj.camera.farPlane = ParseFloatAt(actorBlock, p, 1000.0f);
        p = actorBlock.find("\"camAspectRatio\""); if (p != std::string::npos) obj.camera.aspectRatio = ParseFloatAt(actorBlock, p, 16.0f / 9.0f);

        // Parse Water Properties
        p = actorBlock.find("\"isWaterBody\""); if (p != std::string::npos) obj.isWaterBody = ParseBoolAt(actorBlock, p, false);
        if (obj.type == PrimitiveType::WaterBody) obj.isWaterBody = true;
        p = actorBlock.find("\"waterType\""); if (p != std::string::npos) obj.water.type = (WaterBodyType)ParseIntAt(actorBlock, p, 0);
        p = actorBlock.find("\"waterName\""); if (p != std::string::npos) obj.water.name = ParseStringAt(actorBlock, p, "WaterBody");
        p = actorBlock.find("\"waterRoughness\""); if (p != std::string::npos) obj.water.roughness = ParseFloatAt(actorBlock, p, 0.1f);
        p = actorBlock.find("\"waterMetallic\""); if (p != std::string::npos) obj.water.metallic = ParseFloatAt(actorBlock, p, 0.1f);
        p = actorBlock.find("\"waterOpacity\""); if (p != std::string::npos) obj.water.opacity = ParseFloatAt(actorBlock, p, 0.8f);
        p = actorBlock.find("\"waterEnableDepth\""); if (p != std::string::npos) obj.water.enableDepth = ParseBoolAt(actorBlock, p, true);
        p = actorBlock.find("\"waterWaveSpeed\""); if (p != std::string::npos) obj.water.waveSpeed = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"waterWaveScale\""); if (p != std::string::npos) obj.water.waveScale = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"waterWaveHeight\""); if (p != std::string::npos) obj.water.waveHeight = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"waterNormalScale\""); if (p != std::string::npos) obj.water.normalScale = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"waterDepthRange\""); if (p != std::string::npos) obj.water.depthRange = ParseFloatAt(actorBlock, p, 10.0f);
        p = actorBlock.find("\"waterFoamDistance\""); if (p != std::string::npos) obj.water.foamDistance = ParseFloatAt(actorBlock, p, 1.5f);
        p = actorBlock.find("\"waterFoamStrength\""); if (p != std::string::npos) obj.water.foamStrength = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"waterFoamSpeed\""); if (p != std::string::npos) obj.water.foamSpeed = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"waterFresnelStrength\""); if (p != std::string::npos) obj.water.fresnelStrength = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"waterSpecularPower\""); if (p != std::string::npos) obj.water.specularPower = ParseFloatAt(actorBlock, p, 128.0f);
        p = actorBlock.find("\"waterSparklePower\""); if (p != std::string::npos) obj.water.sparklePower = ParseFloatAt(actorBlock, p, 800.0f);
        p = actorBlock.find("\"waterSimEnabled\""); if (p != std::string::npos) obj.water.simulationEnabled = ParseBoolAt(actorBlock, p, true);
        p = actorBlock.find("\"waterWaveDamping\""); if (p != std::string::npos) obj.water.waveDamping = ParseFloatAt(actorBlock, p, 0.985f);
        p = actorBlock.find("\"waterDisturbanceStrength\""); if (p != std::string::npos) obj.water.disturbanceStrength = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"waterHeightScale\""); if (p != std::string::npos) obj.water.heightScale = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"waterColor\""); if (p != std::string::npos) {
            obj.water.waterColor = ParseVec4(actorBlock, p);
        }
        p = actorBlock.find("\"waterShallowColor\""); if (p != std::string::npos) {
            obj.water.shallowColor = ParseVec4(actorBlock, p);
        }
        p = actorBlock.find("\"waterChoppiness\""); if (p != std::string::npos) obj.water.choppiness = ParseFloatAt(actorBlock, p, 0.7f);
        p = actorBlock.find("\"waterOceanExtent\""); if (p != std::string::npos) obj.water.oceanExtent = ParseFloatAt(actorBlock, p, 3000.0f);
        p = actorBlock.find("\"waterIsCameraCentered\""); if (p != std::string::npos) obj.water.isCameraCentered = ParseBoolAt(actorBlock, p, true);

        // Parse Terrain Properties
        p = actorBlock.find("\"isTerrain\""); if (p != std::string::npos) obj.isTerrain = ParseBoolAt(actorBlock, p, false);
        if (obj.type == PrimitiveType::Terrain) obj.isTerrain = true;
        if (obj.isTerrain) {
            p = actorBlock.find("\"terrainWidth\""); if (p != std::string::npos) obj.terrain.width = ParseFloatAt(actorBlock, p, 64.0f);
            p = actorBlock.find("\"terrainLength\""); if (p != std::string::npos) obj.terrain.length = ParseFloatAt(actorBlock, p, 64.0f);
            p = actorBlock.find("\"terrainResolution\""); if (p != std::string::npos) obj.terrain.resolution = ParseIntAt(actorBlock, p, 128);
            p = actorBlock.find("\"terrainHeightScale\""); if (p != std::string::npos) obj.terrain.heightScale = ParseFloatAt(actorBlock, p, 1.0f);
            p = actorBlock.find("\"terrainBrushType\""); if (p != std::string::npos) obj.terrain.brushType = (Eunoia::TerrainBrushType)ParseIntAt(actorBlock, p, 0);
            p = actorBlock.find("\"terrainBrushFalloff\""); if (p != std::string::npos) obj.terrain.brushFalloff = (Eunoia::TerrainFalloffType)ParseIntAt(actorBlock, p, 0);
            p = actorBlock.find("\"terrainBrushSize\""); if (p != std::string::npos) obj.terrain.brushSize = ParseFloatAt(actorBlock, p, 8.0f);
            p = actorBlock.find("\"terrainBrushStrength\""); if (p != std::string::npos) obj.terrain.brushStrength = ParseFloatAt(actorBlock, p, 5.0f);
            p = actorBlock.find("\"terrainFlattenTarget\""); if (p != std::string::npos) obj.terrain.flattenTargetHeight = ParseFloatAt(actorBlock, p, 0.0f);
            p = actorBlock.find("\"terrainNoiseScale\""); if (p != std::string::npos) obj.terrain.noiseScale = ParseFloatAt(actorBlock, p, 0.15f);

            p = actorBlock.find("\"terrainHeights\"");
            if (p != std::string::npos) {
                size_t lb = actorBlock.find('[', p);
                size_t rb = actorBlock.find(']', lb);
                if (lb != std::string::npos && rb != std::string::npos) {
                    std::string hList = actorBlock.substr(lb + 1, rb - lb - 1);
                    std::stringstream ss(hList);
                    std::string item;
                    obj.terrain.heightmap.clear();
                    while (std::getline(ss, item, ',')) {
                        try { obj.terrain.heightmap.push_back(std::stof(item)); } catch (...) {}
                    }
                }
            }
            if (obj.terrain.heightmap.empty()) {
                obj.terrain.InitializeGrid(obj.terrain.width, obj.terrain.length, obj.terrain.resolution);
            }

            // Parse Terrain Material Layers
            size_t tmlPos = actorBlock.find("\"terrainMaterialLayers\"");
            if (tmlPos != std::string::npos) {
                size_t tmlStart = actorBlock.find('[', tmlPos);
                size_t tmlEnd = (tmlStart != std::string::npos) ? FindMatchingBracket(actorBlock, tmlStart) : std::string::npos;
                if (tmlStart != std::string::npos && tmlEnd != std::string::npos) {
                    obj.terrain.materialLayers.clear();
                    std::string tmlArray = actorBlock.substr(tmlStart, tmlEnd - tmlStart + 1);
                    size_t tmlSearchPos = 0;
                    while (true) {
                        size_t lObjStart = tmlArray.find('{', tmlSearchPos);
                        if (lObjStart == std::string::npos) break;
                        size_t lObjEnd = FindMatchingBrace(tmlArray, lObjStart);
                        if (lObjEnd == std::string::npos) break;

                        std::string lBlock = tmlArray.substr(lObjStart, lObjEnd - lObjStart + 1);
                        Eunoia::TerrainMaterialLayer layer;
                        size_t lp;
                        lp = lBlock.find("\"name\""); if (lp != std::string::npos) layer.name = ParseStringAt(lBlock, lp, "Default_Material");
                        lp = lBlock.find("\"path\""); if (lp != std::string::npos) layer.path = ParseStringAt(lBlock, lp, "");
                        lp = lBlock.find("\"baseColor\""); if (lp != std::string::npos) layer.baseColor = ParseVec3(lBlock, lp);
                        lp = lBlock.find("\"metallic\""); if (lp != std::string::npos) layer.metallic = ParseFloatAt(lBlock, lp, 0.0f);
                        lp = lBlock.find("\"roughness\""); if (lp != std::string::npos) layer.roughness = ParseFloatAt(lBlock, lp, 0.5f);
                        lp = lBlock.find("\"normalStrength\""); if (lp != std::string::npos) layer.normalStrength = ParseFloatAt(lBlock, lp, 1.0f);
                        lp = lBlock.find("\"specular\""); if (lp != std::string::npos) layer.specular = ParseFloatAt(lBlock, lp, 0.5f);
                        lp = lBlock.find("\"baseColorTexture\""); if (lp != std::string::npos) layer.baseColorTexture = ParseStringAt(lBlock, lp, "");
                        lp = lBlock.find("\"normalTexture\""); if (lp != std::string::npos) layer.normalTexture = ParseStringAt(lBlock, lp, "");
                        lp = lBlock.find("\"roughnessTexture\""); if (lp != std::string::npos) layer.roughnessTexture = ParseStringAt(lBlock, lp, "");
                        lp = lBlock.find("\"metallicTexture\""); if (lp != std::string::npos) layer.metallicTexture = ParseStringAt(lBlock, lp, "");
                        lp = lBlock.find("\"aoTexture\""); if (lp != std::string::npos) layer.aoTexture = ParseStringAt(lBlock, lp, "");
                        lp = lBlock.find("\"uvScale\"");
                        if (lp != std::string::npos) {
                            size_t lb = lBlock.find('[', lp);
                            size_t rb = lBlock.find(']', lb);
                            if (lb != std::string::npos && rb != std::string::npos) {
                                std::string arr = lBlock.substr(lb + 1, rb - lb - 1);
                                std::stringstream ss(arr);
                                char comma;
                                ss >> layer.uvScale.x >> comma >> layer.uvScale.y;
                            }
                        }

                        obj.terrain.materialLayers.push_back(layer);
                        tmlSearchPos = lObjEnd + 1;
                    }
                }
            }

            // Parse Terrain Layer Weights (Continuous per-vertex multi-material blend weights)
            bool loadedLayerWeights = false;
            p = actorBlock.find("\"terrainLayerWeights\"");
            if (p != std::string::npos) {
                size_t outerLb = actorBlock.find('[', p);
                if (outerLb != std::string::npos) {
                    obj.terrain.layerWeights.clear();
                    size_t wSearchPos = outerLb + 1;
                    while (true) {
                        size_t innerLb = actorBlock.find('[', wSearchPos);
                        size_t outerRb = actorBlock.find(']', wSearchPos);
                        if (innerLb == std::string::npos || (outerRb != std::string::npos && innerLb > outerRb)) break;
                        size_t innerRb = actorBlock.find(']', innerLb);
                        if (innerRb == std::string::npos) break;

                        std::string rowStr = actorBlock.substr(innerLb + 1, innerRb - innerLb - 1);
                        std::stringstream ss(rowStr);
                        std::string item;
                        std::vector<float> rowWeights;
                        while (std::getline(ss, item, ',')) {
                            try { rowWeights.push_back(std::stof(item)); } catch (...) {}
                        }
                        obj.terrain.layerWeights.push_back(std::move(rowWeights));
                        wSearchPos = innerRb + 1;
                    }
                    if (!obj.terrain.layerWeights.empty()) {
                        loadedLayerWeights = true;
                    }
                }
            }

            // Backward compatibility: Migrate legacy discrete quadMaterialIndices to continuous layerWeights
            if (!loadedLayerWeights) {
                obj.terrain.EnsureLayerWeights();
                p = actorBlock.find("\"terrainQuadMaterials\"");
                if (p != std::string::npos) {
                    size_t lb = actorBlock.find('[', p);
                    size_t rb = actorBlock.find(']', lb);
                    if (lb != std::string::npos && rb != std::string::npos) {
                        std::string qmList = actorBlock.substr(lb + 1, rb - lb - 1);
                        std::stringstream ss(qmList);
                        std::string item;
                        int res = obj.terrain.resolution;
                        int totalVerts = (res + 1) * (res + 1);

                        int quadIdx = 0;
                        while (std::getline(ss, item, ',')) {
                            try {
                                uint8_t mIdx = (uint8_t)std::stoi(item);
                                if (mIdx < obj.terrain.layerWeights.size()) {
                                    int qx = quadIdx % res;
                                    int qz = quadIdx / res;
                                    if (qz < res) {
                                        int v00 = qz * (res + 1) + qx;
                                        int v10 = qz * (res + 1) + (qx + 1);
                                        int v01 = (qz + 1) * (res + 1) + qx;
                                        int v11 = (qz + 1) * (res + 1) + (qx + 1);

                                        obj.terrain.layerWeights[mIdx][v00] += 1.0f;
                                        obj.terrain.layerWeights[mIdx][v10] += 1.0f;
                                        obj.terrain.layerWeights[mIdx][v01] += 1.0f;
                                        obj.terrain.layerWeights[mIdx][v11] += 1.0f;
                                    }
                                }
                            } catch (...) {}
                            quadIdx++;
                        }

                        // Normalize weights across layers
                        for (int v = 0; v < totalVerts; ++v) {
                            float sumW = 0.0f;
                            for (size_t l = 0; l < obj.terrain.layerWeights.size(); ++l) {
                                sumW += obj.terrain.layerWeights[l][v];
                            }
                            if (sumW > 0.0001f) {
                                for (size_t l = 0; l < obj.terrain.layerWeights.size(); ++l) {
                                    obj.terrain.layerWeights[l][v] /= sumW;
                                }
                            } else {
                                obj.terrain.layerWeights[0][v] = 1.0f;
                            }
                        }
                    }
                }
            } else {
                obj.terrain.EnsureLayerWeights();
            }

            obj.terrain.RebuildTerrainMesh(obj.mesh);
        }

        // Parse 3D User Interface Properties
        p = actorBlock.find("\"isUserInterface\""); if (p != std::string::npos) obj.isUserInterface = ParseBoolAt(actorBlock, p, false);
        if (obj.type == PrimitiveType::UserInterface) obj.isUserInterface = true;
        if (obj.type == PrimitiveType::LevelAnimation) {
            obj.hasLevelAnimation = true;
            obj.levelAnimation.enabled = true;
        }
        p = actorBlock.find("\"uiAssetPath\""); if (p != std::string::npos) obj.ui3d.uiAssetPath = ParseStringAt(actorBlock, p, "");
        p = actorBlock.find("\"uiSpace\""); if (p != std::string::npos) obj.ui3d.space = (EunoiaUI::UI3DSpaceType)ParseIntAt(actorBlock, p, 0);
        p = actorBlock.find("\"uiSize\""); if (p != std::string::npos) {
            glm::vec4 v = ParseVec4(actorBlock, p);
            obj.ui3d.size = glm::vec2(v.x, v.y);
        }
        p = actorBlock.find("\"uiCanvasResolution\""); if (p != std::string::npos) {
            glm::vec4 v = ParseVec4(actorBlock, p);
            obj.ui3d.canvasResolution = glm::vec2(v.x, v.y);
        }
        p = actorBlock.find("\"uiTwoSided\""); if (p != std::string::npos) obj.ui3d.twoSided = ParseBoolAt(actorBlock, p, true);
        p = actorBlock.find("\"uiOpacity\""); if (p != std::string::npos) obj.ui3d.opacity = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"uiInteractable\""); if (p != std::string::npos) obj.ui3d.interactable = ParseBoolAt(actorBlock, p, true);
        p = actorBlock.find("\"uiBackgroundColor\"");
        if (p != std::string::npos) {
            obj.ui3d.backgroundColor = ParseVec4(actorBlock, p);
            if (std::abs(obj.ui3d.backgroundColor.r - 0.10f) < 0.03f &&
                std::abs(obj.ui3d.backgroundColor.g - 0.12f) < 0.03f &&
                std::abs(obj.ui3d.backgroundColor.b - 0.16f) < 0.03f) {
                obj.ui3d.backgroundColor = glm::vec4(0.0f);
            }
        } else {
            obj.ui3d.backgroundColor = glm::vec4(0.0f);
        }

        p = actorBlock.find("\"uiBorderColor\"");
        if (p != std::string::npos) {
            obj.ui3d.borderColor = ParseVec4(actorBlock, p);
            if (std::abs(obj.ui3d.borderColor.r - 0.25f) < 0.03f &&
                std::abs(obj.ui3d.borderColor.g - 0.60f) < 0.03f &&
                std::abs(obj.ui3d.borderColor.b - 1.00f) < 0.03f) {
                obj.ui3d.borderColor = glm::vec4(0.0f);
            }
        } else {
            obj.ui3d.borderColor = glm::vec4(0.0f);
        }

        p = actorBlock.find("\"uiBorderWidth\"");
        if (p != std::string::npos) {
            obj.ui3d.borderWidth = ParseFloatAt(actorBlock, p, 0.0f);
            if (obj.ui3d.borderColor.a <= 0.001f) obj.ui3d.borderWidth = 0.0f;
        } else {
            obj.ui3d.borderWidth = 0.0f;
        }

        // Parse Level Animation Component
        p = actorBlock.find("\"hasLevelAnimation\"");
        if (p != std::string::npos) {
            obj.hasLevelAnimation = ParseBoolAt(actorBlock, p, false);
            if (obj.hasLevelAnimation) {
                obj.levelAnimation.enabled = true;
                size_t lp = actorBlock.find("\"levelAnimationEnabled\"");
                if (lp != std::string::npos) obj.levelAnimation.enabled = ParseBoolAt(actorBlock, lp, true);
                lp = actorBlock.find("\"levelAnimationPlayOnStart\"");
                if (lp != std::string::npos) obj.levelAnimation.playOnStart = ParseBoolAt(actorBlock, lp, true);
                lp = actorBlock.find("\"levelAnimationLoop\"");
                if (lp != std::string::npos) obj.levelAnimation.loop = ParseBoolAt(actorBlock, lp, true);
                lp = actorBlock.find("\"levelAnimationPlaybackSpeed\"");
                if (lp != std::string::npos) obj.levelAnimation.playbackSpeed = ParseFloatAt(actorBlock, lp, 1.0f);
                lp = actorBlock.find("\"levelAnimationPath\"");
                if (lp != std::string::npos) {
                    std::string aPath = ParseStringAt(actorBlock, lp, "");
                    if (!aPath.empty()) {
                        obj.levelAnimation.LoadAnimation(aPath);
                    }
                }
            }
        }

        // Parse Animation Graph Modifier
        p = actorBlock.find("\"hasAnimator\"");
        if (p != std::string::npos) {
            obj.hasAnimator = ParseBoolAt(actorBlock, p, false);
            if (obj.hasAnimator) {
                obj.animator.enabled = true;
                size_t ap = actorBlock.find("\"animatorEnabled\""); if (ap != std::string::npos) obj.animator.enabled = ParseBoolAt(actorBlock, ap, true);
                ap = actorBlock.find("\"animatorGraphPath\"");
                if (ap != std::string::npos) {
                    std::string gPath = ParseStringAt(actorBlock, ap, "");
                    if (!gPath.empty()) {
                        obj.animator.LoadGraph(gPath);
                    }
                }
            }
        }

        // Parse Physics Modifier
        p = actorBlock.find("\"hasPhysics\"");
        if (p != std::string::npos) {
            obj.hasPhysics = ParseBoolAt(actorBlock, p, false);
            if (obj.hasPhysics) {
                p = actorBlock.find("\"physicsEnabled\""); if (p != std::string::npos) obj.physics.enabled = ParseBoolAt(actorBlock, p, true);
                p = actorBlock.find("\"physicsIsCharacterPhysics\"");
                if (p != std::string::npos) {
                    obj.physics.isCharacterPhysics = ParseBoolAt(actorBlock, p, false);
                    if (obj.physics.isCharacterPhysics) obj.physics.name = "Character Physics";
                }
                p = actorBlock.find("\"physicsEnableStepBackImpulse\"");
                if (p != std::string::npos) obj.physics.enableStepBackImpulse = ParseBoolAt(actorBlock, p, false);
                p = actorBlock.find("\"physicsStepBackImpulseMultiplier\"");
                if (p != std::string::npos) obj.physics.stepBackImpulseMultiplier = ParseFloatAt(actorBlock, p, 1.0f);
                p = actorBlock.find("\"physicsUseGravity\""); if (p != std::string::npos) obj.physics.useGravity = ParseBoolAt(actorBlock, p, true);
                p = actorBlock.find("\"physicsGravityMultiplier\""); if (p != std::string::npos) obj.physics.gravityMultiplier = ParseFloatAt(actorBlock, p, 1.0f);
                p = actorBlock.find("\"physicsMass\""); if (p != std::string::npos) obj.physics.mass = ParseFloatAt(actorBlock, p, 1.0f);
                p = actorBlock.find("\"physicsLinearDamping\""); if (p != std::string::npos) obj.physics.linearDamping = ParseFloatAt(actorBlock, p, 0.05f);
                p = actorBlock.find("\"physicsAngularDamping\""); if (p != std::string::npos) obj.physics.angularDamping = ParseFloatAt(actorBlock, p, 0.05f);
                p = actorBlock.find("\"physicsBounciness\""); if (p != std::string::npos) obj.physics.bounciness = ParseFloatAt(actorBlock, p, 0.2f);
                p = actorBlock.find("\"physicsFriction\""); if (p != std::string::npos) obj.physics.friction = ParseFloatAt(actorBlock, p, 0.6f);
                p = actorBlock.find("\"physicsIsKinematic\""); if (p != std::string::npos) obj.physics.isKinematic = ParseBoolAt(actorBlock, p, false);
                p = actorBlock.find("\"physicsLockPosX\""); if (p != std::string::npos) obj.physics.lockPositionX = ParseBoolAt(actorBlock, p, false);
                p = actorBlock.find("\"physicsLockPosY\""); if (p != std::string::npos) obj.physics.lockPositionY = ParseBoolAt(actorBlock, p, false);
                p = actorBlock.find("\"physicsLockPosZ\""); if (p != std::string::npos) obj.physics.lockPositionZ = ParseBoolAt(actorBlock, p, false);
                p = actorBlock.find("\"physicsLockRotX\""); if (p != std::string::npos) obj.physics.lockRotationX = ParseBoolAt(actorBlock, p, false);
                p = actorBlock.find("\"physicsLockRotY\""); if (p != std::string::npos) obj.physics.lockRotationY = ParseBoolAt(actorBlock, p, false);
                p = actorBlock.find("\"physicsLockRotZ\""); if (p != std::string::npos) obj.physics.lockRotationZ = ParseBoolAt(actorBlock, p, false);
            }
        }

        // Parse Modifiers array
        size_t modArrPos = actorBlock.find("\"modifiers\"");
        if (modArrPos != std::string::npos) {
            size_t mStart = actorBlock.find('[', modArrPos);
            size_t mEnd = (mStart != std::string::npos) ? FindMatchingBracket(actorBlock, mStart) : std::string::npos;
            if (mStart != std::string::npos && mEnd != std::string::npos) {
                obj.modifiers.clear();
                std::string modArray = actorBlock.substr(mStart, mEnd - mStart + 1);
                size_t mSearchPos = 0;
                while (true) {
                    size_t mObjStart = modArray.find('{', mSearchPos);
                    if (mObjStart == std::string::npos) break;
                    size_t mObjEnd = FindMatchingBrace(modArray, mObjStart);
                    if (mObjEnd == std::string::npos) break;

                    std::string mBlock = modArray.substr(mObjStart, mObjEnd - mObjStart + 1);
                    CollisionModifier mod;
                    size_t mp;
                    mp = mBlock.find("\"name\""); if (mp != std::string::npos) mod.name = ParseStringAt(mBlock, mp, "Box Collision");
                    mp = mBlock.find("\"enabled\""); if (mp != std::string::npos) mod.enabled = ParseBoolAt(mBlock, mp, true);
                    mp = mBlock.find("\"shape\""); if (mp != std::string::npos) mod.shapeType = (CollisionShapeType)ParseIntAt(mBlock, mp, 1);
                    mp = mBlock.find("\"isTrigger\""); if (mp != std::string::npos) mod.isTrigger = ParseBoolAt(mBlock, mp, false);
                    mp = mBlock.find("\"boxExtents\""); if (mp != std::string::npos) mod.boxExtents = ParseVec3(mBlock, mp);
                    mp = mBlock.find("\"radius\""); if (mp != std::string::npos) mod.radius = ParseFloatAt(mBlock, mp, 0.5f);
                    mp = mBlock.find("\"height\""); if (mp != std::string::npos) mod.height = ParseFloatAt(mBlock, mp, 1.0f);
                    mp = mBlock.find("\"centerOffset\""); if (mp != std::string::npos) mod.centerOffset = ParseVec3(mBlock, mp);
                    mp = mBlock.find("\"friction\""); if (mp != std::string::npos) mod.friction = ParseFloatAt(mBlock, mp, 0.5f);
                    mp = mBlock.find("\"restitution\""); if (mp != std::string::npos) mod.restitution = ParseFloatAt(mBlock, mp, 0.0f);
                    mp = mBlock.find("\"preset\""); if (mp != std::string::npos) mod.preset = ParseStringAt(mBlock, mp, "Default");
                    mp = mBlock.find("\"showWireframe\""); if (mp != std::string::npos) mod.showWireframe = ParseBoolAt(mBlock, mp, true);
                    mp = mBlock.find("\"isDefaultFit\""); if (mp != std::string::npos) mod.isDefaultFit = ParseBoolAt(mBlock, mp, true);
                    mp = mBlock.find("\"wireframeColor\""); if (mp != std::string::npos) mod.wireframeColor = ParseVec4(mBlock, mp);
                    mp = mBlock.find("\"preventSideFall\""); if (mp != std::string::npos) mod.preventSideFall = ParseBoolAt(mBlock, mp, true);
                    mp = mBlock.find("\"sideFall\""); if (mp != std::string::npos) mod.sideFall = ParseBoolAt(mBlock, mp, false);
                    mp = mBlock.find("\"planeSize\"");
                    if (mp != std::string::npos) {
                        size_t lb = mBlock.find('[', mp);
                        size_t rb = mBlock.find(']', lb);
                        if (lb != std::string::npos && rb != std::string::npos) {
                            std::string arr = mBlock.substr(lb + 1, rb - lb - 1);
                            std::stringstream ss(arr);
                            char comma;
                            ss >> mod.planeSize.x >> comma >> mod.planeSize.y;
                        }
                    }

                    obj.modifiers.push_back(mod);
                    mSearchPos = mObjEnd + 1;
                }
                if (!obj.modifiers.empty()) {
                    obj.collision = obj.modifiers[0];
                }
            }
        } else {
            // Legacy single collision fallback if older scene file
            p = actorBlock.find("\"collisionEnabled\""); if (p != std::string::npos) obj.collision.enabled = ParseBoolAt(actorBlock, p, true);
            p = actorBlock.find("\"collisionShape\""); if (p != std::string::npos) obj.collision.shapeType = (CollisionShapeType)ParseIntAt(actorBlock, p, (int)obj.collision.shapeType);
            p = actorBlock.find("\"collisionIsTrigger\""); if (p != std::string::npos) obj.collision.isTrigger = ParseBoolAt(actorBlock, p, false);
            p = actorBlock.find("\"collisionBoxExtents\""); if (p != std::string::npos) obj.collision.boxExtents = ParseVec3(actorBlock, p);
            p = actorBlock.find("\"collisionRadius\""); if (p != std::string::npos) obj.collision.radius = ParseFloatAt(actorBlock, p, obj.collision.radius);
            p = actorBlock.find("\"collisionHeight\""); if (p != std::string::npos) obj.collision.height = ParseFloatAt(actorBlock, p, obj.collision.height);
            p = actorBlock.find("\"collisionCenterOffset\""); if (p != std::string::npos) obj.collision.centerOffset = ParseVec3(actorBlock, p);
            p = actorBlock.find("\"collisionFriction\""); if (p != std::string::npos) obj.collision.friction = ParseFloatAt(actorBlock, p, 0.5f);
            p = actorBlock.find("\"collisionRestitution\""); if (p != std::string::npos) obj.collision.restitution = ParseFloatAt(actorBlock, p, 0.0f);
            p = actorBlock.find("\"collisionPreset\""); if (p != std::string::npos) obj.collision.preset = ParseStringAt(actorBlock, p, "Default");
            p = actorBlock.find("\"collisionShowWireframe\""); if (p != std::string::npos) obj.collision.showWireframe = ParseBoolAt(actorBlock, p, true);
            p = actorBlock.find("\"collisionIsDefaultFit\""); if (p != std::string::npos) obj.collision.isDefaultFit = ParseBoolAt(actorBlock, p, true);
            p = actorBlock.find("\"collisionWireframeColor\"");
            if (p != std::string::npos) {
                obj.collision.wireframeColor = ParseVec4(actorBlock, p);
            }
            p = actorBlock.find("\"collisionPlaneSize\"");
            if (p != std::string::npos) {
                size_t lb = actorBlock.find('[', p);
                size_t rb = actorBlock.find(']', lb);
                if (lb != std::string::npos && rb != std::string::npos) {
                    std::string arr = actorBlock.substr(lb + 1, rb - lb - 1);
                    std::stringstream ss(arr);
                    char comma;
                    ss >> obj.collision.planeSize.x >> comma >> obj.collision.planeSize.y;
                }
            }
            if (obj.collision.enabled && obj.collision.shapeType != CollisionShapeType::None && obj.modifiers.empty() && !obj.isImportedMesh && obj.type != PrimitiveType::ImportedMesh) {
                obj.modifiers.push_back(obj.collision);
            }
        }

        // Parse Attached Behaviours (dev.md Section 14)
        size_t behPos = actorBlock.find("\"behaviours\"");
        if (behPos != std::string::npos) {
            size_t bArrayStart = actorBlock.find('[', behPos);
            size_t bArrayEnd = (bArrayStart != std::string::npos) ? FindMatchingBracket(actorBlock, bArrayStart) : std::string::npos;
            if (bArrayStart != std::string::npos && bArrayEnd != std::string::npos) {
                std::string behArray = actorBlock.substr(bArrayStart, bArrayEnd - bArrayStart + 1);
                size_t bSearchPos = 0;
                while (true) {
                    size_t bObjStart = behArray.find('{', bSearchPos);
                    if (bObjStart == std::string::npos) break;
                    size_t bObjEnd = FindMatchingBrace(behArray, bObjStart);
                    if (bObjEnd == std::string::npos) break;

                    std::string bBlock = behArray.substr(bObjStart, bObjEnd - bObjStart + 1);
                    size_t cnPos = bBlock.find("\"className\"");
                    std::string className = (cnPos != std::string::npos) ? ParseStringAt(bBlock, cnPos, "") : "";
                    size_t apPos = bBlock.find("\"assetPath\"");
                    std::string assetPath = (apPos != std::string::npos) ? ParseStringAt(bBlock, apPos, "") : "";
                    if (!className.empty()) {
                        if (!BehaviourRegistry::Get().Has(className) && !assetPath.empty()) {
                            if (assetPath.ends_with(".cpp")) {
                                BehaviourRegistry::Get().RegisterScriptFile(className, assetPath);
                            }
                        }
                        auto bInst = BehaviourRegistry::Get().Create(className);
                        if (bInst) {
                            if (!assetPath.empty()) {
                                bInst->SetSourceCppPath(assetPath);
                            }
                            size_t enPos = bBlock.find("\"enabled\"");
                            if (enPos != std::string::npos) {
                                bInst->SetEnabled(ParseBoolAt(bBlock, enPos, true));
                            }

                            for (auto& prop : bInst->GetProperties()) {
                                size_t propSearch = bBlock.find("\"" + prop.name + "\"");
                                if (propSearch != std::string::npos) {
                                    size_t itemStart = bBlock.rfind('{', propSearch);
                                    size_t itemEnd = bBlock.find('}', propSearch);
                                    if (itemStart != std::string::npos && itemEnd != std::string::npos) {
                                        std::string pItem = bBlock.substr(itemStart, itemEnd - itemStart + 1);
                                        if (prop.type == BehaviourPropertyType::Bool && prop.dataPtr) {
                                            size_t vp = pItem.find("\"value\"");
                                            if (vp != std::string::npos) *reinterpret_cast<bool*>(prop.dataPtr) = ParseBoolAt(pItem, vp, *reinterpret_cast<bool*>(prop.dataPtr));
                                        } else if (prop.type == BehaviourPropertyType::Int && prop.dataPtr) {
                                            size_t vp = pItem.find("\"value\"");
                                            if (vp != std::string::npos) *reinterpret_cast<int*>(prop.dataPtr) = ParseIntAt(pItem, vp, *reinterpret_cast<int*>(prop.dataPtr));
                                        } else if (prop.type == BehaviourPropertyType::Float && prop.dataPtr) {
                                            size_t vp = pItem.find("\"value\"");
                                            if (vp != std::string::npos) *reinterpret_cast<float*>(prop.dataPtr) = ParseFloatAt(pItem, vp, *reinterpret_cast<float*>(prop.dataPtr));
                                        } else if (prop.type == BehaviourPropertyType::Double && prop.dataPtr) {
                                            size_t vp = pItem.find("\"value\"");
                                            if (vp != std::string::npos) *reinterpret_cast<double*>(prop.dataPtr) = (double)ParseFloatAt(pItem, vp, (float)*reinterpret_cast<double*>(prop.dataPtr));
                                        } else if (prop.type == BehaviourPropertyType::String && prop.dataPtr) {
                                            size_t vp = pItem.find("\"value\"");
                                            if (vp != std::string::npos) *reinterpret_cast<std::string*>(prop.dataPtr) = ParseStringAt(pItem, vp, *reinterpret_cast<std::string*>(prop.dataPtr));
                                        } else if (prop.type == BehaviourPropertyType::Vec3 && prop.dataPtr) {
                                            size_t vp = pItem.find("\"value\"");
                                            if (vp != std::string::npos) *reinterpret_cast<glm::vec3*>(prop.dataPtr) = ParseVec3(pItem, vp);
                                        } else if (prop.type == BehaviourPropertyType::Color3 && prop.dataPtr) {
                                            size_t vp = pItem.find("\"value\"");
                                            if (vp != std::string::npos) *reinterpret_cast<glm::vec3*>(prop.dataPtr) = ParseVec3(pItem, vp);
                                        } else if (prop.type == BehaviourPropertyType::ObjectRef) {
                                            size_t tidPos = pItem.find("\"targetId\"");
                                            if (tidPos != std::string::npos) prop.targetId = ParseIntAt(pItem, tidPos, -1);
                                            size_t apPos = pItem.find("\"assetPath\"");
                                            if (apPos != std::string::npos) prop.assetPath = ParseStringAt(pItem, apPos);
                                        }
                                    }
                                }
                            }
                            obj.AddBehaviour(std::move(bInst));
                        }
                    }
                    bSearchPos = bObjEnd + 1;
                }
            }
        }

        auto itFast = fastMeshes.find(obj.id);
        if (itFast != fastMeshes.end() && !itFast->second.vertices.empty()) {
            obj.mesh = std::move(itFast->second);
        } else {
            // Rebuild mesh geometry with loaded params for standard primitives
            if (!obj.isImportedMesh && !obj.isLight && !IsLightPrimitive(obj.type) && !obj.isCamera && !IsCameraPrimitive(obj.type) && obj.type != PrimitiveType::Empty) {
                obj.RebuildMesh();
            }

            // Restore mesh geometry for imported meshes, or empty mesh for empty/light actors
            if (obj.isImportedMesh && !obj.meshFilePath.empty()) {
                std::string resolvedMeshPath = obj.meshFilePath;
                std::error_code ec;
                if (!std::filesystem::exists(resolvedMeshPath, ec)) {
                    // Try AssetManager resolution
                    std::filesystem::path amRes = AssetManager::Get().ResolvePhysicalOrCookedPath(obj.meshFilePath);
                    if (!amRes.empty() && std::filesystem::exists(amRes, ec)) {
                        resolvedMeshPath = amRes.string();
                    }
                }
                if (!std::filesystem::exists(resolvedMeshPath, ec)) {
                    // Try Projects root scan
                    std::filesystem::path projectsDir = "Projects";
                    if (std::filesystem::exists(projectsDir, ec)) {
                        for (const auto& entry : std::filesystem::recursive_directory_iterator(projectsDir, ec)) {
                            if (!entry.is_directory(ec) && entry.path().filename() == std::filesystem::path(obj.meshFilePath).filename()) {
                                resolvedMeshPath = entry.path().string();
                                break;
                            }
                        }
                    }
                }

                ImportedModel model;
                try {
                    model = MeshImporter::Load(resolvedMeshPath);
                    obj.meshFilePath = resolvedMeshPath;
                } catch (const std::exception& e) {
                    std::cout << "[SceneSerializer] Error loading mesh for " << obj.name << ": " << e.what() << " (neglecting, forcing open)\n";
                } catch (...) {
                    std::cout << "[SceneSerializer] Unknown error loading mesh for " << obj.name << " (neglecting, forcing open)\n";
                }
                if (model.valid && !model.meshes.empty()) {
                    if (obj.submeshIndex >= 0 && obj.submeshIndex < (int)model.meshes.size()) {
                        obj.mesh.vertices = model.meshes[obj.submeshIndex].vertices;
                        obj.mesh.indices  = model.meshes[obj.submeshIndex].indices;

                        glm::vec3 minBound(1e9f);
                        glm::vec3 maxBound(-1e9f);
                        for (const auto& v : obj.mesh.vertices) {
                            if (!std::isnan(v.pos.x) && !std::isnan(v.pos.y) && !std::isnan(v.pos.z) &&
                                !std::isinf(v.pos.x) && !std::isinf(v.pos.y) && !std::isinf(v.pos.z)) {
                                minBound = glm::min(minBound, v.pos);
                                maxBound = glm::max(maxBound, v.pos);
                            }
                        }
                        glm::vec3 pivot = (minBound + maxBound) * 0.5f;
                        uint32_t vCount = (uint32_t)obj.mesh.vertices.size();
                        for (uint32_t& idx : obj.mesh.indices) { if (idx >= vCount) idx = 0; }
                        for (auto& v : obj.mesh.vertices) {
                            if (std::isnan(v.pos.x)||std::isnan(v.pos.y)||std::isnan(v.pos.z)||
                                std::isinf(v.pos.x)||std::isinf(v.pos.y)||std::isinf(v.pos.z)) {
                                v.pos = glm::vec3(0.0f);
                            } else {
                                v.pos -= pivot;
                            }
                        }
                        if (obj.parentId != -1 && glm::length(obj.position) < 0.0001f && glm::length(pivot) > 0.0001f) {
                            obj.position = pivot;
                        }
                    } else if (model.meshes.size() == 1) {
                        if (!model.meshes[0].vertices.empty()) {
                            obj.mesh.vertices = model.meshes[0].vertices;
                            obj.mesh.indices  = model.meshes[0].indices;
                        } else {
                            obj.mesh = model.GetMergedMesh();
                        }
                        glm::vec3 minBound(1e9f);
                        glm::vec3 maxBound(-1e9f);
                        for (const auto& v : obj.mesh.vertices) {
                            if (!std::isnan(v.pos.x) && !std::isnan(v.pos.y) && !std::isnan(v.pos.z) &&
                                !std::isinf(v.pos.x) && !std::isinf(v.pos.y) && !std::isinf(v.pos.z)) {
                            minBound = glm::min(minBound, v.pos);
                            maxBound = glm::max(maxBound, v.pos);
                            }
                        }
                        glm::vec3 pivot = (minBound + maxBound) * 0.5f;
                        uint32_t vCount = (uint32_t)obj.mesh.vertices.size();
                        for (uint32_t& idx : obj.mesh.indices) { if (idx >= vCount) idx = 0; }
                        for (auto& v : obj.mesh.vertices) {
                            if (std::isnan(v.pos.x)||std::isnan(v.pos.y)||std::isnan(v.pos.z)||
                                std::isinf(v.pos.x)||std::isinf(v.pos.y)||std::isinf(v.pos.z)) {
                                v.pos = glm::vec3(0.0f);
                            } else {
                                v.pos -= pivot;
                            }
                        }
                        if (obj.parentId != -1 && glm::length(obj.position) < 0.0001f && glm::length(pivot) > 0.0001f) {
                            obj.position = pivot;
                        }
                    } else {
                        // submeshIndex == -1 with multiple meshes: parent/root node, keeps empty geometry
                        obj.mesh.vertices.clear();
                        obj.mesh.indices.clear();
                    }
                } else {
                    // Fallback to empty mesh or default cube representation so scene forces open
                    obj.mesh = GeometryBuilder::CreateCube(1.0f, 1);
                    std::cout << "[SceneSerializer] Notice: Actor '" << obj.name << "' (ID: " << obj.id << ") mesh '" << obj.meshFilePath << "' could not be loaded. Force opening with fallback geometry.\n";
                }
            } else if (obj.type == PrimitiveType::Empty || obj.isLight || IsLightPrimitive(obj.type)) {
                obj.isLight = (obj.type != PrimitiveType::Empty);
                obj.mesh.vertices.clear();
                obj.mesh.indices.clear();
            }

            if (!obj.mesh.vertices.empty()) {
                if (!obj.smoothShading) {
                    GeometryBuilder::ComputeFlatNormals(obj.mesh);
                }
            }
        }

        // Sanitize already-corrupted transforms on load
        {
            bool corrupted = false;
            auto sanitizeVec3 = [&](glm::vec3& v, const glm::vec3& fallback, const char* fieldName) {
                if (std::isnan(v.x) || std::isnan(v.y) || std::isnan(v.z) ||
                    !std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
                    v = fallback;
                    corrupted = true;
                    (void)fieldName;
                }
            };
            sanitizeVec3(obj.position, glm::vec3(0.0f), "position");
            sanitizeVec3(obj.rotation, glm::vec3(0.0f), "rotation");
            sanitizeVec3(obj.scale,    glm::vec3(1.0f), "scale");
            const float kMinScale = 1e-4f;
            obj.scale = glm::max(obj.scale, glm::vec3(kMinScale));
            if (corrupted) {
                std::cerr << "[SceneLoad] WARNING: Actor \"" << obj.name
                          << "\" (id=" << obj.id << ") had NaN/Inf transform on load — sanitized.\n";
            }
        }

        // Sanitize corrupted mesh vertices
        int badVertexCount = 0;
        for (auto& v : obj.mesh.vertices) {
            if (std::isnan(v.pos.x) || std::isnan(v.pos.y) || std::isnan(v.pos.z) ||
                !std::isfinite(v.pos.x) || !std::isfinite(v.pos.y) || !std::isfinite(v.pos.z)) {
                v.pos = glm::vec3(0.0f);
                ++badVertexCount;
            }
        }
        if (badVertexCount > 0) {
            std::cerr << "[SceneLoad] WARNING: Actor \"" << obj.name << "\" (id=" << obj.id
                      << ") had " << badVertexCount << " corrupted mesh vertex position(s) — sanitized.\n";
        }

        return obj;
    }

    static void FinalizeActors(Scene& scene) {
        // Reconstruct childIds on all objects from parentId and ensure continuous 20 LOD levels
        for (auto& o : scene.objects) {
            o.childIds.clear();
            o.mesh.CalculateAABB();
            if (o.mesh.indices.size() >= 48 && o.mesh.lodIndices.empty()) {
                o.mesh.GenerateLODs();
            }
        }
        for (const auto& o : scene.objects) {
            if (o.parentId != -1) {
                GameObject* parent = scene.FindObject(o.parentId);
                if (parent) {
                    parent->childIds.push_back(o.id);
                }
            }
        }

        scene.SyncLightActors();
        scene.SyncLightPositionsFromActors();
        scene.ResolveAllBehaviourReferences();
    }

    static void ParseActors(const std::string& json, Scene& scene, const std::string& binPath = "") {
        auto fastMeshes = LoadFastMeshMap(binPath);
        auto blocks = ExtractActorBlocks(json);
        for (const auto& block : blocks) {
            scene.objects.push_back(ParseSingleActor(block, fastMeshes));
        }
        FinalizeActors(scene);
    }

    enum class ProgressiveLoadStage {
        Idle,
        Addressables,
        Skybox,
        Lights,
        Meshes,
        PostProcessing,
        Finalize,
        Completed
    };

    struct ProgressiveSceneLoader {
        std::atomic<bool> active{false};
        std::atomic<ProgressiveLoadStage> stage{ProgressiveLoadStage::Idle};
        std::string filePath;
        std::string binPath;
        std::string content;
        float completionTimer = 0.0f;
        std::atomic<bool> completed{false};
        std::atomic<bool> hasError{false};
        std::atomic<bool> cancelRequested{false};
        std::atomic<uint64_t> currentSessionId{0};
        std::atomic<float> progress{0.0f};
        std::atomic<Eunoia::SceneLoadState> loadState{Eunoia::SceneLoadState::Unloaded};

        std::mutex stateMutex;
        std::string stageName;
        std::string currentDetail;
        std::string subDetail;
        std::deque<GameObject> stagedActors;
        std::atomic<bool> bgParseDone{false};
        std::atomic<bool> envParsed{false};
        std::chrono::high_resolution_clock::time_point startTime;

        std::thread workerThread;

        ~ProgressiveSceneLoader() {
            Cancel();
        }

        void Cancel() {
            cancelRequested = true;
            if (workerThread.joinable()) {
                workerThread.join();
            }
        }

        void Start(Scene& scene, const std::string& levelPath, const std::string& binaryPath = "") {
            Cancel();
            cancelRequested = false;
            uint64_t session = ++currentSessionId;

            filePath = levelPath;
            binPath = binaryPath;
            if (binPath.empty()) {
                std::filesystem::path p(filePath);
                std::filesystem::path cand = p.parent_path() / (p.stem().string() + ".bin");
                if (std::filesystem::exists(cand)) {
                    binPath = cand.string();
                }
            }

            // Clear the scene first
            scene.objects.clear();
            scene.pointLights.clear();
            scene.selectedId = -1;
            scene.outlinerNodeOpenState.clear();
            scene.contentBrowserFolderOpenState.clear();

            active = true;
            completed = false;
            hasError = false;
            bgParseDone = false;
            envParsed = false;
            completionTimer = 0.8f;
            stage = ProgressiveLoadStage::Skybox;
            loadState = Eunoia::SceneLoadState::ReadingCache;
            progress = 0.05f;
            startTime = std::chrono::high_resolution_clock::now();

            {
                std::lock_guard<std::mutex> lock(stateMutex);
                stageName = "Initializing";
                currentDetail = "Reading level file...";
                subDetail = std::filesystem::path(filePath).filename().string();
                stagedActors.clear();
            }

            EngineLogger::Get().LogAction("LEVEL_LOAD_START", filePath, "Asynchronous background level loading initiated");

            workerThread = std::thread([this, session, levelPath, binaryPathCopy = binPath]() {
                std::string actualPath = levelPath;
                std::error_code ec;
                if (!std::filesystem::exists(actualPath, ec) || std::filesystem::file_size(actualPath, ec) == 0) {
                    std::filesystem::path resolved = AssetManager::Get().ResolvePhysicalOrCookedPath(levelPath);
                    if (!resolved.empty() && std::filesystem::exists(resolved, ec) && std::filesystem::file_size(resolved, ec) > 0) {
                        actualPath = resolved.string();
                    }
                }
                std::ifstream file(actualPath);
                if (!file.is_open()) {
                    if (session != currentSessionId.load() || cancelRequested.load()) return;
                    hasError = true;
                    completed = true;
                    stage = ProgressiveLoadStage::Completed;
                    {
                        std::lock_guard<std::mutex> lock(stateMutex);
                        stageName = "Error";
                        currentDetail = "Failed to open level file: " + levelPath;
                        subDetail = "";
                    }
                    progress = 1.0f;
                    completionTimer = 2.5f;
                    return;
                }

                std::stringstream ss;
                ss << file.rdbuf();
                file.close();
                std::string fileContent = ss.str();

                if (session != currentSessionId.load() || cancelRequested.load()) return;

                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    content = fileContent;
                }

                if (session != currentSessionId.load() || cancelRequested.load()) return;

                // Asynchronous Addressable Preload (Models & Textures)
                std::vector<std::string> modelAddresses;
                std::vector<std::pair<std::string, TextureUsageSlot>> textureAddresses;
                SceneSerializer::ExtractLevelAddressableAssets(fileContent, modelAddresses, textureAddresses);

                if (!modelAddresses.empty() || !textureAddresses.empty()) {
                    stage = ProgressiveLoadStage::Addressables;
                    {
                        std::lock_guard<std::mutex> lock(stateMutex);
                        stageName = "Addressables";
                        currentDetail = "Asynchronously preloading assets...";
                        subDetail = std::to_string(modelAddresses.size()) + " models, " + std::to_string(textureAddresses.size()) + " textures";
                        progress = 0.10f;
                    }

                    Eunoia::Addressables::PreloadLevelAssetsAsync(
                        modelAddresses,
                        textureAddresses,
                        [this](float subProg, const std::string& item, const std::string& cat) {
                            std::lock_guard<std::mutex> lock(stateMutex);
                            progress = 0.10f + 0.18f * subProg; // 0.10f to 0.28f
                            currentDetail = "Pre-decoding Addressable " + cat + ": " + std::filesystem::path(item).filename().string();
                            subDetail = cat + " (" + item + ")";
                        },
                        &cancelRequested
                    );
                }

                if (session != currentSessionId.load() || cancelRequested.load()) return;

                auto fastMeshes = SceneSerializer::LoadFastMeshMap(binaryPathCopy);
                if (session != currentSessionId.load() || cancelRequested.load()) return;

                auto blocks = SceneSerializer::ExtractActorBlocks(fileContent);
                if (session != currentSessionId.load() || cancelRequested.load()) return;

                size_t total = blocks.size();
                stage = ProgressiveLoadStage::Meshes;
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    stageName = "Meshes";
                    currentDetail = "Loading " + std::to_string(total) + " actors...";
                    progress = 0.30f;
                }

                for (size_t i = 0; i < blocks.size(); ++i) {
                    if (session != currentSessionId.load() || cancelRequested.load()) return;

                    GameObject obj = SceneSerializer::ParseSingleActor(blocks[i], fastMeshes);

                    {
                        std::lock_guard<std::mutex> lock(stateMutex);
                        currentDetail = "Loading actor: " + obj.name + " (" + std::to_string(i + 1) + "/" + std::to_string(total) + ")";
                        subDetail = obj.isImportedMesh ? ("Model: " + std::filesystem::path(obj.meshFilePath).filename().string()) : (std::string("Type: ") + GetPrimitiveTypeName(obj.type));
                        progress = 0.30f + 0.65f * ((float)(i + 1) / (float)(total > 0 ? total : 1));
                        stagedActors.push_back(std::move(obj));
                    }
                }

                if (session != currentSessionId.load() || cancelRequested.load()) return;
                bgParseDone = true;
            });
        }

        bool Step(Scene& scene, size_t actorsPerStep = 6) {
            if (!active.load() || completed.load()) return false;

            // Apply environment and skybox once content is ready
            if (!envParsed.load()) {
                std::string contentCopy;
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    contentCopy = content;
                }
                if (!contentCopy.empty()) {
                    SceneSerializer::ParseSkyboxAndEnvironment(contentCopy, scene);
                    SceneSerializer::ParseLights(contentCopy, scene);
                    SceneSerializer::ParsePostProcessing(contentCopy, scene);
                    SceneSerializer::ParseMetadata(contentCopy, scene);
                    envParsed = true;
                    stage = ProgressiveLoadStage::Meshes;
                }
            }

            // Transfer batch of parsed actors from background queue to main scene
            std::vector<GameObject> toAdd;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                size_t count = 0;
                while (!stagedActors.empty() && count < actorsPerStep) {
                    toAdd.push_back(std::move(stagedActors.front()));
                    stagedActors.pop_front();
                    count++;
                }
            }

            for (auto& obj : toAdd) {
                scene.objects.push_back(std::move(obj));
            }

            bool isAllTransferred = false;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                isAllTransferred = bgParseDone.load() && stagedActors.empty();
            }

            if (isAllTransferred && !completed.load()) {
                if (workerThread.joinable()) {
                    workerThread.join();
                }

                SceneSerializer::FinalizeActors(scene);

                // WickedEngine Fast Binary Archive Technique:
                // Cache geometry binary stream for instant loading on subsequent plays
                if (binPath.empty() || !std::filesystem::exists(binPath)) {
                    std::filesystem::path p(filePath);
                    std::string autoBin = (p.parent_path() / (p.stem().string() + ".bin")).string();
                    SceneSerializer::SaveFastMeshCache(scene, autoBin);
                }

                if (!scene.objects.empty()) {
                    scene.selectedId = scene.objects.front().id;
                }

                auto endTime = std::chrono::high_resolution_clock::now();
                float elapsedSec = std::chrono::duration<float>(endTime - startTime).count();

                stage = ProgressiveLoadStage::Completed;
                loadState = Eunoia::SceneLoadState::Ready;
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    stageName = "Ready";
                    currentDetail = "Level loaded (" + std::to_string(scene.objects.size()) + " actors in " + std::to_string(elapsedSec) + "s)";
                    subDetail = std::filesystem::path(filePath).filename().string();
                }
                progress = 1.0f;
                completed = true;
                completionTimer = 0.8f;

                EngineLogger::Get().LogAction("LEVEL_LOAD_COMPLETE", filePath, "Loaded " + std::to_string(scene.objects.size()) + " actors in " + std::to_string(elapsedSec) + "s");
                return false;
            }

            return true;
        }
    };

    static bool LoadLevel(Scene& level, const std::string& filePath, const std::string& binPath = "") {
        return LoadScene(level, filePath, nullptr, binPath);
    }
};

using LevelSerializer = SceneSerializer;
