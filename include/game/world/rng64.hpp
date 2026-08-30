#pragma once

#include "engine/core/types.hpp"

namespace csc::game::world {

/// SplitMix64 — deterministic, bit-identical across platforms (fixed constants),
/// and splittable: successive draws (and sub-seeds derived from them) are
/// independent. No heap, no globals. Shared by the Fase 4 generators
/// (star_system_gen, planet_terrain, …).
struct Rng64 {
    u64 state;

    explicit Rng64(u64 seed) : state(seed) {}

    u64 next_u64()
    {
        u64 z = (state += 0x9E3779B97F4A7C15ull);
        z     = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z     = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    /// [0, 1) — top 24 bits of a draw, exact in f32.
    f32 next_unit()
    {
        return static_cast<f32>(next_u64() >> 40) * (1.0f / 16777216.0f);
    }

    f32 range(f32 lo, f32 hi) { return lo + (hi - lo) * next_unit(); }

    /// Inclusive [lo, hi].
    u32 range_u32(u32 lo, u32 hi)
    {
        if (hi <= lo) {
            return lo;
        }
        return lo + static_cast<u32>(next_u64() % (static_cast<u64>(hi - lo) + 1ull));
    }
};

}  // namespace csc::game::world
