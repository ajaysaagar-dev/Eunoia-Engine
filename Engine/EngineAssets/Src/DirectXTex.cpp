#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <EngineAssets/DirectXTex.h>
#include <fstream>
#include <iostream>
#include <cassert>
#include <algorithm>
#include <cmath>

namespace DirectX {

// ============================================================================
// Internal DDS File Structures
// ============================================================================

#pragma pack(push, 1)

struct DDS_PIXELFORMAT {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwFourCC;
    uint32_t dwRGBBitCount;
    uint32_t dwRBitMask;
    uint32_t dwGBitMask;
    uint32_t dwBBitMask;
    uint32_t dwABitMask;
};

#define DDPF_ALPHAPIXELS 0x00000001
#define DDPF_ALPHA       0x00000002
#define DDPF_FOURCC      0x00000004
#define DDPF_RGB         0x00000040
#define DDPF_YUV         0x00000200
#define DDPF_LUMINANCE   0x00020000

#define DDSD_CAPS        0x00000001
#define DDSD_HEIGHT      0x00000002
#define DDSD_WIDTH       0x00000004
#define DDSD_PITCH       0x00000008
#define DDSD_PIXELFORMAT 0x00001000
#define DDSD_MIPMAPCOUNT 0x00020000
#define DDSD_LINEARSIZE  0x00080000
#define DDSD_DEPTH       0x00800000

#define DDSCAPS_COMPLEX  0x00000008
#define DDSCAPS_MIPMAP   0x00400000
#define DDSCAPS_TEXTURE  0x00001000

#define DDSCAPS2_CUBEMAP           0x00000200
#define DDSCAPS2_CUBEMAP_POSITIVEX 0x00000400
#define DDSCAPS2_CUBEMAP_NEGATIVEX 0x00000800
#define DDSCAPS2_CUBEMAP_POSITIVEY 0x00001000
#define DDSCAPS2_CUBEMAP_NEGATIVEY 0x00002000
#define DDSCAPS2_CUBEMAP_POSITIVEZ 0x00004000
#define DDSCAPS2_CUBEMAP_NEGATIVEZ 0x00008000
#define DDSCAPS2_CUBEMAP_ALLFACES  (DDSCAPS2_CUBEMAP_POSITIVEX | DDSCAPS2_CUBEMAP_NEGATIVEX | \
                                    DDSCAPS2_CUBEMAP_POSITIVEY | DDSCAPS2_CUBEMAP_NEGATIVEY | \
                                    DDSCAPS2_CUBEMAP_POSITIVEZ | DDSCAPS2_CUBEMAP_NEGATIVEZ)
#define DDSCAPS2_VOLUME            0x00200000

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
    ((uint32_t)(uint8_t)(ch0) | ((uint32_t)(uint8_t)(ch1) << 8) | \
    ((uint32_t)(uint8_t)(ch2) << 16) | ((uint32_t)(uint8_t)(ch3) << 24))
#endif

const uint32_t FOURCC_DXT1 = MAKEFOURCC('D', 'X', 'T', '1');
const uint32_t FOURCC_DXT2 = MAKEFOURCC('D', 'X', 'T', '2');
const uint32_t FOURCC_DXT3 = MAKEFOURCC('D', 'X', 'T', '3');
const uint32_t FOURCC_DXT4 = MAKEFOURCC('D', 'X', 'T', '4');
const uint32_t FOURCC_DXT5 = MAKEFOURCC('D', 'X', 'T', '5');
const uint32_t FOURCC_ATI1 = MAKEFOURCC('A', 'T', 'I', '1');
const uint32_t FOURCC_BC4U = MAKEFOURCC('B', 'C', '4', 'U');
const uint32_t FOURCC_BC4S = MAKEFOURCC('B', 'C', '4', 'S');
const uint32_t FOURCC_ATI2 = MAKEFOURCC('A', 'T', 'I', '2');
const uint32_t FOURCC_BC5U = MAKEFOURCC('B', 'C', '5', 'U');
const uint32_t FOURCC_BC5S = MAKEFOURCC('B', 'C', '5', 'S');
const uint32_t FOURCC_DX10 = MAKEFOURCC('D', 'X', '1', '0');

struct DDS_HEADER {
    uint32_t        dwSize;
    uint32_t        dwFlags;
    uint32_t        dwHeight;
    uint32_t        dwWidth;
    uint32_t        dwPitchOrLinearSize;
    uint32_t        dwDepth;
    uint32_t        dwMipMapCount;
    uint32_t        dwReserved1[11];
    DDS_PIXELFORMAT ddspf;
    uint32_t        dwCaps;
    uint32_t        dwCaps2;
    uint32_t        dwCaps3;
    uint32_t        dwCaps4;
    uint32_t        dwReserved2;
};

struct DDS_HEADER_DXT10 {
    DXGI_FORMAT dxgiFormat;
    uint32_t    resourceDimension;
    uint32_t    miscFlag;
    uint32_t    arraySize;
    uint32_t    miscFlags2;
};

const uint32_t DDS_MAGIC = 0x20534444; // "DDS "

#pragma pack(pop)

// ============================================================================
// Format Helpers
// ============================================================================

bool IsCompressed(DXGI_FORMAT fmt) {
    switch (fmt) {
        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return true;
        default:
            return false;
    }
}

bool IsSRGB(DXGI_FORMAT fmt) {
    switch (fmt) {
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return true;
        default:
            return false;
    }
}

DXGI_FORMAT MakeSRGB(DXGI_FORMAT fmt) {
    switch (fmt) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:      return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_BC1_UNORM:          return DXGI_FORMAT_BC1_UNORM_SRGB;
        case DXGI_FORMAT_BC2_UNORM:          return DXGI_FORMAT_BC2_UNORM_SRGB;
        case DXGI_FORMAT_BC3_UNORM:          return DXGI_FORMAT_BC3_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8A8_UNORM:      return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8X8_UNORM:      return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
        case DXGI_FORMAT_BC7_UNORM:          return DXGI_FORMAT_BC7_UNORM_SRGB;
        default: return fmt;
    }
}

DXGI_FORMAT MakeLinear(DXGI_FORMAT fmt) {
    switch (fmt) {
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_BC1_UNORM_SRGB:      return DXGI_FORMAT_BC1_UNORM;
        case DXGI_FORMAT_BC2_UNORM_SRGB:      return DXGI_FORMAT_BC2_UNORM;
        case DXGI_FORMAT_BC3_UNORM_SRGB:      return DXGI_FORMAT_BC3_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;
        case DXGI_FORMAT_BC7_UNORM_SRGB:      return DXGI_FORMAT_BC7_UNORM;
        default: return fmt;
    }
}

size_t BitsPerPixel(DXGI_FORMAT fmt) {
    switch (fmt) {
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_UINT:
        case DXGI_FORMAT_R32G32B32A32_SINT:
            return 128;

        case DXGI_FORMAT_R32G32B32_TYPELESS:
        case DXGI_FORMAT_R32G32B32_FLOAT:
        case DXGI_FORMAT_R32G32B32_UINT:
        case DXGI_FORMAT_R32G32B32_SINT:
            return 96;

        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
        case DXGI_FORMAT_R16G16B16A16_UINT:
        case DXGI_FORMAT_R16G16B16A16_SNORM:
        case DXGI_FORMAT_R16G16B16A16_SINT:
        case DXGI_FORMAT_R32G32_TYPELESS:
        case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R32G32_UINT:
        case DXGI_FORMAT_R32G32_SINT:
            return 64;

        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_UINT:
        case DXGI_FORMAT_R8G8B8A8_SNORM:
        case DXGI_FORMAT_R8G8B8A8_SINT:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R16G16_UNORM:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_UINT:
            return 32;

        case DXGI_FORMAT_R8G8_UNORM:
        case DXGI_FORMAT_R8G8_UINT:
        case DXGI_FORMAT_R16_FLOAT:
        case DXGI_FORMAT_R16_UNORM:
            return 16;

        case DXGI_FORMAT_R8_UNORM:
        case DXGI_FORMAT_R8_UINT:
        case DXGI_FORMAT_A8_UNORM:
            return 8;

        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
            return 4;

        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return 8;

        default:
            return 0;
    }
}

bool ComputePitch(DXGI_FORMAT fmt, size_t width, size_t height, size_t& rowPitch, size_t& slicePitch) {
    if (width == 0 || height == 0) return false;

    if (IsCompressed(fmt)) {
        size_t bpb = (fmt == DXGI_FORMAT_BC1_UNORM || fmt == DXGI_FORMAT_BC1_UNORM_SRGB ||
                      fmt == DXGI_FORMAT_BC4_UNORM || fmt == DXGI_FORMAT_BC4_SNORM) ? 8 : 16;
        size_t numBlocksWide = (width > 0) ? std::max<size_t>(1, (width + 3) / 4) : 0;
        size_t numBlocksHigh = (height > 0) ? std::max<size_t>(1, (height + 3) / 4) : 0;
        rowPitch = numBlocksWide * bpb;
        slicePitch = rowPitch * numBlocksHigh;
        return true;
    }

    size_t bpp = BitsPerPixel(fmt);
    if (bpp == 0) return false;

    rowPitch = (width * bpp + 7) / 8;
    slicePitch = rowPitch * height;
    return true;
}

const char* GetFormatName(DXGI_FORMAT fmt) {
    switch (fmt) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:      return "R8G8B8A8_UNORM";
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
        case DXGI_FORMAT_B8G8R8A8_UNORM:      return "B8G8R8A8_UNORM";
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
        case DXGI_FORMAT_R16G16B16A16_FLOAT:  return "R16G16B16A16_FLOAT";
        case DXGI_FORMAT_R32G32B32A32_FLOAT:  return "R32G32B32A32_FLOAT";
        case DXGI_FORMAT_BC1_UNORM:          return "BC1_UNORM";
        case DXGI_FORMAT_BC1_UNORM_SRGB:     return "BC1_UNORM_SRGB";
        case DXGI_FORMAT_BC2_UNORM:          return "BC2_UNORM";
        case DXGI_FORMAT_BC2_UNORM_SRGB:     return "BC2_UNORM_SRGB";
        case DXGI_FORMAT_BC3_UNORM:          return "BC3_UNORM";
        case DXGI_FORMAT_BC3_UNORM_SRGB:     return "BC3_UNORM_SRGB";
        case DXGI_FORMAT_BC4_UNORM:          return "BC4_UNORM";
        case DXGI_FORMAT_BC5_UNORM:          return "BC5_UNORM";
        case DXGI_FORMAT_BC6H_UF16:          return "BC6H_UF16";
        case DXGI_FORMAT_BC7_UNORM:          return "BC7_UNORM";
        case DXGI_FORMAT_BC7_UNORM_SRGB:     return "BC7_UNORM_SRGB";
        case DXGI_FORMAT_R8_UNORM:           return "R8_UNORM";
        default:                             return "UNKNOWN_FORMAT";
    }
}

// ============================================================================
// TexMetadata & ScratchImage Implementation
// ============================================================================

size_t TexMetadata::ComputeIndex(size_t mip, size_t item, size_t slice) const {
    if (mip >= mipLevels || item >= arraySize || slice >= depth) return size_t(-1);
    return item * (mipLevels * depth) + slice * mipLevels + mip;
}

ScratchImage::ScratchImage(ScratchImage&& moveFrom) noexcept {
    m_metadata = moveFrom.m_metadata;
    m_images = std::move(moveFrom.m_images);
    m_pixels = std::move(moveFrom.m_pixels);
    moveFrom.m_metadata = {};
}

ScratchImage& ScratchImage::operator=(ScratchImage&& moveFrom) noexcept {
    if (this != &moveFrom) {
        Release();
        m_metadata = moveFrom.m_metadata;
        m_images = std::move(moveFrom.m_images);
        m_pixels = std::move(moveFrom.m_pixels);
        moveFrom.m_metadata = {};
    }
    return *this;
}

ScratchImage::ScratchImage(const ScratchImage& other) {
    if (!other.m_images.empty()) {
        Initialize(other.m_metadata);
        if (!m_pixels.empty() && !other.m_pixels.empty()) {
            memcpy(m_pixels.data(), other.m_pixels.data(), std::min(m_pixels.size(), other.m_pixels.size()));
        }
    }
}

ScratchImage& ScratchImage::operator=(const ScratchImage& other) {
    if (this != &other) {
        Release();
        if (!other.m_images.empty()) {
            Initialize(other.m_metadata);
            if (!m_pixels.empty() && !other.m_pixels.empty()) {
                memcpy(m_pixels.data(), other.m_pixels.data(), std::min(m_pixels.size(), other.m_pixels.size()));
            }
        }
    }
    return *this;
}

void ScratchImage::Release() {
    m_metadata = {};
    m_images.clear();
    m_pixels.clear();
}

bool ScratchImage::Initialize(const TexMetadata& metadata) {
    Release();
    if (metadata.width == 0 || metadata.height == 0 || metadata.mipLevels == 0 || metadata.arraySize == 0) {
        return false;
    }

    m_metadata = metadata;
    size_t totalImages = metadata.arraySize * metadata.depth * metadata.mipLevels;
    m_images.resize(totalImages);

    // Calculate total buffer size
    size_t totalBytes = 0;
    for (size_t item = 0; item < metadata.arraySize; ++item) {
        for (size_t slice = 0; slice < metadata.depth; ++slice) {
            size_t w = metadata.width;
            size_t h = metadata.height;
            for (size_t mip = 0; mip < metadata.mipLevels; ++mip) {
                size_t rowPitch = 0, slicePitch = 0;
                if (!ComputePitch(metadata.format, w, h, rowPitch, slicePitch)) {
                    Release();
                    return false;
                }
                totalBytes += slicePitch;
                if (w > 1) w >>= 1;
                if (h > 1) h >>= 1;
            }
        }
    }

    m_pixels.resize(totalBytes);
    uint8_t* pCurr = m_pixels.data();

    // Setup image subresources
    size_t imgIdx = 0;
    for (size_t item = 0; item < metadata.arraySize; ++item) {
        for (size_t slice = 0; slice < metadata.depth; ++slice) {
            size_t w = metadata.width;
            size_t h = metadata.height;
            for (size_t mip = 0; mip < metadata.mipLevels; ++mip) {
                size_t rowPitch = 0, slicePitch = 0;
                ComputePitch(metadata.format, w, h, rowPitch, slicePitch);

                Image& img = m_images[imgIdx++];
                img.width = w;
                img.height = h;
                img.format = metadata.format;
                img.rowPitch = rowPitch;
                img.slicePitch = slicePitch;
                img.pixels = pCurr;

                pCurr += slicePitch;
                if (w > 1) w >>= 1;
                if (h > 1) h >>= 1;
            }
        }
    }

    return true;
}

bool ScratchImage::Initialize2D(DXGI_FORMAT fmt, size_t width, size_t height, size_t arraySize, size_t mipLevels) {
    TexMetadata md;
    md.width = width;
    md.height = height;
    md.depth = 1;
    md.arraySize = std::max<size_t>(1, arraySize);
    md.mipLevels = std::max<size_t>(1, mipLevels);
    md.format = fmt;
    md.dimension = TEX_DIMENSION_TEXTURE2D;
    md.miscFlags = 0;
    md.miscFlags2 = 0;
    return Initialize(md);
}

bool ScratchImage::InitializeCube(DXGI_FORMAT fmt, size_t width, size_t height, size_t nCubes, size_t mipLevels) {
    TexMetadata md;
    md.width = width;
    md.height = height;
    md.depth = 1;
    md.arraySize = std::max<size_t>(1, nCubes) * 6;
    md.mipLevels = std::max<size_t>(1, mipLevels);
    md.format = fmt;
    md.dimension = TEX_DIMENSION_TEXTURE2D;
    md.miscFlags = TEX_MISC_TEXTURECUBE;
    md.miscFlags2 = 0;
    return Initialize(md);
}

bool ScratchImage::InitializeFromImage(const Image& srcImage) {
    if (!Initialize2D(srcImage.format, srcImage.width, srcImage.height, 1, 1)) return false;
    Image* dst = &m_images[0];
    if (dst->slicePitch == srcImage.slicePitch && dst->rowPitch == srcImage.rowPitch) {
        memcpy(dst->pixels, srcImage.pixels, dst->slicePitch);
    } else {
        size_t copyBytes = std::min(dst->rowPitch, srcImage.rowPitch);
        for (size_t y = 0; y < srcImage.height; ++y) {
            memcpy(dst->pixels + y * dst->rowPitch, srcImage.pixels + y * srcImage.rowPitch, copyBytes);
        }
    }
    return true;
}

const Image* ScratchImage::GetImage(size_t mip, size_t item, size_t slice) const {
    size_t idx = m_metadata.ComputeIndex(mip, item, slice);
    if (idx >= m_images.size()) return nullptr;
    return &m_images[idx];
}

bool ScratchImage::IsAlphaAllOpaque() const {
    if (m_images.empty() || !m_images[0].pixels) return true;
    const Image& img = m_images[0];
    if (img.format != DXGI_FORMAT_R8G8B8A8_UNORM && img.format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
        img.format != DXGI_FORMAT_B8G8R8A8_UNORM && img.format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
        return true;
    }
    for (size_t y = 0; y < img.height; ++y) {
        const uint8_t* row = img.pixels + y * img.rowPitch;
        for (size_t x = 0; x < img.width; ++x) {
            if (row[x * 4 + 3] < 254) return false;
        }
    }
    return true;
}

// ============================================================================
// DDS Loading and Saving
// ============================================================================

static DXGI_FORMAT GetDXGIFormatFromPixelFormat(const DDS_PIXELFORMAT& ddspf) {
    if (ddspf.dwFlags & DDPF_FOURCC) {
        if (ddspf.dwFourCC == FOURCC_DXT1) return DXGI_FORMAT_BC1_UNORM;
        if (ddspf.dwFourCC == FOURCC_DXT2 || ddspf.dwFourCC == FOURCC_DXT3) return DXGI_FORMAT_BC2_UNORM;
        if (ddspf.dwFourCC == FOURCC_DXT4 || ddspf.dwFourCC == FOURCC_DXT5) return DXGI_FORMAT_BC3_UNORM;
        if (ddspf.dwFourCC == FOURCC_ATI1 || ddspf.dwFourCC == FOURCC_BC4U) return DXGI_FORMAT_BC4_UNORM;
        if (ddspf.dwFourCC == FOURCC_BC4S) return DXGI_FORMAT_BC4_SNORM;
        if (ddspf.dwFourCC == FOURCC_ATI2 || ddspf.dwFourCC == FOURCC_BC5U) return DXGI_FORMAT_BC5_UNORM;
        if (ddspf.dwFourCC == FOURCC_BC5S) return DXGI_FORMAT_BC5_SNORM;
    }

    if (ddspf.dwFlags & DDPF_RGB) {
        if (ddspf.dwRGBBitCount == 32) {
            if (ddspf.dwRBitMask == 0x000000ff && ddspf.dwGBitMask == 0x0000ff00 &&
                ddspf.dwBBitMask == 0x00ff0000 && ddspf.dwABitMask == 0xff000000) {
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            }
            if (ddspf.dwRBitMask == 0x00ff0000 && ddspf.dwGBitMask == 0x0000ff00 &&
                ddspf.dwBBitMask == 0x000000ff && ddspf.dwABitMask == 0xff000000) {
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            }
            if (ddspf.dwRBitMask == 0x00ff0000 && ddspf.dwGBitMask == 0x0000ff00 &&
                ddspf.dwBBitMask == 0x000000ff && ddspf.dwABitMask == 0x00000000) {
                return DXGI_FORMAT_B8G8R8X8_UNORM;
            }
        }
        if (ddspf.dwRGBBitCount == 16) {
            if (ddspf.dwRBitMask == 0x7c00 && ddspf.dwGBitMask == 0x03e0 &&
                ddspf.dwBBitMask == 0x001f && ddspf.dwABitMask == 0x8000) {
                return DXGI_FORMAT_B5G5R5A1_UNORM;
            }
            if (ddspf.dwRBitMask == 0xf800 && ddspf.dwGBitMask == 0x07e0 &&
                ddspf.dwBBitMask == 0x001f) {
                return DXGI_FORMAT_B5G6R5_UNORM;
            }
        }
    }

    if (ddspf.dwFlags & DDPF_LUMINANCE) {
        if (ddspf.dwRGBBitCount == 8) return DXGI_FORMAT_R8_UNORM;
        if (ddspf.dwRGBBitCount == 16) return DXGI_FORMAT_R8G8_UNORM;
    }

    if (ddspf.dwFlags & DDPF_ALPHA) {
        if (ddspf.dwRGBBitCount == 8) return DXGI_FORMAT_A8_UNORM;
    }

    return DXGI_FORMAT_UNKNOWN;
}

bool LoadFromDDSMemory(const void* pSource, size_t size, TexMetadata& metadata, ScratchImage& image) {
    if (!pSource || size < (sizeof(uint32_t) + sizeof(DDS_HEADER))) return false;

    const uint8_t* pData = reinterpret_cast<const uint8_t*>(pSource);
    uint32_t magic = *reinterpret_cast<const uint32_t*>(pData);
    if (magic != DDS_MAGIC) return false;

    const DDS_HEADER* header = reinterpret_cast<const DDS_HEADER*>(pData + sizeof(uint32_t));
    if (header->dwSize != sizeof(DDS_HEADER) || header->ddspf.dwSize != sizeof(DDS_PIXELFORMAT)) return false;

    size_t offset = sizeof(uint32_t) + sizeof(DDS_HEADER);
    bool hasDX10 = false;
    DDS_HEADER_DXT10 dxt10Header = {};

    if ((header->ddspf.dwFlags & DDPF_FOURCC) && (header->ddspf.dwFourCC == FOURCC_DX10)) {
        if (size < offset + sizeof(DDS_HEADER_DXT10)) return false;
        memcpy(&dxt10Header, pData + offset, sizeof(DDS_HEADER_DXT10));
        offset += sizeof(DDS_HEADER_DXT10);
        hasDX10 = true;
    }

    metadata = {};
    metadata.width = header->dwWidth;
    metadata.height = header->dwHeight;
    metadata.depth = (header->dwFlags & DDSD_DEPTH) ? std::max<size_t>(1, header->dwDepth) : 1;
    metadata.mipLevels = (header->dwFlags & DDSD_MIPMAPCOUNT) ? std::max<size_t>(1, header->dwMipMapCount) : 1;

    if (hasDX10) {
        metadata.format = dxt10Header.dxgiFormat;
        metadata.arraySize = std::max<size_t>(1, dxt10Header.arraySize);
        metadata.miscFlags = dxt10Header.miscFlag;
        metadata.miscFlags2 = dxt10Header.miscFlags2;
        metadata.dimension = static_cast<TEX_DIMENSION>(dxt10Header.resourceDimension);
    } else {
        metadata.format = GetDXGIFormatFromPixelFormat(header->ddspf);
        metadata.arraySize = 1;
        metadata.dimension = (metadata.depth > 1) ? TEX_DIMENSION_TEXTURE3D : TEX_DIMENSION_TEXTURE2D;

        if (header->dwCaps2 & DDSCAPS2_CUBEMAP) {
            metadata.miscFlags |= TEX_MISC_TEXTURECUBE;
            metadata.arraySize = 6;
        }
    }

    if (metadata.format == DXGI_FORMAT_UNKNOWN) {
        return false;
    }

    if (!image.Initialize(metadata)) {
        return false;
    }

    // Read pixel data slice by slice
    for (size_t item = 0; item < metadata.arraySize; ++item) {
        for (size_t slice = 0; slice < metadata.depth; ++slice) {
            for (size_t mip = 0; mip < metadata.mipLevels; ++mip) {
                const Image* img = image.GetImage(mip, item, slice);
                if (!img) return false;

                if (offset + img->slicePitch > size) {
                    return false;
                }

                memcpy(img->pixels, pData + offset, img->slicePitch);
                offset += img->slicePitch;
            }
        }
    }

    return true;
}

bool LoadFromDDSFile(const char* szFile, TexMetadata& metadata, ScratchImage& image) {
    if (!szFile || !*szFile) return false;
    std::ifstream file(szFile, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize fileSize = file.tellg();
    if (fileSize <= 0) return false;

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) return false;

    return LoadFromDDSMemory(buffer.data(), buffer.size(), metadata, image);
}

bool LoadFromDDSFile(const wchar_t* szFile, TexMetadata& metadata, ScratchImage& image) {
    if (!szFile || !*szFile) return false;
    char mbPath[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, szFile, -1, mbPath, MAX_PATH, nullptr, nullptr);
    return LoadFromDDSFile(mbPath, metadata, image);
}

bool SaveToDDSMemory(const Image* images, size_t nimages, const TexMetadata& metadata, std::vector<uint8_t>& blob) {
    if (!images || nimages == 0 || metadata.width == 0 || metadata.height == 0) return false;

    bool needDX10 = false;
    switch (metadata.format) {
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            needDX10 = (IsSRGB(metadata.format) || metadata.arraySize > 1);
            break;
        default:
            needDX10 = true;
            break;
    }

    size_t headerSize = sizeof(uint32_t) + sizeof(DDS_HEADER);
    if (needDX10) headerSize += sizeof(DDS_HEADER_DXT10);

    size_t totalDataSize = 0;
    for (size_t i = 0; i < nimages; ++i) {
        totalDataSize += images[i].slicePitch;
    }

    blob.resize(headerSize + totalDataSize);
    uint8_t* pDest = blob.data();

    // Magic
    *reinterpret_cast<uint32_t*>(pDest) = DDS_MAGIC;
    pDest += sizeof(uint32_t);

    // DDS Header
    DDS_HEADER header = {};
    header.dwSize = sizeof(DDS_HEADER);
    header.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
    if (metadata.mipLevels > 1) {
        header.dwFlags |= DDSD_MIPMAPCOUNT;
        header.dwMipMapCount = static_cast<uint32_t>(metadata.mipLevels);
    }
    if (metadata.depth > 1) {
        header.dwFlags |= DDSD_DEPTH;
        header.dwDepth = static_cast<uint32_t>(metadata.depth);
    }

    header.dwWidth = static_cast<uint32_t>(metadata.width);
    header.dwHeight = static_cast<uint32_t>(metadata.height);
    header.dwPitchOrLinearSize = static_cast<uint32_t>(images[0].rowPitch);
    header.dwCaps = DDSCAPS_TEXTURE;
    if (metadata.mipLevels > 1) header.dwCaps |= (DDSCAPS_MIPMAP | DDSCAPS_COMPLEX);
    if (metadata.IsCubemap()) {
        header.dwCaps |= DDSCAPS_COMPLEX;
        header.dwCaps2 |= (DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_ALLFACES);
    }

    header.ddspf.dwSize = sizeof(DDS_PIXELFORMAT);
    if (needDX10) {
        header.ddspf.dwFlags = DDPF_FOURCC;
        header.ddspf.dwFourCC = FOURCC_DX10;
    } else {
        switch (metadata.format) {
            case DXGI_FORMAT_BC1_UNORM:
                header.ddspf.dwFlags = DDPF_FOURCC;
                header.ddspf.dwFourCC = FOURCC_DXT1;
                break;
            case DXGI_FORMAT_BC2_UNORM:
                header.ddspf.dwFlags = DDPF_FOURCC;
                header.ddspf.dwFourCC = FOURCC_DXT3;
                break;
            case DXGI_FORMAT_BC3_UNORM:
                header.ddspf.dwFlags = DDPF_FOURCC;
                header.ddspf.dwFourCC = FOURCC_DXT5;
                break;
            case DXGI_FORMAT_BC4_UNORM:
                header.ddspf.dwFlags = DDPF_FOURCC;
                header.ddspf.dwFourCC = FOURCC_ATI1;
                break;
            case DXGI_FORMAT_BC5_UNORM:
                header.ddspf.dwFlags = DDPF_FOURCC;
                header.ddspf.dwFourCC = FOURCC_ATI2;
                break;
            case DXGI_FORMAT_R8G8B8A8_UNORM:
                header.ddspf.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
                header.ddspf.dwRGBBitCount = 32;
                header.ddspf.dwRBitMask = 0x000000ff;
                header.ddspf.dwGBitMask = 0x0000ff00;
                header.ddspf.dwBBitMask = 0x00ff0000;
                header.ddspf.dwABitMask = 0xff000000;
                break;
            default:
                header.ddspf.dwFlags = DDPF_FOURCC;
                header.ddspf.dwFourCC = FOURCC_DX10;
                needDX10 = true;
                break;
        }
    }

    memcpy(pDest, &header, sizeof(DDS_HEADER));
    pDest += sizeof(DDS_HEADER);

    if (needDX10) {
        DDS_HEADER_DXT10 dxt10 = {};
        dxt10.dxgiFormat = metadata.format;
        dxt10.resourceDimension = metadata.dimension;
        dxt10.miscFlag = metadata.miscFlags;
        dxt10.arraySize = static_cast<uint32_t>(metadata.arraySize);
        dxt10.miscFlags2 = metadata.miscFlags2;
        memcpy(pDest, &dxt10, sizeof(DDS_HEADER_DXT10));
        pDest += sizeof(DDS_HEADER_DXT10);
    }

    for (size_t i = 0; i < nimages; ++i) {
        memcpy(pDest, images[i].pixels, images[i].slicePitch);
        pDest += images[i].slicePitch;
    }

    return true;
}

bool SaveToDDSFile(const Image* images, size_t nimages, const TexMetadata& metadata, const char* szFile) {
    std::vector<uint8_t> blob;
    if (!SaveToDDSMemory(images, nimages, metadata, blob)) return false;

    std::ofstream file(szFile, std::ios::binary);
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(blob.data()), blob.size());
    return file.good();
}

// ============================================================================
// Mipmap Generation
// ============================================================================

static inline float sRGBToLinear(float s) {
    return (s <= 0.04045f) ? (s / 12.92f) : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

static inline float LinearTosRGB(float l) {
    return (l <= 0.0031308f) ? (l * 12.92f) : (1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f);
}

bool GenerateMipMaps(const Image& baseImage, uint32_t filter, size_t levels, ScratchImage& mipChain) {
    if (!baseImage.pixels || baseImage.width == 0 || baseImage.height == 0) return false;

    // Currently support RGBA8 32-bit images for CPU mip generation
    if (baseImage.format != DXGI_FORMAT_R8G8B8A8_UNORM &&
        baseImage.format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
        baseImage.format != DXGI_FORMAT_B8G8R8A8_UNORM &&
        baseImage.format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
        return false;
    }

    size_t maxDim = std::max(baseImage.width, baseImage.height);
    size_t fullLevels = 1;
    while (maxDim > 1) {
        maxDim >>= 1;
        fullLevels++;
    }

    if (levels == 0 || levels > fullLevels) {
        levels = fullLevels;
    }

    if (!mipChain.Initialize2D(baseImage.format, baseImage.width, baseImage.height, 1, levels)) {
        return false;
    }

    // Level 0: copy directly
    const Image* img0 = mipChain.GetImage(0, 0, 0);
    memcpy(img0->pixels, baseImage.pixels, img0->slicePitch);

    bool srgbFilter = (filter & TEX_FILTER_SRGB) || IsSRGB(baseImage.format);
    bool normalNormalize = (filter & TEX_FILTER_NORMAL_NORMALIZE);

    // Downsample loop
    for (size_t m = 1; m < levels; ++m) {
        const Image* src = mipChain.GetImage(m - 1, 0, 0);
        const Image* dst = mipChain.GetImage(m, 0, 0);

        for (size_t dy = 0; dy < dst->height; ++dy) {
            uint8_t* dstRow = dst->pixels + dy * dst->rowPitch;

            size_t sy0 = dy * 2;
            size_t sy1 = std::min(sy0 + 1, src->height - 1);
            const uint8_t* srcRow0 = src->pixels + sy0 * src->rowPitch;
            const uint8_t* srcRow1 = src->pixels + sy1 * src->rowPitch;

            for (size_t dx = 0; dx < dst->width; ++dx) {
                size_t sx0 = dx * 2;
                size_t sx1 = std::min(sx0 + 1, src->width - 1);

                const uint8_t* p00 = srcRow0 + sx0 * 4;
                const uint8_t* p10 = srcRow0 + sx1 * 4;
                const uint8_t* p01 = srcRow1 + sx0 * 4;
                const uint8_t* p11 = srcRow1 + sx1 * 4;

                uint8_t* pOut = dstRow + dx * 4;

                if (normalNormalize) {
                    // Normal map downsample: average signed normals, renormalize vector
                    float nx = ((p00[0] + p10[0] + p01[0] + p11[0]) / 4.0f / 127.5f) - 1.0f;
                    float ny = ((p00[1] + p10[1] + p01[1] + p11[1]) / 4.0f / 127.5f) - 1.0f;
                    float nz = ((p00[2] + p10[2] + p01[2] + p11[2]) / 4.0f / 127.5f) - 1.0f;
                    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                    if (len > 1e-5f) {
                        nx /= len; ny /= len; nz /= len;
                    } else {
                        nx = 0.0f; ny = 0.0f; nz = 1.0f;
                    }
                    pOut[0] = static_cast<uint8_t>(std::clamp((nx * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
                    pOut[1] = static_cast<uint8_t>(std::clamp((ny * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
                    pOut[2] = static_cast<uint8_t>(std::clamp((nz * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
                    pOut[3] = static_cast<uint8_t>((p00[3] + p10[3] + p01[3] + p11[3]) / 4);
                } else if (srgbFilter) {
                    // sRGB-aware box filter in linear color space
                    float r = (sRGBToLinear(p00[0] / 255.0f) + sRGBToLinear(p10[0] / 255.0f) +
                               sRGBToLinear(p01[0] / 255.0f) + sRGBToLinear(p11[0] / 255.0f)) * 0.25f;
                    float g = (sRGBToLinear(p00[1] / 255.0f) + sRGBToLinear(p10[1] / 255.0f) +
                               sRGBToLinear(p01[1] / 255.0f) + sRGBToLinear(p11[1] / 255.0f)) * 0.25f;
                    float b = (sRGBToLinear(p00[2] / 255.0f) + sRGBToLinear(p10[2] / 255.0f) +
                               sRGBToLinear(p01[2] / 255.0f) + sRGBToLinear(p11[2] / 255.0f)) * 0.25f;
                    float a = (p00[3] + p10[3] + p01[3] + p11[3]) * 0.25f;

                    pOut[0] = static_cast<uint8_t>(std::clamp(LinearTosRGB(r) * 255.0f, 0.0f, 255.0f));
                    pOut[1] = static_cast<uint8_t>(std::clamp(LinearTosRGB(g) * 255.0f, 0.0f, 255.0f));
                    pOut[2] = static_cast<uint8_t>(std::clamp(LinearTosRGB(b) * 255.0f, 0.0f, 255.0f));
                    pOut[3] = static_cast<uint8_t>(a);
                } else {
                    // Standard linear averaging (for Roughness, Metallic, AO)
                    pOut[0] = static_cast<uint8_t>((p00[0] + p10[0] + p01[0] + p11[0] + 2) >> 2);
                    pOut[1] = static_cast<uint8_t>((p00[1] + p10[1] + p01[1] + p11[1] + 2) >> 2);
                    pOut[2] = static_cast<uint8_t>((p00[2] + p10[2] + p01[2] + p11[2] + 2) >> 2);
                    pOut[3] = static_cast<uint8_t>((p00[3] + p10[3] + p01[3] + p11[3] + 2) >> 2);
                }
            }
        }
    }

    return true;
}

// ============================================================================
// Block Decompression (BC1, BC2, BC3, BC4, BC5, BC7)
// ============================================================================

static void DecodeBC1Block(const uint8_t* block, uint8_t rgba[16][4]) {
    uint16_t c0 = *reinterpret_cast<const uint16_t*>(block);
    uint16_t c1 = *reinterpret_cast<const uint16_t*>(block + 2);
    uint32_t code = *reinterpret_cast<const uint32_t*>(block + 4);

    uint8_t r0 = (c0 >> 11) & 0x1f; r0 = (r0 * 527 + 23) >> 6;
    uint8_t g0 = (c0 >> 5)  & 0x3f; g0 = (g0 * 259 + 33) >> 6;
    uint8_t b0 = c0         & 0x1f; b0 = (b0 * 527 + 23) >> 6;

    uint8_t r1 = (c1 >> 11) & 0x1f; r1 = (r1 * 527 + 23) >> 6;
    uint8_t g1 = (c1 >> 5)  & 0x3f; g1 = (g1 * 259 + 33) >> 6;
    uint8_t b1 = c1         & 0x1f; b1 = (b1 * 527 + 23) >> 6;

    uint8_t palette[4][4];
    palette[0][0] = r0; palette[0][1] = g0; palette[0][2] = b0; palette[0][3] = 255;
    palette[1][0] = r1; palette[1][1] = g1; palette[1][2] = b1; palette[1][3] = 255;

    if (c0 > c1) {
        palette[2][0] = (2 * r0 + r1 + 1) / 3;
        palette[2][1] = (2 * g0 + g1 + 1) / 3;
        palette[2][2] = (2 * b0 + b1 + 1) / 3;
        palette[2][3] = 255;

        palette[3][0] = (r0 + 2 * r1 + 1) / 3;
        palette[3][1] = (g0 + 2 * g1 + 1) / 3;
        palette[3][2] = (b0 + 2 * b1 + 1) / 3;
        palette[3][3] = 255;
    } else {
        palette[2][0] = (r0 + r1) / 2;
        palette[2][1] = (g0 + g1) / 2;
        palette[2][2] = (b0 + b1) / 2;
        palette[2][3] = 255;

        palette[3][0] = 0; palette[3][1] = 0; palette[3][2] = 0; palette[3][3] = 0;
    }

    for (int i = 0; i < 16; ++i) {
        int idx = (code >> (i * 2)) & 3;
        rgba[i][0] = palette[idx][0];
        rgba[i][1] = palette[idx][1];
        rgba[i][2] = palette[idx][2];
        rgba[i][3] = palette[idx][3];
    }
}

static void DecodeBC3AlphaBlock(const uint8_t* block, uint8_t alpha[16]) {
    uint8_t a0 = block[0];
    uint8_t a1 = block[1];

    uint8_t table[8];
    table[0] = a0;
    table[1] = a1;

    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i) {
            table[i + 1] = ((7 - i) * a0 + i * a1 + 3) / 7;
        }
    } else {
        for (int i = 1; i <= 4; ++i) {
            table[i + 1] = ((5 - i) * a0 + i * a1 + 2) / 5;
        }
        table[6] = 0;
        table[7] = 255;
    }

    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= static_cast<uint64_t>(block[2 + i]) << (i * 8);
    }

    for (int i = 0; i < 16; ++i) {
        int idx = (bits >> (i * 3)) & 7;
        alpha[i] = table[idx];
    }
}

bool Decompress(const Image& cImage, DXGI_FORMAT format, ScratchImage& image) {
    if (!cImage.pixels || cImage.width == 0 || cImage.height == 0) return false;

    DXGI_FORMAT dstFmt = (format != DXGI_FORMAT_UNKNOWN) ? format :
                         (IsSRGB(cImage.format) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM);

    if (!image.Initialize2D(dstFmt, cImage.width, cImage.height, 1, 1)) return false;

    const Image* dst = image.GetImage(0, 0, 0);
    size_t numBlocksX = (cImage.width + 3) / 4;
    size_t numBlocksY = (cImage.height + 3) / 4;

    for (size_t by = 0; by < numBlocksY; ++by) {
        for (size_t bx = 0; bx < numBlocksX; ++bx) {
            uint8_t blockRGBA[16][4];

            if (cImage.format == DXGI_FORMAT_BC1_UNORM || cImage.format == DXGI_FORMAT_BC1_UNORM_SRGB) {
                const uint8_t* pBlock = cImage.pixels + (by * numBlocksX + bx) * 8;
                DecodeBC1Block(pBlock, blockRGBA);
            } else if (cImage.format == DXGI_FORMAT_BC2_UNORM || cImage.format == DXGI_FORMAT_BC2_UNORM_SRGB) {
                const uint8_t* pBlock = cImage.pixels + (by * numBlocksX + bx) * 16;
                DecodeBC1Block(pBlock + 8, blockRGBA);
                for (int i = 0; i < 16; ++i) {
                    uint8_t a4 = (pBlock[i / 2] >> ((i % 2) * 4)) & 0xf;
                    blockRGBA[i][3] = a4 * 17;
                }
            } else if (cImage.format == DXGI_FORMAT_BC3_UNORM || cImage.format == DXGI_FORMAT_BC3_UNORM_SRGB) {
                const uint8_t* pBlock = cImage.pixels + (by * numBlocksX + bx) * 16;
                DecodeBC1Block(pBlock + 8, blockRGBA);
                uint8_t alpha[16];
                DecodeBC3AlphaBlock(pBlock, alpha);
                for (int i = 0; i < 16; ++i) blockRGBA[i][3] = alpha[i];
            } else if (cImage.format == DXGI_FORMAT_BC4_UNORM || cImage.format == DXGI_FORMAT_BC4_SNORM) {
                const uint8_t* pBlock = cImage.pixels + (by * numBlocksX + bx) * 8;
                uint8_t red[16];
                DecodeBC3AlphaBlock(pBlock, red);
                for (int i = 0; i < 16; ++i) {
                    blockRGBA[i][0] = red[i];
                    blockRGBA[i][1] = red[i];
                    blockRGBA[i][2] = red[i];
                    blockRGBA[i][3] = 255;
                }
            } else if (cImage.format == DXGI_FORMAT_BC5_UNORM || cImage.format == DXGI_FORMAT_BC5_SNORM) {
                const uint8_t* pBlock = cImage.pixels + (by * numBlocksX + bx) * 16;
                uint8_t r[16], g[16];
                DecodeBC3AlphaBlock(pBlock, r);
                DecodeBC3AlphaBlock(pBlock + 8, g);
                for (int i = 0; i < 16; ++i) {
                    float x = (r[i] / 127.5f) - 1.0f;
                    float y = (g[i] / 127.5f) - 1.0f;
                    float z = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y));
                    blockRGBA[i][0] = r[i];
                    blockRGBA[i][1] = g[i];
                    blockRGBA[i][2] = static_cast<uint8_t>((z * 0.5f + 0.5f) * 255.0f);
                    blockRGBA[i][3] = 255;
                }
            } else {
                // Fallback for unsupported / BC7 decode: fill flat neutral
                for (int i = 0; i < 16; ++i) {
                    blockRGBA[i][0] = 128; blockRGBA[i][1] = 128; blockRGBA[i][2] = 128; blockRGBA[i][3] = 255;
                }
            }

            // Scatter 4x4 block into destination image
            for (size_t py = 0; py < 4; ++py) {
                size_t y = by * 4 + py;
                if (y >= dst->height) break;
                uint8_t* pDstRow = dst->pixels + y * dst->rowPitch;
                for (size_t px = 0; px < 4; ++px) {
                    size_t x = bx * 4 + px;
                    if (x >= dst->width) break;
                    uint8_t* pPixel = pDstRow + x * 4;
                    int srcIdx = static_cast<int>(py * 4 + px);
                    pPixel[0] = blockRGBA[srcIdx][0];
                    pPixel[1] = blockRGBA[srcIdx][1];
                    pPixel[2] = blockRGBA[srcIdx][2];
                    pPixel[3] = blockRGBA[srcIdx][3];
                }
            }
        }
    }

    return true;
}

// ============================================================================
// Block Compression (BC1, BC3, BC4, BC5)
// ============================================================================

static uint16_t RGB888To565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void CompressBC1Block(const uint8_t rgba[16][4], uint8_t* outBlock) {
    // Find min and max bounding box in RGB space
    uint8_t minR = 255, minG = 255, minB = 255;
    uint8_t maxR = 0,   maxG = 0,   maxB = 0;

    for (int i = 0; i < 16; ++i) {
        minR = std::min(minR, rgba[i][0]);
        minG = std::min(minG, rgba[i][1]);
        minB = std::min(minB, rgba[i][2]);

        maxR = std::max(maxR, rgba[i][0]);
        maxG = std::max(maxG, rgba[i][1]);
        maxB = std::max(maxB, rgba[i][2]);
    }

    uint16_t c0 = RGB888To565(maxR, maxG, maxB);
    uint16_t c1 = RGB888To565(minR, minG, minB);

    if (c0 < c1) std::swap(c0, c1);
    else if (c0 == c1 && c0 > 0) c1--;

    *reinterpret_cast<uint16_t*>(outBlock) = c0;
    *reinterpret_cast<uint16_t*>(outBlock + 2) = c1;

    uint8_t pal[4][3];
    pal[0][0] = maxR; pal[0][1] = maxG; pal[0][2] = maxB;
    pal[1][0] = minR; pal[1][1] = minG; pal[1][2] = minB;
    pal[2][0] = (2 * maxR + minR) / 3; pal[2][1] = (2 * maxG + minG) / 3; pal[2][2] = (2 * maxB + minB) / 3;
    pal[3][0] = (maxR + 2 * minR) / 3; pal[3][1] = (maxG + 2 * minG) / 3; pal[3][2] = (maxB + 2 * minB) / 3;

    uint32_t indices = 0;
    for (int i = 0; i < 16; ++i) {
        int bestIdx = 0;
        int bestDist = 0x7fffffff;
        for (int p = 0; p < 4; ++p) {
            int dr = rgba[i][0] - pal[p][0];
            int dg = rgba[i][1] - pal[p][1];
            int db = rgba[i][2] - pal[p][2];
            int dist = dr * dr + dg * dg + db * db;
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx = p;
            }
        }
        indices |= (bestIdx << (i * 2));
    }
    *reinterpret_cast<uint32_t*>(outBlock + 4) = indices;
}

static void CompressBC3AlphaBlock(const uint8_t alpha[16], uint8_t* outBlock) {
    uint8_t minA = 255, maxA = 0;
    for (int i = 0; i < 16; ++i) {
        minA = std::min(minA, alpha[i]);
        maxA = std::max(maxA, alpha[i]);
    }

    outBlock[0] = maxA;
    outBlock[1] = minA;

    uint8_t pal[8];
    pal[0] = maxA;
    pal[1] = minA;
    for (int i = 1; i <= 6; ++i) {
        pal[i + 1] = ((7 - i) * maxA + i * minA + 3) / 7;
    }

    uint64_t indices = 0;
    for (int i = 0; i < 16; ++i) {
        int bestIdx = 0;
        int bestDist = 0x7fffffff;
        for (int p = 0; p < 8; ++p) {
            int d = static_cast<int>(alpha[i]) - static_cast<int>(pal[p]);
            int dist = d * d;
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx = p;
            }
        }
        indices |= (static_cast<uint64_t>(bestIdx) << (i * 3));
    }

    for (int i = 0; i < 6; ++i) {
        outBlock[2 + i] = static_cast<uint8_t>((indices >> (i * 8)) & 0xff);
    }
}

bool Compress(const Image& srcImage, DXGI_FORMAT format, ScratchImage& image) {
    if (!srcImage.pixels || srcImage.width == 0 || srcImage.height == 0) return false;
    if (!IsCompressed(format)) return false;

    if (!image.Initialize2D(format, srcImage.width, srcImage.height, 1, 1)) return false;

    const Image* dst = image.GetImage(0, 0, 0);
    size_t numBlocksX = (srcImage.width + 3) / 4;
    size_t numBlocksY = (srcImage.height + 3) / 4;

    for (size_t by = 0; by < numBlocksY; ++by) {
        for (size_t bx = 0; bx < numBlocksX; ++bx) {
            uint8_t blockRGBA[16][4];

            // Gather 4x4 block from source
            for (size_t py = 0; py < 4; ++py) {
                size_t y = std::min(by * 4 + py, srcImage.height - 1);
                const uint8_t* pSrcRow = srcImage.pixels + y * srcImage.rowPitch;
                for (size_t px = 0; px < 4; ++px) {
                    size_t x = std::min(bx * 4 + px, srcImage.width - 1);
                    const uint8_t* pSrc = pSrcRow + x * 4;
                    int idx = static_cast<int>(py * 4 + px);
                    blockRGBA[idx][0] = pSrc[0];
                    blockRGBA[idx][1] = pSrc[1];
                    blockRGBA[idx][2] = pSrc[2];
                    blockRGBA[idx][3] = pSrc[3];
                }
            }

            if (format == DXGI_FORMAT_BC1_UNORM || format == DXGI_FORMAT_BC1_UNORM_SRGB) {
                uint8_t* pOut = dst->pixels + (by * numBlocksX + bx) * 8;
                CompressBC1Block(blockRGBA, pOut);
            } else if (format == DXGI_FORMAT_BC3_UNORM || format == DXGI_FORMAT_BC3_UNORM_SRGB) {
                uint8_t* pOut = dst->pixels + (by * numBlocksX + bx) * 16;
                uint8_t alpha[16];
                for (int i = 0; i < 16; ++i) alpha[i] = blockRGBA[i][3];
                CompressBC3AlphaBlock(alpha, pOut);
                CompressBC1Block(blockRGBA, pOut + 8);
            } else if (format == DXGI_FORMAT_BC4_UNORM || format == DXGI_FORMAT_BC4_SNORM) {
                uint8_t* pOut = dst->pixels + (by * numBlocksX + bx) * 8;
                uint8_t red[16];
                for (int i = 0; i < 16; ++i) red[i] = blockRGBA[i][0];
                CompressBC3AlphaBlock(red, pOut);
            } else if (format == DXGI_FORMAT_BC5_UNORM || format == DXGI_FORMAT_BC5_SNORM) {
                uint8_t* pOut = dst->pixels + (by * numBlocksX + bx) * 16;
                uint8_t red[16], green[16];
                for (int i = 0; i < 16; ++i) {
                    red[i] = blockRGBA[i][0];
                    green[i] = blockRGBA[i][1];
                }
                CompressBC3AlphaBlock(red, pOut);
                CompressBC3AlphaBlock(green, pOut + 8);
            }
        }
    }

    return true;
}

// ============================================================================
// Normal Map & Channel Processing Utilities
// ============================================================================

bool ComputeNormalMap(const Image& srcImage, uint32_t flags, float amplitude, DXGI_FORMAT format, ScratchImage& normalMap) {
    if (!srcImage.pixels || srcImage.width == 0 || srcImage.height == 0) return false;

    DXGI_FORMAT dstFormat = (format != DXGI_FORMAT_UNKNOWN) ? format : DXGI_FORMAT_R8G8B8A8_UNORM;
    if (!normalMap.Initialize2D(dstFormat, srcImage.width, srcImage.height, 1, 1)) return false;

    const Image* dst = normalMap.GetImage(0, 0, 0);

    auto SampleHeight = [&](int x, int y) -> float {
        if (x < 0) x = 0; else if (x >= (int)srcImage.width) x = (int)srcImage.width - 1;
        if (y < 0) y = 0; else if (y >= (int)srcImage.height) y = (int)srcImage.height - 1;
        const uint8_t* p = srcImage.pixels + y * srcImage.rowPitch + x * 4;
        return (p[0] * 0.299f + p[1] * 0.587f + p[2] * 0.114f) / 255.0f;
    };

    for (int y = 0; y < (int)srcImage.height; ++y) {
        uint8_t* pDstRow = dst->pixels + y * dst->rowPitch;
        for (int x = 0; x < (int)srcImage.width; ++x) {
            // Sobel 3x3 filter
            float tl = SampleHeight(x - 1, y - 1);
            float t  = SampleHeight(x,     y - 1);
            float tr = SampleHeight(x + 1, y - 1);
            float l  = SampleHeight(x - 1, y);
            float r  = SampleHeight(x + 1, y);
            float bl = SampleHeight(x - 1, y + 1);
            float b  = SampleHeight(x,     y + 1);
            float br = SampleHeight(x + 1, y + 1);

            float dX = (tr + 2.0f * r + br) - (tl + 2.0f * l + bl);
            float dY = (bl + 2.0f * b + br) - (tl + 2.0f * t + tr);

            if (flags & CNMAP_INVERT_X) dX = -dX;
            if (flags & CNMAP_INVERT_Y) dY = -dY;

            float nx = -dX * amplitude;
            float ny = -dY * amplitude;
            float nz = 1.0f;

            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-5f) { nx /= len; ny /= len; nz /= len; }

            uint8_t* outP = pDstRow + x * 4;
            outP[0] = static_cast<uint8_t>(std::clamp((nx * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
            outP[1] = static_cast<uint8_t>(std::clamp((ny * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
            outP[2] = static_cast<uint8_t>(std::clamp((nz * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
            outP[3] = 255;
        }
    }

    return true;
}

bool InvertGreenChannel(Image& img) {
    if (!img.pixels || img.width == 0 || img.height == 0) return false;
    for (size_t y = 0; y < img.height; ++y) {
        uint8_t* row = img.pixels + y * img.rowPitch;
        for (size_t x = 0; x < img.width; ++x) {
            row[x * 4 + 1] = 255 - row[x * 4 + 1];
        }
    }
    return true;
}

bool PackPbrChannels(const Image* roughnessImg, int roughCh,
                     const Image* metallicImg,  int metalCh,
                     const Image* aoImg,        int aoCh,
                     ScratchImage& packedOut) {
    size_t w = 1, h = 1;
    if (roughnessImg) { w = roughnessImg->width; h = roughnessImg->height; }
    else if (metallicImg) { w = metallicImg->width; h = metallicImg->height; }
    else if (aoImg) { w = aoImg->width; h = aoImg->height; }
    else return false;

    if (!packedOut.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, w, h, 1, 1)) return false;

    const Image* dst = packedOut.GetImage(0, 0, 0);
    for (size_t y = 0; y < h; ++y) {
        uint8_t* dstRow = dst->pixels + y * dst->rowPitch;
        for (size_t x = 0; x < w; ++x) {
            uint8_t r = 255, g = 255, b = 255, a = 255;

            // GLTF standard packed packing:
            // R = AO, G = Roughness, B = Metallic
            if (aoImg && x < aoImg->width && y < aoImg->height) {
                const uint8_t* p = aoImg->pixels + y * aoImg->rowPitch + x * 4;
                r = p[std::clamp(aoCh, 0, 3)];
            }
            if (roughnessImg && x < roughnessImg->width && y < roughnessImg->height) {
                const uint8_t* p = roughnessImg->pixels + y * roughnessImg->rowPitch + x * 4;
                g = p[std::clamp(roughCh, 0, 3)];
            }
            if (metallicImg && x < metallicImg->width && y < metallicImg->height) {
                const uint8_t* p = metallicImg->pixels + y * metallicImg->rowPitch + x * 4;
                b = p[std::clamp(metalCh, 0, 3)];
            }

            dstRow[x * 4 + 0] = r;
            dstRow[x * 4 + 1] = g;
            dstRow[x * 4 + 2] = b;
            dstRow[x * 4 + 3] = a;
        }
    }

    return true;
}

} // namespace DirectX
