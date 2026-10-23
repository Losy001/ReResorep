#pragma once
#include <imgui.h>

#include <cstdint>
#include <string>

namespace resorep::ui {

// 0xRRGGBB. The default is Dear ImGui's dark-theme blue.
inline constexpr uint32_t kDefaultAccent = 0x4296FA;

// Recolours every accent-dependent ImGui colour (buttons, tabs, headers, checkboxes,
// sliders, selection, frames) from one colour, the way the dark theme derives them.
void ApplyAccentColor(uint32_t rgb);

ImVec4 AccentColor();      // the accent itself
ImVec4 AccentTextColor();  // a lighter shade that stays readable on the dark background

std::string HexColor(uint32_t rgb);                               // "#RRGGBB"
uint32_t ParseHexColor(const std::string& text, uint32_t fallback);  // "#RRGGBB" or "RRGGBB"

}  // namespace resorep::ui
