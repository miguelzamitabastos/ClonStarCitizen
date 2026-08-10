#pragma once

#include "engine/core/types.hpp"
#include "engine/input/actions.hpp"
#include "engine/math/glm.hpp"
#include "game/combat/damage.hpp"

#include <flecs.h>

namespace csc::game::character {

// --- Tunables ----------------------------------------------------------------
inline constexpr f32 kDefaultCapsuleRadius = 0.4f;
inline constexpr f32 kDefaultCapsuleHeight = 1.8f;
inline constexpr f32 kDefaultMoveSpeed     = 5.5f;
inline constexpr f32 kDefaultEvaThrust     = 9.f;   // m/s² suit thrusters
inline constexpr f32 kGravityDefault       = 9.81f;
inline constexpr f32 kJumpSpeed            = 4.5f;
inline constexpr f32 kGroundSnapEpsilon    = 0.02f;
inline constexpr f32 kInteractRange        = 2.5f;
inline constexpr f32 kFpsWeaponRange       = 80.f;
inline constexpr f32 kEyeHeight            = 1.6f;
inline constexpr f32 kThirdPersonDistance  = 3.2f;
inline constexpr f32 kThirdPersonHeight    = 1.2f;
inline constexpr f32 kLookPitchLimit       = 1.55334306f; // ~89 deg
inline constexpr f32 kHealthTargetRadius   = 1.2f;
inline constexpr u32 kMaxHealthTargets     = 32;
inline constexpr u32 kMaxInteractables     = 32;
inline constexpr u32 kMaxGravityZones      = 16;
inline constexpr u32 kInteractEventCapacity = 16;

/// Forward = local -Z (matches ship / camera convention).
inline constexpr glm::vec3 kCharForward{0.f, 0.f, -1.f};

// --- Components (POD) --------------------------------------------------------

/// Kinematic capsule controller — not a RigidBody6DOF.
struct CharacterController {
    f32       radius     = kDefaultCapsuleRadius;
    f32       height     = kDefaultCapsuleHeight;
    glm::vec3 velocity{0.f}; // sim space (world OR ship-local when LocalToShip)
    bool      grounded   = false;
    f32       move_speed = kDefaultMoveSpeed;
    f32       eva_thrust = kDefaultEvaThrust;
    f32       yaw        = 0.f; // radians, horizontal look
    f32       pitch      = 0.f;
    bool      third_person = false;
};

struct Health {
    f32 max_hp = 100.f;
    f32 hp     = 100.f;
};

struct EquippedItem {
    u32 item_id            = 1;
    u32 ammo               = 30;
    u32 ammo_max           = 30;
    f32 damage             = 28.f;
    f32 fire_cooldown      = 0.18f;
    f32 cooldown_remaining = 0.f;
};

// --- P2-05: full inventory -----------------------------------------------------

inline constexpr u32 kMaxInventorySlots = 8;
inline constexpr u32 kItemNameBytes     = 24;

enum class ItemKind : u8 {
    None       = 0,
    Weapon     = 1,
    Consumable = 2,
};

/// Static item catalog entry (fixed table, no runtime registration).
struct ItemDef {
    u32      id   = 0;
    char     name[kItemNameBytes]{};
    ItemKind kind = ItemKind::None;
    // Weapon stats (kind == Weapon)
    f32 damage        = 0.f;
    f32 fire_cooldown = 0.f;
    u32 ammo_max      = 0;
    // Consumable effect (kind == Consumable)
    f32 heal_amount = 0.f;
    u32 ammo_refill = 0;
};

/// Catalog ids: 1=Rifle, 2=Pistol, 3=Medkit, 4=AmmoPack.
inline constexpr u32 kItemRifle    = 1;
inline constexpr u32 kItemPistol   = 2;
inline constexpr u32 kItemMedkit   = 3;
inline constexpr u32 kItemAmmoPack = 4;

/// nullptr when the id is not in the catalog.
[[nodiscard]] const ItemDef* item_find(u32 item_id);

struct InventorySlot {
    u32 item_id = 0;
    u32 qty     = 0;
};

/// Fixed-slot personal inventory (P2-05). Weapon ammo resets on equip (simple).
struct Inventory {
    InventorySlot slots[kMaxInventorySlots]{};
};

/// World item the player can pick up via Interact.
struct ItemPickup {
    u32 item_id = 0;
    u32 qty     = 1;
};

// Pure helpers (no ECS access).
[[nodiscard]] bool inventory_add(Inventory& inv, u32 item_id, u32 qty);
[[nodiscard]] bool inventory_remove(Inventory& inv, u32 item_id, u32 qty);
[[nodiscard]] u32  inventory_count(const Inventory& inv, u32 item_id);

enum class GravityZoneShape : u8 {
    Box    = 0,
    Sphere = 1,
};

/// World-space gravity volume. Characters without LocalToShip sample these each tick.
struct GravityZone {
    glm::vec3         center{0.f};
    glm::vec3         half_extents{1.f, 1.f, 1.f}; // box
    f32               sphere_radius = 1.f;
    glm::vec3         gravity_vector{0.f, -1.f, 0.f}; // direction (normalized in use)
    f32               magnitude     = kGravityDefault;
    GravityZoneShape  shape         = GravityZoneShape::Box;
};

/// ---------------------------------------------------------------------------
/// LocalToShip contract (P1B-08) — authoritative for Fase 2 / Fase 5:
///
/// When this component is present on a character (or hatch prop):
///   1. Simulation of CharacterController.velocity and locomotion / collision
///      runs entirely in the ship's local frame (NOT world space).
///   2. `local_position` / `local_orientation` are the authoritative sim state.
///   3. World `ecs::Position` / `ecs::Orientation` are derived ONLY for
///      render / audio / camera and MUST NOT be fed back into locomotion:
///        world_pos = ship.rb.position + ship.rb.orientation * local_position
///        world_ori = ship.rb.orientation * local_orientation
///   4. Removing LocalToShip (EVA exit) copies the current world pose into
///      Position and clears the attachment; subsequent sim is world-space.
///   5. `ship_entity` is a flecs entity id that must carry RigidBody6DOF.
/// ---------------------------------------------------------------------------
struct LocalToShip {
    flecs::entity_t ship_entity = 0;
    glm::vec3       local_position{0.f};
    glm::quat       local_orientation{1.f, 0.f, 0.f, 0.f};
};

struct Interactable {};

struct InteractablePrompt {
    char label[32]{};
};

/// Hatch: Interact toggles LocalToShip attachment (interior ↔ EVA).
struct ShipHatch {
    flecs::entity_t ship = 0;
};

/// Pilot seat: Interact toggles ControlMode ShipPilot ↔ OnFoot.
struct PilotSeat {
    flecs::entity_t ship = 0;
};

/// P2-03: turret seat — Interact enters TurretControl for `turret`
/// (Interact again while manning exits back to OnFoot).
struct TurretSeat {
    flecs::entity_t turret = 0;
};

struct PlayerCharacter {};
struct CharacterDead {};

struct InteractEvent {
    flecs::entity_t actor  = 0;
    flecs::entity_t target = 0;
};

struct InteractEventQueue {
    InteractEvent events[kInteractEventCapacity]{};
    u32           head  = 0;
    u32           count = 0;

    [[nodiscard]] bool push(const InteractEvent& ev)
    {
        if (count >= kInteractEventCapacity) {
            return false;
        }
        const u32 idx = (head + count) % kInteractEventCapacity;
        events[idx]   = ev;
        ++count;
        return true;
    }

    [[nodiscard]] bool try_pop(InteractEvent& out)
    {
        if (count == 0) {
            return false;
        }
        out  = events[head];
        head = (head + 1u) % kInteractEventCapacity;
        --count;
        return true;
    }
};

/// Singleton: current interactable under the crosshair / short ray (UI/HUD).
struct InteractionFocus {
    flecs::entity_t target = 0;
    char            prompt[32]{};
};

// --- API ---------------------------------------------------------------------

void register_systems(flecs::world& world);

/// Fixed-timestep: gravity sample, locomotion, interact, FPS fire, LocalToShip sync.
void fixed_step(flecs::world& world, f32 dt);

[[nodiscard]] flecs::entity spawn_player_character(
    flecs::world& world, const glm::vec3& world_or_local_pos, flecs::entity_t ship_or_zero);

[[nodiscard]] flecs::entity spawn_health_target(
    flecs::world& world, const glm::vec3& position, f32 scale = 1.5f);

[[nodiscard]] flecs::entity spawn_gravity_zone_box(
    flecs::world&   world,
    const glm::vec3& center,
    const glm::vec3& half_extents,
    const glm::vec3& gravity_dir,
    f32              magnitude,
    const char*      name = "GravityZone");

[[nodiscard]] flecs::entity spawn_ship_hatch(
    flecs::world&    world,
    flecs::entity_t  ship,
    const glm::vec3& local_pos,
    const char*      prompt = "Exit hatch");

[[nodiscard]] flecs::entity spawn_pilot_seat(
    flecs::world&    world,
    flecs::entity_t  ship,
    const glm::vec3& local_pos,
    const char*      prompt = "Pilot seat");

/// P2-03: interactable seat that puts the player in control of `turret`.
[[nodiscard]] flecs::entity spawn_turret_seat(
    flecs::world&    world,
    flecs::entity_t  ship,
    flecs::entity_t  turret,
    const glm::vec3& local_pos,
    const char*      prompt = "Turret seat");

[[nodiscard]] flecs::entity spawn_station_interactable(
    flecs::world& world, const glm::vec3& position, const char* prompt = "Station terminal");

/// P2-05: spawn a pickable item (Interact adds it to the player Inventory).
[[nodiscard]] flecs::entity spawn_item_pickup(
    flecs::world&    world,
    const glm::vec3& position,
    u32              item_id,
    u32              qty);

/// P2-05: HUD snapshot of the player's inventory (fixed buffers, no heap).
struct InventoryTelemetry {
    bool found = false;
    char weapon_name[kItemNameBytes]{};
    u32  medkits    = 0;
    u32  ammo_packs = 0;
};

void fill_inventory_telemetry(flecs::world& world, InventoryTelemetry& out);

/// Fill debug overlay fields from the first PlayerCharacter (if any).
void fill_player_telemetry(
    flecs::world& world,
    f32&          health,
    f32&          health_max,
    u32&          ammo,
    u32&          ammo_max,
    bool&         grounded,
    bool&         eva,
    bool&         found);

}  // namespace csc::game::character
