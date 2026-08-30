#pragma once

#include "engine/core/types.hpp"
#include "game/world/world.hpp"

#include <cstddef>

namespace csc::game::world {

// --- P4-01: deterministic star-system generator -----------------------------
// seed (u64) -> StarSystemData, filling the SAME fixed-array struct the rest of
// the engine already consumes (P1D-02). Fully deterministic per Fase 4 rule 1:
// the same seed produces byte-identical output on any machine / any session.
// Pure — no FILE I/O, no heap, no globals. The Fase 1D fixed system becomes one
// particular seed via P4-04; this task only builds the generator.

/// A system is star + [kMinPlanets, kMaxPlanets] planets; roughly half the
/// planets also get a landing zone. Worst case 1 + 6 + 6 = 13 bodies, well under
/// kMaxCelestialBodies (16).
inline constexpr u32 kMinPlanets = 2;
inline constexpr u32 kMaxPlanets = 6;

inline constexpr f32 kStarRadiusMin   = 60.f;
inline constexpr f32 kStarRadiusMax   = 150.f;
inline constexpr f32 kPlanetRadiusMin = 110.f;
inline constexpr f32 kPlanetRadiusMax = 340.f;

/// First planet's orbit radius (jittered per system) and the multiplicative
/// step between successive orbits — geometric spacing, always > 1 so orbit
/// radii are strictly increasing.
inline constexpr f32 kFirstOrbitRadius = 3000.f;
inline constexpr f32 kFirstOrbitJitterMin = 0.85f;
inline constexpr f32 kFirstOrbitJitterMax = 1.25f;
inline constexpr f32 kOrbitStepMin = 1.55f;
inline constexpr f32 kOrbitStepMax = 2.10f;

/// Per-body values derived from the seed but not stored in CelestialBody —
/// exposed so P4-02 (terrain) / P4-06 (resources) / P4-07 (POIs) can reuse the
/// exact same deterministic numbers. Index-parallel to StarSystemData.bodies.
struct GeneratedBodyInfo {
    u64  body_seed        = 0;     ///< sub-seed for this body's terrain / contents
    f32  orbit_radius     = 0.f;   ///< 0 for the star
    f32  orbit_angle_rad  = 0.f;
    bool has_atmosphere   = false; ///< planets only
    bool has_landing_zone = false; ///< planets only (an LZ body follows in the array)
};

struct GeneratedSystemInfo {
    u64               seed = 0;
    GeneratedBodyInfo bodies[kMaxCelestialBodies]{};
    u32               body_count = 0;
};

/// Deterministic name for a generated system (`"Sys-XXXXXXXX"`, hex of the low
/// 32 bits of the seed). Fits kNameBytes.
void generated_system_name(u64 seed, char* out, std::size_t cap);

/// Generate a star system from `seed` into `out` (fully overwritten) and, if
/// `info` is non-null, the parallel per-body derived data. Returns the body
/// count written. Layout: index 0 is the Star; then, per planet, the Planet
/// body immediately followed by its LandingZone body when it has one.
u32 generate_star_system(u64 seed, StarSystemData& out, GeneratedSystemInfo* info = nullptr);

/// P4-01 headless check (scene gate CSC_SYSTEMGEN_SMOKE=1): generate several
/// seeds and assert body counts in range, star at index 0, orbit radii strictly
/// increasing, every landing zone paired to the planet before it, and that
/// re-generating the same seed is byte-identical. Logs `CSC_SYSTEMGEN_SMOKE:
/// PASS|FAIL`. Pure — needs no world.
[[nodiscard]] bool star_system_gen_smoke_test();

}  // namespace csc::game::world
