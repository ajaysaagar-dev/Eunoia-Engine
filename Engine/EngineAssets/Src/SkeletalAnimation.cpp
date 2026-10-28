#include <EngineAssets/SkeletalAnimation.h>
#include <EngineAssets/Addressables.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>

#include "ufbx.h"

namespace Eunoia {

SkeletalAnimationManager& SkeletalAnimationManager::Get() {
    static SkeletalAnimationManager s_instance;
    return s_instance;
}

void SkeletalAnimationManager::ClearCache() {
    m_meshCache.clear();
    m_clipCache.clear();
}

static glm::mat4 UfbxToGlmMat4(const ufbx_matrix& m) {
    glm::mat4 out(1.0f);
    out[0][0] = (float)m.cols[0].x; out[0][1] = (float)m.cols[0].y; out[0][2] = (float)m.cols[0].z; out[0][3] = 0.0f;
    out[1][0] = (float)m.cols[1].x; out[1][1] = (float)m.cols[1].y; out[1][2] = (float)m.cols[1].z; out[1][3] = 0.0f;
    out[2][0] = (float)m.cols[2].x; out[2][1] = (float)m.cols[2].y; out[2][2] = (float)m.cols[2].z; out[2][3] = 0.0f;
    out[3][0] = (float)m.cols[3].x; out[3][1] = (float)m.cols[3].y; out[3][2] = (float)m.cols[3].z; out[3][3] = 1.0f;
    return out;
}

static glm::vec3 UfbxToGlmVec3(const ufbx_vec3& v) {
    return glm::vec3((float)v.x, (float)v.y, (float)v.z);
}

static glm::quat UfbxToGlmQuat(const ufbx_quat& q) {
    return glm::quat((float)q.w, (float)q.x, (float)q.y, (float)q.z);
}

std::shared_ptr<SkeletalMesh> SkeletalAnimationManager::LoadSkeletalMesh(const std::string& filePath) {
    std::string normPath = filePath;
    std::replace(normPath.begin(), normPath.end(), '\\', '/');

    auto it = m_meshCache.find(normPath);
    if (it != m_meshCache.end()) return it->second;

    auto skelMesh = std::make_shared<SkeletalMesh>();
    skelMesh->sourcePath = normPath;
    skelMesh->name = std::filesystem::path(normPath).stem().string();

    std::string ext = std::filesystem::path(normPath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".fbx") {
        ufbx_load_opts opts = { 0 };
        opts.target_axes = ufbx_axes_right_handed_y_up;
        opts.target_unit_meters = 1.0f;
        opts.generate_missing_normals = true;

        ufbx_error error;
        ufbx_scene* scene = ufbx_load_file(normPath.c_str(), &opts, &error);
        if (scene) {
            // 1. Collect all bones / skin clusters
            std::unordered_map<const ufbx_node*, int> nodeToBoneIndex;
            for (size_t i = 0; i < scene->nodes.count; ++i) {
                ufbx_node* node = scene->nodes.data[i];
                if (node->is_root) continue;

                // Treat as bone if flagged as bone, has bone child, or referenced by skin cluster
                bool isBone = (node->bone != nullptr);
                for (size_t c = 0; c < scene->skin_clusters.count; ++c) {
                    if (scene->skin_clusters.data[c]->bone_node == node) {
                        isBone = true;
                        break;
                    }
                }

                if (isBone || scene->skin_clusters.count == 0) {
                    Bone b;
                    b.name = node->name.data ? std::string(node->name.data, node->name.length) : ("Bone_" + std::to_string(i));
                    b.localBindTransform = UfbxToGlmMat4(node->geometry_to_node);
                    b.inverseBindMatrix = glm::inverse(UfbxToGlmMat4(node->geometry_to_world));

                    int boneIdx = (int)skelMesh->skeleton.bones.size();
                    nodeToBoneIndex[node] = boneIdx;
                    skelMesh->skeleton.bones.push_back(b);
                }
            }

            // Fix parent indices
            for (size_t i = 0; i < scene->nodes.count; ++i) {
                ufbx_node* node = scene->nodes.data[i];
                auto bIt = nodeToBoneIndex.find(node);
                if (bIt != nodeToBoneIndex.end()) {
                    int pIdx = -1;
                    ufbx_node* pNode = node->parent;
                    while (pNode && !pNode->is_root) {
                        auto pIt = nodeToBoneIndex.find(pNode);
                        if (pIt != nodeToBoneIndex.end()) {
                            pIdx = pIt->second;
                            break;
                        }
                        pNode = pNode->parent;
                    }
                    skelMesh->skeleton.bones[bIt->second].parentIndex = pIdx;
                }
            }

            skelMesh->skeleton.BuildIndexMap();

            // 2. Load meshes & skin weights
            for (size_t m = 0; m < scene->meshes.count; ++m) {
                ufbx_mesh* uMesh = scene->meshes.data[m];
                if (!uMesh || uMesh->num_vertices == 0) continue;

                SkinnedSubmesh sub;
                sub.name = uMesh->name.data ? std::string(uMesh->name.data, uMesh->name.length) : ("Submesh_" + std::to_string(m));

                size_t numFaces = uMesh->num_faces;
                sub.bindVertices.reserve(uMesh->num_indices);
                sub.indices.reserve(uMesh->num_indices);

                // Check skin deformers
                ufbx_skin_deformer* skin = (uMesh->skin_deformers.count > 0) ? uMesh->skin_deformers.data[0] : nullptr;

                for (size_t f = 0; f < numFaces; ++f) {
                    ufbx_face face = uMesh->faces.data[f];
                    if (face.num_indices < 3) continue;

                    // Triangulate polygon fans
                    for (size_t vi = 1; vi + 1 < face.num_indices; ++vi) {
                        uint32_t fIndices[3] = {
                            face.index_begin,
                            (uint32_t)(face.index_begin + vi),
                            (uint32_t)(face.index_begin + vi + 1)
                        };

                        for (int k = 0; k < 3; ++k) {
                            uint32_t index = fIndices[k];
                            uint32_t vIndex = uMesh->vertex_indices.data[index];

                            SkinnedVertex sv;
                            ufbx_vec3 pos = ufbx_get_vertex_vec3(&uMesh->vertex_position, index);
                            sv.pos = UfbxToGlmVec3(pos);

                            if (uMesh->vertex_normal.exists) {
                                ufbx_vec3 n = ufbx_get_vertex_vec3(&uMesh->vertex_normal, index);
                                sv.normal = UfbxToGlmVec3(n);
                            } else {
                                sv.normal = glm::vec3(0, 1, 0);
                            }

                            if (uMesh->vertex_uv.exists) {
                                ufbx_vec2 uv = ufbx_get_vertex_vec2(&uMesh->vertex_uv, index);
                                sv.uv = glm::vec2((float)uv.x, (float)uv.y);
                            }

                            // Extract skin weights
                            if (skin && vIndex < skin->vertices.count) {
                                ufbx_skin_vertex skinV = skin->vertices.data[vIndex];
                                float weightSum = 0.0f;
                                for (size_t wi = 0; wi < skinV.num_weights && wi < 4; ++wi) {
                                    ufbx_skin_weight sw = skin->weights.data[skinV.weight_begin + wi];
                                    if (sw.cluster_index < skin->clusters.count) {
                                        ufbx_skin_cluster* cl = skin->clusters.data[sw.cluster_index];
                                        auto bIt = nodeToBoneIndex.find(cl->bone_node);
                                        if (bIt != nodeToBoneIndex.end()) {
                                            sv.boneIndices[wi] = (uint32_t)bIt->second;
                                            sv.boneWeights[wi] = (float)sw.weight;
                                            weightSum += (float)sw.weight;
                                        }
                                    }
                                }
                                if (weightSum > 0.0001f) {
                                    sv.boneWeights /= weightSum;
                                }
                            }

                            sub.indices.push_back((uint32_t)sub.bindVertices.size());
                            sub.bindVertices.push_back(sv);
                        }
                    }
                }

                // Initialize runtime vertices to bind pose
                sub.runtimeVertices.resize(sub.bindVertices.size());
                for (size_t i = 0; i < sub.bindVertices.size(); ++i) {
                    sub.runtimeVertices[i].pos = sub.bindVertices[i].pos;
                    sub.runtimeVertices[i].normal = sub.bindVertices[i].normal;
                    sub.runtimeVertices[i].uv = sub.bindVertices[i].uv;
                    sub.runtimeVertices[i].tangent = sub.bindVertices[i].tangent;
                }

                if (!sub.bindVertices.empty() && !sub.indices.empty()) {
                    skelMesh->submeshes.push_back(std::move(sub));
                }
            }

            skelMesh->valid = !skelMesh->submeshes.empty();
            ufbx_free_scene(scene);
        }
    }

    // Fallback: If imported through general mesh importer or has no skeleton, generate a default human/root bone rig
    if (!skelMesh->valid || skelMesh->skeleton.bones.empty()) {
        ImportedModel fallbackModel = MeshImporter::Load(normPath);
        if (fallbackModel.valid) {
            skelMesh->skeleton.bones.clear();

            // Create a clean base hierarchy
            Bone rootBone;
            rootBone.name = "Root";
            rootBone.parentIndex = -1;
            rootBone.localBindTransform = glm::mat4(1.0f);
            rootBone.inverseBindMatrix = glm::mat4(1.0f);
            skelMesh->skeleton.bones.push_back(rootBone);

            Bone spineBone;
            spineBone.name = "Spine";
            spineBone.parentIndex = 0;
            spineBone.localBindTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            spineBone.inverseBindMatrix = glm::inverse(spineBone.localBindTransform);
            skelMesh->skeleton.bones.push_back(spineBone);

            Bone headBone;
            headBone.name = "Head";
            headBone.parentIndex = 1;
            headBone.localBindTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.6f, 0.0f));
            headBone.inverseBindMatrix = glm::inverse(headBone.localBindTransform * spineBone.localBindTransform);
            skelMesh->skeleton.bones.push_back(headBone);

            skelMesh->skeleton.BuildIndexMap();

            // Convert imported meshes to skinned submeshes
            for (const auto& m : fallbackModel.meshes) {
                if (!m.valid || m.vertices.empty()) continue;
                SkinnedSubmesh sub;
                sub.name = m.name;
                sub.indices = m.indices;
                sub.material = m.material;

                sub.bindVertices.resize(m.vertices.size());
                sub.runtimeVertices = m.vertices;
                for (size_t i = 0; i < m.vertices.size(); ++i) {
                    sub.bindVertices[i].pos = m.vertices[i].pos;
                    sub.bindVertices[i].normal = m.vertices[i].normal;
                    sub.bindVertices[i].uv = m.vertices[i].uv;
                    sub.bindVertices[i].tangent = m.vertices[i].tangent;

                    // Assign root bone weighting
                    sub.bindVertices[i].boneIndices = glm::uvec4(0, 0, 0, 0);
                    sub.bindVertices[i].boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                }
                skelMesh->submeshes.push_back(std::move(sub));
            }
            skelMesh->valid = !skelMesh->submeshes.empty();
        }
    }

    m_meshCache[normPath] = skelMesh;
    return skelMesh;
}

std::shared_ptr<AnimationClip> SkeletalAnimationManager::LoadAnimationClip(const std::string& filePath, const Skeleton* targetSkeleton) {
    std::string normPath = filePath;
    std::replace(normPath.begin(), normPath.end(), '\\', '/');

    auto it = m_clipCache.find(normPath);
    if (it != m_clipCache.end()) return it->second;

    auto clip = std::make_shared<AnimationClip>();
    clip->sourcePath = normPath;
    clip->name = std::filesystem::path(normPath).stem().string();

    std::string ext = std::filesystem::path(normPath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".fbx") {
        ufbx_load_opts opts = { 0 };
        opts.target_axes = ufbx_axes_right_handed_y_up;
        opts.target_unit_meters = 1.0f;

        ufbx_error error;
        ufbx_scene* scene = ufbx_load_file(normPath.c_str(), &opts, &error);
        if (scene) {
            if (scene->anim_stacks.count > 0) {
                ufbx_anim_stack* stack = scene->anim_stacks.data[0];
                clip->duration = (float)(stack->time_end - stack->time_begin);
                if (clip->duration <= 0.01f) clip->duration = 1.0f;

                ufbx_anim* anim = stack->anim;
                float sampleRate = 30.0f; // 30 FPS sampling
                size_t numSamples = std::max((size_t)2, (size_t)std::ceil(clip->duration * sampleRate) + 1);

                for (size_t n = 0; n < scene->nodes.count; ++n) {
                    ufbx_node* node = scene->nodes.data[n];
                    if (node->is_root) continue;

                    std::string boneName = node->name.data ? std::string(node->name.data, node->name.length) : "";
                    if (boneName.empty()) continue;

                    AnimationTrack track;
                    track.boneName = boneName;
                    if (targetSkeleton) {
                        track.boneIndex = targetSkeleton->FindBoneIndex(boneName);
                    }

                    track.keys.reserve(numSamples);
                    for (size_t s = 0; s < numSamples; ++s) {
                        float t = (float)s / sampleRate;
                        if (t > clip->duration) t = clip->duration;

                        ufbx_transform xf = ufbx_evaluate_transform(anim, node, (double)t);
                        Keyframe kf;
                        kf.time = t;
                        kf.transform.translation = UfbxToGlmVec3(xf.translation);
                        kf.transform.rotation = UfbxToGlmQuat(xf.rotation);
                        kf.transform.scale = UfbxToGlmVec3(xf.scale);
                        track.keys.push_back(kf);
                    }

                    clip->tracks.push_back(std::move(track));
                }
            }
            ufbx_free_scene(scene);
        }
    }

    // Default procedural placeholder if no tracks found (e.g. idle breathing motion)
    if (clip->tracks.empty()) {
        clip->duration = 2.0f;
        AnimationTrack rootTrack;
        rootTrack.boneName = "Root";
        rootTrack.boneIndex = 0;

        float times[] = { 0.0f, 1.0f, 2.0f };
        for (float t : times) {
            Keyframe kf;
            kf.time = t;
            kf.transform.translation = glm::vec3(0.0f, std::sin(t * 3.14159f) * 0.05f, 0.0f);
            kf.transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            kf.transform.scale = glm::vec3(1.0f);
            rootTrack.keys.push_back(kf);
        }
        clip->tracks.push_back(rootTrack);
    }

    clip->BuildTrackMap();
    m_clipCache[normPath] = clip;
    return clip;
}

std::vector<std::string> SkeletalAnimationManager::FindProjectSkeletalMeshes(const std::string& contentDir) {
    std::vector<std::string> results;
    std::error_code ec;
    if (!std::filesystem::exists(contentDir, ec)) return results;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(contentDir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".obj") {
            std::string p = entry.path().string();
            std::replace(p.begin(), p.end(), '\\', '/');
            results.push_back(p);
        }
    }
    return results;
}

std::vector<std::string> SkeletalAnimationManager::FindProjectAnimations(const std::string& contentDir, const std::string& targetSkeletonMesh) {
    std::vector<std::string> results;
    std::error_code ec;
    if (!std::filesystem::exists(contentDir, ec)) return results;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(contentDir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".fbx" || ext == ".anim" || ext == ".eanim") {
            std::string p = entry.path().string();
            std::replace(p.begin(), p.end(), '\\', '/');

            // If metadata sidecar exists, check target skeleton match
            std::string metaPath = p + ".assetmeta";
            if (!targetSkeletonMesh.empty() && std::filesystem::exists(metaPath, ec)) {
                // If meta has skeleton, check if matches
                std::ifstream mf(metaPath);
                if (mf.is_open()) {
                    std::string mContent((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
                    if (mContent.find("targetSkeleton") != std::string::npos) {
                        if (mContent.find(targetSkeletonMesh) == std::string::npos) {
                            continue; // Associated with different skeleton
                        }
                    }
                }
            }

            results.push_back(p);
        }
    }
    return results;
}

} // namespace Eunoia
