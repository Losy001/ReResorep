#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace resorep {

// Tamper and corruption check for ReResorep.exe and the proxy d3d11.dll. After linking, the
// build seals each one (tools/seal.cpp): the SHA-256 of the file goes into a reserved slot in
// the file itself. At startup each one checks that its file still matches.
//
// The digest leaves out the slot, the PE checksum and any Authenticode certificate, so the
// files can still be code-signed after sealing. It catches damaged downloads and copies
// changed by anything that doesn't know to reseal them; it is not a defence against someone
// who patches the check itself out (only code signing is).
enum class SealState {
    Valid,
    Invalid,    // sealed, but the file no longer matches
    NotSealed,  // no slot (a build from before sealing, or not a ReResorep file)
    Unreadable,
};

SealState CheckSeal(const std::vector<uint8_t>& file);
SealState CheckSealedFile(const std::filesystem::path& path);
// The file this code was loaded from (the exe, or the DLL that links this).
SealState CheckOwnSeal();

// Writes the seal into `file` (a whole PE image). False if there is no slot, or more than one.
bool SealImage(std::vector<uint8_t>& file);

}  // namespace resorep
