// End-to-end test of the built proxy: loads bin/proxy/d3d11.dll from a temp folder the
// way a game would, then checks replacement, encrypted packs, dumping and hot reload
// on a WARP device. Run: build\Release\proxy_test.exe <path to bin\proxy\d3d11.dll>
#include <windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "dds.h"
#include "integrity.h"
#include "mod_state.h"
#include "resorep_crypto.h"
#include "resorep_hash.h"

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

static void WriteFile(const fs::path& p, const std::vector<uint8_t>& data) {
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

static std::vector<uint8_t> SolidRgba(uint32_t w, uint32_t h, uint32_t rgba) {
    std::vector<uint8_t> px(size_t(w) * h * 4);
    for (size_t i = 0; i < px.size(); i += 4) std::memcpy(&px[i], &rgba, 4);
    return px;
}

static std::vector<uint8_t> SolidDds(uint32_t w, uint32_t h, uint32_t rgba) {
    auto px = SolidRgba(w, h, rgba);
    return resorep::WriteDds(DXGI_FORMAT_R8G8B8A8_UNORM, w, h, {{px.data(), w * 4}});
}

struct GameTexture {
    std::vector<uint8_t> pixels;
    uint32_t hash;
};

static GameTexture MakeGameTexture(uint32_t seed) {
    GameTexture t{SolidRgba(64, 64, 0xFF000000u | seed), 0};
    t.hash = resorep::ComputeTextureHash(t.pixels.data(), t.pixels.size());
    return t;
}

// Never throws: a missing file reads as no time, so a failed check can't end the test early.
static fs::file_time_type WriteTime(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    return ec ? fs::file_time_type{} : t;
}

static UINT CreateAndGetWidth(ID3D11Device* dev, const GameTexture& t) {
    D3D11_TEXTURE2D_DESC desc{64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_IMMUTABLE,
                              D3D11_BIND_SHADER_RESOURCE, 0, 0};
    D3D11_SUBRESOURCE_DATA init{t.pixels.data(), 64 * 4, UINT(t.pixels.size())};
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(dev->CreateTexture2D(&desc, &init, &tex))) return 0;
    tex->GetDesc(&desc);
    return desc.Width;
}

static std::vector<uint8_t> ReadBytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The build seals the proxy; any change to it shows, except code-signing it later.
static void TestSeal(const fs::path& dll) {
    using resorep::CheckSeal;
    using resorep::SealState;
    const std::vector<uint8_t> sealed = ReadBytes(dll);
    CHECK(CheckSeal(sealed) == SealState::Valid);

    std::vector<uint8_t> f = sealed;
    f[f.size() / 2] ^= 1;  // a changed byte
    CHECK(CheckSeal(f) == SealState::Invalid);
    f = sealed;
    f.insert(f.end(), 64, 0xCC);  // something appended
    CHECK(CheckSeal(f) == SealState::Invalid);

    // Signed after sealing: a certificate table at the end, its directory entry and the PE
    // checksum are set. None of that is part of the digest.
    uint32_t pe;
    std::memcpy(&pe, &sealed[0x3C], 4);
    const size_t security = pe + 24 + 112 + 4 * 8;  // PE32+ data directory 4
    // The table starts 8-aligned; the linker already aligns the file, so a signer adds no padding.
    CHECK(sealed.size() % 8 == 0);
    f = sealed;
    const uint32_t certAt = uint32_t(f.size()), certSize = 24;
    f.insert(f.end(), certSize, 0xAB);
    std::memcpy(&f[security], &certAt, 4);
    std::memcpy(&f[security + 4], &certSize, 4);
    f[pe + 24 + 64] ^= 0x5A;  // checksum
    CHECK(CheckSeal(f) == SealState::Valid);
    std::vector<uint8_t> signedFile = f;
    signedFile.back() ^= 1;  // inside the certificate: not hashed
    CHECK(CheckSeal(signedFile) == SealState::Valid);
    signedFile = f;
    signedFile.insert(signedFile.end(), 8, 0);  // the table must reach the end of the file
    CHECK(CheckSeal(signedFile) == SealState::Invalid);
    // A "certificate" covering nearly the whole file would leave the seal itself unhashed.
    signedFile = sealed;
    const uint32_t earlyAt = 0x10, earlySize = uint32_t(sealed.size()) - earlyAt;
    std::memcpy(&signedFile[security], &earlyAt, 4);
    std::memcpy(&signedFile[security + 4], &earlySize, 4);
    CHECK(CheckSeal(signedFile) == SealState::Invalid);
}

// Run as its own process (proxy_test <dll> tampered): a proxy that fails its seal loads and
// passes calls through, but replaces nothing.
static int TestTamperedProxy(const fs::path& dll) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const fs::path dir = fs::path(tmp) / L"reresorep_proxy_tamper_test";
    fs::remove_all(dir);
    fs::create_directories(dir / L"modded");
    std::vector<uint8_t> f = ReadBytes(dll);
    f.insert(f.end(), 64, 0xCC);
    WriteFile(dir / L"d3d11.dll", f);
    std::ofstream(dir / L"dllsettings.ini") << "modded_textures_folder=modded\ndll_log_enabled=true\n";
    const GameTexture tex = MakeGameTexture(0x0F1E2D);
    WriteFile(dir / L"modded" / (std::to_wstring(tex.hash) + L".dds"), SolidDds(8, 8, 0xFF0000FF));

    HMODULE proxy = LoadLibraryW((dir / L"d3d11.dll").c_str());
    CHECK(proxy != nullptr);
    if (!proxy) return 1;
    auto create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(proxy, "D3D11CreateDevice"));
    ComPtr<ID3D11Device> dev;
    CHECK(create && SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                     nullptr, nullptr)));
    if (dev) CHECK(CreateAndGetWidth(dev.Get(), tex) == 64);  // the game's own texture
    const std::vector<uint8_t> log = ReadBytes(dir / L"dll.log");
    CHECK(std::string(log.begin(), log.end()).find("damaged or was modified") != std::string::npos);
    std::printf(g_failures ? "%d check(s) failed\n" : "all tampered-proxy checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // FAIL lines survive a crash
    if (argc < 2) {
        std::printf("usage: proxy_test <d3d11.dll> [tampered]\n");
        return 2;
    }
    if (argc > 2 && std::string(argv[2]) == "tampered") return TestTamperedProxy(argv[1]);
    TestSeal(argv[1]);
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const fs::path dir = fs::path(tmp) / L"reresorep_proxy_test";
    fs::remove_all(dir);
    fs::create_directories(dir / L"modded");
    fs::copy_file(argv[1], dir / L"d3d11.dll");
    auto writeSettings = [&](bool lowImpact) {
        std::ofstream ini(dir / L"dllsettings.ini");
        ini << "modded_textures_folder=modded\noriginal_textures_folder=original\n"
               "mod_creator_mode_enabled=true\nsave_textures=true\ndll_log_enabled=true\ndll_log_level=debug\n"
               "game_exe_sha256=" << std::string(64, 'c') << "\nlow_impact_mode=" << (lowImpact ? "true" : "false")
            << "\n";
    };
    writeSettings(false);

    const GameTexture plain = MakeGameTexture(0x112233);
    const GameTexture encrypted = MakeGameTexture(0x445566);
    const GameTexture untouched = MakeGameTexture(0x778899);
    const GameTexture later = MakeGameTexture(0xAABBCC);
    const GameTexture offByOne = MakeGameTexture(0xDDEEF0);
    WriteFile(dir / L"modded" / (std::to_wstring(plain.hash) + L"~plain.dds"), SolidDds(32, 32, 0xFF0000FF));
    const auto dds = SolidDds(16, 16, 0xFF00FF00);
    const uint8_t iv[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    WriteFile(dir / L"modded" / (std::to_wstring(encrypted.hash) + L".dds"),
              resorep::EncryptTexture(dds.data(), dds.size(), iv));
    WriteFile(dir / L"modded" / (std::to_wstring(offByOne.hash + 2) + L".dds"), SolidDds(8, 8, 0xFFFFFFFF));
    const GameTexture categorized = MakeGameTexture(0x135790);
    fs::create_directories(dir / L"modded" / L"Pack" / L"Weapons");
    WriteFile(dir / L"modded" / L"Pack" / L"Weapons" / (std::to_wstring(categorized.hash) + L".dds"),
              SolidDds(8, 8, 0xFF00FFFF));

    // Mods marked for a Siege build: this game's applies, another build's is skipped.
    const GameTexture forThisBuild = MakeGameTexture(0x246802);
    const GameTexture forOtherBuild = MakeGameTexture(0x369258);
    fs::create_directories(dir / L"modded" / L"This build");
    fs::create_directories(dir / L"modded" / L"Other build");
    resorep::WriteModGame(dir / L"modded" / L"This build", std::string(64, 'c'), "");
    resorep::WriteModGame(dir / L"modded" / L"Other build", std::string(64, 'd'), "");
    WriteFile(dir / L"modded" / L"This build" / (std::to_wstring(forThisBuild.hash) + L".dds"), SolidDds(8, 8, 0xFF0000FF));
    WriteFile(dir / L"modded" / L"Other build" / (std::to_wstring(forOtherBuild.hash) + L".dds"), SolidDds(8, 8, 0xFF0000FF));

    HMODULE proxy = LoadLibraryW((dir / L"d3d11.dll").c_str());
    CHECK(proxy != nullptr);
    if (!proxy) return 1;
    CHECK(GetProcAddress(proxy, "ReResorepProxyVersion") != nullptr);

    // A pass-through export called before any device exists takes the lazy-load path.
    using FromDxgi = HRESULT(WINAPI*)(IUnknown*, IUnknown**);
    auto fromDxgi = reinterpret_cast<FromDxgi>(GetProcAddress(proxy, "CreateDirect3D11DeviceFromDXGIDevice"));
    CHECK(fromDxgi != nullptr);
    IUnknown* winrtDevice = nullptr;
    CHECK(FAILED(fromDxgi(nullptr, &winrtDevice)));  // rejected by the real d3d11, but no crash

    auto create = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(proxy, "D3D11CreateDevice"));
    ComPtr<ID3D11Device> dev;
    CHECK(SUCCEEDED(create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr,
                           nullptr)));
    if (!dev) return 1;

    // The same export with real arguments, now on the fast path.
    ComPtr<IUnknown> dxgiDevice;
    dev.As(&dxgiDevice);
    CHECK(SUCCEEDED(fromDxgi(dxgiDevice.Get(), &winrtDevice)));
    if (winrtDevice) winrtDevice->Release();

    CHECK(CreateAndGetWidth(dev.Get(), plain) == 32);      // replaced
    CHECK(CreateAndGetWidth(dev.Get(), encrypted) == 16);  // replaced from resorep1 container
    CHECK(CreateAndGetWidth(dev.Get(), offByOne) == 8);    // hash tolerance
    CHECK(CreateAndGetWidth(dev.Get(), untouched) == 64);  // no mod: game's own texture
    CHECK(CreateAndGetWidth(dev.Get(), later) == 64);
    CHECK(CreateAndGetWidth(dev.Get(), forThisBuild) == 8);
    CHECK(CreateAndGetWidth(dev.Get(), forOtherBuild) == 64);

    // Through ID3D11Device1 too (same vtable slot).
    ComPtr<ID3D11Device1> dev1;
    if (SUCCEEDED(dev.As(&dev1))) CHECK(CreateAndGetWidth(dev1.Get(), plain) == 32);

    // Hot reload: a mod dropped in while the game runs applies to the next load.
    WriteFile(dir / L"modded" / (std::to_wstring(later.hash) + L".dds"), SolidDds(4, 4, 0xFF123456));
    Sleep(1500);
    CHECK(CreateAndGetWidth(dev.Get(), later) == 4);

    // Categories: a mod in a sub-folder applies, and turning the category off stops it.
    CHECK(CreateAndGetWidth(dev.Get(), categorized) == 8);
    std::ofstream(dir / L"modded" / L"reresorep_disabled.ini") << "disabled=Pack/Weapons" << std::endl;
    Sleep(1500);
    CHECK(CreateAndGetWidth(dev.Get(), categorized) == 64);

    // Dumping happens on a worker thread.
    const fs::path dumped = dir / L"original" / (std::to_wstring(untouched.hash) + L".dds");
    for (int i = 0; i < 40 && !fs::exists(dumped); ++i) Sleep(50);
    CHECK(fs::exists(dumped));
    resorep::DdsImage img;
    CHECK(resorep::LoadTextureFile(dumped.wstring(), img));
    CHECK(img.width == 64 && img.format == DXGI_FORMAT_R8G8B8A8_UNORM);
    if (!img.subresources.empty())
        CHECK(resorep::ComputeTextureHash(img.data.data() + img.subresources[0].offset, img.subresources[0].slicePitch) ==
              untouched.hash);

    // Not dumped twice while the file exists...
    const auto firstWrite = WriteTime(dumped);
    Sleep(1500);
    CreateAndGetWidth(dev.Get(), untouched);
    Sleep(500);
    CHECK(WriteTime(dumped) == firstWrite);
    // ...nor after it was deleted while the game runs, until a redump request.
    fs::remove(dumped);
    Sleep(1500);
    CreateAndGetWidth(dev.Get(), untouched);
    Sleep(500);
    CHECK(!fs::exists(dumped));
    auto requestRedump = [&] {
        std::ofstream(dir / L"reresorep_redump.request").close();
        for (int i = 0; i < 40 && fs::exists(dir / L"reresorep_redump.request"); ++i) Sleep(50);
        CHECK(!fs::exists(dir / L"reresorep_redump.request"));  // the proxy takes the request
    };
    requestRedump();
    CreateAndGetWidth(dev.Get(), untouched);
    for (int i = 0; i < 40 && !fs::exists(dumped); ++i) Sleep(50);
    CHECK(fs::exists(dumped));

    // A redump request (the manager's Redump button) also rewrites dumps that already exist, once.
    Sleep(1500);
    const auto beforeRedump = WriteTime(dumped);
    requestRedump();
    CreateAndGetWidth(dev.Get(), untouched);
    for (int i = 0; i < 40 && WriteTime(dumped) == beforeRedump; ++i) Sleep(50);
    const auto redumpedAt = WriteTime(dumped);
    CHECK(redumpedAt != beforeRedump);
    Sleep(1500);
    CreateAndGetWidth(dev.Get(), untouched);
    Sleep(500);
    CHECK(WriteTime(dumped) == redumpedAt);

    // Low impact mode: mods still apply, but nothing is dumped and the modded folder isn't watched.
    writeSettings(true);
    Sleep(1500);
    CHECK(CreateAndGetWidth(dev.Get(), plain) == 32);
    const GameTexture quiet = MakeGameTexture(0x5A5A5A);
    const fs::path quietDump = dir / L"original" / (std::to_wstring(quiet.hash) + L".dds");
    CHECK(CreateAndGetWidth(dev.Get(), quiet) == 64);
    Sleep(500);
    CHECK(!fs::exists(quietDump));
    WriteFile(dir / L"modded" / (std::to_wstring(quiet.hash) + L".dds"), SolidDds(4, 4, 0xFF654321));
    Sleep(1500);
    CHECK(CreateAndGetWidth(dev.Get(), quiet) == 64);  // added while playing: not picked up yet
    // Turned off again: the settings change re-reads the mods, and dumping is back.
    writeSettings(false);
    Sleep(1500);
    CHECK(CreateAndGetWidth(dev.Get(), quiet) == 4);
    for (int i = 0; i < 40 && !fs::exists(quietDump); ++i) Sleep(50);
    CHECK(fs::exists(quietDump));

    std::printf(g_failures ? "%d check(s) failed (log: %ls)\n" : "all proxy checks passed\n", g_failures,
                (dir / L"dll.log").c_str());
    return g_failures ? 1 : 0;
}
