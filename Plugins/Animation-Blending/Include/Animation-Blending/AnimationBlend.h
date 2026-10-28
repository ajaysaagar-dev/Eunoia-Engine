#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <Animation-Graph/SkeletalAnimation.h>

namespace Eunoia {

struct BlendAxis {
    std::string name = "Axis";
    float minVal = 0.0f;
    float maxVal = 100.0f;
    int divisions = 4;

    BlendAxis() = default;
    BlendAxis(const std::string& n, float minV, float maxV, int div = 4)
        : name(n), minVal(minV), maxVal(maxV), divisions(div) {}
};

struct BlendSamplePoint {
    uint32_t id = 0;
    std::string animationClipPath = "";
    std::string animationClipName = "";
    glm::vec2 coords{0.0f, 0.0f}; // In axis units (e.g. Speed 0..100, Direction -180..180)

    BlendSamplePoint() = default;
    BlendSamplePoint(uint32_t nid, const std::string& path, const std::string& clipName, const glm::vec2& c)
        : id(nid), animationClipPath(path), animationClipName(clipName), coords(c) {}
};

class AnimationBlend {
public:
    std::string name = "NewAnimationBlend";
    std::string assetPath = "";
    std::string skeletonMeshPath = "";
    BlendAxis xAxis{"Speed", 0.0f, 100.0f, 4};
    BlendAxis yAxis{"Direction", -180.0f, 180.0f, 4};
    std::vector<BlendSamplePoint> samplePoints;
    uint32_t nextSampleId = 1;

    AnimationBlend() = default;

    BlendSamplePoint& AddSamplePoint(const std::string& clipPath, const std::string& clipName, const glm::vec2& coords) {
        BlendSamplePoint pt;
        pt.id = nextSampleId++;
        pt.animationClipPath = clipPath;
        pt.animationClipName = clipName;
        pt.coords = coords;
        samplePoints.push_back(pt);
        return samplePoints.back();
    }

    void RemoveSamplePoint(uint32_t id) {
        samplePoints.erase(std::remove_if(samplePoints.begin(), samplePoints.end(),
            [&](const BlendSamplePoint& p) { return p.id == id; }), samplePoints.end());
    }

    BlendSamplePoint* FindSamplePoint(uint32_t id) {
        for (auto& p : samplePoints) {
            if (p.id == id) return &p;
        }
        return nullptr;
    }

    // Calculates normalized 2D Inverse Distance Weights (IDW) for all sample points
    std::vector<float> CalculateWeights(float x, float y) const {
        std::vector<float> weights(samplePoints.size(), 0.0f);
        if (samplePoints.empty()) return weights;
        if (samplePoints.size() == 1) {
            weights[0] = 1.0f;
            return weights;
        }

        float clampedX = glm::clamp(x, std::min(xAxis.minVal, xAxis.maxVal), std::max(xAxis.minVal, xAxis.maxVal));
        float clampedY = glm::clamp(y, std::min(yAxis.minVal, yAxis.maxVal), std::max(yAxis.minVal, yAxis.maxVal));

        float rangeX = std::max(std::abs(xAxis.maxVal - xAxis.minVal), 0.0001f);
        float rangeY = std::max(std::abs(yAxis.maxVal - yAxis.minVal), 0.0001f);

        float normX = (clampedX - xAxis.minVal) / rangeX;
        float normY = (clampedY - yAxis.minVal) / rangeY;

        float totalWeight = 0.0f;
        for (size_t i = 0; i < samplePoints.size(); ++i) {
            float sNormX = (samplePoints[i].coords.x - xAxis.minVal) / rangeX;
            float sNormY = (samplePoints[i].coords.y - yAxis.minVal) / rangeY;

            float dx = normX - sNormX;
            float dy = normY - sNormY;
            float distSq = dx * dx + dy * dy;

            if (distSq < 1e-6f) {
                // Exact match at sample point
                std::fill(weights.begin(), weights.end(), 0.0f);
                weights[i] = 1.0f;
                return weights;
            }

            float w = 1.0f / distSq;
            weights[i] = w;
            totalWeight += w;
        }

        if (totalWeight > 1e-6f) {
            for (float& w : weights) {
                w /= totalWeight;
            }
        }
        return weights;
    }

    // Evaluates the blended pose at coordinates (x, y) and phase normalizedTime [0..1]
    void EvaluatePose(float x, float y, float normalizedTime, const Skeleton& skeleton,
                      const std::unordered_map<std::string, std::shared_ptr<AnimationClip>>& loadedClips,
                      std::vector<BoneTransform>& outTransforms) const {
        size_t numBones = skeleton.bones.size();
        outTransforms.resize(numBones);

        // Initialize with bind pose
        for (size_t b = 0; b < numBones; ++b) {
            const auto& bone = skeleton.bones[b];
            BoneTransform def;
            def.translation = glm::vec3(bone.localBindTransform[3]);
            def.rotation = glm::quat_cast(bone.localBindTransform);
            def.scale = glm::vec3(
                glm::length(glm::vec3(bone.localBindTransform[0])),
                glm::length(glm::vec3(bone.localBindTransform[1])),
                glm::length(glm::vec3(bone.localBindTransform[2]))
            );
            outTransforms[b] = def;
        }

        if (samplePoints.empty()) return;

        std::vector<float> weights = CalculateWeights(x, y);

        // Collect samples with non-zero weight
        struct ActiveSample {
            std::shared_ptr<AnimationClip> clip;
            float weight;
        };
        std::vector<ActiveSample> activeSamples;
        for (size_t i = 0; i < samplePoints.size(); ++i) {
            if (weights[i] > 1e-4f) {
                auto it = loadedClips.find(samplePoints[i].animationClipPath);
                if (it != loadedClips.end() && it->second) {
                    activeSamples.push_back({ it->second, weights[i] });
                }
            }
        }

        if (activeSamples.empty()) return;

        // If only 1 active sample, sample directly
        if (activeSamples.size() == 1) {
            float time = normalizedTime * activeSamples[0].clip->duration;
            activeSamples[0].clip->SamplePose(time, skeleton, outTransforms, true);
            return;
        }

        // Multi-sample pose blending
        std::vector<std::vector<BoneTransform>> samplePoses(activeSamples.size());
        for (size_t s = 0; s < activeSamples.size(); ++s) {
            float time = normalizedTime * activeSamples[s].clip->duration;
            activeSamples[s].clip->SamplePose(time, skeleton, samplePoses[s], true);
        }

        // Blend all bone transforms
        for (size_t b = 0; b < numBones; ++b) {
            glm::vec3 blendedTrans(0.0f);
            glm::vec3 blendedScale(0.0f);
            glm::quat refRot = samplePoses[0][b].rotation;
            glm::quat blendedRot(0.0f, 0.0f, 0.0f, 0.0f);

            for (size_t s = 0; s < activeSamples.size(); ++s) {
                float w = activeSamples[s].weight;
                const auto& bt = samplePoses[s][b];

                blendedTrans += bt.translation * w;
                blendedScale += bt.scale * w;

                // Sign-align rotation to reference quaternion for correct geodesic blending
                glm::quat q = bt.rotation;
                if (glm::dot(q, refRot) < 0.0f) {
                    q = -q;
                }
                blendedRot.w += q.w * w;
                blendedRot.x += q.x * w;
                blendedRot.y += q.y * w;
                blendedRot.z += q.z * w;
            }

            float qLen = glm::length(blendedRot);
            if (qLen > 1e-5f) {
                blendedRot = glm::normalize(blendedRot);
            } else {
                blendedRot = refRot;
            }

            outTransforms[b].translation = blendedTrans;
            outTransforms[b].rotation = blendedRot;
            outTransforms[b].scale = blendedScale;
        }
    }

    bool SaveToFile(const std::string& filePath) {
        std::ofstream out(filePath);
        if (!out.is_open()) return false;

        out << "{\n";
        out << "  \"name\": \"" << name << "\",\n";
        out << "  \"skeletonMesh\": \"" << skeletonMeshPath << "\",\n";
        out << "  \"nextSampleId\": " << nextSampleId << ",\n";
        out << "  \"xAxis\": {\n";
        out << "    \"name\": \"" << xAxis.name << "\",\n";
        out << "    \"min\": " << xAxis.minVal << ",\n";
        out << "    \"max\": " << xAxis.maxVal << ",\n";
        out << "    \"divisions\": " << xAxis.divisions << "\n";
        out << "  },\n";
        out << "  \"yAxis\": {\n";
        out << "    \"name\": \"" << yAxis.name << "\",\n";
        out << "    \"min\": " << yAxis.minVal << ",\n";
        out << "    \"max\": " << yAxis.maxVal << ",\n";
        out << "    \"divisions\": " << yAxis.divisions << "\n";
        out << "  },\n";
        out << "  \"samples\": [\n";
        for (size_t i = 0; i < samplePoints.size(); ++i) {
            const auto& pt = samplePoints[i];
            out << "    {\n";
            out << "      \"id\": " << pt.id << ",\n";
            out << "      \"clipPath\": \"" << pt.animationClipPath << "\",\n";
            out << "      \"clipName\": \"" << pt.animationClipName << "\",\n";
            out << "      \"x\": " << pt.coords.x << ",\n";
            out << "      \"y\": " << pt.coords.y << "\n";
            out << "    }" << (i + 1 < samplePoints.size() ? "," : "") << "\n";
        }
        out << "  ]\n";
        out << "}\n";

        assetPath = filePath;
        return true;
    }

    bool LoadFromFile(const std::string& filePath) {
        std::ifstream in(filePath);
        if (!in.is_open()) return false;

        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();

        samplePoints.clear();
        nextSampleId = 1;

        auto extractString = [](const std::string& json, const std::string& key) -> std::string {
            std::string search = "\"" + key + "\":";
            size_t pos = json.find(search);
            if (pos == std::string::npos) return "";
            size_t start = json.find('\"', pos + search.size());
            if (start == std::string::npos) return "";
            size_t end = json.find('\"', start + 1);
            if (end == std::string::npos) return "";
            return json.substr(start + 1, end - start - 1);
        };

        auto extractNumber = [](const std::string& json, const std::string& key, double defVal) -> double {
            std::string search = "\"" + key + "\":";
            size_t pos = json.find(search);
            if (pos == std::string::npos) return defVal;
            size_t start = pos + search.size();
            while (start < json.size() && (json[start] == ' ' || json[start] == '\t' || json[start] == '\r' || json[start] == '\n' || json[start] == '\"')) start++;
            size_t end = start;
            while (end < json.size() && (json[end] == '-' || json[end] == '.' || (json[end] >= '0' && json[end] <= '9') || json[end] == 'e' || json[end] == 'E')) end++;
            if (start == end) return defVal;
            try { return std::stod(json.substr(start, end - start)); } catch (...) { return defVal; }
        };

        name = extractString(content, "name");
        skeletonMeshPath = extractString(content, "skeletonMesh");
        nextSampleId = (uint32_t)extractNumber(content, "nextSampleId", 1.0);

        // X Axis
        size_t xPos = content.find("\"xAxis\":");
        if (xPos != std::string::npos) {
            size_t blockEnd = content.find('}', xPos);
            std::string xBlock = content.substr(xPos, blockEnd - xPos + 1);
            xAxis.name = extractString(xBlock, "name");
            xAxis.minVal = (float)extractNumber(xBlock, "min", 0.0);
            xAxis.maxVal = (float)extractNumber(xBlock, "max", 100.0);
            xAxis.divisions = (int)extractNumber(xBlock, "divisions", 4.0);
        }

        // Y Axis
        size_t yPos = content.find("\"yAxis\":");
        if (yPos != std::string::npos) {
            size_t blockEnd = content.find('}', yPos);
            std::string yBlock = content.substr(yPos, blockEnd - yPos + 1);
            yAxis.name = extractString(yBlock, "name");
            yAxis.minVal = (float)extractNumber(yBlock, "min", -180.0);
            yAxis.maxVal = (float)extractNumber(yBlock, "max", 180.0);
            yAxis.divisions = (int)extractNumber(yBlock, "divisions", 4.0);
        }

        // Samples
        size_t samplesPos = content.find("\"samples\":");
        if (samplesPos != std::string::npos) {
            size_t arrStart = content.find('[', samplesPos);
            size_t arrEnd = content.find(']', arrStart);
            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string arrContent = content.substr(arrStart, arrEnd - arrStart);
                size_t objStart = 0;
                while ((objStart = arrContent.find('{', objStart)) != std::string::npos) {
                    size_t objEnd = arrContent.find('}', objStart);
                    if (objEnd == std::string::npos) break;
                    std::string objStr = arrContent.substr(objStart, objEnd - objStart + 1);

                    BlendSamplePoint pt;
                    pt.id = (uint32_t)extractNumber(objStr, "id", 0.0);
                    pt.animationClipPath = extractString(objStr, "clipPath");
                    pt.animationClipName = extractString(objStr, "clipName");
                    pt.coords.x = (float)extractNumber(objStr, "x", 0.0);
                    pt.coords.y = (float)extractNumber(objStr, "y", 0.0);

                    if (pt.id != 0 && !pt.animationClipPath.empty()) {
                        samplePoints.push_back(pt);
                    }
                    objStart = objEnd + 1;
                }
            }
        }

        assetPath = filePath;
        return true;
    }
};

} // namespace Eunoia
