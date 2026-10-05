#pragma once
#include "WickedEngine.h"
#include "EunoiaTheme.h"
#include "EunoiaImGuiBackend.h"
#include "EunoiaProjectBrowser.h"
#include "EunoiaOutlinerPanel.h"
#include "EunoiaDetailsPanel.h"
#include "EunoiaBottomDrawer.h"
#include "EunoiaViewportOverlay.h"

class Editor;
class EditorComponent;

namespace EunoiaUI
{
	class UIManager
	{
	public:
		UIManager();
		~UIManager() = default;

		void Initialize(HWND window, Editor* mainEditor);
		void Update(EditorComponent& editor, wi::scene::Scene& scene);
		void Render(EditorComponent& editor, wi::scene::Scene& scene, wi::graphics::CommandList cmd);

		void ToggleProjectBrowser() { m_showProjectBrowser = !m_showProjectBrowser; }
		bool IsProjectBrowserOpen() const { return m_showProjectBrowser; }

		bool IsPlayMode() const { return m_isPlayMode; }
		void StartPlayMode(EditorComponent& editor, wi::scene::Scene& scene);
		void StopPlayMode(EditorComponent& editor, wi::scene::Scene& scene);

		// Layout rectangles
		struct ViewportRect
		{
			float x = 0;
			float y = 0;
			float width = 0;
			float height = 0;
		};
		ViewportRect GetViewportRect() const { return m_viewportRect; }

		ProjectBrowser& GetProjectBrowser() { return m_projectBrowser; }
		BottomDrawer& GetBottomDrawer() { return m_bottomDrawer; }

	private:
		Editor* m_mainEditor = nullptr;
		HWND m_window = nullptr;

		ProjectBrowser m_projectBrowser;
		OutlinerPanel m_outlinerPanel;
		DetailsPanel m_detailsPanel;
		BottomDrawer m_bottomDrawer;
		ViewportOverlay m_viewportOverlay;

		bool m_showProjectBrowser = false;
		bool m_showOutliner = true;
		bool m_showDetails = true;
		bool m_showBottomDrawer = true;
		bool m_showViewportOverlay = true;
		bool m_showGraphicsSettings = false;

		void RenderGraphicsSettingsModal(EditorComponent& editor);

		// Layout sizing
		float m_topBarHeight = 64.0f;
		float m_leftSidebarWidth = 280.0f;
		float m_rightSidebarWidth = 320.0f;
		ViewportRect m_viewportRect;

		// Play simulation state
		bool m_isPlayMode = false;
		wi::Archive m_playModeSceneBackup;

		void RenderCustomTitleBar(EditorComponent& editor, wi::scene::Scene& scene);
		void RenderTopMenuBar(EditorComponent& editor, wi::scene::Scene& scene);
		void RenderLayoutSplitters(float screenW, float screenH);
	};
}
