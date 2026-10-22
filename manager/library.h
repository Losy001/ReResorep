#pragma once
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace resorep::ui {

struct TextureFile {
    std::filesystem::path path;
    uint32_t hash = 0;
    std::string label;  // text after "~" in the file name
    bool enabled = true;
    uint64_t size = 0;
};

// The modded/ and original/ folders as the UI sees them. Disabled mods live in
// modded/disabled/, which the proxy never scans.
class Library {
public:
    void SetFolders(const std::filesystem::path& modded, const std::filesystem::path& original);
    ~Library();

    // Rescans if a folder changed on disk (cheap to call every frame).
    bool RefreshIfChanged();
    void Refresh();

    const std::vector<TextureFile>& mods() const { return mods_; }
    const std::vector<TextureFile>& dumps() const { return dumps_; }
    bool IsModded(uint32_t hash) const { return enabledHashes_.count(hash) != 0; }
    const std::filesystem::path& moddedFolder() const { return modded_; }
    const std::filesystem::path& originalFolder() const { return original_; }
    std::filesystem::path disabledFolder() const { return modded_ / L"disabled"; }

    // All return an error message, empty on success.
    std::string SetEnabled(const TextureFile& mod, bool enabled);
    std::string Delete(const TextureFile& file);
    std::string Import(const std::filesystem::path& source, uint32_t hash, const std::string& label, bool overwrite);
    std::filesystem::path TargetPath(uint32_t hash, const std::string& label) const;

private:
    void Watch();
    void CloseWatches();

    std::filesystem::path modded_, original_;
    std::vector<TextureFile> mods_, dumps_;
    std::unordered_set<uint32_t> enabledHashes_;
    HANDLE watches_[3] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
};

}  // namespace resorep::ui
