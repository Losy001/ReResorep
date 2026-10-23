#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "settings.h"

namespace resorep::ui {

// Everything the manager persists (reresorep.ini next to the exe).
struct ManagerSettings {
    std::filesystem::path moddedFolder;
    std::filesystem::path originalFolder;
    bool modCreatorMode = false;
    bool saveTextures = false;
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
    std::vector<std::filesystem::path> games;  // game executables

    static std::filesystem::path File();
    void Load();
    bool Save() const;
};

enum class ProxyStatus {
    NotInstalled,
    Installed,       // our current proxy
    Outdated,        // an older ReResorep build
    LegacyResorep,   // the original Java Resorep's dx11proxy
    Missing,         // the exe no longer exists
};
const char* StatusText(ProxyStatus s);

struct GameState {
    std::filesystem::path exe;
    ProxyStatus status = ProxyStatus::NotInstalled;
    bool chainsOtherD3D11 = false;  // a non-Microsoft ori_d3d11.dll (e.g. ReShade) is chained
    std::string proxyVersion;       // version of the installed proxy, if any
};

// The proxy the manager installs: bin/proxy/d3d11.dll next to ReResorep.exe.
std::filesystem::path BundledProxy();

GameState InspectGame(const std::filesystem::path& exe);

// Return an error message, empty on success.
std::string InstallProxy(const std::filesystem::path& exe, const ManagerSettings& settings);
std::string UninstallProxy(const std::filesystem::path& exe);
std::string WriteGameSettings(const std::filesystem::path& exe, const ManagerSettings& settings);

}  // namespace resorep::ui
