#pragma once
#include <dxgiformat.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace resorep {

struct FormatInfo {
    bool known = false;
    bool blockCompressed = false;
    uint32_t bytesPerBlock = 0;  // bytes per 4x4 block (BC) or per pixel
};
FormatInfo GetFormatInfo(DXGI_FORMAT format);
const char* FormatName(DXGI_FORMAT format);

// Row pitch / row count for one mip of a 2D surface.
struct SurfaceLayout {
    uint32_t rowPitch = 0;
    uint32_t rows = 0;
    size_t size() const { return size_t(rowPitch) * rows; }
};
bool ComputeSurfaceLayout(DXGI_FORMAT format, uint32_t width, uint32_t height, SurfaceLayout& out);

struct DdsSubresource {
    size_t offset;  // into DdsImage::data
    uint32_t rowPitch;
    uint32_t slicePitch;
};

// A parsed .dds. Subresources are ordered array-slice-major, like D3D11 expects.
struct DdsImage {
    uint32_t width = 0, height = 0, depth = 1;
    uint32_t mipLevels = 1, arraySize = 1;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool isCubemap = false;
    bool isVolume = false;
    std::vector<DdsSubresource> subresources;
    std::vector<uint8_t> data;  // whole file; subresource offsets point into it
};

// Accepts plain DDS bytes (legacy or DX10 header). Takes ownership of `file`.
bool ParseDds(std::vector<uint8_t>&& file, DdsImage& out, std::string* error = nullptr);

// Reads a file from disk, decrypting it first if it is a resorep1 container.
bool LoadTextureFile(const std::wstring& path, DdsImage& out, bool* wasEncrypted = nullptr,
                     std::string* error = nullptr);

// Serialises 2D mips into a .dds. Uses a legacy header when an equivalent exists so older
// editors can open the file, otherwise a DX10 header.
struct MipData {
    const void* data;
    uint32_t rowPitch;  // source row pitch (may include padding)
};
std::vector<uint8_t> WriteDds(DXGI_FORMAT format, uint32_t width, uint32_t height,
                              const std::vector<MipData>& mips);

}  // namespace resorep
