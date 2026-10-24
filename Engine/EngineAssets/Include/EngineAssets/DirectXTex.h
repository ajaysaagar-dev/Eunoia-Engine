#pragma once
// ============================================================================
// DirectXTex — DirectX Texture Library for Eunoia Engine
//
// Inspired by and compatible with Microsoft DirectXTex:
// https://github.com/microsoft/directxtex
//
// Features:
// - DDS (DirectDraw Surface) format reading and writing (legacy DXT1-5, ATI1/2,
//   and DX10 extended headers for BC1-BC7, Cubemaps, Arrays, Volume textures)
// - Subresource and pitch calculation (256-byte D3D12 row alignment support)
// - Mipmap generation with linear-space sRGB filtering and normal map normalization
// - High-performance block decompression (BC1, BC2, BC3, BC4, BC5, BC7)
// - High-performance block compression (BC1, BC3, BC4, BC5)
// - Normal map generation from heightmaps (Sobel filtering)
// - Channel swizzling, packing (PBR Metallic/Roughness/AO), and Green/Y inversion
// ============================================================================

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <dxgiformat.h>
#ifdef GetClassName
#undef GetClassName
#endif

namespace DirectX {

    // ------------------------------------------------------------------------
    // Flags & Enumerations
    // ------------------------------------------------------------------------

    enum TEX_MISC_FLAG : uint32_t {
        TEX_MISC_TEXTURECUBE = 0x4L,
    };

    enum TEX_MISC_FLAG2 : uint32_t {
        TEX_MISC2_ALPHA_MODE_MASK     = 0x7L,
        TEX_MISC2_ALPHA_MODE_UNKNOWN  = 0x0L,
        TEX_MISC2_ALPHA_MODE_STRAIGHT = 0x1L,
        TEX_MISC2_ALPHA_MODE_PREMUL   = 0x2L,
        TEX_MISC2_ALPHA_MODE_OPAQUE   = 0x3L,
        TEX_MISC2_ALPHA_MODE_CUSTOM   = 0x4L,
    };

    enum TEX_DIMENSION : uint32_t {
        TEX_DIMENSION_TEXTURE1D = 2,
        TEX_DIMENSION_TEXTURE2D = 3,
        TEX_DIMENSION_TEXTURE3D = 4,
    };

    enum TEX_FILTER_FLAGS : uint32_t {
        TEX_FILTER_DEFAULT          = 0,
        TEX_FILTER_POINT            = 0x100000,
        TEX_FILTER_LINEAR           = 0x200000,
        TEX_FILTER_BOX              = 0x400000,
        TEX_FILTER_SRGB_IN          = 0x1000000,
        TEX_FILTER_SRGB_OUT         = 0x2000000,
        TEX_FILTER_SRGB             = (TEX_FILTER_SRGB_IN | TEX_FILTER_SRGB_OUT),
        TEX_FILTER_NORMAL_NORMALIZE = 0x8000000,
    };

    enum CNMAP_FLAGS : uint32_t {
        CNMAP_DEFAULT       = 0,
        CNMAP_CHANNEL_RED   = 0x1,
        CNMAP_CHANNEL_GREEN = 0x2,
        CNMAP_CHANNEL_BLUE  = 0x3,
        CNMAP_CHANNEL_ALPHA = 0x4,
        CNMAP_CHANNEL_LUM   = 0x5,
        CNMAP_INVERT_SIGN   = 0x8,
        CNMAP_INVERT_X      = 0x10,
        CNMAP_INVERT_Y      = 0x20,
    };

    // ------------------------------------------------------------------------
    // Metadata & Image Structures
    // ------------------------------------------------------------------------

    struct TexMetadata {
        size_t          width       = 0;
        size_t          height      = 0;
        size_t          depth       = 1;
        size_t          arraySize   = 1;
        size_t          mipLevels   = 1;
        uint32_t        miscFlags   = 0;
        uint32_t        miscFlags2  = 0;
        DXGI_FORMAT     format      = DXGI_FORMAT_UNKNOWN;
        TEX_DIMENSION   dimension   = TEX_DIMENSION_TEXTURE2D;

        size_t ComputeIndex(size_t mip, size_t item, size_t slice) const;
        bool IsCubemap() const { return (miscFlags & TEX_MISC_TEXTURECUBE) != 0; }
        bool IsPMAlpha() const { return (miscFlags2 & TEX_MISC2_ALPHA_MODE_MASK) == TEX_MISC2_ALPHA_MODE_PREMUL; }
    };

    struct Image {
        size_t      width       = 0;
        size_t      height      = 0;
        DXGI_FORMAT format      = DXGI_FORMAT_UNKNOWN;
        size_t      rowPitch    = 0;
        size_t      slicePitch  = 0;
        uint8_t*    pixels      = nullptr;
    };

    class ScratchImage {
    public:
        ScratchImage() noexcept = default;
        ~ScratchImage() { Release(); }

        ScratchImage(ScratchImage&& moveFrom) noexcept;
        ScratchImage& operator=(ScratchImage&& moveFrom) noexcept;

        ScratchImage(const ScratchImage& other);
        ScratchImage& operator=(const ScratchImage& other);

        bool Initialize(const TexMetadata& metadata);
        bool Initialize2D(DXGI_FORMAT fmt, size_t width, size_t height, size_t arraySize, size_t mipLevels);
        bool InitializeCube(DXGI_FORMAT fmt, size_t width, size_t height, size_t nCubes, size_t mipLevels);
        bool InitializeFromImage(const Image& srcImage);
        void Release();

        const TexMetadata& GetMetadata() const { return m_metadata; }
        const Image* GetImage(size_t mip, size_t item, size_t slice) const;
        const Image* GetImages() const { return m_images.empty() ? nullptr : m_images.data(); }
        size_t GetImageCount() const { return m_images.size(); }
        uint8_t* GetPixels() { return m_pixels.empty() ? nullptr : m_pixels.data(); }
        const uint8_t* GetPixels() const { return m_pixels.empty() ? nullptr : m_pixels.data(); }
        size_t GetPixelsSize() const { return m_pixels.size(); }
        bool IsAlphaAllOpaque() const;

    private:
        TexMetadata          m_metadata;
        std::vector<Image>   m_images;
        std::vector<uint8_t> m_pixels;
    };

    // ------------------------------------------------------------------------
    // Format Helpers
    // ------------------------------------------------------------------------

    bool IsCompressed(DXGI_FORMAT fmt);
    bool IsSRGB(DXGI_FORMAT fmt);
    DXGI_FORMAT MakeSRGB(DXGI_FORMAT fmt);
    DXGI_FORMAT MakeLinear(DXGI_FORMAT fmt);
    size_t BitsPerPixel(DXGI_FORMAT fmt);
    bool ComputePitch(DXGI_FORMAT fmt, size_t width, size_t height, size_t& rowPitch, size_t& slicePitch);
    const char* GetFormatName(DXGI_FORMAT fmt);

    // ------------------------------------------------------------------------
    // DDS Input / Output
    // ------------------------------------------------------------------------

    bool LoadFromDDSMemory(const void* pSource, size_t size, TexMetadata& metadata, ScratchImage& image);
    bool LoadFromDDSFile(const char* szFile, TexMetadata& metadata, ScratchImage& image);
    bool LoadFromDDSFile(const wchar_t* szFile, TexMetadata& metadata, ScratchImage& image);

    bool SaveToDDSMemory(const Image* images, size_t nimages, const TexMetadata& metadata, std::vector<uint8_t>& blob);
    bool SaveToDDSFile(const Image* images, size_t nimages, const TexMetadata& metadata, const char* szFile);

    // ------------------------------------------------------------------------
    // Mipmap Generation
    // ------------------------------------------------------------------------

    bool GenerateMipMaps(const Image& baseImage, uint32_t filter, size_t levels, ScratchImage& mipChain);

    // ------------------------------------------------------------------------
    // Compression & Decompression
    // ------------------------------------------------------------------------

    bool Decompress(const Image& cImage, DXGI_FORMAT format, ScratchImage& image);
    bool Compress(const Image& srcImage, DXGI_FORMAT format, ScratchImage& image);
    bool Compress(const Image* srcImages, size_t nimages, const TexMetadata& metadata, DXGI_FORMAT format, ScratchImage& image);

    // ------------------------------------------------------------------------
    // Normal Map Generation & Texture Processing
    // ------------------------------------------------------------------------

    bool ComputeNormalMap(const Image& srcImage, uint32_t flags, float amplitude, DXGI_FORMAT format, ScratchImage& normalMap);
    bool InvertGreenChannel(Image& img);
    bool PackPbrChannels(const Image* roughnessImg, int roughCh,
                         const Image* metallicImg,  int metalCh,
                         const Image* aoImg,        int aoCh,
                         ScratchImage& packedOut);

} // namespace DirectX
