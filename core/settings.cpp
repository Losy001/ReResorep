#include "settings.h"

#include <windows.h>

#include <fstream>
#include <sstream>

namespace resorep {
namespace {
std::string Trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}
}  // namespace

bool IniFile::Load(const std::filesystem::path& path) {
    entries_.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first && line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        first = false;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        if (key.empty() || key[0] == '#' || key[0] == ';') continue;
        entries_.emplace_back(std::move(key), Trim(line.substr(eq + 1)));
    }
    return true;
}

bool IniFile::Save(const std::filesystem::path& path) const {
    std::ostringstream ss;
    for (const auto& [k, v] : entries_) ss << k << '=' << v << "\r\n";
    const std::filesystem::path tmp = path.wstring() + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        const std::string data = ss.str();
        out.write(data.data(), std::streamsize(data.size()));
        if (!out) return false;
    }
    // Atomic replace so the proxy never reads a half-written file.
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

std::optional<std::string> IniFile::Get(const std::string& key) const {
    for (const auto& [k, v] : entries_)
        if (k == key) return v;
    return std::nullopt;
}

std::string IniFile::GetOr(const std::string& key, const std::string& fallback) const {
    auto v = Get(key);
    return v ? *v : fallback;
}

bool IniFile::GetBool(const std::string& key, bool fallback) const {
    auto v = Get(key);
    if (!v) return fallback;
    return *v == "true" || *v == "1" || *v == "TRUE" || *v == "True";
}

void IniFile::Set(const std::string& key, const std::string& value) {
    for (auto& [k, v] : entries_) {
        if (k == key) {
            v = value;
            return;
        }
    }
    entries_.emplace_back(key, value);
}

std::vector<std::string> IniFile::GetAll(const std::string& key) const {
    std::vector<std::string> out;
    for (const auto& [k, v] : entries_)
        if (k == key) out.push_back(v);
    return out;
}

void IniFile::SetAll(const std::string& key, const std::vector<std::string>& values) {
    std::erase_if(entries_, [&](const auto& e) { return e.first == key; });
    for (const auto& v : values) entries_.emplace_back(key, v);
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

}  // namespace resorep
