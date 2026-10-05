#pragma once
#include "WickedEngine.h"

class EditorComponent;

namespace EunoiaUI
{
	class DetailsPanel
	{
	public:
		DetailsPanel() = default;
		~DetailsPanel() = default;

		void Render(EditorComponent& editor, wi::scene::Scene& scene, float width, float height, float posX, float posY);

	private:
		bool m_uniformScaleLock = false;

		void DrawTransformSection(wi::scene::Scene& scene, wi::ecs::Entity entity);
		void DrawMeshSection(wi::scene::Scene& scene, wi::ecs::Entity entity);
		void DrawMaterialSection(wi::scene::Scene& scene, wi::ecs::Entity entity);
		void DrawLightSection(wi::scene::Scene& scene, wi::ecs::Entity entity);
		void DrawCameraSection(wi::scene::Scene& scene, wi::ecs::Entity entity);
		void DrawPhysicsSection(wi::scene::Scene& scene, wi::ecs::Entity entity);
		void DrawSoundSection(wi::scene::Scene& scene, wi::ecs::Entity entity);
		void DrawWeatherSection(wi::scene::Scene& scene, wi::ecs::Entity entity);
		void DrawAddComponentMenu(wi::scene::Scene& scene, wi::ecs::Entity entity);
	};
}
