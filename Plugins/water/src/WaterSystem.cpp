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
    float     choppiness;        // 55
    glm::vec3 renderOrigin;      // 56-58
    float     debugMode;         // 59
};
static_assert(sizeof(WaterShaderConstantsBuffer) == 240, "WaterShaderConstantsBuffer must be exactly 240 bytes (60 floats)");

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

    // 3. Create dedicated camera-following Ocean Surface Grid Mesh (256x256 quads, dev.md Section 8)
    {
        std::vector<MeshVertex> oceanVertices;
        std::vector<uint32_t> oceanIndices;
        oceanVertices.reserve(257 * 257);
        oceanIndices.reserve(256 * 256 * 6);

        for (UINT z = 0; z <= 256; ++z) {
            float fz = (float)z / 256.0f - 0.5f;
            for (UINT x = 0; x <= 256; ++x) {
                float fx = (float)x / 256.0f - 0.5f;
                MeshVertex vert = {};
                vert.pos = glm::vec3(fx, 0.0f, fz);
                vert.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                vert.uv = glm::vec2((float)x / 256.0f, (float)z / 256.0f);
                vert.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                oceanVertices.push_back(vert);
            }
        }

        for (UINT z = 0; z < 256; ++z) {
            for (UINT x = 0; x < 256; ++x) {
                uint32_t topLeft = z * 257 + x;
                uint32_t topRight = topLeft + 1;
                uint32_t bottomLeft = (z + 1) * 257 + x;
                uint32_t bottomRight = bottomLeft + 1;

                oceanIndices.push_back(topLeft);
                oceanIndices.push_back(bottomLeft);
                oceanIndices.push_back(topRight);

                oceanIndices.push_back(topRight);
                oceanIndices.push_back(bottomLeft);
                oceanIndices.push_back(bottomRight);
            }
        }

        m_oceanIndexCount = (UINT)oceanIndices.size();

        UINT vbSize = (UINT)(oceanVertices.size() * sizeof(MeshVertex));
        UINT ibSize = (UINT)(oceanIndices.size() * sizeof(uint32_t));

        D3D12_HEAP_PROPERTIES uploadHeap = {};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC vbDesc = {};
        vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        vbDesc.Width = vbSize;
        vbDesc.Height = 1;
        vbDesc.DepthOrArraySize = 1;
        vbDesc.MipLevels = 1;
        vbDesc.SampleDesc.Count = 1;
        vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hrVB = m_device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &vbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_oceanVB)
        );
        if (SUCCEEDED(hrVB)) {
            void* pData = nullptr;
            m_oceanVB->Map(0, nullptr, &pData);
            memcpy(pData, oceanVertices.data(), vbSize);
            m_oceanVB->Unmap(0, nullptr);

            m_oceanVbv.BufferLocation = m_oceanVB->GetGPUVirtualAddress();
            m_oceanVbv.StrideInBytes = sizeof(MeshVertex);
            m_oceanVbv.SizeInBytes = vbSize;
        }

        D3D12_RESOURCE_DESC ibDesc = vbDesc;
        ibDesc.Width = ibSize;

        HRESULT hrIB = m_device->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &ibDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_oceanIB)
        );
        if (SUCCEEDED(hrIB)) {
            void* pData = nullptr;
            m_oceanIB->Map(0, nullptr, &pData);
            memcpy(pData, oceanIndices.data(), ibSize);
            m_oceanIB->Unmap(0, nullptr);

            m_oceanIbv.BufferLocation = m_oceanIB->GetGPUVirtualAddress();
            m_oceanIbv.SizeInBytes = ibSize;
            m_oceanIbv.Format = DXGI_FORMAT_R32_UINT;
        }
    }

    m_needsInitialClear = true;
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
    if (m_oceanVB) { m_oceanVB->Release(); m_oceanVB = nullptr; }
    if (m_oceanIB) { m_oceanIB->Release(); m_oceanIB = nullptr; }
    if (m_clearRootSig) { m_clearRootSig->Release(); m_clearRootSig = nullptr; }
    if (m_clearPSO) { m_clearPSO->Release(); m_clearPSO = nullptr; }
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
    // 0. Clear Compute Root Signature & PSO (Clears Ping-Pong Textures to 0.0f)
    // ------------------------------------------------------------------------
    {
        D3D12_DESCRIPTOR_RANGE uavRange = {};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 0; // u0

        D3D12_ROOT_PARAMETER rootParam = {};
        rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParam.DescriptorTable.NumDescriptorRanges = 1;
        rootParam.DescriptorTable.pDescriptorRanges = &uavRange;

        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.NumParameters = 1;
        rsDesc.pParameters = &rootParam;
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ID3DBlob* sigBlob = nullptr;
        hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob);
        if (FAILED(hr)) return false;
        hr = m_device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&m_clearRootSig));
        sigBlob->Release();
        if (FAILED(hr)) return false;

        const std::string clearHLSL = R"HLSL(
RWTexture2D<float> gTarget : register(u0);
[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= 256 || id.y >= 256) return;
    gTarget[id.xy] = 0.0f;
}
)HLSL";
        ID3DBlob* csBlob = nullptr;
        hr = D3DCompile(clearHLSL.c_str(), clearHLSL.length(), nullptr, nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &csBlob, nullptr);
        if (FAILED(hr) || !csBlob) return false;

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_clearRootSig;
        psoDesc.CS.pShaderBytecode = csBlob->GetBufferPointer();
        psoDesc.CS.BytecodeLength = csBlob->GetBufferSize();
        hr = m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_clearPSO));
        csBlob->Release();
        if (FAILED(hr)) return false;
    }

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
    gWaterHeight[id.xy] = clamp(gWaterHeight[id.xy] + falloff * Strength, -50.0, 50.0);
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
    if (isnan(next) || isinf(next)) next = 0.0;
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
        // 0: WaterConstants (60 32-bit values)
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParams[0].Constants.ShaderRegister = 0;
        rootParams[0].Constants.RegisterSpace = 0;
        rootParams[0].Constants.Num32BitValues = 60;

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
    float    choppiness;
    float3   renderOrigin;
    float    debugMode;
};
#define oceanExtent (shallowColor.a)

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
struct GerstnerWave {
    float2 direction;
    float  wavelength;
    float  amplitude;
    float  speed;
    float  steepness;
};
static const GerstnerWave c_oceanWaves[8] = {
    { float2(0.894, 0.447),  50.0, 0.30, 1.00, 0.60 },
    { float2(-0.707, 0.707), 35.0, 0.22, 1.15, 0.65 },
    { float2(0.316, 0.949),  20.0, 0.14, 1.35, 0.70 },
    { float2(-0.923, -0.385), 12.0, 0.09, 1.65, 0.75 },
    { float2(0.600, -0.800),  7.5, 0.05, 2.00, 0.80 },
    { float2(-0.447, 0.894),  4.5, 0.030, 2.50, 0.85 },
    { float2(0.707, 0.707),   2.4, 0.015, 3.20, 0.90 },
    { float2(-0.800, -0.600), 1.2, 0.008, 4.00, 0.95 }
};
void EvaluateGerstnerWaves(float2 worldXZ, float t, float speedMultiplier, float scaleMultiplier, float heightMultiplier, float choppinessMultiplier, out float3 displacement, out float3 normal) {
    displacement = float3(0.0, 0.0, 0.0);
    float normX = 0.0; float normZ = 0.0; float normY = 1.0;
    float sMul = clamp(scaleMultiplier, 0.02, 5.0);
    float hMul = clamp(heightMultiplier, 0.0, 5.0);
    float spMul = clamp(speedMultiplier, 0.0, 10.0);
    float chMul = clamp(choppinessMultiplier, 0.0, 2.0);
    for (int i = 0; i < 8; ++i) {
        GerstnerWave w = c_oceanWaves[i];
        float wl = max(w.wavelength * sMul, 0.1);
        float amp = w.amplitude * hMul;
        float spd = w.speed * spMul;
        float q = clamp(w.steepness * chMul, 0.0, 1.0);
        float k = 6.2831853 / wl;
        float omega = k * spd;
        float phase = k * dot(w.direction, worldXZ) - omega * t;
        float cosP = cos(phase);
        float sinP = sin(phase);
        float qAmp = q * amp;
        displacement.x += qAmp * w.direction.x * cosP;
        displacement.z += qAmp * w.direction.y * cosP;
        displacement.y += amp * sinP;
        float wa = k * amp;
        normX -= w.direction.x * wa * cosP;
        normZ -= w.direction.y * wa * cosP;
        normY -= q * wa * sinP;
    }
    normal = normalize(float3(normX, max(normY, 0.1), normZ));
}
float3 EvaluateDetailNormal(float2 worldXZ, float t, float3 baseNormal, float strength) {
    static const float2 dirs[4] = { float2(0.707, 0.707), float2(-0.894, 0.447), float2(0.316, -0.949), float2(-0.600, -0.800) };
    static const float freqs[4] = { 2.5, 4.2, 7.8, 12.0 };
    static const float speeds[4] = { 1.8, 2.4, 3.1, 4.0 };
    static const float amps[4] = { 0.04, 0.025, 0.015, 0.008 };
    float dx = 0.0; float dz = 0.0;
    for (int i = 0; i < 4; ++i) {
        float phase = dot(dirs[i], worldXZ) * freqs[i] - t * speeds[i];
        float c = cos(phase) * amps[i] * freqs[i];
        dx += dirs[i].x * c;
        dz += dirs[i].y * c;
    }
    float s = clamp(strength, 0.0, 2.0);
    return normalize(baseNormal + float3(-dx * s, 1.0, -dz * s) * 0.35);
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
    float3 baseWorldP;
    if (oceanExtent > 10.0) {
        baseWorldP = float3(input.pos.x * oceanExtent + renderOrigin.x, renderOrigin.y, input.pos.z * oceanExtent + renderOrigin.z);
    } else {
        baseWorldP = input.pos + renderOrigin;
    }
    float3 waveDisp; float3 waveNorm;
    EvaluateGerstnerWaves(baseWorldP.xz, time, waveSpeed, waveScale, waveHeight, choppiness, waveDisp, waveNorm);
    if (oceanExtent > 10.0) {
        float distFromCam = length(baseWorldP.xz - cameraPos.xz);
        float taperStart = oceanExtent * 0.40;
        float taperEnd = oceanExtent * 0.50;
        float taper = saturate((taperEnd - distFromCam) / max(taperEnd - taperStart, 1.0));
        waveDisp *= taper;
    }
    float simH = 0.0; float simDx = 0.0; float simDz = 0.0;
    if (simEnabled > 0.5) {
        simH = gHeightMap.SampleLevel(gLinearSampler, input.uv, 0) * heightScale;
        if (isnan(simH) || isinf(simH)) simH = 0.0;
        float2 uvEps = float2(1.0 / 256.0, 1.0 / 256.0);
        float hL = gHeightMap.SampleLevel(gLinearSampler, input.uv - float2(uvEps.x, 0.0), 0) * heightScale;
        float hR = gHeightMap.SampleLevel(gLinearSampler, input.uv + float2(uvEps.x, 0.0), 0) * heightScale;
        float hD = gHeightMap.SampleLevel(gLinearSampler, input.uv - float2(0.0, uvEps.y), 0) * heightScale;
        float hU = gHeightMap.SampleLevel(gLinearSampler, input.uv + float2(0.0, uvEps.y), 0) * heightScale;
        simDx = (hR - hL) * 128.0;
        simDz = (hU - hD) * 128.0;
    }
    float3 finalWorldP = baseWorldP + waveDisp + float3(0.0, simH, 0.0);
    float3 finalNormal = normalize(waveNorm + float3(-simDx, 0.0, -simDz));
    VSOut o;
    o.clipPos  = mul(viewProj, float4(finalWorldP, 1.0f));
    o.worldPos = finalWorldP;
    o.normal   = finalNormal;
    o.uv       = input.uv;
    o.eyeDist  = length(cameraPos - finalWorldP);
    return o;
}
)HLSL";

        const std::string fallbackPS = fallbackCommon + R"HLSL(
Texture2D<float> gSceneDepth : register(t1);
SamplerState gPointSampler   : register(s1);
struct VSOut { float4 clipPos : SV_POSITION; float3 worldPos : TEXCOORD0; float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; float eyeDist : TEXCOORD3; };
float4 PSMain(VSOut input) : SV_TARGET {
    float3 N = EvaluateDetailNormal(input.worldPos.xz, time, normalize(input.normal), normalScale);
    float3 V = normalize(cameraPos - input.worldPos);
    float3 L = normalize(float3(0.4, 0.85, 0.35));
    float3 H = normalize(V + L);
    float2 screenUV = input.clipPos.xy / max(screenSize, float2(1.0, 1.0));
    float rawSceneDepth = gSceneDepth.SampleLevel(gPointSampler, screenUV, 0);
    float sceneLinearZ = LinearizeDepth(rawSceneDepth, nearPlane, farPlane);
    float waterLinearZ = LinearizeDepth(input.clipPos.z, nearPlane, farPlane);
    bool isSkyBackground = (rawSceneDepth >= 0.9999);
    float thickness = isSkyBackground ? 500.0 : max(sceneLinearZ - waterLinearZ, 0.0);
    float depthFactor = saturate(thickness / max(depthFade, 0.1));
    float4 baseColor = lerp(float4(shallowColor.rgb, 1.0), float4(waterColor.rgb, 1.0), depthFactor);
    float foam = 0.0;
    if (foamEnabled > 0.5) {
        if (!isSkyBackground) {
            float foamFactor = 1.0 - saturate(thickness / max(foamDistance, 0.01));
            float foamNoise  = WaterFBM(input.worldPos.xz * foamScale + time * foamSpeed);
            foam = smoothstep(0.4, 0.8, foamFactor * foamNoise) * foamStrength;
        }
        float wavePeak = (input.worldPos.y - renderOrigin.y);
        if (wavePeak > waveHeight * 0.4 && waveHeight > 0.05) {
            float crestFactor = saturate((wavePeak - waveHeight * 0.4) / max(waveHeight * 0.5, 0.01));
            float crestNoise  = WaterFBM(input.worldPos.xz * (foamScale * 1.5) + time * (foamSpeed * 0.8));
            foam = max(foam, smoothstep(0.5, 0.85, crestFactor * crestNoise) * 0.35 * foamStrength);
        }
    }
    float NdotV = saturate(dot(N, V));
    float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);
    fresnel = saturate(fresnel * fresnelStrength);
    float NdotH = saturate(dot(N, H));
    float spec = pow(NdotH, max(specularPower, 16.0)) * specularIntensity * 0.75;
    float sparkle = pow(NdotH, max(sparklePower, 64.0)) * specularIntensity * 0.35;
    float3 R = reflect(-V, N);
    float3 skyZenith = float3(0.08, 0.22, 0.42);
    float3 skyHorizon = float3(0.35, 0.52, 0.68);
    float3 skyReflectColor = lerp(skyHorizon, skyZenith, saturate(R.y));
    float3 finalColor = lerp(baseColor.rgb, skyReflectColor, fresnel * reflectionStr);
    finalColor += (spec + sparkle) * float3(1.0, 0.98, 0.92);
    finalColor = lerp(finalColor, float3(0.92, 0.96, 0.98), foam);
    finalColor = saturate(finalColor);
    float waterAlpha = isSkyBackground ? opacity : saturate(lerp(saturate(thickness / 0.5), 1.0, fresnel) * opacity);
    waterAlpha = max(waterAlpha, foam * 0.9);
    int dbg = (int)(debugMode + 0.5);
    if (dbg == 1) return float4(baseColor.rgb, 1.0);
    else if (dbg == 2) return float4(N * 0.5 + 0.5, 1.0);
    else if (dbg == 3) {
        float hNorm = saturate((input.worldPos.y - renderOrigin.y + 0.5) / 1.0);
        return float4(lerp(float3(0.0, 0.1, 0.8), float3(1.0, 0.2, 0.0), hNorm), 1.0);
    }
    else if (dbg == 4) return float4(foam, foam, foam, 1.0);
    else if (dbg == 5) { float dVis = saturate(thickness / 15.0); return float4(dVis, dVis, dVis, 1.0); }
    else if (dbg == 6) {
        float2 grid = frac(input.uv * 256.0);
        float edge = step(grid.x, 0.08) + step(grid.y, 0.08);
        return float4(lerp(float3(0.02, 0.08, 0.15), float3(0.0, 1.0, 0.7), saturate(edge)), 1.0);
    }
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

    // Clear all height textures on startup or reset to guarantee zeroed memory (dev.md Section 8)
    if (m_needsInitialClear && m_clearRootSig && m_clearPSO) {
        m_needsInitialClear = false;
        ID3D12DescriptorHeap* heaps[] = { m_descriptorHeap };
        cmdList->SetDescriptorHeaps(1, heaps);
        cmdList->SetComputeRootSignature(m_clearRootSig);
        cmdList->SetPipelineState(m_clearPSO);

        for (int i = 0; i < 3; ++i) {
            D3D12_RESOURCE_BARRIER b = {};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = m_heightTextures[i];
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = m_heightStates[i];
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            cmdList->ResourceBarrier(1, &b);
            m_heightStates[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

            cmdList->SetComputeRootDescriptorTable(0, m_uavGpuHandles[i]);
            cmdList->Dispatch(SIM_RES / 8, SIM_RES / 8, 1);

            D3D12_RESOURCE_BARRIER uavB = {};
            uavB.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uavB.UAV.pResource = m_heightTextures[i];
            cmdList->ResourceBarrier(1, &uavB);

            b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            cmdList->ResourceBarrier(1, &b);
            m_heightStates[i] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }
    }

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
        if ((obj.isWaterBody || obj.type == PrimitiveType::WaterBody) && obj.visible) {
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
    UINT currentVBOffset = 500000 * sizeof(Vertex);
    UINT currentIBOffset = 1500000 * sizeof(uint32_t);

    for (auto& obj : scene.objects) {
        if (!obj.isWaterBody && obj.type != PrimitiveType::WaterBody) continue;
        if (!obj.visible) continue;

        const WaterBodyComponent& w = obj.water;

        WaterShaderConstantsBuffer cb = {};
        cb.viewProj          = vp; // Camera proj * view
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

        bool isOceanGrid = (w.isCameraCentered || w.type == WaterBodyType::Ocean || w.type == WaterBodyType::Sea);
        glm::vec3 renderOrigin;
        if (isOceanGrid) {
            float extent = (w.oceanExtent > 10.0f) ? w.oceanExtent : 2000.0f;
            float gridSpacing = extent / 256.0f;
            // Snap camera X/Z to grid increments to avoid crawling/jitter (dev.md Section 9)
            renderOrigin.x = std::floor(camPos.x / gridSpacing) * gridSpacing;
            renderOrigin.y = w.elevation; // Strictly fixed at water elevation, NEVER follows camera Y (dev.md Section 10)
            renderOrigin.z = std::floor(camPos.z / gridSpacing) * gridSpacing;
            cb.shallowColor.a = extent; // Pass oceanExtent via shallowColor.a
        } else {
            renderOrigin = scene.GetWorldPosition(obj);
            renderOrigin.y = w.elevation;
            cb.shallowColor.a = 0.0f;
        }

        cb.choppiness        = w.choppiness;
        cb.renderOrigin      = renderOrigin;
        cb.debugMode         = (float)w.debugMode;

        cmdList->SetGraphicsRoot32BitConstants(0, 60, &cb, 0);

        if (isOceanGrid && m_oceanVB && m_oceanIB) {
            cmdList->IASetVertexBuffers(0, 1, &m_oceanVbv);
            cmdList->IASetIndexBuffer(&m_oceanIbv);
            cmdList->DrawIndexedInstanced(m_oceanIndexCount, 1, 0, 0, 0);
        } else {
            UINT vCount = (UINT)obj.mesh.vertices.size();
            UINT iCount = (UINT)obj.mesh.indices.size();
            if (vCount == 0 || iCount == 0 || vCount > 65536 || iCount > 262144) continue;

            if (pVertexMapped && vertexBuffer) {
                uint8_t* dstV = (uint8_t*)pVertexMapped + currentVBOffset;
                memcpy(dstV, obj.mesh.vertices.data(), vCount * sizeof(MeshVertex));
            }
            if (pIndexMapped && indexBuffer) {
                uint8_t* dstI = (uint8_t*)pIndexMapped + currentIBOffset;
                memcpy(dstI, obj.mesh.indices.data(), iCount * sizeof(uint32_t));
            }

            D3D12_VERTEX_BUFFER_VIEW objVbv = {};
            objVbv.BufferLocation = vertexBuffer->GetGPUVirtualAddress() + currentVBOffset;
            objVbv.StrideInBytes = sizeof(MeshVertex);
            objVbv.SizeInBytes = vCount * sizeof(MeshVertex);

            D3D12_INDEX_BUFFER_VIEW objIbv = {};
            objIbv.BufferLocation = indexBuffer->GetGPUVirtualAddress() + currentIBOffset;
            objIbv.SizeInBytes = iCount * sizeof(uint32_t);
            objIbv.Format = DXGI_FORMAT_R32_UINT;

            cmdList->IASetVertexBuffers(0, 1, &objVbv);
            cmdList->IASetIndexBuffer(&objIbv);
            cmdList->DrawIndexedInstanced(iCount, 1, 0, 0, 0);

            currentVBOffset += vCount * sizeof(MeshVertex);
            currentIBOffset += iCount * sizeof(uint32_t);
        }
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
