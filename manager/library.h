#pragma once
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <dxgiformat.h>

#include "mod_state.h"

namespace resorep::ui {

struct TextureFile {
    std::filesystem::path path;
    uint32_t hash = 0;
    std::string label;      // text after "~" in the file name
    std::wstring relDir;    // folder relative to modded/ ("" = loose), '/' separated
    bool selfDisabled = false;  // switched off on its own
    bool enabled = true;        // on, and no folder above it is off
    uint64_t size = 0;
    int64_t modified = 0;  // last write time, file_time ticks
    // From the DDS header (0 / UNKNOWN if it could not be read).
    uint32_t width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

// A mod (top-level folder of modded/) or a category inside one (any deeper folder).
struct ModFolder {
    std::wstring rel;  // '/' separated, relative to modded/
    std::string name;
    int depth = 0;     // 1 = mod, 2+ = category
    bool selfDisabled = false;
    bool enabled = true;
    size_t total = 0, active = 0;  // textures below it, and how many of them are on
};

// The modded/ and original/ folders as the UI sees them.
class Library {
public:
    void SetFolders(const std::filesystem::path& modded, const std::filesystem::path& original);
    ~Library();

    // Rescans if a folder changed on disk (cheap to call every frame).
    bool RefreshIfChanged();
    void Refresh();

    const std::vector<TextureFile>& mods() const { return mods_; }
    const std::vector<TextureFile>& dumps() const { return dumps_; }
    const std::vector<ModFolder>& folders() const { return folders_; }  // depth-first order
    bool IsModded(uint32_t hash) const { return active_.count(hash) != 0; }
    // The file the proxy will use for this hash, or nullptr.
    const std::filesystem::path* ActiveMod(uint32_t hash) const;
    const std::filesystem::path& moddedFolder() const { return modded_; }
    const std::filesystem::path& originalFolder() const { return original_; }

    // All return an error message, empty on success.
    std::string SetEnabled(const TextureFile& mod, bool enabled);
    std::string SetFolderEnabled(const std::wstring& rel, bool enabled);
    std::string CreateFolder(const std::wstring& parentRel, const std::string& name);
    std::string MoveTo(const TextureFile& mod, const std::wstring& relDir);
    std::string Delete(const TextureFile& file);
    std::string DeleteFolder(const std::wstring& rel);
    std::string Import(const std::filesystem::path& source, uint32_t hash, const std::string& label, bool overwrite,
                       const std::wstring& relDir = {});
    std::filesystem::path TargetPath(uint32_t hash, const std::string& label, const std::wstring& relDir = {}) const;

private:
    TextureFile MakeFile(const std::filesystem::directory_entry& entry, uint32_t hash);
    void Watch();
    void CloseWatches();
    std::string SaveState();

    std::filesystem::path modded_, original_;
    DisabledSet disabled_;
    std::vector<TextureFile> mods_, dumps_;
    std::vector<ModFolder> folders_;
    std::unordered_map<uint32_t, std::filesystem::path> active_;
    // Header info per file, reused while its size and timestamp are unchanged.
    struct CachedInfo {
        int64_t modified;
        uint64_t size;
        uint32_t width, height;
        DXGI_FORMAT format;
    };
    std::unordered_map<std::wstring, CachedInfo> infoCache_;
    HANDLE watches_[2] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
};

}  // namespace resorep::ui
