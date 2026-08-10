#pragma once

#include "engine/core/types.hpp"
#include "engine/input/actions.hpp"
#include "engine/math/glm.hpp"
#include "game/combat/damage.hpp"

#include <flecs.h>

namespace csc::game::flight {

// --- Tunables (light fighter) ------------------------------------------------
inline constexpr f32 kShipMassKg           = 45000.f;
inline constexpr f32 kMainThrusterForceN   = 320000.f; // ~7.1 m/s² main
inline constexpr f32 kManeuverThrusterForceN = 90000.f;
inline constexpr f32 kRetroThrusterForceN  = 200000.f;
inline constexpr f32 kCoupledBrakeForceN   = 280000.f; // stop in a few seconds
inline constexpr f32 kMaxTorqueNm          = 180000.f;
inline constexpr f32 kLookTorqueScale      = 0.004f;   // mouse px → torque input
inline constexpr f32 kRollInputStrength    = 1.f;
inline constexpr u32 kMaxThrusters         = 12;
inline constexpr u32 kMaxWeaponMounts      = 2;
inline constexpr u32 kProjectilePoolSize   = 32;
inline constexpr f32 kProjectileSpeed      = 250.f;
inline constexpr f32 kProjectileLifetime    = 2.5f;
inline constexpr f32 kProjectileRadius     = 0.35f;
inline constexpr f32 kChaseCamDistance     = 14.f;
inline constexpr f32 kChaseCamHeight       = 4.5f;
inline constexpr f32 kChaseCamLookAhead    = 8.f;

/// Local -Z is ship forward (nose); +Y up; +X right.
inline constexpr glm::vec3 kShipForward{0.f, 0.f, -1.f};

// --- Components (POD) --------------------------------------------------------

struct RigidBody6DOF {
    glm::vec3 position{0.f};
    glm::quat orientation{1.f, 0.f, 0.f, 0.f};
    glm::vec3 linear_vel{0.f};
    glm::vec3 angular_vel{0.f}; // rad/s, world space
    f32       mass = kShipMassKg;
    glm::vec3 inertia_diag{1.f, 1.f, 1.f}; // kg·m² per body axis
};

struct ShipHull {
    f32 max_hp = 1000.f;
    f32 hp     = 1000.f;
    f32 radius = 3.f;
};

struct Thruster {
    glm::vec3 relative_pos{0.f}; // body space, from CoM
    glm::vec3 direction{0.f, 0.f, -1.f}; // body space unit
    f32       max_force = 0.f;
};

struct ThrusterSet {
    Thruster thrusters[kMaxThrusters]{};
    f32      activation[kMaxThrusters]{}; // [0,1]
    u32      count = 0;
};

struct PowerPlant {
    f32 output_rate = 400.f; // energy units / s
    f32 capacity    = 1000.f;
    f32 stored      = 1000.f;
    f32 weight_thrusters = 0.45f;
    f32 weight_shields   = 0.35f;
    f32 weight_weapons   = 0.20f;
    /// Fractions available this tick after distribution [0,1].
    f32 frac_thrusters = 1.f;
    f32 frac_shields   = 1.f;
    f32 frac_weapons   = 1.f;
};

struct ShieldGenerator {
    f32 max_capacity = 500.f;
    f32 current      = 500.f;
    f32 regen_rate   = 40.f;  // shield / s when powered
    f32 power_draw   = 80.f;  // energy / s at full regen demand
};

struct WeaponMount {
    glm::vec3 local_offset{0.f, 0.f, -2.f};
    f32       cooldown           = 0.25f;
    f32       cooldown_remaining = 0.f;
    f32       energy_cost        = 15.f;
    f32       heat               = 0.f;
    f32       heat_max           = 100.f;
    f32       damage             = 55.f;
    f32       range              = 400.f;
    bool      hitscan            = false; // false = projectile pool
};

struct WeaponMountSet {
    WeaponMount mounts[kMaxWeaponMounts]{};
    u32         count = 1;
};

struct FlightControl {
    bool      coupled      = true;
    glm::vec3 thrust_input{0.f}; // body: +X right, +Y up, +Z forward (!= local -Z)
    glm::vec3 torque_input{0.f}; // pitch, yaw, roll desired [-1,1]
};

struct PlayerShip {};
struct Destroyed {};

/// Preallocated projectile motion (entity also has Position / InstanceTag when alive).
struct Projectile {
    glm::vec3       velocity{0.f};
    f32             life_remaining = 0.f;
    f32             damage         = 0.f;
    flecs::entity_t source         = 0;
    bool            alive          = false;
};

/// Singleton: indices into pre-spawned projectile entities (level load).
struct ProjectilePool {
    flecs::entity_t entities[kProjectilePoolSize]{};
    u32             count = 0;
};

// --- API ---------------------------------------------------------------------

void register_systems(flecs::world& world);

/// Fixed-timestep entry: input map, power, thrusters, integrate, weapons, damage, cleanup.
void fixed_step(flecs::world& world, f32 dt);

/// Pure: ActionState → FlightControl thrust/torque (+ coupled toggle on just_pressed).
void map_actions_to_flight_control(
    const input::ActionState& in,
    FlightControl&            ctrl,
    f32                       look_torque_scale = kLookTorqueScale);

/// Pure: FlightControl (+ optional brake vel) → thruster activations in [0,1].
void map_flight_control_to_thrusters(
    const FlightControl& ctrl,
    const glm::vec3&     body_linear_vel,
    ThrusterSet&         thrusters);

[[nodiscard]] flecs::entity spawn_player_ship(flecs::world& world, const glm::vec3& position);

[[nodiscard]] flecs::entity spawn_damage_target(
    flecs::world& world, const glm::vec3& position, f32 scale = 4.f);

/// Pre-spawn inactive projectile entities into ProjectilePool singleton (level load).
void spawn_projectile_pool(flecs::world& world);

/// Fill debug overlay fields from the first PlayerShip (if any).
void fill_player_telemetry(
    flecs::world& world,
    f32&          speed,
    f32&          energy,
    f32&          energy_capacity,
    f32&          shield_pct,
    f32&          hull_hp,
    f32&          hull_max_hp,
    bool&         coupled,
    bool&         found);

}  // namespace csc::game::flight
