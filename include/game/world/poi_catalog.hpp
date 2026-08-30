#pragma once

#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"
#include "game/world/world.hpp"

#include <flecs.h>

namespace csc::game::world {

// --- P4-07: hand-authored points of interest over the procedural universe ---
// Stations / outposts / beacons / wrecks placed deterministically in a system
// (in orbit, on a generated planet's surface, or at an absolute offset). Extends
// the P3-04 location idea from "populated decks in a scene" to "curated anchors
// in a generated system".

inline constexpr u32 kMaxPoiDefs   = 24;
inline constexpr u32 kPoiIdBytes   = 40;
inline constexpr u32 kPoiNameBytes = 32;

inline constexpr const char* kPoiCatalogPath = "assets/data/pois.cfg";

enum class PoiKind : u8 { Station = 0, Outpost = 1, Beacon = 2, Wreck = 3 };
enum class PoiPlacement : u8 { Orbit = 0, PlanetSurface = 1, Absolute = 2 };

struct PoiDef {
    char         id[kPoiIdBytes]{};
    char         name[kPoiNameBytes]{};
    PoiKind      kind      = PoiKind::Outpost;
    PoiPlacement placement = PoiPlacement::Orbit;
    u64          system_seed = 0;  ///< which system; 0 = home (kHomeSystemSeed)

    // Orbit
    f32 orbit_radius    = 3000.f;
    f32 orbit_angle_deg = 0.f;
    f32 orbit_y         = 0.f;

    // PlanetSurface (Nth Planet body in the system, 0-based)
    u32 planet_index = 0;
    f32 lat_deg      = 0.f;
    f32 lon_deg      = 0.f;
    f32 altitude     = 20.f;

    // Absolute
    glm::vec3 pos{0.f};

    f32  marker_radius = 30.f;
    char location_id[kPoiIdBytes]{};  ///< optional P3-04 locations.cfg bridge (populated station)
};

struct PoiCatalog {
    PoiDef items[kMaxPoiDefs]{};
    u32    count = 0;
};

[[nodiscard]] bool load_poi_catalog_file(PoiCatalog& out, const char* path);
bool load_poi_catalog(flecs::world& world);

/// Spawn every POI whose `system_seed` matches `system_seed` (0 in the def means
/// "home"). Orbit / Absolute place directly; PlanetSurface uses P4-02 terrain to
/// sit the POI on the Nth planet's surface. Station POIs reuse the P1D station
/// spawn; others are markers. A `location_id` that resolves in the P3-04
/// LocationCatalog also gets its NPCs populated at the POI position.
void spawn_pois_for_system(
    flecs::world& world, u64 system_seed, const StarSystemData& data);

/// P4-07 headless check (scene gate CSC_POI_SMOKE=1): catalog loads, ids unique,
/// PlanetSurface POIs name a plausible planet index, and placement resolution is
/// deterministic. Logs `CSC_POI_SMOKE: PASS|FAIL`.
[[nodiscard]] bool poi_smoke_test();

}  // namespace csc::game::world
