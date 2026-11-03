#include <Editor/EngineUI.h>
#include <Editor/UndoManager.h>
#include <Editor/EditorGizmoSystem.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cctype>
#include <windows.h>
#include <commdlg.h>
#ifdef GetClassName
#undef GetClassName
#endif
#include <map>
#include <EngineAssets/TextureManager.h>
#include <EngineAssets/DirectXTex.h>
#include <EngineAssets/MeshImporter.h>
#include <EngineCore/EngineLogger.h>
#include <EngineScene/BehaviourRegistry.h>
#include <EnginePlatform/InputSystem.h>
#include <EngineScene/ScreenPrint.h>
#include <shellapi.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>
#include <Water/WaterSystem.h>
#include <UISystem/UIEditorWindow.h>
#include <UISystem/UISystem.h>
#include <Collisions/CollisionTypes.h>
#include <Collisions/CollisionSystem.h>
#include <PostProcess/PostProcessSystem.h>
#include <TextureStreaming/TextureStreamingSystem.h>
#include <Node-Behaviour/NodeBehaviourManager.h>
#include <Node-Behaviour/NodeBehaviourInstance.h>
#include <SFLOD/SFLODSystem.h>
#include <Animation-Graph/AnimatorGraphEditor.h>
#include <Animation-Blending/AnimationBlendManager.h>
#include <EngineAssets/SkeletalAnimation.h>
#include <Level-Animation/LevelAnimation.h>
#include <Level-Animation/LevelAnimationEditor.h>
#include <Level-Animation/LevelAnimationManager.h>
void EngineUI::RenderProjectBrowser(Scene& scene, OrbitCamera& camera) {
    if (!showProjectBrowser) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();

    // 1. Dark semi-transparent backdrop
    ImDrawList* bgDrawList = ImGui::GetForegroundDrawList();
    bgDrawList->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(8, 12, 18, 220));

    // 2. Centered Window
    float winW = 820.0f;
    float winH = 540.0f;
    ImVec2 centerPos(vp->Pos.x + (vp->Size.x - winW) * 0.5f, vp->Pos.y + (vp->Size.y - winH) * 0.5f);
    ImGui::SetNextWindowPos(centerPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 18.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.12f, 0.60f, 0.95f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.118f, 0.118f, 0.118f, 0.98f));

    bool open = true;
    bool hasActiveProject = !activeProjectRoot.empty();

    if (ImGui::Begin("Project Browser###ProjectBrowserModal", hasActiveProject ? &open : nullptr, flags)) {
        if (!open) {
            showProjectBrowser = false;
        }

        // Title Header
        ImGui::TextColored(ImVec4(0.12f, 0.68f, 1.00f, 1.0f), "EUNOIA ENGINE");
        ImGui::SameLine();
        ImGui::TextDisabled("ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â Project Browser");
        if (hasActiveProject) {
            ImGui::SameLine(winW - 140.0f);
            if (ImGui::SmallButton("ÃƒÂ¢Ã…â€œÃ¢â‚¬â€œ Return to Editor")) {
                showProjectBrowser = false;
            }
        }

        ImGui::TextWrapped("Select a previously created project, or configure and create a new empty game project.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Top Tabs: [ Recent Projects ] | [ New Project ]
        bool tab0Active = (projectBrowserTab == 0);
        if (tab0Active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.50f, 0.90f, 1.0f));
        std::string recentTabLabel = "Recent Projects (" + std::to_string(recentProjects.size()) + ")";
        if (ImGui::Button(recentTabLabel.c_str(), ImVec2(180, 32))) {
            projectBrowserTab = 0;
        }
        if (tab0Active) ImGui::PopStyleColor();

        ImGui::SameLine();
        bool tab1Active = (projectBrowserTab == 1);
        if (tab1Active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.50f, 0.90f, 1.0f));
        if (ImGui::Button("New Project", ImVec2(150, 32))) {
            projectBrowserTab = 1;
        }
        if (tab1Active) ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::Button("Browse for Project...", ImVec2(170, 32))) {
            std::string selectedFolder = ShowSelectFolderDialog(nullptr, "Select Existing Project Folder");
            if (!selectedFolder.empty()) {
                LoadProject(selectedFolder, scene, camera);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (projectBrowserTab == 0) {
            // =================================================================
            // TAB 0: RECENT PROJECTS (PREVIOUSLY CREATED PROJECTS)
            // =================================================================
            ImGui::SetNextItemWidth(winW - 220.0f);
            ImGui::InputTextWithHint("##ProjFilter", "Filter projects...", projectBrowserSearchBuf, sizeof(projectBrowserSearchBuf));
            ImGui::SameLine();
            if (ImGui::Button("Refresh List", ImVec2(140, 0))) {
                LoadRecentProjects();
            }

            ImGui::Spacing();

            // Projects List Child Window
            ImGui::BeginChild("RecentProjectsScroll", ImVec2(0, winH - 240.0f), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            if (recentProjects.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("No recent projects found.");
                ImGui::Text("Click 'New Project' above to create your first game project!");
            } else {
                for (size_t i = 0; i < recentProjects.size(); ++i) {
                    const auto& p = recentProjects[i];

                    // Filter search
                    if (strlen(projectBrowserSearchBuf) > 0) {
                        std::string searchLower = projectBrowserSearchBuf;
                        std::string nameLower = p.name;
                        for (auto& c : searchLower) c = tolower(c);
                        for (auto& c : nameLower) c = tolower(c);
                        if (nameLower.find(searchLower) == std::string::npos && p.rootPath.find(projectBrowserSearchBuf) == std::string::npos) {
                            continue;
                        }
                    }

                    ImGui::PushID((int)i);
                    bool isSelected = (selectedProjectIndex == (int)i);

                    ImVec2 curPos = ImGui::GetCursorPos();
                    ImVec2 itemSize(ImGui::GetContentRegionAvail().x, 48.0f);
                    ImVec2 screenPos = ImGui::GetCursorScreenPos();
                    ImVec2 pMin = screenPos;
                    ImVec2 pMax = ImVec2(screenPos.x + itemSize.x, screenPos.y + itemSize.y);

                    // Full-width invisible button to reliably capture clicks anywhere on the card
                    ImGui::InvisibleButton("##InEditorProjSelBtn", itemSize);
                    bool isHovered = ImGui::IsItemHovered();
                    if (ImGui::IsItemClicked()) {
                        selectedProjectIndex = (int)i;
                    }
                    if (isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        LoadProject(p.rootPath, scene, camera);
                    }

                    // Background styling
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 bgCol = isSelected ? IM_COL32(30, 95, 195, 180)
                                             : (isHovered ? IM_COL32(42, 52, 70, 160) : IM_COL32(24, 28, 38, 120));
                    ImU32 borderCol = isSelected ? IM_COL32(50, 160, 255, 230)
                                                 : (isHovered ? IM_COL32(70, 130, 220, 150) : IM_COL32(40, 45, 58, 100));
                    dl->AddRectFilled(pMin, pMax, bgCol, 4.0f);
                    dl->AddRect(pMin, pMax, borderCol, 4.0f, 0, isSelected ? 2.0f : 1.0f);

                    // Render text inside selectable card area using draw list (prevents SetCursorPos assertion)
                    dl->AddText(ImVec2(pMin.x + 12.0f, pMin.y + 6.0f), IM_COL32(50, 204, 255, 255), p.name.c_str());
                    std::string pathText = "Root: " + p.rootPath + "  |  Content: " + p.rootPath + "/Content";
                    dl->AddText(ImVec2(pMin.x + 12.0f, pMin.y + 24.0f), IM_COL32(150, 150, 160, 255), pathText.c_str());

                    if (!p.lastOpened.empty()) {
                        ImVec2 lastOpenedSize = ImGui::CalcTextSize(p.lastOpened.c_str());
                        dl->AddText(ImVec2(pMax.x - lastOpenedSize.x - 16.0f, pMin.y + 14.0f), IM_COL32(150, 150, 160, 255), p.lastOpened.c_str());
                    }

                    ImGui::PopID();
                }
            }
            ImGui::EndChild();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Bottom Actions for Tab 0
            bool hasSelection = (selectedProjectIndex >= 0 && selectedProjectIndex < (int)recentProjects.size());
            if (!hasSelection) ImGui::BeginDisabled();
            if (ImGui::Button("Open Selected Project", ImVec2(200, 34))) {
                if (hasSelection) {
                    LoadProject(recentProjects[selectedProjectIndex].rootPath, scene, camera);
                }
            }
            if (!hasSelection) ImGui::EndDisabled();

            ImGui::SameLine();
            if (!hasSelection) ImGui::BeginDisabled();
            if (ImGui::Button("Remove From List", ImVec2(140, 34))) {
                if (hasSelection) {
                    recentProjects.erase(recentProjects.begin() + selectedProjectIndex);
                    selectedProjectIndex = -1;
                    SaveRecentProjects();
                }
            }
            if (!hasSelection) ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::TextDisabled("(Double-click any project to open immediately)");

            if (hasActiveProject) {
                ImGui::SameLine(winW - 170.0f);
                if (ImGui::Button("Cancel", ImVec2(120, 34))) {
                    showProjectBrowser = false;
                }
            }
        } else {
            // =================================================================
            // TAB 1: NEW PROJECT (CREATE PROJECT SETUP)
            // =================================================================
            ImGui::TextColored(ImVec4(0.12f, 0.68f, 1.00f, 1.0f), "Create New Game Project");
            ImGui::TextDisabled("Set the project name and location. A new empty project structure with Content folder will be created.");
            ImGui::Spacing();

            // 1. Project Name
            ImGui::Text("Project Name:");
            ImGui::SetNextItemWidth(360.0f);
            ImGui::InputText("##NewProjNameInput", newProjectNameBuf, sizeof(newProjectNameBuf));

            ImGui::Spacing();

            // 2. Project Location
            ImGui::Text("Project Location (Parent Folder):");
            ImGui::SetNextItemWidth(560.0f);
            ImGui::InputText("##NewProjLocInput", newProjectPathBuf, sizeof(newProjectPathBuf));
            ImGui::SameLine();
            if (ImGui::Button("Browse Folder...", ImVec2(140, 0))) {
                std::string picked = ShowSelectFolderDialog(nullptr, "Select Folder for New Project");
                if (!picked.empty()) {
                    strncpy(newProjectPathBuf, picked.c_str(), sizeof(newProjectPathBuf) - 1);
                }
            }

            ImGui::Spacing();

            // 3. Computed Project Path & Content Structure Preview
            std::filesystem::path targetProjectDir = std::filesystem::path(newProjectPathBuf) / newProjectNameBuf;
            std::filesystem::path targetContentDir = targetProjectDir / "Content";
            std::filesystem::path targetRegistryDir = targetProjectDir / "Registry";
            std::filesystem::path targetBuildDir   = targetProjectDir / "Build";

            ImGui::BeginChild("NewProjPreview", ImVec2(0, 160), true);
            ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.25f, 1.0f), "Project Structure Summary:");
            ImGui::BulletText("Project Directory: %s", targetProjectDir.string().c_str());
            ImGui::BulletText("Content Root (Content Browser): %s", targetContentDir.string().c_str());
            ImGui::BulletText("Asset Registry Directory: %s", targetRegistryDir.string().c_str());
            ImGui::BulletText("Standalone Game Build Directory: %s", targetBuildDir.string().c_str());
            ImGui::BulletText("Starter Template: Eunoia Project Template (Default Level & Configs)");
            ImGui::EndChild();

            ImGui::Spacing();

            // Validation status
            std::error_code ec;
            bool nameValid = strlen(newProjectNameBuf) > 0;
            bool pathValid = strlen(newProjectPathBuf) > 0;
            bool targetExists = std::filesystem::exists(targetProjectDir, ec);

            if (!nameValid) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Please enter a project name.");
            } else if (!pathValid) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Please select a valid project directory.");
            } else if (targetExists) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Folder already exists on disk. Will initialize/load project.");
            } else {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "ÃƒÂ¢Ã…â€œÃ¢â‚¬Â¦ Ready to create empty project in selected path.");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            bool canCreate = nameValid && pathValid;
            if (!canCreate) ImGui::BeginDisabled();
            if (ImGui::Button("ÃƒÂ¢Ã…â€œÃ‚Â¨ Create Project", ImVec2(180, 36))) {
                CreateNewProject(newProjectPathBuf, newProjectNameBuf, scene, camera);
            }
            if (!canCreate) ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 36))) {
                if (hasActiveProject) {
                    showProjectBrowser = false;
                } else {
                    projectBrowserTab = 0;
                }
            }
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}




// ============================================================================
// Standalone Game Build Pipeline Implementation
// ============================================================================

void EngineUI::OpenGameBuildModal(Scene& scene) {
    showGameBuildModal = true;
    if (isGameBuilding) return;

    std::filesystem::path projectBase = activeProjectRoot.empty() ? contentRootPath.parent_path() : activeProjectRoot;
    std::string projName = projectBase.filename().string();
    if (projName.empty()) projName = "Game";
    std::filesystem::path gameBuildDir = projectBase / "Build";

    std::lock_guard<std::mutex> lock(gameBuildMutex);
    gameBuildOutputDir = gameBuildDir;
    gameBuildExePath = (gameBuildDir / (projName + ".exe")).string();
    gameBuildProgress = 0.0f;
    gameBuildStage = "Ready to Build";
    gameBuildLog = "[SYSTEM] Ready to package standalone game project: " + projName + "\n";
    gameBuildSuccess = false;
}

void EngineUI::StartGameBuildAsync(Scene& scene) {
    if (isGameBuilding) return;

    if (gameBuildThread.joinable()) {
        gameBuildThread.join();
    }

    std::filesystem::path projectBase = activeProjectRoot.empty() ? contentRootPath.parent_path() : activeProjectRoot;
    std::string projName = projectBase.filename().string();
    if (projName.empty()) projName = "Game";
    std::filesystem::path gameBuildDir = projectBase / "Build";
    std::filesystem::path localContentRoot = contentRootPath;

    // Snapshot current active level on main thread before launching background thread
    std::error_code ec;
    std::string activeRelLevel = "Content/Levels/Main.elevel";
    std::filesystem::path activeLevelAbsPath;

    if (!currentLevelFilePath.empty() && std::filesystem::exists(currentLevelFilePath, ec)) {
        auto relP = std::filesystem::relative(currentLevelFilePath, projectBase, ec);
        if (!ec) activeRelLevel = relP.string();
        std::replace(activeRelLevel.begin(), activeRelLevel.end(), '\\', '/');
        activeLevelAbsPath = currentLevelFilePath;
    } else {
        std::filesystem::path saveP = localContentRoot / "Levels" / "Main.elevel";
        std::filesystem::create_directories(saveP.parent_path(), ec);
        currentLevelFilePath = saveP.string();
        activeLevelAbsPath = saveP;
        activeRelLevel = "Content/Levels/Main.elevel";
    }

    LevelSerializer::SaveLevel(scene, activeLevelAbsPath.string());
    AddLog("LogBuild", "Snapshotted active editor level: " + activeLevelAbsPath.string(), 1);

    isGameBuilding = true;
    gameBuildProgress = 0.05f;
    gameBuildStage = "Initializing standalone packaging pipeline...";
    gameBuildSuccess = false;
    gameBuildOutputDir = gameBuildDir;
    gameBuildExePath = (gameBuildDir / (projName + ".exe")).string();

    {
        std::lock_guard<std::mutex> lock(gameBuildMutex);
        gameBuildLog = "[BUILD START] Packaging standalone game: " + projName + "\n";
        gameBuildLog += "[BUILD] Target output directory: " + gameBuildDir.string() + "\n";
        gameBuildLog += "[BUILD] Active level snapshot: " + activeRelLevel + "\n";
    }

    gameBuildThread = std::thread([this, projectBase, projName, gameBuildDir, localContentRoot, activeRelLevel, activeLevelAbsPath]() {
        auto updateStatus = [this](float progress, const std::string& stage, const std::string& logLine) {
            std::lock_guard<std::mutex> lock(gameBuildMutex);
            gameBuildProgress = progress;
            gameBuildStage = stage;
            if (!logLine.empty()) {
                time_t rawtime;
                struct tm* timeinfo;
                char timeBuf[32] = {};
                time(&rawtime);
                timeinfo = localtime(&rawtime);
                if (timeinfo) {
                    strftime(timeBuf, sizeof(timeBuf), "[%H:%M:%S] ", timeinfo);
                }
                gameBuildLog += std::string(timeBuf) + logLine + "\n";
            }
        };

        std::error_code sec;

        try {
            updateStatus(0.10f, "Creating directory layout...", "Creating standalone distribution directories in: " + gameBuildDir.string());

            std::filesystem::path binariesDir    = gameBuildDir / "Binaries";
            std::filesystem::path dataDir        = gameBuildDir / "Data";
            std::filesystem::path depsBuildDir   = gameBuildDir / "Deps";
            std::filesystem::path buildContent   = gameBuildDir / "Content";
            std::filesystem::path buildShaders   = gameBuildDir / "Shaders";
            std::filesystem::path buildResources = gameBuildDir / "Resources";

            std::filesystem::create_directories(gameBuildDir, sec);
            std::filesystem::create_directories(binariesDir, sec);
            std::filesystem::create_directories(dataDir, sec);
            std::filesystem::create_directories(depsBuildDir, sec);
            std::filesystem::create_directories(buildContent, sec);
            std::filesystem::create_directories(buildShaders, sec);
            std::filesystem::create_directories(buildResources, sec);

            std::filesystem::create_directories(dataDir / "Content", sec);
            std::filesystem::create_directories(dataDir / "Shaders", sec);
            std::filesystem::create_directories(dataDir / "Resources", sec);

            // Copy template structure if available
            std::filesystem::path gameTemplateDir = "C:\\Projects\\Eunoia-Engine\\Eunoia-Engine\\Templates\\Game Build Template";
            if (!std::filesystem::exists(gameTemplateDir, sec)) {
                gameTemplateDir = std::filesystem::current_path() / "Templates" / "Game Build Template";
            }
            if (std::filesystem::exists(gameTemplateDir, sec)) {
                for (const auto& item : std::filesystem::directory_iterator(gameTemplateDir, sec)) {
                    if (item.is_directory(sec)) {
                        std::filesystem::create_directories(gameBuildDir / item.path().filename(), sec);
                    }
                }
            }

            // Stage 2: Deploy Active Level Snapshot
            updateStatus(0.25f, "Deploying active level snapshot...", "Deploying startup level: " + activeRelLevel);
            std::filesystem::path targetLevel1 = gameBuildDir / activeRelLevel;
            std::filesystem::path targetLevel2 = dataDir / activeRelLevel;
            std::filesystem::path targetLevelFallback = gameBuildDir / "Main.elevel";
            std::filesystem::path targetLevelFallbackData = dataDir / "Main.elevel";

            std::filesystem::create_directories(targetLevel1.parent_path(), sec);
            std::filesystem::create_directories(targetLevel2.parent_path(), sec);

            if (std::filesystem::exists(activeLevelAbsPath, sec)) {
                std::filesystem::copy_file(activeLevelAbsPath, targetLevel1, std::filesystem::copy_options::overwrite_existing, sec);
                std::filesystem::copy_file(activeLevelAbsPath, targetLevel2, std::filesystem::copy_options::overwrite_existing, sec);
                std::filesystem::copy_file(activeLevelAbsPath, targetLevelFallback, std::filesystem::copy_options::overwrite_existing, sec);
                std::filesystem::copy_file(activeLevelAbsPath, targetLevelFallbackData, std::filesystem::copy_options::overwrite_existing, sec);
                updateStatus(0.30f, "Active level deployed", "Active level deployed to standalone root and Data/ successfully.");
            } else {
                updateStatus(0.30f, "Warning: Level snapshot missing", "Warning: Could not read level snapshot at " + activeLevelAbsPath.string());
            }

            // Recursive copy lambda that filters out source code and developer files
            auto copyGameFiles = [](const std::filesystem::path& src, const std::filesystem::path& dst) {
                std::error_code ec;
                if (!std::filesystem::exists(src, ec)) return;
                std::filesystem::create_directories(dst, ec);
                for (const auto& entry : std::filesystem::recursive_directory_iterator(src, std::filesystem::directory_options::skip_permission_denied, ec)) {
                    const auto rel = std::filesystem::relative(entry.path(), src, ec);
                    const auto target = dst / rel;
                    if (entry.is_directory(ec)) {
                        std::filesystem::create_directories(target, ec);
                    } else if (entry.is_regular_file(ec)) {
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        std::string relStr = rel.string();
                        bool isBehaviourScript = (ext == ".cpp" && (relStr.find("Behaviours") != std::string::npos || relStr.find("behaviours") != std::string::npos));
                        // Filter out engine source code, intermediate compiler files, and git files (keep behaviour scripts for runtime)
                        if (!isBehaviourScript) {
                            if (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp" ||
                                ext == ".obj" || ext == ".o" || ext == ".pdb" || ext == ".idb" ||
                                ext == ".ilk" || ext == ".tlog" || ext == ".log" || ext == ".tmp" ||
                                ext == ".econfigs" || ext == ".git" || ext == ".gitignore") {
                                continue;
                        }
                        }
                        std::filesystem::create_directories(target.parent_path(), ec);
                        std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing, ec);
                    }
                }
            };

            // Stage 3: Packaging Project Content
            updateStatus(0.40f, "Packaging game content...", "Copying game levels, textures, materials, and audio from: " + localContentRoot.string());
            copyGameFiles(localContentRoot, buildContent);
            copyGameFiles(localContentRoot, dataDir / "Content");
            updateStatus(0.65f, "Game content packaged", "Project content copied directly without engine developer artifacts.");

            // Stage 5: Deploying Shaders and Engine Resources
            updateStatus(0.70f, "Deploying Shaders & Resources...", "Deploying DirectX 12 shaders and engine graphics resources...");
            std::filesystem::path engineRoot = "C:\\Projects\\Eunoia-Engine\\Eunoia-Engine";
            if (!std::filesystem::exists(engineRoot / "Shaders", sec)) {
                engineRoot = "C:\\Projects\\Eunoia-Engine";
            }
            if (!std::filesystem::exists(engineRoot / "Shaders", sec)) {
                engineRoot = std::filesystem::current_path();
            }

            if (std::filesystem::exists(engineRoot / "Shaders", sec)) {
                copyGameFiles(engineRoot / "Shaders", buildShaders);
                copyGameFiles(engineRoot / "Shaders", dataDir / "Shaders");
                updateStatus(0.75f, "Shaders deployed", "DirectX 12 Shaders deployed.");
            }
            if (std::filesystem::exists(engineRoot / "Resources", sec)) {
                copyGameFiles(engineRoot / "Resources", buildResources);
                copyGameFiles(engineRoot / "Resources", dataDir / "Resources");
                updateStatus(0.80f, "Resources deployed", "Engine graphics resources deployed.");
            }

            // Stage 6: Deploying Runtime Dependencies (DLLs)
            updateStatus(0.85f, "Deploying runtime dependencies...", "Deploying glfw3.dll and runtime libraries...");
            char curExeBuf[MAX_PATH] = {};
            GetModuleFileNameA(NULL, curExeBuf, MAX_PATH);
            std::filesystem::path curExePath(curExeBuf);
            std::filesystem::path curExeDir = curExePath.parent_path();

            std::vector<std::filesystem::path> dllCandidates = {
                curExeDir / "glfw3.dll",
                engineRoot / "deps" / "glfw-3.5.1.bin.WIN64" / "lib-mingw-w64" / "glfw3.dll",
                engineRoot / "Plugins" / "primitives" / "primitives.dll",
                curExeDir / "primitives.dll"
            };

            for (const auto& dllSrc : dllCandidates) {
                if (std::filesystem::exists(dllSrc, sec)) {
                    std::string fname = dllSrc.filename().string();
                    std::filesystem::copy_file(dllSrc, gameBuildDir / fname, std::filesystem::copy_options::overwrite_existing, sec);
                    std::filesystem::copy_file(dllSrc, binariesDir / fname, std::filesystem::copy_options::overwrite_existing, sec);
                    std::filesystem::copy_file(dllSrc, depsBuildDir / fname, std::filesystem::copy_options::overwrite_existing, sec);
                    updateStatus(0.88f, "Dependency deployed", "Deployed DLL: " + fname);
                }
            }

            // Stage 7: Writing GameConfig.json, Version.euversion, and Run.bat
            updateStatus(0.90f, "Configuring standalone game metadata...", "Generating GameConfig.json and Version.euversion...");

            auto writeGameConfig = [&](const std::filesystem::path& destCfg) {
                std::ofstream cf(destCfg);
                if (cf.is_open()) {
                    nlohmann::json gj;
                    gj["gameName"] = projName;
                    gj["startupLevel"] = activeRelLevel;
                    gj["defaultLevel"] = activeRelLevel;
                    gj["standalone"] = true;
                    gj["version"] = "1.0.0";
                    gj["renderer"] = {
                        {"backend", "DirectX12"},
                        {"preferDX12", true}
                    };
                    gj["window"] = {
                        {"title", projName},
                        {"width", 1920},
                        {"height", 1080},
                        {"fullscreen", false},
                        {"vsync", true}
                    };
                    cf << gj.dump(4);
                    cf.close();
                }
            };

            writeGameConfig(gameBuildDir / "GameConfig.json");
            writeGameConfig(dataDir / "GameConfig.json");

            {
                std::ofstream vf(gameBuildDir / "Version.euversion");
                if (vf.is_open()) {
                    nlohmann::json vj;
                    vj["engineVersion"] = "0.1.0";
                    vj["projectVersion"] = "1.0.0";
                    vj["gameName"] = projName;
                    vf << vj.dump(4);
                    vf.close();
                }
            }

            {
                std::ofstream runScript(gameBuildDir / "Run.bat");
                if (runScript.is_open()) {
                    runScript << "@echo off\n";
                    runScript << "cd /d \"%~dp0\"\n";
                    runScript << "echo Launching " << projName << " (DirectX 12 Standalone)...\n";
                    runScript << "start \"\" \"" << projName << ".exe\"\n";
                    runScript.close();
                }
            }

            // Stage 8: Building / Copying Standalone Executable
            updateStatus(0.95f, "Deploying standalone executable...", "Packaging " + projName + ".exe...");
            std::filesystem::path rootGameExe = gameBuildDir / (projName + ".exe");
            std::filesystem::path binGameExe  = binariesDir / (projName + ".exe");

            std::filesystem::path exeSource = curExePath;
            if (!std::filesystem::exists(exeSource, sec)) {
                exeSource = "C:\\Projects\\Eunoia-Engine\\Build\\Engine\\Eunoia-Editor.exe";
            }
            if (!std::filesystem::exists(exeSource, sec)) {
                exeSource = engineRoot / "Build" / "Runtime" / "Eunoia-Runtime.exe";
            }

            if (std::filesystem::exists(exeSource, sec)) {
                std::filesystem::copy_file(exeSource, rootGameExe, std::filesystem::copy_options::overwrite_existing, sec);
                std::filesystem::copy_file(exeSource, binGameExe, std::filesystem::copy_options::overwrite_existing, sec);
                updateStatus(0.98f, "Executable created", "Executable deployed: " + rootGameExe.string());
            } else {
                updateStatus(0.98f, "Executable error", "Warning: Could not find base executable source!");
            }

            // Stage 9: Completion
            {
                std::lock_guard<std::mutex> lock(gameBuildMutex);
                gameBuildProgress = 1.0f;
                gameBuildStage = "Build Succeeded";
                gameBuildSuccess = true;
                isGameBuilding = false;
                gameBuildExePath = rootGameExe.string();
                gameBuildOutputDir = gameBuildDir;
                gameBuildLog += "[BUILD SUCCESS] Standalone Game Package created successfully!\n";
                gameBuildLog += "[BUILD SUCCESS] Executable: " + rootGameExe.filename().string() + "\n";
                gameBuildLog += "[BUILD SUCCESS] Startup Level: " + activeRelLevel + "\n";
                gameBuildLog += "[BUILD SUCCESS] Standalone build complete in: " + gameBuildDir.string() + "\n";

                m_bottomProgress.active = true;
                m_bottomProgress.completionTimer = 3.5f;
                m_bottomProgress.title = "Standalone Game Package Built Successfully";
                m_bottomProgress.success = true;
            }

        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(gameBuildMutex);
            gameBuildProgress = 1.0f;
            gameBuildStage = "Build Failed";
            gameBuildSuccess = false;
            isGameBuilding = false;
            gameBuildLog += "[BUILD ERROR] Exception occurred during packaging: " + std::string(e.what()) + "\n";

            m_bottomProgress.active = true;
            m_bottomProgress.completionTimer = 4.0f;
            m_bottomProgress.title = "Standalone Game Build Failed";
            m_bottomProgress.success = false;
        } catch (...) {
            std::lock_guard<std::mutex> lock(gameBuildMutex);
            gameBuildProgress = 1.0f;
            gameBuildStage = "Build Failed";
            gameBuildSuccess = false;
            isGameBuilding = false;
            gameBuildLog += "[BUILD ERROR] Unknown exception occurred during packaging!\n";

            m_bottomProgress.active = true;
            m_bottomProgress.completionTimer = 4.0f;
            m_bottomProgress.title = "Standalone Game Build Failed";
            m_bottomProgress.success = false;
        }
    });
}
