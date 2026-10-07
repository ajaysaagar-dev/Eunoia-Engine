#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <mutex>
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

enum class TextureUsageSlot : int {
    BaseColor = 0,
    Normal = 1,
    Roughness = 2,
    Metallic = 3,
    AO = 4,
    Opacity = 5,
    Emission = 6,
    Cubemap = 7,
    Generic = 8
};

inline const char* GetTextureUsageSlotName(TextureUsageSlot slot) {
    switch (slot) {
        case TextureUsageSlot::BaseColor: return "BaseColor";
        case TextureUsageSlot::Normal:    return "Normal";
        case TextureUsageSlot::Roughness: return "Roughness";
        case TextureUsageSlot::Metallic:  return "Metallic";
        case TextureUsageSlot::AO:        return "AO";
        case TextureUsageSlot::Opacity:   return "Opacity";
        case TextureUsageSlot::Emission:  return "Emission";
        case TextureUsageSlot::Cubemap:   return "Cubemap";
        default:                          return "Generic";
    }
}

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
    mutable std::mutex m_cacheMutex;

    TextureManager() {
        m_projectRoot = "";
        m_textureQuality = TextureQuality::None;
        m_maxTextureResolution = 0;
    }

public:
    static TextureManager& Get() {
        static TextureManager instance;
        return instance;
    }

    static float CalculateTextureMinLOD(int nativeWidth, int nativeHeight, int maxAllowedResolution) {
        return ::CalculateTextureMinLOD(nativeWidth, nativeHeight, maxAllowedResolution);
    }

    void SetProjectRoot(const std::filesystem::path& root) {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (m_projectRoot != root) {
            m_projectRoot = root;
            m_resolvedPathCache.clear();
        }
    }

    void ClearCache() {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
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
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_qualityCallbacks[id] = cb;
    }

    void UnregisterQualityChangedCallback(const std::string& id) {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_qualityCallbacks.erase(id);
    }

    void InvalidateTexture(const std::string& name) {
        if (name.empty()) return;
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        std::string resolved = ResolvePathInternal(name);
        m_resolvedPathCache.erase(name);
        if (!resolved.empty()) {
            m_cache.erase(resolved + "@color");
            m_cache.erase(resolved + "@normal");
            for (int s = 0; s < 10; ++s) {
                m_cache.erase(resolved + "@slot=" + std::to_string(s));
            }
            m_resolvedPathCache.erase(resolved);
        }
    }

    std::string ResolvePath(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        return ResolvePathInternal(name);
    }

private:
    std::string ResolvePathInternal(const std::string& name) {
        std::string cleanName = name;
        while (!cleanName.empty() && (cleanName.front() == ' ' || cleanName.front() == '\t' || cleanName.front() == '\"' || cleanName.front() == '\'')) {
            cleanName.erase(cleanName.begin());
        }
        while (!cleanName.empty() && (cleanName.back() == ' ' || cleanName.back() == '\t' || cleanName.back() == '\"' || cleanName.back() == '\'')) {
            cleanName.pop_back();
        }
        if (cleanName.empty() || cleanName == "none") return "";

        auto itCache = m_resolvedPathCache.find(cleanName);
        if (itCache != m_resolvedPathCache.end() && !itCache->second.empty()) {
            return itCache->second;
        }

        std::string result = "";
        std::error_code ec;

        // 1. Direct absolute or relative path check
        if (std::filesystem::exists(cleanName, ec)) {
            result = std::filesystem::canonical(cleanName, ec).string();
            if (result.empty()) result = cleanName;
        }

        // 2. Build comprehensive search roots
        if (result.empty()) {
            std::vector<std::filesystem::path> searchRoots;
            auto addRoot = [&](const std::filesystem::path& p) {
                if (!p.empty() && std::filesystem::exists(p, ec)) {
                    searchRoots.push_back(p);
                }
            };

            if (!m_projectRoot.empty()) {
                addRoot(m_projectRoot);
                std::filesystem::path parentDir = m_projectRoot.parent_path();
                if (m_projectRoot.filename() == "Content") {
                    addRoot(parentDir);
                    addRoot(m_projectRoot / "Textures");
                    addRoot(m_projectRoot / "Materials");
                    addRoot(m_projectRoot / "Materials" / "textures");
                    addRoot(m_projectRoot / "ImportedTextures");
                    addRoot(m_projectRoot / "Meshes");
                    addRoot(m_projectRoot / "Models");
                    addRoot(parentDir / "Content");
                    addRoot(parentDir / "Textures");
                    addRoot(parentDir / "Materials");
                } else {
                    addRoot(m_projectRoot / "Content");
                    addRoot(m_projectRoot / "Content" / "Textures");
                    addRoot(m_projectRoot / "Content" / "Materials");
                    addRoot(m_projectRoot / "Content" / "Materials" / "textures");
                    addRoot(m_projectRoot / "Content" / "ImportedTextures");
                    addRoot(m_projectRoot / "Content" / "Meshes");
                    addRoot(m_projectRoot / "Content" / "Models");
                    addRoot(m_projectRoot / "Textures");
                    addRoot(m_projectRoot / "Materials");
                    addRoot(m_projectRoot / "ImportedTextures");
                }
            }

            std::filesystem::path cwd = std::filesystem::current_path();
            addRoot(cwd);
            addRoot(cwd / "Content");
            addRoot(cwd / "Projects");
            addRoot(cwd / "Resources");

            // Subpath test
            for (const auto& root : searchRoots) {
                std::filesystem::path cand = root / cleanName;
                if (std::filesystem::exists(cand, ec) && !std::filesystem::is_directory(cand, ec)) {
                    result = std::filesystem::canonical(cand, ec).string();
                    if (result.empty()) result = cand.string();
                    break;
                }
            }

            // Fallback: bare filename in search roots
            if (result.empty()) {
                std::string targetFilename = std::filesystem::path(cleanName).filename().string();
                if (!targetFilename.empty()) {
                    for (const auto& root : searchRoots) {
                        std::filesystem::path cand = root / targetFilename;
                        if (std::filesystem::exists(cand, ec) && !std::filesystem::is_directory(cand, ec)) {
                            result = std::filesystem::canonical(cand, ec).string();
                            if (result.empty()) result = cand.string();
                            break;
                        }
                    }
                }
            }

            // Recursive search inside project root & parent directory
            if (result.empty() && !m_projectRoot.empty()) {
                std::string targetFilename = std::filesystem::path(cleanName).filename().string();
                if (!targetFilename.empty()) {
                    std::vector<std::filesystem::path> recurseDirs = { m_projectRoot };
                    if (m_projectRoot.filename() == "Content" && !m_projectRoot.parent_path().empty()) {
                        recurseDirs.push_back(m_projectRoot.parent_path());
                    }
                    for (const auto& dir : recurseDirs) {
                        if (std::filesystem::exists(dir, ec) && std::filesystem::is_directory(dir, ec)) {
                            try {
                                auto options = std::filesystem::directory_options::skip_permission_denied;
                                for (auto it = std::filesystem::recursive_directory_iterator(dir, options, ec);
                                     !ec && it != std::filesystem::recursive_directory_iterator();
                                     it.increment(ec)) {
                                    if (!it->is_directory(ec) && it->path().filename().string() == targetFilename) {
                                        result = it->path().string();
                                        break;
                                    }
                                }
                            } catch (...) {
                                // Ignore filesystem permissions/access errors
                            }
                        }
                        if (!result.empty()) break;
                    }
                }
            }
        }

        if (!result.empty()) {
            m_resolvedPathCache[cleanName] = result;
            m_resolvedPathCache[name] = result;
        }
        return result;
    }

public:
    CachedTexture* GetTexture(const std::string& name, TextureUsageSlot slot = TextureUsageSlot::BaseColor) {
        if (name.empty() || name == "none") return nullptr;
        std::string resolved = ResolvePath(name);
        if (resolved.empty()) return nullptr;

        std::string cacheKey = resolved + "@slot=" + std::to_string(static_cast<int>(slot));
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            auto it = m_cache.find(cacheKey);
            if (it != m_cache.end()) {
                return &it->second;
            }
        }

        CachedTexture tex;
        tex.isNormalMap = (slot == TextureUsageSlot::Normal);

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
                std::lock_guard<std::mutex> lock(m_cacheMutex);
                m_cache[cacheKey] = std::move(tex);
                return &m_cache[cacheKey];
            }
        }

        // Standard 2D image (PNG, JPG, BMP, TGA, HDR, PSD) via stb_image + DirectXTex Mipmap generation
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
            if (slot == TextureUsageSlot::Normal) {
                filterFlags |= DirectX::TEX_FILTER_NORMAL_NORMALIZE;
            } else if (slot == TextureUsageSlot::BaseColor || slot == TextureUsageSlot::Emission) {
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

        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cache[cacheKey] = std::move(tex);
        return &m_cache[cacheKey];
    }

    CachedTexture* GetTexture(const std::string& name, bool isNormalMap) {
        return GetTexture(name, isNormalMap ? TextureUsageSlot::Normal : TextureUsageSlot::BaseColor);
    }

    bool IsTextureLoaded(const std::string& name, TextureUsageSlot slot = TextureUsageSlot::BaseColor) const {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto itCache = m_resolvedPathCache.find(name);
        std::string resolved = (itCache != m_resolvedPathCache.end()) ? itCache->second : name;
        std::string cacheKey = resolved + "@slot=" + std::to_string(static_cast<int>(slot));
        return m_cache.find(cacheKey) != m_cache.end();
    }
};
