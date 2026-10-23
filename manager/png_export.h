#pragma once
#include <filesystem>
#include <string>

namespace resorep::ui {

// Decodes a texture file's top mip (first slice) and saves it as an 8-bit RGBA PNG (with alpha),
// turned `quarterTurns` clockwise and then, if `mirror`, flipped left-to-right (the same order
// as the preview). Single-channel textures become grayscale and two-channel normal maps get
// their blue (z) reconstructed. Encrypted resorep1 files are refused. Returns an error
// message, empty on success.
std::string ExportPng(const std::filesystem::path& texture, const std::filesystem::path& png, int quarterTurns = 0,
                      bool mirror = false);

}  // namespace resorep::ui
