#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <functional>

namespace EunoiaUI
{
	struct ProjectEntry
	{
		std::string name;
		std::string path;
		std::string lastOpened;
	};

	class ProjectBrowser
	{
	public:
		ProjectBrowser();
		~ProjectBrowser() = default;

		void LoadRecentProjects();
		void SaveRecentProjects();

		// Renders the Project Browser modal / window
		// Returns true if a project was selected or created
		bool Render(bool* p_open = nullptr);

		bool CreateNewProject(const std::string& parentDir, const std::string& projName);
		bool OpenProject(const std::filesystem::path& projPath);

		const std::filesystem::path& GetActiveProjectPath() const { return m_activeProjectPath; }
		const std::string& GetActiveProjectName() const { return m_activeProjectName; }
		bool HasActiveProject() const { return !m_activeProjectPath.empty(); }

		void SetOnProjectLoadedCallback(std::function<void(const std::filesystem::path&)> callback)
		{
			m_onProjectLoaded = callback;
		}

		static std::string ShowSelectFolderDialog(void* owner = nullptr, const std::string& title = "Select Project Location");
		static std::string ShowOpenFileDialog(const char* filter = "Level Files (*.eulevel;*.wiscene)\0*.eulevel;*.wiscene\0All Files (*.*)\0*.*\0");

	private:
		int m_activeTab = 0; // 0 = Recent Projects, 1 = New Project
		char m_newProjectNameBuf[64] = "MyProject";
		char m_newProjectPathBuf[260] = "C:\\Projects\\Eunoia-Engine\\Projects";
		char m_searchFilterBuf[64] = "";
		int m_selectedProjectIndex = 0;

		std::vector<ProjectEntry> m_recentProjects;
		std::filesystem::path m_activeProjectPath;
		std::string m_activeProjectName;

		std::function<void(const std::filesystem::path&)> m_onProjectLoaded;
	};
}
