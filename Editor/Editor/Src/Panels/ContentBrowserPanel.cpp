#include <Editor/EngineUI.h>
#include <Editor/ContentHelpers.h>
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
void EngineUI::RenderContentBrowser(Scene& scene) {
    RenderContentBrowserWindow(scene);
}

void EngineUI::RenderContentBrowserWindow(Scene& scene) {
    if (!showContentBrowserWindow) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // Dockable window: size hint for first launch, can be repositioned via DockSpace
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.75f, 260.0f * uiScale), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Content Browser", &showContentBrowserWindow, flags)) {
        RenderContentBrowserContent(scene);
    }
    ImGui::End();
}

void EngineUI::RenderAnimationTimelineWindow(Scene& scene) {
    if (!showAnimationWindow) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // Dockable window: size hint for first launch, can be repositioned via DockSpace
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.75f, 260.0f * uiScale), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Animation", &showAnimationWindow, flags)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        Eunoia::LevelAnimationEditor::Get().RenderTimeline(scene, ImGui::GetCursorScreenPos(), avail);
    }
    ImGui::End();
}

void EngineUI::RenderOutputLogWindow() {
    if (!showOutputLogWindow) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();

    // Dockable window: size hint for first launch, can be repositioned via DockSpace
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.75f, 260.0f * uiScale), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Output Log", &showOutputLogWindow, flags)) {
        // Filter toolbar matching Old Eunoia Engine
        if (ImGui::Button("Clear")) {
            logs.clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Info", &logShowInfo);
        ImGui::SameLine();
        ImGui::Checkbox("Warnings", &logShowWarnings);
        ImGui::SameLine();
        ImGui::Checkbox("Errors", &logShowErrors);
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &logAutoScroll);

        ImGui::SameLine(ImGui::GetWindowWidth() - 220.0f);
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##LogSearch", "Filter Log...", logSearchBuf, sizeof(logSearchBuf));

        ImGui::Separator();

        // Log messages child
        ImGui::BeginChild("LogScrollArea", ImVec2(0, -32), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        std::string searchLower = logSearchBuf;
        std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

        for (const auto& entry : logs) {
            if (entry.level == 0 && !logShowInfo) continue;
            if (entry.level == 1 && !logShowWarnings) continue;
            if ((entry.level == 2 || entry.level == 3) && !logShowErrors) continue;

            if (!searchLower.empty()) {
                std::string msgLower = entry.message;
                std::transform(msgLower.begin(), msgLower.end(), msgLower.begin(), ::tolower);
                if (msgLower.find(searchLower) == std::string::npos) continue;
            }

            ImVec4 col = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
            if (entry.level == 1) col = ImVec4(1.0f, 0.85f, 0.2f, 1.0f);      // Warning Yellow
            else if (entry.level == 2 || entry.level == 3) col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Error Red
            else if (entry.level == 4) col = ImVec4(0.3f, 1.0f, 0.4f, 1.0f);  // Success Green

            ImGui::TextColored(col, "[%s] %s", entry.category.c_str(), entry.message.c_str());
        }

        if (logAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        // Command line input
        ImGui::PushItemWidth(-1.0f);
        if (ImGui::InputTextWithHint("##LogCommandInput", "Enter Lua / Engine command and press Enter...", consoleInput, sizeof(consoleInput), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (consoleInput[0] != '\0') {
                AddLog("Command", consoleInput, 0);
                consoleInput[0] = '\0';
            }
        }
        ImGui::PopItemWidth();
    }
    ImGui::End();
}

Scene& EngineUI::GetActiveRenderScene(Scene& defaultScene) {
    return defaultScene;
}

OrbitCamera& EngineUI::GetActiveRenderCamera(OrbitCamera& defaultCamera) {
    return defaultCamera;
}

void EngineUI::RenderContentBrowserContent(Scene& scene) {
    const ImVec4 cbAccentBlue  = ImVec4(0.000f, 0.400f, 1.000f, 1.0f); // #0066ff
    const ImVec4 accentBlue    = cbAccentBlue;
    const ImVec4 cbTextWhite   = ImVec4(1.000f, 1.000f, 1.000f, 1.0f);
    const ImVec4 cbTextMuted   = ImVec4(0.631f, 0.631f, 0.667f, 1.0f);

    // Registry-Backed Content Browser (dev.md Section 14, 34, 35)
                std::error_code ec;
                if (currentVirtualDir.empty()) currentVirtualDir = "/Game";

                // 1. Top Navigation & Action Toolbar (matching Old Eunoia Engine)
                bool isAtRoot = (currentVirtualDir == "/Game");
                if (isAtRoot) ImGui::BeginDisabled();
                if (ImGui::Button("< Up")) {
                    currentVirtualDir = AssetPath::GetDirectory(currentVirtualDir);
                    selectedContentItem = "";
                    selectedContentItems.clear();
                }
                if (isAtRoot) ImGui::EndDisabled();

                // Drop target on "< Up" button to move items up into parent directory
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                        std::string targetDir = AssetPath::GetDirectory(currentVirtualDir);
                        if (!targetDir.empty()) {
                            std::string payloadData((const char*)payload->Data);
                            std::stringstream pss(payloadData);
                            std::string item;
                            while (std::getline(pss, item, '\n')) {
                                if (item.empty()) continue;
                                if (item.rfind("DIR:", 0) == 0) {
                                    MoveFolderToDirectory(item.substr(4), targetDir, contentRootPath);
                                } else if (item.rfind("ASSET:", 0) == 0) {
                                    const AssetMetadata* m = AssetRegistry::Get().FindByPath(item.substr(6));
                                    if (m) MoveAssetToDirectory(m->id, targetDir, contentRootPath);
                                }
                            }
                            TriggerBackgroundRegistrySync(contentRootPath);
                        }
                    } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                        std::string targetDir = AssetPath::GetDirectory(currentVirtualDir);
                        if (!targetDir.empty()) {
                            std::string idStr((const char*)payload->Data);
                            MoveAssetToDirectory(AssetID::FromString(idStr), targetDir, contentRootPath);
                            TriggerBackgroundRegistrySync(contentRootPath);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SameLine();
                if (ImGui::Button("Content")) {
                    currentVirtualDir = "/Game";
                    selectedContentItem = "";
                    selectedContentItems.clear();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                        std::string payloadData((const char*)payload->Data);
                        std::stringstream pss(payloadData);
                        std::string item;
                        while (std::getline(pss, item, '\n')) {
                            if (item.empty()) continue;
                            if (item.rfind("DIR:", 0) == 0) {
                                MoveFolderToDirectory(item.substr(4), "/Game", contentRootPath);
                            } else if (item.rfind("ASSET:", 0) == 0) {
                                const AssetMetadata* m = AssetRegistry::Get().FindByPath(item.substr(6));
                                if (m) MoveAssetToDirectory(m->id, "/Game", contentRootPath);
                            }
                        }
                        TriggerBackgroundRegistrySync(contentRootPath);
                    } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                        std::string idStr((const char*)payload->Data);
                        MoveAssetToDirectory(AssetID::FromString(idStr), "/Game", contentRootPath);
                        TriggerBackgroundRegistrySync(contentRootPath);
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("|");
                ImGui::SameLine();

                // + Add / Create Menu (matching Old Eunoia Engine)
                if (ImGui::Button("+ Add / Create")) {
                    ImGui::OpenPopup("CBAddCreatePopup");
                }
                if (ImGui::BeginPopup("CBAddCreatePopup")) {
                    if (ImGui::MenuItem("New Level (.eulevel)...")) {
                        scene.Clear();
                        currentLevelFilePath = "";
                        AddLog("ContentBrowser", "Created New Level", 3);
                    }
                    if (ImGui::MenuItem("New Folder...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewFolderPopup = true;
                        snprintf(newFolderNameBuf, sizeof(newFolderNameBuf), "NewFolder");
                    }
                    if (ImGui::MenuItem("New Material (.emat)...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewMaterialPopup = true;
                        snprintf(newMaterialNameBuf, sizeof(newMaterialNameBuf), "M_NewMaterial");
                    }
                    if (ImGui::MenuItem("New Level Animation (.elanim)...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewLevelAnimationPopup = true;
                        snprintf(newLevelAnimationNameBuf, sizeof(newLevelAnimationNameBuf), "LA_NewAnimation");
                    }

                    if (ImGui::MenuItem("New Behaviour Script (.cpp)...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewBehaviourPopup = true;
                        std::string defName = "NewBehaviour";
                        if (IsBehaviourNameAlreadyExists(defName)) {
                            int bIdx = 1;
                            while (IsBehaviourNameAlreadyExists("NewBehaviour_" + std::to_string(bIdx))) bIdx++;
                            defName = "NewBehaviour_" + std::to_string(bIdx);
                        }
                        snprintf(newBehaviourNameBuf, sizeof(newBehaviourNameBuf), "%s", defName.c_str());
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Import 3D Model / Level...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        ImportMeshWithSavePrompt(scene);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Show in File Explorer")) {
                        std::string diskSub = currentVirtualDir;
                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                        std::filesystem::path folderDisk = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                        ShellExecuteA(NULL, "explore", folderDisk.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
                    }
                    ImGui::EndPopup();
                }

                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("|");
                ImGui::SameLine();

                // Interactive Segmented Breadcrumbs
                std::string breadcrumb = currentVirtualDir;
                std::stringstream bss(breadcrumb);
                std::string seg;
                std::string accumVirtual = "";
                int segIdx = 0;
                while (std::getline(bss, seg, '/')) {
                    if (seg.empty()) continue;
                    accumVirtual += "/" + seg;
                    if (segIdx > 0) {
                        ImGui::SameLine();
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled(">");
                        ImGui::SameLine();
                    }
                    ImGui::PushID(segIdx++);
                    bool isCurSeg = (accumVirtual == currentVirtualDir);
                    if (isCurSeg) ImGui::PushStyleColor(ImGuiCol_Text, accentBlue);
                    if (ImGui::Button(seg.c_str())) {
                        currentVirtualDir = accumVirtual;
                        selectedContentItem = "";
                        selectedContentItems.clear();
                    }
                    if (isCurSeg) ImGui::PopStyleColor();

                    std::string bcTarget = accumVirtual;
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                            std::string payloadData((const char*)payload->Data);
                            std::stringstream pss(payloadData);
                            std::string item;
                            while (std::getline(pss, item, '\n')) {
                                if (item.empty()) continue;
                                if (item.rfind("DIR:", 0) == 0) {
                                    MoveFolderToDirectory(item.substr(4), bcTarget, contentRootPath);
                                } else if (item.rfind("ASSET:", 0) == 0) {
                                    const AssetMetadata* m = AssetRegistry::Get().FindByPath(item.substr(6));
                                    if (m) MoveAssetToDirectory(m->id, bcTarget, contentRootPath);
                                }
                            }
                            TriggerBackgroundRegistrySync(contentRootPath);
                        } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                            std::string idStr((const char*)payload->Data);
                            MoveAssetToDirectory(AssetID::FromString(idStr), bcTarget, contentRootPath);
                            TriggerBackgroundRegistrySync(contentRootPath);
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::PopID();
                }

                // Controls on right: Search, Zoom Slider (matching Old Eunoia Engine)
                float rightControlsWidth = 140.0f + 80.0f + ImGui::GetStyle().ItemSpacing.x;
                if (contentBrowserSearch[0] != '\0') {
                    rightControlsWidth += ImGui::CalcTextSize("X").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
                }
                float availW = ImGui::GetContentRegionAvail().x;
                if (availW > rightControlsWidth + 10.0f) {
                    ImGui::SameLine(ImGui::GetWindowWidth() - rightControlsWidth - ImGui::GetStyle().WindowPadding.x);
                } else {
                    ImGui::SameLine();
                }

                ImGui::SetNextItemWidth(140.0f);
                ImGui::InputTextWithHint("##CBSearch", "Search...", contentBrowserSearch, sizeof(contentBrowserSearch));
                if (contentBrowserSearch[0] != '\0') {
                    ImGui::SameLine();
                    if (ImGui::Button("X##ClearCBSearch")) {
                        contentBrowserSearch[0] = '\0';
                    }
                }

                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                if (ImGui::SliderFloat("##CBZoom", &contentThumbnailSize, 64.0f, 128.0f, "%.0f px")) {
                    SaveEditorConfig();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Thumbnail Card Scale");

                ImGui::Separator();

                // Modals for Renaming Asset / Folder
                if (showRenameAssetPopup) {
                    ImGui::OpenPopup("Rename Asset##Modal");
                    showRenameAssetPopup = false;
                }
                if (ImGui::BeginPopupModal("Rename Asset##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    const AssetMetadata* rMeta = AssetRegistry::Get().FindById(renameTargetAssetId);
                    std::string curName = rMeta ? rMeta->objectName : renameTargetAssetName;
                    std::string curType = rMeta ? AssetTypeToString(rMeta->type) : "Asset";

                    ImGui::Text("Renaming %s: %s", curType.c_str(), curName.c_str());
                    ImGui::Spacing();
                    ImGui::Text("Enter new name:");
                    ImGui::SetNextItemWidth(300.0f);
                    if (renameFocusInput) {
                        ImGui::SetKeyboardFocusHere();
                        renameFocusInput = false;
                    }
                    ImGui::InputText("##RenameAssetInput", renameItemNameBuf, sizeof(renameItemNameBuf));

                    std::string inputName = renameItemNameBuf;
                    while (!inputName.empty() && isspace((unsigned char)inputName.front())) inputName.erase(inputName.begin());
                    while (!inputName.empty() && isspace((unsigned char)inputName.back())) inputName.pop_back();

                    bool hasInvalidChar = (inputName.find('/') != std::string::npos || inputName.find('\\') != std::string::npos ||
                                           inputName.find(':') != std::string::npos || inputName.find('*') != std::string::npos ||
                                           inputName.find('?') != std::string::npos || inputName.find('\"') != std::string::npos ||
                                           inputName.find('<') != std::string::npos || inputName.find('>') != std::string::npos ||
                                           inputName.find('|') != std::string::npos);

                    bool isValid = !inputName.empty() && !hasInvalidChar;
                    if (hasInvalidChar) {
                        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Name contains invalid filename characters (/ \\ : * ? \" < > |)");
                    }

                    ImGui::Spacing();
                    if (!isValid) ImGui::BeginDisabled();
                    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
                    bool okClicked = ImGui::Button("Rename", ImVec2(120, 0));
                    if (!isValid) ImGui::EndDisabled();

                    if (isValid && (okClicked || enterPressed)) {
                        RenameContentAsset(renameTargetAssetId, inputName);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                if (showRenameFolderPopup) {
                    ImGui::OpenPopup("Rename Folder##Modal");
                    showRenameFolderPopup = false;
                }
                if (ImGui::BeginPopupModal("Rename Folder##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string oldDirName = AssetPath::GetObjectName(renameTargetVirtualDir);
                    ImGui::Text("Renaming virtual folder: %s", renameTargetVirtualDir.c_str());
                    ImGui::Spacing();
                    ImGui::Text("Enter new folder name:");
                    ImGui::SetNextItemWidth(300.0f);
                    if (renameFocusInput) {
                        ImGui::SetKeyboardFocusHere();
                        renameFocusInput = false;
                    }
                    ImGui::InputText("##RenameFolderInput", renameItemNameBuf, sizeof(renameItemNameBuf));

                    std::string inputName = renameItemNameBuf;
                    while (!inputName.empty() && isspace((unsigned char)inputName.front())) inputName.erase(inputName.begin());
                    while (!inputName.empty() && isspace((unsigned char)inputName.back())) inputName.pop_back();

                    bool hasInvalidChar = (inputName.find('/') != std::string::npos || inputName.find('\\') != std::string::npos ||
                                           inputName.find(':') != std::string::npos || inputName.find('*') != std::string::npos ||
                                           inputName.find('?') != std::string::npos || inputName.find('\"') != std::string::npos ||
                                           inputName.find('<') != std::string::npos || inputName.find('>') != std::string::npos ||
                                           inputName.find('|') != std::string::npos);

                    bool isValid = !inputName.empty() && !hasInvalidChar && renameTargetVirtualDir != "/Game";
                    if (hasInvalidChar) {
                        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Name contains invalid folder characters (/ \\ : * ? \" < > |)");
                    }

                    ImGui::Spacing();
                    if (!isValid) ImGui::BeginDisabled();
                    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
                    bool okClicked = ImGui::Button("Rename", ImVec2(120, 0));
                    if (!isValid) ImGui::EndDisabled();

                    if (isValid && (okClicked || enterPressed)) {
                        RenameContentFolder(renameTargetVirtualDir, inputName);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // Modals for creating Folder / Material / Skybox / Behaviour
                if (showNewFolderPopup) {
                    ImGui::OpenPopup("Create Folder##Modal");
                    showNewFolderPopup = false;
                }
                if (ImGui::BeginPopupModal("Create Folder##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Creating virtual folder in: %s", targetDir.c_str());
                    ImGui::Spacing();
                    ImGui::Text("Enter folder name:");
                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputText("##FolderNameInput", newFolderNameBuf, sizeof(newFolderNameBuf));
                    ImGui::Spacing();
                    if (ImGui::Button("Create", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                        if (strlen(newFolderNameBuf) > 0) {
                            std::string diskSub = targetDir;
                            if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                            if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                            std::filesystem::path newDiskDir = contentRootPath / diskSub / newFolderNameBuf;
                            std::error_code dirEc;
                            if (std::filesystem::create_directories(newDiskDir, dirEc)) {
                                TriggerBackgroundRegistrySync(contentRootPath);
                                AddLog("LogContent", "Created folder: " + std::string(newFolderNameBuf) + " in " + targetDir, 2);
                            }
                        }
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                if (showNewMaterialPopup) {
                    ImGui::OpenPopup("Create Material##Modal");
                    showNewMaterialPopup = false;
                }
                if (ImGui::BeginPopupModal("Create Material##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Creating Material in: %s", targetDir.c_str());
                    ImGui::Spacing();
                    ImGui::Text("Enter material asset name:");
                    ImGui::SetNextItemWidth(260.0f);
                    ImGui::InputText("##MatNameInput", newMaterialNameBuf, sizeof(newMaterialNameBuf));
                    ImGui::Spacing();
                    if (ImGui::Button("Create", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                        if (strlen(newMaterialNameBuf) > 0) {
                            std::string matFileName = newMaterialNameBuf;
                            if (matFileName.find(".emat") == std::string::npos) matFileName += ".emat";
                            std::string diskSub = targetDir;
                            if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                            if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                            std::filesystem::path dstDir = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                            std::filesystem::create_directories(dstDir, ec);
                            std::filesystem::path matFilePath = dstDir / matFileName;

                            MaterialAsset newMat;
                            newMat.name = std::filesystem::path(matFileName).stem().string();
                            newMat.filePath = matFilePath.string();
                            newMat.assetId = AssetID::CreateRandom();
                            newMat.virtualPath = AssetPath::Combine(targetDir, newMat.name);
                            newMat.baseColor = glm::vec3(0.75f, 0.75f, 0.75f);
                            newMat.metallic = 0.0f;
                            newMat.roughness = 0.5f;
                            newMat.specular = 0.5f;

                            if (SaveMaterialFile(matFilePath.string(), newMat)) {
                                TriggerBackgroundRegistrySync(contentRootPath);
                                AddLog("LogContent", "Created material asset: " + newMat.name + " (" + newMat.assetId.ToString() + ") in " + targetDir, 2);
                                OpenMaterialEditor(matFilePath.string());
                            }
                        }
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                if (showNewBehaviourPopup) {
                    ImGui::OpenPopup("Create Behaviour Script (.cpp)##Modal");
                    showNewBehaviourPopup = false;
                }
                if (ImGui::BeginPopupModal("Create Behaviour Script (.cpp)##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Creating Standalone Behaviour Script (.cpp) in: %s", targetDir.c_str());
                    ImGui::Spacing();
                    ImGui::Text("Enter Behaviour Class Name (C++ class inheriting EunoiaBehaviour):");
                    ImGui::SetNextItemWidth(300.0f);
                    ImGui::InputText("##BehNameInput", newBehaviourNameBuf, sizeof(newBehaviourNameBuf));

                    std::string inputName = newBehaviourNameBuf;
                    while (!inputName.empty() && isspace((unsigned char)inputName.front())) inputName.erase(inputName.begin());
                    while (!inputName.empty() && isspace((unsigned char)inputName.back())) inputName.pop_back();

                    bool isDuplicate = !inputName.empty() && IsBehaviourNameAlreadyExists(inputName);
                    bool isValidName = !inputName.empty();

                    if (isDuplicate) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "A behaviour named '%s' already exists in this project!", inputName.c_str());
                    } else if (isValidName) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "Valid name (will be created in folder: %s)", targetDir.c_str());
                    }

                    ImGui::Spacing();

                    bool canCreate = isValidName && !isDuplicate;
                    if (!canCreate) {
                        ImGui::BeginDisabled();
                    }

                    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
                    bool createClicked = ImGui::Button("Create", ImVec2(120, 0));

                    if (!canCreate) {
                        ImGui::EndDisabled();
                    }

                    if (canCreate && (createClicked || enterPressed)) {
                        std::string behName = inputName;
                        std::string diskSub = targetDir;
                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                        std::filesystem::path behDir = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                        std::error_code bec;
                        std::filesystem::create_directories(behDir, bec);

                        std::filesystem::path cppPath = behDir / (behName + ".cpp");
                        std::filesystem::path metaPath = behDir / (behName + ".cpp.assetmeta");

                        std::ofstream cppFile(cppPath);
                        if (cppFile.is_open()) {
                            cppFile << "#include <EngineScene/EunoiaBehaviour.h>\n"
                                << "#include <EngineScene/BehaviourRegistry.h>\n"
                                << "#include <EngineScene/GameObject.h>\n"
                                << "#include <EnginePlatform/InputSystem.h>\n"
                                << "#include <EngineCore/Log.h>\n\n"
                                << "// ============================================================================\n"
                                << "// Empty Base Behaviour Structure\n"
                                << "// ============================================================================\n"
                                << "class " << behName << " : public EunoiaBehaviour {\n"
                                << "public:\n"
                                << "    // Public variables are automatically registered as editable properties\n\n"
                                << "    // ------------------------------------------------------------------------\n"
                                << "    // Lifecycle Methods\n"
                                << "    // ------------------------------------------------------------------------\n"
                                << "    void OnCreate() override {\n"
                                << "        // Called once when behaviour is instantiated or attached\n"
                                << "    }\n\n"
                                << "    void OnEnable() override {\n"
                                << "        // Called when behaviour or owner becomes enabled\n"
                                << "    }\n\n"
                                << "    void Start() override {\n"
                                << "        // Called once before the first frame Update\n"
                                << "    }\n\n"
                                << "    void Update(float deltaTime) override {\n"
                                << "        // Called every frame\n"
                                << "    }\n\n"
                                << "    void FixedUpdate(float fixedDeltaTime) override {\n"
                                << "        // Called at fixed time intervals (physics / fixed tick)\n"
                                << "    }\n\n"
                                << "    void LateUpdate(float deltaTime) override {\n"
                                << "        // Called after all Update calls each frame\n"
                                << "    }\n\n"
                                << "    void OnDisable() override {\n"
                                << "        // Called when behaviour or owner becomes disabled\n"
                                << "    }\n\n"
                                << "    void OnDestroy() override {\n"
                                << "        // Called when behaviour is removed or owner is destroyed\n"
                                << "    }\n\n"
                                << "};\n\n"
                                << "// Register behaviour with the engine's BehaviourRegistry\n"
                                << "REGISTER_BEHAVIOUR(" << behName << ")\n";
                        }

                        std::ofstream metaFile(metaPath);
                        if (metaFile.is_open()) {
                            AssetID aid = AssetID::CreateRandom();
                            std::string vPath = (targetDir.empty() || targetDir == "/") ?
                                ("/" + behName) : (targetDir + "/" + behName);
                            metaFile << "# Eunoia-Editor Asset Sidecar Metadata\n"
                                     << "assetId: " << aid.ToString() << "\n"
                                     << "type: Behaviour Script\n"
                                     << "baseClass: EunoiaBehaviour\n"
                                     << "virtualPath: " << vPath << "\n";
                        }
#ifdef _WIN32
                        SetFileAttributesW(metaPath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif

                        // Register the newly created script file immediately so it appears in Details Panel Behaviours list
                        BehaviourRegistry::Get().RegisterScriptFile(behName, cppPath.string());
                        TriggerBackgroundRegistrySync(contentRootPath);
                        AddLog("LogContent", "Created Behaviour asset: " + behName + ".cpp in " + targetDir, 2);

                        // Open project as workspace in VS Code and open the script file
                        OpenScriptInCodeEditor(cppPath.string());

                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // User Interface asset creation modal
                if (showNewUIPopup) {
                    ImGui::OpenPopup("Create User Interface##UIModal");
                    showNewUIPopup = false;
                }
                if (ImGui::BeginPopupModal("Create User Interface##UIModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Enter User Interface Name (.eui):");
                    ImGui::SetNextItemWidth(300.0f);
                    ImGui::InputText("##UINameInput", newUINameBuf, sizeof(newUINameBuf));

                    std::string inputName = newUINameBuf;
                    while (!inputName.empty() && isspace((unsigned char)inputName.front())) inputName.erase(inputName.begin());
                    while (!inputName.empty() && isspace((unsigned char)inputName.back())) inputName.pop_back();

                    bool isValidName = !inputName.empty();
                    if (isValidName) {
                        ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "Will create: %s.eui in %s", inputName.c_str(), targetDir.c_str());
                    }
                    ImGui::Spacing();

                    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
                    if (!isValidName) ImGui::BeginDisabled();
                    bool createClicked = ImGui::Button("Create", ImVec2(120, 0));
                    if (!isValidName) ImGui::EndDisabled();

                    if (isValidName && (createClicked || enterPressed)) {
                        std::string diskSub = targetDir;
                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                        std::filesystem::path uiDir = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                        std::error_code bec;
                        std::filesystem::create_directories(uiDir, bec);

                        std::filesystem::path euiPath = uiDir / (inputName + ".eui");
                        EunoiaUI::UISystemManager::Get().CreateNewUI(inputName, euiPath.string());

                        // Write .assetmeta sidecar
                        std::filesystem::path metaPath = uiDir / (inputName + ".eui.assetmeta");
                        std::ofstream metaFile(metaPath);
                        AssetID aid = AssetID::CreateRandom();
                        std::string vPath = (targetDir.empty() || targetDir == "/") ?
                            ("/" + inputName) : (targetDir + "/" + inputName);
                        if (metaFile.is_open()) {
                            metaFile << "# Eunoia-Editor Asset Sidecar Metadata\n"
                                     << "assetId: " << aid.ToString() << "\n"
                                     << "type: User Interface\n"
                                     << "virtualPath: " << vPath << "\n";
                        }
#ifdef _WIN32
                        SetFileAttributesW(metaPath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
                        // Register in AssetRegistry immediately
                        AssetMetadata vMeta;
                        vMeta.id = aid;
                        vMeta.type = AssetType::UserInterface;
                        vMeta.objectName = inputName;
                        std::filesystem::path rel = std::filesystem::relative(euiPath, contentRootPath, bec);
                        vMeta.sourcePath = bec ? (inputName + ".eui") : rel.generic_string();
                        vMeta.virtualPath = vPath;
                        AssetRegistry::Get().RegisterAsset(vMeta);

                        TriggerBackgroundRegistrySync(contentRootPath);
                        OpenUIEditor(euiPath.string());
                        AddLog("LogContent", "Created User Interface: " + inputName + ".eui in " + targetDir, 2);

                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // Node Behaviour asset creation modal
                if (showNewNodeBehaviourPopup) {
                    ImGui::OpenPopup("Create Node Behaviour##NBModal");
                    showNewNodeBehaviourPopup = false;
                }
                if (ImGui::BeginPopupModal("Create Node Behaviour##NBModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Enter Node Behaviour Name (.enb):");
                    ImGui::SetNextItemWidth(300.0f);
                    ImGui::InputText("##NBNameInput", newNodeBehaviourNameBuf, sizeof(newNodeBehaviourNameBuf));

                    std::string inputName = newNodeBehaviourNameBuf;
                    while (!inputName.empty() && isspace((unsigned char)inputName.front())) inputName.erase(inputName.begin());
                    while (!inputName.empty() && isspace((unsigned char)inputName.back())) inputName.pop_back();

                    bool isValidName = !inputName.empty();
                    if (isValidName) {
                        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Will create: %s.enb in %s", inputName.c_str(), targetDir.c_str());
                    }
                    ImGui::Spacing();

                    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
                    if (!isValidName) ImGui::BeginDisabled();
                    bool createClicked = ImGui::Button("Create", ImVec2(120, 0));
                    if (!isValidName) ImGui::EndDisabled();

                    if (isValidName && (createClicked || enterPressed)) {
                        std::string diskSub = targetDir;
                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                        std::filesystem::path nbDir = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                        std::error_code bec;
                        std::filesystem::create_directories(nbDir, bec);

                        std::string createdPath = Eunoia::NodeBehaviour::NodeBehaviourManager::Get().CreateNewNodeBehaviour(nbDir.string(), inputName);

                        // Write .assetmeta sidecar
                        std::filesystem::path enbPath(createdPath);
                        std::filesystem::path metaPath = nbDir / (inputName + ".enb.assetmeta");
                        std::ofstream metaFile(metaPath);
                        AssetID aid = AssetID::CreateRandom();
                        std::string vPath = (targetDir.empty() || targetDir == "/") ?
                            ("/" + inputName) : (targetDir + "/" + inputName);
                        if (metaFile.is_open()) {
                            metaFile << "# Eunoia-Editor Asset Sidecar Metadata\n"
                                     << "assetId: " << aid.ToString() << "\n"
                                     << "type: Node Behaviour\n"
                                     << "virtualPath: " << vPath << "\n";
                        }
#ifdef _WIN32
                        SetFileAttributesW(metaPath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
                        // Register in AssetRegistry immediately
                        AssetMetadata vMeta;
                        vMeta.id = aid;
                        vMeta.type = AssetType::NodeBehaviour;
                        vMeta.objectName = inputName;
                        std::filesystem::path rel = std::filesystem::relative(enbPath, contentRootPath, bec);
                        vMeta.sourcePath = bec ? (inputName + ".enb") : rel.generic_string();
                        vMeta.virtualPath = vPath;
                        AssetRegistry::Get().RegisterAsset(vMeta);

                        TriggerBackgroundRegistrySync(contentRootPath);
                        OpenNodeBehaviourEditor(createdPath);
                        AddLog("LogContent", "Created Node Behaviour: " + inputName + ".enb in " + targetDir, 2);

                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // Animator Graph asset creation modal
                // Animation Blend asset creation modal
                                // Level Animation asset creation modal
                if (showNewLevelAnimationPopup) {
                    ImGui::OpenPopup("Create Level Animation##LAModal");
                    showNewLevelAnimationPopup = false;
                }
                if (ImGui::BeginPopupModal("Create Level Animation##LAModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Enter Level Animation Name (.elanim):");
                    ImGui::SetNextItemWidth(300.0f);
                    ImGui::InputText("##LANameInput", newLevelAnimationNameBuf, sizeof(newLevelAnimationNameBuf));

                    std::string inputName = newLevelAnimationNameBuf;
                    while (!inputName.empty() && isspace((unsigned char)inputName.front())) inputName.erase(inputName.begin());
                    while (!inputName.empty() && isspace((unsigned char)inputName.back())) inputName.pop_back();

                    bool isValidName = !inputName.empty();
                    if (isValidName) {
                        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Will create: %s.elanim in %s", inputName.c_str(), targetDir.c_str());
                    }
                    ImGui::Spacing();

                    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
                    if (!isValidName) ImGui::BeginDisabled();
                    bool createClicked = ImGui::Button("Create", ImVec2(120, 0));
                    if (!isValidName) ImGui::EndDisabled();

                    if (isValidName && (createClicked || enterPressed)) {
                        std::string diskSub = targetDir;
                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                        std::filesystem::path laDir = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                        std::error_code laec;
                        std::filesystem::create_directories(laDir, laec);

                        std::string createdPath = Eunoia::LevelAnimationManager::Get().CreateNewLevelAnimation(laDir.string(), inputName);

                        // Write .assetmeta sidecar
                        std::filesystem::path laPath(createdPath);
                        std::filesystem::path metaPath = laDir / (inputName + ".elanim.assetmeta");
                        std::ofstream metaFile(metaPath);
                        AssetID aid = AssetID::CreateRandom();
                        std::string vPath = (targetDir.empty() || targetDir == "/") ?
                            ("/" + inputName) : (targetDir + "/" + inputName);
                        if (metaFile.is_open()) {
                            metaFile << "# Eunoia-Editor Asset Sidecar Metadata\n"
                                     << "assetId: " << aid.ToString() << "\n"
                                     << "type: Level Animation\n"
                                     << "virtualPath: " << vPath << "\n";
                        }
#ifdef _WIN32
                        SetFileAttributesW(metaPath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
                        AssetMetadata vMeta;
                        vMeta.id = aid;
                        vMeta.type = AssetType::LevelAnimation;
                        vMeta.objectName = inputName;
                        vMeta.sourcePath = std::filesystem::relative(laPath, contentRootPath).string();
                        vMeta.virtualPath = vPath;
                        AssetRegistry::Get().RegisterAsset(vMeta);
                        TriggerBackgroundRegistrySync(contentRootPath);
                        OpenLevelAnimationEditor(createdPath);
                        AddLog("LogContent", "Created Level Animation: " + inputName + ".elanim in " + targetDir, 2);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                if (showNewAnimationBlendPopup) {
                    ImGui::OpenPopup("Create Animation Blend##BlendModal");
                    showNewAnimationBlendPopup = false;
                }
                if (ImGui::BeginPopupModal("Create Animation Blend##BlendModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Enter Animation Blend Name (.eblend):");
                    ImGui::SetNextItemWidth(300.0f);
                    ImGui::InputText("##BlendNameInput", newAnimationBlendNameBuf, sizeof(newAnimationBlendNameBuf));

                    std::string inputName = newAnimationBlendNameBuf;
                    while (!inputName.empty() && isspace((unsigned char)inputName.front())) inputName.erase(inputName.begin());
                    while (!inputName.empty() && isspace((unsigned char)inputName.back())) inputName.pop_back();

                    bool isValidName = !inputName.empty();
                    if (isValidName) {
                        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Will create: %s.eblend in %s", inputName.c_str(), targetDir.c_str());
                    }
                    ImGui::Spacing();

                    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
                    if (!isValidName) ImGui::BeginDisabled();
                    bool createClicked = ImGui::Button("Create", ImVec2(120, 0));
                    if (!isValidName) ImGui::EndDisabled();

                    if (isValidName && (createClicked || enterPressed)) {
                        std::string diskSub = targetDir;
                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                        std::filesystem::path blendDir = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                        std::error_code bec;
                        std::filesystem::create_directories(blendDir, bec);

                        std::string createdPath = Eunoia::AnimationBlendManager::Get().CreateNewAnimationBlend(blendDir.string(), inputName);

                        // Write .assetmeta sidecar
                        std::filesystem::path blendPath(createdPath);
                        std::filesystem::path metaPath = blendDir / (inputName + ".eblend.assetmeta");
                        std::ofstream metaFile(metaPath);
                        AssetID aid = AssetID::CreateRandom();
                        std::string vPath = (targetDir.empty() || targetDir == "/") ?
                            ("/" + inputName) : (targetDir + "/" + inputName);
                        if (metaFile.is_open()) {
                            metaFile << "# Eunoia-Editor Asset Sidecar Metadata\n"
                                     << "assetId: " << aid.ToString() << "\n"
                                     << "type: Animation Blend\n"
                                     << "virtualPath: " << vPath << "\n";
                        }
#ifdef _WIN32
                        SetFileAttributesW(metaPath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
                        // Register in AssetRegistry immediately
                        AssetMetadata vMeta;
                        vMeta.id = aid;
                        vMeta.type = AssetType::AnimationBlend;
                        vMeta.objectName = inputName;
                        std::filesystem::path rel = std::filesystem::relative(blendPath, contentRootPath, bec);
                        vMeta.sourcePath = bec ? (inputName + ".eblend") : rel.generic_string();
                        vMeta.virtualPath = vPath;
                        AssetRegistry::Get().RegisterAsset(vMeta);
                        TriggerBackgroundRegistrySync(contentRootPath);
                        OpenAnimationBlendEditor(createdPath);
                        AddLog("LogContent", "Created Animation Blend: " + inputName + ".eblend in " + targetDir, 2);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                if (showNewAnimatorGraphPopup) {
                    ImGui::OpenPopup("Create Animator Graph##AGModal");
                    showNewAnimatorGraphPopup = false;
                }
                if (ImGui::BeginPopupModal("Create Animator Graph##AGModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string targetDir = popupTargetVirtualDir.empty() ? currentVirtualDir : popupTargetVirtualDir;
                    ImGui::Text("Enter Animator Graph Name (.eanimator):");
                    ImGui::SetNextItemWidth(300.0f);
                    ImGui::InputText("##AGNameInput", newAnimatorGraphNameBuf, sizeof(newAnimatorGraphNameBuf));

                    std::string inputName = newAnimatorGraphNameBuf;
                    while (!inputName.empty() && isspace((unsigned char)inputName.front())) inputName.erase(inputName.begin());
                    while (!inputName.empty() && isspace((unsigned char)inputName.back())) inputName.pop_back();

                    bool isValidName = !inputName.empty();
                    if (isValidName) {
                        ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Will create: %s.eanimator in %s", inputName.c_str(), targetDir.c_str());
                    }
                    ImGui::Spacing();

                    bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter);
                    if (!isValidName) ImGui::BeginDisabled();
                    bool createClicked = ImGui::Button("Create", ImVec2(120, 0));
                    if (!isValidName) ImGui::EndDisabled();

                    if (isValidName && (createClicked || enterPressed)) {
                        std::string diskSub = targetDir;
                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                        std::filesystem::path agDir = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                        std::error_code bec;
                        std::filesystem::create_directories(agDir, bec);

                        std::string createdPath = Eunoia::AnimatorGraphManager::Get().CreateNewAnimatorGraph(agDir.string(), inputName);

                        // Write .assetmeta sidecar
                        std::filesystem::path agPath(createdPath);
                        std::filesystem::path metaPath = agDir / (inputName + ".eanimator.assetmeta");
                        std::ofstream metaFile(metaPath);
                        AssetID aid = AssetID::CreateRandom();
                        std::string vPath = (targetDir.empty() || targetDir == "/") ?
                            ("/" + inputName) : (targetDir + "/" + inputName);
                        if (metaFile.is_open()) {
                            metaFile << "# Eunoia-Editor Asset Sidecar Metadata\n"
                                     << "assetId: " << aid.ToString() << "\n"
                                     << "type: Animator Graph\n"
                                     << "virtualPath: " << vPath << "\n";
                        }
#ifdef _WIN32
                        SetFileAttributesW(metaPath.wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
                        // Register in AssetRegistry immediately
                        AssetMetadata vMeta;
                        vMeta.id = aid;
                        vMeta.type = AssetType::AnimatorGraph;
                        vMeta.objectName = inputName;
                        std::filesystem::path rel = std::filesystem::relative(agPath, contentRootPath, bec);
                        vMeta.sourcePath = bec ? (inputName + ".eanimator") : rel.generic_string();
                        vMeta.virtualPath = vPath;
                        AssetRegistry::Get().RegisterAsset(vMeta);

                        TriggerBackgroundRegistrySync(contentRootPath);
                        OpenAnimatorGraphEditor(createdPath);
                        AddLog("LogContent", "Created Animator Graph: " + inputName + ".eanimator in " + targetDir, 2);

                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                        popupTargetVirtualDir = "";
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // Batch Animation Import Dialog: asks user to select skeleton for all currently importing animation files
                if (showBatchSkeletonImportPopup) {
                    ImGui::OpenPopup("Select Skeleton for Animation Batch##AnimBatchModal");
                    showBatchSkeletonImportPopup = false;
                }
                if (ImGui::BeginPopupModal("Select Skeleton for Animation Batch##AnimBatchModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Importing %zu Animation File(s)", pendingBatchImportAnimFiles.size());
                    ImGui::Text("Select the target Skeleton Mesh for all currently importing animations:");
                    ImGui::Spacing();

                    std::vector<std::string> skelMeshes = Eunoia::SkeletalAnimationManager::Get().FindProjectSkeletalMeshes(contentRootPath.string());
                    std::string comboLabel = selectedBatchImportSkeletonMesh.empty() 
                        ? "[Choose Target Skeleton Mesh...]" 
                        : std::filesystem::path(selectedBatchImportSkeletonMesh).filename().string();

                    ImGui::SetNextItemWidth(360.0f);
                    if (ImGui::BeginCombo("##BatchSkelCombo", comboLabel.c_str())) {
                        if (skelMeshes.empty()) {
                            ImGui::Selectable("No 3D Models in Content/", false, ImGuiSelectableFlags_Disabled);
                        }
                        for (const auto& sm : skelMeshes) {
                            std::string smName = std::filesystem::path(sm).filename().string();
                            bool isSel = (sm == selectedBatchImportSkeletonMesh);
                            if (ImGui::Selectable(smName.c_str(), isSel)) {
                                selectedBatchImportSkeletonMesh = sm;
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    bool hasSelected = !selectedBatchImportSkeletonMesh.empty();
                    if (!hasSelected) ImGui::BeginDisabled();
                    if (ImGui::Button("Apply Skeleton & Import", ImVec2(200, 0))) {
                        // Apply selected skeleton to all importing animations
                        for (const auto& animPath : pendingBatchImportAnimFiles) {
                            std::string metaPath = animPath + ".assetmeta";
                            std::ofstream mf(metaPath, std::ios::app);
                            if (mf.is_open()) {
                                mf << "targetSkeleton: " << selectedBatchImportSkeletonMesh << "\n";
                            }
                            AddLog("LogContent", "Imported Animation " + std::filesystem::path(animPath).filename().string() + " linked to skeleton: " + std::filesystem::path(selectedBatchImportSkeletonMesh).filename().string(), 0);
                        }
                        pendingBatchImportAnimFiles.clear();
                        TriggerBackgroundRegistrySync(contentRootPath);
                        ImGui::CloseCurrentPopup();
                    }
                    if (!hasSelected) ImGui::EndDisabled();

                    ImGui::SameLine();
                    if (ImGui::Button("Cancel Batch", ImVec2(120, 0))) {
                        pendingBatchImportAnimFiles.clear();
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                // Bones Detected Import Options Dialog (Ask whether Skeletal Mesh or Animation when bones found)
                if (showBonesImportOptionPopup) {
                    ImGui::OpenPopup("Bones Detected - Import Options##BonesImportModal");
                    showBonesImportOptionPopup = false;
                }
                if (ImGui::BeginPopupModal("Bones Detected - Import Options##BonesImportModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    std::string fileName = std::filesystem::path(pendingBonesImportFilePath).filename().string();
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Bones / Skeleton Detected in Asset:");
                    ImGui::TextDisabled("%s", fileName.c_str());
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    ImGui::Text("How would you like to import this asset?");
                    ImGui::Spacing();

                    ImGui::RadioButton("Skeletal Mesh (Rigged 3D Character / Mesh)", &bonesImportOptionType, 0);
                    ImGui::Indent(24.0f);
                    ImGui::TextDisabled("Imports as a rigged 3D mesh with bones hierarchy.\nThis model will be registered as a Skeleton Mesh and will show\nwherever a Skeleton is needed (e.g. in Animator Graphs and animation linking).");
                    ImGui::Unindent(24.0f);

                    ImGui::Spacing();
                    ImGui::RadioButton("Animation (Skeletal Animation Clip / Motion)", &bonesImportOptionType, 1);
                    ImGui::Indent(24.0f);
                    ImGui::TextDisabled("Imports skeletal motion/keyframe curves linked to a project Skeleton Mesh.");

                    if (bonesImportOptionType == 1) {
                        ImGui::Spacing();
                        ImGui::Text("Target Skeleton Mesh:");
                        std::vector<std::string> skelMeshes = Eunoia::SkeletalAnimationManager::Get().FindProjectSkeletalMeshes(contentRootPath.string());

                        std::string comboLabel = selectedBonesAnimationTargetSkeleton.empty()
                            ? "[Choose Target Skeleton Mesh...]"
                            : std::filesystem::path(selectedBonesAnimationTargetSkeleton).filename().string();

                        ImGui::SetNextItemWidth(380.0f);
                        if (ImGui::BeginCombo("##BonesTargetSkelCombo", comboLabel.c_str())) {
                            if (skelMeshes.empty()) {
                                ImGui::Selectable("No Skeletal Meshes found in Content/", false, ImGuiSelectableFlags_Disabled);
                            }
                            for (const auto& sm : skelMeshes) {
                                std::string smName = std::filesystem::path(sm).filename().string();
                                bool isSel = (sm == selectedBonesAnimationTargetSkeleton);
                                if (ImGui::Selectable(smName.c_str(), isSel)) {
                                    selectedBonesAnimationTargetSkeleton = sm;
                                }
                            }
                            ImGui::EndCombo();
                        }

                        if (skelMeshes.empty()) {
                            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "(Note: No Skeletal Meshes found yet. You can import this model as a Skeletal Mesh first)");
                        }
                    }
                    ImGui::Unindent(24.0f);

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    if (ImGui::Button("Confirm & Import", ImVec2(160, 0))) {
                        std::filesystem::path fPath(pendingBonesImportFilePath);
                        std::string metaPath = pendingBonesImportFilePath + ".assetmeta";

                        AssetMetadata meta;
                        AssetMetadata* existingMeta = AssetRegistry::Get().FindByNameOrPath(fPath.filename().string());
                        if (existingMeta) {
                            meta = *existingMeta;
                        } else {
                            AssetRegistry::Get().ReadOrGenerateSidecar(fPath, meta);
                            meta.objectName = fPath.stem().string();
                            std::error_code bec;
                            std::filesystem::path rel = std::filesystem::relative(fPath, contentRootPath, bec);
                            meta.sourcePath = bec ? fPath.filename().string() : rel.generic_string();
                            meta.virtualPath = currentVirtualDir + "/" + meta.objectName;
                        }

                        if (bonesImportOptionType == 0) {
                            // Skeletal Mesh
                            meta.type = AssetType::SkeletalMesh;
                            AssetRegistry::Get().RegisterAsset(meta);

                            std::ofstream mf(metaPath);
                            if (mf.is_open()) {
                                mf << "# Eunoia-Editor Asset Sidecar Metadata\n";
                                mf << "assetId: " << meta.id.ToString() << "\n";
                                mf << "type: Skeletal Mesh\n";
                                mf << "isSkeletalMesh: true\n";
                                mf << "hasSkeleton: true\n";
                                mf << "virtualPath: " << meta.virtualPath << "\n";
                            }
#ifdef _WIN32
                            SetFileAttributesW(std::filesystem::path(metaPath).wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
                            AddLog("LogContent", "Registered Skeletal Mesh: " + fPath.filename().string() + " (Ready for Animator Graph)", 2);
                        } else {
                            // Animation
                            meta.type = AssetType::Animation;
                            AssetRegistry::Get().RegisterAsset(meta);

                            std::ofstream mf(metaPath);
                            if (mf.is_open()) {
                                mf << "# Eunoia-Editor Asset Sidecar Metadata\n";
                                mf << "assetId: " << meta.id.ToString() << "\n";
                                mf << "type: Animation\n";
                                mf << "isAnimation: true\n";
                                mf << "targetSkeleton: " << selectedBonesAnimationTargetSkeleton << "\n";
                                mf << "virtualPath: " << meta.virtualPath << "\n";
                            }
#ifdef _WIN32
                            SetFileAttributesW(std::filesystem::path(metaPath).wstring().c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
                            std::string skelName = selectedBonesAnimationTargetSkeleton.empty() ? "None" : std::filesystem::path(selectedBonesAnimationTargetSkeleton).filename().string();
                            AddLog("LogContent", "Registered Animation: " + fPath.filename().string() + " linked to Skeleton: " + skelName, 2);
                        }

                        TriggerBackgroundRegistrySync(contentRootPath);
                        pendingBonesImportFilePath = "";
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                        pendingBonesImportFilePath = "";
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }

                // Two-Pane Content Browser Layout (Left: Folders, Right: Assets)
                float maxTreeW = ImGui::GetWindowWidth() - 200.0f;
                if (maxTreeW < 140.0f) maxTreeW = 140.0f;
                contentBrowserFolderTreeWidth = std::clamp(contentBrowserFolderTreeWidth, 120.0f, maxTreeW);

                ImGui::BeginChild("CBFolderTreePane", ImVec2(contentBrowserFolderTreeWidth, 0), true);
                ImGui::TextColored(ImVec4(0.443f, 0.443f, 0.478f, 1.0f), "FOLDERS"); // dev.md text-gray-500
                ImGui::Separator();

                // Directory Hierarchy with highlighting and drag/drop
                auto RenderVirtualDirNode = [&](auto& self, const std::string& vDir) -> void {
                    std::vector<std::string> subdirs = AssetRegistry::Get().GetSubdirectories(vDir);
                    subdirs.erase(std::remove_if(subdirs.begin(), subdirs.end(), [](const std::string& s) {
                        return s.find(".internal") != std::string::npos;
                    }), subdirs.end());
                    std::string dirName = AssetPath::GetObjectName(vDir);
                    if (vDir == "/Game") dirName = "Content";

                    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
                    bool isCurrent = (vDir == currentVirtualDir);
                    if (isCurrent) nodeFlags |= ImGuiTreeNodeFlags_Selected;
                    if (subdirs.empty()) nodeFlags |= ImGuiTreeNodeFlags_Leaf;
                    else nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;

                    if (isCurrent) {
                        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.00f, 0.40f, 1.00f, 1.00f)); // #0066ff
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.10f, 0.50f, 1.00f, 1.00f));
                        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.00f, 0.35f, 0.90f, 1.00f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                    }

                    if (m_cbFolderStateNeedsApply && !subdirs.empty() && m_cbAppliedFolders.find(vDir) == m_cbAppliedFolders.end()) {
                        auto it = scene.contentBrowserFolderOpenState.find(vDir);
                        if (it != scene.contentBrowserFolderOpenState.end()) {
                            ImGui::SetNextItemOpen(it->second);
                        }
                        m_cbAppliedFolders.insert(vDir);
                    }

                    bool nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)std::hash<std::string>{}(vDir), nodeFlags, "%s", dirName.c_str());

                    if (isCurrent) {
                        ImGui::PopStyleColor(4);
                    }

                    if (!subdirs.empty()) {
                        scene.contentBrowserFolderOpenState[vDir] = nodeOpen;
                    }

                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                        currentVirtualDir = vDir;
                        selectedContentItem = "";
                        selectedContentItems.clear();
                        lastSelectedContentItem = "";
                    }

                    // Drag source for folder in tree
                    if (vDir != "/Game" && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        std::string payloadStr = "DIR:" + vDir + "\n";
                        ImGui::SetDragDropPayload("CONTENT_DRAG", payloadStr.c_str(), payloadStr.length() + 1);
                        ImGui::Text("%s", dirName.c_str());
                        ImGui::EndDragDropSource();
                    }

                    // Drop target on folder tree node
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                            std::string payloadData((const char*)payload->Data);
                            std::stringstream pss(payloadData);
                            std::string item;
                            while (std::getline(pss, item, '\n')) {
                                if (item.empty()) continue;
                                if (item.rfind("DIR:", 0) == 0) {
                                    MoveFolderToDirectory(item.substr(4), vDir, contentRootPath);
                                } else if (item.rfind("ASSET:", 0) == 0) {
                                    const AssetMetadata* m = AssetRegistry::Get().FindByPath(item.substr(6));
                                    if (m) MoveAssetToDirectory(m->id, vDir, contentRootPath);
                                }
                            }
                            TriggerBackgroundRegistrySync(contentRootPath);
                        } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                            std::string idStr((const char*)payload->Data);
                            MoveAssetToDirectory(AssetID::FromString(idStr), vDir, contentRootPath);
                            TriggerBackgroundRegistrySync(contentRootPath);
                        }
                        ImGui::EndDragDropTarget();
                    }

                    // Right click context menu on folder in tree
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("Open Folder")) {
                            currentVirtualDir = vDir;
                            selectedContentItem = "";
                            selectedContentItems.clear();
                            lastSelectedContentItem = "";
                        }
                        if (vDir != "/Game") {
                            if (ImGui::MenuItem("Rename Folder...", "F2")) {
                                OpenRenameFolderModal(vDir);
                            }
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("New Folder...")) {
                            popupTargetVirtualDir = vDir;
                            showNewFolderPopup = true;
                            snprintf(newFolderNameBuf, sizeof(newFolderNameBuf), "NewFolder");
                        }
                        if (ImGui::MenuItem("New Material (.emat)...")) {
                            popupTargetVirtualDir = vDir;
                            showNewMaterialPopup = true;
                            snprintf(newMaterialNameBuf, sizeof(newMaterialNameBuf), "M_NewMaterial");
                        }
                        if (ImGui::MenuItem("New Level Animation (.elanim)...")) {
                            popupTargetVirtualDir = vDir;
                            showNewLevelAnimationPopup = true;
                            snprintf(newLevelAnimationNameBuf, sizeof(newLevelAnimationNameBuf), "LA_NewAnimation");
                        }

                        if (ImGui::MenuItem("New Behaviour Script (.cpp)...")) {
                            popupTargetVirtualDir = vDir;
                            showNewBehaviourPopup = true;
                            std::string defName = "NewBehaviour";
                            if (IsBehaviourNameAlreadyExists(defName)) {
                                int idx = 1;
                                while (IsBehaviourNameAlreadyExists("NewBehaviour_" + std::to_string(idx))) idx++;
                                defName = "NewBehaviour_" + std::to_string(idx);
                            }
                            snprintf(newBehaviourNameBuf, sizeof(newBehaviourNameBuf), "%s", defName.c_str());
                        }
                        if (ImGui::MenuItem("New User Interface")) {
                            popupTargetVirtualDir = vDir;
                            showNewUIPopup = true;
                            snprintf(newUINameBuf, sizeof(newUINameBuf), "UI_NewInterface");
                        }
                        if (ImGui::MenuItem("New Animator Graph (.eanimator)...")) {
                            popupTargetVirtualDir = vDir;
                            showNewAnimatorGraphPopup = true;
                            snprintf(newAnimatorGraphNameBuf, sizeof(newAnimatorGraphNameBuf), "AG_NewAnimator");
                        }
                        if (ImGui::MenuItem("New Node Behaviour (.enb)...")) {
                            popupTargetVirtualDir = vDir;
                            showNewNodeBehaviourPopup = true;
                            snprintf(newNodeBehaviourNameBuf, sizeof(newNodeBehaviourNameBuf), "NB_NewBehaviour");
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Import Model...")) {
                            popupTargetVirtualDir = vDir;
                            ImportMeshWithSavePrompt(scene);
                        }
                        if (ImGui::MenuItem("Show in Explorer")) {
                            std::string diskSub = vDir;
                            if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                            if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                            std::filesystem::path folderDisk = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                            std::string cmd = "explorer \"" + folderDisk.string() + "\"";
                            system(cmd.c_str());
                        }
                        if (vDir != "/Game") {
                            ImGui::Separator();
                            if (ImGui::MenuItem("Delete Folder")) {
                                std::string diskSub = vDir;
                                if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                                if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                                std::filesystem::path folderDisk = contentRootPath / diskSub;
                                std::error_code delEc;
                                std::filesystem::remove_all(folderDisk, delEc);
                                if (currentVirtualDir == vDir || currentVirtualDir.rfind(vDir + "/", 0) == 0) {
                                    currentVirtualDir = AssetPath::GetDirectory(vDir);
                                }
                                TriggerBackgroundRegistrySync(contentRootPath);
                            }
                        }
                        ImGui::EndPopup();
                    }

                    if (nodeOpen) {
                        for (const auto& sub : subdirs) {
                            self(self, sub);
                        }
                        ImGui::TreePop();
                    }
                };

                RenderVirtualDirNode(RenderVirtualDirNode, "/Game");
                ImGui::EndChild();

                ImGui::SameLine(0.0f, 2.0f);

                // Horizontal Splitter between Folders tree and Asset items
                float splitterW = 5.0f;
                ImVec2 splitterPos = ImGui::GetCursorScreenPos();
                float splitterH = ImGui::GetContentRegionAvail().y;

                ImGui::InvisibleButton("##CBFolderSplitter", ImVec2(splitterW, splitterH > 10.0f ? splitterH : -1.0f));
                bool splitterHovered = ImGui::IsItemHovered();
                bool splitterActive = ImGui::IsItemActive();

                if (splitterHovered || splitterActive) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                }

                if (splitterActive) {
                    float delta = ImGui::GetIO().MouseDelta.x;
                    if (delta != 0.0f) {
                        contentBrowserFolderTreeWidth = std::clamp(contentBrowserFolderTreeWidth + delta, 120.0f, maxTreeW);
                    }
                }
                if (ImGui::IsItemDeactivated()) {
                    SaveEditorConfig();
                }
                if (splitterHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    contentBrowserFolderTreeWidth = 220.0f;
                    SaveEditorConfig();
                }

                // Draw splitter visual indicator line
                ImU32 splitterCol = splitterActive ? IM_COL32(0, 122, 204, 255) :
                                    (splitterHovered ? IM_COL32(0, 122, 204, 200) : IM_COL32(45, 48, 54, 255));
                ImGui::GetWindowDrawList()->AddRectFilled(splitterPos, ImVec2(splitterPos.x + splitterW, splitterPos.y + (splitterH > 10.0f ? splitterH : 200.0f)), splitterCol);

                ImGui::SameLine(0.0f, 2.0f);

                // Right Pane: Asset Table backed by AssetRegistry (dev.md Section 14)
                ImGui::BeginChild("CBAssetListPane", ImVec2(0, 0), true);

                // Query assets in current virtual directory or search across registry
                std::string searchStr = contentBrowserSearch;
                std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);

                std::vector<AssetMetadata> displayAssets;
                if (!searchStr.empty()) {
                    // Global search
                    if (searchStr.rfind("type:", 0) == 0) {
                        std::string typeFilter = searchStr.substr(5);
                        displayAssets = AssetRegistry::Get().FindByType(StringToAssetType(typeFilter));
                    } else if (searchStr.rfind("path:", 0) == 0) {
                        std::string pathFilter = searchStr.substr(5);
                        displayAssets = AssetRegistry::Get().FindInDirectory(pathFilter, true);
                    } else {
                        // Search by name, path, or AssetID
                        const auto& all = AssetRegistry::Get().GetAllAssets();
                        for (const auto& pair : all) {
                            const auto& m = pair.second;
                            std::string nLower = m.objectName;
                            std::transform(nLower.begin(), nLower.end(), nLower.begin(), ::tolower);
                            std::string pLower = m.virtualPath;
                            std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::tolower);
                            std::string idLower = m.id.ToString();
                            std::transform(idLower.begin(), idLower.end(), idLower.begin(), ::tolower);

                            if (nLower.find(searchStr) != std::string::npos ||
                                pLower.find(searchStr) != std::string::npos ||
                                idLower.find(searchStr) != std::string::npos) {
                                displayAssets.push_back(m);
                            }
                        }
                    }
                } else {
                    displayAssets = AssetRegistry::Get().FindInDirectory(currentVirtualDir, false);
                }

                // Subdirectories in current virtual folder
                std::vector<std::string> currentSubdirs = AssetRegistry::Get().GetSubdirectories(currentVirtualDir);
                currentSubdirs.erase(std::remove_if(currentSubdirs.begin(), currentSubdirs.end(), [](const std::string& s) {
                    return s.find(".internal") != std::string::npos;
                }), currentSubdirs.end());

                // Build ordered list of all visible item keys for Windows Shift+Click contiguous selection
                std::vector<std::string> visibleItemKeys;
                if (searchStr.empty()) {
                    for (const auto& subDir : currentSubdirs) {
                        visibleItemKeys.push_back("DIR:" + subDir);
                    }
                }
                std::unordered_set<std::string> seenItemKeys;
                for (const auto& meta : displayAssets) {
                    std::string sPath = meta.sourcePath;
                    std::transform(sPath.begin(), sPath.end(), sPath.begin(), ::tolower);
                    std::string vPath = meta.virtualPath;
                    std::transform(vPath.begin(), vPath.end(), vPath.begin(), ::tolower);
                    std::string oName = meta.objectName;
                    std::transform(oName.begin(), oName.end(), oName.begin(), ::tolower);

                    if (sPath.find(".assetmeta") != std::string::npos ||
                        sPath.find(".meta") != std::string::npos ||
                        sPath.find(".metadata") != std::string::npos ||
                        sPath.find(".html") != std::string::npos ||
                        sPath.find(".elogs") != std::string::npos ||
                        sPath.find(".log") != std::string::npos ||
                        sPath.find(".internal") != std::string::npos ||
                        vPath.find(".assetmeta") != std::string::npos ||
                        vPath.find(".meta") != std::string::npos ||
                        vPath.find(".metadata") != std::string::npos ||
                        vPath.find(".html") != std::string::npos ||
                        vPath.find(".elogs") != std::string::npos ||
                        vPath.find(".log") != std::string::npos ||
                        vPath.find(".internal") != std::string::npos ||
                        oName.find(".assetmeta") != std::string::npos ||
                        oName.find(".meta") != std::string::npos ||
                        oName.find(".internal") != std::string::npos ||
                        oName.find(".metadata") != std::string::npos) {
                        continue;
                    }

                    std::string itemKey = "ASSET:" + meta.virtualPath;
                    if (seenItemKeys.count(itemKey) == 0) {
                        seenItemKeys.insert(itemKey);
                        visibleItemKeys.push_back(itemKey);
                    }
                }

                // Selection click handler for Windows Ctrl/Shift multi-selection
                auto HandleItemClick = [&](const std::string& itemKey, const std::string& primaryVal) {
                    ImGuiIO& io = ImGui::GetIO();
                    if (io.KeyCtrl) {
                        if (selectedContentItems.count(itemKey)) {
                            selectedContentItems.erase(itemKey);
                            if (selectedContentItem == primaryVal) {
                                selectedContentItem = selectedContentItems.empty() ? "" : *selectedContentItems.begin();
                            }
                        } else {
                            selectedContentItems.insert(itemKey);
                            selectedContentItem = primaryVal;
                        }
                        lastSelectedContentItem = itemKey;
                    } else if (io.KeyShift && !lastSelectedContentItem.empty()) {
                        int idx1 = -1, idx2 = -1;
                        for (size_t i = 0; i < visibleItemKeys.size(); ++i) {
                            if (visibleItemKeys[i] == lastSelectedContentItem) idx1 = (int)i;
                            if (visibleItemKeys[i] == itemKey) idx2 = (int)i;
                        }
                        if (idx1 != -1 && idx2 != -1) {
                            int start = std::min(idx1, idx2);
                            int end = std::max(idx1, idx2);
                            selectedContentItems.clear();
                            for (int i = start; i <= end; ++i) {
                                selectedContentItems.insert(visibleItemKeys[i]);
                            }
                            selectedContentItem = primaryVal;
                        } else {
                            selectedContentItems.clear();
                            selectedContentItems.insert(itemKey);
                            selectedContentItem = primaryVal;
                            lastSelectedContentItem = itemKey;
                        }
                    } else {
                        selectedContentItems.clear();
                        selectedContentItems.insert(itemKey);
                        selectedContentItem = primaryVal;
                        lastSelectedContentItem = itemKey;
                    }
                };

                // Unified Content Item structure for Content View cards
                struct CBItem {
                    bool isFolder = false;
                    std::string subDir;
                    AssetMetadata meta;
                    std::string itemKey;
                    std::string primaryVal;
                    std::string displayName;
                };

                std::vector<CBItem> allItems;
                std::unordered_set<std::string> seenDisplayKeys;

                if (searchStr.empty()) {
                    for (const auto& subDir : currentSubdirs) {
                        CBItem item;
                        item.isFolder = true;
                        item.subDir = subDir;
                        item.itemKey = "DIR:" + subDir;
                        item.primaryVal = subDir;
                        item.displayName = AssetPath::GetObjectName(subDir);
                        allItems.push_back(std::move(item));
                    }
                }

                for (const auto& meta : displayAssets) {
                    std::string sPath = meta.sourcePath;
                    std::transform(sPath.begin(), sPath.end(), sPath.begin(), ::tolower);
                    std::string vPath = meta.virtualPath;
                    std::transform(vPath.begin(), vPath.end(), vPath.begin(), ::tolower);
                    std::string oName = meta.objectName;
                    std::transform(oName.begin(), oName.end(), oName.begin(), ::tolower);

                    if (sPath.find(".assetmeta") != std::string::npos ||
                        sPath.find(".meta") != std::string::npos ||
                        sPath.find(".metadata") != std::string::npos ||
                        sPath.find(".html") != std::string::npos ||
                        sPath.find(".elogs") != std::string::npos ||
                        sPath.find(".log") != std::string::npos ||
                        vPath.find(".assetmeta") != std::string::npos ||
                        vPath.find(".meta") != std::string::npos ||
                        vPath.find(".metadata") != std::string::npos ||
                        vPath.find(".html") != std::string::npos ||
                        vPath.find(".elogs") != std::string::npos ||
                        vPath.find(".log") != std::string::npos ||
                        oName.find(".assetmeta") != std::string::npos ||
                        oName.find(".meta") != std::string::npos ||
                        oName.find(".metadata") != std::string::npos) {
                        continue;
                    }

                    std::string key = "ASSET:" + meta.virtualPath;
                    if (seenDisplayKeys.count(key) > 0 || (!sPath.empty() && seenDisplayKeys.count("SRC:" + sPath) > 0)) {
                        continue;
                    }
                    seenDisplayKeys.insert(key);
                    if (!sPath.empty()) seenDisplayKeys.insert("SRC:" + sPath);

                    CBItem item;
                    item.isFolder = false;
                    item.meta = meta;
                    item.itemKey = key;
                    item.primaryVal = meta.virtualPath;
                    item.displayName = meta.objectName;
                    allItems.push_back(std::move(item));
                }

                // Check physical directory for project files (.euproject, .eproject) or standalone files not in registry
                if (searchStr.empty()) {
                    std::string diskSub = currentVirtualDir;
                    if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                    if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                    std::filesystem::path currentDiskPath = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);

                    std::error_code dirEc;
                    if (std::filesystem::exists(currentDiskPath, dirEc) && std::filesystem::is_directory(currentDiskPath, dirEc)) {
                        for (const auto& entry : std::filesystem::directory_iterator(currentDiskPath, dirEc)) {
                            if (!entry.is_directory(dirEc)) {
                                std::string ext = entry.path().extension().string();
                                std::string extLower = ext;
                                std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);

                                if (extLower == ".euproject" || extLower == ".eproject") {
                                    std::filesystem::path relPath = std::filesystem::relative(entry.path(), contentRootPath, dirEc);
                                    std::string srcPath = dirEc ? entry.path().filename().string() : relPath.generic_string();
                                    std::string vPath = currentVirtualDir + "/" + entry.path().stem().string();

                                    std::string key = "PROJ:" + entry.path().string();
                                    if (seenDisplayKeys.count(key) == 0 && seenDisplayKeys.count("SRC:" + srcPath) == 0) {
                                        seenDisplayKeys.insert(key);
                                        seenDisplayKeys.insert("SRC:" + srcPath);

                                        CBItem projItem;
                                        projItem.isFolder = false;
                                        projItem.meta.id = AssetID::CreateRandom();
                                        projItem.meta.type = AssetType::Project;
                                        projItem.meta.objectName = entry.path().stem().string();
                                        projItem.meta.sourcePath = srcPath;
                                        projItem.meta.virtualPath = vPath;
                                        projItem.itemKey = key;
                                        projItem.primaryVal = vPath;
                                        projItem.displayName = entry.path().filename().string();
                                        allItems.push_back(std::move(projItem));
                                    }
                                }
                            }
                        }
                    }
                }

                if (allItems.empty()) {
                    ImGui::Spacing();
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.55f, 0.62f, 0.72f, 1.0f), "    (No items found in this directory. Right-click here to create)");
                } else {
                    float availWidth = ImGui::GetContentRegionAvail().x;
                    float cardW = contentThumbnailSize;
                    if (cardW < 72.0f) cardW = 72.0f;
                    if (cardW > 160.0f) cardW = 160.0f;
                    float cardH = cardW * 1.20f;
                    float spacing = 8.0f;

                    int itemsPerRow = (int)((availWidth + spacing) / (cardW + spacing));
                    if (itemsPerRow < 1) itemsPerRow = 1;

                    ImDrawList* drawList = ImGui::GetWindowDrawList();

                    for (size_t idx = 0; idx < allItems.size(); ++idx) {
                        const auto& item = allItems[idx];
                        if (idx > 0 && (idx % itemsPerRow) != 0) {
                            ImGui::SameLine(0, spacing);
                        }

                        ImGui::PushID(item.itemKey.c_str());

                        bool isSelected = (selectedContentItems.count(item.itemKey) > 0 || selectedContentItem == item.primaryVal);
                        ImVec2 cardPos = ImGui::GetCursorScreenPos();
                        ImVec2 cardSize(cardW, cardH);
                        ImVec2 pMin = cardPos;
                        ImVec2 pMax = ImVec2(cardPos.x + cardW, cardPos.y + cardH);

                        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));

                        bool clicked = ImGui::Selectable("##cbCard", isSelected, ImGuiSelectableFlags_AllowDoubleClick, cardSize);

                        ImGui::PopStyleColor(3);

                        bool hovered = ImGui::IsItemHovered();

                        if (clicked) {
                            HandleItemClick(item.itemKey, item.primaryVal);
                        }

                        // Double-click action
                        if (hovered && ImGui::IsMouseDoubleClicked(0)) {
                            if (item.isFolder) {
                                currentVirtualDir = item.subDir;
                                selectedContentItem = "";
                                selectedContentItems.clear();
                                lastSelectedContentItem = "";
                            } else {
                                if (item.meta.type == AssetType::Material) {
                                    std::filesystem::path fullMat = AssetManager::Get().ResolvePhysicalOrCookedPath(item.meta.sourcePath);
                                    std::error_code ec;
                                    if (fullMat.empty() || !std::filesystem::exists(fullMat, ec) || std::filesystem::file_size(fullMat, ec) == 0) {
                                        fullMat = contentRootPath / item.meta.sourcePath;
                                    }
                                    OpenMaterialEditor(fullMat.string());
                                } else if (item.meta.type == AssetType::Skybox) {
                                    std::filesystem::path fullSky = contentRootPath / item.meta.sourcePath;
                                    OpenSkyboxEditor(fullSky.string());
                                } else if (item.meta.type == AssetType::Water) {
                                    std::filesystem::path fullWater = contentRootPath / item.meta.sourcePath;
                                    OpenWaterEditor(fullWater.string());
                                } else if (item.meta.type == AssetType::NodeBehaviour || item.meta.sourcePath.find(".enb") != std::string::npos) {
                                    std::filesystem::path fullNb = contentRootPath / item.meta.sourcePath;
                                    OpenNodeBehaviourEditor(fullNb.string());
                                    AddLog("LogContent", "Opened Node Behaviour: " + item.meta.objectName, 2);
                                } else if (item.meta.type == AssetType::AnimatorGraph || item.meta.sourcePath.find(".eanimator") != std::string::npos || item.meta.sourcePath.find(".animator") != std::string::npos) {
                                    std::filesystem::path fullAg = contentRootPath / item.meta.sourcePath;
                                    OpenAnimatorGraphEditor(fullAg.string());
                                    AddLog("LogContent", "Opened Animator Graph: " + item.meta.objectName, 2);
                                } else if (item.meta.type == AssetType::UserInterface || item.meta.type == AssetType::UserInterface3D) {
                                    std::filesystem::path fullUI = contentRootPath / item.meta.sourcePath;
                                    OpenUIEditor(fullUI.string());
                                    AddLog("LogContent", "Opened User Interface: " + item.meta.objectName, 2);
                                } else if (item.meta.type == AssetType::Mesh || item.meta.type == AssetType::SkeletalMesh) {
                                    std::filesystem::path fullMesh = AssetManager::Get().ResolvePhysicalOrCookedPath(item.meta.sourcePath);
                                    std::error_code ec;
                                    if (fullMesh.empty() || !std::filesystem::exists(fullMesh, ec) || std::filesystem::file_size(fullMesh, ec) == 0) {
                                        fullMesh = contentRootPath / item.meta.sourcePath;
                                    }
                                    GameObject& newObj = scene.AddImportedMesh(fullMesh.string(), glm::vec3(0.0f, 0.5f, 0.0f));
                                    scene.selectedId = newObj.id;
                                    AddLog("LogMesh", "Spawned 3D Mesh in level: " + item.meta.objectName, 2);
                                } else if (item.meta.type == AssetType::Level) {
                                    std::filesystem::path fullLevel = AssetManager::Get().ResolvePhysicalOrCookedPath(item.meta.sourcePath);
                                    std::error_code ec;
                                    if (fullLevel.empty() || !std::filesystem::exists(fullLevel, ec) || std::filesystem::file_size(fullLevel, ec) == 0) {
                                        fullLevel = contentRootPath / item.meta.sourcePath;
                                    }
                                    OpenLevelFromPath(scene, fullLevel.string());

                                } else if (item.meta.type == AssetType::Script) {
                                    std::filesystem::path fullScript = contentRootPath / item.meta.sourcePath;
                                    OpenScriptInCodeEditor(fullScript.string());
                                } else if (item.meta.type == AssetType::Project) {
                                    std::filesystem::path fullProjPath = contentRootPath / item.meta.sourcePath;
                                    std::error_code ec;
                                    if (!std::filesystem::exists(fullProjPath, ec)) {
                                        fullProjPath = activeProjectRoot / item.meta.sourcePath;
                                    }
                                    std::filesystem::path projRootDir = fullProjPath.parent_path();
                                    if (projRootDir.filename() == "Content") {
                                        projRootDir = projRootDir.parent_path();
                                    }
                                    if (currentCamera) {
                                        LoadProject(projRootDir, scene, *currentCamera);
                                    }
                                } else {
                                    OpenReferenceViewer(item.meta.id);
                                }
                            }
                        }

                        // Drag & Drop Source
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                            std::string dragStr = "";
                            if (selectedContentItems.count(item.itemKey) > 0 && selectedContentItems.size() > 1) {
                                for (const auto& selKey : selectedContentItems) dragStr += selKey + "\n";
                            } else {
                                dragStr = item.itemKey + "\n";
                            }
                            ImGui::SetDragDropPayload("CONTENT_DRAG", dragStr.c_str(), dragStr.length() + 1);

                            if (!item.isFolder) {
                                std::string idStr = item.meta.id.ToString();
                                ImGui::SetDragDropPayload("ASSET_ID", idStr.c_str(), idStr.length() + 1);
                            }

                            if (selectedContentItems.size() > 1 && selectedContentItems.count(item.itemKey)) {
                                ImGui::Text("Moving %d items", (int)selectedContentItems.size());
                            } else {
                                const char* icon = item.isFolder ? "" : GetAssetTypeIcon(item.meta.type);
                                ImGui::Text("%s %s", icon, item.displayName.c_str());
                            }
                            if (!item.isFolder) {
                                if (item.meta.type == AssetType::Mesh || item.meta.type == AssetType::SkeletalMesh) {
                                    ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "[Drag to Viewport to spawn in Level]");
                                } else if (item.meta.type == AssetType::Level) {
                                    ImGui::TextColored(ImVec4(0.30f, 0.85f, 0.95f, 1.0f), "[Drag to Viewport to open Level]");
                                }
                            }
                            ImGui::EndDragDropSource();
                        }

                        // Drop Target on Folder Cards
                        if (item.isFolder && ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                                std::string payloadData((const char*)payload->Data);
                                std::stringstream pss(payloadData);
                                std::string dragItem;
                                while (std::getline(pss, dragItem, '\n')) {
                                    if (dragItem.empty()) continue;
                                    if (dragItem.rfind("DIR:", 0) == 0) {
                                        MoveFolderToDirectory(dragItem.substr(4), item.subDir, contentRootPath);
                                    } else if (dragItem.rfind("ASSET:", 0) == 0) {
                                        const AssetMetadata* m = AssetRegistry::Get().FindByPath(dragItem.substr(6));
                                        if (m) MoveAssetToDirectory(m->id, item.subDir, contentRootPath);
                                    }
                                }
                                TriggerBackgroundRegistrySync(contentRootPath);
                            } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                                std::string idStr((const char*)payload->Data);
                                MoveAssetToDirectory(AssetID::FromString(idStr), item.subDir, contentRootPath);
                                TriggerBackgroundRegistrySync(contentRootPath);
                            }
                            ImGui::EndDragDropTarget();
                        }

                        // Context Menus
                        if (item.isFolder) {
                            if (ImGui::BeginPopupContextItem("FolderCardCtx")) {
                                if (ImGui::MenuItem("Open Folder")) {
                                    currentVirtualDir = item.subDir;
                                    selectedContentItem = "";
                                    selectedContentItems.clear();
                                    lastSelectedContentItem = "";
                                }
                                if (item.subDir != "/Game") {
                                    if (ImGui::MenuItem("Rename Folder...", "F2")) {
                                        OpenRenameFolderModal(item.subDir);
                                    }
                                }
                                ImGui::Separator();
                                if (ImGui::MenuItem("New Folder")) {
                                    popupTargetVirtualDir = item.subDir;
                                    showNewFolderPopup = true;
                                    snprintf(newFolderNameBuf, sizeof(newFolderNameBuf), "NewFolder");
                                }
                                if (ImGui::MenuItem("New Material")) {
                                    popupTargetVirtualDir = item.subDir;
                                    showNewMaterialPopup = true;
                                    snprintf(newMaterialNameBuf, sizeof(newMaterialNameBuf), "M_NewMaterial");
                                }
                                if (ImGui::MenuItem("New Level Animation (.elanim)...")) {
                                    popupTargetVirtualDir = item.subDir;
                                    showNewLevelAnimationPopup = true;
                                    snprintf(newLevelAnimationNameBuf, sizeof(newLevelAnimationNameBuf), "LA_NewAnimation");
                                }

                                if (ImGui::MenuItem("New Behaviour Script (.cpp)...")) {
                                    popupTargetVirtualDir = item.subDir;
                                    showNewBehaviourPopup = true;
                                    std::string defName = "NewBehaviour";
                                    if (IsBehaviourNameAlreadyExists(defName)) {
                                        int bIdx = 1;
                                        while (IsBehaviourNameAlreadyExists("NewBehaviour_" + std::to_string(bIdx))) bIdx++;
                                        defName = "NewBehaviour_" + std::to_string(bIdx);
                                    }
                                    snprintf(newBehaviourNameBuf, sizeof(newBehaviourNameBuf), "%s", defName.c_str());
                                }
                                if (ImGui::MenuItem("New User Interface")) {
                                    popupTargetVirtualDir = item.subDir;
                                    showNewUIPopup = true;
                                    snprintf(newUINameBuf, sizeof(newUINameBuf), "UI_NewInterface");
                                }
                                if (ImGui::MenuItem("New Animator Graph (.eanimator)...")) {
                                    popupTargetVirtualDir = item.subDir;
                                    showNewAnimatorGraphPopup = true;
                                    snprintf(newAnimatorGraphNameBuf, sizeof(newAnimatorGraphNameBuf), "AG_NewAnimator");
                                }
                        if (ImGui::MenuItem("New Node Behaviour (.enb)...")) {
                                    popupTargetVirtualDir = item.subDir;
                                    showNewNodeBehaviourPopup = true;
                                    snprintf(newNodeBehaviourNameBuf, sizeof(newNodeBehaviourNameBuf), "NB_NewBehaviour");
                                }
                                ImGui::Separator();
                                if (ImGui::MenuItem("Import Model...")) {
                                    popupTargetVirtualDir = item.subDir;
                                    ImportMeshWithSavePrompt(scene);
                                }
                                if (ImGui::MenuItem("Show in Explorer")) {
                                    std::string diskSub = item.subDir;
                                    if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                                    if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                                    std::filesystem::path folderDisk = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                                    std::string cmd = "explorer \"" + folderDisk.string() + "\"";
                                    system(cmd.c_str());
                                }
                                if (item.subDir != "/Game") {
                                    ImGui::Separator();
                                    if (ImGui::MenuItem("Delete Folder")) {
                                        std::string diskSub = item.subDir;
                                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                                        std::filesystem::path folderDisk = contentRootPath / diskSub;
                                        std::error_code delEc;
                                        std::filesystem::remove_all(folderDisk, delEc);
                                        if (currentVirtualDir == item.subDir || currentVirtualDir.rfind(item.subDir + "/", 0) == 0) {
                                            currentVirtualDir = AssetPath::GetDirectory(item.subDir);
                                        }
                                        TriggerBackgroundRegistrySync(contentRootPath);
                                    }
                                }
                                ImGui::EndPopup();
                            }
                        } else {
                            if (ImGui::BeginPopupContextItem("AssetCardCtx")) {
                                if (!selectedContentItems.count(item.itemKey)) {
                                    selectedContentItems.clear();
                                    selectedContentItems.insert(item.itemKey);
                                    selectedContentItem = item.meta.virtualPath;
                                    lastSelectedContentItem = item.itemKey;
                                }

                                if (item.meta.type == AssetType::Behaviour || item.meta.type == AssetType::Script) {
                                    std::filesystem::path fullScript = contentRootPath / item.meta.sourcePath;
                                    if (ImGui::MenuItem("Open in VS Code")) {
                                        OpenScriptInCodeEditor(fullScript.string());
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::Material) {
                                    if (ImGui::MenuItem("Open in Material Editor")) {
                                        std::filesystem::path fullMat = contentRootPath / item.meta.sourcePath;
                                        OpenMaterialEditor(fullMat.string());
                                    }
                                    if (ImGui::MenuItem("Apply to Selected Actor")) {
                                        std::filesystem::path fullMat = contentRootPath / item.meta.sourcePath;
                                        MaterialAsset ma;
                                        if (LoadMaterialFile(fullMat.string(), ma)) {
                                            GameObject* sel = scene.GetSelected();
                                            if (sel) {
                                                ApplyMaterialToActorAndChildren(scene, sel, ma);
                                            }
                                        }
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::Skybox) {
                                    if (ImGui::MenuItem("Open in Skybox Editor")) {
                                        std::filesystem::path fullSky = contentRootPath / item.meta.sourcePath;
                                        OpenSkyboxEditor(fullSky.string());
                                    }
                                    if (ImGui::MenuItem("Set as Active World Skybox")) {
                                        std::filesystem::path fullSky = contentRootPath / item.meta.sourcePath;
                                        SkyboxAsset sky;
                                        if (LoadSkyboxFile(fullSky.string(), sky)) {
                                            ApplySkyboxToScene(scene, sky);
                                        }
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::Water) {
                                    if (ImGui::MenuItem("Open in Water Editor")) {
                                        std::filesystem::path fullWater = contentRootPath / item.meta.sourcePath;
                                        OpenWaterEditor(fullWater.string());
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::NodeBehaviour || item.meta.sourcePath.find(".enb") != std::string::npos) {
                                    if (ImGui::MenuItem("Open in Node Behaviour Editor")) {
                                        std::filesystem::path fullNb = contentRootPath / item.meta.sourcePath;
                                        OpenNodeBehaviourEditor(fullNb.string());
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::AnimationBlend || item.meta.sourcePath.find(".eblend") != std::string::npos) {
                                    if (ImGui::MenuItem("Open in Animation Blend Editor")) {
                                        std::filesystem::path fullBlend = contentRootPath / item.meta.sourcePath;
                                        OpenAnimationBlendEditor(fullBlend.string());
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::LevelAnimation || item.meta.sourcePath.find(".elanim") != std::string::npos) {
                                    if (ImGui::MenuItem("Open in Animation Timeline")) {
                                        std::filesystem::path fullAnim = contentRootPath / item.meta.sourcePath;
                                        OpenLevelAnimationEditor(fullAnim.string());
                                        AddLog("LogLevelAnimation", "Opened Level Animation: " + item.meta.objectName, 2);
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::AnimatorGraph || item.meta.sourcePath.find(".eanimator") != std::string::npos || item.meta.sourcePath.find(".animator") != std::string::npos) {
                                    if (ImGui::MenuItem("Open in Animator Graph Editor")) {
                                        std::filesystem::path fullAg = contentRootPath / item.meta.sourcePath;
                                        OpenAnimatorGraphEditor(fullAg.string());
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::UserInterface || item.meta.type == AssetType::UserInterface3D) {
                                    if (ImGui::MenuItem(item.meta.type == AssetType::UserInterface3D ? "Open in UI 3D Editor" : "Open in UI Editor")) {
                                        std::filesystem::path fullUI = contentRootPath / item.meta.sourcePath;
                                        OpenUIEditor(fullUI.string());
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::Mesh || item.meta.type == AssetType::SkeletalMesh) {
                                    if (ImGui::MenuItem("Spawn Mesh in Level")) {
                                        std::filesystem::path fullMesh = contentRootPath / item.meta.sourcePath;
                                        GameObject& newObj = scene.AddImportedMesh(fullMesh.string(), glm::vec3(0.0f, 0.5f, 0.0f));
                                        scene.selectedId = newObj.id;
                                        AddLog("LogMesh", "Spawned 3D Mesh in level: " + item.meta.objectName, 2);
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::Level) {
                                    if (ImGui::MenuItem("Open Level in Viewport")) {
                                        std::filesystem::path fullLevel = contentRootPath / item.meta.sourcePath;
                                        OpenLevelFromPath(scene, fullLevel.string());
                                    }
                                    ImGui::Separator();
                                }

                                if (item.meta.type == AssetType::Script) {
                                    if (ImGui::MenuItem("Edit Script in VS Code")) {
                                        std::filesystem::path fullScript = contentRootPath / item.meta.sourcePath;
                                        OpenScriptInCodeEditor(fullScript.string());
                                    }
                                    ImGui::Separator();
                                }
                                if (item.meta.type == AssetType::Project) {
                                    if (ImGui::MenuItem("Open Project in Editor")) {
                                        std::filesystem::path fullProjPath = contentRootPath / item.meta.sourcePath;
                                        std::error_code ec;
                                        if (!std::filesystem::exists(fullProjPath, ec)) {
                                            fullProjPath = activeProjectRoot / item.meta.sourcePath;
                                        }
                                        std::filesystem::path projRootDir = fullProjPath.parent_path();
                                        if (projRootDir.filename() == "Content") {
                                            projRootDir = projRootDir.parent_path();
                                        }
                                        if (currentCamera) {
                                            LoadProject(projRootDir, scene, *currentCamera);
                                        }
                                    }
                                    ImGui::Separator();
                                }

                                if (ImGui::MenuItem("Rename...", "F2")) {
                                    OpenRenameAssetModal(item.meta.id, item.meta.objectName, item.meta.virtualPath);
                                }
                                if (ImGui::MenuItem("View in Reference Viewer")) {
                                    OpenReferenceViewer(item.meta.id);
                                }
                                if (ImGui::MenuItem("Copy AssetID")) {
                                    ImGui::SetClipboardText(item.meta.id.ToString().c_str());
                                    AddLog("LogAsset", "Copied AssetID to clipboard: " + item.meta.id.ToString(), 0);
                                }
                                if (ImGui::MenuItem("Copy Virtual Path")) {
                                    ImGui::SetClipboardText(item.meta.virtualPath.c_str());
                                    AddLog("LogAsset", "Copied Virtual Path to clipboard: " + item.meta.virtualPath, 0);
                                }
                                ImGui::Separator();
                                if (ImGui::MenuItem("Delete Asset (Safe Check)")) {
                                    if (selectedContentItems.size() > 1 && selectedContentItems.count(item.itemKey)) {
                                        std::vector<std::string> toDelete(selectedContentItems.begin(), selectedContentItems.end());
                                        int delCount = 0;
                                        for (const auto& delKey : toDelete) {
                                            if (delKey.rfind("ASSET:", 0) == 0) {
                                                const AssetMetadata* dMeta = AssetRegistry::Get().FindByPath(delKey.substr(6));
                                                if (dMeta) {
                                                    auto refs = AssetRegistry::Get().GetReferences(dMeta->id);
                                                    if (refs.empty()) {
                                                        std::filesystem::path fullDisk = contentRootPath / dMeta->sourcePath;
                                                        std::filesystem::remove(fullDisk, ec);
                                                        std::filesystem::remove(fullDisk.string() + ".assetmeta", ec);

                                                        // Also delete respective cooked content files
                                                        std::filesystem::path cookedFile = AssetManager::Get().ResolvePhysicalOrCookedPath(dMeta->id);
                                                        if (!cookedFile.empty() && std::filesystem::exists(cookedFile, ec) && cookedFile != fullDisk) {
                                                            std::filesystem::remove(cookedFile, ec);
                                                        }
                                                        if (!activeProjectRoot.empty() && !dMeta->cookedPath.empty()) {
                                                            std::filesystem::path cookedByMeta = activeProjectRoot / "Cooked" / dMeta->cookedPath;
                                                            if (std::filesystem::exists(cookedByMeta, ec)) std::filesystem::remove(cookedByMeta, ec);
                                                        }
                                                        if (!activeProjectRoot.empty()) {
                                                            std::filesystem::path cookedBySrc = activeProjectRoot / "Cooked" / "Content" / dMeta->sourcePath;
                                                            if (std::filesystem::exists(cookedBySrc, ec)) std::filesystem::remove(cookedBySrc, ec);
                                                        }

                                                        AssetRegistry::Get().UnregisterAsset(dMeta->id);
                                                        delCount++;
                                                    }
                                                }
                                            }
                                        }
                                        selectedContentItems.clear();
                                        selectedContentItem = "";
                                        AddLog("LogAsset", "Deleted " + std::to_string(delCount) + " assets (including cooked content)", 1);
                                        TriggerBackgroundRegistrySync(contentRootPath);
                                    } else {
                                        auto refs = AssetRegistry::Get().GetReferences(item.meta.id);
                                        if (!refs.empty()) {
                                            AddLog("LogAsset", "Cannot delete '" + item.meta.objectName + "': referenced by " + std::to_string(refs.size()) + " assets! Inspect in Reference Viewer.", 3);
                                            OpenReferenceViewer(item.meta.id);
                                        } else {
                                            std::filesystem::path fullDisk = contentRootPath / item.meta.sourcePath;
                                            std::filesystem::remove(fullDisk, ec);
                                            std::filesystem::remove(fullDisk.string() + ".assetmeta", ec);

                                            // Also delete respective cooked content files
                                            std::filesystem::path cookedFile = AssetManager::Get().ResolvePhysicalOrCookedPath(item.meta.id);
                                            if (!cookedFile.empty() && std::filesystem::exists(cookedFile, ec) && cookedFile != fullDisk) {
                                                std::filesystem::remove(cookedFile, ec);
                                            }
                                            if (!activeProjectRoot.empty() && !item.meta.cookedPath.empty()) {
                                                std::filesystem::path cookedByMeta = activeProjectRoot / "Cooked" / item.meta.cookedPath;
                                                if (std::filesystem::exists(cookedByMeta, ec)) std::filesystem::remove(cookedByMeta, ec);
                                            }
                                            if (!activeProjectRoot.empty()) {
                                                std::filesystem::path cookedBySrc = activeProjectRoot / "Cooked" / "Content" / item.meta.sourcePath;
                                                if (std::filesystem::exists(cookedBySrc, ec)) std::filesystem::remove(cookedBySrc, ec);
                                            }

                                            AssetRegistry::Get().UnregisterAsset(item.meta.id);
                                            selectedContentItems.erase(item.itemKey);
                                            if (selectedContentItem == item.meta.virtualPath) selectedContentItem = "";
                                            AddLog("LogAsset", "Deleted asset and respective cooked content: " + item.meta.objectName, 1);
                                            TriggerBackgroundRegistrySync(contentRootPath);
                                        }
                                    }
                                }
                                ImGui::EndPopup();
                            }
                        }

                        // Tooltip
                        if (hovered) {
                            if (item.isFolder) {
                                ImGui::SetTooltip("Folder: %s\n(Double-click to open | Drag & Drop items here)", item.subDir.c_str());
                            } else {
                                size_t depCount = item.meta.dependencies.size();
                                size_t refCount = AssetRegistry::Get().GetReferences(item.meta.id).size();
                                bool isDummyRef = false;
                                std::error_code ec;
                                std::filesystem::path fullDisk = contentRootPath / item.meta.sourcePath;
                                if (!std::filesystem::exists(fullDisk, ec) || std::filesystem::file_size(fullDisk, ec) == 0) {
                                    if (!item.meta.cookedPath.empty()) isDummyRef = true;
                                }
                                const char* statusStr = isDummyRef ? "Cooked (Dummy Reference)" : (item.meta.isMissing ? "Missing" : (AssetManager::Get().IsLoaded(item.meta.id) ? "Loaded" : "Ready"));
                                ImGui::SetTooltip("Object: %s\nType: %s\nVirtual: %s\nAssetID: %s\nDisk: %s%s\nDeps: %d | Refs: %d | Status: %s\n(Double-click to open | Drag & Drop to viewport/folder)",
                                    item.meta.objectName.c_str(), AssetTypeToString(item.meta.type), item.meta.virtualPath.c_str(), item.meta.id.ToString().c_str(), item.meta.sourcePath.c_str(), isDummyRef ? " [Virtual Dummy]" : "", (int)depCount, (int)refCount, statusStr);
                            }
                        }

                        // Drawing Card Visuals with drawList
                        // 1. Outer Box & Selection Glow
                        ImU32 bgCol = isSelected ? IM_COL32(20, 60, 115, 235)
                                                 : (hovered ? IM_COL32(35, 43, 58, 240)
                                                            : IM_COL32(23, 27, 36, 215));
                        ImU32 borderCol = isSelected ? IM_COL32(40, 160, 255, 255)
                                                     : (hovered ? IM_COL32(80, 145, 235, 200)
                                                                : IM_COL32(46, 52, 68, 150));
                        float borderThick = isSelected ? 2.0f : 1.0f;

                        drawList->AddRectFilled(pMin, pMax, bgCol, 5.0f);
                        drawList->AddRect(pMin, pMax, borderCol, 5.0f, 0, borderThick);

                        // 2. Thumbnail Slot
                        float thumbPad = 5.0f;
                        float thumbH = cardW * 0.62f;
                        ImVec2 tMin(pMin.x + thumbPad, pMin.y + thumbPad);
                        ImVec2 tMax(pMax.x - thumbPad, pMin.y + thumbPad + thumbH);
                        drawList->AddRectFilled(tMin, tMax, IM_COL32(13, 16, 22, 255), 4.0f);
                        drawList->AddRect(tMin, tMax, IM_COL32(36, 42, 54, 180), 4.0f, 0, 1.0f);

                        // Determine Icon, GPU Texture Handle, and Accent Color
                        uint64_t iconGpuHandle = 0;
                        const char* iconTag = "FILE";
                        ImU32 accentCol = IM_COL32(135, 150, 170, 255);
                        const char* typeStr = "Unknown";

                        if (item.isFolder) {
                            iconGpuHandle = cbFolderIconGpuHandle;
                            iconTag = "DIR";
                            accentCol = IM_COL32(64, 204, 242, 255); // Cyan
                            typeStr = "Folder";
                        } else {
                            typeStr = AssetTypeToString(item.meta.type);
                            iconGpuHandle = GetContentBrowserAssetIcon(item.meta.type);
                            switch (item.meta.type) {
                                case AssetType::Material:        iconTag = "MAT";   accentCol = IM_COL32(89, 166, 255, 255); break; // Blue
                                case AssetType::Mesh:            iconTag = "3D";    accentCol = IM_COL32(242, 140, 51, 255);  break; // Orange
                                case AssetType::Skybox:          iconTag = "SKY";   accentCol = IM_COL32(76, 191, 230, 255);  break; // Sky Blue
                                case AssetType::Water:           iconTag = "WATER"; accentCol = IM_COL32(0, 160, 255, 255);   break;
                                case AssetType::Texture:         iconTag = "TEX";   accentCol = IM_COL32(51, 217, 128, 255);  break; // Emerald
                                case AssetType::VisualBehaviour: iconTag = "VBEH";  accentCol = IM_COL32(230, 89, 153, 255);  break; // Magenta
                                case AssetType::NodeBehaviour:   iconTag = "NB";    accentCol = IM_COL32(0, 210, 255, 255);   break; // Blueprint Cyan
                                case AssetType::Script:          iconTag = "BEH";   accentCol = IM_COL32(217, 102, 242, 255); break; // Purple
                                case AssetType::Level:           iconTag = "LEVEL"; accentCol = IM_COL32(51, 217, 242, 255);  break; // Cyan
                                case AssetType::Project:         iconTag = "PROJ";  accentCol = IM_COL32(255, 204, 0, 255);   break; // Gold
                                case AssetType::AnimationBlend:  iconTag = "BLEND"; accentCol = IM_COL32(160, 100, 240, 255); break; // Purple
                                case AssetType::AnimatorGraph:   iconTag = "AG";    accentCol = IM_COL32(0, 220, 255, 255);    break; // Cyan
                                case AssetType::LevelAnimation:  iconTag = "L-ANIM"; accentCol = IM_COL32(255, 120, 50, 255); break; // Orange
                                default:                         iconTag = "FILE";  accentCol = IM_COL32(153, 153, 153, 255); break;
                            }
                        }

                        // Center Icon (GPU texture or fallback badge)
                        if (iconGpuHandle != 0) {
                            float maxIconDim = std::min(tMax.x - tMin.x - 10.0f, tMax.y - tMin.y - 10.0f);
                            float iconDim = std::clamp(maxIconDim, 24.0f, 44.0f);
                            float iconCenterX = tMin.x + (tMax.x - tMin.x) * 0.5f;
                            float iconCenterY = tMin.y + (tMax.y - tMin.y) * 0.5f - 1.0f;
                            ImVec2 imgMin(iconCenterX - iconDim * 0.5f, iconCenterY - iconDim * 0.5f);
                            ImVec2 imgMax(iconCenterX + iconDim * 0.5f, iconCenterY + iconDim * 0.5f);
                            drawList->AddImage((ImTextureID)iconGpuHandle, imgMin, imgMax);
                        } else {
                            std::string badgeStr = std::string("[") + iconTag + "]";
                            ImVec2 iconSz = ImGui::CalcTextSize(badgeStr.c_str());
                            ImVec2 iconPos(tMin.x + (tMax.x - tMin.x - iconSz.x) * 0.5f,
                                           tMin.y + (tMax.y - tMin.y - iconSz.y) * 0.5f - 2.0f);
                            drawList->AddText(iconPos, accentCol, badgeStr.c_str());
                        }

                        // Unreal-Engine Signature Accent Stripe
                        drawList->AddRectFilled(ImVec2(tMin.x + 1.0f, tMax.y - 3.5f),
                                                ImVec2(tMax.x - 1.0f, tMax.y - 1.0f),
                                                accentCol, 2.0f);

                        // 3. Type Label Pill
                        float typeY = tMax.y + 3.0f;
                        std::string truncType = typeStr;
                        float maxLabelW = cardW - 10.0f;
                        if (ImGui::CalcTextSize(truncType.c_str()).x > maxLabelW) {
                            while (!truncType.empty() && ImGui::CalcTextSize((truncType + "..").c_str()).x > maxLabelW) {
                                truncType.pop_back();
                            }
                            truncType += "..";
                        }
                        drawList->AddText(ImVec2(pMin.x + 5.0f, typeY), IM_COL32(130, 148, 170, 220), truncType.c_str());

                        // 4. Object Name Label
                        float nameY = typeY + 13.0f;
                        std::string truncName = item.displayName;
                        if (ImGui::CalcTextSize(truncName.c_str()).x > maxLabelW) {
                            while (!truncName.empty() && ImGui::CalcTextSize((truncName + "...").c_str()).x > maxLabelW) {
                                truncName.pop_back();
                            }
                            truncName += "...";
                        }
                        ImU32 textCol = isSelected ? IM_COL32(255, 255, 255, 255)
                                                   : (hovered ? IM_COL32(235, 242, 255, 255)
                                                              : IM_COL32(210, 220, 235, 240));
                        drawList->AddText(ImVec2(pMin.x + 5.0f, nameY), textCol, truncName.c_str());

                        ImGui::PopID();
                    }
                }


                // Right click context menu on empty space of current directory
                if (ImGui::BeginPopupContextWindow("CBBackgroundContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                    if (ImGui::MenuItem("New Folder")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewFolderPopup = true;
                        snprintf(newFolderNameBuf, sizeof(newFolderNameBuf), "NewFolder");
                    }
                    if (ImGui::MenuItem("New Material")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewMaterialPopup = true;
                        snprintf(newMaterialNameBuf, sizeof(newMaterialNameBuf), "M_NewMaterial");
                    }
                    if (ImGui::MenuItem("New User Interface")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewUIPopup = true;
                        snprintf(newUINameBuf, sizeof(newUINameBuf), "UI_NewInterface");
                    }
                    if (ImGui::MenuItem("New Animator Graph (.eanimator)...")) {
                showNewAnimatorGraphPopup = true;
                snprintf(newAnimatorGraphNameBuf, sizeof(newAnimatorGraphNameBuf), "AG_NewAnimator");
            }
                        if (ImGui::MenuItem("New Level Animation (.elanim)...")) {
                popupTargetVirtualDir = currentVirtualDir;
                showNewLevelAnimationPopup = true;
                snprintf(newLevelAnimationNameBuf, sizeof(newLevelAnimationNameBuf), "LA_NewAnimation");
            }
            if (ImGui::MenuItem("New Animation Blend (.eblend)...")) {
                showNewAnimationBlendPopup = true;
                snprintf(newAnimationBlendNameBuf, sizeof(newAnimationBlendNameBuf), "BS_NewBlend");
            }
                        if (ImGui::MenuItem("New Node Behaviour (.enb)...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewNodeBehaviourPopup = true;
                        snprintf(newNodeBehaviourNameBuf, sizeof(newNodeBehaviourNameBuf), "NB_NewBehaviour");
                    }
                    if (ImGui::BeginMenu("Create")) {
                        if (ImGui::MenuItem("Level Animation (.elanim)")) {
                            popupTargetVirtualDir = currentVirtualDir;
                            showNewLevelAnimationPopup = true;
                            snprintf(newLevelAnimationNameBuf, sizeof(newLevelAnimationNameBuf), "LA_NewAnimation");
                        }
                        if (ImGui::MenuItem("Animator Graph (.eanimator)")) {
                            popupTargetVirtualDir = currentVirtualDir;
                            showNewAnimatorGraphPopup = true;
                            snprintf(newAnimatorGraphNameBuf, sizeof(newAnimatorGraphNameBuf), "AG_NewAnimator");
                        }
                        if (ImGui::MenuItem("Node Behaviour (.enb)")) {
                            popupTargetVirtualDir = currentVirtualDir;
                            showNewNodeBehaviourPopup = true;
                            snprintf(newNodeBehaviourNameBuf, sizeof(newNodeBehaviourNameBuf), "NB_NewBehaviour");
                        }
                        if (ImGui::MenuItem("User Interface (.eui)")) {
                            popupTargetVirtualDir = currentVirtualDir;
                            showNewUIPopup = true;
                            snprintf(newUINameBuf, sizeof(newUINameBuf), "UI_NewInterface");
                        }
                        if (ImGui::MenuItem("Behaviour Script (.cpp)")) {
                            popupTargetVirtualDir = currentVirtualDir;
                            showNewBehaviourPopup = true;
                            std::string defName = "NewBehaviour";
                            if (IsBehaviourNameAlreadyExists(defName)) {
                                int idx = 1;
                                while (IsBehaviourNameAlreadyExists("NewBehaviour_" + std::to_string(idx))) idx++;
                                defName = "NewBehaviour_" + std::to_string(idx);
                            }
                            snprintf(newBehaviourNameBuf, sizeof(newBehaviourNameBuf), "%s", defName.c_str());
                        }
                        if (ImGui::MenuItem("Material")) {
                            popupTargetVirtualDir = currentVirtualDir;
                            showNewMaterialPopup = true;
                            snprintf(newMaterialNameBuf, sizeof(newMaterialNameBuf), "M_NewMaterial");
                        }
                        if (ImGui::MenuItem("Folder")) {
                            popupTargetVirtualDir = currentVirtualDir;
                            showNewFolderPopup = true;
                            snprintf(newFolderNameBuf, sizeof(newFolderNameBuf), "NewFolder");
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();

                    if (ImGui::MenuItem("New Behaviour Script (.cpp)...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        showNewBehaviourPopup = true;
                        std::string defName = "NewBehaviour";
                        if (IsBehaviourNameAlreadyExists(defName)) {
                            int idx = 1;
                            while (IsBehaviourNameAlreadyExists("NewBehaviour_" + std::to_string(idx))) idx++;
                            defName = "NewBehaviour_" + std::to_string(idx);
                        }
                        snprintf(newBehaviourNameBuf, sizeof(newBehaviourNameBuf), "%s", defName.c_str());
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Import Model...")) {
                        popupTargetVirtualDir = currentVirtualDir;
                        ImportMeshWithSavePrompt(scene);
                    }

                    if (ImGui::MenuItem("Show in Explorer")) {
                        std::string diskSub = currentVirtualDir;
                        if (diskSub.rfind("/Game", 0) == 0) diskSub = diskSub.substr(5);
                        if (!diskSub.empty() && diskSub[0] == '/') diskSub.erase(0, 1);
                        std::filesystem::path folderDisk = diskSub.empty() ? contentRootPath : (contentRootPath / diskSub);
                        std::string cmd = "explorer \"" + folderDisk.string() + "\"";
                        system(cmd.c_str());
                    }
                    if (ImGui::MenuItem("Rescan Directory")) {
                        TriggerBackgroundRegistrySync(contentRootPath);
                    }
                    ImGui::EndPopup();
                }

                // Drop target on CBAssetListPane empty space (moves content into currentVirtualDir)
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG")) {
                        std::string payloadData((const char*)payload->Data);
                        std::stringstream pss(payloadData);
                        std::string item;
                        while (std::getline(pss, item, '\n')) {
                            if (item.empty()) continue;
                            if (item.rfind("DIR:", 0) == 0) {
                                MoveFolderToDirectory(item.substr(4), currentVirtualDir, contentRootPath);
                            } else if (item.rfind("ASSET:", 0) == 0) {
                                const AssetMetadata* m = AssetRegistry::Get().FindByPath(item.substr(6));
                                if (m) MoveAssetToDirectory(m->id, currentVirtualDir, contentRootPath);
                            }
                        }
                        TriggerBackgroundRegistrySync(contentRootPath);
                    } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                        std::string idStr((const char*)payload->Data);
                        MoveAssetToDirectory(AssetID::FromString(idStr), currentVirtualDir, contentRootPath);
                        TriggerBackgroundRegistrySync(contentRootPath);
                    }
                    ImGui::EndDragDropTarget();
                }

                // F2 Keyboard shortcut to rename currently selected item in Content Browser
                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F2)) {
                    if (!selectedContentItem.empty()) {
                        if (selectedContentItem.rfind("DIR:", 0) == 0) {
                            OpenRenameFolderModal(selectedContentItem.substr(4));
                        } else {
                            std::string pathQuery = selectedContentItem;
                            if (pathQuery.rfind("ASSET:", 0) == 0) pathQuery = pathQuery.substr(6);
                            const AssetMetadata* m = AssetRegistry::Get().FindByPath(pathQuery);
                            if (m) {
                                OpenRenameAssetModal(m->id, m->objectName, m->virtualPath);
                            }
                        }
                    }
                }

                ImGui::EndChild();
            

// Periodic real-time check for external file changes in contentRootPath
    static auto s_lastDiskCheck = std::chrono::steady_clock::now();
    static std::filesystem::file_time_type s_lastContentTime{};
    auto nowTime = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - s_lastDiskCheck).count() > 1500) {
        s_lastDiskCheck = nowTime;
        if (!contentRootPath.empty() && !s_bgSync.isRunning.load()) {
            std::error_code ec;
            auto curTime = std::filesystem::last_write_time(contentRootPath, ec);
            if (!ec) {
                if (s_lastContentTime != std::filesystem::file_time_type{} && curTime != s_lastContentTime) {
                    TriggerBackgroundRegistrySync(contentRootPath);
                }
                s_lastContentTime = curTime;
            }
        }
    }

    // Subtle Bottom-Right Background Sync Progress Indicator
    bool bgSyncActive = s_bgSync.isRunning.load();
    static auto s_lastActiveTime = std::chrono::steady_clock::now();
    if (bgSyncActive) {
        s_lastActiveTime = std::chrono::steady_clock::now();
    }
    auto timeSinceSync = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_lastActiveTime).count();
    bool showBgIndicator = bgSyncActive || (timeSinceSync < 1500);

    if (showBgIndicator) {
        ImVec2 badgeSize(220.0f, 24.0f);
        ImVec2 winPos = ImGui::GetWindowPos(); ImVec2 winSize = ImGui::GetWindowSize();
        ImVec2 badgePos(winPos.x + winSize.x - badgeSize.x - 14.0f, winPos.y + winSize.y - badgeSize.y - 7.0f);
        ImDrawList* fgList = ImGui::GetForegroundDrawList();
        fgList->AddRectFilled(badgePos, ImVec2(badgePos.x + badgeSize.x, badgePos.y + badgeSize.y), IM_COL32(16, 20, 28, 230), 4.0f);
        fgList->AddRect(badgePos, ImVec2(badgePos.x + badgeSize.x, badgePos.y + badgeSize.y), IM_COL32(40, 90, 160, 200), 4.0f);

        if (bgSyncActive) {
            float prog = s_bgSync.progress.load();
            float barW = (badgeSize.x - 20.0f) * std::max(0.05f, prog);
            fgList->AddRectFilled(ImVec2(badgePos.x + 10.0f, badgePos.y + badgeSize.y - 4.0f),
                                  ImVec2(badgePos.x + 10.0f + barW, badgePos.y + badgeSize.y - 2.0f),
                                  IM_COL32(40, 160, 240, 255), 1.0f);
            char buf[64];
            snprintf(buf, sizeof(buf), "Syncing... (%d%%)", (int)(prog * 100.0f));
            fgList->AddText(ImVec2(badgePos.x + 12.0f, badgePos.y + 3.0f), IM_COL32(180, 220, 255, 240), buf);
        } else {
            fgList->AddText(ImVec2(badgePos.x + 12.0f, badgePos.y + 3.0f), IM_COL32(90, 230, 130, 240), "Registry Synchronized");
        }
    }
}


