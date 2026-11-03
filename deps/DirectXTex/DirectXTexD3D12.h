#pragma once
// ============================================================================
// DirectXTexD3D12 — Direct3D 12 Texture Helpers for Eunoia Engine
//
// Inspired by and compatible with Microsoft DirectXTex:
// https://github.com/microsoft/directxtex
// ============================================================================

#include "DirectXTex.h"
#include <d3d12.h>
#include <vector>

namespace DirectX {

    // Creates an ID3D12Resource texture matching the TexMetadata description in COPY_DEST state
    HRESULT CreateTexture(
        ID3D12Device*       pDevice,
        const TexMetadata&  metadata,
        ID3D12Resource**    ppResource
    );

    // Prepares subresource data structures for uploading to D3D12
    HRESULT PrepareUpload(
        ID3D12Device*                       pDevice,
        const Image*                        srcImages,
        size_t                              nimages,
        const TexMetadata&                  metadata,
        std::vector<D3D12_SUBRESOURCE_DATA>& subresources
    );

    // Calculates required intermediate upload buffer byte size for subresources
    UINT64 GetRequiredIntermediateSize(
        ID3D12Device*       pDevice,
        ID3D12Resource*     pDestinationResource,
        UINT                firstSubresource,
        UINT                numSubresources
    );

    // Copies subresource data into upload buffer and records CopyTextureRegion commands
    UINT64 UpdateSubresources(
        ID3D12Device*               pDevice,
        ID3D12GraphicsCommandList*  pCmdList,
        ID3D12Resource*             pDestinationResource,
        ID3D12Resource*             pIntermediate,
        UINT64                      intermediateOffset,
        UINT                        firstSubresource,
        UINT                        numSubresources,
        const D3D12_SUBRESOURCE_DATA* pSrcData
    );

} // namespace DirectX
