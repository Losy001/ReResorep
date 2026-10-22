// Self-contained checks for the core library. Run: build\bin\core_tests.exe
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "blowfish.h"
#include "dds.h"
#include "resorep_crypto.h"
#include "resorep_hash.h"
#include "texture_index.h"

using namespace resorep;

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

static std::vector<uint8_t> Hex(const char* s) {
    std::vector<uint8_t> out;
    for (; s[0] && s[1]; s += 2) out.push_back(uint8_t(std::stoul(std::string(s, 2), nullptr, 16)));
    return out;
}

static void TestBlowfishEcb() {
    // Eric Young's published Blowfish test vectors.
    struct V { const char *key, *plain, *cipher; };
    const V vectors[] = {
        {"0000000000000000", "0000000000000000", "4EF997456198DD78"},
        {"FFFFFFFFFFFFFFFF", "FFFFFFFFFFFFFFFF", "51866FD5B85ECB8A"},
        {"3000000000000000", "1000000000000001", "7D856F9A613063F2"},
        {"0123456789ABCDEF", "1111111111111111", "61F9C3802281B096"},
    };
    for (const V& v : vectors) {
        auto key = Hex(v.key), pt = Hex(v.plain), ct = Hex(v.cipher);
        Blowfish bf(key.data(), key.size());
        uint32_t l = (pt[0] << 24) | (pt[1] << 16) | (pt[2] << 8) | pt[3];
        uint32_t r = (pt[4] << 24) | (pt[5] << 16) | (pt[6] << 8) | pt[7];
        bf.EncryptBlock(l, r);
        CHECK(l == uint32_t((ct[0] << 24) | (ct[1] << 16) | (ct[2] << 8) | ct[3]));
        CHECK(r == uint32_t((ct[4] << 24) | (ct[5] << 16) | (ct[6] << 8) | ct[7]));
        bf.DecryptBlock(l, r);
        CHECK(l == uint32_t((pt[0] << 24) | (pt[1] << 16) | (pt[2] << 8) | pt[3]));
    }
}

static void TestBlowfishCbc() {
    // CBC vector from the same test suite (16-byte key, as Resorep uses).
    auto key = Hex("0123456789ABCDEFF0E1D2C3B4A59687");
    auto iv = Hex("FEDCBA9876543210");
    std::vector<uint8_t> data(32, 0);
    std::memcpy(data.data(), "7654321 Now is the time for ", 28);
    const auto expected = Hex("6B77B4D63006DEE605B156E27403979358DEB9E7154616D959F1652BD5FF92CC");
    Blowfish bf(key.data(), key.size());
    auto copy = data;
    bf.EncryptCbc(iv.data(), copy.data(), copy.size());
    CHECK(copy == expected);
    bf.DecryptCbc(iv.data(), copy.data(), copy.size());
    CHECK(copy == data);
}

static void TestHash() {
    CHECK(ComputeTextureHash(nullptr, 0) == 1);
    const uint8_t neg[] = {0x80};
    CHECK(ComputeTextureHash(neg, 1) == uint32_t(1 - 128));
    const uint8_t mixed[] = {1, 2, 0xFF, 0x7F};
    CHECK(ComputeTextureHash(mixed, 4) == uint32_t(1 + 1 + 2 - 1 + 127));

    std::mt19937 rng(1234);
    for (size_t len : {1u, 15u, 16u, 17u, 31u, 1000u, 65536u + 7u}) {
        std::vector<uint8_t> buf(len);
        for (auto& b : buf) b = uint8_t(rng());
        CHECK(ComputeTextureHash(buf.data(), len) == ComputeTextureHashScalar(buf.data(), len));
    }
}

static void TestContainer() {
    std::vector<uint8_t> dds(1000);
    for (size_t i = 0; i < dds.size(); ++i) dds[i] = uint8_t(i * 7);
    const uint8_t iv[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const auto enc = EncryptTexture(dds.data(), dds.size(), iv);
    CHECK(IsEncryptedTexture(enc.data(), enc.size()));
    CHECK(std::memcmp(enc.data(), "resorep1", 8) == 0 && enc[8] == 8);
    std::vector<uint8_t> dec;
    CHECK(DecryptTexture(enc.data(), enc.size(), dec));
    CHECK(dec.size() >= dds.size() && std::memcmp(dec.data(), dds.data(), dds.size()) == 0);
    CHECK(!IsEncryptedTexture(dds.data(), dds.size()));
}

static void TestFileNames() {
    CHECK(ParseModFileName(L"12345.dds") == 12345u);
    CHECK(ParseModFileName(L"12345~hair.DDS") == 12345u);
    CHECK(ParseModFileName(L"4294967295.dds") == 4294967295u);
    CHECK(!ParseModFileName(L"4294967296.dds"));
    CHECK(!ParseModFileName(L"012.dds"));
    CHECK(ParseModFileName(L"0.dds") == 0u);
    CHECK(!ParseModFileName(L"123abc.dds"));
    CHECK(!ParseModFileName(L"123.png"));
    CHECK(!ParseModFileName(L"hair.dds"));
}

static void TestDdsRoundTrip() {
    for (DXGI_FORMAT fmt : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC7_UNORM_SRGB}) {
        const uint32_t w = 64, h = 32;
        std::vector<std::vector<uint8_t>> levels;
        std::vector<MipData> mips;
        for (uint32_t m = 0; m < 4; ++m) {
            SurfaceLayout l;
            CHECK(ComputeSurfaceLayout(fmt, w >> m, h >> m, l));
            levels.emplace_back(l.size(), uint8_t(m + 1));
        }
        for (uint32_t m = 0; m < 4; ++m) {
            SurfaceLayout l;
            ComputeSurfaceLayout(fmt, w >> m, h >> m, l);
            mips.push_back({levels[m].data(), l.rowPitch});
        }
        auto file = WriteDds(fmt, w, h, mips);
        DdsImage img;
        std::string err;
        CHECK(ParseDds(std::move(file), img, &err));
        CHECK(img.width == w && img.height == h && img.mipLevels == 4 && img.format == fmt);
        CHECK(img.subresources.size() == 4);
        for (uint32_t m = 0; m < img.subresources.size() && m < 4; ++m)
            CHECK(img.data[img.subresources[m].offset] == m + 1);
    }
}

int main() {
    TestBlowfishEcb();
    TestBlowfishCbc();
    TestHash();
    TestContainer();
    TestFileNames();
    TestDdsRoundTrip();
    std::printf(g_failures ? "%d check(s) failed\n" : "all checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
