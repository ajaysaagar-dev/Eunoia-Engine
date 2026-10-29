#include <EngineAssets/MeshImporter.h>
#include <EngineAssets/Addressables.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <mutex>
#include <glm/gtc/type_ptr.hpp>

static std::unordered_map<std::string, ImportedModel> s_modelCache;
static std::mutex s_modelCacheMutex;

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

static void LogShaderResourceValidation(const ImportedMaterial& mat, bool hasTangent, bool hasNormal, bool hasUV) {
    std::cout << "[PBR MATERIAL VALIDATION] Material: " << (mat.name.empty() ? "Material" : mat.name) << "\n"
              << "  BaseColor: " << (mat.baseColorTexture.empty() ? "(None / Factor)" : mat.baseColorTexture) << " [SRGB]\n"
              << "  Normal: " << (mat.normalTexture.empty() ? "(None)" : mat.normalTexture) << " [LINEAR]\n"
              << "  NormalYFlip: " << (mat.normalMapYFlip ? "TRUE (OpenGL/Blender)" : "FALSE (DirectX)") << "\n"
              << "  Metallic: " << (mat.metallicTexture.empty() ? "(None / Factor)" : mat.metallicTexture) << " [LINEAR] (Channel: " << mat.metallicChannel << ")\n"
              << "  Roughness: " << (mat.roughnessTexture.empty() ? "(None / Factor)" : mat.roughnessTexture) << " [LINEAR] (Channel: " << mat.roughnessChannel << ")\n"
              << "  AO: " << (mat.aoTexture.empty() ? "(None / 1.0)" : mat.aoTexture) << " [LINEAR] (Channel: " << mat.aoChannel << ")\n"
              << "  Tangent: " << (hasTangent ? "YES" : "NO") << "\n"
              << "  Vertex normal: " << (hasNormal ? "YES" : "NO") << "\n"
              << "  UV0: " << (hasUV ? "YES" : "NO") << std::endl;
}

PrimitiveMesh ImportedModel::GetMergedMesh() const {
    PrimitiveMesh merged;
    uint32_t offset = 0;
    for (const auto& mesh : meshes) {
        if (!mesh.valid) continue;
        merged.vertices.insert(merged.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        for (uint32_t idx : mesh.indices) {
            merged.indices.push_back(idx + offset);
        }
        offset += mesh.vertices.size();
    }
    return merged;
}

static std::string UrlDecode(const std::string& str) {
    std::string res;
    res.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            int value = 0;
            std::istringstream is(str.substr(i + 1, 2));
            if (is >> std::hex >> value) {
                res += static_cast<char>(value);
                i += 2;
                continue;
            }
        } else if (str[i] == '+') {
            res += ' ';
        } else {
            res += str[i];
        }
    }
    return res;
}

static const std::vector<std::string> s_imageExtensions = {
    ".png", ".jpg", ".jpeg", ".tga", ".dds", ".bmp", ".webp", ".tif", ".tiff"
};

std::string MeshImporter::ResolveModelTexturePath(const std::string& rawPath, const std::filesystem::path& modelDir, const std::string& modelStem) {
    if (rawPath.empty()) return "";

    std::string cleaned = rawPath;
    while (!cleaned.empty() && (cleaned.front() == '"' || cleaned.front() == '\'' || cleaned.front() == ' ' || cleaned.front() == '\t')) {
        cleaned.erase(cleaned.begin());
    }
    while (!cleaned.empty() && (cleaned.back() == '"' || cleaned.back() == '\'' || cleaned.back() == ' ' || cleaned.back() == '\t' || cleaned.back() == '\r' || cleaned.back() == '\n')) {
        cleaned.pop_back();
    }
    if (cleaned.empty()) return "";

    cleaned = UrlDecode(cleaned);
    std::replace(cleaned.begin(), cleaned.end(), '\\', '/');

    auto toNormalized = [](const std::filesystem::path& p) {
        std::error_code ec;
        auto canon = std::filesystem::canonical(p, ec);
        return ec ? std::filesystem::absolute(p).lexically_normal().string() : canon.string();
    };

    auto toLowerStr = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    };

    std::error_code ec;

    // 1. Direct absolute path check
    std::filesystem::path pClean(cleaned);
    if (pClean.is_absolute() && std::filesystem::exists(pClean, ec) && std::filesystem::is_regular_file(pClean, ec)) {
        return toNormalized(pClean);
    }

    // 2. Direct relative to modelDir check
    std::filesystem::path candidate = modelDir / pClean;
    if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec)) {
        return toNormalized(candidate);
    }

    std::string filename = pClean.filename().string();
    if (filename.empty()) return "";
    std::string stem = pClean.stem().string();

    std::string filenameLower = toLowerStr(filename);
    std::string stemLower = toLowerStr(stem);

    // 3. Search common folder conventions relative to modelDir
    std::vector<std::filesystem::path> searchDirs = {
        modelDir,
        modelDir / "Textures",
        modelDir / "textures",
        modelDir / "Materials",
        modelDir / "materials",
        modelDir / "Materials" / "Textures",
        modelDir / "materials" / "textures",
        modelDir / "ImportedTextures",
        modelDir / "textures" / "ImportedTextures"
    };

    if (!modelStem.empty()) {
        searchDirs.push_back(modelDir / (modelStem + ".fbm"));
        searchDirs.push_back(modelDir / (modelStem + "_textures"));
        searchDirs.push_back(modelDir / (modelStem + "-textures"));
        searchDirs.push_back(modelDir / (modelStem + "_Textures"));
        searchDirs.push_back(modelDir / (modelStem + "_Files"));
        searchDirs.push_back(modelDir / (modelStem + "_files"));
    }

    // Pass A: Exact or case-insensitive match for filename in candidate directories
    for (const auto& sdir : searchDirs) {
        if (!std::filesystem::exists(sdir, ec) || !std::filesystem::is_directory(sdir, ec)) continue;

        std::filesystem::path directFile = sdir / filename;
        if (std::filesystem::exists(directFile, ec) && std::filesystem::is_regular_file(directFile, ec)) {
            return toNormalized(directFile);
        }

        for (const auto& entry : std::filesystem::directory_iterator(sdir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            if (toLowerStr(entry.path().filename().string()) == filenameLower) {
                return toNormalized(entry.path());
            }
        }
    }

    // Pass B: Extension fallback across candidate directories
    for (const auto& sdir : searchDirs) {
        if (!std::filesystem::exists(sdir, ec) || !std::filesystem::is_directory(sdir, ec)) continue;

        for (const auto& entry : std::filesystem::directory_iterator(sdir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            if (toLowerStr(entry.path().stem().string()) == stemLower) {
                std::string entryExt = toLowerStr(entry.path().extension().string());
                for (const auto& imgExt : s_imageExtensions) {
                    if (entryExt == imgExt) {
                        return toNormalized(entry.path());
                    }
                }
            }
        }
    }

    // Pass C: Recursive directory search inside modelDir (max depth ~3)
    if (std::filesystem::exists(modelDir, ec) && std::filesystem::is_directory(modelDir, ec)) {
        auto opts = std::filesystem::directory_options::skip_permission_denied;
        for (auto it = std::filesystem::recursive_directory_iterator(modelDir, opts, ec);
             !ec && it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            if (it.depth() > 3) {
                it.pop();
                continue;
            }
            std::string entryFnameLower = toLowerStr(it->path().filename().string());
            if (entryFnameLower == filenameLower) {
                return toNormalized(it->path());
            }
            if (toLowerStr(it->path().stem().string()) == stemLower) {
                std::string entryExt = toLowerStr(it->path().extension().string());
                for (const auto& imgExt : s_imageExtensions) {
                    if (entryExt == imgExt) {
                        return toNormalized(it->path());
                    }
                }
            }
        }
    }

    return "";
}

void MeshImporter::UpdateCachedModelMaterials(const std::string& filePath, const std::vector<ImportedMaterial>& materials) {
    std::lock_guard<std::mutex> lock(s_modelCacheMutex);
    std::error_code ec;

    auto updateModel = [&](ImportedModel& model) {
        for (size_t i = 0; i < model.meshes.size(); ++i) {
            auto& mesh = model.meshes[i];
            bool matched = false;
            for (const auto& mat : materials) {
                if (!mat.name.empty() && (mat.name == mesh.material.name || mat.name == mesh.name)) {
                    mesh.material = mat;
                    matched = true;
                    break;
                }
            }
            if (!matched && i < materials.size()) {
                mesh.material = materials[i];
            } else if (!matched && !materials.empty()) {
                mesh.material = materials[0];
            }
        }
    };

    auto it = s_modelCache.find(filePath);
    if (it != s_modelCache.end()) {
        updateModel(it->second);
        return;
    }

    for (auto& pair : s_modelCache) {
        if (pair.first == filePath || std::filesystem::equivalent(pair.first, filePath, ec)) {
            updateModel(pair.second);
            return;
        }
    }
}

ImportedModel MeshImporter::LoadOBJ(const std::string& filePath) {
    ImportedModel model;
    tinyobj::ObjReaderConfig reader_config;
    reader_config.triangulate = true;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(filePath, reader_config)) {
        if (!reader.Error().empty()) {
            model.errorMsg = reader.Error();
        }
        std::cout << "[MeshImporter] Notice: Parsing OBJ '" << filePath << "' encountered error: " << model.errorMsg << " (neglecting, force opening).\n";
        return model;
    }

    if (!reader.Warning().empty()) {
        std::istringstream stream(reader.Warning());
        std::string line;
        bool hasLoggedMissingMtl = false;
        while (std::getline(stream, line)) {
            if (line.find("not found in .mtl") != std::string::npos) {
                if (!hasLoggedMissingMtl) {
                    hasLoggedMissingMtl = true;
                    std::cout << "[MeshImporter] Notice: Materials not found in .mtl for '" << filePath << "' (neglecting missing materials, using defaults).\n";
                }
            } else if (!line.empty()) {
                std::cout << "[MeshImporter] Warning: " << line << "\n";
            }
        }
    }

    auto& attrib = reader.GetAttrib();
    auto& shapes = reader.GetShapes();
    auto& materials = reader.GetMaterials();
    std::filesystem::path objPath(filePath);
    std::filesystem::path objDir = objPath.parent_path();
    std::string objStem = objPath.stem().string();
    auto resolveObjTex = [&](const std::string& texname) -> std::string {
        return MeshImporter::ResolveModelTexturePath(texname, objDir, objStem);
    };

    for (const auto& shape : shapes) {
        // Group faces by material ID to support multi-material meshes
        std::unordered_map<int, std::vector<size_t>> matFaces;
        size_t faceCount = shape.mesh.num_face_vertices.size();
        for (size_t f = 0; f < faceCount; ++f) {
            int matId = (f < shape.mesh.material_ids.size()) ? shape.mesh.material_ids[f] : -1;
            matFaces[matId].push_back(f);
        }

        for (const auto& kv : matFaces) {
            int matId = kv.first;
            const auto& faceIndices = kv.second;
            if (faceIndices.empty()) continue;

            ImportedMesh mesh;
            mesh.name = shape.name;
            if (matFaces.size() > 1 && matId >= 0 && matId < (int)materials.size()) {
                mesh.name += "_" + materials[matId].name;
            }

            if (matId >= 0 && matId < (int)materials.size()) {
                const auto& tm = materials[matId];
                ImportedMaterial& m = mesh.material;
                m.hasMaterial = true;
                m.name = tm.name.empty() ? "Material" : tm.name;
                m.baseColorTexture = resolveObjTex(tm.diffuse_texname);
                m.normalTexture    = resolveObjTex(!tm.normal_texname.empty() ? tm.normal_texname : tm.bump_texname);
                m.roughnessTexture = resolveObjTex(tm.roughness_texname);
                m.metallicTexture  = resolveObjTex(tm.metallic_texname);
                m.aoTexture        = resolveObjTex(tm.ambient_texname);
                m.emissionTexture  = resolveObjTex(tm.emissive_texname);
                m.opacityTexture   = resolveObjTex(tm.alpha_texname);

                if (std::isfinite(tm.diffuse[0]) && (tm.diffuse[0] > 0.001f || tm.diffuse[1] > 0.001f || tm.diffuse[2] > 0.001f)) {
                    m.baseColor = glm::vec3(tm.diffuse[0], tm.diffuse[1], tm.diffuse[2]);
                }
                if (std::isfinite(tm.emission[0]) && (tm.emission[0] > 0.001f || tm.emission[1] > 0.001f || tm.emission[2] > 0.001f)) {
                    m.emissiveColor = glm::vec3(tm.emission[0], tm.emission[1], tm.emission[2]);
                    m.emissiveIntensity = 1.0f;
                }
                if (tm.dissolve > 0.0f && tm.dissolve < 0.999f && std::isfinite(tm.dissolve)) {
                    m.opacity = tm.dissolve;
                    m.blendMode = 1;
                }
                if (m.roughnessTexture.empty()) {
                    if (tm.roughness > 0.0f && std::isfinite(tm.roughness)) {
                        m.roughness = tm.roughness;
                    } else if (tm.shininess > 0.0f && std::isfinite(tm.shininess)) {
                        m.roughness = std::clamp(std::sqrt(2.0f / (tm.shininess + 2.0f)), 0.04f, 1.0f);
                    }
                }
                if (m.metallicTexture.empty() && tm.metallic > 0.0f && std::isfinite(tm.metallic)) {
                    m.metallic = tm.metallic;
                }
            }

            for (size_t f : faceIndices) {
                for (size_t v = 0; v < 3; ++v) {
                    tinyobj::index_t idx = shape.mesh.indices[f * 3 + v];
                    MeshVertex vertex;

                    vertex.pos = {
                        attrib.vertices[3 * size_t(idx.vertex_index) + 0],
                        attrib.vertices[3 * size_t(idx.vertex_index) + 1],
                        attrib.vertices[3 * size_t(idx.vertex_index) + 2]
                    };

                    if (idx.normal_index >= 0) {
                        vertex.normal = {
                            attrib.normals[3 * size_t(idx.normal_index) + 0],
                            attrib.normals[3 * size_t(idx.normal_index) + 1],
                            attrib.normals[3 * size_t(idx.normal_index) + 2]
                        };
                    } else {
                        vertex.normal = {0.0f, 0.0f, 0.0f};
                    }

                    if (idx.texcoord_index >= 0) {
                        vertex.uv = {
                            attrib.texcoords[2 * size_t(idx.texcoord_index) + 0],
                            1.0f - attrib.texcoords[2 * size_t(idx.texcoord_index) + 1]
                        };
                    } else {
                        vertex.uv = {0.0f, 0.0f};
                    }

                    mesh.vertices.push_back(vertex);
                    mesh.indices.push_back((uint32_t)mesh.vertices.size() - 1);
                }
            }

            // Compute smooth normals if missing in OBJ
            bool missingNormals = false;
            for (const auto& v : mesh.vertices) {
                if (glm::length(v.normal) < 0.001f) {
                    missingNormals = true;
                    break;
                }
            }

            if (missingNormals) {
                GeometryBuilder::ComputeSmoothNormals(mesh.vertices, mesh.indices);
            } else {
                for (auto& v : mesh.vertices) {
                    float nl = glm::length(v.normal);
                    if (nl > 1e-6f) v.normal /= nl;
                }
                GeometryBuilder::CalculateTangents(mesh.vertices, mesh.indices);
            }
            LogShaderResourceValidation(mesh.material, true, true, !attrib.texcoords.empty());

            mesh.valid = !mesh.vertices.empty();
            if (mesh.valid) {
                model.meshes.push_back(mesh);
            }
        }
    }
    model.valid = !model.meshes.empty();
    return model;
}

ImportedModel MeshImporter::LoadGLTF(const std::string& filePath) {
    ImportedModel model;
    cgltf_options options = {};
    cgltf_data* data = NULL;
    cgltf_result result = cgltf_parse_file(&options, filePath.c_str(), &data);

    if (result != cgltf_result_success) {
        model.errorMsg = "Failed to parse GLTF file";
        return model;
    }

    result = cgltf_load_buffers(&options, data, filePath.c_str());
    if (result != cgltf_result_success) {
        model.errorMsg = "Failed to load GLTF buffers";
        cgltf_free(data);
        return model;
    }

    result = cgltf_validate(data);
    if (result != cgltf_result_success) {
        model.errorMsg = "Failed to validate GLTF";
        cgltf_free(data);
        return model;
    }

    std::filesystem::path gltfPath(filePath);
    std::filesystem::path gltfDir = gltfPath.parent_path();
    std::string gltfStem = gltfPath.stem().string();
    std::filesystem::path gltfCacheDir = gltfDir / "Textures";
    auto resolveGltfImage = [&](cgltf_texture* tex, const std::string& matName, const std::string& slotName) -> std::string {
        if (!tex || !tex->image) return "";
        cgltf_image* img = tex->image;
        if (img->uri) {
            std::string uri = img->uri;
            if (uri.rfind("data:", 0) != 0) {
                std::string resolved = MeshImporter::ResolveModelTexturePath(uri, gltfDir, gltfStem);
                if (!resolved.empty()) return resolved;
            }
        }
        if (img->buffer_view) {
            // GLB-embedded or data-URI image: extract raw bytes to Textures folder
            std::error_code ec;
            std::filesystem::create_directories(gltfCacheDir, ec);
            const uint8_t* dataBytes = (const uint8_t*)cgltf_buffer_view_data(img->buffer_view);
            size_t size = img->buffer_view->size;
            if (dataBytes && size > 0) {
                std::string ext = ".png";
                if (img->mime_type && std::string(img->mime_type).find("jpeg") != std::string::npos) ext = ".jpg";
                std::string baseName = (img->name && strlen(img->name) > 0) ? img->name : (matName + "_" + slotName);
                std::filesystem::path outPath = gltfCacheDir / (baseName + ext);
                if (!std::filesystem::exists(outPath, ec)) {
                    FILE* f = fopen(outPath.string().c_str(), "wb");
                    if (f) { fwrite(dataBytes, 1, size, f); fclose(f); }
                }
                return outPath.string();
            }
        }
        return "";
    };

    // Collect all nodes that reference a mesh so we preserve hierarchy transforms and instancing
    struct GltfNodeMeshEntry {
        cgltf_node* node;
        cgltf_mesh* mesh;
    };
    std::vector<GltfNodeMeshEntry> entries;

    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        cgltf_node* node = &data->nodes[i];
        if (node && node->mesh) {
            entries.push_back({ node, node->mesh });
        }
    }

    // Fallback: If no node references any mesh, use data->meshes directly
    if (entries.empty()) {
        for (cgltf_size i = 0; i < data->meshes_count; ++i) {
            entries.push_back({ nullptr, &data->meshes[i] });
        }
    }

    for (size_t entryIdx = 0; entryIdx < entries.size(); ++entryIdx) {
        cgltf_node* node = entries[entryIdx].node;
        cgltf_mesh* gltfMesh = entries[entryIdx].mesh;
        if (!gltfMesh) continue;

        std::string baseMeshName;
        if (node && node->name && strlen(node->name) > 0) {
            baseMeshName = node->name;
        } else if (gltfMesh->name && strlen(gltfMesh->name) > 0) {
            baseMeshName = gltfMesh->name;
        } else {
            baseMeshName = "Mesh_" + std::to_string(entryIdx);
        }

        glm::mat4 nodeWorld(1.0f);
        glm::mat3 normMatrix(1.0f);
        bool hasTransform = (node != nullptr);
        if (hasTransform) {
            float m[16];
            cgltf_node_transform_world(node, m);
            nodeWorld = glm::make_mat4(m);
            normMatrix = glm::transpose(glm::inverse(glm::mat3(nodeWorld)));
        }

        for (cgltf_size j = 0; j < gltfMesh->primitives_count; ++j) {
            cgltf_primitive* primitive = &gltfMesh->primitives[j];
            if (primitive->type != cgltf_primitive_type_triangles) continue;

            ImportedMesh mesh;
            mesh.name = (gltfMesh->primitives_count > 1)
                ? (baseMeshName + "_" + std::to_string(j))
                : baseMeshName;

            if (primitive->material) {
                cgltf_material* mat = primitive->material;
                ImportedMaterial& m = mesh.material;
                m.hasMaterial = true;
                m.name = mat->name ? mat->name : "Material";
                if (mat->has_pbr_metallic_roughness) {
                    auto& pbr = mat->pbr_metallic_roughness;
                    m.baseColorTexture = resolveGltfImage(pbr.base_color_texture.texture, m.name, "BaseColor");
                    m.metallicTexture  = resolveGltfImage(pbr.metallic_roughness_texture.texture, m.name, "MetallicRoughness");
                    m.roughnessTexture = m.metallicTexture; // packed GLTF metallic-roughness
                    m.roughnessChannel = 1; // Green channel = Roughness
                    m.metallicChannel = 2;  // Blue channel = Metallic
                    m.aoChannel = 0;
                    if (std::isfinite(pbr.base_color_factor[0])) {
                        m.baseColor = glm::vec3(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]);
                    }
                    if (m.metallicTexture.empty() && std::isfinite(pbr.metallic_factor)) m.metallic = pbr.metallic_factor;
                    if (m.roughnessTexture.empty() && std::isfinite(pbr.roughness_factor)) m.roughness = pbr.roughness_factor;
                }
                m.normalTexture   = resolveGltfImage(mat->normal_texture.texture, m.name, "Normal");
                m.normalMapYFlip  = true; // GLTF standard expects OpenGL normal map convention
                m.aoTexture       = resolveGltfImage(mat->occlusion_texture.texture, m.name, "AO");
                m.emissionTexture = resolveGltfImage(mat->emissive_texture.texture, m.name, "Emissive");
                if (std::isfinite(mat->emissive_factor[0])) {
                    m.emissiveColor = glm::vec3(mat->emissive_factor[0], mat->emissive_factor[1], mat->emissive_factor[2]);
                    if (m.emissiveColor.r > 0.001f || m.emissiveColor.g > 0.001f || m.emissiveColor.b > 0.001f || !m.emissionTexture.empty()) {
                        m.emissiveIntensity = 1.0f;
                    }
                }
                if (mat->has_emissive_strength) {
                    m.emissiveIntensity = mat->emissive_strength.emissive_strength;
                }
                if (mat->alpha_mode == cgltf_alpha_mode_mask) {
                    m.blendMode = 1;
                    m.opacityMaskClipValue = std::isfinite(mat->alpha_cutoff) ? mat->alpha_cutoff : 0.5f;
                } else if (mat->alpha_mode == cgltf_alpha_mode_blend) {
                    m.blendMode = 2;
                } else {
                    m.blendMode = 0;
                }
            }

            cgltf_accessor* posAccessor = nullptr;
            cgltf_accessor* normAccessor = nullptr;
            cgltf_accessor* uvAccessor = nullptr;
            cgltf_accessor* tanAccessor = nullptr;

            for (cgltf_size k = 0; k < primitive->attributes_count; ++k) {
                cgltf_attribute* attr = &primitive->attributes[k];
                if (attr->type == cgltf_attribute_type_position) posAccessor = attr->data;
                if (attr->type == cgltf_attribute_type_normal) normAccessor = attr->data;
                if (attr->type == cgltf_attribute_type_texcoord) uvAccessor = attr->data;
                if (attr->type == cgltf_attribute_type_tangent) tanAccessor = attr->data;
            }

            if (!posAccessor) continue;

            for (cgltf_size k = 0; k < posAccessor->count; ++k) {
                MeshVertex vertex;
                vertex.normal = {0.0f, 0.0f, 0.0f};
                vertex.uv = {0.0f, 0.0f};
                vertex.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
                cgltf_accessor_read_float(posAccessor, k, &vertex.pos.x, 3);
                if (hasTransform) {
                    glm::vec4 tp = nodeWorld * glm::vec4(vertex.pos, 1.0f);
                    vertex.pos = glm::vec3(tp);
                }
                if (normAccessor) {
                    cgltf_accessor_read_float(normAccessor, k, &vertex.normal.x, 3);
                    if (hasTransform) {
                        vertex.normal = normMatrix * vertex.normal;
                    }
                    float nLen = glm::length(vertex.normal);
                    if (nLen > 1e-6f) vertex.normal /= nLen;
                    else vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                }
                if (uvAccessor) {
                    cgltf_accessor_read_float(uvAccessor, k, &vertex.uv.x, 2);
                }
                if (tanAccessor) {
                    cgltf_accessor_read_float(tanAccessor, k, &vertex.tangent.x, 4);
                    if (hasTransform) {
                        glm::vec3 t3 = glm::mat3(nodeWorld) * glm::vec3(vertex.tangent);
                        float tLen = glm::length(t3);
                        if (tLen > 1e-6f) t3 /= tLen;
                        else t3 = glm::vec3(1.0f, 0.0f, 0.0f);
                        vertex.tangent = glm::vec4(t3, vertex.tangent.w);
                    }
                }
                mesh.vertices.push_back(vertex);
            }

            if (primitive->indices) {
                for (cgltf_size k = 0; k < primitive->indices->count; ++k) {
                    mesh.indices.push_back(cgltf_accessor_read_index(primitive->indices, k));
                }
            } else {
                for (cgltf_size k = 0; k < posAccessor->count; ++k) {
                    mesh.indices.push_back((uint32_t)k);
                }
            }

            if (!normAccessor) {
                GeometryBuilder::ComputeSmoothNormals(mesh.vertices, mesh.indices);
            } else {
                for (size_t idx = 0; idx + 2 < mesh.indices.size(); idx += 3) {
                    MeshVertex& v0 = mesh.vertices[mesh.indices[idx]];
                    MeshVertex& v1 = mesh.vertices[mesh.indices[idx+1]];
                    MeshVertex& v2 = mesh.vertices[mesh.indices[idx+2]];

                    if (glm::length(v0.normal) < 0.001f || glm::length(v1.normal) < 0.001f || glm::length(v2.normal) < 0.001f) {
                        glm::vec3 normal = glm::normalize(glm::cross(v1.pos - v0.pos, v2.pos - v0.pos));
                        v0.normal = normal;
                        v1.normal = normal;
                        v2.normal = normal;
                    }
                }
            }

            bool hasImportedTangents = false;
            for (const auto& v : mesh.vertices) {
                if (v.tangent != glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)) {
                    hasImportedTangents = true;
                    break;
                }
            }
            if (!hasImportedTangents) {
                GeometryBuilder::CalculateTangents(mesh.vertices, mesh.indices);
            }

            LogShaderResourceValidation(mesh.material, true, true, true);

            mesh.valid = !mesh.vertices.empty();
            if (mesh.valid) {
                model.meshes.push_back(mesh);
            }
        }
    }
    
    cgltf_free(data);
    model.valid = !model.meshes.empty();
    return model;
}

#include "ufbx.h"

static std::string ResolveFbxTexturePath(ufbx_texture* tex, const std::filesystem::path& fbxDir,
                                          const std::filesystem::path& texturesDir, const std::string& matName,
                                          const std::string& slotName, const std::string& modelStem = "") {
    if (!tex) return "";

    if (tex->content.size > 0) {
        std::error_code ec;
        std::filesystem::create_directories(texturesDir, ec);
        std::string ext = ".png";
        std::string baseName = matName + "_" + slotName;
        if (tex->filename.length > 0) {
            std::filesystem::path origPath(std::string(tex->filename.data, tex->filename.length));
            std::string origExt = origPath.extension().string();
            if (!origExt.empty()) ext = origExt;
            std::string origStem = origPath.stem().string();
            if (!origStem.empty()) baseName = origStem;
        }
        std::filesystem::path outPath = texturesDir / (baseName + ext);
        if (!std::filesystem::exists(outPath, ec)) {
            FILE* f = fopen(outPath.string().c_str(), "wb");
            if (f) {
                fwrite(tex->content.data, 1, tex->content.size, f);
                fclose(f);
            }
        }
        return outPath.string();
    }

    if (tex->filename.length > 0) {
        std::string raw(tex->filename.data, tex->filename.length);
        std::string found = MeshImporter::ResolveModelTexturePath(raw, fbxDir, modelStem);
        if (!found.empty() && std::filesystem::exists(found)) return found;
    }
    if (tex->relative_filename.length > 0) {
        std::string raw(tex->relative_filename.data, tex->relative_filename.length);
        std::string found = MeshImporter::ResolveModelTexturePath(raw, fbxDir, modelStem);
        if (!found.empty() && std::filesystem::exists(found)) return found;
    }
    return "";
}

static void PopulateFbxMaterial(ImportedMaterial& m, ufbx_material* mat,
                                const std::filesystem::path& fbxDir, const std::filesystem::path& texturesDir,
                                const std::string& modelStem = "") {
    if (!mat) return;
    m.hasMaterial = true;
    m.name = mat->name.length > 0 ? std::string(mat->name.data, mat->name.length) : "Material";
    m.normalMapYFlip = true; // Blender/OpenGL standard normal map convention

    // 1. PBR Material Properties (Maya Stingray, Blender Principled, 3ds Max Physical)
    m.baseColorTexture = ResolveFbxTexturePath(mat->pbr.base_color.texture, fbxDir, texturesDir, m.name, "BaseColor", modelStem);
    m.normalTexture    = ResolveFbxTexturePath(mat->pbr.normal_map.texture, fbxDir, texturesDir, m.name, "Normal", modelStem);
    m.roughnessTexture = ResolveFbxTexturePath(mat->pbr.roughness.texture, fbxDir, texturesDir, m.name, "Roughness", modelStem);
    m.metallicTexture  = ResolveFbxTexturePath(mat->pbr.metalness.texture, fbxDir, texturesDir, m.name, "Metallic", modelStem);
    m.aoTexture        = ResolveFbxTexturePath(mat->pbr.ambient_occlusion.texture, fbxDir, texturesDir, m.name, "AO", modelStem);
    m.emissionTexture  = ResolveFbxTexturePath(mat->pbr.emission_color.texture, fbxDir, texturesDir, m.name, "Emissive", modelStem);
    m.opacityTexture   = ResolveFbxTexturePath(mat->pbr.opacity.texture, fbxDir, texturesDir, m.name, "Opacity", modelStem);

    if (mat->pbr.base_color.has_value) {
        ufbx_vec3 bc = mat->pbr.base_color.value_vec3;
        if (std::isfinite((float)bc.x) && std::isfinite((float)bc.y) && std::isfinite((float)bc.z))
            m.baseColor = glm::vec3((float)bc.x, (float)bc.y, (float)bc.z);
    }
    if (mat->pbr.metalness.has_value && m.metallicTexture.empty()) {
        m.metallic = std::isfinite((float)mat->pbr.metalness.value_real) ? (float)mat->pbr.metalness.value_real : 0.0f;
    }
    if (mat->pbr.roughness.has_value && m.roughnessTexture.empty()) {
        m.roughness = std::isfinite((float)mat->pbr.roughness.value_real) ? (float)mat->pbr.roughness.value_real : 0.5f;
    }
    if (mat->pbr.emission_color.has_value) {
        ufbx_vec3 ec = mat->pbr.emission_color.value_vec3;
        if (std::isfinite((float)ec.x) && std::isfinite((float)ec.y) && std::isfinite((float)ec.z))
            m.emissiveColor = glm::vec3((float)ec.x, (float)ec.y, (float)ec.z);
    }
    if (mat->pbr.emission_factor.has_value && std::isfinite((float)mat->pbr.emission_factor.value_real)) {
        m.emissiveIntensity = (float)mat->pbr.emission_factor.value_real;
    } else if (m.emissiveColor.r > 0.001f || m.emissiveColor.g > 0.001f || m.emissiveColor.b > 0.001f || !m.emissionTexture.empty()) {
        m.emissiveIntensity = 1.0f;
    }
    if (mat->pbr.opacity.has_value && std::isfinite((float)mat->pbr.opacity.value_real)) {
        m.opacity = (float)mat->pbr.opacity.value_real;
    }

    // 2. FBX Classic Lambert / Phong fallbacks
    if (m.baseColorTexture.empty() && mat->fbx.diffuse_color.texture) {
        m.baseColorTexture = ResolveFbxTexturePath(mat->fbx.diffuse_color.texture, fbxDir, texturesDir, m.name, "Diffuse", modelStem);
    }
    if (m.normalTexture.empty()) {
        if (mat->fbx.normal_map.texture) {
            m.normalTexture = ResolveFbxTexturePath(mat->fbx.normal_map.texture, fbxDir, texturesDir, m.name, "Normal", modelStem);
        } else if (mat->fbx.bump.texture) {
            m.normalTexture = ResolveFbxTexturePath(mat->fbx.bump.texture, fbxDir, texturesDir, m.name, "Bump", modelStem);
        }
    }
    if (m.emissionTexture.empty() && mat->fbx.emission_color.texture) {
        m.emissionTexture = ResolveFbxTexturePath(mat->fbx.emission_color.texture, fbxDir, texturesDir, m.name, "Emission", modelStem);
    }
    if (m.opacityTexture.empty() && mat->fbx.transparency_color.texture) {
        m.opacityTexture = ResolveFbxTexturePath(mat->fbx.transparency_color.texture, fbxDir, texturesDir, m.name, "Transparency", modelStem);
    }

    if (!m.opacityTexture.empty() || m.opacity < 0.999f) {
        m.blendMode = 1;
    }
}

ImportedModel MeshImporter::LoadFBX(const std::string& filePath) {
    ImportedModel model;
    ufbx_load_opts opts = { 0 };
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.generate_missing_normals = true;

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(filePath.c_str(), &opts, &error);
    if (!scene) {
        char errBuf[512];
        ufbx_format_error(errBuf, sizeof(errBuf), &error);
        model.errorMsg = errBuf;
        return model;
    }

    std::vector<uint32_t> tri_indices;
    std::filesystem::path fbxPath(filePath);
    std::filesystem::path fbxDir = fbxPath.parent_path();
    std::string modelStem = fbxPath.stem().string();
    std::filesystem::path texturesDir = fbxDir / "Textures";

    // Collect all nodes that contain a mesh so we preserve hierarchy transforms and instancing
    struct NodeMeshEntry {
        ufbx_node* node;
        ufbx_mesh* mesh;
    };
    std::vector<NodeMeshEntry> entries;

    for (size_t i = 0; i < scene->nodes.count; ++i) {
        ufbx_node* node = scene->nodes.data[i];
        if (node && node->mesh && node->mesh->num_faces > 0) {
            entries.push_back({ node, node->mesh });
        }
    }

    // Fallback: If no node references any mesh, use scene->meshes directly
    if (entries.empty()) {
        for (size_t i = 0; i < scene->meshes.count; ++i) {
            ufbx_mesh* mesh = scene->meshes.data[i];
            if (mesh && mesh->num_faces > 0) {
                entries.push_back({ nullptr, mesh });
            }
        }
    }

    for (size_t entryIdx = 0; entryIdx < entries.size(); ++entryIdx) {
        ufbx_node* node = entries[entryIdx].node;
        ufbx_mesh* mesh = entries[entryIdx].mesh;
        if (!mesh || mesh->num_faces == 0) continue;

        std::string baseMeshName;
        if (node && node->name.data && node->name.length > 0) {
            baseMeshName = std::string(node->name.data, node->name.length);
        } else if (mesh->name.data && mesh->name.length > 0) {
            baseMeshName = std::string(mesh->name.data, mesh->name.length);
        } else {
            baseMeshName = "FBX_Mesh_" + std::to_string(entryIdx);
        }

        ufbx_matrix geomToWorld;
        ufbx_matrix normMatrix;
        bool hasTransform = (node != nullptr);
        if (hasTransform) {
            geomToWorld = node->geometry_to_world;
            normMatrix = ufbx_matrix_for_normals(&node->geometry_to_world);
        }

        size_t max_tris = mesh->max_face_triangles * 3;
        if (max_tris > tri_indices.size()) {
            tri_indices.resize(std::max(max_tris, (size_t)64));
        }

        // Group faces by material index (supports multi-material submesh splitting)
        size_t numMats = std::max((size_t)1, (size_t)mesh->materials.count);
        std::vector<std::vector<size_t>> matFaces(numMats);

        for (size_t f = 0; f < mesh->faces.count; ++f) {
            size_t matIdx = 0;
            if (mesh->face_material.count > f) {
                int32_t fm = mesh->face_material.data[f];
                if (fm >= 0 && (size_t)fm < numMats) {
                    matIdx = (size_t)fm;
                }
            }
            matFaces[matIdx].push_back(f);
        }

        for (size_t mIdx = 0; mIdx < numMats; ++mIdx) {
            const auto& faceList = matFaces[mIdx];
            if (faceList.empty()) continue;

            ImportedMesh im;
            im.name = baseMeshName;
            if (numMats > 1) {
                im.name += "_" + std::to_string(mIdx);
            }

            for (size_t f : faceList) {
                ufbx_face face = mesh->faces.data[f];
                uint32_t num_tris = ufbx_triangulate_face(tri_indices.data(), tri_indices.size(), mesh, face);

                for (uint32_t t = 0; t < num_tris * 3; ++t) {
                    uint32_t idx = tri_indices[t];
                    MeshVertex v;
                    ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, idx);
                    if (hasTransform) {
                        p = ufbx_transform_position(&geomToWorld, p);
                    }
                    v.pos = { (float)p.x, (float)p.y, (float)p.z };

                    if (mesh->vertex_normal.exists) {
                        ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh->vertex_normal, idx);
                        if (hasTransform) {
                            n = ufbx_transform_direction(&normMatrix, n);
                        }
                        glm::vec3 gn((float)n.x, (float)n.y, (float)n.z);
                        float nLen = glm::length(gn);
                        if (nLen > 1e-6f) gn /= nLen;
                        else gn = glm::vec3(0.0f, 1.0f, 0.0f);
                        v.normal = gn;
                    } else {
                        v.normal = { 0.0f, 0.0f, 0.0f };
                    }

                    if (mesh->vertex_uv.exists) {
                        ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, idx);
                        v.uv = { (float)uv.x, 1.0f - (float)uv.y };
                    } else {
                        v.uv = { 0.0f, 0.0f };
                    }

                    if (mesh->vertex_tangent.exists) {
                        ufbx_vec3 tg = ufbx_get_vertex_vec3(&mesh->vertex_tangent, idx);
                        if (hasTransform) {
                            tg = ufbx_transform_direction(&geomToWorld, tg);
                        }
                        float h = 1.0f;
                        if (mesh->vertex_bitangent.exists) {
                            ufbx_vec3 b = ufbx_get_vertex_vec3(&mesh->vertex_bitangent, idx);
                            if (hasTransform) {
                                b = ufbx_transform_direction(&geomToWorld, b);
                            }
                            glm::vec3 gn = v.normal;
                            glm::vec3 gt((float)tg.x, (float)tg.y, (float)tg.z);
                            glm::vec3 gb((float)b.x, (float)b.y, (float)b.z);
                            h = (glm::dot(glm::cross(gn, gt), gb) < 0.0f) ? -1.0f : 1.0f;
                        }
                        glm::vec3 gt((float)tg.x, (float)tg.y, (float)tg.z);
                        float tLen = glm::length(gt);
                        if (tLen > 1e-6f) gt /= tLen;
                        else gt = glm::vec3(1.0f, 0.0f, 0.0f);
                        v.tangent = { gt.x, gt.y, gt.z, h };
                    }

                    im.vertices.push_back(v);
                    im.indices.push_back((uint32_t)im.vertices.size() - 1);
                }
            }

            if (!mesh->vertex_normal.exists) {
                GeometryBuilder::ComputeSmoothNormals(im.vertices, im.indices);
            } else {
                for (size_t k = 0; k + 2 < im.indices.size(); k += 3) {
                    MeshVertex& v0 = im.vertices[im.indices[k]];
                    MeshVertex& v1 = im.vertices[im.indices[k+1]];
                    MeshVertex& v2 = im.vertices[im.indices[k+2]];
                    if (glm::length(v0.normal) < 0.001f || glm::length(v1.normal) < 0.001f || glm::length(v2.normal) < 0.001f) {
                        glm::vec3 n = glm::normalize(glm::cross(v1.pos - v0.pos, v2.pos - v0.pos));
                        v0.normal = n;
                        v1.normal = n;
                        v2.normal = n;
                    }
                }
            }

            if (!mesh->vertex_tangent.exists) {
                GeometryBuilder::CalculateTangents(im.vertices, im.indices);
            }

            if (mesh->materials.count > mIdx && mesh->materials.data[mIdx]) {
                PopulateFbxMaterial(im.material, mesh->materials.data[mIdx], fbxDir, texturesDir, modelStem);
            } else if (mesh->materials.count > 0 && mesh->materials.data[0]) {
                PopulateFbxMaterial(im.material, mesh->materials.data[0], fbxDir, texturesDir, modelStem);
            }

            LogShaderResourceValidation(im.material, true, mesh->vertex_normal.exists, mesh->vertex_uv.exists);

            im.valid = !im.vertices.empty();
            if (im.valid) {
                model.meshes.push_back(im);
            }
        }
    }

    ufbx_free_scene(scene);
    model.valid = !model.meshes.empty();
    return model;
}

bool MeshImporter::IsSupportedFormat(const std::string& ext) {
    std::string lowerExt = ext;
    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
    return lowerExt == ".obj" || lowerExt == ".gltf" || lowerExt == ".glb" || lowerExt == ".fbx";
}

void MeshImporter::ClearCache() {
    std::lock_guard<std::mutex> lock(s_modelCacheMutex);
    s_modelCache.clear();
}

void MeshImporter::InvalidateCache(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(s_modelCacheMutex);
    s_modelCache.erase(filePath);
}

ImportedModel MeshImporter::Load(const std::string& filePath) {
    if (filePath.empty()) {
        ImportedModel m;
        m.errorMsg = "Empty file path";
        return m;
    }

    // 1. Check in-memory cache first to avoid re-parsing heavy models repeatedly
    {
        std::lock_guard<std::mutex> lock(s_modelCacheMutex);
        auto it = s_modelCache.find(filePath);
        if (it != s_modelCache.end() && it->second.valid) {
            return it->second;
        }
    }

    try {
        std::string effectivePath = filePath;
        std::error_code ec;
        if (!std::filesystem::exists(effectivePath, ec) || std::filesystem::file_size(effectivePath, ec) == 0) {
            std::string resolved = Addressables::ResolveAddress(filePath);
            if (!resolved.empty() && std::filesystem::exists(resolved, ec) && std::filesystem::file_size(resolved, ec) > 0) {
                effectivePath = resolved;
            }
        }

        // Check if effectivePath is already in cache
        if (effectivePath != filePath) {
            std::lock_guard<std::mutex> lock(s_modelCacheMutex);
            auto it = s_modelCache.find(effectivePath);
            if (it != s_modelCache.end() && it->second.valid) {
                s_modelCache[filePath] = it->second;
                return it->second;
            }
        }

        std::filesystem::path path(effectivePath);
        if (!std::filesystem::exists(path, ec)) {
            ImportedModel model;
            model.errorMsg = "File does not exist: " + filePath;
            std::cout << "[MeshImporter] Notice: " << model.errorMsg << " (neglecting, force opening scene).\n";
            return model;
        }

        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        ImportedModel model;
        if (ext == ".obj") {
            model = LoadOBJ(effectivePath);
        } else if (ext == ".gltf" || ext == ".glb") {
            model = LoadGLTF(effectivePath);
        } else if (ext == ".fbx") {
            model = LoadFBX(effectivePath);
        } else {
            model.errorMsg = "Unsupported format: " + ext;
            return model;
        }

        // Cache successfully loaded models
        if (model.valid) {
            for (auto& mesh : model.meshes) {
                if (mesh.valid && !mesh.vertices.empty() && !mesh.indices.empty()) {
                    GeometryBuilder::OptimizeMesh(mesh.vertices, mesh.indices);
                    // Generate LODs for imported meshes
                    PrimitiveMesh pm;
                    pm.vertices = mesh.vertices;
                    pm.indices = mesh.indices;
                    pm.GenerateLODs();
                    mesh.lodIndices = std::move(pm.lodIndices);
                }
            }
            std::lock_guard<std::mutex> lock(s_modelCacheMutex);
            s_modelCache[filePath] = model;
            if (effectivePath != filePath) {
                s_modelCache[effectivePath] = model;
            }
        }
        
        return model;
    } catch (const std::exception& ex) {
        ImportedModel model;
        model.errorMsg = ex.what();
        std::cout << "[MeshImporter] Exception loading '" << filePath << "': " << ex.what() << " (neglecting, force opening).\n";
        return model;
    } catch (...) {
        ImportedModel model;
        model.errorMsg = "Unknown exception loading model";
        std::cout << "[MeshImporter] Unknown exception loading '" << filePath << "' (neglecting, force opening).\n";
        return model;
    }
}
