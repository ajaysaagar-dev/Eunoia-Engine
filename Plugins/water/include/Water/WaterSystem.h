#pragma once
// ============================================================================
// Eunoia Engine — AquaTerrain DX12 Water System
// WaterSystem: Complete GPU height-field simulation and transparent water rendering
// ============================================================================

#include <d3d12.h>
#include <dxgi1_6.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <EngineScene/Scene.h>

namespace Eunoia {

class WaterSystem {
public:
    static WaterSystem& Get();

    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, UINT width, UINT height);
    void Shutdown();
    void Resize(UINT width, UINT height);

    void Update(float deltaTime);

    // Add water disturbance impulse in world space (converts to water body UV space)
    void AddImpulse(const glm::vec3& worldPos, float radius, float strength);
    void AddImpulseUV(float u, float v, float radiusUV, float strength);

    // Execute GPU height-field wave simulation pass (AquaTerrain CS)
    bool Simulate(ID3D12GraphicsCommandList* cmdList, Scene& scene);

    // Execute transparent water rendering pass
    bool Render(
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
        void* pIndexMapped
    );

    bool IsInitialized() const { return m_initialized; }

private:
    WaterSystem();
    ~WaterSystem();

    bool CreatePipelines();
    bool CreateResources();
    void ReleaseResources();

    ID3D12Device*             m_device = nullptr;
    ID3D12CommandQueue*       m_commandQueue = nullptr;
    UINT                      m_width = 1280;
    UINT                      m_height = 720;
    bool                      m_initialized = false;

    // Simulation parameters
    static constexpr UINT     SIM_RES = 256;
    float                     m_waterTime = 0.0f;
    float                     m_simAccumulator = 0.0f;

    // 3 Ping-Pong height textures (Prev, Curr, Next)
    ID3D12Resource*           m_heightTextures[3] = {};
    D3D12_RESOURCE_STATES     m_heightStates[3] = {};
    int                       m_prevIdx = 0;
    int                       m_currIdx = 1;
    int                       m_nextIdx = 2;

    // Descriptor heap for simulation & rendering (UAVs, SRVs)
    ID3D12DescriptorHeap*     m_descriptorHeap = nullptr;
    UINT                      m_descriptorSize = 0;

    D3D12_GPU_DESCRIPTOR_HANDLE m_uavGpuHandles[3] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_uavCpuHandles[3] = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_srvGpuHandles[3] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_srvCpuHandles[3] = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_depthSrvGpuHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_depthSrvCpuHandle = {};

    // Compute Pipelines
    ID3D12RootSignature*      m_disturbRootSig = nullptr;
    ID3D12PipelineState*      m_disturbPSO = nullptr;

    ID3D12RootSignature*      m_updateRootSig = nullptr;
    ID3D12PipelineState*      m_updatePSO = nullptr;

    // Graphics Pipeline
    ID3D12RootSignature*      m_waterRenderRootSig = nullptr;
    ID3D12PipelineState*      m_waterRenderPSO = nullptr;

    struct ImpulseData {
        float u, v;
        float radius;
        float strength;
    };
    std::vector<ImpulseData>  m_pendingImpulses;
};

} // namespace Eunoia
