// Seals a freshly linked ReResorep.exe or d3d11.dll (see core/integrity.h) into a copy. Runs
// after every build of those two targets: reresorep_seal <linked file> <sealed copy>
// The linked file is never changed, so the build doesn't see it as modified and relink it.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "integrity.h"

namespace {
bool ReadWhole(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    out.resize(size_t(in.tellg()));
    in.seekg(0);
    return bool(in.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size()))) && !out.empty();
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: reresorep_seal <linked exe or dll> <sealed copy>\n");
        return 2;
    }
    const std::filesystem::path linked = argv[1], sealed = argv[2];
    std::vector<uint8_t> file;
    if (!ReadWhole(linked, file)) {
        std::fprintf(stderr, "reresorep_seal: cannot read %ls\n", linked.c_str());
        return 1;
    }
    if (!resorep::SealImage(file)) {
        std::fprintf(stderr, "reresorep_seal: %ls has no seal slot, or more than one\n", linked.c_str());
        return 1;
    }
    // Visual Studio runs this on every build, linked or not: leave an up-to-date copy untouched.
    if (std::vector<uint8_t> current; ReadWhole(sealed, current) && current == file) return 0;

    std::error_code ec;
    std::filesystem::create_directories(sealed.parent_path(), ec);
    {
        std::ofstream out(sealed, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
        out.close();
        if (out.fail()) {
            std::fprintf(stderr, "reresorep_seal: cannot write %ls (is it running?)\n", sealed.c_str());
            return 1;
        }
    }
    if (resorep::CheckSealedFile(sealed) != resorep::SealState::Valid) {
        std::fprintf(stderr, "reresorep_seal: %ls does not verify after sealing\n", sealed.c_str());
        return 1;
    }
    std::printf("Sealed %ls\n", sealed.filename().c_str());
    return 0;
}
