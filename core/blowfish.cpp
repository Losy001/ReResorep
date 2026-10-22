#include "blowfish.h"

#include <cstring>
#include <utility>

namespace resorep {
namespace {
#include "blowfish_tables.inc"

uint32_t LoadBE(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

void StoreBE(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}
}  // namespace

Blowfish::Blowfish(const uint8_t* key, size_t keyLen) {
    std::memcpy(p_, kInitP, sizeof(p_));
    std::memcpy(s_, kInitS, sizeof(s_));

    size_t k = 0;
    for (uint32_t& p : p_) {
        uint32_t word = 0;
        for (int b = 0; b < 4; ++b) {
            word = (word << 8) | key[k];
            k = (k + 1) % keyLen;
        }
        p ^= word;
    }

    uint32_t l = 0, r = 0;
    for (int i = 0; i < 18; i += 2) {
        EncryptBlock(l, r);
        p_[i] = l;
        p_[i + 1] = r;
    }
    for (auto& box : s_) {
        for (int i = 0; i < 256; i += 2) {
            EncryptBlock(l, r);
            box[i] = l;
            box[i + 1] = r;
        }
    }
}

uint32_t Blowfish::F(uint32_t x) const {
    return ((s_[0][x >> 24] + s_[1][(x >> 16) & 0xFF]) ^ s_[2][(x >> 8) & 0xFF]) + s_[3][x & 0xFF];
}

void Blowfish::EncryptBlock(uint32_t& l, uint32_t& r) const {
    for (int i = 0; i < 16; ++i) {
        l ^= p_[i];
        r ^= F(l);
        std::swap(l, r);
    }
    std::swap(l, r);
    r ^= p_[16];
    l ^= p_[17];
}

void Blowfish::DecryptBlock(uint32_t& l, uint32_t& r) const {
    for (int i = 17; i > 1; --i) {
        l ^= p_[i];
        r ^= F(l);
        std::swap(l, r);
    }
    std::swap(l, r);
    r ^= p_[1];
    l ^= p_[0];
}

void Blowfish::DecryptCbc(const uint8_t iv[8], uint8_t* data, size_t len) const {
    uint8_t prev[8];
    std::memcpy(prev, iv, 8);
    for (size_t off = 0; off + 8 <= len; off += 8) {
        uint8_t* block = data + off;
        uint8_t cipher[8];
        std::memcpy(cipher, block, 8);
        uint32_t l = LoadBE(block), r = LoadBE(block + 4);
        DecryptBlock(l, r);
        StoreBE(block, l);
        StoreBE(block + 4, r);
        for (int i = 0; i < 8; ++i) block[i] ^= prev[i];
        std::memcpy(prev, cipher, 8);
    }
}

void Blowfish::EncryptCbc(const uint8_t iv[8], uint8_t* data, size_t len) const {
    uint8_t prev[8];
    std::memcpy(prev, iv, 8);
    for (size_t off = 0; off + 8 <= len; off += 8) {
        uint8_t* block = data + off;
        for (int i = 0; i < 8; ++i) block[i] ^= prev[i];
        uint32_t l = LoadBE(block), r = LoadBE(block + 4);
        EncryptBlock(l, r);
        StoreBE(block, l);
        StoreBE(block + 4, r);
        std::memcpy(prev, block, 8);
    }
}

}  // namespace resorep
