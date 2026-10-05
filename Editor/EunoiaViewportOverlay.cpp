#include "stdafx.h"
#include "EunoiaViewportOverlay.h"
#include "EunoiaTheme.h"
#include "Editor.h"
#include "ModelImporter.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"


using namespace wi::ecs;
using namespace wi::scene;

namespace EunoiaUI
{
	ViewportOverlay::ViewportOverlay() = default;

	void ViewportOverlay::Render(EditorComponent& editor, Scene& scene, float x, float y, float width, float height)
	{
		// Hotkey controls (only when not typing in text fields)
		if (!ImGui::GetIO().WantTextInput)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_W)) m_gizmoOperation = ImGuizmo::TRANSLATE;
			if (ImGui::IsKeyPressed(ImGuiKey_E)) m_gizmoOperation = ImGuizmo::ROTATE;
			if (ImGui::IsKeyPressed(ImGuiKey_R)) m_gizmoOperation = ImGuizmo::SCALE;
			if (ImGui::IsKeyPressed(ImGuiKey_Q))
			{
				m_gizmoMode = (m_gizmoMode == ImGuizmo::WORLD) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
			}
		}

		DrawOverlayControls(editor, x, y, width, height);
		DrawGizmo(editor, scene, x, y, width, height);
		HandleDropTarget(editor, scene, x, y, width, height);
		DrawCameraPIP(editor, scene, x, y, width, height);
	}

	void ViewportOverlay::DrawOverlayControls(EditorComponent& editor, float x, float y, float width, float height)
	{
		ImGuiViewport* vp = ImGui::GetMainViewport();

		// Interactive overlay toolbar panel (UE5 style floating bar)
		ImGui::SetNextWindowPos(ImVec2(x + 12.0f, y + 12.0f), ImGuiCond_Always);
		ImGuiWindowFlags ctrlFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 4.0f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.14f, 0.18f, 0.88f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.24f, 0.26f, 0.32f, 0.80f));

		if (ImGui::Begin("##ViewportTopControls", nullptr, ctrlFlags))
		{
			// Stats
			ImGui::TextColored(Colors::AccentBlue, "FPS: %.1f", ImGui::GetIO().Framerate);
			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();

			// View Mode: Lit / Wireframe / Unlit
			bool isLit = (m_viewMode == 0);
			bool isWire = (m_viewMode == 1);
			bool isUnlit = (m_viewMode == 2);

			if (isLit) ImGui::PushStyleColor(ImGuiCol_Button, Colors::AccentBlue);
			if (ImGui::SmallButton("Lit")) { m_viewMode = 0; }
			if (isLit) ImGui::PopStyleColor();

			ImGui::SameLine();
			if (isWire) ImGui::PushStyleColor(ImGuiCol_Button, Colors::AccentBlue);
			if (ImGui::SmallButton("Wire")) { m_viewMode = 1; }
			if (isWire) ImGui::PopStyleColor();

			ImGui::SameLine();
			if (isUnlit) ImGui::PushStyleColor(ImGuiCol_Button, Colors::AccentBlue);
			if (ImGui::SmallButton("Unlit")) { m_viewMode = 2; }
			if (isUnlit) ImGui::PopStyleColor();

			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();

			// Gizmo Controls
			bool isTrans = (m_gizmoOperation == ImGuizmo::TRANSLATE);
			bool isRot = (m_gizmoOperation == ImGuizmo::ROTATE);
			bool isScale = (m_gizmoOperation == ImGuizmo::SCALE);

			if (isTrans) ImGui::PushStyleColor(ImGuiCol_Button, Colors::AccentBlue);
			if (ImGui::SmallButton("Pos [W]")) m_gizmoOperation = ImGuizmo::TRANSLATE;
			if (isTrans) ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translate Mode (Hotkey: W)");

			ImGui::SameLine();
			if (isRot) ImGui::PushStyleColor(ImGuiCol_Button, Colors::AccentBlue);
			if (ImGui::SmallButton("Rot [E]")) m_gizmoOperation = ImGuizmo::ROTATE;
			if (isRot) ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate Mode (Hotkey: E)");

			ImGui::SameLine();
			if (isScale) ImGui::PushStyleColor(ImGuiCol_Button, Colors::AccentBlue);
			if (ImGui::SmallButton("Sca [R]")) m_gizmoOperation = ImGuizmo::SCALE;
			if (isScale) ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale Mode (Hotkey: R)");

			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();

			// Coordinate Space Toggle
			const char* modeLabel = (m_gizmoMode == ImGuizmo::WORLD) ? "World [Q]" : "Local [Q]";
			if (ImGui::SmallButton(modeLabel))
			{
				m_gizmoMode = (m_gizmoMode == ImGuizmo::WORLD) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Coordinate Space: World vs Local (Hotkey: Q)");

			ImGui::SameLine();
			ImGui::TextDisabled("|");
			ImGui::SameLine();

			// Snapping Toggle & Config
			if (m_useSnap) ImGui::PushStyleColor(ImGuiCol_Button, Colors::AccentBlue);
			if (ImGui::SmallButton(m_useSnap ? "Snap [ON]" : "Snap [OFF]"))
			{
				m_useSnap = !m_useSnap;
			}
			if (m_useSnap) ImGui::PopStyleColor();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Grid & Angle Snapping");

			if (m_useSnap)
			{
				ImGui::SameLine();
				if (m_gizmoOperation == ImGuizmo::TRANSLATE)
				{
					ImGui::SetNextItemWidth(55.0f);
					if (ImGui::DragFloat("##SnapTrans", &m_snapTranslationVal, 0.1f, 0.01f, 100.0f, "%.2f"))
					{
						m_snapTranslation[0] = m_snapTranslationVal;
						m_snapTranslation[1] = m_snapTranslationVal;
						m_snapTranslation[2] = m_snapTranslationVal;
					}
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translation Snap Step (Grid Units)");
				}
				else if (m_gizmoOperation == ImGuizmo::ROTATE)
				{
					ImGui::SetNextItemWidth(50.0f);
					ImGui::DragFloat("##SnapRot", &m_snapRotation, 1.0f, 1.0f, 180.0f, "%.0f deg");
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotation Snap Step (Degrees)");
				}
				else if (m_gizmoOperation == ImGuizmo::SCALE)
				{
					ImGui::SetNextItemWidth(50.0f);
					ImGui::DragFloat("##SnapScale", &m_snapScale, 0.05f, 0.01f, 10.0f, "%.2f");
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale Snap Factor");
				}
			}

			ImGui::SameLine();
			ImGui::TextDisabled("| Cam Speed:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(70.0f);
			ImGui::SliderFloat("##CamSpeed", &m_cameraSpeed, 1.0f, 50.0f, "%.0f");
		}
		ImGui::End();

		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(3);
	}

	void ViewportOverlay::DrawGizmo(EditorComponent& editor, Scene& scene, float x, float y, float width, float height)
	{
		// Find selected entity
		Entity selectedEntity = INVALID_ENTITY;
		if (!editor.translator.selected.empty())
		{
			selectedEntity = editor.translator.selected[0].entity;
		}
		else
		{
			for (size_t i = 0; i < scene.names.GetCount(); ++i)
			{
				Entity ent = scene.names.GetEntity(i);
				if (editor.IsSelected(ent))
				{
					selectedEntity = ent;
					break;
				}
			}
		}

		if (selectedEntity == INVALID_ENTITY) return;

		TransformComponent* trans = scene.transforms.GetComponent(selectedEntity);
		if (!trans) return;

		if (trans->IsDirty())
		{
			trans->UpdateTransform();
		}

		const CameraComponent& camera = (editor.renderPath && editor.renderPath->camera) ? *editor.renderPath->camera : editor.GetCurrentEditorScene().camera;
		XMMATRIX V = camera.GetView();
		XMMATRIX P = camera.GetProjection();

		XMFLOAT4X4 viewMat, projMat, worldMat;
		XMStoreFloat4x4(&viewMat, V);
		XMStoreFloat4x4(&projMat, P);
		worldMat = trans->world;

		ImGuizmo::Enable(true);
		ImGuizmo::SetID((int)selectedEntity);
		ImGuizmo::SetOrthographic(camera.IsOrtho());
		ImGuizmo::SetRect(x, y, width, height);

		float* snapPtr = nullptr;
		if (m_useSnap)
		{
			if (m_gizmoOperation == ImGuizmo::TRANSLATE) snapPtr = m_snapTranslation;
			else if (m_gizmoOperation == ImGuizmo::ROTATE) snapPtr = &m_snapRotation;
			else if (m_gizmoOperation == ImGuizmo::SCALE) snapPtr = &m_snapScale;
		}

		ImGuizmo::MODE actualMode = (m_gizmoOperation == ImGuizmo::SCALE) ? ImGuizmo::LOCAL : (ImGuizmo::MODE)m_gizmoMode;

		float matrixTranslation[3], matrixRotation[3], matrixScale[3];
		if (ImGuizmo::Manipulate(
			&viewMat.m[0][0],
			&projMat.m[0][0],
			m_gizmoOperation,
			actualMode,
			&worldMat.m[0][0],
			NULL,
			snapPtr))
		{
			// Check if entity is child in hierarchy
			const HierarchyComponent* hier = scene.hierarchy.GetComponent(selectedEntity);
			if (hier != nullptr && hier->parentID != INVALID_ENTITY)
			{
				const TransformComponent* parentTrans = scene.transforms.GetComponent(hier->parentID);
				if (parentTrans != nullptr)
				{
					XMMATRIX parentWorld = parentTrans->GetWorldMatrix();
					XMVECTOR det;
					XMMATRIX invParentWorld = XMMatrixInverse(&det, parentWorld);
					XMMATRIX newWorld = XMLoadFloat4x4(&worldMat);
					XMMATRIX newLocal = XMMatrixMultiply(newWorld, invParentWorld);

					XMFLOAT4X4 newLocalMat;
					XMStoreFloat4x4(&newLocalMat, newLocal);
					ImGuizmo::DecomposeMatrixToComponents(&newLocalMat.m[0][0], matrixTranslation, matrixRotation, matrixScale);
				}
				else
				{
					ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], matrixTranslation, matrixRotation, matrixScale);
				}
			}
			else
			{
				ImGuizmo::DecomposeMatrixToComponents(&worldMat.m[0][0], matrixTranslation, matrixRotation, matrixScale);
			}

			// Apply to entity transform
			trans->translation_local = XMFLOAT3(matrixTranslation[0], matrixTranslation[1], matrixTranslation[2]);
			XMVECTOR rotQ = XMQuaternionRotationRollPitchYaw(
				wi::math::DegreesToRadians(matrixRotation[0]),
				wi::math::DegreesToRadians(matrixRotation[1]),
				wi::math::DegreesToRadians(matrixRotation[2])
			);
			XMStoreFloat4(&trans->rotation_local, rotQ);
			trans->scale_local = XMFLOAT3(matrixScale[0], matrixScale[1], matrixScale[2]);
			trans->UpdateTransform();
			trans->SetDirty();
		}
	}

	void ViewportOverlay::HandleDropTarget(EditorComponent& editor, Scene& scene, float x, float y, float width, float height)
	{
		const ImGuiPayload* currentPayload = ImGui::GetDragDropPayload();
		if (!currentPayload || (!currentPayload->IsDataType("CONTENT_ASSET_PATH") && !currentPayload->IsDataType("CONTENT_DRAG")))
		{
			return;
		}

		// Active transparent full-viewport window specifically when a drag is in flight
		ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;

		if (ImGui::Begin("##ViewportDropAreaActive", nullptr, flags))
		{
			ImGui::InvisibleButton("##ViewportDropAreaButton", ImVec2(width, height));

			if (ImGui::BeginDragDropTarget())
			{
				// Visual highlight box on viewport
				ImDrawList* dl = ImGui::GetForegroundDrawList();
				dl->AddRect(ImVec2(x + 2, y + 2), ImVec2(x + width - 2, y + height - 2), IM_COL32(0, 150, 255, 200), 4.0f, 0, 3.0f);

				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_ASSET_PATH");
				if (!payload) payload = ImGui::AcceptDragDropPayload("CONTENT_DRAG");

				if (payload)
				{
					const char* assetPath = (const char*)payload->Data;
					if (assetPath && assetPath[0] != '\0')
					{
						std::string pathStr = assetPath;
						std::string ext = wi::helper::toUpper(wi::helper::GetExtensionFromFileName(pathStr));

						// Snapshot existing entities before load
						wi::unordered_set<Entity> beforeEntities;
						scene.FindAllEntities(beforeEntities);

						// Import according to file extension
						if (ext == "OBJ")
						{
							ImportModel_OBJ(pathStr, scene);
						}
						else if (ext == "GLTF" || ext == "GLB")
						{
							ImportModel_GLTF(pathStr, scene);
						}
						else if (ext == "FBX")
						{
							ImportModel_FBX(pathStr, scene);
						}
						else if (ext == "PLY")
						{
							ImportModel_PLY(pathStr, scene);
						}
						else if (ext == "EULEVEL" || ext == "WISCENE")
						{
							wi::scene::LoadModel(scene, pathStr);
						}
						else
						{
							wi::scene::LoadModel(scene, pathStr);
						}

						// Snapshot entities after load and find newly added root entity
						wi::unordered_set<Entity> afterEntities;
						scene.FindAllEntities(afterEntities);

						wi::vector<Entity> newEntities;
						for (auto& ent : afterEntities)
						{
							if (beforeEntities.find(ent) == beforeEntities.end())
							{
								newEntities.push_back(ent);
							}
						}

						Entity spawnedRoot = INVALID_ENTITY;
						for (auto ent : newEntities)
						{
							const HierarchyComponent* hier = scene.hierarchy.GetComponent(ent);
							if (!hier || hier->parentID == INVALID_ENTITY || beforeEntities.find(hier->parentID) != beforeEntities.end())
							{
								spawnedRoot = ent;
								break;
							}
						}
						if (spawnedRoot == INVALID_ENTITY && !newEntities.empty())
						{
							spawnedRoot = newEntities[0];
						}

						// Calculate 3D spawn position from mouse drop coordinates
						ImVec2 mousePos = ImGui::GetMousePos();
						float relMouseX = mousePos.x - x;
						float relMouseY = mousePos.y - y;

						const CameraComponent& camera = (editor.renderPath && editor.renderPath->camera) ? *editor.renderPath->camera : editor.GetCurrentEditorScene().camera;

						XMFLOAT3 spawnPos;
						if (editor.renderPath)
						{
							wi::primitive::Ray ray = wi::renderer::GetPickRay((long)relMouseX, (long)relMouseY, *editor.renderPath, camera);
							wi::scene::PickResult pick = wi::scene::Pick(ray, wi::enums::FILTER_OPAQUE, ~0u, scene);
							if (pick.entity != INVALID_ENTITY && pick.distance > 0.0f)
							{
								spawnPos = pick.position;
							}
							else if (std::abs(ray.direction.y) > 0.0001f)
							{
								float t = -ray.origin.y / ray.direction.y;
								if (t > 0.0f && t < 2000.0f)
								{
									spawnPos = XMFLOAT3(ray.origin.x + ray.direction.x * t, 0.0f, ray.origin.z + ray.direction.z * t);
								}
								else
								{
									spawnPos = editor.GetPositionInFrontOfCamera();
								}
							}
							else
							{
								spawnPos = editor.GetPositionInFrontOfCamera();
							}
						}
						else
						{
							spawnPos = editor.GetPositionInFrontOfCamera();
						}

						// Position root and select it immediately
						if (spawnedRoot != INVALID_ENTITY)
						{
							TransformComponent* trans = scene.transforms.GetComponent(spawnedRoot);
							if (trans)
							{
								trans->translation_local = spawnPos;
								trans->UpdateTransform();
								trans->SetDirty();
							}

							editor.ClearSelected();
							editor.AddSelected(spawnedRoot);
							editor.componentsWnd.RefreshEntityTree();
						}

						wi::backlog::post("[EunoiaUI] Spawned 3D Model into Viewport: " + pathStr);
					}
				}
				ImGui::EndDragDropTarget();
			}
		}
		ImGui::End();
	}

	void ViewportOverlay::DrawCameraPIP(EditorComponent& editor, Scene& scene, float x, float y, float width, float height)
	{
		// Check if a camera is selected
		Entity selectedCamera = INVALID_ENTITY;
		for (size_t i = 0; i < scene.cameras.GetCount(); ++i)
		{
			Entity ent = scene.cameras.GetEntity(i);
			if (editor.IsSelected(ent))
			{
				selectedCamera = ent;
				break;
			}
		}

		if (selectedCamera == INVALID_ENTITY) return;

		float pipW = 240.0f;
		float pipH = 140.0f;
		float pipX = x + width - pipW - 16.0f;
		float pipY = y + height - pipH - 16.0f;

		ImGui::SetNextWindowPos(ImVec2(pipX, pipY), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(pipW, pipH), ImGuiCond_Always);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
		ImGui::PushStyleColor(ImGuiCol_Border, Colors::AccentBlue);

		if (ImGui::Begin("Camera Preview###CameraPIP", nullptr, flags))
		{
			const NameComponent* nc = scene.names.GetComponent(selectedCamera);
			ImGui::TextDisabled("Camera: %s", nc ? nc->name.c_str() : "Camera");
			ImGui::Separator();
			ImGui::TextColored(Colors::AccentBlue, "[Live Camera Feed]");
			ImGui::TextDisabled("Aspect: 16:9");
		}
		ImGui::End();

		ImGui::PopStyleColor();
	}
}
