// ============================================================================
// Eunoia Engine — Custom Native UI Renderer (DirectX 12)
// ============================================================================

#include "UISystem/CustomUI/CustomUIRenderer.h"
#include <d3dcompiler.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>

namespace Eunoia { namespace UI {

bool CustomUIRenderer::Initialize(ID3D12Device* device, DXGI_FORMAT rtvFormat) {
    if (!device) return false;
    m_device = device;
    m_rtvFormat = rtvFormat;

    CreateDeviceResources(device, rtvFormat);
    BuildDefaultFontAtlas(device);

    m_initialized = true;
    std::cout << "[CustomUI] DirectX 12 native UI renderer initialized successfully.\n";
    return true;
}

void CustomUIRenderer::Shutdown() {
    if (m_vertexBuffer && m_mappedVertices) {
        m_vertexBuffer->Unmap(0, nullptr);
        m_mappedVertices = nullptr;
    }
    if (m_indexBuffer && m_mappedIndices) {
        m_indexBuffer->Unmap(0, nullptr);
        m_mappedIndices = nullptr;
    }

    m_rootSignature.Reset();
    m_pipelineState.Reset();
    m_vertexBuffer.Reset();
    m_indexBuffer.Reset();
    m_fontTexture.Reset();
    m_srvHeap.Reset();
    m_initialized = false;
}

void CustomUIRenderer::CreateDeviceResources(ID3D12Device* device, DXGI_FORMAT rtvFormat) {
    // 1. Root Signature
    // Param 0: 32-bit constants (b0) for screen resolution (float2)
    // Param 1: Descriptor table (t0) for UI / Font texture
    D3D12_ROOT_PARAMETER rootParams[2] = {};

    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].Constants.Num32BitValues = 4; // float2 resolution + float2 pad
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE descRange = {};
    descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descRange.NumDescriptors = 1;
    descRange.BaseShaderRegister = 0;
    descRange.RegisterSpace = 0;
    descRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &descRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static Sampler (s0)
    D3D12_STATIC_SAMPLER_DESC staticSampler = {};
    staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    staticSampler.ShaderRegister = 0;
    staticSampler.RegisterSpace = 0;
    staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 2;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &staticSampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
    device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));

    // 2. Compile or load CustomUI.hlsl shaders
    std::string hlslPath = "Shaders/CustomUI.hlsl";
    std::ifstream hlslFile(hlslPath);
    std::string hlslSource;
    if (hlslFile.is_open()) {
        std::stringstream ss;
        ss << hlslFile.rdbuf();
        hlslSource = ss.str();
    } else {
        std::ifstream fbFile("Build/Engine/Shaders/CustomUI.hlsl");
        if (fbFile.is_open()) {
            std::stringstream ss;
            ss << fbFile.rdbuf();
            hlslSource = ss.str();
        }
    }

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!hlslSource.empty()) {
        D3DCompile(hlslSource.c_str(), hlslSource.length(), nullptr, nullptr, nullptr, "VS_CustomUI", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
        D3DCompile(hlslSource.c_str(), hlslSource.length(), nullptr, nullptr, nullptr, "PS_CustomUI", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    }

    // 3. Input Element Layout
    D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0,  8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "PARAM",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "RECT",     0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // 4. Pipeline State
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    if (vsBlob) psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    if (psBlob) psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.InputLayout = { inputElements, _countof(inputElements) };

    // Standard Alpha Blending
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = rtvFormat;
    psoDesc.SampleDesc.Count = 1;

    device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState));

    // 5. Dynamic Upload Buffers for Geometry
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = sizeof(CustomUIVertex) * MAX_VERTICES;
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexBuffer));

    D3D12_RESOURCE_DESC ibDesc = vbDesc;
    ibDesc.Width = sizeof(uint32_t) * MAX_INDICES;
    device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_indexBuffer));

    D3D12_RANGE readRange = { 0, 0 };
    m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_mappedVertices));
    m_indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_mappedIndices));

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(CustomUIVertex);
    m_vertexBufferView.SizeInBytes = (UINT)vbDesc.Width;

    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    m_indexBufferView.SizeInBytes = (UINT)ibDesc.Width;

    // 6. SRV Descriptor Heap for UI textures
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.NumDescriptors = 16;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap));
}

void CustomUIRenderer::BuildDefaultFontAtlas(ID3D12Device* device) {
    // Generate a default 256x256 monochrome font atlas for crisp text
    const uint32_t atlasW = 256;
    const uint32_t atlasH = 256;
    std::vector<uint32_t> pixels(atlasW * atlasH, 0xFFFFFFFF);

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = atlasW;
    texDesc.Height = atlasH;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;

    device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_fontTexture));

    // Upload texture via upload heap
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadSize);

    D3D12_RESOURCE_DESC uploadBufDesc = {};
    uploadBufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadBufDesc.Width = uploadSize;
    uploadBufDesc.Height = 1;
    uploadBufDesc.DepthOrArraySize = 1;
    uploadBufDesc.MipLevels = 1;
    uploadBufDesc.SampleDesc.Count = 1;
    uploadBufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadBuffer;
    device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadBufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));

    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    uploadBuffer->Map(0, &readRange, &mappedData);
    memcpy(mappedData, pixels.data(), pixels.size() * sizeof(uint32_t));
    uploadBuffer->Unmap(0, nullptr);

    // Create Shader Resource View in slot 0
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateShaderResourceView(m_fontTexture.Get(), &srvDesc, cpuHandle);
}

void CustomUIRenderer::BeginFrame(uint32_t screenWidth, uint32_t screenHeight) {
    m_screenWidth = std::max(1u, screenWidth);
    m_screenHeight = std::max(1u, screenHeight);

    m_vertexData.clear();
    m_indexData.clear();
    m_batches.clear();

    while (!m_clipStack.empty()) m_clipStack.pop();
    m_clipStack.push(glm::vec4(0.0f, 0.0f, (float)m_screenWidth, (float)m_screenHeight));
}

void CustomUIRenderer::EndFrame() {
    if (m_vertexData.empty() || m_indexData.empty()) return;

    uint32_t vCount = std::min((uint32_t)m_vertexData.size(), MAX_VERTICES);
    uint32_t iCount = std::min((uint32_t)m_indexData.size(), MAX_INDICES);

    if (m_mappedVertices) memcpy(m_mappedVertices, m_vertexData.data(), vCount * sizeof(CustomUIVertex));
    if (m_mappedIndices)  memcpy(m_mappedIndices,  m_indexData.data(),  iCount * sizeof(uint32_t));
}

void CustomUIRenderer::AddQuad(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3,
                              const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, const glm::vec2& uv3,
                              const glm::vec4& color, const glm::vec4& params, const glm::vec4& rect) {
    if (m_vertexData.size() + 4 >= MAX_VERTICES || m_indexData.size() + 6 >= MAX_INDICES) return;

    uint32_t baseIdx = (uint32_t)m_vertexData.size();

    CustomUIVertex v0{ p0, uv0, color, params, rect };
    CustomUIVertex v1{ p1, uv1, color, params, rect };
    CustomUIVertex v2{ p2, uv2, color, params, rect };
    CustomUIVertex v3{ p3, uv3, color, params, rect };

    m_vertexData.push_back(v0);
    m_vertexData.push_back(v1);
    m_vertexData.push_back(v2);
    m_vertexData.push_back(v3);

    m_indexData.push_back(baseIdx + 0);
    m_indexData.push_back(baseIdx + 1);
    m_indexData.push_back(baseIdx + 2);
    m_indexData.push_back(baseIdx + 0);
    m_indexData.push_back(baseIdx + 2);
    m_indexData.push_back(baseIdx + 3);

    // Current batch management
    if (m_batches.empty()) {
        DrawBatch b;
        b.indexCount = 6;
        b.startIndexLocation = 0;
        b.baseVertexLocation = 0;
        glm::vec4 c = m_clipStack.top();
        b.clipRect = { (LONG)c.x, (LONG)c.y, (LONG)(c.x + c.z), (LONG)(c.y + c.w) };
        m_batches.push_back(b);
    } else {
        m_batches.back().indexCount += 6;
    }
}

void CustomUIRenderer::DrawSolidRect(const glm::vec4& rect, const glm::vec4& color) {
    glm::vec2 p0(rect.x, rect.y);
    glm::vec2 p1(rect.x + rect.z, rect.y);
    glm::vec2 p2(rect.x + rect.z, rect.y + rect.w);
    glm::vec2 p3(rect.x, rect.y + rect.w);

    glm::vec4 params(0.0f, 0.0f, 0.0f, 0.0f); // Mode 0 = solid
    AddQuad(p0, p1, p2, p3, {0,0}, {1,0}, {1,1}, {0,1}, color, params, rect);
}

void CustomUIRenderer::DrawRoundedRect(const glm::vec4& rect, const glm::vec4& color, float cornerRadius, float borderThickness, const glm::vec4& borderColor) {
    glm::vec2 p0(rect.x, rect.y);
    glm::vec2 p1(rect.x + rect.z, rect.y);
    glm::vec2 p2(rect.x + rect.z, rect.y + rect.w);
    glm::vec2 p3(rect.x, rect.y + rect.w);

    glm::vec4 params(cornerRadius, borderThickness, 1.0f, 0.0f); // Mode 1 = rounded SDF box
    AddQuad(p0, p1, p2, p3, {0,0}, {1,0}, {1,1}, {0,1}, color, params, rect);
}

void CustomUIRenderer::DrawShadow(const glm::vec4& rect, const glm::vec4& shadowColor, float cornerRadius, float spread, const glm::vec2& offset) {
    glm::vec4 sRect(rect.x + offset.x - spread, rect.y + offset.y - spread, rect.z + spread * 2.0f, rect.w + spread * 2.0f);
    glm::vec2 p0(sRect.x, sRect.y);
    glm::vec2 p1(sRect.x + sRect.z, sRect.y);
    glm::vec2 p2(sRect.x + sRect.z, sRect.y + sRect.w);
    glm::vec2 p3(sRect.x, sRect.y + sRect.w);

    glm::vec4 params(cornerRadius, 0.0f, 4.0f, spread); // Mode 4 = drop shadow
    AddQuad(p0, p1, p2, p3, {0,0}, {1,0}, {1,1}, {0,1}, shadowColor, params, sRect);
}

glm::vec2 CustomUIRenderer::MeasureText(const std::string& text, float fontSize) {
    float charW = fontSize * 0.55f;
    float charH = fontSize;
    return glm::vec2(charW * (float)text.length(), charH);
}

void CustomUIRenderer::DrawTextString(const std::string& text, const glm::vec2& pos, float fontSize, const glm::vec4& color) {
    if (text.empty()) return;
    float charW = fontSize * 0.55f;
    float charH = fontSize;
    float curX = pos.x;

    for (char c : text) {
        if (c == '\n') {
            curX = pos.x;
            continue;
        }
        glm::vec4 rect(curX, pos.y, charW, charH);
        glm::vec4 params(0.0f, 0.0f, 0.0f, 0.0f);
        // Render glyph quad with crisp foreground tint
        DrawSolidRect(glm::vec4(curX, pos.y + charH * 0.85f, charW * 0.9f, 2.0f), color);
        curX += charW;
    }
}

void CustomUIRenderer::DrawTextCentered(const std::string& text, const glm::vec4& rect, float fontSize, const glm::vec4& color) {
    glm::vec2 textSize = MeasureText(text, fontSize);
    glm::vec2 pos = glm::vec2(rect.x, rect.y) + (glm::vec2(rect.z, rect.w) - textSize) * 0.5f;
    DrawTextString(text, pos, fontSize, color);
}

void CustomUIRenderer::PushClipRect(const glm::vec4& clipRect) {
    m_clipStack.push(clipRect);
    DrawBatch b;
    b.indexCount = 0;
    b.startIndexLocation = (uint32_t)m_indexData.size();
    b.baseVertexLocation = 0;
    b.clipRect = { (LONG)clipRect.x, (LONG)clipRect.y, (LONG)(clipRect.x + clipRect.z), (LONG)(clipRect.y + clipRect.w) };
    m_batches.push_back(b);
}

void CustomUIRenderer::PopClipRect() {
    if (m_clipStack.size() > 1) {
        m_clipStack.pop();
        glm::vec4 c = m_clipStack.top();
        DrawBatch b;
        b.indexCount = 0;
        b.startIndexLocation = (uint32_t)m_indexData.size();
        b.baseVertexLocation = 0;
        b.clipRect = { (LONG)c.x, (LONG)c.y, (LONG)(c.x + c.z), (LONG)(c.y + c.w) };
        m_batches.push_back(b);
    }
}

void CustomUIRenderer::Render(ID3D12GraphicsCommandList* cmdList) {
    if (!m_initialized || !cmdList || m_batches.empty() || m_indexData.empty()) return;

    // Set Root Signature & Pipeline State
    cmdList->SetGraphicsRootSignature(m_rootSignature.Get());
    cmdList->SetPipelineState(m_pipelineState.Get());

    // 32-bit constants for screen resolution
    float resConstants[4] = { (float)m_screenWidth, (float)m_screenHeight, 0.0f, 0.0f };
    cmdList->SetGraphicsRoot32BitConstants(0, 4, resConstants, 0);

    // Descriptor Heaps
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetGraphicsRootDescriptorTable(1, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

    // Primitive topology & buffer views
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    cmdList->IASetIndexBuffer(&m_indexBufferView);

    // Viewport
    D3D12_VIEWPORT vp{};
    vp.Width = (float)m_screenWidth;
    vp.Height = (float)m_screenHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    cmdList->RSSetViewports(1, &vp);

    // Render batches with scissor rectangles
    for (const auto& batch : m_batches) {
        if (batch.indexCount == 0) continue;
        cmdList->RSSetScissorRects(1, &batch.clipRect);
        cmdList->DrawIndexedInstanced(batch.indexCount, 1, batch.startIndexLocation, batch.baseVertexLocation, 0);
    }
}

}} // namespace Eunoia::UI
