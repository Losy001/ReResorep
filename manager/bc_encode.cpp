#include "bc_encode.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <future>
#include <thread>

#include "dds.h"

namespace resorep::ui {
namespace {

enum class Kind { None, BC1, BC2, BC3, BC7, RGBA8, BGRA8, BGRX8 };

Kind KindOf(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: return Kind::BC1;
        case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: return Kind::BC2;
        case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB: return Kind::BC3;
        case DXGI_FORMAT_BC7_TYPELESS: case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB: return Kind::BC7;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return Kind::RGBA8;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return Kind::BGRA8;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS: case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            return Kind::BGRX8;
        default: return Kind::None;
    }
}

using Block = float[16][4];  // 4x4 pixels, row by row, RGBA 0..1

uint8_t ToByte(float v) { return uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

float Dist(const float* a, const float* b, int channels) {
    float d = 0;
    for (int c = 0; c < channels; ++c) d += (a[c] - b[c]) * (a[c] - b[c]);
    return d;
}

// The line the used pixels lie closest to: their mean and main direction (power iteration on
// the covariance). Endpoints are where the pixels' projections onto it start and end.
void FitLine(const Block px, const bool* use, int channels, float lo[4], float hi[4]) {
    float mean[4] = {}, n = 0;
    for (int i = 0; i < 16; ++i)
        if (use[i]) {
            for (int c = 0; c < channels; ++c) mean[c] += px[i][c];
            ++n;
        }
    for (int c = 0; c < channels; ++c) mean[c] /= std::max(n, 1.0f);
    float cov[4][4] = {};
    for (int i = 0; i < 16; ++i)
        if (use[i])
            for (int a = 0; a < channels; ++a)
                for (int b = 0; b < channels; ++b) cov[a][b] += (px[i][a] - mean[a]) * (px[i][b] - mean[b]);
    // Start from the channel that varies most, so the iteration can't begin at right angles.
    int widest = 0;
    for (int c = 1; c < channels; ++c)
        if (cov[c][c] > cov[widest][widest]) widest = c;
    float axis[4] = {};
    for (int c = 0; c < channels; ++c) axis[c] = cov[widest][c];
    for (int iter = 0; iter < 8; ++iter) {
        float next[4] = {}, len = 0;
        for (int a = 0; a < channels; ++a) {
            for (int b = 0; b < channels; ++b) next[a] += cov[a][b] * axis[b];
            len += next[a] * next[a];
        }
        if (len < 1e-12f) break;
        len = std::sqrt(len);
        for (int c = 0; c < channels; ++c) axis[c] = next[c] / len;
    }
    float tMin = 0, tMax = 0;
    for (int i = 0; i < 16; ++i) {
        if (!use[i]) continue;
        float t = 0;
        for (int c = 0; c < channels; ++c) t += (px[i][c] - mean[c]) * axis[c];
        tMin = std::min(tMin, t);
        tMax = std::max(tMax, t);
    }
    for (int c = 0; c < channels; ++c) {
        lo[c] = std::clamp(mean[c] + axis[c] * tMin, 0.0f, 1.0f);
        hi[c] = std::clamp(mean[c] + axis[c] * tMax, 0.0f, 1.0f);
    }
}

// Least-squares endpoints for pixels at the given weights (0 = a, 1 = b) along the line.
bool RefitLine(const Block px, const bool* use, const float* w, int channels, float a[4], float b[4]) {
    float aa = 0, ab = 0, bb = 0, ax[4] = {}, bx[4] = {};
    for (int i = 0; i < 16; ++i) {
        if (!use[i]) continue;
        const float wa = 1 - w[i], wb = w[i];
        aa += wa * wa;
        ab += wa * wb;
        bb += wb * wb;
        for (int c = 0; c < channels; ++c) {
            ax[c] += wa * px[i][c];
            bx[c] += wb * px[i][c];
        }
    }
    const float det = aa * bb - ab * ab;
    if (std::fabs(det) < 1e-6f) return false;
    for (int c = 0; c < channels; ++c) {
        a[c] = std::clamp((ax[c] * bb - bx[c] * ab) / det, 0.0f, 1.0f);
        b[c] = std::clamp((bx[c] * aa - ax[c] * ab) / det, 0.0f, 1.0f);
    }
    return true;
}

// --- BC1 colour (also the colour half of BC2/BC3) ---

uint16_t To565(const float* c) {
    return uint16_t((std::lround(std::clamp(c[0], 0.0f, 1.0f) * 31) << 11) |
                    (std::lround(std::clamp(c[1], 0.0f, 1.0f) * 63) << 5) | std::lround(std::clamp(c[2], 0.0f, 1.0f) * 31));
}

void From565(uint16_t v, float* c) {
    const uint32_t r = v >> 11, g = (v >> 5) & 63, b = v & 31;
    c[0] = float((r << 3) | (r >> 2)) / 255.0f;
    c[1] = float((g << 2) | (g >> 4)) / 255.0f;
    c[2] = float((b << 3) | (b >> 2)) / 255.0f;
}

struct ColorFit {
    uint16_t c0 = 0, c1 = 0;
    uint8_t index[16] = {};
    float error = 0;
};

// The nearest palette entry for each pixel. threeColor: c0 <= c1 mode, where index 3 is
// transparent black and `opaque` says which pixels need it.
ColorFit EvaluateColor(const Block px, const bool* opaque, uint16_t c0, uint16_t c1, bool threeColor) {
    ColorFit fit;
    fit.c0 = c0;
    fit.c1 = c1;
    float pal[4][4];
    From565(c0, pal[0]);
    From565(c1, pal[1]);
    for (int c = 0; c < 3; ++c) {
        pal[2][c] = threeColor ? (pal[0][c] + pal[1][c]) / 2 : (2 * pal[0][c] + pal[1][c]) / 3;
        pal[3][c] = threeColor ? 0 : (pal[0][c] + 2 * pal[1][c]) / 3;
    }
    for (int i = 0; i < 16; ++i) {
        if (!opaque[i]) {
            fit.index[i] = 3;
            continue;
        }
        int best = 0;
        float bestD = Dist(px[i], pal[0], 3);
        for (int k = 1; k < (threeColor ? 3 : 4); ++k)
            if (const float d = Dist(px[i], pal[k], 3); d < bestD) bestD = d, best = k;
        fit.index[i] = uint8_t(best);
        fit.error += bestD;
    }
    return fit;
}

void EncodeColor(const Block px, bool allowTransparent, uint8_t* out) {
    bool opaque[16];
    int opaqueCount = 0;
    for (int i = 0; i < 16; ++i) opaqueCount += opaque[i] = !allowTransparent || px[i][3] >= 0.5f;
    const bool threeColor = opaqueCount < 16;
    ColorFit best;
    if (opaqueCount) {
        float lo[4], hi[4];
        FitLine(px, opaque, 3, lo, hi);
        best = EvaluateColor(px, opaque, To565(hi), To565(lo), threeColor);
        static constexpr float kWeight4[4] = {0, 1, 1.0f / 3, 2.0f / 3}, kWeight3[4] = {0, 1, 0.5f, 0};
        for (int iter = 0; iter < 2; ++iter) {
            float w[16], a[4], b[4];
            for (int i = 0; i < 16; ++i) w[i] = (threeColor ? kWeight3 : kWeight4)[best.index[i]];
            if (!RefitLine(px, opaque, w, 3, a, b)) break;
            const ColorFit next = EvaluateColor(px, opaque, To565(a), To565(b), threeColor);
            if (next.error >= best.error) break;
            best = next;
        }
    } else {
        std::fill(std::begin(best.index), std::end(best.index), uint8_t(3));
    }
    // The decoder picks the mode from the endpoints' order: c0 > c1 is four colours.
    if (threeColor ? best.c0 > best.c1 : best.c0 < best.c1) {
        std::swap(best.c0, best.c1);
        for (uint8_t& k : best.index)
            if (k < 2) k ^= 1;
            else if (!threeColor) k ^= 1;  // 2 <-> 3
    }
    uint32_t bits = 0;
    for (int i = 0; i < 16; ++i) bits |= uint32_t(best.index[i]) << (2 * i);
    std::memcpy(out, &best.c0, 2);
    std::memcpy(out + 2, &best.c1, 2);
    std::memcpy(out + 4, &bits, 4);
}

// --- BC2 / BC3 alpha ---

void EncodeAlpha4(const Block px, uint8_t* out) {
    uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) bits |= uint64_t(std::lround(std::clamp(px[i][3], 0.0f, 1.0f) * 15)) << (4 * i);
    std::memcpy(out, &bits, 8);
}

void EncodeAlpha8(const Block px, uint8_t* out) {
    float lo = 1, hi = 0;
    for (int i = 0; i < 16; ++i) lo = std::min(lo, px[i][3]), hi = std::max(hi, px[i][3]);
    const uint8_t a0 = ToByte(hi), a1 = ToByte(lo);
    uint64_t bits = 0;
    if (a0 > a1) {  // eight-value mode; equal endpoints leave every index at 0
        float pal[8] = {float(a0), float(a1)};
        for (int k = 2; k < 8; ++k) pal[k] = ((8 - k) * a0 + (k - 1) * a1) / 7.0f;
        for (int i = 0; i < 16; ++i) {
            const float v = std::clamp(px[i][3], 0.0f, 1.0f) * 255;
            int best = 0;
            for (int k = 1; k < 8; ++k)
                if (std::fabs(pal[k] - v) < std::fabs(pal[best] - v)) best = k;
            bits |= uint64_t(best) << (3 * i);
        }
    }
    out[0] = a0;
    out[1] = a1;
    std::memcpy(out + 2, &bits, 6);
}

// --- BC7 mode 6: one RGBA line, 7-bit endpoints plus a shared low bit each, 4-bit indices ---

constexpr int kBc7Weights[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

struct Bc7Fit {
    uint8_t q[2][4] = {};  // 7-bit endpoints
    uint8_t p[2] = {};     // their low bits
    uint8_t index[16] = {};
    float error = 0;
};

// Rounds an endpoint to 7 bits + low bit, choosing the low bit that lands closer.
void QuantizeBc7(const float* e, uint8_t* q, uint8_t& p) {
    float bestErr = 1e30f;
    for (int pb = 0; pb < 2; ++pb) {
        uint8_t cand[4];
        float err = 0;
        for (int c = 0; c < 4; ++c) {
            const float v = std::clamp(e[c], 0.0f, 1.0f) * 255;
            cand[c] = uint8_t(std::clamp<long>(std::lround((v - pb) / 2), 0, 127));
            const float back = float((cand[c] << 1) | pb);
            err += (back - v) * (back - v);
        }
        if (err < bestErr) {
            bestErr = err;
            std::memcpy(q, cand, 4);
            p = uint8_t(pb);
        }
    }
}

Bc7Fit EvaluateBc7(const Block px, const float* a, const float* b) {
    Bc7Fit fit;
    QuantizeBc7(a, fit.q[0], fit.p[0]);
    QuantizeBc7(b, fit.q[1], fit.p[1]);
    float pal[16][4];
    for (int k = 0; k < 16; ++k)
        for (int c = 0; c < 4; ++c) {
            const int e0 = (fit.q[0][c] << 1) | fit.p[0], e1 = (fit.q[1][c] << 1) | fit.p[1];
            pal[k][c] = float(((64 - kBc7Weights[k]) * e0 + kBc7Weights[k] * e1 + 32) >> 6) / 255.0f;
        }
    for (int i = 0; i < 16; ++i) {
        int best = 0;
        float bestD = Dist(px[i], pal[0], 4);
        for (int k = 1; k < 16; ++k)
            if (const float d = Dist(px[i], pal[k], 4); d < bestD) bestD = d, best = k;
        fit.index[i] = uint8_t(best);
        fit.error += bestD;
    }
    return fit;
}

struct BitWriter {
    uint8_t* out;
    int pos = 0;
    void Put(uint32_t value, int bits) {
        for (int i = 0; i < bits; ++i, ++pos)
            if (value >> i & 1) out[pos >> 3] |= uint8_t(1u << (pos & 7));
    }
};

// --- BC7 mode 5: colour and alpha on lines of their own (7-bit RGB, 8-bit alpha, 2-bit indices
// each), for blocks whose alpha doesn't follow their colour ---

constexpr int kBc7Weights2[4] = {0, 21, 43, 64};

struct Bc7SplitFit {
    uint8_t rgb[2][3] = {};  // 7-bit
    uint8_t alpha[2] = {};
    uint8_t colorIndex[16] = {}, alphaIndex[16] = {};
    float error = 0;
};

uint8_t Quantize7(float v) { return uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 127)); }
int Expand7(uint8_t q) { return (q << 1) | (q >> 6); }

Bc7SplitFit EvaluateBc7Split(const Block px, const float* a, const float* b, float alphaA, float alphaB) {
    Bc7SplitFit fit;
    for (int c = 0; c < 3; ++c) fit.rgb[0][c] = Quantize7(a[c]), fit.rgb[1][c] = Quantize7(b[c]);
    fit.alpha[0] = ToByte(alphaA);
    fit.alpha[1] = ToByte(alphaB);
    float pal[4][3], alphaPal[4];
    for (int k = 0; k < 4; ++k) {
        const int w = kBc7Weights2[k];
        for (int c = 0; c < 3; ++c)
            pal[k][c] = float(((64 - w) * Expand7(fit.rgb[0][c]) + w * Expand7(fit.rgb[1][c]) + 32) >> 6) / 255.0f;
        alphaPal[k] = float(((64 - w) * fit.alpha[0] + w * fit.alpha[1] + 32) >> 6) / 255.0f;
    }
    for (int i = 0; i < 16; ++i) {
        int best = 0, bestA = 0;
        float bestD = Dist(px[i], pal[0], 3), bestDA = std::fabs(px[i][3] - alphaPal[0]);
        for (int k = 1; k < 4; ++k) {
            if (const float d = Dist(px[i], pal[k], 3); d < bestD) bestD = d, best = k;
            if (const float d = std::fabs(px[i][3] - alphaPal[k]); d < bestDA) bestDA = d, bestA = k;
        }
        fit.colorIndex[i] = uint8_t(best);
        fit.alphaIndex[i] = uint8_t(bestA);
        fit.error += bestD + bestDA * bestDA;
    }
    return fit;
}

Bc7SplitFit FitBc7Split(const Block px) {
    bool all[16];
    std::fill(std::begin(all), std::end(all), true);
    float lo[4], hi[4];
    FitLine(px, all, 3, lo, hi);
    float alphaLo = 1, alphaHi = 0;
    for (int i = 0; i < 16; ++i) alphaLo = std::min(alphaLo, px[i][3]), alphaHi = std::max(alphaHi, px[i][3]);
    Bc7SplitFit best = EvaluateBc7Split(px, lo, hi, alphaLo, alphaHi);
    for (int iter = 0; iter < 2; ++iter) {
        float w[16], a[4], b[4];
        for (int i = 0; i < 16; ++i) w[i] = kBc7Weights2[best.colorIndex[i]] / 64.0f;
        if (!RefitLine(px, all, w, 3, a, b)) break;
        // Alpha's own least-squares line: its values moved into channel 0.
        Block alphaOnly;
        float aw[16], aa[4] = {alphaLo}, ab[4] = {alphaHi};
        for (int i = 0; i < 16; ++i) alphaOnly[i][0] = px[i][3], aw[i] = kBc7Weights2[best.alphaIndex[i]] / 64.0f;
        RefitLine(alphaOnly, all, aw, 1, aa, ab);
        const Bc7SplitFit next = EvaluateBc7Split(px, a, b, aa[0], ab[0]);
        if (next.error >= best.error) break;
        best = next;
    }
    return best;
}

void WriteBc7Split(Bc7SplitFit fit, uint8_t* out) {
    // Each set's first index is stored with 1 bit, so its top bit must be 0.
    if (fit.colorIndex[0] >= 2) {
        std::swap(fit.rgb[0], fit.rgb[1]);
        for (uint8_t& k : fit.colorIndex) k = uint8_t(3 - k);
    }
    if (fit.alphaIndex[0] >= 2) {
        std::swap(fit.alpha[0], fit.alpha[1]);
        for (uint8_t& k : fit.alphaIndex) k = uint8_t(3 - k);
    }
    std::memset(out, 0, 16);
    BitWriter w{out};
    w.Put(1u << 5, 6);  // mode 5
    w.Put(0, 2);        // no channel rotation
    for (int c = 0; c < 3; ++c) {
        w.Put(fit.rgb[0][c], 7);
        w.Put(fit.rgb[1][c], 7);
    }
    w.Put(fit.alpha[0], 8);
    w.Put(fit.alpha[1], 8);
    w.Put(fit.colorIndex[0], 1);
    for (int i = 1; i < 16; ++i) w.Put(fit.colorIndex[i], 2);
    w.Put(fit.alphaIndex[0], 1);
    for (int i = 1; i < 16; ++i) w.Put(fit.alphaIndex[i], 2);
}

void EncodeBc7(const Block px, uint8_t* out) {
    bool all[16];
    std::fill(std::begin(all), std::end(all), true);
    float lo[4], hi[4];
    FitLine(px, all, 4, lo, hi);
    Bc7Fit best = EvaluateBc7(px, lo, hi);
    for (int iter = 0; iter < 2; ++iter) {
        float w[16], a[4], b[4];
        for (int i = 0; i < 16; ++i) w[i] = kBc7Weights[best.index[i]] / 64.0f;
        if (!RefitLine(px, all, w, 4, a, b)) break;
        const Bc7Fit next = EvaluateBc7(px, a, b);
        if (next.error >= best.error) break;
        best = next;
    }
    if (const Bc7SplitFit split = FitBc7Split(px); split.error < best.error) {
        WriteBc7Split(split, out);
        return;
    }
    // The first pixel's index is stored with 3 bits, so its top bit must be 0.
    if (best.index[0] >= 8) {
        std::swap(best.q[0], best.q[1]);
        std::swap(best.p[0], best.p[1]);
        for (uint8_t& k : best.index) k = uint8_t(15 - k);
    }
    std::memset(out, 0, 16);
    BitWriter w{out};
    w.Put(1u << 6, 7);  // mode 6
    for (int c = 0; c < 4; ++c) {
        w.Put(best.q[0][c], 7);
        w.Put(best.q[1][c], 7);
    }
    w.Put(best.p[0], 1);
    w.Put(best.p[1], 1);
    w.Put(best.index[0], 3);
    for (int i = 1; i < 16; ++i) w.Put(best.index[i], 4);
}

}  // namespace

bool CanEncode(DXGI_FORMAT format) { return KindOf(format) != Kind::None; }

std::vector<uint8_t> EncodeSurface(DXGI_FORMAT format, uint32_t width, uint32_t height, const float* rgba) {
    const Kind kind = KindOf(format);
    SurfaceLayout layout;
    if (kind == Kind::None || !ComputeSurfaceLayout(format, width, height, layout)) return {};
    std::vector<uint8_t> out(layout.size());

    if (kind == Kind::RGBA8 || kind == Kind::BGRA8 || kind == Kind::BGRX8) {
        const bool bgr = kind != Kind::RGBA8;
        for (size_t i = 0; i < size_t(width) * height; ++i) {
            const float* c = rgba + i * 4;
            uint8_t* o = &out[i * 4];
            o[0] = ToByte(c[bgr ? 2 : 0]);
            o[1] = ToByte(c[1]);
            o[2] = ToByte(c[bgr ? 0 : 2]);
            o[3] = kind == Kind::BGRX8 ? 255 : ToByte(c[3]);
        }
        return out;
    }

    const uint32_t blockBytes = kind == Kind::BC1 ? 8 : 16;
    auto encodeRows = [&](uint32_t firstRow, uint32_t endRow) {
        for (uint32_t by = firstRow; by < endRow; ++by)
            for (uint32_t bx = 0; bx < layout.rowPitch / blockBytes; ++bx) {
                Block px;
                // Blocks hanging over the edge repeat the last row/column.
                for (int i = 0; i < 16; ++i) {
                    const uint32_t x = std::min(bx * 4 + (i & 3), width - 1), y = std::min(by * 4 + (i >> 2), height - 1);
                    std::memcpy(px[i], rgba + (size_t(y) * width + x) * 4, sizeof(px[i]));
                }
                uint8_t* o = &out[size_t(by) * layout.rowPitch + size_t(bx) * blockBytes];
                switch (kind) {
                    case Kind::BC1: EncodeColor(px, true, o); break;
                    case Kind::BC2: EncodeAlpha4(px, o), EncodeColor(px, false, o + 8); break;
                    case Kind::BC3: EncodeAlpha8(px, o), EncodeColor(px, false, o + 8); break;
                    default: EncodeBc7(px, o); break;
                }
            }
    };
    // Rows of blocks are independent: spread them over the CPU's cores.
    const uint32_t threads = std::clamp(std::thread::hardware_concurrency(), 1u, 16u);
    const uint32_t per = (layout.rows + threads - 1) / threads;
    std::vector<std::future<void>> jobs;
    for (uint32_t first = 0; first < layout.rows; first += per)
        jobs.push_back(std::async(std::launch::async, encodeRows, first, std::min(first + per, layout.rows)));
    for (auto& job : jobs) job.get();
    return out;
}

}  // namespace resorep::ui
