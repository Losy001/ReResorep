// d3d11.dll proxy: forwards every export to the real d3d11.dll and hooks texture
// creation on the devices it hands out.
#include <windows.h>
#include <d3d11.h>

#include <filesystem>

#include "runtime.h"
#include "version.h"
#include "texture_hook.h"

namespace {
const char* const kPassthroughNames[] = {
#include "passthrough_names.inc"
};
constexpr size_t kPassthroughCount = std::size(kPassthroughNames);

HMODULE g_real = nullptr;
FARPROC g_realCreateDevice = nullptr;
FARPROC g_realCreateDeviceAndSwapChain = nullptr;

// Called if the real DLL lacks an export we forward (older Windows builds).
extern "C" HRESULT WINAPI MissingExport() { return E_NOTIMPL; }

bool IsOurProxy(HMODULE module) { return GetProcAddress(module, "ReResorepProxyVersion") != nullptr; }

HMODULE LoadRealD3D11() {
    // A d3d11.dll the game shipped with (or another wrapper such as ReShade) is
    // kept as ori_d3d11.dll by the installer, so chain to it when present.
    const std::filesystem::path chained = resorep::proxy::ModuleDirectory() / L"ori_d3d11.dll";
    std::error_code ec;  // no exceptions while the game loads d3d11.dll
    if (std::filesystem::exists(chained, ec)) {
        if (HMODULE m = LoadLibraryW(chained.c_str())) {
            if (!IsOurProxy(m)) return m;
            FreeLibrary(m);
        }
    }
    wchar_t sys[MAX_PATH];
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    return LoadLibraryW((std::wstring(sys, n) + L"\\d3d11.dll").c_str());
}

BOOL CALLBACK LoadOnce(PINIT_ONCE, PVOID, PVOID*);
INIT_ONCE g_loadOnce = INIT_ONCE_STATIC_INIT;
}  // namespace

extern "C" {
// Filled on first use; read by the generated assembly stubs.
void* g_origProcs[kPassthroughCount] = {};

void ResorepEnsureLoaded() { InitOnceExecuteOnce(&g_loadOnce, LoadOnce, nullptr, nullptr); }

// Lets the manager (and ourselves) recognise this DLL without relying on file sizes.
__declspec(dllexport) DWORD WINAPI ReResorepProxyVersion() { return RERESOREP_PROXY_VERSION_NUMBER; }
}

namespace {
BOOL CALLBACK LoadOnce(PINIT_ONCE, PVOID, PVOID*) {
    g_real = LoadRealD3D11();
    for (size_t i = 0; i < kPassthroughCount; ++i) {
        FARPROC p = g_real ? GetProcAddress(g_real, kPassthroughNames[i]) : nullptr;
        g_origProcs[i] = p ? reinterpret_cast<void*>(p) : reinterpret_cast<void*>(&MissingExport);
    }
    if (g_real) {
        g_realCreateDevice = GetProcAddress(g_real, "D3D11CreateDevice");
        g_realCreateDeviceAndSwapChain = GetProcAddress(g_real, "D3D11CreateDeviceAndSwapChain");
    }
    return TRUE;
}
}  // namespace

extern "C" HRESULT WINAPI D3D11CreateDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driverType, HMODULE software,
                                            UINT flags, const D3D_FEATURE_LEVEL* featureLevels, UINT numLevels,
                                            UINT sdkVersion, ID3D11Device** device, D3D_FEATURE_LEVEL* level,
                                            ID3D11DeviceContext** context) {
    ResorepEnsureLoaded();
    if (!g_realCreateDevice) return E_FAIL;
    const HRESULT hr = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(g_realCreateDevice)(
        adapter, driverType, software, flags, featureLevels, numLevels, sdkVersion, device, level, context);
    if (SUCCEEDED(hr) && device && *device) resorep::proxy::InstallTextureHook(*device);
    return hr;
}

extern "C" HRESULT WINAPI D3D11CreateDeviceAndSwapChain(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driverType,
                                                        HMODULE software, UINT flags,
                                                        const D3D_FEATURE_LEVEL* featureLevels, UINT numLevels,
                                                        UINT sdkVersion, const DXGI_SWAP_CHAIN_DESC* swapChainDesc,
                                                        IDXGISwapChain** swapChain, ID3D11Device** device,
                                                        D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** context) {
    ResorepEnsureLoaded();
    if (!g_realCreateDeviceAndSwapChain) return E_FAIL;
    const HRESULT hr = reinterpret_cast<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(g_realCreateDeviceAndSwapChain)(
        adapter, driverType, software, flags, featureLevels, numLevels, sdkVersion, swapChainDesc, swapChain, device,
        level, context);
    if (SUCCEEDED(hr) && device && *device) resorep::proxy::InstallTextureHook(*device);
    return hr;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    // Deliberately nothing else here: loading DLLs or starting threads under the
    // loader lock is how the original could hang games on startup.
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
