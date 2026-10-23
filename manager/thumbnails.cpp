#include "thumbnails.h"

#include <algorithm>
#include <vector>

#include "dds.h"

namespace resorep::ui {
namespace {
constexpr size_t kMaxEntries = 1500;
constexpr uint64_t kEvictAfterFrames = 300;

}  // namespace

ThumbnailCache::ThumbnailCache(ID3D11Device* device) : device_(device), worker_([this] { Worker(); }) {}

ThumbnailCache::~ThumbnailCache() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    worker_.join();
}

std::wstring ThumbnailCache::Key(const std::filesystem::path& path, uint32_t maxSize) {
    return path.wstring() + L"|" + std::to_wstring(maxSize);
}

ID3D11ShaderResourceView* ThumbnailCache::Get(const std::filesystem::path& path, uint32_t maxSize,
                                              const TextureInfo** info) {
    const std::wstring key = Key(path, maxSize);
    std::lock_guard lock(mutex_);
    auto [it, inserted] = entries_.try_emplace(key);
    it->second.lastUsed = frame_;
    if (inserted) {
        jobs_.push_front({key, path, maxSize});  // newest first: what is on screen now
        wake_.notify_one();
    }
    if (info) *info = it->second.ready ? &it->second.info : nullptr;
    return it->second.ready ? it->second.srv.Get() : nullptr;
}

void ThumbnailCache::Invalidate(const std::filesystem::path& path) {
    const std::wstring prefix = path.wstring() + L"|";
    std::lock_guard lock(mutex_);
    std::erase_if(entries_, [&](const auto& e) { return e.second.ready && e.first.starts_with(prefix); });
}

void ThumbnailCache::EndFrame() {
    std::lock_guard lock(mutex_);
    ++frame_;
    if (entries_.size() <= kMaxEntries) return;
    std::erase_if(entries_, [&](const auto& e) { return e.second.ready && e.second.lastUsed + kEvictAfterFrames < frame_; });
}

void ThumbnailCache::Worker() {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
            if (!entries_.count(job.key)) continue;  // evicted before we got to it
        }

        Entry result;
        result.ready = true;
        DdsImage img;
        // Only the mips needed for this size are read from disk.
        if (!LoadTextureMips(job.path.wstring(), job.maxSize, img, &result.info.encrypted, &result.info.error)) {
            if (result.info.error.empty()) result.info.error = "cannot decode";
        } else {
            result.info.width = img.width;
            result.info.height = img.height;
            result.info.mipLevels = img.mipLevels;
            result.info.format = img.format;
            if (img.isVolume) {
                result.info.error = "volume textures cannot be previewed";
            } else {
                const uint32_t start = 0;
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width = std::max(1u, img.width >> start);
                desc.Height = std::max(1u, img.height >> start);
                desc.MipLevels = img.mipLevels - start;
                desc.ArraySize = 1;
                desc.Format = img.format;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_IMMUTABLE;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                std::vector<D3D11_SUBRESOURCE_DATA> sub;
                for (uint32_t m = start; m < img.mipLevels; ++m) {
                    const DdsSubresource& s = img.subresources[m];
                    sub.push_back({img.data.data() + s.offset, s.rowPitch, s.slicePitch});
                }
                Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
                D3D11_SHADER_RESOURCE_VIEW_DESC view{};
                view.Format = ShaderViewFormat(img.format);
                view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                view.Texture2D.MipLevels = desc.MipLevels;
                // D3D11 devices are free-threaded, so the upload happens here, off the UI thread.
                if (FAILED(device_->CreateTexture2D(&desc, sub.data(), &tex)) ||
                    FAILED(device_->CreateShaderResourceView(tex.Get(), &view, &result.srv)))
                    result.info.error = "GPU rejected this format";
            }
        }

        std::lock_guard lock(mutex_);
        auto it = entries_.find(job.key);
        if (it != entries_.end()) {
            result.lastUsed = it->second.lastUsed;
            it->second = std::move(result);
        }
    }
}

}  // namespace resorep::ui
