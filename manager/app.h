#pragma once
#include <windows.h>
#include <d3d11.h>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "games.h"
#include "library.h"
#include "thumbnails.h"

namespace resorep::ui {

class App {
public:
    App(HWND hwnd, ID3D11Device* device);
    void Frame();  // builds one ImGui frame
    void OnDropFiles(const std::vector<std::filesystem::path>& files);

private:
    enum class Tab { Mods, Dumps, Games, Settings };
    struct Selection {
        std::filesystem::path path;
        bool isMod = false;
    };
    enum class ModView { All, Loose, Folder };
    struct PendingImport {
        std::filesystem::path source;
        std::wstring relDir;
        char hash[16] = {};
        char label[64] = {};
    };

    void DrawToolbarMods();
    void DrawSidebar();
    void DrawFolderNode(size_t& index);
    void DrawFolderPopups();
    void DrawMoveMenu(const TextureFile& file);
    bool InView(const TextureFile& file) const;
    std::wstring ImportDir() const { return modView_ == ModView::Folder ? viewFolder_ : std::wstring(); }
    void DrawToolbarDumps();
    void DrawGrid(const std::vector<TextureFile>& files, bool isMod);
    void DrawCard(const TextureFile& file, bool isMod, float size);
    void DrawDetails();
    void DrawGames();
    void DrawSettings();
    void DrawImportPopup();
    void DrawStatusBar();

    void ImportFiles(const std::vector<std::filesystem::path>& files, const std::wstring& relDir);
    void ReplaceDump(uint32_t hash);
    void ToggleEnabled(const TextureFile& file);
    void Defer(std::function<void()> action) { deferred_.push_back(std::move(action)); }
    void ApplySettings();
    void RefreshGames();
    void Status(const std::string& error, const std::string& success);
    const TextureFile* FindSelected() const;

    HWND hwnd_;
    ThumbnailCache thumbs_;
    Library library_;
    ManagerSettings settings_;
    std::vector<GameState> games_;

    Tab tab_ = Tab::Mods;
    std::optional<Selection> selection_;
    char filter_[32] = {};
    bool showDisabled_ = true;
    bool hideModdedDumps_ = false;
    float thumbSize_ = 128.0f;
    ModView modView_ = ModView::All;
    std::wstring viewFolder_;
    std::wstring newFolderParent_;
    char newFolderName_[64] = {};
    bool openNewFolderPopup_ = false;
    std::wstring deleteFolder_;
    bool openDeleteFolderPopup_ = false;
    std::vector<PendingImport> pendingImports_;
    bool openImportPopup_ = false;
    std::vector<std::function<void()>> deferred_;

    std::string statusText_;
    bool statusIsError_ = false;
    std::chrono::steady_clock::time_point statusTime_;
};

}  // namespace resorep::ui
