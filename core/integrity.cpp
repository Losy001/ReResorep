#include "integrity.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>

namespace resorep {
namespace {

struct Seal {
    uint8_t magic[16];
    uint8_t digest[32];  // filled in by tools/seal.cpp after linking
};
// volatile: emitted as is and read at run time, so the sealer finds exactly these bytes in the
// file. The magic is only ever read from here, so it occurs once in a sealed binary.
const volatile Seal kSeal = {{'R', 'e', 'R', 'e', 's', 'o', 'r', 'e', 'p', ':', 's', 'e', 'a', 'l', ':', '1'}, {}};

uint32_t Read32(const std::vector<uint8_t>& f, size_t at) {
    uint32_t v;
    std::memcpy(&v, f.data() + at, 4);
    return v;
}
uint16_t Read16(const std::vector<uint8_t>& f, size_t at) {
    uint16_t v;
    std::memcpy(&v, f.data() + at, 2);
    return v;
}

// Where the seal goes, and what the digest leaves out.
struct Layout {
    size_t slot = 0;       // offset of the digest bytes
    size_t checksum = 0;   // PE optional header CheckSum
    size_t security = 0;   // the certificate table's data directory entry, 0 if none
    size_t hashedEnd = 0;  // the certificate table, if any, is at the end and not hashed
};

enum class Find { Ok, NoSlot, Bad };

Find Locate(const std::vector<uint8_t>& f, Layout& out) {
    uint8_t magic[sizeof(kSeal.magic)];
    for (size_t i = 0; i < sizeof(magic); ++i) magic[i] = kSeal.magic[i];
    const std::boyer_moore_horspool_searcher searcher(std::begin(magic), std::end(magic));
    const auto first = std::search(f.begin(), f.end(), searcher);
    if (first == f.end()) return Find::NoSlot;
    if (std::search(first + 1, f.end(), searcher) != f.end()) return Find::Bad;  // ambiguous
    out.slot = size_t(first - f.begin()) + sizeof(magic);
    if (out.slot + sizeof(kSeal.digest) > f.size()) return Find::Bad;

    // PE headers: DOS header -> "PE\0\0" -> COFF header -> optional header.
    if (f.size() < 0x40 || f[0] != 'M' || f[1] != 'Z') return Find::Bad;
    const size_t pe = Read32(f, 0x3C);
    if (pe > f.size() || f.size() - pe < 24 || std::memcmp(&f[pe], "PE\0\0", 4) != 0) return Find::Bad;
    const size_t opt = pe + 24;
    const size_t optSize = Read16(f, pe + 4 + 16);
    if (opt + optSize > f.size() || optSize < 2) return Find::Bad;
    const uint16_t kind = Read16(f, opt);
    if (kind != 0x20B && kind != 0x10B) return Find::Bad;
    const size_t dirCountAt = opt + (kind == 0x20B ? 108 : 92);
    const size_t dirs = opt + (kind == 0x20B ? 112 : 96);
    if (dirs > opt + optSize) return Find::Bad;
    out.checksum = opt + 64;
    out.hashedEnd = f.size();
    out.security = 0;
    if (Read32(f, dirCountAt) > 4 && dirs + 5 * 8 <= opt + optSize) {
        out.security = dirs + 4 * 8;
        const uint64_t certAt = Read32(f, out.security), certSize = Read32(f, out.security + 4);
        if (certSize != 0) {
            // A signature is appended at the very end; anything else there would go unhashed.
            if (certAt + certSize != f.size()) return Find::Bad;
            out.hashedEnd = size_t(certAt);
        }
    }
    // Everything the digest blanks out lies in the hashed part (a crafted file could say otherwise).
    if (out.slot + sizeof(kSeal.digest) > out.hashedEnd || out.checksum + 4 > out.hashedEnd ||
        (out.security && out.security + 8 > out.hashedEnd))
        return Find::Bad;
    return Find::Ok;
}

bool Sha256(const uint8_t* data, size_t size, uint8_t digest[32]) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return false;
    bool ok = BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0));
    for (size_t done = 0; ok && done < size;) {
        const ULONG chunk = ULONG(std::min<size_t>(size - done, 1u << 30));
        ok = BCRYPT_SUCCESS(BCryptHashData(hash, const_cast<PUCHAR>(data + done), chunk, 0));
        done += chunk;
    }
    ok = ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, 32, 0));
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

bool Digest(const std::vector<uint8_t>& f, const Layout& at, uint8_t digest[32]) {
    std::vector<uint8_t> copy(f.begin(), f.begin() + ptrdiff_t(at.hashedEnd));
    std::memset(&copy[at.slot], 0, sizeof(kSeal.digest));
    std::memset(&copy[at.checksum], 0, 4);
    if (at.security) std::memset(&copy[at.security], 0, 8);
    return Sha256(copy.data(), copy.size(), digest);
}

bool ReadWhole(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamoff size = in.tellg();
    if (size <= 0) return false;
    out.resize(size_t(size));
    in.seekg(0);
    return bool(in.read(reinterpret_cast<char*>(out.data()), size));
}

}  // namespace

SealState CheckSeal(const std::vector<uint8_t>& file) {
    Layout at;
    switch (Locate(file, at)) {
        case Find::NoSlot: return SealState::NotSealed;
        case Find::Bad: return SealState::Invalid;
        case Find::Ok: break;
    }
    uint8_t digest[32];
    if (!Digest(file, at, digest)) return SealState::Unreadable;
    return std::memcmp(digest, &file[at.slot], 32) == 0 ? SealState::Valid : SealState::Invalid;
}

SealState CheckSealedFile(const std::filesystem::path& path) {
    std::vector<uint8_t> file;
    return ReadWhole(path, file) ? CheckSeal(file) : SealState::Unreadable;
}

SealState CheckOwnSeal() {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&CheckOwnSeal), &self))
        return SealState::Unreadable;
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(self, path.data(), DWORD(path.size()));
        if (n == 0) return SealState::Unreadable;
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    return CheckSealedFile(path);
}

bool SealImage(std::vector<uint8_t>& file) {
    Layout at;
    if (Locate(file, at) != Find::Ok) return false;
    uint8_t digest[32];
    if (!Digest(file, at, digest)) return false;
    std::memcpy(&file[at.slot], digest, sizeof(digest));
    return true;
}

}  // namespace resorep
