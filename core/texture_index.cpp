#include "texture_index.h"

#include <cwctype>
#include <system_error>

#include "resorep_hash.h"

namespace resorep {

std::optional<uint32_t> ParseModFileName(const std::wstring& fileName) {
    const size_t dot = fileName.rfind(L'.');
    if (dot == std::wstring::npos || _wcsicmp(fileName.c_str() + dot, L".dds") != 0) return std::nullopt;

    size_t end = 0;
    while (end < dot && std::iswdigit(fileName[end])) ++end;
    if (end == 0 || end > 10) return std::nullopt;
    if (end != dot && fileName[end] != L'~') return std::nullopt;
    // The original compared against "%u", so a leading zero never matched.
    if (fileName[0] == L'0' && end > 1) return std::nullopt;

    const unsigned long long value = std::stoull(fileName.substr(0, end));
    if (value > 0xFFFFFFFFull) return std::nullopt;
    return uint32_t(value);
}

void TextureIndex::Scan(const std::filesystem::path& folder) {
    files_.clear();
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::wstring name = it->path().filename().wstring();
        if (auto hash = ParseModFileName(name)) {
            // Several files for one hash: keep the alphabetically first, which is
            // what the original's sorted std::set lookup returned.
            auto [pos, inserted] = files_.try_emplace(*hash, it->path());
            if (!inserted && name < pos->second.filename().wstring()) pos->second = it->path();
        }
    }
}

const std::filesystem::path* TextureIndex::FindExact(uint32_t hash) const {
    auto it = files_.find(hash);
    return it == files_.end() ? nullptr : &it->second;
}

const std::filesystem::path* TextureIndex::Find(uint32_t hash) const {
    if (files_.empty()) return nullptr;
    for (int delta : kHashTolerance)
        if (auto* p = FindExact(hash + uint32_t(delta))) return p;
    return nullptr;
}

}  // namespace resorep
