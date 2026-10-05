#pragma once
#include "WickedEngine.h"
#include <string>
#include <unordered_set>

class EditorComponent;

namespace EunoiaUI
{
	class OutlinerPanel
	{
	public:
		OutlinerPanel() = default;
		~OutlinerPanel() = default;

		void Render(EditorComponent& editor, wi::scene::Scene& scene, float width, float height, float posX, float posY);

		const std::string& GetFilter() const { return m_filter; }

		static wi::ecs::Entity CreateActor(EditorComponent& editor, wi::scene::Scene& scene, const std::string& type);
		static void DrawAddActorMenuItems(EditorComponent& editor, wi::scene::Scene& scene);

	private:
		char m_filterBuf[64] = "";
		std::string m_filter;

		bool m_isRenaming = false;
		wi::ecs::Entity m_renamingEntity = wi::ecs::INVALID_ENTITY;
		char m_renameBuf[128] = "";

		void DrawEntityNode(EditorComponent& editor, wi::scene::Scene& scene, wi::ecs::Entity entity, std::unordered_set<wi::ecs::Entity>& visited);
		void DrawContextMenu(EditorComponent& editor, wi::scene::Scene& scene, wi::ecs::Entity targetEntity);
	};
}
