// End-to-end test of the built proxy: loads bin/proxy/d3d11.dll from a temp folder the
// way a game would, then checks replacement, encrypted packs, dumping and hot reload
// on a WARP device. Run: build\Release\proxy_test.exe <path to bin\proxy\d3d11.dll>
#include <windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "dds.h"
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

static UINT CreateAndGetWidth(ID3D11Device* dev, const GameTexture& t) {
    D3D11_TEXTURE2D_DESC desc{64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_IMMUTABLE,
                              D3D11_BIND_SHADER_RESOURCE, 0, 0};
    D3D11_SUBRESOURCE_DATA init{t.pixels.data(), 64 * 4, UINT(t.pixels.size())};
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(dev->CreateTexture2D(&desc, &init, &tex))) return 0;
    tex->GetDesc(&desc);
    return desc.Width;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: proxy_test <d3d11.dll>\n");
        return 2;
    }
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const fs::path dir = fs::path(tmp) / L"reresorep_proxy_test";
    fs::remove_all(dir);
    fs::create_directories(dir / L"modded");
    fs::copy_file(argv[1], dir / L"d3d11.dll");
    {
        std::ofstream ini(dir / L"dllsettings.ini");
        ini << "modded_textures_folder=modded\noriginal_textures_folder=original\n"
               "mod_creator_mode_enabled=true\nsave_textures=true\ndll_log_enabled=true\ndll_log_level=debug\n";
    }

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
    const auto firstWrite = fs::last_write_time(dumped);
    Sleep(1500);
    CreateAndGetWidth(dev.Get(), untouched);
    Sleep(500);
    CHECK(fs::last_write_time(dumped) == firstWrite);
    // ...but deleted while the game runs, it is dumped again the next time the game loads it.
    fs::remove(dumped);
    Sleep(1500);
    CreateAndGetWidth(dev.Get(), untouched);
    for (int i = 0; i < 40 && !fs::exists(dumped); ++i) Sleep(50);
    CHECK(fs::exists(dumped));

    // A redump request (the manager's Redump button) rewrites dumps that already exist, once.
    Sleep(1500);
    const auto beforeRedump = fs::last_write_time(dumped);
    std::ofstream(dir / L"reresorep_redump.request").close();
    for (int i = 0; i < 40 && fs::exists(dir / L"reresorep_redump.request"); ++i) Sleep(50);
    CHECK(!fs::exists(dir / L"reresorep_redump.request"));  // the proxy takes the request
    CreateAndGetWidth(dev.Get(), untouched);
    for (int i = 0; i < 40 && fs::last_write_time(dumped) == beforeRedump; ++i) Sleep(50);
    const auto redumpedAt = fs::last_write_time(dumped);
    CHECK(redumpedAt != beforeRedump);
    Sleep(1500);
    CreateAndGetWidth(dev.Get(), untouched);
    Sleep(500);
    CHECK(fs::last_write_time(dumped) == redumpedAt);

    std::printf(g_failures ? "%d check(s) failed (log: %ls)\n" : "all proxy checks passed\n", g_failures,
                (dir / L"dll.log").c_str());
    return g_failures ? 1 : 0;
}
