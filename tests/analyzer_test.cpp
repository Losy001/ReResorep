// Runs the texture analyser (on the WARP software renderer) over synthetic textures:
// two colour patterns, the normal maps generated from them, and a depth map.
// Checks the detected kinds and that each colour texture matches its own normal map best.
#include <windows.h>

#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <vector>

#include "analyzer.h"
#include "dds.h"

namespace fs = std::filesystem;
using namespace resorep;
using namespace resorep::ui;

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

constexpr uint32_t kSize = 128;
using HeightFn = std::function<float(float, float)>;  // height 0..1 at (u, v)

static void Save(const fs::path& path, DXGI_FORMAT fmt, const std::vector<uint8_t>& px, uint32_t bpp) {
    const auto dds = WriteDds(fmt, kSize, kSize, {{px.data(), kSize * bpp}});
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(dds.data()), std::streamsize(dds.size()));
}

// Colour texture: two tints chosen by the height pattern.
static void WriteColor(const fs::path& path, const HeightFn& height, uint32_t tintA, uint32_t tintB) {
    std::vector<uint8_t> px(kSize * kSize * 4);
    for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x) {
            const uint32_t c = height((x + 0.5f) / kSize, (y + 0.5f) / kSize) > 0.5f ? tintA : tintB;
            std::memcpy(&px[(y * kSize + x) * 4], &c, 4);
        }
    Save(path, DXGI_FORMAT_R8G8B8A8_UNORM, px, 4);
}

// Tangent-space normal map from the same height pattern (softened so slopes are wide).
static void WriteNormal(const fs::path& path, const HeightFn& height, bool twoChannel) {
    auto h = [&](float u, float v) {
        float s = 0;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) s += height(u + dx * 1.5f / kSize, v + dy * 1.5f / kSize);
        return s / 25;
    };
    const uint32_t bpp = twoChannel ? 2 : 4;
    std::vector<uint8_t> px(kSize * kSize * bpp);
    for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x) {
            const float u = (x + 0.5f) / kSize, v = (y + 0.5f) / kSize, e = 1.0f / kSize;
            const float dx = (h(u + e, v) - h(u - e, v)) * 8, dy = (h(u, v + e) - h(u, v - e)) * 8;
            const float len = std::sqrt(dx * dx + dy * dy + 1);
            const float n[3] = {-dx / len, -dy / len, 1 / len};
            uint8_t* p = &px[(y * kSize + x) * bpp];
            for (uint32_t c = 0; c < bpp; ++c) p[c] = c < 3 ? uint8_t(std::lround((n[c] * 0.5f + 0.5f) * 255)) : 255;
        }
    Save(path, twoChannel ? DXGI_FORMAT_R8G8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM, px, bpp);
}

static void WriteDepth(const fs::path& path, const HeightFn& height) {
    std::vector<uint8_t> px(kSize * kSize);
    for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x) px[y * kSize + x] = uint8_t(height((x + 0.5f) / kSize, (y + 0.5f) / kSize) * 255);
    Save(path, DXGI_FORMAT_R8_UNORM, px, 1);
}

// UI icon: white shape on a transparent background.
static void WriteIcon(const fs::path& path, const HeightFn& shape) {
    std::vector<uint8_t> px(kSize * kSize * 4);
    for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x) {
            const bool on = shape((x + 0.5f) / kSize, (y + 0.5f) / kSize) > 0.5f;
            const uint32_t c = on ? 0xFFFFFFFFu : 0x00000000u;
            std::memcpy(&px[(y * kSize + x) * 4], &c, 4);
        }
    Save(path, DXGI_FORMAT_R8G8B8A8_UNORM, px, 4);
}

int main() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const fs::path dir = fs::path(tmp) / L"reresorep_analyzer_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // Pattern A: a few large plates. Pattern B: diagonal stripes.
    const HeightFn plates = [](float u, float v) {
        return float((u > 0.1f && u < 0.45f && v > 0.15f && v < 0.5f) || (u > 0.55f && u < 0.9f && v > 0.6f && v < 0.85f));
    };
    const HeightFn stripes = [](float u, float v) { return float(std::fmod((u + v) * 3.0f, 1.0f) > 0.5f); };

    WriteColor(dir / L"colorA.dds", plates, 0xFF3060C0, 0xFF202020);
    WriteNormal(dir / L"normalA.dds", plates, false);
    WriteColor(dir / L"colorB.dds", stripes, 0xFF40A0E0, 0xFF103010);
    WriteNormal(dir / L"normalB_rg.dds", stripes, true);
    WriteDepth(dir / L"depthA.dds", plates);
    WriteIcon(dir / L"icon.dds", plates);

    TextureAnalyzer analyzer(dir / L"cache.bin", /*useWarp=*/true);
    TextureTraits colorA, normalA, colorB, normalB, depthA;
    CHECK(analyzer.AnalyzeNow(dir / L"colorA.dds", colorA));
    CHECK(analyzer.AnalyzeNow(dir / L"normalA.dds", normalA));
    CHECK(analyzer.AnalyzeNow(dir / L"colorB.dds", colorB));
    CHECK(analyzer.AnalyzeNow(dir / L"normalB_rg.dds", normalB));
    CHECK(analyzer.AnalyzeNow(dir / L"depthA.dds", depthA));

    CHECK(colorA.kind == TextureKind::Color);
    CHECK(colorB.kind == TextureKind::Color);
    CHECK(normalA.kind == TextureKind::Normal);
    CHECK(normalB.kind == TextureKind::Normal);  // two-channel (BC5-style) normal map
    CHECK(depthA.kind == TextureKind::Grayscale);
    TextureTraits icon;
    CHECK(analyzer.AnalyzeNow(dir / L"icon.dds", icon));
    CHECK(icon.kind == TextureKind::Color);  // white art with transparency is not a depth map

    // Siege-style packed map: blue full, red empty, detail in green (ABGR in memory order).
    WriteColor(dir / L"packed.dds", plates, 0xFFFFC000, 0xFFFF3000);
    // Pale pink/white art also has blue near full, but not above red and green.
    WriteColor(dir / L"palepink.dds", plates, 0xFFF0E8FF, 0xFFFFFFFF);
    // Grey colour art (concrete, a logo) in a 4-channel format is not a depth map.
    WriteColor(dir / L"greyart.dds", plates, 0xFFB0B0B0, 0xFF303030);
    TextureTraits packed, palePink, greyArt;
    CHECK(analyzer.AnalyzeNow(dir / L"packed.dds", packed));
    CHECK(analyzer.AnalyzeNow(dir / L"palepink.dds", palePink));
    CHECK(analyzer.AnalyzeNow(dir / L"greyart.dds", greyArt));
    CHECK(packed.kind == TextureKind::Packed);
    CHECK(palePink.kind == TextureKind::Color);
    CHECK(greyArt.kind == TextureKind::Color);

    const float aa = DetailSimilarity(colorA, normalA), ab = DetailSimilarity(colorA, normalB);
    const float bb = DetailSimilarity(colorB, normalB), ba = DetailSimilarity(colorB, normalA);
    std::printf("similarity colorA~normalA %.2f, colorA~normalB %.2f, colorB~normalB %.2f, colorB~normalA %.2f\n", aa, ab,
                bb, ba);
    CHECK(aa > ab + 0.2f);
    CHECK(bb > ba + 0.2f);
    CHECK(aa > 0.4f && bb > 0.4f);

    fs::remove_all(dir);
    std::printf(g_failures ? "%d check(s) failed\n" : "all analyzer checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
