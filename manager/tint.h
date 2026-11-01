#pragma once
#include <dxgiformat.h>

#include <cstddef>
#include <filesystem>
#include <string>

namespace resorep::ui {

struct TintSettings {
    enum class Mode {
        Recolor,   // the colour's hue, each pixel keeps its own saturation and brightness
        Colorize,  // the colour's hue and saturation, each pixel keeps its lightness
        Multiply,  // pixel x colour: the classic tint, darkens
    };
    Mode mode = Mode::Recolor;
    float color[3] = {1.0f, 0.15f, 0.15f};
    float strength = 1.0f;    // 0..1: how much of the tinted result replaces the original
    float saturation = 1.0f;  // 0..2, applied after the tint
    float brightness = 1.0f;  // 0..2, applied after the tint
};

// Tints `count` RGBA pixels (0..1) in place; alpha is left alone.
void TintPixels(const TintSettings& s, float* rgba, size_t count);

// Why a texture in this format can't be tinted, nullptr when it can.
const char* TintUnsupported(DXGI_FORMAT format);

// Reads a texture, tints every mip and writes it to `out` in the same format.
// Returns an error message, empty on success.
std::string TintTexture(const std::filesystem::path& in, const std::filesystem::path& out, const TintSettings& s);

}  // namespace resorep::ui
