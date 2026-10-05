#pragma once
#include "ImGui/imgui.h"
#include <string>

namespace EunoiaUI
{
	// Apply exact Eunoia UE5-style dark theme to Dear ImGui
	void SetupTheme();

	// Custom UE5-style colored transform pill (Location: Red, Rotation: Green, Scale: Blue)
	// Has a click-to-reset button on left and a drag float field on right
	bool DrawTransformPill(const char* label, float& value, const ImVec4& color, float resetValue = 0.0f, float speed = 0.05f);

	// Theme color constants
	namespace Colors
	{
		inline const ImVec4 PillRed    = ImVec4(0.88f, 0.24f, 0.24f, 1.0f); // Location X / Pitch
		inline const ImVec4 PillGreen  = ImVec4(0.24f, 0.88f, 0.31f, 1.0f); // Location Y / Yaw
		inline const ImVec4 PillBlue   = ImVec4(0.24f, 0.51f, 0.88f, 1.0f); // Location Z / Roll
		inline const ImVec4 AccentBlue = ImVec4(0.12f, 0.60f, 0.95f, 1.0f); // UE5 highlight blue
		inline const ImVec4 DarkBg     = ImVec4(0.10f, 0.12f, 0.16f, 0.98f);
		inline const ImVec4 TextMuted  = ImVec4(0.55f, 0.57f, 0.60f, 1.0f);
	}
}
