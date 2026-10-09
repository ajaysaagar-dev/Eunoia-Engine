#include <EngineCore/Log.h>
#include <EnginePlatform/Window.h>
#include <EnginePlatform/InputSystem.h>
#include <EngineScene/Scene.h>
#include <EngineScene/SceneSerializer.h>
#include <EngineScene/ScreenPrint.h>
#include <EngineAssets/AssetManager.h>
#include <EngineRenderer/Camera.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <filesystem>

void AddEngineLog(const std::string& category, const std::string& message, int level) {
    std::cout << "[" << category << "] " << message << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "========================================================\n";
    std::cout << "[Eunoia Runtime] Initializing Standalone Game Runtime...\n";
    std::cout << "========================================================\n";

    // 1. Initialize Window
    EnginePlatform::WindowConfig winConfig;
    winConfig.title = "Eunoia Engine - Standalone Game";
    winConfig.width = 1280;
    winConfig.height = 720;
    winConfig.resizable = true;

    EnginePlatform::Window window;
    if (!window.Create(winConfig)) {
        std::cerr << "[Eunoia Runtime] Failed to create game window!\n";
        return -1;
    }

    // 2. Setup Input System
    InputSystem::Get().SetWindow(window.Handle());
    InputSystem::Get().SetupDefaultActions();

    // 3. Setup Level
    Level level;

    // Check Data/GameConfig.json for startupLevel
    std::string configLevelPath = "";
    std::error_code ec;
    std::vector<std::string> cfgCandidates = {
        "Data/GameConfig.json",
        "GameConfig.json",
        "Data/Config.json"
    };
    for (const auto& cfgPath : cfgCandidates) {
        if (std::filesystem::exists(cfgPath, ec)) {
            std::ifstream cf(cfgPath);
            if (cf.is_open()) {
                std::string line;
                while (std::getline(cf, line)) {
                    size_t pos = line.find("\"startupLevel\"");
                    if (pos == std::string::npos) pos = line.find("\"defaultLevel\"");
                    if (pos != std::string::npos) {
                        size_t colon = line.find(':', pos);
                        if (colon != std::string::npos) {
                            size_t q1 = line.find('\"', colon);
                            size_t q2 = (q1 != std::string::npos) ? line.find('\"', q1 + 1) : std::string::npos;
                            if (q1 != std::string::npos && q2 != std::string::npos) {
                                configLevelPath = line.substr(q1 + 1, q2 - q1 - 1);
                                break;
                            }
                        }
                    }
                }
            }
            if (!configLevelPath.empty()) break;
        }
    }

    // Search for levels in configured path, cooked content or local directories
    std::vector<std::string> searchPaths;
    if (!configLevelPath.empty()) {
        searchPaths.push_back(configLevelPath);
        searchPaths.push_back("Data/" + configLevelPath);
        searchPaths.push_back("Data/Content/" + configLevelPath);
        searchPaths.push_back("Content/" + configLevelPath);
    }
    searchPaths.push_back("Data/Content/Levels/Main.elevel");
    searchPaths.push_back("Content/Levels/Main.elevel");
    searchPaths.push_back("Data/Cooked/Levels/Main.elevel");
    searchPaths.push_back("Cooked/Levels/Main.elevel");
    searchPaths.push_back("Levels/Main.elevel");
    searchPaths.push_back("Main.elevel");
    searchPaths.push_back("Data/Content/Scenes/Main.escene");
    searchPaths.push_back("Cooked/Scenes/Main.escene");
    searchPaths.push_back("Cooked/Main.escene");
    searchPaths.push_back("Scenes/Main.escene");
    searchPaths.push_back("Content/Scenes/Main.escene");
    searchPaths.push_back("Main.escene");

    bool loaded = false;
    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path, ec)) {
            std::cout << "[Eunoia Runtime] Loading level: " << path << std::endl;
            if (LevelSerializer::LoadLevel(level, path)) {
                loaded = true;
                break;
            }
        }
    }

    if (!loaded) {
        std::cout << "[Eunoia Runtime] No packaged level found; initializing default game level." << std::endl;
        level.LoadDefaultLevel();
    }

    std::cout << "[Eunoia Runtime] Level loaded with " << level.objects.size() << " actors.\n";

    // Start play mode (initializes behaviours, resolves references, calls OnCreate)
    level.StartPlayMode();

    std::cout << "[Eunoia Runtime] Entering game loop. Close window to exit." << std::endl;

    auto lastTime = std::chrono::high_resolution_clock::now();

    // 4. Game Simulation Loop
    while (!window.ShouldClose()) {
        InputSystem::Get().BeginFrame();
        window.PollEvents();
        InputSystem::Get().Update(window.Handle());

        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        if (deltaTime > 0.1f) deltaTime = 0.1f;

        // Update ScreenPrint system
        ScreenPrint::System::Get().Update(deltaTime);

        // Update Level behaviours and physics
        level.Update(deltaTime);

        // Frame timing throttle (~60 FPS)
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // 5. Teardown
    level.StopPlayMode();

    window.Destroy();
    glfwTerminate();

    std::cout << "[Eunoia Runtime] Game shut down cleanly.\n";
    return 0;
}
