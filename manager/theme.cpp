#include "theme.h"

#include <imgui_internal.h>  // ImLerp

#include <cstdio>
#include <cstdlib>

namespace resorep::ui {
namespace {
ImVec4 g_accent(0.26f, 0.59f, 0.98f, 1.0f);

ImVec4 WithAlpha(ImVec4 c, float a) { return ImVec4(c.x, c.y, c.z, a); }
ImVec4 Scale(ImVec4 c, float k) { return ImVec4(c.x * k, c.y * k, c.z * k, c.w); }
ImVec4 Mix(ImVec4 a, ImVec4 b, float t) { return ImLerp(a, b, t); }
}  // namespace

void ApplyAccentColor(uint32_t rgb) {
    const ImVec4 a(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
    g_accent = a;
    ImVec4* c = ImGui::GetStyle().Colors;

    // Same relationships as ImGui::StyleColorsDark, with its blue (0.26, 0.59, 0.98) replaced.
    c[ImGuiCol_FrameBg] = WithAlpha(Scale(a, 0.50f), 0.54f);
    c[ImGuiCol_FrameBgHovered] = WithAlpha(a, 0.40f);
    c[ImGuiCol_FrameBgActive] = WithAlpha(a, 0.67f);
    c[ImGuiCol_TitleBgActive] = Scale(a, 0.50f);
    c[ImGuiCol_CheckMark] = a;
    c[ImGuiCol_SliderGrab] = Scale(a, 0.92f);
    c[ImGuiCol_SliderGrabActive] = a;
    c[ImGuiCol_Button] = WithAlpha(a, 0.40f);
    c[ImGuiCol_ButtonHovered] = a;
    c[ImGuiCol_ButtonActive] = Scale(a, 0.80f);
    c[ImGuiCol_Header] = WithAlpha(a, 0.31f);
    c[ImGuiCol_HeaderHovered] = WithAlpha(a, 0.80f);
    c[ImGuiCol_HeaderActive] = a;
    c[ImGuiCol_SeparatorHovered] = WithAlpha(Scale(a, 0.75f), 0.78f);
    c[ImGuiCol_SeparatorActive] = Scale(a, 0.75f);
    c[ImGuiCol_ResizeGrip] = WithAlpha(a, 0.20f);
    c[ImGuiCol_ResizeGripHovered] = WithAlpha(a, 0.67f);
    c[ImGuiCol_ResizeGripActive] = WithAlpha(a, 0.95f);
    c[ImGuiCol_TextSelectedBg] = WithAlpha(a, 0.35f);
    c[ImGuiCol_NavCursor] = a;
    c[ImGuiCol_PlotHistogram] = a;  // progress bars
    c[ImGuiCol_TabHovered] = c[ImGuiCol_HeaderHovered];
    c[ImGuiCol_Tab] = Mix(c[ImGuiCol_Header], c[ImGuiCol_TitleBgActive], 0.80f);
    c[ImGuiCol_TabSelected] = Mix(c[ImGuiCol_HeaderActive], c[ImGuiCol_TitleBgActive], 0.60f);
    c[ImGuiCol_TabSelectedOverline] = c[ImGuiCol_HeaderActive];
    c[ImGuiCol_TabDimmed] = Mix(c[ImGuiCol_Tab], c[ImGuiCol_TitleBg], 0.80f);
    c[ImGuiCol_TabDimmedSelected] = Mix(c[ImGuiCol_TabSelected], c[ImGuiCol_TitleBg], 0.40f);
}

ImVec4 AccentColor() { return g_accent; }

ImVec4 AccentTextColor() { return Mix(g_accent, ImVec4(1, 1, 1, 1), 0.30f); }

std::string HexColor(uint32_t rgb) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%06X", rgb & 0xFFFFFF);
    return buf;
}

uint32_t ParseHexColor(const std::string& text, uint32_t fallback) {
    const char* s = text.c_str();
    if (*s == '#') ++s;
    char* end = nullptr;
    const unsigned long v = std::strtoul(s, &end, 16);
    return (end - s == 6 && *end == 0) ? uint32_t(v) : fallback;
}

}  // namespace resorep::ui
