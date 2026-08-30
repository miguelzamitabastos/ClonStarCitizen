#pragma once

#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"
#include "game/combat/damage.hpp"
#include "game/flight/flight.hpp"

#include <flecs.h>

namespace csc::game::flight {

// --- P3-01: data-driven ship stats -------------------------------------------
// A new ship = a new [[ship]] entry in assets/data/ships.cfg with a unique text
// id. No engine code, no recompile, no per-type `if`/`switch` (see the Fase 3
// architecture rules in STATUS.md). spawn_player_ship / spawn_npc_ship build
// every ship component from the ShipDef looked up by id.

inline constexpr u32 kMaxShipDefs         = 16;
inline constexpr u32 kShipIdBytes         = 48;  ///< e.g. "ship.fighter.hornet_clone"
inline constexpr u32 kShipDisplayBytes    = 32;
inline constexpr u32 kShipRoleBytes       = 16;
inline constexpr u32 kMaxTurretHardpoints = 4;

inline constexpr const char* kShipCatalogPath = "assets/data/ships.cfg";

/// Ship id used when a caller does not name one. Both must exist in ships.cfg
/// or the catalog load fails loudly.
inline constexpr const char* kDefaultPlayerShipId = "ship.player.default";
inline constexpr const char* kDefaultNpcShipId    = "ship.npc.skiff";

/// Immutable per-type ship stats, filled from ships.cfg at load. POD, no heap.
/// Field defaults mirror the Fase 1/2 hardcoded player ship so a missing entry
/// degrades to the previous behaviour instead of a broken ship.
struct ShipDef {
    char id[kShipIdBytes]{};
    char display_name[kShipDisplayBytes]{};
    char role[kShipRoleBytes]{};  ///< informational tag for the P3-02 catalog / hangar UI

    // Rigid body / rendering.
    f32       mass_kg = kShipMassKg;
    glm::vec3 inertia_diag{180000.f, 220000.f, 90000.f};
    f32       hull_radius  = 3.5f;
    f32       render_scale = 2.2f;

    // Thrust authority. main/maneuver/retro scale the default thruster rig;
    // max_torque_nm is the rotation authority the integrator applies directly.
    f32 main_thrust_n     = kMainThrusterForceN;
    f32 maneuver_thrust_n = kManeuverThrusterForceN;
    f32 retro_thrust_n    = kRetroThrusterForceN;
    f32 max_torque_nm     = kMaxTorqueNm;

    // Hull / power / shield.
    f32 hull_hp           = 1000.f;
    f32 power_output      = 450.f;
    f32 power_capacity    = 1200.f;
    f32 shield_capacity   = 600.f;
    f32 shield_regen      = 35.f;
    f32 shield_power_draw = 90.f;

    // P2-02 subsystem banks: max hp per ENG/SHD/WPN/SEN (combat::subsystem_index order).
    f32 subsystem_hp[combat::kSubsystemCount] = {300.f, 250.f, 200.f, 150.f};

    // Weapon hardpoints (the weapon fitted to each is P3-05; P3-01 only places them).
    u32       weapon_mount_count = 1;
    glm::vec3 weapon_mount_offset[kMaxWeaponMounts] = {{0.f, -0.5f, -3.f}, {0.f, 0.f, 0.f}};

    // Turret hardpoints (P2-03): where a turret CAN be mounted on this hull.
    u32       turret_hardpoint_count = 0;
    glm::vec3 turret_hardpoint_offset[kMaxTurretHardpoints]{};

    // Cargo capacity (economy::CargoHold).
    f32 cargo_volume = 40.f;
    f32 cargo_mass   = 100.f;
};

struct ShipCatalog {
    ShipDef items[kMaxShipDefs]{};
    u32     count = 0;
};

/// Sim-time per-ship values the flight integrator reads off the entity instead
/// of the old file-scope tunables (P3-01). Set at spawn from the ShipDef; when
/// absent (e.g. a bare damage target) the integrator falls back to the k*
/// constants.
struct ShipSpec {
    f32 max_torque_nm = kMaxTorqueNm;
};

/// Parse `path` into `out`. Returns false — and logs every problem — on: file
/// missing, zero entries, a duplicate text id, or a malformed / absent id.
/// Cross-catalog reference checks (weapon ids, P3-05) belong to the P3-09
/// content_smoke_test, not here.
[[nodiscard]] bool load_ship_catalog_file(ShipCatalog& out, const char* path);

/// Load ships.cfg into a ShipCatalog singleton on `world`. Loud on failure;
/// returns false so a scene can decide whether to continue with defaults.
bool load_ship_catalog(flecs::world& world);

/// Look up a ship def by text id. nullptr if the id is unknown or the catalog
/// singleton is not present.
[[nodiscard]] const ShipDef* find_ship_def(const ShipCatalog& cat, const char* id);
[[nodiscard]] const ShipDef* find_ship_def(flecs::world& world, const char* id);

// --- P3-03: ship ownership + hangar dealer ----------------------------------

/// Trade-in value = purchase price × this (you lose the rest selling back).
inline constexpr f32 kShipResaleFraction = 0.6f;

/// Ships the player owns + which one is active. In-session only for now — not
/// serialised in the save v1 schema (same debt as ShipSpec; the P1F schema bump
/// that resolves the Fase 2 components resolves this too).
struct ShipOwnership {
    char owned_ids[kMaxShipDefs][kShipIdBytes]{};
    u32  owned_count = 0;
    char active_id[kShipIdBytes]{};
};

/// A hangar kiosk bound to one catalog ship. Interacting with it (P3-03):
///  - not owned  → buy it (if credits suffice) and equip it now;
///  - owned, not active → switch the active ship to it;
///  - owned and active  → sell it back (unless it is the starter ship).
struct ShipDealer {
    char id[kShipIdBytes]{};
    i32  price = 0;
};

/// True if `id` is in the owned list.
[[nodiscard]] bool ship_owns(const ShipOwnership& own, const char* id);

/// Create the ShipOwnership singleton if absent: owns `starter_id` only, with it
/// as the active ship. `starter_id` nullptr/empty → kDefaultPlayerShipId.
void ship_ownership_init(flecs::world& world, const char* starter_id);

/// Overwrite every stat component of the live PlayerShip entity from `def`,
/// keeping its world pose, velocity and cargo contents (hull/shield refill to
/// the new maxima). Warns and does nothing if there is no PlayerShip.
void apply_ship_def_to_player(flecs::world& world, const ShipDef& def);

/// Resolve one ShipDealer interaction against ShipOwnership + PlayerWallet +
/// ShipCatalog singletons. Returns true if ownership / active ship / wallet
/// changed; logs the outcome either way.
[[nodiscard]] bool ship_dealer_interact(flecs::world& world, const ShipDealer& deal);

/// Spawn a hangar kiosk interactable for catalog ship `id` at `price`
/// (character::Interactable + InteractablePrompt, same shape as an economy NPC).
[[nodiscard]] flecs::entity spawn_ship_dealer(
    flecs::world& world, const glm::vec3& position, const char* id, i32 price);

/// Headless P3-03 check (scene gate CSC_HANGAR_SMOKE=1): buy the fighter, assert
/// wallet + live PlayerShip mass changed, sell it back, assert the revert, and
/// assert the starter ship can't be sold. Logs `CSC_HANGAR_SMOKE: PASS|FAIL`.
[[nodiscard]] bool hangar_smoke_test(flecs::world& world);

}  // namespace csc::game::flight
