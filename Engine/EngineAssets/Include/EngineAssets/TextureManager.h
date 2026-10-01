#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <glm/glm.hpp>
#include <EngineAssets/DirectXTex.h>

extern "C" {
    unsigned char *stbi_load(char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);
    void stbi_image_free(void *retval_from_stbi_load);
}

struct CachedTexture {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<uint8_t> data;
    glm::vec3 averageColor{0.5f, 0.5f, 0.5f};
    bool valid = false;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    size_t mipLevels = 1;
    DirectX::ScratchImage scratchImage;
    bool isNormalMap = false;

    // Fast point/bilinear sample at wrapped UV
    glm::vec4 Sample(float u, float v) const {
        if (!valid || data.empty() || width <= 0 || height <= 0) {
            return glm::vec4(averageColor, 1.0f);
        }
        // Repeat wrapping
        u = u - std::floor(u);
        v = v - std::floor(v);
        // Direct3D / standard UV orientation
        v = 1.0f - v;

        int x = (int)(u * (width - 1));
        int y = (int)(v * (height - 1));
        if (x < 0) x = 0; else if (x >= width) x = width - 1;
        if (y < 0) y = 0; else if (y >= height) y = height - 1;

        int idx = (y * width + x) * channels;
        float r = data[idx + 0] / 255.0f;
        float g = (channels > 1) ? (data[idx + 1] / 255.0f) : r;
        float b = (channels > 2) ? (data[idx + 2] / 255.0f) : r;
        float a = (channels > 3) ? (data[idx + 3] / 255.0f) : 1.0f;
        return glm::vec4(r, g, b, a);
    }
};

enum class TextureQuality : int {
    None = 0,    // Native (unconstrained realtime resolution)
    Low = 1,     // 256
    Medium = 2,  // 512
    High = 3,    // 1024
    Ultra = 4,   // 2048
    Max = 5      // 4096
};

inline int GetMaxResolutionForQuality(TextureQuality quality) {
    switch (quality) {
        case TextureQuality::None:   return 0; // 0 = unconstrained native quality
        case TextureQuality::Low:    return 256;
        case TextureQuality::Medium: return 512;
        case TextureQuality::High:   return 1024;
        case TextureQuality::Ultra:  return 2048;
        case TextureQuality::Max:    return 4096;
        default: return 0;
    }
}

inline const char* GetTextureQualityName(TextureQuality quality) {
    switch (quality) {
        case TextureQuality::None:   return "None (Native)";
        case TextureQuality::Low:    return "Low (256)";
        case TextureQuality::Medium: return "Medium (512)";
        case TextureQuality::High:   return "High (1024)";
        case TextureQuality::Ultra:  return "Ultra (2048)";
        case TextureQuality::Max:    return "Max (4096)";
        default: return "None (Native)";
    }
}

// Calculates D3D12 ResourceMinLODClamp for Shader Resource Views.
// If maxAllowedResolution <= 0 (TextureQuality::None), returns 0.0f (uses full native realtime quality).
// If texture's native resolution is <= maxAllowedResolution (e.g. 2K texture when setting is 4K), returns 0.0f (uses full native resolution).
// If texture's native resolution is > maxAllowedResolution (e.g. 4K texture when setting is 2K), clamps to the appropriate mip level.
inline float CalculateTextureMinLOD(int nativeWidth, int nativeHeight, int maxAllowedResolution) {
    if (maxAllowedResolution <= 0) {
        return 0.0f; // None: unconstrained full realtime quality
    }
    int maxDim = std::max(nativeWidth, nativeHeight);
    if (maxDim <= maxAllowedResolution || maxDim <= 0) {
        return 0.0f;
    }
    float ratio = (float)maxDim / (float)maxAllowedResolution;
    return std::max(0.0f, std::log2(ratio));
}

class TextureManager {
public:
    using TextureQualityCallback = std::function<void(TextureQuality quality, int maxRes)>;

private:
    std::unordered_map<std::string, CachedTexture> m_cache;
    std::unordered_map<std::string, std::string> m_resolvedPathCache;
    std::filesystem::path m_projectRoot;
    TextureQuality m_textureQuality = TextureQuality::None;
    int m_maxTextureResolution = 0;
    std::unordered_map<std::string, TextureQualityCallback> m_qualityCallbacks;

    TextureManager() {
        m_projectRoot = "C:\\Projects\\Eunoia-Engine\\Projects\\test";
        m_textureQuality = TextureQuality::None;
        m_maxTextureResolution = 0;
    }

public:
    static TextureManager& Get() {
        static TextureManager instance;
        return instance;
    }

    void SetProjectRoot(const std::filesystem::path& root) {
        if (m_projectRoot != root) {
            m_projectRoot = root;
            m_resolvedPathCache.clear();
        }
    }

    void ClearCache() {
        m_cache.clear();
        m_resolvedPathCache.clear();
    }

    TextureQuality GetTextureQuality() const { return m_textureQuality; }
    int GetMaxTextureResolution() const { return m_maxTextureResolution; }

    void SetTextureQuality(TextureQuality quality) {
        if (m_textureQuality != quality) {
            m_textureQuality = quality;
            m_maxTextureResolution = GetMaxResolutionForQuality(quality);
            for (const auto& kv : m_qualityCallbacks) {
                if (kv.second) kv.second(quality, m_maxTextureResolution);
            }
        }
    }

    void SetTextureQualitySilent(TextureQuality quality) {
        m_textureQuality = quality;
        m_maxTextureResolution = GetMaxResolutionForQuality(quality);
    }

    void RegisterQualityChangedCallback(const std::string& id, TextureQualityCallback cb) {
        m_qualityCallbacks[id] = cb;
    }

    void UnregisterQualityChangedCallback(const std::string& id) {
        m_qualityCallbacks.erase(id);
    }

    void InvalidateTexture(const std::string& name) {
        if (name.empty()) return;
        std::string resolved = ResolvePath(name);
        m_resolvedPathCache.erase(name);
        if (!resolved.empty()) {
            m_cache.erase(resolved + "@color");
            m_cache.erase(resolved + "@normal");
            m_resolvedPathCache.erase(resolved);
        }
    }

    std::string ResolvePath(const std::string& name) {
        if (name.empty() || name == "none") return "";
        auto itCache = m_resolvedPathCache.find(name);
        if (itCache != m_resolvedPathCache.end()) {
            return itCache->second;
        }

        std::string result = "";
        std::error_code ec;

        // 1. Direct absolute or relative path
        if (std::filesystem::exists(name, ec)) {
            result = std::filesystem::canonical(name, ec).string();
            if (result.empty()) result = name;
        }
        // 2. Relative to project root
        else if (!m_projectRoot.empty() && std::filesystem::exists(m_projectRoot / name, ec)) {
            result = (m_projectRoot / name).string();
        }
        // 3. Under Materials/textures/
        else if (!m_projectRoot.empty() && std::filesystem::exists(m_projectRoot / "Materials" / "textures" / name, ec)) {
            result = (m_projectRoot / "Materials" / "textures" / name).string();
        }
        // 4. Under Materials/
        else if (!m_projectRoot.empty() && std::filesystem::exists(m_projectRoot / "Materials" / name, ec)) {
            result = (m_projectRoot / "Materials" / name).string();
        }
        // 5. Recursive search matching filename inside project root
        else if (!m_projectRoot.empty() && std::filesystem::exists(m_projectRoot, ec) && std::filesystem::is_directory(m_projectRoot, ec)) {
            try {
                std::string targetFilename = std::filesystem::path(name).filename().string();
                auto options = std::filesystem::directory_options::skip_permission_denied;
                for (auto it = std::filesystem::recursive_directory_iterator(m_projectRoot, options, ec);
                     !ec && it != std::filesystem::recursive_directory_iterator();
                     it.increment(ec)) {
                    if (!it->is_directory(ec) && it->path().filename().string() == targetFilename) {
                        result = it->path().string();
                        break;
                    }
                }
            } catch (...) {
                // Ignore filesystem errors
            }
        }

        // Cache the result (even if empty, avoiding repeated failed disk searches every frame)
        m_resolvedPathCache[name] = result;
        return result;
    }

    CachedTexture* GetTexture(const std::string& name, bool isNormalMap = false) {
        if (name.empty() || name == "none") return nullptr;
        std::string resolved = ResolvePath(name);
        if (resolved.empty()) return nullptr;

        std::string cacheKey = resolved + (isNormalMap ? "@normal" : "@color");
        auto it = m_cache.find(cacheKey);
        if (it != m_cache.end()) {
            return &it->second;
        }

        CachedTexture tex;
        tex.isNormalMap = isNormalMap;

        // Check if file is DDS format
        std::string ext = std::filesystem::path(resolved).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".dds") {
            DirectX::TexMetadata metadata;
            DirectX::ScratchImage scratch;
            if (DirectX::LoadFromDDSFile(resolved.c_str(), metadata, scratch)) {
                tex.width = static_cast<int>(metadata.width);
                tex.height = static_cast<int>(metadata.height);
                tex.channels = 4;
                tex.format = metadata.format;
                tex.mipLevels = metadata.mipLevels;
                tex.valid = true;

                // If compressed, decompress mip 0 for CPU-side sampling/averageColor
                if (DirectX::IsCompressed(metadata.format)) {
                    const DirectX::Image* mip0 = scratch.GetImage(0, 0, 0);
                    if (mip0) {
                        DirectX::ScratchImage decomp;
                        if (DirectX::Decompress(*mip0, DXGI_FORMAT_R8G8B8A8_UNORM, decomp)) {
                            const DirectX::Image* dImg = decomp.GetImage(0, 0, 0);
                            tex.data.assign(dImg->pixels, dImg->pixels + dImg->slicePitch);
                        }
                    }
                } else if (metadata.format == DXGI_FORMAT_R8G8B8A8_UNORM ||
                           metadata.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                           metadata.format == DXGI_FORMAT_B8G8R8A8_UNORM) {
                    const DirectX::Image* mip0 = scratch.GetImage(0, 0, 0);
                    if (mip0) {
                        tex.data.assign(mip0->pixels, mip0->pixels + (tex.width * tex.height * 4));
                    }
                }

                // Compute averageColor if data available
                if (!tex.data.empty()) {
                    int w = tex.width;
                    int h = tex.height;
                    int stepX = std::max(1, w / 32);
                    int stepY = std::max(1, h / 32);
                    double sumR = 0, sumG = 0, sumB = 0;
                    int sampleCount = 0;
                    for (int y = 0; y < h; y += stepY) {
                        for (int x = 0; x < w; x += stepX) {
                            int idx = (y * w + x) * 4;
                            sumR += tex.data[idx + 0];
                            sumG += tex.data[idx + 1];
                            sumB += tex.data[idx + 2];
                            sampleCount++;
                        }
                    }
                    if (sampleCount > 0) {
                        tex.averageColor = glm::vec3(sumR / sampleCount / 255.0, sumG / sampleCount / 255.0, sumB / sampleCount / 255.0);
                    }
                }

                tex.scratchImage = std::move(scratch);
                m_cache[cacheKey] = std::move(tex);
                return &m_cache[cacheKey];
            }
        }

        // Standard 2D image (PNG, JPG, BMP, TGA) via stb_image + DirectXTex Mipmap generation
        int w = 0, h = 0, comp = 0;
        unsigned char* pixels = stbi_load(resolved.c_str(), &w, &h, &comp, 4);
        if (pixels && w > 0 && h > 0) {
            tex.channels = 4;
            tex.valid = true;
            tex.width = w;
            tex.height = h;
            tex.format = DXGI_FORMAT_R8G8B8A8_UNORM;
            tex.data.assign(pixels, pixels + (w * h * 4));

            // Fast compute averageColor using 32x32 point stride
            int stepX = std::max(1, w / 32);
            int stepY = std::max(1, h / 32);
            double sumR = 0, sumG = 0, sumB = 0;
            int sampleCount = 0;
            for (int y = 0; y < h; y += stepY) {
                for (int x = 0; x < w; x += stepX) {
                    int idx = (y * w + x) * 4;
                    sumR += pixels[idx + 0];
                    sumG += pixels[idx + 1];
                    sumB += pixels[idx + 2];
                    sampleCount++;
                }
            }
            if (sampleCount > 0) {
                tex.averageColor = glm::vec3(sumR / sampleCount / 255.0, sumG / sampleCount / 255.0, sumB / sampleCount / 255.0);
            }

            // Generate full mipmap chain using DirectXTex
            DirectX::Image baseImage;
            baseImage.width = static_cast<size_t>(w);
            baseImage.height = static_cast<size_t>(h);
            baseImage.format = DXGI_FORMAT_R8G8B8A8_UNORM;
            baseImage.rowPitch = static_cast<size_t>(w * 4);
            baseImage.slicePitch = static_cast<size_t>(w * h * 4);
            baseImage.pixels = pixels;

            uint32_t filterFlags = DirectX::TEX_FILTER_BOX;
            if (isNormalMap) {
                filterFlags |= DirectX::TEX_FILTER_NORMAL_NORMALIZE;
            } else {
                filterFlags |= DirectX::TEX_FILTER_SRGB;
            }

            DirectX::ScratchImage mipChain;
            if (DirectX::GenerateMipMaps(baseImage, filterFlags, 0, mipChain)) {
                tex.mipLevels = mipChain.GetMetadata().mipLevels;
                tex.scratchImage = std::move(mipChain);
            } else {
                tex.mipLevels = 1;
            }

            stbi_image_free(pixels);
        }

        m_cache[cacheKey] = std::move(tex);
        return &m_cache[cacheKey];
    }
};
