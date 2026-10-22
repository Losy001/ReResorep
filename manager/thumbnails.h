#pragma once
#include <d3d11.h>
#include <wrl/client.h>

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace resorep::ui {

struct TextureInfo {
    uint32_t width = 0, height = 0, mipLevels = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool encrypted = false;
    std::string error;  // non-empty if the file could not be decoded
};

// Decodes .dds files on a worker thread into GPU textures for ImGui::Image.
// Only a downscaled mip chain is uploaded, so thousands of dumps stay cheap.
class ThumbnailCache {
public:
    explicit ThumbnailCache(ID3D11Device* device);
    ~ThumbnailCache();

    // Returns the SRV if ready (nullptr while loading or on error) and starts loading on first call.
    // `maxSize` 0 means full resolution (used for the detail preview).
    ID3D11ShaderResourceView* Get(const std::filesystem::path& path, uint32_t maxSize, const TextureInfo** info);

    // Drops entries not used for a while; call once per frame.
    void EndFrame();
    void Invalidate(const std::filesystem::path& path);

private:
    struct Entry {
        bool ready = false;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        TextureInfo info;
        uint64_t lastUsed = 0;
    };
    struct Job {
        std::wstring key;
        std::filesystem::path path;
        uint32_t maxSize;
    };

    void Worker();
    static std::wstring Key(const std::filesystem::path& path, uint32_t maxSize);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    std::unordered_map<std::wstring, Entry> entries_;
    uint64_t frame_ = 0;
    bool stop_ = false;
    std::thread worker_;
};

}  // namespace resorep::ui
