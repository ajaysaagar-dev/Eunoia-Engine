#pragma once
#include <EngineScene/Scene.h>
#include <EngineScene/BehaviourRegistry.h>
#include <VisualBehaviuors/VisualBehaviourInstance.h>
#include <EngineCore/EngineLogger.h>
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

    // Fast binary cache for pre-parsed mesh geometries (bypasses heavy OBJ/FBX disk parsing on game start)
    static bool SaveFastMeshCache(const Scene& scene, const std::string& binPath) {
        std::ofstream out(binPath, std::ios::binary);
        if (!out.is_open()) return false;

        uint32_t magic = 0x454D5348; // "EMSH"
        out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));

        uint32_t actorCount = (uint32_t)scene.objects.size();
        out.write(reinterpret_cast<const char*>(&actorCount), sizeof(actorCount));

        for (const auto& obj : scene.objects) {
            out.write(reinterpret_cast<const char*>(&obj.id), sizeof(obj.id));
            uint32_t vCount = (uint32_t)obj.mesh.vertices.size();
            out.write(reinterpret_cast<const char*>(&vCount), sizeof(vCount));
            if (vCount > 0) {
                out.write(reinterpret_cast<const char*>(obj.mesh.vertices.data()), vCount * sizeof(MeshVertex));
            }
            uint32_t iCount = (uint32_t)obj.mesh.indices.size();
            out.write(reinterpret_cast<const char*>(&iCount), sizeof(iCount));
            if (iCount > 0) {
                out.write(reinterpret_cast<const char*>(obj.mesh.indices.data()), iCount * sizeof(uint32_t));
            }
        }
        return true;
    }

    static std::unordered_map<int, PrimitiveMesh> LoadFastMeshMap(const std::string& binPath) {
        std::unordered_map<int, PrimitiveMesh> meshMap;
        if (binPath.empty() || !std::filesystem::exists(binPath)) return meshMap;
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
        std::ofstream file(filePath);
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
        file << "      \"strength\": " << scene.shadowStrength << ",\n";
        file << "      \"bias\": " << scene.shadowBias << ",\n";
        file << "      \"pcfRadius\": " << scene.pcfRadius << "\n";
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
        file << "      \"colorTint\": " << scene.postColorTint << "\n";
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
            file << "        \"opacity\": " << obj.opacity << ",\n";
            file << "        \"opacityMaskClipValue\": " << obj.opacityMaskClipValue << ",\n";
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

            // Light Component Properties (dev.md)
            file << "      \"lightType\": " << (int)obj.light.type << ",\n";
            file << "      \"lightEnabled\": " << BoolStr(obj.light.enabled) << ",\n";
            file << "      \"lightColor\": " << WriteVec3(obj.light.color) << ",\n";
            file << "      \"lightIntensity\": " << obj.light.intensity << ",\n";
            file << "      \"lightTemperature\": " << obj.light.temperature << ",\n";
            file << "      \"lightUseTemp\": " << BoolStr(obj.light.useTemperature) << ",\n";
            file << "      \"lightRange\": " << obj.light.range << ",\n";
            file << "      \"lightAttenuation\": " << obj.light.attenuation << ",\n";
            file << "      \"lightInnerCone\": " << obj.light.innerConeAngle << ",\n";
            file << "      \"lightOuterCone\": " << obj.light.outerConeAngle << ",\n";
            file << "      \"lightConeFalloff\": " << obj.light.coneFalloff << ",\n";
            file << "      \"lightAreaShape\": " << obj.light.areaShape << ",\n";
            file << "      \"lightAreaWidth\": " << obj.light.width << ",\n";
            file << "      \"lightAreaHeight\": " << obj.light.height << ",\n";
            file << "      \"lightAreaRadius\": " << obj.light.radius << ",\n";
            file << "      \"lightAreaLength\": " << obj.light.length << ",\n";
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
            file << "      \"lightLayer\": " << obj.light.lightLayer << ",\n";

            // Baking System (dev.md)
            file << "      \"lightBakeState\": " << (int)obj.light.bakeState << ",\n";
            file << "      \"lightBakeRes\": " << obj.light.bakeResolution << ",\n";
            file << "      \"lightBakeDirty\": " << BoolStr(obj.light.isBakeDirty) << ",\n";
            file << "      \"lightGuid\": " << QuoteStr(obj.light.lightGuid) << ",\n";
            file << "      \"lightBakedLightmap\": " << QuoteStr(obj.light.bakedLightmapPath) << ",\n";
            file << "      \"lightBakedShadowmap\": " << QuoteStr(obj.light.bakedShadowmapPath) << ",\n";
            file << "      \"lightBakeSig\": " << QuoteStr(std::to_string(obj.light.bakeSignature)) << ",\n";
            file << "      \"lightBakedPos\": " << WriteVec3(obj.light.bakedPosition) << ",\n";
            file << "      \"lightBakedRot\": " << WriteVec3(obj.light.bakedRotation) << ",\n";
            file << "      \"lightBakedIntensity\": " << obj.light.bakedIntensity << ",\n";
            file << "      \"lightBakedColor\": " << WriteVec3(obj.light.bakedColor) << ",\n";
            file << "      \"lightBakedRange\": " << obj.light.bakedRange << ",\n";
            file << "      \"lightBakedInnerCone\": " << obj.light.bakedInnerCone << ",\n";
            file << "      \"lightBakedOuterCone\": " << obj.light.bakedOuterCone << ",\n";
            file << "      \"lightBakedShadowBias\": " << obj.light.bakedShadowBias << ",\n";

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

            file << "      \"isUserInterface\": " << BoolStr(obj.isUserInterface || obj.type == PrimitiveType::UserInterface) << ",\n";
            file << "      \"uiAssetPath\": \"" << obj.ui3d.uiAssetPath << "\",\n";
            file << "      \"uiSpace\": " << (int)obj.ui3d.space << ",\n";
            file << "      \"uiSize\": [" << obj.ui3d.size.x << ", " << obj.ui3d.size.y << "],\n";
            file << "      \"uiCanvasResolution\": [" << obj.ui3d.canvasResolution.x << ", " << obj.ui3d.canvasResolution.y << "],\n";
            file << "      \"uiTwoSided\": " << BoolStr(obj.ui3d.twoSided) << ",\n";
            file << "      \"uiOpacity\": " << obj.ui3d.opacity << ",\n";
            file << "      \"uiInteractable\": " << BoolStr(obj.ui3d.interactable) << ",\n";
            file << "      \"uiBackgroundColor\": [" << obj.ui3d.backgroundColor.r << ", " << obj.ui3d.backgroundColor.g << ", " << obj.ui3d.backgroundColor.b << ", " << obj.ui3d.backgroundColor.a << "],\n";
            file << "      \"uiBorderColor\": [" << obj.ui3d.borderColor.r << ", " << obj.ui3d.borderColor.g << ", " << obj.ui3d.borderColor.b << ", " << obj.ui3d.borderColor.a << "],\n";
            file << "      \"uiBorderWidth\": " << obj.ui3d.borderWidth << ",\n";

            file << "      \"hasPhysics\": " << BoolStr(obj.hasPhysics) << ",\n";
            if (obj.hasPhysics) {
                file << "      \"physicsEnabled\": " << BoolStr(obj.physics.enabled) << ",\n";
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
        file << "  \"playAnimations\": " << BoolStr(scene.playAnimations) << "\n";

        file << "}\n";
        file.close();
        return true;
    }

    // Save level alias
    static bool SaveLevel(const Scene& level, const std::string& filePath, SceneProgressFn onProgress = nullptr) {
        return SaveScene(level, filePath, onProgress);
    }

    // Load a scene from a JSON file
    static bool LoadScene(Scene& scene, const std::string& filePath, SceneProgressFn onProgress = nullptr, const std::string& binPath = "") {
        if (onProgress) onProgress(0.1f, "Opening level file", filePath);
        std::ifstream file(filePath);
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

                // Shadows
                size_t shadowPos = envBlock.find("\"shadows\"");
                if (shadowPos != std::string::npos) {
                    size_t sStart = envBlock.find('{', shadowPos);
                    size_t sEnd = FindMatchingBrace(envBlock, sStart);
                    std::string sBlock = envBlock.substr(sStart, sEnd - sStart + 1);

                    size_t p;
                    p = sBlock.find("\"enabled\""); if (p != std::string::npos) scene.enableShadows = ParseBoolAt(sBlock, p, true);
                    p = sBlock.find("\"strength\""); if (p != std::string::npos) scene.shadowStrength = ParseFloatAt(sBlock, p, 0.85f);
                    p = sBlock.find("\"bias\""); if (p != std::string::npos) scene.shadowBias = ParseFloatAt(sBlock, p, 0.0012f);
                    p = sBlock.find("\"pcfRadius\""); if (p != std::string::npos) scene.pcfRadius = ParseFloatAt(sBlock, p, 1.2f);
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
        } else {
            scene.enablePostProcessing = false;
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
            p = matBlock.find("\"opacity\""); if (p != std::string::npos) obj.opacity = ParseFloatAt(matBlock, p, 1.0f);
            p = matBlock.find("\"opacityMaskClipValue\""); if (p != std::string::npos) obj.opacityMaskClipValue = ParseFloatAt(matBlock, p, 0.333f);
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
        p = actorBlock.find("\"lightEnabled\""); if (p != std::string::npos) obj.light.enabled = ParseBoolAt(actorBlock, p, true);
        p = actorBlock.find("\"lightColor\""); if (p != std::string::npos) obj.light.color = ParseVec3(actorBlock, p);
        p = actorBlock.find("\"lightIntensity\""); if (p != std::string::npos) obj.light.intensity = ParseFloatAt(actorBlock, p, 2.0f);
        p = actorBlock.find("\"lightTemperature\""); if (p != std::string::npos) obj.light.temperature = ParseFloatAt(actorBlock, p, 6500.0f);
        p = actorBlock.find("\"lightUseTemp\""); if (p != std::string::npos) obj.light.useTemperature = ParseBoolAt(actorBlock, p, false);
        p = actorBlock.find("\"lightRange\""); if (p != std::string::npos) obj.light.range = ParseFloatAt(actorBlock, p, 10.0f);
        p = actorBlock.find("\"lightAttenuation\""); if (p != std::string::npos) obj.light.attenuation = ParseFloatAt(actorBlock, p, 2.0f);
        p = actorBlock.find("\"lightInnerCone\""); if (p != std::string::npos) obj.light.innerConeAngle = ParseFloatAt(actorBlock, p, 20.0f);
        p = actorBlock.find("\"lightOuterCone\""); if (p != std::string::npos) obj.light.outerConeAngle = ParseFloatAt(actorBlock, p, 45.0f);
        p = actorBlock.find("\"lightConeFalloff\""); if (p != std::string::npos) obj.light.coneFalloff = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"lightAreaShape\""); if (p != std::string::npos) obj.light.areaShape = ParseIntAt(actorBlock, p, 0);
        p = actorBlock.find("\"lightAreaWidth\""); if (p != std::string::npos) obj.light.width = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"lightAreaHeight\""); if (p != std::string::npos) obj.light.height = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"lightAreaRadius\""); if (p != std::string::npos) obj.light.radius = ParseFloatAt(actorBlock, p, 0.5f);
        p = actorBlock.find("\"lightAreaLength\""); if (p != std::string::npos) obj.light.length = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"lightTwoSided\""); if (p != std::string::npos) obj.light.twoSided = ParseBoolAt(actorBlock, p, false);
        p = actorBlock.find("\"lightSkyColor\""); if (p != std::string::npos) obj.light.skyColor = ParseVec3(actorBlock, p);
        p = actorBlock.find("\"lightGroundColor\""); if (p != std::string::npos) obj.light.groundColor = ParseVec3(actorBlock, p);
        p = actorBlock.find("\"lightEnvMap\""); if (p != std::string::npos) obj.light.envMapTexture = ParseStringAt(actorBlock, p, "");
        p = actorBlock.find("\"lightCastShadows\""); if (p != std::string::npos) obj.light.castShadows = ParseBoolAt(actorBlock, p, true);
        p = actorBlock.find("\"lightShadowStrength\""); if (p != std::string::npos) obj.light.shadowStrength = ParseFloatAt(actorBlock, p, 0.85f);
        p = actorBlock.find("\"lightShadowBias\""); if (p != std::string::npos) obj.light.shadowBias = ParseFloatAt(actorBlock, p, 0.0012f);
        p = actorBlock.find("\"lightShadowRes\""); if (p != std::string::npos) obj.light.shadowResolution = ParseIntAt(actorBlock, p, 2048);
        p = actorBlock.find("\"lightShadowDist\""); if (p != std::string::npos) obj.light.shadowDistance = ParseFloatAt(actorBlock, p, 50.0f);
        p = actorBlock.find("\"lightVolumetric\""); if (p != std::string::npos) obj.light.volumetric = ParseBoolAt(actorBlock, p, false);
        p = actorBlock.find("\"lightVolScattering\""); if (p != std::string::npos) obj.light.volumetricScattering = ParseFloatAt(actorBlock, p, 0.2f);
        p = actorBlock.find("\"lightVolIntensity\""); if (p != std::string::npos) obj.light.volumetricIntensity = ParseFloatAt(actorBlock, p, 1.0f);
        p = actorBlock.find("\"lightLayer\""); if (p != std::string::npos) obj.light.lightLayer = (uint32_t)ParseIntAt(actorBlock, p, 1);

        // Parse Baking Properties
        p = actorBlock.find("\"lightBakeState\""); if (p != std::string::npos) obj.light.bakeState = (Eunoia::LightBakeState)ParseIntAt(actorBlock, p, 0);
        p = actorBlock.find("\"lightBakeRes\""); if (p != std::string::npos) obj.light.bakeResolution = ParseIntAt(actorBlock, p, 1024);
        p = actorBlock.find("\"lightBakeDirty\""); if (p != std::string::npos) obj.light.isBakeDirty = ParseBoolAt(actorBlock, p, false);
        p = actorBlock.find("\"lightGuid\""); if (p != std::string::npos) obj.light.lightGuid = ParseStringAt(actorBlock, p, "");
        p = actorBlock.find("\"lightBakedLightmap\""); if (p != std::string::npos) obj.light.bakedLightmapPath = ParseStringAt(actorBlock, p, "");
        p = actorBlock.find("\"lightBakedShadowmap\""); if (p != std::string::npos) obj.light.bakedShadowmapPath = ParseStringAt(actorBlock, p, "");
        p = actorBlock.find("\"lightBakeSig\""); if (p != std::string::npos) {
            std::string sigStr = ParseStringAt(actorBlock, p, "");
            if (!sigStr.empty()) {
                try { obj.light.bakeSignature = std::stoull(sigStr); } catch (...) {}
            }
        }
        p = actorBlock.find("\"lightBakedPos\""); if (p != std::string::npos) obj.light.bakedPosition = ParseVec3(actorBlock, p);
        p = actorBlock.find("\"lightBakedRot\""); if (p != std::string::npos) obj.light.bakedRotation = ParseVec3(actorBlock, p);
        p = actorBlock.find("\"lightBakedIntensity\""); if (p != std::string::npos) obj.light.bakedIntensity = ParseFloatAt(actorBlock, p, 2.0f);
        p = actorBlock.find("\"lightBakedColor\""); if (p != std::string::npos) obj.light.bakedColor = ParseVec3(actorBlock, p);
        p = actorBlock.find("\"lightBakedRange\""); if (p != std::string::npos) obj.light.bakedRange = ParseFloatAt(actorBlock, p, 10.0f);
        p = actorBlock.find("\"lightBakedInnerCone\""); if (p != std::string::npos) obj.light.bakedInnerCone = ParseFloatAt(actorBlock, p, 20.0f);
        p = actorBlock.find("\"lightBakedOuterCone\""); if (p != std::string::npos) obj.light.bakedOuterCone = ParseFloatAt(actorBlock, p, 45.0f);
        p = actorBlock.find("\"lightBakedShadowBias\""); if (p != std::string::npos) obj.light.bakedShadowBias = ParseFloatAt(actorBlock, p, 0.0012f);

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

        // Parse 3D User Interface Properties
        p = actorBlock.find("\"isUserInterface\""); if (p != std::string::npos) obj.isUserInterface = ParseBoolAt(actorBlock, p, false);
        if (obj.type == PrimitiveType::UserInterface) obj.isUserInterface = true;
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

        // Parse Physics Modifier
        p = actorBlock.find("\"hasPhysics\"");
        if (p != std::string::npos) {
            obj.hasPhysics = ParseBoolAt(actorBlock, p, false);
            if (obj.hasPhysics) {
                p = actorBlock.find("\"physicsEnabled\""); if (p != std::string::npos) obj.physics.enabled = ParseBoolAt(actorBlock, p, true);
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
                            if (assetPath.ends_with(".ebehaviour")) {
                                VisualBehaviuors::RegisterVisualBehaviourAsset(assetPath);
                            } else if (assetPath.ends_with(".cpp")) {
                                BehaviourRegistry::Get().RegisterScriptFile(className, assetPath);
                            }
                        }
                        auto bInst = BehaviourRegistry::Get().Create(className);
                        if (bInst) {
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
                ImportedModel model;
                try {
                    model = MeshImporter::Load(obj.meshFilePath);
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
        // Reconstruct childIds on all objects from parentId
        for (auto& o : scene.objects) {
            o.childIds.clear();
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

            active = true;
            completed = false;
            hasError = false;
            bgParseDone = false;
            envParsed = false;
            completionTimer = 0.8f;
            stage = ProgressiveLoadStage::Skybox;
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
                std::ifstream file(levelPath);
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

                auto fastMeshes = SceneSerializer::LoadFastMeshMap(binaryPathCopy);
                if (session != currentSessionId.load() || cancelRequested.load()) return;

                auto blocks = SceneSerializer::ExtractActorBlocks(fileContent);
                if (session != currentSessionId.load() || cancelRequested.load()) return;

                size_t total = blocks.size();
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    stageName = "Meshes";
                    currentDetail = "Loading " + std::to_string(total) + " actors...";
                    progress = 0.25f;
                }

                for (size_t i = 0; i < blocks.size(); ++i) {
                    if (session != currentSessionId.load() || cancelRequested.load()) return;

                    GameObject obj = SceneSerializer::ParseSingleActor(blocks[i], fastMeshes);

                    {
                        std::lock_guard<std::mutex> lock(stateMutex);
                        currentDetail = "Loading mesh: " + obj.name + " (" + std::to_string(i + 1) + "/" + std::to_string(total) + ")";
                        subDetail = obj.isImportedMesh ? ("Model: " + std::filesystem::path(obj.meshFilePath).filename().string()) : (std::string("Type: ") + GetPrimitiveTypeName(obj.type));
                        progress = 0.25f + 0.65f * ((float)(i + 1) / (float)(total > 0 ? total : 1));
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
                if (!scene.objects.empty()) {
                    scene.selectedId = scene.objects.front().id;
                }

                auto endTime = std::chrono::high_resolution_clock::now();
                float elapsedSec = std::chrono::duration<float>(endTime - startTime).count();

                stage = ProgressiveLoadStage::Completed;
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
