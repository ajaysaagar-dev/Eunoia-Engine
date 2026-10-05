#include "stdafx.h"
#include "EunoiaProjectBrowser.h"
#include "EunoiaTheme.h"
#include "ImGui/imgui.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <shlobj.h>
#include <commdlg.h>
#include <algorithm>


namespace EunoiaUI
{
	ProjectBrowser::ProjectBrowser()
	{
		std::error_code ec;
		std::filesystem::path defaultDir = "C:\\Projects\\Eunoia-Engine\\Projects";
		if (!std::filesystem::exists(defaultDir, ec))
		{
			std::filesystem::path alt = std::filesystem::current_path() / "Projects";
			if (std::filesystem::exists(alt, ec))
			{
				defaultDir = alt;
			}
			else
			{
				std::filesystem::create_directories(defaultDir, ec);
			}
		}
		strncpy_s(m_newProjectPathBuf, defaultDir.string().c_str(), sizeof(m_newProjectPathBuf) - 1);
		LoadRecentProjects();
	}

	void ProjectBrowser::LoadRecentProjects()
	{
		m_recentProjects.clear();
		std::ifstream file("projects.json");
		if (!file.is_open()) return;

		std::string line;
		ProjectEntry current;
		while (std::getline(file, line))
		{
			// Simple key-value / line parsing
			size_t col = line.find('=');
			if (col != std::string::npos)
			{
				std::string key = line.substr(0, col);
				std::string val = line.substr(col + 1);
				if (key == "name") current.name = val;
				else if (key == "path") current.path = val;
				else if (key == "lastOpened") {
					current.lastOpened = val;
					if (!current.name.empty() && !current.path.empty())
					{
						std::error_code ec;
						if (std::filesystem::exists(current.path, ec))
						{
							m_recentProjects.push_back(current);
						}
					}
					current = ProjectEntry();
				}
			}
		}
	}

	void ProjectBrowser::SaveRecentProjects()
	{
		std::ofstream file("projects.json");
		if (!file.is_open()) return;

		for (const auto& p : m_recentProjects)
		{
			file << "name=" << p.name << "\n";
			file << "path=" << p.path << "\n";
			file << "lastOpened=" << p.lastOpened << "\n";
		}
	}

	std::string ProjectBrowser::ShowSelectFolderDialog(void* owner, const std::string& title)
	{
		std::string selectedPath;
		BROWSEINFOA bi = { 0 };
		bi.hwndOwner = (HWND)owner;
		bi.lpszTitle = title.c_str();
		bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

		LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
		if (pidl != 0)
		{
			char path[MAX_PATH];
			if (SHGetPathFromIDListA(pidl, path))
			{
				selectedPath = path;
			}
			CoTaskMemFree(pidl);
		}
		return selectedPath;
	}

	std::string ProjectBrowser::ShowOpenFileDialog(const char* filter)
	{
		char filename[MAX_PATH] = "";
		OPENFILENAMEA ofn = { 0 };
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFilter = filter;
		ofn.lpstrFile = filename;
		ofn.nMaxFile = MAX_PATH;
		ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

		if (GetOpenFileNameA(&ofn))
		{
			return std::string(filename);
		}
		return "";
	}

	std::string ProjectBrowser::ShowSaveFileDialog(const char* filter, const char* defaultName)
	{
		char filename[MAX_PATH] = "";
		if (defaultName && defaultName[0] != '\0')
		{
			strncpy_s(filename, defaultName, sizeof(filename) - 1);
		}
		OPENFILENAMEA ofn = { 0 };
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFilter = filter;
		ofn.lpstrFile = filename;
		ofn.nMaxFile = MAX_PATH;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

		if (GetSaveFileNameA(&ofn))
		{
			return std::string(filename);
		}
		return "";
	}

	bool ProjectBrowser::CreateNewProject(const std::string& parentDir, const std::string& projName)
	{
		if (parentDir.empty() || projName.empty()) return false;

		std::error_code ec;
		std::filesystem::path projRoot = std::filesystem::path(parentDir) / projName;
		std::filesystem::create_directories(projRoot / "Content", ec);
		std::filesystem::create_directories(projRoot / "Levels", ec);

		// Record timestamp
		auto now = std::chrono::system_clock::now();
		auto in_time_t = std::chrono::system_clock::to_time_t(now);
		char timeBuf[64];
		ctime_s(timeBuf, sizeof(timeBuf), &in_time_t);
		std::string timeStr(timeBuf);
		if (!timeStr.empty() && timeStr.back() == '\n') timeStr.pop_back();

		ProjectEntry entry;
		entry.name = projName;
		entry.path = projRoot.string();
		entry.lastOpened = timeStr;

		// Add or update in recent
		m_recentProjects.erase(
			std::remove_if(m_recentProjects.begin(), m_recentProjects.end(),
				[&](const ProjectEntry& p) { return p.path == entry.path; }),
			m_recentProjects.end());
		m_recentProjects.insert(m_recentProjects.begin(), entry);
		SaveRecentProjects();

		return OpenProject(projRoot);
	}

	bool ProjectBrowser::OpenProject(const std::filesystem::path& projPath)
	{
		std::error_code ec;
		if (!std::filesystem::exists(projPath, ec)) return false;

		m_activeProjectPath = projPath;
		m_activeProjectName = projPath.filename().string();

		// Update timestamp in recent list
		for (auto& p : m_recentProjects)
		{
			if (p.path == projPath.string())
			{
				auto now = std::chrono::system_clock::now();
				auto in_time_t = std::chrono::system_clock::to_time_t(now);
				char timeBuf[64];
				ctime_s(timeBuf, sizeof(timeBuf), &in_time_t);
				std::string timeStr(timeBuf);
				if (!timeStr.empty() && timeStr.back() == '\n') timeStr.pop_back();
				p.lastOpened = timeStr;
				break;
			}
		}
		SaveRecentProjects();

		if (m_onProjectLoaded)
		{
			m_onProjectLoaded(projPath);
		}

		return true;
	}

	bool ProjectBrowser::Render(bool* p_open)
	{
		ImGuiViewport* vp = ImGui::GetMainViewport();

		// 1. Semi-transparent backdrop
		ImDrawList* bgDrawList = ImGui::GetForegroundDrawList();
		bgDrawList->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(8, 12, 18, 220));

		// 2. Centered Window
		float winW = 840.0f;
		float winH = 540.0f;
		ImVec2 centerPos(vp->Pos.x + (vp->Size.x - winW) * 0.5f, vp->Pos.y + (vp->Size.y - winH) * 0.5f);
		ImGui::SetNextWindowPos(centerPos, ImGuiCond_Appearing);
		ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 18.0f));
		ImGui::PushStyleColor(ImGuiCol_Border, Colors::AccentBlue);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, Colors::DarkBg);

		bool projectSelected = false;
		bool windowOpen = true;

		if (ImGui::Begin("Project Browser###EunoiaProjectBrowserModal", p_open ? p_open : &windowOpen, flags))
		{
			// Title Header
			ImGui::TextColored(Colors::AccentBlue, "EUNOIA ENGINE");
			ImGui::SameLine();
			ImGui::TextDisabled("- Project Browser");

			if (HasActiveProject() && p_open)
			{
				ImGui::SameLine(winW - 150.0f);
				if (ImGui::SmallButton("< Return to Editor"))
				{
					*p_open = false;
				}
			}

			ImGui::TextWrapped("Select a previously created project, or configure and create a new empty game project.");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// Tabs: [ Recent Projects ] | [ New Project ]
			bool tab0Active = (m_activeTab == 0);
			if (tab0Active) ImGui::PushStyleColor(ImGuiCol_Button, Colors::AccentBlue);
			if (ImGui::Button("Recent Projects", ImVec2(150, 32))) m_activeTab = 0;
			if (tab0Active) ImGui::PopStyleColor();

			ImGui::SameLine();
			bool tab1Active = (m_activeTab == 1);
			if (tab1Active) ImGui::PushStyleColor(ImGuiCol_Button, Colors::AccentBlue);
			if (ImGui::Button("+ New Project", ImVec2(150, 32))) m_activeTab = 1;
			if (tab1Active) ImGui::PopStyleColor();

			ImGui::SameLine(winW - 140.0f);
			if (ImGui::Button("Browse Folder...", ImVec2(120, 32)))
			{
				std::string folder = ShowSelectFolderDialog(nullptr, "Select Existing Project Folder");
				if (!folder.empty())
				{
					OpenProject(folder);
					projectSelected = true;
					if (p_open) *p_open = false;
				}
			}

			ImGui::Spacing();

			if (m_activeTab == 0)
			{
				// Recent projects tab
				ImGui::InputTextWithHint("##ProjectSearch", "Search Projects...", m_searchFilterBuf, sizeof(m_searchFilterBuf));
				ImGui::Spacing();

				std::string searchLower = m_searchFilterBuf;
				std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

				ImGui::BeginChild("RecentProjectsList", ImVec2(0, -50), true);

				if (m_recentProjects.empty())
				{
					ImGui::TextDisabled("No recent projects found. Create a new one or browse a folder.");
				}
				else
				{
					for (int i = 0; i < (int)m_recentProjects.size(); ++i)
					{
						const auto& p = m_recentProjects[i];
						std::string nameLower = p.name;
						std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

						if (!searchLower.empty() && nameLower.find(searchLower) == std::string::npos)
							continue;

						ImGui::PushID(i);
						bool isSelected = (m_selectedProjectIndex == i);

						if (ImGui::Selectable("##projSelectable", isSelected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 48)))
						{
							m_selectedProjectIndex = i;
							if (ImGui::IsMouseDoubleClicked(0))
							{
								OpenProject(p.path);
								projectSelected = true;
								if (p_open) *p_open = false;
							}
						}

						// Card contents on top of selectable
						ImVec2 itemMin = ImGui::GetItemRectMin();
						ImDrawList* drawList = ImGui::GetWindowDrawList();
						drawList->AddRectFilled(itemMin, ImVec2(itemMin.x + 4, itemMin.y + 48), IM_COL32(30, 144, 255, 255));

						ImGui::SameLine(18.0f);
						ImGui::BeginGroup();
						ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", p.name.c_str());
						ImGui::TextDisabled("%s", p.path.c_str());
						ImGui::EndGroup();

						ImGui::SameLine(winW - 240.0f);
						ImGui::TextDisabled("%s", p.lastOpened.c_str());

						ImGui::PopID();
					}
				}
				ImGui::EndChild();

				// Bottom toolbar
				if (!m_recentProjects.empty() && m_selectedProjectIndex >= 0 && m_selectedProjectIndex < (int)m_recentProjects.size())
				{
					if (ImGui::Button("Open Selected Project", ImVec2(180, 34)))
					{
						OpenProject(m_recentProjects[m_selectedProjectIndex].path);
						projectSelected = true;
						if (p_open) *p_open = false;
					}
					ImGui::SameLine();
					if (ImGui::Button("Remove from Recent", ImVec2(160, 34)))
					{
						m_recentProjects.erase(m_recentProjects.begin() + m_selectedProjectIndex);
						if (m_selectedProjectIndex >= (int)m_recentProjects.size())
							m_selectedProjectIndex = (int)m_recentProjects.size() - 1;
						SaveRecentProjects();
					}
				}
			}
			else
			{
				// New Project Tab
				ImGui::BeginChild("NewProjectForm", ImVec2(0, -60), true);

				ImGui::TextColored(Colors::AccentBlue, "Configure New Project");
				ImGui::Spacing();

				ImGui::Text("Project Name:");
				ImGui::SetNextItemWidth(400.0f);
				ImGui::InputText("##NewProjName", m_newProjectNameBuf, sizeof(m_newProjectNameBuf));

				ImGui::Spacing();
				ImGui::Text("Project Location:");
				ImGui::SetNextItemWidth(winW - 180.0f);
				ImGui::InputText("##NewProjLocation", m_newProjectPathBuf, sizeof(m_newProjectPathBuf));
				ImGui::SameLine();
				if (ImGui::Button("Browse...", ImVec2(100, 0)))
				{
					std::string chosen = ShowSelectFolderDialog(nullptr, "Choose Parent Folder");
					if (!chosen.empty())
					{
						strncpy_s(m_newProjectPathBuf, chosen.c_str(), sizeof(m_newProjectPathBuf) - 1);
					}
				}

				ImGui::Spacing();
				std::filesystem::path fullTarget = std::filesystem::path(m_newProjectPathBuf) / m_newProjectNameBuf;
				ImGui::TextDisabled("New Project will be created at: %s", fullTarget.string().c_str());

				ImGui::EndChild();

				// Create button
				if (ImGui::Button("Create Project", ImVec2(180, 38)))
				{
					if (CreateNewProject(m_newProjectPathBuf, m_newProjectNameBuf))
					{
						projectSelected = true;
						if (p_open) *p_open = false;
					}
				}
			}
		}
		ImGui::End();

		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(3);

		return projectSelected;
	}
}
