#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <EngineAssets/Geometry.h>
#include <EngineAssets/MeshImporter.h>

namespace Eunoia {

struct Bone {
    std::string name;
    int parentIndex = -1;
    glm::mat4 localBindTransform{1.0f};
    glm::mat4 inverseBindMatrix{1.0f};
};

struct Skeleton {
    std::vector<Bone> bones;
    std::unordered_map<std::string, int> boneNameToIndex;

    int FindBoneIndex(const std::string& name) const {
        auto it = boneNameToIndex.find(name);
        return it != boneNameToIndex.end() ? it->second : -1;
    }

    void BuildIndexMap() {
        boneNameToIndex.clear();
        for (int i = 0; i < (int)bones.size(); ++i) {
            boneNameToIndex[bones[i].name] = i;
        }
    }
};

struct BoneTransform {
    glm::vec3 translation{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // w, x, y, z
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    glm::mat4 ToMatrix() const {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), translation);
        glm::mat4 r = glm::mat4_cast(rotation);
        glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
        return t * r * s;
    }

    static BoneTransform Lerp(const BoneTransform& a, const BoneTransform& b, float alpha) {
        alpha = glm::clamp(alpha, 0.0f, 1.0f);
        BoneTransform res;
        res.translation = glm::mix(a.translation, b.translation, alpha);
        res.rotation = glm::slerp(a.rotation, b.rotation, alpha);
        res.scale = glm::mix(a.scale, b.scale, alpha);
        return res;
    }
};

struct Keyframe {
    float time = 0.0f; // seconds
    BoneTransform transform;
};

struct AnimationTrack {
    std::string boneName;
    int boneIndex = -1;
    std::vector<Keyframe> keys;

    BoneTransform Sample(float time, float duration, bool loop) const {
        if (keys.empty()) return BoneTransform{};
        if (keys.size() == 1) return keys[0].transform;

        if (loop && duration > 0.0001f) {
            time = std::fmod(time, duration);
            if (time < 0.0f) time += duration;
        } else {
            time = glm::clamp(time, 0.0f, duration);
        }

        if (time <= keys.front().time) return keys.front().transform;
        if (time >= keys.back().time) return keys.back().transform;

        // Binary search for key pair
        size_t low = 0;
        size_t high = keys.size() - 1;
        while (low + 1 < high) {
            size_t mid = (low + high) / 2;
            if (keys[mid].time <= time) low = mid;
            else high = mid;
        }

        float t0 = keys[low].time;
        float t1 = keys[high].time;
        float factor = (t1 > t0) ? (time - t0) / (t1 - t0) : 0.0f;
        return BoneTransform::Lerp(keys[low].transform, keys[high].transform, factor);
    }
};

struct AnimationClip {
    std::string name;
    std::string sourcePath;
    float duration = 1.0f; // seconds
    float ticksPerSecond = 30.0f;
    std::vector<AnimationTrack> tracks;
    std::unordered_map<std::string, size_t> trackMap;

    void BuildTrackMap() {
        trackMap.clear();
        for (size_t i = 0; i < tracks.size(); ++i) {
            trackMap[tracks[i].boneName] = i;
        }
    }

    void SamplePose(float time, const Skeleton& skeleton, std::vector<BoneTransform>& outLocalTransforms, bool loop = true) const {
        outLocalTransforms.resize(skeleton.bones.size());

        // Default to bind pose
        for (size_t i = 0; i < skeleton.bones.size(); ++i) {
            const auto& b = skeleton.bones[i];
            BoneTransform def;
            def.translation = glm::vec3(b.localBindTransform[3]);
            def.rotation = glm::quat_cast(b.localBindTransform);
            def.scale = glm::vec3(
                glm::length(glm::vec3(b.localBindTransform[0])),
                glm::length(glm::vec3(b.localBindTransform[1])),
                glm::length(glm::vec3(b.localBindTransform[2]))
            );
            outLocalTransforms[i] = def;
        }

        // Apply animated tracks
        for (const auto& track : tracks) {
            int bIdx = track.boneIndex;
            if (bIdx < 0 || bIdx >= (int)skeleton.bones.size()) {
                bIdx = skeleton.FindBoneIndex(track.boneName);
            }
            if (bIdx >= 0 && bIdx < (int)outLocalTransforms.size()) {
                outLocalTransforms[bIdx] = track.Sample(time, duration, loop);
            }
        }
    }
};

struct SkinnedVertex {
    glm::vec3 pos{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    glm::uvec4 boneIndices{0, 0, 0, 0};
    glm::vec4 boneWeights{0.0f, 0.0f, 0.0f, 0.0f};
};

struct SkinnedSubmesh {
    std::string name;
    std::vector<SkinnedVertex> bindVertices;
    std::vector<MeshVertex> runtimeVertices;
    std::vector<uint32_t> indices;
    ImportedMaterial material;
};

class SkeletalMesh {
public:
    std::string name;
    std::string sourcePath;
    Skeleton skeleton;
    std::vector<SkinnedSubmesh> submeshes;
    bool valid = false;

    // Bounding volume for framing and bounds calculation
    glm::vec3 aabbMin{0.0f};
    glm::vec3 aabbMax{0.0f};
    glm::vec3 aabbCenter{0.0f};
    float boundingRadius = 1.0f;

    // Evaluates final bone transformation matrices: Matrix = WorldPose * InverseBindMatrix
    void ComputeBoneMatrices(const std::vector<BoneTransform>& localTransforms, std::vector<glm::mat4>& outBoneMatrices) const {
        size_t count = skeleton.bones.size();
        outBoneMatrices.resize(count, glm::mat4(1.0f));
        std::vector<glm::mat4> worldTransforms(count, glm::mat4(1.0f));

        for (size_t i = 0; i < count; ++i) {
            glm::mat4 localMat = (i < localTransforms.size()) ? localTransforms[i].ToMatrix() : skeleton.bones[i].localBindTransform;
            int parent = skeleton.bones[i].parentIndex;
            if (parent >= 0 && parent < (int)count) {
                worldTransforms[i] = worldTransforms[parent] * localMat;
            } else {
                worldTransforms[i] = localMat;
            }
            outBoneMatrices[i] = worldTransforms[i] * skeleton.bones[i].inverseBindMatrix;
        }
    }

    // CPU skinning deformation for real-time live preview
    void DeformMesh(const std::vector<glm::mat4>& boneMatrices) {
        if (boneMatrices.empty()) return;

        for (auto& sub : submeshes) {
            size_t vCount = sub.bindVertices.size();
            sub.runtimeVertices.resize(vCount);

            for (size_t i = 0; i < vCount; ++i) {
                const auto& bv = sub.bindVertices[i];
                auto& rv = sub.runtimeVertices[i];

                glm::mat4 skinMat(0.0f);
                float totalWeight = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    float w = bv.boneWeights[k];
                    if (w > 0.0001f) {
                        uint32_t bIdx = bv.boneIndices[k];
                        if (bIdx < boneMatrices.size()) {
                            skinMat += boneMatrices[bIdx] * w;
                            totalWeight += w;
                        }
                    }
                }

                if (totalWeight < 0.0001f) {
                    skinMat = glm::mat4(1.0f);
                }

                glm::vec4 deformedPos = skinMat * glm::vec4(bv.pos, 1.0f);
                glm::mat3 normalMat = glm::mat3(skinMat);
                glm::vec3 dn = normalMat * bv.normal;
                float dnLen = glm::length(dn);
                glm::vec3 deformedNorm = (dnLen > 1e-6f) ? (dn / dnLen) : glm::vec3(0.0f, 1.0f, 0.0f);

                rv.pos = glm::vec3(deformedPos);
                rv.normal = deformedNorm;
                rv.uv = bv.uv;
                rv.tangent = bv.tangent;
            }
        }
    }
};

class SkeletalAnimationManager {
public:
    static SkeletalAnimationManager& Get();

    std::shared_ptr<SkeletalMesh> LoadSkeletalMesh(const std::string& filePath);
    std::shared_ptr<AnimationClip> LoadAnimationClip(const std::string& filePath, const Skeleton* targetSkeleton = nullptr);
    
    // Check whether a 3D model file contains skeleton bones / animation
    static bool FileHasBones(const std::string& filePath, bool* outHasAnimation = nullptr);

    // Discover all skeletal meshes and animation clips in project Content directory
    std::vector<std::string> FindProjectSkeletalMeshes(const std::string& contentDir);
    std::vector<std::string> FindProjectAnimations(const std::string& contentDir, const std::string& targetSkeletonMesh = "");

    void ClearCache();

private:
    SkeletalAnimationManager() = default;
    ~SkeletalAnimationManager() = default;

    std::unordered_map<std::string, std::shared_ptr<SkeletalMesh>> m_meshCache;
    std::unordered_map<std::string, std::shared_ptr<AnimationClip>> m_clipCache;
};

} // namespace Eunoia
