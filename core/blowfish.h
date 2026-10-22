#pragma once
#include <cstddef>
#include <cstdint>

namespace resorep {

class Blowfish {
public:
    Blowfish(const uint8_t* key, size_t keyLen);

    void EncryptBlock(uint32_t& l, uint32_t& r) const;
    void DecryptBlock(uint32_t& l, uint32_t& r) const;

    // CBC over whole 8-byte blocks; a trailing partial block is left untouched
    // (matches libtomcrypt's cbc_decrypt, which the original proxy used).
    void DecryptCbc(const uint8_t iv[8], uint8_t* data, size_t len) const;
    void EncryptCbc(const uint8_t iv[8], uint8_t* data, size_t len) const;

private:
    uint32_t F(uint32_t x) const;

    uint32_t p_[18];
    uint32_t s_[4][256];
};

}  // namespace resorep
