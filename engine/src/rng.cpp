// rng.cpp — xoshiro256** + splitmix64 (reference algorithms, public domain
// dedicated to the authors David Blackman and Sebastiano Vigna, 2019).
// Seeding follows the canonical recommendation: fill the state with four
// successive splitmix64 outputs so even seeds like 0 spread well.
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).

#include <micrort/rng.hpp>

namespace micrort {

namespace {

inline std::uint64_t rotl64(std::uint64_t x, unsigned k) {
    return (x << k) | (x >> (64u - k));
}

} // namespace

std::uint64_t splitmix64(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

Xoshiro256::Xoshiro256(std::uint64_t seed) {
    std::uint64_t sm = seed; // local copy — the caller's value is not mutated
    s_[0] = splitmix64(sm);
    s_[1] = splitmix64(sm);
    s_[2] = splitmix64(sm);
    s_[3] = splitmix64(sm);
}

std::uint64_t Xoshiro256::next() {
    const std::uint64_t result = rotl64(s_[1] * 5, 7) * 9;
    const std::uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl64(s_[3], 45);
    return result;
}

std::uint64_t Xoshiro256::bounded(std::uint64_t n) {
    if (n == 0) {
        return 0;
    }
    // Rejection threshold = 2^64 mod n: values below it would bias modulo.
    // (2^64 - n) % n == 2^64 mod n for every n >= 1.
    const std::uint64_t threshold = (0ull - n) % n;
    std::uint64_t x = next();
    while (x < threshold) {
        x = next();
    }
    return x % n;
}

} // namespace micrort
