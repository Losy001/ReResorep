#include "games.h"

#include <algorithm>
#include <cstdlib>
#include <system_error>

#include "theme.h"
#include "win_util.h"

namespace resorep::ui {
namespace fs = std::filesystem;

namespace {
constexpr char kProxyMarker[] = "ReResorepProxyVersion";
// File sizes of the original Resorep proxies (its application.properties), so we
// can recognise and replace them.
constexpr uintmax_t kLegacyProxySizes[] = {63488,  1944576, 1945088, 2001408, 504832, 2001920, 505344,
                                           506368, 124416,  109056,  110592,  132608, 115712};

bool IsLegacyProxy(const fs::path& dll) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(dll, ec);
    return !ec && std::find(std::begin(kLegacyProxySizes), std::end(kLegacyProxySizes), size) !=
                      std::end(kLegacyProxySizes);
}

bool IsOurProxy(const fs::path& dll) { return FileContains(dll, kProxyMarker); }
bool IsAnyProxy(const fs::path& dll) { return IsOurProxy(dll) || IsLegacyProxy(dll); }
bool IsMicrosoftDll(const fs::path& dll) { return FileCompanyName(dll) == L"Microsoft Corporation"; }

std::string Describe(const char* what, const std::error_code& ec) {
    std::string msg = std::string(what) + ": " + ec.message();
    if (ec == std::errc::permission_denied) msg += " (try running ReResorep as administrator)";
    return msg;
}
}  // namespace

fs::path ManagerSettings::File() { return ExeDirectory() / L"reresorep.ini"; }

void ManagerSettings::Load() {
    IniFile ini;
    ini.Load(File());
    const fs::path root = ExeDirectory();
    auto folder = [&](const char* key, const wchar_t* fallback) {
        const std::string v = ini.GetOr(key, "");
        return v.empty() ? root / fallback : fs::path(Utf8ToWide(v));
    };
    moddedFolder = folder(keys::kModdedFolder, L"modded");
    originalFolder = folder(keys::kOriginalFolder, L"original");
    modCreatorMode = ini.GetBool(keys::kModCreatorMode, false);
    saveTextures = ini.GetBool(keys::kSaveTextures, false);
    logEnabled = ini.GetBool(keys::kLogEnabled, false);
    logDebug = ini.GetOr(keys::kLogLevel, "") == "debug";
    sortKey = ini.GetOr("ui_sort", "hash");
    sortDescending = ini.GetBool("ui_sort_descending", false);
    previewRotation = ((std::atoi(ini.GetOr("ui_preview_rotation", "180").c_str()) / 90 % 4 + 4) % 4) * 90;
    previewMirror = ini.GetBool("ui_preview_mirror", false);
    sharpZoom = ini.GetBool("ui_sharp_zoom", true);
    accentColor = ParseHexColor(ini.GetOr("ui_accent_color", ""), kDefaultAccent);
    hideNormals = ini.GetBool("ui_hide_normals", false);
    hideGrayscale = ini.GetBool("ui_hide_grayscale", false);
    hidePacked = ini.GetBool("ui_hide_packed", false);
    onlyBlackIce = ini.GetBool("ui_only_black_ice", false);
    games.clear();
    for (const std::string& g : ini.GetAll("game")) games.emplace_back(Utf8ToWide(g));
}

bool ManagerSettings::Save() const {
    IniFile ini;
    ini.Load(File());
    ini.Set(keys::kModdedFolder, WideToUtf8(moddedFolder.wstring()));
    ini.Set(keys::kOriginalFolder, WideToUtf8(originalFolder.wstring()));
    ini.SetBool(keys::kModCreatorMode, modCreatorMode);
    ini.SetBool(keys::kSaveTextures, saveTextures);
    ini.SetBool(keys::kLogEnabled, logEnabled);
    ini.Set(keys::kLogLevel, logDebug ? "debug" : "info");
    ini.Set("ui_sort", sortKey);
    ini.SetBool("ui_sort_descending", sortDescending);
    ini.Set("ui_preview_rotation", std::to_string(previewRotation));
    ini.SetBool("ui_preview_mirror", previewMirror);
    ini.SetBool("ui_sharp_zoom", sharpZoom);
    ini.Set("ui_accent_color", HexColor(accentColor));
    ini.SetBool("ui_hide_normals", hideNormals);
    ini.SetBool("ui_hide_grayscale", hideGrayscale);
    ini.SetBool("ui_hide_packed", hidePacked);
    ini.SetBool("ui_only_black_ice", onlyBlackIce);
    std::vector<std::string> list;
    for (const auto& g : games) list.push_back(WideToUtf8(g.wstring()));
    ini.SetAll("game", list);
    return ini.Save(File());
}

const char* StatusText(ProxyStatus s) {
    switch (s) {
        case ProxyStatus::NotInstalled: return "Not installed";
        case ProxyStatus::Installed: return "Installed";
        case ProxyStatus::Outdated: return "Update available";
        case ProxyStatus::LegacyResorep: return "Old Resorep proxy";
        case ProxyStatus::Missing: return "Game not found";
    }
    return "";
}

fs::path BundledProxy() { return ExeDirectory() / L"proxy" / L"d3d11.dll"; }

GameState InspectGame(const fs::path& exe) {
    GameState st;
    st.exe = exe;
    if (!fs::exists(exe)) {
        st.status = ProxyStatus::Missing;
        return st;
    }
    const fs::path dir = exe.parent_path();
    const fs::path dll = dir / L"d3d11.dll";
    const fs::path ori = dir / L"ori_d3d11.dll";
    if (fs::exists(dll)) {
        if (IsOurProxy(dll)) st.status = FilesEqual(dll, BundledProxy()) ? ProxyStatus::Installed : ProxyStatus::Outdated;
        else if (IsLegacyProxy(dll)) st.status = ProxyStatus::LegacyResorep;
    }
    st.chainsOtherD3D11 = fs::exists(ori) && !IsMicrosoftDll(ori) && !IsAnyProxy(ori);
    if (st.status == ProxyStatus::Installed || st.status == ProxyStatus::Outdated) {
        st.proxyVersion = FileVersionString(dll);
        if (st.proxyVersion.empty()) st.proxyVersion = "pre-2.0 build";  // built before version info existed
    } else if (st.status == ProxyStatus::LegacyResorep) {
        st.proxyVersion = "1.x";
    }
    return st;
}

std::string WriteGameSettings(const fs::path& exe, const ManagerSettings& s) {
    IniFile ini;
    ini.Set(keys::kOriginalFolder, WideToUtf8(s.originalFolder.wstring()));
    ini.Set(keys::kModdedFolder, WideToUtf8(s.moddedFolder.wstring()));
    ini.SetBool(keys::kModCreatorMode, s.modCreatorMode);
    ini.SetBool(keys::kSaveTextures, s.saveTextures);
    ini.SetBool(keys::kLogEnabled, s.logEnabled);
    ini.Set(keys::kLogFile, WideToUtf8((exe.parent_path() / L"dll.log").wstring()));
    ini.Set(keys::kLogLevel, s.logDebug ? "debug" : "info");
    return ini.Save(exe.parent_path() / kDllSettingsFile) ? std::string() : "Could not write dllsettings.ini";
}

std::string InstallProxy(const fs::path& exe, const ManagerSettings& settings) {
    const fs::path bundled = BundledProxy();
    if (!fs::exists(bundled)) return "proxy\\d3d11.dll is missing next to ReResorep.exe";
    if (!fs::exists(exe)) return "Game executable not found";

    const fs::path dir = exe.parent_path();
    const fs::path dll = dir / L"d3d11.dll";
    const fs::path ori = dir / L"ori_d3d11.dll";
    std::error_code ec;

    if (fs::exists(dll) && !IsAnyProxy(dll)) {
        // The game (or a tool like ReShade) already has a d3d11.dll: keep it and chain to it.
        if (fs::exists(ori)) fs::rename(ori, dir / L"ori_d3d11.dll.bak", ec);
        if (!ec) fs::rename(dll, ori, ec);
        if (ec) return Describe("Could not keep the existing d3d11.dll", ec);
    }
    // The old Resorep copied System32's d3d11.dll here; a stale copy breaks after
    // Windows updates, and our proxy loads the system one directly.
    if (fs::exists(ori) && IsMicrosoftDll(ori)) fs::remove(ori, ec);

    fs::copy_file(bundled, dll, fs::copy_options::overwrite_existing, ec);
    if (ec) return Describe("Could not copy d3d11.dll", ec);
    return WriteGameSettings(exe, settings);
}

std::string UninstallProxy(const fs::path& exe) {
    const fs::path dir = exe.parent_path();
    const fs::path dll = dir / L"d3d11.dll";
    const fs::path ori = dir / L"ori_d3d11.dll";
    std::error_code ec;

    if (fs::exists(dll)) {
        if (!IsAnyProxy(dll)) return "d3d11.dll in the game folder is not a Resorep proxy; leaving it alone";
        fs::remove(dll, ec);
        if (ec) return Describe("Could not remove d3d11.dll", ec);
    }
    if (fs::exists(ori)) {
        if (IsMicrosoftDll(ori) || IsAnyProxy(ori)) fs::remove(ori, ec);
        else fs::rename(ori, dll, ec);  // restore the game's own / other tool's d3d11.dll
        if (ec) return Describe("Could not restore ori_d3d11.dll", ec);
    }
    fs::remove(dir / kDllSettingsFile, ec);
    return {};
}

}  // namespace resorep::ui
