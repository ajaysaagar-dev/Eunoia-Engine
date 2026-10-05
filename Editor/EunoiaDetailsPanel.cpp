#include "stdafx.h"
#include "EunoiaDetailsPanel.h"
#include "EunoiaTheme.h"
#include "Editor.h"
#include "ImGui/imgui.h"

using namespace wi::ecs;
using namespace wi::scene;

namespace EunoiaUI
{
	void DetailsPanel::Render(EditorComponent& editor, Scene& scene, float width, float height, float posX, float posY)
	{
		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
		if (ImGui::Begin("Details###EunoiaDetailsWindow", nullptr, flags))
		{
			// Check if any entity is selected
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

			if (selectedEntity == INVALID_ENTITY)
			{
				ImGui::TextDisabled("Select an actor in the Outliner or Viewport to view details.");
				ImGui::End();
				ImGui::PopStyleVar(2);
				return;
			}

			// Entity Header: Type Badge, Name & ID
			if (scene.lights.Contains(selectedEntity))
			{
				const LightComponent* l = scene.lights.GetComponent(selectedEntity);
				const char* ltStr = "LightComponent";
				if (l && l->GetType() == LightComponent::DIRECTIONAL) ltStr = "DirectionalLightComponent";
				else if (l && l->GetType() == LightComponent::POINT) ltStr = "PointLightComponent";
				else if (l && l->GetType() == LightComponent::SPOT) ltStr = "SpotLightComponent";
				else if (l && l->GetType() == LightComponent::RECTANGLE) ltStr = "RectangleLightComponent";
				ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.20f, 1.0f), "[%s]", ltStr);
			}
			else if (scene.cameras.Contains(selectedEntity))
			{
				ImGui::TextColored(ImVec4(0.30f, 0.90f, 0.70f, 1.0f), "[CameraComponent]");
			}
			else if (scene.meshes.Contains(selectedEntity) || scene.objects.Contains(selectedEntity))
			{
				ImGui::TextColored(ImVec4(0.20f, 0.65f, 1.00f, 1.0f), "[MeshComponent]");
			}
			else if (scene.emitters.Contains(selectedEntity))
			{
				ImGui::TextColored(ImVec4(1.00f, 0.40f, 0.20f, 1.0f), "[EmitterComponent]");
			}
			else
			{
				ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "[TransformActor]");
			}

			NameComponent* nameComp = scene.names.GetComponent(selectedEntity);
			char nameBuf[128] = "";
			if (nameComp)
			{
				strncpy_s(nameBuf, nameComp->name.c_str(), sizeof(nameBuf) - 1);
			}

			ImGui::PushItemWidth(-80.0f);
			if (ImGui::InputText("##ActorNameDetails", nameBuf, sizeof(nameBuf)))
			{
				if (nameComp) nameComp->name = nameBuf;
			}
			ImGui::PopItemWidth();

			ImGui::SameLine();
			ImGui::TextDisabled("ID: %u", (uint32_t)selectedEntity);
			ImGui::Separator();

			// Add Component Button
			DrawAddComponentMenu(scene, selectedEntity);
			ImGui::Separator();

			// Component sections
			DrawTransformSection(scene, selectedEntity);
			DrawMeshSection(scene, selectedEntity);
			DrawMaterialSection(scene, selectedEntity);
			DrawLightSection(scene, selectedEntity);
			DrawCameraSection(scene, selectedEntity);
			DrawPhysicsSection(scene, selectedEntity);
			DrawSoundSection(scene, selectedEntity);
			DrawWeatherSection(scene, selectedEntity);
		}
		ImGui::End();

		ImGui::PopStyleVar(2);
	}

	void DetailsPanel::DrawAddComponentMenu(Scene& scene, Entity entity)
	{
		if (ImGui::Button("+ Add Component", ImVec2(-1.0f, 28.0f)))
		{
			ImGui::OpenPopup("AddComponentPopup");
		}

		if (ImGui::BeginPopup("AddComponentPopup"))
		{
			if (!scene.lights.Contains(entity) && ImGui::MenuItem("Light Component"))
			{
				scene.lights.Create(entity);
			}
			if (!scene.cameras.Contains(entity) && ImGui::MenuItem("Camera Component"))
			{
				scene.cameras.Create(entity);
			}
			if (!scene.materials.Contains(entity) && ImGui::MenuItem("Material Component"))
			{
				scene.materials.Create(entity);
			}
			if (!scene.rigidbodies.Contains(entity) && ImGui::MenuItem("RigidBody Physics"))
			{
				scene.rigidbodies.Create(entity);
			}
			if (!scene.colliders.Contains(entity) && ImGui::MenuItem("Collider Component"))
			{
				scene.colliders.Create(entity);
			}
			if (!scene.emitters.Contains(entity) && ImGui::MenuItem("Particle Emitter"))
			{
				scene.emitters.Create(entity);
			}
			if (!scene.hairs.Contains(entity) && ImGui::MenuItem("Hair Particles"))
			{
				scene.hairs.Create(entity);
			}
			if (!scene.forces.Contains(entity) && ImGui::MenuItem("Force Field Component"))
			{
				scene.forces.Create(entity);
			}
			if (!scene.decals.Contains(entity) && ImGui::MenuItem("Decal Component"))
			{
				scene.decals.Create(entity);
			}
			if (!scene.sounds.Contains(entity) && ImGui::MenuItem("Sound Component"))
			{
				scene.sounds.Create(entity);
			}
			if (!scene.weathers.Contains(entity) && ImGui::MenuItem("Weather Component"))
			{
				scene.weathers.Create(entity);
			}
			if (!scene.scripts.Contains(entity) && ImGui::MenuItem("Script Component"))
			{
				scene.scripts.Create(entity);
			}
			ImGui::EndPopup();
		}
	}

	void DetailsPanel::DrawTransformSection(Scene& scene, Entity entity)
	{
		TransformComponent* trans = scene.transforms.GetComponent(entity);
		if (!trans) return;

		if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// Location
			ImGui::Text("Location");
			float locX = trans->translation_local.x;
			float locY = trans->translation_local.y;
			float locZ = trans->translation_local.z;
			bool modLoc = false;
			modLoc |= DrawTransformPill("X##Loc", locX, Colors::PillRed, 0.0f);
			modLoc |= DrawTransformPill("Y##Loc", locY, Colors::PillGreen, 0.0f);
			modLoc |= DrawTransformPill("Z##Loc", locZ, Colors::PillBlue, 0.0f);
			if (modLoc)
			{
				trans->translation_local = XMFLOAT3(locX, locY, locZ);
				trans->SetDirty();
			}

			ImGui::Spacing();

			// Rotation (Euler angles in degrees)
			ImGui::Text("Rotation");
			XMFLOAT3 rpy = wi::math::QuaternionToRollPitchYaw(trans->rotation_local);
			float rotX = wi::math::RadiansToDegrees(rpy.x);
			float rotY = wi::math::RadiansToDegrees(rpy.y);
			float rotZ = wi::math::RadiansToDegrees(rpy.z);
			bool modRot = false;
			modRot |= DrawTransformPill("X##Rot", rotX, Colors::PillRed, 0.0f, 0.5f);
			modRot |= DrawTransformPill("Y##Rot", rotY, Colors::PillGreen, 0.0f, 0.5f);
			modRot |= DrawTransformPill("Z##Rot", rotZ, Colors::PillBlue, 0.0f, 0.5f);
			if (modRot)
			{
				XMVECTOR Q = XMQuaternionRotationRollPitchYaw(
					wi::math::DegreesToRadians(rotX),
					wi::math::DegreesToRadians(rotY),
					wi::math::DegreesToRadians(rotZ)
				);
				XMStoreFloat4(&trans->rotation_local, Q);
				trans->SetDirty();
			}

			ImGui::Spacing();

			// Scale
			ImGui::Text("Scale");
			float scaX = trans->scale_local.x;
			float scaY = trans->scale_local.y;
			float scaZ = trans->scale_local.z;
			bool modSca = false;
			float oldX = scaX;
			if (DrawTransformPill("X##Sca", scaX, Colors::PillRed, 1.0f))
			{
				modSca = true;
				if (m_uniformScaleLock && oldX != 0.0f)
				{
					float ratio = scaX / oldX;
					scaY *= ratio;
					scaZ *= ratio;
				}
			}
			modSca |= DrawTransformPill("Y##Sca", scaY, Colors::PillGreen, 1.0f);
			modSca |= DrawTransformPill("Z##Sca", scaZ, Colors::PillBlue, 1.0f);
			if (modSca)
			{
				trans->scale_local = XMFLOAT3(scaX, scaY, scaZ);
				trans->SetDirty();
			}

			ImGui::Checkbox("Uniform Scale", &m_uniformScaleLock);
			ImGui::Separator();
		}
	}

	void DetailsPanel::DrawMeshSection(Scene& scene, Entity entity)
	{
		ObjectComponent* object = scene.objects.GetComponent(entity);
		if (!object) return;

		if (ImGui::CollapsingHeader("Mesh Component", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const MeshComponent* mesh = scene.meshes.GetComponent(object->meshID);
			if (mesh)
			{
				ImGui::Text("Vertices: %zu", mesh->vertex_positions.size());
				ImGui::Text("Indices:  %zu", mesh->indices.size());
				ImGui::Text("Subsets:  %zu", mesh->subsets.size());
			}
			else
			{
				ImGui::TextDisabled("No mesh data bound.");
			}

			bool castShadow = object->IsCastingShadow();
			if (ImGui::Checkbox("Cast Shadows", &castShadow))
			{
				object->SetCastShadow(castShadow);
			}


			ImGui::Separator();
		}
	}

	void DetailsPanel::DrawMaterialSection(Scene& scene, Entity entity)
	{
		MaterialComponent* mat = scene.materials.GetComponent(entity);
		if (!mat)
		{
			ObjectComponent* object = scene.objects.GetComponent(entity);
			if (object)
			{
				const MeshComponent* mesh = scene.meshes.GetComponent(object->meshID);
				if (mesh && !mesh->subsets.empty())
				{
					mat = scene.materials.GetComponent(mesh->subsets[0].materialID);
				}
			}
		}

		if (!mat) return;

		if (ImGui::CollapsingHeader("Material Component", ImGuiTreeNodeFlags_DefaultOpen))
		{
			float col[4] = { mat->baseColor.x, mat->baseColor.y, mat->baseColor.z, mat->baseColor.w };
			if (ImGui::ColorEdit4("Base Color", col))
			{
				mat->baseColor = XMFLOAT4(col[0], col[1], col[2], col[3]);
				mat->SetDirty();
			}

			float roughness = mat->roughness;
			if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f))
			{
				mat->roughness = roughness;
				mat->SetDirty();
			}

			float metalness = mat->metalness;
			if (ImGui::SliderFloat("Metallic", &metalness, 0.0f, 1.0f))
			{
				mat->metalness = metalness;
				mat->SetDirty();
			}

			float normalStrength = mat->normalMapStrength;
			if (ImGui::SliderFloat("Normal Strength", &normalStrength, 0.0f, 2.0f))
			{
				mat->normalMapStrength = normalStrength;
				mat->SetDirty();
			}

			float emissive[4] = { mat->emissiveColor.x, mat->emissiveColor.y, mat->emissiveColor.z, mat->emissiveColor.w };
			if (ImGui::ColorEdit4("Emissive", emissive))
			{
				mat->emissiveColor = XMFLOAT4(emissive[0], emissive[1], emissive[2], emissive[3]);
				mat->SetDirty();
			}

			ImGui::Separator();
		}
	}

	void DetailsPanel::DrawLightSection(Scene& scene, Entity entity)
	{
		LightComponent* light = scene.lights.GetComponent(entity);
		if (!light) return;

		if (ImGui::CollapsingHeader("Light Component", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// Type selector
			int typeIdx = (int)light->GetType();
			const char* types[] = { "Directional", "Point", "Spot", "Rectangle" };
			if (ImGui::Combo("Type", &typeIdx, types, 4))
			{
				light->SetType((LightComponent::LightType)typeIdx);
			}

			float col[3] = { light->color.x, light->color.y, light->color.z };
			if (ImGui::ColorEdit3("Color", col))
			{
				light->color = XMFLOAT3(col[0], col[1], col[2]);
			}

			float energy = light->intensity;
			if (ImGui::DragFloat("Intensity", &energy, 0.1f, 0.0f, 1000.0f))
			{
				light->intensity = energy;
			}

			float range = light->range;
			if (ImGui::DragFloat("Range", &range, 0.5f, 0.0f, 1000.0f))
			{
				light->range = range;
			}

			if (light->GetType() == LightComponent::LightType::SPOT)
			{
				float outerAngle = wi::math::RadiansToDegrees(light->outerConeAngle);
				if (ImGui::SliderFloat("Outer Angle", &outerAngle, 1.0f, 179.0f))
				{
					light->outerConeAngle = wi::math::DegreesToRadians(outerAngle);
				}

				float innerAngle = wi::math::RadiansToDegrees(light->innerConeAngle);
				if (ImGui::SliderFloat("Inner Angle", &innerAngle, 0.0f, outerAngle))
				{
					light->innerConeAngle = wi::math::DegreesToRadians(innerAngle);
				}
			}
			else if (light->GetType() == LightComponent::LightType::RECTANGLE)
			{
				float rectW = light->length;
				if (ImGui::DragFloat("Width", &rectW, 0.1f, 0.01f, 100.0f))
				{
					light->length = rectW;
				}
				float rectH = light->height;
				if (ImGui::DragFloat("Height", &rectH, 0.1f, 0.01f, 100.0f))
				{
					light->height = rectH;
				}
			}

			bool castShadow = light->IsCastingShadow();
			if (ImGui::Checkbox("Cast Shadow", &castShadow))
			{
				light->SetCastShadow(castShadow);
			}

			bool volumetrics = light->IsVolumetricsEnabled();
			if (ImGui::Checkbox("Volumetric Fog", &volumetrics))
			{
				light->SetVolumetricsEnabled(volumetrics);
			}

			if (volumetrics)
			{
				float boost = light->volumetric_boost;
				if (ImGui::DragFloat("Volumetric Boost", &boost, 0.1f, 0.0f, 50.0f))
				{
					light->volumetric_boost = boost;
				}
			}

			bool visualizer = light->IsVisualizerEnabled();
			if (ImGui::Checkbox("Visualizer Gizmo", &visualizer))
			{
				light->SetVisualizerEnabled(visualizer);
			}

			bool clouds = light->IsVolumetricCloudsEnabled();
			if (ImGui::Checkbox("Affect Volumetric Clouds", &clouds))
			{
				light->SetVolumetricCloudsEnabled(clouds);
			}

			ImGui::Separator();
		}
	}

	void DetailsPanel::DrawCameraSection(Scene& scene, Entity entity)
	{
		CameraComponent* camera = scene.cameras.GetComponent(entity);
		if (!camera) return;

		if (ImGui::CollapsingHeader("Camera Component", ImGuiTreeNodeFlags_DefaultOpen))
		{
			float fov = wi::math::RadiansToDegrees(camera->fov);
			if (ImGui::SliderFloat("FOV (deg)", &fov, 10.0f, 140.0f))
			{
				camera->fov = wi::math::DegreesToRadians(fov);
			}

			float nearP = camera->zNearP;
			if (ImGui::DragFloat("Near Plane", &nearP, 0.01f, 0.001f, 10.0f))
			{
				camera->zNearP = nearP;
			}

			float farP = camera->zFarP;
			if (ImGui::DragFloat("Far Plane", &farP, 1.0f, 1.0f, 100000.0f))
			{
				camera->zFarP = farP;
			}

			ImGui::Separator();
		}
	}

	void DetailsPanel::DrawPhysicsSection(Scene& scene, Entity entity)
	{
		RigidBodyPhysicsComponent* rb = scene.rigidbodies.GetComponent(entity);
		if (!rb) return;

		if (ImGui::CollapsingHeader("RigidBody Physics", ImGuiTreeNodeFlags_DefaultOpen))
		{
			float mass = rb->mass;
			if (ImGui::DragFloat("Mass", &mass, 0.1f, 0.0f, 1000.0f))
			{
				rb->mass = mass;
			}

			float friction = rb->friction;
			if (ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f))
			{
				rb->friction = friction;
			}

			float restitution = rb->restitution;
			if (ImGui::SliderFloat("Restitution", &restitution, 0.0f, 1.0f))
			{
				rb->restitution = restitution;
			}

			bool kinematic = rb->IsKinematic();
			if (ImGui::Checkbox("Kinematic", &kinematic))
			{
				rb->SetKinematic(kinematic);
			}

			ImGui::Separator();
		}
	}

	void DetailsPanel::DrawSoundSection(Scene& scene, Entity entity)
	{
		SoundComponent* sound = scene.sounds.GetComponent(entity);
		if (!sound) return;

		if (ImGui::CollapsingHeader("Sound Component", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Sound: %s", sound->filename.c_str());
			float volume = sound->volume;
			if (ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f))
			{
				sound->volume = volume;
			}

			bool looping = sound->IsLooped();
			if (ImGui::Checkbox("Looping", &looping))
			{
				sound->SetLooped(looping);
			}

			ImGui::Separator();
		}
	}

	void DetailsPanel::DrawWeatherSection(Scene& scene, Entity entity)
	{
		WeatherComponent* weather = scene.weathers.GetComponent(entity);
		if (!weather) return;

		if (ImGui::CollapsingHeader("Weather Component", ImGuiTreeNodeFlags_DefaultOpen))
		{
			float horizon[3] = { weather->horizon.x, weather->horizon.y, weather->horizon.z };
			if (ImGui::ColorEdit3("Horizon Color", horizon))
			{
				weather->horizon = XMFLOAT3(horizon[0], horizon[1], horizon[2]);
			}

			float zenith[3] = { weather->zenith.x, weather->zenith.y, weather->zenith.z };
			if (ImGui::ColorEdit3("Zenith Color", zenith))
			{
				weather->zenith = XMFLOAT3(zenith[0], zenith[1], zenith[2]);
			}

			float fogStart = weather->fogStart;
			if (ImGui::DragFloat("Fog Start", &fogStart, 1.0f, 0.0f, 1000.0f))
			{
				weather->fogStart = fogStart;
			}

			float fogDensity = weather->fogDensity;
			if (ImGui::SliderFloat("Fog Density", &fogDensity, 0.0f, 1.0f))
			{
				weather->fogDensity = fogDensity;
			}

			ImGui::Separator();
		}
	}
}
