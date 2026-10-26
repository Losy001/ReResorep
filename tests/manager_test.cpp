// Checks for the manager's per-game logic: which mod wins per Siege build (it must match the
// proxy's index), the saved game settings, exe hashing and moving mod marks.
// Run: build\Release\manager_test.exe
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "games.h"
#include "library.h"
#include "mod_state.h"
#include "settings.h"
#include "texture_index.h"
#include "win_util.h"

namespace fs = std::filesystem;
using namespace resorep;
using namespace resorep::ui;

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

static fs::path TempDir(const wchar_t* name) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const fs::path dir = fs::path(tmp) / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

static void Touch(const fs::path& p, const std::string& text = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

static std::wstring ToUpperForTest(std::wstring s) {
    for (wchar_t& c : s) c = wchar_t(towupper(c));
    return s;
}

static const TextureFile* Find(const Library& lib, const wchar_t* name) {
    for (const TextureFile& m : lib.mods())
        if (m.path.filename() == name) return &m;
    return nullptr;
}

// Several mods replace hash 100: one for any game, two marked for build A, one for build B.
static void TestWinnersPerBuild() {
    const fs::path root = TempDir(L"reresorep_manager_test");
    const fs::path modded = root / L"modded", original = root / L"original";
    const std::string buildA(64, 'a'), buildB(64, 'b');
    Touch(modded / L"Shared/100~shared.dds");
    Touch(modded / L"A1/100~a1.dds");
    Touch(modded / L"A2/100~a2.dds");
    Touch(modded / L"B/100~b.dds");
    WriteModGame(modded / L"A1", buildA, "");
    WriteModGame(modded / L"A2", buildA, "");
    WriteModGame(modded / L"B", buildB, "");

    // Default folders: every build counts. "a1" sorts before "shared", which sorts after "b".
    Library lib;
    lib.SetFolders(modded, original);
    CHECK(lib.winners().size() == 3);  // "", A, B
    CHECK(Find(lib, L"100~a1.dds") && !Find(lib, L"100~a1.dds")->unused);    // wins in A
    CHECK(Find(lib, L"100~a2.dds") && Find(lib, L"100~a2.dds")->unused);     // loses to a1 in A
    CHECK(Find(lib, L"100~b.dds") && !Find(lib, L"100~b.dds")->unused);      // wins in B
    CHECK(Find(lib, L"100~shared.dds") && !Find(lib, L"100~shared.dds")->unused);  // wins in unmarked builds
    CHECK(lib.ActiveMod(100) && lib.ActiveMod(100)->filename() == L"100~shared.dds");
    for (const TextureFile& m : lib.mods()) CHECK(!m.otherBuild);

    // Each build's winner is what that game's proxy would load.
    for (const auto& [build, winners] : lib.winners()) {
        TextureIndex index;
        index.Scan(modded, build.empty() ? std::string(64, 'f') : build);
        CHECK(index.FindExact(100) && winners.count(100) && *index.FindExact(100) == winners.at(100));
    }

    // As build A's own folders: B's mod is for another build, the shared one loses to a1.
    lib.SetGame(buildA);
    CHECK(lib.winners().size() == 1);
    CHECK(Find(lib, L"100~b.dds")->otherBuild && !Find(lib, L"100~b.dds")->unused);
    CHECK(Find(lib, L"100~shared.dds")->unused && !Find(lib, L"100~a1.dds")->unused);
    CHECK(lib.ActiveMod(100) && lib.ActiveMod(100)->filename() == L"100~a1.dds");

    // A turned-off mod is neither the winner nor "unused".
    DisabledSet off;
    off.Set(L"A1", true);
    off.Save(modded);
    lib.Refresh();
    CHECK(!Find(lib, L"100~a1.dds")->unused && Find(lib, L"100~a1.dds")->enabled == false);
    CHECK(lib.ActiveMod(100) && lib.ActiveMod(100)->filename() == L"100~a2.dds");

    // Moving marks after a patched exe: only mods marked for the old hash change.
    const std::string patched(64, 'c');
    CHECK(RemarkMods(modded, buildA, patched, "Patched") == 2);
    CHECK(ReadModGame(modded / L"A1") == patched && ReadModGame(modded / L"B") == buildB);
    CHECK(ReadModGame(modded / L"Shared").empty());
    fs::remove_all(root);
}

static void TestSettingsAndHashing() {
    const fs::path root = TempDir(L"reresorep_manager_settings_test");
    const fs::path exe = root / L"Y9S2" / L"RainbowSix.exe";
    Touch(exe, "abc");
    const ExeHash h = HashExe(exe);
    // SHA-256("abc")
    CHECK(h.sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(h.size == 3);

    const fs::path iniFile = ManagerSettings::File();
    const bool hadIni = fs::exists(iniFile);
    CHECK(!hadIni);  // the test runs from build output, never from a real install
    if (hadIni) return;

    ManagerSettings s;
    s.moddedFolder = root / L"modded";
    s.originalFolder = root / L"original";
    s.games = {exe};
    CHECK(!s.ExeSha(exe).size() && s.NeedsHash(exe));
    s.exeHashes[GameKey(exe)] = h;
    CHECK(!s.NeedsHash(exe));
    CHECK(s.FoldersFor(exe).first == s.moddedFolder);  // Default until Own folders is ticked

    s.ownFolders[GameKey(exe)] = h.sha256.substr(0, 16);
    s.profile = exe;
    CHECK(s.FoldersFor(exe).first == GameFolder(h.sha256) / L"modded");
    CHECK(GameFolder(h.sha256).filename() == L"ba7816bf8f01cfea");

    // The exe is patched: a new hash, but the game keeps its folder.
    ExeHash patched = h;
    patched.sha256 = std::string(64, 'd');
    patched.previous = h.sha256;
    s.exeHashes[GameKey(exe)] = patched;
    CHECK(s.FoldersFor(exe).first == GameFolder(h.sha256) / L"modded");
    CHECK(s.Save());

    ManagerSettings loaded;
    loaded.Load();
    CHECK(loaded.games.size() == 1 && GameKey(loaded.games[0]) == GameKey(exe));
    CHECK(loaded.ExeSha(exe) == patched.sha256);
    CHECK(loaded.exeHashes[GameKey(exe)].previous == h.sha256);
    CHECK(!loaded.NeedsHash(exe));
    CHECK(loaded.UsesOwnFolders(exe) && loaded.FoldersFor(exe) == s.FoldersFor(exe));
    CHECK(GameKey(loaded.profile) == GameKey(exe));
    // Path keys ignore case.
    CHECK(loaded.UsesOwnFolders(fs::path(ToUpperForTest(exe.wstring()))));

    // The proxy's settings carry the game's own folders and its current hash.
    loaded.modCreatorMode = loaded.saveTextures = loaded.lowImpact = true;
    CHECK(!loaded.Dumping());  // low impact mode wins
    CHECK(WriteGameSettings(exe, loaded).empty());
    IniFile dll;
    CHECK(dll.Load(exe.parent_path() / kDllSettingsFile));
    CHECK(dll.GetBool(keys::kLowImpact, false));
    CHECK(!dll.GetBool(keys::kSaveTextures, true));  // older proxies don't know low_impact_mode
    loaded.lowImpact = false;
    CHECK(dll.GetOr(keys::kGameSha256, "") == patched.sha256);
    CHECK(fs::path(Utf8ToWide(dll.GetOr(keys::kModdedFolder, ""))) == GameFolder(h.sha256) / L"modded");
    CHECK(fs::is_directory(GameFolder(h.sha256) / L"original"));

    // Removed from the list and added back: it still has its own folders and its hash.
    loaded.RemoveGame(exe, false);
    CHECK(loaded.games.empty() && loaded.profile.empty());
    CHECK(loaded.Save());
    ManagerSettings removed;
    removed.Load();
    CHECK(removed.games.empty());
    removed.games = {exe};
    CHECK(removed.UsesOwnFolders(exe) && removed.FoldersFor(exe) == s.FoldersFor(exe));
    CHECK(removed.ExeSha(exe) == patched.sha256 && removed.exeHashes[GameKey(exe)].previous == h.sha256);

    // A second copy of the same build shares its folders, even while the first is off the list.
    const fs::path copy = root / L"Copy" / L"RainbowSix.exe";
    removed.ownFolders[GameKey(copy)] = h.sha256.substr(0, 16);
    removed.RemoveGame(exe, false);
    CHECK(removed.SharesOwnFolders(copy) && removed.SharesOwnFolders(exe));
    removed.ownFolders.erase(GameKey(copy));
    CHECK(!removed.SharesOwnFolders(exe));
    removed.games = {exe};

    // Removed completely: added back, it starts over on the Default folders.
    removed.RemoveGame(fs::path(ToUpperForTest(exe.wstring())), true);
    CHECK(removed.Save());
    ManagerSettings forgotten;
    forgotten.Load();
    forgotten.games = {exe};
    CHECK(!forgotten.UsesOwnFolders(exe) && forgotten.FoldersFor(exe).first == s.moddedFolder);
    CHECK(forgotten.ExeSha(exe).empty() && forgotten.NeedsHash(exe));

    fs::remove(iniFile);
    fs::remove_all(GameFolder(h.sha256).parent_path());
    fs::remove_all(root);
}

// Changes to several selected textures: saved and rescanned once, at the end of the batch.
static void TestBatch() {
    const fs::path root = TempDir(L"reresorep_batch_test");
    const fs::path modded = root / L"modded", original = root / L"original";
    Touch(modded / L"201.dds");
    Touch(modded / L"202.dds");
    Touch(modded / L"203.dds");
    fs::create_directories(modded / L"Mod");
    Library lib;
    lib.SetFolders(modded, original);
    const std::vector<TextureFile> mods = lib.mods();
    CHECK(mods.size() == 3);

    const std::string e = lib.Batch([&] {
        for (const TextureFile& m : mods) CHECK(lib.SetEnabled(m, false).empty());
        CHECK(lib.mods().size() == 3 && lib.mods()[0].enabled);  // not rescanned yet
        CHECK(lib.MoveTo(mods[0], L"Mod").empty());
        CHECK(lib.MoveTo(mods[1], L"Mod").empty());
    });
    CHECK(e.empty());
    CHECK(lib.mods().size() == 3);
    for (const TextureFile& m : lib.mods()) CHECK(!m.enabled);  // moved ones stay turned off
    CHECK(Find(lib, L"201.dds") && Find(lib, L"201.dds")->relDir == L"Mod");
    CHECK(Find(lib, L"203.dds") && Find(lib, L"203.dds")->relDir.empty());
    DisabledSet saved;
    saved.Load(modded);
    CHECK(saved.IsDisabled(L"Mod/202.dds") && saved.IsDisabled(L"203.dds"));
    fs::remove_all(root);
}

// A proxy in the game folder that fails its seal shows as Modified; an intact one doesn't.
static void TestProxyStatus(const fs::path& builtProxy) {
    const fs::path root = TempDir(L"reresorep_proxy_status_test");
    const fs::path exe = root / L"Game" / L"RainbowSix.exe";
    Touch(exe, "game");
    const fs::path dll = exe.parent_path() / L"d3d11.dll";
    fs::copy_file(builtProxy, dll);
    ManagerSettings s;
    // Not the bundled copy (there is none next to this test), but intact.
    CHECK(InspectGame(exe, s).status == ProxyStatus::Outdated);
    std::ofstream(dll, std::ios::binary | std::ios::app) << "appended";
    const GameState g = InspectGame(exe, s);
    CHECK(g.status == ProxyStatus::Modified && HasOurProxy(g.status));
    CHECK(!InstallProxy(exe, s).empty());  // no intact bundled proxy to install
    CHECK(UninstallProxy(exe).empty() && !fs::exists(dll));  // still recognised as ours
    fs::remove_all(root);
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    TestWinnersPerBuild();
    TestSettingsAndHashing();
    TestBatch();
    if (argc > 1) TestProxyStatus(argv[1]);
    else CHECK(!"pass the built d3d11.dll");
    fs::remove(ExeDirectory() / L"texture_info.cache");  // written by the Library above
    std::printf(g_failures ? "%d check(s) failed\n" : "all manager checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
