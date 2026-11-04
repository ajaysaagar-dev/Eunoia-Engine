#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <windows.h>
#include <dwmapi.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <filesystem>

#include <EngineAssets/Geometry.h>
#include <EngineScene/GameObject.h>
#include <EngineScene/Level.h>
#include <EngineScene/LevelSerializer.h>
#include <EngineRenderer/Camera.h>
#include <Editor/EngineUI.h>
#include <Editor/EditorGizmoSystem.h>
#include <EngineAssets/TextureManager.h>
#include <EngineAssets/DirectXTex.h>
#include <EngineAssets/DirectXTexD3D12.h>
#include <EngineAssets/AssetSystem.h>
#include <EngineAssets/MeshImporter.h>
#include <EnginePlatform/InputSystem.h>
#include <EngineCore/EngineLogger.h>
#include <EngineScene/ScreenPrint.h>
#include <Editor/StartupSplash.h>
#include <Water/WaterSystem.h>
#include <Graphics/GraphicsSystem.h>
#include <EngineRenderer/TextureStreamingSystem.h>
#include <UISystem/UISystem.h>
// Standalone CustomUI removed - purely Dear ImGui for maximum performance
#include <sstream>

namespace Eunoia {
class ViewportOnlyRenderer {
public:
    static ViewportOnlyRenderer& Get() {
        static ViewportOnlyRenderer s_instance;
        return s_instance;
    }
    void BeginFrame() {}
    void UpdateFrustum(const glm::mat4&) {}
};

class MeshClusterCullingSystem {
public:
    static MeshClusterCullingSystem& Get() {
        static MeshClusterCullingSystem s_instance;
        return s_instance;
    }
    bool IsInitialized() const { return false; }
    void Initialize(ID3D12Device* = nullptr, ID3D12CommandQueue* = nullptr, UINT = 0, UINT = 0) {}
    void UpdateCamera(const glm::mat4&, const glm::mat4&, const glm::vec3&, const glm::vec4*, float, float) {}
    void ExecuteCullingPass(ID3D12GraphicsCommandList*, Scene&, ID3D12Resource*, D3D12_RESOURCE_STATES) {}
    bool RenderClusteredBatch(ID3D12GraphicsCommandList*, const Scene::RenderBatch&) { return false; }
    void FetchStatistics() {}
    void Shutdown() {}
    void Resize(UINT, UINT) {}
};
}

extern "C" {
    unsigned char *stbi_load(char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);
    void stbi_image_free(void *retval_from_stbi_load);
}

#include "imgui.h"
#include "ImGuizmo.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_dx12.h"

// Configuration matching UI_Ref.svg blueprint
const int WIDTH = 1728;
const int HEIGHT = 1117;
const UINT FRAME_COUNT = 2;

// Real-Time Shadow Mapping Configuration
const UINT SHADOW_MAP_WIDTH = 2048;
const UINT SHADOW_MAP_HEIGHT = 2048;
const UINT POINT_SHADOW_MAP_SIZE = 512;
const UINT MAX_SHADOW_POINT_LIGHTS = 4;
const UINT MAX_SHADOW_SPOT_LIGHTS = 4;

// Real-Time Lights Configuration (Directional, Point, Spot, Area)
const int MAX_POINT_LIGHTS = 64;

static UINT g_currentDirShadowRes = 2048;
static UINT g_currentPointShadowRes = 512;
static UINT g_currentSpotShadowRes = 1024;
static UINT g_activePointLightResolutions[MAX_SHADOW_POINT_LIGHTS] = { 512, 512, 512, 512 };
static UINT g_activeSpotLightResolutions[MAX_SHADOW_SPOT_LIGHTS] = { 1024, 1024, 1024, 1024 };
static glm::mat4 g_currentSpotLightSpaceMatrices[MAX_SHADOW_SPOT_LIGHTS] = {};

// HLSL Constant Buffer struct (must be 256-byte aligned in D3D12)
struct alignas(256) SceneConstantBuffer
{
	glm::mat4 mvp;
	glm::mat4 lightSpaceMatrix;
	glm::vec3 cameraPos;
	float ambientIntensity;
	glm::vec3 lightDir;
	float shadowBias;
	glm::vec3 lightColor;
	float shadowStrength;
	float pcfRadius;
	float enableShadows;
	float shadowMapSize;
	float numPointLights;
	glm::vec4 ambientColor;
	glm::vec4 pointLightPosRange[MAX_POINT_LIGHTS];
	glm::vec4 pointLightColorIntensity[MAX_POINT_LIGHTS];
	glm::vec4 pointLightDirType[MAX_POINT_LIGHTS];
	glm::vec4 pointLightSpotAreaParams[MAX_POINT_LIGHTS];
	glm::vec4 pointLightShadowParams[MAX_POINT_LIGHTS];
	glm::mat4 spotLightSpaceMatrices[MAX_SHADOW_SPOT_LIGHTS];
	float enablePostProcessing;
	float postExposureEV;
	float postExposureMultiplier;
	float postTonemapper;
	float postSaturation;
	float postContrast;
	float postGamma;
	float padPost;
	glm::vec4 postColorFilter;
	float maxShadowDistance;
	float enableAO;
	float aoPower;
	float aoRange;
	// Volumetric Fog (Wicked Engine fogHF)
	float enableFog;
	float fogStart;
	float fogDensity;
	float fogHeightStart;
	float fogHeightEnd;
	float shadowSampleCount;
	float shadowPCSS;
	float enableScreenSpaceShadows;
	glm::vec4 fogColor;
	// WickedEngine Shadow & Cascade Parameters
	float screenSpaceShadowRange;
	float screenSpaceShadowSamples;
	float enableCapsuleShadows;
	float capsuleShadowAngle;
	float capsuleShadowFade;
	float cascadeCount;
	float cascadeBlending;
	float visualizeCascades;
	glm::vec4 cascadeSplits;
};

struct alignas(256) ShadowConstantBuffer
{
	glm::mat4 lightSpaceMatrix;
};

// Volumetric Fog & God Rays Constant Buffer (Wicked Engine Technique)
struct GodRayLightData
{
	glm::vec4 posRange;          // xyz: pos, w: range
	glm::vec4 colorIntensity;    // xyz: color, w: intensity
	glm::vec4 dirType;           // xyz: dir, w: type (1=point, 2=spot, 3=area)
	glm::vec4 spotParams;        // x: cosInner, y: cosOuter, z: attenuation, w: rangeSmoothing
	glm::vec4 volumetricParams;  // x: volumetric ? 1 : 0, y: volumetric_boost, z: scatteringG, w: volumetricIntensity
	glm::vec4 shadowParams;      // x: maxRayDistance, y: shadowSlot (-1 if none), z: shadowResolution, w: shadowBias
	glm::mat4 shadowMatrix;      // spot light-space matrix
};
static_assert(sizeof(GodRayLightData) == 160, "GodRayLightData must be 160 bytes");

struct alignas(256) GodRayConstants
{
	glm::mat4 invViewProj;
	glm::mat4 dirLightSpaceMatrix;
	glm::vec3 cameraPos;
	float pad0;
	glm::vec3 dirLightDir;
	float dirLightIntensity;
	glm::vec3 dirLightColor;
	float pad1;
	glm::vec4 dirVolumetricParams; // x: enabled(1/0), y: boost, z: scatteringG, w: intensity
	glm::vec4 dirExtraParams;      // x: sampleSteps, y: maxRayDistance, z: shadowBias, w: shadowMapSize
	float fogDensity;
	float fogStart;
	float fogHeightStart;
	float fogHeightEnd;
	glm::vec4 fogColor;
	glm::vec4 screenSize;          // width, height, 1/width, 1/height
	float numLights;               // number of active point/spot volumetric lights
	float maxRayDistance;          // overall max trace distance
	float globalSampleSteps;       // default / max raymarch steps
	float spotShadowAllocRes;      // allocated spot shadow resolution (e.g. 1024)
	GodRayLightData lights[16];
};
static_assert(sizeof(GodRayConstants) <= 4096, "GodRayConstants must fit within 4096 bytes");

struct MaterialShaderConstants
{
	float baseColor[3];
	float metallic;
	float roughness;
	float normalStrength;
	float specular;
	float emissiveIntensity;
	float emissiveColor[3];
	float isUnlit;
	float hasAlbedoTex;
	float hasNormalTex;
	float hasRoughTex;
	float hasAoTex;
	float receiveShadows;
	float uvScale[2];
	float pad0;
	float blendMode;
	float opacity;
	float opacityMaskClipValue;
	float hasOpacityTex;
	float normalMapYFlip;
	float roughnessChannel;
	float metallicChannel;
	float aoChannel;
	float materialDebugMode;
	float hasMetalTex;
	float hasEmissionTex;
	float pad3;
};
static_assert(sizeof(MaterialShaderConstants) == 128, "MaterialShaderConstants must be 128 bytes (32 floats)");

struct SkyboxShaderConstants
{
	glm::mat4 invViewProj;
	glm::vec3 cameraPos;
	float exposure;
	glm::vec3 tint;
	float rotation;
	float hasHdrTex;
	float ambientContribution;
	float pad0;
	float pad1;
};
static_assert(sizeof(SkyboxShaderConstants) == 112, "SkyboxShaderConstants must be 112 bytes (28 floats)");

// Window & GLFW
static GLFWwindow* g_window = nullptr;
static int g_currentWidth = WIDTH;
static int g_currentHeight = HEIGHT;

// DirectX 12 Core Objects
static IDXGIFactory4* g_dxgiFactory = nullptr;
static ID3D12Device* g_d3dDevice = nullptr;
static ID3D12CommandQueue* g_commandQueue = nullptr;
static IDXGISwapChain3* g_swapChain = nullptr;
static ID3D12DescriptorHeap* g_rtvDescHeap = nullptr;
static ID3D12DescriptorHeap* g_dsvDescHeap = nullptr;
static ID3D12DescriptorHeap* g_srvDescHeap = nullptr;
static UINT g_rtvDescriptorSize = 0;
static UINT g_srvDescriptorSize = 0;

static ID3D12Resource* g_renderTargets[FRAME_COUNT] = {};

// Offscreen Scene Render Targets & Real-Time Anti-Aliasing (dev.md)
static ID3D12Resource* g_sceneRenderTarget = nullptr;
static ID3D12Resource* g_sceneHistoryRenderTarget = nullptr;
static D3D12_CPU_DESCRIPTOR_HANDLE g_sceneRtvHandle = {};
static D3D12_CPU_DESCRIPTOR_HANDLE g_sceneHistoryRtvHandle = {};
static D3D12_CPU_DESCRIPTOR_HANDLE g_sceneSrvCpuHandle = {};
static D3D12_GPU_DESCRIPTOR_HANDLE g_sceneSrvGpuHandle = {};
static D3D12_CPU_DESCRIPTOR_HANDLE g_sceneHistorySrvCpuHandle = {};
static D3D12_GPU_DESCRIPTOR_HANDLE g_sceneHistorySrvGpuHandle = {};
static ID3D12RootSignature* g_antiAliasingRootSignature = nullptr;
static ID3D12PipelineState* g_antiAliasingPipelineState = nullptr;

// Volumetric Fog & God Rays (Wicked Engine Technique)
static ID3D12Resource* g_godRayConstantBuffer = nullptr;
static void* g_pGodRayConstantMapped = nullptr;
static ID3D12RootSignature* g_godRayRootSignature = nullptr;
static ID3D12PipelineState* g_godRayPipelineState = nullptr;
static D3D12_CPU_DESCRIPTOR_HANDLE g_godRayTableCpuHandle = {};
static D3D12_GPU_DESCRIPTOR_HANDLE g_godRayTableGpuHandle = {};
static glm::mat4 g_currentDirLightSpaceMatrix = glm::mat4(1.0f);
static glm::vec3 g_currentDirLightDir = glm::vec3(0.0f, -1.0f, 0.0f);
int createGodRayPipeline();

// Wicked Engine Ambient Occlusion Pipeline (SSAO, HBAO, MSAO, RTAO)
static ID3D12Resource* g_aoRenderTarget = nullptr;
static ID3D12Resource* g_aoBlurRenderTarget = nullptr;
static D3D12_CPU_DESCRIPTOR_HANDLE g_aoRtvHandle = {};
static D3D12_CPU_DESCRIPTOR_HANDLE g_aoBlurRtvHandle = {};
static D3D12_CPU_DESCRIPTOR_HANDLE g_aoTableCpuHandle = {};
static D3D12_GPU_DESCRIPTOR_HANDLE g_aoTableGpuHandle = {};
static ID3D12Resource* g_aoConstantBuffer = nullptr;
static void* g_pAoConstantMapped = nullptr;
static ID3D12RootSignature* g_aoRootSignature = nullptr;
static ID3D12PipelineState* g_aoGeneratePipelineState = nullptr;
static ID3D12PipelineState* g_aoBlurPipelineState = nullptr;
static ID3D12PipelineState* g_aoCompositePipelineState = nullptr;
int createAoPipeline();
int createAoRenderTargets(int width, int height);
static void UpdateAoDepthSrv();
static uint64_t g_globalFrameCounter = 0;
static glm::vec2 g_currentJitter = { 0.0f, 0.0f };
static ID3D12CommandAllocator* g_commandAllocators[FRAME_COUNT] = {};
static ID3D12GraphicsCommandList* g_commandLists[FRAME_COUNT] = {};
static ID3D12GraphicsCommandList* g_commandList = nullptr;

static ID3D12Resource* g_depthStencilBuffer = nullptr;

// Synchronization
static UINT g_frameIndex = 0;
static HANDLE g_fenceEvent = nullptr;
static ID3D12Fence* g_fence = nullptr;
static UINT64 g_fenceValues[FRAME_COUNT] = {};
static UINT64 g_globalFenceValue = 0;

// Pipeline & Shaders
static ID3D12RootSignature* g_rootSignature = nullptr;
static ID3D12PipelineState* g_pipelineState = nullptr;
static ID3D12PipelineState* g_pipelineStateTwoSided = nullptr;
static ID3D12PipelineState* g_depthPrepassPipelineState = nullptr;
static ID3D12PipelineState* g_depthPrepassTwoSidedPipelineState = nullptr;

// Dynamic Scene Buffers
const size_t MAX_SCENE_VERTICES = 1000000;
const size_t MAX_SCENE_INDICES  = 3000000;

static ID3D12Resource* g_vertexBuffer = nullptr;
static ID3D12Resource* g_indexBuffer = nullptr;
static ID3D12Resource* g_constantBuffer = nullptr;

static D3D12_VERTEX_BUFFER_VIEW g_vertexBufferView = {};
static D3D12_INDEX_BUFFER_VIEW  g_indexBufferView = {};

static void* g_pVertexMapped = nullptr;
static void* g_pIndexMapped = nullptr;
static void* g_pConstantMapped = nullptr;

// Material Editor Live 3D PBR Preview Buffers
const size_t MAX_MAT_PREVIEW_VERTICES = 10000;
const size_t MAX_MAT_PREVIEW_INDICES  = 40000;
static ID3D12Resource* g_matPreviewVertexBuffer = nullptr;
static ID3D12Resource* g_matPreviewIndexBuffer = nullptr;
static ID3D12Resource* g_matPreviewConstantBuffer = nullptr;
static D3D12_VERTEX_BUFFER_VIEW g_matPreviewVertexBufferView = {};
static D3D12_INDEX_BUFFER_VIEW  g_matPreviewIndexBufferView = {};
static void* g_pMatPreviewVertexMapped = nullptr;
static void* g_pMatPreviewIndexMapped = nullptr;
static void* g_pMatPreviewConstantMapped = nullptr;
static int g_currentMatPreviewMeshType = -1;
static uint32_t g_currentMatPreviewIndexCount = 0;
static ID3D12Resource* g_matPreviewRenderTarget = nullptr;
static ID3D12Resource* g_matPreviewDepthBuffer = nullptr;
static D3D12_CPU_DESCRIPTOR_HANDLE g_matPreviewRtvHandle = {};
static D3D12_CPU_DESCRIPTOR_HANDLE g_matPreviewDsvHandle = {};
static D3D12_CPU_DESCRIPTOR_HANDLE g_matPreviewSrvCpuHandle = {};
static D3D12_GPU_DESCRIPTOR_HANDLE g_matPreviewSrvGpuHandle = {};

static uint32_t g_currentVertexCount = 0;
static uint32_t g_currentIndexCount = 0;

// ImGui & Engine SRV allocation tracking
static const UINT SRV_HEAP_CAPACITY = 8192; // Must match srvHeapDesc.NumDescriptors in initD3D12
static UINT g_srvDescriptorAllocIndex = 0;
static bool g_deviceLost = false;

// Game Engine State
static Scene g_scene;
static OrbitCamera g_camera;
static EngineUI g_engineUI;
static std::vector<Scene::RenderBatch> g_sceneBatches;
static size_t g_numPureSceneBatches = 0;

static std::vector<ID3D12Resource*> g_allocatedTextures;
static std::unordered_map<std::string, DX12GpuTexture> g_gpuTextureMap;
struct MaterialTableEntry {
	D3D12_CPU_DESCRIPTOR_HANDLE baseCpu = {};
	D3D12_GPU_DESCRIPTOR_HANDLE baseGpu = {};
	std::string albedo;
	std::string normal;
	std::string rough;
	std::string metal;
	std::string ao;
	std::string opacity;
	std::string emission;
};

struct SkyboxTableEntry {
	D3D12_CPU_DESCRIPTOR_HANDLE baseCpu = {};
	D3D12_GPU_DESCRIPTOR_HANDLE baseGpu = {};
	std::string texturePath;
};

static std::unordered_map<std::string, MaterialTableEntry> g_materialDescriptorTables;

static DX12GpuTexture g_fallbackWhite;
static DX12GpuTexture g_fallbackNormal;
static DX12GpuTexture g_fallbackRoughness;
static DX12GpuTexture g_fallbackMetallic;
static DX12GpuTexture g_fallbackAO;
static DX12GpuTexture g_fallbackMissing;
static DX12GpuTexture g_fallbackBlack;  // Used as no-op lightmap when no bake is active

// Lightmap SRV (t9) â€“ filled by the light baking system or the black fallback
static D3D12_CPU_DESCRIPTOR_HANDLE g_lightmapSrvCpuHandle = {};
static D3D12_GPU_DESCRIPTOR_HANDLE g_lightmapSrvGpuHandle = {};
static bool g_lightmapSrvInHeap = false; // true once the dedicated slot has been written

// Real-Time Directional Shadow Resources
static ID3D12Resource* g_shadowDepthBuffer = nullptr;
static D3D12_CPU_DESCRIPTOR_HANDLE g_shadowDsvHandle = {};
static D3D12_CPU_DESCRIPTOR_HANDLE g_shadowSrvCpuHandle = {};
static D3D12_GPU_DESCRIPTOR_HANDLE g_shadowSrvGpuHandle = {};

// Real-Time Point Light Shadow Cubemap Array Resources
static ID3D12Resource* g_pointShadowDepthBuffer = nullptr;
static D3D12_CPU_DESCRIPTOR_HANDLE g_pointShadowDsvHandles[MAX_SHADOW_POINT_LIGHTS * 6] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE g_pointShadowSrvCpuHandle = {};
static D3D12_GPU_DESCRIPTOR_HANDLE g_pointShadowSrvGpuHandle = {};
static int g_activeShadowPointLights = 0;

// Real-Time Spot / Area Light Shadow 2D Array Resources
static ID3D12Resource* g_spotShadowDepthBuffer = nullptr;
static D3D12_CPU_DESCRIPTOR_HANDLE g_spotShadowDsvHandles[MAX_SHADOW_SPOT_LIGHTS] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE g_spotShadowSrvCpuHandle = {};
static D3D12_GPU_DESCRIPTOR_HANDLE g_spotShadowSrvGpuHandle = {};
static int g_activeShadowSpotLights = 0;

static ID3D12RootSignature* g_shadowRootSignature = nullptr;
static ID3D12PipelineState* g_shadowPipelineState = nullptr;
static ID3D12PipelineState* g_shadowTwoSidedPipelineState = nullptr;

static ID3D12RootSignature* g_skyboxRootSignature = nullptr;
static ID3D12PipelineState* g_skyboxPipelineState = nullptr;
static std::unordered_map<std::string, SkyboxTableEntry> g_skyboxDescriptorTables;

static ID3D12Resource* g_shadowConstantBuffer = nullptr;
static void* g_pShadowConstantMapped = nullptr;

static UINT g_dsvDescriptorSize = 0;

// Dedicated Upload Fence & Allocator (avoids frame fence collision)
static ID3D12CommandAllocator* g_uploadCmdAlloc = nullptr;
static ID3D12GraphicsCommandList* g_uploadCmdList = nullptr;
static ID3D12Fence* g_uploadFence = nullptr;
static HANDLE g_uploadFenceEvent = nullptr;
static UINT64 g_uploadFenceValue = 0;

// Mouse tracking
static bool s_isLeftMouseDown = false;
static bool s_isRightMouseDown = false;
static bool s_isMiddleMouseDown = false;
static bool s_firstMouseAfterCapture = true;
static double s_lastMouseX = 0.0, s_lastMouseY = 0.0;
static glm::vec3 s_camVelocity(0.0f);
bool g_pendingPickClick = false;
#define s_pendingPickClick g_pendingPickClick

// Forward declarations for real-time graphics pipelines
int createAntiAliasingPipeline();
int createSceneRenderTargets(int width, int height);

// WickedEngine-matching HDR Scene Render Target Format (wi::renderer::format_rendertarget_main)
constexpr DXGI_FORMAT SCENE_RENDER_TARGET_FORMAT = DXGI_FORMAT_R11G11B10_FLOAT;

// WickedEngine Variable Refresh Rate (VRR) / Tearing Support
static bool g_tearingSupported = false;

// D3D12 Helper Functions
inline D3D12_RESOURCE_BARRIER CreateTransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	return barrier;
}

inline D3D12_HEAP_PROPERTIES CreateHeapProperties(D3D12_HEAP_TYPE type)
{
	D3D12_HEAP_PROPERTIES props = {};
	props.Type = type;
	props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	props.CreationNodeMask = 1;
	props.VisibleNodeMask = 1;
	return props;
}

inline D3D12_RESOURCE_DESC CreateBufferResourceDesc(UINT64 size)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Alignment = 0;
	desc.Width = size;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	return desc;
}

// Returns false (and logs once) if the heap is full instead of writing OOB.
bool TryAllocSrvDescriptors(UINT count, UINT& outBaseIndex)
{
	if (g_srvDescriptorAllocIndex + count > SRV_HEAP_CAPACITY)
	{
		static bool warned = false;
		if (!warned)
		{
			std::cerr << "[SRV HEAP] Exhausted (" << SRV_HEAP_CAPACITY
			          << " descriptors). Further textures/materials will use fallbacks.\n";
			g_engineUI.AddLog("LogRecovery",
				"SRV descriptor heap is full â€” new textures/materials will show as fallback until you restart or free assets.", 2);
			warned = true;
		}
		return false;
	}
	outBaseIndex = g_srvDescriptorAllocIndex;
	g_srvDescriptorAllocIndex += count;
	return true;
}

// ImGui SRV Descriptor Allocator Callbacks
static void ImGui_SrvAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu)
{
	UINT idx = 0;
	if (!TryAllocSrvDescriptors(1, idx))
	{
		// Safe fallback: return heap slot 0
		*out_cpu = info->SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		*out_gpu = info->SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
		return;
	}
	D3D12_CPU_DESCRIPTOR_HANDLE cpu = info->SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE gpu = info->SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	cpu.ptr += (SIZE_T)idx * g_srvDescriptorSize;
	gpu.ptr += (UINT64)idx * g_srvDescriptorSize;
	*out_cpu = cpu;
	*out_gpu = gpu;
}

static void ImGui_SrvFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
{
	// Linear allocator - no-op
}

static std::vector<std::string> g_recordedFrameOps;
static std::vector<std::string> g_frameOpHistory[FRAME_COUNT];

void RecordGpuBreadcrumbOp(const char* desc)
{
	if (desc)
	{
		g_recordedFrameOps.push_back(desc);
	}
}

static const char* GetDredOpName(D3D12_AUTO_BREADCRUMB_OP op)
{
	switch (op)
	{
	case D3D12_AUTO_BREADCRUMB_OP_SETMARKER: return "SetMarker";
	case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT: return "BeginEvent";
	case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT: return "EndEvent";
	case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED: return "DrawInstanced";
	case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED: return "DrawIndexedInstanced";
	case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT: return "ExecuteIndirect";
	case D3D12_AUTO_BREADCRUMB_OP_DISPATCH: return "Dispatch";
	case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION: return "CopyBufferRegion";
	case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION: return "CopyTextureRegion";
	case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE: return "CopyResource";
	case D3D12_AUTO_BREADCRUMB_OP_COPYTILES: return "CopyTiles";
	case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE: return "ResolveSubresource";
	case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW: return "ClearRenderTargetView";
	case D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW: return "ClearUnorderedAccessView";
	case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW: return "ClearDepthStencilView";
	case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER: return "ResourceBarrier";
	case D3D12_AUTO_BREADCRUMB_OP_EXECUTEBUNDLE: return "ExecuteBundle";
	case D3D12_AUTO_BREADCRUMB_OP_PRESENT: return "Present";
	case D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA: return "ResolveQueryData";
	case D3D12_AUTO_BREADCRUMB_OP_BEGINSUBMISSION: return "BeginSubmission";
	case D3D12_AUTO_BREADCRUMB_OP_ENDSUBMISSION: return "EndSubmission";
	case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME: return "DecodeFrame";
	case D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES: return "ProcessFrames";
	case D3D12_AUTO_BREADCRUMB_OP_ATOMICCOPYBUFFERUINT: return "AtomicCopyBufferUint";
	case D3D12_AUTO_BREADCRUMB_OP_ATOMICCOPYBUFFERUINT64: return "AtomicCopyBufferUint64";
	case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCEREGION: return "ResolveSubresourceRegion";
	case D3D12_AUTO_BREADCRUMB_OP_WRITEBUFFERIMMEDIATE: return "WriteBufferImmediate";
	default: return "UnknownOp";
	}
}

static void DumpDREDInformation()
{
	ID3D12DeviceRemovedExtendedData1* dred = nullptr;
	std::stringstream logStream;
	if (g_d3dDevice && SUCCEEDED(g_d3dDevice->QueryInterface(IID_PPV_ARGS(&dred))))
	{
		D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs = {};
		if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs)))
		{
			std::cerr << "[DRED] AutoBreadcrumbs output captured:\n";
			logStream << "[DRED] AutoBreadcrumbs output captured:\n";
			const D3D12_AUTO_BREADCRUMB_NODE1* node = breadcrumbs.pHeadAutoBreadcrumbNode;
			int nodeIdx = 0;
			while (node)
			{
				std::wcerr << L"[DRED] Node " << nodeIdx << L" - Command List: "
				           << (node->pCommandListDebugNameW ? node->pCommandListDebugNameW : L"(unnamed)")
				           << L", Command Queue: "
				           << (node->pCommandQueueDebugNameW ? node->pCommandQueueDebugNameW : L"(unnamed)")
				           << L"\n";
				logStream << "[DRED] Node " << nodeIdx << " - Command List: "
				          << (node->pCommandListDebugNameA ? node->pCommandListDebugNameA : "(unnamed)")
				          << "\n";
				if (node->pLastBreadcrumbValue && node->BreadcrumbCount > 0)
				{
					UINT completed = *node->pLastBreadcrumbValue;
					std::cerr << "[DRED]   Completed ops: " << completed
					          << " / " << node->BreadcrumbCount << " total ops\n";
					logStream << "[DRED]   Completed ops: " << completed << " / " << node->BreadcrumbCount << " total ops\n";
					if (node->pCommandHistory)
					{
						UINT startOp = completed > 4 ? completed - 4 : 0;
						for (UINT opIdx = startOp; opIdx < node->BreadcrumbCount && opIdx <= completed + 2; ++opIdx)
						{
							const char* status = (opIdx < completed) ? "[COMPLETED]" : ((opIdx == completed) ? "--> [CURRENT/HUNG]" : "[PENDING]");
							std::string desc = "(untracked)";
							if (opIdx < g_recordedFrameOps.size())
							{
								desc = g_recordedFrameOps[opIdx];
							}
							else
							{
								for (UINT f = 0; f < FRAME_COUNT; ++f)
								{
									if (opIdx < g_frameOpHistory[f].size())
									{
										desc = g_frameOpHistory[f][opIdx];
										break;
									}
								}
							}
							std::cerr << "[DRED]     Op " << opIdx << " " << status << ": "
							          << GetDredOpName(node->pCommandHistory[opIdx])
							          << " -- " << desc << "\n";
							logStream << "[DRED]     Op " << opIdx << " " << status << ": "
							          << GetDredOpName(node->pCommandHistory[opIdx])
							          << " -- " << desc << "\n";
						}
					}
				}
				node = node->pNext;
				nodeIdx++;
			}
		}

		D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault = {};
		if (SUCCEEDED(dred->GetPageFaultAllocationOutput1(&pageFault)) &&
			pageFault.PageFaultVA != 0)
		{
			std::cerr << "[DRED] GPU page fault at VA 0x" << std::hex << pageFault.PageFaultVA << std::dec << "\n";
			logStream << "[DRED] GPU page fault at VA 0x" << std::hex << pageFault.PageFaultVA << std::dec << "\n";

			std::cerr << "[DRED] Known Engine Resource Memory Map:\n";
			logStream << "[DRED] Known Engine Resource Memory Map:\n";
			if (g_vertexBuffer) {
				UINT64 va = g_vertexBuffer->GetGPUVirtualAddress();
				std::cerr << "  g_vertexBuffer:         [0x" << std::hex << va << " - 0x" << (va + MAX_SCENE_VERTICES * sizeof(Vertex)) << std::dec << "]\n";
				logStream << "  g_vertexBuffer:         [0x" << std::hex << va << " - 0x" << (va + MAX_SCENE_VERTICES * sizeof(Vertex)) << std::dec << "]\n";
			}
			if (g_indexBuffer) {
				UINT64 va = g_indexBuffer->GetGPUVirtualAddress();
				std::cerr << "  g_indexBuffer:          [0x" << std::hex << va << " - 0x" << (va + MAX_SCENE_INDICES * sizeof(uint32_t)) << std::dec << "]\n";
				logStream << "  g_indexBuffer:          [0x" << std::hex << va << " - 0x" << (va + MAX_SCENE_INDICES * sizeof(uint32_t)) << std::dec << "]\n";
			}
			if (g_constantBuffer) {
				UINT64 va = g_constantBuffer->GetGPUVirtualAddress();
				std::cerr << "  g_constantBuffer:       [0x" << std::hex << va << " - 0x" << (va + sizeof(SceneConstantBuffer)) << std::dec << "]\n";
				logStream << "  g_constantBuffer:       [0x" << std::hex << va << " - 0x" << (va + sizeof(SceneConstantBuffer)) << std::dec << "]\n";
			}
			if (g_shadowConstantBuffer) {
				UINT64 va = g_shadowConstantBuffer->GetGPUVirtualAddress();
				std::cerr << "  g_shadowConstantBuffer: [0x" << std::hex << va << " - 0x" << (va + sizeof(ShadowConstantBuffer)) << std::dec << "]\n";
				logStream << "  g_shadowConstantBuffer: [0x" << std::hex << va << " - 0x" << (va + sizeof(ShadowConstantBuffer)) << std::dec << "]\n";
			}
			if (g_srvDescHeap) {
				UINT64 va = g_srvDescHeap->GetGPUDescriptorHandleForHeapStart().ptr;
				std::cerr << "  g_srvDescHeap (GPU):    [0x" << std::hex << va << " - 0x" << (va + (UINT64)SRV_HEAP_CAPACITY * g_srvDescriptorSize) << std::dec << "]\n";
				logStream << "  g_srvDescHeap (GPU):    [0x" << std::hex << va << " - 0x" << (va + (UINT64)SRV_HEAP_CAPACITY * g_srvDescriptorSize) << std::dec << "]\n";
			}
			for (const auto& kv : g_gpuTextureMap) {
				std::cerr << "  Texture '" << kv.first << "': SRV_GPU=0x" << std::hex << kv.second.gpuHandle.ptr << std::dec << "\n";
				logStream << "  Texture '" << kv.first << "': SRV_GPU=0x" << std::hex << kv.second.gpuHandle.ptr << std::dec << "\n";
			}

			const D3D12_DRED_ALLOCATION_NODE1* allocNode = pageFault.pHeadExistingAllocationNode;
			int allocCount = 0;
			while (allocNode)
			{
				allocCount++;
				std::cerr << "[DRED] Existing Allocation #" << allocCount << " (type: 0x" << std::hex << (UINT)allocNode->AllocationType << std::dec << "): ";
				logStream << "[DRED] Existing Allocation #" << allocCount << " (type: 0x" << std::hex << (UINT)allocNode->AllocationType << std::dec << "): ";
				if (allocNode->ObjectNameA) {
					std::cerr << allocNode->ObjectNameA;
					logStream << allocNode->ObjectNameA;
				} else if (allocNode->ObjectNameW) {
					std::wcerr << allocNode->ObjectNameW;
					logStream << "(wide name)";
				} else {
					std::cerr << "(unnamed)";
					logStream << "(unnamed)";
				}
				std::cerr << " (pObject=" << allocNode->pObject << ")\n";
				logStream << " (pObject=" << allocNode->pObject << ")\n";
				allocNode = allocNode->pNext;
			}
			if (allocCount == 0) {
				std::cerr << "[DRED]   (No matching existing allocation found by driver)\n";
				logStream << "[DRED]   (No matching existing allocation found by driver)\n";
			}

			const D3D12_DRED_ALLOCATION_NODE1* freedNode = pageFault.pHeadRecentFreedAllocationNode;
			int freedCount = 0;
			while (freedNode)
			{
				freedCount++;
				std::cerr << "[DRED] Recent Freed Allocation #" << freedCount << " (type: 0x" << std::hex << (UINT)freedNode->AllocationType << std::dec << "): ";
				logStream << "[DRED] Recent Freed Allocation #" << freedCount << " (type: 0x" << std::hex << (UINT)freedNode->AllocationType << std::dec << "): ";
				if (freedNode->ObjectNameA) {
					std::cerr << freedNode->ObjectNameA;
					logStream << freedNode->ObjectNameA;
				} else if (freedNode->ObjectNameW) {
					std::wcerr << freedNode->ObjectNameW;
					logStream << "(wide name)";
				} else {
					std::cerr << "(unnamed)";
					logStream << "(unnamed)";
				}
				std::cerr << " (pObject=" << freedNode->pObject << ")\n";
				logStream << " (pObject=" << freedNode->pObject << ")\n";
				freedNode = freedNode->pNext;
			}
			if (freedCount == 0) {
				std::cerr << "[DRED]   (No matching recently freed allocation found by driver)\n";
				logStream << "[DRED]   (No matching recently freed allocation found by driver)\n";
			}
		}
		dred->Release();
	}

	EngineLogger::Get().LogError("DirectX12", 0x887A0006, "DXGI_ERROR_DEVICE_HUNG / GPU Page Fault", logStream.str());
}

static std::vector<ID3D12Resource*> g_deferredReleases;

void SafeDeferredRelease(ID3D12Resource*& pRes)
{
	if (pRes)
	{
		g_deferredReleases.push_back(pRes);
		pRes = nullptr;
	}
}

void FlushDeferredReleases()
{
	if (g_deferredReleases.empty()) return;
	for (auto* res : g_deferredReleases)
	{
		if (res)
		{
			res->Release();
		}
	}
	g_deferredReleases.clear();
}

bool SafeWaitForFence(ID3D12Fence* fence, UINT64 targetValue, HANDLE eventHandle, DWORD timeoutMs = 5000, const char* context = "GPU Fence")
{
	if (!fence || !eventHandle || g_deviceLost) return false;

	if (fence->GetCompletedValue() >= targetValue) return true;

	ResetEvent(eventHandle);
	fence->SetEventOnCompletion(targetValue, eventHandle);

	while (fence->GetCompletedValue() < targetValue)
	{
		DWORD waitRes = WaitForSingleObject(eventHandle, timeoutMs);

		if (waitRes == WAIT_OBJECT_0)
		{
			if (fence->GetCompletedValue() >= targetValue)
			{
				return true;
			}
		}
		else if (waitRes == WAIT_TIMEOUT)
		{
			std::cerr << "[RECOVERY] Engine detected stall at " << context << " (target fence: " << targetValue
			          << ", completed: " << fence->GetCompletedValue() << ", timeout: " << timeoutMs << "ms)!\n";

			HRESULT removedReason = g_d3dDevice ? g_d3dDevice->GetDeviceRemovedReason() : S_OK;
			if (FAILED(removedReason))
			{
				g_deviceLost = true;
				std::cerr << "[RECOVERY] D3D12 Device Removed reason: 0x" << std::hex << removedReason << std::dec << "\n";
				DumpDREDInformation();
				g_engineUI.AddLog("LogRecovery", "GPU Device Lost/Removed detected (0x" + std::to_string(removedReason) + "). Please save work if possible and restart editor.", 3);
				return false;
			}

			// Try a secondary wait of 3000ms before giving up
			DWORD waitRetry = WaitForSingleObject(eventHandle, 3000);
			if (waitRetry == WAIT_OBJECT_0 && fence->GetCompletedValue() >= targetValue)
			{
				return true;
			}

			removedReason = g_d3dDevice ? g_d3dDevice->GetDeviceRemovedReason() : S_OK;
			if (FAILED(removedReason))
			{
				g_deviceLost = true;
				std::cerr << "[RECOVERY] D3D12 Device Removed on retry: 0x" << std::hex << removedReason << std::dec << "\n";
				DumpDREDInformation();
				g_engineUI.AddLog("LogRecovery", "GPU Device Lost/Removed detected on retry.", 3);
			}
			else
			{
				g_engineUI.AddLog("LogRecovery", std::string("Engine wait timeout at ") + context + ". GPU is taking longer than expected.", 1);
			}

			return false;
		}
		else
		{
			break;
		}
	}

	return fence->GetCompletedValue() >= targetValue;
}

void WaitForGpuIdle()
{
	if (!g_commandQueue || !g_fence || g_deviceLost) return;

	const UINT64 fenceToWait = ++g_globalFenceValue;
	HRESULT hr = g_commandQueue->Signal(g_fence, fenceToWait);
	if (FAILED(hr)) return;

	if (g_fence->GetCompletedValue() < fenceToWait)
	{
		HANDLE tempEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (tempEvent)
		{
			hr = g_fence->SetEventOnCompletion(fenceToWait, tempEvent);
			if (SUCCEEDED(hr))
			{
				while (g_fence->GetCompletedValue() < fenceToWait)
				{
					DWORD res = WaitForSingleObject(tempEvent, 5000);
					if (res == WAIT_OBJECT_0)
					{
						break;
					}
					else if (res == WAIT_TIMEOUT)
					{
						HRESULT removedReason = g_d3dDevice ? g_d3dDevice->GetDeviceRemovedReason() : S_OK;
						if (FAILED(removedReason))
						{
							g_deviceLost = true;
							DumpDREDInformation();
							break;
						}
					}
					else
					{
						break;
					}
				}
			}
			CloseHandle(tempEvent);
		}
	}

	for (UINT i = 0; i < FRAME_COUNT; i++)
	{
		g_fenceValues[i] = fenceToWait;
	}
}

DX12GpuTexture UploadTextureToD3D12(const DirectX::ScratchImage& scratch, const std::string& debugName = "")
{
	DX12GpuTexture result = {};
	if (scratch.GetImageCount() == 0 || !g_d3dDevice || !g_uploadCmdAlloc || !g_uploadCmdList || !g_uploadFence) return result;

	const DirectX::TexMetadata& meta = scratch.GetMetadata();
	if (meta.width == 0 || meta.height == 0) return result;

	ID3D12Resource* tex = nullptr;
	HRESULT hr = DirectX::CreateTexture(g_d3dDevice, meta, &tex);
	if (FAILED(hr) || !tex) {
		std::cerr << "[TEXTURE UPLOAD] Failed to create D3D12 texture resource: hr=0x" << std::hex << hr << std::dec << "\n";
		return result;
	}

	if (!debugName.empty()) {
		std::wstring wname(debugName.begin(), debugName.end());
		tex->SetName(wname.c_str());
	} else {
		tex->SetName(L"Texture_DirectXTex");
	}

	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	hr = DirectX::PrepareUpload(g_d3dDevice, scratch.GetImages(), scratch.GetImageCount(), meta, subresources);
	if (FAILED(hr) || subresources.empty()) {
		tex->Release();
		return result;
	}

	UINT numSub = static_cast<UINT>(subresources.size());
	UINT64 uploadSize = DirectX::GetRequiredIntermediateSize(g_d3dDevice, tex, 0, numSub);

	D3D12_RESOURCE_DESC upDesc = CreateBufferResourceDesc(uploadSize);
	D3D12_HEAP_PROPERTIES upHeap = CreateHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
	ID3D12Resource* uploadBuf = nullptr;
	hr = g_d3dDevice->CreateCommittedResource(&upHeap, D3D12_HEAP_FLAG_NONE, &upDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf));
	if (FAILED(hr) || !uploadBuf) {
		tex->Release();
		return result;
	}
	uploadBuf->SetName(L"Texture_Upload_Staging_Mips");

	g_uploadCmdAlloc->Reset();
	g_uploadCmdList->Reset(g_uploadCmdAlloc, nullptr);

	DirectX::UpdateSubresources(g_d3dDevice, g_uploadCmdList, tex, uploadBuf, 0, 0, numSub, subresources.data());

	D3D12_RESOURCE_BARRIER b = CreateTransitionBarrier(tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	g_uploadCmdList->ResourceBarrier(1, &b);

	g_uploadCmdList->Close();
	ID3D12CommandList* lists[] = { g_uploadCmdList };
	g_commandQueue->ExecuteCommandLists(1, lists);

	g_uploadFenceValue++;
	g_commandQueue->Signal(g_uploadFence, g_uploadFenceValue);
	bool uploadOk = SafeWaitForFence(g_uploadFence, g_uploadFenceValue, g_uploadFenceEvent, 10000, "UploadTextureMips");

	if (!uploadOk) {
		std::cerr << "[TEXTURE UPLOAD] Fence wait failed for texture (" << meta.width << "x" << meta.height << ")\n";
		if (uploadBuf) uploadBuf->Release();
		if (tex) tex->Release();
		return result;
	}

	uploadBuf->Release();

	UINT descIdx = 0;
	if (!TryAllocSrvDescriptors(1, descIdx)) {
		tex->Release();
		return result;
	}
	D3D12_CPU_DESCRIPTOR_HANDLE cpu = g_srvDescHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE gpu = g_srvDescHeap->GetGPUDescriptorHandleForHeapStart();
	cpu.ptr += (SIZE_T)descIdx * g_srvDescriptorSize;
	gpu.ptr += (UINT64)descIdx * g_srvDescriptorSize;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = meta.format;

	float lodClamp = TextureManager::CalculateTextureMinLOD(
		static_cast<int>(meta.width), static_cast<int>(meta.height),
		TextureManager::Get().GetMaxTextureResolution());

	if (meta.IsCubemap()) {
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srvDesc.TextureCube.MipLevels = static_cast<UINT>(meta.mipLevels);
		srvDesc.TextureCube.MostDetailedMip = 0;
		srvDesc.TextureCube.ResourceMinLODClamp = lodClamp;
	} else {
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = static_cast<UINT>(meta.mipLevels);
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = lodClamp;
	}

	g_d3dDevice->CreateShaderResourceView(tex, &srvDesc, cpu);

	result.resource = tex;
	result.cpuHandle = cpu;
	result.gpuHandle = gpu;
	result.width = static_cast<int>(meta.width);
	result.height = static_cast<int>(meta.height);
	result.mipLevels = static_cast<UINT>(meta.mipLevels);
	result.format = meta.format;
	result.isCubemap = meta.IsCubemap();

	g_allocatedTextures.push_back(tex);
	return result;
}

DX12GpuTexture UploadTextureToD3D12(const void* pixels, int width, int height, const std::string& debugName = "", bool isNormalMap = false)
{
	DX12GpuTexture result = {};
	if (!pixels || width <= 0 || height <= 0) return result;

	// Wrap pixels and generate mip chain using DirectXTex
	DirectX::Image baseImg;
	baseImg.width = static_cast<size_t>(width);
	baseImg.height = static_cast<size_t>(height);
	baseImg.format = isNormalMap ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	baseImg.rowPitch = static_cast<size_t>(width * 4);
	baseImg.slicePitch = static_cast<size_t>(width * height * 4);
	baseImg.pixels = const_cast<uint8_t*>(static_cast<const uint8_t*>(pixels));

	uint32_t filterFlags = DirectX::TEX_FILTER_BOX;
	if (isNormalMap) filterFlags |= DirectX::TEX_FILTER_NORMAL_NORMALIZE;
	else filterFlags |= DirectX::TEX_FILTER_SRGB;

	DirectX::ScratchImage mipChain;
	if (DirectX::GenerateMipMaps(baseImg, filterFlags, 0, mipChain)) {
		return UploadTextureToD3D12(mipChain, debugName);
	}

	// Fallback single-level image if mip generation failed
	DirectX::ScratchImage single;
	if (single.InitializeFromImage(baseImg)) {
		return UploadTextureToD3D12(single, debugName);
	}
	return result;
}

void InitFallbackTextures()
{
	uint32_t whitePix = 0xFFFFFFFF;
	g_fallbackWhite = UploadTextureToD3D12(&whitePix, 1, 1, "Fallback_White");

	uint8_t normalPix[4] = { 128, 128, 255, 255 };
	g_fallbackNormal = UploadTextureToD3D12(normalPix, 1, 1, "Fallback_Normal", true);

	uint32_t roughPix = 0xFFFFFFFF;
	g_fallbackRoughness = UploadTextureToD3D12(&roughPix, 1, 1, "Fallback_Roughness");

	uint32_t metalPix = 0xFF000000;
	g_fallbackMetallic = UploadTextureToD3D12(&metalPix, 1, 1, "Fallback_Metallic");

	uint32_t aoPix = 0xFFFFFFFF;
	g_fallbackAO = UploadTextureToD3D12(&aoPix, 1, 1, "Fallback_AO");

	uint32_t blackPix = 0xFF000000;
	g_fallbackBlack = UploadTextureToD3D12(&blackPix, 1, 1, "Fallback_Black");

	// Missing texture checkerboard fallback from dev.md Section 20
	CachedTexture* checker = AssetManager::Get().GetMissingTextureFallback();
	if (checker && !checker->data.empty() && checker->width > 0 && checker->height > 0) {
		g_fallbackMissing = UploadTextureToD3D12(checker->data.data(), checker->width, checker->height, "Fallback_MissingChecker");
	} else {
		g_fallbackMissing = g_fallbackWhite;
	}
}

// Allocate a persistent SRV heap slot for the lightmap (t9). Call once after InitFallbackTextures().
void InitLightmapSrvSlot()
{
	UINT idx = 0;
	if (!TryAllocSrvDescriptors(1, idx))
	{
		std::cerr << "[Renderer] Failed to allocate SRV heap slot for lightmap fallback.\n";
		return;
	}
	D3D12_CPU_DESCRIPTOR_HANDLE cpuBase = g_srvDescHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE gpuBase = g_srvDescHeap->GetGPUDescriptorHandleForHeapStart();
	g_lightmapSrvCpuHandle.ptr = cpuBase.ptr + (SIZE_T)idx * g_srvDescriptorSize;
	g_lightmapSrvGpuHandle.ptr = gpuBase.ptr + (UINT64)idx * g_srvDescriptorSize;

	// Write the black fallback so the slot is always valid
	if (g_fallbackBlack.resource)
	{
		g_d3dDevice->CopyDescriptorsSimple(
			1, g_lightmapSrvCpuHandle, g_fallbackBlack.cpuHandle,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	g_lightmapSrvInHeap = true;
}



// Synchronously load and upload editor UI icons so they are instantly and reliably available on GPU
DX12GpuTexture LoadEditorIconSync(const std::string& name, const DX12GpuTexture& fallback = g_fallbackWhite)
{
	if (name.empty()) return fallback;
	std::string resolved = TextureManager::Get().ResolvePath(name);
	if (resolved.empty()) resolved = name;

	std::string cacheKey = resolved + "@slot=" + std::to_string((int)TextureUsageSlot::BaseColor);
	auto it = g_gpuTextureMap.find(cacheKey);
	if (it != g_gpuTextureMap.end() && it->second.resource) {
		return it->second;
	}

	CachedTexture* cached = TextureManager::Get().GetTexture(resolved, TextureUsageSlot::BaseColor);
	if (cached && cached->valid) {
		DX12GpuTexture tex;
		if (cached->scratchImage.GetImageCount() > 0) {
			tex = UploadTextureToD3D12(cached->scratchImage, resolved);
		} else if (!cached->data.empty() && cached->width > 0 && cached->height > 0) {
			tex = UploadTextureToD3D12(cached->data.data(), cached->width, cached->height, resolved, false);
		}
		if (tex.resource) {
			g_gpuTextureMap[cacheKey] = tex;
			return tex;
		}
	}
	return fallback;
}

DX12GpuTexture GetOrLoadGPUTexture(const std::string& name, const DX12GpuTexture& fallback, TextureUsageSlot slot = TextureUsageSlot::BaseColor)
{
	if (name.empty() || name == "none") return fallback;

	std::string resolved = TextureManager::Get().ResolvePath(name);
	if (resolved.empty()) {
		const AssetMetadata* meta = AssetRegistry::Get().FindByVirtualPath(name);
		if (!meta) {
			AssetID id = AssetID::FromString(name);
			if (id.IsValid()) meta = AssetRegistry::Get().FindById(id);
		}
		if (meta && !meta->sourcePath.empty()) {
			std::filesystem::path cookedCand = AssetManager::Get().ResolvePhysicalOrCookedPath(meta->sourcePath);
			resolved = (!cookedCand.empty()) ? cookedCand.string() : meta->sourcePath;
		} else {
			resolved = name;
		}
	}

	bool isNormal = (slot == TextureUsageSlot::Normal);
	std::string cacheKey = resolved + "@slot=" + std::to_string((int)slot);
	auto it = g_gpuTextureMap.find(cacheKey);
	if (it != g_gpuTextureMap.end()) {
		return it->second;
	}

	// 1. Check if TextureStreamingSystem already has texture ready on GPU
	if (EngineRenderer::TextureStreamingSystem::Get().IsTextureReady(resolved, slot)) {
		DX12GpuTexture streamed = EngineRenderer::TextureStreamingSystem::Get().GetTextureOrFallback(resolved, fallback, slot);
		if (streamed.resource) {
			g_gpuTextureMap[cacheKey] = streamed;
			return streamed;
		}
	}

	// 2. Fast check if already cached in TextureManager CPU memory
	if (TextureManager::Get().IsTextureLoaded(resolved, slot)) {
		CachedTexture* cached = TextureManager::Get().GetTexture(resolved, slot);
		if (cached && cached->valid) {
			bool isNormal = (slot == TextureUsageSlot::Normal);
			DX12GpuTexture tex;
			if (cached->scratchImage.GetImageCount() > 0) {
				tex = UploadTextureToD3D12(cached->scratchImage, resolved);
			} else if (!cached->data.empty() && cached->width > 0 && cached->height > 0) {
				tex = UploadTextureToD3D12(cached->data.data(), cached->width, cached->height, resolved, isNormal);
			}
			if (tex.resource) {
				g_gpuTextureMap[cacheKey] = tex;
				return tex;
			}
		}
	}

	// 3. Asynchronously stream from disk via EngineCore::JobSystem (dev.md)
	EngineRenderer::TextureStreamingSystem::Get().RequestTexture(resolved, slot);
	return fallback;
}

void InvalidateGpuTexture(const std::string& name)
{
	TextureManager::Get().InvalidateTexture(name);
	EngineRenderer::TextureStreamingSystem::Get().InvalidateTexture(name);
	for (int s = 0; s < 10; ++s) {
		g_gpuTextureMap.erase(name + "@slot=" + std::to_string(s));
	}
	g_skyboxDescriptorTables.erase(name);
}

// Update the lightmap SRV slot (no-op since light baking removed, slot holds black fallback).
void UpdateLightmapSrv()
{
	// Baking system decommissioned; persistent fallback holds slot valid.
}

D3D12_GPU_DESCRIPTOR_HANDLE GetOrCreateMaterialTable(
	const std::string& albedo,
	const std::string& normal,
	const std::string& rough,
	const std::string& metal,
	const std::string& ao,
	const std::string& opacity,
	const std::string& emission = "")
{
	std::string key = albedo + "|" + normal + "|" + rough + "|" + metal + "|" + ao + "|" + opacity + "|" + emission;
	auto it = g_materialDescriptorTables.find(key);
	if (it != g_materialDescriptorTables.end()) {
		return it->second.baseGpu;
	}

	DX12GpuTexture texAlbedo   = (!albedo.empty() && albedo != "none")
		? GetOrLoadGPUTexture(albedo, g_fallbackWhite, TextureUsageSlot::BaseColor)
		: g_fallbackWhite;
	DX12GpuTexture texNormal   = GetOrLoadGPUTexture(normal,   g_fallbackNormal, TextureUsageSlot::Normal);
	DX12GpuTexture texRough    = GetOrLoadGPUTexture(rough,    g_fallbackRoughness, TextureUsageSlot::Roughness);
	DX12GpuTexture texMetal    = GetOrLoadGPUTexture(metal,    g_fallbackMetallic, TextureUsageSlot::Metallic);
	DX12GpuTexture texAO       = GetOrLoadGPUTexture(ao,       g_fallbackAO, TextureUsageSlot::AO);
	DX12GpuTexture texOpacity  = (!opacity.empty() && opacity != "none")
		? GetOrLoadGPUTexture(opacity, g_fallbackWhite, TextureUsageSlot::Opacity)
		: g_fallbackWhite;
	DX12GpuTexture texEmission = (!emission.empty() && emission != "none")
		? GetOrLoadGPUTexture(emission, g_fallbackBlack, TextureUsageSlot::Emission)
		: g_fallbackBlack;

	static D3D12_GPU_DESCRIPTOR_HANDLE s_fallbackTable = {};
	static bool s_hasFallbackTable = false;

	UINT baseIdx = 0;
	if (!TryAllocSrvDescriptors(7, baseIdx))
	{
		if (s_hasFallbackTable) {
			return s_fallbackTable;
		}
		return g_srvDescHeap ? g_srvDescHeap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
	}

	D3D12_CPU_DESCRIPTOR_HANDLE baseCpu = g_srvDescHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE baseGpu = g_srvDescHeap->GetGPUDescriptorHandleForHeapStart();
	baseCpu.ptr += (SIZE_T)baseIdx * g_srvDescriptorSize;
	baseGpu.ptr += (UINT64)baseIdx * g_srvDescriptorSize;

	DX12GpuTexture slots[7] = { texAlbedo, texNormal, texRough, texMetal, texAO, texOpacity, texEmission };
	const std::string texNames[7] = { albedo, normal, rough, metal, ao, opacity, emission };
	const TextureUsageSlot texSlots[7] = {
		TextureUsageSlot::BaseColor,
		TextureUsageSlot::Normal,
		TextureUsageSlot::Roughness,
		TextureUsageSlot::Metallic,
		TextureUsageSlot::AO,
		TextureUsageSlot::Opacity,
		TextureUsageSlot::Emission
	};

	for (int i = 0; i < 7; ++i) {
		D3D12_CPU_DESCRIPTOR_HANDLE srcCpu = (slots[i].resource && slots[i].cpuHandle.ptr != 0) ? slots[i].cpuHandle : (i == 6 ? g_fallbackBlack.cpuHandle : g_fallbackWhite.cpuHandle);
		D3D12_CPU_DESCRIPTOR_HANDLE dst = baseCpu;
		dst.ptr += (SIZE_T)i * g_srvDescriptorSize;
		g_d3dDevice->CopyDescriptorsSimple(1, dst, srcCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		// If texture is currently streaming, register this descriptor slot so it gets auto-patched the moment the GPU upload finishes
		if (!texNames[i].empty() && texNames[i] != "none") {
			std::string resolved = TextureManager::Get().ResolvePath(texNames[i]);
			if (resolved.empty()) resolved = texNames[i];
			std::string cacheKey = resolved + "@slot=" + std::to_string(static_cast<int>(texSlots[i]));
			bool isReady = (g_gpuTextureMap.find(cacheKey) != g_gpuTextureMap.end()) ||
			               EngineRenderer::TextureStreamingSystem::Get().IsTextureReady(resolved, texSlots[i]);
			if (!isReady) {
				EngineRenderer::TextureStreamingSystem::Get().RegisterMaterialTableSlot(cacheKey, dst);
			}
		}
	}

	if (!s_hasFallbackTable) {
		s_fallbackTable = baseGpu;
		s_hasFallbackTable = true;
	}

	MaterialTableEntry entry;
	entry.baseCpu = baseCpu;
	entry.baseGpu = baseGpu;
	entry.albedo = albedo;
	entry.normal = normal;
	entry.rough = rough;
	entry.metal = metal;
	entry.ao = ao;
	entry.opacity = opacity;
	entry.emission = emission;
	g_materialDescriptorTables[key] = entry;
	return baseGpu;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetOrCreateSkyboxTable(const std::string& texturePath)
{
	std::string key = texturePath.empty() ? "__default_skybox__" : texturePath;

	// Resolve the full texture path and cache key
	std::string resolved = TextureManager::Get().ResolvePath(texturePath);
	if (resolved.empty() && !texturePath.empty() && texturePath != "none") {
		const AssetMetadata* meta = AssetRegistry::Get().FindByVirtualPath(texturePath);
		if (!meta) {
			AssetID id = AssetID::FromString(texturePath);
			if (id.IsValid()) meta = AssetRegistry::Get().FindById(id);
		}
		if (meta && !meta->sourcePath.empty()) {
			std::filesystem::path cp = AssetManager::Get().ResolvePhysicalOrCookedPath(meta->sourcePath);
			resolved = (!cp.empty()) ? cp.string() : meta->sourcePath;
		} else {
			resolved = texturePath;
		}
	}
	std::string cacheKey = resolved + "@slot=" + std::to_string(static_cast<int>(TextureUsageSlot::BaseColor));

	auto it = g_skyboxDescriptorTables.find(key);
	if (it != g_skyboxDescriptorTables.end()) {
		// Table already allocated. Refresh descriptor if GPU texture is now ready
		if (!texturePath.empty() && texturePath != "none" && it->second.baseCpu.ptr != 0) {
			auto texIt = g_gpuTextureMap.find(cacheKey);
			if (texIt != g_gpuTextureMap.end() && texIt->second.resource && texIt->second.cpuHandle.ptr != 0) {
				g_d3dDevice->CopyDescriptorsSimple(1, it->second.baseCpu, texIt->second.cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			} else if (EngineRenderer::TextureStreamingSystem::Get().IsTextureReady(resolved, TextureUsageSlot::BaseColor)) {
				DX12GpuTexture streamed = EngineRenderer::TextureStreamingSystem::Get().GetTextureOrFallback(resolved, g_fallbackWhite, TextureUsageSlot::BaseColor);
				if (streamed.resource && streamed.cpuHandle.ptr != 0) {
					g_gpuTextureMap[cacheKey] = streamed;
					g_d3dDevice->CopyDescriptorsSimple(1, it->second.baseCpu, streamed.cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				}
			}
		}
		return it->second.baseGpu;
	}

	DX12GpuTexture tex = (!texturePath.empty() && texturePath != "none")
		? GetOrLoadGPUTexture(texturePath, g_fallbackWhite, TextureUsageSlot::BaseColor)
		: g_fallbackWhite;

	UINT baseIdx = 0;
	if (!TryAllocSrvDescriptors(1, baseIdx))
	{
		return g_srvDescHeap ? g_srvDescHeap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
	}

	D3D12_CPU_DESCRIPTOR_HANDLE baseCpu = g_srvDescHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE baseGpu = g_srvDescHeap->GetGPUDescriptorHandleForHeapStart();
	baseCpu.ptr += (SIZE_T)baseIdx * g_srvDescriptorSize;
	baseGpu.ptr += (UINT64)baseIdx * g_srvDescriptorSize;

	D3D12_CPU_DESCRIPTOR_HANDLE srcCpu = (tex.resource && tex.cpuHandle.ptr != 0) ? tex.cpuHandle : g_fallbackWhite.cpuHandle;
	g_d3dDevice->CopyDescriptorsSimple(1, baseCpu, srcCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Register with TextureStreamingSystem so whenever streaming finishes, this descriptor slot is updated automatically
	if (!texturePath.empty() && texturePath != "none") {
		bool isReady = (g_gpuTextureMap.find(cacheKey) != g_gpuTextureMap.end()) ||
		               EngineRenderer::TextureStreamingSystem::Get().IsTextureReady(resolved, TextureUsageSlot::BaseColor);
		if (!isReady) {
			EngineRenderer::TextureStreamingSystem::Get().RegisterMaterialTableSlot(cacheKey, baseCpu);
		}
	}

	SkyboxTableEntry entry;
	entry.baseCpu = baseCpu;
	entry.baseGpu = baseGpu;
	entry.texturePath = texturePath;
	g_skyboxDescriptorTables[key] = entry;
	return baseGpu;
}

// Pre-upload all textures and material descriptor tables needed by current scene batches
// Runs BEFORE renderFrame() resets the frame command list, eliminating GPU command queue deadlocks
void PreRenderUploadTextures()
{
	// Process completed asynchronous texture streaming uploads and update material tables (dev.md)
	EngineRenderer::TextureStreamingSystem::Get().Update();

	// Wicked Engine Viewport Visibility Culling: Only visible textures in active viewport retain native quality
	std::unordered_set<std::string> visibleTextures;
	for (const auto& batch : g_sceneBatches)
	{
		if (batch.indexCount == 0) continue;
		if (batch.isVisible || batch.disableStreaming)
		{
			if (!batch.albedoTex.empty() && batch.albedoTex != "none") visibleTextures.insert(batch.albedoTex);
			if (!batch.normalTex.empty() && batch.normalTex != "none") visibleTextures.insert(batch.normalTex);
			if (!batch.roughTex.empty() && batch.roughTex != "none") visibleTextures.insert(batch.roughTex);
			if (!batch.metalTex.empty() && batch.metalTex != "none") visibleTextures.insert(batch.metalTex);
			if (!batch.aoTex.empty() && batch.aoTex != "none") visibleTextures.insert(batch.aoTex);
			if (!batch.opacityTex.empty() && batch.opacityTex != "none") visibleTextures.insert(batch.opacityTex);
			if (!batch.emissionTex.empty() && batch.emissionTex != "none") visibleTextures.insert(batch.emissionTex);
		}
	}

	if (g_engineUI.showMaterialEditor)
	{
		const auto& curMat = g_engineUI.activeMaterial;
		if (!curMat.baseColorTexture.empty() && curMat.baseColorTexture != "none") visibleTextures.insert(curMat.baseColorTexture);
		if (!curMat.normalTexture.empty() && curMat.normalTexture != "none") visibleTextures.insert(curMat.normalTexture);
		if (!curMat.roughnessTexture.empty() && curMat.roughnessTexture != "none") visibleTextures.insert(curMat.roughnessTexture);
		if (!curMat.metallicTexture.empty() && curMat.metallicTexture != "none") visibleTextures.insert(curMat.metallicTexture);
		if (!curMat.aoTexture.empty() && curMat.aoTexture != "none") visibleTextures.insert(curMat.aoTexture);
		if (!curMat.opacityTexture.empty() && curMat.opacityTexture != "none") visibleTextures.insert(curMat.opacityTexture);
		if (!curMat.emissionTexture.empty() && curMat.emissionTexture != "none") visibleTextures.insert(curMat.emissionTexture);
	}

	if (g_scene.enableSkybox && !g_scene.skyboxTexturePath.empty() && g_scene.skyboxTexturePath != "none")
	{
		visibleTextures.insert(g_scene.skyboxTexturePath);
		// Pre-request and trigger GPU texture upload if CPU image is ready
		GetOrLoadGPUTexture(g_scene.skyboxTexturePath, g_fallbackWhite, TextureUsageSlot::BaseColor);
	}

	EngineRenderer::TextureStreamingSystem::Get().UpdateViewportVisibility(visibleTextures);

	for (const auto& batch : g_sceneBatches)
	{
		if (batch.indexCount == 0) continue;
		GetOrCreateMaterialTable(
			batch.albedoTex, batch.normalTex, batch.roughTex, batch.metalTex, batch.aoTex, batch.opacityTex, batch.emissionTex);
	}

	if (g_engineUI.showMaterialEditor)
	{
		const auto& curMat = g_engineUI.activeMaterial;
		GetOrCreateMaterialTable(
			curMat.baseColorTexture,
			curMat.normalTexture,
			curMat.roughnessTexture,
			curMat.metallicTexture,
			curMat.aoTexture,
			curMat.opacityTexture,
			curMat.emissionTexture
		);
	}

	if (g_scene.enableSkybox && !g_scene.skyboxTexturePath.empty())
	{
		GetOrCreateSkyboxTable(g_scene.skyboxTexturePath);
	}

	// Re-copy updated descriptors into all active skybox descriptor tables if texture upload completed
	for (auto& kv : g_skyboxDescriptorTables)
	{
		SkyboxTableEntry& entry = kv.second;
		if (entry.baseCpu.ptr == 0 || entry.texturePath.empty() || entry.texturePath == "none") continue;

		std::string resolved = TextureManager::Get().ResolvePath(entry.texturePath);
		if (resolved.empty()) {
			const AssetMetadata* meta = AssetRegistry::Get().FindByVirtualPath(entry.texturePath);
			if (!meta) {
				AssetID id = AssetID::FromString(entry.texturePath);
				if (id.IsValid()) meta = AssetRegistry::Get().FindById(id);
			}
			if (meta && !meta->sourcePath.empty()) {
				std::filesystem::path cp = AssetManager::Get().ResolvePhysicalOrCookedPath(meta->sourcePath);
				resolved = (!cp.empty()) ? cp.string() : meta->sourcePath;
			} else {
				resolved = entry.texturePath;
			}
		}
		std::string cacheKey = resolved + "@slot=" + std::to_string(static_cast<int>(TextureUsageSlot::BaseColor));
		auto texIt = g_gpuTextureMap.find(cacheKey);
		if (texIt != g_gpuTextureMap.end() && texIt->second.resource && texIt->second.cpuHandle.ptr != 0) {
			g_d3dDevice->CopyDescriptorsSimple(1, entry.baseCpu, texIt->second.cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		} else if (EngineRenderer::TextureStreamingSystem::Get().IsTextureReady(resolved, TextureUsageSlot::BaseColor)) {
			DX12GpuTexture streamed = EngineRenderer::TextureStreamingSystem::Get().GetTextureOrFallback(resolved, g_fallbackWhite, TextureUsageSlot::BaseColor);
			if (streamed.resource && streamed.cpuHandle.ptr != 0) {
				g_gpuTextureMap[cacheKey] = streamed;
				g_d3dDevice->CopyDescriptorsSimple(1, entry.baseCpu, streamed.cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			}
		}
	}
}

// Updates the D3D12 Shader Resource View (SRV) ResourceMinLODClamp for all active textures in real time.
// Zero reallocation, zero disk read, zero GPU stall.
// If a texture's native resolution is <= maxAllowedResolution (e.g. 2K texture in 4K quality), it retains full native sharpness (0.0f clamp).
void UpdateAllTextureLODClamps()
{
	if (!g_d3dDevice) return;

	int maxRes = TextureManager::Get().GetMaxTextureResolution();

	// 1. Update standalone texture SRVs in g_gpuTextureMap with new ResourceMinLODClamp
	for (auto& pair : g_gpuTextureMap)
	{
		DX12GpuTexture& tex = pair.second;
		if (!tex.resource || tex.cpuHandle.ptr == 0) continue;

		float lodClamp = TextureManager::CalculateTextureMinLOD(tex.width, tex.height, maxRes);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = tex.format;
		if (tex.isCubemap) {
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MipLevels = tex.mipLevels;
			srvDesc.TextureCube.MostDetailedMip = 0;
			srvDesc.TextureCube.ResourceMinLODClamp = lodClamp;
		} else {
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = tex.mipLevels;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.ResourceMinLODClamp = lodClamp;
		}
		g_d3dDevice->CreateShaderResourceView(tex.resource, &srvDesc, tex.cpuHandle);
	}

	// 2. Re-copy updated descriptors into all active contiguous material descriptor tables
	for (auto& kv : g_materialDescriptorTables)
	{
		MaterialTableEntry& entry = kv.second;
		if (entry.baseCpu.ptr == 0) continue;

		DX12GpuTexture texAlbedo   = (!entry.albedo.empty() && entry.albedo != "none")
			? GetOrLoadGPUTexture(entry.albedo, g_fallbackWhite, TextureUsageSlot::BaseColor)
			: g_fallbackWhite;
		DX12GpuTexture texNormal   = GetOrLoadGPUTexture(entry.normal,   g_fallbackNormal, TextureUsageSlot::Normal);
		DX12GpuTexture texRough    = GetOrLoadGPUTexture(entry.rough,    g_fallbackRoughness, TextureUsageSlot::Roughness);
		DX12GpuTexture texMetal    = GetOrLoadGPUTexture(entry.metal,    g_fallbackMetallic, TextureUsageSlot::Metallic);
		DX12GpuTexture texAO       = GetOrLoadGPUTexture(entry.ao,       g_fallbackAO, TextureUsageSlot::AO);
		DX12GpuTexture texOpacity  = (!entry.opacity.empty() && entry.opacity != "none")
			? GetOrLoadGPUTexture(entry.opacity, g_fallbackWhite, TextureUsageSlot::Opacity)
			: g_fallbackWhite;
		DX12GpuTexture texEmission = (!entry.emission.empty() && entry.emission != "none")
			? GetOrLoadGPUTexture(entry.emission, g_fallbackBlack, TextureUsageSlot::Emission)
			: g_fallbackBlack;

		DX12GpuTexture slots[7] = { texAlbedo, texNormal, texRough, texMetal, texAO, texOpacity, texEmission };
		for (int i = 0; i < 7; ++i) {
			D3D12_CPU_DESCRIPTOR_HANDLE srcCpu = (slots[i].resource && slots[i].cpuHandle.ptr != 0)
				? slots[i].cpuHandle
				: (i == 6 ? g_fallbackBlack.cpuHandle : g_fallbackWhite.cpuHandle);
			D3D12_CPU_DESCRIPTOR_HANDLE dst = entry.baseCpu;
			dst.ptr += (SIZE_T)i * g_srvDescriptorSize;
			g_d3dDevice->CopyDescriptorsSimple(1, dst, srcCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}
	}

	// 3. Re-copy updated descriptors into all active skybox descriptor tables
	for (auto& kv : g_skyboxDescriptorTables)
	{
		SkyboxTableEntry& entry = kv.second;
		if (entry.baseCpu.ptr == 0) continue;

		DX12GpuTexture tex = (!entry.texturePath.empty() && entry.texturePath != "none")
			? GetOrLoadGPUTexture(entry.texturePath, g_fallbackWhite, TextureUsageSlot::BaseColor)
			: g_fallbackWhite;

		D3D12_CPU_DESCRIPTOR_HANDLE srcCpu = (tex.resource && tex.cpuHandle.ptr != 0)
			? tex.cpuHandle
			: g_fallbackWhite.cpuHandle;
		g_d3dDevice->CopyDescriptorsSimple(1, entry.baseCpu, srcCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
}

static void EnsureGodRayDescriptorTable()
{
	if (g_godRayTableCpuHandle.ptr == 0 && g_srvDescHeap && g_d3dDevice)
	{
		UINT descIdx = 0;
		if (TryAllocSrvDescriptors(4, descIdx))
		{
			g_godRayTableCpuHandle = g_srvDescHeap->GetCPUDescriptorHandleForHeapStart();
			g_godRayTableGpuHandle = g_srvDescHeap->GetGPUDescriptorHandleForHeapStart();
			g_godRayTableCpuHandle.ptr += (SIZE_T)descIdx * g_srvDescriptorSize;
			g_godRayTableGpuHandle.ptr += (UINT64)descIdx * g_srvDescriptorSize;

			// Initialize default null SRVs in all 4 slots so no descriptor is unbound
			D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv2D = {};
			nullSrv2D.Format = DXGI_FORMAT_R32_FLOAT;
			nullSrv2D.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			nullSrv2D.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			nullSrv2D.Texture2D.MipLevels = 1;

			D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv2DArray = {};
			nullSrv2DArray.Format = DXGI_FORMAT_R32_FLOAT;
			nullSrv2DArray.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			nullSrv2DArray.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			nullSrv2DArray.Texture2DArray.ArraySize = MAX_SHADOW_SPOT_LIGHTS;
			nullSrv2DArray.Texture2DArray.MipLevels = 1;

			D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvCubeArray = {};
			nullSrvCubeArray.Format = DXGI_FORMAT_R32_FLOAT;
			nullSrvCubeArray.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
			nullSrvCubeArray.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			nullSrvCubeArray.TextureCubeArray.NumCubes = MAX_SHADOW_POINT_LIGHTS;
			nullSrvCubeArray.TextureCubeArray.MipLevels = 1;

			D3D12_CPU_DESCRIPTOR_HANDLE h0 = g_godRayTableCpuHandle;
			D3D12_CPU_DESCRIPTOR_HANDLE h1 = h0; h1.ptr += (SIZE_T)1 * g_srvDescriptorSize;
			D3D12_CPU_DESCRIPTOR_HANDLE h2 = h0; h2.ptr += (SIZE_T)2 * g_srvDescriptorSize;
			D3D12_CPU_DESCRIPTOR_HANDLE h3 = h0; h3.ptr += (SIZE_T)3 * g_srvDescriptorSize;

			g_d3dDevice->CreateShaderResourceView(nullptr, &nullSrv2D, h0);
			g_d3dDevice->CreateShaderResourceView(nullptr, &nullSrv2D, h1);
			g_d3dDevice->CreateShaderResourceView(nullptr, &nullSrv2DArray, h2);
			g_d3dDevice->CreateShaderResourceView(nullptr, &nullSrvCubeArray, h3);
		}
	}
}

static void UpdateGodRayDepthSrv()
{
	if (!g_d3dDevice || !g_depthStencilBuffer || !g_srvDescHeap) return;
	EnsureGodRayDescriptorTable();
	if (g_godRayTableCpuHandle.ptr == 0) return;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	// Slot 0 of god ray table: depth buffer
	D3D12_CPU_DESCRIPTOR_HANDLE destCpu = g_godRayTableCpuHandle;
	g_d3dDevice->CreateShaderResourceView(g_depthStencilBuffer, &srvDesc, destCpu);
}

static void UpdateGodRayShadowSrv()
{
	if (!g_d3dDevice || !g_shadowDepthBuffer || !g_srvDescHeap) return;
	EnsureGodRayDescriptorTable();
	if (g_godRayTableCpuHandle.ptr == 0) return;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	// Slot 1 of god ray table: directional shadow map
	D3D12_CPU_DESCRIPTOR_HANDLE destCpu = g_godRayTableCpuHandle;
	destCpu.ptr += (SIZE_T)1 * g_srvDescriptorSize;
	g_d3dDevice->CreateShaderResourceView(g_shadowDepthBuffer, &srvDesc, destCpu);
}

static void UpdateGodRaySpotShadowSrv()
{
	if (!g_d3dDevice || !g_spotShadowDepthBuffer || !g_srvDescHeap) return;
	EnsureGodRayDescriptorTable();
	if (g_godRayTableCpuHandle.ptr == 0) return;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = MAX_SHADOW_SPOT_LIGHTS;
	srvDesc.Texture2DArray.MipLevels = 1;

	// Slot 2 of god ray table: spot shadow map array
	D3D12_CPU_DESCRIPTOR_HANDLE destCpu = g_godRayTableCpuHandle;
	destCpu.ptr += (SIZE_T)2 * g_srvDescriptorSize;
	g_d3dDevice->CreateShaderResourceView(g_spotShadowDepthBuffer, &srvDesc, destCpu);
}

static void UpdateGodRayPointShadowSrv()
{
	if (!g_d3dDevice || !g_pointShadowDepthBuffer || !g_srvDescHeap) return;
	EnsureGodRayDescriptorTable();
	if (g_godRayTableCpuHandle.ptr == 0) return;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
	srvDesc.TextureCubeArray.First2DArrayFace = 0;
	srvDesc.TextureCubeArray.NumCubes = MAX_SHADOW_POINT_LIGHTS;
	srvDesc.TextureCubeArray.MipLevels = 1;

	// Slot 3 of god ray table: point shadow map cube array
	D3D12_CPU_DESCRIPTOR_HANDLE destCpu = g_godRayTableCpuHandle;
	destCpu.ptr += (SIZE_T)3 * g_srvDescriptorSize;
	g_d3dDevice->CreateShaderResourceView(g_pointShadowDepthBuffer, &srvDesc, destCpu);
}

static void EnsureAoDescriptorTable()
{
	if (g_aoTableCpuHandle.ptr == 0 && g_srvDescHeap && g_d3dDevice)
	{
		UINT descIdx = 0;
		if (TryAllocSrvDescriptors(3, descIdx))
		{
			g_aoTableCpuHandle = g_srvDescHeap->GetCPUDescriptorHandleForHeapStart();
			g_aoTableGpuHandle = g_srvDescHeap->GetGPUDescriptorHandleForHeapStart();
			g_aoTableCpuHandle.ptr += (SIZE_T)descIdx * g_srvDescriptorSize;
			g_aoTableGpuHandle.ptr += (UINT64)descIdx * g_srvDescriptorSize;

			// Initialize default null SRVs so all 3 slots are valid
			D3D12_SHADER_RESOURCE_VIEW_DESC nullDepth = {};
			nullDepth.Format = DXGI_FORMAT_R32_FLOAT;
			nullDepth.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			nullDepth.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			nullDepth.Texture2D.MipLevels = 1;

			D3D12_SHADER_RESOURCE_VIEW_DESC nullAo = {};
			nullAo.Format = DXGI_FORMAT_R8_UNORM;
			nullAo.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			nullAo.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			nullAo.Texture2D.MipLevels = 1;

			D3D12_CPU_DESCRIPTOR_HANDLE h0 = g_aoTableCpuHandle;
			D3D12_CPU_DESCRIPTOR_HANDLE h1 = h0; h1.ptr += (SIZE_T)1 * g_srvDescriptorSize;
			D3D12_CPU_DESCRIPTOR_HANDLE h2 = h0; h2.ptr += (SIZE_T)2 * g_srvDescriptorSize;

			g_d3dDevice->CreateShaderResourceView(nullptr, &nullDepth, h0);
			g_d3dDevice->CreateShaderResourceView(nullptr, &nullAo, h1);
			g_d3dDevice->CreateShaderResourceView(nullptr, &nullAo, h2);
		}
	}
}

static void UpdateAoDepthSrv()
{
	if (!g_d3dDevice || !g_depthStencilBuffer || !g_srvDescHeap) return;
	EnsureAoDescriptorTable();
	if (g_aoTableCpuHandle.ptr == 0) return;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	g_d3dDevice->CreateShaderResourceView(g_depthStencilBuffer, &srvDesc, g_aoTableCpuHandle);
}

int createDepthStencilView(int width, int height)
{
	if (g_depthStencilBuffer)
	{
		g_depthStencilBuffer->Release();
		g_depthStencilBuffer = nullptr;
	}

	D3D12_RESOURCE_DESC depthDesc = {};
	depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthDesc.Alignment = 0;
	depthDesc.Width = (UINT64)width;
	depthDesc.Height = (UINT)height;
	depthDesc.DepthOrArraySize = 1;
	depthDesc.MipLevels = 1;
	depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.SampleDesc.Quality = 0;
	depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE depthClear = {};
	depthClear.Format = DXGI_FORMAT_D32_FLOAT;
	depthClear.DepthStencil.Depth = 1.0f;
	depthClear.DepthStencil.Stencil = 0;

	D3D12_HEAP_PROPERTIES heapProps = CreateHeapProperties(D3D12_HEAP_TYPE_DEFAULT);

	HRESULT hr = g_d3dDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&depthDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClear,
		IID_PPV_ARGS(&g_depthStencilBuffer)
	);
	if (FAILED(hr))
	{
		std::cerr << "Failed to create D3D12 depth stencil buffer: " << hr << std::endl;
		return EXIT_FAILURE;
	}
	g_depthStencilBuffer->SetName(L"g_depthStencilBuffer");

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = g_dsvDescHeap->GetCPUDescriptorHandleForHeapStart();
	g_d3dDevice->CreateDepthStencilView(g_depthStencilBuffer, &dsvDesc, dsvHandle);

	// Update depth SRV in God Ray descriptor table (slot 0)
	UpdateGodRayDepthSrv();
	// Update depth SRV in Ambient Occlusion descriptor table (slot 0)
	UpdateAoDepthSrv();

	return EXIT_SUCCESS;
}

int createDirectionalShadowResource(UINT dirRes = 2048)
{
	if (!g_d3dDevice) return EXIT_FAILURE;

	auto SnapRes = [](UINT r, UINT defVal) -> UINT {
		if (r == 0) return 0;
		if (r < 128) r = 128;
		if (r > 4096) r = 4096;
		if (r <= 192) return 128;
		if (r <= 384) return 256;
		if (r <= 768) return 512;
		if (r <= 1536) return 1024;
		if (r <= 3072) return 2048;
		return 4096;
	};
	dirRes = SnapRes(dirRes, 2048);

	// Defer release of prior buffer
	SafeDeferredRelease(g_shadowDepthBuffer);

	D3D12_HEAP_PROPERTIES heapProps = CreateHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC depthDesc = {};
	depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthDesc.Alignment = 0;
	depthDesc.Width = dirRes;
	depthDesc.Height = dirRes;
	depthDesc.DepthOrArraySize = 1;
	depthDesc.MipLevels = 1;
	depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.SampleDesc.Quality = 0;
	depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE depthClear = {};
	depthClear.Format = DXGI_FORMAT_D32_FLOAT;
	depthClear.DepthStencil.Depth = 1.0f;
	depthClear.DepthStencil.Stencil = 0;

	HRESULT hr = g_d3dDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&depthDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&depthClear,
		IID_PPV_ARGS(&g_shadowDepthBuffer)
	);
	if (FAILED(hr))
	{
		std::cerr << "Failed to create D3D12 directional shadow depth buffer: " << hr << std::endl;
		return EXIT_FAILURE;
	}
	g_shadowDepthBuffer->SetName(L"g_shadowDepthBuffer");

	// Create DSV at index 1 of g_dsvDescHeap
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

	g_shadowDsvHandle = g_dsvDescHeap->GetCPUDescriptorHandleForHeapStart();
	g_shadowDsvHandle.ptr += (SIZE_T)1 * g_dsvDescriptorSize;
	g_d3dDevice->CreateDepthStencilView(g_shadowDepthBuffer, &dsvDesc, g_shadowDsvHandle);

	// Create SRV in g_srvDescHeap for directional shadow map
	if (g_shadowSrvCpuHandle.ptr == 0)
	{
		UINT descIdx = 0;
		TryAllocSrvDescriptors(1, descIdx);
		g_shadowSrvCpuHandle = g_srvDescHeap->GetCPUDescriptorHandleForHeapStart();
		g_shadowSrvGpuHandle = g_srvDescHeap->GetGPUDescriptorHandleForHeapStart();
		g_shadowSrvCpuHandle.ptr += (SIZE_T)descIdx * g_srvDescriptorSize;
		g_shadowSrvGpuHandle.ptr += (UINT64)descIdx * g_srvDescriptorSize;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	g_d3dDevice->CreateShaderResourceView(g_shadowDepthBuffer, &srvDesc, g_shadowSrvCpuHandle);

	// Update shadow SRV in God Ray descriptor table (slot 1)
	UpdateGodRayShadowSrv();

	g_currentDirShadowRes = dirRes;
	return EXIT_SUCCESS;
}

int createPointShadowResource(UINT ptRes = 512)
{
	if (!g_d3dDevice) return EXIT_FAILURE;

	auto SnapRes = [](UINT r, UINT defVal) -> UINT {
		if (r == 0) return 0;
		if (r < 128) r = 128;
		if (r > 4096) r = 4096;
		if (r <= 192) return 128;
		if (r <= 384) return 256;
		if (r <= 768) return 512;
		if (r <= 1536) return 1024;
		if (r <= 3072) return 2048;
		return 4096;
	};
	ptRes = SnapRes(ptRes, 512);

	SafeDeferredRelease(g_pointShadowDepthBuffer);

	D3D12_HEAP_PROPERTIES heapProps = CreateHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC ptDepthDesc = {};
	ptDepthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	ptDepthDesc.Alignment = 0;
	ptDepthDesc.Width = ptRes;
	ptDepthDesc.Height = ptRes;
	ptDepthDesc.DepthOrArraySize = MAX_SHADOW_POINT_LIGHTS * 6; // 24
	ptDepthDesc.MipLevels = 1;
	ptDepthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	ptDepthDesc.SampleDesc.Count = 1;
	ptDepthDesc.SampleDesc.Quality = 0;
	ptDepthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	ptDepthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE ptDepthClear = {};
	ptDepthClear.Format = DXGI_FORMAT_D32_FLOAT;
	ptDepthClear.DepthStencil.Depth = 1.0f;
	ptDepthClear.DepthStencil.Stencil = 0;

	HRESULT hr = g_d3dDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&ptDepthDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&ptDepthClear,
		IID_PPV_ARGS(&g_pointShadowDepthBuffer)
	);
	if (FAILED(hr))
	{
		std::cerr << "Failed to create D3D12 point shadow depth buffer: " << hr << std::endl;
		return EXIT_FAILURE;
	}
	g_pointShadowDepthBuffer->SetName(L"g_pointShadowDepthBuffer");

	// Create 24 DSVs at indices 2..25 of g_dsvDescHeap
	for (UINT i = 0; i < MAX_SHADOW_POINT_LIGHTS * 6; ++i)
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC ptDsvDesc = {};
		ptDsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
		ptDsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
		ptDsvDesc.Flags = D3D12_DSV_FLAG_NONE;
		ptDsvDesc.Texture2DArray.FirstArraySlice = i;
		ptDsvDesc.Texture2DArray.ArraySize = 1;
		ptDsvDesc.Texture2DArray.MipSlice = 0;

		g_pointShadowDsvHandles[i] = g_dsvDescHeap->GetCPUDescriptorHandleForHeapStart();
		g_pointShadowDsvHandles[i].ptr += (SIZE_T)(2 + i) * g_dsvDescriptorSize;
		g_d3dDevice->CreateDepthStencilView(g_pointShadowDepthBuffer, &ptDsvDesc, g_pointShadowDsvHandles[i]);
	}

	// Create SRV in g_srvDescHeap for TextureCubeArray
	if (g_pointShadowSrvCpuHandle.ptr == 0)
	{
		UINT ptDescIdx = 0;
		TryAllocSrvDescriptors(1, ptDescIdx);
		g_pointShadowSrvCpuHandle = g_srvDescHeap->GetCPUDescriptorHandleForHeapStart();
		g_pointShadowSrvGpuHandle = g_srvDescHeap->GetGPUDescriptorHandleForHeapStart();
		g_pointShadowSrvCpuHandle.ptr += (SIZE_T)ptDescIdx * g_srvDescriptorSize;
		g_pointShadowSrvGpuHandle.ptr += (UINT64)ptDescIdx * g_srvDescriptorSize;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC ptSrvDesc = {};
	ptSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	ptSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	ptSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
	ptSrvDesc.TextureCubeArray.First2DArrayFace = 0;
	ptSrvDesc.TextureCubeArray.NumCubes = MAX_SHADOW_POINT_LIGHTS;
	ptSrvDesc.TextureCubeArray.MipLevels = 1;
	g_d3dDevice->CreateShaderResourceView(g_pointShadowDepthBuffer, &ptSrvDesc, g_pointShadowSrvCpuHandle);

	UpdateGodRayPointShadowSrv();

	g_currentPointShadowRes = ptRes;
	return EXIT_SUCCESS;
}

int createSpotShadowResource(UINT spotRes = 1024)
{
	if (!g_d3dDevice) return EXIT_FAILURE;

	auto SnapRes = [](UINT r, UINT defVal) -> UINT {
		if (r == 0) return 0;
		if (r < 128) r = 128;
		if (r > 4096) r = 4096;
		if (r <= 192) return 128;
		if (r <= 384) return 256;
		if (r <= 768) return 512;
		if (r <= 1536) return 1024;
		if (r <= 3072) return 2048;
		return 4096;
	};
	spotRes = SnapRes(spotRes, 1024);

	SafeDeferredRelease(g_spotShadowDepthBuffer);

	D3D12_HEAP_PROPERTIES heapProps = CreateHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC spotDepthDesc = {};
	spotDepthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	spotDepthDesc.Alignment = 0;
	spotDepthDesc.Width = spotRes;
	spotDepthDesc.Height = spotRes;
	spotDepthDesc.DepthOrArraySize = MAX_SHADOW_SPOT_LIGHTS; // 4
	spotDepthDesc.MipLevels = 1;
	spotDepthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	spotDepthDesc.SampleDesc.Count = 1;
	spotDepthDesc.SampleDesc.Quality = 0;
	spotDepthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	spotDepthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE spotDepthClear = {};
	spotDepthClear.Format = DXGI_FORMAT_D32_FLOAT;
	spotDepthClear.DepthStencil.Depth = 1.0f;
	spotDepthClear.DepthStencil.Stencil = 0;

	HRESULT hr = g_d3dDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&spotDepthDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&spotDepthClear,
		IID_PPV_ARGS(&g_spotShadowDepthBuffer)
	);
	if (FAILED(hr))
	{
		std::cerr << "Failed to create D3D12 spot shadow depth buffer: " << hr << std::endl;
		return EXIT_FAILURE;
	}
	g_spotShadowDepthBuffer->SetName(L"g_spotShadowDepthBuffer");

	// Create 4 DSVs at indices 26..29 of g_dsvDescHeap
	for (UINT i = 0; i < MAX_SHADOW_SPOT_LIGHTS; ++i)
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC spotDsvDesc = {};
		spotDsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
		spotDsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
		spotDsvDesc.Flags = D3D12_DSV_FLAG_NONE;
		spotDsvDesc.Texture2DArray.FirstArraySlice = i;
		spotDsvDesc.Texture2DArray.ArraySize = 1;
		spotDsvDesc.Texture2DArray.MipSlice = 0;

		g_spotShadowDsvHandles[i] = g_dsvDescHeap->GetCPUDescriptorHandleForHeapStart();
		g_spotShadowDsvHandles[i].ptr += (SIZE_T)(26 + i) * g_dsvDescriptorSize;
		g_d3dDevice->CreateDepthStencilView(g_spotShadowDepthBuffer, &spotDsvDesc, g_spotShadowDsvHandles[i]);
	}

	// Create SRV in g_srvDescHeap for Texture2DArray
	if (g_spotShadowSrvCpuHandle.ptr == 0)
	{
		UINT spotDescIdx = 0;
		TryAllocSrvDescriptors(1, spotDescIdx);
		g_spotShadowSrvCpuHandle = g_srvDescHeap->GetCPUDescriptorHandleForHeapStart();
		g_spotShadowSrvGpuHandle = g_srvDescHeap->GetGPUDescriptorHandleForHeapStart();
		g_spotShadowSrvCpuHandle.ptr += (SIZE_T)spotDescIdx * g_srvDescriptorSize;
		g_spotShadowSrvGpuHandle.ptr += (UINT64)spotDescIdx * g_srvDescriptorSize;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC spotSrvDesc = {};
	spotSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	spotSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	spotSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	spotSrvDesc.Texture2DArray.FirstArraySlice = 0;
	spotSrvDesc.Texture2DArray.ArraySize = MAX_SHADOW_SPOT_LIGHTS;
	spotSrvDesc.Texture2DArray.MipLevels = 1;
	g_d3dDevice->CreateShaderResourceView(g_spotShadowDepthBuffer, &spotSrvDesc, g_spotShadowSrvCpuHandle);

	UpdateGodRaySpotShadowSrv();

	g_currentSpotShadowRes = spotRes;
	return EXIT_SUCCESS;
}

int createShadowResources(UINT dirRes = 2048, UINT ptRes = 512, UINT spotRes = 1024)
{
	createDirectionalShadowResource(dirRes);
	createPointShadowResource(ptRes);
	createSpotShadowResource(spotRes);

	// 4. Shadow Constant Buffer (upload heap: slot 0 for directional, slots 1..24 for point lights, slots 25..28 for spot lights)
	if (!g_shadowConstantBuffer)
	{
		UINT64 shadowCbSize = (UINT64)(1 + MAX_SHADOW_POINT_LIGHTS * 6 + MAX_SHADOW_SPOT_LIGHTS) * 256;
		D3D12_RESOURCE_DESC cbDesc = CreateBufferResourceDesc(shadowCbSize);
		D3D12_HEAP_PROPERTIES uploadHeap = CreateHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
		g_d3dDevice->CreateCommittedResource(
			&uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_shadowConstantBuffer)
		);
		if (g_shadowConstantBuffer) g_shadowConstantBuffer->SetName(L"g_shadowConstantBuffer");
		D3D12_RANGE readRange = { 0, 0 };
		g_shadowConstantBuffer->Map(0, &readRange, &g_pShadowConstantMapped);
	}

	return EXIT_SUCCESS;
}

int createSceneRenderTargets(int width, int height)
{
	if (!g_d3dDevice || width <= 0 || height <= 0) return EXIT_FAILURE;

	SafeDeferredRelease(g_sceneRenderTarget);
	SafeDeferredRelease(g_sceneHistoryRenderTarget);

	D3D12_HEAP_PROPERTIES heapProps = CreateHeapProperties(D3D12_HEAP_TYPE_DEFAULT);

	D3D12_RESOURCE_DESC rtDesc = {};
	rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rtDesc.Alignment = 0;
	rtDesc.Width = (UINT64)width;
	rtDesc.Height = (UINT)height;
	rtDesc.DepthOrArraySize = 1;
	rtDesc.MipLevels = 1;
	rtDesc.Format = SCENE_RENDER_TARGET_FORMAT;
	rtDesc.SampleDesc.Count = 1;
	rtDesc.SampleDesc.Quality = 0;
	rtDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearVal = {};
	clearVal.Format = SCENE_RENDER_TARGET_FORMAT;
	clearVal.Color[0] = g_scene.clearColor.r;
	clearVal.Color[1] = g_scene.clearColor.g;
	clearVal.Color[2] = g_scene.clearColor.b;
	clearVal.Color[3] = 1.0f;

	HRESULT hr = g_d3dDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&rtDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&clearVal,
		IID_PPV_ARGS(&g_sceneRenderTarget)
	);
	if (FAILED(hr))
	{
		std::cerr << "[Graphics] Failed to create D3D12 scene render target: 0x" << std::hex << hr << std::dec << "\n";
		return EXIT_FAILURE;
	}
	g_sceneRenderTarget->SetName(L"g_sceneRenderTarget");

	hr = g_d3dDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&rtDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearVal,
		IID_PPV_ARGS(&g_sceneHistoryRenderTarget)
	);
	if (FAILED(hr))
	{
		std::cerr << "[Graphics] Failed to create D3D12 scene history render target: 0x" << std::hex << hr << std::dec << "\n";
		return EXIT_FAILURE;
	}
	g_sceneHistoryRenderTarget->SetName(L"g_sceneHistoryRenderTarget");

	if (g_rtvDescHeap)
	{
		g_sceneRtvHandle = g_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
		g_sceneRtvHandle.ptr += (SIZE_T)(FRAME_COUNT + 0) * g_rtvDescriptorSize;
		g_d3dDevice->CreateRenderTargetView(g_sceneRenderTarget, nullptr, g_sceneRtvHandle);

		g_sceneHistoryRtvHandle = g_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
		g_sceneHistoryRtvHandle.ptr += (SIZE_T)(FRAME_COUNT + 1) * g_rtvDescriptorSize;
		g_d3dDevice->CreateRenderTargetView(g_sceneHistoryRenderTarget, nullptr, g_sceneHistoryRtvHandle);
	}

	if (g_srvDescHeap)
	{
		if (g_sceneSrvCpuHandle.ptr == 0)
		{
			UINT baseIdx = 0;
			if (TryAllocSrvDescriptors(2, baseIdx))
			{
				g_sceneSrvCpuHandle = g_srvDescHeap->GetCPUDescriptorHandleForHeapStart();
				g_sceneSrvGpuHandle = g_srvDescHeap->GetGPUDescriptorHandleForHeapStart();
				g_sceneSrvCpuHandle.ptr += (SIZE_T)baseIdx * g_srvDescriptorSize;
				g_sceneSrvGpuHandle.ptr += (UINT64)baseIdx * g_srvDescriptorSize;

				g_sceneHistorySrvCpuHandle.ptr = g_sceneSrvCpuHandle.ptr + g_srvDescriptorSize;
				g_sceneHistorySrvGpuHandle.ptr = g_sceneSrvGpuHandle.ptr + (UINT64)g_srvDescriptorSize;
			}
		}

		if (g_sceneSrvCpuHandle.ptr != 0)
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = SCENE_RENDER_TARGET_FORMAT;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;

			g_d3dDevice->CreateShaderResourceView(g_sceneRenderTarget, &srvDesc, g_sceneSrvCpuHandle);
			g_d3dDevice->CreateShaderResourceView(g_sceneHistoryRenderTarget, &srvDesc, g_sceneHistorySrvCpuHandle);
		}
	}

	createAoRenderTargets(width, height);

	return EXIT_SUCCESS;
}

int createAoRenderTargets(int width, int height)
{
	if (!g_d3dDevice || width <= 0 || height <= 0) return EXIT_FAILURE;

	SafeDeferredRelease(g_aoRenderTarget);
	SafeDeferredRelease(g_aoBlurRenderTarget);

	D3D12_HEAP_PROPERTIES heapProps = CreateHeapProperties(D3D12_HEAP_TYPE_DEFAULT);

	D3D12_RESOURCE_DESC rtDesc = {};
	rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rtDesc.Alignment = 0;
	rtDesc.Width = (UINT64)width;
	rtDesc.Height = (UINT)height;
	rtDesc.DepthOrArraySize = 1;
	rtDesc.MipLevels = 1;
	rtDesc.Format = DXGI_FORMAT_R8_UNORM;
	rtDesc.SampleDesc.Count = 1;
	rtDesc.SampleDesc.Quality = 0;
	rtDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearVal = {};
	clearVal.Format = DXGI_FORMAT_R8_UNORM;
	clearVal.Color[0] = 1.0f;
	clearVal.Color[1] = 1.0f;
	clearVal.Color[2] = 1.0f;
	clearVal.Color[3] = 1.0f;

	HRESULT hr = g_d3dDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&rtDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&clearVal,
		IID_PPV_ARGS(&g_aoRenderTarget)
	);
	if (FAILED(hr))
	{
		std::cerr << "[Graphics] Failed to create D3D12 AO render target: 0x" << std::hex << hr << std::dec << "\n";
		return EXIT_FAILURE;
	}
	g_aoRenderTarget->SetName(L"g_aoRenderTarget");

	hr = g_d3dDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&rtDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearVal,
		IID_PPV_ARGS(&g_aoBlurRenderTarget)
	);
	if (FAILED(hr))
	{
		std::cerr << "[Graphics] Failed to create D3D12 AO blur render target: 0x" << std::hex << hr << std::dec << "\n";
		return EXIT_FAILURE;
	}
	g_aoBlurRenderTarget->SetName(L"g_aoBlurRenderTarget");

	if (g_rtvDescHeap)
	{
		g_aoRtvHandle = g_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
		g_aoRtvHandle.ptr += (SIZE_T)(FRAME_COUNT + 3) * g_rtvDescriptorSize;
		g_d3dDevice->CreateRenderTargetView(g_aoRenderTarget, nullptr, g_aoRtvHandle);

		g_aoBlurRtvHandle = g_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
		g_aoBlurRtvHandle.ptr += (SIZE_T)(FRAME_COUNT + 4) * g_rtvDescriptorSize;
		g_d3dDevice->CreateRenderTargetView(g_aoBlurRenderTarget, nullptr, g_aoBlurRtvHandle);
	}

	EnsureAoDescriptorTable();
	if (g_aoTableCpuHandle.ptr != 0)
	{
		UpdateAoDepthSrv();

		D3D12_CPU_DESCRIPTOR_HANDLE hAoRaw = g_aoTableCpuHandle;
		hAoRaw.ptr += (SIZE_T)1 * g_srvDescriptorSize;

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_R8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		g_d3dDevice->CreateShaderResourceView(g_aoRenderTarget, &srvDesc, hAoRaw);

		D3D12_CPU_DESCRIPTOR_HANDLE hAoBlur = g_aoTableCpuHandle;
		hAoBlur.ptr += (SIZE_T)2 * g_srvDescriptorSize;

		g_d3dDevice->CreateShaderResourceView(g_aoBlurRenderTarget, &srvDesc, hAoBlur);
	}

	return EXIT_SUCCESS;
}

void EnsureShadowBuffers(UINT reqDirRes, UINT reqPtRes, UINT reqSpotRes)
{
	auto SnapRes = [](UINT r, UINT defVal) -> UINT {
		if (r == 0) return 0;
		if (r < 128) r = 128;
		if (r > 4096) r = 4096;
		if (r <= 192) return 128;
		if (r <= 384) return 256;
		if (r <= 768) return 512;
		if (r <= 1536) return 1024;
		if (r <= 3072) return 2048;
		return 4096;
	};
	reqDirRes = (reqDirRes == 0) ? 128 : SnapRes(reqDirRes, 2048);
	reqPtRes = (reqPtRes == 0) ? 128 : SnapRes(reqPtRes, 512);
	reqSpotRes = (reqSpotRes == 0) ? 128 : SnapRes(reqSpotRes, 1024);

	bool needDir  = (!g_shadowDepthBuffer || reqDirRes != g_currentDirShadowRes);
	bool needPt   = (!g_pointShadowDepthBuffer || reqPtRes != g_currentPointShadowRes);
	bool needSpot = (!g_spotShadowDepthBuffer || reqSpotRes != g_currentSpotShadowRes);

	if (needDir || needPt || needSpot)
	{
		WaitForGpuIdle();
		FlushDeferredReleases();

		if (needDir)  createDirectionalShadowResource(reqDirRes);
		if (needPt)   createPointShadowResource(reqPtRes);
		if (needSpot) createSpotShadowResource(reqSpotRes);
	}
}

int initD3D12(HWND hwnd)
{
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG) || defined(EUNOIA_D3D12_DEBUG)
	{
		ID3D12Debug* debugController = nullptr;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			ID3D12Debug1* debugController1 = nullptr;
			if (SUCCEEDED(debugController->QueryInterface(IID_PPV_ARGS(&debugController1))))
			{
				debugController1->SetEnableGPUBasedValidation(TRUE);
				debugController1->Release();
			}
			debugController->Release();
			std::cout << "[DEBUG] D3D12 debug layer + GPU-based validation enabled.\n";
		}
		dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}
#endif

	// Enable DRED (Device Removed Extended Data) for auto-breadcrumbs and page fault diagnostics
	{
		ID3D12DeviceRemovedExtendedDataSettings* dredSettings = nullptr;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings))))
		{
			dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			dredSettings->Release();
			std::cout << "[DEBUG] DRED (auto-breadcrumbs + page fault) enabled.\n";
		}
	}

	// 1. Create DXGI Factory
	HRESULT hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&g_dxgiFactory));
	if (FAILED(hr))
	{
		std::cerr << "Failed to create DXGIFactory2: " << hr << std::endl;
		return EXIT_FAILURE;
	}

	// 2. Select Hardware Adapter (Prefer High Performance / Discrete GPU via IDXGIFactory6)
	IDXGIAdapter1* adapter = nullptr;
	IDXGIFactory6* factory6 = nullptr;
	if (SUCCEEDED(g_dxgiFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
	{
		for (UINT i = 0; SUCCEEDED(factory6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter))); ++i)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				adapter->Release();
				adapter = nullptr;
				continue;
			}
			if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
			{
				char descStr[128];
				WideCharToMultiByte(CP_ACP, 0, desc.Description, -1, descStr, sizeof(descStr), nullptr, nullptr);
				std::cout << "[EngineRHI] High Performance D3D12 GPU Selected: " << descStr << std::endl;
				break;
			}
			adapter->Release();
			adapter = nullptr;
		}
		factory6->Release();
	}

	if (!adapter)
	{
		for (UINT i = 0; g_dxgiFactory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			{
				adapter->Release();
				continue;
			}

			if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
			{
				char descStr[128];
				WideCharToMultiByte(CP_ACP, 0, desc.Description, -1, descStr, sizeof(descStr), nullptr, nullptr);
				std::cout << "[EngineRHI] Direct3D 12 Adapter: " << descStr << std::endl;
				break;
			}
			adapter->Release();
			adapter = nullptr;
		}
	}

	// 3. Create Device
	hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_d3dDevice));
	if (adapter) adapter->Release();
	if (FAILED(hr))
	{
		std::cerr << "Failed to create D3D12Device: " << hr << std::endl;
		return EXIT_FAILURE;
	}

#if defined(_DEBUG) || defined(EUNOIA_D3D12_DEBUG)
	{
		ID3D12InfoQueue* infoQueue = nullptr;
		if (SUCCEEDED(g_d3dDevice->QueryInterface(IID_PPV_ARGS(&infoQueue))))
		{
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
			infoQueue->Release();
		}
	}
#endif

	// 4. Create Command Queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	hr = g_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_commandQueue));
	if (FAILED(hr))
	{
		std::cerr << "Failed to create D3D12CommandQueue: " << hr << std::endl;
		return EXIT_FAILURE;
	}
	g_commandQueue->SetName(L"g_commandQueue");

	// 5. Create SwapChain
	g_dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

	// Check tearing support (WickedEngine Variable Refresh Rate / low latency presentation)
	{
		BOOL allowTearing = FALSE;
		IDXGIFactory5* factory5 = nullptr;
		if (SUCCEEDED(g_dxgiFactory->QueryInterface(IID_PPV_ARGS(&factory5))))
		{
			HRESULT hrTear = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
			if (SUCCEEDED(hrTear) && allowTearing)
			{
				g_tearingSupported = true;
				std::cout << "[EngineRHI] Variable Refresh Rate / Tearing Supported & Enabled (WickedEngine low-latency presentation).\n";
			}
			factory5->Release();
		}
	}

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FRAME_COUNT;
	swapChainDesc.Width = (UINT)g_currentWidth;
	swapChainDesc.Height = (UINT)g_currentHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;
	if (g_tearingSupported) swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

	IDXGISwapChain1* sc = nullptr;
	hr = g_dxgiFactory->CreateSwapChainForHwnd(g_commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, &sc);
	if (FAILED(hr))
	{
		std::cerr << "Failed to create DXGI SwapChain: " << hr << std::endl;
		return EXIT_FAILURE;
	}
	sc->QueryInterface(IID_PPV_ARGS(&g_swapChain));
	sc->Release();
	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

	// 6. Create Descriptor Heaps (RTV, DSV, SRV)
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = FRAME_COUNT + 16;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	g_d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_rtvDescHeap));
	g_rtvDescriptorSize = g_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	g_rtvDescHeap->SetName(L"g_rtvDescHeap");

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 64; // Index 0: SwapChain Depth, Index 1: Directional Shadow Depth, Indices 2..25: Point Cubemap Faces, Indices 26..29: Spot/Area Slices
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	g_d3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&g_dsvDescHeap));
	g_dsvDescriptorSize = g_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	g_dsvDescHeap->SetName(L"g_dsvDescHeap");

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = SRV_HEAP_CAPACITY; // 8192
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	g_d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&g_srvDescHeap));
	g_srvDescriptorSize = g_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	g_srvDescHeap->SetName(L"g_srvDescHeap");

	// 7. Create RTVs for backbuffers
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT i = 0; i < FRAME_COUNT; i++)
	{
		g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i]));
		if (g_renderTargets[i])
		{
			std::wstring rtName = L"g_renderTarget_" + std::to_wstring(i);
			g_renderTargets[i]->SetName(rtName.c_str());
		}
		g_d3dDevice->CreateRenderTargetView(g_renderTargets[i], nullptr, rtvHandle);
		rtvHandle.ptr += g_rtvDescriptorSize;
	}

	// 8. Create Depth Stencil View, Shadow Map Resources & Scene Color Render Targets
	createDepthStencilView(g_currentWidth, g_currentHeight);
	createShadowResources();
	createSceneRenderTargets(g_currentWidth, g_currentHeight);

	// 9. Create Command Allocators & Lists (Per-frame for zero concurrency hazard)
	for (UINT i = 0; i < FRAME_COUNT; i++)
	{
		g_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_commandAllocators[i]));
		if (g_commandAllocators[i])
		{
			std::wstring caName = L"g_commandAllocator_" + std::to_wstring(i);
			g_commandAllocators[i]->SetName(caName.c_str());
		}

		g_d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_commandAllocators[i], nullptr, IID_PPV_ARGS(&g_commandLists[i]));
		if (g_commandLists[i])
		{
			std::wstring clName = L"g_commandList_" + std::to_wstring(i);
			g_commandLists[i]->SetName(clName.c_str());
			g_commandLists[i]->Close();
		}
	}
	g_commandList = g_commandLists[0];

	// 10. Create Fence & Event
	g_d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
	g_fence->SetName(L"g_fence");
	for (UINT i = 0; i < FRAME_COUNT; i++) g_fenceValues[i] = 0;
	g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	// 11. Dedicated Upload Command Allocator, Command List, and Upload Fence
	g_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_uploadCmdAlloc));
	g_uploadCmdAlloc->SetName(L"g_uploadCmdAlloc");
	g_d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_uploadCmdAlloc, nullptr, IID_PPV_ARGS(&g_uploadCmdList));
	g_uploadCmdList->SetName(L"g_uploadCmdList");
	g_uploadCmdList->Close();
	g_d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_uploadFence));
	g_uploadFence->SetName(L"g_uploadFence");
	g_uploadFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	g_uploadFenceValue = 0;

	// 11b. Initialize High-Performance DX12 Asynchronous Texture Streaming System (dev.md)
	EngineRenderer::TextureStreamingSystem::Get().Initialize(g_d3dDevice, g_commandQueue);
	EngineRenderer::TextureStreamingSystem::Get().SetDescriptorAllocator(
		g_srvDescHeap, g_srvDescriptorSize,
		[](UINT count, UINT& outIdx) { return TryAllocSrvDescriptors(count, outIdx); }
	);

	// 12. Initialize Mesh Cluster Culling Pipeline (dev.md Plugin)
	Eunoia::MeshClusterCullingSystem::Get().Initialize(g_d3dDevice, g_commandQueue, (UINT)g_currentWidth, (UINT)g_currentHeight);

	// 13. Initialize AquaTerrain DX12 Water System (dev.md Plugin)
	Eunoia::WaterSystem::Get().Initialize(g_d3dDevice, g_commandQueue, (UINT)g_currentWidth, (UINT)g_currentHeight);

	// 14. Initialize Real-Time Anti-Aliasing Pipeline (dev.md Plugin)
	createAntiAliasingPipeline();

	// 15. Initialize Volumetric Fog & God Rays Pipeline (Wicked Engine Technique)
	createGodRayPipeline();

	// 16. Initialize Real-Time Ambient Occlusion Pipeline (Wicked Engine Techniques: SSAO, HBAO, MSAO, RTAO)
	createAoPipeline();

	return EXIT_SUCCESS;
}

static HRESULT CompileOrLoadCachedShader(
	const char* hlslSource,
	const char* entryPoint,
	const char* targetProfile,
	ID3DBlob** ppBlobOut,
	ID3DBlob** ppErrorBlobOut)
{
	if (!hlslSource || !entryPoint || !targetProfile || !ppBlobOut) {
		return E_INVALIDARG;
	}

	// Compute a stable FNV-1a 64-bit hash of source + entryPoint + profile
	std::string key = std::string(entryPoint) + "_" + targetProfile + "_" + hlslSource;
	uint64_t hashVal = 14695981039346656037ull;
	for (char c : key) {
		hashVal ^= (uint8_t)c;
		hashVal *= 1099511628211ull;
	}
	char hexHash[32];
	snprintf(hexHash, sizeof(hexHash), "%016llx", (unsigned long long)hashVal);

	std::filesystem::path cacheDir = "Shaders/Cache";
	std::error_code ec;
	if (!std::filesystem::exists(cacheDir, ec)) {
		std::filesystem::create_directories(cacheDir, ec);
	}

	std::filesystem::path cacheFile = cacheDir / (std::string(entryPoint) + "_" + targetProfile + "_" + hexHash + ".cso");
	if (std::filesystem::exists(cacheFile, ec)) {
		std::ifstream f(cacheFile, std::ios::binary | std::ios::ate);
		if (f.is_open()) {
			std::streamsize size = f.tellg();
			f.seekg(0, std::ios::beg);
			if (size > 0) {
				ID3DBlob* blob = nullptr;
				HRESULT hr = D3DCreateBlob((SIZE_T)size, &blob);
				if (SUCCEEDED(hr) && blob) {
					if (f.read(reinterpret_cast<char*>(blob->GetBufferPointer()), size)) {
						*ppBlobOut = blob;
						return S_OK;
					}
					blob->Release();
				}
			}
		}
	}

	// Cache miss or read failed: compile with D3DCompile
	HRESULT hr = D3DCompile(hlslSource, strlen(hlslSource), nullptr, nullptr, nullptr, entryPoint, targetProfile, 0, 0, ppBlobOut, ppErrorBlobOut);
	if (SUCCEEDED(hr) && *ppBlobOut) {
		// Save compiled bytecode to cache file for instant future launches
		std::ofstream out(cacheFile, std::ios::binary);
		if (out.is_open()) {
			out.write(reinterpret_cast<const char*>((*ppBlobOut)->GetBufferPointer()), (*ppBlobOut)->GetBufferSize());
		}
	}
	return hr;
}

int createShadersAndPipeline()
{
	// 1. Root Signature (CBV b0, 32-bit constants b1, Descriptor Table t0-t6 for materials, Descriptor Table t7 for shadow map, Descriptor Table t8 for point shadows, Descriptor Table t9 for spot/area shadows, Samplers s0-s2)
	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 7;
	srvRange.BaseShaderRegister = 0;
	srvRange.RegisterSpace = 0;
	srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE shadowSrvRange = {};
	shadowSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	shadowSrvRange.NumDescriptors = 1;
	shadowSrvRange.BaseShaderRegister = 7;
	shadowSrvRange.RegisterSpace = 0;
	shadowSrvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE ptShadowSrvRange = {};
	ptShadowSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ptShadowSrvRange.NumDescriptors = 1;
	ptShadowSrvRange.BaseShaderRegister = 8;
	ptShadowSrvRange.RegisterSpace = 0;
	ptShadowSrvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE spotShadowSrvRange = {};
	spotShadowSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	spotShadowSrvRange.NumDescriptors = 1;
	spotShadowSrvRange.BaseShaderRegister = 9;
	spotShadowSrvRange.RegisterSpace = 0;
	spotShadowSrvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE lightmapSrvRange = {};
	lightmapSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	lightmapSrvRange.NumDescriptors = 1;
	lightmapSrvRange.BaseShaderRegister = 10;
	lightmapSrvRange.RegisterSpace = 0;
	lightmapSrvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootParams[7] = {};
	// 0: CBV b0 (Frame Constants)
	rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParams[0].Descriptor.ShaderRegister = 0;
	rootParams[0].Descriptor.RegisterSpace = 0;
	rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	// 1: 32-bit Constants b1 (32 floats Material Constants)
	rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParams[1].Constants.ShaderRegister = 1;
	rootParams[1].Constants.RegisterSpace = 0;
	rootParams[1].Constants.Num32BitValues = 32;
	rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	// 2: Descriptor Table t0-t6 (7 SRVs for Material Textures)
	rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[2].DescriptorTable.pDescriptorRanges = &srvRange;
	rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// 3: Descriptor Table t7 (1 SRV for Directional Shadow Map)
	rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[3].DescriptorTable.pDescriptorRanges = &shadowSrvRange;
	rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// 4: Descriptor Table t8 (1 SRV for Point Light Shadow Cubemap Array)
	rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[4].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[4].DescriptorTable.pDescriptorRanges = &ptShadowSrvRange;
	rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// 5: Descriptor Table t9 (1 SRV for Spot / Area Light Shadow 2D Array)
	rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[5].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[5].DescriptorTable.pDescriptorRanges = &spotShadowSrvRange;
	rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// 6: Descriptor Table t10 (1 SRV for Baked Lightmap)
	rootParams[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[6].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[6].DescriptorTable.pDescriptorRanges = &lightmapSrvRange;
	rootParams[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC staticSamplers[3] = {};
	// Sampler 0: s0 (Anisotropic wrap for materials)
	staticSamplers[0].Filter = D3D12_FILTER_ANISOTROPIC;
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].MipLODBias = 0.0f;
	staticSamplers[0].MaxAnisotropy = 16;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	staticSamplers[0].MinLOD = 0.0f;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[0].ShaderRegister = 0;
	staticSamplers[0].RegisterSpace = 0;
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// Sampler 1: s1 (Hardware PCF Comparison Sampler for 2D Directional & Spot Shadow Maps)
	staticSamplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	staticSamplers[1].MipLODBias = 0.0f;
	staticSamplers[1].MaxAnisotropy = 1;
	staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	staticSamplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	staticSamplers[1].MinLOD = 0.0f;
	staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[1].ShaderRegister = 1;
	staticSamplers[1].RegisterSpace = 0;
	staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// Sampler 2: s2 (Hardware PCF Comparison Sampler for Point Light Cube Shadows - Clamp for seamless edges)
	staticSamplers[2].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	staticSamplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[2].MipLODBias = 0.0f;
	staticSamplers[2].MaxAnisotropy = 1;
	staticSamplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	staticSamplers[2].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	staticSamplers[2].MinLOD = 0.0f;
	staticSamplers[2].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[2].ShaderRegister = 2;
	staticSamplers[2].RegisterSpace = 0;
	staticSamplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
	rootSigDesc.NumParameters = 7;
	rootSigDesc.pParameters = rootParams;
	rootSigDesc.NumStaticSamplers = 3;
	rootSigDesc.pStaticSamplers = staticSamplers;
	rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ID3DBlob* serializedRootSig = nullptr;
	ID3DBlob* errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &errorBlob);
	if (FAILED(hr))
	{
		if (errorBlob)
		{
			std::cerr << "RootSig serialization error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
			errorBlob->Release();
		}
		return EXIT_FAILURE;
	}
	g_d3dDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&g_rootSignature));
	serializedRootSig->Release();

	// 2. Compile Main HLSL Shaders with Hardware PBR, 2K Textures & Real-Time Directional, Point, Spot, Area Shadows
	const char* hlslSource = R"(
		cbuffer FrameConstants : register(b0)
		{
			float4x4 mvp;
			float4x4 lightSpaceMatrix;
			float3 cameraPos;
			float ambientIntensity;
			float3 lightDir;
			float shadowBias;
			float3 lightColor;
			float shadowStrength;
			float pcfRadius;
			float enableShadows;
			float shadowMapSize;
			float numPointLights;
			float4 ambientColor;
			float4 pointLightPosRange[64];
			float4 pointLightColorIntensity[64];
			float4 pointLightDirType[64];
			float4 pointLightSpotAreaParams[64];
			float4 pointLightShadowParams[64];
			float4x4 spotLightSpaceMatrices[4];
			float enablePostProcessing;
			float postExposureEV;
			float postExposureMultiplier;
			float postTonemapper;
			float postSaturation;
			float postContrast;
			float postGamma;
			float spotShadowAllocSize; // passed via cb.padPost for distance-adaptive viewport scaling
			float4 postColorFilter;
			float maxShadowDistance;
			float enableAO;
			float aoPower;
			float aoRange;
			float enableFog;
			float fogStart;
			float fogDensity;
			float fogHeightStart;
			float fogHeightEnd;
			float shadowSampleCount;
			float shadowPCSS;
			float enableScreenSpaceShadows;
			float4 fogColor;
			float screenSpaceShadowRange;
			float screenSpaceShadowSamples;
			float enableCapsuleShadows;
			float capsuleShadowAngle;
			float capsuleShadowFade;
			float cascadeCount;
			float cascadeBlending;
			float visualizeCascades;
			float4 cascadeSplits;
		};

		cbuffer MaterialConstants : register(b1)
		{
			float3 baseColor;
			float metallic;
			float roughness;
			float normalStrength;
			float specular;
			float emissiveIntensity;
			float3 emissiveColor;
			float isUnlit;
			float hasAlbedoTex;
			float hasNormalTex;
			float hasRoughTex;
			float hasAoTex;
			float receiveShadows;
			float2 uvScale;
			float pad0;
			float blendMode;
			float opacity;
			float opacityMaskClipValue;
			float hasOpacityTex;
			float normalMapYFlip;
			float roughnessChannel;
			float metallicChannel;
			float aoChannel;
			float materialDebugMode;
			float hasMetalTex;
			float hasEmissionTex;
			float pad3;
		};

		Texture2D g_albedoTex   : register(t0);
		Texture2D g_normalTex   : register(t1);
		Texture2D g_roughTex    : register(t2);
		Texture2D g_metalTex    : register(t3);
		Texture2D g_aoTex       : register(t4);
		Texture2D g_opacityTex  : register(t5);
		Texture2D g_emissiveTex : register(t6);
		Texture2D g_shadowMap   : register(t7);
		TextureCubeArray g_pointShadowMap : register(t8);
		Texture2DArray g_spotShadowMap : register(t9);
		Texture2D g_lightmapTex : register(t10);

		SamplerState g_sampler : register(s0);
		SamplerComparisonState g_shadowSampler : register(s1);
		SamplerComparisonState g_pointShadowSampler : register(s2);

		struct VSInput
		{
			float3 position : POSITION;
			float3 normal   : NORMAL;
			float2 uv       : TEXCOORD;
			float3 color    : COLOR;
			float4 tangent  : TANGENT;
		};

		struct PSInput
		{
			float4 position    : SV_POSITION;
			float3 worldPos    : WORLD_POS;
			float3 normal      : NORMAL;
			float2 uv          : TEXCOORD;
			float3 color       : COLOR;
			float4 shadowCoord : SHADOW_COORD;
			float4 tangent     : TANGENT;
		};

		float SamplePbrChannel(float4 smp, float chIdx)
		{
			if (chIdx < 0.5f) return smp.r;
			if (chIdx < 1.5f) return (smp.g > 0.0001f || smp.r <= 0.0001f) ? smp.g : smp.r;
			if (chIdx < 2.5f) return (smp.b > 0.0001f || smp.r <= 0.0001f) ? smp.b : smp.r;
			return smp.a;
		}

		float4 VSDepthPrepass(VSInput input) : SV_POSITION
		{
			return mul(mvp, float4(input.position, 1.0f));
		}

		PSInput VSMain(VSInput input)
		{
			PSInput output;
			output.position = mul(mvp, float4(input.position, 1.0f));
			output.worldPos = input.position;
			output.normal   = input.normal;
			output.uv       = input.uv;
			output.color    = input.color;
			output.shadowCoord = mul(lightSpaceMatrix, float4(input.position, 1.0f));
			output.tangent  = input.tangent;
			return output;
		}

		#define M_PI 3.14159265359f

		// ============================================================================
		// NVIDIA Falcor & WickedEngine Microfacet BRDF Formulation
		// Isotropic GGX Normal Distribution (NDF)
		// ============================================================================
		float D_GGX(float alpha, float NoH)
		{
			float a2 = max(alpha * alpha, 0.00001f);
			float d = ((NoH * a2 - NoH) * NoH + 1.0f);
			return a2 / (d * d * M_PI);
		}

		// Height-Correlated Smith Masking-Shadowing Function (Eric Heitz / Falcor evalMaskingSmithGGXCorrelated)
		float V_SmithGGXCorrelated(float alpha, float NoV, float NoL)
		{
			float a2 = max(alpha * alpha, 0.00001f);
			float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
			float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
			return 0.5f / max(lambdaV + lambdaL, 0.00001f);
		}

		// Schlick Fresnel with f0 and f90 (NVIDIA Falcor Fresnel.slang)
		float3 F_Schlick(float3 f0, float3 f90, float VoH)
		{
			return f0 + (f90 - f0) * pow(clamp(1.0f - VoH, 0.0f, 1.0f), 5.0f);
		}

		float3 F_Schlick(float3 f0, float VoH)
		{
			return F_Schlick(f0, float3(1.0f, 1.0f, 1.0f), VoH);
		}

		// Disney Burley Retro-Reflective Diffuse BRDF (Brent Burley 2012 / Falcor)
		float3 Diffuse_Burley(float3 albedo, float roughness, float NoV, float NoL, float VoH)
		{
			float FD90 = 0.5f + 2.0f * VoH * VoH * roughness;
			float FdV = 1.0f + (FD90 - 1.0f) * pow(clamp(1.0f - NoV, 0.0f, 1.0f), 5.0f);
			float FdL = 1.0f + (FD90 - 1.0f) * pow(clamp(1.0f - NoL, 0.0f, 1.0f), 5.0f);
			return albedo * ((FdV * FdL) / M_PI);
		}

		// NVIDIA Falcor Pre-Integrated Specular Environment Integral GGX (Ray Tracing Gems Ch. 32)
		// Multi-scattering energy compensation polynomial for photorealistic metallic & dielectric sheen
		float3 ApproxSpecularIntegralGGX(float3 specularReflectance, float alpha, float cosTheta)
		{
			cosTheta = abs(cosTheta);
			float4 X = float4(1.0f, cosTheta, cosTheta * cosTheta, cosTheta * cosTheta * cosTheta);
			float4 Y = float4(1.0f, alpha, alpha * alpha, alpha * alpha * alpha);

			float2x2 M1 = float2x2(0.995367f, -1.38839f, -0.24751f, 1.97442f);
			float3x3 M2 = float3x3(1.0f, 2.68132f, 52.366f, 16.0932f, -3.98452f, 59.3013f, -5.18731f, 255.259f, 2544.07f);
			float2x2 M3 = float2x2(-0.0564526f, 3.82901f, 16.91f, -11.0303f);
			float3x3 M4 = float3x3(1.0f, 4.11118f, -1.37886f, 19.3254f, -28.9947f, 16.9514f, 0.545386f, 96.0994f, -79.4492f);

			float bias = dot(mul(M1, X.xy), Y.xy) / max(dot(mul(M2, X.xyw), Y.xyw), 0.0001f);
			float scale = dot(mul(M3, X.xy), Y.xy) / max(dot(mul(M4, X.xzw), Y.xyw), 0.0001f);

			float luma = dot(specularReflectance, float3(0.3333f, 0.3333f, 0.3333f));
			bias *= saturate(luma * 50.0f);

			return specularReflectance * max(scale, 0.0f) + max(bias, 0.0f);
		}

		// ============================================================================
		// WickedEngine Gran Turismo 7 Uchimura Tonemap Curve (tonemapCS.hlsl)
		// ============================================================================
		float UchimuraCurve(float x, float P, float a, float m, float l, float c, float b)
		{
			float l0 = ((P - m) * l) / a;
			float S0 = m + l0;
			float S1 = m + a * l0;
			float C2 = (a * P) / max(P - S1, 0.0001f);
			float CP = -C2 / P;

			float w0 = 1.0f - smoothstep(0.0f, m, x);
			float w2 = step(m + l0, x);
			float w1 = 1.0f - w0 - w2;

			float T = m * pow(max(x / m, 0.0f), c) + b;
			float S = P - (P - S1) * exp(CP * (x - S0));
			float L = m + a * (x - m);

			return T * w0 + L * w1 + S * w2;
		}

		float3 Uchimura(float3 color)
		{
			const float P = 1.0f;
			const float a = 1.0f;
			const float m = 0.22f;
			const float l = 0.4f;
			const float c = 1.33f;
			const float b = 0.0f;
			return float3(
				UchimuraCurve(color.r, P, a, m, l, c, b),
				UchimuraCurve(color.g, P, a, m, l, c, b),
				UchimuraCurve(color.b, P, a, m, l, c, b)
			);
		}

		// ============================================================================
		// WickedEngine ACES Fitted RRT+ODT Curve (tonemapCS.hlsl)
		// ============================================================================
		float3 ACESFitted(float3 color)
		{
			static const float3x3 ACESInputMat = {
				0.59719f, 0.35458f, 0.04823f,
				0.07600f, 0.90834f, 0.01566f,
				0.02840f, 0.13383f, 0.83777f
			};
			static const float3x3 ACESOutputMat = {
				 1.60475f, -0.53108f, -0.07367f,
				-0.10208f,  1.10813f, -0.00605f,
				-0.00327f, -0.07276f,  1.07602f
			};

			color = mul(ACESInputMat, color);
			color = clamp(color, 0.0f, 100.0f);
			float3 a = color * (color + 0.0245786f) - 0.000090537f;
			float3 b = color * (0.983729f * color + 0.4329510f) + 0.238081f;
			color = a / b;
			color = mul(ACESOutputMat, color);
			return saturate(color);
		}

		// ============================================================================
		// NVIDIA Falcor & Hable Uncharted 2 Filmic Tonemapper (ToneMapping.ps.slang)
		// ============================================================================
		float3 Uncharted2Tonemap(float3 x)
		{
			const float A = 0.22f; // Shoulder Strength
			const float B = 0.30f; // Linear Strength
			const float C = 0.10f; // Linear Angle
			const float D = 0.20f; // Toe Strength
			const float E = 0.01f; // Toe Numerator
			const float F = 0.30f; // Toe Denominator
			return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - (E / F);
		}

		float3 ToneMapHable(float3 color)
		{
			float exposureBias = 2.0f;
			float3 curr = Uncharted2Tonemap(exposureBias * color);
			float3 whiteScale = 1.0f / Uncharted2Tonemap(float3(11.2f, 11.2f, 11.2f));
			return curr * whiteScale;
		}

		// ================================================================
		// WickedEngine Vogel Disk Sampling Patterns (shadowHF.hlsli)
		// Remapped via: baked_value * 2 - 1 from PoissonGenerator.h
		// ================================================================
		static const float2 vogel_points4[4] = {
			float2(0.353553f, 0.000000f),
			float2(-0.451560f, 0.413635f),
			float2(0.069174f, -0.787537f),
			float2(0.569060f, 0.742409f)
		};

		static const float2 vogel_points8[8] = {
			float2(0.25000000f, 0.00000000f),
			float2(-0.31930089f, 0.29248416f),
			float2(0.04891348f, -0.55687296f),
			float2(0.40238643f, 0.52496207f),
			float2(-0.73851585f, -0.13074535f),
			float2(0.69968677f, -0.44490278f),
			float2(-0.23419666f, 0.87043202f),
			float2(-0.44604915f, -0.85938364f)
		};

		static const float2 vogel_points16[16] = {
			float2(0.17677665f, 0.00000000f),
			float2(-0.22577983f, 0.20681751f),
			float2(0.03458714f, -0.39376867f),
			float2(0.28453016f, 0.37120426f),
			float2(-0.52220953f, -0.09245092f),
			float2(0.49475324f, -0.31459379f),
			float2(-0.16560209f, 0.61548841f),
			float2(-0.31540442f, -0.60767603f),
			float2(0.68456841f, 0.25023210f),
			float2(-0.71235347f, 0.29377294f),
			float2(0.34362423f, -0.73360229f),
			float2(0.25340176f, 0.80903494f),
			float2(-0.76454973f, -0.44352412f),
			float2(0.89722824f, -0.19680285f),
			float2(-0.54790950f, 0.77848911f),
			float2(-0.12594837f, -0.97615927f)
		};

		static const float2 vogel_points32[32] = {
			float2(0.12500000f, 0.00000000f),
			float2(-0.15965044f, 0.14624202f),
			float2(0.02445674f, -0.27843648f),
			float2(0.20119321f, 0.26248097f),
			float2(-0.36925793f, -0.06537271f),
			float2(0.34984338f, -0.22245139f),
			float2(-0.11709833f, 0.43521595f),
			float2(-0.22302461f, -0.42969179f),
			float2(0.48406291f, 0.17694080f),
			float2(-0.50370997f, 0.20772886f),
			float2(0.24297905f, -0.51873517f),
			float2(0.17918217f, 0.57207417f),
			float2(-0.54061830f, -0.31361890f),
			float2(0.63443625f, -0.13916063f),
			float2(-0.38743055f, 0.55047488f),
			float2(-0.08905894f, -0.69024885f),
			float2(0.54879880f, 0.46308208f),
			float2(-0.73889750f, 0.03009081f),
			float2(0.53931046f, -0.53597510f),
			float2(-0.03660476f, 0.77976608f),
			float2(-0.51236552f, -0.61490381f),
			float2(0.81227481f, 0.10993028f),
			float2(-0.68869931f, 0.47834957f),
			float2(0.18879366f, -0.83590192f),
			float2(0.43436146f, 0.75957572f),
			float2(-0.85019755f, -0.27210116f),
			float2(0.82646751f, -0.38088906f),
			float2(-0.35873961f, 0.85479879f),
			float2(-0.31848884f, -0.88836360f),
			float2(0.84942913f, 0.44759941f),
			float2(-0.94430852f, 0.24780297f),
			float2(0.53754807f, -0.83391672f)
		};

		static const float2 poissonDisk16[16] = {
			float2(0.17677665f, 0.00000000f),
			float2(-0.22577983f, 0.20681751f),
			float2(0.03458714f, -0.39376867f),
			float2(0.28453016f, 0.37120426f),
			float2(-0.52220953f, -0.09245092f),
			float2(0.49475324f, -0.31459379f),
			float2(-0.16560209f, 0.61548841f),
			float2(-0.31540442f, -0.60767603f),
			float2(0.68456841f, 0.25023210f),
			float2(-0.71235347f, 0.29377294f),
			float2(0.34362423f, -0.73360229f),
			float2(0.25340176f, 0.80903494f),
			float2(-0.76454973f, -0.44352412f),
			float2(0.89722824f, -0.19680285f),
			float2(-0.54790950f, 0.77848911f),
			float2(-0.12594837f, -0.97615927f)
		};

		// ----------------------------------------------------------------
		// Directional Light Shadows (WickedEngine shadow_2D & PCSS in shadowHF.hlsli)
		// ----------------------------------------------------------------
		float CalculateShadow(float3 worldPos, float3 N, float3 L)
		{
			if (enableShadows < 0.5f || receiveShadows < 0.5f) return 1.0f;

			// Distance range culling: Only shadows within maxShadowDistance are rendered and shown
			float distToCam = length(worldPos - cameraPos);
			if (distToCam >= maxShadowDistance) return 1.0f;

			float cosTheta = saturate(dot(N, L));
			float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));

			float lightScale = length(lightSpaceMatrix[0].xyz);
			float texelSizeWorld = (lightScale > 0.00001f) ? (2.0f / (max(shadowMapSize, 128.0f) * lightScale)) : 0.02f;

			// Precision normal offset bias to prevent self-shadowing without detaching dynamic character shadows
			float normalBias = texelSizeWorld * (0.6f * sinTheta + 0.1f);
			float3 biasedWorldPos = worldPos + N * normalBias;

			float4 shadowCoord = mul(lightSpaceMatrix, float4(biasedWorldPos, 1.0f));
			float3 projCoords = shadowCoord.xyz / shadowCoord.w;

			if (projCoords.z > 1.0f || projCoords.z < 0.0f ||
				projCoords.x < -1.0f || projCoords.x > 1.0f ||
				projCoords.y < -1.0f || projCoords.y > 1.0f)
			{
				return 1.0f;
			}

			float2 shadowUV;
			shadowUV.x = projCoords.x * 0.5f + 0.5f;
			shadowUV.y = -projCoords.y * 0.5f + 0.5f;

			float borderDist = min(min(shadowUV.x, 1.0f - shadowUV.x), min(shadowUV.y, 1.0f - shadowUV.y));
			if (borderDist <= 0.0f) return 1.0f;
			float fade = saturate(borderDist * 10.0f);

			float zBorderDist = min(projCoords.z, 1.0f - projCoords.z);
			fade = min(fade, saturate(zBorderDist * 10.0f));

			float depthBias = max(shadowBias * (1.0f - cosTheta), shadowBias * 0.15f);
			float currentDepth = projCoords.z - depthBias;

			// Soft shadow spread calculation matching WickedEngine: inv_res * mad(radius, 8, 2)
			float spread = (1.0f / max(shadowMapSize, 128.0f)) * (max(pcfRadius, 0.2f) * 8.0f + 2.0f) * 0.25f;

			// Percentage-Closer Soft Shadows (PCSS) blocker search (WickedEngine SHADOW_SAMPLING_PCSS)
			if (shadowPCSS > 0.5f)
			{
				float blockerCount = 0.0f;
				float blockerSum = 0.0f;
				const float search = 2.0f;
				[unroll]
				for (float bx = -search; bx <= search; bx += 2.0f)
				{
					for (float by = -search; by <= search; by += 2.0f)
					{
						float2 bUV = clamp(shadowUV + spread * float2(bx, by) * 2.0f, float2(0.001f, 0.001f), float2(0.999f, 0.999f));
						float d = g_shadowMap.SampleLevel(g_sampler, bUV, 0).r;
						if (d < currentDepth)
						{
							blockerCount += 1.0f;
							blockerSum += d;
						}
					}
				}
				if (blockerCount > 0.0f)
				{
					float avgBlocker = blockerSum / blockerCount;
					float penumbra = abs(currentDepth - avgBlocker) * 150.0f;
					spread = clamp(spread * penumbra, (1.0f / max(shadowMapSize, 128.0f)) * 0.75f, spread * 3.5f);
				}
			}

			// Vogel Disk PCF Sampling
			float shadow = 0.0f;
			if (shadowSampleCount <= 4.5f)
			{
				[unroll]
				for (int i = 0; i < 4; ++i)
				{
					float2 offset = vogel_points4[i] * spread;
					shadow += g_shadowMap.SampleCmpLevelZero(g_shadowSampler, shadowUV + offset, currentDepth);
				}
				shadow /= 4.0f;
			}
			else if (shadowSampleCount <= 8.5f)
			{
				[unroll]
				for (int i = 0; i < 8; ++i)
				{
					float2 offset = vogel_points8[i] * spread;
					shadow += g_shadowMap.SampleCmpLevelZero(g_shadowSampler, shadowUV + offset, currentDepth);
				}
				shadow /= 8.0f;
			}
			else if (shadowSampleCount >= 24.0f)
			{
				[unroll]
				for (int i = 0; i < 32; ++i)
				{
					float2 offset = vogel_points32[i] * spread;
					shadow += g_shadowMap.SampleCmpLevelZero(g_shadowSampler, shadowUV + offset, currentDepth);
				}
				shadow /= 32.0f;
			}
			else
			{
				[unroll]
				for (int i = 0; i < 16; ++i)
				{
					float2 offset = vogel_points16[i] * spread;
					shadow += g_shadowMap.SampleCmpLevelZero(g_shadowSampler, shadowUV + offset, currentDepth);
				}
				shadow /= 16.0f;
			}

			float rawShadow = lerp(1.0f - shadowStrength, 1.0f, shadow);
			float finalShadow = lerp(1.0f, rawShadow, fade);

			// Smooth edge fade as camera distance approaches maxShadowDistance (outer 15% range)
			float distFade = 1.0f;
			float fadeStart = maxShadowDistance * 0.85f;
			if (distToCam > fadeStart)
			{
				distFade = 1.0f - saturate((distToCam - fadeStart) / max(maxShadowDistance - fadeStart, 0.001f));
			}
			return lerp(1.0f, finalShadow, distFade);
		}

		// ----------------------------------------------------------------
		// Screen Space Shadows / Contact Shadows (WickedEngine screenspaceshadowCS.hlsl)
		// ----------------------------------------------------------------
		float CalculateContactShadow(float3 worldPos, float3 N, float3 L)
		{
			if (enableScreenSpaceShadows < 0.5f || enableShadows < 0.5f || receiveShadows < 0.5f) return 1.0f;

			float distToCam = length(worldPos - cameraPos);
			if (distToCam >= maxShadowDistance) return 1.0f;

			float numSteps = clamp(screenSpaceShadowSamples, 4.0f, 64.0f);
			float stepSize = screenSpaceShadowRange / numSteps;
			float3 rayPos = worldPos + N * 0.02f;
			float occlusion = 0.0f;

			[loop]
			for (int s = 1; s <= 32; ++s)
			{
				if (float(s) > numSteps) break;
				rayPos += L * stepSize;

				float4 clipPos = mul(mvp, float4(rayPos, 1.0f));
				float3 ndc = clipPos.xyz / max(clipPos.w, 0.0001f);
				float2 scUV = float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
				if (scUV.x < 0.0f || scUV.x > 1.0f || scUV.y < 0.0f || scUV.y > 1.0f || ndc.z < 0.0f || ndc.z > 1.0f)
					break;

				float4 scCoord = mul(lightSpaceMatrix, float4(rayPos, 1.0f));
				float3 scProj = scCoord.xyz / max(scCoord.w, 0.0001f);
				float2 smUV = float2(scProj.x * 0.5f + 0.5f, -scProj.y * 0.5f + 0.5f);
				if (smUV.x >= 0.0f && smUV.x <= 1.0f && smUV.y >= 0.0f && smUV.y <= 1.0f)
				{
					float smZ = g_shadowMap.SampleLevel(g_sampler, smUV, 0).r;
					float deltaZ = scProj.z - smZ;
					if (deltaZ > 0.0012f && deltaZ < 0.035f)
					{
						occlusion = 1.0f - pow(float(s) / numSteps, 2.0f);
						float2 fade = max(12.0f * abs(scUV - 0.5f) - 5.0f, 0.0f);
						occlusion *= saturate(1.0f - dot(fade, fade));
						break;
					}
				}
			}
			return saturate(1.0f - occlusion * shadowStrength);
		}

		// ----------------------------------------------------------------
		// Capsule Shadows (WickedEngine capsuleShadowHF.hlsli)
		// ----------------------------------------------------------------
		float CalculateCapsuleShadow(float3 worldPos, float3 N)
		{
			if (enableCapsuleShadows < 0.5f) return 1.0f;

			float coneAngleRad = capsuleShadowAngle * 0.0174532925f;
			float cosCone = cos(coneAngleRad);

			float3 groundPt = float3(worldPos.x, 0.0f, worldPos.z);
			float3 toGround = groundPt - worldPos;
			float dist = length(toGround);
			if (dist < 4.0f && dist > 0.01f)
			{
				float capRadius = 0.40f;
				float cosTheta = sqrt((dist * dist) / (capRadius * capRadius + dist * dist));
				float cosPhi = dot(-N, normalize(toGround));
				float occ = saturate(1.0f - max(0.0f, cosPhi) * (1.0f - cosTheta) / max(1.0f - cosCone, 0.001f));
				return lerp(occ, 1.0f, capsuleShadowFade);
			}
			return 1.0f;
		}

		static const float3 pointShadowOffsets[16] = {
			float3( 0.577f,  0.577f,  0.577f), float3(-0.577f,  0.577f,  0.577f),
			float3( 0.577f, -0.577f,  0.577f), float3(-0.577f, -0.577f,  0.577f),
			float3( 0.577f,  0.577f, -0.577f), float3(-0.577f,  0.577f, -0.577f),
			float3( 0.577f, -0.577f, -0.577f), float3(-0.577f, -0.577f, -0.577f),
			float3( 1.0f,  0.0f,  0.0f), float3(-1.0f,  0.0f,  0.0f),
			float3( 0.0f,  1.0f,  0.0f), float3( 0.0f, -1.0f,  0.0f),
			float3( 0.0f,  0.0f,  1.0f), float3( 0.0f,  0.0f, -1.0f),
			float3( 0.707f,  0.707f,  0.0f), float3(-0.707f, -0.707f,  0.0f)
		};

		// ----------------------------------------------------------------
		// Point Light Shadows (Cube Shadow Maps - WickedEngine shadow_cube in shadowHF.hlsli)
		// ----------------------------------------------------------------
		float CalculatePointShadow(int shadowIdx, float3 worldPos, float3 pPos, float pRange, float3 N, float shadowRes, float sBias, float sStrength)
		{
			if (enableShadows < 0.5f || receiveShadows < 0.5f) return 1.0f;

			float distToCam = length(worldPos - cameraPos);
			if (distToCam >= maxShadowDistance) return 1.0f;

			float3 toFrag = worldPos - pPos;
			float currentDist = length(toFrag);
			if (currentDist >= pRange || currentDist <= 0.001f) return 1.0f;

			float3 L = -toFrag / currentDist;
			float cosTheta = saturate(dot(N, L));

			float worldBias = max(sBias * 20.0f * (1.0f - cosTheta), sBias * 5.0f);

			float absZ = max(abs(toFrag.x), max(abs(toFrag.y), abs(toFrag.z)));
			float nearZ = 0.05f;
			float farZ = max(pRange, 5.0f);

			float biasedZ = max(absZ - worldBias, nearZ);
			if (biasedZ >= farZ) return 1.0f;

			float refDepth = saturate((farZ / (farZ - nearZ)) - (farZ * nearZ / (farZ - nearZ)) / max(biasedZ, 0.0001f));

			float filterScale = 512.0f / max(shadowRes, 128.0f);
			float diskRadius = (0.008f + 0.020f * saturate(currentDist / farZ)) * (max(pcfRadius, 0.2f) * 8.0f + 2.0f) * 0.125f * filterScale;

			float shadow = 0.0f;
			[unroll]
			for (int k = 0; k < 16; ++k)
			{
				float3 sampleDir = toFrag + pointShadowOffsets[k] * diskRadius;
				shadow += g_pointShadowMap.SampleCmpLevelZero(g_pointShadowSampler, float4(sampleDir, (float)shadowIdx), refDepth);
			}
			shadow /= 16.0f;

			float distFade = saturate((pRange - currentDist) / max(pRange * 0.1f, 0.5f));
			float finalShadow = lerp(1.0f - sStrength, 1.0f, shadow);
			return lerp(1.0f, finalShadow, distFade);
		}

		// ----------------------------------------------------------------
		// Spot Light Shadows (WickedEngine shadow_2D with distance-adaptive scaling)
		// ----------------------------------------------------------------
		float CalculateSpotShadow(int shadowIdx, float3 worldPos, float3 N, float3 L, float4x4 spotMatrix, float shadowRes, float sBias, float sStrength)
		{
			if (enableShadows < 0.5f || receiveShadows < 0.5f) return 1.0f;

			float distToCam = length(worldPos - cameraPos);
			if (distToCam >= maxShadowDistance) return 1.0f;

			float cosTheta = saturate(dot(N, L));
			float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));

			float safeAllocRes = max(spotShadowAllocSize, 256.0f);
			float safeShadowRes = max(shadowRes, 128.0f);

			float texelSize = 1.0f / safeShadowRes;
			float normalBias = texelSize * (0.8f * sinTheta + 0.2f);
			float3 biasedWorldPos = worldPos + N * normalBias;

			float4 shadowCoord = mul(spotMatrix, float4(biasedWorldPos, 1.0f));
			float3 projCoords = shadowCoord.xyz / shadowCoord.w;

			if (projCoords.z > 1.0f || projCoords.z < 0.0f ||
				projCoords.x < -1.0f || projCoords.x > 1.0f ||
				projCoords.y < -1.0f || projCoords.y > 1.0f)
			{
				return 1.0f;
			}

			float2 shadowUV;
			shadowUV.x = projCoords.x * 0.5f + 0.5f;
			shadowUV.y = -projCoords.y * 0.5f + 0.5f;

			float uvScale = saturate(safeShadowRes / safeAllocRes);
			float2 scaledUV = shadowUV * uvScale;

			float depthBias = max(sBias * (1.0f - cosTheta), sBias * 0.15f);
			float currentDepth = projCoords.z - depthBias;

			float filterRadius = (1.0f / safeAllocRes) * (max(pcfRadius, 0.2f) * 8.0f + 2.0f) * 0.25f;
			float shadow = 0.0f;
			[unroll]
			for (int k = 0; k < 16; ++k)
			{
				float2 offset = vogel_points16[k] * filterRadius;
				float2 sampleUV = clamp(scaledUV + offset, float2(0.0005f, 0.0005f), float2(uvScale - 0.0005f, uvScale - 0.0005f));
				shadow += g_spotShadowMap.SampleCmpLevelZero(g_shadowSampler, float3(sampleUV, (float)shadowIdx), currentDepth);
			}
			shadow /= 16.0f;

			return lerp(1.0f - sStrength, 1.0f, shadow);
		}
		float4 PSMain(PSInput input) : SV_TARGET
		{
			float2 uv = input.uv * uvScale;

			float4 albedoSample = float4(baseColor, 1.0f);
			if (hasAlbedoTex > 0.5f)
			{
				albedoSample = g_albedoTex.Sample(g_sampler, uv);
			}

			float currentOpacity = opacity;
			if (pad0 > 0.5f)
			{
				currentOpacity *= saturate(input.color.r);
			}
			if (hasOpacityTex > 0.5f)
			{
				currentOpacity *= g_opacityTex.Sample(g_sampler, uv).r;
			}
			else if (hasAlbedoTex > 0.5f)
			{
				currentOpacity *= albedoSample.a;
			}

			if (blendMode > 0.5f && blendMode < 1.5f)
			{
				clip(currentOpacity - opacityMaskClipValue);
			}

			float3 V = normalize(cameraPos - input.worldPos);
			float3 L = normalize(lightDir);
			float3 H = normalize(L + V);

			float3 N = normalize(input.normal);
			float3 T = normalize(input.tangent.xyz);
			T = normalize(T - dot(T, N) * N);
			float3 B = normalize(cross(N, T) * input.tangent.w);
			float3x3 TBN = float3x3(T, B, N);

			if (hasNormalTex > 0.5f && normalStrength > 0.01f)
			{
				float4 rawNorm = g_normalTex.Sample(g_sampler, uv);
				float2 nXY = rawNorm.xy * 2.0f - 1.0f;
				if (normalMapYFlip > 0.5f)
				{
					nXY.y = -nXY.y;
				}
				nXY *= normalStrength;
				// Reconstruct Z for 2-channel BC5/RG normal maps where B is 0,
				// or decode 3-channel RGB normal maps where B is in [0, 1] range.
				float nZ = (rawNorm.z > 0.01f) ? (rawNorm.z * 2.0f - 1.0f) : sqrt(saturate(1.0f - dot(nXY, nXY)));
				float3 nSample = normalize(float3(nXY, max(nZ, 0.001f)));
				N = normalize(mul(nSample, TBN));
			}

			float3 albedo = (hasAlbedoTex > 0.5f) ? (albedoSample.rgb * baseColor) : baseColor;

			float rough = roughness;
			if (hasRoughTex > 0.5f)
			{
				float4 rSamp = g_roughTex.Sample(g_sampler, uv);
				rough = SamplePbrChannel(rSamp, roughnessChannel);
			}
			rough = clamp(rough, 0.04f, 1.0f);

			float metal = metallic;
			if (hasMetalTex > 0.5f)
			{
				float4 mSamp = g_metalTex.Sample(g_sampler, uv);
				metal = clamp(SamplePbrChannel(mSamp, metallicChannel), 0.0f, 1.0f);
			}

			float ao = 1.0f;
			if (hasAoTex > 0.5f)
			{
				float4 aoSamp = g_aoTex.Sample(g_sampler, uv);
				ao = clamp(SamplePbrChannel(aoSamp, aoChannel), 0.05f, 1.0f);
			}

			// Screen Space Ambient Occlusion (WickedEngine Reference: pow(saturate(1 - occ), power))
			if (enableAO > 0.5f)
			{
				float occCombined = 0.0f;

				// 1. Multi-tap Horizon & Contact Occlusion using scene depth map (g_shadowMap)
				float4 scCoord = mul(lightSpaceMatrix, float4(input.worldPos, 1.0f));
				float3 scProj = scCoord.xyz / max(scCoord.w, 0.0001f);
				float2 scUV = float2(scProj.x * 0.5f + 0.5f, -scProj.y * 0.5f + 0.5f);
				float currentZ = scProj.z;

				float contactOcc = 0.0f;
				if (scProj.z >= 0.0f && scProj.z <= 1.0f &&
					scUV.x >= 0.005f && scUV.x <= 0.995f &&
					scUV.y >= 0.005f && scUV.y <= 0.995f)
				{
					float aoRadius = clamp(aoRange * 0.0040f, 0.001f, 0.035f);
					float maxDepthDelta = clamp(0.045f * aoRange, 0.005f, 0.25f);
					float selfBias = 0.0006f;

					[unroll]
					for (int tap = 0; tap < 16; ++tap)
					{
						float2 sampleUV = scUV + poissonDisk16[tap] * aoRadius;
						float sampleZ = g_shadowMap.SampleLevel(g_sampler, sampleUV, 0).r;
						float deltaZ = currentZ - sampleZ;
						if (deltaZ > selfBias && deltaZ < maxDepthDelta)
						{
							float weight = 1.0f - (deltaZ / maxDepthDelta);
							contactOcc += weight * weight;
						}
					}
					contactOcc = saturate(contactOcc / 16.0f);
				}

				// 2. Geometric Cavity & Hemispherical Ambient Sky Occlusion
				float skyDome = saturate(N.y * 0.5f + 0.5f);
				float normalCurvature = saturate(length(fwidth(N)) * 3.5f);
				float grazingCavity = pow(saturate(1.0f - abs(dot(N, V))), 2.5f);
				float geomOcc = saturate((1.0f - skyDome) * 0.35f + normalCurvature * 0.45f + grazingCavity * 0.20f);

				// Combine contact occlusion and geometric cavity
				occCombined = saturate(contactOcc * 1.5f + geomOcc * 0.5f);
				occCombined = saturate(occCombined * clamp(aoRange, 0.25f, 3.0f));

				// WickedEngine exponential power shaping: pow(saturate(1.0 - occ), power)
				float ssao = pow(saturate(1.0f - occCombined), max(aoPower, 0.25f));
				ao *= ssao;
			}

			// Diagnostic Material Debug Modes (dev.md Section 11 & 15)
			if (materialDebugMode > 0.5f && materialDebugMode < 1.5f) // 1: Base Color Only
			{
				return float4(pow(saturate(albedo), 1.0f / 2.2f), currentOpacity);
			}
			else if (materialDebugMode > 1.5f && materialDebugMode < 2.5f) // 2: Normal Visualization
			{
				return float4(N * 0.5f + 0.5f, currentOpacity);
			}
			else if (materialDebugMode > 2.5f && materialDebugMode < 3.5f) // 3: Roughness
			{
				return float4(rough, rough, rough, currentOpacity);
			}
			else if (materialDebugMode > 3.5f && materialDebugMode < 4.5f) // 4: Metallic
			{
				return float4(metal, metal, metal, currentOpacity);
			}
			else if (materialDebugMode > 4.5f && materialDebugMode < 5.5f) // 5: AO
			{
				return float4(ao, ao, ao, currentOpacity);
			}
			else if (materialDebugMode > 5.5f && materialDebugMode < 6.5f) // 6: Tangent
			{
				return float4(T * 0.5f + 0.5f, currentOpacity);
			}
			else if (materialDebugMode > 6.5f && materialDebugMode < 7.5f) // 7: Bitangent
			{
				return float4(B * 0.5f + 0.5f, currentOpacity);
			}
			else if (materialDebugMode > 7.5f && materialDebugMode < 8.5f) // 8: Vertex Normal
			{
				float3 vN = normalize(input.normal);
				return float4(vN * 0.5f + 0.5f, currentOpacity);
			}
			else if (materialDebugMode > 8.5f && materialDebugMode < 9.5f) // 9: UV0
			{
				return float4(frac(uv.x), frac(uv.y), 0.0f, currentOpacity);
			}
			else if (materialDebugMode > 9.5f && materialDebugMode < 10.5f) // 10: Simple Diffuse (diagnostic)
			{
				float3 vN = normalize(input.normal);
				float3 simpleDiff = albedo * (max(dot(vN, L), 0.0f) * lightColor + ambientColor.rgb * ambientIntensity);
				return float4(pow(saturate(simpleDiff), 1.0f / 2.2f), currentOpacity);
			}

			float3 emissive = emissiveColor * emissiveIntensity;
			if (hasEmissionTex > 0.5f)
			{
				float4 emSample = g_emissiveTex.Sample(g_sampler, uv);
				emissive = emSample.rgb * emissiveColor * max(emissiveIntensity, 1.0f);
			}

			if (isUnlit > 0.5f)
			{
				float3 unlitBase = (length(input.color) > 0.001f && hasAlbedoTex < 0.5f) ? (input.color * albedo) : albedo;
				return float4(pow(saturate(unlitBase + emissive), 1.0f / 2.2f), currentOpacity);
			}

			float NdotL = saturate(dot(N, L));
			float NdotV = max(dot(N, V), 0.0001f);
			float NdotH = saturate(dot(N, H));
			float VdotH = saturate(dot(V, H));

			float3 F0 = lerp(float3(0.04f * specular * 2.0f, 0.04f * specular * 2.0f, 0.04f * specular * 2.0f), albedo, metal);
			float3 F = F_Schlick(F0, VdotH);

			float roughnessBRDF = rough * rough;
			float D = D_GGX(roughnessBRDF, NdotH);
			float Vis = V_SmithGGXCorrelated(roughnessBRDF, NdotV, NdotL);
			float3 specBRDF = D * Vis * F;
			// NVIDIA Falcor & Disney Retro-Reflective Diffuse BRDF
			float3 diffBRDF = (1.0f - F) * (1.0f - metal) * Diffuse_Burley(albedo, rough, NdotV, NdotL, VdotH);

			// Directional Sun Light with Shadows (WickedEngine Cascade & Vogel Disk PCF)
			float shadowFactor = CalculateShadow(input.worldPos, N, L);
			float contactShadow = CalculateContactShadow(input.worldPos, N, L);
			shadowFactor = min(shadowFactor, contactShadow);
			float capShadow = CalculateCapsuleShadow(input.worldPos, N);
			shadowFactor *= capShadow;

			float3 directLit = (diffBRDF + specBRDF) * NdotL * lightColor * shadowFactor;

			// Cascade Visualizer (Color Overlay)
			if (visualizeCascades > 0.5f)
			{
				float distToCam = length(input.worldPos - cameraPos);
				float3 cascadeColor = float3(1.0f, 1.0f, 1.0f);
				if (distToCam < cascadeSplits.x) {
					cascadeColor = float3(1.35f, 0.65f, 0.65f); // Cascade 1: Red
				} else if (distToCam < cascadeSplits.y) {
					cascadeColor = float3(0.65f, 1.35f, 0.65f); // Cascade 2: Green
				} else if (distToCam < cascadeSplits.z) {
					cascadeColor = float3(0.65f, 0.65f, 1.35f); // Cascade 3: Blue
				} else {
					cascadeColor = float3(1.35f, 1.35f, 0.60f); // Cascade 4: Yellow
				}
				directLit *= cascadeColor;
			}

			// Cavity & contact ambient occlusion darkening on direct lighting (WickedEngine & UE5 reference)
			float directAO = lerp(ao, 1.0f, saturate(NdotL * shadowFactor * 0.65f + 0.35f));
			directLit *= directAO;

			// Ambient & Indirect Lighting with Falcor Multi-Scattering Specular Energy Preservation
			float3 ambCol = ambientColor.rgb;
			float effectiveAmbIntensity = ambientIntensity;
			float3 ambientDiff = albedo * ambCol * effectiveAmbIntensity * (1.0f - metal) * ao;
			float3 ambSpecEnergy = ApproxSpecularIntegralGGX(F0, roughnessBRDF, NdotV);
			float3 ambientSpec = ambSpecEnergy * ambCol * effectiveAmbIntensity * ao;

			// Multiple Lights (Point, Spot, Area) - WickedEngine Forward+ Lighting & Falcor Disney Diffuse
			float3 pointLightsContribution = float3(0, 0, 0);
			int numLights = min((int)numPointLights, 64);
			[loop]
			for (int i = 0; i < numLights; ++i)
			{
				float3 pPos = pointLightPosRange[i].xyz;
				float pRange = pointLightPosRange[i].w;
				float3 pCol = pointLightColorIntensity[i].xyz;
				float pIntensity = pointLightColorIntensity[i].w;
				float3 lDir = pointLightDirType[i].xyz;
				int lType = (int)(pointLightDirType[i].w + 0.5f);
				float4 spParams = pointLightSpotAreaParams[i];
				float4 shParams = pointLightShadowParams[i];
				int shadowIdx = (int)shParams.x;
				float sStrength = shParams.y;
				float sBias = shParams.z;
				float shadowRes = shParams.w;

				float3 toLight = pPos - input.worldPos;
				float dist = length(toLight);
				if (dist < pRange && dist > 0.0001f)
				{
					float3 pL = toLight / dist;
					float3 pH = normalize(pL + V);
					float pNdotL = saturate(dot(N, pL));
					float pNdotH = saturate(dot(N, pH));
					float pVdotH = saturate(dot(V, pH));

					float attenExp = max(spParams.z, 0.2f);
					float normDist = dist / max(pRange, 0.0001f);
					float distAtten = saturate(1.0f - normDist);
					distAtten = pow(distAtten, attenExp) / (dist * dist + 1.0f);

					float spotAngleAtten = 1.0f;
					if (lType == 2) // Spot Light
					{
						float cosDir = dot(normalize(lDir), -pL);
						float cosInner = spParams.x;
						float cosOuter = spParams.y;
						spotAngleAtten = saturate((cosDir - cosOuter) / max(cosInner - cosOuter, 0.001f));
						spotAngleAtten = spotAngleAtten * spotAngleAtten;

						// Smooth range ending dissolve
						float rangeSmooth = clamp(spParams.w, 0.001f, 1.0f);
						float rangeDissolve = saturate((1.0f - normDist) / rangeSmooth);
						rangeDissolve = rangeDissolve * rangeDissolve * (3.0f - 2.0f * rangeDissolve);
						distAtten *= rangeDissolve;
					}
					else if (lType == 3) // Area Light
					{
						float twoSided = spParams.w;
						float forwardDot = dot(normalize(lDir), -pL);
						if (twoSided < 0.5f && forwardDot <= 0.0f)
						{
							spotAngleAtten = 0.0f;
						}
						else
						{
							spotAngleAtten = saturate(abs(forwardDot) * 1.2f);
						}
					}

					if (distAtten * spotAngleAtten > 0.00001f)
					{
						float3 pF = F_Schlick(F0, pVdotH);
						float pD = D_GGX(roughnessBRDF, pNdotH);
						float pVis = V_SmithGGXCorrelated(roughnessBRDF, NdotV, pNdotL);
						float3 pSpec = pD * pVis * pF;
						float3 pDiff = (1.0f - pF) * (1.0f - metal) * Diffuse_Burley(albedo, rough, NdotV, pNdotL, pVdotH);

						float pShadowFactor = 1.0f;
						if (enableShadows > 0.5f && shadowIdx >= 0 && shadowIdx < 4 && receiveShadows > 0.5f)
						{
							if (lType == 1) // Point Light
							{
								pShadowFactor = CalculatePointShadow(shadowIdx, input.worldPos, pPos, pRange, N, shadowRes, sBias, sStrength);
							}
							else if (lType == 2 || lType == 3) // Spot or Area Light
							{
								float4x4 sMatrix = spotLightSpaceMatrices[shadowIdx];
								pShadowFactor = CalculateSpotShadow(shadowIdx, input.worldPos, N, pL, sMatrix, shadowRes, sBias, sStrength);
							}
						}

						pointLightsContribution += (pDiff + pSpec) * pNdotL * pCol * pIntensity * distAtten * spotAngleAtten * pShadowFactor;
					}
				}
			}
			pointLightsContribution *= lerp(ao, 1.0f, 0.25f);

			// Lightmap: input.color.xy = lightmap UVs, input.color.z = hasLightmap flag (1.0 = yes)
			float hasLightmap = input.color.z;
			float3 bakedLit = float3(0.0f, 0.0f, 0.0f);
			if (hasLightmap > 0.5f)
			{
				float3 bakedIllum = g_lightmapTex.Sample(g_sampler, input.color.xy).rgb;
				float3 pkD = (1.0f - 0.04f) * (1.0f - metal);
				bakedLit = pkD * albedo * bakedIllum * ao;
			}
			else if (length(input.color.rgb) > 0.0001f && pad0 < 0.5f)
			{
				float3 pkD = (1.0f - 0.04f) * (1.0f - metal);
				bakedLit = pkD * albedo * input.color.rgb * ao;
			}
			float3 litColor = ambientDiff + ambientSpec + bakedLit + directLit + pointLightsContribution + emissive;

			// Wicked Engine Volumetric Height Fog (fogHF.hlsli)
			if (enableFog > 0.5f)
			{
				float dist = length(input.worldPos - cameraPos);
				float startDist = max(fogStart, 0.001f);
				float startDistanceFalloff = saturate((dist - startDist) / startDist);
				float fogFalloffScale = 1.0f / max(0.01f, fogHeightEnd - fogHeightStart);
				float fogFalloff = 6.907755f * fogFalloffScale;

				float originHeight = cameraPos.y;
				float3 V = normalize(input.worldPos - cameraPos);
				float Z = V.y;
				float effectiveZ = max(abs(Z), 0.001f);

				float endLineHeight = originHeight + dist * Z;
				float minLineHeight = min(originHeight, endLineHeight);
				float heightLineFalloff = max(minLineHeight - fogHeightStart, 0.0f);

				float baseHeightFogDistance = clamp((fogHeightStart - minLineHeight) / effectiveZ, 0.0f, dist);
				float exponentialFogDistance = dist - baseHeightFogDistance;
				float exponentialHeightLineIntegral = exp(-heightLineFalloff * fogFalloff) * (1.0f - exp(-exponentialFogDistance * effectiveZ * fogFalloff)) / (effectiveZ * fogFalloff);

				float opticalDepth = fogDensity * startDistanceFalloff * (baseHeightFogDistance + exponentialHeightLineIntegral);
				float fogAmount = 1.0f - saturate(exp(-opticalDepth));

				litColor = lerp(litColor, fogColor.rgb, fogAmount);
			}

			// Post Processing: Color Grading, Exposure, and Tone Mapping (Falcor & WickedEngine)
			if (enablePostProcessing > 0.5f)
			{
				// 1. Exposure (EV Compensation & Multiplier)
				float evFactor = exp2(postExposureEV);
				litColor *= (evFactor * max(postExposureMultiplier, 0.0f));

				// 2. Color Filter / White Balance Tint
				litColor *= max(postColorFilter.rgb, float3(0, 0, 0));

				// 3. Contrast (Linear Power Curve)
				if (abs(postContrast - 1.0f) > 0.001f)
				{
					litColor = pow(max(litColor, 0.00001f), max(postContrast, 0.01f));
				}

				// 4. Tonemapping (Falcor Uncharted 2, WickedEngine ACES Fitted, Reinhard, Uchimura GT7)
				if (postTonemapper > 0.5f && postTonemapper < 1.5f)
				{
					// ACES Fitted (WickedEngine ACESInputMat & ACESOutputMat RRT+ODT fit)
					litColor = ACESFitted(litColor);
				}
				else if (postTonemapper > 1.5f && postTonemapper < 2.5f)
				{
					// Reinhard tonemapping
					litColor = litColor / (litColor + 1.0f);
				}
				else if (postTonemapper > 2.5f && postTonemapper < 3.5f)
				{
					// Uchimura (Gran Turismo 7 curve from WickedEngine tonemapCS.hlsl)
					litColor = Uchimura(litColor);
				}
				else if (postTonemapper > 3.5f)
				{
					// NVIDIA Falcor & Hable Uncharted 2 Filmic Operator
					litColor = ToneMapHable(litColor);
				}

				// 5. Saturation
				if (abs(postSaturation - 1.0f) > 0.001f)
				{
					float luma = dot(litColor, float3(0.2126f, 0.7152f, 0.0722f));
					litColor = lerp(float3(luma, luma, luma), litColor, max(postSaturation, 0.0f));
				}

				// 6. Gamma correction
				float invGamma = 1.0f / max(2.2f * max(postGamma, 0.1f), 0.01f);
				litColor = pow(saturate(litColor), invGamma);
			}
			else
			{
				litColor = pow(saturate(litColor), 1.0f / 2.2f);
			}

			return float4(litColor, currentOpacity);
		}
	)";

	ID3DBlob* vertexShader = nullptr;
	ID3DBlob* pixelShader = nullptr;

	hr = CompileOrLoadCachedShader(hlslSource, "VSMain", "vs_5_0", &vertexShader, &errorBlob);
	if (FAILED(hr))
	{
		if (errorBlob) { std::cerr << "VS compile error: " << (char*)errorBlob->GetBufferPointer() << std::endl; errorBlob->Release(); }
		return EXIT_FAILURE;
	}

	hr = CompileOrLoadCachedShader(hlslSource, "PSMain", "ps_5_0", &pixelShader, &errorBlob);
	if (FAILED(hr))
	{
		if (errorBlob) { std::cerr << "PS compile error: " << (char*)errorBlob->GetBufferPointer() << std::endl; errorBlob->Release(); }
		return EXIT_FAILURE;
	}

	// 3. Input Layout (Position, Normal, UV, Color, Tangent)
	static_assert(sizeof(Vertex) == 60, "Vertex struct size must be exactly 60 bytes matching input layout");
	static_assert(offsetof(Vertex, position) == 0,  "Vertex position offset mismatch");
	static_assert(offsetof(Vertex, normal)   == 12, "Vertex normal offset mismatch");
	static_assert(offsetof(Vertex, uv)       == 24, "Vertex uv offset mismatch");
	static_assert(offsetof(Vertex, color)    == 32, "Vertex color offset mismatch");
	static_assert(offsetof(Vertex, tangent)  == 44, "Vertex tangent offset mismatch");

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	// 4. Graphics Pipeline State Object (PSO) for Main Lit Pass
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.InputLayout = { inputElementDescs, 5 };
	psoDesc.pRootSignature = g_rootSignature;
	psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
	psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };

	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK; // Default backface culling for optimal rasterizer performance
	psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
	psoDesc.RasterizerState.DepthClipEnable = TRUE;

	psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	psoDesc.DepthStencilState.DepthEnable = TRUE;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	psoDesc.DepthStencilState.StencilEnable = FALSE;

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = SCENE_RENDER_TARGET_FORMAT;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleDesc.Count = 1;

	hr = g_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_pipelineState));

	// Two-Sided PSO for materials/objects with twoSided == true (foliage, leaves, cloth, double-sided meshes)
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDescTwoSided = psoDesc;
	psoDescTwoSided.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	HRESULT hrTwoSided = g_d3dDevice->CreateGraphicsPipelineState(&psoDescTwoSided, IID_PPV_ARGS(&g_pipelineStateTwoSided));
	if (FAILED(hrTwoSided))
	{
		std::cerr << "Warning: Failed to create Two-Sided Pipeline State: " << hrTwoSided << std::endl;
		g_pipelineStateTwoSided = g_pipelineState;
	}

	vertexShader->Release();
	pixelShader->Release();

	if (FAILED(hr))
	{
		std::cerr << "Failed to create D3D12 Graphics Pipeline State: " << hr << std::endl;
		return EXIT_FAILURE;
	}

	// 4.5. Wicked Engine Early-Z / Depth Prepass Pipeline State
	ID3DBlob* depthPrepassVS = nullptr;
	hr = CompileOrLoadCachedShader(hlslSource, "VSDepthPrepass", "vs_5_0", &depthPrepassVS, &errorBlob);
	if (SUCCEEDED(hr))
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC depthPsoDesc = {};
		depthPsoDesc.InputLayout = { inputElementDescs, 5 };
		depthPsoDesc.pRootSignature = g_rootSignature;
		depthPsoDesc.VS = { depthPrepassVS->GetBufferPointer(), depthPrepassVS->GetBufferSize() };
		depthPsoDesc.PS = { nullptr, 0 }; // Null PS: Depth-only
		depthPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		depthPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		depthPsoDesc.RasterizerState.FrontCounterClockwise = TRUE;
		depthPsoDesc.RasterizerState.DepthClipEnable = TRUE;
		depthPsoDesc.DepthStencilState.DepthEnable = TRUE;
		depthPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		depthPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		depthPsoDesc.DepthStencilState.StencilEnable = FALSE;
		depthPsoDesc.SampleMask = UINT_MAX;
		depthPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		depthPsoDesc.NumRenderTargets = 0;
		depthPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
		depthPsoDesc.SampleDesc.Count = 1;

		hr = g_d3dDevice->CreateGraphicsPipelineState(&depthPsoDesc, IID_PPV_ARGS(&g_depthPrepassPipelineState));

		D3D12_GRAPHICS_PIPELINE_STATE_DESC depthPsoDescTwoSided = depthPsoDesc;
		depthPsoDescTwoSided.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		HRESULT hrDepthTwoSided = g_d3dDevice->CreateGraphicsPipelineState(&depthPsoDescTwoSided, IID_PPV_ARGS(&g_depthPrepassTwoSidedPipelineState));
		if (FAILED(hrDepthTwoSided))
		{
			g_depthPrepassTwoSidedPipelineState = g_depthPrepassPipelineState;
		}

		depthPrepassVS->Release();
		if (FAILED(hr))
		{
			std::cerr << "Warning: Failed to create Depth Prepass PSO: " << hr << std::endl;
			g_depthPrepassPipelineState = nullptr;
		}
	}
	else
	{
		if (errorBlob) { errorBlob->Release(); }
	}

	// 5. Shadow Pass Root Signature & Pipeline State
	D3D12_ROOT_PARAMETER shadowRootParam = {};
	shadowRootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	shadowRootParam.Descriptor.ShaderRegister = 0;
	shadowRootParam.Descriptor.RegisterSpace = 0;
	shadowRootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	D3D12_ROOT_SIGNATURE_DESC shadowRootSigDesc = {};
	shadowRootSigDesc.NumParameters = 1;
	shadowRootSigDesc.pParameters = &shadowRootParam;
	shadowRootSigDesc.NumStaticSamplers = 0;
	shadowRootSigDesc.pStaticSamplers = nullptr;
	shadowRootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ID3DBlob* serializedShadowRootSig = nullptr;
	hr = D3D12SerializeRootSignature(&shadowRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedShadowRootSig, &errorBlob);
	if (FAILED(hr))
	{
		if (errorBlob) { std::cerr << "Shadow RootSig serialization error: " << (char*)errorBlob->GetBufferPointer() << std::endl; errorBlob->Release(); }
		return EXIT_FAILURE;
	}
	g_d3dDevice->CreateRootSignature(0, serializedShadowRootSig->GetBufferPointer(), serializedShadowRootSig->GetBufferSize(), IID_PPV_ARGS(&g_shadowRootSignature));
	serializedShadowRootSig->Release();

	const char* shadowHlsl = R"(
		cbuffer ShadowConstants : register(b0)
		{
			float4x4 lightSpaceMatrix;
		};

		struct VSInput
		{
			float3 position : POSITION;
			float3 normal   : NORMAL;
			float2 uv       : TEXCOORD;
			float3 color    : COLOR;
		};

		float4 VSShadow(VSInput input) : SV_POSITION
		{
			return mul(lightSpaceMatrix, float4(input.position, 1.0f));
		}
	)";

	ID3DBlob* shadowVS = nullptr;
	hr = CompileOrLoadCachedShader(shadowHlsl, "VSShadow", "vs_5_0", &shadowVS, &errorBlob);
	if (FAILED(hr))
	{
		if (errorBlob) { std::cerr << "Shadow VS compile error: " << (char*)errorBlob->GetBufferPointer() << std::endl; errorBlob->Release(); }
		return EXIT_FAILURE;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = {};
	shadowPsoDesc.InputLayout = { inputElementDescs, 4 };
	shadowPsoDesc.pRootSignature = g_shadowRootSignature;
	shadowPsoDesc.VS = { shadowVS->GetBufferPointer(), shadowVS->GetBufferSize() };
	shadowPsoDesc.PS = { nullptr, 0 }; // Depth-only pass
	shadowPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	shadowPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	shadowPsoDesc.RasterizerState.FrontCounterClockwise = TRUE;
	shadowPsoDesc.RasterizerState.DepthClipEnable = TRUE;
	shadowPsoDesc.RasterizerState.DepthBias = 50;
	shadowPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
	shadowPsoDesc.DepthStencilState.DepthEnable = TRUE;
	shadowPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	shadowPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	shadowPsoDesc.DepthStencilState.StencilEnable = FALSE;
	shadowPsoDesc.SampleMask = UINT_MAX;
	shadowPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	shadowPsoDesc.NumRenderTargets = 0;
	shadowPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	shadowPsoDesc.SampleDesc.Count = 1;

	hr = g_d3dDevice->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&g_shadowPipelineState));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDescTwoSided = shadowPsoDesc;
	shadowPsoDescTwoSided.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	HRESULT hrShadowTwoSided = g_d3dDevice->CreateGraphicsPipelineState(&shadowPsoDescTwoSided, IID_PPV_ARGS(&g_shadowTwoSidedPipelineState));
	if (FAILED(hrShadowTwoSided))
	{
		g_shadowTwoSidedPipelineState = g_shadowPipelineState;
	}

	shadowVS->Release();
	if (FAILED(hr))
	{
		std::cerr << "Failed to create shadow PSO: " << hr << std::endl;
		return EXIT_FAILURE;
	}

	// 5. Create Skybox Root Signature and Pipeline State
	D3D12_ROOT_PARAMETER skyboxRootParams[2] = {};

	// Root Parameter 0: 28 32-bit constants for Skybox (invViewProj, cameraPos, exposure, tint, rotation, hasHdrTex, ambientContribution)
	skyboxRootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	skyboxRootParams[0].Constants.ShaderRegister = 0;
	skyboxRootParams[0].Constants.RegisterSpace = 0;
	skyboxRootParams[0].Constants.Num32BitValues = 28;
	skyboxRootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	// Root Parameter 1: Descriptor Table (1 SRV: t0, Texture2D HDR Skybox)
	D3D12_DESCRIPTOR_RANGE skyboxSrvRange = {};
	skyboxSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	skyboxSrvRange.NumDescriptors = 1;
	skyboxSrvRange.BaseShaderRegister = 0;
	skyboxSrvRange.RegisterSpace = 0;
	skyboxSrvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	skyboxRootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	skyboxRootParams[1].DescriptorTable.NumDescriptorRanges = 1;
	skyboxRootParams[1].DescriptorTable.pDescriptorRanges = &skyboxSrvRange;
	skyboxRootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// Static Sampler for Skybox: Linear Wrap
	D3D12_STATIC_SAMPLER_DESC skyboxSampler = {};
	skyboxSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	skyboxSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	skyboxSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	skyboxSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	skyboxSampler.MipLODBias = 0.0f;
	skyboxSampler.MaxAnisotropy = 1;
	skyboxSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	skyboxSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	skyboxSampler.MinLOD = 0.0f;
	skyboxSampler.MaxLOD = D3D12_FLOAT32_MAX;
	skyboxSampler.ShaderRegister = 0;
	skyboxSampler.RegisterSpace = 0;
	skyboxSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC skyboxRootSigDesc = {};
	skyboxRootSigDesc.NumParameters = 2;
	skyboxRootSigDesc.pParameters = skyboxRootParams;
	skyboxRootSigDesc.NumStaticSamplers = 1;
	skyboxRootSigDesc.pStaticSamplers = &skyboxSampler;
	skyboxRootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	ID3DBlob* serializedSkyboxRootSig = nullptr;
	hr = D3D12SerializeRootSignature(&skyboxRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedSkyboxRootSig, &errorBlob);
	if (FAILED(hr))
	{
		if (errorBlob) { std::cerr << "Skybox RootSig error: " << (char*)errorBlob->GetBufferPointer() << std::endl; errorBlob->Release(); }
		return EXIT_FAILURE;
	}
	g_d3dDevice->CreateRootSignature(0, serializedSkyboxRootSig->GetBufferPointer(), serializedSkyboxRootSig->GetBufferSize(), IID_PPV_ARGS(&g_skyboxRootSignature));
	serializedSkyboxRootSig->Release();

	const char* skyboxHlsl = R"(
		cbuffer SkyboxConstants : register(b0)
		{
			float4x4 invViewProj;
			float3 cameraPos;
			float exposure;
			float3 tint;
			float rotation;
			float hasHdrTex;
			float ambientContribution;
			float2 padding;
		};

		struct SkyboxVSOutput
		{
			float4 position : SV_Position;
			float2 clipPos : TEXCOORD0;
		};

		SkyboxVSOutput VSSkybox(uint vertexId : SV_VertexID)
		{
			SkyboxVSOutput output;
			float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
			output.position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 1.0f, 1.0f);
			output.clipPos = output.position.xy;
			return output;
		}

		Texture2D g_skyboxTexture : register(t0);
		SamplerState g_skyboxSampler : register(s0);

		float4 PSSkybox(SkyboxVSOutput input) : SV_Target
		{
			float4 unprojected = mul(invViewProj, float4(input.clipPos, 1.0f, 1.0f));
			float3 dir = normalize(unprojected.xyz / max(abs(unprojected.w), 1e-6f));
			float rad = radians(rotation);
			float cosR = cos(rad);
			float sinR = sin(rad);
			float3 rotDir;
			rotDir.x = dir.x * cosR - dir.z * sinR;
			rotDir.y = dir.y;
			rotDir.z = dir.x * sinR + dir.z * cosR;

			float3 color;
			if (hasHdrTex > 0.5f)
			{
				const float PI = 3.14159265358979323846f;
				float u = 0.5f + atan2(rotDir.z, rotDir.x) / (2.0f * PI);
				float v = 0.5f - asin(clamp(rotDir.y, -1.0f, 1.0f)) / PI;
				color = g_skyboxTexture.SampleLevel(g_skyboxSampler, float2(u, v), 0).rgb;
			}
			else
			{
				float t = 0.5f * (rotDir.y + 1.0f);
				color = lerp(float3(0.08f, 0.10f, 0.14f), float3(0.25f, 0.45f, 0.75f), saturate(t));
				float horizon = 1.0f - abs(rotDir.y);
				color += float3(0.18f, 0.18f, 0.15f) * pow(saturate(horizon), 6.0f);
			}

			color *= tint * exposure;
			color = color / (color + 1.0f);
			color = pow(saturate(color), 1.0f / 2.2f);
			return float4(color, 1.0f);
		}
	)";

	ID3DBlob* skyboxVS = nullptr;
	ID3DBlob* skyboxPS = nullptr;
	hr = CompileOrLoadCachedShader(skyboxHlsl, "VSSkybox", "vs_5_0", &skyboxVS, &errorBlob);
	if (FAILED(hr))
	{
		if (errorBlob) { std::cerr << "Skybox VS compile error: " << (char*)errorBlob->GetBufferPointer() << std::endl; errorBlob->Release(); }
		return EXIT_FAILURE;
	}
	hr = CompileOrLoadCachedShader(skyboxHlsl, "PSSkybox", "ps_5_0", &skyboxPS, &errorBlob);
	if (FAILED(hr))
	{
		if (errorBlob) { std::cerr << "Skybox PS compile error: " << (char*)errorBlob->GetBufferPointer() << std::endl; errorBlob->Release(); }
		return EXIT_FAILURE;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyboxPsoDesc = {};
	skyboxPsoDesc.InputLayout = { nullptr, 0 };
	skyboxPsoDesc.pRootSignature = g_skyboxRootSignature;
	skyboxPsoDesc.VS = { skyboxVS->GetBufferPointer(), skyboxVS->GetBufferSize() };
	skyboxPsoDesc.PS = { skyboxPS->GetBufferPointer(), skyboxPS->GetBufferSize() };
	skyboxPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	skyboxPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	skyboxPsoDesc.RasterizerState.FrontCounterClockwise = FALSE;
	skyboxPsoDesc.RasterizerState.DepthClipEnable = FALSE;
	skyboxPsoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
	skyboxPsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	skyboxPsoDesc.DepthStencilState.DepthEnable = TRUE;
	skyboxPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // Don't write depth!
	skyboxPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; // Draw only where depth is 1.0 (empty background)
	skyboxPsoDesc.DepthStencilState.StencilEnable = FALSE;
	skyboxPsoDesc.SampleMask = UINT_MAX;
	skyboxPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	skyboxPsoDesc.NumRenderTargets = 1;
	skyboxPsoDesc.RTVFormats[0] = SCENE_RENDER_TARGET_FORMAT;
	skyboxPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	skyboxPsoDesc.SampleDesc.Count = 1;

	hr = g_d3dDevice->CreateGraphicsPipelineState(&skyboxPsoDesc, IID_PPV_ARGS(&g_skyboxPipelineState));
	skyboxVS->Release();
	skyboxPS->Release();
	if (FAILED(hr))
	{
		std::cerr << "Failed to create skybox PSO: " << hr << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

int createAntiAliasingPipeline()
{
	if (!g_d3dDevice) return EXIT_FAILURE;

	// 1. Root Signature
	D3D12_DESCRIPTOR_RANGE texTableRange = {};
	texTableRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	texTableRange.NumDescriptors = 2; // t0: SceneTexture, t1: HistoryTexture
	texTableRange.BaseShaderRegister = 0;
	texTableRange.RegisterSpace = 0;
	texTableRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootParams[2] = {};
	// Param 0: 32-bit constants (12 floats)
	rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParams[0].Constants.ShaderRegister = 0;
	rootParams[0].Constants.RegisterSpace = 0;
	rootParams[0].Constants.Num32BitValues = 12;
	rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	// Param 1: SRV table (t0..t1)
	rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[1].DescriptorTable.pDescriptorRanges = &texTableRange;
	rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
	// s0: Linear Clamp
	samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].MipLODBias = 0.0f;
	samplers[0].MaxAnisotropy = 1;
	samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	samplers[0].MinLOD = 0.0f;
	samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	samplers[0].ShaderRegister = 0;
	samplers[0].RegisterSpace = 0;
	samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// s1: Point Clamp
	samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[1].MipLODBias = 0.0f;
	samplers[1].MaxAnisotropy = 1;
	samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	samplers[1].MinLOD = 0.0f;
	samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
	samplers[1].ShaderRegister = 1;
	samplers[1].RegisterSpace = 0;
	samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.NumParameters = 2;
	rsDesc.pParameters = rootParams;
	rsDesc.NumStaticSamplers = 2;
	rsDesc.pStaticSamplers = samplers;
	rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ID3DBlob* sigBlob = nullptr;
	ID3DBlob* errBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
	if (FAILED(hr))
	{
		if (errBlob) { std::cerr << "[Graphics] AA RootSig error: " << (char*)errBlob->GetBufferPointer() << "\n"; errBlob->Release(); }
		return EXIT_FAILURE;
	}

	hr = g_d3dDevice->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&g_antiAliasingRootSignature));
	sigBlob->Release();
	if (FAILED(hr)) return EXIT_FAILURE;
	g_antiAliasingRootSignature->SetName(L"g_antiAliasingRootSignature");

	// 2. Load/Compile Shaders
	std::string hlslSource;
	std::ifstream shaderFile("Shaders/AntiAliasing.hlsl");
	if (shaderFile.is_open())
	{
		std::stringstream ss;
		ss << shaderFile.rdbuf();
		hlslSource = ss.str();
	}
	if (hlslSource.empty())
	{
		std::ifstream fallbackFile("../Shaders/AntiAliasing.hlsl");
		if (fallbackFile.is_open())
		{
			std::stringstream ss;
			ss << fallbackFile.rdbuf();
			hlslSource = ss.str();
		}
	}
	if (hlslSource.empty())
	{
		std::cerr << "[Graphics] Could not read Shaders/AntiAliasing.hlsl!\n";
		return EXIT_FAILURE;
	}

	ID3DBlob* vsBlob = nullptr;
	ID3DBlob* psBlob = nullptr;
	hr = CompileOrLoadCachedShader(hlslSource.c_str(), "VS_AntiAliasing", "vs_5_0", &vsBlob, &errBlob);
	if (FAILED(hr))
	{
		if (errBlob) { std::cerr << "[Graphics] AA VS error: " << (char*)errBlob->GetBufferPointer() << "\n"; errBlob->Release(); }
		return EXIT_FAILURE;
	}

	hr = CompileOrLoadCachedShader(hlslSource.c_str(), "PS_AntiAliasing", "ps_5_0", &psBlob, &errBlob);
	if (FAILED(hr))
	{
		if (errBlob) { std::cerr << "[Graphics] AA PS error: " << (char*)errBlob->GetBufferPointer() << "\n"; errBlob->Release(); }
		if (vsBlob) vsBlob->Release();
		return EXIT_FAILURE;
	}

	// 3. Pipeline State
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = g_antiAliasingRootSignature;
	psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;

	hr = g_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_antiAliasingPipelineState));
	vsBlob->Release();
	psBlob->Release();

	if (FAILED(hr))
	{
		std::cerr << "[Graphics] Failed to create Anti-Aliasing PSO: 0x" << std::hex << hr << std::dec << "\n";
		return EXIT_FAILURE;
	}
	g_antiAliasingPipelineState->SetName(L"g_antiAliasingPipelineState");
	std::cout << "[Graphics] Real-time Anti-Aliasing pipeline initialized successfully.\n";
	return EXIT_SUCCESS;
}

static const char* g_godRayHlslSource = R"(
struct GodRayLightData
{
	float4 posRange;          // xyz: pos, w: range
	float4 colorIntensity;    // xyz: color, w: intensity
	float4 dirType;           // xyz: dir, w: type (1=point, 2=spot, 3=area)
	float4 spotParams;        // x: cosInner, y: cosOuter, z: attenuation, w: rangeSmoothing
	float4 volumetricParams;  // x: volumetric ? 1 : 0, y: volumetric_boost, z: scatteringG, w: volumetricIntensity
	float4 shadowParams;      // x: maxRayDistance, y: shadowSlot (-1 if none), z: shadowResolution, w: shadowBias
	float4x4 shadowMatrix;    // spot light-space matrix
};

cbuffer GodRayCB : register(b0)
{
	float4x4 invViewProj;
	float4x4 dirLightSpaceMatrix;
	float3 cameraPos;
	float pad0;
	float3 dirLightDir;
	float dirLightIntensity;
	float3 dirLightColor;
	float pad1;
	float4 dirVolumetricParams; // x: enabled(1/0), y: boost, z: scatteringG, w: intensity
	float4 dirExtraParams;      // x: sampleSteps, y: maxRayDistance, z: shadowBias, w: shadowMapSize
	float fogDensity;
	float fogStart;
	float fogHeightStart;
	float fogHeightEnd;
	float4 fogColor;
	float4 screenSize;          // width, height, 1/width, 1/height
	float numLights;
	float maxRayDistance;
	float globalSampleSteps;
	float spotShadowAllocRes;
	GodRayLightData lights[16];
};

Texture2D g_depthTexture : register(t0);
Texture2D g_dirShadowMap : register(t1);
Texture2DArray g_spotShadowMap : register(t2);
TextureCubeArray g_pointShadowMap : register(t3);

SamplerState g_linearClampSampler : register(s0);
SamplerComparisonState g_shadowCompSampler : register(s1);

struct VSOutput
{
	float4 pos : SV_POSITION;
	float2 uv  : TEXCOORD0;
};

VSOutput VS_GodRays(uint id : SV_VertexID)
{
	VSOutput output;
	output.uv = float2((id << 1) & 2, id & 2);
	output.pos = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
	return output;
}

// Wicked Engine Henyey-Greenstein Mie scattering phase function (globals.hlsli:1073)
float ComputeScatteringPhase(float lightDotView, float g)
{
	float g2 = g * g;
	return (1.0f - g2) / (4.0f * 3.14159265f * pow(max(1.0f + g2 - 2.0f * g * lightDotView, 0.0001f), 1.5f));
}

// Interleaved Gradient Noise for screen-space ray dithering (eliminates stepping bands)
float ScreenDither(float2 screenPos)
{
	return frac(52.9829189f * frac(dot(screenPos, float2(0.06711056f, 0.00583715f))));
}

// Wicked Engine exponential height fog amount (fogHF.hlsli)
float GetSampleFog(float dist, float3 samplePos)
{
	float startDist = max(fogStart, 0.001f);
	float startDistFalloff = saturate((dist - startDist) / startDist);
	float fogFalloffScale = 1.0f / max(0.01f, fogHeightEnd - fogHeightStart);
	float fogFalloff = 6.907755f * fogFalloffScale;

	float h = samplePos.y;
	float heightFalloff = max(h - fogHeightStart, 0.0f);
	float optDepth = fogDensity * startDistFalloff * exp(-heightFalloff * fogFalloff);
	return 1.0f - saturate(exp(-optDepth));
}

// Ray-Sphere intersection for bounded local light raymarching (Wicked Engine technique)
bool RaySphereIntersect(float3 rayOrigin, float3 rayDir, float3 sphereCenter, float sphereRadius, out float tEnter, out float tExit)
{
	float3 rc = rayOrigin - sphereCenter;
	float c = dot(rc, rc) - (sphereRadius * sphereRadius);
	float b = dot(rayDir, rc);
	float d = b * b - c;
	if (d < 0.0f)
	{
		tEnter = 0.0f;
		tExit = 0.0f;
		return false;
	}
	float sqrtD = sqrt(d);
	tEnter = -b - sqrtD;
	tExit = -b + sqrtD;
	return true;
}

float4 PS_GodRays(VSOutput input) : SV_TARGET
{
	// Load depth directly from current screen pixel to eliminate any viewport offset / ghosting
	int2 pixelCoord = int2(input.pos.xy);
	float depth = g_depthTexture.Load(int3(pixelCoord, 0)).r;

	// Reconstruct world position from depth
	float4 clipPos = float4(input.uv.x * 2.0f - 1.0f, (1.0f - input.uv.y) * 2.0f - 1.0f, depth, 1.0f);
	float4 worldPosH = mul(invViewProj, clipPos);
	float3 worldPos = worldPosH.xyz / max(abs(worldPosH.w), 1e-6f);

	float3 rayVector = worldPos - cameraPos;
	float linearDist = (depth >= 0.99999f) ? 100000.0f : length(rayVector);
	float3 rayDir = normalize(rayVector);

	float ditherVal = ScreenDither(input.pos.xy);
	float3 finalGodRays = float3(0, 0, 0);

	// 1. Directional Light Volumetric Scattering (Wicked Engine volumetricLight_DirectionalPS.hlsl)
	if (dirVolumetricParams.x > 0.5f && dirLightIntensity > 0.001f)
	{
		float maxDirDist = (depth >= 0.99999f) ? dirExtraParams.y : min(linearDist, dirExtraParams.y);
		if (maxDirDist > 0.05f)
		{
			uint nSteps = (uint)clamp(dirExtraParams.x, 8.0f, 64.0f);
			float stepSize = maxDirDist / (float)nSteps;
			float marchedDist = stepSize * ditherVal;

			float3 L = normalize(dirLightDir);
			float cosTheta = dot(L, -rayDir);
			float phase = ComputeScatteringPhase(clamp(cosTheta, -1.0f, 1.0f), dirVolumetricParams.z);

			float dirAccum = 0.0f;
			[loop]
			for (uint i = 0; i < nSteps; ++i)
			{
				float3 sampleP = cameraPos + rayDir * marchedDist;

				float4 shadowPos = mul(dirLightSpaceMatrix, float4(sampleP, 1.0f));
				float3 shadowCoord = shadowPos.xyz;
				shadowCoord.xy = shadowCoord.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

				float inLight = 0.0f;
				if (shadowCoord.x >= 0.0f && shadowCoord.x <= 1.0f &&
				    shadowCoord.y >= 0.0f && shadowCoord.y <= 1.0f &&
				    shadowCoord.z >= 0.0f && shadowCoord.z <= 1.0f)
				{
					inLight = g_dirShadowMap.SampleCmpLevelZero(g_shadowCompSampler, shadowCoord.xy, shadowCoord.z - dirExtraParams.z).r;
				}

				if (inLight > 0.001f)
				{
					float fogSample = GetSampleFog(marchedDist, sampleP);
					float stepContrib = inLight * (fogSample * (1.0f + dirVolumetricParams.y * 0.5f) + dirVolumetricParams.y * 0.05f) * phase;
					dirAccum += stepContrib;
				}

				marchedDist += stepSize;
			}

			finalGodRays += dirLightColor * (dirLightIntensity * dirVolumetricParams.w * (dirAccum / (float)nSteps));
		}
	}

	// 2. Local Volumetric Lights: Spot Lights & Point Lights (Wicked Engine volumetricLight_SpotPS.hlsl & volumetricLight_PointPS.hlsl)
	uint nL = (uint)min(numLights, 16.0f);
	[loop]
	for (uint l = 0; l < nL; ++l)
	{
		GodRayLightData ld = lights[l];
		if (ld.volumetricParams.x < 0.5f || ld.colorIntensity.w <= 0.001f)
			continue;

		float3 plPos = ld.posRange.xyz;
		float plRange = ld.posRange.w;

		// Bound ray march strictly to the light's sphere of influence
		float tEnter = 0.0f;
		float tExit = 0.0f;
		if (!RaySphereIntersect(cameraPos, rayDir, plPos, plRange, tEnter, tExit))
			continue; // Ray misses light volume completely: 0 GPU cost

		tEnter = max(0.0f, tEnter);
		tExit = min(linearDist, tExit);
		if (tExit <= tEnter + 0.001f)
			continue; // Obscured by foreground geometry

		float segLength = tExit - tEnter;
		uint nSteps = (uint)clamp(globalSampleSteps, 8.0f, 32.0f);
		float stepSize = segLength / (float)nSteps;
		float marchedDist = tEnter + stepSize * ditherVal;

		uint lType = (uint)ld.dirType.w;
		int shadowSlot = (int)ld.shadowParams.y;
		float sBias = ld.shadowParams.w;
		float shadowRes = max(ld.shadowParams.z, 128.0f);
		float rangeSq = plRange * plRange;
		float rangeRcpSq = 1.0f / max(rangeSq, 0.0001f);

		float3 localAccum = float3(0, 0, 0);

		[loop]
		for (uint s = 0; s < nSteps; ++s)
		{
			float3 sampleP = cameraPos + rayDir * marchedDist;
			float3 toLight = plPos - sampleP;
			float distSq = dot(toLight, toLight);

			if (distSq < rangeSq)
			{
				float dist = sqrt(distSq);
				float3 pL = toLight / max(dist, 0.0001f);

				// Wicked Engine / GLTF smooth distance attenuation
				float distPerRange = distSq * rangeRcpSq;
				float distPerRange4 = distPerRange * distPerRange;
				float distAtten = saturate(1.0f - distPerRange4) / max(distSq, 0.25f);

				float spotAngleAtten = 1.0f;
				if (lType == 2) // Spot Light
				{
					float cosDir = dot(normalize(ld.dirType.xyz), -pL);
					float cosInner = ld.spotParams.x;
					float cosOuter = ld.spotParams.y;
					spotAngleAtten = saturate((cosDir - cosOuter) / max(cosInner - cosOuter, 0.001f));
					spotAngleAtten = spotAngleAtten * spotAngleAtten;
				}
				else if (lType == 3) // Area Light
				{
					float forwardDot = dot(normalize(ld.dirType.xyz), -pL);
					spotAngleAtten = saturate(abs(forwardDot) * 1.2f);
				}

				if (distAtten * spotAngleAtten > 0.00001f)
				{
					float inLight = 1.0f;
					if (shadowSlot >= 0 && shadowSlot < 4)
					{
						if (lType == 1) // Point light cubemap shadow
						{
							float3 toSample = sampleP - plPos;
							float absZ = max(abs(toSample.x), max(abs(toSample.y), abs(toSample.z)));
							float nearZ = 0.05f;
							float farZ = max(plRange, 5.0f);
							float biasedZ = max(absZ - sBias * 5.0f, nearZ);
							if (biasedZ < farZ)
							{
								float refDepth = saturate((farZ / (farZ - nearZ)) - (farZ * nearZ / (farZ - nearZ)) / max(biasedZ, 0.0001f));
								inLight = g_pointShadowMap.SampleCmpLevelZero(g_shadowCompSampler, float4(toSample, (float)shadowSlot), refDepth).r;
							}
						}
						else if (lType == 2 || lType == 3) // Spot light 2D shadow array
						{
							float4 shadowCoord = mul(ld.shadowMatrix, float4(sampleP, 1.0f));
							if (shadowCoord.w > 0.001f)
							{
								float3 projCoords = shadowCoord.xyz / shadowCoord.w;
								if (projCoords.z >= 0.0f && projCoords.z <= 1.0f &&
								    projCoords.x >= -1.0f && projCoords.x <= 1.0f &&
								    projCoords.y >= -1.0f && projCoords.y <= 1.0f)
								{
									float2 shadowUV = float2(projCoords.x * 0.5f + 0.5f, -projCoords.y * 0.5f + 0.5f);
									float safeAllocRes = max(spotShadowAllocRes, 256.0f);
									float uvScale = saturate(shadowRes / safeAllocRes);
									float2 scaledUV = shadowUV * uvScale;
									inLight = g_spotShadowMap.SampleCmpLevelZero(g_shadowCompSampler, float3(scaledUV, (float)shadowSlot), projCoords.z - sBias * 0.15f).r;
								}
								else
								{
									inLight = 0.0f;
								}
							}
							else
							{
								inLight = 0.0f;
							}
						}
					}

					if (inLight > 0.001f)
					{
						float cosTheta = dot(pL, -rayDir);
						float phase = ComputeScatteringPhase(clamp(cosTheta, -1.0f, 1.0f), ld.volumetricParams.z);
						float fogSample = GetSampleFog(marchedDist, sampleP);
						// Wicked Engine formula: attenuation * (boost + fogSample) * phase * stepSize
						float stepContrib = inLight * distAtten * spotAngleAtten * (ld.volumetricParams.y * 0.5f + fogSample * 2.0f) * phase * stepSize;
						localAccum += ld.colorIntensity.xyz * (ld.colorIntensity.w * ld.volumetricParams.w * stepContrib);
					}
				}
			}

			marchedDist += stepSize;
		}

		finalGodRays += localAccum;
	}

	return float4(finalGodRays, 1.0f);
}
)";

int createGodRayPipeline()
{
	if (!g_d3dDevice) return EXIT_FAILURE;

	// 1. Root Signature
	D3D12_DESCRIPTOR_RANGE srvTable = {};
	srvTable.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvTable.NumDescriptors = 4;
	srvTable.BaseShaderRegister = 0;
	srvTable.RegisterSpace = 0;
	srvTable.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootParams[2] = {};
	rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParams[0].Descriptor.ShaderRegister = 0;
	rootParams[0].Descriptor.RegisterSpace = 0;
	rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[1].DescriptorTable.pDescriptorRanges = &srvTable;
	rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
	// s0: Linear Clamp
	samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	samplers[0].ShaderRegister = 0;
	samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// s1: Comparison Linear Clamp
	samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	samplers[1].ShaderRegister = 1;
	samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.NumParameters = 2;
	rsDesc.pParameters = rootParams;
	rsDesc.NumStaticSamplers = 2;
	rsDesc.pStaticSamplers = samplers;
	rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ID3DBlob* sigBlob = nullptr;
	ID3DBlob* errBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
	if (FAILED(hr))
	{
		if (errBlob) { std::cerr << "[Graphics] God Ray RootSig error: " << (char*)errBlob->GetBufferPointer() << "\n"; errBlob->Release(); }
		return EXIT_FAILURE;
	}

	hr = g_d3dDevice->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&g_godRayRootSignature));
	sigBlob->Release();
	if (FAILED(hr)) return EXIT_FAILURE;
	g_godRayRootSignature->SetName(L"g_godRayRootSignature");

	// 2. Compile shaders
	ID3DBlob* vsBlob = nullptr;
	ID3DBlob* psBlob = nullptr;
	hr = CompileOrLoadCachedShader(g_godRayHlslSource, "VS_GodRays", "vs_5_0", &vsBlob, &errBlob);
	if (FAILED(hr))
	{
		if (errBlob) { std::cerr << "[Graphics] God Ray VS error: " << (char*)errBlob->GetBufferPointer() << "\n"; errBlob->Release(); }
		return EXIT_FAILURE;
	}

	hr = CompileOrLoadCachedShader(g_godRayHlslSource, "PS_GodRays", "ps_5_0", &psBlob, &errBlob);
	if (FAILED(hr))
	{
		if (errBlob) { std::cerr << "[Graphics] God Ray PS error: " << (char*)errBlob->GetBufferPointer() << "\n"; errBlob->Release(); }
		if (vsBlob) vsBlob->Release();
		return EXIT_FAILURE;
	}

	// 3. Pipeline State with Additive Blending
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = g_godRayRootSignature;
	psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;

	hr = g_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_godRayPipelineState));
	vsBlob->Release();
	psBlob->Release();
	if (FAILED(hr)) return EXIT_FAILURE;
	g_godRayPipelineState->SetName(L"g_godRayPipelineState");

	// 4. Create Constant Buffer
	if (!g_godRayConstantBuffer)
	{
		D3D12_RESOURCE_DESC cbDesc = CreateBufferResourceDesc(4096);
		D3D12_HEAP_PROPERTIES uploadHeap = CreateHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
		hr = g_d3dDevice->CreateCommittedResource(
			&uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_godRayConstantBuffer)
		);
		if (SUCCEEDED(hr) && g_godRayConstantBuffer)
		{
			g_godRayConstantBuffer->SetName(L"g_godRayConstantBuffer");
			D3D12_RANGE readRange = { 0, 0 };
			g_godRayConstantBuffer->Map(0, &readRange, &g_pGodRayConstantMapped);
		}
	}

	// Update SRVs if resources already exist
	UpdateGodRayDepthSrv();
	UpdateGodRayShadowSrv();
	UpdateGodRaySpotShadowSrv();
	UpdateGodRayPointShadowSrv();

	std::cout << "[Graphics] Volumetric God Rays pipeline initialized successfully (Wicked Engine Technique).\n";
	return EXIT_SUCCESS;
}

// Wicked Engine Ambient Occlusion Constants (matches Shaders/AmbientOcclusion.hlsl)
struct alignas(16) AOConstants
{
	glm::mat4 viewMatrix;
	glm::mat4 projMatrix;
	glm::mat4 invProjMatrix;
	glm::vec4 resolution;
	glm::vec4 cameraParams;
	glm::vec4 viewportParams;
	glm::vec4 aoParams;
	glm::uvec4 aoConfig;
};

int createAoPipeline()
{
	if (!g_d3dDevice) return EXIT_FAILURE;

	// 1. Root Signature
	D3D12_DESCRIPTOR_RANGE srvTable = {};
	srvTable.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvTable.NumDescriptors = 2; // t0, t1
	srvTable.BaseShaderRegister = 0;
	srvTable.RegisterSpace = 0;
	srvTable.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootParams[2] = {};
	rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParams[0].Descriptor.ShaderRegister = 0; // b0
	rootParams[0].Descriptor.RegisterSpace = 0;
	rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[1].DescriptorTable.pDescriptorRanges = &srvTable;
	rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
	// s0: Point Clamp
	samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[0].ShaderRegister = 0;
	samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// s1: Linear Clamp
	samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplers[1].ShaderRegister = 1;
	samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.NumParameters = 2;
	rsDesc.pParameters = rootParams;
	rsDesc.NumStaticSamplers = 2;
	rsDesc.pStaticSamplers = samplers;
	rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ID3DBlob* sigBlob = nullptr;
	ID3DBlob* errBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob);
	if (FAILED(hr))
	{
		if (errBlob) { std::cerr << "[Graphics] AO RootSig error: " << (char*)errBlob->GetBufferPointer() << "\n"; errBlob->Release(); }
		return EXIT_FAILURE;
	}

	hr = g_d3dDevice->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&g_aoRootSignature));
	sigBlob->Release();
	if (FAILED(hr)) return EXIT_FAILURE;
	g_aoRootSignature->SetName(L"g_aoRootSignature");

	// 2. Load Shaders
	std::string hlslSource;
	std::vector<std::string> searchPaths = {
		"Shaders/AmbientOcclusion.hlsl",
		"../Shaders/AmbientOcclusion.hlsl",
		"Eunoia-Engine/Shaders/AmbientOcclusion.hlsl",
		"../Eunoia-Engine/Shaders/AmbientOcclusion.hlsl",
		"Build/Engine/Shaders/AmbientOcclusion.hlsl",
		"../../Shaders/AmbientOcclusion.hlsl"
	};
	for (const auto& path : searchPaths)
	{
		std::ifstream shaderFile(path);
		if (shaderFile.is_open())
		{
			std::stringstream ss;
			ss << shaderFile.rdbuf();
			hlslSource = ss.str();
			if (!hlslSource.empty()) break;
		}
	}
	if (hlslSource.empty())
	{
		std::cerr << "[Graphics] Could not read Shaders/AmbientOcclusion.hlsl from any search path!\n";
		return EXIT_FAILURE;
	}

	ID3DBlob* vsBlob = nullptr;
	ID3DBlob* psGenBlob = nullptr;
	ID3DBlob* psBlurBlob = nullptr;
	ID3DBlob* psCompBlob = nullptr;

	hr = CompileOrLoadCachedShader(hlslSource.c_str(), "VS_AO", "vs_5_0", &vsBlob, &errBlob);
	if (FAILED(hr))
	{
		if (errBlob) { std::cerr << "[Graphics] AO VS error: " << (char*)errBlob->GetBufferPointer() << "\n"; errBlob->Release(); }
		return EXIT_FAILURE;
	}

	hr = CompileOrLoadCachedShader(hlslSource.c_str(), "PS_AO_Generate", "ps_5_0", &psGenBlob, &errBlob);
	if (FAILED(hr))
	{
		if (errBlob) { std::cerr << "[Graphics] AO Gen PS error: " << (char*)errBlob->GetBufferPointer() << "\n"; errBlob->Release(); }
		if (vsBlob) vsBlob->Release();
		return EXIT_FAILURE;
	}

	hr = CompileOrLoadCachedShader(hlslSource.c_str(), "PS_AO_BilateralBlur", "ps_5_0", &psBlurBlob, &errBlob);
	if (FAILED(hr))
	{
		if (errBlob) { std::cerr << "[Graphics] AO Blur PS error: " << (char*)errBlob->GetBufferPointer() << "\n"; errBlob->Release(); }
		if (vsBlob) vsBlob->Release();
		if (psGenBlob) psGenBlob->Release();
		return EXIT_FAILURE;
	}

	hr = CompileOrLoadCachedShader(hlslSource.c_str(), "PS_AO_Composite", "ps_5_0", &psCompBlob, &errBlob);
	if (FAILED(hr))
	{
		if (errBlob) { std::cerr << "[Graphics] AO Composite PS error: " << (char*)errBlob->GetBufferPointer() << "\n"; errBlob->Release(); }
		if (vsBlob) vsBlob->Release();
		if (psGenBlob) psGenBlob->Release();
		if (psBlurBlob) psBlurBlob->Release();
		return EXIT_FAILURE;
	}

	// 3. PSOs
	// 3a. Generate PSO (R8_UNORM output, opaque)
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = g_aoRootSignature;
	psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
	psoDesc.PS = { psGenBlob->GetBufferPointer(), psGenBlob->GetBufferSize() };
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.DepthStencilState.StencilEnable = FALSE;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
	psoDesc.SampleDesc.Count = 1;

	hr = g_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_aoGeneratePipelineState));
	if (FAILED(hr)) return EXIT_FAILURE;
	g_aoGeneratePipelineState->SetName(L"g_aoGeneratePipelineState");

	// 3b. Blur PSO (R8_UNORM output, opaque)
	psoDesc.PS = { psBlurBlob->GetBufferPointer(), psBlurBlob->GetBufferSize() };
	hr = g_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_aoBlurPipelineState));
	if (FAILED(hr)) return EXIT_FAILURE;
	g_aoBlurPipelineState->SetName(L"g_aoBlurPipelineState");

	// 3c. Composite PSO (SCENE_RENDER_TARGET_FORMAT output, multiplicative hardware blend)
	psoDesc.PS = { psCompBlob->GetBufferPointer(), psCompBlob->GetBufferSize() };
	psoDesc.RTVFormats[0] = SCENE_RENDER_TARGET_FORMAT;
	psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_DEST_COLOR;
	psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
	psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

	hr = g_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_aoCompositePipelineState));
	vsBlob->Release();
	psGenBlob->Release();
	psBlurBlob->Release();
	psCompBlob->Release();
	if (FAILED(hr)) return EXIT_FAILURE;
	g_aoCompositePipelineState->SetName(L"g_aoCompositePipelineState");

	// 4. Create Constant Buffer
	if (!g_aoConstantBuffer)
	{
		D3D12_RESOURCE_DESC cbDesc = CreateBufferResourceDesc(512);
		D3D12_HEAP_PROPERTIES uploadHeap = CreateHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
		hr = g_d3dDevice->CreateCommittedResource(
			&uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_aoConstantBuffer)
		);
		if (SUCCEEDED(hr) && g_aoConstantBuffer)
		{
			g_aoConstantBuffer->SetName(L"g_aoConstantBuffer");
			D3D12_RANGE readRange = { 0, 0 };
			g_aoConstantBuffer->Map(0, &readRange, &g_pAoConstantMapped);
		}
	}

	std::cout << "[Graphics] Real-time Ambient Occlusion pipeline initialized successfully (Wicked Engine: SSAO, HBAO, MSAO, RTAO).\n";
	return EXIT_SUCCESS;
}

int createDynamicBuffers()
{
	// 1. Vertex Buffer
	UINT64 vertexBufferSize = MAX_SCENE_VERTICES * sizeof(Vertex);
	D3D12_HEAP_PROPERTIES uploadHeap = CreateHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC vBufferDesc = CreateBufferResourceDesc(vertexBufferSize);

	g_d3dDevice->CreateCommittedResource(
		&uploadHeap, D3D12_HEAP_FLAG_NONE, &vBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_vertexBuffer)
	);
	if (g_vertexBuffer) g_vertexBuffer->SetName(L"g_vertexBuffer");

	D3D12_RANGE readRange = { 0, 0 };
	g_vertexBuffer->Map(0, &readRange, &g_pVertexMapped);

	g_vertexBufferView.BufferLocation = g_vertexBuffer->GetGPUVirtualAddress();
	g_vertexBufferView.StrideInBytes = sizeof(Vertex);
	g_vertexBufferView.SizeInBytes = (UINT)vertexBufferSize;

	// 2. Index Buffer
	UINT64 indexBufferSize = MAX_SCENE_INDICES * sizeof(uint32_t);
	D3D12_RESOURCE_DESC iBufferDesc = CreateBufferResourceDesc(indexBufferSize);

	g_d3dDevice->CreateCommittedResource(
		&uploadHeap, D3D12_HEAP_FLAG_NONE, &iBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_indexBuffer)
	);
	if (g_indexBuffer) g_indexBuffer->SetName(L"g_indexBuffer");

	g_indexBuffer->Map(0, &readRange, &g_pIndexMapped);

	g_indexBufferView.BufferLocation = g_indexBuffer->GetGPUVirtualAddress();
	g_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	g_indexBufferView.SizeInBytes = (UINT)indexBufferSize;

	// 3. Constant Buffer
	UINT64 cbSize = (sizeof(SceneConstantBuffer) + 255) & ~255;
	D3D12_RESOURCE_DESC cbDesc = CreateBufferResourceDesc(cbSize * 2);

	g_d3dDevice->CreateCommittedResource(
		&uploadHeap, D3D12_HEAP_FLAG_NONE, &cbDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_constantBuffer)
	);
	if (g_constantBuffer) g_constantBuffer->SetName(L"g_constantBuffer");

	g_constantBuffer->Map(0, &readRange, &g_pConstantMapped);

	// 4. Material Editor Live 3D Preview Buffers
	UINT64 matVBufferSize = MAX_MAT_PREVIEW_VERTICES * sizeof(Vertex);
	D3D12_RESOURCE_DESC matVDesc = CreateBufferResourceDesc(matVBufferSize);
	g_d3dDevice->CreateCommittedResource(
		&uploadHeap, D3D12_HEAP_FLAG_NONE, &matVDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_matPreviewVertexBuffer)
	);
	if (g_matPreviewVertexBuffer) {
		g_matPreviewVertexBuffer->SetName(L"g_matPreviewVertexBuffer");
		g_matPreviewVertexBuffer->Map(0, &readRange, &g_pMatPreviewVertexMapped);
		g_matPreviewVertexBufferView.BufferLocation = g_matPreviewVertexBuffer->GetGPUVirtualAddress();
		g_matPreviewVertexBufferView.StrideInBytes = sizeof(Vertex);
		g_matPreviewVertexBufferView.SizeInBytes = (UINT)matVBufferSize;
	}

	UINT64 matIBufferSize = MAX_MAT_PREVIEW_INDICES * sizeof(uint32_t);
	D3D12_RESOURCE_DESC matIDesc = CreateBufferResourceDesc(matIBufferSize);
	g_d3dDevice->CreateCommittedResource(
		&uploadHeap, D3D12_HEAP_FLAG_NONE, &matIDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_matPreviewIndexBuffer)
	);
	if (g_matPreviewIndexBuffer) {
		g_matPreviewIndexBuffer->SetName(L"g_matPreviewIndexBuffer");
		g_matPreviewIndexBuffer->Map(0, &readRange, &g_pMatPreviewIndexMapped);
		g_matPreviewIndexBufferView.BufferLocation = g_matPreviewIndexBuffer->GetGPUVirtualAddress();
		g_matPreviewIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
		g_matPreviewIndexBufferView.SizeInBytes = (UINT)matIBufferSize;
	}

	D3D12_RESOURCE_DESC matCbDesc = CreateBufferResourceDesc(cbSize);
	g_d3dDevice->CreateCommittedResource(
		&uploadHeap, D3D12_HEAP_FLAG_NONE, &matCbDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_matPreviewConstantBuffer)
	);
	if (g_matPreviewConstantBuffer) {
		g_matPreviewConstantBuffer->SetName(L"g_matPreviewConstantBuffer");
		g_matPreviewConstantBuffer->Map(0, &readRange, &g_pMatPreviewConstantMapped);
	}

	g_currentMatPreviewMeshType = -1;

	// Initialize Dedicated 512x512 PBR Material Preview Offscreen Render Target & Depth Buffer
	if (!g_matPreviewRenderTarget && g_d3dDevice && g_rtvDescHeap && g_dsvDescHeap && g_srvDescHeap)
	{
		static const UINT MAT_PREVIEW_RES = 512;
		D3D12_HEAP_PROPERTIES defaultHeapProps = {};
		defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC rtvDesc = {};
		rtvDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		rtvDesc.Width = MAT_PREVIEW_RES;
		rtvDesc.Height = MAT_PREVIEW_RES;
		rtvDesc.DepthOrArraySize = 1;
		rtvDesc.MipLevels = 1;
		rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rtvDesc.SampleDesc.Count = 1;
		rtvDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		D3D12_CLEAR_VALUE rtvClear = {};
		rtvClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rtvClear.Color[0] = 0.118f;
		rtvClear.Color[1] = 0.118f;
		rtvClear.Color[2] = 0.118f;
		rtvClear.Color[3] = 1.0f;

		HRESULT hr = g_d3dDevice->CreateCommittedResource(
			&defaultHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&rtvDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&rtvClear,
			IID_PPV_ARGS(&g_matPreviewRenderTarget)
		);
		if (SUCCEEDED(hr) && g_matPreviewRenderTarget)
		{
			g_matPreviewRenderTarget->SetName(L"g_matPreviewRenderTarget");

			g_matPreviewRtvHandle = g_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
			g_matPreviewRtvHandle.ptr += (SIZE_T)(FRAME_COUNT + 2) * g_rtvDescriptorSize;
			g_d3dDevice->CreateRenderTargetView(g_matPreviewRenderTarget, nullptr, g_matPreviewRtvHandle);

			UINT srvIdx = 0;
			if (TryAllocSrvDescriptors(1, srvIdx))
			{
				g_matPreviewSrvCpuHandle = g_srvDescHeap->GetCPUDescriptorHandleForHeapStart();
				g_matPreviewSrvGpuHandle = g_srvDescHeap->GetGPUDescriptorHandleForHeapStart();
				g_matPreviewSrvCpuHandle.ptr += (SIZE_T)srvIdx * g_srvDescriptorSize;
				g_matPreviewSrvGpuHandle.ptr += (UINT64)srvIdx * g_srvDescriptorSize;

				D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MipLevels = 1;
				g_d3dDevice->CreateShaderResourceView(g_matPreviewRenderTarget, &srvDesc, g_matPreviewSrvCpuHandle);
				g_engineUI.matPreviewTextureId = g_matPreviewSrvGpuHandle.ptr;
			}
		}

		D3D12_RESOURCE_DESC dsvDesc = {};
		dsvDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		dsvDesc.Width = MAT_PREVIEW_RES;
		dsvDesc.Height = MAT_PREVIEW_RES;
		dsvDesc.DepthOrArraySize = 1;
		dsvDesc.MipLevels = 1;
		dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
		dsvDesc.SampleDesc.Count = 1;
		dsvDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE dsvClear = {};
		dsvClear.Format = DXGI_FORMAT_D32_FLOAT;
		dsvClear.DepthStencil.Depth = 1.0f;

		hr = g_d3dDevice->CreateCommittedResource(
			&defaultHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&dsvDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&dsvClear,
			IID_PPV_ARGS(&g_matPreviewDepthBuffer)
		);
		if (SUCCEEDED(hr) && g_matPreviewDepthBuffer)
		{
			g_matPreviewDepthBuffer->SetName(L"g_matPreviewDepthBuffer");
			g_matPreviewDsvHandle = g_dsvDescHeap->GetCPUDescriptorHandleForHeapStart();
			g_matPreviewDsvHandle.ptr += (SIZE_T)30 * g_dsvDescriptorSize;
			D3D12_DEPTH_STENCIL_VIEW_DESC dsvViewDesc = {};
			dsvViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
			dsvViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			g_d3dDevice->CreateDepthStencilView(g_matPreviewDepthBuffer, &dsvViewDesc, g_matPreviewDsvHandle);
		}
	}
	g_currentMatPreviewIndexCount = 0;

	return EXIT_SUCCESS;
}

static void SyncViewportCameraToLevelCamera()
{
	// In Play Mode, camera is completely static unless controlled by a behaviour script.
	// Viewport mouse dragging never alters the camera actor.
	return;
}

static void SyncLevelCameraToViewportCamera()
{
	if (g_scene.isPlayMode)
	{
		GameObject* camObj = nullptr;
		if (g_scene.activeLevelCameraId != -1)
		{
			camObj = g_scene.FindObject(g_scene.activeLevelCameraId);
		}
		if (!camObj)
		{
			for (auto& obj : g_scene.objects)
			{
				if (obj.isCamera || obj.type == PrimitiveType::Camera)
				{
					camObj = &obj;
					g_scene.activeLevelCameraId = obj.id;
					break;
				}
			}
		}
		if (camObj && (camObj->isCamera || camObj->type == PrimitiveType::Camera))
		{
			glm::mat4 worldMat = g_scene.GetWorldMatrix(*camObj);
			glm::vec3 worldPos = glm::vec3(worldMat[3]);
			glm::vec3 worldRot = camObj->rotation;
			if (camObj->parentId != -1)
			{
				const GameObject* p = g_scene.FindObjectConst(camObj->parentId);
				while (p)
				{
					worldRot += p->rotation;
					p = (p->parentId != -1) ? g_scene.FindObjectConst(p->parentId) : nullptr;
				}
			}

			g_camera.yaw = worldRot.y;
			g_camera.pitch = -std::clamp(worldRot.x, -89.0f, 89.0f);
			g_camera.fov = camObj->camera.fov;
			g_camera.isOrthographic = camObj->camera.isOrthographic;
			g_camera.orthoSize = camObj->camera.orthoSize;
			g_camera.nearPlane = std::max(0.01f, camObj->camera.nearPlane);
			g_camera.farPlane = std::max(1.0f, camObj->camera.farPlane);
			g_camera.distance = 0.001f;
			g_camera.target = worldPos + g_camera.GetForward() * 0.001f;
		}
	}
}

void updateSceneGeometry()
{
	SyncLevelCameraToViewportCamera();
	static std::vector<Vertex> sceneVertices;
	static std::vector<uint32_t> sceneIndices;

	EngineUI::ViewportRect vpRect = g_engineUI.GetViewportRect((float)g_currentWidth, (float)g_currentHeight);
	float aspect = (vpRect.width > 0 && vpRect.height > 0) ? (vpRect.width / vpRect.height) : 1.777f;

	Scene& activeScene = g_engineUI.GetActiveRenderScene(g_scene);
	OrbitCamera& activeCam = g_engineUI.GetActiveRenderCamera(g_camera);

	glm::mat4 view = activeCam.GetViewMatrix();
	glm::mat4 proj = activeCam.GetProjectionMatrix(aspect);
	glm::mat4 viewProj = proj * view;

	Eunoia::ViewportOnlyRenderer::Get().BeginFrame();
	Eunoia::ViewportOnlyRenderer::Get().UpdateFrustum(viewProj);

	activeScene.BuildSceneMesh(sceneVertices, sceneIndices, g_sceneBatches, activeCam.GetPosition(), &viewProj);
	g_numPureSceneBatches = g_sceneBatches.size();

	// Light & Camera Editor Gizmos (dev.md)
	Eunoia::EditorGizmoSystem::Get().RenderGizmos(activeScene, activeCam.GetPosition(), sceneVertices, sceneIndices, g_sceneBatches);

	g_currentVertexCount = (uint32_t)std::min(sceneVertices.size(), MAX_SCENE_VERTICES);
	g_currentIndexCount  = (uint32_t)std::min(sceneIndices.size(), MAX_SCENE_INDICES);

	// Clamp out-of-bounds indices to 0 to prevent GPU vertex fetch page faults
	for (size_t i = 0; i < g_currentIndexCount; ++i)
	{
		if (sceneIndices[i] >= g_currentVertexCount)
		{
			sceneIndices[i] = 0;
		}
	}

	if (g_pVertexMapped && g_currentVertexCount > 0)
	{
		memcpy(g_pVertexMapped, sceneVertices.data(), g_currentVertexCount * sizeof(Vertex));
	}
	if (g_pIndexMapped && g_currentIndexCount > 0)
	{
		memcpy(g_pIndexMapped, sceneIndices.data(), g_currentIndexCount * sizeof(uint32_t));
	}
}

// Returns active camera position based on current mode:
// Viewport Editor Mode: returns Viewport Camera eye position (g_camera.GetPosition()).
// Game Mode: returns the active game level camera position (or g_camera.GetPosition()).
static glm::vec3 GetActiveCameraPosition()
{
	if (g_scene.isPlayMode && g_scene.activeLevelCameraId != -1)
	{
		const GameObject* camObj = g_scene.FindObjectConst(g_scene.activeLevelCameraId);
		if (camObj && (camObj->isCamera || camObj->type == PrimitiveType::Camera))
		{
			glm::mat4 worldMat = g_scene.GetWorldMatrix(*camObj);
			return glm::vec3(worldMat[3]);
		}
	}
	return g_camera.GetPosition();
}

// WickedEngine-inspired Distance-Adaptive Shadow Resolution (Shadow LOD) Calculator
// Nearest shadow renders at full base resolution set in the Details panel for that light,
// while distant shadows dynamically scale down to lower resolutions (or cull) to boost performance.
static UINT CalculateDistanceShadowResolution(UINT baseRes, float dist, float range, float maxShadowDist)
{
	if (baseRes == 0) return 0;
	if (maxShadowDist > 0.1f && dist > maxShadowDist) return 0; // Hard cull beyond shadow distance

	float safeRange = std::max(range, 0.5f);
	float safeDist = std::max(dist, 0.001f);

	// WickedEngine scaling ratio: amount = range / dist
	float amount = 1.0f;
	if (safeDist > safeRange * 1.25f)
	{
		amount = (safeRange * 1.25f) / safeDist;
	}

	// Distance culling if very distant relative to light range
	if (safeDist > safeRange * 5.0f && safeDist > 70.0f)
	{
		return 0; // Culled: 0 draw calls
	}

	float targetRes = (float)baseRes * amount;
	if (targetRes >= (float)baseRes * 0.70f)
	{
		return baseRes; // Nearest shadow: full resolution configured in Details panel
	}
	else if (targetRes >= (float)baseRes * 0.35f)
	{
		return std::max(baseRes / 2, 256u); // Distance shadow: half resolution
	}
	else if (targetRes >= (float)baseRes * 0.18f)
	{
		return std::max(baseRes / 4, 128u); // Further shadow: quarter resolution
	}
	else
	{
		return std::max(baseRes / 8, 128u); // Furthest shadow: minimum resolution
	}
}

// Distance LOD candidate structures for shadow lights
struct ShadowLightCandidate {
	size_t lightIndex;
	float distToCam;
	UINT effectiveRes;
};

static std::unordered_map<size_t, int> g_frameLightShadowSlot;
static std::unordered_map<size_t, UINT> g_frameLightEffectiveRes;

void updateConstantBuffer()
{
	if (!g_pConstantMapped) return;

	SyncLevelCameraToViewportCamera();
	Scene& activeScene = g_engineUI.GetActiveRenderScene(g_scene);
	OrbitCamera& activeCam = g_engineUI.GetActiveRenderCamera(g_camera);
	activeScene.SyncLightPositionsFromActors();

	glm::vec3 activeCamPos = (&activeScene == &g_scene) ? GetActiveCameraPosition() : activeCam.GetPosition();

	// Distance-Adaptive Shadow Resolution (WickedEngine Distance LOD Reference)
	// Prioritize shadow-casting lights by proximity to active camera (viewport camera or game camera)
	std::vector<ShadowLightCandidate> ptCandidates;
	std::vector<ShadowLightCandidate> spotCandidates;

	g_frameLightShadowSlot.clear();
	g_frameLightEffectiveRes.clear();

	if (activeScene.enableShadows)
	{
		for (size_t i = 0; i < activeScene.pointLights.size(); ++i)
		{
			const auto& pl = activeScene.pointLights[i];
			if (!pl.enabled || !pl.castShadows) continue;

			float distToCam = glm::distance(activeCamPos, pl.position);
			UINT baseRes = (UINT)std::clamp(pl.shadowResolution, 256, 4096);
			float maxDist = (pl.shadowDistance > 0.1f) ? pl.shadowDistance : 50.0f;
			if (g_scene.shadowDistance > 1.0f && maxDist > g_scene.shadowDistance) {
				maxDist = g_scene.shadowDistance;
			}
			if (distToCam > maxDist + pl.range) continue; // Distance culled
			UINT effRes = CalculateDistanceShadowResolution(baseRes, distToCam, pl.range, maxDist);

			if (effRes == 0) continue; // Distance culled (0 draw calls)

			if (pl.type == LightType::Point)
			{
				ptCandidates.push_back({ i, distToCam, effRes });
			}
			else if (pl.type == LightType::Spot || pl.type == LightType::Area)
			{
				spotCandidates.push_back({ i, distToCam, effRes });
			}
		}
	}

	// Sort nearest first so closest lights to the active camera receive primary shadow slots and highest resolution
	std::sort(ptCandidates.begin(), ptCandidates.end(), [](const ShadowLightCandidate& a, const ShadowLightCandidate& b) {
		return a.distToCam < b.distToCam;
	});
	std::sort(spotCandidates.begin(), spotCandidates.end(), [](const ShadowLightCandidate& a, const ShadowLightCandidate& b) {
		return a.distToCam < b.distToCam;
	});

	int activeShadowPointLights = 0;
	UINT reqPtRes = 0;
	for (size_t k = 0; k < ptCandidates.size() && activeShadowPointLights < (int)MAX_SHADOW_POINT_LIGHTS; ++k)
	{
		int slot = activeShadowPointLights++;
		g_frameLightShadowSlot[ptCandidates[k].lightIndex] = slot;
		g_frameLightEffectiveRes[ptCandidates[k].lightIndex] = ptCandidates[k].effectiveRes;
		g_activePointLightResolutions[slot] = ptCandidates[k].effectiveRes;
		if (ptCandidates[k].effectiveRes > reqPtRes) reqPtRes = ptCandidates[k].effectiveRes;
	}

	int activeShadowSpotLights = 0;
	UINT reqSpotRes = 0;
	for (size_t k = 0; k < spotCandidates.size() && activeShadowSpotLights < (int)MAX_SHADOW_SPOT_LIGHTS; ++k)
	{
		int slot = activeShadowSpotLights++;
		g_frameLightShadowSlot[spotCandidates[k].lightIndex] = slot;
		g_frameLightEffectiveRes[spotCandidates[k].lightIndex] = spotCandidates[k].effectiveRes;
		g_activeSpotLightResolutions[slot] = spotCandidates[k].effectiveRes;
		if (spotCandidates[k].effectiveRes > reqSpotRes) reqSpotRes = spotCandidates[k].effectiveRes;
	}

	if (reqPtRes == 0) reqPtRes = (UINT)std::max(activeScene.shadowResolutionCube, 128);
	if (reqSpotRes == 0) reqSpotRes = (UINT)std::max(activeScene.shadowResolution2D, 128);
	UINT reqDirRes = (UINT)activeScene.shadowResolution2D;
	if (reqDirRes == 0) reqDirRes = 128; // minimal allocation to ensure valid descriptor table

	EnsureShadowBuffers(reqDirRes, reqPtRes, reqSpotRes);

	EngineUI::ViewportRect vpRect = g_engineUI.GetViewportRect((float)g_currentWidth, (float)g_currentHeight);
	float aspect = (vpRect.width > 0 && vpRect.height > 0) ? (vpRect.width / vpRect.height) : 1.777f;
	if (aspect <= 0.01f || std::isnan(aspect)) aspect = 1.777f;

	glm::mat4 model = glm::mat4(1.0f);
	glm::mat4 view  = activeCam.GetViewMatrix();
	glm::mat4 proj  = activeCam.GetProjectionMatrix(aspect);

	g_currentJitter = glm::vec2(0.0f, 0.0f);
	auto currentAaMethod = Eunoia::GraphicsSystem::Get().GetAntiAliasingMethod();
	bool isTemporalAA = (currentAaMethod == Eunoia::AntiAliasingMethod::TAA ||
	                     currentAaMethod == Eunoia::AntiAliasingMethod::TSR ||
	                     currentAaMethod == Eunoia::AntiAliasingMethod::DLSS ||
	                     currentAaMethod == Eunoia::AntiAliasingMethod::FSR ||
	                     currentAaMethod == Eunoia::AntiAliasingMethod::XeSS);
	if (isTemporalAA && vpRect.width > 1.0f && vpRect.height > 1.0f)
	{
		g_currentJitter = Eunoia::GraphicsSystem::Get().GetTemporalJitter(g_globalFrameCounter, (uint32_t)vpRect.width, (uint32_t)vpRect.height);
		proj[2][0] += (2.0f * g_currentJitter.x) / vpRect.width;
		proj[2][1] += (2.0f * g_currentJitter.y) / vpRect.height;
	}
	++g_globalFrameCounter;

	// Update Mesh Cluster Culling Camera (dev.md Section 6, 15, 16)
	if (Eunoia::MeshClusterCullingSystem::Get().IsInitialized())
	{
		glm::mat4 vp = proj * view;
		glm::vec4 frustumPlanes[6];
		frustumPlanes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]); // Left
		frustumPlanes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]); // Right
		frustumPlanes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]); // Bottom
		frustumPlanes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]); // Top
		frustumPlanes[4] = glm::vec4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);                                                 // Near
		frustumPlanes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]); // Far
		for (int p = 0; p < 6; ++p)
		{
			float len = glm::length(glm::vec3(frustumPlanes[p]));
			if (len > 1e-6f) frustumPlanes[p] /= len;
		}
		Eunoia::MeshClusterCullingSystem::Get().UpdateCamera(
			view, proj, g_camera.GetPosition(), frustumPlanes, vpRect.width, vpRect.height
		);
	}

	// Light View-Projection for Directional Sun Light
	glm::vec3 lightDir = activeScene.lightDirection;
	float lLen = glm::length(lightDir);
	if (lLen > 0.0001f && !std::isnan(lLen) && std::isfinite(lLen)) {
		lightDir /= lLen;
	} else {
		lightDir = glm::vec3(0.6f, 1.0f, 0.8f);
	}

	// Calculate Camera Frustum Focused Directional Shadow Frustum
	// This bounds the shadow map strictly around the active player/camera view area,
	// maintaining high, stable shadow texel resolution regardless of falling objects or physics motion.
	glm::vec3 camPos = activeCam.GetPosition();
	glm::vec3 camForward = activeCam.GetForward();
	glm::vec3 camUp = activeCam.GetUp();
	glm::vec3 camRight = activeCam.GetRight();

	if (!std::isfinite(camPos.x) || !std::isfinite(camPos.y) || !std::isfinite(camPos.z)) camPos = glm::vec3(0.0f);
	if (!std::isfinite(camForward.x) || !std::isfinite(camForward.y) || !std::isfinite(camForward.z) || glm::length(camForward) < 1e-4f) camForward = glm::vec3(0.0f, 0.0f, -1.0f);
	if (!std::isfinite(camUp.x) || !std::isfinite(camUp.y) || !std::isfinite(camUp.z) || glm::length(camUp) < 1e-4f) camUp = glm::vec3(0.0f, 1.0f, 0.0f);
	if (!std::isfinite(camRight.x) || !std::isfinite(camRight.y) || !std::isfinite(camRight.z) || glm::length(camRight) < 1e-4f) camRight = glm::vec3(1.0f, 0.0f, 0.0f);

	float maxDist = (activeScene.shadowDistance > 1.0f) ? activeScene.shadowDistance : 60.0f;
	float shadowDist = std::clamp(maxDist, 5.0f, activeCam.farPlane);
	float nearZ = std::max(0.1f, activeCam.nearPlane);

	float fovY = glm::radians(std::clamp(activeCam.fov, 10.0f, 120.0f));
	float tanHalfFovY = std::tan(fovY * 0.5f);

	float nearH = 2.0f * tanHalfFovY * nearZ;
	float nearW = nearH * aspect;
	float farH = 2.0f * tanHalfFovY * shadowDist;
	float farW = farH * aspect;

	if (activeCam.isOrthographic) {
		nearH = (activeCam.orthoSize > 0.01f ? activeCam.orthoSize : 5.0f) * 2.0f;
		nearW = nearH * aspect;
		farH = nearH;
		farW = nearW;
	}

	glm::vec3 nearCenter = camPos + camForward * nearZ;
	glm::vec3 farCenter = camPos + camForward * shadowDist;

	glm::vec3 frustumCorners[8] = {
		nearCenter + camUp * (nearH * 0.5f) - camRight * (nearW * 0.5f),
		nearCenter + camUp * (nearH * 0.5f) + camRight * (nearW * 0.5f),
		nearCenter - camUp * (nearH * 0.5f) - camRight * (nearW * 0.5f),
		nearCenter - camUp * (nearH * 0.5f) + camRight * (nearW * 0.5f),
		farCenter + camUp * (farH * 0.5f) - camRight * (farW * 0.5f),
		farCenter + camUp * (farH * 0.5f) + camRight * (farW * 0.5f),
		farCenter - camUp * (farH * 0.5f) - camRight * (farW * 0.5f),
		farCenter - camUp * (farH * 0.5f) + camRight * (farW * 0.5f)
	};

	glm::vec3 frustumCenter(0.0f);
	for (int i = 0; i < 8; ++i) {
		frustumCenter += frustumCorners[i];
	}
	frustumCenter /= 8.0f;

	float shadowRadius = 0.0f;
	for (int i = 0; i < 8; ++i) {
		float dist = glm::length(frustumCorners[i] - frustumCenter);
		if (dist > shadowRadius && std::isfinite(dist)) shadowRadius = dist;
	}
	if (shadowRadius < 15.0f || !std::isfinite(shadowRadius)) shadowRadius = 15.0f;

	// Quantize shadow radius in 2.0m steps to eliminate projection breathing
	shadowRadius = std::ceil(shadowRadius * 0.5f) * 2.0f;

	float orthoHalfSize = shadowRadius + 2.0f;
	float lightDistance = shadowRadius * 2.0f + 25.0f; // Pull back light to catch casters outside view
	glm::vec3 up = (std::abs(lightDir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

	// Light-space texel snapping to lock shadow texel grid to world space and prevent swimming/shimmering
	glm::mat4 lightViewIdentity = glm::lookAt(lightDir * lightDistance, glm::vec3(0.0f), up);
	glm::vec4 centerInLightView = lightViewIdentity * glm::vec4(frustumCenter, 1.0f);
	float worldUnitsPerTexel = (2.0f * orthoHalfSize) / (float)std::max((int)g_currentDirShadowRes, 256);
	if (worldUnitsPerTexel > 1e-6f) {
		centerInLightView.x = std::floor(centerInLightView.x / worldUnitsPerTexel) * worldUnitsPerTexel;
		centerInLightView.y = std::floor(centerInLightView.y / worldUnitsPerTexel) * worldUnitsPerTexel;
	}
	glm::vec3 snappedCenter = glm::vec3(glm::inverse(lightViewIdentity) * centerInLightView);
	glm::vec3 lightPos = snappedCenter + lightDir * lightDistance;
	glm::mat4 lightView = glm::lookAt(lightPos, snappedCenter, up);

	float nearPlane = 1.0f;
	float farPlane = lightDistance + shadowRadius * 2.0f + 25.0f;
	glm::mat4 lightProj = glm::ortho(-orthoHalfSize, orthoHalfSize, -orthoHalfSize, orthoHalfSize, nearPlane, farPlane);
	glm::mat4 lightSpaceMatrix = lightProj * lightView;

	// Calculate 8 NDC corners of the light frustum in world space for debug wireframe rendering
	glm::mat4 invLightSpace = glm::inverse(lightSpaceMatrix);
	const glm::vec4 ndcCorners[8] = {
		// Near plane: Z = 0
		{ -1.0f, -1.0f, 0.0f, 1.0f },
		{  1.0f, -1.0f, 0.0f, 1.0f },
		{  1.0f,  1.0f, 0.0f, 1.0f },
		{ -1.0f,  1.0f, 0.0f, 1.0f },
		// Far plane: Z = 1
		{ -1.0f, -1.0f, 1.0f, 1.0f },
		{  1.0f, -1.0f, 1.0f, 1.0f },
		{  1.0f,  1.0f, 1.0f, 1.0f },
		{ -1.0f,  1.0f, 1.0f, 1.0f },
	};
	for (int i = 0; i < 8; ++i) {
		glm::vec4 wp = invLightSpace * ndcCorners[i];
		if (std::abs(wp.w) > 1e-5f) {
			g_scene.lightFrustumCorners[i] = glm::vec3(wp) / wp.w;
		}
	}
	g_scene.hasLightFrustumCorners = true;

	if (g_pShadowConstantMapped)
	{
		ShadowConstantBuffer scb = {};
		scb.lightSpaceMatrix = lightSpaceMatrix;
		memcpy(g_pShadowConstantMapped, &scb, sizeof(scb));
	}

	g_currentDirLightSpaceMatrix = lightSpaceMatrix;
	g_currentDirLightDir = lightDir;

	SceneConstantBuffer cb = {};
	cb.mvp = proj * view * model;
	cb.lightSpaceMatrix = lightSpaceMatrix;
	cb.cameraPos = activeCamPos;
	cb.ambientIntensity = activeScene.ambientIntensity;
	cb.ambientColor = glm::vec4(activeScene.ambientColor, 1.0f);
	if (activeScene.enableSkybox && activeScene.enableSkyboxLighting)
	{
		float skyAmbient = activeScene.skyboxAmbientContribution +
		                   (activeScene.skyboxLightingIntensity * activeScene.skyboxLightingDiffuse * (activeScene.skyboxExposure * 0.5f));
		cb.ambientIntensity = activeScene.ambientIntensity + skyAmbient;
		glm::vec3 tintLight = activeScene.skyboxTint * activeScene.skyboxLightingColor;

		if (!activeScene.skyboxTexturePath.empty() && activeScene.skyboxTexturePath != "none")
		{
			std::string resolvedSky = activeScene.skyboxTexturePath;
			const AssetMetadata* meta = AssetRegistry::Get().FindByVirtualPath(activeScene.skyboxTexturePath);
			if (!meta) {
				AssetID id = AssetID::FromString(g_scene.skyboxTexturePath);
				if (id.IsValid()) meta = AssetRegistry::Get().FindById(id);
			}
			if (meta && !meta->sourcePath.empty()) resolvedSky = meta->sourcePath;
			else {
				std::string tm = TextureManager::Get().ResolvePath(activeScene.skyboxTexturePath);
				if (!tm.empty()) resolvedSky = tm;
			}
			CachedTexture* skyTex = TextureManager::Get().GetTexture(resolvedSky, TextureUsageSlot::BaseColor);
			if (skyTex && skyTex->valid && glm::length(skyTex->averageColor) > 0.001f)
			{
				tintLight *= skyTex->averageColor * 1.5f;
			}
		}

		cb.ambientColor = glm::vec4(glm::mix(activeScene.ambientColor, tintLight, 0.75f), 1.0f);
	}
	cb.lightDir = lightDir;
	cb.shadowBias = activeScene.shadowBias;
	cb.lightColor = activeScene.lightColor * activeScene.lightIntensity;
	cb.shadowStrength = activeScene.shadowStrength;
	cb.pcfRadius = activeScene.pcfRadius;
	cb.enableShadows = (activeScene.enableShadows && activeScene.shadowResolution2D > 0) ? 1.0f : 0.0f;
	cb.shadowMapSize = (float)g_currentDirShadowRes;

	// Post Processing parameters (Color Grading & Exposure)
	cb.enablePostProcessing = activeScene.enablePostProcessing ? 1.0f : 0.0f;
	cb.postExposureEV = activeScene.postExposureEV;
	cb.postExposureMultiplier = activeScene.postExposureMultiplier;
	cb.postTonemapper = (float)activeScene.postTonemapper;
	cb.postSaturation = activeScene.postSaturation;
	cb.postContrast = activeScene.postContrast;
	cb.postGamma = activeScene.postGamma;
	cb.padPost = (float)g_currentSpotShadowRes; // Pass spot shadow buffer allocation size to HLSL for distance-adaptive viewport scaling

	// Calculate white balance adjusted color filter
	glm::vec3 ppFilter = activeScene.postColorFilter;
	float tempDelta = (activeScene.postColorTemperature - 6500.0f) / 6500.0f;
	ppFilter.r *= (1.0f - tempDelta * 0.35f);
	ppFilter.b *= (1.0f + tempDelta * 0.35f);
	ppFilter.g *= (1.0f + activeScene.postColorTint * 0.20f);
	ppFilter = glm::max(ppFilter, glm::vec3(0.0f));
	cb.postColorFilter = glm::vec4(ppFilter, 1.0f);
	cb.maxShadowDistance = (activeScene.shadowDistance > 1.0f) ? activeScene.shadowDistance : 60.0f;
	cb.enableAO = 0.0f; // Handled cleanly by dedicated PASS 2.55 (Wicked Engine Screen Space Ambient Occlusion)
	cb.aoPower = activeScene.aoPower;
	cb.aoRange = activeScene.aoRange;

	// Volumetric Fog (Wicked Engine fogHF)
	cb.enableFog = activeScene.enableFog ? 1.0f : 0.0f;
	cb.fogStart = activeScene.fogStart;
	cb.fogDensity = activeScene.fogDensity;
	cb.fogHeightStart = activeScene.fogHeightStart;
	cb.fogHeightEnd = activeScene.fogHeightEnd;
	cb.shadowSampleCount = (float)activeScene.shadowSampleCount;
	cb.shadowPCSS = activeScene.shadowPCSS ? 1.0f : 0.0f;
	cb.enableScreenSpaceShadows = (activeScene.screenSpaceShadows && activeScene.enableShadows && activeScene.shadowResolution2D > 0) ? 1.0f : 0.0f;
	cb.fogColor = glm::vec4(activeScene.fogColor, 1.0f);
	cb.screenSpaceShadowRange = activeScene.screenSpaceShadowRange;
	cb.screenSpaceShadowSamples = (float)activeScene.screenSpaceShadowSamples;
	cb.enableCapsuleShadows = activeScene.capsuleShadows ? 1.0f : 0.0f;
	cb.capsuleShadowAngle = activeScene.capsuleShadowAngle;
	cb.capsuleShadowFade = activeScene.capsuleShadowFade;
	cb.cascadeCount = (float)activeScene.cascadeCount;
	cb.cascadeBlending = activeScene.cascadeBlending ? 1.0f : 0.0f;
	cb.visualizeCascades = activeScene.visualizeCascades ? 1.0f : 0.0f;
	cb.cascadeSplits = glm::vec4(
		activeScene.cascadeDistances[0],
		activeScene.cascadeDistances[1],
		activeScene.cascadeDistances[2],
		activeScene.cascadeDistances[3]
	);

	// Fill Point, Spot, and Area Lights (up to MAX_POINT_LIGHTS)
	int activePointLights = 0;

	for (size_t i = 0; i < activeScene.pointLights.size() && activePointLights < MAX_POINT_LIGHTS; ++i)
	{
		const auto& pl = activeScene.pointLights[i];
		if (!pl.enabled) continue;

		float effectiveIntensity = pl.intensity;
		if (pl.enableDistanceCulling)
		{
			float distToCam = glm::distance(cb.cameraPos, pl.position);
			if (distToCam >= pl.maxVisibleDistance)
				continue; // Completely cull beyond maxVisibleDistance
			if (distToCam > pl.minVisibleDistance)
			{
				float range = std::max(pl.maxVisibleDistance - pl.minVisibleDistance, 0.001f);
				float t = std::clamp((distToCam - pl.minVisibleDistance) / range, 0.0f, 1.0f);
				float fade = 1.0f - (t * t * (3.0f - 2.0f * t)); // smoothstep fade
				effectiveIntensity *= fade;
			}
			if (effectiveIntensity <= 0.0001f)
				continue;
		}

		cb.pointLightPosRange[activePointLights] = glm::vec4(pl.position, pl.range);
		cb.pointLightColorIntensity[activePointLights] = glm::vec4(pl.color, effectiveIntensity);

		glm::vec3 dir = pl.direction;
		float dLen = glm::length(dir);
		if (dLen > 0.0001f) dir /= dLen;
		else dir = glm::vec3(0.0f, -1.0f, 0.0f);

		cb.pointLightDirType[activePointLights] = glm::vec4(dir, (float)pl.type);

		if (pl.type == LightType::Spot)
		{
			float cosInner = std::cos(glm::radians(std::min(pl.innerConeAngle, pl.outerConeAngle)));
			float cosOuter = std::cos(glm::radians(std::max(pl.innerConeAngle, pl.outerConeAngle)));
			float smoothing = std::clamp(pl.rangeSmoothing, 0.001f, 1.0f);
			cb.pointLightSpotAreaParams[activePointLights] = glm::vec4(cosInner, cosOuter, pl.attenuation, smoothing);
		}
		else if (pl.type == LightType::Area)
		{
			cb.pointLightSpotAreaParams[activePointLights] = glm::vec4(pl.width, pl.height, pl.attenuation, pl.twoSided ? 1.0f : 0.0f);
		}
		else
		{
			cb.pointLightSpotAreaParams[activePointLights] = glm::vec4(0.0f, 0.0f, pl.attenuation, 0.0f);
		}

		int shadowSlot = -1;
		UINT effectiveRes = (UINT)pl.shadowResolution;
		auto itSlot = g_frameLightShadowSlot.find(i);
		if (itSlot != g_frameLightShadowSlot.end())
		{
			shadowSlot = itSlot->second;
			effectiveRes = g_frameLightEffectiveRes[i];

			if (pl.type == LightType::Point)
			{
				if (g_pShadowConstantMapped)
				{
					float nearZ = 0.05f;
					float farZ = std::max(pl.range, 5.0f);
					glm::mat4 ptProj = glm::perspectiveLH_ZO(glm::radians(90.0f), 1.0f, nearZ, farZ);
					glm::vec3 pos = pl.position;

					glm::mat4 faceViews[6] = {
						glm::lookAtLH(pos, pos + glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)), // +X (Face 0)
						glm::lookAtLH(pos, pos + glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)), // -X (Face 1)
						glm::lookAtLH(pos, pos + glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f, 0.0f, -1.0f)), // +Y (Face 2)
						glm::lookAtLH(pos, pos + glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f, 0.0f,  1.0f)), // -Y (Face 3)
						glm::lookAtLH(pos, pos + glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, 1.0f,  0.0f)), // +Z (Face 4)
						glm::lookAtLH(pos, pos + glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, 1.0f,  0.0f))  // -Z (Face 5)
					};

					for (int f = 0; f < 6; ++f)
					{
						ShadowConstantBuffer ptScb = {};
						ptScb.lightSpaceMatrix = ptProj * faceViews[f];
						UINT64 faceOffset = (UINT64)(1 + shadowSlot * 6 + f) * 256;
						memcpy((uint8_t*)g_pShadowConstantMapped + faceOffset, &ptScb, sizeof(ptScb));
					}
				}
			}
			else if (pl.type == LightType::Spot || pl.type == LightType::Area)
			{
				float fov = (pl.type == LightType::Spot) ? std::max(pl.outerConeAngle * 2.0f, 5.0f) : 120.0f;
				fov = std::min(fov, 160.0f);
				float nearZ = 0.05f;
				float farZ = std::max(pl.range, 5.0f);
				glm::mat4 spotProj = glm::perspectiveLH_ZO(glm::radians(fov), 1.0f, nearZ, farZ);
				glm::vec3 spotUp = (std::abs(dir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
				glm::mat4 spotView = glm::lookAtLH(pl.position, pl.position + dir, spotUp);
				glm::mat4 spotLightSpace = spotProj * spotView;

				cb.spotLightSpaceMatrices[shadowSlot] = spotLightSpace;
				if (shadowSlot >= 0 && shadowSlot < (int)MAX_SHADOW_SPOT_LIGHTS)
				{
					g_currentSpotLightSpaceMatrices[shadowSlot] = spotLightSpace;
				}

				if (g_pShadowConstantMapped)
				{
					ShadowConstantBuffer spotScb = {};
					spotScb.lightSpaceMatrix = spotLightSpace;
					UINT64 spotOffset = (UINT64)(1 + MAX_SHADOW_POINT_LIGHTS * 6 + shadowSlot) * 256;
					memcpy((uint8_t*)g_pShadowConstantMapped + spotOffset, &spotScb, sizeof(spotScb));
				}
			}
		}

		cb.pointLightShadowParams[activePointLights] = glm::vec4(
			(float)shadowSlot,
			pl.shadowStrength,
			pl.shadowBias,
			(float)effectiveRes
		);

		activePointLights++;
	}

	cb.numPointLights = (float)activePointLights;
	g_activeShadowPointLights = activeShadowPointLights;
	g_activeShadowSpotLights = activeShadowSpotLights;

	memcpy(g_pConstantMapped, &cb, sizeof(cb));

	// If Camera Picture-in-Picture (PiP) preview is active, compute and upload camera's view/projection into slot 1
	EngineUI::CameraPipRect pipRect = g_engineUI.GetCameraPipRect((float)g_currentWidth, (float)g_currentHeight, g_scene);
	if (pipRect.active && g_pConstantMapped)
	{
		GameObject* camObj = g_scene.FindObject(pipRect.selectedCameraId);
		if (camObj && (camObj->isCamera || camObj->type == PrimitiveType::Camera))
		{
			glm::mat4 worldMat = g_scene.GetWorldMatrix(*camObj);
			glm::vec3 worldPos = glm::vec3(worldMat[3]);
			glm::vec3 worldRot = camObj->rotation;
			if (camObj->parentId != -1)
			{
				const GameObject* p = g_scene.FindObjectConst(camObj->parentId);
				while (p)
				{
					worldRot += p->rotation;
					p = (p->parentId != -1) ? g_scene.FindObjectConst(p->parentId) : nullptr;
				}
			}

			OrbitCamera pipCam;
			pipCam.yaw = worldRot.y;
			pipCam.pitch = -std::clamp(worldRot.x, -89.0f, 89.0f);
			pipCam.fov = camObj->camera.fov;
			pipCam.isOrthographic = camObj->camera.isOrthographic;
			pipCam.orthoSize = camObj->camera.orthoSize;
			pipCam.nearPlane = std::max(0.01f, camObj->camera.nearPlane);
			pipCam.farPlane = std::max(1.0f, camObj->camera.farPlane);
			pipCam.distance = 0.001f;
			pipCam.target = worldPos + pipCam.GetForward() * 0.001f;

			float pipAspect = (pipRect.width > 0.0f && pipRect.height > 0.0f) ? (pipRect.width / pipRect.height) : 1.777f;
			glm::mat4 pipView = pipCam.GetViewMatrix();
			glm::mat4 pipProj = pipCam.GetProjectionMatrix(pipAspect);

			SceneConstantBuffer pipCb = cb;
			pipCb.mvp = pipProj * pipView * model;
			pipCb.cameraPos = pipCam.GetPosition();

			memcpy((uint8_t*)g_pConstantMapped + sizeof(SceneConstantBuffer), &pipCb, sizeof(pipCb));
		}
	}
}

void resizeBuffers(int width, int height)
{
	if (width <= 0 || height <= 0) return;

	WaitForGpuIdle();

	for (UINT i = 0; i < FRAME_COUNT; i++)
	{
		if (g_renderTargets[i])
		{
			g_renderTargets[i]->Release();
			g_renderTargets[i] = nullptr;
		}
	}

	UINT swapChainFlags = g_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
	g_swapChain->ResizeBuffers(FRAME_COUNT, (UINT)width, (UINT)height, DXGI_FORMAT_R8G8B8A8_UNORM, swapChainFlags);
	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT i = 0; i < FRAME_COUNT; i++)
	{
		g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i]));
		g_d3dDevice->CreateRenderTargetView(g_renderTargets[i], nullptr, rtvHandle);
		rtvHandle.ptr += g_rtvDescriptorSize;
	}

	createDepthStencilView(width, height);
	createSceneRenderTargets(width, height);
	g_engineUI.RequestViewportRedraw(15);
}

void renderFrame()
{
	if (g_deviceLost) return;

	g_commandList = g_commandLists[g_frameIndex];
	g_commandAllocators[g_frameIndex]->Reset();
	g_commandList->Reset(g_commandAllocators[g_frameIndex], nullptr);

	g_recordedFrameOps.clear();

	// Update the lightmap SRV descriptor slot (t9) each frame
	UpdateLightmapSrv();

	Scene& activeRenderScene = g_engineUI.GetActiveRenderScene(g_scene);

	// ----------------------------------------------------
	// PASS 1: Directional Shadow Depth Map Pass (2048x2048 D32)
	// ----------------------------------------------------
	bool canRender3DScene = (g_engineUI.isGameOnlyWindow || g_engineUI.isInitialSceneLoaded || (&g_engineUI.GetActiveRenderScene(g_scene) != &g_scene));
	bool need3DSceneRender = (canRender3DScene && (g_engineUI.isGameOnlyWindow ||
	                                              g_scene.isPlayMode ||
	                                              g_engineUI.isPlayModeActive ||
	                                              g_engineUI.realtimeViewport ||
	                                              g_engineUI.viewportRedrawCountdown > 0 ||
	                                              (&g_engineUI.GetActiveRenderScene(g_scene) != &g_scene)));

	if (need3DSceneRender && !g_deviceLost && g_currentIndexCount > 0 && g_shadowDepthBuffer && g_shadowPipelineState && activeRenderScene.enableShadows)
	{
		D3D12_RESOURCE_BARRIER shadowBarrier = CreateTransitionBarrier(
			g_shadowDepthBuffer,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_DEPTH_WRITE
		);
		RecordGpuBreadcrumbOp("[Barrier] ShadowDepthBuffer -> DEPTH_WRITE");
		g_commandList->ResourceBarrier(1, &shadowBarrier);

		D3D12_VIEWPORT shadowViewport = { 0.0f, 0.0f, (float)g_currentDirShadowRes, (float)g_currentDirShadowRes, 0.0f, 1.0f };
		D3D12_RECT shadowScissor = { 0, 0, (LONG)g_currentDirShadowRes, (LONG)g_currentDirShadowRes };
		g_commandList->RSSetViewports(1, &shadowViewport);
		g_commandList->RSSetScissorRects(1, &shadowScissor);

		RecordGpuBreadcrumbOp("[ClearDSV] ShadowDepthBuffer");
		g_commandList->ClearDepthStencilView(g_shadowDsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		g_commandList->OMSetRenderTargets(0, nullptr, FALSE, &g_shadowDsvHandle);

		g_commandList->SetGraphicsRootSignature(g_shadowRootSignature);
		g_commandList->SetPipelineState(g_shadowPipelineState);
		ID3D12PipelineState* curShadowPso = g_shadowPipelineState;
		g_commandList->SetGraphicsRootConstantBufferView(0, g_shadowConstantBuffer->GetGPUVirtualAddress());

		g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_commandList->IASetVertexBuffers(0, 1, &g_vertexBufferView);
		g_commandList->IASetIndexBuffer(&g_indexBufferView);

		for (size_t bIdx = 0; bIdx < g_sceneBatches.size(); ++bIdx)
		{
			const auto& batch = g_sceneBatches[bIdx];
			if (batch.startIndex >= g_currentIndexCount || batch.indexCount == 0 || !batch.castShadows || batch.isUnlit || !batch.isShadowVisible) continue;
			UINT drawCount = std::min(batch.indexCount, g_currentIndexCount - batch.startIndex);
			if (drawCount == 0) continue;

			ID3D12PipelineState* targetShadowPso = batch.twoSided ? g_shadowTwoSidedPipelineState : g_shadowPipelineState;
			if (targetShadowPso && targetShadowPso != curShadowPso)
			{
				g_commandList->SetPipelineState(targetShadowPso);
				curShadowPso = targetShadowPso;
			}

			char opDesc[128];
			snprintf(opDesc, sizeof(opDesc), "[ShadowDraw] Batch %zu count=%u start=%u", bIdx, drawCount, batch.startIndex);
			RecordGpuBreadcrumbOp(opDesc);

			g_commandList->DrawIndexedInstanced(drawCount, 1, batch.startIndex, 0, 0);
		}

		D3D12_RESOURCE_BARRIER shadowReadBarrier = CreateTransitionBarrier(
			g_shadowDepthBuffer,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		);
		RecordGpuBreadcrumbOp("[Barrier] ShadowDepthBuffer -> PIXEL_SHADER_RESOURCE");
		g_commandList->ResourceBarrier(1, &shadowReadBarrier);
	}

	// ----------------------------------------------------
	// PASS 1.5: Point Light Shadow Cubemap Array Pass (up to 4 lights * 6 faces)
	// ----------------------------------------------------
	if (need3DSceneRender && !g_deviceLost && g_currentIndexCount > 0 && g_pointShadowDepthBuffer && g_shadowPipelineState && g_activeShadowPointLights > 0)
	{
		D3D12_RESOURCE_BARRIER ptBarrier = CreateTransitionBarrier(
			g_pointShadowDepthBuffer,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_DEPTH_WRITE
		);
		RecordGpuBreadcrumbOp("[Barrier] PointShadowDepthBuffer -> DEPTH_WRITE");
		g_commandList->ResourceBarrier(1, &ptBarrier);

		g_commandList->SetGraphicsRootSignature(g_shadowRootSignature);
		g_commandList->SetPipelineState(g_shadowPipelineState);
		ID3D12PipelineState* curPtShadowPso = g_shadowPipelineState;

		g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_commandList->IASetVertexBuffers(0, 1, &g_vertexBufferView);
		g_commandList->IASetIndexBuffer(&g_indexBufferView);

		for (int l = 0; l < g_activeShadowPointLights; ++l)
		{
			UINT ptRes = g_activePointLightResolutions[l];
			if (ptRes == 0) continue; // Distance culled (0 draw calls)

			D3D12_VIEWPORT ptViewport = { 0.0f, 0.0f, (float)ptRes, (float)ptRes, 0.0f, 1.0f };
			D3D12_RECT ptScissor = { 0, 0, (LONG)ptRes, (LONG)ptRes };
			g_commandList->RSSetViewports(1, &ptViewport);
			g_commandList->RSSetScissorRects(1, &ptScissor);

			for (int f = 0; f < 6; ++f)
			{
				int dsvIdx = l * 6 + f;
				g_commandList->ClearDepthStencilView(g_pointShadowDsvHandles[dsvIdx], D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
				g_commandList->OMSetRenderTargets(0, nullptr, FALSE, &g_pointShadowDsvHandles[dsvIdx]);

				UINT64 faceCbOffset = (UINT64)(1 + l * 6 + f) * 256;
				g_commandList->SetGraphicsRootConstantBufferView(0, g_shadowConstantBuffer->GetGPUVirtualAddress() + faceCbOffset);

				for (size_t bIdx = 0; bIdx < g_sceneBatches.size(); ++bIdx)
				{
					const auto& batch = g_sceneBatches[bIdx];
					if (batch.startIndex >= g_currentIndexCount || batch.indexCount == 0 || !batch.castShadows || batch.isUnlit) continue;
					UINT drawCount = (UINT)std::min((size_t)batch.indexCount, (size_t)(g_currentIndexCount - batch.startIndex));
					if (drawCount == 0) continue;

					ID3D12PipelineState* targetShadowPso = batch.twoSided ? g_shadowTwoSidedPipelineState : g_shadowPipelineState;
					if (targetShadowPso && targetShadowPso != curPtShadowPso)
					{
						g_commandList->SetPipelineState(targetShadowPso);
						curPtShadowPso = targetShadowPso;
					}

					g_commandList->DrawIndexedInstanced(drawCount, 1, (UINT)batch.startIndex, 0, 0);
				}
			}
		}

		D3D12_RESOURCE_BARRIER ptReadBarrier = CreateTransitionBarrier(
			g_pointShadowDepthBuffer,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		);
		RecordGpuBreadcrumbOp("[Barrier] PointShadowDepthBuffer -> PIXEL_SHADER_RESOURCE");
		g_commandList->ResourceBarrier(1, &ptReadBarrier);
	}

	// ----------------------------------------------------
	// PASS 1.8: Spot & Area Light Shadow 2D Array Pass (up to 4 lights)
	// ----------------------------------------------------
	if (need3DSceneRender && !g_deviceLost && g_currentIndexCount > 0 && g_spotShadowDepthBuffer && g_shadowPipelineState && g_activeShadowSpotLights > 0)
	{
		D3D12_RESOURCE_BARRIER spotBarrier = CreateTransitionBarrier(
			g_spotShadowDepthBuffer,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_DEPTH_WRITE
		);
		RecordGpuBreadcrumbOp("[Barrier] SpotShadowDepthBuffer -> DEPTH_WRITE");
		g_commandList->ResourceBarrier(1, &spotBarrier);

		g_commandList->SetGraphicsRootSignature(g_shadowRootSignature);
		g_commandList->SetPipelineState(g_shadowPipelineState);
		ID3D12PipelineState* curSpotShadowPso = g_shadowPipelineState;

		g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_commandList->IASetVertexBuffers(0, 1, &g_vertexBufferView);
		g_commandList->IASetIndexBuffer(&g_indexBufferView);

		for (int s = 0; s < g_activeShadowSpotLights; ++s)
		{
			UINT spotRes = g_activeSpotLightResolutions[s];
			if (spotRes == 0) continue; // Distance culled (0 draw calls)

			D3D12_VIEWPORT spotViewport = { 0.0f, 0.0f, (float)spotRes, (float)spotRes, 0.0f, 1.0f };
			D3D12_RECT spotScissor = { 0, 0, (LONG)spotRes, (LONG)spotRes };
			g_commandList->RSSetViewports(1, &spotViewport);
			g_commandList->RSSetScissorRects(1, &spotScissor);

			g_commandList->ClearDepthStencilView(g_spotShadowDsvHandles[s], D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
			g_commandList->OMSetRenderTargets(0, nullptr, FALSE, &g_spotShadowDsvHandles[s]);

			UINT64 spotCbOffset = (UINT64)(1 + MAX_SHADOW_POINT_LIGHTS * 6 + s) * 256;
			g_commandList->SetGraphicsRootConstantBufferView(0, g_shadowConstantBuffer->GetGPUVirtualAddress() + spotCbOffset);

			for (size_t bIdx = 0; bIdx < g_sceneBatches.size(); ++bIdx)
			{
				const auto& batch = g_sceneBatches[bIdx];
				if (batch.startIndex >= g_currentIndexCount || batch.indexCount == 0 || !batch.castShadows || batch.isUnlit) continue;
				UINT drawCount = (UINT)std::min((size_t)batch.indexCount, (size_t)(g_currentIndexCount - batch.startIndex));
				if (drawCount == 0) continue;

				ID3D12PipelineState* targetShadowPso = batch.twoSided ? g_shadowTwoSidedPipelineState : g_shadowPipelineState;
				if (targetShadowPso && targetShadowPso != curSpotShadowPso)
				{
					g_commandList->SetPipelineState(targetShadowPso);
					curSpotShadowPso = targetShadowPso;
				}

				g_commandList->DrawIndexedInstanced(drawCount, 1, (UINT)batch.startIndex, 0, 0);
			}
		}

		D3D12_RESOURCE_BARRIER spotReadBarrier = CreateTransitionBarrier(
			g_spotShadowDepthBuffer,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		);
		RecordGpuBreadcrumbOp("[Barrier] SpotShadowDepthBuffer -> PIXEL_SHADER_RESOURCE");
		g_commandList->ResourceBarrier(1, &spotReadBarrier);
	}

	// ----------------------------------------------------
	// PASS 1.9: Mesh Cluster Culling GPU Compute Pass (dev.md Sections 5, 8, 9, 10)
	// ----------------------------------------------------
	if (need3DSceneRender && !g_deviceLost && Eunoia::MeshClusterCullingSystem::Get().IsInitialized())
	{
		RecordGpuBreadcrumbOp("[Compute] MeshClusterCulling ExecuteCullingPass");
		Eunoia::MeshClusterCullingSystem::Get().ExecuteCullingPass(
			g_commandList, g_scene, g_depthStencilBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE
		);
	}

	// ----------------------------------------------------
	// PASS 2: Main Scene PBR Lit Pass with Shadows & Point Lights
	// ----------------------------------------------------
	D3D12_RESOURCE_BARRIER barrier = CreateTransitionBarrier(
		g_renderTargets[g_frameIndex],
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);
	char bufRtvBarrier[96];
	snprintf(bufRtvBarrier, sizeof(bufRtvBarrier), "[Barrier] BackBuffer[%u] -> RENDER_TARGET", g_frameIndex);
	RecordGpuBreadcrumbOp(bufRtvBarrier);
	g_commandList->ResourceBarrier(1, &barrier);

	// Restrict 3D Viewport & Scissor to the Center Viewport (matching UI_Ref.svg blueprint)
	EngineUI::ViewportRect vpRect = g_engineUI.GetViewportRect((float)g_currentWidth, (float)g_currentHeight);
	float vx = std::max(0.0f, std::min((float)g_currentWidth - 1.0f, vpRect.x));
	float vy = std::max(0.0f, std::min((float)g_currentHeight - 1.0f, vpRect.y));
	float vw = std::max(1.0f, std::min((float)g_currentWidth - vx, vpRect.width));
	float vh = std::max(1.0f, std::min((float)g_currentHeight - vy, vpRect.height));

	D3D12_VIEWPORT viewport = { vx, vy, vw, vh, 0.0f, 1.0f };
	D3D12_RECT scissor = { (LONG)vx, (LONG)vy, (LONG)(vx + vw), (LONG)(vy + vh) };
	g_commandList->RSSetViewports(1, &viewport);
	g_commandList->RSSetScissorRects(1, &scissor);

	// Clear RTV & DSV
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += (SIZE_T)g_frameIndex * g_rtvDescriptorSize;

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = g_dsvDescHeap->GetCPUDescriptorHandleForHeapStart();

	// activeRenderScene already declared
	const float clearColor[4] = {
		activeRenderScene.clearColor.r,
		activeRenderScene.clearColor.g,
		activeRenderScene.clearColor.b,
		1.0f
	};
	char bufClearRtv[64];
	snprintf(bufClearRtv, sizeof(bufClearRtv), "[ClearRTV] BackBuffer[%u]", g_frameIndex);
	RecordGpuBreadcrumbOp(bufClearRtv);

	if (g_engineUI.isGameOnlyWindow)
	{
		g_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	}
	else
	{
		// First clear entire backbuffer to editor background #1E1E1E (RGB 0.118f, 0.118f, 0.118f)
		const float editorBgColor[4] = { 0.118f, 0.118f, 0.118f, 1.0f };
		g_commandList->ClearRenderTargetView(rtvHandle, editorBgColor, 0, nullptr);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE activeSceneRtv = rtvHandle;
	if (g_sceneRenderTarget && g_antiAliasingPipelineState)
	{
		activeSceneRtv = g_sceneRtvHandle;
		if (need3DSceneRender)
		{
			g_commandList->ClearRenderTargetView(g_sceneRtvHandle, clearColor, 0, nullptr);
			RecordGpuBreadcrumbOp("[ClearDSV] DepthStencilBuffer");
			g_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		}
	}
	else if (!g_engineUI.isGameOnlyWindow)
	{
		// Fallback: Clear viewport area of backbuffer directly
		if (need3DSceneRender)
		{
			g_commandList->ClearRenderTargetView(rtvHandle, clearColor, 1, &scissor);
			RecordGpuBreadcrumbOp("[ClearDSV] DepthStencilBuffer");
			g_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		}
	}
	else
	{
		if (need3DSceneRender)
		{
			RecordGpuBreadcrumbOp("[ClearDSV] DepthStencilBuffer");
			g_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		}
	}

	g_commandList->OMSetRenderTargets(1, &activeSceneRtv, FALSE, &dsvHandle);

	// ----------------------------------------------------
	// PASS 2.0: Wicked Engine Hardware Early-Z Depth Prepass
	// (Populates depth buffer ahead of main PBR pass to eliminate pixel shader overdraw)
	// ----------------------------------------------------
	if (need3DSceneRender && !g_deviceLost && g_currentIndexCount > 0 && g_depthPrepassPipelineState)
	{
		RecordGpuBreadcrumbOp("[Depth Prepass] Render Opaque Depth");
		// Unbind color RTV to achieve maximum fill rate without writing color
		g_commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);
		g_commandList->SetGraphicsRootSignature(g_rootSignature);
		g_commandList->SetPipelineState(g_depthPrepassPipelineState);
		ID3D12PipelineState* curDepthPso = g_depthPrepassPipelineState;
		g_commandList->SetGraphicsRootConstantBufferView(0, g_constantBuffer->GetGPUVirtualAddress());

		g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_commandList->IASetVertexBuffers(0, 1, &g_vertexBufferView);
		g_commandList->IASetIndexBuffer(&g_indexBufferView);

		for (size_t bIdx = 0; bIdx < g_sceneBatches.size(); ++bIdx)
		{
			const auto& batch = g_sceneBatches[bIdx];
			if (batch.startIndex >= g_currentIndexCount || batch.indexCount == 0 || !batch.isVisible) continue;
			if (batch.blendMode != 0 || batch.opacity < 0.999f) continue; // Only pre-pass opaque geometry
			UINT drawCount = std::min(batch.indexCount, g_currentIndexCount - batch.startIndex);
			if (drawCount == 0) continue;

			ID3D12PipelineState* targetDepthPso = batch.twoSided ? g_depthPrepassTwoSidedPipelineState : g_depthPrepassPipelineState;
			if (targetDepthPso && targetDepthPso != curDepthPso)
			{
				g_commandList->SetPipelineState(targetDepthPso);
				curDepthPso = targetDepthPso;
			}

			g_commandList->DrawIndexedInstanced(drawCount, 1, batch.startIndex, 0, 0);
		}

		// Rebind active scene color render target + depth stencil view for main PBR lit pass
		g_commandList->OMSetRenderTargets(1, &activeSceneRtv, FALSE, &dsvHandle);
	}

	// Set Descriptor Heap (for ImGui and scene resources)
	ID3D12DescriptorHeap* descriptorHeaps[] = { g_srvDescHeap };
	g_commandList->SetDescriptorHeaps(1, descriptorHeaps);

	// 1. Draw 3D Scene Primitives with Hardware PBR, Full Texture Resolution & Real-Time Shadows
	if (need3DSceneRender && !g_deviceLost && g_currentIndexCount > 0)
	{
		g_commandList->SetGraphicsRootSignature(g_rootSignature);
		g_commandList->SetPipelineState(g_pipelineState);
		ID3D12PipelineState* curMainPso = g_pipelineState;
		g_commandList->SetGraphicsRootConstantBufferView(0, g_constantBuffer->GetGPUVirtualAddress());
		g_commandList->SetGraphicsRootDescriptorTable(3, g_shadowSrvGpuHandle);
		g_commandList->SetGraphicsRootDescriptorTable(4, g_pointShadowSrvGpuHandle);
		g_commandList->SetGraphicsRootDescriptorTable(5, g_spotShadowSrvGpuHandle);
		if (g_lightmapSrvInHeap)
			g_commandList->SetGraphicsRootDescriptorTable(6, g_lightmapSrvGpuHandle);

		g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_commandList->IASetVertexBuffers(0, 1, &g_vertexBufferView);
		g_commandList->IASetIndexBuffer(&g_indexBufferView);

		for (size_t bIdx = 0; bIdx < g_sceneBatches.size(); ++bIdx)
		{
			const auto& batch = g_sceneBatches[bIdx];
			if (batch.startIndex >= g_currentIndexCount || batch.indexCount == 0 || !batch.isVisible) continue;
			UINT drawCount = std::min(batch.indexCount, g_currentIndexCount - batch.startIndex);
			if (drawCount == 0) continue;

			ID3D12PipelineState* targetMainPso = batch.twoSided ? g_pipelineStateTwoSided : g_pipelineState;
			if (targetMainPso && targetMainPso != curMainPso)
			{
				g_commandList->SetPipelineState(targetMainPso);
				curMainPso = targetMainPso;
			}

			MaterialShaderConstants matConsts = {};
			matConsts.baseColor[0] = batch.baseColor.r;
			matConsts.baseColor[1] = batch.baseColor.g;
			matConsts.baseColor[2] = batch.baseColor.b;
			matConsts.metallic = batch.metallic;
			matConsts.roughness = batch.roughness;
			matConsts.normalStrength = batch.normalStrength;
			matConsts.specular = batch.specular;
			matConsts.emissiveIntensity = batch.emissiveIntensity;
			matConsts.emissiveColor[0] = batch.emissiveColor.r;
			matConsts.emissiveColor[1] = batch.emissiveColor.g;
			matConsts.emissiveColor[2] = batch.emissiveColor.b;
			matConsts.isUnlit = (batch.isUnlit || g_engineUI.viewMode == 1) ? 1.0f : 0.0f;
			matConsts.hasAlbedoTex = (!batch.albedoTex.empty() && batch.albedoTex != "none") ? 1.0f : 0.0f;
			matConsts.hasNormalTex = (!batch.normalTex.empty() && batch.normalTex != "none") ? 1.0f : 0.0f;
			matConsts.hasRoughTex  = (!batch.roughTex.empty() && batch.roughTex != "none")  ? 1.0f : 0.0f;
			matConsts.hasAoTex     = (!batch.aoTex.empty() && batch.aoTex != "none")        ? 1.0f : 0.0f;
			matConsts.receiveShadows = batch.receiveShadows ? 1.0f : 0.0f;
			matConsts.uvScale[0] = batch.uvScale.x != 0.0f ? batch.uvScale.x : 1.0f;
			matConsts.uvScale[1] = batch.uvScale.y != 0.0f ? batch.uvScale.y : 1.0f;
			matConsts.pad0 = batch.useVertexAlpha;
			matConsts.blendMode = (float)batch.blendMode;
			matConsts.opacity = batch.opacity;
			matConsts.opacityMaskClipValue = batch.opacityMaskClipValue > 0.0f ? batch.opacityMaskClipValue : 0.333f;
			matConsts.hasOpacityTex = (!batch.opacityTex.empty() && batch.opacityTex != "none") ? 1.0f : 0.0f;
			matConsts.normalMapYFlip = batch.normalMapYFlip ? 1.0f : 0.0f;
			matConsts.roughnessChannel = (float)batch.roughnessChannel;
			matConsts.metallicChannel = (float)batch.metallicChannel;
			matConsts.aoChannel = (float)batch.aoChannel;
			matConsts.materialDebugMode = (float)batch.materialDebugMode;
			matConsts.hasMetalTex = (!batch.metalTex.empty() && batch.metalTex != "none") ? 1.0f : 0.0f;
			matConsts.hasEmissionTex = (!batch.emissionTex.empty() && batch.emissionTex != "none") ? 1.0f : 0.0f;

			g_commandList->SetGraphicsRoot32BitConstants(1, 32, &matConsts, 0);

			D3D12_GPU_DESCRIPTOR_HANDLE tableHandle = GetOrCreateMaterialTable(
				batch.albedoTex, batch.normalTex, batch.roughTex, batch.metalTex, batch.aoTex, batch.opacityTex, batch.emissionTex);
			if (tableHandle.ptr == 0) continue;

			char bufSceneDraw[160];
			snprintf(bufSceneDraw, sizeof(bufSceneDraw), "[SceneDraw] Batch %zu (%s) count=%u start=%u table=0x%llx",
				bIdx, batch.albedoTex.empty() ? "untextured" : batch.albedoTex.c_str(),
				drawCount, batch.startIndex, (unsigned long long)tableHandle.ptr);
			RecordGpuBreadcrumbOp(bufSceneDraw);

			g_commandList->SetGraphicsRootDescriptorTable(2, tableHandle);

			// GPU Mesh Cluster Culling branch (dev.md Sections 10, 11)
			if (batch.meshClusterCulling && Eunoia::MeshClusterCullingSystem::Get().IsInitialized())
			{
				char bufSceneIndirect[160];
				snprintf(bufSceneIndirect, sizeof(bufSceneIndirect), "[SceneDraw-ClusterCull] Batch %zu (%s) ExecuteIndirect",
					bIdx, batch.albedoTex.empty() ? "untextured" : batch.albedoTex.c_str());
				RecordGpuBreadcrumbOp(bufSceneIndirect);

				bool rendered = Eunoia::MeshClusterCullingSystem::Get().RenderClusteredBatch(g_commandList, batch);
				if (!rendered)
				{
					// Safe fallback to standard rasterization if cluster execution is skipped or has no visible clusters
					g_commandList->DrawIndexedInstanced(drawCount, 1, batch.startIndex, 0, 0);
				}
			}
			else
			{
				// Standard rasterization path for objects with Mesh Cluster Culling OFF (zero overhead)
				g_commandList->DrawIndexedInstanced(drawCount, 1, batch.startIndex, 0, 0);
			}
		}
	}

	// ----------------------------------------------------
	// PASS 2.2: HDR Skybox Pass (Renders behind geometry via Early-Z test at far plane)
	// ----------------------------------------------------
	if (need3DSceneRender && !g_deviceLost && activeRenderScene.enableSkybox && g_skyboxRootSignature && g_skyboxPipelineState)
	{
		RecordGpuBreadcrumbOp("[Skybox Pass] Render HDR Skybox");
		g_commandList->SetGraphicsRootSignature(g_skyboxRootSignature);
		g_commandList->SetPipelineState(g_skyboxPipelineState);

		EngineUI::ViewportRect vpRect = g_engineUI.GetViewportRect((float)g_currentWidth, (float)g_currentHeight);
		float aspect = (vpRect.width > 0 && vpRect.height > 0) ? (vpRect.width / vpRect.height) : 1.777f;
		if (aspect <= 0.01f || std::isnan(aspect)) aspect = 1.777f;

		glm::mat4 view = g_camera.GetViewMatrix();
		glm::mat4 proj = g_camera.GetProjectionMatrix(aspect);
		glm::mat4 skyboxView = glm::mat4(glm::mat3(view));
		glm::mat4 invVP = glm::inverse(proj * skyboxView);

		SkyboxShaderConstants skyConsts = {};
		skyConsts.invViewProj = invVP;
		skyConsts.cameraPos = glm::vec3(0.0f);
		skyConsts.exposure = g_scene.skyboxExposure;
		skyConsts.tint = g_scene.skyboxTint;
		skyConsts.rotation = g_scene.skyboxRotation;
		skyConsts.ambientContribution = g_scene.enableSkyboxLighting ? (g_scene.skyboxLightingIntensity * g_scene.skyboxLightingDiffuse) : 0.0f;

		bool hasHdrTex = !g_scene.skyboxTexturePath.empty() && g_scene.skyboxTexturePath != "none";
		skyConsts.hasHdrTex = hasHdrTex ? 1.0f : 0.0f;

		g_commandList->SetGraphicsRoot32BitConstants(0, 28, &skyConsts, 0);

		D3D12_GPU_DESCRIPTOR_HANDLE skyTable = GetOrCreateSkyboxTable(g_scene.skyboxTexturePath);
		if (skyTable.ptr != 0)
		{
			g_commandList->SetGraphicsRootDescriptorTable(1, skyTable);
		}

		g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_commandList->DrawInstanced(3, 1, 0, 0);
	}

	// ----------------------------------------------------
	// PASS 2.3: AquaTerrain DX12 Water System (Simulation & Translucent Render)
	// ----------------------------------------------------
	if (need3DSceneRender && !g_deviceLost && Eunoia::WaterSystem::Get().IsInitialized())
	{
		RecordGpuBreadcrumbOp("[Water Pass] AquaTerrain GPU Simulation");
		Eunoia::WaterSystem::Get().Simulate(g_commandList, g_scene);

		RecordGpuBreadcrumbOp("[Water Pass] AquaTerrain Water Render");
		EngineUI::ViewportRect vpRectW = g_engineUI.GetViewportRect((float)g_currentWidth, (float)g_currentHeight);
		float aspectW = (vpRectW.width > 0 && vpRectW.height > 0) ? (vpRectW.width / vpRectW.height) : 1.777f;
		if (aspectW <= 0.01f || std::isnan(aspectW)) aspectW = 1.777f;

		glm::mat4 viewW = g_camera.GetViewMatrix();
		glm::mat4 projW = g_camera.GetProjectionMatrix(aspectW);
		glm::vec3 camPosW = g_camera.GetPosition();

		Eunoia::WaterSystem::Get().Render(
			g_commandList,
			g_scene,
			viewW,
			projW,
			camPosW,
			g_depthStencilBuffer,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			g_camera.nearPlane,
			g_camera.farPlane,
			(float)g_currentWidth,
			(float)g_currentHeight,
			g_vertexBuffer,
			g_vertexBufferView,
			g_indexBuffer,
			g_indexBufferView,
			g_pVertexMapped,
			g_pIndexMapped
		);
	}

	// ----------------------------------------------------
	// PASS 2.5: Selected Camera Picture-in-Picture (PiP) Hardware Pass
	// ----------------------------------------------------
	EngineUI::CameraPipRect pipRect = g_engineUI.GetCameraPipRect((float)g_currentWidth, (float)g_currentHeight, g_scene);
	if (need3DSceneRender && !g_deviceLost && pipRect.active && g_currentIndexCount > 0 && g_numPureSceneBatches > 0)
	{
		float px = std::max(0.0f, std::min((float)g_currentWidth - 1.0f, pipRect.x));
		float py = std::max(0.0f, std::min((float)g_currentHeight - 1.0f, pipRect.y));
		float pw = std::max(1.0f, std::min((float)g_currentWidth - px, pipRect.width));
		float ph = std::max(1.0f, std::min((float)g_currentHeight - py, pipRect.height));

		D3D12_VIEWPORT pipViewport = { px, py, pw, ph, 0.0f, 1.0f };
		D3D12_RECT pipScissor = { (LONG)px, (LONG)py, (LONG)(px + pw), (LONG)(py + ph) };

		RecordGpuBreadcrumbOp("[PiP Pass] Selected Camera Preview");
		g_commandList->RSSetViewports(1, &pipViewport);
		g_commandList->RSSetScissorRects(1, &pipScissor);

		// Clear Depth & Stencil exclusively within the PiP rect for a fresh camera z-buffer
		g_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1, &pipScissor);
		// Clear PiP color rectangle to scene background clear color
		g_commandList->ClearRenderTargetView(activeSceneRtv, clearColor, 1, &pipScissor);

		// Set camera Constant Buffer from Slot 1 (Camera MVP & Pos)
		g_commandList->SetGraphicsRootSignature(g_rootSignature);
		g_commandList->SetPipelineState(g_pipelineState);
		g_commandList->SetGraphicsRootConstantBufferView(0, g_constantBuffer->GetGPUVirtualAddress() + sizeof(SceneConstantBuffer));
		g_commandList->SetGraphicsRootDescriptorTable(3, g_shadowSrvGpuHandle);
		g_commandList->SetGraphicsRootDescriptorTable(4, g_pointShadowSrvGpuHandle);
		g_commandList->SetGraphicsRootDescriptorTable(5, g_spotShadowSrvGpuHandle);
		if (g_lightmapSrvInHeap)
			g_commandList->SetGraphicsRootDescriptorTable(6, g_lightmapSrvGpuHandle);

		g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_commandList->IASetVertexBuffers(0, 1, &g_vertexBufferView);
		g_commandList->IASetIndexBuffer(&g_indexBufferView);

		// Render all pure scene geometry (batches [0, g_numPureSceneBatches))
		// This automatically excludes editor-only gizmos (lights, camera frustum lines, transform gizmo, grid)
		for (size_t bIdx = 0; bIdx < g_numPureSceneBatches; ++bIdx)
		{
			const auto& batch = g_sceneBatches[bIdx];
			if (batch.startIndex >= g_currentIndexCount || batch.indexCount == 0) continue;
			UINT drawCount = (UINT)std::min((size_t)batch.indexCount, (size_t)(g_currentIndexCount - batch.startIndex));
			if (drawCount == 0) continue;

			MaterialShaderConstants matConsts = {};
			matConsts.baseColor[0] = batch.baseColor.r;
			matConsts.baseColor[1] = batch.baseColor.g;
			matConsts.baseColor[2] = batch.baseColor.b;
			matConsts.metallic     = batch.metallic;
			matConsts.roughness    = batch.roughness;
			matConsts.normalStrength = batch.normalStrength;
			matConsts.specular     = batch.specular;
			matConsts.emissiveIntensity = batch.emissiveIntensity;
			matConsts.emissiveColor[0] = batch.emissiveColor.r;
			matConsts.emissiveColor[1] = batch.emissiveColor.g;
			matConsts.emissiveColor[2] = batch.emissiveColor.b;
			matConsts.isUnlit      = (batch.isUnlit || g_engineUI.viewMode == 1) ? 1.0f : 0.0f;
			matConsts.hasAlbedoTex = (!batch.albedoTex.empty() && batch.albedoTex != "none") ? 1.0f : 0.0f;
			matConsts.hasNormalTex = (!batch.normalTex.empty() && batch.normalTex != "none") ? 1.0f : 0.0f;
			matConsts.hasRoughTex  = (!batch.roughTex.empty() && batch.roughTex != "none")  ? 1.0f : 0.0f;
			matConsts.hasAoTex     = (!batch.aoTex.empty() && batch.aoTex != "none")        ? 1.0f : 0.0f;
			matConsts.receiveShadows = batch.receiveShadows ? 1.0f : 0.0f;
			matConsts.uvScale[0]   = batch.uvScale.x != 0.0f ? batch.uvScale.x : 1.0f;
			matConsts.uvScale[1]   = batch.uvScale.y != 0.0f ? batch.uvScale.y : 1.0f;
			matConsts.pad0         = batch.useVertexAlpha;
			matConsts.blendMode    = (float)batch.blendMode;
			matConsts.opacity      = batch.opacity;
			matConsts.opacityMaskClipValue = batch.opacityMaskClipValue > 0.0f ? batch.opacityMaskClipValue : 0.333f;
			matConsts.hasOpacityTex = (!batch.opacityTex.empty() && batch.opacityTex != "none") ? 1.0f : 0.0f;
			matConsts.normalMapYFlip = batch.normalMapYFlip ? 1.0f : 0.0f;
			matConsts.roughnessChannel = (float)batch.roughnessChannel;
			matConsts.metallicChannel = (float)batch.metallicChannel;
			matConsts.aoChannel = (float)batch.aoChannel;
			matConsts.materialDebugMode = (float)batch.materialDebugMode;

			g_commandList->SetGraphicsRoot32BitConstants(1, 32, &matConsts, 0);

			D3D12_GPU_DESCRIPTOR_HANDLE tableHandle = GetOrCreateMaterialTable(
				batch.albedoTex, batch.normalTex, batch.roughTex, batch.metalTex, batch.aoTex, batch.opacityTex);
			if (tableHandle.ptr == 0) continue;

			g_commandList->SetGraphicsRootDescriptorTable(2, tableHandle);
			g_commandList->DrawIndexedInstanced(drawCount, 1, (UINT)batch.startIndex, 0, 0);
		}

		if (g_scene.enableSkybox && g_skyboxRootSignature && g_skyboxPipelineState)
		{
			g_commandList->SetGraphicsRootSignature(g_skyboxRootSignature);
			g_commandList->SetPipelineState(g_skyboxPipelineState);

			const GameObject* selCamObj = g_scene.FindObject(pipRect.selectedCameraId);
			glm::mat4 pipView = selCamObj ? glm::inverse(g_scene.GetWorldMatrix(*selCamObj)) : g_camera.GetViewMatrix();
			glm::mat4 skyboxPipView = glm::mat4(glm::mat3(pipView));
			float pipAspect = (ph > 0.0f) ? (pw / ph) : 1.777f;
			float pipFov = (selCamObj && selCamObj->camera.fov > 0.0f) ? glm::radians(selCamObj->camera.fov) : glm::radians(45.0f);
			glm::mat4 pipProj = glm::perspective(pipFov, pipAspect, 0.1f, 500.0f);
			glm::mat4 pipInvVP = glm::inverse(pipProj * skyboxPipView);

			SkyboxShaderConstants pipSkyConsts = {};
			pipSkyConsts.invViewProj = pipInvVP;
			pipSkyConsts.cameraPos = glm::vec3(0.0f);
			pipSkyConsts.exposure = g_scene.skyboxExposure;
			pipSkyConsts.tint = g_scene.skyboxTint;
			pipSkyConsts.rotation = g_scene.skyboxRotation;
			pipSkyConsts.ambientContribution = g_scene.enableSkyboxLighting ? (g_scene.skyboxLightingIntensity * g_scene.skyboxLightingDiffuse) : 0.0f;
			pipSkyConsts.hasHdrTex = (!g_scene.skyboxTexturePath.empty() && g_scene.skyboxTexturePath != "none") ? 1.0f : 0.0f;

			g_commandList->SetGraphicsRoot32BitConstants(0, 28, &pipSkyConsts, 0);
			D3D12_GPU_DESCRIPTOR_HANDLE skyTable = GetOrCreateSkyboxTable(g_scene.skyboxTexturePath);
			if (skyTable.ptr != 0)
			{
				g_commandList->SetGraphicsRootDescriptorTable(1, skyTable);
			}
			g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			g_commandList->DrawInstanced(3, 1, 0, 0);
		}

		// Restore main viewport and scissor
		g_commandList->RSSetViewports(1, &viewport);
		g_commandList->RSSetScissorRects(1, &scissor);
	}

	// ----------------------------------------------------
	// PASS 2.55: Material Editor Live 3D PBR Hardware Preview Pass
	// Renders preview cube (default), sphere, cylinder, plane, torus, or capsule
	// using dedicated 512x512 DirectX 12 offscreen render target and studio lighting.
	// ----------------------------------------------------
	EngineUI::MatPreviewPipRect matRect = g_engineUI.GetMatPreviewPipRect();
	bool needMatPreviewRender = (!g_deviceLost && 
	                             (g_engineUI.showMaterialEditor || matRect.active) &&
	                             g_matPreviewRenderTarget && g_matPreviewDepthBuffer && 
	                             g_matPreviewVertexBuffer && g_matPreviewIndexBuffer && g_matPreviewConstantBuffer);
	if (needMatPreviewRender)
	{
		RecordGpuBreadcrumbOp("[MatPreview Pass] Live 3D PBR Material Mesh Offscreen");

		// Transition preview render target to RENDER_TARGET state
		D3D12_RESOURCE_BARRIER toRtvBarrier = CreateTransitionBarrier(
			g_matPreviewRenderTarget,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		);
		g_commandList->ResourceBarrier(1, &toRtvBarrier);

		// Bind dedicated 512x512 preview RTV and DSV
		g_commandList->OMSetRenderTargets(1, &g_matPreviewRtvHandle, FALSE, &g_matPreviewDsvHandle);

		D3D12_VIEWPORT matViewport = { 0.0f, 0.0f, 512.0f, 512.0f, 0.0f, 1.0f };
		D3D12_RECT matScissor = { 0, 0, 512, 512 };
		g_commandList->RSSetViewports(1, &matViewport);
		g_commandList->RSSetScissorRects(1, &matScissor);

		// Clear Preview RT with theme background #1E1E1E and Depth buffer to 1.0f
		const float matPreviewBg[4] = { 0.118f, 0.118f, 0.118f, 1.0f };
		g_commandList->ClearRenderTargetView(g_matPreviewRtvHandle, matPreviewBg, 0, nullptr);
		g_commandList->ClearDepthStencilView(g_matPreviewDsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

		// Check if preview geometry needs to be rebuilt/uploaded
		int targetMeshType = g_engineUI.matEditorPreviewMesh;
		if (g_currentMatPreviewMeshType != targetMeshType || g_currentMatPreviewIndexCount == 0)
		{
			PrimitiveMesh pMesh;
			if (targetMeshType == 0) { // Cube (Default)
				pMesh = GeometryBuilder::CreateBox(1.15f, 1.15f, 1.15f, 4, 4, 4);
			} else if (targetMeshType == 1) { // Sphere
				pMesh = GeometryBuilder::CreateSphere(0.68f, 32, 32);
			} else if (targetMeshType == 2) { // Cylinder
				pMesh = GeometryBuilder::CreateCylinder(0.55f, 1.15f, 32, 2, true, true);
			} else if (targetMeshType == 3) { // Plane
				pMesh = GeometryBuilder::CreatePlane(1.6f, 1.6f, 8, 8);
			} else if (targetMeshType == 4) { // Torus
				pMesh = GeometryBuilder::CreateTorus(0.55f, 0.22f, 32, 24);
			} else if (targetMeshType == 5) { // Capsule
				pMesh = GeometryBuilder::CreateCapsule(0.40f, 0.75f, 24, 4, 12);
			} else {
				pMesh = GeometryBuilder::CreateBox(1.15f, 1.15f, 1.15f, 4, 4, 4);
			}

			GeometryBuilder::OptimizeMesh(pMesh);

			size_t vCount = std::min(pMesh.vertices.size(), MAX_MAT_PREVIEW_VERTICES);
			size_t iCount = std::min(pMesh.indices.size(), MAX_MAT_PREVIEW_INDICES);

			if (g_pMatPreviewVertexMapped && vCount > 0)
			{
				std::vector<Vertex> vtxList(vCount);
				for (size_t vi = 0; vi < vCount; ++vi)
				{
					vtxList[vi].position = pMesh.vertices[vi].pos;
					vtxList[vi].normal   = pMesh.vertices[vi].normal;
					vtxList[vi].uv       = pMesh.vertices[vi].uv;
					vtxList[vi].color    = glm::vec3(1.0f);
					vtxList[vi].tangent  = pMesh.vertices[vi].tangent;
				}
				memcpy(g_pMatPreviewVertexMapped, vtxList.data(), vCount * sizeof(Vertex));
			}

			if (g_pMatPreviewIndexMapped && iCount > 0)
			{
				memcpy(g_pMatPreviewIndexMapped, pMesh.indices.data(), iCount * sizeof(uint32_t));
			}

			g_currentMatPreviewMeshType = targetMeshType;
			g_currentMatPreviewIndexCount = (uint32_t)iCount;
		}

		// Update continuous turntable rotation if auto-rotate is enabled
		if (g_engineUI.matEditorAutoRotate)
		{
			g_engineUI.matEditorRotationAngle += 0.45f;
			if (g_engineUI.matEditorRotationAngle >= 360.0f)
				g_engineUI.matEditorRotationAngle -= 360.0f;
		}

		// Configure Preview Camera MVP & Studio Lighting
		glm::vec3 camPos(0.0f, 0.0f, 2.3f);
		glm::vec3 camTarget(0.0f, 0.0f, 0.0f);
		glm::vec3 camUp(0.0f, 1.0f, 0.0f);
		if (targetMeshType == 3) { // Plane
			camPos = glm::vec3(0.0f, 1.4f, 1.8f);
		}
		glm::mat4 matView = glm::lookAt(camPos, camTarget, camUp);
		glm::mat4 matProj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 50.0f);
		glm::mat4 matModel = glm::mat4(1.0f);

		float rotRad = glm::radians(g_engineUI.matEditorRotationAngle);
		if (targetMeshType == 0) { // Cube
			// Isometric view angle: pitch down 22 deg, yaw -35 deg + rotation
			matModel = glm::rotate(matModel, glm::radians(22.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			matModel = glm::rotate(matModel, rotRad - glm::radians(35.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		} else if (targetMeshType == 3) { // Plane
			matModel = glm::rotate(matModel, glm::radians(-15.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			matModel = glm::rotate(matModel, rotRad, glm::vec3(0.0f, 1.0f, 0.0f));
		} else {
			matModel = glm::rotate(matModel, glm::radians(10.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			matModel = glm::rotate(matModel, rotRad, glm::vec3(0.0f, 1.0f, 0.0f));
		}

		// Studio lighting calculations
		float rad = glm::radians(g_engineUI.matEditorLightAngle);
		glm::vec3 previewLightDir = glm::normalize(glm::vec3(sinf(rad) * 0.7f, -cosf(rad) * 0.5f - 0.3f, 0.75f));
		glm::vec3 previewLightCol = glm::vec3(1.0f);
		glm::vec3 previewAmbientCol = glm::vec3(0.12f, 0.13f, 0.16f);
		float previewAmbientIntensity = 0.5f;

		int preset = g_engineUI.matEditorLightPreset;
		if (preset == 1) { // Studio 3-Point
			previewLightCol = glm::vec3(1.0f, 0.98f, 0.94f) * 1.3f;
			previewAmbientCol = glm::vec3(0.15f, 0.17f, 0.22f);
			previewAmbientIntensity = 0.65f;
		} else if (preset == 2) { // Warm Sunset
			previewLightCol = glm::vec3(1.0f, 0.65f, 0.35f) * 1.4f;
			previewAmbientCol = glm::vec3(0.20f, 0.10f, 0.09f);
			previewAmbientIntensity = 0.55f;
		} else if (preset == 3) { // Cool Moonlight
			previewLightCol = glm::vec3(0.55f, 0.75f, 1.0f) * 1.2f;
			previewAmbientCol = glm::vec3(0.06f, 0.10f, 0.18f);
			previewAmbientIntensity = 0.45f;
		}

		SceneConstantBuffer matCb = {};
		matCb.mvp = matProj * matView * matModel;
		matCb.cameraPos = camPos;
		matCb.ambientIntensity = previewAmbientIntensity;
		matCb.ambientColor = glm::vec4(previewAmbientCol, 1.0f);
		matCb.lightDir = previewLightDir;
		matCb.lightColor = previewLightCol;
		matCb.shadowStrength = 0.0f;
		matCb.enableShadows = 0.0f;
		matCb.enablePostProcessing = 0.0f;
		matCb.numPointLights = 0.0f;

		// Studio secondary fill light (Point light 0)
		if (preset == 1) {
			matCb.numPointLights = 1.0f;
			matCb.pointLightPosRange[0] = glm::vec4(-1.8f, 1.2f, 1.2f, 15.0f);
			matCb.pointLightColorIntensity[0] = glm::vec4(0.45f, 0.55f, 0.75f, 1.2f);
			matCb.pointLightDirType[0] = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // Point light
		}

		if (g_pMatPreviewConstantMapped)
		{
			memcpy(g_pMatPreviewConstantMapped, &matCb, sizeof(matCb));
		}

		// Upload active material shader constants from EngineUI activeMaterial
		const auto& curMat = g_engineUI.activeMaterial;

		g_commandList->SetGraphicsRootSignature(g_rootSignature);
		ID3D12PipelineState* matPreviewPso = (curMat.twoSided && g_pipelineStateTwoSided) ? g_pipelineStateTwoSided : g_pipelineState;
		g_commandList->SetPipelineState(matPreviewPso);
		g_commandList->SetGraphicsRootConstantBufferView(0, g_matPreviewConstantBuffer->GetGPUVirtualAddress());
		g_commandList->SetGraphicsRootDescriptorTable(3, g_shadowSrvGpuHandle);
		g_commandList->SetGraphicsRootDescriptorTable(4, g_pointShadowSrvGpuHandle);
		g_commandList->SetGraphicsRootDescriptorTable(5, g_spotShadowSrvGpuHandle);
		if (g_lightmapSrvInHeap)
			g_commandList->SetGraphicsRootDescriptorTable(6, g_lightmapSrvGpuHandle);

		g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_commandList->IASetVertexBuffers(0, 1, &g_matPreviewVertexBufferView);
		g_commandList->IASetIndexBuffer(&g_matPreviewIndexBufferView);

		MaterialShaderConstants matConsts = {};
		matConsts.baseColor[0] = curMat.baseColor.r;
		matConsts.baseColor[1] = curMat.baseColor.g;
		matConsts.baseColor[2] = curMat.baseColor.b;
		matConsts.metallic     = curMat.metallic;
		matConsts.roughness    = curMat.roughness;
		matConsts.normalStrength = curMat.normalStrength;
		matConsts.specular     = curMat.specular;
		matConsts.emissiveIntensity = curMat.emissiveIntensity;
		matConsts.emissiveColor[0] = curMat.emissiveColor.r;
		matConsts.emissiveColor[1] = curMat.emissiveColor.g;
		matConsts.emissiveColor[2] = curMat.emissiveColor.b;
		matConsts.isUnlit      = (curMat.shadingModel == 1) ? 1.0f : 0.0f;
		matConsts.hasAlbedoTex = (!curMat.baseColorTexture.empty() && curMat.baseColorTexture != "none") ? 1.0f : 0.0f;
		matConsts.hasNormalTex = (!curMat.normalTexture.empty() && curMat.normalTexture != "none") ? 1.0f : 0.0f;
		matConsts.hasRoughTex  = (!curMat.roughnessTexture.empty() && curMat.roughnessTexture != "none") ? 1.0f : 0.0f;
		matConsts.hasAoTex     = (!curMat.aoTexture.empty() && curMat.aoTexture != "none") ? 1.0f : 0.0f;
		matConsts.receiveShadows = 0.0f;
		matConsts.uvScale[0]   = curMat.uvScale.x != 0.0f ? curMat.uvScale.x : 1.0f;
		matConsts.uvScale[1]   = curMat.uvScale.y != 0.0f ? curMat.uvScale.y : 1.0f;
		matConsts.blendMode    = (float)curMat.blendMode;
		matConsts.opacity      = curMat.opacity;
		matConsts.opacityMaskClipValue = curMat.opacityMaskClipValue > 0.0f ? curMat.opacityMaskClipValue : 0.333f;
		matConsts.hasOpacityTex = (!curMat.opacityTexture.empty() && curMat.opacityTexture != "none") ? 1.0f : 0.0f;
		matConsts.normalMapYFlip = curMat.normalMapYFlip ? 1.0f : 0.0f;
		matConsts.roughnessChannel = (float)curMat.roughnessChannel;
		matConsts.metallicChannel = (float)curMat.metallicChannel;
		matConsts.aoChannel = (float)curMat.aoChannel;
		matConsts.materialDebugMode = (float)curMat.materialDebugMode;

		g_commandList->SetGraphicsRoot32BitConstants(1, 32, &matConsts, 0);

		D3D12_GPU_DESCRIPTOR_HANDLE matTable = GetOrCreateMaterialTable(
			curMat.baseColorTexture,
			curMat.normalTexture,
			curMat.roughnessTexture,
			curMat.metallicTexture,
			curMat.aoTexture,
			curMat.opacityTexture,
			curMat.emissionTexture
		);

		if (matTable.ptr != 0 && g_currentMatPreviewIndexCount > 0)
		{
			g_commandList->SetGraphicsRootDescriptorTable(2, matTable);
			g_commandList->DrawIndexedInstanced(g_currentMatPreviewIndexCount, 1, 0, 0, 0);
		}

		// Transition preview render target back to PIXEL_SHADER_RESOURCE state for ImGui presentation
		D3D12_RESOURCE_BARRIER toSrvBarrier = CreateTransitionBarrier(
			g_matPreviewRenderTarget,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		);
		g_commandList->ResourceBarrier(1, &toSrvBarrier);

		if (g_matPreviewSrvGpuHandle.ptr != 0)
		{
			g_engineUI.matPreviewTextureId = g_matPreviewSrvGpuHandle.ptr;
		}

		// Restore main scene render target, viewport and scissor
		g_commandList->OMSetRenderTargets(1, &activeSceneRtv, FALSE, &dsvHandle);
		g_commandList->RSSetViewports(1, &viewport);
		g_commandList->RSSetScissorRects(1, &scissor);
	}

	// ----------------------------------------------------
	// PASS 2.4: Volumetric Fog & God Rays Pass (Wicked Engine Technique)
	// Additively accumulates volumetric light scattering into g_sceneRenderTarget
	// ----------------------------------------------------
	bool anyVolumetric = (g_scene.directionalVolumetric && g_scene.lightIntensity > 0.001f);
	if (!anyVolumetric)
	{
		for (const auto& pl : g_scene.pointLights)
		{
			if (pl.enabled && pl.volumetric && (pl.intensity > 0.001f) && (pl.volumetricIntensity > 0.001f))
			{
				anyVolumetric = true;
				break;
			}
		}
	}

	bool needGodRays = canRender3DScene && !g_deviceLost && g_scene.enableFog && anyVolumetric &&
	                   g_godRayPipelineState && g_godRayRootSignature && g_godRayConstantBuffer &&
	                   g_godRayTableGpuHandle.ptr != 0 && g_shadowDepthBuffer && g_depthStencilBuffer;
	if (needGodRays)
	{
		RecordGpuBreadcrumbOp("[GodRays] Volumetric Light Pass");

		// Transition depth buffer to PIXEL_SHADER_RESOURCE for reading in pixel shader
		D3D12_RESOURCE_BARRIER toDepthSrv = CreateTransitionBarrier(
			g_depthStencilBuffer,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		);
		g_commandList->ResourceBarrier(1, &toDepthSrv);

		// Output additively to scene render target
		g_commandList->OMSetRenderTargets(1, &activeSceneRtv, FALSE, nullptr);
		g_commandList->RSSetViewports(1, &viewport);
		g_commandList->RSSetScissorRects(1, &scissor);

		EngineUI::ViewportRect vpRect = g_engineUI.GetViewportRect((float)g_currentWidth, (float)g_currentHeight);
		float aspect = (vpRect.width > 0 && vpRect.height > 0) ? (vpRect.width / vpRect.height) : 1.777f;
		if (aspect <= 0.01f || std::isnan(aspect)) aspect = 1.777f;

		glm::mat4 view = g_camera.GetViewMatrix();
		glm::mat4 proj = g_camera.GetProjectionMatrix(aspect);
		glm::mat4 invVP = glm::inverse(proj * view);

		GodRayConstants grc = {};
		grc.invViewProj = invVP;
		grc.dirLightSpaceMatrix = g_currentDirLightSpaceMatrix;
		grc.cameraPos = g_camera.GetPosition();
		grc.pad0 = 0.0f;
		grc.dirLightDir = g_currentDirLightDir;
		grc.dirLightIntensity = g_scene.lightIntensity;
		grc.dirLightColor = g_scene.lightColor;
		grc.pad1 = 0.0f;

		if (g_scene.directionalVolumetric)
		{
			grc.dirVolumetricParams = glm::vec4(
				1.0f,
				g_scene.directionalVolumetricBoost,
				std::clamp(g_scene.directionalVolumetricScattering, 0.0f, 0.95f),
				g_scene.directionalVolumetricIntensity
			);
		}
		else
		{
			grc.dirVolumetricParams = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
		}

		grc.dirExtraParams = glm::vec4(
			(float)std::clamp(g_scene.directionalVolumetricSteps, 8, 64),
			(g_scene.directionalVolumetricMaxDistance > 1.0f) ? g_scene.directionalVolumetricMaxDistance : 120.0f,
			g_scene.shadowBias,
			(float)g_scene.shadowResolution
		);

		grc.fogDensity = g_scene.fogDensity;
		grc.fogStart = g_scene.fogStart;
		grc.fogHeightStart = g_scene.fogHeightStart;
		grc.fogHeightEnd = g_scene.fogHeightEnd;
		grc.fogColor = glm::vec4(g_scene.fogColor, 1.0f);
		grc.screenSize = glm::vec4((float)g_currentWidth, (float)g_currentHeight, 1.0f / (float)g_currentWidth, 1.0f / (float)g_currentHeight);

		float maxVolDist = g_scene.directionalVolumetric ? g_scene.directionalVolumetricMaxDistance : 50.0f;
		int maxSteps = g_scene.directionalVolumetric ? g_scene.directionalVolumetricSteps : 16;
		int volLightCount = 0;

		for (size_t i = 0; i < g_scene.pointLights.size() && volLightCount < 16; ++i)
		{
			const auto& pl = g_scene.pointLights[i];
			if (!pl.enabled || !pl.volumetric || pl.intensity <= 0.001f || pl.volumetricIntensity <= 0.001f)
				continue;

			GodRayLightData& ld = grc.lights[volLightCount++];
			ld.posRange = glm::vec4(pl.position, std::max(pl.range, 0.1f));
			ld.colorIntensity = glm::vec4(pl.color, pl.intensity);

			glm::vec3 dir = pl.direction;
			float dLen = glm::length(dir);
			if (dLen > 0.0001f) dir /= dLen;
			else dir = glm::vec3(0.0f, -1.0f, 0.0f);
			ld.dirType = glm::vec4(dir, (float)pl.type);

			float cosInner = std::cos(glm::radians(std::min(pl.innerConeAngle, pl.outerConeAngle)));
			float cosOuter = std::cos(glm::radians(std::max(pl.innerConeAngle, pl.outerConeAngle)));
			ld.spotParams = glm::vec4(cosInner, cosOuter, pl.attenuation, std::clamp(pl.rangeSmoothing, 0.001f, 1.0f));

			ld.volumetricParams = glm::vec4(
				1.0f,
				std::max(pl.volumetric_boost, 0.0f),
				std::clamp(pl.volumetricScattering, 0.0f, 0.95f),
				std::max(pl.volumetricIntensity, 0.0f)
			);

			int shadowSlot = -1;
			auto itSlot = g_frameLightShadowSlot.find(i);
			if (itSlot != g_frameLightShadowSlot.end() && pl.castShadows)
			{
				shadowSlot = itSlot->second;
			}

			ld.shadowParams = glm::vec4(
				pl.volumetricMaxDistance,
				(float)shadowSlot,
				(float)pl.shadowResolution,
				pl.shadowBias
			);

			if (shadowSlot >= 0 && shadowSlot < (int)MAX_SHADOW_SPOT_LIGHTS)
			{
				ld.shadowMatrix = g_currentSpotLightSpaceMatrices[shadowSlot];
			}
			else
			{
				float fov = (pl.type == LightType::Spot) ? std::max(pl.outerConeAngle * 2.0f, 5.0f) : 120.0f;
				fov = std::min(fov, 160.0f);
				float nearZ = 0.05f;
				float farZ = std::max(pl.range, 5.0f);
				glm::mat4 spotProj = glm::perspectiveLH_ZO(glm::radians(fov), 1.0f, nearZ, farZ);
				glm::vec3 spotUp = (std::abs(dir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
				glm::mat4 spotView = glm::lookAtLH(pl.position, pl.position + dir, spotUp);
				ld.shadowMatrix = spotProj * spotView;
			}

			if (pl.volumetricMaxDistance > maxVolDist) maxVolDist = pl.volumetricMaxDistance;
			if (pl.volumetricSteps > maxSteps) maxSteps = pl.volumetricSteps;
		}

		grc.numLights = (float)volLightCount;
		grc.maxRayDistance = (maxVolDist > 1.0f) ? maxVolDist : 120.0f;
		grc.globalSampleSteps = (float)std::clamp(maxSteps, 8, 64);
		grc.spotShadowAllocRes = (float)g_currentSpotShadowRes;

		if (g_pGodRayConstantMapped) {
			memcpy(g_pGodRayConstantMapped, &grc, sizeof(grc));
		}

		g_commandList->SetGraphicsRootSignature(g_godRayRootSignature);
		g_commandList->SetPipelineState(g_godRayPipelineState);

		ID3D12DescriptorHeap* descriptorHeaps[] = { g_srvDescHeap };
		g_commandList->SetDescriptorHeaps(1, descriptorHeaps);

		g_commandList->SetGraphicsRootConstantBufferView(0, g_godRayConstantBuffer->GetGPUVirtualAddress());
		g_commandList->SetGraphicsRootDescriptorTable(1, g_godRayTableGpuHandle);

		g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_commandList->DrawInstanced(3, 1, 0, 0);

		// Transition depth buffer back to DEPTH_WRITE
		D3D12_RESOURCE_BARRIER toDepthWrite = CreateTransitionBarrier(
			g_depthStencilBuffer,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_DEPTH_WRITE
		);
		g_commandList->ResourceBarrier(1, &toDepthWrite);
	}

	// ----------------------------------------------------
	// PASS 2.55: Ambient Occlusion Pass (Wicked Engine Technique)
	// SSAO / HBAO / MSAO / RTAO with Cross-Bilateral Depth-Aware Filtering
	// ----------------------------------------------------
	bool needAO = canRender3DScene && !g_deviceLost && g_scene.enableAO && g_scene.aoType > 0 &&
	              g_aoGeneratePipelineState && g_aoBlurPipelineState && g_aoCompositePipelineState &&
	              g_aoRootSignature && g_aoConstantBuffer && g_aoTableGpuHandle.ptr != 0 &&
	              g_aoRenderTarget && g_depthStencilBuffer;
	if (needAO)
	{
		RecordGpuBreadcrumbOp("[AO] Ambient Occlusion Pass");

		// Transition depth buffer to PIXEL_SHADER_RESOURCE for reading in pixel shader
		D3D12_RESOURCE_BARRIER toDepthSrv = CreateTransitionBarrier(
			g_depthStencilBuffer,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		);
		g_commandList->ResourceBarrier(1, &toDepthSrv);

		EngineUI::ViewportRect vpRect = g_engineUI.GetViewportRect((float)g_currentWidth, (float)g_currentHeight);
		float aspect = (vpRect.width > 0 && vpRect.height > 0) ? (vpRect.width / vpRect.height) : 1.777f;
		if (aspect <= 0.01f || std::isnan(aspect)) aspect = 1.777f;

		glm::mat4 view = g_camera.GetViewMatrix();
		glm::mat4 proj = g_camera.GetProjectionMatrix(aspect);
		glm::mat4 invProj = glm::inverse(proj);

		AOConstants aoc = {};
		aoc.viewMatrix = view;
		aoc.projMatrix = proj;
		aoc.invProjMatrix = invProj;
		aoc.resolution = glm::vec4((float)g_currentWidth, (float)g_currentHeight, 1.0f / (float)g_currentWidth, 1.0f / (float)g_currentHeight);
		aoc.cameraParams = glm::vec4(g_camera.nearPlane, g_camera.farPlane, g_camera.fov, aspect);
		aoc.viewportParams = glm::vec4(viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height);
		aoc.aoParams = glm::vec4(
			std::max(g_scene.aoPower, 0.25f),
			std::max(g_scene.aoRange, 0.05f),
			(float)std::clamp(g_scene.aoSampleCount, 1, 64),
			std::max(g_scene.aoBias, 0.0001f)
		);
		aoc.aoConfig = glm::uvec4(
			(uint32_t)std::clamp(g_scene.aoType, 1, 4),
			g_scene.aoBlur ? 1u : 0u,
			(uint32_t)std::clamp((int)g_scene.aoBlurRadius, 1, 8),
			(uint32_t)g_globalFrameCounter
		);

		if (g_pAoConstantMapped)
		{
			memcpy(g_pAoConstantMapped, &aoc, sizeof(aoc));
		}

		g_commandList->SetGraphicsRootSignature(g_aoRootSignature);
		ID3D12DescriptorHeap* descriptorHeaps[] = { g_srvDescHeap };
		g_commandList->SetDescriptorHeaps(1, descriptorHeaps);
		g_commandList->SetGraphicsRootConstantBufferView(0, g_aoConstantBuffer->GetGPUVirtualAddress());

		// 1. AO GENERATE PASS -> renders to g_aoRenderTarget
		g_commandList->OMSetRenderTargets(1, &g_aoRtvHandle, FALSE, nullptr);
		g_commandList->RSSetViewports(1, &viewport);
		g_commandList->RSSetScissorRects(1, &scissor);
		g_commandList->SetPipelineState(g_aoGeneratePipelineState);
		g_commandList->SetGraphicsRootDescriptorTable(1, g_aoTableGpuHandle);

		g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_commandList->DrawInstanced(3, 1, 0, 0);

		// Transition g_aoRenderTarget to PIXEL_SHADER_RESOURCE
		D3D12_RESOURCE_BARRIER toAoRawSrv = CreateTransitionBarrier(
			g_aoRenderTarget,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		);
		g_commandList->ResourceBarrier(1, &toAoRawSrv);

		D3D12_GPU_DESCRIPTOR_HANDLE finalAoSourceHandle = g_aoTableGpuHandle;

		// 2. AO BILATERAL BLUR PASS (if enabled) -> renders to g_aoBlurRenderTarget
		if (g_scene.aoBlur && g_aoBlurRenderTarget)
		{
			D3D12_RESOURCE_BARRIER toBlurRt = CreateTransitionBarrier(
				g_aoBlurRenderTarget,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			);
			g_commandList->ResourceBarrier(1, &toBlurRt);

			g_commandList->OMSetRenderTargets(1, &g_aoBlurRtvHandle, FALSE, nullptr);
			g_commandList->RSSetViewports(1, &viewport);
			g_commandList->RSSetScissorRects(1, &scissor);
			g_commandList->SetPipelineState(g_aoBlurPipelineState);
			g_commandList->SetGraphicsRootDescriptorTable(1, g_aoTableGpuHandle);

			g_commandList->DrawInstanced(3, 1, 0, 0);

			D3D12_RESOURCE_BARRIER toBlurSrv = CreateTransitionBarrier(
				g_aoBlurRenderTarget,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			);
			g_commandList->ResourceBarrier(1, &toBlurSrv);

			// Slot 1: register(t1) binds Slot 2 (AO Blurred)
			finalAoSourceHandle.ptr += (UINT64)g_srvDescriptorSize;
		}

		// 3. AO COMPOSITE PASS -> modulates activeSceneRtv with multiplicative blend
		g_commandList->OMSetRenderTargets(1, &activeSceneRtv, FALSE, nullptr);
		g_commandList->RSSetViewports(1, &viewport);
		g_commandList->RSSetScissorRects(1, &scissor);
		g_commandList->SetPipelineState(g_aoCompositePipelineState);
		g_commandList->SetGraphicsRootDescriptorTable(1, finalAoSourceHandle);

		g_commandList->DrawInstanced(3, 1, 0, 0);

		// Restore g_aoRenderTarget to RENDER_TARGET for next frame
		D3D12_RESOURCE_BARRIER restoreAoRt = CreateTransitionBarrier(
			g_aoRenderTarget,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		);
		g_commandList->ResourceBarrier(1, &restoreAoRt);

		// Transition depth buffer back to DEPTH_WRITE
		D3D12_RESOURCE_BARRIER toDepthWrite = CreateTransitionBarrier(
			g_depthStencilBuffer,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_DEPTH_WRITE
		);
		g_commandList->ResourceBarrier(1, &toDepthWrite);
	}

	// ----------------------------------------------------
	// PASS 2.6: Real-Time Anti-Aliasing Resolve Pass (dev.md)
	// Resolves g_sceneRenderTarget to backbuffer with active AA method
	// ----------------------------------------------------
	if (canRender3DScene && g_sceneRenderTarget && g_antiAliasingPipelineState && g_antiAliasingRootSignature)
	{
		RecordGpuBreadcrumbOp("[AntiAliasing] Resolve Pass");

		// Transition scene render target to pixel shader resource
		D3D12_RESOURCE_BARRIER toSrvBarrier = CreateTransitionBarrier(
			g_sceneRenderTarget,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		);
		g_commandList->ResourceBarrier(1, &toSrvBarrier);

		// Output to BackBuffer inside 3D viewport region
		g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
		g_commandList->RSSetViewports(1, &viewport);
		g_commandList->RSSetScissorRects(1, &scissor);

		g_commandList->SetGraphicsRootSignature(g_antiAliasingRootSignature);
		g_commandList->SetPipelineState(g_antiAliasingPipelineState);

		ID3D12DescriptorHeap* descriptorHeaps[] = { g_srvDescHeap };
		g_commandList->SetDescriptorHeaps(1, descriptorHeaps);

		// Root descriptor table t0..t1 (Current scene + History)
		g_commandList->SetGraphicsRootDescriptorTable(1, g_sceneSrvGpuHandle);

		float effectiveSharpen = g_scene.enablePostProcessing ? g_scene.postSharpenAmount : 0.0f;
		float effectiveDenoiser = g_scene.enablePostProcessing ? g_scene.postDenoiserAmount : 0.0f;

		uint32_t aaMethodVal = (uint32_t)Eunoia::GraphicsSystem::Get().GetAntiAliasingMethod();
		struct alignas(16) {
			uint32_t method;
			float totalWidth;
			float totalHeight;
			float invTotalWidth;
			float invTotalHeight;
			float jitterX;
			float jitterY;
			uint32_t frameCount;
			float sharpness;
			float denoiser;
			float pad[2];
		} aaParams = {
			aaMethodVal,
			(float)g_currentWidth,
			(float)g_currentHeight,
			1.0f / (float)g_currentWidth,
			1.0f / (float)g_currentHeight,
			need3DSceneRender ? g_currentJitter.x : 0.0f,
			need3DSceneRender ? g_currentJitter.y : 0.0f,
			(uint32_t)g_globalFrameCounter,
			effectiveSharpen,
			effectiveDenoiser,
			{ 0.0f, 0.0f }
		};
		g_commandList->SetGraphicsRoot32BitConstants(0, 12, &aaParams, 0);

		// Draw full-screen triangle to resolve into viewport area of BackBuffer
		g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_commandList->DrawInstanced(3, 1, 0, 0);

		// If temporal AA is active, copy current frame to history buffer
		bool isTemporal = (aaMethodVal == 1 || aaMethodVal == 2 || aaMethodVal >= 8);
		if (need3DSceneRender && isTemporal && g_sceneHistoryRenderTarget)
		{
			D3D12_RESOURCE_BARRIER copyBarriers[2] = {
				CreateTransitionBarrier(g_sceneRenderTarget, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE),
				CreateTransitionBarrier(g_sceneHistoryRenderTarget, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)
			};
			g_commandList->ResourceBarrier(2, copyBarriers);

			g_commandList->CopyResource(g_sceneHistoryRenderTarget, g_sceneRenderTarget);

			D3D12_RESOURCE_BARRIER restoreBarriers[2] = {
				CreateTransitionBarrier(g_sceneRenderTarget, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
				CreateTransitionBarrier(g_sceneHistoryRenderTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
			};
			g_commandList->ResourceBarrier(2, restoreBarriers);
		}
		else
		{
			// Transition scene target back to RENDER_TARGET for next frame
			D3D12_RESOURCE_BARRIER toRtBarrier = CreateTransitionBarrier(
				g_sceneRenderTarget,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			);
			g_commandList->ResourceBarrier(1, &toRtBarrier);
		}
	}
	else if (!canRender3DScene && !g_engineUI.isGameOnlyWindow)
	{
		const float vpBg[4] = { 0.118f, 0.118f, 0.118f, 1.0f };
		g_commandList->ClearRenderTargetView(rtvHandle, vpBg, 1, &scissor);
	}

	// Reset viewport & scissor for ImGui to full window
	D3D12_VIEWPORT guiViewport = { 0.0f, 0.0f, (float)g_currentWidth, (float)g_currentHeight, 0.0f, 1.0f };
	D3D12_RECT guiScissor = { 0, 0, (LONG)g_currentWidth, (LONG)g_currentHeight };
	g_commandList->RSSetViewports(1, &guiViewport);
	g_commandList->RSSetScissorRects(1, &guiScissor);
	g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	// 2. Render Unreal Engine 5 UI with Dear ImGui
	ImDrawData* drawData = ImGui::GetDrawData();
	if (drawData)
	{
		ImGui_ImplDX12_RenderDrawData(drawData, g_commandList);
	}

	// 3. Standalone Custom UI render pass removed - purely Dear ImGui

	// Transition BackBuffer to Present
	barrier = CreateTransitionBarrier(
		g_renderTargets[g_frameIndex],
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT
	);
	char bufPresentBarrier[96];
	snprintf(bufPresentBarrier, sizeof(bufPresentBarrier), "[Barrier] BackBuffer[%u] -> PRESENT", g_frameIndex);
	RecordGpuBreadcrumbOp(bufPresentBarrier);
	g_commandList->ResourceBarrier(1, &barrier);

	g_commandList->Close();

	ID3D12CommandList* commandLists[] = { g_commandList };
	g_commandQueue->ExecuteCommandLists(1, commandLists);
	g_frameOpHistory[g_frameIndex] = g_recordedFrameOps;

	UINT syncInterval = g_engineUI.vsyncEnabled ? 1 : 0;
	UINT presentFlags = (syncInterval == 0 && g_tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
	HRESULT hrPresent = g_swapChain->Present(syncInterval, presentFlags);
	if (hrPresent == DXGI_ERROR_DEVICE_REMOVED || hrPresent == DXGI_ERROR_DEVICE_RESET)
	{
		g_deviceLost = true;
		HRESULT reason = g_d3dDevice ? g_d3dDevice->GetDeviceRemovedReason() : hrPresent;
		std::cerr << "[RECOVERY] Present detected device loss (0x" << std::hex << reason << std::dec << ")\n";
		DumpDREDInformation();
		g_engineUI.AddLog("LogRecovery", "GPU Device Lost/Removed detected on Present. Please save work and restart editor.", 3);
		return;
	}

	// Signal fence for current frame
	const UINT64 currentFenceValue = ++g_globalFenceValue;
	g_commandQueue->Signal(g_fence, currentFenceValue);
	g_fenceValues[g_frameIndex] = currentFenceValue;

	// Advance to next backbuffer
	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

	// Pipelined GPU-CPU overlap: we do NOT block the CPU here!
	// Next frame's CPU recording will overlap with the GPU's execution of this frame.
	// We only synchronize at the start of the next frame before re-using that specific backbuffer.

	// Update non-stalling readback statistics for Mesh Cluster Culling (dev.md Section 14)
	if (Eunoia::MeshClusterCullingSystem::Get().IsInitialized())
	{
		Eunoia::MeshClusterCullingSystem::Get().FetchStatistics();
	}

	// Decrement on-demand viewport redraw settle counter
	if (g_engineUI.viewportRedrawCountdown > 0)
	{
		g_engineUI.viewportRedrawCountdown--;
	}
}

void cleanUpD3D12()
{
	WaitForGpuIdle();
	FlushDeferredReleases();
	EngineRenderer::TextureStreamingSystem::Get().Shutdown();
	Eunoia::MeshClusterCullingSystem::Get().Shutdown();
	Eunoia::WaterSystem::Get().Shutdown();
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	if (g_vertexBuffer) { g_vertexBuffer->Unmap(0, nullptr); g_vertexBuffer->Release(); g_vertexBuffer = nullptr; }
	if (g_indexBuffer) { g_indexBuffer->Unmap(0, nullptr); g_indexBuffer->Release(); g_indexBuffer = nullptr; }
	if (g_constantBuffer) { g_constantBuffer->Unmap(0, nullptr); g_constantBuffer->Release(); g_constantBuffer = nullptr; }
	if (g_shadowConstantBuffer) { g_shadowConstantBuffer->Unmap(0, nullptr); g_shadowConstantBuffer->Release(); g_shadowConstantBuffer = nullptr; }
	if (g_matPreviewVertexBuffer) { g_matPreviewVertexBuffer->Unmap(0, nullptr); g_matPreviewVertexBuffer->Release(); g_matPreviewVertexBuffer = nullptr; }
	if (g_matPreviewRenderTarget) { g_matPreviewRenderTarget->Release(); g_matPreviewRenderTarget = nullptr; }
	if (g_matPreviewDepthBuffer) { g_matPreviewDepthBuffer->Release(); g_matPreviewDepthBuffer = nullptr; }
	if (g_matPreviewIndexBuffer) { g_matPreviewIndexBuffer->Unmap(0, nullptr); g_matPreviewIndexBuffer->Release(); g_matPreviewIndexBuffer = nullptr; }
	if (g_matPreviewConstantBuffer) { g_matPreviewConstantBuffer->Unmap(0, nullptr); g_matPreviewConstantBuffer->Release(); g_matPreviewConstantBuffer = nullptr; }
	g_pVertexMapped = nullptr;
	g_pIndexMapped = nullptr;
	g_pConstantMapped = nullptr;
	g_pShadowConstantMapped = nullptr;
	g_pMatPreviewVertexMapped = nullptr;
	g_pMatPreviewIndexMapped = nullptr;
	g_pMatPreviewConstantMapped = nullptr;

	for (auto* tex : g_allocatedTextures) {
		if (tex) tex->Release();
	}
	g_allocatedTextures.clear();
	g_gpuTextureMap.clear();
	g_materialDescriptorTables.clear();

	if (g_depthStencilBuffer) { g_depthStencilBuffer->Release(); g_depthStencilBuffer = nullptr; }
	if (g_shadowDepthBuffer) { g_shadowDepthBuffer->Release(); g_shadowDepthBuffer = nullptr; }
	if (g_pointShadowDepthBuffer) { g_pointShadowDepthBuffer->Release(); g_pointShadowDepthBuffer = nullptr; }
	if (g_spotShadowDepthBuffer) { g_spotShadowDepthBuffer->Release(); g_spotShadowDepthBuffer = nullptr; }

	for (UINT i = 0; i < FRAME_COUNT; i++)
	{
		if (g_commandLists[i]) { g_commandLists[i]->Release(); g_commandLists[i] = nullptr; }
		if (g_renderTargets[i]) { g_renderTargets[i]->Release(); g_renderTargets[i] = nullptr; }
		if (g_commandAllocators[i]) { g_commandAllocators[i]->Release(); g_commandAllocators[i] = nullptr; }
	}
	g_commandList = nullptr;

	if (g_pipelineState) { g_pipelineState->Release(); g_pipelineState = nullptr; }
	if (g_depthPrepassPipelineState) { g_depthPrepassPipelineState->Release(); g_depthPrepassPipelineState = nullptr; }
	if (g_rootSignature) { g_rootSignature->Release(); g_rootSignature = nullptr; }
	if (g_shadowPipelineState) { g_shadowPipelineState->Release(); g_shadowPipelineState = nullptr; }
	if (g_shadowRootSignature) { g_shadowRootSignature->Release(); g_shadowRootSignature = nullptr; }
	if (g_skyboxPipelineState) { g_skyboxPipelineState->Release(); g_skyboxPipelineState = nullptr; }
	if (g_skyboxRootSignature) { g_skyboxRootSignature->Release(); g_skyboxRootSignature = nullptr; }
	g_skyboxDescriptorTables.clear();

	if (g_godRayPipelineState) { g_godRayPipelineState->Release(); g_godRayPipelineState = nullptr; }
	if (g_godRayRootSignature) { g_godRayRootSignature->Release(); g_godRayRootSignature = nullptr; }
	if (g_godRayConstantBuffer) { g_godRayConstantBuffer->Unmap(0, nullptr); g_godRayConstantBuffer->Release(); g_godRayConstantBuffer = nullptr; }
	g_pGodRayConstantMapped = nullptr;
	g_godRayTableCpuHandle = {};
	g_godRayTableGpuHandle = {};

	if (g_aoGeneratePipelineState) { g_aoGeneratePipelineState->Release(); g_aoGeneratePipelineState = nullptr; }
	if (g_aoBlurPipelineState) { g_aoBlurPipelineState->Release(); g_aoBlurPipelineState = nullptr; }
	if (g_aoCompositePipelineState) { g_aoCompositePipelineState->Release(); g_aoCompositePipelineState = nullptr; }
	if (g_aoRootSignature) { g_aoRootSignature->Release(); g_aoRootSignature = nullptr; }
	if (g_aoConstantBuffer) { g_aoConstantBuffer->Unmap(0, nullptr); g_aoConstantBuffer->Release(); g_aoConstantBuffer = nullptr; }
	g_pAoConstantMapped = nullptr;
	g_aoTableCpuHandle = {};
	g_aoTableGpuHandle = {};
	SafeDeferredRelease(g_aoRenderTarget);
	SafeDeferredRelease(g_aoBlurRenderTarget);

	if (g_sceneRenderTarget) { g_sceneRenderTarget->Release(); g_sceneRenderTarget = nullptr; }
	if (g_sceneHistoryRenderTarget) { g_sceneHistoryRenderTarget->Release(); g_sceneHistoryRenderTarget = nullptr; }
	if (g_antiAliasingPipelineState) { g_antiAliasingPipelineState->Release(); g_antiAliasingPipelineState = nullptr; }
	if (g_antiAliasingRootSignature) { g_antiAliasingRootSignature->Release(); g_antiAliasingRootSignature = nullptr; }
	g_sceneSrvCpuHandle.ptr = 0;
	g_sceneSrvGpuHandle.ptr = 0;
	g_sceneHistorySrvCpuHandle.ptr = 0;
	g_sceneHistorySrvGpuHandle.ptr = 0;

	if (g_rtvDescHeap) { g_rtvDescHeap->Release(); g_rtvDescHeap = nullptr; }
	if (g_dsvDescHeap) { g_dsvDescHeap->Release(); g_dsvDescHeap = nullptr; }
	if (g_srvDescHeap) { g_srvDescHeap->Release(); g_srvDescHeap = nullptr; }

	if (g_uploadCmdList) { g_uploadCmdList->Release(); g_uploadCmdList = nullptr; }
	if (g_uploadCmdAlloc) { g_uploadCmdAlloc->Release(); g_uploadCmdAlloc = nullptr; }
	if (g_uploadFence) { g_uploadFence->Release(); g_uploadFence = nullptr; }
	if (g_uploadFenceEvent) { CloseHandle(g_uploadFenceEvent); g_uploadFenceEvent = nullptr; }

	if (g_fence) { g_fence->Release(); g_fence = nullptr; }
	if (g_fenceEvent) { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }

	if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
	if (g_commandQueue) { g_commandQueue->Release(); g_commandQueue = nullptr; }
	if (g_d3dDevice) { g_d3dDevice->Release(); g_d3dDevice = nullptr; }
	if (g_dxgiFactory) { g_dxgiFactory->Release(); g_dxgiFactory = nullptr; }

	g_srvDescriptorAllocIndex = 0;
	g_globalFenceValue = 0;
	g_uploadFenceValue = 0;
	for (UINT i = 0; i < FRAME_COUNT; i++) g_fenceValues[i] = 0;
}

// GLFW Callbacks
static void frameBufferResizeCallback(GLFWwindow* window, int width, int height)
{
	g_currentWidth = width;
	g_currentHeight = height;
	resizeBuffers(width, height);
	Eunoia::MeshClusterCullingSystem::Get().Resize((UINT)width, (UINT)height);
	Eunoia::WaterSystem::Get().Resize((UINT)width, (UINT)height);
}

static bool RayTriangleIntersect(
	const glm::vec3& orig, const glm::vec3& dir,
	const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
	float& t)
{
	const float EPSILON = 1e-7f;
	glm::vec3 edge1 = v1 - v0;
	glm::vec3 edge2 = v2 - v0;
	glm::vec3 h = glm::cross(dir, edge2);
	float a = glm::dot(edge1, h);
	if (a > -EPSILON && a < EPSILON) return false;

	float f = 1.0f / a;
	glm::vec3 s = orig - v0;
	float u = f * glm::dot(s, h);
	if (u < 0.0f || u > 1.0f) return false;

	glm::vec3 q = glm::cross(s, edge1);
	float v = f * glm::dot(dir, q);
	if (v < 0.0f || u + v > 1.0f) return false;

	float localT = f * glm::dot(edge2, q);
	if (localT > EPSILON)
	{
		t = localT;
		return true;
	}
	return false;
}

static void PickObjectAtCursor(float mouseX, float mouseY)
{
	if (g_currentWidth <= 0 || g_currentHeight <= 0) return;

	EngineUI::ViewportRect vpRect = g_engineUI.GetViewportRect((float)g_currentWidth, (float)g_currentHeight);
	if (mouseX < vpRect.x || mouseX > vpRect.x + vpRect.width ||
	    mouseY < vpRect.y || mouseY > vpRect.y + vpRect.height)
	{
		return;
	}

	float localMouseX = mouseX - vpRect.x;
	float localMouseY = mouseY - vpRect.y;

	float ndcX = (2.0f * localMouseX) / vpRect.width - 1.0f;
	float ndcY = 1.0f - (2.0f * localMouseY) / vpRect.height;

	Scene& pickScene = g_engineUI.GetActiveRenderScene(g_scene);
	OrbitCamera& pickCamera = g_engineUI.GetActiveRenderCamera(g_camera);
	float aspect = (vpRect.width > 0 && vpRect.height > 0) ? (vpRect.width / vpRect.height) : 1.777f;
	glm::mat4 proj = pickCamera.GetProjectionMatrix(aspect);
	glm::mat4 view = pickCamera.GetViewMatrix();
	glm::mat4 vp = proj * view;
	float vpDet = glm::determinant(vp);
	if (std::abs(vpDet) < 1e-6f || std::isnan(vpDet)) return;
	glm::mat4 invVP = glm::inverse(vp);

	glm::vec4 nearPointWorld = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
	glm::vec4 farPointWorld  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);

	if (nearPointWorld.w != 0.0f) nearPointWorld /= nearPointWorld.w;
	if (farPointWorld.w != 0.0f)  farPointWorld  /= farPointWorld.w;

	glm::vec3 rayOrigin = glm::vec3(nearPointWorld);
	glm::vec3 rayDiff = glm::vec3(farPointWorld - nearPointWorld);
	float rayDiffLen = glm::length(rayDiff);
	if (rayDiffLen < 1e-6f || std::isnan(rayDiffLen)) return;
	glm::vec3 rayDir = rayDiff / rayDiffLen;

	// 1. Check if user clicked on a Light Sprite Billboard in screen space
	int closestLightId = -1;
	float closestLightDist2D = 25.0f; // pixel tolerance for clicking icon
	for (const auto& obj : pickScene.objects)
	{
		if (!obj.visible || !obj.isLight) continue;
		glm::vec3 worldPos = pickScene.GetWorldPosition(obj);
		glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
		if (clip.w <= 0.05f) continue;
		glm::vec3 ndcLight = glm::vec3(clip) / clip.w;
		if (ndcLight.z < -1.0f || ndcLight.z > 1.0f) continue;

		float screenX = (ndcLight.x * 0.5f + 0.5f) * (float)g_currentWidth;
		float screenY = ((1.0f - ndcLight.y) * 0.5f) * (float)g_currentHeight;

		float dist2D = std::hypot(mouseX - screenX, mouseY - screenY);
		if (dist2D < closestLightDist2D)
		{
			closestLightDist2D = dist2D;
			closestLightId = obj.id;
		}
	}

	if (closestLightId != -1)
	{
		g_scene.selectedId = closestLightId;
		g_engineUI.selectedActorIds.clear();
		g_engineUI.selectedActorIds.insert(closestLightId);
		GameObject* hitObj = g_scene.FindObject(closestLightId);
		if (hitObj)
		{
			g_engineUI.AddLog("LogActor", "Selected Light: " + hitObj->name, 0);
		}
		return;
	}

	struct HitCandidate {
		int id;
		float dist;
	};
	std::vector<HitCandidate> hits;

	for (const auto& obj : pickScene.objects)
	{
		if (!obj.visible || obj.mesh.vertices.empty()) continue;

		glm::mat4 model = pickScene.GetWorldMatrix(obj);
		float modelDet = glm::determinant(model);
		if (std::abs(modelDet) < 1e-6f || std::isnan(modelDet)) continue;
		glm::mat4 invModel = glm::inverse(model);

		glm::vec3 localRayOrig = glm::vec3(invModel * glm::vec4(rayOrigin, 1.0f));
		glm::vec3 unnormLocalRayDir = glm::vec3(invModel * glm::vec4(rayDir, 0.0f));
		float lrdLen = glm::length(unnormLocalRayDir);
		if (lrdLen < 1e-6f || std::isnan(lrdLen)) continue;
		glm::vec3 localRayDir = unnormLocalRayDir / lrdLen;

		const auto& verts = obj.mesh.vertices;
		const auto& inds  = obj.mesh.indices;

		float objClosestDist = 1e9f;
		bool objHit = false;

		for (size_t i = 0; i + 2 < inds.size(); i += 3)
		{
			const glm::vec3& v0 = verts[inds[i]].pos;
			const glm::vec3& v1 = verts[inds[i + 1]].pos;
			const glm::vec3& v2 = verts[inds[i + 2]].pos;

			float t = 0.0f;
			if (RayTriangleIntersect(localRayOrig, localRayDir, v0, v1, v2, t))
			{
				glm::vec3 localHit = localRayOrig + localRayDir * t;
				glm::vec3 worldHit = glm::vec3(model * glm::vec4(localHit, 1.0f));
				float worldDist = glm::dot(worldHit - rayOrigin, rayDir);

				if (worldDist > 0.0f && worldDist < objClosestDist)
				{
					objClosestDist = worldDist;
					objHit = true;
				}
			}
		}

		if (objHit)
		{
			hits.push_back({ obj.id, objClosestDist });
		}
	}

	int closestId = -1;
	if (!hits.empty())
	{
		// Sort candidates by ray distance ascending
		std::sort(hits.begin(), hits.end(), [](const HitCandidate& a, const HitCandidate& b) {
			return a.dist < b.dist;
		});

		// Find candidates closely overlapping at the front surface (within 0.01 threshold)
		float minHitDist = hits.front().dist;
		std::vector<int> coLocatedCandidates;
		for (const auto& h : hits)
		{
			if (std::abs(h.dist - minHitDist) < 0.02f)
			{
				coLocatedCandidates.push_back(h.id);
			}
		}

		if (coLocatedCandidates.size() > 1)
		{
			// Check if currently selected actor is one of the co-located overlapping actors
			auto it = std::find(coLocatedCandidates.begin(), coLocatedCandidates.end(), g_scene.selectedId);
			if (it != coLocatedCandidates.end())
			{
				// Cycle to the next overlapping actor
				size_t nextIdx = (std::distance(coLocatedCandidates.begin(), it) + 1) % coLocatedCandidates.size();
				closestId = coLocatedCandidates[nextIdx];
			}
			else
			{
				closestId = coLocatedCandidates.front();
			}
		}
		else
		{
			closestId = hits.front().id;
		}
	}

	if (&pickScene == &g_scene)
	{
		if (closestId != -1)
		{
			GameObject* hitObj = g_scene.FindObject(closestId);

			g_scene.selectedId = closestId;
			g_engineUI.selectedActorIds.clear();
			g_engineUI.selectedActorIds.insert(closestId);
			if (hitObj)
			{
				g_engineUI.AddLog("LogActor", "Selected Actor: " + hitObj->name, 0);
				EngineLogger::Get().LogAction("OBJECT_SELECTED", hitObj->name, "ID: " + std::to_string(closestId));
			}
		}
		else
		{
			// Clicked empty background: deselect
			g_scene.selectedId = -1;
		g_engineUI.selectedActorIds.clear();
			EngineLogger::Get().LogAction("DESELECT_ALL", "None", "Clicked background");
		}
	}

}

static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	if (g_scene.isPlayMode)
	{
		if (button == GLFW_MOUSE_BUTTON_LEFT) s_isLeftMouseDown = (action == GLFW_PRESS);
		else if (button == GLFW_MOUSE_BUTTON_MIDDLE) s_isMiddleMouseDown = (action == GLFW_PRESS);
		else if (button == GLFW_MOUSE_BUTTON_RIGHT) s_isRightMouseDown = (action == GLFW_PRESS);
		return;
	}

	ImGuiIO& io = ImGui::GetIO();
	g_engineUI.RequestViewportRedraw(10);

	if (button == GLFW_MOUSE_BUTTON_LEFT)
	{
		s_isLeftMouseDown = (action == GLFW_PRESS);
		EngineLogger::Get().LogAction(action == GLFW_PRESS ? "MOUSE_PRESS_LEFT" : "MOUSE_RELEASE_LEFT");
		if (action == GLFW_PRESS)
		{
			bool isAlt = (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
			              glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
			bool isPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
			OrbitCamera& activeCam = g_engineUI.GetActiveRenderCamera(g_camera);
			if (!g_scene.isPlayMode && !isAlt && !activeCam.isFlying && !io.WantCaptureMouse && !isPopupOpen && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
			{
				s_pendingPickClick = true;
			}
		}
	}
	else if (button == GLFW_MOUSE_BUTTON_MIDDLE)
	{
		s_isMiddleMouseDown = (action == GLFW_PRESS);
	}
	else if (button == GLFW_MOUSE_BUTTON_RIGHT)
	{
		if (action == GLFW_PRESS)
		{
			s_isRightMouseDown = true;
			bool isAlt = (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
			              glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);
			bool isPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);

			double mx, my;
			glfwGetCursorPos(window, &mx, &my);
			EngineUI::ViewportRect vpRect = g_engineUI.GetViewportRect((float)g_currentWidth, (float)g_currentHeight);
			bool inViewport = (mx >= vpRect.x && mx <= vpRect.x + vpRect.width &&
			                   my >= vpRect.y && my <= vpRect.y + vpRect.height);

			// Right click on viewport: lock and hide cursor for looking
			if (!isAlt && !isPopupOpen && inViewport)
			{
				OrbitCamera& activeFlyCam = g_engineUI.GetActiveRenderCamera(g_camera);
				activeFlyCam.isFlying = true;
				g_camera.isFlying = true;
				s_firstMouseAfterCapture = true;
				glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
				if (glfwRawMouseMotionSupported())
				{
					glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
				}
				ImGui::SetMouseCursor(ImGuiMouseCursor_None);
				InputSystem::Get().ResetMouseDelta();
			}
		}
		else if (action == GLFW_RELEASE)
		{
			s_isRightMouseDown = false;
			OrbitCamera& activeFlyCam = g_engineUI.GetActiveRenderCamera(g_camera);
			activeFlyCam.isFlying = false;
			g_camera.isFlying = false;
			if (glfwRawMouseMotionSupported())
			{
				glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
			}
			if (!InputSystem::Get().IsCursorLocked())
			{
				glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
			}
			else
			{
				glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			}
			InputSystem::Get().ResetMouseDelta();
		}
	}
}

static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
	double deltaX = xpos - s_lastMouseX;
	double deltaY = ypos - s_lastMouseY;
	s_lastMouseX = xpos;
	s_lastMouseY = ypos;

	bool shouldLockCursor = InputSystem::Get().IsCursorLocked() || (g_camera.isFlying && s_isRightMouseDown);
	if (shouldLockCursor)
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_None);
		if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
		{
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
	}
	else
	{
		if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
		{
			glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}

	if (g_scene.isPlayMode)
	{
		// Game mode: Camera is static unless controlled by a behaviour script
		return;
	}

	if (s_firstMouseAfterCapture && g_camera.isFlying)
	{
		s_lastMouseX = xpos;
		s_lastMouseY = ypos;
		s_firstMouseAfterCapture = false;
		return;
	}

	ImGuiIO& io = ImGui::GetIO();
	if (ImGuizmo::IsUsing()) return;

	bool isAlt = (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
	              glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);

	// 1. Fly Camera (Hold RMB + Mouse Movement -> Look around)
	OrbitCamera& activeFlyLookCam = g_engineUI.GetActiveRenderCamera(g_camera);
	if (activeFlyLookCam.isFlying && s_isRightMouseDown && !isAlt)
	{
		activeFlyLookCam.LookAround((float)deltaX, (float)deltaY);
		if (&activeFlyLookCam == &g_camera) SyncViewportCameraToLevelCamera();
		g_engineUI.RequestViewportRedraw(10);
		return;
	}

	bool wantCapture = io.WantCaptureMouse;
	if (wantCapture || ImGuizmo::IsOver()) return;

	// 2. Alt Navigation (Unreal Engine Maya-style viewport controls)
	OrbitCamera& activeNavCam = g_engineUI.GetActiveRenderCamera(g_camera);
	if (isAlt)
	{
		if (s_isLeftMouseDown)
		{
			// Alt + LMB + Drag: Orbit around pivot/selected object
			activeNavCam.Orbit((float)deltaX, (float)deltaY);
			if (&activeNavCam == &g_camera) SyncViewportCameraToLevelCamera();
			g_engineUI.RequestViewportRedraw(10);
		}
		else if (s_isRightMouseDown)
		{
			// Alt + RMB + Drag: Dolly/zoom
			activeNavCam.Dolly((float)(deltaX - deltaY));
			if (&activeNavCam == &g_camera) SyncViewportCameraToLevelCamera();
			g_engineUI.RequestViewportRedraw(10);
		}
		else if (s_isMiddleMouseDown)
		{
			// Alt + MMB + Drag: Pan/track
			activeNavCam.Pan((float)deltaX, (float)deltaY);
			if (&activeNavCam == &g_camera) SyncViewportCameraToLevelCamera();
			g_engineUI.RequestViewportRedraw(10);
		}
	}
	else
	{
		// Non-Alt navigation
		if (s_isMiddleMouseDown)
		{
			activeNavCam.Pan((float)deltaX, (float)deltaY);
			if (&activeNavCam == &g_camera) SyncViewportCameraToLevelCamera();
			g_engineUI.RequestViewportRedraw(10);
		}
	}
}

static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
	InputSystem::ScrollCallback(window, xoffset, yoffset);

	if (g_scene.isPlayMode)
	{
		// Game mode: Viewport zoom is disabled
		return;
	}

	ImGuiIO& io = ImGui::GetIO();

	double mx, my;
	glfwGetCursorPos(window, &mx, &my);
	EngineUI::ViewportRect vpRect = g_engineUI.GetViewportRect((float)g_currentWidth, (float)g_currentHeight);
	bool inViewport = (mx >= vpRect.x && mx <= vpRect.x + vpRect.width &&
	                   my >= vpRect.y && my <= vpRect.y + vpRect.height);

	OrbitCamera& activeScrollCam = g_engineUI.GetActiveRenderCamera(g_camera);
	// Mouse wheel on viewport (or during fly camera): increase / decrease fly camera speed
	if ((s_isRightMouseDown && activeScrollCam.isFlying) || (inViewport && s_isRightMouseDown))
	{
		activeScrollCam.AdjustSpeed((float)yoffset);
		g_engineUI.ShowCameraSpeedOverlay(activeScrollCam.moveSpeed);
		g_engineUI.RequestViewportRedraw(10);
	}
	else if (inViewport)
	{
		if (io.KeyCtrl)
		{
			activeScrollCam.AdjustSpeed((float)yoffset);
			g_engineUI.ShowCameraSpeedOverlay(activeScrollCam.moveSpeed);
			g_engineUI.RequestViewportRedraw(10);
		}
		else
		{
			bool wantCapture = io.WantCaptureMouse;
			if (wantCapture) return;
			activeScrollCam.Zoom((float)yoffset);
			if (&activeScrollCam == &g_camera) SyncViewportCameraToLevelCamera();
			g_engineUI.RequestViewportRedraw(10);
		}
	}
	else
	{
		bool wantCapture = io.WantCaptureMouse;
		if (wantCapture) return;
		activeScrollCam.Zoom((float)yoffset);
		if (&activeScrollCam == &g_camera) SyncViewportCameraToLevelCamera();
		g_engineUI.RequestViewportRedraw(10);
	}
}

static void dropCallback(GLFWwindow* window, int count, const char** paths)
{
	if (count > 0 && paths && paths[0]) {
		EngineLogger::Get().LogAction("FILE_DROP", paths[0], "Count: " + std::to_string(count));
	}
	g_engineUI.HandleFileDrop(paths, count);
}

bool RecoverD3D12Device(HWND hwnd)
{
	std::cerr << "[RECOVERY] Attempting full DirectX 12 device recovery and state restoration...\n";
	EngineLogger::Get().LogAction("DEVICE_RECOVERY_START", "DirectX12", "Attempting full D3D12 device recreation.");

	// 1. Release all mapped buffers and hardware resources
	FlushDeferredReleases();
	if (g_vertexBuffer) { g_vertexBuffer->Unmap(0, nullptr); g_vertexBuffer->Release(); g_vertexBuffer = nullptr; }
	if (g_indexBuffer) { g_indexBuffer->Unmap(0, nullptr); g_indexBuffer->Release(); g_indexBuffer = nullptr; }
	if (g_constantBuffer) { g_constantBuffer->Unmap(0, nullptr); g_constantBuffer->Release(); g_constantBuffer = nullptr; }
	if (g_shadowConstantBuffer) { g_shadowConstantBuffer->Unmap(0, nullptr); g_shadowConstantBuffer->Release(); g_shadowConstantBuffer = nullptr; }
	if (g_matPreviewVertexBuffer) { g_matPreviewVertexBuffer->Unmap(0, nullptr); g_matPreviewVertexBuffer->Release(); g_matPreviewVertexBuffer = nullptr; }
	if (g_matPreviewRenderTarget) { g_matPreviewRenderTarget->Release(); g_matPreviewRenderTarget = nullptr; }
	if (g_matPreviewDepthBuffer) { g_matPreviewDepthBuffer->Release(); g_matPreviewDepthBuffer = nullptr; }
	if (g_matPreviewIndexBuffer) { g_matPreviewIndexBuffer->Unmap(0, nullptr); g_matPreviewIndexBuffer->Release(); g_matPreviewIndexBuffer = nullptr; }
	if (g_matPreviewConstantBuffer) { g_matPreviewConstantBuffer->Unmap(0, nullptr); g_matPreviewConstantBuffer->Release(); g_matPreviewConstantBuffer = nullptr; }
	g_pVertexMapped = nullptr;
	g_pIndexMapped = nullptr;
	g_pConstantMapped = nullptr;
	g_pShadowConstantMapped = nullptr;
	g_pMatPreviewVertexMapped = nullptr;
	g_pMatPreviewIndexMapped = nullptr;
	g_pMatPreviewConstantMapped = nullptr;

	for (auto* tex : g_allocatedTextures) {
		if (tex) tex->Release();
	}
	g_allocatedTextures.clear();
	g_gpuTextureMap.clear();
	g_materialDescriptorTables.clear();

	if (g_depthStencilBuffer) { g_depthStencilBuffer->Release(); g_depthStencilBuffer = nullptr; }
	if (g_shadowDepthBuffer) { g_shadowDepthBuffer->Release(); g_shadowDepthBuffer = nullptr; }
	if (g_pointShadowDepthBuffer) { g_pointShadowDepthBuffer->Release(); g_pointShadowDepthBuffer = nullptr; }
	if (g_spotShadowDepthBuffer) { g_spotShadowDepthBuffer->Release(); g_spotShadowDepthBuffer = nullptr; }
	g_shadowSrvCpuHandle = {}; g_shadowSrvGpuHandle = {};
	g_pointShadowSrvCpuHandle = {}; g_pointShadowSrvGpuHandle = {};
	g_spotShadowSrvCpuHandle = {}; g_spotShadowSrvGpuHandle = {};

	for (UINT i = 0; i < FRAME_COUNT; i++)
	{
		if (g_commandLists[i]) { g_commandLists[i]->Release(); g_commandLists[i] = nullptr; }
		if (g_renderTargets[i]) { g_renderTargets[i]->Release(); g_renderTargets[i] = nullptr; }
		if (g_commandAllocators[i]) { g_commandAllocators[i]->Release(); g_commandAllocators[i] = nullptr; }
	}
	g_commandList = nullptr;

	if (g_pipelineState) { g_pipelineState->Release(); g_pipelineState = nullptr; }
	if (g_depthPrepassPipelineState) { g_depthPrepassPipelineState->Release(); g_depthPrepassPipelineState = nullptr; }
	if (g_rootSignature) { g_rootSignature->Release(); g_rootSignature = nullptr; }
	if (g_shadowPipelineState) { g_shadowPipelineState->Release(); g_shadowPipelineState = nullptr; }
	if (g_shadowRootSignature) { g_shadowRootSignature->Release(); g_shadowRootSignature = nullptr; }
	if (g_skyboxPipelineState) { g_skyboxPipelineState->Release(); g_skyboxPipelineState = nullptr; }
	if (g_skyboxRootSignature) { g_skyboxRootSignature->Release(); g_skyboxRootSignature = nullptr; }
	g_skyboxDescriptorTables.clear();

	if (g_godRayPipelineState) { g_godRayPipelineState->Release(); g_godRayPipelineState = nullptr; }
	if (g_godRayRootSignature) { g_godRayRootSignature->Release(); g_godRayRootSignature = nullptr; }
	if (g_godRayConstantBuffer) { g_godRayConstantBuffer->Unmap(0, nullptr); g_godRayConstantBuffer->Release(); g_godRayConstantBuffer = nullptr; }
	g_pGodRayConstantMapped = nullptr;
	g_godRayTableCpuHandle = {};
	g_godRayTableGpuHandle = {};

	if (g_aoGeneratePipelineState) { g_aoGeneratePipelineState->Release(); g_aoGeneratePipelineState = nullptr; }
	if (g_aoBlurPipelineState) { g_aoBlurPipelineState->Release(); g_aoBlurPipelineState = nullptr; }
	if (g_aoCompositePipelineState) { g_aoCompositePipelineState->Release(); g_aoCompositePipelineState = nullptr; }
	if (g_aoRootSignature) { g_aoRootSignature->Release(); g_aoRootSignature = nullptr; }
	if (g_aoConstantBuffer) { g_aoConstantBuffer->Unmap(0, nullptr); g_aoConstantBuffer->Release(); g_aoConstantBuffer = nullptr; }
	g_pAoConstantMapped = nullptr;
	g_aoTableCpuHandle = {};
	g_aoTableGpuHandle = {};
	SafeDeferredRelease(g_aoRenderTarget);
	SafeDeferredRelease(g_aoBlurRenderTarget);

	if (g_rtvDescHeap) { g_rtvDescHeap->Release(); g_rtvDescHeap = nullptr; }
	if (g_dsvDescHeap) { g_dsvDescHeap->Release(); g_dsvDescHeap = nullptr; }
	if (g_srvDescHeap) { g_srvDescHeap->Release(); g_srvDescHeap = nullptr; }

	if (g_uploadCmdList) { g_uploadCmdList->Release(); g_uploadCmdList = nullptr; }
	if (g_uploadCmdAlloc) { g_uploadCmdAlloc->Release(); g_uploadCmdAlloc = nullptr; }
	if (g_uploadFence) { g_uploadFence->Release(); g_uploadFence = nullptr; }
	if (g_uploadFenceEvent) { CloseHandle(g_uploadFenceEvent); g_uploadFenceEvent = nullptr; }

	if (g_fence) { g_fence->Release(); g_fence = nullptr; }
	if (g_fenceEvent) { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }

	if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
	if (g_commandQueue) { g_commandQueue->Release(); g_commandQueue = nullptr; }
	if (g_d3dDevice) { g_d3dDevice->Release(); g_d3dDevice = nullptr; }
	if (g_dxgiFactory) { g_dxgiFactory->Release(); g_dxgiFactory = nullptr; }

	g_srvDescriptorAllocIndex = 0;
	g_globalFenceValue = 0;
	g_uploadFenceValue = 0;
	for (UINT i = 0; i < FRAME_COUNT; i++) g_fenceValues[i] = 0;

	// Shutdown ImGui DX12 backend
	ImGui_ImplDX12_Shutdown();

	// Brief sleep to allow OS graphics driver reset to complete
	Sleep(150);

	// 2. Re-create D3D12 device, swap chain, allocators, views
	if (initD3D12(hwnd) != EXIT_SUCCESS) {
		std::cerr << "[RECOVERY] initD3D12 failed during recovery!\n";
		return false;
	}

	if (createShadersAndPipeline() != EXIT_SUCCESS) {
		std::cerr << "[RECOVERY] createShadersAndPipeline failed during recovery!\n";
		return false;
	}

	if (createDynamicBuffers() != EXIT_SUCCESS) {
		std::cerr << "[RECOVERY] createDynamicBuffers failed during recovery!\n";
		return false;
	}

	// 3. Re-initialize ImGui DX12 backend
	ImGui_ImplDX12_InitInfo init_info = {};
	init_info.Device = g_d3dDevice;
	init_info.CommandQueue = g_commandQueue;
	init_info.NumFramesInFlight = FRAME_COUNT;
	init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	init_info.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	init_info.SrvDescriptorHeap = g_srvDescHeap;
	init_info.SrvDescriptorAllocFn = ImGui_SrvAlloc;
	init_info.SrvDescriptorFreeFn  = ImGui_SrvFree;
	ImGui_ImplDX12_Init(&init_info);

	// 4. Reload textures & fallbacks
	InitFallbackTextures();
	InitLightmapSrvSlot();
	DX12GpuTexture dirLightIcon = LoadEditorIconSync("Resources/Icons/directional_light.png", g_fallbackWhite);
	g_engineUI.directionalLightIconGpuHandle = dirLightIcon.gpuHandle.ptr;

	DX12GpuTexture ptLightIcon = LoadEditorIconSync("Resources/Icons/point_light.png", g_fallbackWhite);
	g_engineUI.pointLightIconGpuHandle = ptLightIcon.gpuHandle.ptr;

	DX12GpuTexture spotLightIcon = LoadEditorIconSync("Resources/Icons/spot_light.png", g_fallbackWhite);
	g_engineUI.spotLightIconGpuHandle = spotLightIcon.gpuHandle.ptr;

	DX12GpuTexture areaLightIcon = LoadEditorIconSync("Resources/Icons/area_light.png", g_fallbackWhite);
	g_engineUI.areaLightIconGpuHandle = areaLightIcon.gpuHandle.ptr;

	DX12GpuTexture lightIcon = LoadEditorIconSync("Resources/Icons/light.png", ptLightIcon);
	g_engineUI.lightIconGpuHandle = lightIcon.gpuHandle.ptr ? lightIcon.gpuHandle.ptr : ptLightIcon.gpuHandle.ptr;

	DX12GpuTexture skyLightIcon = LoadEditorIconSync("Resources/Icons/sky_light.png", lightIcon);
	g_engineUI.skyLightIconGpuHandle = skyLightIcon.gpuHandle.ptr ? skyLightIcon.gpuHandle.ptr : lightIcon.gpuHandle.ptr;

	DX12GpuTexture cameraIcon = LoadEditorIconSync("Resources/Icons/camera.png", g_fallbackWhite);
	g_engineUI.cameraIconGpuHandle = cameraIcon.gpuHandle.ptr;
	DX12GpuTexture engineIcon = LoadEditorIconSync("Resources/Icons/eunoia.png", g_fallbackWhite);
	g_engineUI.engineIconGpuHandle = engineIcon.gpuHandle.ptr;
	DX12GpuTexture eyeIcon = LoadEditorIconSync("Resources/Icons/Outliner/Eye.png", g_fallbackWhite);
	g_engineUI.outlinerEyeIconGpuHandle = eyeIcon.gpuHandle.ptr;

	DX12GpuTexture cbFolderIcon = LoadEditorIconSync("Resources/Icons/Content Browser/folder.png", g_fallbackWhite);
	g_engineUI.cbFolderIconGpuHandle = cbFolderIcon.gpuHandle.ptr;
	DX12GpuTexture cb3dIcon = LoadEditorIconSync("Resources/Icons/Content Browser/3d.png", g_fallbackWhite);
	g_engineUI.cb3dIconGpuHandle = cb3dIcon.gpuHandle.ptr;
	DX12GpuTexture cbMatIcon = LoadEditorIconSync("Resources/Icons/Content Browser/material.png", g_fallbackWhite);
	g_engineUI.cbMaterialIconGpuHandle = cbMatIcon.gpuHandle.ptr;
	DX12GpuTexture cbSkyIcon = LoadEditorIconSync("Resources/Icons/Content Browser/skybox.png", g_fallbackWhite);
	g_engineUI.cbSkyboxIconGpuHandle = cbSkyIcon.gpuHandle.ptr;
	DX12GpuTexture cbTexIcon = LoadEditorIconSync("Resources/Icons/Content Browser/texture.png", g_fallbackWhite);
	g_engineUI.cbTextureIconGpuHandle = cbTexIcon.gpuHandle.ptr;
	DX12GpuTexture cbLevelIcon = LoadEditorIconSync("Resources/Icons/Content Browser/level.png", g_fallbackWhite);
	g_engineUI.cbLevelIconGpuHandle = cbLevelIcon.gpuHandle.ptr;
	DX12GpuTexture cbBehIcon = LoadEditorIconSync("Resources/Icons/Content Browser/behaviour.png", g_fallbackWhite);
	g_engineUI.cbBehaviourIconGpuHandle = cbBehIcon.gpuHandle.ptr;
	EunoiaUI::UISystemManager::Get().SetTextureLoader([](const std::string& path, int* outW, int* outH) -> uint64_t {
		DX12GpuTexture tex = LoadEditorIconSync(path, g_fallbackWhite);
		if (tex.resource && tex.gpuHandle.ptr != 0) {
			if (outW) *outW = tex.width;
			if (outH) *outH = tex.height;
			return tex.gpuHandle.ptr;
		}
		return 0;
	});

	// 5. Pre-upload textures & rebuild scene geometry
	PreRenderUploadTextures();
	updateSceneGeometry();
	updateConstantBuffer();

	g_deviceLost = false;
	std::cerr << "[RECOVERY] D3D12 device successfully recovered and state restored!\n";
	EngineLogger::Get().LogAction("DEVICE_RECOVERED", "DirectX12", "GPU device recovered from reset. Session restored to exact condition.");
	g_engineUI.AddLog("LogRecovery", "DirectX 12 graphics device successfully recovered â€” resumed exact session state.", 0);
	return true;
}

// ============================================================================
// Custom Window Frame & Window Procedure for Editor
// Provides borderless dark custom frame, native resize borders, snap, and DWM shadow
// ============================================================================
static WNDPROC g_originalWndProc = nullptr;

static LRESULT CALLBACK CustomWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_NCCALCSIZE:
	{
		if (wParam == TRUE)
		{
			// Returning 0 removes default OS caption and borders; client area fills entire window
			if (IsZoomed(hwnd))
			{
				// When maximized, adjust the client rect to match monitor work area (avoids 8px overflow)
				HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
				MONITORINFO mi = { sizeof(mi) };
				if (GetMonitorInfo(hMon, &mi))
				{
					NCCALCSIZE_PARAMS* p = (NCCALCSIZE_PARAMS*)lParam;
					p->rgrc[0] = mi.rcWork;
				}
				return 0;
			}
			return 0;
		}
		return 0;
	}

	case WM_GETMINMAXINFO:
	{
		MINMAXINFO* mmi = (MINMAXINFO*)lParam;
		HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		if (GetMonitorInfo(hMon, &mi))
		{
			mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
			mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
			mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
			mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
		}
		mmi->ptMinTrackSize.x = 800;
		mmi->ptMinTrackSize.y = 500;
		return 0;
	}

	case WM_NCHITTEST:
	{
		if (IsZoomed(hwnd))
		{
			return HTCLIENT;
		}

		POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
		RECT rc;
		GetWindowRect(hwnd, &rc);

		const int BORDER_SIZE = 7;
		bool left   = (pt.x >= rc.left && pt.x < rc.left + BORDER_SIZE);
		bool right  = (pt.x <= rc.right && pt.x > rc.right - BORDER_SIZE);
		bool top    = (pt.y >= rc.top && pt.y < rc.top + BORDER_SIZE);
		bool bottom = (pt.y <= rc.bottom && pt.y > rc.bottom - BORDER_SIZE);

		if (top && left)     return HTTOPLEFT;
		if (top && right)    return HTTOPRIGHT;
		if (bottom && left)  return HTBOTTOMLEFT;
		if (bottom && right) return HTBOTTOMRIGHT;
		if (left)            return HTLEFT;
		if (right)           return HTRIGHT;
		if (top)             return HTTOP;
		if (bottom)          return HTBOTTOM;

		return HTCLIENT;
	}

	case WM_NCACTIVATE:
		return TRUE;
	}

	return CallWindowProc(g_originalWndProc, hwnd, uMsg, wParam, lParam);
}

static void SetupCustomWindowFrame(HWND hwnd)
{
	if (!hwnd) return;

	LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
	style |= WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CAPTION | WS_SYSMENU;
	SetWindowLongPtr(hwnd, GWL_STYLE, style);

	g_originalWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)CustomWindowProc);

	MARGINS margins = { 1, 1, 1, 1 };
	DwmExtendFrameIntoClientArea(hwnd, &margins);

	DragAcceptFiles(hwnd, TRUE);

	SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
		SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

int main(int argc, char** argv)
{
	EngineLogger::Get().Init("logs.elogs");
	EngineLogger::Get().LogAction("ENGINE_START", "Eunoia-Editor", "DirectX 12 Initializing");

	bool isGameOnlyMode = false;
	std::filesystem::path gameProjectPath;
	std::string gameScenePath;
	std::string gameBinPath;
	float cmdCamYaw = 0.0f;
	float cmdCamPitch = 0.0f;
	glm::vec3 cmdCamTarget(0.0f);
	float cmdCamDist = 0.001f;
	float cmdCamFov = 60.0f;
	bool hasCmdCam = false;

	std::filesystem::path selectedProjectPath;
	bool forceStandaloneBrowser = false;
	std::string standaloneGameName = "";
	std::string standaloneStartupLevel = "";
	int gameWindowWidth = WIDTH;
	int gameWindowHeight = HEIGHT;
	bool gameWindowFullscreen = false;

	// Standalone Game Build Auto-Detection:
	// If GameConfig.json exists in the executable's folder or Data/, or if the binary is named <GameName>.exe (not Eunoia-Editor.exe),
	// boot directly into dedicated standalone game mode with the default opening level!
	char exePathBuf[MAX_PATH] = {};
	GetModuleFileNameA(NULL, exePathBuf, MAX_PATH);
	std::filesystem::path currentExePath(exePathBuf);
	std::filesystem::path exeDir = currentExePath.parent_path();
	std::error_code cpec;
	std::filesystem::current_path(exeDir, cpec);
	std::string exeNameLower = currentExePath.filename().string();
	std::transform(exeNameLower.begin(), exeNameLower.end(), exeNameLower.begin(), ::tolower);

	std::error_code sec;
	std::filesystem::path candidateCfg = exeDir / "GameConfig.json";
	if (!std::filesystem::exists(candidateCfg, sec)) candidateCfg = exeDir / "Data" / "GameConfig.json";
	if (!std::filesystem::exists(candidateCfg, sec)) candidateCfg = std::filesystem::current_path() / "GameConfig.json";
	if (!std::filesystem::exists(candidateCfg, sec)) candidateCfg = std::filesystem::current_path() / "Data" / "GameConfig.json";

	bool isStandaloneExe = (exeNameLower != "eunoia-editor.exe" && exeNameLower != "editor.exe");

	if (std::filesystem::exists(candidateCfg, sec)) {
		try {
			std::ifstream cf(candidateCfg);
			if (cf.is_open()) {
				nlohmann::json gj;
				cf >> gj;
				if (gj.contains("gameName") && gj["gameName"].is_string()) {
					standaloneGameName = gj["gameName"].get<std::string>();
				}
				if (gj.contains("startupLevel") && gj["startupLevel"].is_string()) {
					standaloneStartupLevel = gj["startupLevel"].get<std::string>();
				} else if (gj.contains("defaultLevel") && gj["defaultLevel"].is_string()) {
					standaloneStartupLevel = gj["defaultLevel"].get<std::string>();
				}
				if (gj.contains("window") && gj["window"].is_object()) {
					if (gj["window"].contains("width") && gj["window"]["width"].is_number()) {
						gameWindowWidth = gj["window"]["width"].get<int>();
					}
					if (gj["window"].contains("height") && gj["window"]["height"].is_number()) {
						gameWindowHeight = gj["window"]["height"].get<int>();
					}
					if (gj["window"].contains("fullscreen") && gj["window"]["fullscreen"].is_boolean()) {
						gameWindowFullscreen = gj["window"]["fullscreen"].get<bool>();
					}
				}
				if (isStandaloneExe) {
					isGameOnlyMode = true;
				} else if (gj.contains("standalone") && gj["standalone"].is_boolean() && gj["standalone"].get<bool>()) {
					isGameOnlyMode = true;
				}
			}
		} catch (...) {}
	} else if (isStandaloneExe) {
		isGameOnlyMode = true;
	}

	if (isGameOnlyMode) {
		gameProjectPath = exeDir;
		selectedProjectPath = exeDir;
		if (!standaloneStartupLevel.empty() && gameScenePath.empty()) {
			std::filesystem::path cand1 = exeDir / standaloneStartupLevel;
			std::filesystem::path cand2 = exeDir / "Data" / standaloneStartupLevel;
			std::filesystem::path cand3 = exeDir / "Content" / standaloneStartupLevel;
			if (std::filesystem::exists(cand1, sec)) gameScenePath = cand1.string();
			else if (std::filesystem::exists(cand2, sec)) gameScenePath = cand2.string();
			else if (std::filesystem::exists(cand3, sec)) gameScenePath = cand3.string();
			else gameScenePath = standaloneStartupLevel;
		}
	}

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--game" || arg == "-game" || arg == "/game" || arg == "--play" || arg == "-play") {
			isGameOnlyMode = true;
			if (i + 1 < argc && argv[i + 1][0] != '-') {
				std::filesystem::path p(argv[++i]);
				std::error_code ec;
				if (std::filesystem::is_regular_file(p, ec) && (p.extension() == ".euproject" || p.extension() == ".eproject")) {
					gameProjectPath = p.parent_path().string();
				} else {
					gameProjectPath = p.string();
				}
			}
		} else if (arg == "--project" || arg == "-project" || arg == "-p" || arg == "/project") {
			if (i + 1 < argc && argv[i + 1][0] != '-') {
				std::filesystem::path p(argv[++i]);
				std::error_code ec;
				if (std::filesystem::is_regular_file(p, ec) && (p.extension() == ".euproject" || p.extension() == ".eproject")) {
					selectedProjectPath = p.parent_path();
				} else {
					selectedProjectPath = p;
				}
			}
		} else if (arg == "--browse" || arg == "--browser" || arg == "-b") {
			forceStandaloneBrowser = true;
		} else if (arg == "--scene" || arg == "-scene" || arg == "/scene" || arg == "--level" || arg == "-level" || arg == "/level") {
			if (i + 1 < argc && argv[i + 1][0] != '-') {
				gameScenePath = argv[++i];
			}
		} else if (arg == "--bin" || arg == "-bin" || arg == "/bin") {
			if (i + 1 < argc && argv[i + 1][0] != '-') {
				gameBinPath = argv[++i];
			}
		} else if (arg == "--cam-yaw" && i + 1 < argc) {
			cmdCamYaw = std::stof(argv[++i]);
			hasCmdCam = true;
		} else if (arg == "--cam-pitch" && i + 1 < argc) {
			cmdCamPitch = std::stof(argv[++i]);
			hasCmdCam = true;
		} else if (arg == "--cam-tx" && i + 1 < argc) {
			cmdCamTarget.x = std::stof(argv[++i]);
			hasCmdCam = true;
		} else if (arg == "--cam-ty" && i + 1 < argc) {
			cmdCamTarget.y = std::stof(argv[++i]);
			hasCmdCam = true;
		} else if (arg == "--cam-tz" && i + 1 < argc) {
			cmdCamTarget.z = std::stof(argv[++i]);
			hasCmdCam = true;
		} else if (arg == "--cam-dist" && i + 1 < argc) {
			cmdCamDist = std::stof(argv[++i]);
			hasCmdCam = true;
		} else if (arg == "--cam-fov" && i + 1 < argc) {
			cmdCamFov = std::stof(argv[++i]);
			hasCmdCam = true;
		} else if (!arg.empty() && arg[0] != '-' && selectedProjectPath.empty()) {
			std::error_code ec;
			std::filesystem::path p(arg);
			if (std::filesystem::exists(p, ec)) {
				if (std::filesystem::is_regular_file(p, ec) && (p.extension() == ".euproject" || p.extension() == ".eproject")) {
					// Directly opening a .euproject or .eproject project file!
					selectedProjectPath = p.parent_path();
				} else {
					selectedProjectPath = p;
				}
			}
		}
	}

	// 1. Initialize GLFW
	if (!glfwInit())
	{
		std::cerr << "Failed to initialize GLFW!" << std::endl;
		return -1;
	}

	if (isGameOnlyMode) {
		selectedProjectPath = gameProjectPath.empty() ? std::filesystem::current_path() : gameProjectPath;
		std::cerr << "[INFO] Launching in Dedicated Game Mode for: " << selectedProjectPath.string() << std::endl;
	} else {
		// If project wasn't explicitly given via CLI, launch the standalone Project Browser window
		if (selectedProjectPath.empty()) {
			// Auto-load recent project if not explicitly forcing standalone browser
			if (!forceStandaloneBrowser) {
				std::filesystem::path recentCfg;
				char* appdata = getenv("LOCALAPPDATA");
				if (appdata) {
					recentCfg = std::filesystem::path(appdata) / "EunoiaEngine" / "RecentProjects.json";
				} else {
					recentCfg = "RecentProjects.json";
				}
				std::error_code ec;
				if (std::filesystem::exists(recentCfg, ec)) {
					std::ifstream f(recentCfg);
					if (f.is_open()) {
						try {
							nlohmann::json j;
							f >> j;
							if (j.is_array() && !j.empty()) {
								for (const auto& item : j) {
									if (item.contains("rootPath")) {
										std::string rp = item["rootPath"].get<std::string>();
										if (std::filesystem::exists(rp, ec) && std::filesystem::is_directory(rp, ec)) {
											selectedProjectPath = rp;
											std::cerr << "[INFO] Auto-loading recent project: " << rp << std::endl;
											break;
										}
									}
								}
							}
						} catch (...) {}
					}
				}

				if (selectedProjectPath.empty()) {
					std::filesystem::path swatPath = "C:\\Projects\\Eunoia-Engine\\Projects\\SWAT";
					if (std::filesystem::exists(swatPath, ec) && std::filesystem::is_directory(swatPath, ec)) {
						selectedProjectPath = swatPath;
						std::cerr << "[INFO] Defaulting to SWAT project: " << swatPath.string() << std::endl;
					}
				}
			}

			if (selectedProjectPath.empty()) {
				selectedProjectPath = EngineUI::RunStandaloneProjectBrowser();
			}
			
			// If user closed the project browser or pressed Quit, exit cleanly
			if (selectedProjectPath.empty()) {
				std::cerr << "[INFO] Project Browser closed without selection. Exiting.\n";
				glfwTerminate();
				return 0;
			}
		}
		std::cerr << "[INFO] Project selected: " << selectedProjectPath.string() << std::endl;
	}

	auto startupStart = std::chrono::high_resolution_clock::now();
	auto stageStart = startupStart;
	auto logStartupStage = [&](const std::string& stageName, const std::string& desc) {
		auto now = std::chrono::high_resolution_clock::now();
		auto stageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - stageStart).count();
		auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startupStart).count();
		stageStart = now;
		std::string msg = desc + " (+" + std::to_string(stageMs) + "ms, total " + std::to_string(totalMs) + "ms)";
		EngineLogger::Get().LogAction("STARTUP_STAGE", stageName, msg);
	};

	if (!isGameOnlyMode) {
		StartupSplash::Get().Show("Resources/Icons/eunoia.png");
		StartupSplash::Get().PumpEvents();
		logStartupStage("StartupSplash", "Startup splash window displayed");
	}

	// 2. Create main window (D3D12, NO_API)
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Hidden during initialization behind splash or until level loaded!
	if (isGameOnlyMode) {
		glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	}

	std::string winTitle = isGameOnlyMode ? (!standaloneGameName.empty() ? standaloneGameName : (selectedProjectPath.filename().string() + " - Game")) : "Eunoia-Editor";
	int initW = isGameOnlyMode ? gameWindowWidth : WIDTH;
	int initH = isGameOnlyMode ? gameWindowHeight : HEIGHT;
	g_window = glfwCreateWindow(initW, initH, winTitle.c_str(), nullptr, nullptr);
	if (!g_window)
	{
		std::cerr << "Failed to create GLFW window!" << std::endl;
		StartupSplash::Get().Close();
		glfwTerminate();
		return -1;
	}

	HWND hwnd = glfwGetWin32Window(g_window);
	if (!isGameOnlyMode) {
		SetupCustomWindowFrame(hwnd);
	}
	g_engineUI.SetWindow(g_window);
	g_engineUI.isGameOnlyWindow = isGameOnlyMode;
	InputSystem::Get().SetWindow(g_window);

#ifdef _WIN32
	// Set window icon (both large for Taskbar/Alt-Tab and small for title bar / system menu)
	HICON hIconBig = (HICON)LoadImageA(NULL, "Resources/Icons/eunoia.ico", IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
	HICON hIconSmall = (HICON)LoadImageA(NULL, "Resources/Icons/eunoia.ico", IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
	if (!hIconBig) {
		hIconBig = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(1));
	}
	if (!hIconSmall) {
		hIconSmall = (HICON)LoadImageA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(1), IMAGE_ICON, 16, 16, 0);
		if (!hIconSmall) hIconSmall = hIconBig;
	}
	if (hIconBig) {
		SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
	}
	if (hIconSmall) {
		SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
	}
#endif

	int fbWidth = 0, fbHeight = 0;
	glfwGetFramebufferSize(g_window, &fbWidth, &fbHeight);
	if (fbWidth > 0 && fbHeight > 0)
	{
		g_currentWidth = fbWidth;
		g_currentHeight = fbHeight;
	}

	glfwSetFramebufferSizeCallback(g_window, frameBufferResizeCallback);
	glfwSetMouseButtonCallback(g_window, mouseButtonCallback);
	glfwSetCursorPosCallback(g_window, cursorPosCallback);
	glfwSetScrollCallback(g_window, scrollCallback);
	glfwSetDropCallback(g_window, dropCallback);

	StartupSplash::Get().PumpEvents();
	logStartupStage("PlatformWindow", "Main window created");

	// 2. Initialize DirectX 12
	if (initD3D12(hwnd) != EXIT_SUCCESS)
	{
		std::cerr << "DirectX 12 initialization failed!" << std::endl;
		StartupSplash::Get().Close();
		return -1;
	}
	StartupSplash::Get().PumpEvents();
	logStartupStage("DX12Device", "DirectX 12 device and queues initialized");

	if (createShadersAndPipeline() != EXIT_SUCCESS)
	{
		std::cerr << "DirectX 12 pipeline creation failed!" << std::endl;
		StartupSplash::Get().Close();
		return -1;
	}
	StartupSplash::Get().PumpEvents();
	logStartupStage("ShadersPipeline", "Shaders and pipeline states compiled / cached");

	if (createDynamicBuffers() != EXIT_SUCCESS)
	{
		std::cerr << "DirectX 12 buffer creation failed!" << std::endl;
		StartupSplash::Get().Close();
		return -1;
	}
	StartupSplash::Get().PumpEvents();
	logStartupStage("DynamicBuffers", "Dynamic buffers allocated");

	// 3. Initialize Dear ImGui for DirectX 12
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;  // Enable all panels to be dockable
	io.IniFilename = "editor_layout.ini"; // Save docking layout across sessions

	// Load friendly, high-DPI readable UI font (Segoe UI at 18.0px) with fast 1x oversample
	ImFont* mainFont = nullptr;
	ImFontConfig fontConfig;
	fontConfig.OversampleH = 1;
	// Try Inter font first (Spartan-style clean UI), then fall back to Segoe UI / Arial
	const std::string interPath = "Resources/Fonts/Inter-Regular.ttf";
	if (std::filesystem::exists(interPath)) {
		mainFont = io.Fonts->AddFontFromFileTTF(interPath.c_str(), 15.0f, &fontConfig);
	} else if (std::filesystem::exists("C:\\Windows\\Fonts\\segoeui.ttf")) {
		mainFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f, &fontConfig);
	} else if (std::filesystem::exists("C:\\Windows\\Fonts\\arial.ttf")) {
		mainFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 16.0f, &fontConfig);
	}

	// Merge symbols from Segoe UI Symbol font for clean glyphs/icons
	if (!isGameOnlyMode && mainFont && std::filesystem::exists("C:\\Windows\\Fonts\\seguisym.ttf")) {
		ImFontConfig symbolConfig;
		symbolConfig.MergeMode = true;
		symbolConfig.OversampleH = 1;
		symbolConfig.OversampleV = 1;
		static const ImWchar symbolRanges[] = {
			0x2022, 0x2022, // Bullet
			0x2190, 0x21FF, // Arrows
			0x25A0, 0x25FF, // Geometric Shapes
			0x2600, 0x26FF, // Miscellaneous Symbols
			0x2700, 0x27BF, // Dingbats
			0,
		};
		io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisym.ttf", 18.0f, &symbolConfig, symbolRanges);
	}

	if (!mainFont) {
		io.Fonts->AddFontDefault();
		io.FontGlobalScale = 1.25f;
	}

	if (!isGameOnlyMode) {
		g_engineUI.SetupTheme();
	}

	ImGui_ImplGlfw_InitForOther(g_window, true);

	ImGui_ImplDX12_InitInfo init_info = {};
	init_info.Device = g_d3dDevice;
	init_info.CommandQueue = g_commandQueue;
	init_info.NumFramesInFlight = FRAME_COUNT;
	init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	init_info.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	init_info.SrvDescriptorHeap = g_srvDescHeap;
	init_info.SrvDescriptorAllocFn = ImGui_SrvAlloc;
	init_info.SrvDescriptorFreeFn  = ImGui_SrvFree;

	ImGui_ImplDX12_Init(&init_info);
	logStartupStage("ImGuiFonts", "Dear ImGui and fonts initialized");

	InitFallbackTextures();
	InitLightmapSrvSlot();
	TextureManager::Get().RegisterQualityChangedCallback("D3D12Renderer", [](TextureQuality /*q*/, int /*maxRes*/) {
		UpdateAllTextureLODClamps();
	});
	if (!isGameOnlyMode) {
		DX12GpuTexture dirLightIcon = LoadEditorIconSync("Resources/Icons/directional_light.png", g_fallbackWhite);
		g_engineUI.directionalLightIconGpuHandle = dirLightIcon.gpuHandle.ptr;

		DX12GpuTexture ptLightIcon = LoadEditorIconSync("Resources/Icons/point_light.png", g_fallbackWhite);
		g_engineUI.pointLightIconGpuHandle = ptLightIcon.gpuHandle.ptr;

		DX12GpuTexture spotLightIcon = LoadEditorIconSync("Resources/Icons/spot_light.png", g_fallbackWhite);
		g_engineUI.spotLightIconGpuHandle = spotLightIcon.gpuHandle.ptr;

		DX12GpuTexture areaLightIcon = LoadEditorIconSync("Resources/Icons/area_light.png", g_fallbackWhite);
		g_engineUI.areaLightIconGpuHandle = areaLightIcon.gpuHandle.ptr;

		DX12GpuTexture lightIcon = LoadEditorIconSync("Resources/Icons/light.png", ptLightIcon);
		g_engineUI.lightIconGpuHandle = lightIcon.gpuHandle.ptr ? lightIcon.gpuHandle.ptr : ptLightIcon.gpuHandle.ptr;

		DX12GpuTexture skyLightIcon = LoadEditorIconSync("Resources/Icons/sky_light.png", lightIcon);
		g_engineUI.skyLightIconGpuHandle = skyLightIcon.gpuHandle.ptr ? skyLightIcon.gpuHandle.ptr : lightIcon.gpuHandle.ptr;

		DX12GpuTexture cameraIcon = LoadEditorIconSync("Resources/Icons/camera.png", g_fallbackWhite);
		g_engineUI.cameraIconGpuHandle = cameraIcon.gpuHandle.ptr;
		DX12GpuTexture engineIcon = LoadEditorIconSync("Resources/Icons/eunoia.png", g_fallbackWhite);
		g_engineUI.engineIconGpuHandle = engineIcon.gpuHandle.ptr;
		DX12GpuTexture eyeIcon = LoadEditorIconSync("Resources/Icons/Outliner/Eye.png", g_fallbackWhite);
		g_engineUI.outlinerEyeIconGpuHandle = eyeIcon.gpuHandle.ptr;

		DX12GpuTexture cbFolderIcon = LoadEditorIconSync("Resources/Icons/Content Browser/folder.png", g_fallbackWhite);
		g_engineUI.cbFolderIconGpuHandle = cbFolderIcon.gpuHandle.ptr;
		DX12GpuTexture cb3dIcon = LoadEditorIconSync("Resources/Icons/Content Browser/3d.png", g_fallbackWhite);
		g_engineUI.cb3dIconGpuHandle = cb3dIcon.gpuHandle.ptr;
		DX12GpuTexture cbMatIcon = LoadEditorIconSync("Resources/Icons/Content Browser/material.png", g_fallbackWhite);
		g_engineUI.cbMaterialIconGpuHandle = cbMatIcon.gpuHandle.ptr;
		DX12GpuTexture cbSkyIcon = LoadEditorIconSync("Resources/Icons/Content Browser/skybox.png", g_fallbackWhite);
		g_engineUI.cbSkyboxIconGpuHandle = cbSkyIcon.gpuHandle.ptr;
		DX12GpuTexture cbTexIcon = LoadEditorIconSync("Resources/Icons/Content Browser/texture.png", g_fallbackWhite);
		g_engineUI.cbTextureIconGpuHandle = cbTexIcon.gpuHandle.ptr;
		DX12GpuTexture cbLevelIcon = LoadEditorIconSync("Resources/Icons/Content Browser/level.png", g_fallbackWhite);
		g_engineUI.cbLevelIconGpuHandle = cbLevelIcon.gpuHandle.ptr;
		DX12GpuTexture cbBehIcon = LoadEditorIconSync("Resources/Icons/Content Browser/behaviour.png", g_fallbackWhite);
		g_engineUI.cbBehaviourIconGpuHandle = cbBehIcon.gpuHandle.ptr;
	}
	EunoiaUI::UISystemManager::Get().SetTextureLoader([](const std::string& path, int* outW, int* outH) -> uint64_t {
		DX12GpuTexture tex = LoadEditorIconSync(path, g_fallbackWhite);
		if (tex.resource && tex.gpuHandle.ptr != 0) {
			if (outW) *outW = tex.width;
			if (outH) *outH = tex.height;
			return tex.gpuHandle.ptr;
		}
		return 0;
	});

	StartupSplash::Get().PumpEvents();
	logStartupStage("Resources", "Fallback textures and icons loaded");

	// Initialize EngineUI with the project selected from the standalone Project Browser window
	g_engineUI.InitWithProject(selectedProjectPath, g_scene, g_camera, isGameOnlyMode, gameScenePath, gameBinPath);

	StartupSplash::Get().PumpEvents();
	logStartupStage("EngineUI", "Engine UI and Asset Registry initialized");

	if (!isGameOnlyMode) {
		if (g_engineUI.pendingInitialSceneLoad) {
			StartupSplash::Get().UpdateProgress(0.15f, "Preparing project level...");
			g_engineUI.LoadPendingInitialScene(g_scene, g_camera, isGameOnlyMode);
		}

		// Fast drain staged actors and keep splash updated smoothly
		while (g_engineUI.IsProgressiveLoadingActive()) {
			if (!g_engineUI.m_progressiveLoader.completed) {
				g_engineUI.m_progressiveLoader.Step(g_scene, 128);
				float p = g_engineUI.m_progressiveLoader.progress.load();
				std::string detail;
				{
					std::lock_guard<std::mutex> lock(g_engineUI.m_progressiveLoader.stateMutex);
					detail = g_engineUI.m_progressiveLoader.currentDetail;
				}
				StartupSplash::Get().UpdateProgress(p, detail.empty() ? "Loading objects, lights & skyboxes..." : detail);
			} else {
				break;
			}
		}

		updateSceneGeometry();
		if (g_scene.objects.empty()) {
			StartupSplash::Get().UpdateProgress(1.0f, "Empty level ready.");
			logStartupStage("ScenePreload", "Empty level initialized");
		} else {
			StartupSplash::Get().UpdateProgress(1.0f, "Scene completely loaded (" + std::to_string(g_scene.objects.size()) + " actors).");
			logStartupStage("ScenePreload", "Scene and actors loaded");
		}

		// Warm up GPU pipelines and render targets behind splash before revealing window to prevent blank frame
		for (int warmup = 0; warmup < 2; ++warmup)
		{
			InputSystem::Get().BeginFrame();
			glfwPollEvents();
			InputSystem::Get().Update(g_window);

			ImGui_ImplDX12_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			bool warmupExit = false;
			g_engineUI.Render(g_scene, g_camera, 60.0f, 16.6f, g_currentVertexCount, g_currentIndexCount, warmupExit);

			ImGui::Render();

			if (g_fence && g_fenceValues[g_frameIndex] > 0 && g_fence->GetCompletedValue() < g_fenceValues[g_frameIndex])
			{
				SafeWaitForFence(g_fence, g_fenceValues[g_frameIndex], g_fenceEvent, 5000, "Warmup frame fence sync");
			}

			updateSceneGeometry();
			updateConstantBuffer();
			PreRenderUploadTextures();
			renderFrame();
		}
		logStartupStage("WarmupFrames", "Warm-up frames presented behind splash");

		StartupSplash::Get().Close();
		ShowWindow(hwnd, SW_MAXIMIZE);
		glfwShowWindow(g_window);
		UpdateWindow(hwnd);
		logStartupStage("EditorWindow", "Editor window opened maximized â€” startup complete");
	} else {
		// Dedicated Standalone Game Mode:
		// Drain background progressive loader until the startup level is completely loaded
		while (g_engineUI.IsProgressiveLoadingActive() && !g_engineUI.m_progressiveLoader.completed && !g_engineUI.m_progressiveLoader.hasError) {
			g_engineUI.m_progressiveLoader.Step(g_scene, 256);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		if (g_engineUI.m_progressiveLoader.completed) {
			g_engineUI.m_progressiveLoader.active = false;
		}

		GameObject* camObj = nullptr;
		if (g_scene.activeLevelCameraId != -1) {
			camObj = g_scene.FindObject(g_scene.activeLevelCameraId);
		}
		if (!camObj) {
			for (auto& obj : g_scene.objects) {
				if (obj.isCamera || obj.type == PrimitiveType::Camera) {
					camObj = &obj;
					g_scene.activeLevelCameraId = obj.id;
					break;
				}
			}
		}
		if (camObj && (camObj->isCamera || camObj->type == PrimitiveType::Camera)) {
			glm::mat4 worldMat = g_scene.GetWorldMatrix(*camObj);
			glm::vec3 worldPos = glm::vec3(worldMat[3]);
			glm::vec3 worldRot = camObj->rotation;
			if (camObj->parentId != -1)
			{
				const GameObject* p = g_scene.FindObjectConst(camObj->parentId);
				while (p)
				{
					worldRot += p->rotation;
					p = (p->parentId != -1) ? g_scene.FindObjectConst(p->parentId) : nullptr;
				}
			}
			g_camera.yaw = worldRot.y;
			g_camera.pitch = -std::clamp(worldRot.x, -89.0f, 89.0f);
			g_camera.fov = camObj->camera.fov;
			g_camera.isOrthographic = camObj->camera.isOrthographic;
			g_camera.orthoSize = camObj->camera.orthoSize;
			g_camera.nearPlane = std::max(0.01f, camObj->camera.nearPlane);
			g_camera.farPlane = std::max(1.0f, camObj->camera.farPlane);
			g_camera.distance = 0.001f;
			g_camera.target = worldPos + g_camera.GetForward() * 0.001f;
		} else if (hasCmdCam) {
			g_camera.yaw = cmdCamYaw;
			g_camera.pitch = std::clamp(cmdCamPitch, -89.0f, 89.0f);
			g_camera.target = cmdCamTarget;
			g_camera.distance = cmdCamDist;
			g_camera.fov = cmdCamFov;
		}
		g_scene.showGrid = false;
		g_engineUI.showGizmo = false;
		CursorState.Locked();
		g_scene.StartPlayMode();

		// Warm up GPU pipelines and present 2 frames so swapchain and geometry buffers are ready
		for (int warmup = 0; warmup < 2; ++warmup)
		{
			InputSystem::Get().BeginFrame();
			glfwPollEvents();
			InputSystem::Get().Update(g_window);

			ImGui_ImplDX12_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			bool warmupExit = false;
			g_engineUI.Render(g_scene, g_camera, 60.0f, 16.6f, g_currentVertexCount, g_currentIndexCount, warmupExit);

			ImGui::Render();

			if (g_fence && g_fenceValues[g_frameIndex] > 0 && g_fence->GetCompletedValue() < g_fenceValues[g_frameIndex])
			{
				SafeWaitForFence(g_fence, g_fenceValues[g_frameIndex], g_fenceEvent, 5000, "Warmup frame fence sync");
			}

			updateSceneGeometry();
			updateConstantBuffer();
			PreRenderUploadTextures();
			renderFrame();
		}

		ShowWindow(hwnd, SW_SHOW);
		glfwShowWindow(g_window);
		UpdateWindow(hwnd);
	}

	auto lastTime = std::chrono::high_resolution_clock::now();
	float frameCount = 0.0f;
	float fpsTimer = 0.0f;
	float currentFps = 60.0f;
	float currentFrameTimeMs = 16.6f;

	// 4. Main Event & Render Loop
	while (!glfwWindowShouldClose(g_window))
	{
		InputSystem::Get().BeginFrame();
		glfwPollEvents();
		InputSystem::Get().Update(g_window);

		auto currentTime = std::chrono::high_resolution_clock::now();
		float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(
			currentTime - lastTime).count();
		lastTime = currentTime;

		if (deltaTime > 0.1f) deltaTime = 0.1f;
		ScreenPrint::System::Get().Update(deltaTime);

		// Once the editor window is open and displayed (no viewport rendered initially), load the respective scene/level
		if (!isGameOnlyMode && g_engineUI.pendingInitialSceneLoad)
		{
			if (g_engineUI.initialLoadFrameCountdown > 0)
			{
				g_engineUI.initialLoadFrameCountdown--;
			}
			else
			{
				g_engineUI.LoadPendingInitialScene(g_scene, g_camera, isGameOnlyMode);
				updateSceneGeometry();
			}
		}

		// Process Fly Mode (Hold RMB + W, S, A, D, E, Q) - Smooth acceleration, deceleration & ease-in-out
		if (!g_scene.isPlayMode)
		{
			glm::vec3 targetVelocity(0.0f);

			OrbitCamera& activeFlyMoveCam = g_engineUI.GetActiveRenderCamera(g_camera);
			if (s_isRightMouseDown && activeFlyMoveCam.isFlying)
			{
				float speed = activeFlyMoveCam.moveSpeed;
				if (glfwGetKey(g_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
				    glfwGetKey(g_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
				{
					speed *= 2.5f; // Fast fly with Shift
				}

				glm::vec3 moveDir(0.0f);
				glm::vec3 forward = activeFlyMoveCam.GetForward();
				glm::vec3 right   = activeFlyMoveCam.GetRight();
				glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

				if (glfwGetKey(g_window, GLFW_KEY_W) == GLFW_PRESS) moveDir += forward;
				if (glfwGetKey(g_window, GLFW_KEY_S) == GLFW_PRESS) moveDir -= forward;
				if (glfwGetKey(g_window, GLFW_KEY_D) == GLFW_PRESS) moveDir += right;
				if (glfwGetKey(g_window, GLFW_KEY_A) == GLFW_PRESS) moveDir -= right;
				if (glfwGetKey(g_window, GLFW_KEY_E) == GLFW_PRESS) moveDir += worldUp;
				if (glfwGetKey(g_window, GLFW_KEY_Q) == GLFW_PRESS) moveDir -= worldUp;

				if (glm::length(moveDir) > 0.001f)
				{
					targetVelocity = glm::normalize(moveDir) * speed;
				}
			}

			// Smooth exponential lerp (ease-in-ease-out acceleration & deceleration)
			// Higher response rate (14.0f) during acceleration, natural smooth damping (10.0f) during stop
			float blendRate = (glm::length(targetVelocity) > 0.001f) ? 14.0f : 10.0f;
			float blendFactor = 1.0f - std::exp(-blendRate * deltaTime);
			s_camVelocity = glm::mix(s_camVelocity, targetVelocity, blendFactor);

			// Cut off sub-epsilon velocity to avoid drift
			if (glm::length(s_camVelocity) < 0.005f && glm::length(targetVelocity) < 0.001f)
			{
				s_camVelocity = glm::vec3(0.0f);
			}

			if (glm::length(s_camVelocity) > 0.0001f)
			{
				activeFlyMoveCam.Move(s_camVelocity * deltaTime);
				if (&activeFlyMoveCam == &g_camera) SyncViewportCameraToLevelCamera();
				g_engineUI.RequestViewportRedraw(10);
			}
		}

		// Generic camera motion tracking (catches any camera move/rotation from script, gizmo, focus)
		if (!g_scene.isPlayMode)
		{
			static glm::vec3 s_lastCamPos(0.0f);
			static float s_lastCamYaw = 0.0f;
			static float s_lastCamPitch = 0.0f;
			static glm::vec3 s_lastCamTarget(0.0f);
			static float s_lastCamDist = 0.0f;
			static float s_lastCamFov = 0.0f;
			if (glm::distance(g_camera.GetPosition(), s_lastCamPos) > 1e-4f ||
			    std::abs(g_camera.yaw - s_lastCamYaw) > 1e-4f ||
			    std::abs(g_camera.pitch - s_lastCamPitch) > 1e-4f ||
			    glm::distance(g_camera.target, s_lastCamTarget) > 1e-4f ||
			    std::abs(g_camera.distance - s_lastCamDist) > 1e-4f ||
			    std::abs(g_camera.fov - s_lastCamFov) > 1e-4f)
			{
				s_lastCamPos = g_camera.GetPosition();
				s_lastCamYaw = g_camera.yaw;
				s_lastCamPitch = g_camera.pitch;
				s_lastCamTarget = g_camera.target;
				s_lastCamDist = g_camera.distance;
				s_lastCamFov = g_camera.fov;
				g_engineUI.RequestViewportRedraw(10);
			}
		}

		// Game Mode Exit listener (Escape or Ctrl + Delete closes standalone game or exits in-editor Play Mode)
		if (isGameOnlyMode)
		{
			if (glfwGetKey(g_window, GLFW_KEY_ESCAPE) == GLFW_PRESS || InputSystem::Get().IsKeyPressed(Key::Escape))
			{
				CursorState.UnLocked();
				glfwSetWindowShouldClose(g_window, GLFW_TRUE);
			}
		}
		// Game Mode Exit listener (Escape or Ctrl + Delete closes standalone game or exits in-editor Play Mode)
		if (isGameOnlyMode)
		{
			if (glfwGetKey(g_window, GLFW_KEY_ESCAPE) == GLFW_PRESS || InputSystem::Get().IsKeyPressed(Key::Escape))
			{
				CursorState.UnLocked();
				glfwSetWindowShouldClose(g_window, GLFW_TRUE);
			}
		}
		// Game Mode Exit listener (Ctrl + Delete closes standalone game or exits in-editor Play Mode)
		if (isGameOnlyMode || g_scene.isPlayMode)
		{
			bool isCtrlDown = (glfwGetKey(g_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
			                   glfwGetKey(g_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS ||
			                   InputSystem::Get().IsKeyDown(Key::LeftControl) ||
			                   InputSystem::Get().IsKeyDown(Key::RightControl) ||
			                   ImGui::GetIO().KeyCtrl);
			bool isDeletePressed = (InputSystem::Get().IsKeyPressed(Key::Delete) ||
			                        glfwGetKey(g_window, GLFW_KEY_DELETE) == GLFW_PRESS);
			if (isCtrlDown && isDeletePressed)
			{
				CursorState.UnLocked();
				if (isGameOnlyMode)
				{
					glfwSetWindowShouldClose(g_window, GLFW_TRUE);
				}
				else if (g_scene.isPlayMode)
				{
					g_engineUI.ExitPlayMode(g_scene, &g_camera);
				}
			}
		}

		frameCount += 1.0f;
		fpsTimer += deltaTime;
		if (fpsTimer >= 0.5f)
		{
			currentFps = frameCount / fpsTimer;
			currentFrameTimeMs = (fpsTimer / frameCount) * 1000.0f;
			frameCount = 0.0f;
			fpsTimer = 0.0f;
		}

		// Update 3D Scene animations and runtime behaviours
		g_scene.Update(deltaTime);
		Eunoia::WaterSystem::Get().Update(deltaTime);

		// Synchronize viewport camera dynamically as the level camera moves or rotates
		SyncLevelCameraToViewportCamera();

		// Forward input to Custom UI Framework
		// New ImGui Frame (Pure Dear ImGui - lightweight input and render)
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		if (isGameOnlyMode && !InputSystem::Get().IsCursorLocked())
		{
			if (glfwGetMouseButton(g_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS ||
			    glfwGetMouseButton(g_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
			{
				CursorState.Locked();
			}
		}

		bool shouldLockCursor = InputSystem::Get().IsCursorLocked() || (g_camera.isFlying && s_isRightMouseDown);
		if (shouldLockCursor)
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_None);
			if (glfwGetInputMode(g_window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
			{
				glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
				if (glfwRawMouseMotionSupported())
				{
					glfwSetInputMode(g_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
				}
			}
		}
		else
		{
			if (glfwGetInputMode(g_window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
			{
				if (glfwRawMouseMotionSupported())
				{
					glfwSetInputMode(g_window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
				}
				glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
			}
		}

		// Render Unreal Engine 5 Editor UI & 3D Gizmos
		bool shouldExit = false;
		g_engineUI.Render(g_scene, g_camera, currentFps, currentFrameTimeMs,
		                  g_currentVertexCount, g_currentIndexCount, shouldExit);
		if (shouldExit)
		{
			glfwSetWindowShouldClose(g_window, GLFW_TRUE);
		}

		// Process deferred 3D object picking only if the click was NOT on an active/hovered gizmo or UI element (disabled in Play Mode)
		if (!g_scene.isPlayMode && s_pendingPickClick)
		{
			s_pendingPickClick = false;
			ImGuiIO& io = ImGui::GetIO();
			bool isPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
			if (!g_camera.isFlying && !io.WantCaptureMouse && !isPopupOpen && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
			{
				double mouseX, mouseY;
				glfwGetCursorPos(g_window, &mouseX, &mouseY);
				PickObjectAtCursor((float)mouseX, (float)mouseY);
			}
		}

		static int s_recoveryAttempts = 0;

		// Device Loss Notification Overlay (dev.md Task 3) - Only shown if automatic recovery attempts were exhausted
		if (g_deviceLost && s_recoveryAttempts >= 3)
		{
			ImGui::OpenPopup("GPU Device Lost");
			ImVec2 center = ImGui::GetMainViewport()->GetCenter();
			ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(540, 250), ImGuiCond_Appearing);
			if (ImGui::BeginPopupModal("GPU Device Lost", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
			{
				ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "CRITICAL: GPU Device Was Lost (Driver Reset)");
				ImGui::Separator();
				ImGui::Spacing();
				ImGui::TextWrapped("The graphics driver reset or the GPU device was removed. Automatic recovery was attempted but the driver remained unresponsive.");
				ImGui::Spacing();
				ImGui::TextWrapped("You can retry recovery or save your level to avoid losing changes before restarting the editor.");
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
				if (ImGui::Button("Retry Recovery", ImVec2(140, 32)))
				{
					s_recoveryAttempts = 0;
				}
				ImGui::SameLine();
				if (ImGui::Button("Save Current Level", ImVec2(160, 32)))
				{
					g_engineUI.SaveLevel(g_scene);
				}
				ImGui::SameLine();
				if (ImGui::Button("Restart / Exit", ImVec2(130, 32)))
				{
					glfwSetWindowShouldClose(g_window, GLFW_TRUE);
				}
				ImGui::EndPopup();
			}
		}

		ImGui::Render();

		// Update 3D Scene GPU Buffers AFTER gizmo has applied any transform changes
		// When device is lost, attempt automatic recovery to resume from exact condition
		if (g_deviceLost)
		{
			HWND hwnd = glfwGetWin32Window(g_window);
			if (s_recoveryAttempts < 3)
			{
				s_recoveryAttempts++;
				std::cerr << "[RECOVERY] Attempting automatic device recreation (attempt " << s_recoveryAttempts << "/3)...\n";
				g_engineUI.AddLog("LogRecovery", "Attempting automatic D3D12 device recovery (attempt " + std::to_string(s_recoveryAttempts) + "/3)...", 1);
				if (RecoverD3D12Device(hwnd))
				{
					s_recoveryAttempts = 0;
					g_engineUI.AddLog("LogRecovery", "DirectX 12 graphics device successfully recovered! Resumed from exact condition.", 0);
				}
				else
				{
					Sleep(100);
				}
			}
			else
			{
				Sleep(50);
			}
		}
		else
		{
			try {
				// Synchronize: wait for the frame that previously used this backbuffer / upload buffers to finish on GPU
				if (g_fence && g_fenceValues[g_frameIndex] > 0 && g_fence->GetCompletedValue() < g_fenceValues[g_frameIndex])
				{
					SafeWaitForFence(g_fence, g_fenceValues[g_frameIndex], g_fenceEvent, 5000, "Frame start buffer sync");
				}

				bool need3DSceneRender = (g_engineUI.isGameOnlyWindow ||
				                          g_scene.isPlayMode ||
				                          g_engineUI.isPlayModeActive ||
				                          g_engineUI.realtimeViewport ||
				                          g_engineUI.showMaterialEditor ||
				                          g_engineUI.viewportRedrawCountdown > 0 ||
				                          (&g_engineUI.GetActiveRenderScene(g_scene) != &g_scene));

				if (need3DSceneRender)
				{
					updateSceneGeometry();
					updateConstantBuffer();

					// Pre-upload all scene batch textures before the frame command list begins recording
					PreRenderUploadTextures();
				}

				// Render DirectX 12 Frame
				renderFrame();

				// Idle power saver: when viewport is not rendering and no UI interaction is taking place
				if (!need3DSceneRender && !ImGui::IsAnyItemActive() && !s_isLeftMouseDown && !s_isRightMouseDown && !s_isMiddleMouseDown)
				{
					Sleep(8);
				}
			} catch (const std::exception& ex) {
				std::cerr << "[RECOVERY] Frame exception: " << ex.what() << "\n";
				g_engineUI.AddLog("LogRecovery", std::string("Frame exception caught, skipping frame: ") + ex.what(), 3);
			} catch (...) {
				std::cerr << "[RECOVERY] Unknown frame exception\n";
				g_engineUI.AddLog("LogRecovery", "Unknown frame exception caught, skipping frame", 3);
			}
		}
	}

	// 5. Cleanup
	if (isGameOnlyMode) {
		g_scene.StopPlayMode();
	}

	cleanUpD3D12();

	glfwDestroyWindow(g_window);
	glfwTerminate();

	return EXIT_SUCCESS;
}
