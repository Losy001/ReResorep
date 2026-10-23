#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace resorep {

// Encrypted mod container used by the original Resorep:
//   [0..7]   "resorep1"
//   [8]      IV length n (8 for Blowfish)
//   [9..9+n) IV
//   [9+n..]  Blowfish-CBC ciphertext of a .dds file, 16-byte key
bool IsEncryptedTexture(const uint8_t* data, size_t size);

// Decrypts the payload into `out`. Returns false if the header is invalid.
bool DecryptTexture(const uint8_t* data, size_t size, std::vector<uint8_t>& out);

// For reading part of an encrypted file: where the ciphertext starts (0 if the header is
// invalid), and the IV. Ciphertext offsets equal plaintext offsets from there on.
size_t EncryptedPayloadOffset(const uint8_t* data, size_t size, uint8_t iv[8]);
// CBC decrypts any run of whole blocks given the ciphertext block before it (or the IV).
void DecryptTextureRange(const uint8_t previousCipherBlock[8], uint8_t* data, size_t len);

// Produces a container the original proxy can read. Data is zero-padded to 8 bytes,
// which is harmless because DDS readers ignore trailing bytes.
std::vector<uint8_t> EncryptTexture(const uint8_t* dds, size_t size, const uint8_t iv[8]);

}  // namespace resorep
