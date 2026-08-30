#pragma once

#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"

#include <cstddef>

namespace csc::game::world {

// --- P4-05: inter-system navigation (jump points) + galaxy map -------------
// A deterministic little cluster of star systems. Each node carries the u64
// seed that P4-01's generate_star_system consumes, a 2D map position, and links
// to a few neighbours. Jumping is a load transition: pick a linked node, rebuild
// the world for its seed (see world::request_jump / world::fixed_step).

inline constexpr u32 kMaxGalaxySystems = 12;
inline constexpr u32 kMaxSystemLinks   = 4;
inline constexpr u64 kDefaultGalaxySeed = 0x6C6178795F76310Aull;  // "laxy_v1\n"
/// Node 0's seed — matches `base_seed` in assets/data/star_system.cfg (P4-04),
/// so galaxy node 0 IS the curated home system.
inline constexpr u64 kHomeSystemSeed = 20260830ull;

struct GalaxySystem {
    char      name[24]{};
    u64       seed        = 0;
    glm::vec2 map_pos{0.f};          ///< arbitrary galaxy-map units
    u32       links[kMaxSystemLinks]{};
    u32       link_count = 0;
};

struct GalaxyMap {
    u64          galaxy_seed = 0;
    GalaxySystem systems[kMaxGalaxySystems]{};
    u32          count   = 0;
    u32          current = 0;         ///< index of the loaded system
};

/// Build a deterministic galaxy from `galaxy_seed`: `count` systems on a disc,
/// each linked to its 2–3 nearest neighbours, and the graph made connected.
/// System 0 is the "home" node — its seed is `home_system_seed` (so the fixed
/// system, P4-04, is galaxy node 0). Fully overwrites `out`.
void generate_galaxy(u64 galaxy_seed, u64 home_system_seed, GalaxyMap& out);

/// True if `a` and `b` are directly linked (jumpable in one hop).
[[nodiscard]] bool galaxy_linked(const GalaxyMap& g, u32 a, u32 b);

/// Set `g.current = target` iff it is linked to the current node. Returns
/// success. Pure state change — the caller rebuilds the world.
[[nodiscard]] bool galaxy_jump(GalaxyMap& g, u32 target);

/// Seed of the currently-selected system.
[[nodiscard]] u64 galaxy_current_seed(const GalaxyMap& g);

/// Log the galaxy graph (systems, positions, links, current) — the "simplified
/// map" for this phase; a real map view is Fase 6 polish.
void galaxy_log(const GalaxyMap& g);

/// P4-05 headless check (scene gate CSC_GALAXY_SMOKE=1): generate a galaxy and
/// assert it is deterministic, every link is symmetric, the graph is connected
/// (BFS from 0 reaches all nodes), node 0 carries the home seed, and
/// galaxy_jump accepts linked targets / rejects the rest and round-trips.
/// Logs `CSC_GALAXY_SMOKE: PASS|FAIL`.
[[nodiscard]] bool galaxy_smoke_test();

}  // namespace csc::game::world
