#pragma once
#include <emmintrin.h>

#include <cstddef>
#include <cstdint>

namespace resorep {

// Texture fingerprint used by the original Resorep proxy (dx11proxy64.dll,
// sub_180001DF0). It is 1 + the sum of every byte read as int8, over
// SysMemSlicePitch bytes of the first subresource. Wraps like uint32.
// Existing texture packs are named after this value, so it must not change.
inline uint32_t ComputeTextureHashScalar(const void* data, size_t size) {
    const int8_t* p = static_cast<const int8_t*>(data);
    uint32_t sum = 1;
    for (size_t i = 0; i < size; ++i)
        sum += static_cast<uint32_t>(static_cast<int32_t>(p[i]));
    return sum;
}

// Same value, 16 bytes at a time: int8 x == uint8(x ^ 0x80) - 128, and
// _mm_sad_epu8 against zero sums unsigned bytes.
inline uint32_t ComputeTextureHash(const void* data, size_t size) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    const size_t vecBytes = size & ~size_t(15);
    const __m128i bias = _mm_set1_epi8(-128);
    const __m128i zero = _mm_setzero_si128();
    __m128i acc = _mm_setzero_si128();
    for (size_t i = 0; i < vecBytes; i += 16) {
        const __m128i v = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(p + i)), bias);
        acc = _mm_add_epi64(acc, _mm_sad_epu8(v, zero));
    }
    const uint64_t unsignedSum =
        uint64_t(_mm_cvtsi128_si64(acc)) + uint64_t(_mm_cvtsi128_si64(_mm_unpackhi_epi64(acc, acc)));
    const uint32_t vecSum = uint32_t(unsignedSum) - uint32_t(128 * vecBytes);
    return vecSum + ComputeTextureHashScalar(p + vecBytes, size - vecBytes);
}

// The original also accepts a mod whose hash is off by up to 2, tried in this order.
inline constexpr int kHashTolerance[] = {0, 1, -1, 2, -2};

}  // namespace resorep
