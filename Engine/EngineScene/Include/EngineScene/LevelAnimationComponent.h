#pragma once

#include <string>
#include <memory>
#include <Level-Animation/LevelAnimation.h>

struct GameObject;
class Scene;

namespace Eunoia {

struct LevelAnimationComponent {
    bool enabled = true;
    bool playOnStart = true; // By default true as specified
    bool loop = true;
    float playbackSpeed = 1.0f;
    std::string animationPath = "";
    float currentTime = 0.0f;
    bool isPlaying = false;
    std::shared_ptr<LevelAnimation> animation;

    void LoadAnimation(const std::string& path) {
        animationPath = path;
        animation = std::make_shared<LevelAnimation>();
        if (!animation->LoadFromFile(path)) {
            animation.reset();
        } else {
            loop = animation->loop;
        }
    }

    void Play() { isPlaying = true; }
    void Pause() { isPlaying = false; }
    void Stop() { isPlaying = false; currentTime = 0.0f; }

    void Update(float dt, GameObject* owner, Scene* scene);
};

} // namespace Eunoia
