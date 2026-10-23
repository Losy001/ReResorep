#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace resorep {

// User-written names for texture hashes ("3123456789=Ash helmet"). Kept in labels.txt
// next to ReResorep.exe; shareable by exporting/importing the same format.
class LabelStore {
public:
    static constexpr wchar_t kFileName[] = L"labels.txt";

    bool Load(const std::filesystem::path& file);
    bool Save(const std::filesystem::path& file) const;

    const std::string* Get(uint32_t hash) const;
    void Set(uint32_t hash, std::string label);  // empty label removes it
    size_t size() const { return labels_.size(); }

    // Accepts "hash=label", "hash,label" and "hash<TAB>label" lines; '#' or ';' start a
    // comment; anything else is skipped. Labels are trimmed.
    static std::vector<std::pair<uint32_t, std::string>> Parse(const std::string& text);
    // Returns how many labels were added or changed.
    size_t Merge(const std::vector<std::pair<uint32_t, std::string>>& entries, bool overwrite);
    std::string Serialize() const;

private:
    std::map<uint32_t, std::string> labels_;
};

}  // namespace resorep
