#pragma once

#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"

#include <flecs.h>

#include <cstddef>

namespace csc::game::world {

// --- Tunables ----------------------------------------------------------------
inline constexpr u32         kMaxCelestialBodies      = 16;
inline constexpr u32         kMaxStreamTriggers       = 16;
inline constexpr u32         kMaxStreamContentPerTrig = 8;
inline constexpr std::size_t kNameBytes               = 64;
/// Distance (metres, relative frame) from origin at which rebase fires.
inline constexpr f32 kFloatingOriginThreshold = 2000.f;
inline constexpr f32 kDefaultStreamLoadRadius   = 120.f;
inline constexpr f32 kDefaultStreamUnloadRadius = 180.f;

// --- Floating-origin contract (P1D-01 / P1D-07) -------------------------------
//
// DECISION: floating origin with f32 relative positions. Do NOT mix with f64
// world positions (rejected alternative — see STATUS.md).
//
// Contract (Fase 1A / 1B / 4 / 5 MUST respect):
//   1. All gameplay / render positions (`ecs::Position`, `RigidBody6DOF.position`,
//      `GravityZone.center`, stream trigger centers, camera eye/target) are f32
//      coordinates relative to the current floating origin.
//   2. Absolute world = relative + FloatingOrigin.origin_offset (accumulated).
//   3. When the player's relative distance from (0,0,0) exceeds
//      FloatingOrigin.threshold, rebase_if_needed subtracts the player position
//      from every relative position in one O(n) pass, adds that delta to
//      origin_offset, and increments rebase_count.
//   4. Rebase never deletes entities / never mutates archetypes mid-iteration
//      beyond in-place field writes. LocalToShip.local_position is NOT shifted
//      (ship-local); world caches on those entities are shifted consistently
//      with the host RigidBody6DOF so the next LocalToShip sync stays valid.
//   5. Rebase runs from the fixed-step path only (not every render frame).
//
// ---------------------------------------------------------------------------

enum class CelestialBodyType : u8 {
    Star         = 0,
    Planet       = 1,
    Station      = 2,
    LandingZone  = 3,
};

struct CelestialBody {
    char              name[kNameBytes]{};
    CelestialBodyType type   = CelestialBodyType::Planet;
    glm::vec3         position{0.f}; // config / absolute-at-load → relative frame
    f32               radius = 1.f;
};

/// Fixed star-system table loaded once from assets/data/star_system.cfg (P1D-02).
struct StarSystemData {
    char          system_name[kNameBytes]{};
    CelestialBody bodies[kMaxCelestialBodies]{};
    u32           body_count = 0;
};

/// Flecs singleton — floating origin state (P1D-07).
struct FloatingOrigin {
    glm::vec3 origin_offset{0.f}; // accumulated absolute of current origin
    u32       rebase_count = 0;
    f32       threshold    = kFloatingOriginThreshold;
};

enum class StreamContentKind : u8 {
    StationInterior = 0,
    LandingZone     = 1,
};

/// Proximity volume: soft load/unload of heavy content (P1D-03).
/// When loaded, content entities gain InteriorLoaded + InstanceTag (if meshable).
struct LevelStreamTrigger {
    glm::vec3         center{0.f};
    f32               load_radius   = kDefaultStreamLoadRadius;
    f32               unload_radius = kDefaultStreamUnloadRadius;
    StreamContentKind kind          = StreamContentKind::StationInterior;
    flecs::entity_t   content[kMaxStreamContentPerTrig]{};
    u32               content_count = 0;
    bool              loaded        = false;
};

/// Tag: streamed interior / landing-zone content currently active.
struct InteriorLoaded {};

/// Tag: entity participates in a LevelStreamTrigger content set.
struct StreamContent {
    StreamContentKind kind = StreamContentKind::StationInterior;
};

/// Marker for the station host entity (static RigidBody6DOF — LocalToShip target).
struct StationRoot {};

/// Marker for planetary landing-zone root.
struct LandingZoneRoot {};

// --- API ---------------------------------------------------------------------

void register_systems(flecs::world& world);

/// Fixed-step: rebase check + proximity stream activate/deactivate. No heap.
void fixed_step(flecs::world& world, f32 dt);

/// Rebase relative frame if player distance > threshold. Allocation-free.
void rebase_if_needed(flecs::world& world);

/// Load star_system.cfg into StarSystemData (level load — may use FILE I/O once).
[[nodiscard]] bool load_star_system_config(StarSystemData& out, const char* path);

/// Apply defaults / placeholders if config missing (Sistema-01 / Estacion-Alfa / …).
void star_system_set_placeholders(StarSystemData& out);

/// Spawn universe_test content from StarSystemData (level load only).
/// Returns player ship entity. Sets FloatingOrigin singleton + ControlMode ShipPilot.
/// P3-02: `player_ship_id` (nullptr = ship.player.default) picks the ShipDef the
/// player spawns with — plumbed from `--ship=` via SceneContext.
[[nodiscard]] flecs::entity spawn_universe_test(
    flecs::world& world, const StarSystemData& data, const char* player_ship_id = nullptr);

/// Copy rebase_count from FloatingOrigin (0 if absent).
void fill_world_telemetry(flecs::world& world, u32& rebase_count, bool& found);

}  // namespace csc::game::world
