#include "library.h"

#include <algorithm>
#include <system_error>
#include <unordered_map>

#include "dds.h"
#include "mod_package.h"
#include "settings.h"
#include "texture_index.h"
#include "win_util.h"

namespace resorep::ui {
namespace fs = std::filesystem;

namespace {
std::string ErrorText(const std::error_code& ec) { return ec ? ec.message() : std::string(); }

// Folder and label names end up on disk; keep to characters Windows allows.
std::wstring SanitizeName(const std::string& name) {
    std::wstring w = Utf8ToWide(name);
    std::erase_if(w, [](wchar_t c) { return c < 32 || wcschr(L"<>:\"/\\|?*", c); });
    while (!w.empty() && (w.back() == L' ' || w.back() == L'.')) w.pop_back();
    return w;
}

// Sorts "a", "a/b", "a b" so children directly follow their parent.
std::wstring TreeKey(const std::wstring& rel) {
    std::wstring k = DisabledSet::Key(rel);
    std::replace(k.begin(), k.end(), L'/', L'\x01');
    return k;
}
}  // namespace

Library::~Library() { CloseWatches(); }

TextureFile Library::MakeFile(const fs::directory_entry& e, uint32_t hash) {
    TextureFile f;
    f.path = e.path();
    f.hash = hash;
    std::error_code ec;
    f.size = e.file_size(ec);
    f.modified = e.last_write_time(ec).time_since_epoch().count();
    const std::wstring name = e.path().filename().wstring();
    const size_t tilde = name.find(L'~');
    if (tilde != std::wstring::npos) f.label = WideToUtf8(name.substr(tilde + 1, name.rfind(L'.') - tilde - 1));

    auto [it, inserted] = infoCache_.try_emplace(f.path.wstring());
    CachedInfo& c = it->second;
    if (inserted || c.modified != f.modified || c.size != f.size) {
        DdsImage header;
        const bool ok = ReadTextureInfo(f.path.wstring(), header);
        c = {f.modified, f.size, ok ? header.width : 0, ok ? header.height : 0, ok ? header.format : DXGI_FORMAT_UNKNOWN};
    }
    f.width = c.width;
    f.height = c.height;
    f.format = c.format;
    return f;
}

void Library::SetFolders(const fs::path& modded, const fs::path& original) {
    modded_ = modded;
    original_ = original;
    std::error_code ec;
    fs::create_directories(modded_, ec);
    fs::create_directories(original_, ec);
    Watch();
    Refresh();
}

void Library::CloseWatches() {
    for (HANDLE& h : watches_) {
        if (h != INVALID_HANDLE_VALUE) FindCloseChangeNotification(h);
        h = INVALID_HANDLE_VALUE;
    }
}

void Library::Watch() {
    CloseWatches();
    constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
                              FILE_NOTIFY_CHANGE_LAST_WRITE;
    watches_[0] = FindFirstChangeNotificationW(modded_.c_str(), TRUE, kFilter);
    watches_[1] = FindFirstChangeNotificationW(original_.c_str(), FALSE, kFilter);
}

bool Library::RefreshIfChanged() {
    bool changed = false;
    for (HANDLE h : watches_) {
        if (h != INVALID_HANDLE_VALUE && WaitForSingleObject(h, 0) == WAIT_OBJECT_0) {
            FindNextChangeNotification(h);
            changed = true;
        }
    }
    if (changed) Refresh();
    return changed;
}

void Library::Refresh() {
    mods_.clear();
    dumps_.clear();
    folders_.clear();
    active_.clear();
    disabled_.Load(modded_);

    std::error_code ec;
    for (fs::recursive_directory_iterator it(modded_, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path rel = it->path().lexically_relative(modded_);
        if (it->is_directory(ec)) {
            ModFolder f;
            f.rel = DisabledSet::Slashed(rel);
            f.name = WideToUtf8(it->path().filename().wstring());
            f.depth = int(std::count(f.rel.begin(), f.rel.end(), L'/')) + 1;
            f.selfDisabled = disabled_.IsDisabled(rel);
            f.enabled = !disabled_.IsEffectivelyDisabled(rel);
            folders_.push_back(std::move(f));
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        const auto hash = ParseModFileName(it->path().filename().wstring());
        if (!hash) continue;
        TextureFile f = MakeFile(*it, *hash);
        f.relDir = DisabledSet::Slashed(rel.parent_path());
        f.selfDisabled = disabled_.IsDisabled(rel);
        f.enabled = !disabled_.IsEffectivelyDisabled(rel);
        mods_.push_back(std::move(f));
    }
    for (fs::directory_iterator it(original_, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (const auto hash = ParseModFileName(it->path().filename().wstring())) dumps_.push_back(MakeFile(*it, *hash));
    }

    std::sort(folders_.begin(), folders_.end(),
              [](const ModFolder& a, const ModFolder& b) { return TreeKey(a.rel) < TreeKey(b.rel); });
    auto byHash = [](const TextureFile& a, const TextureFile& b) {
        return a.hash != b.hash ? a.hash < b.hash : a.path < b.path;
    };
    std::sort(mods_.begin(), mods_.end(), byHash);
    std::sort(dumps_.begin(), dumps_.end(), byHash);

    // Per-folder counts, and which file wins per hash (same rule as the proxy's index).
    std::unordered_map<std::wstring, size_t> folderIndex;
    for (size_t i = 0; i < folders_.size(); ++i) folderIndex[DisabledSet::Key(folders_[i].rel)] = i;
    std::unordered_map<uint32_t, std::wstring> activeKey;
    for (const TextureFile& m : mods_) {
        for (std::wstring dir = DisabledSet::Key(m.relDir); !dir.empty();) {
            if (auto it = folderIndex.find(dir); it != folderIndex.end()) {
                folders_[it->second].total++;
                folders_[it->second].active += m.enabled;
            }
            const size_t slash = dir.rfind(L'/');
            dir.resize(slash == std::wstring::npos ? 0 : slash);
        }
        if (!m.enabled) continue;
        const std::wstring key = DisabledSet::Key(m.path.lexically_relative(modded_));
        auto [pos, inserted] = activeKey.try_emplace(m.hash, key);
        if (inserted || key < pos->second) {
            pos->second = key;
            active_[m.hash] = m.path;
        }
    }
}

const fs::path* Library::ActiveMod(uint32_t hash) const {
    auto it = active_.find(hash);
    return it == active_.end() ? nullptr : &it->second;
}

std::string Library::SaveState() {
    const bool ok = disabled_.Save(modded_);
    Refresh();
    return ok ? std::string() : "Could not write " + WideToUtf8(DisabledSet::kFileName);
}

std::string Library::SetEnabled(const TextureFile& mod, bool enabled) {
    disabled_.Set(mod.path.lexically_relative(modded_), !enabled);
    return SaveState();
}

std::string Library::SetFolderEnabled(const std::wstring& rel, bool enabled) {
    disabled_.Set(rel, !enabled);
    return SaveState();
}

std::string Library::CreateFolder(const std::wstring& parentRel, const std::string& name) {
    const std::wstring clean = SafeFolderName(name);  // same rules as mod import
    if (clean.empty()) return "Enter a name.";
    const fs::path dir = modded_ / parentRel / clean;
    if (fs::exists(dir)) return "A folder with that name already exists.";
    std::error_code ec;
    fs::create_directories(dir, ec);
    Refresh();
    return ErrorText(ec);
}

std::string Library::RenameFolder(const std::wstring& rel, const std::string& name) {
    if (rel.empty()) return "Pick a mod or category.";
    const std::wstring clean = SafeFolderName(name);
    if (clean.empty()) return "Enter a name.";
    const fs::path from = modded_ / rel;
    const fs::path to = from.parent_path() / clean;
    if (from.filename() == to.filename()) return {};
    // A change of case only is the same folder on Windows, so it "exists" already.
    if (DisabledSet::Key(from.filename()) != DisabledSet::Key(to.filename()) && fs::exists(to))
        return "A folder with that name already exists.";
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec) return ErrorText(ec);
    disabled_.Rename(from.lexically_relative(modded_), to.lexically_relative(modded_));
    return SaveState();
}

std::string Library::MoveTo(const TextureFile& mod, const std::wstring& relDir) {
    const fs::path target = modded_ / relDir / mod.path.filename();
    std::error_code ec;
    if (fs::equivalent(target.parent_path(), mod.path.parent_path(), ec)) return {};
    if (fs::exists(target)) return "That category already has a file with this name.";
    fs::rename(mod.path, target, ec);
    if (ec) return ErrorText(ec);
    disabled_.Rename(mod.path.lexically_relative(modded_), target.lexically_relative(modded_));
    return SaveState();
}

std::string Library::CopyTo(const TextureFile& mod, const std::wstring& relDir) {
    const fs::path target = modded_ / relDir / mod.path.filename();
    if (fs::exists(target)) return "That category already has a file with this name.";
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    fs::copy_file(mod.path, target, ec);
    Refresh();
    return ErrorText(ec);
}

std::string Library::Delete(const TextureFile& file) {
    const bool ok = MoveToRecycleBin(file.path);
    if (ok && file.path.parent_path() != original_) disabled_.Set(file.path.lexically_relative(modded_), false);
    const std::string e = SaveState();
    return ok ? e : "Could not move the file to the Recycle Bin.";
}

std::string Library::DeleteFolder(const std::wstring& rel) {
    if (rel.empty()) return "Pick a mod or category.";
    const bool ok = MoveToRecycleBin(modded_ / rel);
    if (ok) disabled_.RemoveUnder(rel);
    const std::string e = SaveState();
    return ok ? e : "Could not move the folder to the Recycle Bin.";
}

fs::path Library::TargetPath(uint32_t hash, const std::string& label, const std::wstring& relDir) const {
    std::wstring name = std::to_wstring(hash);
    const std::wstring clean = SanitizeName(label);
    if (!clean.empty()) name += L"~" + clean;
    return modded_ / relDir / (name + L".dds");
}

std::string Library::Import(const fs::path& source, uint32_t hash, const std::string& label, bool overwrite,
                            const std::wstring& relDir) {
    const fs::path target = TargetPath(hash, label, relDir);
    std::error_code ec;
    if (fs::equivalent(source, target, ec)) return {};
    fs::create_directories(target.parent_path(), ec);
    fs::copy_file(source, target, overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none, ec);
    Refresh();
    return ErrorText(ec);
}

}  // namespace resorep::ui
