#include "win_util.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

#include "settings.h"

namespace resorep::ui {
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

std::vector<fs::path> PickFiles(HWND owner, const wchar_t* title, const std::vector<FileFilter>& filters,
                                bool multiple) {
    std::vector<fs::path> out;
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;

    std::vector<COMDLG_FILTERSPEC> specs;
    for (const auto& f : filters) specs.push_back({f.name, f.spec});
    if (!specs.empty()) dlg->SetFileTypes(UINT(specs.size()), specs.data());
    dlg->SetTitle(title);
    FILEOPENDIALOGOPTIONS opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | (multiple ? FOS_ALLOWMULTISELECT : 0));
    if (FAILED(dlg->Show(owner))) return out;

    ComPtr<IShellItemArray> items;
    if (FAILED(dlg->GetResults(&items))) return out;
    DWORD count = 0;
    items->GetCount(&count);
    for (DWORD i = 0; i < count; ++i) {
        ComPtr<IShellItem> item;
        PWSTR path = nullptr;
        if (SUCCEEDED(items->GetItemAt(i, &item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            out.emplace_back(path);
            CoTaskMemFree(path);
        }
    }
    return out;
}

std::optional<fs::path> PickFolder(HWND owner, const wchar_t* title) {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
        return std::nullopt;
    dlg->SetTitle(title);
    FILEOPENDIALOGOPTIONS opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (FAILED(dlg->Show(owner)) || FAILED(dlg->GetResult(&item)) ||
        FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        return std::nullopt;
    fs::path result(path);
    CoTaskMemFree(path);
    return result;
}

bool MoveToRecycleBin(const fs::path& path) {
    std::wstring from = path.wstring();
    from.push_back(L'\0');  // SHFileOperation wants a double-null-terminated list
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

void ShowInExplorer(const fs::path& path) {
    if (PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(path.c_str())) {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
    }
}

void OpenWithShell(const fs::path& path) {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring FileCompanyName(const fs::path& path) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size) return {};
    std::vector<uint8_t> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};

    struct LangCodePage { WORD lang, codePage; } *translations = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &len) ||
        len < sizeof(LangCodePage))
        return {};
    wchar_t key[64];
    swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\CompanyName", translations[0].lang, translations[0].codePage);
    wchar_t* value = nullptr;
    if (!VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&value), &len) || !value) return {};
    return value;
}

bool FileContains(const fs::path& path, const std::string& needle) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return data.find(needle) != std::string::npos;
}

bool FilesEqual(const fs::path& a, const fs::path& b) {
    std::error_code ea, eb;
    const auto sizeA = fs::file_size(a, ea);
    const auto sizeB = fs::file_size(b, eb);
    if (ea || eb || sizeA != sizeB) return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    return std::equal(std::istreambuf_iterator<char>(fa), std::istreambuf_iterator<char>(),
                      std::istreambuf_iterator<char>(fb));
}

fs::path ExeDirectory() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    return fs::path(std::wstring(buf, n)).parent_path();
}

std::string FormatBytes(uint64_t bytes) {
    char buf[32];
    if (bytes >= (1ull << 20)) snprintf(buf, sizeof(buf), "%.1f MB", double(bytes) / (1 << 20));
    else if (bytes >= 1024) snprintf(buf, sizeof(buf), "%.0f KB", double(bytes) / 1024);
    else snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    return buf;
}

std::string ToUtf8(const fs::path& p) { return WideToUtf8(p.wstring()); }

}  // namespace resorep::ui
