#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "analyzer.h"
#include "games.h"
#include "labels.h"
#include "mod_package.h"
#include "library.h"
#include "thumbnails.h"
#include "win_util.h"

namespace resorep::ui {

class App {
public:
    App(HWND hwnd, ID3D11Device* device);
    void Frame();  // builds one ImGui frame
    void OnDropFiles(const std::vector<std::filesystem::path>& files);

private:
    enum class Tab { Mods, Dumps, Games, Settings, About };
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
    void DrawMoveMenu(const TextureFile& file, bool copy);  // "Move to" or "Copy to"
    void DrawAddToModMenu(const TextureFile& dump);
    void DrawAddToModItems(const TextureFile& dump);  // the menu's entries, also used by the details button
    void AddDumpToMod(const TextureFile& dump, const std::wstring& rel);
    bool InView(const TextureFile& file) const;
    std::wstring ImportDir() const { return modView_ == ModView::Folder ? viewFolder_ : std::wstring(); }
    void DrawToolbarDumps();
    void DrawViewOptions(bool isMod);
    void SortVisible(std::vector<const TextureFile*>& files, bool isMod) const;
    // Draws a texture with the chosen preview rotation/mirroring, fitted into a box.
    // extraTurns/extraMirror add to the global setting (used by the details preview's own buttons).
    void DrawPreview(ImDrawList* dl, ID3D11ShaderResourceView* srv, uint32_t w, uint32_t h, ImVec2 boxMin,
                     ImVec2 boxSize, ImU32 tint, int extraTurns = 0, bool extraMirror = false) const;
    ImVec2 PreviewSize(uint32_t w, uint32_t h, float box, int extraTurns = 0) const;
    void DrawGrid(const std::vector<TextureFile>& files, bool isMod);
    void DrawCard(const TextureFile& file, bool isMod, float size);
    void DrawDetails();
    void DrawGames();
    void DrawSettings();
    void DrawAbout();
    void DrawImportPopup();
    void DrawStatusBar();
    void DrawDeleteDumpsPopup();
    void DrawSimilarWindow();
    void DrawLabelEditor(const TextureFile& file, float width);

    // Labels and similarity search.
    enum class SimilarMode { Normals, Colors, Any };
    struct SimilarResult {
        std::filesystem::path path;
        bool isMod;
        uint32_t hash;
        float score;
        TextureKind kind;
        uint32_t width, height;
        int64_t modified;
    };
    struct SimilarSearch {
        TextureFile source;
        bool sourceIsMod = false;
        SimilarMode mode = SimilarMode::Normals;
        bool computed = false;
        size_t notAnalysed = 0;
        std::vector<SimilarResult> results;
    };
    const std::string& DisplayLabel(const TextureFile& file) const;
    bool MatchesFilter(const TextureFile& file) const;
    size_t CountMatches(const std::vector<TextureFile>& files) const;
    void DrawSearch(float y);
    TextureKind KindOf(const TextureFile& file) { return analyzer_->Kind(file.path, file.modified, file.size); }
    void SetLabel(uint32_t hash, const std::string& label);
    void ImportLabels();
    void ExportLabels();
    void OpenSimilar(const TextureFile& file, bool isMod, SimilarMode mode);
    void ComputeSimilar();
    void QueueAnalysis();

    // Mod packages (.zip).
    struct ExportDialog {
        std::wstring rel;
        std::string name;
        size_t textures = 0;
        bool includeTurnedOff = true;
        bool includeLabels = true;
    };
    struct ImportModDialog {
        std::filesystem::path zip;
        PackageInfo info;
        char name[96] = {};
        bool merge = false;
    };
    void BeginImportMod(const std::filesystem::path& zip);
    void DrawPackagePopups();

    void ImportFiles(const std::vector<std::filesystem::path>& files, const std::wstring& relDir);
    void ReplaceDump(uint32_t hash);
    void ExportAsPng(const TextureFile& file);  // asks where, then writes the PNG
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
    LabelStore labels_;
    std::unique_ptr<TextureAnalyzer> analyzer_;
    std::optional<SimilarSearch> similar_;
    char labelEdit_[128] = {};
    std::optional<uint32_t> labelEditHash_;
    bool focusLabel_ = false;
    struct PreviewTweak {
        int quarterTurns = 0;  // clockwise
        bool mirror = false;
        float zoom = 1.0f;                // 1 = whole texture fits the preview
        ImVec2 centre = {0.5f, 0.5f};     // point of the picture (0..1 across it) at the middle of the view
    };
    std::unordered_map<uint32_t, PreviewTweak> previewTweaks_;  // per hash, this session only
    Microsoft::WRL::ComPtr<ID3D11SamplerState> pointSampler_;   // for the sharp zoomed preview
    std::optional<ExportDialog> exportDialog_;
    std::optional<ImportModDialog> importModDialog_;
    bool openExportPopup_ = false;
    bool openImportModPopup_ = false;
    bool openDeleteDumpsPopup_ = false;
    std::future<size_t> deleteDumpsJob_;  // result: how many dumps could not be deleted
    std::shared_ptr<std::atomic<size_t>> deleteProgress_;  // files processed so far
    // Games tab: running games and the Close button's background job.
    static constexpr DWORD kCloseGraceMs = 10000;
    std::unordered_map<std::wstring, bool> gameRunning_;
    std::chrono::steady_clock::time_point lastRunningCheck_;
    std::future<CloseResult> closeJob_;
    std::filesystem::path closingExe_;
    size_t deleteTotal_ = 0;

    Tab tab_ = Tab::Mods;
    std::optional<Selection> selection_;
    char filter_[64] = {};          // the search field in the tab row; applies to Mods and Dumps
    std::optional<Tab> selectTab_;  // switch to this tab on the next frame
    bool showDisabled_ = true;
    bool hideModdedDumps_ = false;
    float thumbSize_ = 128.0f;
    ModView modView_ = ModView::All;
    std::wstring viewFolder_;
    std::wstring newFolderParent_;
    std::optional<TextureFile> addAfterCreate_;  // dump to add once the "New mod" popup creates it
    char newFolderName_[64] = {};
    bool openNewFolderPopup_ = false;
    std::wstring deleteFolder_;
    bool openDeleteFolderPopup_ = false;
    std::wstring renameFolder_;
    char renameName_[64] = {};
    bool openRenameFolderPopup_ = false;
    std::vector<PendingImport> pendingImports_;
    bool openImportPopup_ = false;
    std::vector<std::function<void()>> deferred_;

    std::string statusText_;
    bool statusIsError_ = false;
    std::chrono::steady_clock::time_point statusTime_;
};

}  // namespace resorep::ui
