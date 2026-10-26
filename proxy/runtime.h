#pragma once
#include <windows.h>
#include <d3d11.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "texture_index.h"

namespace resorep::proxy {

struct Config {
    std::filesystem::path originalFolder;
    std::filesystem::path moddedFolder;
    bool dumpTextures = false;  // save_textures, honoured only in mod creator mode (as before)
    bool lowImpact = false;     // see keys::kLowImpact
    bool logEnabled = false;
    bool logDebug = false;
    std::filesystem::path logFile;
    std::string gameSha256;  // this game's exe, written by the manager; mods for other games are skipped
};

// Process-wide state of the proxy. Created on the first D3D11 device, not in DllMain.
class Runtime {
public:
    static Runtime& Get();

    void EnsureStarted();
    // The proxy's file still matches its seal (core/integrity.h). If not, it only passes calls
    // through to the real d3d11.dll: no hooks, no mods, no dumps. Valid after EnsureStarted.
    bool intact() const { return intact_; }

    std::shared_ptr<const Config> config() const { return config_.load(std::memory_order_acquire); }
    std::shared_ptr<const TextureIndex> index() const { return index_.load(std::memory_order_acquire); }

    // Copies the initial data and writes original/<hash>.dds on a worker thread.
    void QueueDump(uint32_t hash, const D3D11_TEXTURE2D_DESC& desc, const D3D11_SUBRESOURCE_DATA* init);

    void Log(bool debug, const char* fmt, ...);

private:
    Runtime() = default;
    void LoadConfig();
    void Rescan();
    void AddDumpsOnDisk();     // what the dump folder already has counts as dumped
    void StartRedump();        // from now on, dump every texture again as the game loads it
    void WatchThread();
    void DumpThread();

    std::atomic<std::shared_ptr<const Config>> config_;
    std::atomic<std::shared_ptr<const TextureIndex>> index_{std::make_shared<TextureIndex>()};
    struct DumpState;
    DumpState* dump_ = nullptr;
    std::filesystem::file_time_type settingsStamp_{};
    bool intact_ = false;
    CRITICAL_SECTION logLock_;
    FILE* logFile_ = nullptr;
};

std::filesystem::path ModuleDirectory();

}  // namespace resorep::proxy
