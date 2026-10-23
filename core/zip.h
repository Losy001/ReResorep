#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace resorep {

uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0);

// Decompresses a raw DEFLATE stream (RFC 1951) into `out`, which should be sized to the
// expected output length; fails rather than writing past it.
bool Inflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out);

// Writes a .zip with stored (uncompressed) entries: textures are already compressed,
// and any zip tool, Windows Explorer included, can open the result.
class ZipWriter {
public:
    bool Open(const std::filesystem::path& path);
    bool AddFile(const std::string& name, const uint8_t* data, size_t size);  // name: UTF-8, '/' separated
    bool AddDirectory(const std::string& name);                                // trailing '/' added
    bool Finish();
    const std::string& error() const { return error_; }

private:
    struct Central {
        std::string name;
        uint32_t crc, size, offset;
    };
    bool Add(const std::string& name, const uint8_t* data, size_t size);

    std::ofstream out_;
    std::vector<Central> entries_;
    uint64_t offset_ = 0;
    uint16_t dosTime_ = 0, dosDate_ = 0;
    std::string error_;
};

// Reads a .zip's central directory; entries are extracted on demand.
class ZipReader {
public:
    struct Entry {
        std::string name;  // as stored ('/' separated)
        uint16_t method, flags;
        uint32_t crc, compressedSize, size, localOffset;
        bool isDirectory() const { return !name.empty() && (name.back() == '/' || name.back() == '\\'); }
    };

    bool Open(const std::filesystem::path& path);
    const std::vector<Entry>& entries() const { return entries_; }
    // Stored and deflated entries; checks size and CRC. Refuses entries over `maxSize`.
    bool Extract(const Entry& entry, std::vector<uint8_t>& out, uint32_t maxSize = 1u << 30);
    const std::string& error() const { return error_; }

private:
    std::ifstream in_;
    uint64_t fileSize_ = 0;
    std::vector<Entry> entries_;
    std::string error_;
};

}  // namespace resorep
