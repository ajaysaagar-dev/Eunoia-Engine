#include "stdafx.h"
#include "EunoiaOutlinerPanel.h"
#include "EunoiaTheme.h"
#include "Editor.h"
#include "ImGui/imgui.h"
#include <algorithm>

using namespace wi::ecs;
using namespace wi::scene;

namespace EunoiaUI
{
	void OutlinerPanel::Render(EditorComponent& editor, Scene& scene, float width, float height, float posX, float posY)
	{
		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
		if (ImGui::Begin("Outliner###EunoiaOutlinerWindow", nullptr, flags))
		{
			// Top Add Button & Search bar
			if (ImGui::Button("+ Add", ImVec2(60, 0)))
			{
				ImGui::OpenPopup("AddActorTopPopup");
			}
			if (ImGui::BeginPopup("AddActorTopPopup"))
			{
				DrawAddActorMenuItems(editor, scene);
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			ImGui::PushItemWidth(-1.0f);
			if (ImGui::InputTextWithHint("##OutlinerFilter", "Search Actors...", m_filterBuf, sizeof(m_filterBuf)))
			{
				m_filter = m_filterBuf;
			}
			ImGui::PopItemWidth();
			ImGui::Separator();

			// Context menu on background
			if (ImGui::BeginPopupContextWindow("OutlinerBgContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
			{
				DrawContextMenu(editor, scene, INVALID_ENTITY);
				ImGui::EndPopup();
			}

			// Actor list
			ImGui::BeginChild("OutlinerTreeArea", ImVec2(0, 0), false);

			std::string filterLower = m_filter;
			std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

			std::unordered_set<Entity> visited;

			// First, find all root entities (entities that do NOT have a parent HierarchyComponent)
			wi::vector<Entity> rootEntities;
			for (size_t i = 0; i < scene.names.GetCount(); ++i)
			{
				Entity ent = scene.names.GetEntity(i);
				const HierarchyComponent* hier = scene.hierarchy.GetComponent(ent);
				if (!hier || hier->parentID == INVALID_ENTITY)
				{
					rootEntities.push_back(ent);
				}
			}

			// Render root nodes recursively
			for (Entity ent : rootEntities)
			{
				DrawEntityNode(editor, scene, ent, visited);
			}

			ImGui::EndChild();
		}
		ImGui::End();

		ImGui::PopStyleVar(2);
	}

	void OutlinerPanel::DrawEntityNode(EditorComponent& editor, Scene& scene, Entity entity, std::unordered_set<Entity>& visited)
	{
		if (visited.count(entity)) return;
		visited.insert(entity);

		const NameComponent* nameComp = scene.names.GetComponent(entity);
		std::string entityName = nameComp ? nameComp->name : ("Entity " + std::to_string(entity));

		// Check filter
		if (!m_filter.empty())
		{
			std::string nameLower = entityName;
			std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
			std::string filterLower = m_filter;
			std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
			if (nameLower.find(filterLower) == std::string::npos)
			{
				// If not matching, still check if children match
				// For now simple skip
			}
		}

		// Find children
		wi::vector<Entity> children;
		for (size_t i = 0; i < scene.hierarchy.GetCount(); ++i)
		{
			const HierarchyComponent& hier = scene.hierarchy[i];
			if (hier.parentID == entity)
			{
				children.push_back(scene.hierarchy.GetEntity(i));
			}
		}

		bool hasChildren = !children.empty();
		bool isSelected = editor.IsSelected(entity);

		ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (isSelected) nodeFlags |= ImGuiTreeNodeFlags_Selected;
		if (!hasChildren) nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

		ImGui::PushID((int)entity);

		// Eye icon / visibility toggle button on left
		const LayerComponent* layer = scene.layers.GetComponent(entity);
		bool isVisible = layer ? (layer->layerMask != 0) : true;
		if (ImGui::SmallButton(isVisible ? "[O]" : "[ ]"))
		{
			LayerComponent* layerMut = scene.layers.GetComponent(entity);
			if (!layerMut)
			{
				scene.layers.Create(entity);
				layerMut = scene.layers.GetComponent(entity);
			}
			if (layerMut)
			{
				layerMut->layerMask = isVisible ? 0 : ~0u;
			}
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Toggle Visibility");
		}
		ImGui::SameLine();

		// Badge indicating type
		const char* typeBadge = "[T]";
		if (scene.objects.Contains(entity)) typeBadge = "[M]";
		else if (scene.lights.Contains(entity)) typeBadge = "[L]";
		else if (scene.cameras.Contains(entity)) typeBadge = "[C]";
		else if (scene.sounds.Contains(entity)) typeBadge = "[S]";

		ImGui::TextDisabled("%s", typeBadge);
		ImGui::SameLine();

		// Node text / rename input
		bool nodeOpen = false;
		if (m_isRenaming && m_renamingEntity == entity)
		{
			ImGui::SetNextItemWidth(200.0f);
			if (ImGui::InputText("##RenameActor", m_renameBuf, sizeof(m_renameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
			{
				NameComponent* nc = scene.names.GetComponent(entity);
				if (nc) nc->name = m_renameBuf;
				m_isRenaming = false;
				m_renamingEntity = INVALID_ENTITY;
			}
			if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0))
			{
				m_isRenaming = false;
				m_renamingEntity = INVALID_ENTITY;
			}
		}
		else
		{
			nodeOpen = ImGui::TreeNodeEx((void*)(intptr_t)entity, nodeFlags, "%s", entityName.c_str());

			if (ImGui::IsItemClicked(0))
			{
				if (ImGui::GetIO().KeyCtrl)
				{
					if (editor.IsSelected(entity))
					{
						// already handled or keep
					}
					else
					{
						editor.AddSelected(entity);
					}
				}
				else
				{
					editor.ClearSelected();
					editor.AddSelected(entity);
				}
			}

			// Context menu for this entity
			if (ImGui::BeginPopupContextItem())
			{
				editor.ClearSelected();
				editor.AddSelected(entity);
				DrawContextMenu(editor, scene, entity);
				ImGui::EndPopup();
			}

			// Drag and drop source (for reparenting)
			if (ImGui::BeginDragDropSource())
			{
				ImGui::SetDragDropPayload("OUTLINER_ACTOR_ENTITY", &entity, sizeof(Entity));
				ImGui::Text("Reparent %s", entityName.c_str());
				ImGui::EndDragDropSource();
			}

			// Drag and drop target (to receive children)
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OUTLINER_ACTOR_ENTITY"))
				{
					Entity droppedEntity = *(const Entity*)payload->Data;
					if (droppedEntity != entity)
					{
						scene.Component_Attach(droppedEntity, entity);
					}
				}
				ImGui::EndDragDropTarget();
			}
		}

		if (hasChildren && nodeOpen)
		{
			for (Entity child : children)
			{
				DrawEntityNode(editor, scene, child, visited);
			}
			ImGui::TreePop();
		}

		ImGui::PopID();
	}

	void OutlinerPanel::DrawContextMenu(EditorComponent& editor, Scene& scene, Entity targetEntity)
	{
		if (targetEntity != INVALID_ENTITY)
		{
			const NameComponent* nc = scene.names.GetComponent(targetEntity);
			std::string name = nc ? nc->name : "Actor";
			ImGui::TextDisabled("Actor: %s", name.c_str());
			ImGui::Separator();

			if (ImGui::MenuItem("Focus (F)"))
			{
				editor.FocusCameraOnSelected();
			}
			if (ImGui::MenuItem("Rename (F2)"))
			{
				m_isRenaming = true;
				m_renamingEntity = targetEntity;
				strncpy_s(m_renameBuf, name.c_str(), sizeof(m_renameBuf) - 1);
			}
			if (ImGui::MenuItem("Duplicate (Ctrl+D)"))
			{
				Entity dup = scene.Entity_Duplicate(targetEntity);
				editor.ClearSelected();
				editor.AddSelected(dup);
			}
			if (ImGui::MenuItem("Detach from Parent"))
			{
				scene.Component_Detach(targetEntity);
			}
			if (ImGui::MenuItem("Delete (Del)"))
			{
				scene.Entity_Remove(targetEntity);
				editor.ClearSelected();
			}
			ImGui::Separator();
		}

		if (ImGui::BeginMenu("+ Add Actor"))
		{
			DrawAddActorMenuItems(editor, scene);
			ImGui::EndMenu();
		}
	}

	void OutlinerPanel::DrawAddActorMenuItems(EditorComponent& editor, Scene& scene)
	{
		if (ImGui::MenuItem("Empty Actor")) CreateActor(editor, scene, "Empty");
		ImGui::Separator();

		if (ImGui::BeginMenu("3D Meshes"))
		{
			if (ImGui::MenuItem("Cube")) CreateActor(editor, scene, "Cube");
			if (ImGui::MenuItem("Sphere")) CreateActor(editor, scene, "Sphere");
			if (ImGui::MenuItem("Plane")) CreateActor(editor, scene, "Plane");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Lights"))
		{
			if (ImGui::MenuItem("Directional Light")) CreateActor(editor, scene, "DirectionalLight");
			if (ImGui::MenuItem("Point Light")) CreateActor(editor, scene, "PointLight");
			if (ImGui::MenuItem("Spot Light")) CreateActor(editor, scene, "SpotLight");
			if (ImGui::MenuItem("Environment Probe")) CreateActor(editor, scene, "EnvironmentProbe");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Cameras"))
		{
			if (ImGui::MenuItem("Perspective Camera Actor")) CreateActor(editor, scene, "Camera");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Visual Effects"))
		{
			if (ImGui::MenuItem("Particle Emitter")) CreateActor(editor, scene, "Emitter");
			if (ImGui::MenuItem("Hair Particle System")) CreateActor(editor, scene, "Hair");
			if (ImGui::MenuItem("Decal Projector")) CreateActor(editor, scene, "Decal");
			if (ImGui::MenuItem("Force Field")) CreateActor(editor, scene, "ForceField");
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Audio & Media"))
		{
			if (ImGui::MenuItem("Sound Actor")) CreateActor(editor, scene, "Sound");
			if (ImGui::MenuItem("Video Player Actor")) CreateActor(editor, scene, "Video");
			ImGui::EndMenu();
		}
	}

	Entity OutlinerPanel::CreateActor(EditorComponent& editor, Scene& scene, const std::string& type)
	{
		Entity newEnt = INVALID_ENTITY;
		XMFLOAT3 spawnPos = editor.GetPositionInFrontOfCamera();

		if (type == "Cube")
		{
			newEnt = scene.Entity_CreateCube("Cube");
		}
		else if (type == "Sphere")
		{
			newEnt = scene.Entity_CreateSphere("Sphere");
		}
		else if (type == "Plane")
		{
			newEnt = scene.Entity_CreatePlane("Plane");
		}
		else if (type == "DirectionalLight")
		{
			newEnt = scene.Entity_CreateLight("Directional Light", spawnPos, XMFLOAT3(1, 1, 1), 2.0f, 100.0f, LightComponent::LightType::DIRECTIONAL);
		}
		else if (type == "PointLight")
		{
			newEnt = scene.Entity_CreateLight("Point Light", spawnPos, XMFLOAT3(1, 1, 1), 5.0f, 10.0f, LightComponent::LightType::POINT);
		}
		else if (type == "SpotLight")
		{
			newEnt = scene.Entity_CreateLight("Spot Light", spawnPos, XMFLOAT3(1, 1, 1), 5.0f, 10.0f, LightComponent::LightType::SPOT);
		}
		else if (type == "EnvironmentProbe")
		{
			newEnt = scene.Entity_CreateEnvironmentProbe("Environment Probe", spawnPos);
		}
		else if (type == "Camera")
		{
			newEnt = scene.Entity_CreateCamera("Camera Actor", 1920, 1080, 0.1f, 1000.0f);
		}
		else if (type == "Emitter")
		{
			newEnt = scene.Entity_CreateEmitter("Particle Emitter", spawnPos);
		}
		else if (type == "Hair")
		{
			newEnt = scene.Entity_CreateHair("Hair Particles", spawnPos);
		}
		else if (type == "Decal")
		{
			newEnt = scene.Entity_CreateDecal("Decal", "");
		}
		else if (type == "ForceField")
		{
			newEnt = scene.Entity_CreateForce("Force Field", spawnPos);
		}
		else if (type == "Sound")
		{
			newEnt = scene.Entity_CreateSound("Sound Actor", "", spawnPos);
		}
		else if (type == "Video")
		{
			newEnt = scene.Entity_CreateVideo("Video Actor", "");
		}
		else if (type == "Empty")
		{
			newEnt = scene.Entity_CreateTransform("Empty Actor");
		}

		if (newEnt != INVALID_ENTITY)
		{
			TransformComponent* trans = scene.transforms.GetComponent(newEnt);
			if (trans)
			{
				trans->translation_local = spawnPos;
				trans->SetDirty();
			}
			editor.ClearSelected();
			editor.AddSelected(newEnt);
		}

		return newEnt;
	}
}
