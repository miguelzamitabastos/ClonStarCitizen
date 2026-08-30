#pragma once

#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"

#include <flecs.h>

namespace csc::game::character {

// --- P3-06: data-driven character suits / armour --------------------------
// Same pattern as ship_catalog / weapon_catalog: a new suit = a new [[suit]]
// entry in assets/data/suits.cfg with a unique text id. The player wears one
// (spawn_player_character / a suit-locker kiosk); its stats hook into character
// damage (protection) and EVA locomotion (EVA propellant).

inline constexpr u32 kMaxSuitDefs      = 16;
inline constexpr u32 kSuitIdBytes      = 40;   ///< e.g. "suit.armor.heavy"
inline constexpr u32 kSuitDisplayBytes = 24;

inline constexpr const char* kSuitCatalogPath    = "assets/data/suits.cfg";
inline constexpr const char* kDefaultPlayerSuitId = "suit.flight.standard";

/// Immutable per-suit stats from suits.cfg. Field defaults = the Fase 1B
/// baseline (no protection, generous EVA, normal speed), so a missing catalog
/// degrades safely and the default suit changes nothing.
struct SuitDef {
    char id[kSuitIdBytes]{};
    char display_name[kSuitDisplayBytes]{};

    f32 damage_reduction      = 0.f;    ///< [0, 0.95] of incoming character damage absorbed
    f32 eva_capacity          = 100.f;  ///< EVA propellant reservoir (units)
    f32 eva_drain_per_sec     = 12.f;   ///< consumed while thrusting in EVA
    f32 eva_recharge_per_sec  = 20.f;   ///< regained while grounded / in a gravity zone
    f32 move_speed_mult       = 1.f;    ///< multiplies CharacterController.move_speed on foot
};

struct SuitCatalog {
    SuitDef items[kMaxSuitDefs]{};
    u32     count = 0;
};

/// The suit a character is wearing. Copied from a SuitDef at spawn / at a
/// locker; `eva_charge` is the only field that changes at runtime (drains while
/// EVA-thrusting, recharges under gravity).
struct Suit {
    char id[kSuitIdBytes]{};
    f32  damage_reduction      = 0.f;
    f32  eva_capacity          = 100.f;
    f32  eva_drain_per_sec     = 12.f;
    f32  eva_recharge_per_sec  = 20.f;
    f32  move_speed_mult       = 1.f;
    f32  eva_charge            = 100.f;  ///< runtime
};

/// A suit-locker kiosk. Interact equips its suit on the player — free, it is the
/// player's own locker, not a shop (contrast flight::ShipDealer).
struct SuitLocker {
    char suit_id[kSuitIdBytes]{};
};

[[nodiscard]] bool load_suit_catalog_file(SuitCatalog& out, const char* path);
bool load_suit_catalog(flecs::world& world);

[[nodiscard]] const SuitDef* find_suit_def(const SuitCatalog& cat, const char* id);
[[nodiscard]] const SuitDef* find_suit_def(flecs::world& world, const char* id);

/// Build a Suit (full EVA charge) from a def. nullptr → the SuitDef{} baseline.
[[nodiscard]] Suit suit_from_def(const SuitDef* def);

/// Re-equip the player character's Suit from catalog id `suit_id` (full charge).
/// Returns true if the worn suit changed. Logs the outcome. No-op (warns) if
/// there is no player character or the id is unknown.
bool equip_player_suit(flecs::world& world, const char* suit_id);

/// Spawn a suit-locker interactable for catalog suit `suit_id`.
[[nodiscard]] flecs::entity spawn_suit_locker(
    flecs::world& world, const glm::vec3& position, const char* suit_id);

/// Headless P3-06 check (scene gate CSC_SUIT_SMOKE=1): equip each catalog suit
/// on the player and assert the worn Suit matches the SuitDef (full EVA charge),
/// plus an unknown id is rejected. Logs `CSC_SUIT_SMOKE: PASS|FAIL`.
[[nodiscard]] bool suit_smoke_test(flecs::world& world);

}  // namespace csc::game::character
