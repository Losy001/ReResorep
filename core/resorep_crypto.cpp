#include "resorep_crypto.h"

#include <cstring>

#include "blowfish.h"

namespace resorep {
namespace {
constexpr char kMagic[8] = {'r', 'e', 's', 'o', 'r', 'e', 'p', '1'};
constexpr size_t kHeader = 9;

// The original stores the key as two XOR halves (dx11proxy64.dll .rdata).
constexpr uint8_t kKeyA[16] = {0x72, 0x65, 0x73, 0x6f, 0x72, 0x65, 0x70, 0x31,
                               0x2e, 0x3d, 0x1e, 0x13, 0x4f, 0x1e, 0x1c, 0x0b};
constexpr uint8_t kKeyB[16] = {0x7d, 0x01, 0xe1, 0x51, 0x2e, 0x67, 0x6c, 0xdd,
                               0xa4, 0x9a, 0xd2, 0x77, 0xff, 0x47, 0x75, 0x1e};

const Blowfish& Cipher() {
    static const Blowfish cipher = [] {
        uint8_t key[16];
        for (int i = 0; i < 16; ++i) key[i] = kKeyA[i] ^ kKeyB[i];
        return Blowfish(key, sizeof(key));
    }();
    return cipher;
}
}  // namespace

bool IsEncryptedTexture(const uint8_t* data, size_t size) {
    return size >= kHeader && std::memcmp(data, kMagic, sizeof(kMagic)) == 0;
}

bool DecryptTexture(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
    if (!IsEncryptedTexture(data, size)) return false;
    const size_t ivLen = data[8];
    if (ivLen < 8 || size <= kHeader + ivLen) return false;

    out.assign(data + kHeader + ivLen, data + size);
    Cipher().DecryptCbc(data + kHeader, out.data(), out.size());
    return true;
}

size_t EncryptedPayloadOffset(const uint8_t* data, size_t size, uint8_t iv[8]) {
    if (!IsEncryptedTexture(data, size)) return 0;
    const size_t ivLen = data[8];
    if (ivLen < 8 || size < kHeader + ivLen) return 0;
    std::memcpy(iv, data + kHeader, 8);
    return kHeader + ivLen;
}

void DecryptTextureRange(const uint8_t previousCipherBlock[8], uint8_t* data, size_t len) {
    Cipher().DecryptCbc(previousCipherBlock, data, len);
}

std::vector<uint8_t> EncryptTexture(const uint8_t* dds, size_t size, const uint8_t iv[8]) {
    std::vector<uint8_t> out(kHeader + 8 + ((size + 7) & ~size_t(7)), 0);
    std::memcpy(out.data(), kMagic, sizeof(kMagic));
    out[8] = 8;
    std::memcpy(out.data() + kHeader, iv, 8);
    std::memcpy(out.data() + kHeader + 8, dds, size);
    Cipher().EncryptCbc(iv, out.data() + kHeader + 8, out.size() - kHeader - 8);
    return out;
}

}  // namespace resorep
