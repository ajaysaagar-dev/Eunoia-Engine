#include <EngineAssets/AssetManager.h>
#include <EngineAssets/AssetEvents.h>
#include <iostream>
#include <fstream>

AssetManager::AssetManager() {
    InitFallbackCheckerTexture();
}

void AssetManager::Initialize(const std::filesystem::path& projectRoot) {
    m_projectRoot = projectRoot;
    std::filesystem::path projBase = projectRoot;
    if (!projBase.empty() && projBase.filename() == "Content") {
        projBase = projBase.parent_path();
    }
    std::filesystem::path regDir = projBase / "Registry";
    std::filesystem::path regPath = regDir / "AssetRegistry.json";
    std::filesystem::path legacyContentRegPath = projBase / "Content" / "Registry" / "AssetRegistry.json";
    std::error_code ec;

    if (std::filesystem::exists(regPath, ec)) {
        AssetRegistry::Get().SetProjectRoot(projBase);
        AssetRegistry::Get().Load(regPath);
    } else if (std::filesystem::exists(legacyContentRegPath, ec)) {
        // Migrate legacy Content/Registry to project root Registry/
        AssetRegistry::Get().SetProjectRoot(projBase);
        AssetRegistry::Get().Load(legacyContentRegPath);
        std::filesystem::create_directories(regDir, ec);
        AssetRegistry::Get().Save(regPath);
        std::filesystem::remove_all(projBase / "Content" / "Registry", ec);
    } else {
        AssetRegistry::Get().SetProjectRoot(projBase);
        AssetRegistry::Get().ScanAndSync(projectRoot);
    }
}

void AssetManager::InitFallbackCheckerTexture() {
    const int size = 32;
    const int checkSize = 4;
    m_missingTextureFallback.width = size;
    m_missingTextureFallback.height = size;
    m_missingTextureFallback.channels = 4;
    m_missingTextureFallback.valid = true;
    m_missingTextureFallback.averageColor = glm::vec3(0.5f, 0.0f, 0.5f);
    m_missingTextureFallback.data.resize(size * size * 4);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            bool isMagenta = ((x / checkSize) + (y / checkSize)) % 2 == 0;
            int idx = (y * size + x) * 4;
            if (isMagenta) {
                m_missingTextureFallback.data[idx + 0] = 255; // R
                m_missingTextureFallback.data[idx + 1] = 0;   // G
                m_missingTextureFallback.data[idx + 2] = 255; // B
                m_missingTextureFallback.data[idx + 3] = 255; // A
            } else {
                m_missingTextureFallback.data[idx + 0] = 0;   // R
                m_missingTextureFallback.data[idx + 1] = 0;   // G
                m_missingTextureFallback.data[idx + 2] = 0;   // B
                m_missingTextureFallback.data[idx + 3] = 255; // A
            }
        }
    }
}

CachedTexture* AssetManager::GetMissingTextureFallback() {
    return &m_missingTextureFallback;
}

AssetHandle<CachedTexture> AssetManager::GetOrLoadTexture(const AssetID& id) {
    if (!id.IsValid()) {
        return AssetHandle<CachedTexture>(id, &m_missingTextureFallback);
    }

    std::lock_guard<std::mutex> lock(m_managerMutex);

    // 1. Check runtime cache
    auto it = m_loadedTextures.find(id);
    if (it != m_loadedTextures.end()) {
        return AssetHandle<CachedTexture>(id, &it->second);
    }

    // 2. Query registry metadata
    AssetMetadata* meta = AssetRegistry::Get().Find(id);
    if (!meta) {
        m_states[id] = AssetState::Missing;
        return AssetHandle<CachedTexture>(id, &m_missingTextureFallback);
    }

    // 3. Resolve disk source path
    std::filesystem::path fullPath = m_projectRoot / meta->sourcePath;
    std::error_code ec;
    if (!std::filesystem::exists(fullPath, ec)) {
        meta->isMissing = true;
        m_states[id] = AssetState::Missing;
        std::cerr << "[AssetManager] Texture missing on disk: " << meta->sourcePath << " (AssetID: " << id.ToString() << ")\n";
        return AssetHandle<CachedTexture>(id, &m_missingTextureFallback);
    }

    // 4. Load via TextureManager
    m_states[id] = AssetState::Loading;
    CachedTexture* tex = TextureManager::Get().GetTexture(fullPath.string());
    if (!tex || !tex->valid) {
        m_states[id] = AssetState::Failed;
        return AssetHandle<CachedTexture>(id, &m_missingTextureFallback);
    }

    m_loadedTextures[id] = *tex;
    m_states[id] = AssetState::Loaded;
    return AssetHandle<CachedTexture>(id, &m_loadedTextures[id]);
}

AssetHandle<CachedTexture> AssetManager::GetOrLoadTexture(const std::string& virtualPathOrDisk) {
    if (virtualPathOrDisk.empty() || virtualPathOrDisk == "none") {
        return AssetHandle<CachedTexture>(AssetID::Null(), nullptr);
    }

    // 1. Query registry
    AssetMetadata* meta = AssetRegistry::Get().FindByNameOrPath(virtualPathOrDisk);
    if (meta) {
        return GetOrLoadTexture(meta->id);
    }

    // 2. Fallback direct disk query via TextureManager
    CachedTexture* directTex = TextureManager::Get().GetTexture(virtualPathOrDisk);
    if (directTex && directTex->valid) {
        // Register ad-hoc texture metadata
        AssetMetadata adHoc;
        adHoc.id = AssetID::CreateRandom();
        adHoc.type = AssetType::Texture;
        adHoc.virtualPath = AssetPath::Normalize("/Game/Textures/" + std::filesystem::path(virtualPathOrDisk).stem().string());
        adHoc.objectName = std::filesystem::path(virtualPathOrDisk).stem().string();
        adHoc.sourcePath = virtualPathOrDisk;
        AssetRegistry::Get().RegisterAsset(adHoc);

        std::lock_guard<std::mutex> lock(m_managerMutex);
        m_loadedTextures[adHoc.id] = *directTex;
        m_states[adHoc.id] = AssetState::Loaded;
        return AssetHandle<CachedTexture>(adHoc.id, &m_loadedTextures[adHoc.id]);
    }

    return AssetHandle<CachedTexture>(AssetID::Null(), &m_missingTextureFallback);
}

void AssetManager::Unload(const AssetID& id) {
    std::lock_guard<std::mutex> lock(m_managerMutex);
    m_loadedTextures.erase(id);
    m_states[id] = AssetState::Unloaded;
}

bool AssetManager::IsLoaded(const AssetID& id) const {
    std::lock_guard<std::mutex> lock(m_managerMutex);
    return m_loadedTextures.find(id) != m_loadedTextures.end();
}

AssetState AssetManager::GetState(const AssetID& id) const {
    std::lock_guard<std::mutex> lock(m_managerMutex);
    auto it = m_states.find(id);
    if (it != m_states.end()) return it->second;
    return AssetState::Unloaded;
}

void AssetManager::Clear() {
    std::lock_guard<std::mutex> lock(m_managerMutex);
    m_loadedTextures.clear();
    m_states.clear();
}

std::filesystem::path AssetManager::ResolvePhysicalOrCookedPath(const AssetID& id) const {
    const AssetMetadata* meta = AssetRegistry::Get().GetMetadata(id);
    if (!meta) return "";
    return ResolvePhysicalOrCookedPath(meta->sourcePath);
}

std::filesystem::path AssetManager::ResolvePhysicalOrCookedPath(const std::string& virtualOrDiskPath) const {
    if (virtualOrDiskPath.empty() || virtualOrDiskPath == "none" || virtualOrDiskPath == "None") return "";
    std::filesystem::path projRoot = (m_projectRoot.filename() == "Content") ? m_projectRoot.parent_path() : m_projectRoot;
    std::filesystem::path contentDir = projRoot / "Content";
    std::filesystem::path cookedDir = projRoot / "Cooked" / "Content";
    std::filesystem::path cookedBase = projRoot / "Cooked";
    std::error_code ec;

    auto isValid = [&](const std::filesystem::path& p) -> bool {
        return std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec) && std::filesystem::file_size(p, ec) > 0;
    };

    auto checkDdsOrAlt = [&](const std::filesystem::path& p) -> std::filesystem::path {
        if (isValid(p)) return p;
        std::filesystem::path pDds = p;
        pDds.replace_extension(".dds");
        if (isValid(pDds)) return pDds;
        return {};
    };

    // 1. Direct absolute/relative check
    std::filesystem::path cand = virtualOrDiskPath;
    if (cand.is_relative()) {
        std::filesystem::path candFromContent = checkDdsOrAlt(contentDir / cand);
        if (!candFromContent.empty()) return candFromContent;

        std::filesystem::path candFromRoot = checkDdsOrAlt(projRoot / cand);
        if (!candFromRoot.empty()) return candFromRoot;
    } else {
        std::filesystem::path candValid = checkDdsOrAlt(cand);
        if (!candValid.empty()) return candValid;
    }

    // If cand is an absolute path, compute path relative to content or project root
    std::filesystem::path relFromContent;
    if (cand.is_absolute()) {
        std::string candStr = cand.string();
        std::replace(candStr.begin(), candStr.end(), '\\', '/');
        std::string contentStr = contentDir.string();
        std::replace(contentStr.begin(), contentStr.end(), '\\', '/');
        std::string projStr = projRoot.string();
        std::replace(projStr.begin(), projStr.end(), '\\', '/');

        if (candStr.rfind(contentStr, 0) == 0) {
            relFromContent = std::filesystem::relative(cand, contentDir, ec);
        } else if (candStr.rfind(projStr, 0) == 0) {
            std::filesystem::path relProj = std::filesystem::relative(cand, projRoot, ec);
            std::string relPStr = relProj.string();
            if (relPStr.rfind("Content", 0) == 0) {
                relFromContent = std::filesystem::relative(cand, contentDir, ec);
            } else {
                std::filesystem::path cookedCand = checkDdsOrAlt(cookedBase / relProj);
                if (!cookedCand.empty()) return cookedCand;
            }
        }
    } else {
        std::string s = cand.string();
        std::replace(s.begin(), s.end(), '\\', '/');
        if (s.rfind("/Game/", 0) == 0) s = s.substr(6);
        else if (s.rfind("Content/", 0) == 0) s = s.substr(8);
        while (!s.empty() && (s.front() == '/' || s.front() == '\\')) s.erase(s.begin());
        relFromContent = s;
    }

    if (!relFromContent.empty()) {
        std::filesystem::path directCooked = checkDdsOrAlt(cookedDir / relFromContent);
        if (!directCooked.empty()) return directCooked;
    }

    // 2. Check metadata registry for cookedPath, id, or sourcePath
    const AssetMetadata* meta = AssetRegistry::Get().FindByPath(virtualOrDiskPath);
    if (!meta) meta = AssetRegistry::Get().FindByNameOrPath(virtualOrDiskPath);
    if (!meta) {
        AssetID aid = AssetID::FromString(virtualOrDiskPath);
        if (aid.IsValid()) meta = AssetRegistry::Get().FindById(aid);
    }
    if (meta) {
        if (!meta->cookedPath.empty()) {
            std::filesystem::path candCooked = checkDdsOrAlt(cookedBase / meta->cookedPath);
            if (!candCooked.empty()) return candCooked;
            std::filesystem::path candCooked2 = checkDdsOrAlt(projRoot / meta->cookedPath);
            if (!candCooked2.empty()) return candCooked2;
        }

        // Try id.dds and id.ext in Cooked/Content
        std::filesystem::path idDds = cookedDir / (meta->id.ToString() + ".dds");
        if (isValid(idDds)) return idDds;
        std::string ext = std::filesystem::path(meta->sourcePath).extension().string();
        std::filesystem::path idFile = cookedDir / (meta->id.ToString() + ext);
        if (isValid(idFile)) return idFile;

        // Try sourcePath under Cooked/Content
        if (!meta->sourcePath.empty()) {
            std::filesystem::path srcCooked = checkDdsOrAlt(cookedDir / meta->sourcePath);
            if (!srcCooked.empty()) return srcCooked;
        }
    }

    // 3. Search Cooked/Content and Cooked/Cache/Textures by stem and filename
    std::string stem = std::filesystem::path(virtualOrDiskPath).stem().string();
    std::string filename = std::filesystem::path(virtualOrDiskPath).filename().string();
    if (!stem.empty()) {
        std::vector<std::filesystem::path> searchDirs = {
            cookedDir,
            cookedBase / "Cache" / "Textures"
        };
        for (const auto& dir : searchDirs) {
            if (std::filesystem::exists(dir, ec)) {
                try {
                    auto options = std::filesystem::directory_options::skip_permission_denied;
                    for (auto it = std::filesystem::recursive_directory_iterator(dir, options, ec);
                         !ec && it != std::filesystem::recursive_directory_iterator();
                         it.increment(ec)) {
                        if (!it->is_directory(ec) && std::filesystem::file_size(it->path(), ec) > 0) {
                            std::string fname = it->path().filename().string();
                            std::string fstem = it->path().stem().string();
                            if (fname == filename || fstem == stem || fstem.rfind(stem + "_", 0) == 0) {
                                return it->path();
                            }
                        }
                    }
                } catch (...) {}
            }
        }
    }

    return virtualOrDiskPath;
}

bool AssetManager::CookProject(const std::filesystem::path& outputDir, std::string& outLog, AssetCookProgressFn onProgress) {
    outLog = "Cooking content feature has been removed. All assets remain directly in Content/.\n";
    if (onProgress) onProgress(1.0f, "Assets remain directly in Content/", "");
    return true;
}
