#include <EngineAssets/DirectXTexD3D12.h>
#include <algorithm>
#include <iostream>

namespace DirectX {

HRESULT CreateTexture(
    ID3D12Device*       pDevice,
    const TexMetadata&  metadata,
    ID3D12Resource**    ppResource
) {
    if (!pDevice || !ppResource || metadata.width == 0 || metadata.height == 0) {
        return E_INVALIDARG;
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Width = static_cast<UINT64>(metadata.width);
    desc.Height = static_cast<UINT>(metadata.height);
    desc.MipLevels = static_cast<UINT16>(std::max<size_t>(1, metadata.mipLevels));
    desc.Format = metadata.format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (metadata.dimension == TEX_DIMENSION_TEXTURE3D) {
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        desc.DepthOrArraySize = static_cast<UINT16>(std::max<size_t>(1, metadata.depth));
    } else {
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.DepthOrArraySize = static_cast<UINT16>(std::max<size_t>(1, metadata.arraySize));
    }

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    return pDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(ppResource)
    );
}

HRESULT PrepareUpload(
    ID3D12Device*                       pDevice,
    const Image*                        srcImages,
    size_t                              nimages,
    const TexMetadata&                  metadata,
    std::vector<D3D12_SUBRESOURCE_DATA>& subresources
) {
    if (!srcImages || nimages == 0) return E_INVALIDARG;

    subresources.clear();
    subresources.reserve(nimages);

    for (size_t i = 0; i < nimages; ++i) {
        D3D12_SUBRESOURCE_DATA subData = {};
        subData.pData = srcImages[i].pixels;
        subData.RowPitch = static_cast<LONG_PTR>(srcImages[i].rowPitch);
        subData.SlicePitch = static_cast<LONG_PTR>(srcImages[i].slicePitch);
        subresources.push_back(subData);
    }

    return S_OK;
}

UINT64 GetRequiredIntermediateSize(
    ID3D12Device*       pDevice,
    ID3D12Resource*     pDestinationResource,
    UINT                firstSubresource,
    UINT                numSubresources
) {
    if (!pDevice || !pDestinationResource || numSubresources == 0) return 0;

    D3D12_RESOURCE_DESC desc = pDestinationResource->GetDesc();
    UINT64 requiredSize = 0;
    pDevice->GetCopyableFootprints(&desc, firstSubresource, numSubresources, 0, nullptr, nullptr, nullptr, &requiredSize);
    return requiredSize;
}

UINT64 UpdateSubresources(
    ID3D12Device*               pDevice,
    ID3D12GraphicsCommandList*  pCmdList,
    ID3D12Resource*             pDestinationResource,
    ID3D12Resource*             pIntermediate,
    UINT64                      intermediateOffset,
    UINT                        firstSubresource,
    UINT                        numSubresources,
    const D3D12_SUBRESOURCE_DATA* pSrcData
) {
    if (!pDevice || !pCmdList || !pDestinationResource || !pIntermediate || !pSrcData || numSubresources == 0) {
        return 0;
    }

    D3D12_RESOURCE_DESC desc = pDestinationResource->GetDesc();

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(numSubresources);
    std::vector<UINT> numRows(numSubresources);
    std::vector<UINT64> rowSizeInBytes(numSubresources);
    UINT64 totalBytes = 0;

    pDevice->GetCopyableFootprints(
        &desc,
        firstSubresource,
        numSubresources,
        intermediateOffset,
        layouts.data(),
        numRows.data(),
        rowSizeInBytes.data(),
        &totalBytes
    );

    uint8_t* pData = nullptr;
    HRESULT hr = pIntermediate->Map(0, nullptr, reinterpret_cast<void**>(&pData));
    if (FAILED(hr) || !pData) {
        return 0;
    }

    for (UINT i = 0; i < numSubresources; ++i) {
        D3D12_SUBRESOURCE_DATA subData = pSrcData[i];
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = layouts[i];
        UINT rowCount = numRows[i];
        UINT64 rowSize = rowSizeInBytes[i];

        uint8_t* pDestSub = pData + layout.Offset;
        const uint8_t* pSrcSub = reinterpret_cast<const uint8_t*>(subData.pData);

        for (UINT r = 0; r < rowCount; ++r) {
            memcpy(pDestSub + r * layout.Footprint.RowPitch,
                   pSrcSub + r * subData.RowPitch,
                   static_cast<size_t>(rowSize));
        }

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = pDestinationResource;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = firstSubresource + i;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = pIntermediate;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = layout;

        pCmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }

    pIntermediate->Unmap(0, nullptr);
    return totalBytes;
}

} // namespace DirectX
