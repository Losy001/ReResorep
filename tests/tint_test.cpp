// Checks the tint feature: each writable format survives encode -> decode close to the input
// (decoded on the GPU, or WARP), TintPixels' modes, and a whole file tinted with every mip.
// Run: build\Release\tint_test.exe
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "bc_encode.h"
#include "dds.h"
#include "png_export.h"
#include "tint.h"

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

// A smooth two-colour picture with a few sharp edges and, if `alpha`, a gradient in alpha.
static std::vector<float> Picture(uint32_t w, uint32_t h, bool alpha) {
    std::vector<float> px(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            float* p = &px[(size_t(y) * w + x) * 4];
            const float u = (x + 0.5f) / w, v = (y + 0.5f) / h;
            const bool stripe = (x / 8 + y / 8) % 5 == 0;
            p[0] = stripe ? 0.95f : 0.1f + 0.3f * u;
            p[1] = stripe ? 0.95f : 0.6f + 0.3f * v;
            p[2] = stripe ? 0.95f : 0.8f;
            p[3] = alpha ? v : 1.0f;
        }
    return px;
}

// Writes `px` with a full, box-filtered mip chain; `levels` gets each mip's pixels as written.
static fs::path Save(const fs::path& path, DXGI_FORMAT fmt, uint32_t w, uint32_t h, const std::vector<float>& px,
                     std::vector<std::vector<float>>* levels = nullptr) {
    std::vector<std::vector<uint8_t>> mips;
    std::vector<MipData> data;
    std::vector<float> level = px;
    for (uint32_t mw = w, mh = h;; mw = std::max(1u, mw / 2), mh = std::max(1u, mh / 2)) {
        if (levels) levels->push_back(level);
        mips.push_back(EncodeSurface(fmt, mw, mh, level.data()));
        SurfaceLayout layout;
        ComputeSurfaceLayout(fmt, mw, mh, layout);
        data.push_back({mips.back().data(), layout.rowPitch});
        if (mw == 1 && mh == 1) break;
        // Box-filtered next mip.
        const uint32_t nw = std::max(1u, mw / 2), nh = std::max(1u, mh / 2);
        std::vector<float> next(size_t(nw) * nh * 4);
        for (uint32_t y = 0; y < nh; ++y)
            for (uint32_t x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c) {
                    float sum = 0;
                    for (uint32_t dy = 0; dy < 2; ++dy)
                        for (uint32_t dx = 0; dx < 2; ++dx)
                            sum += level[((std::min(y * 2 + dy, mh - 1)) * mw + std::min(x * 2 + dx, mw - 1)) * 4 + c];
                    next[(size_t(y) * nw + x) * 4 + c] = sum / 4;
                }
        level.swap(next);
    }
    const auto dds = WriteDds(fmt, w, h, data);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(dds.data()), std::streamsize(dds.size()));
    return path;
}

// Root-mean-square difference (0..1 scale) over the channels that matter.
static float Rms(const std::vector<float>& a, const std::vector<float>& b, int channels) {
    double sum = 0;
    size_t n = 0;
    for (size_t i = 0; i < a.size(); i += 4)
        for (int c = 0; c < channels; ++c, ++n) sum += double(a[i + c] - b[i + c]) * (a[i + c] - b[i + c]);
    return float(std::sqrt(sum / double(n)));
}

static void TestRoundTrip(const fs::path& dir) {
    struct Case {
        DXGI_FORMAT format;
        bool alpha;
        float maxRms;
    };
    const Case cases[] = {
        {DXGI_FORMAT_R8G8B8A8_UNORM, true, 0.003f}, {DXGI_FORMAT_B8G8R8A8_UNORM, true, 0.003f},
        {DXGI_FORMAT_BC1_UNORM, false, 0.04f},      {DXGI_FORMAT_BC2_UNORM_SRGB, true, 0.04f},
        {DXGI_FORMAT_BC3_UNORM, true, 0.04f},       {DXGI_FORMAT_BC7_UNORM, true, 0.03f},
        {DXGI_FORMAT_BC7_UNORM_SRGB, false, 0.03f},
    };
    constexpr uint32_t w = 64, h = 32;
    const std::vector<float> src = Picture(w, h, true);
    for (const Case& c : cases) {
        std::vector<float> want = src;
        if (!c.alpha)
            for (size_t i = 3; i < want.size(); i += 4) want[i] = 1.0f;
        std::vector<std::vector<float>> levels;
        const fs::path file =
            Save(dir / ("format" + std::to_string(int(c.format)) + ".dds"), c.format, w, h, want, &levels);
        DdsImage img;
        std::vector<std::vector<float>> mips;
        CHECK(LoadTextureFile(file.wstring(), img));
        CHECK(img.format == c.format && img.mipLevels == 7);
        const std::string e = DecodeMips(img, img.mipLevels, mips);
        if (!e.empty()) std::printf("%s: %s\n", FormatName(c.format), e.c_str());
        CHECK(e.empty() && mips.size() == 7);
        if (mips.size() != levels.size()) continue;
        // Every mip, down to 1x1, decodes to what was encoded into it.
        std::printf("%-12s rms per mip:", FormatName(c.format));
        for (size_t m = 0; m < mips.size(); ++m) {
            CHECK(mips[m].size() == levels[m].size());
            if (mips[m].size() != levels[m].size()) continue;
            const float rms = Rms(mips[m], levels[m], 4);
            std::printf(" %.4f", rms);
            // Below 16 px wide one block holds stripes, gradient and their blend at once, more
            // than a single line of colours can, so those mips get a looser bound.
            CHECK(rms <= ((w >> m) < 16 ? 0.05f : c.maxRms));
        }
        std::printf("\n");
        CHECK(mips[6].size() == 4);  // 1x1
    }
}

static void TestModes() {
    float cyan[4] = {0.1f, 0.8f, 0.9f, 0.5f}, white[4] = {1, 1, 1, 1}, black[4] = {0, 0, 0, 1};
    TintSettings s;
    s.color[0] = 1, s.color[1] = 0, s.color[2] = 0;
    TintPixels(s, cyan, 1);
    TintPixels(s, white, 1);
    TintPixels(s, black, 1);
    // Recolor: cyan turns red with the same brightness; white and black stay; alpha is kept.
    CHECK(std::fabs(cyan[0] - 0.9f) < 0.01f && cyan[1] < 0.15f && cyan[2] < 0.15f && cyan[3] == 0.5f);
    CHECK(white[0] == 1 && white[1] == 1 && white[2] == 1);
    CHECK(black[0] == 0 && black[1] == 0 && black[2] == 0);

    float grey[4] = {0.5f, 0.5f, 0.5f, 1};
    s.mode = TintSettings::Mode::Colorize;
    TintPixels(s, grey, 1);
    CHECK(grey[0] > 0.9f && grey[1] < 0.05f && grey[2] < 0.05f);  // grey also takes the colour

    float half[4] = {0.5f, 0.5f, 0.5f, 1};
    s.mode = TintSettings::Mode::Multiply;
    s.color[1] = 0.5f;
    s.strength = 0.5f;
    TintPixels(s, half, 1);
    CHECK(std::fabs(half[0] - 0.5f) < 1e-4f && std::fabs(half[1] - 0.375f) < 1e-4f && std::fabs(half[2] - 0.25f) < 1e-4f);
}

static void TestTintFile(const fs::path& dir) {
    constexpr uint32_t w = 64, h = 64;
    std::vector<std::vector<float>> levels;
    const fs::path in = Save(dir / L"in.dds", DXGI_FORMAT_BC7_UNORM_SRGB, w, h, Picture(w, h, false), &levels);
    const fs::path out = dir / L"out.dds";
    TintSettings s;
    s.color[0] = 1, s.color[1] = 0.1f, s.color[2] = 0.1f;
    CHECK(TintTexture(in, out, s).empty());
    DdsImage img;
    CHECK(LoadTextureFile(out.wstring(), img));
    CHECK(img.format == DXGI_FORMAT_BC7_UNORM_SRGB && img.width == w && img.height == h && img.mipLevels == 7);
    std::vector<std::vector<float>> mips;
    CHECK(DecodeMips(img, img.mipLevels, mips).empty());
    if (mips.size() == 7) {
        // The cyan parts are red now, every mip; the white stripes stay white.
        for (size_t m = 0; m < mips.size(); ++m) {
            const auto& mip = mips[m];
            double r = 0, g = 0;
            for (size_t i = 0; i < mip.size(); i += 4) r += mip[i], g += mip[i + 1];
            CHECK(r > g);
            // And each mip is its own original mip, tinted (two BC7 passes of loss allowed).
            std::vector<float> want = levels[m];
            TintPixels(s, want.data(), want.size() / 4);
            const float rms = mip.size() == want.size() ? Rms(mip, want, 3) : 1.0f;
            std::printf("tinted mip %zu rms %.4f\n", m, rms);
            CHECK(rms <= 0.05f);
        }
        const float* stripe = &mips[0][0];  // (0, 0) is a stripe
        CHECK(stripe[0] > 0.9f && stripe[1] > 0.9f && stripe[2] > 0.9f);
    }

    // Formats that can't be written back are refused with a reason.
    CHECK(TintUnsupported(DXGI_FORMAT_BC5_UNORM) != nullptr);
    CHECK(TintUnsupported(DXGI_FORMAT_BC6H_UF16) != nullptr);
    CHECK(TintUnsupported(DXGI_FORMAT_BC7_UNORM) == nullptr);
}

int main() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const fs::path dir = fs::path(tmp) / L"reresorep_tint_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    TestRoundTrip(dir);
    TestModes();
    TestTintFile(dir);
    fs::remove_all(dir);
    std::printf(g_failures ? "%d failure(s)\n" : "tint_test: all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
