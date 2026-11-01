#pragma once
#include <dxgiformat.h>

#include <cstdint>
#include <vector>

namespace resorep::ui {

// Formats EncodeSurface can write: BC1, BC2, BC3, BC7 and the 8-bit RGBA/BGRA/BGRX ones, in
// any of their typeless/UNORM/sRGB variants.
bool CanEncode(DXGI_FORMAT format);

// Encodes one mip of RGBA floats (0..1, width * height * 4, the stored values: sRGB formats are
// not converted) into `format`, rows packed the way ComputeSurfaceLayout lays them out.
// BC7 uses mode 6 (one RGBA line per block) or mode 5 (colour and alpha on lines of their own),
// whichever fits each block better. Quick and close to the original on most game textures, but
// blocks with several distinct colours lose some detail.
std::vector<uint8_t> EncodeSurface(DXGI_FORMAT format, uint32_t width, uint32_t height, const float* rgba);

}  // namespace resorep::ui
