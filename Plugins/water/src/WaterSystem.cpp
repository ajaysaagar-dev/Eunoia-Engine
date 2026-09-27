// ============================================================================
// Eunoia Engine — AquaTerrain DX12 Water System Implementation
// ============================================================================

#include "Water/WaterSystem.h"
#include <d3dcompiler.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace Eunoia {

// C++ reflection of WaterConstants (matches WaterCommon.hlsli exactly, 56 floats = 224 bytes)
struct alignas(16) WaterShaderConstantsBuffer
{
    glm::mat4 viewProj;          // 0-15
    glm::vec3 cameraPos;         // 16-18
    float     time;              // 19
    glm::vec4 waterColor;        // 20-23
    glm::vec4 shallowColor;      // 24-27
    float     roughness;         // 28
    float     opacity;           // 29
    float     reflectionStr;     // 30
    float     refractionStr;     // 31
    float     waveSpeed;         // 32
    float     waveScale;         // 33
    float     waveHeight;        // 34
    float     normalScale;       // 35
    float     foamEnabled;       // 36
    float     foamScale;         // 37
    float     depthFade;         // 38
    float     specularIntensity; // 39
    float     foamDistance;      // 40
    float     foamStrength;      // 41
    float     foamSpeed;         // 42
    float     fresnelStrength;   // 43
    float     specularPower;     // 44
    float     sparklePower;      // 45
    float     nearPlane;         // 46
    float     farPlane;          // 47
    glm::vec2 screenSize;        // 48-49
    glm::vec2 flowDirection;     // 50-51
    float     flowSpeed;         // 52
    float     simEnabled;        // 53
    float     heightScale;       // 54
    float     _padCommon;        // 55
};
static_assert(sizeof(WaterShaderConstantsBuffer) == 224, "WaterShaderConstantsBuffer must be exactly 224 bytes (56 floats)");

WaterSystem& WaterSystem::Get() {
    static WaterSystem s_instance;
    return s_instance;
}

WaterSystem::WaterSystem() = default;

WaterSystem::~WaterSystem() {
    Shutdown();
}

bool WaterSystem::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, UINT width, UINT height) {
    if (!device || !queue) return false;
    m_device = device;
    m_commandQueue = queue;
    m_width = width ? width : 1280;
    m_height = height ? height : 720;

    m_descriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    if (!CreateResources()) {
        std::cerr << "[WaterSystem] Failed to create water GPU resources." << std::endl;
        return false;
    }

    if (!CreatePipelines()) {
        std::cerr << "[WaterSystem] Failed to create water pipelines." << std::endl;
        return false;
    }

    m_initialized = true;
    std::cout << "[WaterSystem] AquaTerrain DX12 Dynamic Water System initialized successfully ("
              << SIM_RES << "x" << SIM_RES << " simulation grid)." << std::endl;
    return true;
}

void WaterSystem::Shutdown() {
    ReleaseResources();
    m_device = nullptr;
    m_commandQueue = nullptr;
    m_initialized = false;
}

void WaterSystem::Resize(UINT width, UINT height) {
    m_width = width ? width : 1280;
    m_height = height ? height : 720;
}

void WaterSystem::Update(float deltaTime) {
    m_waterTime += deltaTime;
    if (m_waterTime > 1e6f) m_waterTime = 0.0f;
    m_simAccumulator += deltaTime;
}

void WaterSystem::AddImpulse(const glm::vec3& worldPos, float radius, float strength) {
    // Convert world position into UV coordinates for each water body
    // In our system, the simulation is mapped over the primary water body
    AddImpulseUV(0.5f, 0.5f, radius * 0.05f, strength);
}

void WaterSystem::AddImpulseUV(float u, float v, float radiusUV, float strength) {
    u = std::clamp(u, 0.05f, 0.95f);
    v = std::clamp(v, 0.05f, 0.95f);
    radiusUV = std::clamp(radiusUV, 0.01f, 0.35f);
    m_pendingImpulses.push_back({ u, v, radiusUV, strength });
}

bool WaterSystem::CreateResources() {
    ReleaseResources();

    // 1. Allocate 3 Ping-Pong height textures (Prev, Curr, Next)
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = SIM_RES;
    texDesc.Height = SIM_RES;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    for (int i = 0; i < 3; ++i) {
        HRESULT hr = m_device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            nullptr,
            IID_PPV_ARGS(&m_heightTextures[i])
        );
        if (FAILED(hr)) {
            std::cerr << "[WaterSystem] Failed to create height texture " << i << ": " << hr << std::endl;
            return false;
        }
        m_heightStates[i] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        std::wstring name = L"Water_HeightTexture_" + std::to_wstring(i);
        m_heightTextures[i]->SetName(name.c_str());
    }

    // 2. Allocate Descriptor Heap (3 UAVs + 3 SRVs + 1 Depth SRV = 7 descriptors)
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 7;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hr = m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_descriptorHeap));
    if (FAILED(hr)) {
        std::cerr << "[WaterSystem] Failed to create descriptor heap: " << hr << std::endl;
        return false;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = m_descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = m_descriptorHeap->GetGPUDescriptorHandleForHeapStart();

    // Create 3 UAVs (slots 0..2)
    for (int i = 0; i < 3; ++i) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_device->CreateUnorderedAccessView(m_heightTextures[i], nullptr, &uavDesc, cpuStart);

        m_uavCpuHandles[i] = cpuStart;
        m_uavGpuHandles[i] = gpuStart;
        cpuStart.ptr += m_descriptorSize;
        gpuStart.ptr += m_descriptorSize;
    }

    // Create 3 SRVs (slots 3..5)
    for (int i = 0; i < 3; ++i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(m_heightTextures[i], &srvDesc, cpuStart);

        m_srvCpuHandles[i] = cpuStart;
        m_srvGpuHandles[i] = gpuStart;
        cpuStart.ptr += m_descriptorSize;
        gpuStart.ptr += m_descriptorSize;
    }

    // Slot 6: Depth SRV (populated dynamically during Render)
    m_depthSrvCpuHandle = cpuStart;
    m_depthSrvGpuHandle = gpuStart;

    return true;
}

void WaterSystem::ReleaseResources() {
    for (int i = 0; i < 3; ++i) {
        if (m_heightTextures[i]) {
            m_heightTextures[i]->Release();
            m_heightTextures[i] = nullptr;
        }
    }
    if (m_descriptorHeap) {
        m_descriptorHeap->Release();
        m_descriptorHeap = nullptr;
    }
    if (m_disturbRootSig) { m_disturbRootSig->Release(); m_disturbRootSig = nullptr; }
    if (m_disturbPSO) { m_disturbPSO->Release(); m_disturbPSO = nullptr; }
    if (m_updateRootSig) { m_updateRootSig->Release(); m_updateRootSig = nullptr; }
    if (m_updatePSO) { m_updatePSO->Release(); m_updatePSO = nullptr; }
    if (m_waterRenderRootSig) { m_waterRenderRootSig->Release(); m_waterRenderRootSig = nullptr; }
    if (m_waterRenderPSO) { m_waterRenderPSO->Release(); m_waterRenderPSO = nullptr; }
}

static ID3DBlob* CompileOrFallback(
    const std::vector<std::string>& filePaths,
    const std::string& fallbackCode,
    const char* entryPoint,
    const char* target)
{
    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = E_FAIL;

    for (const auto& path : filePaths) {
        std::ifstream f(path);
        if (f.good()) {
            std::wstring wpath(path.begin(), path.end());
            hr = D3DCompileFromFile(wpath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    entryPoint, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
            if (SUCCEEDED(hr)) return blob;
            if (errors) {
                std::cerr << "[WaterSystem] Compile error in " << path << ": "
                          << (char*)errors->GetBufferPointer() << std::endl;
                errors->Release();
                errors = nullptr;
            }
        }
    }

    // Fallback to embedded string compilation
    hr = D3DCompile(fallbackCode.c_str(), fallbackCode.size(), nullptr, nullptr, nullptr,
                    entryPoint, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
    if (FAILED(hr)) {
        if (errors) {
            std::cerr << "[WaterSystem] Fallback compile error (" << target << "): "
                      << (char*)errors->GetBufferPointer() << std::endl;
            errors->Release();
        }
        return nullptr;
    }
    return blob;
}

bool WaterSystem::CreatePipelines() {
    HRESULT hr;
    ID3DBlob* errorBlob = nullptr;

    // ------------------------------------------------------------------------
    // 1. Disturbance Root Signature & Compute PSO
    // ------------------------------------------------------------------------
    {
        D3D12_DESCRIPTOR_RANGE uavRange = {};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 0;
        uavRange.RegisterSpace = 0;
        uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER rootParams[2] = {};
        // 0: Constants (float2 GridSize, float2 Center, float Radius, float Strength = 6 floats)
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParams[0].Constants.ShaderRegister = 0;
        rootParams[0].Constants.RegisterSpace = 0;
        rootParams[0].Constants.Num32BitValues = 8;

        // 1: UAV table (u0)
        rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[1].DescriptorTable.pDescriptorRanges = &uavRange;

        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.NumParameters = 2;
        rsDesc.pParameters = rootParams;
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob* sigBlob = nullptr;
        hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob);
        if (FAILED(hr)) return false;
        hr = m_device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&m_disturbRootSig));
        sigBlob->Release();
        if (FAILED(hr)) return false;

        const std::string fallbackDisturb = R"HLSL(
RWTexture2D<float> gWaterHeight : register(u0);
cbuffer WaterDisturbConstants : register(b0) {
    float2 GridSize;
    float2 Center;
    float  Radius;
    float  Strength;
};
[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)GridSize.x || id.y >= (uint)GridSize.y) return;
    float2 uv = (float2(id.xy) + 0.5) / GridSize;
    float dist = distance(uv, Center);
    float falloff = smoothstep(0.0, 1.0, saturate(1.0 - dist / max(Radius, 0.001)));
    gWaterHeight[id.xy] += falloff * Strength;
}
)HLSL";

        ID3DBlob* csBlob = CompileOrFallback(
            { "Shaders/Water/WaterDisturbCS.hlsl", "../Shaders/Water/WaterDisturbCS.hlsl", "Build/Engine/Shaders/Water/WaterDisturbCS.hlsl" },
            fallbackDisturb, "CSMain", "cs_5_0"
        );
        if (!csBlob) return false;

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_disturbRootSig;
        psoDesc.CS.pShaderBytecode = csBlob->GetBufferPointer();
        psoDesc.CS.BytecodeLength = csBlob->GetBufferSize();
        hr = m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_disturbPSO));
        csBlob->Release();
        if (FAILED(hr)) return false;
    }

    // ------------------------------------------------------------------------
    // 2. Wave Propagation Update Root Signature & Compute PSO
    // ------------------------------------------------------------------------
    {
        D3D12_DESCRIPTOR_RANGE srvRange0 = {};
        srvRange0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange0.NumDescriptors = 1;
        srvRange0.BaseShaderRegister = 0; // t0: PrevHeight

        D3D12_DESCRIPTOR_RANGE srvRange1 = {};
        srvRange1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange1.NumDescriptors = 1;
        srvRange1.BaseShaderRegister = 1; // t1: CurrHeight

        D3D12_DESCRIPTOR_RANGE uavRange = {};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 0; // u0: NextHeight

        D3D12_ROOT_PARAMETER rootParams[4] = {};
        // 0: Constants (float2 GridSize, float k1, float k2, float k3, float _pad0,1,2 = 8 floats)
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParams[0].Constants.ShaderRegister = 0;
        rootParams[0].Constants.RegisterSpace = 0;
        rootParams[0].Constants.Num32BitValues = 8;

        // 1: t0 (Prev)
        rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange0;

        // 2: t1 (Curr)
        rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[2].DescriptorTable.pDescriptorRanges = &srvRange1;

        // 3: u0 (Next)
        rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[3].DescriptorTable.pDescriptorRanges = &uavRange;

        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.NumParameters = 4;
        rsDesc.pParameters = rootParams;
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob* sigBlob = nullptr;
        hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob);
        if (FAILED(hr)) return false;
        hr = m_device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&m_updateRootSig));
        sigBlob->Release();
        if (FAILED(hr)) return false;

        const std::string fallbackUpdate = R"HLSL(
Texture2D<float> gPrevHeight   : register(t0);
Texture2D<float> gCurrHeight   : register(t1);
RWTexture2D<float> gNextHeight : register(u0);
cbuffer WaterUpdateConstants : register(b0) {
    float2 GridSize;
    float  k1;
    float  k2;
    float  k3;
    float3 _pad;
};
[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    int2 coord = int2(id.xy);
    if (coord.x >= (int)GridSize.x || coord.y >= (int)GridSize.y) return;
    float prev = gPrevHeight.Load(int3(coord, 0));
    float curr = gCurrHeight.Load(int3(coord, 0));
    int2 maxC = int2(GridSize) - 1;
    float left  = gCurrHeight.Load(int3(clamp(coord + int2(-1,  0), int2(0, 0), maxC), 0));
    float right = gCurrHeight.Load(int3(clamp(coord + int2( 1,  0), int2(0, 0), maxC), 0));
    float up    = gCurrHeight.Load(int3(clamp(coord + int2( 0, -1), int2(0, 0), maxC), 0));
    float down  = gCurrHeight.Load(int3(clamp(coord + int2( 0,  1), int2(0, 0), maxC), 0));
    float next = k1 * prev + k2 * curr + k3 * (left + right + up + down);
    int distFromEdge = min(min(coord.x, (int)GridSize.x - 1 - coord.x),
                           min(coord.y, (int)GridSize.y - 1 - coord.y));
    if (distFromEdge < 4) next *= (float)distFromEdge / 4.0;
    next = clamp(next, -50.0, 50.0);
    gNextHeight[coord] = next;
}
)HLSL";

        ID3DBlob* csBlob = CompileOrFallback(
            { "Shaders/Water/WaterUpdateCS.hlsl", "../Shaders/Water/WaterUpdateCS.hlsl", "Build/Engine/Shaders/Water/WaterUpdateCS.hlsl" },
            fallbackUpdate, "CSMain", "cs_5_0"
        );
        if (!csBlob) return false;

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_updateRootSig;
        psoDesc.CS.pShaderBytecode = csBlob->GetBufferPointer();
        psoDesc.CS.BytecodeLength = csBlob->GetBufferSize();
        hr = m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_updatePSO));
        csBlob->Release();
        if (FAILED(hr)) return false;
    }

    // ------------------------------------------------------------------------
    // 3. Water Rendering Graphics Pipeline (VS + PS)
    // ------------------------------------------------------------------------
    {
        D3D12_DESCRIPTOR_RANGE srvRangeHeight = {};
        srvRangeHeight.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRangeHeight.NumDescriptors = 1;
        srvRangeHeight.BaseShaderRegister = 0; // t0: HeightMap

        D3D12_DESCRIPTOR_RANGE srvRangeDepth = {};
        srvRangeDepth.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRangeDepth.NumDescriptors = 1;
        srvRangeDepth.BaseShaderRegister = 1; // t1: SceneDepth

        D3D12_ROOT_PARAMETER rootParams[3] = {};
        // 0: WaterConstants (56 32-bit values)
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParams[0].Constants.ShaderRegister = 0;
        rootParams[0].Constants.RegisterSpace = 0;
        rootParams[0].Constants.Num32BitValues = 56;

        // 1: t0 (HeightMap)
        rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[1].DescriptorTable.pDescriptorRanges = &srvRangeHeight;

        // 2: t1 (SceneDepth)
        rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[2].DescriptorTable.pDescriptorRanges = &srvRangeDepth;

        // Static Samplers: s0: Linear wrap (for height map), s1: Point clamp (for scene depth)
        D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
        samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplers[0].ShaderRegister = 0;
        samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[1].ShaderRegister = 1;
        samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.NumParameters = 3;
        rsDesc.pParameters = rootParams;
        rsDesc.NumStaticSamplers = 2;
        rsDesc.pStaticSamplers = samplers;
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* sigBlob = nullptr;
        hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob);
        if (FAILED(hr)) return false;
        hr = m_device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&m_waterRenderRootSig));
        sigBlob->Release();
        if (FAILED(hr)) return false;

        const std::string fallbackCommon = R"HLSL(
cbuffer WaterConstants : register(b0) {
    float4x4 viewProj;
    float3   cameraPos;
    float    time;
    float4   waterColor;
    float4   shallowColor;
    float    roughness;
    float    opacity;
    float    reflectionStr;
    float    refractionStr;
    float    waveSpeed;
    float    waveScale;
    float    waveHeight;
    float    normalScale;
    float    foamEnabled;
    float    foamScale;
    float    depthFade;
    float    specularIntensity;
    float    foamDistance;
    float    foamStrength;
    float    foamSpeed;
    float    fresnelStrength;
    float    specularPower;
    float    sparklePower;
    float    nearPlane;
    float    farPlane;
    float2   screenSize;
    float2   flowDirection;
    float    flowSpeed;
    float    simEnabled;
    float    heightScale;
    float    _padCommon;
};
float Hash21(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}
float ValueNoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1.0, 0.0));
    float c = Hash21(i + float2(0.0, 1.0));
    float d = Hash21(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
float WaterFBM(float2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    float2 shift = float2(100.0, 100.0);
    for (int i = 0; i < 4; ++i) {
        value += amplitude * ValueNoise(p);
        p = p * 2.0 + shift;
        amplitude *= 0.5;
    }
    return value;
}
float SinWave(float2 pos, float2 dir, float wavelength, float speed, float amplitude, float t) {
    float k = 6.2831853 / max(wavelength, 0.05);
    float d = dot(normalize(dir + float2(0.0001, 0.0001)), pos);
    return amplitude * sin(k * d + t * speed);
}
float EvaluateWaves(float2 pos, float t, float speed, float scale, float height, float2 flowDir, float flowSpd) {
    float2 p = pos + flowDir * (t * flowSpd);
    float safeScale = max(scale, 0.01);
    float safeHeight = clamp(height, 0.0, 20.0);
    float safeSpeed = clamp(speed, 0.0, 10.0);
    float h0 = SinWave(p, float2(1.0, 0.2),  6.0 * safeScale, 1.2 * safeSpeed, 0.45 * safeHeight, t);
    float h1 = SinWave(p, float2(-0.4, 0.9), 3.5 * safeScale, 1.6 * safeSpeed, 0.35 * safeHeight, t);
    float h2 = SinWave(p, float2(0.7, -0.7), 2.0 * safeScale, 2.2 * safeSpeed, 0.20 * safeHeight, t);
    return h0 + h1 + h2;
}
float LinearizeDepth(float zBuffer, float nearP, float farP) {
    return (nearP * farP) / max(farP - zBuffer * (farP - nearP), 1e-5);
}
)HLSL";

        const std::string fallbackVS = fallbackCommon + R"HLSL(
Texture2D<float> gHeightMap : register(t0);
SamplerState gLinearSampler : register(s0);
struct VSIn { float3 pos : POSITION; float3 norm : NORMAL; float2 uv : TEXCOORD0; };
struct VSOut { float4 clipPos : SV_POSITION; float3 worldPos : TEXCOORD0; float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; float eyeDist : TEXCOORD3; };
VSOut VSMain(VSIn input) {
    float3 p = input.pos;
    float t = time;
    float simH = 0.0;
    if (simEnabled > 0.5) {
        simH = gHeightMap.SampleLevel(gLinearSampler, input.uv, 0) * heightScale;
    }
    float procH = EvaluateWaves(p.xz, t, waveSpeed, waveScale, waveHeight, flowDirection, flowSpeed);
    float totalH = simH + procH;
    p.y += totalH;
    float eps = 0.15;
    float hL = EvaluateWaves(p.xz - float2(eps, 0.0), t, waveSpeed, waveScale, waveHeight, flowDirection, flowSpeed);
    float hR = EvaluateWaves(p.xz + float2(eps, 0.0), t, waveSpeed, waveScale, waveHeight, flowDirection, flowSpeed);
    float hD = EvaluateWaves(p.xz - float2(0.0, eps), t, waveSpeed, waveScale, waveHeight, flowDirection, flowSpeed);
    float hU = EvaluateWaves(p.xz + float2(0.0, eps), t, waveSpeed, waveScale, waveHeight, flowDirection, flowSpeed);
    if (simEnabled > 0.5) {
        float2 uvEps = float2(1.0 / 256.0, 1.0 / 256.0);
        hL += gHeightMap.SampleLevel(gLinearSampler, input.uv - float2(uvEps.x, 0.0), 0) * heightScale;
        hR += gHeightMap.SampleLevel(gLinearSampler, input.uv + float2(uvEps.x, 0.0), 0) * heightScale;
        hD += gHeightMap.SampleLevel(gLinearSampler, input.uv - float2(0.0, uvEps.y), 0) * heightScale;
        hU += gHeightMap.SampleLevel(gLinearSampler, input.uv + float2(0.0, uvEps.y), 0) * heightScale;
    }
    float dx = (hR - hL) / (2.0 * eps);
    float dz = (hU - hD) / (2.0 * eps);
    float3 N = normalize(float3(-dx * normalScale, 1.0, -dz * normalScale));
    VSOut o;
    o.clipPos  = mul(float4(p, 1.0), viewProj);
    o.worldPos = p;
    o.normal   = N;
    o.uv       = input.uv;
    o.eyeDist  = length(cameraPos - p);
    return o;
}
)HLSL";

        const std::string fallbackPS = fallbackCommon + R"HLSL(
Texture2D<float> gSceneDepth : register(t1);
SamplerState gPointSampler   : register(s1);
struct VSOut { float4 clipPos : SV_POSITION; float3 worldPos : TEXCOORD0; float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; float eyeDist : TEXCOORD3; };
float4 PSMain(VSOut input) : SV_TARGET {
    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPos - input.worldPos);
    float3 L = normalize(float3(0.4, 0.85, 0.35));
    float3 H = normalize(V + L);
    float2 screenUV = input.clipPos.xy / max(screenSize, float2(1.0, 1.0));
    float rawSceneDepth = gSceneDepth.SampleLevel(gPointSampler, screenUV, 0);
    float sceneLinearZ = LinearizeDepth(rawSceneDepth, nearPlane, farPlane);
    float waterLinearZ = LinearizeDepth(input.clipPos.z, nearPlane, farPlane);
    float thickness = max(sceneLinearZ - waterLinearZ, 0.0);
    float depthFactor = saturate(thickness / max(depthFade, 0.01));
    float4 baseColor = lerp(shallowColor, waterColor, depthFactor);
    float foam = 0.0;
    if (foamEnabled > 0.5) {
        float foamFactor = 1.0 - saturate(thickness / max(foamDistance, 0.01));
        float foamNoise  = WaterFBM(input.worldPos.xz * foamScale + time * foamSpeed);
        foam = smoothstep(0.3, 0.75, foamFactor * foamNoise) * foamStrength;
        float crestFactor = saturate((1.0 - N.y) * 2.0);
        foam = max(foam, crestFactor * foamNoise * 0.4 * foamStrength);
    }
    float NdotV = saturate(dot(N, V));
    float fresnel = pow(1.0 - NdotV, 5.0) * fresnelStrength;
    fresnel = saturate(fresnel);
    float NdotH = saturate(dot(N, H));
    float spec = pow(NdotH, max(specularPower, 1.0)) * specularIntensity;
    float sparkle = pow(NdotH, max(sparklePower, 10.0)) * specularIntensity * 2.5;
    float3 skyReflectColor = lerp(float3(0.35, 0.50, 0.65), float3(0.80, 0.90, 1.0), fresnel);
    float3 finalColor = lerp(baseColor.rgb, skyReflectColor, fresnel * reflectionStr);
    finalColor += (spec + sparkle);
    finalColor = lerp(finalColor, float3(1.0, 1.0, 1.0), foam);
    finalColor = saturate(finalColor);
    float waterAlpha = saturate(lerp(saturate(thickness / 0.35), 1.0, fresnel) * opacity);
    waterAlpha = max(waterAlpha, foam * 0.9);
    return float4(finalColor, waterAlpha);
}
)HLSL";

        ID3DBlob* vsBlob = CompileOrFallback(
            { "Shaders/Water/WaterVS.hlsl", "../Shaders/Water/WaterVS.hlsl", "Build/Engine/Shaders/Water/WaterVS.hlsl" },
            fallbackVS, "VSMain", "vs_5_0"
        );
        if (!vsBlob) return false;

        ID3DBlob* psBlob = CompileOrFallback(
            { "Shaders/Water/WaterPS.hlsl", "../Shaders/Water/WaterPS.hlsl", "Build/Engine/Shaders/Water/WaterPS.hlsl" },
            fallbackPS, "PSMain", "ps_5_0"
        );
        if (!psBlob) { vsBlob->Release(); return false; }

        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        // Translucent Alpha Blend State (AquaTerrain Section 20)
        D3D12_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable           = TRUE;
        blendDesc.RenderTarget[0].SrcBlend              = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout                     = { inputLayout, 3 };
        psoDesc.pRootSignature                  = m_waterRenderRootSig;
        psoDesc.VS                              = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
        psoDesc.PS                              = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
        psoDesc.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE; // Two-sided water surface
        psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;
        psoDesc.BlendState                      = blendDesc;
        psoDesc.DepthStencilState.DepthEnable   = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask= D3D12_DEPTH_WRITE_MASK_ZERO; // Depth read-only for transparent water!
        psoDesc.DepthStencilState.DepthFunc     = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        psoDesc.SampleMask                      = UINT_MAX;
        psoDesc.PrimitiveTopologyType           = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets                = 1;
        psoDesc.RTVFormats[0]                   = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat                       = DXGI_FORMAT_D32_FLOAT;
        psoDesc.SampleDesc.Count                = 1;

        hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_waterRenderPSO));
        vsBlob->Release();
        psBlob->Release();
        if (FAILED(hr)) return false;
    }

    return true;
}

bool WaterSystem::Simulate(ID3D12GraphicsCommandList* cmdList, Scene& scene) {
    if (!cmdList || !m_initialized || !m_updatePSO || !m_descriptorHeap) return false;

    // Check if any water body has simulation enabled
    bool anySimEnabled = false;
    float damping = 0.985f;
    float waveSpeed = 1.0f;
    for (auto& obj : scene.objects) {
        if ((obj.isWaterBody || obj.type == PrimitiveType::WaterBody) && obj.water.simulationEnabled) {
            anySimEnabled = true;
            damping = obj.water.waveDamping;
            waveSpeed = obj.water.waveSpeed;
            break;
        }
    }
    if (!anySimEnabled && m_pendingImpulses.empty()) return true;

    // Transition barriers for Ping-Pong simulation
    // NextHeight -> UAV, PrevHeight and CurrHeight -> NON_PIXEL_SHADER_RESOURCE
    D3D12_RESOURCE_BARRIER barriers[3] = {};
    int numBarriers = 0;

    if (m_heightStates[m_nextIdx] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        barriers[numBarriers].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[numBarriers].Transition.pResource = m_heightTextures[m_nextIdx];
        barriers[numBarriers].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[numBarriers].Transition.StateBefore = m_heightStates[m_nextIdx];
        barriers[numBarriers].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        m_heightStates[m_nextIdx] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        numBarriers++;
    }
    if (m_heightStates[m_prevIdx] != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        barriers[numBarriers].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[numBarriers].Transition.pResource = m_heightTextures[m_prevIdx];
        barriers[numBarriers].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[numBarriers].Transition.StateBefore = m_heightStates[m_prevIdx];
        barriers[numBarriers].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        m_heightStates[m_prevIdx] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        numBarriers++;
    }
    if (m_heightStates[m_currIdx] != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        barriers[numBarriers].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[numBarriers].Transition.pResource = m_heightTextures[m_currIdx];
        barriers[numBarriers].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[numBarriers].Transition.StateBefore = m_heightStates[m_currIdx];
        barriers[numBarriers].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        m_heightStates[m_currIdx] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        numBarriers++;
    }
    if (numBarriers > 0) {
        cmdList->ResourceBarrier(numBarriers, barriers);
    }

    // Set descriptor heap
    ID3D12DescriptorHeap* heaps[] = { m_descriptorHeap };
    cmdList->SetDescriptorHeaps(1, heaps);

    // 1. Dispatch Wave Equation Update Compute Shader
    cmdList->SetComputeRootSignature(m_updateRootSig);
    cmdList->SetPipelineState(m_updatePSO);

    // Discrete wave equation coefficients (AquaTerrain Section 9)
    float dt = 1.0f / 60.0f;
    float c = std::clamp(waveSpeed, 0.2f, 3.0f);
    float k3 = std::clamp(c * c * dt * dt * 0.45f, 0.01f, 0.24f); // Stable neighbor coupling (< 0.25)
    float d = std::clamp(damping, 0.90f, 0.999f);
    float k1 = -(d * 0.98f);
    float k2 = (1.0f + d * 0.98f) - (4.0f * k3);

    struct {
        float gridSizeX, gridSizeY;
        float k1, k2, k3;
        float pad0, pad1, pad2;
    } updateConstants = {
        (float)SIM_RES, (float)SIM_RES,
        k1, k2, k3,
        0.0f, 0.0f, 0.0f
    };
    cmdList->SetComputeRoot32BitConstants(0, 8, &updateConstants, 0);
    cmdList->SetComputeRootDescriptorTable(1, m_srvGpuHandles[m_prevIdx]);
    cmdList->SetComputeRootDescriptorTable(2, m_srvGpuHandles[m_currIdx]);
    cmdList->SetComputeRootDescriptorTable(3, m_uavGpuHandles[m_nextIdx]);

    cmdList->Dispatch(SIM_RES / 8, SIM_RES / 8, 1);

    // UAV barrier on NextHeight
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_heightTextures[m_nextIdx];
    cmdList->ResourceBarrier(1, &uavBarrier);

    // 2. Dispatch Disturbance Compute Shaders for any pending impulses
    if (!m_pendingImpulses.empty() && m_disturbPSO) {
        cmdList->SetComputeRootSignature(m_disturbRootSig);
        cmdList->SetPipelineState(m_disturbPSO);
        cmdList->SetComputeRootDescriptorTable(1, m_uavGpuHandles[m_nextIdx]);

        for (const auto& imp : m_pendingImpulses) {
            struct {
                float gridSizeX, gridSizeY;
                float centerX, centerY;
                float radius;
                float strength;
                float pad0, pad1;
            } disturbConstants = {
                (float)SIM_RES, (float)SIM_RES,
                imp.u, imp.v,
                imp.radius,
                imp.strength,
                0.0f, 0.0f
            };
            cmdList->SetComputeRoot32BitConstants(0, 8, &disturbConstants, 0);
            cmdList->Dispatch(SIM_RES / 8, SIM_RES / 8, 1);
            cmdList->ResourceBarrier(1, &uavBarrier);
        }
        m_pendingImpulses.clear();
    }

    // 3. Transition NextHeight to SHADER_RESOURCE for vertex shader sampling
    D3D12_RESOURCE_BARRIER toSrv = {};
    toSrv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toSrv.Transition.pResource = m_heightTextures[m_nextIdx];
    toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    toSrv.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    m_heightStates[m_nextIdx] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(1, &toSrv);

    // 4. Ping-Pong rotate: Prev = Curr, Curr = Next, Next = old Prev
    int oldPrev = m_prevIdx;
    m_prevIdx = m_currIdx;
    m_currIdx = m_nextIdx;
    m_nextIdx = oldPrev;

    return true;
}

bool WaterSystem::Render(
    ID3D12GraphicsCommandList* cmdList,
    Scene& scene,
    const glm::mat4& view,
    const glm::mat4& proj,
    const glm::vec3& camPos,
    ID3D12Resource* sceneDepthBuffer,
    D3D12_RESOURCE_STATES currentDepthState,
    float nearPlane,
    float farPlane,
    float viewportWidth,
    float viewportHeight,
    ID3D12Resource* vertexBuffer,
    D3D12_VERTEX_BUFFER_VIEW vbv,
    ID3D12Resource* indexBuffer,
    D3D12_INDEX_BUFFER_VIEW ibv,
    void* pVertexMapped,
    void* pIndexMapped)
{
    if (!cmdList || !m_initialized || !m_waterRenderPSO || !m_descriptorHeap) return false;

    // Check if there are visible water bodies to render
    bool hasVisibleWater = false;
    for (auto& obj : scene.objects) {
        if ((obj.isWaterBody || obj.type == PrimitiveType::WaterBody) && obj.visible && !obj.mesh.vertices.empty()) {
            hasVisibleWater = true;
            break;
        }
    }
    if (!hasVisibleWater) return true;

    // 1. Update Scene Depth SRV in descriptor heap
    if (sceneDepthBuffer) {
        D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
        depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        depthSrvDesc.Texture2D.MostDetailedMip = 0;
        depthSrvDesc.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(sceneDepthBuffer, &depthSrvDesc, m_depthSrvCpuHandle);

        // Transition Scene Depth buffer to DEPTH_READ | PIXEL_SHADER_RESOURCE
        D3D12_RESOURCE_BARRIER depthBarrier = {};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = sceneDepthBuffer;
        depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        depthBarrier.Transition.StateBefore = currentDepthState;
        depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        cmdList->ResourceBarrier(1, &depthBarrier);
    }

    // 2. Bind Pipeline & Descriptors
    ID3D12DescriptorHeap* heaps[] = { m_descriptorHeap };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetGraphicsRootSignature(m_waterRenderRootSig);
    cmdList->SetPipelineState(m_waterRenderPSO);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Bind Height Map SRV (t0) and Scene Depth SRV (t1)
    cmdList->SetGraphicsRootDescriptorTable(1, m_srvGpuHandles[m_currIdx]);
    cmdList->SetGraphicsRootDescriptorTable(2, m_depthSrvGpuHandle);

    glm::mat4 vp = proj * view;

    // 3. Render Each Water Body
    // Reserve dynamic vertex/index memory region offset (in upper half of buffer)
    static UINT s_waterVBOffset = 500000 * sizeof(Vertex);
    static UINT s_waterIBOffset = 1500000 * sizeof(uint32_t);

    for (auto& obj : scene.objects) {
        if (!obj.isWaterBody && obj.type != PrimitiveType::WaterBody) continue;
        if (!obj.visible || obj.mesh.vertices.empty() || obj.mesh.indices.empty()) continue;

        const WaterBodyComponent& w = obj.water;

        // Model matrix
        glm::mat4 T = glm::translate(glm::mat4(1.0f), scene.GetWorldPosition(obj));
        glm::mat4 R = glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotation.y), glm::vec3(0, 1, 0));
        R = glm::rotate(R, glm::radians(obj.rotation.x), glm::vec3(1, 0, 0));
        R = glm::rotate(R, glm::radians(obj.rotation.z), glm::vec3(0, 0, 1));
        glm::mat4 S = glm::scale(glm::mat4(1.0f), obj.scale);
        glm::mat4 model = T * R * S;
        glm::mat4 mvp = vp * model;

        WaterShaderConstantsBuffer cb = {};
        cb.viewProj          = mvp;
        cb.cameraPos         = camPos;
        cb.time              = m_waterTime;
        cb.waterColor        = w.waterColor;
        cb.shallowColor      = w.shallowColor;
        cb.roughness         = w.roughness;
        cb.opacity           = w.opacity;
        cb.reflectionStr     = w.reflectionStrength;
        cb.refractionStr     = w.refractionStrength;
        cb.waveSpeed         = w.waveSpeed;
        cb.waveScale         = std::max(w.waveScale, 0.01f);
        cb.waveHeight        = w.waveHeight;
        cb.normalScale       = w.normalScale;
        cb.foamEnabled       = w.enableFoam ? 1.0f : 0.0f;
        cb.foamScale         = std::max(w.foamScale, 0.01f);
        cb.depthFade         = std::max(w.depthRange, 0.1f);
        cb.specularIntensity = w.specularIntensity;
        cb.foamDistance      = std::max(w.foamDistance, 0.01f);
        cb.foamStrength      = w.foamStrength;
        cb.foamSpeed         = w.foamSpeed;
        cb.fresnelStrength   = w.fresnelStrength;
        cb.specularPower     = std::max(w.specularPower, 1.0f);
        cb.sparklePower      = std::max(w.sparklePower, 1.0f);
        cb.nearPlane         = nearPlane;
        cb.farPlane          = farPlane;
        cb.screenSize        = glm::vec2(viewportWidth, viewportHeight);
        cb.flowDirection     = w.flowDirection;
        cb.flowSpeed         = w.flowSpeed;
        cb.simEnabled        = w.simulationEnabled ? 1.0f : 0.0f;
        cb.heightScale       = w.heightScale;
        cb._padCommon        = 0.0f;

        cmdList->SetGraphicsRoot32BitConstants(0, 56, &cb, 0);

        UINT vCount = (UINT)obj.mesh.vertices.size();
        UINT iCount = (UINT)obj.mesh.indices.size();
        if (vCount > 65536 || iCount > 262144) continue;

        // Copy vertices & indices to mapped memory
        if (pVertexMapped && vertexBuffer) {
            uint8_t* dstV = (uint8_t*)pVertexMapped + s_waterVBOffset;
            memcpy(dstV, obj.mesh.vertices.data(), vCount * sizeof(Vertex));
        }
        if (pIndexMapped && indexBuffer) {
            uint8_t* dstI = (uint8_t*)pIndexMapped + s_waterIBOffset;
            memcpy(dstI, obj.mesh.indices.data(), iCount * sizeof(uint32_t));
        }

        D3D12_VERTEX_BUFFER_VIEW objVbv = vbv;
        objVbv.BufferLocation = vertexBuffer->GetGPUVirtualAddress() + s_waterVBOffset;
        objVbv.SizeInBytes = vCount * sizeof(Vertex);

        D3D12_INDEX_BUFFER_VIEW objIbv = ibv;
        objIbv.BufferLocation = indexBuffer->GetGPUVirtualAddress() + s_waterIBOffset;
        objIbv.SizeInBytes = iCount * sizeof(uint32_t);
        objIbv.Format = DXGI_FORMAT_R32_UINT;

        cmdList->IASetVertexBuffers(0, 1, &objVbv);
        cmdList->IASetIndexBuffer(&objIbv);
        cmdList->DrawIndexedInstanced(iCount, 1, 0, 0, 0);
    }

    // 4. Transition Scene Depth buffer back to its original state
    if (sceneDepthBuffer) {
        D3D12_RESOURCE_BARRIER depthBarrier = {};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = sceneDepthBuffer;
        depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        depthBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        depthBarrier.Transition.StateAfter = currentDepthState;
        cmdList->ResourceBarrier(1, &depthBarrier);
    }

    return true;
}

} // namespace Eunoia
