#include "texture_hook.h"

#include <array>
#include <atomic>
#include <mutex>

#include "dds.h"
#include "resorep_hash.h"
#include "runtime.h"

namespace resorep::proxy {
namespace {

using CreateTexture2DFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_TEXTURE2D_DESC*,
                                                       const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**);
constexpr size_t kCreateTexture2DSlot = 5;  // IUnknown(3), CreateBuffer, CreateTexture1D, CreateTexture2D

// We patch the device's vtable instead of wrapping the device: every interface the
// game queries (ID3D11Device1..5) shares that slot, and refcounting stays untouched.
struct PatchedVtable {
    void** vtable;
    CreateTexture2DFn original;
};
std::array<PatchedVtable, 16> g_patched;
std::atomic<size_t> g_patchedCount{0};
std::mutex g_patchMutex;

CreateTexture2DFn OriginalFor(ID3D11Device* device) {
    void** vtable = *reinterpret_cast<void***>(device);
    const size_t n = g_patchedCount.load(std::memory_order_acquire);
    for (size_t i = 0; i < n; ++i)
        if (g_patched[i].vtable == vtable) return g_patched[i].original;
    return n ? g_patched[0].original : nullptr;
}

// DXGI format families: a mod saved as BC3_UNORM must still be created as
// BC3_UNORM_SRGB if that is what the game asked for, or its views fail.
int FormatFamily(DXGI_FORMAT f) {
    struct Range { int first, last; };
    static constexpr Range kFamilies[] = {
        {1, 4}, {5, 8}, {9, 14}, {15, 18}, {23, 25}, {27, 32}, {33, 38}, {39, 43}, {48, 52},
        {53, 59}, {60, 64}, {70, 72}, {73, 75}, {76, 78}, {79, 81}, {82, 84}, {94, 96}, {97, 99},
    };
    for (int i = 0; i < int(std::size(kFamilies)); ++i)
        if (f >= kFamilies[i].first && f <= kFamilies[i].last) return i;
    if (f == DXGI_FORMAT_B8G8R8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_TYPELESS || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
        return 100;
    if (f == DXGI_FORMAT_B8G8R8X8_UNORM || f == DXGI_FORMAT_B8G8R8X8_TYPELESS || f == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
        return 101;
    return -1 - int(f);
}

bool CreateReplacement(CreateTexture2DFn original, ID3D11Device* device, const std::filesystem::path& path,
                       const D3D11_TEXTURE2D_DESC& gameDesc, ID3D11Texture2D** out) {
    Runtime& rt = Runtime::Get();
    DdsImage img;
    std::string error;
    if (!LoadTextureFile(path.wstring(), img, nullptr, &error)) {
        rt.Log(false, "Failed to load DDS file %ls: %s", path.c_str(), error.c_str());
        return false;
    }
    if (img.isVolume || img.isCubemap || img.arraySize != 1) {
        rt.Log(false, "Skipping %ls: replacement must be a plain 2D texture", path.c_str());
        return false;
    }

    D3D11_TEXTURE2D_DESC desc = gameDesc;
    desc.Width = img.width;
    desc.Height = img.height;
    desc.MipLevels = img.mipLevels;
    desc.ArraySize = 1;
    desc.Format = FormatFamily(img.format) == FormatFamily(gameDesc.Format) ? gameDesc.Format : img.format;
    desc.MiscFlags &= ~D3D11_RESOURCE_MISC_GENERATE_MIPS;

    std::vector<D3D11_SUBRESOURCE_DATA> subresources;
    for (const DdsSubresource& s : img.subresources)
        subresources.push_back({img.data.data() + s.offset, s.rowPitch, s.slicePitch});

    const HRESULT hr = original(device, &desc, subresources.data(), out);
    if (FAILED(hr)) {
        rt.Log(false, "CreateTexture2D rejected %ls (hr=0x%08lX, %ux%u %s)", path.c_str(), hr, desc.Width,
               desc.Height, FormatName(desc.Format));
        return false;
    }
    return true;
}

HRESULT STDMETHODCALLTYPE HookedCreateTexture2D(ID3D11Device* self, const D3D11_TEXTURE2D_DESC* desc,
                                                const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture2D** out) {
    const CreateTexture2DFn original = OriginalFor(self);
    // Same filter as the original proxy: immutable/default textures with data, no arrays.
    if (!desc || !init || !init->pSysMem || !out || desc->Usage > D3D11_USAGE_IMMUTABLE || desc->ArraySize != 1)
        return original(self, desc, init, out);

    Runtime& rt = Runtime::Get();
    const auto cfg = rt.config();
    const auto index = rt.index();  // keeps this snapshot alive while we use it
    // Nothing to replace or dump: don't read the texture's data at all.
    if (index->size() == 0 && !cfg->dumpTextures && !(cfg->logEnabled && cfg->logDebug))
        return original(self, desc, init, out);

    const uint32_t hash = ComputeTextureHash(init->pSysMem, init->SysMemSlicePitch);
    if (cfg->logDebug)
        rt.Log(true, "Game is loading texture %u (%ux%u %s)", hash, desc->Width, desc->Height, FormatName(desc->Format));

    if (cfg->dumpTextures) rt.QueueDump(hash, *desc, init);

    if (const std::filesystem::path* mod = index->Find(hash)) {
        if (CreateReplacement(original, self, *mod, *desc, out)) {
            rt.Log(true, "Overriding texture %u with %ls", hash, mod->c_str());
            return S_OK;
        }
    }
    return original(self, desc, init, out);
}

}  // namespace

void InstallTextureHook(ID3D11Device* device) {
    if (!device) return;
    Runtime::Get().EnsureStarted();

    std::lock_guard lock(g_patchMutex);
    void** vtable = *reinterpret_cast<void***>(device);
    if (vtable[kCreateTexture2DSlot] == reinterpret_cast<void*>(&HookedCreateTexture2D)) return;

    const size_t n = g_patchedCount.load(std::memory_order_relaxed);
    if (n == g_patched.size()) return;
    g_patched[n] = {vtable, reinterpret_cast<CreateTexture2DFn>(vtable[kCreateTexture2DSlot])};
    g_patchedCount.store(n + 1, std::memory_order_release);

    DWORD oldProtect;
    if (VirtualProtect(&vtable[kCreateTexture2DSlot], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        InterlockedExchangePointer(&vtable[kCreateTexture2DSlot], reinterpret_cast<void*>(&HookedCreateTexture2D));
        VirtualProtect(&vtable[kCreateTexture2DSlot], sizeof(void*), oldProtect, &oldProtect);
        Runtime::Get().Log(false, "Hooked ID3D11Device::CreateTexture2D");
    }
}

}  // namespace resorep::proxy
