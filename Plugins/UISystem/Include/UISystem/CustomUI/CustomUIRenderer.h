#pragma once
// ============================================================================
// Eunoia Engine — Custom Native UI Renderer (DirectX 12)
// Direct hardware-accelerated rendering pass for UI elements.
// High-performance batched dynamic vertex/index buffers, anti-aliased SDF quads,
// rounded rectangles, drop shadows, and text glyph rasterization.
// ============================================================================

#include "CustomUITypes.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <stack>

namespace Eunoia { namespace UI {

using Microsoft::WRL::ComPtr;

class CustomUIRenderer {
public:
    static CustomUIRenderer& Get() {
        static CustomUIRenderer instance;
        return instance;
    }

    bool Initialize(ID3D12Device* device, DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

    // Frame lifecycle
    void BeginFrame(uint32_t screenWidth, uint32_t screenHeight);
    void EndFrame();
    void Render(ID3D12GraphicsCommandList* cmdList);

    // 2D Draw Primitives
    void DrawSolidRect(const glm::vec4& rect, const glm::vec4& color);
    void DrawRoundedRect(const glm::vec4& rect, const glm::vec4& color, float cornerRadius = 6.0f, float borderThickness = 0.0f, const glm::vec4& borderColor = glm::vec4(0.0f));
    void DrawShadow(const glm::vec4& rect, const glm::vec4& shadowColor, float cornerRadius = 6.0f, float spread = 8.0f, const glm::vec2& offset = glm::vec2(0.0f, 3.0f));
    void DrawTextString(const std::string& text, const glm::vec2& pos, float fontSize, const glm::vec4& color);
    void DrawTextCentered(const std::string& text, const glm::vec4& rect, float fontSize, const glm::vec4& color);

    // Measurement
    glm::vec2 MeasureText(const std::string& text, float fontSize);

    // Clipping stack
    void PushClipRect(const glm::vec4& clipRect);
    void PopClipRect();

private:
    CustomUIRenderer() = default;
    ~CustomUIRenderer() = default;

    void CreateDeviceResources(ID3D12Device* device, DXGI_FORMAT rtvFormat);
    void BuildDefaultFontAtlas(ID3D12Device* device);
    void AddQuad(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3,
                 const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, const glm::vec2& uv3,
                 const glm::vec4& color, const glm::vec4& params, const glm::vec4& rect);

    bool m_initialized = false;
    ID3D12Device* m_device = nullptr;
    DXGI_FORMAT m_rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    uint32_t m_screenWidth = 1920;
    uint32_t m_screenHeight = 1080;

    // D3D12 Pipeline Objects
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12Resource>      m_constantBuffer;
    ComPtr<ID3D12Resource>      m_fontTexture;
    ComPtr<ID3D12DescriptorHeap>m_srvHeap;

    // Dynamic Geometry Buffers
    static const uint32_t MAX_VERTICES = 131072;
    static const uint32_t MAX_INDICES  = 262144;
    ComPtr<ID3D12Resource>      m_vertexBuffer;
    ComPtr<ID3D12Resource>      m_indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW    m_vertexBufferView{};
    D3D12_INDEX_BUFFER_VIEW     m_indexBufferView{};
    CustomUIVertex*             m_mappedVertices = nullptr;
    uint32_t*                   m_mappedIndices = nullptr;

    std::vector<CustomUIVertex> m_vertexData;
    std::vector<uint32_t>       m_indexData;

    struct DrawBatch {
        uint32_t indexCount = 0;
        uint32_t startIndexLocation = 0;
        int32_t  baseVertexLocation = 0;
        D3D12_RECT clipRect{};
    };
    std::vector<DrawBatch> m_batches;
    std::stack<glm::vec4>  m_clipStack;
};

}} // namespace Eunoia::UI
