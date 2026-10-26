#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "settings.h"

namespace resorep::ui {

// SHA-256 of a game exe, remembered while the file's size and timestamp stay the same.
struct ExeHash {
    uint64_t size = 0;
    int64_t modified = 0;
    std::string sha256;  // lower-case hex
    // The hash before the exe last changed (patched or replaced), until the user decides
    // whether mods marked for it move to the new hash. "" when nothing is pending.
    std::string previous;
};

// Everything the manager persists (reresorep.ini next to the exe).
struct ManagerSettings {
    // The Default profile: the shared folders every game uses unless it has its own.
    std::filesystem::path moddedFolder;
    std::filesystem::path originalFolder;
    bool modCreatorMode = false;
    bool saveTextures = false;
    bool lowImpact = false;  // the proxy only replaces textures (keys::kLowImpact)
    bool logEnabled = false;
    bool logDebug = false;
    // Texture manager view options.
    std::string sortKey = "hash";
    bool sortDescending = false;
    int previewRotation = 180;  // degrees clockwise: 0, 90, 180, 270
    bool previewMirror = false;
    bool sharpZoom = true;  // zoomed-in preview shows hard-edged texels instead of blending them
    uint32_t accentColor = 0x4296FA;  // 0xRRGGBB
    bool hideNormals = false;
    bool hideGrayscale = false;
    bool hidePacked = false;
    bool onlyBlackIce = false;
    std::vector<std::filesystem::path> games;  // game executables
    std::map<std::wstring, ExeHash> exeHashes;  // by GameKey
    // GameKey -> folder name under games\: the exe's hash (16 hex digits) when Own folders was
    // ticked. Kept when the exe changes later, so the game's mods and dumps stay where they are.
    std::map<std::wstring, std::string> ownFolders;
    std::filesystem::path profile;              // game shown on the Mods/Dumps tabs; empty = Default

    // Whether installed proxies dump textures.
    bool Dumping() const { return modCreatorMode && saveTextures && !lowImpact; }

    static std::filesystem::path File();
    void Load();
    bool Save() const;

    // The exe's SHA-256 as last worked out ("" if never), and whether that is out of date.
    std::string ExeSha(const std::filesystem::path& exe) const;
    bool NeedsHash(const std::filesystem::path& exe) const;
    bool UsesOwnFolders(const std::filesystem::path& exe) const;
    // modded and original folders the game's proxy uses: its own (games\<hash>) or the Default ones.
    std::pair<std::filesystem::path, std::filesystem::path> FoldersFor(const std::filesystem::path& exe) const;
    // Another game, listed or remembered after Remove from list, uses the same own folders
    // (two copies of the same build).
    bool SharesOwnFolders(const std::filesystem::path& exe) const;
    // Takes the game off the list. Its hash and own folders are kept for when it is added back,
    // unless `forget`.
    void RemoveGame(const std::filesystem::path& exe, bool forget);
};

// Case-insensitive key for a game exe path.
std::wstring GameKey(const std::filesystem::path& exe);
// games\<first 16 hex digits of the exe hash> next to ReResorep.exe.
std::filesystem::path GameFolder(const std::string& exeSha256);
// Re-marks mods in `moddedRoot` marked for `from` as made for `to`. Returns how many.
size_t RemarkMods(const std::filesystem::path& moddedRoot, const std::string& from, const std::string& to,
                  const std::string& gameName);

enum class ProxyStatus {
    NotInstalled,
    Installed,       // our current proxy
    Outdated,        // an older ReResorep build
    LegacyResorep,   // the original Java Resorep's dx11proxy
    Missing,         // the exe no longer exists
    Modified,        // our proxy, but damaged or changed since it was built: it does nothing
};
const char* StatusText(ProxyStatus s);
// One of our proxies is in the game folder (current, older or modified).
inline bool HasOurProxy(ProxyStatus s) {
    return s == ProxyStatus::Installed || s == ProxyStatus::Outdated || s == ProxyStatus::Modified;
}

struct GameState {
    std::filesystem::path exe;
    ProxyStatus status = ProxyStatus::NotInstalled;
    bool chainsOtherD3D11 = false;  // a non-Microsoft ori_d3d11.dll (e.g. ReShade) is chained
    std::string proxyVersion;       // version of the installed proxy, if any
    std::string sha256;             // exe hash, "" until worked out
    std::string previousSha256;     // see ExeHash::previous
    bool ownFolders = false;
};
// "Y9S2_NewBloodHM (1a2b3c4d)": the game's folder name, and its exe hash to tell builds apart.
std::string GameLabel(const GameState& g);

// The proxy the manager installs: bin/proxy/d3d11.dll next to ReResorep.exe.
std::filesystem::path BundledProxy();

GameState InspectGame(const std::filesystem::path& exe, const ManagerSettings& settings);
// Reads the whole exe; call off the UI thread.
ExeHash HashExe(const std::filesystem::path& exe);

// Return an error message, empty on success.
std::string InstallProxy(const std::filesystem::path& exe, const ManagerSettings& settings);
std::string UninstallProxy(const std::filesystem::path& exe);
// The game's folders (see FoldersFor) and exe hash go into its dllsettings.ini.
std::string WriteGameSettings(const std::filesystem::path& exe, const ManagerSettings& settings);
// Asks the proxy in a running game to dump every texture again as the game loads it.
std::string RequestRedump(const std::filesystem::path& exe);

}  // namespace resorep::ui
