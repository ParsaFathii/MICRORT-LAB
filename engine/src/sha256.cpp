// sha256.cpp — self-contained SHA-256 (FIPS 180-4) implementation.
//
// Straightforward single-block-at-a-time streaming over the padded message:
//   - message padded with 0x80, zero bits and a 64-bit big-endian bit length
//   - 512-bit blocks, 64-round compression with the standard K constants
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).

#include <micrort/sha256.hpp>

#include <cstddef>

namespace micrort {
namespace {

// Round constants (first 32 bits of the fractional parts of the cube roots
// of the first 64 primes, FIPS 180-4 section 4.2.2). Immutable.
constexpr std::uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline std::uint32_t rotr(std::uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

// Process one 64-byte block, updating the running state h[0..7].
void compress(std::uint32_t h[8], const std::uint8_t block[64]) {
    std::uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[4 * i + 0]) << 24) |
               (static_cast<std::uint32_t>(block[4 * i + 1]) << 16) |
               (static_cast<std::uint32_t>(block[4 * i + 2]) << 8) |
               (static_cast<std::uint32_t>(block[4 * i + 3]));
    }
    for (unsigned i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^
                                 (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^
                                 (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

    for (unsigned i = 0; i < 64; ++i) {
        const std::uint32_t S1 =
            rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = hh + S1 + ch + K[i] + w[i];
        const std::uint32_t S0 =
            rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = S0 + maj;

        hh = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

} // namespace

std::array<std::uint8_t, 32> sha256(const std::string& input) {
    std::uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                          0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

    // Total message length in BITS (input.size() * 8). Fits comfortably:
    // we only hash configuration documents, well below 2^61 bits.
    const std::uint64_t bitLen =
        static_cast<std::uint64_t>(input.size()) * 8u;

    // Process all complete 64-byte blocks (arithmetically derived bounds
    // keep the padding writes provably in-range for the compiler).
    const std::size_t rem = input.size() % 64;          // 0..63
    const std::size_t pos = input.size() - rem;         // full-block prefix
    const std::size_t fullBlocks = pos / 64;
    for (std::size_t b = 0; b < fullBlocks; ++b) {
        std::uint8_t block[64];
        for (unsigned i = 0; i < 64; ++i) {
            block[i] = static_cast<std::uint8_t>(input[b * 64 + i]);
        }
        compress(h, block);
    }

    // Final partial block: 0x80 delimiter, zero padding, 8-byte big-endian
    // bit length. At least one padding byte is always needed, so the tail
    // may spill into one extra block.
    std::uint8_t tail[128] = {0};
    for (std::size_t i = 0; i < rem; ++i) {
        tail[i] = static_cast<std::uint8_t>(input[pos + i]);
    }
    tail[rem] = 0x80u;
    const std::size_t tailLen = (rem + 1 + 8 <= 64) ? 64 : 128;
    for (unsigned i = 0; i < 8; ++i) {
        tail[tailLen - 1 - i] =
            static_cast<std::uint8_t>((bitLen >> (8u * i)) & 0xffu);
    }
    compress(h, tail);
    if (tailLen == 128) {
        compress(h, tail + 64);
    }

    std::array<std::uint8_t, 32> digest{};
    for (unsigned i = 0; i < 8; ++i) {
        digest[4 * i + 0] = static_cast<std::uint8_t>((h[i] >> 24) & 0xffu);
        digest[4 * i + 1] = static_cast<std::uint8_t>((h[i] >> 16) & 0xffu);
        digest[4 * i + 2] = static_cast<std::uint8_t>((h[i] >> 8) & 0xffu);
        digest[4 * i + 3] = static_cast<std::uint8_t>(h[i] & 0xffu);
    }
    return digest;
}

std::string sha256_hex(const std::string& input) {
    static constexpr char kHex[] = "0123456789abcdef"; // immutable constant
    const std::array<std::uint8_t, 32> d = sha256(input);
    std::string out;
    out.reserve(64);
    for (std::size_t i = 0; i < d.size(); ++i) {
        out.push_back(kHex[(d[i] >> 4) & 0x0fu]);
        out.push_back(kHex[d[i] & 0x0fu]);
    }
    return out;
}

} // namespace micrort
