#pragma once
#include <string>
#include <algorithm>

enum class AssetType {
    Unknown,
    Texture,
    Material,
    Mesh,
    SkeletalMesh,
    Animation,
    Audio,
    Level,
    Scene = Level,
    Prefab,
    Shader,
    Font,
    Particle,
    Script,
    BehaviourScript = Script,
    Behaviour = Script,
    Blueprint = Script,
    VisualBehaviour,
    NodeBehaviour,
    Skybox,
    Water,
    UserInterface,
    UI = UserInterface,
    UserInterface3D,
    UI3D = UserInterface3D,
    Data,
    Project,
    AnimatorGraph,
    AnimationGraph = AnimatorGraph,
    AnimationBlend,
    BlendSpace = AnimationBlend,
    LevelAnimation,
    LevelSequence = LevelAnimation
};

inline const char* AssetTypeToString(AssetType type) {
    switch (type) {
        case AssetType::Texture:         return "Texture";
        case AssetType::Material:        return "Material";
        case AssetType::Mesh:            return "Mesh";
        case AssetType::SkeletalMesh:    return "SkeletalMesh";
        case AssetType::Animation:       return "Animation";
        case AssetType::Audio:           return "Audio";
        case AssetType::Level:           return "Level";
        case AssetType::Prefab:          return "Prefab";
        case AssetType::Shader:          return "Shader";
        case AssetType::Font:            return "Font";
        case AssetType::Particle:        return "Particle";
        case AssetType::Script:          return "Behaviour Script";
        case AssetType::VisualBehaviour: return "Visual Behaviour";
        case AssetType::NodeBehaviour:   return "Node Behaviour";
        case AssetType::Skybox:          return "Skybox";
        case AssetType::Water:           return "Water Body";
        case AssetType::UserInterface:   return "User Interface";
        case AssetType::UserInterface3D: return "User Interface 3D";
        case AssetType::Data:            return "Data";
        case AssetType::Project:         return "Project";
        case AssetType::AnimatorGraph:   return "Animator Graph";
        case AssetType::AnimationBlend:  return "Animation Blend";
        case AssetType::LevelAnimation:  return "Level Animation";
        default:                         return "Unknown";
    }
}

inline AssetType StringToAssetType(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "texture")      return AssetType::Texture;
    if (s == "material")     return AssetType::Material;
    if (s == "mesh")         return AssetType::Mesh;
    if (s == "skeletalmesh") return AssetType::SkeletalMesh;
    if (s == "animation")    return AssetType::Animation;
    if (s == "audio")        return AssetType::Audio;
    if (s == "level" || s == "scene") return AssetType::Level;
    if (s == "prefab")       return AssetType::Prefab;
    if (s == "shader")       return AssetType::Shader;
    if (s == "font")         return AssetType::Font;
    if (s == "particle")     return AssetType::Particle;
    if (s == "script" || s == "behaviourscript" || s == "behaviorscript" || s == "behaviour script" || s == "behavior script" || s == "behaviour" || s == "behavior" || s == "blueprint" || s == "actorblueprint") return AssetType::Script;
    if (s == "visualbehaviour" || s == "visual-behaviour" || s == "visual behaviour" || s == "ebehaviour") return AssetType::VisualBehaviour;
    if (s == "nodebehaviour" || s == "node-behaviour" || s == "node behaviour" || s == "enb") return AssetType::NodeBehaviour;
    if (s == "skybox" || s == "hdrskybox") return AssetType::Skybox;
    if (s == "water")        return AssetType::Water;
    if (s == "userinterface3d" || s == "user_interface_3d" || s == "user interface 3d" || s == "ui3d" || s == "eui3d") return AssetType::UserInterface3D;
    if (s == "userinterface" || s == "user_interface" || s == "user interface" || s == "ui" || s == "eui") return AssetType::UserInterface;
    if (s == "data")         return AssetType::Data;
    if (s == "project" || s == "euproject" || s == "eproject") return AssetType::Project;
    if (s == "animatorgraph" || s == "animator-graph" || s == "animator graph" || s == "eanimator" || s == "animator") return AssetType::AnimatorGraph;
    if (s == "animationblend" || s == "animation-blend" || s == "animation blend" || s == "eblend" || s == "blend") return AssetType::AnimationBlend;
    if (s == "levelanimation" || s == "level-animation" || s == "level animation" || s == "elanim") return AssetType::LevelAnimation;
    return AssetType::Unknown;
}

inline AssetType DetectAssetTypeFromExtension(const std::string& ext) {
    std::string e = ext;
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    if (!e.empty() && e[0] == '.') e.erase(0, 1);

    if (e.empty() || e == "assetmeta" || e == "meta" || e == "metadata" || e == "json" || e == "elogs" || e == "log") {
        return AssetType::Unknown;
    }

    if (e == "png" || e == "jpg" || e == "jpeg" || e == "bmp" || e == "tga" || e == "dds" || e == "hdr" || e == "exr") {
        return AssetType::Texture;
    }
    if (e == "emat" || e == "mat") {
        return AssetType::Material;
    }
    if (e == "eskybox" || e == "ehdr" || e == "skybox") {
        return AssetType::Skybox;
    }
    if (e == "water") {
        return AssetType::Water;
    }
    if (e == "eui3d" || e == "ui3d") {
        return AssetType::UserInterface3D;
    }
    if (e == "eui" || e == "ui") {
        return AssetType::UserInterface;
    }
    if (e == "obj" || e == "fbx" || e == "gltf" || e == "glb") {
        return AssetType::Mesh;
    }
    if (e == "wav" || e == "mp3" || e == "ogg") {
        return AssetType::Audio;
    }
    if (e == "level" || e == "elevel" || e == "scene" || e == "escene" || e == "umap") {
        return AssetType::Level;
    }
    if (e == "prefab") {
        return AssetType::Prefab;
    }
    if (e == "hlsl" || e == "glsl" || e == "spv" || e == "vert" || e == "frag") {
        return AssetType::Shader;
    }
    if (e == "ttf" || e == "otf") {
        return AssetType::Font;
    }
    if (e == "enb" || e == "nodebehaviour") {
        return AssetType::NodeBehaviour;
    }
    if (e == "ebehaviour" || e == "visualbehaviour") {
        return AssetType::VisualBehaviour;
    }
    if (e == "elanim") {
        return AssetType::LevelAnimation;
    }
    if (e == "eblend" || e == "blend") {
        return AssetType::AnimationBlend;
    }
    if (e == "eanimator" || e == "animator") {
        return AssetType::AnimatorGraph;
    }
    if (e == "anim" || e == "eanim") {
        return AssetType::Animation;
    }
    if (e == "cpp" || e == "h") {
        return AssetType::Script;
    }
    if (e == "euproject" || e == "eproject") {
        return AssetType::Project;
    }
    return AssetType::Unknown;
}

inline const char* GetAssetTypeIcon(AssetType type) {
    switch (type) {
        case AssetType::Texture:         return "🖼";
        case AssetType::Material:        return "🎨";
        case AssetType::Mesh:            return "🧊";
        case AssetType::SkeletalMesh:    return "🦴";
        case AssetType::Animation:       return "🏃";
        case AssetType::Audio:           return "🔊";
        case AssetType::Level:           return "🌐";
        case AssetType::Prefab:          return "📦";
        case AssetType::Shader:          return "⚡";
        case AssetType::Font:            return "🔤";
        case AssetType::Particle:        return "✨";
        case AssetType::Script:          return "📜";
        case AssetType::VisualBehaviour: return "🎮";
        case AssetType::NodeBehaviour:   return "⚡";
        case AssetType::Skybox:          return "🌌";
        case AssetType::Water:           return "🌊";
        case AssetType::UserInterface:   return "🖥";
        case AssetType::UserInterface3D: return "🔮";
        case AssetType::Data:            return "📊";
        case AssetType::Project:         return "📁";
        case AssetType::LevelAnimation:  return "🎬";
        default:                         return "📄";
    }
}
