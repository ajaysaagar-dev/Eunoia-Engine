#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <Level-Animation/LevelAnimation.h>

struct GameObject;
class Scene;

namespace Eunoia {

class LevelAnimationManager {
public:
    static LevelAnimationManager& Get();

    std::shared_ptr<LevelAnimation> LoadLevelAnimation(const std::string& filePath);
    std::string CreateNewLevelAnimation(const std::string& folderPath, const std::string& name);

    // Active animation being edited in Animation timeline
    std::shared_ptr<LevelAnimation> GetActiveAnimation() { return m_activeAnimation; }
    void SetActiveAnimation(std::shared_ptr<LevelAnimation> anim) { m_activeAnimation = anim; }
    int GetActiveActorId() const { return m_activeActorId; }
    void SetActiveActorId(int id) { m_activeActorId = id; }

    float GetCurrentPlayheadTime() const { return m_currentPlayheadTime; }
    void SetCurrentPlayheadTime(float t) { m_currentPlayheadTime = t; }

    // Recording / Auto-Keying
    bool IsRecording() const { return m_isRecording; }
    void SetRecording(bool rec) { m_isRecording = rec; }

    void RecordProperty(Scene& scene, int objectId, PropertyChannelType channel, float value);
    void RecordTransform(Scene& scene, GameObject& obj);
    void RecordLight(Scene& scene, GameObject& obj);
    void RecordCamera(Scene& scene, GameObject& obj);
    void RecordMaterial(Scene& scene, GameObject& obj);

private:
    LevelAnimationManager() = default;
    ~LevelAnimationManager() = default;

    std::unordered_map<std::string, std::shared_ptr<LevelAnimation>> m_loadedAnimations;
    std::shared_ptr<LevelAnimation> m_activeAnimation;
    int m_activeActorId = -1;
    float m_currentPlayheadTime = 0.0f;
    bool m_isRecording = false;
};

} // namespace Eunoia
