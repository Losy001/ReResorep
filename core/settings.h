#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace resorep {

// Minimal key=value file (UTF-8), the format of the original dllsettings.ini.
// Keeps line order and unknown keys so round-tripping a hand-edited file is safe.
class IniFile {
public:
    bool Load(const std::filesystem::path& path);
    bool Save(const std::filesystem::path& path) const;

    std::optional<std::string> Get(const std::string& key) const;
    std::string GetOr(const std::string& key, const std::string& fallback) const;
    bool GetBool(const std::string& key, bool fallback) const;
    void Set(const std::string& key, const std::string& value);
    void SetBool(const std::string& key, bool value) { Set(key, value ? "true" : "false"); }

    // Repeated keys (the manager stores one "game=" line per hooked exe).
    std::vector<std::string> GetAll(const std::string& key) const;
    void SetAll(const std::string& key, const std::vector<std::string>& values);

private:
    std::vector<std::pair<std::string, std::string>> entries_;
};

// Keys understood by the proxy; names match the original Resorep.
namespace keys {
inline constexpr char kOriginalFolder[] = "original_textures_folder";
inline constexpr char kModdedFolder[] = "modded_textures_folder";
inline constexpr char kSaveTextures[] = "save_textures";
inline constexpr char kModCreatorMode[] = "mod_creator_mode_enabled";
inline constexpr char kLogEnabled[] = "dll_log_enabled";
inline constexpr char kLogFile[] = "dll_log_file";
inline constexpr char kLogLevel[] = "dll_log_level";
}  // namespace keys

inline constexpr wchar_t kDllSettingsFile[] = L"dllsettings.ini";
// Dropped next to the proxy by the manager's Redump button: the running game dumps every
// texture again (overwriting) as it loads it. The proxy deletes it once seen.
inline constexpr wchar_t kRedumpRequestFile[] = L"reresorep_redump.request";

std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);

}  // namespace resorep
