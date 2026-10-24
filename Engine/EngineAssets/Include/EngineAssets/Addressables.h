#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <filesystem>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <EngineAssets/AssetRegistry.h>
#include <EngineAssets/AssetManager.h>
#include <EngineAssets/TextureManager.h>
#include <EngineAssets/MeshImporter.h>

namespace Eunoia {

class Addressables {
public:
    struct PreloadStats {
        size_t totalModels = 0;
        size_t loadedModels = 0;
        size_t totalTextures = 0;
        size_t loadedTextures = 0;
        float elapsedSeconds = 0.0f;
    };

    using PreloadProgressFn = std::function<void(float progress, const std::string& item, const std::string& category)>;

    // Resolve any address (virtual path /Game/..., AssetID GUID, relative disk path, or file name)
    // to an absolute or canonical disk path. Returns the resolved path, or the original address if unresolved.
    static std::string ResolveAddress(const std::string& address) {
        if (address.empty() || address == "none" || address == "None") return "";

        // Clean leading/trailing quotes and spaces
        std::string clean = address;
        while (!clean.empty() && (clean.front() == ' ' || clean.front() == '\t' || clean.front() == '\"' || clean.front() == '\'')) {
            clean.erase(clean.begin());
        }
        while (!clean.empty() && (clean.back() == ' ' || clean.back() == '\t' || clean.back() == '\"' || clean.back() == '\'')) {
            clean.pop_back();
        }
        if (clean.empty() || clean == "none" || clean == "None") return "";

        std::error_code ec;

        // 1. Check with AssetManager first (handles virtual dummy references and cooked paths)
        std::filesystem::path cookedCand = AssetManager::Get().ResolvePhysicalOrCookedPath(clean);
        if (!cookedCand.empty() && std::filesystem::exists(cookedCand, ec) && std::filesystem::file_size(cookedCand, ec) > 0) {
            auto can = std::filesystem::weakly_canonical(cookedCand, ec).string();
            return can.empty() ? cookedCand.string() : can;
        }

        // 2. Direct absolute or relative filesystem path check
        if (std::filesystem::exists(clean, ec) && std::filesystem::file_size(clean, ec) > 0) {
            auto can = std::filesystem::weakly_canonical(clean, ec).string();
            return can.empty() ? clean : can;
        }

        // 3. Check virtual path in AssetRegistry (e.g. /Game/...)
        if (clean.rfind("/Game/", 0) == 0 || clean.rfind("Game/", 0) == 0) {
            std::string vPath = clean;
            if (vPath.front() != '/') vPath = "/" + vPath;
            const AssetMetadata* meta = AssetRegistry::Get().FindByVirtualPath(vPath);
            if (meta) {
                if (!meta->cookedPath.empty()) {
                    std::filesystem::path cp = AssetManager::Get().ResolvePhysicalOrCookedPath(meta->cookedPath);
                    if (std::filesystem::exists(cp, ec) && std::filesystem::file_size(cp, ec) > 0) return cp.string();
                }
                if (!meta->sourcePath.empty()) {
                    if (std::filesystem::exists(meta->sourcePath, ec) && std::filesystem::file_size(meta->sourcePath, ec) > 0) {
                        return meta->sourcePath;
                    }
                    const auto& root = AssetRegistry::Get().GetProjectRoot();
                    if (!root.empty()) {
                        auto p = root / meta->sourcePath;
                        if (std::filesystem::exists(p, ec) && std::filesystem::file_size(p, ec) > 0) return p.string();
                    }
                }
            }
        }

        // 4. AssetID GUID check (length >= 32 with hyphens)
        if (clean.length() >= 32 && clean.find('-') != std::string::npos) {
            AssetID id = AssetID::FromString(clean);
            if (id.IsValid()) {
                const AssetMetadata* meta = AssetRegistry::Get().FindById(id);
                if (meta) {
                    if (!meta->cookedPath.empty()) {
                        std::filesystem::path cp = AssetManager::Get().ResolvePhysicalOrCookedPath(meta->cookedPath);
                        if (std::filesystem::exists(cp, ec) && std::filesystem::file_size(cp, ec) > 0) return cp.string();
                    }
                    if (!meta->sourcePath.empty()) {
                        if (std::filesystem::exists(meta->sourcePath, ec) && std::filesystem::file_size(meta->sourcePath, ec) > 0) return meta->sourcePath;
                        const auto& root = AssetRegistry::Get().GetProjectRoot();
                        if (!root.empty()) {
                            auto p = root / meta->sourcePath;
                            if (std::filesystem::exists(p, ec) && std::filesystem::file_size(p, ec) > 0) return p.string();
                        }
                    }
                }
            }
        }

        // 5. Try ProjectRoot relative paths
        const auto& projRoot = AssetRegistry::Get().GetProjectRoot();
        if (!projRoot.empty()) {
            auto cand1 = projRoot / clean;
            if (std::filesystem::exists(cand1, ec) && std::filesystem::file_size(cand1, ec) > 0) {
                return std::filesystem::weakly_canonical(cand1, ec).string();
            }

            auto cand2 = projRoot / "Content" / clean;
            if (std::filesystem::exists(cand2, ec) && std::filesystem::file_size(cand2, ec) > 0) {
                return std::filesystem::weakly_canonical(cand2, ec).string();
            }

            auto candCooked = projRoot / "Cooked" / "Content" / clean;
            if (std::filesystem::exists(candCooked, ec) && std::filesystem::file_size(candCooked, ec) > 0) {
                return std::filesystem::weakly_canonical(candCooked, ec).string();
            }
        }

        // 6. Delegate to TextureManager's search path resolver
        std::string tmResolved = TextureManager::Get().ResolvePath(clean);
        if (!tmResolved.empty() && std::filesystem::exists(tmResolved, ec) && std::filesystem::file_size(tmResolved, ec) > 0) {
            return tmResolved;
        }

        // 7. AssetRegistry FindByNameOrPath fuzzy lookup
        const AssetMetadata* metaQuery = AssetRegistry::Get().FindByNameOrPath(clean);
        if (metaQuery) {
            if (!metaQuery->cookedPath.empty()) {
                std::filesystem::path cp = AssetManager::Get().ResolvePhysicalOrCookedPath(metaQuery->cookedPath);
                if (std::filesystem::exists(cp, ec) && std::filesystem::file_size(cp, ec) > 0) return cp.string();
            }
            if (!metaQuery->sourcePath.empty()) {
                if (std::filesystem::exists(metaQuery->sourcePath, ec) && std::filesystem::file_size(metaQuery->sourcePath, ec) > 0) return metaQuery->sourcePath;
                if (!projRoot.empty()) {
                    auto p = projRoot / metaQuery->sourcePath;
                    if (std::filesystem::exists(p, ec) && std::filesystem::file_size(p, ec) > 0) return p.string();
                }
            }
        }

        // 8. If clean has filename only, try searching common Content and Cooked subdirectories
        if (!projRoot.empty()) {
            std::string filename = std::filesystem::path(clean).filename().string();
            const std::vector<std::filesystem::path> subdirs = {
                projRoot / "Cooked" / "Content",
                projRoot / "Cooked" / "Content" / "Models",
                projRoot / "Cooked" / "Content" / "Meshes",
                projRoot / "Cooked" / "Content" / "Textures",
                projRoot / "Cooked" / "Content" / "Materials",
                projRoot / "Cooked" / "Cache" / "Textures",
                projRoot / "Content",
                projRoot / "Content" / "Models",
                projRoot / "Content" / "Meshes",
                projRoot / "Content" / "Textures",
                projRoot / "Content" / "Materials",
                projRoot / "Content" / "Environment"
            };
            for (const auto& d : subdirs) {
                auto candidate = d / filename;
                if (std::filesystem::exists(candidate, ec) && std::filesystem::file_size(candidate, ec) > 0) {
                    return candidate.string();
                }
            }
        }

        return clean;
    }

    // Resolve a disk path back to virtual /Game/... address
    static std::string ResolveToVirtualAddress(const std::string& diskPath) {
        if (diskPath.empty()) return "";
        std::string resolved = ResolveAddress(diskPath);
        const auto& all = AssetRegistry::Get().GetAllAssets();
        for (const auto& kv : all) {
            if (kv.second.sourcePath == diskPath || kv.second.sourcePath == resolved) {
                return kv.second.virtualPath;
            }
        }
        return diskPath;
    }

    // Check if an address exists
    static bool Exists(const std::string& address) {
        if (address.empty()) return false;
        std::string res = ResolveAddress(address);
        std::error_code ec;
        return !res.empty() && std::filesystem::exists(res, ec);
    }

    // Asynchronously or multi-threaded preloads and decodes models and textures in parallel.
    // Models are parsed and cached into MeshImporter.
    // Textures are decoded and mipmapped into TextureManager.
    // cancelSignal can be checked to abort early if loading was cancelled.
    static PreloadStats PreloadLevelAssetsAsync(
        const std::vector<std::string>& modelAddresses,
        const std::vector<std::pair<std::string, TextureUsageSlot>>& textureAddresses,
        PreloadProgressFn onProgress = nullptr,
        const std::atomic<bool>* cancelSignal = nullptr
    ) {
        auto startTime = std::chrono::high_resolution_clock::now();
        PreloadStats stats;

        // 1. Deduplicate unique models
        std::vector<std::string> uniqueModels;
        {
            std::unordered_set<std::string> seenModels;
            for (const auto& addr : modelAddresses) {
                if (addr.empty() || addr == "none" || addr == "None") continue;
                std::string resolved = ResolveAddress(addr);
                if (resolved.empty()) resolved = addr;
                if (seenModels.insert(resolved).second) {
                    uniqueModels.push_back(resolved);
                }
            }
        }
        stats.totalModels = uniqueModels.size();

        // 2. Deduplicate unique textures
        std::vector<std::pair<std::string, TextureUsageSlot>> uniqueTextures;
        {
            std::unordered_set<std::string> seenTextures;
            for (const auto& pair : textureAddresses) {
                if (pair.first.empty() || pair.first == "none" || pair.first == "None") continue;
                std::string resolved = ResolveAddress(pair.first);
                if (resolved.empty()) resolved = pair.first;
                std::string key = resolved + "@" + std::to_string(static_cast<int>(pair.second));
                if (seenTextures.insert(key).second) {
                    uniqueTextures.emplace_back(resolved, pair.second);
                }
            }
        }

        unsigned int hw = std::thread::hardware_concurrency();
        size_t numWorkers = std::clamp(hw == 0 ? 4u : hw, 2u, 8u);

        std::atomic<size_t> loadedModelsCount{0};
        std::atomic<size_t> loadedTexturesCount{0};

        // Phase 1: Parallel asynchronous model preloading & parsing
        if (!uniqueModels.empty()) {
            std::atomic<size_t> nextModelIdx{0};
            std::mutex extraTexMutex;
            std::vector<std::pair<std::string, TextureUsageSlot>> extraTextures;

            auto modelWorker = [&]() {
                while (true) {
                    if (cancelSignal && cancelSignal->load()) break;
                    size_t idx = nextModelIdx.fetch_add(1);
                    if (idx >= uniqueModels.size()) break;

                    const auto& path = uniqueModels[idx];
                    ImportedModel model = MeshImporter::Load(path);
                    if (model.valid) {
                        loadedModelsCount++;
                        // Discover material textures referenced by imported model
                        std::vector<std::pair<std::string, TextureUsageSlot>> foundTex;
                        for (const auto& mesh : model.meshes) {
                            const auto& mat = mesh.material;
                            if (!mat.baseColorTexture.empty()) foundTex.emplace_back(mat.baseColorTexture, TextureUsageSlot::BaseColor);
                            if (!mat.normalTexture.empty())    foundTex.emplace_back(mat.normalTexture,    TextureUsageSlot::Normal);
                            if (!mat.roughnessTexture.empty()) foundTex.emplace_back(mat.roughnessTexture, TextureUsageSlot::Roughness);
                            if (!mat.metallicTexture.empty())  foundTex.emplace_back(mat.metallicTexture,  TextureUsageSlot::Metallic);
                            if (!mat.aoTexture.empty())        foundTex.emplace_back(mat.aoTexture,        TextureUsageSlot::AO);
                            if (!mat.emissionTexture.empty())  foundTex.emplace_back(mat.emissionTexture,  TextureUsageSlot::Emission);
                            if (!mat.opacityTexture.empty())   foundTex.emplace_back(mat.opacityTexture,   TextureUsageSlot::Opacity);
                        }
                        if (!foundTex.empty()) {
                            std::lock_guard<std::mutex> lk(extraTexMutex);
                            extraTextures.insert(extraTextures.end(), foundTex.begin(), foundTex.end());
                        }
                    }

                    if (onProgress) {
                        float p = (float)(idx + 1) / (float)uniqueModels.size() * 0.4f;
                        onProgress(p, path, "Model");
                    }
                }
            };

            std::vector<std::thread> workers;
            size_t workerCount = std::min(numWorkers, uniqueModels.size());
            for (size_t w = 0; w < workerCount; ++w) {
                workers.emplace_back(modelWorker);
            }
            for (auto& th : workers) {
                if (th.joinable()) th.join();
            }

            // Merge discovered model textures into uniqueTextures list
            if (!extraTextures.empty()) {
                std::unordered_set<std::string> seen;
                for (const auto& t : uniqueTextures) {
                    seen.insert(t.first + "@" + std::to_string(static_cast<int>(t.second)));
                }
                for (const auto& et : extraTextures) {
                    std::string resolved = ResolveAddress(et.first);
                    if (resolved.empty()) resolved = et.first;
                    std::string k = resolved + "@" + std::to_string(static_cast<int>(et.second));
                    if (seen.insert(k).second) {
                        uniqueTextures.emplace_back(resolved, et.second);
                    }
                }
            }
        }

        // Phase 2: Parallel asynchronous texture decoding & mipmap generation
        stats.totalTextures = uniqueTextures.size();
        if (!uniqueTextures.empty()) {
            std::atomic<size_t> nextTexIdx{0};

            auto texWorker = [&]() {
                while (true) {
                    if (cancelSignal && cancelSignal->load()) break;
                    size_t idx = nextTexIdx.fetch_add(1);
                    if (idx >= uniqueTextures.size()) break;

                    const auto& t = uniqueTextures[idx];
                    CachedTexture* ct = TextureManager::Get().GetTexture(t.first, t.second);
                    if (ct && ct->valid) {
                        loadedTexturesCount++;
                    }

                    if (onProgress) {
                        float baseP = uniqueModels.empty() ? 0.0f : 0.4f;
                        float spanP = uniqueModels.empty() ? 1.0f : 0.6f;
                        float p = baseP + spanP * ((float)(idx + 1) / (float)uniqueTextures.size());
                        onProgress(p, t.first, "Texture");
                    }
                }
            };

            std::vector<std::thread> workers;
            size_t workerCount = std::min(numWorkers, uniqueTextures.size());
            for (size_t w = 0; w < workerCount; ++w) {
                workers.emplace_back(texWorker);
            }
            for (auto& th : workers) {
                if (th.joinable()) th.join();
            }
        }

        stats.loadedModels = loadedModelsCount.load();
        stats.loadedTextures = loadedTexturesCount.load();

        auto endTime = std::chrono::high_resolution_clock::now();
        stats.elapsedSeconds = std::chrono::duration<float>(endTime - startTime).count();

        std::cout << "[Addressables] Preload complete: " << stats.loadedModels << "/" << stats.totalModels
                  << " models, " << stats.loadedTextures << "/" << stats.totalTextures
                  << " textures in " << stats.elapsedSeconds << "s\n";

        return stats;
    }
};

} // namespace Eunoia

using Addressables = Eunoia::Addressables;
