#pragma once
#include "WickedEngine.h"
#include "ImGui/imgui.h"
#include "ImGui/ImGuizmo.h"

class EditorComponent;

namespace EunoiaUI
{
	class ViewportOverlay
	{
	public:
		ViewportOverlay();
		~ViewportOverlay() = default;

		void Render(EditorComponent& editor, wi::scene::Scene& scene, float x, float y, float width, float height);

		ImGuizmo::OPERATION GetCurrentGizmoOperation() const { return m_gizmoOperation; }
		void SetGizmoOperation(ImGuizmo::OPERATION op) { m_gizmoOperation = op; }

		ImGuizmo::MODE GetCurrentGizmoMode() const { return m_gizmoMode; }
		void SetGizmoMode(ImGuizmo::MODE mode) { m_gizmoMode = mode; }

		bool GetUseSnap() const { return m_useSnap; }
		void SetUseSnap(bool snap) { m_useSnap = snap; }

		float GetSnapTranslation() const { return m_snapTranslationVal; }
		void SetSnapTranslation(float snap) { m_snapTranslationVal = snap; m_snapTranslation[0] = snap; m_snapTranslation[1] = snap; m_snapTranslation[2] = snap; }

		float GetSnapRotation() const { return m_snapRotation; }
		void SetSnapRotation(float snap) { m_snapRotation = snap; }

		float GetSnapScale() const { return m_snapScale; }
		void SetSnapScale(float snap) { m_snapScale = snap; }

	private:
		ImGuizmo::OPERATION m_gizmoOperation = ImGuizmo::TRANSLATE;
		ImGuizmo::MODE m_gizmoMode = ImGuizmo::WORLD;
		bool m_useSnap = false;
		float m_snapTranslationVal = 1.0f;
		float m_snapTranslation[3] = { 1.0f, 1.0f, 1.0f };
		float m_snapRotation = 15.0f;
		float m_snapScale = 0.25f;

		float m_cameraSpeed = 10.0f;
		int m_viewMode = 0; // 0 = Lit, 1 = Wireframe, 2 = Unlit

		void DrawOverlayControls(EditorComponent& editor, float x, float y, float width, float height);
		void DrawGizmo(EditorComponent& editor, wi::scene::Scene& scene, float x, float y, float width, float height);
		void HandleDropTarget(EditorComponent& editor, wi::scene::Scene& scene, float x, float y, float width, float height);
		void DrawCameraPIP(EditorComponent& editor, wi::scene::Scene& scene, float x, float y, float width, float height);
	};
}
