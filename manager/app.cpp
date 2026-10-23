#include "app.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

#include "dds.h"
#include "theme.h"
#include "licenses.h"
#include "version.h"
#include "mod_state.h"
#include "texture_index.h"
#include "win_util.h"

namespace resorep::ui {
namespace fs = std::filesystem;

namespace {
const ImVec4 kErrorColor(1.0f, 0.45f, 0.4f, 1.0f);
const ImVec4 kOkColor(0.5f, 0.9f, 0.5f, 1.0f);
const ImVec4 kDimColor(0.6f, 0.6f, 0.6f, 1.0f);
constexpr float kDetailsWidth = 340.0f;
constexpr float kSidebarWidth = 260.0f;

ImTextureID ToTexId(ID3D11ShaderResourceView* srv) { return (ImTextureID)(uintptr_t)srv; }

struct SortOption {
    const char* key;
    const char* label;
    bool modsOnly;
};
constexpr SortOption kSortOptions[] = {
    {"hash", "Hash", false},          {"name", "Name", false},
    {"date", "Date modified", false}, {"filesize", "File size", false},
    {"resolution", "Resolution", false}, {"format", "Format", false},
    {"category", "Mod / category", true},
};

std::string FormatDate(int64_t fileTimeTicks) {
    // MSVC's file_clock counts 100 ns ticks since 1601, exactly like FILETIME.
    FILETIME ft{DWORD(uint64_t(fileTimeTicks)), DWORD(uint64_t(fileTimeTicks) >> 32)};
    SYSTEMTIME utc, local;
    if (!fileTimeTicks || !FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        return "-";
    char buf[32];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u", local.wYear, local.wMonth, local.wDay, local.wHour,
             local.wMinute);
    return buf;
}

std::optional<uint32_t> ParseHash(const char* text) {
    if (!*text) return std::nullopt;
    char* end = nullptr;
    const unsigned long long v = strtoull(text, &end, 10);
    if (*end || v > 0xFFFFFFFFull) return std::nullopt;
    return uint32_t(v);
}

std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

fs::path LabelsFile() { return ExeDirectory() / LabelStore::kFileName; }

const ImU32 kKindColors[] = {0, 0, IM_COL32(120, 150, 255, 255), IM_COL32(200, 200, 200, 255)};
const char* const kKindBadges[] = {"", "", "normal", "depth"};

// Fits a w x h image into a box, keeping aspect ratio.
ImVec2 Fit(uint32_t w, uint32_t h, float box) {
    if (!w || !h) return ImVec2(box, box);
    const float scale = box / float(std::max(w, h));
    return ImVec2(std::max(1.0f, w * scale), std::max(1.0f, h * scale));
}

void HelpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
}  // namespace

App::App(HWND hwnd, ID3D11Device* device) : hwnd_(hwnd), thumbs_(device) {
    settings_.Load();
    ApplyAccentColor(settings_.accentColor);
    labels_.Load(LabelsFile());
    analyzer_ = std::make_unique<TextureAnalyzer>(ExeDirectory() / L"texture_analysis.cache");
    library_.SetFolders(settings_.moddedFolder, settings_.originalFolder);
    QueueAnalysis();
    RefreshGames();
}

void App::QueueAnalysis() {
    // Kind() queues anything not analysed yet; results are cached on disk.
    for (const auto& f : library_.mods()) KindOf(f);
    for (const auto& f : library_.dumps()) KindOf(f);
}

const std::string& App::DisplayLabel(const TextureFile& file) const {
    if (const std::string* user = labels_.Get(file.hash)) return *user;
    return file.label;
}

bool App::MatchesFilter(const TextureFile& f) const {
    if (!filter_[0]) return true;
    if (std::to_string(f.hash).find(filter_) != std::string::npos) return true;
    const std::string needle = Lower(filter_);
    if (Lower(f.label).find(needle) != std::string::npos) return true;
    const std::string* user = labels_.Get(f.hash);
    return user && Lower(*user).find(needle) != std::string::npos;
}

void App::SetLabel(uint32_t hash, const std::string& label) {
    labels_.Set(hash, label);
    Status(labels_.Save(LabelsFile()) ? "" : "Could not write labels.txt next to ReResorep.exe",
           label.empty() ? "Label removed" : "Label saved");
}

void App::ImportLabels() {
    auto files = PickFiles(hwnd_, L"Import labels", {{L"Label files", L"*.txt;*.ini;*.csv"}, {L"All files", L"*.*"}},
                           false);
    if (files.empty()) return;
    std::ifstream in(files[0], std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto entries = LabelStore::Parse(text);
    if (entries.empty()) {
        Status("No labels found in that file (expected lines like 3123456789=Ash helmet)", "");
        return;
    }
    const size_t changed = labels_.Merge(entries, true);
    const bool saved = labels_.Save(LabelsFile());
    Status(saved ? "" : "Could not write labels.txt next to ReResorep.exe",
           "Imported " + std::to_string(entries.size()) + " labels (" + std::to_string(changed) + " new or changed)");
}

void App::ExportLabels() {
    auto target = PickSaveFile(hwnd_, L"Export labels", L"reresorep_labels.txt", {{L"Label files", L"*.txt"}});
    if (!target) return;
    std::ofstream out(*target, std::ios::binary | std::ios::trunc);
    const std::string text = labels_.Serialize();
    out.write(text.data(), std::streamsize(text.size()));
    Status(out ? "" : "Could not write " + ToUtf8(*target),
           "Exported " + std::to_string(labels_.size()) + " labels to " + ToUtf8(target->filename()));
}

void App::OpenSimilar(const TextureFile& file, bool isMod, SimilarMode mode) {
    similar_ = SimilarSearch{file, isMod, mode};
    ComputeSimilar();
}

void App::ComputeSimilar() {
    SimilarSearch& s = *similar_;
    const TextureFile& src = s.source;
    const auto srcTraits = analyzer_->Traits(src.path, src.modified, src.size);
    s.results.clear();
    s.notAnalysed = 0;
    s.computed = srcTraits.has_value();
    if (!srcTraits) return;

    auto consider = [&](const TextureFile& f, bool isMod) {
        if (f.path == src.path || f.hash == src.hash) return;
        // Material textures share an aspect ratio; usually the exact size.
        if (src.width && f.width && uint64_t(src.width) * f.height != uint64_t(f.width) * src.height) return;
        const auto t = analyzer_->Traits(f.path, f.modified, f.size);
        if (!t || t->kind == TextureKind::Unknown) {
            ++s.notAnalysed;
            return;
        }
        if (s.mode == SimilarMode::Normals && t->kind != TextureKind::Normal) return;
        if (s.mode == SimilarMode::Colors && t->kind != TextureKind::Color) return;

        const float detail = std::max(0.0f, DetailSimilarity(*srcTraits, *t));
        const float sizeScore = (f.width == src.width && f.height == src.height) ? 1.0f : 0.4f;
        float score;
        if (!isMod && !s.sourceIsMod) {
            // Games load a material's textures together, so dumps of one material were
            // written within moments of each other.
            const double seconds = std::fabs(double(f.modified - src.modified)) / 1e7;
            const float timeScore = float(1.0 / (1.0 + seconds / 5.0));
            score = 0.65f * detail + 0.15f * sizeScore + 0.20f * timeScore;
        } else {
            score = 0.8f * detail + 0.2f * sizeScore;
        }
        if (score > 0.05f) s.results.push_back({f.path, isMod, f.hash, score, t->kind, f.width, f.height, f.modified});
    };
    for (const auto& f : library_.dumps()) consider(f, false);
    for (const auto& f : library_.mods()) consider(f, true);
    std::sort(s.results.begin(), s.results.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    if (s.results.size() > 50) s.results.resize(50);
}

void App::Status(const std::string& error, const std::string& success) {
    statusIsError_ = !error.empty();
    statusText_ = statusIsError_ ? error : success;
    statusTime_ = std::chrono::steady_clock::now();
}

void App::RefreshGames() {
    games_.clear();
    for (const auto& exe : settings_.games) games_.push_back(InspectGame(exe));
}

void App::ApplySettings() {
    if (!settings_.Save()) {
        Status("Could not save reresorep.ini", "");
        return;
    }
    if (settings_.moddedFolder != library_.moddedFolder() || settings_.originalFolder != library_.originalFolder())
        library_.SetFolders(settings_.moddedFolder, settings_.originalFolder);
    // Installed proxies watch their dllsettings.ini, so this reaches a running game.
    std::string error;
    for (const GameState& g : games_) {
        if (g.status == ProxyStatus::Installed || g.status == ProxyStatus::Outdated) {
            const std::string e = WriteGameSettings(g.exe, settings_);
            if (!e.empty()) error = ToUtf8(g.exe.filename()) + ": " + e;
        }
    }
    Status(error, "Settings saved");
}

const TextureFile* App::FindSelected() const {
    if (!selection_) return nullptr;
    const auto& list = selection_->isMod ? library_.mods() : library_.dumps();
    for (const auto& f : list)
        if (f.path == selection_->path) return &f;
    return nullptr;
}

void App::OnDropFiles(const std::vector<fs::path>& files) {
    std::vector<fs::path> textures;
    for (const fs::path& f : files) {
        if (_wcsicmp(f.extension().c_str(), L".zip") == 0) BeginImportMod(f);  // one package at a time
        else textures.push_back(f);
    }
    if (!textures.empty()) ImportFiles(textures, ImportDir());
}

void App::BeginImportMod(const fs::path& zip) {
    ImportModDialog d;
    d.zip = zip;
    d.info = InspectModPackage(zip);
    if (!d.info.error.empty()) {
        Status(d.info.error, "");
        return;
    }
    snprintf(d.name, sizeof(d.name), "%s", d.info.name.c_str());
    importModDialog_ = std::move(d);
    openImportModPopup_ = true;
}

void App::DrawPackagePopups() {
    if (openExportPopup_) {
        ImGui::OpenPopup("Export mod");
        openExportPopup_ = false;
    }
    if (exportDialog_ && ImGui::BeginPopupModal("Export mod", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ExportDialog& d = *exportDialog_;
        ImGui::Text("Export \"%s\" (%zu textures) as a .zip package.", d.name.c_str(), d.textures);
        ImGui::Checkbox("Include textures and categories that are turned off", &d.includeTurnedOff);
        ImGui::SetItemTooltip("They stay turned off for whoever imports the package.");
        ImGui::Checkbox("Include my labels for these textures", &d.includeLabels);
        ImGui::Spacing();
        if (ImGui::Button("Export...", ImVec2(120, 0))) {
            const ExportDialog copy = d;
            ImGui::CloseCurrentPopup();
            exportDialog_.reset();
            Defer([this, copy] {
                const std::wstring fileName = SafeFolderName(copy.name) + L".zip";
                auto target = PickSaveFile(hwnd_, L"Export mod", fileName.c_str(), {{L"Mod package (.zip)", L"*.zip"}}, L"zip");
                if (!target) return;
                const PackageResult r = ExportModPackage(library_.moddedFolder(), copy.rel, *target,
                                                         {copy.includeTurnedOff, copy.includeLabels}, labels_);
                Status(r.error, "Exported " + std::to_string(r.textures) + " textures (" + FormatBytes(r.bytes) +
                                    (r.labels ? ", " + std::to_string(r.labels) + " labels" : std::string()) + ") to " +
                                    ToUtf8(target->filename()));
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            exportDialog_.reset();
        }
        ImGui::EndPopup();
    }

    if (openImportModPopup_) {
        ImGui::OpenPopup("Import mod");
        openImportModPopup_ = false;
    }
    if (importModDialog_ && ImGui::BeginPopupModal("Import mod", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImportModDialog& d = *importModDialog_;
        ImGui::Text("%s", ToUtf8(d.zip.filename()).c_str());
        ImGui::TextColored(kDimColor, "%zu textures, %s%s%s", d.info.textures, FormatBytes(d.info.bytes).c_str(),
                           d.info.labels ? (", " + std::to_string(d.info.labels) + " labels").c_str() : "",
                           d.info.fromReResorep ? "" : " (plain zip)");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(320);
        ImGui::InputText("Mod name", d.name, sizeof(d.name));
        const std::wstring folder = SafeFolderName(d.name);
        const bool exists = !folder.empty() && fs::exists(library_.moddedFolder() / folder);
        if (folder.empty()) {
            ImGui::TextColored(kErrorColor, "Enter a name for the mod.");
        } else if (exists) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "A mod named \"%s\" already exists.", WideToUtf8(folder).c_str());
            ImGui::Checkbox("Merge into it (replace files with the same name)", &d.merge);
        }
        if (d.info.labels)
            ImGui::TextColored(kDimColor, "Labels are added only for hashes you have not labelled yourself.");
        ImGui::Spacing();
        ImGui::BeginDisabled(folder.empty() || (exists && !d.merge));
        if (ImGui::Button("Import", ImVec2(120, 0))) {
            const ImportModDialog copy = d;
            ImGui::CloseCurrentPopup();
            importModDialog_.reset();
            Defer([this, copy] {
                const PackageResult r =
                    ImportModPackage(copy.zip, library_.moddedFolder(), copy.name, copy.merge, labels_);
                if (r.labels) labels_.Save(LabelsFile());
                library_.Refresh();
                QueueAnalysis();
                std::string msg = "Imported " + std::to_string(r.textures) + " textures into \"" + copy.name + "\"";
                if (r.labels) msg += ", " + std::to_string(r.labels) + " new labels";
                if (r.skipped) msg += " (" + std::to_string(r.skipped) + " entries skipped)";
                Status(r.error, msg);
                if (r.error.empty()) {
                    tab_ = Tab::Mods;
                    modView_ = ModView::Folder;
                    viewFolder_ = SafeFolderName(copy.name);
                }
            });
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            importModDialog_.reset();
        }
        ImGui::EndPopup();
    }
}

void App::ImportFiles(const std::vector<fs::path>& files, const std::wstring& relDir) {
    int imported = 0;
    std::string error;
    for (const fs::path& file : files) {
        const std::wstring name = file.filename().wstring();
        if (auto hash = ParseModFileName(name)) {
            const size_t tilde = name.find(L'~');
            const std::string label =
                tilde == std::wstring::npos ? "" : WideToUtf8(name.substr(tilde + 1, name.rfind(L'.') - tilde - 1));
            const std::string e = library_.Import(file, *hash, label, true, relDir);
            if (e.empty()) ++imported;
            else error = e;
            thumbs_.Invalidate(library_.TargetPath(*hash, label, relDir));
            continue;
        }
        // Not named after a hash: ask. When a dump is selected, assume it replaces that one.
        PendingImport p;
        p.source = file;
        p.relDir = relDir;
        if (const TextureFile* sel = FindSelected(); sel && !selection_->isMod)
            snprintf(p.hash, sizeof(p.hash), "%u", sel->hash);
        snprintf(p.label, sizeof(p.label), "%s", WideToUtf8(file.stem().wstring()).c_str());
        pendingImports_.push_back(p);
        openImportPopup_ = true;
    }
    if (imported || !error.empty()) Status(error, "Imported " + std::to_string(imported) + " texture(s)");
}

void App::ToggleEnabled(const TextureFile& file) {
    Status(library_.SetEnabled(file, file.selfDisabled), file.selfDisabled ? "Turned on" : "Turned off");
}

bool App::InView(const TextureFile& file) const {
    switch (modView_) {
        case ModView::All: return true;
        case ModView::Loose: return file.relDir.empty();
        case ModView::Folder: {
            const std::wstring dir = DisabledSet::Key(file.relDir), view = DisabledSet::Key(viewFolder_);
            return dir == view || dir.starts_with(view + L"/");
        }
    }
    return true;
}

void App::ReplaceDump(uint32_t hash) {
    auto files = PickFiles(hwnd_, L"Choose the replacement texture", {{L"DirectDraw Surface", L"*.dds"}}, false);
    if (files.empty()) return;
    const fs::path target = library_.TargetPath(hash, "");
    const std::string e = library_.Import(files[0], hash, "", true);
    thumbs_.Invalidate(target);
    Status(e, "Texture " + std::to_string(hash) + " now replaced by " + ToUtf8(files[0].filename()));
}

void App::Frame() {
    if (library_.RefreshIfChanged()) {
        if (selection_ && !FindSelected()) selection_.reset();
        QueueAnalysis();
    }
    if (similar_ && !similar_->computed) ComputeSimilar();  // waiting for the source's analysis

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("ReResorep", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float statusHeight = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginTabBar("tabs")) {
        auto tab = [&](const char* name, Tab t) {
            if (ImGui::BeginTabItem(name)) {
                if (tab_ != t) selection_.reset();
                tab_ = t;
                ImGui::EndTabItem();
            }
        };
        char modsLabel[48], dumpsLabel[48];
        snprintf(modsLabel, sizeof(modsLabel), "Mods (%zu)###mods", library_.mods().size());
        snprintf(dumpsLabel, sizeof(dumpsLabel), "Dumps (%zu)###dumps", library_.dumps().size());
        tab(modsLabel, Tab::Mods);
        tab(dumpsLabel, Tab::Dumps);
        tab("Games", Tab::Games);
        tab("Settings", Tab::Settings);
        tab("About", Tab::About);
        ImGui::EndTabBar();
    }

    ImGui::BeginChild("content", ImVec2(0, -statusHeight));
    switch (tab_) {
        case Tab::Mods:
        case Tab::Dumps: {
            const bool mods = tab_ == Tab::Mods;
            mods ? DrawToolbarMods() : DrawToolbarDumps();
            if (mods) {
                DrawSidebar();
                ImGui::SameLine();
            }
            ImGui::BeginChild("grid", ImVec2(-kDetailsWidth - ImGui::GetStyle().ItemSpacing.x, 0),
                              ImGuiChildFlags_Borders);
            DrawGrid(mods ? library_.mods() : library_.dumps(), mods);
            ImGui::EndChild();
            ImGui::SameLine();
            // Always-on scrollbar: otherwise a tall preview adds a scrollbar, which narrows the
            // panel, which shrinks the preview, which removes the scrollbar... every frame.
            ImGui::BeginChild("details", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            DrawDetails();
            ImGui::EndChild();
            break;
        }
        case Tab::Games: DrawGames(); break;
        case Tab::Settings: DrawSettings(); break;
        case Tab::About: DrawAbout(); break;
    }
    ImGui::EndChild();
    DrawStatusBar();
    DrawImportPopup();
    DrawFolderPopups();
    DrawPackagePopups();
    DrawDeleteDumpsPopup();
    ImGui::End();
    DrawSimilarWindow();

    if (closeJob_.valid() && closeJob_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const CloseResult r = closeJob_.get();
        const std::string name = ToUtf8(closingExe_.filename());
        std::string msg = r.killed ? name + " did not close within " + std::to_string(kCloseGraceMs / 1000) +
                                         " seconds, so it was ended forcefully."
                          : r.closed ? "Closed " + name + "."
                                     : name + " was not running.";
        Status(r.error, msg);
        lastRunningCheck_ = {};
    }

    if (deleteDumpsJob_.valid() &&
        deleteDumpsJob_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const size_t failed = deleteDumpsJob_.get();
        const size_t deleted = deleteProgress_ ? deleteProgress_->load() - failed : 0;
        library_.Refresh();
        Status(failed ? std::to_string(failed) + " dump(s) could not be deleted (in use by the game?); " +
                            std::to_string(deleted) + " deleted."
                      : "",
               "Deleted " + std::to_string(deleted) + " dumps. Labels were kept.");
    }

    // Actions that rescan the library run here, after nothing points into it any more.
    auto actions = std::move(deferred_);
    deferred_.clear();
    for (auto& action : actions) action();

    thumbs_.EndFrame();
}

void App::DrawToolbarMods() {
    if (ImGui::Button("Import .dds...")) {
        ImportFiles(PickFiles(hwnd_, L"Import modded textures", {{L"DirectDraw Surface", L"*.dds"}}, true),
                    ImportDir());
    }
    ImGui::SetItemTooltip("Imports into the mod/category selected on the left (or as loose textures).\n"
                          "Files named <hash>.dds or <hash>~name.dds are imported directly.\n"
                          "You can also drag files onto this window.");
    ImGui::SameLine();
    if (ImGui::Button("Open folder")) OpenWithShell(library_.moddedFolder());
    ImGui::SameLine();
    ImGui::Checkbox("Show turned off", &showDisabled_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    ImGui::InputTextWithHint("##filter", "Filter hash / name", filter_, sizeof(filter_));
    DrawViewOptions(true);
}

void App::DrawSidebar() {
    ImGui::BeginChild("sidebar", ImVec2(kSidebarWidth, 0), ImGuiChildFlags_Borders);
    size_t loose = 0;
    for (const auto& m : library_.mods()) loose += m.relDir.empty();
    char text[64];
    snprintf(text, sizeof(text), "All textures (%zu)", library_.mods().size());
    if (ImGui::Selectable(text, modView_ == ModView::All)) modView_ = ModView::All;
    snprintf(text, sizeof(text), "Loose textures (%zu)", loose);
    if (ImGui::Selectable(text, modView_ == ModView::Loose)) modView_ = ModView::Loose;
    ImGui::SetItemTooltip("Textures directly in the modded folder, not in any mod.");

    ImGui::SeparatorText("Mods");
    const auto& folders = library_.folders();
    if (folders.empty())
        ImGui::TextWrapped("No mods yet. A mod is a folder; its sub-folders are categories you can turn off.");
    for (size_t i = 0; i < folders.size();) DrawFolderNode(i);

    ImGui::Spacing();
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("+ New mod", ImVec2(half, 0))) {
        newFolderParent_.clear();
        newFolderName_[0] = 0;
        openNewFolderPopup_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Import mod...", ImVec2(-1, 0))) {
        auto zips = PickFiles(hwnd_, L"Import a mod package", {{L"Mod packages (.zip)", L"*.zip"}}, false);
        if (!zips.empty()) BeginImportMod(zips[0]);
    }
    ImGui::SetItemTooltip("Import a .zip exported from ReResorep, or any zip of .dds textures.\n"
                          "You can also drop a .zip onto this window.");
    ImGui::EndChild();
}

void App::DrawFolderNode(size_t& index) {
    const auto& folders = library_.folders();
    const ModFolder& f = folders[index];
    const bool hasChildren = index + 1 < folders.size() && folders[index + 1].depth > f.depth;
    ImGui::PushID(f.rel.c_str());

    bool on = !f.selfDisabled;
    if (ImGui::Checkbox("##on", &on)) {
        const std::wstring rel = f.rel;
        Defer([this, rel, on] { Status(library_.SetFolderEnabled(rel, on), on ? "Turned on" : "Turned off"); });
    }
    ImGui::SetItemTooltip(f.depth == 1 ? "Turn this whole mod on or off" : "Turn this category on or off");
    ImGui::SameLine(0, 4);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
    if (modView_ == ModView::Folder && DisabledSet::Key(viewFolder_) == DisabledSet::Key(f.rel))
        flags |= ImGuiTreeNodeFlags_Selected;
    if (!f.enabled) ImGui::PushStyleColor(ImGuiCol_Text, kDimColor);
    const bool open = ImGui::TreeNodeEx("node", flags, "%s  %zu/%zu", f.name.c_str(), f.active, f.total);
    if (!f.enabled) ImGui::PopStyleColor();
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        modView_ = ModView::Folder;
        viewFolder_ = f.rel;
    }
    if (!f.enabled && !f.selfDisabled) ImGui::SetItemTooltip("Off because a folder above it is turned off.");

    if (ImGui::BeginPopupContextItem("ctx")) {
        const std::wstring rel = f.rel;
        if (ImGui::MenuItem(f.selfDisabled ? "Turn on" : "Turn off"))
            Defer([this, rel, on = f.selfDisabled] { Status(library_.SetFolderEnabled(rel, on), "Updated"); });
        if (ImGui::MenuItem("New category inside...")) {
            newFolderParent_ = rel;
            newFolderName_[0] = 0;
            openNewFolderPopup_ = true;
        }
        if (ImGui::MenuItem("Import .dds here..."))
            Defer([this, rel] {
                ImportFiles(PickFiles(hwnd_, L"Import modded textures", {{L"DirectDraw Surface", L"*.dds"}}, true), rel);
            });
        if (ImGui::MenuItem("Open folder")) OpenWithShell(library_.moddedFolder() / rel);
        if (ImGui::MenuItem(f.depth == 1 ? "Export mod..." : "Export category as mod...")) {
            exportDialog_ = ExportDialog{rel, f.name, f.total};
            openExportPopup_ = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Move to Recycle Bin...")) {
            deleteFolder_ = rel;
            openDeleteFolderPopup_ = true;
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();

    ++index;
    if (open) {
        while (index < folders.size() && folders[index].depth > f.depth) DrawFolderNode(index);
        ImGui::TreePop();
    } else {
        while (index < folders.size() && folders[index].depth > f.depth) ++index;
    }
}

void App::DrawMoveMenu(const TextureFile& file) {
    if (!ImGui::BeginMenu("Move to")) return;
    auto item = [&](const char* label, const std::wstring& rel, int depth) {
        ImGui::Indent(float(depth) * 10.0f);
        const bool current = DisabledSet::Key(rel) == DisabledSet::Key(file.relDir);
        if (ImGui::MenuItem(label, nullptr, current, !current))
            Defer([this, file, rel] { Status(library_.MoveTo(file, rel), "Moved"); });
        ImGui::Unindent(float(depth) * 10.0f);
    };
    item("(loose, no mod)", L"", 0);
    for (const ModFolder& f : library_.folders()) {
        ImGui::PushID(f.rel.c_str());
        item(f.name.c_str(), f.rel, f.depth - 1);
        ImGui::PopID();
    }
    ImGui::EndMenu();
}

void App::AddDumpToMod(const TextureFile& dump, const std::wstring& rel) {
    const std::string where = rel.empty() ? "loose textures" : "\"" + WideToUtf8(rel) + "\"";
    if (fs::exists(library_.TargetPath(dump.hash, "", rel))) {
        Status(where + " already has a texture for " + std::to_string(dump.hash), "");
        return;
    }
    const std::string e = library_.Import(dump.path, dump.hash, "", false, rel);
    Status(e, "Added " + std::to_string(dump.hash) + " to " + where +
                  ". It looks the same in-game until you edit that copy.");
}

void App::DrawAddToModMenu(const TextureFile& dump) {
    if (!ImGui::BeginMenu("Add to mod")) return;
    // A copy of the dump goes into the mod as <hash>.dds, ready to be edited.
    auto item = [&](const char* label, const std::wstring& rel, int depth) {
        ImGui::Indent(float(depth) * 10.0f);
        const bool already = fs::exists(library_.TargetPath(dump.hash, "", rel));
        if (ImGui::MenuItem(label, nullptr, already, !already))
            Defer([this, dump, rel] { AddDumpToMod(dump, rel); });
        ImGui::Unindent(float(depth) * 10.0f);
    };
    for (const ModFolder& f : library_.folders()) {
        ImGui::PushID(f.rel.c_str());
        item(f.name.c_str(), f.rel, f.depth - 1);
        ImGui::PopID();
    }
    if (!library_.folders().empty()) ImGui::Separator();
    item("(loose, no mod)", L"", 0);
    if (ImGui::MenuItem("New mod...")) {
        addAfterCreate_ = dump;
        newFolderParent_.clear();
        newFolderName_[0] = 0;
        openNewFolderPopup_ = true;
    }
    ImGui::EndMenu();
}

void App::DrawFolderPopups() {
    if (openNewFolderPopup_) {
        ImGui::OpenPopup("New folder");
        openNewFolderPopup_ = false;
    }
    if (ImGui::BeginPopupModal("New folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (newFolderParent_.empty())
            ImGui::TextUnformatted(addAfterCreate_ ? "Name of the new mod (the texture is added to it):"
                                                   : "Name of the new mod:");
        else ImGui::Text("New category inside %s:", WideToUtf8(newFolderParent_).c_str());
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(300);
        const bool enter =
            ImGui::InputText("##name", newFolderName_, sizeof(newFolderName_), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Create", ImVec2(120, 0)) || enter) {
            const std::wstring parent = newFolderParent_;
            const std::string name = newFolderName_;
            std::optional<TextureFile> add = std::exchange(addAfterCreate_, std::nullopt);
            Defer([this, parent, name, add] {
                const std::string e = library_.CreateFolder(parent, name);
                // Same cleanup of the name as the folder on disk got.
                const std::wstring folder = SafeFolderName(name);
                const std::wstring rel = parent.empty() ? folder : parent + L"/" + folder;
                Status(e, "Created " + name);
                if (!e.empty()) return;
                modView_ = ModView::Folder;
                viewFolder_ = rel;
                if (add) AddDumpToMod(*add, rel);
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            addAfterCreate_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (openDeleteFolderPopup_) {
        ImGui::OpenPopup("Delete folder");
        openDeleteFolderPopup_ = false;
    }
    if (ImGui::BeginPopupModal("Delete folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        size_t count = 0;
        for (const ModFolder& f : library_.folders())
            if (DisabledSet::Key(f.rel) == DisabledSet::Key(deleteFolder_)) count = f.total;
        ImGui::Text("Move \"%s\" and its %zu texture(s) to the Recycle Bin?", WideToUtf8(deleteFolder_).c_str(), count);
        if (ImGui::Button("Move to Recycle Bin", ImVec2(180, 0))) {
            const std::wstring rel = deleteFolder_;
            Defer([this, rel] {
                Status(library_.DeleteFolder(rel), "Moved to Recycle Bin");
                if (modView_ == ModView::Folder) modView_ = ModView::All;
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void App::DrawToolbarDumps() {
    if (ImGui::Button("Open folder")) OpenWithShell(library_.originalFolder());
    ImGui::SameLine();
    ImGui::Checkbox("Hide already modded", &hideModdedDumps_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    ImGui::InputTextWithHint("##filter", "Filter hash", filter_, sizeof(filter_));
    if (!settings_.modCreatorMode || !settings_.saveTextures) {
        ImGui::SameLine();
        ImGui::TextColored(kDimColor, "Dumping is off - enable it in Settings.");
    }
    ImGui::SameLine();
    const bool deleting = deleteDumpsJob_.valid();
    ImGui::BeginDisabled(deleting || library_.dumps().empty());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.24f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.14f, 0.14f, 1.0f));
    char deleteLabel[64] = "Delete all dumps...";
    if (deleting && deleteProgress_)
        snprintf(deleteLabel, sizeof(deleteLabel), "Deleting %zu / %zu...###deleteall", deleteProgress_->load(),
                 deleteTotal_);
    if (ImGui::Button(deleteLabel)) openDeleteDumpsPopup_ = true;
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    DrawViewOptions(false);
}

void App::DrawDeleteDumpsPopup() {
    if (openDeleteDumpsPopup_) {
        ImGui::OpenPopup("Delete all dumps");
        openDeleteDumpsPopup_ = false;
    }
    if (!ImGui::BeginPopupModal("Delete all dumps", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    uint64_t bytes = 0;
    for (const auto& d : library_.dumps()) bytes += d.size;
    ImGui::Text("Permanently delete all %zu dumped textures (%s)?", library_.dumps().size(), FormatBytes(bytes).c_str());
    ImGui::TextColored(kDimColor, "%s", ToUtf8(library_.originalFolder()).c_str());
    ImGui::Spacing();
    ImGui::TextColored(kErrorColor, "They are not moved to the Recycle Bin. This cannot be undone.");
    ImGui::BulletText("Your labels are kept (labels.txt). They show again when a texture is dumped again.");
    ImGui::BulletText("Your mods are not touched.");
    ImGui::BulletText("Other files in that folder are left alone.");
    if (settings_.modCreatorMode && settings_.saveTextures)
        ImGui::BulletText("Dumping is still on, so a running game will dump textures again.");
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.24f, 0.24f, 1.0f));
    if (ImGui::Button("Delete permanently", ImVec2(200, 0))) {
        std::vector<fs::path> paths;
        for (const auto& d : library_.dumps()) paths.push_back(d.path);
        deleteTotal_ = paths.size();
        deleteProgress_ = std::make_shared<std::atomic<size_t>>(0);
        // Off the UI thread so thousands of files don't freeze the window; the button shows progress.
        deleteDumpsJob_ = std::async(std::launch::async, [paths = std::move(paths), progress = deleteProgress_] {
            size_t failed = 0;
            for (const fs::path& p : paths) {
                std::error_code ec;
                if (!fs::remove(p, ec) && fs::exists(p, ec)) ++failed;  // already gone counts as done
                ++*progress;
            }
            return failed;
        });
        if (selection_ && !selection_->isMod) selection_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::SortVisible(std::vector<const TextureFile*>& files, bool isMod) const {
    const std::string& key = settings_.sortKey;
    // Returns <0, 0, >0 like strcmp, before applying the direction.
    auto compare = [&](const TextureFile& a, const TextureFile& b) -> int {
        auto cmp = [](auto x, auto y) { return x < y ? -1 : (y < x ? 1 : 0); };
        if (key == "name") {
            const int c = cmp(Lower(DisplayLabel(a)), Lower(DisplayLabel(b)));
            return c ? c : cmp(a.path.filename().wstring(), b.path.filename().wstring());
        }
        if (key == "date") return cmp(a.modified, b.modified);
        if (key == "filesize") return cmp(a.size, b.size);
        if (key == "resolution") {
            const int c = cmp(uint64_t(a.width) * a.height, uint64_t(b.width) * b.height);
            return c ? c : cmp(a.width, b.width);
        }
        if (key == "format") return cmp(std::string(FormatName(a.format)), std::string(FormatName(b.format)));
        if (key == "category" && isMod) return cmp(DisabledSet::Key(a.relDir), DisabledSet::Key(b.relDir));
        return 0;  // "hash": the tie-break below
    };
    const bool desc = settings_.sortDescending;
    std::stable_sort(files.begin(), files.end(), [&](const TextureFile* a, const TextureFile* b) {
        int c = compare(*a, *b);
        if (!c) c = a->hash < b->hash ? -1 : (a->hash > b->hash ? 1 : 0);
        if (!c) return a->path < b->path;
        return desc ? c > 0 : c < 0;
    });
}

ImVec2 App::PreviewSize(uint32_t w, uint32_t h, float box, int extraTurns) const {
    // A quarter turn swaps the on-screen aspect ratio.
    const int k = (settings_.previewRotation / 90 + extraTurns) & 3;
    return k % 2 ? Fit(h, w, box) : Fit(w, h, box);
}

void App::DrawPreview(ImDrawList* dl, ID3D11ShaderResourceView* srv, uint32_t w, uint32_t h, ImVec2 boxMin,
                      ImVec2 boxSize, ImU32 tint, int extraTurns, bool extraMirror) const {
    const ImVec2 fit = PreviewSize(w, h, std::min(boxSize.x, boxSize.y), extraTurns);
    const ImVec2 a(boxMin.x + (boxSize.x - fit.x) * 0.5f, boxMin.y + (boxSize.y - fit.y) * 0.5f);
    const ImVec2 b(a.x + fit.x, a.y + fit.y);
    // Texture corners in clockwise order starting top-left; rotating the picture
    // clockwise by k quarter turns shows corner (i - k) at screen corner i.
    static const ImVec2 kCorners[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const int k = (settings_.previewRotation / 90 + extraTurns) & 3;
    ImVec2 uv[4];
    for (int i = 0; i < 4; ++i) uv[i] = kCorners[(i - k + 4) & 3];
    if (settings_.previewMirror != extraMirror) {
        std::swap(uv[0], uv[1]);
        std::swap(uv[2], uv[3]);
    }
    dl->AddImageQuad(ToTexId(srv), a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y), uv[0], uv[1], uv[2], uv[3], tint);
}

void App::DrawViewOptions(bool isMod) {
    // Second toolbar row: how the grid is ordered and how previews are oriented.
    const SortOption* current = &kSortOptions[0];
    for (const SortOption& o : kSortOptions)
        if (settings_.sortKey == o.key && (isMod || !o.modsOnly)) current = &o;
    bool changed = false;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Sort by");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo("##sort", current->label)) {
        for (const SortOption& o : kSortOptions) {
            if (o.modsOnly && !isMod) continue;
            if (ImGui::Selectable(o.label, &o == current)) {
                settings_.sortKey = o.key;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, 4);
    if (ImGui::ArrowButton("##dir", settings_.sortDescending ? ImGuiDir_Down : ImGuiDir_Up)) {
        settings_.sortDescending = !settings_.sortDescending;
        changed = true;
    }
    ImGui::SetItemTooltip(settings_.sortDescending ? "Descending (largest / newest first)"
                                                   : "Ascending (smallest / oldest first)");

    ImGui::SameLine(0, 20);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Rotate");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    static const char* kRotations[] = {"0\xC2\xB0", "90\xC2\xB0", "180\xC2\xB0", "270\xC2\xB0"};
    int rotation = settings_.previewRotation / 90;
    if (ImGui::Combo("##rotate", &rotation, kRotations, 4)) {
        settings_.previewRotation = rotation * 90;
        changed = true;
    }
    ImGui::SetItemTooltip("Turns previews clockwise. Only changes how they are shown, not the files.");
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Mirror", &settings_.previewMirror);
    ImGui::SetItemTooltip("Flips previews left-to-right (after rotating).");

    ImGui::SameLine(0, 20);
    changed |= ImGui::Checkbox("Hide normal maps", &settings_.hideNormals);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Hide depth / grayscale", &settings_.hideGrayscale);
    ImGui::SetItemTooltip("Textures with no colour and no transparency: depth/height, roughness, masks.\n"
                          "Icons and decals on a transparent background are not hidden.\n"
                          "Texture types are detected automatically in the background.");

    ImGui::SameLine(0, 20);
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("##size", &thumbSize_, 64.0f, 256.0f, "Size %.0f");
    if (changed) settings_.Save();
}

void App::DrawGrid(const std::vector<TextureFile>& files, bool isMod) {
    std::vector<const TextureFile*> visible;
    visible.reserve(files.size());
    for (const auto& f : files) {
        if (isMod && !showDisabled_ && !f.enabled) continue;
        if (isMod && !InView(f)) continue;
        if (!isMod && hideModdedDumps_ && library_.IsModded(f.hash)) continue;
        if (settings_.hideNormals || settings_.hideGrayscale) {
            const TextureKind kind = KindOf(f);
            if (settings_.hideNormals && kind == TextureKind::Normal) continue;
            if (settings_.hideGrayscale && kind == TextureKind::Grayscale) continue;
        }
        if (MatchesFilter(f)) visible.push_back(&f);
    }
    SortVisible(visible, isMod);
    if (visible.empty()) {
        ImGui::TextColored(kDimColor, isMod ? "No textures here yet. Import .dds files or drop them here."
                                            : "No dumped textures. Turn on dumping in Settings and play the game.");
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float cell = thumbSize_ + style.ItemSpacing.x;
    const int perRow = std::max(1, int((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) / cell));
    const int rows = int((visible.size() + perRow - 1) / perRow);
    const float rowHeight = thumbSize_ + ImGui::GetTextLineHeight() * 2 + style.ItemSpacing.y * 2;

    ImGuiListClipper clipper;  // only rows on screen are laid out and thumbnailed
    clipper.Begin(rows, rowHeight);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            for (int col = 0; col < perRow; ++col) {
                const size_t i = size_t(row) * perRow + col;
                if (i >= visible.size()) break;
                if (col) ImGui::SameLine();
                DrawCard(*visible[i], isMod, thumbSize_);
            }
        }
    }
}

void App::DrawCard(const TextureFile& file, bool isMod, float size) {
    ImGui::PushID(file.path.c_str());
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float textH = ImGui::GetTextLineHeight();
    const ImVec2 cardSize(size, size + textH * 2 + 4);
    const bool clicked = ImGui::InvisibleButton("card", cardSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool selected = selection_ && selection_->path == file.path;
    if (clicked) selection_ = Selection{file.path, isMod};

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), IM_COL32(28, 28, 32, 255), 4.0f);

    const TextureInfo* info = nullptr;
    ID3D11ShaderResourceView* srv = thumbs_.Get(file.path, file.modified, uint32_t(size), &info);
    if (srv && info) {
        DrawPreview(dl, srv, info->width, info->height, ImVec2(pos.x + 4, pos.y + 4), ImVec2(size - 8, size - 8),
                    (isMod && !file.enabled) ? IM_COL32(255, 255, 255, 70) : IM_COL32_WHITE);
    } else {
        const char* msg = info ? "can't preview" : "loading...";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        dl->AddText(ImVec2(pos.x + (size - ts.x) * 0.5f, pos.y + (size - ts.y) * 0.5f), IM_COL32(140, 140, 140, 255), msg);
    }

    // Badges
    const fs::path* active = isMod ? library_.ActiveMod(file.hash) : nullptr;
    if (isMod && !file.enabled)
        dl->AddText(ImVec2(pos.x + 6, pos.y + 4), IM_COL32(255, 180, 80, 255),
                    file.selfDisabled ? "off" : "category off");
    else if (active && *active != file.path)
        dl->AddText(ImVec2(pos.x + 6, pos.y + 4), IM_COL32(255, 120, 110, 255), "unused (duplicate)");
    if (!isMod && library_.IsModded(file.hash))
        dl->AddText(ImVec2(pos.x + 6, pos.y + 4), IM_COL32(110, 200, 255, 255), "modded");
    if (info && info->encrypted) {
        const float w = ImGui::CalcTextSize("enc").x;
        dl->AddText(ImVec2(pos.x + size - w - 6, pos.y + 4), IM_COL32(255, 220, 120, 255), "enc");
    }

    if (const TextureKind kind = KindOf(file); kind == TextureKind::Normal || kind == TextureKind::Grayscale) {
        const char* badge = kKindBadges[int(kind)];
        const float w = ImGui::CalcTextSize(badge).x;
        dl->AddRectFilled(ImVec2(pos.x + size - w - 10, pos.y + size - textH - 6), ImVec2(pos.x + size - 2, pos.y + size - 2),
                          IM_COL32(20, 20, 24, 200), 3.0f);
        dl->AddText(ImVec2(pos.x + size - w - 6, pos.y + size - textH - 4), kKindColors[int(kind)], badge);
    }

    const ImU32 border = selected ? ImGui::GetColorU32(AccentColor()) : hovered ? IM_COL32(120, 120, 130, 255) : 0;
    if (border) dl->AddRect(pos, ImVec2(pos.x + size, pos.y + size), border, 4.0f, 0, selected ? 2.5f : 1.0f);

    char line[64];
    snprintf(line, sizeof(line), "%u", file.hash);
    dl->PushClipRect(ImVec2(pos.x, pos.y + size), ImVec2(pos.x + size, pos.y + cardSize.y), true);
    dl->AddText(ImVec2(pos.x + 2, pos.y + size + 2), IM_COL32(230, 230, 230, 255), line);
    const ImVec2 line2(pos.x + 2, pos.y + size + 2 + textH);
    if (settings_.sortKey == "date") {
        dl->AddText(line2, IM_COL32(150, 150, 160, 255), FormatDate(file.modified).c_str());
    } else if (settings_.sortKey == "filesize") {
        dl->AddText(line2, IM_COL32(150, 150, 160, 255), FormatBytes(file.size).c_str());
    } else if (const std::string& label = DisplayLabel(file); !label.empty())
        dl->AddText(line2, labels_.Get(file.hash) ? ImGui::GetColorU32(AccentTextColor()) : IM_COL32(150, 150, 160, 255),
                    label.c_str());
    else if (info && !info->error.empty())
        dl->AddText(ImVec2(pos.x + 2, pos.y + size + 2 + textH), IM_COL32(255, 120, 110, 255), info->error.c_str());
    else if (info) {
        snprintf(line, sizeof(line), "%ux%u %s", file.width, file.height, FormatName(file.format));
        dl->AddText(ImVec2(pos.x + 2, pos.y + size + 2 + textH), IM_COL32(150, 150, 160, 255), line);
    }
    dl->PopClipRect();

    if (ImGui::BeginPopupContextItem("ctx")) {
        selection_ = Selection{file.path, isMod};
        if (isMod) {
            if (ImGui::MenuItem(file.selfDisabled ? "Turn on" : "Turn off")) Defer([this, file] { ToggleEnabled(file); });
            DrawMoveMenu(file);
            const bool encrypted = info && info->encrypted;
            if (ImGui::MenuItem("Open in editor", nullptr, false, !encrypted)) OpenWithShell(file.path);
            if (encrypted) ImGui::SetItemTooltip("Encrypted (resorep1) textures can't be opened by image editors.");
        } else {
            if (ImGui::MenuItem("Replace with .dds...")) Defer([this, hash = file.hash] { ReplaceDump(hash); });
            DrawAddToModMenu(file);
            if (ImGui::MenuItem("Open in editor")) OpenWithShell(file.path);
        }
        if (ImGui::MenuItem("Show in Explorer")) ShowInExplorer(file.path);
        ImGui::Separator();
        if (ImGui::MenuItem("Edit label")) focusLabel_ = true;
        const bool isNormal = KindOf(file) == TextureKind::Normal;
        if (ImGui::MenuItem(isNormal ? "Find its color texture" : "Find its normal map"))
            OpenSimilar(file, isMod, isNormal ? SimilarMode::Colors : SimilarMode::Normals);
        if (ImGui::MenuItem("Find similar textures")) OpenSimilar(file, isMod, SimilarMode::Any);
        ImGui::Separator();
        if (ImGui::MenuItem("Move to Recycle Bin"))
            Defer([this, file] { Status(library_.Delete(file), "Moved to Recycle Bin"); });
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void App::DrawDetails() {
    const TextureFile* file = FindSelected();
    if (!file) {
        ImGui::TextColored(kDimColor, "Select a texture to see details.");
        ImGui::Spacing();
        ImGui::TextWrapped(tab_ == Tab::Mods
                               ? "Mods are .dds files named after the hash of the game texture they replace."
                               : "Dumps are the game's own textures, saved while you play with dumping on. "
                                 "Pick one and choose a replacement to create a mod.");
        return;
    }
    const bool isMod = selection_->isMod;
    const float width = ImGui::GetContentRegionAvail().x;

    // The panel always reserves its scrollbar (see BeginChild), so `width` cannot change
    // with the preview's height; and the box is capped so the details stay in view.
    const float box = std::min(width, ImGui::GetWindowHeight() * 0.5f);
    PreviewTweak& tweak = previewTweaks_[file->hash];
    const TextureInfo* info = nullptr;
    ID3D11ShaderResourceView* srv = thumbs_.Get(file->path, file->modified, 0, &info);
    if (srv && info) {
        const ImVec2 fit = PreviewSize(info->width, info->height, box, tweak.quarterTurns);
        const ImVec2 at(ImGui::GetCursorScreenPos().x + (width - fit.x) * 0.5f, ImGui::GetCursorScreenPos().y);
        ImGui::Dummy(ImVec2(width, fit.y));
        DrawPreview(ImGui::GetWindowDrawList(), srv, info->width, info->height, at, fit, IM_COL32_WHITE,
                    tweak.quarterTurns, tweak.mirror);
    } else {
        ImGui::Dummy(ImVec2(width, box));
    }

    // This preview's own orientation, on top of the grid's Rotate/Mirror setting.
    const float buttonW = (width - ImGui::GetStyle().ItemSpacing.x * 3) / 4;
    if (ImGui::Button("Rotate left", ImVec2(buttonW, 0))) tweak.quarterTurns = (tweak.quarterTurns + 3) & 3;
    ImGui::SameLine();
    if (ImGui::Button("Rotate right", ImVec2(buttonW, 0))) tweak.quarterTurns = (tweak.quarterTurns + 1) & 3;
    ImGui::SameLine();
    if (ImGui::Button(tweak.mirror ? "Unmirror" : "Mirror", ImVec2(buttonW, 0))) tweak.mirror = !tweak.mirror;
    ImGui::SameLine();
    ImGui::BeginDisabled(tweak.quarterTurns == 0 && !tweak.mirror);
    if (ImGui::Button("Reset", ImVec2(buttonW, 0))) tweak = {};
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("These buttons turn only this preview (remembered per texture while the app is open).\n"
                          "The Rotate / Mirror options above apply to every preview.");

    ImGui::SeparatorText(isMod ? "Mod" : "Original texture");
    ImGui::Text("Hash: %u", file->hash);
    DrawLabelEditor(*file, width);
    if (!file->label.empty()) ImGui::Text("File name label: %s", file->label.c_str());
    ImGui::Text("Type: %s", KindName(KindOf(*file)));
    if (info) {
        if (!info->error.empty()) ImGui::TextColored(kErrorColor, "%s", info->error.c_str());
        else ImGui::Text("%u x %u, %u mip(s), %s", info->width, info->height, info->mipLevels, FormatName(info->format));
        if (info->encrypted) ImGui::TextColored(AccentTextColor(), "Encrypted (resorep1 pack)");
    }
    ImGui::Text("File size: %s", FormatBytes(file->size).c_str());
    ImGui::Text("Modified: %s", FormatDate(file->modified).c_str());
    if (isMod) ImGui::Text("Mod / category: %s", file->relDir.empty() ? "(loose)" : WideToUtf8(file->relDir).c_str());
    ImGui::TextColored(kDimColor, "%s", ToUtf8(file->path.filename()).c_str());
    ImGui::Spacing();

    if (isMod) {
        if (ImGui::Button(file->selfDisabled ? "Turn on" : "Turn off", ImVec2(width * 0.5f - 4, 0)))
            Defer([this, copy = *file] { ToggleEnabled(copy); });
        ImGui::SameLine();
        const bool encrypted = info && info->encrypted;
        ImGui::BeginDisabled(encrypted);
        if (ImGui::Button("Open in editor", ImVec2(-1, 0))) OpenWithShell(file->path);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip(encrypted ? "Encrypted (resorep1) textures can't be opened by image editors."
                                        : "Opens the .dds in your default editor. Save it there; the preview and a "
                                          "running game pick up the change.");
        if (ImGui::Button("Show in Explorer", ImVec2(-1, 0))) ShowInExplorer(file->path);
        if (!file->selfDisabled && !file->enabled)
            ImGui::TextColored(kErrorColor, "Off because its mod or category is turned off.");
        int sameHash = 0;
        for (const auto& m : library_.mods()) sameHash += (m.enabled && m.hash == file->hash);
        const fs::path* active = library_.ActiveMod(file->hash);
        if (sameHash > 1 && active) {
            ImGui::TextColored(kErrorColor, "%d active textures share this hash. The game uses:", sameHash);
            ImGui::TextWrapped("%s", ToUtf8(active->lexically_relative(library_.moddedFolder())).c_str());
            ImGui::TextColored(kDimColor, "Turn the others off to choose which one wins.");
        }
    } else {
        if (ImGui::Button("Replace with .dds...", ImVec2(-1, 0)))
            Defer([this, hash = file->hash] { ReplaceDump(hash); });
        if (ImGui::Button("Open in editor", ImVec2(width * 0.5f - 4, 0))) OpenWithShell(file->path);
        ImGui::SameLine();
        if (ImGui::Button("Show in Explorer", ImVec2(-1, 0))) ShowInExplorer(file->path);

        // Side-by-side with the mod that currently replaces it.
        for (const auto& m : library_.mods()) {
            const fs::path* active = library_.ActiveMod(file->hash);
            if (!active || m.path != *active) continue;
            ImGui::SeparatorText("Replaced by");
            const TextureInfo* modInfo = nullptr;
            if (ID3D11ShaderResourceView* modSrv = thumbs_.Get(m.path, m.modified, 256, &modInfo); modSrv && modInfo) {
                const ImVec2 fit = PreviewSize(modInfo->width, modInfo->height, std::min(width, 256.0f));
                const ImVec2 at = ImGui::GetCursorScreenPos();
                ImGui::Dummy(fit);
                DrawPreview(ImGui::GetWindowDrawList(), modSrv, modInfo->width, modInfo->height, at, fit,
                            IM_COL32_WHITE);
            }
            ImGui::TextColored(kDimColor, "%s", ToUtf8(m.path.lexically_relative(library_.moddedFolder())).c_str());
            if (ImGui::Button("Go to mod")) {
                tab_ = Tab::Mods;
                modView_ = ModView::All;
                selection_ = Selection{m.path, true};
            }
            break;
        }
    }
}

void App::DrawLabelEditor(const TextureFile& file, float width) {
    if (labelEditHash_ != file.hash) {
        const std::string* current = labels_.Get(file.hash);
        snprintf(labelEdit_, sizeof(labelEdit_), "%s", current ? current->c_str() : "");
        labelEditHash_ = file.hash;
    }
    if (focusLabel_) {
        ImGui::SetKeyboardFocusHere();
        focusLabel_ = false;
    }
    ImGui::SetNextItemWidth(width - 70);
    ImGui::InputTextWithHint("##label", "Label this hash (e.g. Ash helmet)", labelEdit_, sizeof(labelEdit_));
    if (ImGui::IsItemDeactivatedAfterEdit()) SetLabel(file.hash, labelEdit_);
    ImGui::SetItemTooltip("Your own name for this texture hash. Saved in labels.txt next to ReResorep.exe;\n"
                          "applies to the dump and every mod for this hash. Press Enter to save.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!labels_.Get(file.hash));
    if (ImGui::Button("Clear", ImVec2(-1, 0))) {
        labelEdit_[0] = 0;
        SetLabel(file.hash, "");
    }
    ImGui::EndDisabled();
}

void App::DrawSimilarWindow() {
    if (!similar_) return;
    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(560, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Similar textures###similar", &open, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        if (!open) similar_.reset();
        return;
    }
    SimilarSearch& s = *similar_;
    const TextureFile& src = s.source;

    // Source header
    const TextureInfo* info = nullptr;
    if (ID3D11ShaderResourceView* srv = thumbs_.Get(src.path, src.modified, 96, &info); srv && info) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(96, 96));
        DrawPreview(ImGui::GetWindowDrawList(), srv, info->width, info->height, at, ImVec2(96, 96), IM_COL32_WHITE);
    } else {
        ImGui::Dummy(ImVec2(96, 96));
    }
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Text("%u", src.hash);
    if (const std::string& label = DisplayLabel(src); !label.empty()) ImGui::TextColored(AccentTextColor(), "%s", label.c_str());
    ImGui::TextColored(kDimColor, "%s, %ux%u", KindName(KindOf(src)), src.width, src.height);
    static const char* kModes[] = {"Normal maps", "Color textures", "Any kind"};
    int mode = int(s.mode);
    ImGui::SetNextItemWidth(160);
    if (ImGui::Combo("Look for", &mode, kModes, 3)) {
        s.mode = SimilarMode(mode);
        ComputeSimilar();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) ComputeSimilar();
    ImGui::EndGroup();

    ImGui::TextWrapped("Ranked by matching detail, same size, and (for dumps) being saved at nearly the same moment, "
                       "since games load a material's textures together. Check the top results by eye.");
    if (!s.computed) ImGui::TextColored(AccentTextColor(), "Analysing this texture...");
    else if (s.notAnalysed)
        ImGui::TextColored(kDimColor, "%zu candidate(s) not analysed yet; Refresh later for more results.", s.notAnalysed);
    ImGui::Separator();

    if (s.computed && s.results.empty()) ImGui::TextColored(kDimColor, "No matching textures found.");
    ImGui::BeginChild("results");
    std::optional<SimilarResult> show;
    for (size_t i = 0; i < s.results.size(); ++i) {
        const SimilarResult& r = s.results[i];
        ImGui::PushID(int(i));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(72, 72));
        const TextureInfo* ri = nullptr;
        if (ID3D11ShaderResourceView* srv = thumbs_.Get(r.path, r.modified, 72, &ri); srv && ri)
            DrawPreview(ImGui::GetWindowDrawList(), srv, ri->width, ri->height, at, ImVec2(72, 72), IM_COL32_WHITE);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("%u  %s", r.hash, r.isMod ? "(mod)" : "(dump)");
        const std::string* label = labels_.Get(r.hash);
        if (label) ImGui::TextColored(AccentTextColor(), "%s", label->c_str());
        ImGui::TextColored(kDimColor, "%s, %ux%u", KindName(r.kind), r.width, r.height);
        ImGui::ProgressBar(r.score, ImVec2(120, 0), (std::to_string(int(r.score * 100)) + "% match").c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Show")) show = r;
        const std::string* srcLabel = labels_.Get(src.hash);
        if (srcLabel && !label) {
            const std::string suggested =
                *srcLabel + (r.kind == TextureKind::Normal ? " (normal)" : r.kind == TextureKind::Grayscale ? " (depth)" : "");
            ImGui::SameLine();
            if (ImGui::SmallButton(("Label \"" + suggested + "\"").c_str())) SetLabel(r.hash, suggested);
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();

    if (show) {
        tab_ = show->isMod ? Tab::Mods : Tab::Dumps;
        modView_ = ModView::All;
        selection_ = Selection{show->path, show->isMod};
    }
    if (!open) similar_.reset();
}

void App::DrawGames() {
    // Which games are running, checked about once a second while this tab is open.
    if (std::chrono::steady_clock::now() - lastRunningCheck_ > std::chrono::seconds(1)) {
        lastRunningCheck_ = std::chrono::steady_clock::now();
        gameRunning_.clear();
        for (const GameState& g : games_) gameRunning_[g.exe.wstring()] = !FindProcesses(g.exe).empty();
    }

    if (ImGui::Button("Add game...")) {
        auto exe = PickFiles(hwnd_, L"Choose the game executable", {{L"Programs", L"*.exe"}}, false);
        if (!exe.empty()) {
            if (std::find(settings_.games.begin(), settings_.games.end(), exe[0]) != settings_.games.end()) {
                Status("That game is already in the list", "");
            } else {
                settings_.games.push_back(exe[0]);
                settings_.Save();
                RefreshGames();
                Status("", "Added " + ToUtf8(exe[0].filename()) + ". Click Install to enable texture replacement.");
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) RefreshGames();
    ImGui::SameLine();
    ImGui::TextColored(kDimColor, "Close the game before installing or removing. For Siege pick RainbowSix.exe.");
    if (!fs::exists(BundledProxy()))
        ImGui::TextColored(kErrorColor, "proxy\\d3d11.dll is missing next to ReResorep.exe - reinstall ReResorep.");
    ImGui::Spacing();

    if (games_.empty()) {
        ImGui::TextColored(kDimColor, "No games yet. Add the game's .exe; the proxy d3d11.dll goes next to it.");
        return;
    }
    if (!ImGui::BeginTable("games", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("Game", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 470.0f);
    ImGui::TableHeadersRow();

    std::optional<size_t> remove;
    for (size_t i = 0; i < games_.size(); ++i) {
        const GameState& g = games_[i];
        const bool running = gameRunning_.count(g.exe.wstring()) && gameRunning_[g.exe.wstring()];
        ImGui::PushID(int(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(ToUtf8(g.exe.filename()).c_str());
        ImGui::TextColored(kDimColor, "%s", ToUtf8(g.exe.parent_path()).c_str());

        ImGui::TableNextColumn();
        const ImVec4 color = g.status == ProxyStatus::Installed ? kOkColor
                             : g.status == ProxyStatus::NotInstalled ? kDimColor
                                                                     : ImVec4(1.0f, 0.75f, 0.3f, 1.0f);
        ImGui::TextColored(color, "%s", StatusText(g.status));
        if (running) ImGui::TextColored(kOkColor, "Running");
        if (!g.proxyVersion.empty()) {
            ImGui::TextColored(kDimColor, "proxy %s", g.proxyVersion.c_str());
            if (g.status == ProxyStatus::Outdated)
                ImGui::SetItemTooltip("This ReResorep has proxy %s. Click Update to install it.", RERESOREP_VERSION_STRING);
        }
        if (g.chainsOtherD3D11) {
            ImGui::TextColored(kDimColor, "chains ori_d3d11.dll");
            ImGui::SetItemTooltip("Another d3d11.dll (for example ReShade) was kept as ori_d3d11.dll and still loads.");
        }

        ImGui::TableNextColumn();
        const bool installed = g.status == ProxyStatus::Installed;
        if (g.status != ProxyStatus::Missing) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.26f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.60f, 0.32f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.40f, 0.22f, 1.0f));
            ImGui::BeginDisabled(running);
            if (ImGui::Button("Start")) {
                const std::string e = LaunchProgram(g.exe);
                Status(e, "Started " + ToUtf8(g.exe.filename()) +
                              (installed ? "" : " (ReResorep is not installed for it, so no mods apply)"));
                lastRunningCheck_ = {};  // pick the new process up right away
            }
            ImGui::EndDisabled();
            ImGui::PopStyleColor(3);
            ImGui::SetItemTooltip(running     ? "The game is already running"
                                  : installed ? "Launch the game with your mods"
                                              : "Launch the game (install ReResorep first for mods to apply)");
            ImGui::SameLine();

            const bool closingThis = closeJob_.valid() && closingExe_ == g.exe;
            ImGui::BeginDisabled(!running || closeJob_.valid());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.24f, 0.24f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.14f, 0.14f, 1.0f));
            if (ImGui::Button(closingThis ? "Closing...###close" : "Close###close")) {
                closingExe_ = g.exe;
                // Ask nicely, give it time to shut down, then end it; off the UI thread.
                closeJob_ = std::async(std::launch::async, [exe = g.exe] { return CloseProgram(exe, kCloseGraceMs); });
                Status("", "Closing " + ToUtf8(g.exe.filename()) + "...");
            }
            ImGui::PopStyleColor(3);
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("Asks the game to exit. If it is still running after %u seconds, it is ended "
                                  "forcefully (unsaved progress in the game is lost).",
                                  unsigned(kCloseGraceMs / 1000));
            ImGui::SameLine();
            // These rebuild games_, so they run after the table is drawn.
            if (ImGui::Button(installed ? "Reinstall" : g.status == ProxyStatus::NotInstalled ? "Install" : "Update")) {
                Defer([this, exe = g.exe] {
                    Status(InstallProxy(exe, settings_), "Installed into " + ToUtf8(exe.parent_path()));
                    RefreshGames();
                });
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(g.status == ProxyStatus::NotInstalled);
            if (ImGui::Button("Uninstall")) {
                Defer([this, exe = g.exe] {
                    Status(UninstallProxy(exe), "Removed from " + ToUtf8(exe.filename()));
                    RefreshGames();
                });
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Open folder")) OpenWithShell(g.exe.parent_path());
            ImGui::SameLine();
        }
        if (ImGui::Button("Remove")) remove = i;
        ImGui::SetItemTooltip("Remove from this list (does not uninstall).");
        ImGui::PopID();
    }
    ImGui::EndTable();

    if (remove) {
        settings_.games.erase(settings_.games.begin() + *remove);
        settings_.Save();
        RefreshGames();
    }
}

void App::DrawSettings() {
    bool changed = false;
    auto folderRow = [&](const char* label, fs::path& folder, const wchar_t* title) {
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", ToUtf8(folder).c_str());
        ImGui::SetNextItemWidth(-180);
        if (ImGui::InputText("##path", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
            folder = fs::path(Utf8ToWide(buf));
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            if (auto picked = PickFolder(hwnd_, title)) {
                folder = *picked;
                changed = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Open")) OpenWithShell(folder);
        ImGui::PopID();
    };

    ImGui::SeparatorText("Folders");
    folderRow("Load modded textures from", settings_.moddedFolder, L"Modded textures folder");
    folderRow("Save original (dumped) textures to", settings_.originalFolder, L"Original textures folder");

    ImGui::SeparatorText("Mod creator");
    changed |= ImGui::Checkbox("Mod creator mode", &settings_.modCreatorMode);
    HelpMarker("Needed for dumping. Replacement works with or without it.");
    ImGui::BeginDisabled(!settings_.modCreatorMode);
    changed |= ImGui::Checkbox("Dump textures the game loads", &settings_.saveTextures);
    HelpMarker("Each texture is saved once as <hash>.dds into the original folder. "
               "Writing happens on a background thread so the game does not stall.");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Appearance");
    {
        // Applied live while dragging; saved once the colour is chosen.
        uint32_t& rgb = settings_.accentColor;
        float col[3] = {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f};
        auto set = [&](uint32_t value, bool save) {
            rgb = value;
            ApplyAccentColor(rgb);
            if (save) settings_.Save();
        };
        ImGui::SetNextItemWidth(260);
        if (ImGui::ColorEdit3("Accent colour", col, ImGuiColorEditFlags_DisplayHex))
            set((uint32_t(col[0] * 255 + 0.5f) << 16) | (uint32_t(col[1] * 255 + 0.5f) << 8) | uint32_t(col[2] * 255 + 0.5f),
                false);
        if (ImGui::IsItemDeactivatedAfterEdit()) settings_.Save();

        struct Preset {
            const char* name;
            uint32_t rgb;
        };
        static constexpr Preset kPresets[] = {{"Blue", 0x4296FA},   {"Orange", 0xFF8C38}, {"Teal", 0x2EC4B6},
                                              {"Green", 0x4CB04F},  {"Purple", 0x9B6BFF}, {"Pink", 0xFF5FA2},
                                              {"Red", 0xE5484D},    {"Grey", 0x8A8F98}};
        for (const Preset& pr : kPresets) {
            ImGui::PushID(pr.name);
            const ImVec4 c(((pr.rgb >> 16) & 0xFF) / 255.0f, ((pr.rgb >> 8) & 0xFF) / 255.0f, (pr.rgb & 0xFF) / 255.0f, 1.0f);
            if (ImGui::ColorButton(pr.name, c, ImGuiColorEditFlags_NoTooltip, ImVec2(26, 26))) set(pr.rgb, true);
            ImGui::SetItemTooltip("%s", pr.name);
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(rgb == kDefaultAccent);
        if (ImGui::Button("Reset")) set(kDefaultAccent, true);
        ImGui::EndDisabled();
    }

    ImGui::SeparatorText("Labels");
    ImGui::Text("%zu label(s) in %s", labels_.size(), ToUtf8(LabelsFile()).c_str());
    if (ImGui::Button("Import labels...")) ImportLabels();
    ImGui::SetItemTooltip("Merges a labels file (lines like 3123456789=Ash helmet; CSV also works).\n"
                          "Imported labels replace existing labels for the same hash.");
    ImGui::SameLine();
    if (ImGui::Button("Export labels...")) ExportLabels();
    ImGui::SameLine();
    if (ImGui::Button("Open labels file")) {
        if (!std::filesystem::exists(LabelsFile())) labels_.Save(LabelsFile());
        OpenWithShell(LabelsFile());
    }

    ImGui::SeparatorText("Logging");
    changed |= ImGui::Checkbox("Write dll.log in the game folder", &settings_.logEnabled);
    ImGui::BeginDisabled(!settings_.logEnabled);
    changed |= ImGui::Checkbox("Verbose (every texture load)", &settings_.logDebug);
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::TextColored(kDimColor, "Changes are saved immediately and reach a running game within a second.");
    if (changed) ApplySettings();
}

void App::DrawAbout() {
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x * 0.95f);
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted("ReResorep " RERESOREP_VERSION_STRING);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextColored(kDimColor, "Texture replacer and texture manager for Direct3D 11 games.");
    ImGui::Text("Made by %s.", RERESOREP_AUTHOR);
    ImGui::Spacing();

    ImGui::SeparatorText("Credits");
    ImGui::TextColored(AccentTextColor(), "Resorep by RayWing (2017)");
    ImGui::TextWrapped("The original DirectX 11 texture replacer that ReResorep rewrites. To keep existing "
                       "mods working, the texture hash, the encrypted resorep1 pack format and the mod file "
                       "naming follow Resorep exactly; they were worked out from its proxy DLL. "
                       "No Resorep code is included.");
    if (ImGui::SmallButton("Original Resorep release thread"))
        OpenWithShell(L"https://www.undertow.club/threads/tool-resorep-directx-11-texture-replacer.8402/");
    ImGui::Spacing();

    ImGui::TextColored(AccentTextColor(), "Dear ImGui by Omar Cornut and contributors");
    ImGui::TextWrapped("The user interface library. MIT License.");
    if (ImGui::SmallButton("Dear ImGui on GitHub")) OpenWithShell(L"https://github.com/ocornut/imgui");
    ImGui::SameLine();
    static bool showLicense = false;
    if (ImGui::SmallButton(showLicense ? "Hide licence" : "Show licence")) showLicense = !showLicense;
    if (showLicense) {
        ImGui::BeginChild("license", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 12), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(kImGuiLicense);
        ImGui::EndChild();
    }
    ImGui::Spacing();

    ImGui::TextColored(AccentTextColor(), "Blowfish by Bruce Schneier");
    ImGui::TextWrapped("The public-domain cipher used by Resorep's encrypted packs. Its tables are computed "
                       "from the digits of pi (tools/gen_blowfish_tables.py).");

    ImGui::SeparatorText("Please note");
    ImGui::TextWrapped("ReResorep is a fan-made modding tool and is not affiliated with or endorsed by Ubisoft "
                       "or any game developer. Games with anti-cheat (for example BattlEye in online Rainbow Six "
                       "Siege) can detect a replacement d3d11.dll and ban accounts: use it for offline and modded "
                       "setups only.");
    ImGui::PopTextWrapPos();
}

void App::DrawImportPopup() {
    if (openImportPopup_) {
        ImGui::OpenPopup("Import textures");
        openImportPopup_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Import textures", nullptr, ImGuiWindowFlags_NoSavedSettings)) return;

    ImGui::TextWrapped("These files are not named after a texture hash. Enter the hash of the game texture "
                       "each one replaces (you can see hashes in the Dumps tab).");
    ImGui::Spacing();
    bool allValid = true;
    for (size_t i = 0; i < pendingImports_.size(); ++i) {
        PendingImport& p = pendingImports_[i];
        ImGui::PushID(int(i));
        ImGui::SeparatorText(ToUtf8(p.source.filename()).c_str());
        ImGui::SetNextItemWidth(140);
        ImGui::InputTextWithHint("Hash", "e.g. 3123456789", p.hash, sizeof(p.hash), ImGuiInputTextFlags_CharsDecimal);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("Name (optional)", p.label, sizeof(p.label));
        if (!ParseHash(p.hash)) {
            allValid = false;
            ImGui::TextColored(kErrorColor, "Enter a number between 0 and 4294967295");
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(!allValid);
    if (ImGui::Button("Import", ImVec2(120, 0))) {
        std::string error;
        for (const PendingImport& p : pendingImports_) {
            const uint32_t hash = *ParseHash(p.hash);
            const std::string e = library_.Import(p.source, hash, p.label, true, p.relDir);
            thumbs_.Invalidate(library_.TargetPath(hash, p.label, p.relDir));
            if (!e.empty()) error = e;
        }
        Status(error, "Imported " + std::to_string(pendingImports_.size()) + " texture(s)");
        pendingImports_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        pendingImports_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::DrawStatusBar() {
    ImGui::Separator();
    const auto age = std::chrono::steady_clock::now() - statusTime_;
    if (!statusText_.empty() && (statusIsError_ || age < std::chrono::seconds(8))) {
        ImGui::TextColored(statusIsError_ ? kErrorColor : kOkColor, "%s", statusText_.c_str());
    } else {
        ImGui::TextColored(kDimColor, "%zu mods (%zu enabled) | %zu dumps | modded folder: %s", library_.mods().size(),
                           size_t(std::count_if(library_.mods().begin(), library_.mods().end(),
                                                [](const TextureFile& f) { return f.enabled; })),
                           library_.dumps().size(), ToUtf8(library_.moddedFolder()).c_str());
    }
    if (const size_t pending = analyzer_->Pending()) {
        ImGui::SameLine();
        ImGui::TextColored(AccentTextColor(), "| detecting texture types: %zu left", pending);
    }
}

}  // namespace resorep::ui
