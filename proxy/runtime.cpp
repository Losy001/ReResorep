#include "runtime.h"

#include <algorithm>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include "dds.h"
#include "integrity.h"
#include "settings.h"
#include "version.h"

namespace resorep::proxy {
namespace fs = std::filesystem;

namespace {
// Stop queueing dumps when this much copied texture data is waiting to be written,
// so a level load with dumping on cannot balloon the game's memory.
constexpr size_t kMaxPendingDumpBytes = 512ull << 20;

fs::path ResolveFolder(const std::string& value, const wchar_t* fallback) {
    fs::path p = value.empty() ? fs::path(fallback) : fs::path(Utf8ToWide(value));
    return p.is_absolute() ? p : ModuleDirectory() / p;
}

uint32_t FullMipCount(uint32_t w, uint32_t h) {
    uint32_t levels = 1;
    for (uint32_t m = std::max(w, h); m > 1; m >>= 1) ++levels;
    return levels;
}

thread_local bool t_isWatchThread = false;

// Folder scans on the watch thread run at background CPU and disk priority, so they never
// compete with the game. Only the scan itself: a background thread holding a lock the game's
// threads wait for (the log, the dump state, the index pointer) could stall them.
class BackgroundScan {
public:
    BackgroundScan() : on_(t_isWatchThread && SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN)) {}
    ~BackgroundScan() {
        if (on_) SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
    }
    BackgroundScan(const BackgroundScan&) = delete;
    BackgroundScan& operator=(const BackgroundScan&) = delete;

private:
    bool on_;
};
}  // namespace

struct Runtime::DumpState {
    struct Job {
        uint32_t hash;
        DXGI_FORMAT format;
        uint32_t width, height;
        std::vector<std::vector<uint8_t>> mips;
        std::vector<uint32_t> pitches;
        size_t bytes;
    };
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    size_t pendingBytes = 0;
    // Hashes not dumped again until a redump request: on disk when dumping started, or dumped
    // since (even if the file was deleted meanwhile). The dump folder is not watched for it.
    std::unordered_set<uint32_t> seen;
    std::unordered_set<uint32_t> inFlight;  // queued or being written, not on disk yet
    // Since a redump request, files on disk from before it don't count as dumped.
    bool redumping = false;
};

fs::path ModuleDirectory() {
    static const fs::path dir = [] {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ModuleDirectory), &self);
        wchar_t buf[MAX_PATH * 4];
        const DWORD n = GetModuleFileNameW(self, buf, DWORD(std::size(buf)));
        return fs::path(std::wstring(buf, n)).parent_path();
    }();
    return dir;
}

Runtime& Runtime::Get() {
    static Runtime* instance = new Runtime();  // never destroyed: threads may outlive statics
    return *instance;
}

void Runtime::EnsureStarted() {
    static std::once_flag once;
    std::call_once(once, [this] {
        InitializeCriticalSection(&logLock_);
        dump_ = new DumpState();
        intact_ = CheckOwnSeal() == SealState::Valid;
        std::error_code ec;
        settingsStamp_ = fs::last_write_time(ModuleDirectory() / kDllSettingsFile, ec);
        fs::remove(ModuleDirectory() / kRedumpRequestFile, ec);  // left from a session that ended first
        LoadConfig();
        if (!intact_) {
            Log(false, "This d3d11.dll is damaged or was modified, so ReResorep is off: the game runs without "
                       "mods. Repair it on ReResorep's Games tab.");
            return;
        }
        Rescan();
        std::thread([this] { WatchThread(); }).detach();
        std::thread([this] { DumpThread(); }).detach();
        Log(false, "ReResorep proxy " RERESOREP_VERSION_STRING " started%s; %zu modded textures indexed",
            config()->lowImpact ? " in low impact mode" : "", index()->size());
    });
}

void Runtime::LoadConfig() {
    IniFile ini;
    ini.Load(ModuleDirectory() / kDllSettingsFile);

    const auto previous = config();
    auto cfg = std::make_shared<Config>();
    cfg->originalFolder = ResolveFolder(ini.GetOr(keys::kOriginalFolder, ""), L"original");
    cfg->moddedFolder = ResolveFolder(ini.GetOr(keys::kModdedFolder, ""), L"modded");
    cfg->lowImpact = ini.GetBool(keys::kLowImpact, false);
    cfg->dumpTextures = intact_ && !cfg->lowImpact && ini.GetBool(keys::kModCreatorMode, false) &&
                        ini.GetBool(keys::kSaveTextures, false);
    cfg->logEnabled = ini.GetBool(keys::kLogEnabled, false);
    cfg->logDebug = ini.GetOr(keys::kLogLevel, "") == "debug";
    cfg->logFile = ResolveFolder(ini.GetOr(keys::kLogFile, ""), L"dll.log");
    cfg->gameSha256 = ini.GetOr(keys::kGameSha256, "");

    EnterCriticalSection(&logLock_);
    if (logFile_) {
        fclose(logFile_);
        logFile_ = nullptr;
    }
    if (cfg->logEnabled) logFile_ = _wfsopen(cfg->logFile.c_str(), L"a", _SH_DENYWR);
    LeaveCriticalSection(&logLock_);

    // A different dump folder starts over; dumping turned on reads what that folder already has.
    const bool newFolder = previous && previous->originalFolder != cfg->originalFolder;
    const bool readDisk = cfg->dumpTextures && (!previous || !previous->dumpTextures || newFolder);
    config_.store(std::move(cfg), std::memory_order_release);
    if (newFolder) {
        std::lock_guard lock(dump_->mutex);
        dump_->seen = dump_->inFlight;
        dump_->redumping = false;
    }
    if (readDisk) AddDumpsOnDisk();
}

void Runtime::AddDumpsOnDisk() {
    // Read once, not on every change: a dump deleted while the game runs stays deleted until
    // the next redump request (the original Resorep listed the folder on every texture creation).
    const auto cfg = config();
    TextureIndex onDisk;
    {
        BackgroundScan bg;
        onDisk.Scan(cfg->originalFolder);
    }
    std::lock_guard lock(dump_->mutex);
    if (dump_->redumping) return;  // those files are to be written again
    for (const auto& [hash, path] : onDisk.files()) dump_->seen.insert(hash);
}

void Runtime::StartRedump() {
    if (!config()->dumpTextures) {
        Log(false, "Redump requested, but dumping is off");
        return;
    }
    std::lock_guard lock(dump_->mutex);
    dump_->redumping = true;
    dump_->seen = dump_->inFlight;
    Log(false, "Redump requested; textures are dumped again as the game loads them");
}

void Runtime::Rescan() {
    auto idx = std::make_shared<TextureIndex>();
    const auto cfg = config();
    {
        BackgroundScan bg;
        idx->Scan(cfg->moddedFolder, cfg->gameSha256);
    }
    index_.store(std::move(idx), std::memory_order_release);
}

void Runtime::WatchThread() {
    // Picks up mods added/removed/disabled from the manager, settings changes and redump
    // requests without restarting the game. Textures already created keep their old data.
    // The dump folder isn't watched: what was dumped stays dumped until a redump request.
    t_isWatchThread = true;  // its folder scans run at background priority
    enum Watch { kSettings, kModded };
    for (;;) {
        const fs::path modded = config()->moddedFolder;
        const bool watchModded = !config()->lowImpact;
        HANDLE handles[2] = {
            FindFirstChangeNotificationW(ModuleDirectory().c_str(), FALSE,
                                         FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME),
            // Whole tree: mod/category folders, and reresorep_disabled.ini when categories are toggled.
            watchModded ? FindFirstChangeNotificationW(modded.c_str(), TRUE,
                                                       FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME |
                                                           FILE_NOTIFY_CHANGE_DIR_NAME)
                        : INVALID_HANDLE_VALUE,
        };
        if (handles[kSettings] == INVALID_HANDLE_VALUE) return;
        const DWORD count = handles[kModded] == INVALID_HANDLE_VALUE ? 1 : 2;
        const bool pollModded = watchModded && count < 2;

        bool restart = false;
        while (!restart) {
            // A modded folder that doesn't exist yet is polled every 2 s until it appears.
            const DWORD r = WaitForMultipleObjects(count, handles, FALSE, pollModded ? 2000 : INFINITE);
            if (r == WAIT_TIMEOUT) {
                std::error_code ec;  // no exceptions in the game's process
                restart = fs::exists(modded, ec);
                if (restart) Rescan();
                continue;
            }
            if (r - WAIT_OBJECT_0 >= count) return;
            Sleep(250);  // let a burst of file operations finish
            // Serve every folder that changed, not just the first: the game folder changes all
            // the time while dll.log is written, and must not starve the modded folder.
            bool changed[2] = {};
            for (DWORD w = 0; w < count; ++w) {
                if (WaitForSingleObject(handles[w], 0) != WAIT_OBJECT_0) continue;
                changed[w] = true;
                FindNextChangeNotification(handles[w]);
            }
            if (changed[kSettings]) {
                // The game folder changes for many reasons (our own dll.log included);
                // only a redump request or a new dllsettings.ini matters.
                std::error_code ec;
                if (fs::remove(ModuleDirectory() / kRedumpRequestFile, ec)) StartRedump();
                const auto stamp = fs::last_write_time(ModuleDirectory() / kDllSettingsFile, ec);
                if (!ec && stamp != settingsStamp_) {
                    settingsStamp_ = stamp;
                    LoadConfig();
                    Rescan();
                    changed[kModded] = false;  // just rescanned
                    restart = config()->moddedFolder != modded || config()->lowImpact != !watchModded;
                    Log(false, "Settings reloaded");
                }
            }
            if (changed[kModded]) {
                Rescan();
                Log(true, "Modded folder changed; %zu textures indexed", index()->size());
            }
        }
        for (HANDLE h : handles)
            if (h != INVALID_HANDLE_VALUE) FindCloseChangeNotification(h);
    }
}

void Runtime::QueueDump(uint32_t hash, const D3D11_TEXTURE2D_DESC& desc, const D3D11_SUBRESOURCE_DATA* init) {
    if (!GetFormatInfo(desc.Format).known) return;
    const uint32_t mipCount = desc.MipLevels ? desc.MipLevels : FullMipCount(desc.Width, desc.Height);

    DumpState::Job job{hash, desc.Format, desc.Width, desc.Height, {}, {}, 0};
    for (uint32_t mip = 0; mip < mipCount; ++mip) {
        SurfaceLayout layout;
        ComputeSurfaceLayout(desc.Format, desc.Width >> mip, desc.Height >> mip, layout);
        if (!init[mip].pSysMem || init[mip].SysMemPitch < layout.rowPitch) break;
        job.bytes += size_t(init[mip].SysMemPitch) * layout.rows;
    }
    if (job.bytes == 0) return;

    {
        std::lock_guard lock(dump_->mutex);
        if (dump_->pendingBytes + job.bytes > kMaxPendingDumpBytes) return;
        if (!dump_->seen.insert(hash).second) return;
        dump_->inFlight.insert(hash);
        dump_->pendingBytes += job.bytes;
    }
    // Copy outside the lock; the game frees its buffer after CreateTexture2D returns.
    for (uint32_t mip = 0; mip < mipCount; ++mip) {
        SurfaceLayout layout;
        ComputeSurfaceLayout(desc.Format, desc.Width >> mip, desc.Height >> mip, layout);
        if (!init[mip].pSysMem || init[mip].SysMemPitch < layout.rowPitch) break;
        const auto* src = static_cast<const uint8_t*>(init[mip].pSysMem);
        job.mips.emplace_back(src, src + size_t(init[mip].SysMemPitch) * layout.rows);
        job.pitches.push_back(init[mip].SysMemPitch);
    }
    {
        std::lock_guard lock(dump_->mutex);
        dump_->jobs.push_back(std::move(job));
    }
    dump_->wake.notify_one();
}

void Runtime::DumpThread() {
    for (;;) {
        DumpState::Job job;
        {
            std::unique_lock lock(dump_->mutex);
            dump_->wake.wait(lock, [this] { return !dump_->jobs.empty(); });
            job = std::move(dump_->jobs.front());
            dump_->jobs.pop_front();
        }
        std::vector<MipData> mips;
        for (size_t i = 0; i < job.mips.size(); ++i) mips.push_back({job.mips[i].data(), job.pitches[i]});
        const std::vector<uint8_t> file = WriteDds(job.format, job.width, job.height, mips);

        const fs::path folder = config()->originalFolder;
        std::error_code ec;
        fs::create_directories(folder, ec);
        const fs::path target = folder / (std::to_wstring(job.hash) + L".dds");
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (out) out.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
        out.close();
        if (out.fail()) {
            // No half-written file in the dump folder. Not retried until a redump request, so an
            // unwritable folder can't make every texture load copy and fail again.
            fs::remove(target, ec);
            Log(false, "Failed to save texture to file: %ls", target.c_str());
        } else {
            Log(true, "Saved original texture %u (%ux%u, %s)", job.hash, job.width, job.height, FormatName(job.format));
        }

        std::lock_guard lock(dump_->mutex);
        dump_->pendingBytes -= job.bytes;
        dump_->inFlight.erase(job.hash);
    }
}

void Runtime::Log(bool debug, const char* fmt, ...) {
    auto cfg = config();
    if (!cfg || !cfg->logEnabled || (debug && !cfg->logDebug)) return;

    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    SYSTEMTIME t;
    GetLocalTime(&t);
    EnterCriticalSection(&logLock_);
    if (logFile_) {
        fprintf(logFile_, "%04u-%02u-%02u %02u:%02u:%02u.%03u [%5lu] %s\n", t.wYear, t.wMonth, t.wDay, t.wHour,
                t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId(), msg);
        fflush(logFile_);
    }
    LeaveCriticalSection(&logLock_);
}

}  // namespace resorep::proxy
