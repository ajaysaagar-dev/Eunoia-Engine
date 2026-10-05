#include "stdafx.h"
#include "EunoiaBottomDrawer.h"
#include "EunoiaTheme.h"
#include "EunoiaProjectBrowser.h"
#include "Editor.h"
#include "ImGui/imgui.h"
#include "wiLua.h"
#include <shellapi.h>
#include <algorithm>

using namespace wi::ecs;
using namespace wi::scene;

namespace EunoiaUI
{
	BottomDrawer::BottomDrawer()
	{
		std::error_code ec;
		std::filesystem::path candidates[] = {
			std::filesystem::current_path() / "Content",
			std::filesystem::current_path() / ".." / ".." / ".." / ".." / "Content",
			std::filesystem::current_path() / ".." / ".." / ".." / "Content",
			std::filesystem::current_path() / ".." / ".." / "Content",
			std::filesystem::current_path() / ".." / "Content",
			std::filesystem::current_path()
		};

		m_contentRoot = std::filesystem::current_path();
		for (const auto& p : candidates)
		{
			if (std::filesystem::exists(p, ec) && std::filesystem::is_directory(p, ec))
			{
				m_contentRoot = std::filesystem::weakly_canonical(p, ec);
				break;
			}
		}
		m_currentDir = m_contentRoot;

		AddLog("Engine", "Eunoia Editor Content Browser root: " + m_contentRoot.string(), 3);
	}

	void BottomDrawer::SetContentRoot(const std::filesystem::path& root)
	{
		std::error_code ec;
		if (std::filesystem::exists(root, ec))
		{
			m_contentRoot = root;
			m_currentDir = root;
		}
	}

	void BottomDrawer::AddLog(const std::string& category, const std::string& message, int level)
	{
		m_logs.push_back({ category, message, level });
		if (m_logs.size() > 1000)
		{
			m_logs.erase(m_logs.begin(), m_logs.begin() + 100);
		}
	}

	void BottomDrawer::ClearLogs()
	{
		m_logs.clear();
	}

	void BottomDrawer::Render(EditorComponent& editor, float width, float currentH, float posX, float posY)
	{
		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(width, currentH), ImGuiCond_Always);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
		if (ImGui::Begin("BottomDrawer###EunoiaBottomDrawerWindow", nullptr, flags))
		{
			// Drawer Header: Tab Buttons & Collapse/Expand
			bool isContentTab = (m_activeTab == 0);
			bool isLogTab = (m_activeTab == 1);

			if (isContentTab) ImGui::PushStyleColor(ImGuiCol_Button, Colors::AccentBlue);
			if (ImGui::Button("Content Browser", ImVec2(130, 24))) m_activeTab = 0;
			if (isContentTab) ImGui::PopStyleColor();

			ImGui::SameLine();
			if (isLogTab) ImGui::PushStyleColor(ImGuiCol_Button, Colors::AccentBlue);
			if (ImGui::Button("Output Log", ImVec2(100, 24))) m_activeTab = 1;
			if (isLogTab) ImGui::PopStyleColor();

			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();

			if (ImGui::SmallButton(m_isOpen ? "Collapse [-]" : "Expand [+]"))
			{
				m_isOpen = !m_isOpen;
			}

			ImGui::Separator();

			// Render content if expanded
			if (m_isOpen)
			{
				if (m_activeTab == 0)
				{
					RenderContentBrowser(editor);
				}
				else
				{
					RenderOutputLog(editor);
				}
			}
		}
		ImGui::End();

		ImGui::PopStyleVar(2);
	}

	bool BottomDrawer::PassesTypeFilter(const std::filesystem::path& path, bool isDir) const
	{
		if (isDir) return true;
		if (m_typeFilter == 0) return true; // All

		std::string ext = path.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		if (m_typeFilter == 1) // 3D Models & Levels
			return (ext == ".eulevel" || ext == ".wiscene" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".ply");
		if (m_typeFilter == 2) // Textures
			return (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds" || ext == ".tga" || ext == ".bmp" || ext == ".hdr");
		if (m_typeFilter == 3) // Materials
			return (ext == ".wimaterial" || ext == ".mat");
		if (m_typeFilter == 4) // Scripts
			return (ext == ".lua");
		if (m_typeFilter == 5) // Audio
			return (ext == ".wav" || ext == ".ogg" || ext == ".mp3");

		return true;
	}

	void BottomDrawer::HandleMoveItem(const std::string& srcPath, const std::filesystem::path& destDir)
	{
		std::error_code ec;
		std::filesystem::path src(srcPath);
		if (!std::filesystem::exists(src, ec)) return;

		std::filesystem::path dest = destDir / src.filename();
		if (src == dest) return;

		if (std::filesystem::exists(dest, ec))
		{
			AddLog("ContentBrowser", "Cannot move: item with same name already exists in target folder", 2);
			return;
		}

		std::filesystem::rename(src, dest, ec);
		if (ec)
		{
			// Fallback if cross-drive
			std::filesystem::copy(src, dest, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
			if (!ec) std::filesystem::remove_all(src, ec);
		}

		if (!ec)
		{
			AddLog("ContentBrowser", "Moved " + src.filename().string() + " to " + destDir.filename().string(), 3);
			if (m_selectedAssetPath == srcPath) m_selectedAssetPath = dest.string();
		}
		else
		{
			AddLog("ContentBrowser", "Failed to move: " + ec.message(), 2);
		}
	}

	void BottomDrawer::HandleDuplicateItem(const std::filesystem::path& path)
	{
		std::error_code ec;
		if (!std::filesystem::exists(path, ec)) return;

		std::string stem = path.stem().string();
		std::string ext = path.extension().string();
		std::filesystem::path parent = path.parent_path();

		std::filesystem::path copyPath = parent / (stem + "_Copy" + ext);
		int counter = 1;
		while (std::filesystem::exists(copyPath, ec))
		{
			copyPath = parent / (stem + "_Copy" + std::to_string(counter++) + ext);
		}

		if (std::filesystem::is_directory(path, ec))
		{
			std::filesystem::copy(path, copyPath, std::filesystem::copy_options::recursive, ec);
		}
		else
		{
			std::filesystem::copy_file(path, copyPath, std::filesystem::copy_options::overwrite_existing, ec);
		}

		if (!ec)
		{
			AddLog("ContentBrowser", "Duplicated: " + copyPath.filename().string(), 3);
			m_selectedAssetPath = copyPath.string();
		}
		else
		{
			AddLog("ContentBrowser", "Failed to duplicate: " + ec.message(), 2);
		}
	}

	void BottomDrawer::HandleCreateScript(const std::string& name)
	{
		std::string filename = name;
		if (filename.find(".lua") == std::string::npos) filename += ".lua";

		std::filesystem::path fullPath = m_currentDir / filename;
		std::error_code ec;
		if (std::filesystem::exists(fullPath, ec))
		{
			AddLog("ContentBrowser", "Script already exists: " + filename, 2);
			return;
		}

		std::ofstream out(fullPath);
		if (out.is_open())
		{
			out << "-- Eunoia Engine Lua Script: " << filename << "\n";
			out << "-- Documentation: Content/Documentation/EunoiaEngine-Documentation.md\n\n";
			out << "runProcess(function()\n";
			out << "    local scene = GetScene()\n";
			out << "    local camera = GetCamera()\n\n";
			out << "    while true do\n";
			out << "        update()\n";
			out << "        local dt = getDeltaTime()\n";
			out << "        -- Script logic here\n";
			out << "    end\n";
			out << "end)\n";
			out.close();

			AddLog("ContentBrowser", "Created Lua Script: " + filename, 3);
			m_selectedAssetPath = fullPath.string();
		}
	}

	void BottomDrawer::HandleCreateMaterial(const std::string& name)
	{
		std::string filename = name;
		if (filename.find(".wimaterial") == std::string::npos) filename += ".wimaterial";

		std::filesystem::path fullPath = m_currentDir / filename;
		std::error_code ec;
		if (std::filesystem::exists(fullPath, ec))
		{
			AddLog("ContentBrowser", "Material already exists: " + filename, 2);
			return;
		}

		std::ofstream out(fullPath);
		if (out.is_open())
		{
			out << "EunoiaMaterial 1.0\n";
			out << "BaseColor: 1.0 1.0 1.0 1.0\n";
			out << "Roughness: 0.5\n";
			out << "Metalness: 0.0\n";
			out.close();

			AddLog("ContentBrowser", "Created Material: " + filename, 3);
			m_selectedAssetPath = fullPath.string();
		}
	}

	void BottomDrawer::HandleCreateLevel(const std::string& name, EditorComponent& editor)
	{
		std::string filename = name;
		if (filename.find(".eulevel") == std::string::npos && filename.find(".wiscene") == std::string::npos)
		{
			filename += ".eulevel";
		}

		std::filesystem::path fullPath = m_currentDir / filename;
		std::error_code ec;
		if (std::filesystem::exists(fullPath, ec))
		{
			AddLog("ContentBrowser", "Level already exists: " + filename, 2);
			return;
		}

		// Create a clean starter level matching engine standard
		Scene starterScene;

		// Add default directional sun light
		Entity lightEntity = starterScene.Entity_CreateLight(
			"Directional Light",
			XMFLOAT3(0, 10, 0),
			XMFLOAT3(1, 1, 1),
			2.0f,
			100.0f,
			wi::scene::LightComponent::DIRECTIONAL
		);
		TransformComponent* lightTrans = starterScene.transforms.GetComponent(lightEntity);
		if (lightTrans)
		{
			lightTrans->RotateRollPitchYaw(XMFLOAT3(XMConvertToRadians(45.0f), XMConvertToRadians(45.0f), 0.0f));
			lightTrans->SetDirty();
			lightTrans->UpdateTransform();
		}

		// Add default ground plane
		Entity floor = starterScene.Entity_CreatePlane("Ground Plane");
		TransformComponent* floorTrans = starterScene.transforms.GetComponent(floor);
		if (floorTrans)
		{
			floorTrans->scale_local = XMFLOAT3(10.0f, 1.0f, 10.0f);
			floorTrans->SetDirty();
			floorTrans->UpdateTransform();
		}

		// Add default starter cube
		Entity cube = starterScene.Entity_CreateCube("Cube");
		TransformComponent* cubeTrans = starterScene.transforms.GetComponent(cube);
		if (cubeTrans)
		{
			cubeTrans->translation_local = XMFLOAT3(0.0f, 1.0f, 0.0f);
			cubeTrans->SetDirty();
			cubeTrans->UpdateTransform();
		}

		starterScene.weather.ambient = XMFLOAT3(0.2f, 0.2f, 0.25f);
		starterScene.weather.sunColor = XMFLOAT3(1.0f, 0.95f, 0.85f);
		starterScene.weather.sunDirection = XMFLOAT3(0.5f, 0.8f, 0.3f);

		starterScene.Update(0);

		{
			wi::Archive archive(fullPath.string(), false);
			starterScene.Serialize(archive);
		}

		AddLog("ContentBrowser", "Created new Level: " + filename, 3);
		m_selectedAssetPath = fullPath.string();
	}

	void BottomDrawer::RenderContentBrowser(EditorComponent& editor)
	{
		std::error_code ec;
		if (!std::filesystem::exists(m_contentRoot, ec))
		{
			m_contentRoot = std::filesystem::current_path();
			m_currentDir = m_contentRoot;
		}

		// Helper to import files from OS file picker
		auto HandleImport = [&](const char* filter) {
			std::string src = ProjectBrowser::ShowOpenFileDialog(filter);
			if (!src.empty())
			{
				std::error_code copyEc;
				std::filesystem::path srcPath(src);
				std::filesystem::path destPath = m_currentDir / srcPath.filename();
				if (std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing, copyEc))
				{
					AddLog("ContentBrowser", "Imported: " + srcPath.filename().string(), 3);
					m_selectedAssetPath = destPath.string();
				}
				else
				{
					AddLog("ContentBrowser", "Failed to import " + srcPath.filename().string() + ": " + copyEc.message(), 2);
				}
			}
		};

		// -------------------------------------------------------------
		// 1. TOP NAVIGATION & ACTION TOOLBAR
		// -------------------------------------------------------------
		bool isAtRoot = (m_currentDir == m_contentRoot);
		if (isAtRoot) ImGui::BeginDisabled();
		if (ImGui::Button("⬆ Up", ImVec2(50, 24)))
		{
			if (m_currentDir.has_parent_path() && m_currentDir != m_contentRoot)
			{
				m_currentDir = m_currentDir.parent_path();
				m_selectedAssetPath = "";
			}
		}
		if (isAtRoot) ImGui::EndDisabled();

		// Drag & drop on "⬆ Up" button to move files to parent directory
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_ASSET_PATH"))
			{
				std::string srcPath((const char*)payload->Data);
				if (m_currentDir.has_parent_path() && m_currentDir != m_contentRoot)
				{
					HandleMoveItem(srcPath, m_currentDir.parent_path());
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine();
		if (ImGui::Button("🏠 Content", ImVec2(75, 24)))
		{
			m_currentDir = m_contentRoot;
			m_selectedAssetPath = "";
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_ASSET_PATH"))
			{
				std::string srcPath((const char*)payload->Data);
				HandleMoveItem(srcPath, m_contentRoot);
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();

		// Add / Create Menu
		if (ImGui::Button("+ Add / Create", ImVec2(110, 24)))
		{
			ImGui::OpenPopup("CBAddCreatePopup");
		}

		if (ImGui::BeginPopup("CBAddCreatePopup"))
		{
			if (ImGui::MenuItem("🌐 New Level (.eulevel)..."))
			{
				m_showNewLevelModal = true;
				strcpy_s(m_newLevelNameBuf, "NewLevel");
			}
			if (ImGui::MenuItem("📁 New Folder..."))
			{
				m_showNewFolderModal = true;
				strcpy_s(m_newFolderNameBuf, "NewFolder");
			}
			if (ImGui::MenuItem("🎨 New Material (.wimaterial)..."))
			{
				m_showNewMaterialModal = true;
				strcpy_s(m_newMaterialNameBuf, "M_NewMaterial");
			}
			if (ImGui::MenuItem("📜 New Lua Script (.lua)..."))
			{
				m_showNewScriptModal = true;
				strcpy_s(m_newScriptNameBuf, "NewScript");
			}
			ImGui::Separator();
			if (ImGui::MenuItem("📥 Import 3D Model / Level..."))
			{
				HandleImport("3D Model / Level Files (*.obj;*.gltf;*.glb;*.fbx;*.ply;*.eulevel;*.wiscene)\0*.obj;*.gltf;*.glb;*.fbx;*.ply;*.eulevel;*.wiscene\0All Files (*.*)\0*.*\0");
			}
			if (ImGui::MenuItem("🖼 Import Texture..."))
			{
				HandleImport("Texture Files (*.png;*.jpg;*.jpeg;*.dds;*.tga;*.bmp;*.hdr)\0*.png;*.jpg;*.jpeg;*.dds;*.tga;*.bmp;*.hdr\0All Files (*.*)\0*.*\0");
			}
			if (ImGui::MenuItem("🔊 Import Audio..."))
			{
				HandleImport("Audio Files (*.wav;*.ogg;*.mp3)\0*.wav;*.ogg;*.mp3\0All Files (*.*)\0*.*\0");
			}
			ImGui::Separator();
			if (ImGui::MenuItem("📂 Show in File Explorer"))
			{
				ShellExecuteA(NULL, "explore", m_currentDir.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
			}
			ImGui::EndPopup();
		}

		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();

		// Interactive Segmented Breadcrumbs with Drag-Drop Move Support
		std::filesystem::path rel = std::filesystem::relative(m_currentDir, m_contentRoot, ec);
		std::filesystem::path accumPath = m_contentRoot;
		int bcIdx = 0;

		for (const auto& part : rel)
		{
			if (part == ".") continue;
			accumPath /= part;

			ImGui::TextDisabled(">");
			ImGui::SameLine();

			ImGui::PushID(bcIdx++);
			bool isCurrentSeg = (accumPath == m_currentDir);
			if (isCurrentSeg) ImGui::PushStyleColor(ImGuiCol_Text, Colors::AccentBlue);

			if (ImGui::SmallButton(part.string().c_str()))
			{
				m_currentDir = accumPath;
				m_selectedAssetPath = "";
			}

			if (isCurrentSeg) ImGui::PopStyleColor();

			// Drag and drop onto breadcrumb segment to move
			std::filesystem::path segTarget = accumPath;
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_ASSET_PATH"))
				{
					std::string srcPath((const char*)payload->Data);
					HandleMoveItem(srcPath, segTarget);
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::PopID();
			ImGui::SameLine();
		}

		// Right controls: Type Filter, Search, Zoom
		float rightWidth = 420.0f;
		if (ImGui::GetContentRegionAvail().x > rightWidth)
		{
			ImGui::SameLine(ImGui::GetWindowWidth() - rightWidth);
		}
		else
		{
			ImGui::SameLine();
		}

		// Type Filter Combo
		const char* typeFilters[] = { "All Types", "3D Models", "Textures", "Materials", "Scripts", "Audio" };
		ImGui::SetNextItemWidth(100.0f);
		ImGui::Combo("##CBTypeFilter", &m_typeFilter, typeFilters, IM_ARRAYSIZE(typeFilters));
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Filter visible asset types");

		ImGui::SameLine();
		ImGui::SetNextItemWidth(130.0f);
		ImGui::InputTextWithHint("##CBSearch", "🔍 Search...", m_searchFilterBuf, sizeof(m_searchFilterBuf));
		if (m_searchFilterBuf[0] != '\0')
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("X##ClearSearch"))
			{
				m_searchFilterBuf[0] = '\0';
			}
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::SliderFloat("##Zoom", &m_thumbnailSize, 48.0f, 128.0f, "%.0f px");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Thumbnail Card Scale");

		ImGui::Separator();

		// -------------------------------------------------------------
		// 2. TWO-PANE LAYOUT: LEFT FOLDER TREE (20%) + RIGHT ASSET GRID (80%)
		// -------------------------------------------------------------
		float treeWidth = 190.0f;
		ImGui::BeginChild("CBFolderTree", ImVec2(treeWidth, 0), true);
		ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "FOLDERS");
		ImGui::Separator();
		DrawFolderTreeNode(m_contentRoot);
		ImGui::EndChild();

		ImGui::SameLine();

		// Main Asset Grid View
		ImGui::BeginChild("CBAssetGrid", ImVec2(0, 0), true);

		// Context Menu for empty area in grid
		if (ImGui::BeginPopupContextWindow("CBGridEmptyContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::MenuItem("🌐 New Level (.eulevel)..."))
			{
				m_showNewLevelModal = true;
				strcpy_s(m_newLevelNameBuf, "NewLevel");
			}
			if (ImGui::MenuItem("📁 New Folder..."))
			{
				m_showNewFolderModal = true;
				strcpy_s(m_newFolderNameBuf, "NewFolder");
			}
			if (ImGui::MenuItem("🎨 New Material (.wimaterial)..."))
			{
				m_showNewMaterialModal = true;
				strcpy_s(m_newMaterialNameBuf, "M_NewMaterial");
			}
			if (ImGui::MenuItem("📜 New Lua Script (.lua)..."))
			{
				m_showNewScriptModal = true;
				strcpy_s(m_newScriptNameBuf, "NewScript");
			}
			ImGui::Separator();
			if (ImGui::MenuItem("📥 Import 3D Model / Level..."))
			{
				HandleImport("3D Model / Level Files (*.obj;*.gltf;*.glb;*.fbx;*.ply;*.eulevel;*.wiscene)\0*.obj;*.gltf;*.glb;*.fbx;*.ply;*.eulevel;*.wiscene\0All Files (*.*)\0*.*\0");
			}
			if (ImGui::MenuItem("🖼 Import Texture..."))
			{
				HandleImport("Texture Files (*.png;*.jpg;*.jpeg;*.dds;*.tga;*.bmp;*.hdr)\0*.png;*.jpg;*.jpeg;*.dds;*.tga;*.bmp;*.hdr\0All Files (*.*)\0*.*\0");
			}
			if (ImGui::MenuItem("🔊 Import Audio..."))
			{
				HandleImport("Audio Files (*.wav;*.ogg;*.mp3)\0*.wav;*.ogg;*.mp3\0All Files (*.*)\0*.*\0");
			}
			ImGui::Separator();
			if (ImGui::MenuItem("📂 Show in File Explorer"))
			{
				ShellExecuteA(NULL, "explore", m_currentDir.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
			}
			ImGui::EndPopup();
		}

		// Keyboard Shortcuts inside Asset Grid (F2=Rename, Delete=Delete, Backspace=Up)
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		{
			if (ImGui::IsKeyPressed(ImGuiKey_F2) && !m_selectedAssetPath.empty())
			{
				m_renameTargetPath = m_selectedAssetPath;
				std::filesystem::path tp(m_renameTargetPath);
				strcpy_s(m_renameNameBuf, tp.filename().string().c_str());
				m_showRenameModal = true;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_selectedAssetPath.empty())
			{
				m_deleteTargetPath = m_selectedAssetPath;
				m_showDeleteModal = true;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !isAtRoot)
			{
				m_currentDir = m_currentDir.parent_path();
				m_selectedAssetPath = "";
			}
		}

		// Calculate columns
		float availWidth = ImGui::GetContentRegionAvail().x;
		int columns = (int)(availWidth / (m_thumbnailSize + 16.0f));
		if (columns < 1) columns = 1;

		std::string filterLower = m_searchFilterBuf;
		std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

		int itemIdx = 0;
		for (const auto& entry : std::filesystem::directory_iterator(m_currentDir, ec))
		{
			std::string filename = entry.path().filename().string();
			std::string fileLower = filename;
			std::transform(fileLower.begin(), fileLower.end(), fileLower.begin(), ::tolower);

			// Search Substring Filter
			if (!filterLower.empty() && fileLower.find(filterLower) == std::string::npos)
				continue;

			bool isDir = entry.is_directory(ec);

			// Type Filter
			if (!PassesTypeFilter(entry.path(), isDir))
				continue;

			ImGui::PushID(itemIdx);
			ImGui::BeginGroup();

			// Card Visual Classification
			const char* iconTag = isDir ? "[DIR]" : "[FILE]";
			ImVec4 tagColor = ImVec4(0.2f, 0.7f, 0.9f, 1.0f); // Default Cyan for Folders

			std::string ext = entry.path().extension().string();
			std::string extLower = ext;
			std::transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);

			bool isLevel = (extLower == ".eulevel" || extLower == ".wiscene");
			bool is3DModel = (extLower == ".obj" || extLower == ".gltf" || extLower == ".glb" || extLower == ".fbx" || extLower == ".ply");
			bool isTexture = (extLower == ".png" || extLower == ".jpg" || extLower == ".jpeg" || extLower == ".dds" || extLower == ".tga" || extLower == ".bmp" || extLower == ".hdr");
			bool isMaterial = (extLower == ".wimaterial" || extLower == ".mat");
			bool isScript = (extLower == ".lua");
			bool isAudio = (extLower == ".wav" || extLower == ".ogg" || extLower == ".mp3");

			if (isLevel) { iconTag = "🌐 LEVEL"; tagColor = ImVec4(0.20f, 0.85f, 0.95f, 1.0f); }     // Cyan
			else if (is3DModel) { iconTag = "🧊 3D"; tagColor = ImVec4(0.95f, 0.55f, 0.20f, 1.0f); } // Orange
			else if (isTexture) { iconTag = "🖼 TEX"; tagColor = ImVec4(0.20f, 0.85f, 0.50f, 1.0f); } // Emerald
			else if (isMaterial) { iconTag = "🎨 MAT"; tagColor = ImVec4(0.35f, 0.65f, 1.00f, 1.0f); } // Blue
			else if (isScript) { iconTag = "📜 LUA"; tagColor = ImVec4(0.85f, 0.40f, 0.95f, 1.0f); }   // Purple
			else if (isAudio) { iconTag = "🔊 AUD"; tagColor = ImVec4(0.95f, 0.80f, 0.20f, 1.0f); }    // Amber
			else if (isDir) { iconTag = "📁 DIR"; tagColor = ImVec4(0.25f, 0.80f, 0.95f, 1.0f); }

			bool isSelected = (m_selectedAssetPath == entry.path().string());
			if (isSelected)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, Colors::AccentBlue);
				ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.9f));
			}

			std::string shortName = (filename.size() > 7) ? (filename.substr(0, 6) + "..") : filename;
			char btnLabel[128];
			snprintf(btnLabel, sizeof(btnLabel), "%s\n\n%s##card", iconTag, shortName.c_str());

			if (ImGui::Button(btnLabel, ImVec2(m_thumbnailSize, m_thumbnailSize)))
			{
				m_selectedAssetPath = entry.path().string();
			}

			if (isSelected)
			{
				ImGui::PopStyleColor(2);
			}

			// Double Click Actions
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				if (isDir)
				{
					m_currentDir = entry.path();
					m_selectedAssetPath = "";
				}
				else if (isLevel)
				{
					AddLog("ContentBrowser", "Opening Level: " + filename, 3);
					editor.GetCurrentScene().Clear();
					editor.GetCurrentEditorScene().path = entry.path().string();
					editor.ClearSelected();
					editor.Open(entry.path().string());
				}
				else if (is3DModel)
				{
					AddLog("ContentBrowser", "Loading 3D model into scene: " + filename, 0);
					editor.Open(entry.path().string());
				}
				else if (isScript)
				{
					ShellExecuteA(NULL, "open", entry.path().string().c_str(), NULL, NULL, SW_SHOWNORMAL);
				}
				else
				{
					ShellExecuteA(NULL, "open", entry.path().string().c_str(), NULL, NULL, SW_SHOWNORMAL);
				}
			}

			// Drag and Drop Source (any asset or folder)
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
			{
				std::string pathStr = entry.path().string();
				ImGui::SetDragDropPayload("CONTENT_ASSET_PATH", pathStr.c_str(), pathStr.size() + 1);
				ImGui::Text(isLevel ? "Spawn Level: %s" : (is3DModel ? "Spawn 3D Model: %s" : "Move: %s"), filename.c_str());
				ImGui::EndDragDropSource();
			}

			// Drag and Drop Target (if item is a directory)
			if (isDir && ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_ASSET_PATH"))
				{
					std::string srcPath((const char*)payload->Data);
					HandleMoveItem(srcPath, entry.path());
				}
				ImGui::EndDragDropTarget();
			}

			// Right-Click Context Menu for each Card
			if (ImGui::BeginPopupContextItem("CBCardContextMenu"))
			{
				m_selectedAssetPath = entry.path().string();

				if (isDir)
				{
					if (ImGui::MenuItem("📂 Open Folder"))
					{
						m_currentDir = entry.path();
						m_selectedAssetPath = "";
					}
				}
				else if (isLevel)
				{
					if (ImGui::MenuItem("🌐 Open Level"))
					{
						AddLog("ContentBrowser", "Opening Level: " + filename, 3);
						editor.Open(entry.path().string());
					}
					if (ImGui::MenuItem("🚀 Import / Merge into Current Scene"))
					{
						wi::scene::LoadModel(editor.GetCurrentScene(), entry.path().string());
						AddLog("ContentBrowser", "Merged level into scene: " + filename, 3);
					}
				}
				else if (is3DModel)
				{
					if (ImGui::MenuItem("🚀 Spawn / Load into Scene"))
					{
						editor.Open(entry.path().string());
					}
				}
				else if (isScript)
				{
					if (ImGui::MenuItem("▶ Execute Script"))
					{
						wi::lua::RunFile(entry.path().string());
						AddLog("Script", "Executed " + filename, 3);
					}
					if (ImGui::MenuItem("✏️ Edit Script"))
					{
						ShellExecuteA(NULL, "open", entry.path().string().c_str(), NULL, NULL, SW_SHOWNORMAL);
					}
				}

				ImGui::Separator();
				if (ImGui::MenuItem("✏️ Rename (F2)..."))
				{
					m_renameTargetPath = entry.path().string();
					strcpy_s(m_renameNameBuf, filename.c_str());
					m_showRenameModal = true;
				}
				if (ImGui::MenuItem("📋 Duplicate"))
				{
					HandleDuplicateItem(entry.path());
				}
				if (ImGui::MenuItem("🗑️ Delete (Del)..."))
				{
					m_deleteTargetPath = entry.path().string();
					m_showDeleteModal = true;
				}
				ImGui::Separator();
				if (ImGui::MenuItem("📂 Show in File Explorer"))
				{
					ShellExecuteA(NULL, "explore", entry.path().parent_path().string().c_str(), NULL, NULL, SW_SHOWNORMAL);
				}
				if (ImGui::MenuItem("📋 Copy Full Path"))
				{
					ImGui::SetClipboardText(entry.path().string().c_str());
					AddLog("ContentBrowser", "Copied path to clipboard: " + entry.path().string(), 0);
				}

				ImGui::EndPopup();
			}

			// Tooltip with full metadata
			if (ImGui::IsItemHovered())
			{
				uintmax_t fSize = 0;
				if (!isDir) fSize = std::filesystem::file_size(entry.path(), ec);
				std::string sizeStr = isDir ? "Folder" : (std::to_string(fSize / 1024) + " KB");
				ImGui::SetTooltip("%s\nType: %s | Size: %s\nPath: %s", filename.c_str(), iconTag, sizeStr.c_str(), entry.path().string().c_str());
			}

			// Text label below card
			std::string displayLabel = filename;
			if (displayLabel.size() > 11) displayLabel = displayLabel.substr(0, 10) + "..";
			ImGui::TextWrapped("%s", displayLabel.c_str());

			ImGui::EndGroup();

			// Grid wrapping
			if ((itemIdx % columns) < (columns - 1))
			{
				ImGui::SameLine();
			}
			itemIdx++;

			ImGui::PopID();
		}

		ImGui::EndChild();

		// -------------------------------------------------------------
		// 3. POPUP MODALS (New Folder, Rename, Delete, New Script, New Material)
		// -------------------------------------------------------------
		if (m_showNewFolderModal)
		{
			ImGui::OpenPopup("Create Folder###NewFolderPopup");
			m_showNewFolderModal = false;
		}

		if (ImGui::BeginPopupModal("Create Folder###NewFolderPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Enter new folder name:");
			ImGui::SetNextItemWidth(260.0f);
			ImGui::InputText("##FolderNameInput", m_newFolderNameBuf, sizeof(m_newFolderNameBuf));
			ImGui::Spacing();

			if (ImGui::Button("Create", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter))
			{
				if (m_newFolderNameBuf[0] != '\0')
				{
					std::filesystem::create_directories(m_currentDir / m_newFolderNameBuf, ec);
					AddLog("ContentBrowser", "Created folder: " + std::string(m_newFolderNameBuf), 3);
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// Rename Modal
		if (m_showRenameModal)
		{
			ImGui::OpenPopup("Rename Item###RenameItemPopup");
			m_showRenameModal = false;
		}

		if (ImGui::BeginPopupModal("Rename Item###RenameItemPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			std::filesystem::path target(m_renameTargetPath);
			ImGui::Text("Renaming: %s", target.filename().string().c_str());
			ImGui::Spacing();
			ImGui::Text("Enter new name:");
			ImGui::SetNextItemWidth(280.0f);
			ImGui::InputText("##RenameItemInput", m_renameNameBuf, sizeof(m_renameNameBuf));

			std::string newName = m_renameNameBuf;
			bool hasInvalidChar = (newName.find('/') != std::string::npos || newName.find('\\') != std::string::npos ||
				newName.find(':') != std::string::npos || newName.find('*') != std::string::npos ||
				newName.find('?') != std::string::npos || newName.find('\"') != std::string::npos ||
				newName.find('<') != std::string::npos || newName.find('>') != std::string::npos ||
				newName.find('|') != std::string::npos);

			if (hasInvalidChar)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "⚠️ Name contains invalid filename characters (/ \\ : * ? \" < > |)");
			}

			ImGui::Spacing();
			bool canRename = (!newName.empty() && !hasInvalidChar);
			if (!canRename) ImGui::BeginDisabled();

			if (ImGui::Button("Rename", ImVec2(100, 0)) || (canRename && ImGui::IsKeyPressed(ImGuiKey_Enter)))
			{
				std::filesystem::path newPath = target.parent_path() / newName;
				std::filesystem::rename(target, newPath, ec);
				if (!ec)
				{
					AddLog("ContentBrowser", "Renamed to " + newName, 3);
					m_selectedAssetPath = newPath.string();
				}
				else
				{
					AddLog("ContentBrowser", "Failed to rename: " + ec.message(), 2);
				}
				ImGui::CloseCurrentPopup();
			}

			if (!canRename) ImGui::EndDisabled();

			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// Delete Modal
		if (m_showDeleteModal)
		{
			ImGui::OpenPopup("Delete Confirmation###DeleteItemPopup");
			m_showDeleteModal = false;
		}

		if (ImGui::BeginPopupModal("Delete Confirmation###DeleteItemPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			std::filesystem::path target(m_deleteTargetPath);
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Are you sure you want to permanently delete:");
			ImGui::Text("%s", target.filename().string().c_str());
			ImGui::Spacing();

			if (ImGui::Button("Delete", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter))
			{
				std::filesystem::remove_all(target, ec);
				if (!ec)
				{
					AddLog("ContentBrowser", "Deleted: " + target.filename().string(), 1);
					if (m_selectedAssetPath == m_deleteTargetPath) m_selectedAssetPath = "";
				}
				else
				{
					AddLog("ContentBrowser", "Failed to delete: " + ec.message(), 2);
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// New Script Modal
		if (m_showNewScriptModal)
		{
			ImGui::OpenPopup("Create Lua Script###NewScriptPopup");
			m_showNewScriptModal = false;
		}

		if (ImGui::BeginPopupModal("Create Lua Script###NewScriptPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Enter script name (without or with .lua):");
			ImGui::SetNextItemWidth(260.0f);
			ImGui::InputText("##ScriptNameInput", m_newScriptNameBuf, sizeof(m_newScriptNameBuf));
			ImGui::Spacing();

			if (ImGui::Button("Create", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter))
			{
				if (m_newScriptNameBuf[0] != '\0')
				{
					HandleCreateScript(m_newScriptNameBuf);
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// New Material Modal
		if (m_showNewMaterialModal)
		{
			ImGui::OpenPopup("Create Material###NewMaterialPopup");
			m_showNewMaterialModal = false;
		}

		if (ImGui::BeginPopupModal("Create Material###NewMaterialPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Enter material name (without or with .wimaterial):");
			ImGui::SetNextItemWidth(260.0f);
			ImGui::InputText("##MaterialNameInput", m_newMaterialNameBuf, sizeof(m_newMaterialNameBuf));
			ImGui::Spacing();

			if (ImGui::Button("Create", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter))
			{
				if (m_newMaterialNameBuf[0] != '\0')
				{
					HandleCreateMaterial(m_newMaterialNameBuf);
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// New Level Modal
		if (m_showNewLevelModal)
		{
			ImGui::OpenPopup("Create Level###NewLevelPopup");
			m_showNewLevelModal = false;
		}

		if (ImGui::BeginPopupModal("Create Level###NewLevelPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Enter Level name (without or with .eulevel):");
			ImGui::SetNextItemWidth(260.0f);
			ImGui::InputText("##LevelNameInput", m_newLevelNameBuf, sizeof(m_newLevelNameBuf));
			ImGui::Spacing();

			if (ImGui::Button("Create", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter))
			{
				if (m_newLevelNameBuf[0] != '\0')
				{
					HandleCreateLevel(m_newLevelNameBuf, editor);
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void BottomDrawer::DrawFolderTreeNode(const std::filesystem::path& dir)
	{
		std::error_code ec;
		std::string folderName = dir.filename().string();
		if (folderName.empty()) folderName = "Content";

		bool isCurrent = (m_currentDir == dir);
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (isCurrent) flags |= ImGuiTreeNodeFlags_Selected;

		// Check if has subdirectories
		bool hasSubdirs = false;
		for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
		{
			if (entry.is_directory(ec))
			{
				hasSubdirs = true;
				break;
			}
		}

		if (!hasSubdirs) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

		bool nodeOpen = ImGui::TreeNodeEx((void*)std::hash<std::string>{}(dir.string()), flags, "%s %s", isCurrent ? "📂" : "📁", folderName.c_str());
		if (ImGui::IsItemClicked())
		{
			m_currentDir = dir;
			m_selectedAssetPath = "";
		}

		// Drag and Drop target on Folder Tree Node to move items directly into this folder
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_ASSET_PATH"))
			{
				std::string srcPath((const char*)payload->Data);
				HandleMoveItem(srcPath, dir);
			}
			ImGui::EndDragDropTarget();
		}

		// Right Click Context Menu on tree node
		if (ImGui::BeginPopupContextItem("CBTreeNodeContextMenu"))
		{
			if (ImGui::MenuItem("📁 New Subfolder..."))
			{
				m_currentDir = dir;
				m_showNewFolderModal = true;
				strcpy_s(m_newFolderNameBuf, "NewFolder");
			}
			if (ImGui::MenuItem("📂 Show in File Explorer"))
			{
				ShellExecuteA(NULL, "explore", dir.string().c_str(), NULL, NULL, SW_SHOWNORMAL);
			}
			if (dir != m_contentRoot)
			{
				ImGui::Separator();
				if (ImGui::MenuItem("🗑️ Delete Folder..."))
				{
					m_deleteTargetPath = dir.string();
					m_showDeleteModal = true;
				}
			}
			ImGui::EndPopup();
		}

		if (hasSubdirs && nodeOpen)
		{
			for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
			{
				if (entry.is_directory(ec))
				{
					DrawFolderTreeNode(entry.path());
				}
			}
			ImGui::TreePop();
		}
	}

	void BottomDrawer::RenderOutputLog(EditorComponent& editor)
	{
		// Filter toolbar
		if (ImGui::Button("Clear"))
		{
			ClearLogs();
		}
		ImGui::SameLine();
		ImGui::Checkbox("Info", &m_showInfo);
		ImGui::SameLine();
		ImGui::Checkbox("Warnings", &m_showWarnings);
		ImGui::SameLine();
		ImGui::Checkbox("Errors", &m_showErrors);
		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &m_autoScroll);

		ImGui::SameLine(ImGui::GetWindowWidth() - 200.0f);
		ImGui::SetNextItemWidth(180.0f);
		ImGui::InputTextWithHint("##LogSearch", "Filter Log...", m_logSearchBuf, sizeof(m_logSearchBuf));

		ImGui::Separator();

		// Log messages child
		ImGui::BeginChild("LogScrollArea", ImVec2(0, -32), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

		std::string searchLower = m_logSearchBuf;
		std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

		for (const auto& entry : m_logs)
		{
			if (entry.level == 0 && !m_showInfo) continue;
			if (entry.level == 1 && !m_showWarnings) continue;
			if (entry.level == 2 && !m_showErrors) continue;

			if (!searchLower.empty())
			{
				std::string msgLower = entry.message;
				std::transform(msgLower.begin(), msgLower.end(), msgLower.begin(), ::tolower);
				if (msgLower.find(searchLower) == std::string::npos) continue;
			}

			ImVec4 col = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
			if (entry.level == 1) col = ImVec4(1.0f, 0.85f, 0.2f, 1.0f);      // Warning Yellow
			else if (entry.level == 2) col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Error Red
			else if (entry.level == 3) col = ImVec4(0.3f, 1.0f, 0.4f, 1.0f);  // Success Green

			ImGui::TextColored(col, "[%s] %s", entry.category.c_str(), entry.message.c_str());
		}

		if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
		{
			ImGui::SetScrollHereY(1.0f);
		}
		ImGui::EndChild();

		// Command line input
		ImGui::PushItemWidth(-1.0f);
		if (ImGui::InputTextWithHint("##LogCommandInput", "Enter Lua / Engine command and press Enter...", m_commandInputBuf, sizeof(m_commandInputBuf), ImGuiInputTextFlags_EnterReturnsTrue))
		{
			if (m_commandInputBuf[0] != '\0')
			{
				AddLog("Command", m_commandInputBuf, 0);
				std::string cmd = m_commandInputBuf;
				wi::lua::RunText(cmd);
				m_commandInputBuf[0] = '\0';
			}
		}
		ImGui::PopItemWidth();
	}
}
