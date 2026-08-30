#pragma once

#include "engine/core/types.hpp"

#include <flecs.h>

namespace csc::game::flight {

// --- P3-05: data-driven weapon catalog -------------------------------------
// Same pattern as ship_catalog (P3-01): a new gun = a new [[weapon]] entry in
// assets/data/weapons.cfg with a unique text id. Ship hardpoints and turrets
// name the weapon they carry by id; apply_ship_def_components / spawn_turret
// copy the stats onto the live WeaponMount. Nothing per-weapon in engine code.

inline constexpr u32 kMaxWeaponDefs       = 24;
inline constexpr u32 kWeaponIdBytes       = 40;  ///< e.g. "weapon.fixed.repeater"
inline constexpr u32 kWeaponDisplayBytes  = 24;
inline constexpr u32 kWeaponMountTagBytes = 8;   ///< "fixed" | "turret"

inline constexpr const char* kWeaponCatalogPath = "assets/data/weapons.cfg";

/// Weapon fitted when a ship mount / turret names none. Both must exist in
/// weapons.cfg or the reference check fails.
inline constexpr const char* kDefaultShipWeaponId   = "weapon.fixed.repeater";
inline constexpr const char* kDefaultTurretWeaponId = "weapon.turret.repeater";

/// Immutable per-weapon stats, filled from weapons.cfg at load. POD, no heap.
/// Field defaults mirror the Fase 1/2 fixed player gun so a missing catalog
/// degrades to the previous behaviour.
struct WeaponDef {
    char id[kWeaponIdBytes]{};
    char display_name[kWeaponDisplayBytes]{};
    char mount[kWeaponMountTagBytes]{};  ///< informational tag ("fixed" / "turret")

    f32  damage      = 55.f;   ///< per shot / hit
    f32  cooldown    = 0.25f;  ///< seconds between shots
    f32  energy_cost = 15.f;   ///< power units per shot
    f32  heat_max    = 100.f;
    f32  range       = 400.f;
    bool hitscan     = false;  ///< false = projectile pool
};

struct WeaponCatalog {
    WeaponDef items[kMaxWeaponDefs]{};
    u32       count = 0;
};

/// Parse `path` into `out`. Returns false — logging each problem — on: file
/// missing, zero entries, a duplicate text id, or a malformed / absent id.
[[nodiscard]] bool load_weapon_catalog_file(WeaponCatalog& out, const char* path);

/// Load weapons.cfg into a WeaponCatalog singleton on `world`. Loud on failure.
bool load_weapon_catalog(flecs::world& world);

/// Look up a weapon def by text id. nullptr if unknown / catalog absent.
[[nodiscard]] const WeaponDef* find_weapon_def(const WeaponCatalog& cat, const char* id);
[[nodiscard]] const WeaponDef* find_weapon_def(flecs::world& world, const char* id);

/// P3-05 cross-catalog check: every non-empty ShipDef.weapon_id must resolve in
/// the WeaponCatalog. Logs each dangling reference; returns false if any exist.
/// Call after both catalogs load (scene_setup_by_name). No-op (true) if either
/// catalog singleton is missing.
[[nodiscard]] bool validate_ship_weapon_refs(flecs::world& world);

}  // namespace csc::game::flight
