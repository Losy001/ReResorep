#pragma once
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace resorep::ui {

struct FileFilter {
    const wchar_t* name;
    const wchar_t* spec;
};

std::vector<std::filesystem::path> PickFiles(HWND owner, const wchar_t* title, const std::vector<FileFilter>& filters,
                                             bool multiple);
std::optional<std::filesystem::path> PickFolder(HWND owner, const wchar_t* title);

bool MoveToRecycleBin(const std::filesystem::path& path);
void ShowInExplorer(const std::filesystem::path& path);
void OpenWithShell(const std::filesystem::path& path);
// Starts a program with its own folder as working directory. Returns an error message or "".
std::string LaunchProgram(const std::filesystem::path& exe);

// CompanyName from the file's version resource ("" if none).
std::wstring FileCompanyName(const std::filesystem::path& path);
bool FileContains(const std::filesystem::path& path, const std::string& needle);
bool FilesEqual(const std::filesystem::path& a, const std::filesystem::path& b);

std::filesystem::path ExeDirectory();
std::string FormatBytes(uint64_t bytes);
std::string ToUtf8(const std::filesystem::path& p);

}  // namespace resorep::ui
