#pragma once
#include <filesystem>
#include <map>
#include <string>

namespace resorep {

// Which mods, categories (sub-folders) and single textures of the modded folder are
// switched off. Stored as "disabled=<relative path>" lines in
// modded/reresorep_disabled.ini, so turning something back on restores it exactly.
class DisabledSet {
public:
    static constexpr wchar_t kFileName[] = L"reresorep_disabled.ini";

    bool Load(const std::filesystem::path& moddedRoot);
    bool Save(const std::filesystem::path& moddedRoot) const;

    // `rel` is relative to the modded folder: a folder ("Ash/Skins") or a file ("Ash/123.dds").
    bool IsDisabled(const std::filesystem::path& rel) const;
    // True if `rel` or any folder above it is disabled.
    bool IsEffectivelyDisabled(const std::filesystem::path& rel) const;
    void Set(const std::filesystem::path& rel, bool disabled);
    // Keeps entries pointing at a renamed/moved item.
    void Rename(const std::filesystem::path& from, const std::filesystem::path& to);
    // Forgets `rel` and everything under it (after it was deleted).
    void RemoveUnder(const std::filesystem::path& rel);
    bool empty() const { return entries_.empty(); }

    // Case-insensitive key with '/' separators, used for comparisons.
    static std::wstring Key(const std::filesystem::path& rel);
    // Display/storage form with '/' separators, case kept.
    static std::wstring Slashed(const std::filesystem::path& rel);

private:
    std::map<std::wstring, std::wstring> entries_;  // Key -> Slashed
};

}  // namespace resorep
