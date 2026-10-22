#include "app.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>

#include "dds.h"
#include "mod_state.h"
#include "texture_index.h"
#include "win_util.h"

namespace resorep::ui {
namespace fs = std::filesystem;

namespace {
const ImVec4 kErrorColor(1.0f, 0.45f, 0.4f, 1.0f);
const ImVec4 kOkColor(0.5f, 0.9f, 0.5f, 1.0f);
const ImVec4 kDimColor(0.6f, 0.6f, 0.6f, 1.0f);
const ImVec4 kAccentColor(0.35f, 0.65f, 1.0f, 1.0f);
constexpr float kDetailsWidth = 340.0f;
constexpr float kSidebarWidth = 260.0f;

ImTextureID ToTexId(ID3D11ShaderResourceView* srv) { return (ImTextureID)(uintptr_t)srv; }

// The game's textures come out rotated 180 degrees on screen, so every preview is drawn
// with swapped UVs (mirrored on both X and Y). Display only; files are untouched.
const ImVec2 kPreviewUv0(1, 1);
const ImVec2 kPreviewUv1(0, 0);

std::optional<uint32_t> ParseHash(const char* text) {
    if (!*text) return std::nullopt;
    char* end = nullptr;
    const unsigned long long v = strtoull(text, &end, 10);
    if (*end || v > 0xFFFFFFFFull) return std::nullopt;
    return uint32_t(v);
}

bool MatchesFilter(const TextureFile& f, const char* filter) {
    if (!*filter) return true;
    const std::string hash = std::to_string(f.hash);
    if (hash.find(filter) != std::string::npos) return true;
    std::string label = f.label, needle = filter;
    for (char& c : label) c = char(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : needle) c = char(std::tolower(static_cast<unsigned char>(c)));
    return label.find(needle) != std::string::npos;
}

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
    library_.SetFolders(settings_.moddedFolder, settings_.originalFolder);
    RefreshGames();
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

void App::OnDropFiles(const std::vector<fs::path>& files) { ImportFiles(files, ImportDir()); }

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
    if (library_.RefreshIfChanged() && selection_ && !FindSelected()) selection_.reset();

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
            ImGui::BeginChild("details", ImVec2(0, 0), ImGuiChildFlags_Borders);
            DrawDetails();
            ImGui::EndChild();
            break;
        }
        case Tab::Games: DrawGames(); break;
        case Tab::Settings: DrawSettings(); break;
    }
    ImGui::EndChild();
    DrawStatusBar();
    DrawImportPopup();
    DrawFolderPopups();
    ImGui::End();

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
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("##size", &thumbSize_, 64.0f, 256.0f, "Size %.0f");
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
    if (ImGui::Button("+ New mod", ImVec2(-1, 0))) {
        newFolderParent_.clear();
        newFolderName_[0] = 0;
        openNewFolderPopup_ = true;
    }
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

void App::DrawFolderPopups() {
    if (openNewFolderPopup_) {
        ImGui::OpenPopup("New folder");
        openNewFolderPopup_ = false;
    }
    if (ImGui::BeginPopupModal("New folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (newFolderParent_.empty()) ImGui::TextUnformatted("Name of the new mod:");
        else ImGui::Text("New category inside %s:", WideToUtf8(newFolderParent_).c_str());
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(300);
        const bool enter =
            ImGui::InputText("##name", newFolderName_, sizeof(newFolderName_), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Create", ImVec2(120, 0)) || enter) {
            const std::wstring parent = newFolderParent_;
            const std::string name = newFolderName_;
            Defer([this, parent, name] {
                const std::string e = library_.CreateFolder(parent, name);
                Status(e, "Created " + name);
                if (e.empty()) {
                    modView_ = ModView::Folder;
                    viewFolder_ = parent.empty() ? Utf8ToWide(name) : parent + L"/" + Utf8ToWide(name);
                }
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
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
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("##size", &thumbSize_, 64.0f, 256.0f, "Size %.0f");
    if (!settings_.modCreatorMode || !settings_.saveTextures) {
        ImGui::SameLine();
        ImGui::TextColored(kDimColor, "Dumping is off - enable it in Settings.");
    }
}

void App::DrawGrid(const std::vector<TextureFile>& files, bool isMod) {
    std::vector<const TextureFile*> visible;
    visible.reserve(files.size());
    for (const auto& f : files) {
        if (isMod && !showDisabled_ && !f.enabled) continue;
        if (isMod && !InView(f)) continue;
        if (!isMod && hideModdedDumps_ && library_.IsModded(f.hash)) continue;
        if (MatchesFilter(f, filter_)) visible.push_back(&f);
    }
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
    ID3D11ShaderResourceView* srv = thumbs_.Get(file.path, uint32_t(size), &info);
    if (srv && info) {
        const ImVec2 fit = Fit(info->width, info->height, size - 8);
        const ImVec2 a(pos.x + (size - fit.x) * 0.5f, pos.y + (size - fit.y) * 0.5f);
        dl->AddImage(ToTexId(srv), a, ImVec2(a.x + fit.x, a.y + fit.y), kPreviewUv0, kPreviewUv1,
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

    const ImU32 border = selected ? IM_COL32(90, 160, 255, 255) : hovered ? IM_COL32(120, 120, 130, 255) : 0;
    if (border) dl->AddRect(pos, ImVec2(pos.x + size, pos.y + size), border, 4.0f, 0, selected ? 2.5f : 1.0f);

    char line[64];
    snprintf(line, sizeof(line), "%u", file.hash);
    dl->PushClipRect(ImVec2(pos.x, pos.y + size), ImVec2(pos.x + size, pos.y + cardSize.y), true);
    dl->AddText(ImVec2(pos.x + 2, pos.y + size + 2), IM_COL32(230, 230, 230, 255), line);
    if (!file.label.empty())
        dl->AddText(ImVec2(pos.x + 2, pos.y + size + 2 + textH), IM_COL32(150, 150, 160, 255), file.label.c_str());
    else if (info && !info->error.empty())
        dl->AddText(ImVec2(pos.x + 2, pos.y + size + 2 + textH), IM_COL32(255, 120, 110, 255), info->error.c_str());
    else if (info) {
        snprintf(line, sizeof(line), "%ux%u %s", info->width, info->height, FormatName(info->format));
        dl->AddText(ImVec2(pos.x + 2, pos.y + size + 2 + textH), IM_COL32(150, 150, 160, 255), line);
    }
    dl->PopClipRect();

    if (ImGui::BeginPopupContextItem("ctx")) {
        selection_ = Selection{file.path, isMod};
        if (isMod) {
            if (ImGui::MenuItem(file.selfDisabled ? "Turn on" : "Turn off")) Defer([this, file] { ToggleEnabled(file); });
            DrawMoveMenu(file);
        } else {
            if (ImGui::MenuItem("Replace with .dds...")) Defer([this, hash = file.hash] { ReplaceDump(hash); });
            if (ImGui::MenuItem("Open in default editor")) OpenWithShell(file.path);
        }
        if (ImGui::MenuItem("Show in Explorer")) ShowInExplorer(file.path);
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

    const TextureInfo* info = nullptr;
    ID3D11ShaderResourceView* srv = thumbs_.Get(file->path, 0, &info);
    if (srv && info) {
        const ImVec2 fit = Fit(info->width, info->height, width);
        ImGui::Image(ToTexId(srv), fit, kPreviewUv0, kPreviewUv1);
    } else {
        ImGui::Dummy(ImVec2(width, width * 0.5f));
    }

    ImGui::SeparatorText(isMod ? "Mod" : "Original texture");
    ImGui::Text("Hash: %u", file->hash);
    if (!file->label.empty()) ImGui::Text("Name: %s", file->label.c_str());
    if (info) {
        if (!info->error.empty()) ImGui::TextColored(kErrorColor, "%s", info->error.c_str());
        else ImGui::Text("%u x %u, %u mip(s), %s", info->width, info->height, info->mipLevels, FormatName(info->format));
        if (info->encrypted) ImGui::TextColored(kAccentColor, "Encrypted (resorep1 pack)");
    }
    ImGui::Text("File size: %s", FormatBytes(file->size).c_str());
    if (isMod) ImGui::Text("Mod / category: %s", file->relDir.empty() ? "(loose)" : WideToUtf8(file->relDir).c_str());
    ImGui::TextColored(kDimColor, "%s", ToUtf8(file->path.filename()).c_str());
    ImGui::Spacing();

    if (isMod) {
        if (ImGui::Button(file->selfDisabled ? "Turn on" : "Turn off", ImVec2(width * 0.5f - 4, 0)))
            Defer([this, copy = *file] { ToggleEnabled(copy); });
        ImGui::SameLine();
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
            if (ID3D11ShaderResourceView* modSrv = thumbs_.Get(m.path, 256, &modInfo); modSrv && modInfo)
                ImGui::Image(ToTexId(modSrv), Fit(modInfo->width, modInfo->height, std::min(width, 256.0f)), kPreviewUv0,
                             kPreviewUv1);
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

void App::DrawGames() {
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
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 400.0f);
    ImGui::TableHeadersRow();

    std::optional<size_t> remove;
    for (size_t i = 0; i < games_.size(); ++i) {
        const GameState& g = games_[i];
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
            if (ImGui::Button("Start")) {
                const std::string e = LaunchProgram(g.exe);
                Status(e, "Started " + ToUtf8(g.exe.filename()) +
                              (installed ? "" : " (ReResorep is not installed for it, so no mods apply)"));
            }
            ImGui::PopStyleColor(3);
            ImGui::SetItemTooltip(installed ? "Launch the game with your mods"
                                            : "Launch the game (install ReResorep first for mods to apply)");
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

    ImGui::SeparatorText("Logging");
    changed |= ImGui::Checkbox("Write dll.log in the game folder", &settings_.logEnabled);
    ImGui::BeginDisabled(!settings_.logEnabled);
    changed |= ImGui::Checkbox("Verbose (every texture load)", &settings_.logDebug);
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::TextColored(kDimColor, "Changes are saved immediately and reach a running game within a second.");
    if (changed) ApplySettings();
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
}

}  // namespace resorep::ui
