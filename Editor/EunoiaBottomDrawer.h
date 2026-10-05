#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include "wiResourceManager.h"

class EditorComponent;

namespace EunoiaUI
{
	struct LogEntry
	{
		std::string category;
		std::string message;
		int level; // 0=Info, 1=Warning, 2=Error, 3=Success
	};

	class BottomDrawer
	{
	public:
		BottomDrawer();
		~BottomDrawer() = default;

		void Render(EditorComponent& editor, float width, float currentH, float posX, float posY);

		void SetContentRoot(const std::filesystem::path& root);
		const std::filesystem::path& GetContentRoot() const { return m_contentRoot; }

		void AddLog(const std::string& category, const std::string& message, int level = 0);
		void ClearLogs();

		bool IsOpen() const { return m_isOpen; }
		void SetOpen(bool open) { m_isOpen = open; }
		float GetCurrentHeight() const { return m_isOpen ? m_expandedHeight : m_collapsedHeight; }
		float GetExpandedHeight() const { return m_expandedHeight; }
		void SetExpandedHeight(float h) { m_expandedHeight = h; }

	private:
		int m_activeTab = 0; // 0 = Content Browser, 1 = Output Log
		bool m_isOpen = true;
		float m_expandedHeight = 260.0f;
		float m_collapsedHeight = 36.0f;

		// Content Browser State
		std::filesystem::path m_contentRoot;
		std::filesystem::path m_currentDir;
		char m_searchFilterBuf[64] = "";
		int m_typeFilter = 0; // 0=All, 1=3D Models, 2=Textures, 3=Materials, 4=Scripts, 5=Audio
		float m_thumbnailSize = 80.0f;
		std::string m_selectedAssetPath;
		std::string m_hoveredAssetPath;

		// Modals State
		bool m_showNewFolderModal = false;
		char m_newFolderNameBuf[64] = "NewFolder";

		bool m_showRenameModal = false;
		std::string m_renameTargetPath;
		char m_renameNameBuf[128] = "";

		bool m_showDeleteModal = false;
		std::string m_deleteTargetPath;

		bool m_showNewScriptModal = false;
		char m_newScriptNameBuf[64] = "NewScript";

		bool m_showNewMaterialModal = false;
		char m_newMaterialNameBuf[64] = "NewMaterial";

		bool m_showNewLevelModal = false;
		char m_newLevelNameBuf[64] = "NewLevel";

		bool m_showNewSkyboxModal = false;
		char m_newSkyboxNameBuf[64] = "Skybox_New";

		// Output Log State
		std::vector<LogEntry> m_logs;
		bool m_showInfo = true;
		bool m_showWarnings = true;
		bool m_showErrors = true;
		bool m_autoScroll = true;
		char m_logSearchBuf[64] = "";
		char m_commandInputBuf[256] = "";

		// Content Browser Icons (from original Eunoia Engine)
		wi::Resource m_iconFolder;
		wi::Resource m_iconLevel;
		wi::Resource m_icon3D;
		wi::Resource m_iconMaterial;
		wi::Resource m_iconTexture;
		wi::Resource m_iconBehaviour;
		wi::Resource m_iconSkybox;
		wi::Resource m_iconVisualBehaviour;
		bool m_iconsLoaded = false;
		void LoadContentBrowserIcons();

		void RenderContentBrowser(EditorComponent& editor);
		void RenderOutputLog(EditorComponent& editor);
		void DrawFolderTreeNode(const std::filesystem::path& dir);
		void HandleMoveItem(const std::string& srcPath, const std::filesystem::path& destDir);
		void HandleDuplicateItem(const std::filesystem::path& path);
		void HandleCreateScript(const std::string& name);
		void HandleCreateMaterial(const std::string& name);
		void HandleCreateLevel(const std::string& name, EditorComponent& editor);
		void HandleCreateSkybox(const std::string& name);
		bool PassesTypeFilter(const std::filesystem::path& path, bool isDir) const;
	};
}
