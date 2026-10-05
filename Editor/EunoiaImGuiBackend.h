#pragma once
#include "WickedEngine.h"
#include "ImGui/imgui.h"
#include "ImGui/ImGuizmo.h"

namespace EunoiaUI
{
	class ImGuiBackend
	{
	public:
		static bool Initialize(HWND window);
		static void Shutdown();
		static void NewFrame();
		static void RenderDrawData(ImDrawData* drawData, wi::graphics::CommandList cmd);

		static bool IsInitialized();
		static bool WantCaptureMouse();
		static bool WantCaptureKeyboard();

		// Load custom texture for ImGui image display (returns ImTextureID)
		static ImTextureID GetTextureID(const wi::graphics::Texture* texture);
	};
}
