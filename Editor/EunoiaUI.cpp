#include "stdafx.h"
#include "EunoiaUI.h"
#include "Editor.h"
#include "ImGui/imgui.h"

using namespace wi::ecs;
using namespace wi::scene;

namespace EunoiaUI
{
	UIManager::UIManager() = default;

	void UIManager::Initialize(HWND window, Editor* mainEditor)
	{
		m_window = window;
		m_mainEditor = mainEditor;

		ImGuiBackend::Initialize(window);

		// If no active project at start, show project browser
		if (!m_projectBrowser.HasActiveProject())
		{
			m_showProjectBrowser = true;
		}

		m_projectBrowser.SetOnProjectLoadedCallback([this](const std::filesystem::path& path) {
			m_bottomDrawer.SetContentRoot(path / "Content");
			m_bottomDrawer.AddLog("Project", "Loaded project: " + path.string(), 3);
		});
	}

	void UIManager::StartPlayMode(EditorComponent& editor, Scene& scene)
	{
		if (m_isPlayMode) return;
		m_isPlayMode = true;

		// Backup scene state
		m_playModeSceneBackup.SetReadModeAndResetPos(false);
		scene.Serialize(m_playModeSceneBackup);

		m_bottomDrawer.AddLog("Play", "Simulation started.", 0);
	}

	void UIManager::StopPlayMode(EditorComponent& editor, Scene& scene)
	{
		if (!m_isPlayMode) return;
		m_isPlayMode = false;

		// Restore scene state
		if (m_playModeSceneBackup.IsOpen())
		{
			m_playModeSceneBackup.SetReadModeAndResetPos(true);
			scene.Clear();
			scene.Serialize(m_playModeSceneBackup);
			m_playModeSceneBackup.Close();
		}

		m_bottomDrawer.AddLog("Play", "Simulation stopped. State restored.", 0);
	}

	void UIManager::Update(EditorComponent& editor, Scene& scene)
	{
		ImGuiBackend::NewFrame();

		ImGuiViewport* vp = ImGui::GetMainViewport();
		float screenW = vp->Size.x;
		float screenH = vp->Size.y;

		// 1. Custom Title Bar & Top Menu Bar
		RenderCustomTitleBar(editor, scene);
		RenderTopMenuBar(editor, scene);

		// 2. Calculate panel dimensions
		float curBottomH = m_showBottomDrawer ? m_bottomDrawer.GetCurrentHeight() : 0.0f;
		float curLeftW = m_showOutliner ? m_leftSidebarWidth : 0.0f;
		float curRightW = m_showDetails ? m_rightSidebarWidth : 0.0f;
		float sidebarsY = vp->Pos.y + m_topBarHeight;
		float sidebarsH = screenH - m_topBarHeight - curBottomH;
		float bottomY = vp->Pos.y + screenH - curBottomH;

		// 3. Left Sidebar: Outliner
		if (m_showOutliner)
		{
			m_outlinerPanel.Render(editor, scene, curLeftW, sidebarsH, vp->Pos.x, sidebarsY);
		}

		// 4. Right Sidebar: Details
		if (m_showDetails)
		{
			m_detailsPanel.Render(editor, scene, curRightW, sidebarsH, vp->Pos.x + screenW - curRightW, sidebarsY);
		}

		// 5. Bottom Drawer: Content Browser & Output Log
		if (m_showBottomDrawer)
		{
			m_bottomDrawer.Render(editor, screenW, curBottomH, vp->Pos.x, bottomY);
		}

		// Interactive Layout Splitters (Outliner, Details, Bottom Drawer)
		RenderLayoutSplitters(screenW, screenH);

		// 6. Center Viewport calculation (relative to window client area)
		m_viewportRect.x = curLeftW;
		m_viewportRect.y = m_topBarHeight;
		m_viewportRect.width = screenW - curLeftW - curRightW;
		m_viewportRect.height = sidebarsH;
		if (m_viewportRect.width < 10.0f) m_viewportRect.width = 10.0f;
		if (m_viewportRect.height < 10.0f) m_viewportRect.height = 10.0f;

		// 7. Viewport Overlay & ImGuizmo (in absolute screen coordinates)
		if (m_showViewportOverlay)
		{
			m_viewportOverlay.Render(editor, scene, vp->Pos.x + m_viewportRect.x, vp->Pos.y + m_viewportRect.y, m_viewportRect.width, m_viewportRect.height);
		}

		// 8. Project Browser (if opened)
		if (m_showProjectBrowser)
		{
			m_projectBrowser.Render(&m_showProjectBrowser);
		}

		// 9. Graphics Settings Modal (if opened)
		if (m_showGraphicsSettings)
		{
			RenderGraphicsSettingsModal(editor);
		}

		// 10. Skybox Settings Modal (if opened)
		if (m_showSkyboxWindow)
		{
			RenderSkyboxWindow(editor, scene);
		}
	}

	void UIManager::RenderCustomTitleBar(EditorComponent& editor, Scene& scene)
	{
		ImGuiViewport* vp = ImGui::GetMainViewport();
		float titleBarH = 30.0f;
		float topBarW = vp->Size.x;

		ImGui::SetNextWindowPos(vp->Pos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(topBarW, titleBarH), ImGuiCond_Always);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
								 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
								 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.065f, 0.070f, 0.088f, 1.0f));

		if (ImGui::Begin("##CustomTitleBar", nullptr, flags))
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			ImGui::SetCursorPosY(5.0f);

			// 1. Engine Logo & Brand
			ImGui::TextColored(Colors::AccentBlue, "EUNOIA ENGINE");
			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();

			// 2. Project Name & Active Level
			std::string projName = m_projectBrowser.HasActiveProject() ? m_projectBrowser.GetActiveProjectName() : "Default Project";
			std::string lvlName = editor.GetCurrentEditorScene().path.empty() ? "Untitled" : std::filesystem::path(editor.GetCurrentEditorScene().path).stem().string();
			ImGui::Text("%s  —  [%s]", projName.c_str(), lvlName.c_str());

			if (m_isPlayMode)
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.20f, 0.88f, 0.40f, 1.0f), "● PLAY MODE");
			}

			// 3. Workspace Tab: Level Editor
			ImGui::SameLine(ImGui::GetCursorPosX() + 16.0f);
			ImGui::SetCursorPosY(4.0f);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.22f, 0.28f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.98f, 1.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 2.0f));
			ImGui::Button("🌐 Level Editor", ImVec2(0.0f, 22.0f));
			ImVec2 minPos = ImGui::GetItemRectMin();
			ImVec2 maxPos = ImGui::GetItemRectMax();
			dl->AddRectFilled(ImVec2(minPos.x, maxPos.y - 2.0f), ImVec2(maxPos.x, maxPos.y), IM_COL32(0, 122, 204, 255), 1.5f);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);

			// 4. Custom Window Control Buttons (Minimize, Maximize/Restore, Close)
			float btnW = 46.0f;
			float btnH = titleBarH;
			float btnAreaW = btnW * 3.0f;

			ImGui::SameLine(topBarW - btnAreaW);
			ImGui::SetCursorPosY(0.0f);

			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

			// Minimize button
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.22f, 0.28f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.17f, 0.22f, 1.0f));
			if (ImGui::Button(" — ##WinMin", ImVec2(btnW, btnH)))
			{
				if (m_window) ShowWindow(m_window, SW_MINIMIZE);
			}
			ImGui::PopStyleColor(2);

			// Maximize / Restore button
			ImGui::SameLine(0.0f, 0.0f);
			bool isMax = m_window ? (IsZoomed(m_window) != 0) : false;
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.22f, 0.28f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.17f, 0.22f, 1.0f));
			if (ImGui::Button(isMax ? " ❐ ##WinMax" : " □ ##WinMax", ImVec2(btnW, btnH)))
			{
				if (m_window)
				{
					if (isMax) ShowWindow(m_window, SW_RESTORE);
					else ShowWindow(m_window, SW_MAXIMIZE);
				}
			}
			ImGui::PopStyleColor(2);

			// Close button
			ImGui::SameLine(0.0f, 0.0f);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.15f, 0.15f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.70f, 0.10f, 0.10f, 1.0f));
			if (ImGui::Button(" ✕ ##WinClose", ImVec2(btnW, btnH)))
			{
				PostQuitMessage(0);
			}
			ImGui::PopStyleColor(2);

			ImGui::PopStyleColor();
			ImGui::PopStyleVar(2);

			// 5. Window Dragging & Double-Click Maximize
			bool isTitleBarHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
			bool anyItemActive = ImGui::IsAnyItemActive() || ImGui::IsAnyItemHovered();

			if (isTitleBarHovered && !anyItemActive)
			{
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					if (m_window)
					{
						if (isMax) ShowWindow(m_window, SW_RESTORE);
						else ShowWindow(m_window, SW_MAXIMIZE);
					}
				}
				else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					if (m_window)
					{
						ReleaseCapture();
						SendMessage(m_window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
						ImGui::GetIO().MouseDown[0] = false;
					}
				}
			}

			// Subtle bottom border line under title bar
			dl->AddLine(ImVec2(vp->Pos.x, vp->Pos.y + titleBarH),
						ImVec2(vp->Pos.x + topBarW, vp->Pos.y + titleBarH),
						IM_COL32(35, 38, 48, 255), 1.0f);
		}
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(3);
	}

	void UIManager::RenderTopMenuBar(EditorComponent& editor, Scene& scene)
	{
		ImGuiViewport* vp = ImGui::GetMainViewport();

		ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + 30.0f), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(vp->Size.x, 34.0f), ImGuiCond_Always);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize |
								 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar |
								 ImGuiWindowFlags_NoSavedSettings;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 2.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

		if (ImGui::Begin("##TopMenuBarWindow", nullptr, flags))
		{
			// Menu Bar Row
			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu("File"))
				{
					if (ImGui::MenuItem("New Level", "Ctrl+N"))
					{
						editor.NewScene();
					}
					if (ImGui::MenuItem("Open Level...", "Ctrl+O"))
					{
						std::string file = ProjectBrowser::ShowOpenFileDialog("Eunoia Level (*.eulevel;*.wiscene)\0*.eulevel;*.wiscene\0All Files (*.*)\0*.*\0");
						if (!file.empty())
						{
							editor.Open(file);
							m_bottomDrawer.AddLog("Level", "Opened Level: " + file, 3);
						}
					}
					if (ImGui::MenuItem("Save Level", "Ctrl+S"))
					{
						if (editor.GetCurrentEditorScene().path.empty())
						{
							editor.SaveAs();
						}
						else
						{
							editor.Save(editor.GetCurrentEditorScene().path);
							m_bottomDrawer.AddLog("Level", "Saved Level: " + editor.GetCurrentEditorScene().path, 3);
						}
					}
					if (ImGui::MenuItem("Save Level As...", "Ctrl+Shift+S"))
					{
						editor.SaveAs();
					}
					ImGui::Separator();
					if (ImGui::MenuItem("Reset Default Level"))
					{
						editor.NewScene();
					}
					ImGui::Separator();
					if (ImGui::MenuItem("Project Browser...", "Ctrl+Shift+P"))
					{
						m_showProjectBrowser = true;
					}
					ImGui::Separator();
					if (ImGui::MenuItem("Exit", "Alt+F4"))
					{
						PostQuitMessage(0);
					}
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Edit"))
				{
					if (ImGui::MenuItem("Undo", "Ctrl+Z"))
					{
						editor.ConsumeHistoryOperation(true);
					}
					if (ImGui::MenuItem("Redo", "Ctrl+Y"))
					{
						editor.ConsumeHistoryOperation(false);
					}
					ImGui::Separator();
					if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
					{
						if (!editor.translator.selected.empty())
						{
							Entity dup = scene.Entity_Duplicate(editor.translator.selected[0].entity);
							editor.ClearSelected();
							editor.AddSelected(dup);
						}
					}
					if (ImGui::MenuItem("Delete", "Del"))
					{
						if (!editor.translator.selected.empty())
						{
							scene.Entity_Remove(editor.translator.selected[0].entity);
							editor.ClearSelected();
						}
					}
					if (ImGui::MenuItem("Deselect All", "Esc"))
					{
						editor.ClearSelected();
					}
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Actor"))
				{
					OutlinerPanel::DrawAddActorMenuItems(editor, scene);
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Graphics"))
				{
					if (ImGui::MenuItem("Settings..."))
					{
						m_showGraphicsSettings = true;
					}
					ImGui::Separator();
					bool vsync = editor.main ? editor.main->swapChain.desc.vsync : false;
					if (ImGui::MenuItem("V-Sync", nullptr, &vsync))
					{
						wi::eventhandler::SetVSync(vsync);
					}
					if (editor.renderPath)
					{
						bool ssao = editor.renderPath->getAO() != wi::RenderPath3D::AO_DISABLED;
						if (ImGui::MenuItem("Ambient Occlusion", nullptr, &ssao))
						{
							editor.renderPath->setAO(ssao ? wi::RenderPath3D::AO_MSAO : wi::RenderPath3D::AO_DISABLED);
						}
						bool ssr = editor.renderPath->getSSREnabled();
						if (ImGui::MenuItem("SSR (Screen Space Reflections)", nullptr, &ssr))
						{
							editor.renderPath->setSSREnabled(ssr);
						}
						bool bloom = editor.renderPath->getBloomEnabled();
						if (ImGui::MenuItem("Bloom", nullptr, &bloom))
						{
							editor.renderPath->setBloomEnabled(bloom);
						}
						bool motionBlur = editor.renderPath->getMotionBlurEnabled();
						if (ImGui::MenuItem("Motion Blur", nullptr, &motionBlur))
						{
							editor.renderPath->setMotionBlurEnabled(motionBlur);
						}
						bool dof = editor.renderPath->getDepthOfFieldEnabled();
						if (ImGui::MenuItem("Depth of Field", nullptr, &dof))
						{
							editor.renderPath->setDepthOfFieldEnabled(dof);
						}
					}
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Window"))
				{
					ImGui::MenuItem("Outliner", nullptr, &m_showOutliner);
					ImGui::MenuItem("Details", nullptr, &m_showDetails);
					ImGui::MenuItem("Content Browser / Logs", nullptr, &m_showBottomDrawer);
					ImGui::MenuItem("Viewport Overlay", nullptr, &m_showViewportOverlay);
					ImGui::MenuItem("Skybox Settings", nullptr, &m_showSkyboxWindow);
					ImGui::MenuItem("Graphics Settings", nullptr, &m_showGraphicsSettings);
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("World"))
				{
					if (ImGui::MenuItem("Skybox Settings...", "Ctrl+Shift+B"))
					{
						m_showSkyboxWindow = true;
					}
					bool isSkyActive = scene.weather.skyMap.IsValid() || scene.weather.IsRealisticSky() ||
						(scene.weather.horizon.x > 0.001f || scene.weather.horizon.y > 0.001f || scene.weather.horizon.z > 0.001f ||
						 scene.weather.zenith.x > 0.001f || scene.weather.zenith.y > 0.001f || scene.weather.zenith.z > 0.001f);
					if (ImGui::MenuItem(isSkyActive ? "Disable Skybox (Pitch Black)" : "Enable Default Skybox"))
					{
						if (isSkyActive)
						{
							scene.weather.skyMap = {};
							scene.weather.skyMapName.clear();
							scene.weather.SetRealisticSky(false);
							scene.weather.SetVolumetricClouds(false);
							scene.weather.horizon = XMFLOAT3(0, 0, 0);
							scene.weather.zenith = XMFLOAT3(0, 0, 0);
							scene.weather.ambient = XMFLOAT3(0, 0, 0);
							scene.weather.fogDensity = 0.0f;
							if (scene.weathers.GetCount() > 0) scene.weathers[0] = scene.weather;
							for (size_t i = 0; i < scene.probes.GetCount(); ++i) scene.probes[i].SetDirty();
							scene.global_dynamic_probe.SetDirty();
							m_bottomDrawer.AddLog("World", "Skybox disabled (Dark World)", 3);
						}
						else
						{
							scene.weather.SetRealisticSky(true);
							scene.weather.horizon = XMFLOAT3(0.4f, 0.4f, 0.4f);
							scene.weather.zenith = XMFLOAT3(0.1f, 0.2f, 0.4f);
							scene.weather.ambient = XMFLOAT3(0.1f, 0.1f, 0.1f);
							if (scene.weathers.GetCount() > 0) scene.weathers[0] = scene.weather;
							for (size_t i = 0; i < scene.probes.GetCount(); ++i) scene.probes[i].SetDirty();
							scene.global_dynamic_probe.SetDirty();
							m_bottomDrawer.AddLog("World", "Skybox enabled (Realistic Sky)", 3);
						}
					}
					ImGui::Separator();
					if (ImGui::MenuItem("Reset to Default Level"))
					{
						editor.NewScene();
					}
					if (ImGui::MenuItem("Clear World"))
					{
						scene.Clear();
						editor.ClearSelected();
					}
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("Help"))
				{
					if (ImGui::MenuItem("About Eunoia Engine"))
					{
						m_bottomDrawer.AddLog("Help", "Eunoia Engine v2.0 - Powered by EunoiaEngine.", 3);
					}
					ImGui::EndMenu();
				}

				// Center Play / Stop Simulation Buttons inside MenuBar
				float centerOffset = (vp->Size.x * 0.5f) - 45.0f;
				if (centerOffset > 380.0f)
				{
					ImGui::SameLine(centerOffset);
					if (!m_isPlayMode)
					{
						ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.55f, 0.25f, 1.0f));
						if (ImGui::Button(" ▶ Play ", ImVec2(80.0f, 22.0f)))
						{
							StartPlayMode(editor, scene);
						}
						ImGui::PopStyleColor();
					}
					else
					{
						ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.20f, 0.20f, 1.0f));
						if (ImGui::Button(" ⏹ Stop ", ImVec2(80.0f, 22.0f)))
						{
							StopPlayMode(editor, scene);
						}
						ImGui::PopStyleColor();
					}
				}

				// Right side engine info
				float rightWidth = 220.0f;
				if (ImGui::GetContentRegionAvail().x > rightWidth)
				{
					ImGui::SameLine(ImGui::GetWindowWidth() - rightWidth);
					ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.00f, 1.00f), "DirectX 12 | EunoiaEngine");
				}

				ImGui::EndMenuBar();
			}
		}
		ImGui::End();

		ImGui::PopStyleVar(3);
	}

	void UIManager::RenderLayoutSplitters(float screenW, float screenH)
	{
		ImGuiViewport* vp = ImGui::GetMainViewport();
		if (!vp || vp->Size.x <= 10.0f || vp->Size.y <= 10.0f) return;

		float curBottomH = m_showBottomDrawer ? m_bottomDrawer.GetCurrentHeight() : 0.0f;
		float sidebarsY = vp->Pos.y + m_topBarHeight;
		float sidebarsH = screenH - m_topBarHeight - curBottomH;
		float bottomY = vp->Pos.y + screenH - curBottomH;

		ImVec2 mousePos = ImGui::GetIO().MousePos;
		bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
		ImDrawList* drawList = ImGui::GetForegroundDrawList();

		static bool s_dragLeft = false;
		static bool s_dragRight = false;
		static bool s_dragBottom = false;

		// 1. Left Sidebar Splitter
		if (m_showOutliner)
		{
			float splitX = vp->Pos.x + m_leftSidebarWidth;
			bool inLeftHit = (mousePos.x >= splitX - 5.0f && mousePos.x <= splitX + 5.0f &&
							  mousePos.y >= sidebarsY && mousePos.y <= sidebarsY + sidebarsH);

			if (!s_dragRight && !s_dragBottom)
			{
				if (inLeftHit && mouseClicked) s_dragLeft = true;
			}

			if (s_dragLeft)
			{
				if (mouseDown)
				{
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
					float delta = ImGui::GetIO().MouseDelta.x;
					if (delta != 0.0f)
					{
						float maxW = screenW - m_rightSidebarWidth - 250.0f;
						m_leftSidebarWidth = std::clamp(m_leftSidebarWidth + delta, 180.0f, std::max(200.0f, maxW));
					}
					drawList->AddLine(ImVec2(splitX, sidebarsY), ImVec2(splitX, sidebarsY + sidebarsH), IM_COL32(0, 122, 204, 255), 3.0f);
				}
				else
				{
					s_dragLeft = false;
				}
			}
			else if (inLeftHit && !s_dragRight && !s_dragBottom)
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
				drawList->AddLine(ImVec2(splitX, sidebarsY), ImVec2(splitX, sidebarsY + sidebarsH), IM_COL32(0, 122, 204, 200), 2.0f);
			}
		}

		// 2. Right Sidebar Splitter
		if (m_showDetails)
		{
			float splitX = vp->Pos.x + screenW - m_rightSidebarWidth;
			bool inRightHit = (mousePos.x >= splitX - 5.0f && mousePos.x <= splitX + 5.0f &&
							   mousePos.y >= sidebarsY && mousePos.y <= sidebarsY + sidebarsH);

			if (!s_dragLeft && !s_dragBottom)
			{
				if (inRightHit && mouseClicked) s_dragRight = true;
			}

			if (s_dragRight)
			{
				if (mouseDown)
				{
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
					float delta = ImGui::GetIO().MouseDelta.x;
					if (delta != 0.0f)
					{
						float maxW = screenW - m_leftSidebarWidth - 250.0f;
						m_rightSidebarWidth = std::clamp(m_rightSidebarWidth - delta, 200.0f, std::max(220.0f, maxW));
					}
					drawList->AddLine(ImVec2(splitX, sidebarsY), ImVec2(splitX, sidebarsY + sidebarsH), IM_COL32(0, 122, 204, 255), 3.0f);
				}
				else
				{
					s_dragRight = false;
				}
			}
			else if (inRightHit && !s_dragLeft && !s_dragBottom)
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
				drawList->AddLine(ImVec2(splitX, sidebarsY), ImVec2(splitX, sidebarsY + sidebarsH), IM_COL32(0, 122, 204, 200), 2.0f);
			}
		}

		// 3. Bottom Drawer Splitter
		if (m_showBottomDrawer && m_bottomDrawer.IsOpen())
		{
			float splitY = bottomY;
			float startX = vp->Pos.x;
			float endX = vp->Pos.x + screenW;
			bool inBottomHit = (mousePos.y >= splitY - 5.0f && mousePos.y <= splitY + 5.0f &&
								mousePos.x >= startX && mousePos.x <= endX);

			if (!s_dragLeft && !s_dragRight)
			{
				if (inBottomHit && mouseClicked) s_dragBottom = true;
			}

			if (s_dragBottom)
			{
				if (mouseDown)
				{
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
					float delta = ImGui::GetIO().MouseDelta.y;
					if (delta != 0.0f)
					{
						float maxH = screenH - m_topBarHeight - 120.0f;
						float newH = std::clamp(m_bottomDrawer.GetExpandedHeight() - delta, 100.0f, std::max(150.0f, maxH));
						m_bottomDrawer.SetExpandedHeight(newH);
					}
					drawList->AddLine(ImVec2(startX, splitY), ImVec2(endX, splitY), IM_COL32(0, 122, 204, 255), 3.0f);
				}
				else
				{
					s_dragBottom = false;
				}
			}
			else if (inBottomHit && !s_dragLeft && !s_dragRight)
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
				drawList->AddLine(ImVec2(startX, splitY), ImVec2(endX, splitY), IM_COL32(0, 122, 204, 200), 2.0f);
			}
		}
	}

	void UIManager::Render(EditorComponent& editor, Scene& scene, wi::graphics::CommandList cmd)
	{
		ImGui::Render();
		ImGuiBackend::RenderDrawData(ImGui::GetDrawData(), cmd);
	}

	void UIManager::RenderGraphicsSettingsModal(EditorComponent& editor)
	{
		ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Graphics & Rendering Settings###EunoiaGraphicsSettingsModal", &m_showGraphicsSettings))
		{
			if (ImGui::CollapsingHeader("Display & Window", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool vsync = editor.main ? editor.main->swapChain.desc.vsync : false;
				if (ImGui::Checkbox("V-Sync", &vsync))
				{
					wi::eventhandler::SetVSync(vsync);
				}

				if (editor.renderPath)
				{
					float resScale = editor.renderPath->resolutionScale;
					if (ImGui::SliderFloat("Resolution Scale", &resScale, 0.25f, 2.0f, "%.2f"))
					{
						editor.renderPath->resolutionScale = resScale;
						editor.renderPath->ResizeBuffers();
					}
				}
			}

			if (editor.renderPath)
			{
				auto rp = editor.renderPath.get();

				if (ImGui::CollapsingHeader("Anti-Aliasing", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bool fxaa = rp->getFXAAEnabled();
					if (ImGui::Checkbox("FXAA", &fxaa))
					{
						rp->setFXAAEnabled(fxaa);
					}

					bool fsr2 = rp->getFSR2Enabled();
					if (ImGui::Checkbox("FSR 2 (Temporal Super Resolution)", &fsr2))
					{
						rp->setFSR2Enabled(fsr2);
					}

					int msaa = (int)rp->getMSAASampleCount();
					const char* msaaOptions[] = { "1x (Off)", "2x", "4x", "8x" };
					int msaaIdx = 0;
					if (msaa == 2) msaaIdx = 1;
					else if (msaa == 4) msaaIdx = 2;
					else if (msaa == 8) msaaIdx = 3;

					if (ImGui::Combo("MSAA Sample Count", &msaaIdx, msaaOptions, 4))
					{
						uint32_t counts[] = { 1, 2, 4, 8 };
						rp->setMSAASampleCount(counts[msaaIdx]);
					}
				}

				if (ImGui::CollapsingHeader("Lighting & Ambient Occlusion", ImGuiTreeNodeFlags_DefaultOpen))
				{
					int aoMode = (int)rp->getAO();
					const char* aoModes[] = { "Disabled", "SSAO", "HBAO", "MSAO", "RTAO (Raytraced)" };
					if (ImGui::Combo("Ambient Occlusion", &aoMode, aoModes, 5))
					{
						rp->setAO((wi::RenderPath3D::AO)aoMode);
					}

					bool shadows = rp->getShadowsEnabled();
					if (ImGui::Checkbox("Cast Dynamic Shadows", &shadows))
					{
						rp->setShadowsEnabled(shadows);
					}

					bool volLights = rp->getVolumeLightsEnabled();
					if (ImGui::Checkbox("Volumetric Fog & Lights", &volLights))
					{
						rp->setVolumeLightsEnabled(volLights);
					}

					bool shafts = rp->getLightShaftsEnabled();
					if (ImGui::Checkbox("Light Shafts / God Rays", &shafts))
					{
						rp->setLightShaftsEnabled(shafts);
					}
				}

				if (ImGui::CollapsingHeader("Reflections & Ray Tracing", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bool ssr = rp->getSSREnabled();
					if (ImGui::Checkbox("Screen Space Reflections (SSR)", &ssr))
					{
						rp->setSSREnabled(ssr);
					}

					bool ssgi = rp->getSSGIEnabled();
					if (ImGui::Checkbox("Screen Space Global Illumination (SSGI)", &ssgi))
					{
						rp->setSSGIEnabled(ssgi);
					}

					bool rtReflections = rp->getRaytracedReflectionEnabled();
					if (ImGui::Checkbox("Hardware Raytraced Reflections", &rtReflections))
					{
						rp->setRaytracedReflectionsEnabled(rtReflections);
					}

					bool rtDiffuse = rp->getRaytracedDiffuseEnabled();
					if (ImGui::Checkbox("Hardware Raytraced Diffuse GI", &rtDiffuse))
					{
						rp->setRaytracedDiffuseEnabled(rtDiffuse);
					}
				}

				if (ImGui::CollapsingHeader("Post Processing", ImGuiTreeNodeFlags_DefaultOpen))
				{
					bool bloom = rp->getBloomEnabled();
					if (ImGui::Checkbox("Bloom", &bloom))
					{
						rp->setBloomEnabled(bloom);
					}
					if (bloom)
					{
						float thresh = rp->getBloomThreshold();
						if (ImGui::SliderFloat("Bloom Threshold", &thresh, 0.0f, 10.0f))
						{
							rp->setBloomThreshold(thresh);
						}
					}

					bool motionBlur = rp->getMotionBlurEnabled();
					if (ImGui::Checkbox("Motion Blur", &motionBlur))
					{
						rp->setMotionBlurEnabled(motionBlur);
					}
					if (motionBlur)
					{
						float mbStrength = rp->getMotionBlurStrength();
						if (ImGui::SliderFloat("Blur Strength", &mbStrength, 0.0f, 100.0f))
						{
							rp->setMotionBlurStrength(mbStrength);
						}
					}

					bool dof = rp->getDepthOfFieldEnabled();
					if (ImGui::Checkbox("Depth of Field", &dof))
					{
						rp->setDepthOfFieldEnabled(dof);
					}
					if (dof)
					{
						float dofStrength = rp->getDepthOfFieldStrength();
						if (ImGui::SliderFloat("DoF Strength", &dofStrength, 0.0f, 100.0f))
						{
							rp->setDepthOfFieldStrength(dofStrength);
						}
					}

					bool eyeAdapt = rp->getEyeAdaptionEnabled();
					if (ImGui::Checkbox("Eye Adaptation (Auto Exposure)", &eyeAdapt))
					{
						rp->setEyeAdaptionEnabled(eyeAdapt);
					}

					bool sharpen = rp->getSharpenFilterEnabled();
					if (ImGui::Checkbox("Sharpen Filter", &sharpen))
					{
						rp->setSharpenFilterEnabled(sharpen);
					}
					if (sharpen)
					{
						float sharpenAmt = rp->getSharpenFilterAmount();
						if (ImGui::SliderFloat("Sharpen Amount", &sharpenAmt, 0.0f, 1.0f))
						{
							rp->setSharpenFilterAmount(sharpenAmt);
						}
					}

					bool colorGrading = rp->getColorGradingEnabled();
					if (ImGui::Checkbox("Color Grading", &colorGrading))
					{
						rp->setColorGradingEnabled(colorGrading);
					}
				}
			}

			ImGui::Separator();
			if (ImGui::Button("Close", ImVec2(100, 28)))
			{
				m_showGraphicsSettings = false;
			}
		}
		ImGui::End();
	}

	void UIManager::RenderSkyboxWindow(EditorComponent& editor, wi::scene::Scene& scene)
	{
		ImGui::SetNextWindowSize(ImVec2(580, 660), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("🌌 World Skybox Settings###WorldSkyboxSettingsWin", &m_showSkyboxWindow, ImGuiWindowFlags_NoCollapse))
		{
			ImGui::End();
			return;
		}

		WeatherComponent& weather = (scene.weathers.GetCount() > 0) ? scene.weathers[0] : scene.weather;

		auto SyncWeather = [&]() {
			if (scene.weathers.GetCount() > 0)
			{
				scene.weathers[0] = weather;
			}
			scene.weather = weather;
			for (size_t i = 0; i < scene.probes.GetCount(); ++i)
			{
				scene.probes[i].SetDirty();
			}
			scene.global_dynamic_probe.SetDirty();
		};

		bool isTextureActive = weather.skyMap.IsValid();
		bool isRealisticActive = weather.IsRealisticSky();
		bool hasColors = (weather.horizon.x > 0.001f || weather.horizon.y > 0.001f || weather.horizon.z > 0.001f ||
						  weather.zenith.x > 0.001f || weather.zenith.y > 0.001f || weather.zenith.z > 0.001f);
		bool isSkyboxDisabled = !isTextureActive && !isRealisticActive && !hasColors;

		// -----------------------------------------------------------------------------
		// 1. OLD EUNOIA ENGINE HEADER & MASTER ENABLE
		// -----------------------------------------------------------------------------
		ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "World Environment Skybox Configuration");
		ImGui::TextWrapped("Configure the active world skybox background and ambient scene illumination for the level.");
		ImGui::Separator();

		bool enableSkybox = !isSkyboxDisabled;
		if (ImGui::Checkbox("Enable World Skybox", &enableSkybox))
		{
			if (!enableSkybox)
			{
				weather.skyMap = {};
				weather.skyMapName.clear();
				weather.SetRealisticSky(false);
				weather.SetVolumetricClouds(false);
				weather.horizon = XMFLOAT3(0, 0, 0);
				weather.zenith = XMFLOAT3(0, 0, 0);
				weather.ambient = XMFLOAT3(0, 0, 0);
				weather.fogDensity = 0.0f;
				SyncWeather();
				m_bottomDrawer.AddLog("World", "Skybox disabled (Dark World)", 3);
			}
			else
			{
				weather.SetRealisticSky(true);
				weather.ambient = XMFLOAT3(33.0f / 255.0f, 47.0f / 255.0f, 127.0f / 255.0f);
				weather.horizon = XMFLOAT3(101.0f / 255.0f, 101.0f / 255.0f, 227.0f / 255.0f);
				weather.zenith = XMFLOAT3(99.0f / 255.0f, 133.0f / 255.0f, 255.0f / 255.0f);
				weather.fogStart = 100.0f;
				weather.fogDensity = 0.0025f;
				SyncWeather();
				m_bottomDrawer.AddLog("World", "Skybox enabled (Daytime Atmosphere)", 3);
			}
		}

		ImGui::SameLine();
		ImGui::TextDisabled("| Status:");
		ImGui::SameLine();
		if (isTextureActive)
		{
			std::string texName = weather.skyMapName.empty() ? "Custom Texture" : wi::helper::GetFileNameFromPath(weather.skyMapName);
			ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.5f, 1.0f), "🌌 HDR Active (%s)", texName.c_str());
		}
		else if (isRealisticActive)
		{
			ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "☀️ Realistic Sky");
		}
		else if (hasColors)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "🎨 Gradient Sky");
		}
		else
		{
			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "⚫ Pitch Black");
		}

		ImGui::Spacing();

		// Quick Switch Action Buttons
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
		if (isSkyboxDisabled)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.28f, 1.0f));
			ImGui::Button("⚫ Skybox Disabled", ImVec2(145, 26));
			ImGui::PopStyleColor();
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.18f, 0.18f, 1.0f));
			if (ImGui::Button("⚫ Disable Skybox", ImVec2(145, 26)))
			{
				weather.skyMap = {};
				weather.skyMapName.clear();
				weather.SetRealisticSky(false);
				weather.SetVolumetricClouds(false);
				weather.horizon = XMFLOAT3(0, 0, 0);
				weather.zenith = XMFLOAT3(0, 0, 0);
				weather.ambient = XMFLOAT3(0, 0, 0);
				weather.fogDensity = 0.0f;
				SyncWeather();
				m_bottomDrawer.AddLog("World", "Skybox disabled (Dark World)", 3);
			}
			ImGui::PopStyleColor();
		}

		ImGui::SameLine();
		if (ImGui::Button("☀️ Realistic Sky", ImVec2(125, 26)))
		{
			weather.skyMap = {};
			weather.skyMapName.clear();
			weather.SetRealisticSky(true);
			weather.horizon = XMFLOAT3(0.4f, 0.4f, 0.4f);
			weather.zenith = XMFLOAT3(0.1f, 0.2f, 0.4f);
			weather.ambient = XMFLOAT3(0.1f, 0.1f, 0.1f);
			SyncWeather();
			m_bottomDrawer.AddLog("World", "Skybox switched to Realistic Sky", 3);
		}

		ImGui::SameLine();
		if (ImGui::Button("🎨 Gradient Sky", ImVec2(125, 26)))
		{
			weather.skyMap = {};
			weather.skyMapName.clear();
			weather.SetRealisticSky(false);
			weather.horizon = XMFLOAT3(101.0f / 255.0f, 101.0f / 255.0f, 227.0f / 255.0f);
			weather.zenith = XMFLOAT3(99.0f / 255.0f, 133.0f / 255.0f, 255.0f / 255.0f);
			weather.ambient = XMFLOAT3(33.0f / 255.0f, 47.0f / 255.0f, 127.0f / 255.0f);
			SyncWeather();
			m_bottomDrawer.AddLog("World", "Skybox switched to Gradient Sky", 3);
		}
		ImGui::PopStyleVar();

		ImGui::Separator();

		// -----------------------------------------------------------------------------
		// 2. SETTINGS TABS (MATCHING OLD EUNOIA ENGINE & WICKED ENGINE)
		// -----------------------------------------------------------------------------
		if (ImGui::BeginTabBar("##SkyboxTabs"))
		{
			// TAB 1: ACTIVE SKYBOX ASSET (.eskybox)
			if (ImGui::BeginTabItem("📁 Skybox Asset"))
			{
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "Active Skybox Asset (.eskybox)");

				// Scan project content directory for .eskybox files
				std::vector<std::string> skyboxFiles;
				std::error_code ec;
				for (const auto& entry : std::filesystem::recursive_directory_iterator(m_bottomDrawer.GetContentRoot(), ec))
				{
					if (!entry.is_directory(ec))
					{
						std::string ext = wi::helper::toUpper(entry.path().extension().string());
						if (ext == ".ESKYBOX" || ext == ".SKYBOX")
						{
							skyboxFiles.push_back(entry.path().string());
						}
					}
				}

				static std::string activeAssetPath = "";
				std::string comboPreview = activeAssetPath.empty() ? "(Direct Custom Settings)" : wi::helper::GetFileNameFromPath(activeAssetPath);

				ImGui::SetNextItemWidth(300.0f);
				if (ImGui::BeginCombo("Skybox Asset", comboPreview.c_str()))
				{
					if (ImGui::Selectable("(Direct Custom Settings)", activeAssetPath.empty()))
					{
						activeAssetPath = "";
					}
					ImGui::Separator();
					for (const auto& sf : skyboxFiles)
					{
						std::string fName = wi::helper::GetFileNameFromPath(sf);
						bool isSel = (activeAssetPath == sf);
						if (ImGui::Selectable(fName.c_str(), isSel))
						{
							activeAssetPath = sf;

							// Load .eskybox file
							std::ifstream file(sf);
							if (file.is_open())
							{
								std::string line;
								while (std::getline(file, line))
								{
									if (line.empty() || line[0] == '#') continue;
									auto colon = line.find(':');
									if (colon == std::string::npos) continue;
									std::string key = line.substr(0, colon);
									std::string val = line.substr(colon + 1);
									while (!key.empty() && isspace((unsigned char)key.back())) key.pop_back();
									while (!key.empty() && isspace((unsigned char)key.front())) key.erase(key.begin());
									while (!val.empty() && isspace((unsigned char)val.back())) val.pop_back();
									while (!val.empty() && isspace((unsigned char)val.front())) val.erase(val.begin());

									if (key == "hdrTexture" && !val.empty())
									{
										weather.skyMapName = val;
										weather.skyMap = wi::resourcemanager::Load(val);
									}
									else if (key == "exposure")
									{
										try { weather.skyExposure = std::stof(val); } catch (...) {}
									}
									else if (key == "rotation")
									{
										try { weather.sky_rotation = wi::math::DegreesToRadians(std::stof(val)); } catch (...) {}
									}
									else if (key == "tint")
									{
										std::stringstream ss(val);
										ss >> weather.horizon.x >> weather.horizon.y >> weather.horizon.z;
									}
									else if (key == "enableSkybox")
									{
										if (val == "0" || val == "false" || val == "False")
										{
											weather.skyMap = {};
											weather.skyMapName.clear();
											weather.SetRealisticSky(false);
											weather.horizon = XMFLOAT3(0, 0, 0);
											weather.zenith = XMFLOAT3(0, 0, 0);
											weather.ambient = XMFLOAT3(0, 0, 0);
											weather.fogDensity = 0;
										}
									}
								}
								SyncWeather();
								m_bottomDrawer.AddLog("World", "Loaded Skybox Asset: " + fName, 3);
							}
						}
					}
					ImGui::EndCombo();
				}

				ImGui::Spacing();
				if (ImGui::Button("💾 Save Current Skybox (.eskybox)", ImVec2(240, 26)))
				{
					std::string filter = "Skybox Asset (*.eskybox)\0*.eskybox\0All Files (*.*)\0*.*\0";
					std::string savePath = ProjectBrowser::ShowSaveFileDialog(filter.c_str(), "Skybox_New.eskybox");
					if (!savePath.empty())
					{
						if (savePath.find(".eskybox") == std::string::npos) savePath += ".eskybox";

						std::ofstream file(savePath);
						if (file.is_open())
						{
							file << "# Eunoia-Editor Skybox Asset\n";
							file << "name: " << wi::helper::GetFileNameFromPath(savePath) << "\n";
							file << "hdrTexture: " << weather.skyMapName << "\n";
							file << "exposure: " << weather.skyExposure << "\n";
							file << "rotation: " << wi::math::RadiansToDegrees(weather.sky_rotation) << "\n";
							file << "tint: " << weather.horizon.x << " " << weather.horizon.y << " " << weather.horizon.z << "\n";
							file << "ambientContribution: 0.5\n";
							file << "enableSkybox: " << (!isSkyboxDisabled ? 1 : 0) << "\n";
							file << "enableSkyboxLighting: 0\n";
							file << "skyboxLightingIntensity: 1.0\n";
							file << "skyboxLightingColor: 1.0 1.0 1.0\n";
							file << "skyboxLightingSpecular: 1.0\n";
							file << "skyboxLightingDiffuse: 1.0\n";
							file << "realisticSky: " << (weather.IsRealisticSky() ? 1 : 0) << "\n";
							file << "volumetricClouds: " << (weather.IsVolumetricClouds() ? 1 : 0) << "\n";
							file << "fogDensity: " << weather.fogDensity << "\n";
							file.close();

							activeAssetPath = savePath;
							m_bottomDrawer.AddLog("World", "Saved Skybox Asset: " + savePath, 3);
						}
					}
				}

				ImGui::EndTabItem();
			}

			// TAB 2: HDR / TEXTURE & ILLUMINATION
			if (ImGui::BeginTabItem("🌌 HDR Texture & Illumination"))
			{
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "Equirectangular HDR / Texture Illumination");

				std::string texName = weather.skyMapName.empty() ? "(No texture loaded - using procedural / gradient sky)" : wi::helper::GetFileNameFromPath(weather.skyMapName);
				ImGui::Text("Active Texture: %s", texName.c_str());

				// Drag Drop Target Box
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.16f, 0.20f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.28f, 0.32f, 0.40f, 1.0f));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
				ImGui::Button("📥 Drag & Drop HDR / Texture File Here from Content Browser", ImVec2(ImGui::GetContentRegionAvail().x, 42.0f));
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_ASSET_PATH"))
					{
						std::string droppedPath = (const char*)payload->Data;
						std::string ext = wi::helper::toUpper(wi::helper::GetExtensionFromFileName(droppedPath));
						if (ext == "HDR" || ext == "PNG" || ext == "JPG" || ext == "JPEG" || ext == "DDS" || ext == "TGA" || ext == "BMP" || ext == "ESKYBOX")
						{
							weather.skyMapName = droppedPath;
							weather.skyMap = wi::resourcemanager::Load(droppedPath);
							SyncWeather();
							m_bottomDrawer.AddLog("World", "Loaded Skybox Texture: " + droppedPath, 3);
						}
					}
					ImGui::EndDragDropTarget();
				}
				ImGui::PopStyleVar();
				ImGui::PopStyleColor(2);

				ImGui::Spacing();
				if (ImGui::Button("📂 Load HDR / Sky Texture...", ImVec2(200, 26)))
				{
					std::string filter = "Skybox Image (*.hdr;*.png;*.jpg;*.dds;*.tga;*.bmp;*.eskybox)\0*.hdr;*.png;*.jpg;*.jpeg;*.dds;*.tga;*.bmp;*.eskybox\0All Files (*.*)\0*.*\0";
					std::string file = ProjectBrowser::ShowOpenFileDialog(filter.c_str());
					if (!file.empty())
					{
						weather.skyMapName = file;
						weather.skyMap = wi::resourcemanager::Load(file);
						SyncWeather();
						m_bottomDrawer.AddLog("World", "Loaded Skybox Texture: " + file, 3);
					}
				}
				ImGui::SameLine();
				if (weather.skyMap.IsValid())
				{
					if (ImGui::Button("🗑 Clear Texture", ImVec2(130, 26)))
					{
						weather.skyMap = {};
						weather.skyMapName.clear();
						SyncWeather();
						m_bottomDrawer.AddLog("World", "Cleared Skybox Texture", 3);
					}
				}

				ImGui::Separator();
				ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Illumination Parameters");

				if (ImGui::SliderFloat("Exposure / Intensity", &weather.skyExposure, 0.05f, 10.0f, "%.2f"))
				{
					SyncWeather();
				}

				float skyRotDeg = wi::math::RadiansToDegrees(weather.sky_rotation);
				if (ImGui::SliderFloat("Rotation (Azimuth)", &skyRotDeg, 0.0f, 360.0f, "%.1f°"))
				{
					weather.sky_rotation = wi::math::DegreesToRadians(skyRotDeg);
					SyncWeather();
				}

				if (ImGui::ColorEdit3("Sky Tint", &weather.horizon.x))
				{
					SyncWeather();
				}

				if (ImGui::Button("Reset Defaults##SkyboxReset"))
				{
					weather.skyExposure = 1.0f;
					weather.sky_rotation = 0.0f;
					weather.horizon = XMFLOAT3(1.0f, 1.0f, 1.0f);
					SyncWeather();
				}

				ImGui::EndTabItem();
			}

			// TAB 3: SKYBOX BASED LIGHTING (ENVIRONMENT IBL)
			if (ImGui::BeginTabItem("💡 Skybox Lighting (IBL)"))
			{
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "Environment Image-Based Lighting (IBL)");
				ImGui::TextWrapped("Cast diffuse and specular ambient illumination into the 3D scene from the skybox.");
				ImGui::Spacing();

				static bool enableSkyboxLighting = false;
				static float lightingIntensity = 1.0f;
				static XMFLOAT3 lightingColor = XMFLOAT3(1.0f, 1.0f, 1.0f);
				static float diffuseContrib = 1.0f;
				static float specularContrib = 1.0f;

				if (ImGui::Checkbox("Enable Skybox Based Lighting", &enableSkyboxLighting))
				{
					SyncWeather();
				}

				if (enableSkyboxLighting)
				{
					if (ImGui::SliderFloat("Lighting Intensity", &lightingIntensity, 0.0f, 10.0f, "%.2f")) SyncWeather();
					if (ImGui::ColorEdit3("Lighting Color Tint", &lightingColor.x)) SyncWeather();
					if (ImGui::SliderFloat("Diffuse Contribution", &diffuseContrib, 0.0f, 2.0f, "%.2f")) SyncWeather();
					if (ImGui::SliderFloat("Specular / Reflection Contribution", &specularContrib, 0.0f, 2.0f, "%.2f")) SyncWeather();
				}
				else
				{
					ImGui::TextDisabled("  (Skybox based scene lighting is currently disabled by default)");
				}

				ImGui::EndTabItem();
			}

			// TAB 4: PROCEDURAL & ATMOSPHERIC SKY
			if (ImGui::BeginTabItem("☀️ Atmospheric Sky"))
			{
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "Physically Based Sky Atmosphere");

				bool realistic = weather.IsRealisticSky();
				if (ImGui::Checkbox("Realistic Atmosphere (Physically Based)", &realistic))
				{
					weather.SetRealisticSky(realistic);
					SyncWeather();
				}

				if (realistic)
				{
					bool aerial = weather.IsRealisticSkyAerialPerspective();
					if (ImGui::Checkbox("Aerial Perspective", &aerial))
					{
						weather.SetRealisticSkyAerialPerspective(aerial);
						SyncWeather();
					}

					bool highQuality = weather.IsRealisticSkyHighQuality();
					if (ImGui::Checkbox("High Quality Sky (Shadows & Detailed LUT)", &highQuality))
					{
						weather.SetRealisticSkyHighQuality(highQuality);
						SyncWeather();
					}

					bool skyShadow = weather.IsRealisticSkyReceiveShadow();
					if (ImGui::Checkbox("Sky Receive Shadow", &skyShadow))
					{
						weather.SetRealisticSkyReceiveShadow(skyShadow);
						SyncWeather();
					}

					if (ImGui::SliderFloat("Stars Intensity in Night Sky", &weather.stars, 0.0f, 1.0f, "%.2f"))
					{
						SyncWeather();
					}
				}
				else
				{
					ImGui::TextDisabled("Enable 'Realistic Atmosphere' above to activate atmospheric simulation.");
				}

				ImGui::Separator();
				ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "Gradient Colors & Ambient Lighting");

				if (ImGui::ColorEdit3("Horizon Color", &weather.horizon.x)) SyncWeather();
				if (ImGui::ColorEdit3("Zenith Color", &weather.zenith.x)) SyncWeather();
				if (ImGui::ColorEdit3("Ambient Color", &weather.ambient.x)) SyncWeather();

				if (ImGui::Button("Reset All Colors to Black (0, 0, 0)", ImVec2(240, 24)))
				{
					weather.horizon = XMFLOAT3(0, 0, 0);
					weather.zenith = XMFLOAT3(0, 0, 0);
					weather.ambient = XMFLOAT3(0, 0, 0);
					SyncWeather();
				}

				ImGui::Separator();
				ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "Volumetric 3D Clouds & Fog");

				bool clouds = weather.IsVolumetricClouds();
				if (ImGui::Checkbox("Volumetric Clouds", &clouds))
				{
					weather.SetVolumetricClouds(clouds);
					SyncWeather();
				}

				if (clouds)
				{
					if (ImGui::SliderFloat("Cloud Start Height", &weather.volumetricCloudParameters.cloudStartHeight, 500.0f, 2500.0f, "%.0f m")) SyncWeather();
					if (ImGui::SliderFloat("Cloud Thickness", &weather.volumetricCloudParameters.cloudThickness, 0.0f, 8000.0f, "%.0f m")) SyncWeather();
				}

				if (ImGui::SliderFloat("Fog Start Distance", &weather.fogStart, 0.0f, 5000.0f, "%.1f m")) SyncWeather();
				if (ImGui::SliderFloat("Fog Density", &weather.fogDensity, 0.0f, 0.05f, "%.5f")) SyncWeather();

				ImGui::EndTabItem();
			}

			// TAB 5: ENVIRONMENT & WEATHER (OCEAN, WIND, RAIN, COLOR GRADING)
			if (ImGui::BeginTabItem("🌊 Environment & Weather"))
			{
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "Ocean Simulation");

				bool oceanEnabled = weather.IsOceanEnabled();
				if (ImGui::Checkbox("Ocean Simulation", &oceanEnabled))
				{
					weather.SetOceanEnabled(oceanEnabled);
					if (!oceanEnabled)
					{
						scene.ocean = {};
					}
					SyncWeather();
				}

				if (oceanEnabled)
				{
					if (ImGui::SliderFloat("Ocean Patch Size", &weather.oceanParameters.patch_length, 1.0f, 1000.0f))
					{
						scene.ocean = {};
						SyncWeather();
					}
					if (ImGui::SliderFloat("Wave Amplitude", &weather.oceanParameters.wave_amplitude, 0.0f, 1000.0f))
					{
						scene.ocean = {};
						SyncWeather();
					}
					if (ImGui::SliderFloat("Choppiness", &weather.oceanParameters.choppy_scale, 0.0f, 10.0f)) SyncWeather();
					if (ImGui::SliderFloat("Wind Dependency", &weather.oceanParameters.wind_dependency, 0.0f, 1.0f)) SyncWeather();
				}

				ImGui::Separator();
				ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "Wind & Gravity");

				if (ImGui::SliderFloat("Gravity Y", &weather.gravity.y, -20.0f, 20.0f)) SyncWeather();
				if (ImGui::SliderFloat("Wind Speed", &weather.windSpeed, 0.0f, 4.0f)) SyncWeather();
				if (ImGui::SliderFloat("Wind Randomness", &weather.windRandomness, 0.0f, 10.0f)) SyncWeather();

				ImGui::Separator();
				ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "Rain Weather Effect");

				if (ImGui::SliderFloat("Rain Amount", &weather.rain_amount, 0.0f, 1.0f)) SyncWeather();
				if (weather.rain_amount > 0.001f)
				{
					if (ImGui::SliderFloat("Rain Speed", &weather.rain_speed, 0.0f, 2.0f)) SyncWeather();
					if (ImGui::ColorEdit4("Rain Color", &weather.rain_color.x)) SyncWeather();
				}

				ImGui::Separator();
				ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "Color Grading LUT");

				std::string lutName = weather.colorGradingMapName.empty() ? "Load Color Grading LUT (PNG)" : wi::helper::GetFileNameFromPath(weather.colorGradingMapName);
				if (ImGui::Button(lutName.c_str(), ImVec2(220, 26)))
				{
					std::string filter = "Texture (*.png;*.dds)\0*.png;*.dds\0All Files (*.*)\0*.*\0";
					std::string file = ProjectBrowser::ShowOpenFileDialog(filter.c_str());
					if (!file.empty())
					{
						weather.colorGradingMapName = file;
						weather.colorGradingMap = wi::resourcemanager::Load(file, wi::resourcemanager::Flags::IMPORT_COLORGRADINGLUT);
						SyncWeather();
					}
				}
				if (weather.colorGradingMap.IsValid())
				{
					ImGui::SameLine();
					if (ImGui::Button("Clear LUT", ImVec2(90, 26)))
					{
						weather.colorGradingMap = {};
						weather.colorGradingMapName.clear();
						SyncWeather();
					}
				}

				ImGui::EndTabItem();
			}

			// TAB 6: PRESETS (1-CLICK WORLD ATMOSPHERES)
			if (ImGui::BeginTabItem("🎭 Presets"))
			{
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "Atmosphere & Weather Presets");

				float pBtnW = 240.0f;
				float pBtnH = 28.0f;

				if (ImGui::Button("⚫ Pitch Black (Disable Skybox)", ImVec2(pBtnW, pBtnH)))
				{
					weather.skyMap = {};
					weather.skyMapName.clear();
					weather.SetRealisticSky(false);
					weather.SetVolumetricClouds(false);
					weather.ambient = XMFLOAT3(0, 0, 0);
					weather.horizon = XMFLOAT3(0, 0, 0);
					weather.zenith = XMFLOAT3(0, 0, 0);
					weather.fogDensity = 0.0f;
					SyncWeather();
					m_bottomDrawer.AddLog("World", "Applied Preset: Pitch Black (Dark World)", 3);
				}

				if (ImGui::Button("☀️ Daytime Atmosphere", ImVec2(pBtnW, pBtnH)))
				{
					weather.skyMap = {};
					weather.skyMapName.clear();
					weather.SetRealisticSky(true);
					weather.SetVolumetricClouds(false);
					weather.ambient = XMFLOAT3(33.0f / 255.0f, 47.0f / 255.0f, 127.0f / 255.0f);
					weather.horizon = XMFLOAT3(101.0f / 255.0f, 101.0f / 255.0f, 227.0f / 255.0f);
					weather.zenith = XMFLOAT3(99.0f / 255.0f, 133.0f / 255.0f, 255.0f / 255.0f);
					weather.fogStart = 100.0f;
					weather.fogDensity = 0.0025f;
					SyncWeather();
					m_bottomDrawer.AddLog("World", "Applied Preset: Daytime Atmosphere", 3);
				}

				if (ImGui::Button("⛅ Partly Cloudy", ImVec2(pBtnW, pBtnH)))
				{
					weather.skyMap = {};
					weather.skyMapName.clear();
					weather.SetRealisticSky(true);
					weather.SetVolumetricClouds(true);
					weather.ambient = XMFLOAT3(0.12f, 0.12f, 0.15f);
					weather.horizon = XMFLOAT3(0.45f, 0.5f, 0.65f);
					weather.zenith = XMFLOAT3(0.2f, 0.3f, 0.5f);
					weather.fogStart = 50.0f;
					weather.fogDensity = 0.002f;
					SyncWeather();
					m_bottomDrawer.AddLog("World", "Applied Preset: Partly Cloudy", 3);
				}

				if (ImGui::Button("🌅 Sunset", ImVec2(pBtnW, pBtnH)))
				{
					weather.skyMap = {};
					weather.skyMapName.clear();
					weather.SetRealisticSky(false);
					weather.ambient = XMFLOAT3(86.0f / 255.0f, 29.0f / 255.0f, 29.0f / 255.0f);
					weather.horizon = XMFLOAT3(121.0f / 255.0f, 28.0f / 255.0f, 22.0f / 255.0f);
					weather.zenith = XMFLOAT3(80.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f);
					weather.fogStart = 50.0f;
					weather.fogDensity = 0.005f;
					SyncWeather();
					m_bottomDrawer.AddLog("World", "Applied Preset: Sunset", 3);
				}

				if (ImGui::Button("☁️ Overcast Cloudy", ImVec2(pBtnW, pBtnH)))
				{
					weather.skyMap = {};
					weather.skyMapName.clear();
					weather.SetRealisticSky(false);
					weather.ambient = XMFLOAT3(0.1f, 0.1f, 0.1f);
					weather.horizon = XMFLOAT3(0.38f, 0.38f, 0.38f);
					weather.zenith = XMFLOAT3(0.42f, 0.42f, 0.42f);
					weather.fogStart = 0.0f;
					weather.fogDensity = 0.01f;
					SyncWeather();
					m_bottomDrawer.AddLog("World", "Applied Preset: Cloudy", 3);
				}

				if (ImGui::Button("🌙 Night", ImVec2(pBtnW, pBtnH)))
				{
					weather.skyMap = {};
					weather.skyMapName.clear();
					weather.SetRealisticSky(true);
					weather.ambient = XMFLOAT3(12.0f / 255.0f, 21.0f / 255.0f, 77.0f / 255.0f);
					weather.horizon = XMFLOAT3(2.0f / 255.0f, 10.0f / 255.0f, 20.0f / 255.0f);
					weather.zenith = XMFLOAT3(0, 0, 0);
					weather.stars = 0.8f;
					weather.fogStart = 10.0f;
					weather.fogDensity = 0.025f;
					SyncWeather();
					m_bottomDrawer.AddLog("World", "Applied Preset: Night", 3);
				}

				if (ImGui::Button("⚪ White Furnace (Lighting Test)", ImVec2(pBtnW, pBtnH)))
				{
					weather.skyMap = {};
					weather.skyMapName.clear();
					weather.SetRealisticSky(false);
					weather.ambient = XMFLOAT3(0, 0, 0);
					weather.horizon = XMFLOAT3(1, 1, 1);
					weather.zenith = XMFLOAT3(1, 1, 1);
					weather.fogStart = 0;
					weather.fogDensity = 0;
					SyncWeather();
					m_bottomDrawer.AddLog("World", "Applied Preset: White Furnace", 3);
				}

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		ImGui::End();
	}
}
