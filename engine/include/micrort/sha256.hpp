// sha256.hpp — self-contained SHA-256 (FIPS 180-4) for MicroRT-Lab.
//
// Used for config hashing (configHash = first 16 hex chars of the digest of
// the canonical config JSON) and for internal self-tests. No external
// dependencies, no global mutable state — pure functions only.
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).
#ifndef MICRORT_SHA256_HPP
#define MICRORT_SHA256_HPP

#include <array>
#include <cstdint>
#include <string>

namespace micrort {

/// Raw 32-byte SHA-256 digest of the byte string `input`.
/// Deterministic, allocation-free beyond the result. Never throws.
std::array<std::uint8_t, 32> sha256(const std::string& input);

/// Lowercase hexadecimal (64 chars) SHA-256 digest of `input`.
/// Contract: len(result) == 64, chars in [0-9a-f].
std::string sha256_hex(const std::string& input);

} // namespace micrort

#endif // MICRORT_SHA256_HPP
