#pragma once

#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"
#include "game/economy/economy.hpp"

#include <flecs.h>

namespace csc::game::world {

// --- P4-06: procedurally-placed minable resources -------------------------
// Deterministic asteroid deposits scattered in a system's belt. Mining is NOT a
// parallel system: it pulls a commodity into the ship's economy::CargoHold, the
// same units you would otherwise buy (Fase 1C), just from a different source.

inline constexpr u32 kMaxDeposits       = 24;
inline constexpr f32 kDepositMineRange  = 45.f;   ///< ship within this + nearly stopped = mining
inline constexpr f32 kDepositMaxShipSpeed = 12.f; ///< must be slower than this to mine
inline constexpr f32 kDepositYieldPerSec = 3.5f;

/// A minable asteroid. `commodity_id` indexes economy::CommodityTable.
struct ResourceDeposit {
    u32 commodity_id = 0;
    f32 total        = 0.f;   ///< units the deposit started with
    f32 remaining    = 0.f;   ///< units left (0 = depleted)
    f32 yield_per_sec = kDepositYieldPerSec;
    f32 carry        = 0.f;   ///< runtime: sub-unit accumulator between whole-unit transfers
};

/// Tag: a depleted deposit keeps its entity (marker) but no longer yields.
struct DepositDepleted {};

/// Fill `out` with the deposits for a system, deterministic in `system_seed`.
/// `commodity_count` bounds the commodity ids used. Returns the count written
/// (<= max_out, <= kMaxDeposits).
[[nodiscard]] u32 generate_asteroid_field(
    u64 system_seed, u32 commodity_count, ResourceDeposit* out, glm::vec3* out_pos, u32 max_out);

/// Spawn the system's asteroid field as marker entities carrying ResourceDeposit.
void spawn_asteroid_field(flecs::world& world, u64 system_seed);

/// Pure: transfer up to `yield_per_sec*dt` units (capped by `remaining` and cargo
/// space) of `d.commodity_id` from `d` into `hold`. Returns units mined; marks
/// `d.remaining = 0` when exhausted.
[[nodiscard]] u32 mine_deposit(
    ResourceDeposit& d, economy::CargoHold& hold, const economy::CommodityTable& table, f32 dt);

/// Fixed-step: any PlayerShip parked (slow) inside a deposit's range mines it
/// into its CargoHold. Depleted deposits gain DepositDepleted.
void update_mining(flecs::world& world, f32 dt);

/// P4-06 headless check (scene gate CSC_RESOURCES_SMOKE=1): the field is
/// deterministic, every commodity id is valid, and mine_deposit conserves units
/// (respects remaining + cargo capacity) and depletes. Logs
/// `CSC_RESOURCES_SMOKE: PASS|FAIL`.
[[nodiscard]] bool resources_smoke_test();

}  // namespace csc::game::world
