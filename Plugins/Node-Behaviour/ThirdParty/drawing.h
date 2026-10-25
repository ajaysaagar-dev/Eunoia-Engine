#pragma once
#include <imgui.h>

#ifndef ImDrawFlags_RoundAll
#define ImDrawFlags_RoundAll ImDrawFlags_RoundCornersAll
#endif
#ifndef ImDrawFlags_RoundTop
#define ImDrawFlags_RoundTop ImDrawFlags_RoundCornersTop
#endif

namespace ax {
namespace Drawing {

enum class IconType: ImU32 { Flow, Circle, Square, Grid, RoundSquare, Diamond };

void DrawIcon(ImDrawList* drawList, const ImVec2& a, const ImVec2& b, IconType type, bool filled, ImU32 color, ImU32 innerColor);

} // namespace Drawing
} // namespace ax
