#pragma once
#include <dxgiformat.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

namespace resorep::ui {

enum class TextureKind : uint8_t { Unknown, Color, Normal, Grayscale };
const char* KindName(TextureKind kind);

// What the analyser learns about a texture.
struct TextureTraits {
    TextureKind kind = TextureKind::Unknown;
    // 16x16 "where is the detail" map: luminance edges for colour/grayscale textures,
    // surface tilt for normal maps. A texture and its normal map line up here.
    std::array<uint8_t, 256> detail{};
};

// Classifies a 32x32 RGBA sample (values as sampled by the GPU) of a texture in `format`.
TextureTraits AnalyzePixels(const float* rgba32x32, DXGI_FORMAT format);
// Correlation of two detail maps, -1..1 (0 when either has no detail).
float DetailSimilarity(const TextureTraits& a, const TextureTraits& b);

// Analyses textures on a worker thread with its own D3D11 device, and caches results
// on disk keyed by path + size + modified time.
class TextureAnalyzer {
public:
    explicit TextureAnalyzer(std::filesystem::path cacheFile, bool useWarp = false);
    ~TextureAnalyzer();

    // Unknown until analysed; the first call queues the file.
    TextureKind Kind(const std::filesystem::path& path, int64_t modified, uint64_t size);
    std::optional<TextureTraits> Traits(const std::filesystem::path& path, int64_t modified, uint64_t size);
    size_t Pending();

    // Synchronous analysis on the calling thread (used by tests).
    bool AnalyzeNow(const std::filesystem::path& path, TextureTraits& out);

private:
    struct Entry {
        int64_t modified;
        uint64_t size;
        bool done;
        TextureTraits traits;
    };
    struct Gpu;

    Entry* Lookup(const std::filesystem::path& path, int64_t modified, uint64_t size);  // needs mutex_
    void Worker();
    void LoadCache();
    void SaveCache();

    std::filesystem::path cacheFile_;
    bool useWarp_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::wstring> queue_;
    std::unordered_map<std::wstring, Entry> entries_;
    size_t unsaved_ = 0;
    bool stop_ = false;
    std::thread worker_;
};

}  // namespace resorep::ui
