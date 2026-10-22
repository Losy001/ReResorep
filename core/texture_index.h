#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace resorep {

// Mod files are named "<hash>.dds" or "<hash>~<anything>.dds" (the original's rule).
// Returns the hash, or nothing if the name does not follow that pattern.
std::optional<uint32_t> ParseModFileName(const std::wstring& fileName);

// hash -> file. Built once by scanning a folder (non-recursive, like the original),
// instead of the original's directory listing per texture creation.
class TextureIndex {
public:
    void Scan(const std::filesystem::path& folder);

    // Exact match first, then the original's +-1/+-2 tolerance.
    const std::filesystem::path* Find(uint32_t hash) const;
    const std::filesystem::path* FindExact(uint32_t hash) const;

    size_t size() const { return files_.size(); }
    const std::unordered_map<uint32_t, std::filesystem::path>& files() const { return files_; }

private:
    std::unordered_map<uint32_t, std::filesystem::path> files_;
};

}  // namespace resorep
