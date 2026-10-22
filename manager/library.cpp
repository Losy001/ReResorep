#include "library.h"

#include <algorithm>
#include <system_error>

#include "settings.h"
#include "texture_index.h"
#include "win_util.h"

namespace resorep::ui {
namespace fs = std::filesystem;

namespace {
void ScanFolder(const fs::path& folder, bool enabled, std::vector<TextureFile>& out) {
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::wstring name = it->path().filename().wstring();
        const auto hash = ParseModFileName(name);
        if (!hash) continue;
        TextureFile f;
        f.path = it->path();
        f.hash = *hash;
        f.enabled = enabled;
        f.size = it->file_size(ec);
        const size_t tilde = name.find(L'~');
        if (tilde != std::wstring::npos) f.label = WideToUtf8(name.substr(tilde + 1, name.rfind(L'.') - tilde - 1));
        out.push_back(std::move(f));
    }
}

std::string ErrorText(const std::error_code& ec) { return ec ? ec.message() : std::string(); }

// Labels end up in file names; keep them to characters Windows allows.
std::wstring SanitizeLabel(const std::string& label) {
    std::wstring w = Utf8ToWide(label);
    std::erase_if(w, [](wchar_t c) { return c < 32 || wcschr(L"<>:\"/\\|?*", c); });
    return w;
}
}  // namespace

Library::~Library() { CloseWatches(); }

void Library::SetFolders(const fs::path& modded, const fs::path& original) {
    modded_ = modded;
    original_ = original;
    std::error_code ec;
    fs::create_directories(modded_, ec);
    fs::create_directories(disabledFolder(), ec);
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
    const fs::path folders[3] = {modded_, disabledFolder(), original_};
    for (int i = 0; i < 3; ++i)
        watches_[i] = FindFirstChangeNotificationW(folders[i].c_str(), FALSE,
                                                   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE |
                                                       FILE_NOTIFY_CHANGE_LAST_WRITE);
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
    ScanFolder(modded_, true, mods_);
    ScanFolder(disabledFolder(), false, mods_);
    ScanFolder(original_, true, dumps_);
    auto byHash = [](const TextureFile& a, const TextureFile& b) {
        return a.hash != b.hash ? a.hash < b.hash : a.path < b.path;
    };
    std::sort(mods_.begin(), mods_.end(), byHash);
    std::sort(dumps_.begin(), dumps_.end(), byHash);
    enabledHashes_.clear();
    for (const auto& m : mods_)
        if (m.enabled) enabledHashes_.insert(m.hash);
}

std::string Library::SetEnabled(const TextureFile& mod, bool enabled) {
    const fs::path target = (enabled ? modded_ : disabledFolder()) / mod.path.filename();
    if (fs::exists(target)) return "A file with that name already exists in the target folder.";
    std::error_code ec;
    fs::rename(mod.path, target, ec);
    Refresh();
    return ErrorText(ec);
}

std::string Library::Delete(const TextureFile& file) {
    const bool ok = MoveToRecycleBin(file.path);
    Refresh();
    return ok ? std::string() : "Could not move the file to the Recycle Bin.";
}

fs::path Library::TargetPath(uint32_t hash, const std::string& label) const {
    std::wstring name = std::to_wstring(hash);
    const std::wstring clean = SanitizeLabel(label);
    if (!clean.empty()) name += L"~" + clean;
    return modded_ / (name + L".dds");
}

std::string Library::Import(const fs::path& source, uint32_t hash, const std::string& label, bool overwrite) {
    const fs::path target = TargetPath(hash, label);
    std::error_code ec;
    if (fs::equivalent(source, target, ec)) return {};
    fs::copy_file(source, target, overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none, ec);
    Refresh();
    return ErrorText(ec);
}

}  // namespace resorep::ui
