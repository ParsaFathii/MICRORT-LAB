// rng.hpp — deterministic xoshiro256** PRNG for MicroRT-Lab.
//
// Contract (SIMULATION_SCHEMA.md §Determinism rule 6): `seed` drives a
// xoshiro256** generator used ONLY for periodic release jitter (> 0).
// seed 0 = fully static schedule. Same config + seed => byte-identical
// results. Instances are self-contained (no global state) so the engine can
// derive independent sub-streams if needed.
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).
#ifndef MICRORT_RNG_HPP
#define MICRORT_RNG_HPP

#include <cstdint>

namespace micrort {

/// xoshiro256** generator seeded via splitmix64 (canonical pairing from the
/// xoshiro authors — any seed, including 0, produces a well-distributed,
/// reproducible stream).
class Xoshiro256 {
public:
    /// Seed the state with four splitmix64 outputs derived from `seed`.
    /// Deterministic: two instances constructed with equal seeds produce
    /// identical streams forever.
    explicit Xoshiro256(std::uint64_t seed);

    /// Next raw 64-bit value. Full period 2^256 - 1.
    std::uint64_t next();

    /// Unbiased uniform value in [0, n). Uses Lemire-style rejection on the
    /// raw stream, so every output is equally likely. n == 0 returns 0
    /// (documented degenerate case; callers should avoid it).
    std::uint64_t bounded(std::uint64_t n);

private:
    std::uint64_t s_[4];
};

/// splitmix64 step function (used for seeding; exposed for tests).
std::uint64_t splitmix64(std::uint64_t& state);

} // namespace micrort

#endif // MICRORT_RNG_HPP
