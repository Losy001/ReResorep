#include "mod_state.h"

#include <cctype>
#include <cwctype>
#include <vector>

#include "settings.h"

namespace resorep {

std::wstring DisabledSet::Slashed(const std::filesystem::path& rel) {
    std::wstring s = rel.lexically_normal().wstring();
    for (wchar_t& c : s)
        if (c == L'\\') c = L'/';
    while (!s.empty() && s.back() == L'/') s.pop_back();
    if (s == L".") s.clear();
    return s;
}

std::wstring DisabledSet::Key(const std::filesystem::path& rel) {
    std::wstring s = Slashed(rel);
    for (wchar_t& c : s) c = wchar_t(std::towlower(c));
    return s;
}

bool DisabledSet::Load(const std::filesystem::path& moddedRoot) {
    entries_.clear();
    IniFile ini;
    if (!ini.Load(moddedRoot / kFileName)) return false;
    for (const std::string& v : ini.GetAll("disabled")) {
        const std::wstring rel = Slashed(Utf8ToWide(v));
        if (!rel.empty()) entries_[Key(rel)] = rel;
    }
    return true;
}

bool DisabledSet::Save(const std::filesystem::path& moddedRoot) const {
    IniFile ini;
    std::vector<std::string> values;
    for (const auto& [key, rel] : entries_) values.push_back(WideToUtf8(rel));
    ini.SetAll("disabled", values);
    return ini.Save(moddedRoot / kFileName);
}

bool DisabledSet::IsDisabled(const std::filesystem::path& rel) const { return entries_.count(Key(rel)) != 0; }

bool DisabledSet::IsEffectivelyDisabled(const std::filesystem::path& rel) const {
    if (entries_.empty()) return false;
    std::wstring key = Key(rel);
    for (;;) {
        if (entries_.count(key)) return true;
        const size_t slash = key.rfind(L'/');
        if (slash == std::wstring::npos) return false;
        key.resize(slash);
    }
}

void DisabledSet::Set(const std::filesystem::path& rel, bool disabled) {
    const std::wstring slashed = Slashed(rel);
    if (slashed.empty()) return;
    if (disabled) entries_[Key(slashed)] = slashed;
    else entries_.erase(Key(slashed));
}

void DisabledSet::Rename(const std::filesystem::path& from, const std::filesystem::path& to) {
    const std::wstring fromKey = Key(from), toSlashed = Slashed(to);
    std::map<std::wstring, std::wstring> updated;
    for (const auto& [key, rel] : entries_) {
        if (key == fromKey || key.starts_with(fromKey + L"/")) {
            const std::wstring moved = toSlashed + rel.substr(fromKey.size());
            updated[Key(moved)] = moved;
        } else {
            updated[key] = rel;
        }
    }
    entries_ = std::move(updated);
}

void DisabledSet::RemoveUnder(const std::filesystem::path& rel) {
    const std::wstring key = Key(rel);
    std::erase_if(entries_, [&](const auto& e) { return e.first == key || e.first.starts_with(key + L"/"); });
}

std::string ReadModGame(const std::filesystem::path& modFolder) {
    IniFile ini;
    if (!ini.Load(modFolder / kModGameFile)) return {};
    std::string sha = ini.GetOr(keys::kGameSha256, "");
    for (char& c : sha) c = char(std::tolower(static_cast<unsigned char>(c)));
    return sha;
}

bool WriteModGame(const std::filesystem::path& modFolder, const std::string& exeSha256, const std::string& gameName) {
    std::error_code ec;
    if (exeSha256.empty()) {
        std::filesystem::remove(modFolder / kModGameFile, ec);
        return !ec;
    }
    IniFile ini;
    ini.Set(keys::kGameSha256, exeSha256);
    if (!gameName.empty()) ini.Set("game", gameName);
    return ini.Save(modFolder / kModGameFile);
}

}  // namespace resorep
