#include "game/flight/flight.hpp"

#include "engine/ecs/world.hpp"
#include "engine/log/log.hpp"
#include "game/ai/ai.hpp"
#include "game/character/character.hpp"
#include "game/economy/economy.hpp"
#include "game/save/save.hpp"

#include <algorithm>
#include <cmath>

namespace csc::game::flight {
namespace {

constexpr u32 kMaxHullTargets = 64;

struct HullTarget {
    flecs::entity_t id     = 0;
    glm::vec3       center{0.f};
    f32             radius = 0.f;
};

[[nodiscard]] f32 clampf(f32 v, f32 lo, f32 hi)
{
    return std::max(lo, std::min(hi, v));
}

[[nodiscard]] glm::vec3 safe_normalize(const glm::vec3& v, const glm::vec3& fallback)
{
    const f32 len2 = glm::dot(v, v);
    if (len2 < 1e-10f) {
        return fallback;
    }
    return v * (1.f / std::sqrt(len2));
}

void add_thruster(ThrusterSet& set, const glm::vec3& pos, const glm::vec3& dir, f32 force)
{
    if (set.count >= kMaxThrusters) {
        return;
    }
    Thruster& t    = set.thrusters[set.count];
    t.relative_pos = pos;
    t.direction    = safe_normalize(dir, glm::vec3{0.f, 0.f, -1.f});
    t.max_force    = force;
    set.activation[set.count] = 0.f;
    ++set.count;
}

void build_default_thruster_set(ThrusterSet& set)
{
    set = ThrusterSet{};
    add_thruster(set, {0.f, 0.f, 2.5f}, {0.f, 0.f, -1.f}, kMainThrusterForceN);
    add_thruster(set, {0.f, 0.f, -2.5f}, {0.f, 0.f, 1.f}, kRetroThrusterForceN);
    add_thruster(set, {2.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, kManeuverThrusterForceN);
    add_thruster(set, {-2.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, kManeuverThrusterForceN);
    add_thruster(set, {0.f, 2.f, 0.f}, {0.f, -1.f, 0.f}, kManeuverThrusterForceN);
    add_thruster(set, {0.f, -2.f, 0.f}, {0.f, 1.f, 0.f}, kManeuverThrusterForceN);
    add_thruster(set, {1.5f, 0.f, -1.5f}, {0.f, 0.f, 1.f}, kManeuverThrusterForceN * 0.6f);
    add_thruster(set, {-1.5f, 0.f, -1.5f}, {0.f, 0.f, 1.f}, kManeuverThrusterForceN * 0.6f);
    add_thruster(set, {1.5f, 0.f, 1.5f}, {0.f, 0.f, -1.f}, kManeuverThrusterForceN * 0.6f);
    add_thruster(set, {-1.5f, 0.f, 1.5f}, {0.f, 0.f, -1.f}, kManeuverThrusterForceN * 0.6f);
}

[[nodiscard]] glm::vec3 body_to_world(const glm::quat& q, const glm::vec3& v)
{
    return q * v;
}

[[nodiscard]] glm::vec3 world_to_body(const glm::quat& q, const glm::vec3& v)
{
    return glm::conjugate(q) * v;
}

/// Build the DamageEvent for a weapon hit; rolls a hit-location subsystem
/// (P2-02) when the target carries ShipSubsystems.
[[nodiscard]] combat::DamageEvent make_weapon_hit_event(
    flecs::world&   world,
    flecs::entity_t target,
    flecs::entity_t source,
    f32             damage)
{
    combat::DamageEvent ev{};
    ev.target = target;
    ev.source = source;
    ev.amount = damage;

    flecs::entity te = world.entity(target);
    if (te.is_alive() && te.has<ShipSubsystems>()) {
        if (combat::CombatRng* rng = world.try_get_mut<combat::CombatRng>()) {
            ev.subsystem = roll_hit_subsystem(rng->state);
        }
    }
    return ev;
}

u32 collect_hull_targets(flecs::world& world, HullTarget* out, u32 capacity)
{
    u32 n = 0;
    world.each([&](flecs::entity e, const ShipHull& hull, const ecs::Position& p) {
        if (n >= capacity || e.has<Destroyed>()) {
            return;
        }
        out[n].id     = e.id();
        out[n].center = glm::vec3{p.x, p.y, p.z};
        out[n].radius = hull.radius;
        ++n;
    });
    return n;
}

void distribute_power(PowerPlant& plant, f32 dt)
{
    plant.stored = std::min(plant.capacity, plant.stored + plant.output_rate * dt);

    const f32 w_sum = std::max(
        1e-4f,
        plant.weight_thrusters + plant.weight_shields + plant.weight_weapons);

    const f32 budget_t = plant.stored * (plant.weight_thrusters / w_sum);
    const f32 budget_s = plant.stored * (plant.weight_shields / w_sum);
    const f32 budget_w = plant.stored * (plant.weight_weapons / w_sum);

    const f32 demand_t = 120.f * dt;
    const f32 demand_s = 80.f * dt;
    const f32 demand_w = 40.f * dt;

    const f32 give_t = std::min(budget_t, demand_t);
    const f32 give_s = std::min(budget_s, demand_s);
    const f32 give_w = std::min(budget_w, demand_w);

    plant.stored = std::max(0.f, plant.stored - (give_t + give_s + give_w));

    plant.frac_thrusters = (demand_t > 1e-6f) ? clampf(give_t / demand_t, 0.f, 1.f) : 1.f;
    plant.frac_shields   = (demand_s > 1e-6f) ? clampf(give_s / demand_s, 0.f, 1.f) : 1.f;
    plant.frac_weapons   = (demand_w > 1e-6f) ? clampf(give_w / demand_w, 0.f, 1.f) : 1.f;
}

void apply_shield_regen(ShieldGenerator& shield, f32 power_frac, f32 dt)
{
    if (shield.current >= shield.max_capacity) {
        return;
    }
    shield.current =
        std::min(shield.max_capacity, shield.current + shield.regen_rate * power_frac * dt);
}

void accumulate_thruster_wrench(
    const ThrusterSet& thrusters,
    f32                power_frac,
    glm::vec3&         out_force_body,
    glm::vec3&         out_torque_body)
{
    out_force_body  = glm::vec3{0.f};
    out_torque_body = glm::vec3{0.f};
    for (u32 i = 0; i < thrusters.count; ++i) {
        const f32 a = thrusters.activation[i] * power_frac;
        if (a <= 1e-6f) {
            continue;
        }
        const Thruster& t = thrusters.thrusters[i];
        const glm::vec3 f = t.direction * (t.max_force * a);
        out_force_body += f;
        out_torque_body += glm::cross(t.relative_pos, f);
    }
}

void integrate_rigid_body(
    RigidBody6DOF& rb, const glm::vec3& force_world, const glm::vec3& torque_world, f32 dt)
{
    if (rb.mass <= 1e-3f || dt <= 0.f) {
        return;
    }

    const glm::vec3 accel = force_world / rb.mass;
    rb.linear_vel += accel * dt;
    rb.position += rb.linear_vel * dt;

    const glm::vec3 torque_body = world_to_body(rb.orientation, torque_world);
    glm::vec3       ang_body    = world_to_body(rb.orientation, rb.angular_vel);

    const glm::vec3 inv_I{
        1.f / std::max(rb.inertia_diag.x, 1e-3f),
        1.f / std::max(rb.inertia_diag.y, 1e-3f),
        1.f / std::max(rb.inertia_diag.z, 1e-3f),
    };
    ang_body += glm::vec3{
                        torque_body.x * inv_I.x,
                        torque_body.y * inv_I.y,
                        torque_body.z * inv_I.z,
                    }
        * dt;

    rb.angular_vel = body_to_world(rb.orientation, ang_body);

    const f32 ang_speed = glm::length(rb.angular_vel);
    if (ang_speed > 1e-6f) {
        const glm::vec3 axis = rb.angular_vel / ang_speed;
        const glm::quat dq   = glm::angleAxis(ang_speed * dt, axis);
        rb.orientation       = glm::normalize(dq * rb.orientation);
    }
}

void sync_rigid_to_ecs(flecs::entity e, const RigidBody6DOF& rb, bool snapshot_prev)
{
    ecs::Position*          p    = e.try_get_mut<ecs::Position>();
    ecs::PreviousPosition*  prev = e.try_get_mut<ecs::PreviousPosition>();
    ecs::Velocity*          v    = e.try_get_mut<ecs::Velocity>();
    ecs::Orientation*       o    = e.try_get_mut<ecs::Orientation>();

    if (snapshot_prev && p != nullptr && prev != nullptr) {
        prev->x = p->x;
        prev->y = p->y;
        prev->z = p->z;
    }
    if (p != nullptr) {
        p->x = rb.position.x;
        p->y = rb.position.y;
        p->z = rb.position.z;
    }
    if (v != nullptr) {
        v->x = rb.linear_vel.x;
        v->y = rb.linear_vel.y;
        v->z = rb.linear_vel.z;
    }
    if (o != nullptr) {
        o->q = rb.orientation;
    }
}

/// P2-03: camera behind the manned turret, looking along its aim direction.
void update_turret_camera(flecs::world& world)
{
    const ActiveTurretControl* active = world.try_get<ActiveTurretControl>();
    if (active == nullptr || active->turret == 0) {
        return;
    }
    flecs::entity turret = world.entity(active->turret);
    if (!turret.is_alive()) {
        return;
    }
    const ecs::Position*    p = turret.try_get<ecs::Position>();
    const ecs::Orientation* o = turret.try_get<ecs::Orientation>();
    if (p == nullptr || o == nullptr) {
        return;
    }
    const glm::vec3 pos{p->x, p->y, p->z};
    const glm::vec3 aim = o->q * kShipForward;
    const glm::vec3 up  = o->q * glm::vec3{0.f, 1.f, 0.f};

    bool cam_updated = false;
    world.each([&](ecs::Camera3D& cam) {
        if (cam_updated) {
            return;
        }
        cam.eye     = pos - aim * 2.4f + up * 1.1f;
        cam.target  = pos + aim * 14.f;
        cam.up      = up;
        cam_updated = true;
    });
}

void update_chase_camera(flecs::world& world)
{
    const ecs::ControlMode* mode = world.try_get<ecs::ControlMode>();
    if (mode == nullptr) {
        return;
    }
    if (mode->mode == ecs::ControlModeKind::TurretControl) {
        update_turret_camera(world);
        return;
    }
    if (mode->mode != ecs::ControlModeKind::ShipPilot) {
        return;
    }

    const ecs::FrameInterpolation* fi = world.try_get<ecs::FrameInterpolation>();
    const f32 alpha = (fi != nullptr) ? fi->alpha : 1.f;

    bool      ship_found = false;
    glm::vec3 ship_pos{0.f};
    glm::quat ship_ori{1.f, 0.f, 0.f, 0.f};

    world.each([&](flecs::entity e, const RigidBody6DOF& rb) {
        if (ship_found || !e.has<PlayerShip>() || e.has<Destroyed>()) {
            return;
        }
        const ecs::Position*         p    = e.try_get<ecs::Position>();
        const ecs::PreviousPosition* prev = e.try_get<ecs::PreviousPosition>();
        if (p != nullptr && prev != nullptr) {
            const ecs::Position lerped = ecs::lerp_position(*prev, *p, alpha);
            ship_pos = glm::vec3{lerped.x, lerped.y, lerped.z};
        } else {
            ship_pos = rb.position;
        }
        ship_ori   = rb.orientation;
        ship_found = true;
    });

    if (!ship_found) {
        return;
    }

    const glm::vec3 forward = body_to_world(ship_ori, kShipForward);
    const glm::vec3 up      = body_to_world(ship_ori, glm::vec3{0.f, 1.f, 0.f});

    bool cam_updated = false;
    world.each([&](ecs::Camera3D& cam) {
        if (cam_updated) {
            return;
        }
        cam.eye     = ship_pos - forward * kChaseCamDistance + up * kChaseCamHeight;
        cam.target  = ship_pos + forward * kChaseCamLookAhead;
        cam.up      = up;
        cam_updated = true;
    });
}

[[nodiscard]] flecs::entity_t raycast_hull(
    const HullTarget* targets,
    u32               target_count,
    flecs::entity_t   self,
    const glm::vec3&  origin,
    const glm::vec3&  dir,
    f32               range)
{
    f32             best_t      = range;
    flecs::entity_t best_target = 0;
    for (u32 i = 0; i < target_count; ++i) {
        if (targets[i].id == self) {
            continue;
        }
        const glm::vec3 oc   = origin - targets[i].center;
        const f32       b    = glm::dot(oc, dir);
        const f32       c    = glm::dot(oc, oc) - targets[i].radius * targets[i].radius;
        const f32       disc = b * b - c;
        if (disc < 0.f) {
            continue;
        }
        const f32 t = -b - std::sqrt(disc);
        if (t > 0.f && t < best_t) {
            best_t      = t;
            best_target = targets[i].id;
        }
    }
    return best_target;
}

/// Cooldown / heat / energy gate. On success the cost is consumed.
[[nodiscard]] bool weapon_try_consume(PowerPlant& plant, WeaponMount& w)
{
    if (w.cooldown_remaining > 0.f || w.heat >= w.heat_max) {
        return false;
    }
    if (plant.frac_weapons < 0.15f || plant.stored < w.energy_cost) {
        return false;
    }
    plant.stored         = std::max(0.f, plant.stored - w.energy_cost);
    w.cooldown_remaining = w.cooldown;
    w.heat               = std::min(w.heat_max, w.heat + 12.f);
    return true;
}

/// Shared shot emission (fixed mounts P1A + turrets P2-03): hitscan or pool projectile.
void weapon_emit(
    flecs::world&             world,
    flecs::entity_t           source,
    const glm::vec3&          muzzle,
    const glm::vec3&          dir,
    const WeaponMount&        w,
    const HullTarget*         targets,
    u32                       target_count,
    combat::DamageEventQueue* dmg_q,
    ProjectilePool*           pool)
{
    if (w.hitscan) {
        const flecs::entity_t hit =
            raycast_hull(targets, target_count, source, muzzle, dir, w.range);
        if (hit != 0 && dmg_q != nullptr) {
            (void)dmg_q->push(make_weapon_hit_event(world, hit, source, w.damage));
        }
        return;
    }

    if (pool == nullptr) {
        return;
    }
    for (u32 pi = 0; pi < pool->count; ++pi) {
        if (pool->entities[pi] == 0) {
            continue;
        }
        flecs::entity pe = world.entity(pool->entities[pi]);
        if (!pe.is_alive()) {
            continue;
        }
        Projectile* proj = pe.try_get_mut<Projectile>();
        if (proj == nullptr || proj->alive) {
            continue;
        }
        proj->alive          = true;
        proj->velocity       = dir * kProjectileSpeed;
        proj->life_remaining = kProjectileLifetime;
        proj->damage         = w.damage;
        proj->source         = source;

        const ecs::Position pos{muzzle.x, muzzle.y, muzzle.z};
        pe.set<ecs::Position>(pos);
        pe.set<ecs::PreviousPosition>({pos.x, pos.y, pos.z});
        pe.set<ecs::Velocity>({proj->velocity.x, proj->velocity.y, proj->velocity.z});
        pe.set<ecs::Scale>({kProjectileRadius * 2.f});
        pe.add<ecs::InstanceTag>();
        break;
    }
}

void try_fire_mount(
    flecs::world&              world,
    flecs::entity              ship,
    RigidBody6DOF&             rb,
    PowerPlant&                plant,
    WeaponMount&               w,
    const HullTarget*          targets,
    u32                        target_count,
    combat::DamageEventQueue*  dmg_q,
    ProjectilePool*            pool)
{
    if (!weapon_try_consume(plant, w)) {
        return;
    }
    const glm::vec3 muzzle = rb.position + body_to_world(rb.orientation, w.local_offset);
    const glm::vec3 dir    = body_to_world(rb.orientation, kShipForward);
    weapon_emit(world, ship.id(), muzzle, dir, w, targets, target_count, dmg_q, pool);
}

void step_projectiles(
    flecs::world& world, f32 dt, const HullTarget* targets, u32 target_count)
{
    combat::DamageEventQueue* dmg_q = world.try_get_mut<combat::DamageEventQueue>();

    // Defer InstanceTag removals to avoid structural changes mid-iteration.
    flecs::entity_t to_deactivate[kProjectilePoolSize]{};
    u32             deactivate_count = 0;

    world.each([&](flecs::entity e, Projectile& proj, ecs::Position& p,
                   ecs::PreviousPosition& prev, ecs::Velocity& vel) {
        if (!proj.alive) {
            return;
        }
        prev.x = p.x;
        prev.y = p.y;
        prev.z = p.z;

        p.x += proj.velocity.x * dt;
        p.y += proj.velocity.y * dt;
        p.z += proj.velocity.z * dt;
        vel.x = proj.velocity.x;
        vel.y = proj.velocity.y;
        vel.z = proj.velocity.z;

        proj.life_remaining -= dt;

        bool            hit        = false;
        flecs::entity_t hit_target = 0;
        const glm::vec3 pos{p.x, p.y, p.z};

        for (u32 i = 0; i < target_count; ++i) {
            if (targets[i].id == proj.source) {
                continue;
            }
            const f32       r = targets[i].radius + kProjectileRadius;
            const glm::vec3 d = pos - targets[i].center;
            if (glm::dot(d, d) <= r * r) {
                hit        = true;
                hit_target = targets[i].id;
                break;
            }
        }

        if (hit && dmg_q != nullptr) {
            (void)dmg_q->push(
                make_weapon_hit_event(world, hit_target, proj.source, proj.damage));
        }

        if (hit || proj.life_remaining <= 0.f) {
            proj.alive          = false;
            proj.life_remaining = 0.f;
            proj.velocity       = glm::vec3{0.f};
            vel                 = {};
            if (deactivate_count < kProjectilePoolSize) {
                to_deactivate[deactivate_count++] = e.id();
            }
        }
    });

    for (u32 i = 0; i < deactivate_count; ++i) {
        if (to_deactivate[i] == 0) {
            continue;
        }
        flecs::entity e = world.entity(to_deactivate[i]);
        if (e.is_alive() && e.has<ecs::InstanceTag>()) {
            e.remove<ecs::InstanceTag>();
        }
    }
}

void apply_damage_events(flecs::world& world)
{
    combat::DamageEventQueue* q = world.try_get_mut<combat::DamageEventQueue>();
    if (q == nullptr) {
        return;
    }

    flecs::entity_t newly_destroyed[kMaxHullTargets]{};
    u32             destroyed_count = 0;
    flecs::entity_t newly_dead[character::kMaxHealthTargets]{};
    u32             dead_count = 0;

    combat::DamageEvent ev{};
    while (q->try_pop(ev)) {
        if (ev.target == 0) {
            continue;
        }
        flecs::entity target = world.entity(ev.target);
        if (!target.is_alive()) {
            continue;
        }

        // Shared DamageEvent → ShipHull (P1A) and/or Health (P1B).
        f32 remaining = ev.amount;

        if (!target.has<Destroyed>()) {
            if (ShieldGenerator* shield = target.try_get_mut<ShieldGenerator>()) {
                const f32 absorbed = std::min(shield->current, remaining);
                shield->current -= absorbed;
                remaining -= absorbed;
            }
            if (remaining > 0.f) {
                // P2-02: subsystem-addressed hits split between the subsystem
                // bank and hull bleed-through; destroyed banks pass all to hull.
                f32 to_hull = remaining;
                if (ev.subsystem != combat::Subsystem::None) {
                    if (ShipSubsystems* subs = target.try_get_mut<ShipSubsystems>()) {
                        SubsystemHealth& bank =
                            subs->items[combat::subsystem_index(ev.subsystem)];
                        if (bank.hp > 0.f) {
                            const f32 to_subsystem =
                                remaining * (1.f - kSubsystemHullBleed);
                            bank.hp = std::max(0.f, bank.hp - to_subsystem);
                            to_hull = remaining * kSubsystemHullBleed;
                        }
                    }
                }
                if (ShipHull* hull = target.try_get_mut<ShipHull>()) {
                    hull->hp = std::max(0.f, hull->hp - to_hull);
                    if (hull->hp <= 0.f && destroyed_count < kMaxHullTargets) {
                        newly_destroyed[destroyed_count++] = target.id();
                    }
                    remaining = 0.f;
                }
            }
        }

        if (remaining > 0.f && !target.has<character::CharacterDead>()) {
            if (character::Health* hp = target.try_get_mut<character::Health>()) {
                // P2-07: zone multiplier (Head/Limb/Torso) — meaningless for
                // ships, only ever applied here against character::Health.
                const f32 zoned = remaining * combat::body_zone_damage_multiplier(ev.zone);
                hp->hp = std::max(0.f, hp->hp - zoned);
                if (hp->hp <= 0.f && dead_count < character::kMaxHealthTargets) {
                    newly_dead[dead_count++] = target.id();
                }
            }
        }
    }

    for (u32 i = 0; i < destroyed_count; ++i) {
        if (newly_destroyed[i] == 0) {
            continue;
        }
        flecs::entity e = world.entity(newly_destroyed[i]);
        if (e.is_alive() && !e.has<Destroyed>()) {
            e.add<Destroyed>();
        }
    }
    for (u32 i = 0; i < dead_count; ++i) {
        if (newly_dead[i] == 0) {
            continue;
        }
        flecs::entity e = world.entity(newly_dead[i]);
        if (e.is_alive() && !e.has<character::CharacterDead>()) {
            e.add<character::CharacterDead>();
            // P2-07: only the player respawns — NPCs stay dead.
            if (e.has<character::PlayerCharacter>()) {
                e.set<character::RespawnTimer>({character::kRespawnDelaySeconds});
            }
            // P2-10: report this death back to the Combat/Escort mission
            // that spawned it (if any) — a kill, or a mission failure if the
            // protected NPC just died. is_active() guards a slot already
            // released this same tick (e.g. the escort target and a hostile
            // both died together) instead of touching stale data.
            if (const economy::MissionLink* link = e.try_get<economy::MissionLink>()) {
                if (economy::MissionActivePool* missions =
                        world.try_get_mut<economy::MissionActivePool>();
                    missions != nullptr && link->slot < economy::kMaxActiveMissions
                    && missions->pool.is_active(link->slot)) {
                    economy::MissionActive& m = missions->pool.slots[link->slot];
                    if (link->is_escort_target) {
                        log::log_info(
                            log::LogCategory::Game,
                            "Mission failed: escort target lost (slot=%u)",
                            link->slot);
                        missions->pool.release(link->slot);
                    } else {
                        m.kills_confirmed =
                            (m.kills_confirmed + 1u < m.qty_required)
                                ? m.kills_confirmed + 1u
                                : m.qty_required;
                        log::log_info(
                            log::LogCategory::Game,
                            "Mission kill %u/%u (slot=%u)",
                            m.kills_confirmed,
                            m.qty_required,
                            link->slot);
                    }
                }
            }
        }
    }
}

[[nodiscard]] f32 move_toward(f32 current, f32 desired, f32 max_delta)
{
    const f32 delta = desired - current;
    if (delta > max_delta) {
        return current + max_delta;
    }
    if (delta < -max_delta) {
        return current - max_delta;
    }
    return desired;
}

[[nodiscard]] glm::vec3 target_world_pos(flecs::entity e)
{
    if (const RigidBody6DOF* rb = e.try_get<RigidBody6DOF>()) {
        return rb->position;
    }
    if (const ecs::Position* p = e.try_get<ecs::Position>()) {
        return glm::vec3{p->x, p->y, p->z};
    }
    return glm::vec3{0.f};
}

constexpr u32 kMaxTurrets = 16;

/// Turrets whose assigned TurretGunner crew member is alive (one pass, no nesting).
u32 collect_manned_turrets(flecs::world& world, flecs::entity_t* out, u32 capacity)
{
    u32 n = 0;
    world.each([&](flecs::entity e, const CrewMember& crew) {
        if (n >= capacity || crew.role != CrewRole::TurretGunner || crew.turret == 0) {
            return;
        }
        if (e.has<character::CharacterDead>()) {
            return;
        }
        out[n++] = crew.turret;
    });
    return n;
}

/// P2-03: turret slew + world pose sync + fire. Decision comes from the SHARED
/// AI (ai::AiAgent state/target) or the player (ActiveTurretControl); this pass
/// is actuation only.
void step_turrets(
    flecs::world&             world,
    f32                       dt,
    const HullTarget*         targets,
    u32                       target_count,
    combat::DamageEventQueue* dmg_q,
    ProjectilePool*           pool,
    const input::ActionState* actions,
    f32                       look_sens)
{
    const ecs::ControlMode* mode = world.try_get<ecs::ControlMode>();
    const bool              player_turret_mode =
        mode != nullptr && mode->mode == ecs::ControlModeKind::TurretControl;

    ActiveTurretControl active{};
    if (const ActiveTurretControl* a = world.try_get<ActiveTurretControl>()) {
        active = *a;
    }

    flecs::entity_t manned[kMaxTurrets]{};
    const u32       manned_count = collect_manned_turrets(world, manned, kMaxTurrets);

    world.each([&](flecs::entity e, TurretMount& t) {
        if (t.ship == 0) {
            return;
        }
        flecs::entity ship = world.entity(t.ship);
        if (!ship.is_alive() || ship.has<Destroyed>()) {
            return;
        }
        const RigidBody6DOF* rb = ship.try_get<RigidBody6DOF>();
        if (rb == nullptr) {
            return;
        }

        if (t.weapon.cooldown_remaining > 0.f) {
            t.weapon.cooldown_remaining = std::max(0.f, t.weapon.cooldown_remaining - dt);
        }
        t.weapon.heat = std::max(0.f, t.weapon.heat - 15.f * dt);

        const glm::quat rest_world = rb->orientation * t.local_rest;
        const glm::vec3 mount_pos  = rb->position + rb->orientation * t.local_offset;

        bool       fire_desired = false;
        const bool player_drive = player_turret_mode && active.turret == e.id();

        if (player_drive && actions != nullptr) {
            t.yaw -= actions->axes[static_cast<u16>(input::ActionAxis::LookX)] * look_sens;
            t.pitch -=
                actions->axes[static_cast<u16>(input::ActionAxis::LookY)] * look_sens;
            fire_desired =
                actions->pressed[static_cast<u16>(input::Action::Fire)];
        } else if (const ai::AiAgent* agent = e.try_get<ai::AiAgent>()) {
            if (agent->state == ai::AiState::Combat && agent->target != 0) {
                flecs::entity te = world.entity(agent->target);
                if (te.is_alive()) {
                    const glm::vec3 to_target = target_world_pos(te) - mount_pos;
                    const f32       dist      = glm::length(to_target);
                    if (dist > 0.5f) {
                        const glm::vec3 dir_rest =
                            glm::conjugate(rest_world) * (to_target / dist);
                        const f32 desired_yaw = std::atan2(-dir_rest.x, -dir_rest.z);
                        const f32 desired_pitch =
                            std::asin(clampf(dir_rest.y, -1.f, 1.f));
                        t.yaw = move_toward(t.yaw, desired_yaw, t.turn_rate * dt);
                        t.pitch =
                            move_toward(t.pitch, desired_pitch, t.turn_rate * dt);
                        fire_desired = dist <= t.weapon.range;
                    }
                }
            }
        }

        t.yaw   = clampf(t.yaw, -t.yaw_limit, t.yaw_limit);
        t.pitch = clampf(t.pitch, -t.pitch_limit, t.pitch_limit);

        const glm::quat aim_local =
            glm::angleAxis(t.yaw, glm::vec3{0.f, 1.f, 0.f})
            * glm::angleAxis(t.pitch, glm::vec3{1.f, 0.f, 0.f});
        const glm::quat aim_world = rest_world * aim_local;
        const glm::vec3 aim_dir   = aim_world * kShipForward;

        // World pose sync (render/camera) — turret is rigid to the ship.
        if (ecs::Position* p = e.try_get_mut<ecs::Position>()) {
            if (ecs::PreviousPosition* prev = e.try_get_mut<ecs::PreviousPosition>()) {
                prev->x = p->x;
                prev->y = p->y;
                prev->z = p->z;
            }
            p->x = mount_pos.x;
            p->y = mount_pos.y;
            p->z = mount_pos.z;
        }
        if (ecs::Orientation* o = e.try_get_mut<ecs::Orientation>()) {
            o->q = aim_world;
        }

        if (!fire_desired) {
            return;
        }

        // AI fire needs alignment inside the cone; the player aims freely.
        if (!player_drive) {
            const ai::AiAgent* agent = e.try_get<ai::AiAgent>();
            if (agent == nullptr || agent->target == 0) {
                return;
            }
            flecs::entity te = world.entity(agent->target);
            if (!te.is_alive()) {
                return;
            }
            const glm::vec3 to_target = target_world_pos(te) - mount_pos;
            const f32       dist      = glm::length(to_target);
            if (dist < 0.5f
                || glm::dot(aim_dir, to_target / dist) < kTurretAimConeCos) {
                return;
            }
            // Crewed turrets only fire with a living gunner (P2-01).
            if (t.requires_gunner) {
                bool has_gunner = false;
                for (u32 i = 0; i < manned_count; ++i) {
                    if (manned[i] == e.id()) {
                        has_gunner = true;
                        break;
                    }
                }
                if (!has_gunner) {
                    return;
                }
            }
        }

        // P2-02: host ship Weapons bank gates every turret on board.
        if (const ShipSubsystems* subs = ship.try_get<ShipSubsystems>()) {
            if (!subsystem_operational(*subs, combat::Subsystem::Weapons)) {
                return;
            }
        }

        PowerPlant* plant = ship.try_get_mut<PowerPlant>();
        if (plant == nullptr || !weapon_try_consume(*plant, t.weapon)) {
            return;
        }
        const glm::vec3 muzzle = mount_pos + aim_dir * kTurretMuzzleLen;
        weapon_emit(
            world, t.ship, muzzle, aim_dir, t.weapon, targets, target_count, dmg_q, pool);
    });
}

/// P2-01: engineers repair the most damaged subsystem bank of their ship.
/// Turret gunners are pure decision consumers — their actuation is in P2-03.
void step_crew(flecs::world& world, f32 dt)
{
    world.each([&](flecs::entity e, const CrewMember& crew) {
        if (crew.role != CrewRole::Engineer || crew.ship == 0) {
            return;
        }
        if (e.has<character::CharacterDead>()) {
            return;
        }
        flecs::entity ship = world.entity(crew.ship);
        if (!ship.is_alive() || ship.has<Destroyed>()) {
            return;
        }
        ShipSubsystems* subs = ship.try_get_mut<ShipSubsystems>();
        if (subs == nullptr) {
            return;
        }

        // Most damaged bank first (lowest health fraction, below 100%).
        u32 worst      = combat::kSubsystemCount;
        f32 worst_frac = 1.f;
        for (u32 i = 0; i < combat::kSubsystemCount; ++i) {
            const SubsystemHealth& h = subs->items[i];
            if (h.max_hp <= 1e-3f) {
                continue;
            }
            const f32 frac = h.hp / h.max_hp;
            if (frac < worst_frac) {
                worst_frac = frac;
                worst      = i;
            }
        }
        if (worst >= combat::kSubsystemCount) {
            return;
        }
        SubsystemHealth& bank = subs->items[worst];
        bank.hp = std::min(bank.max_hp, bank.hp + crew.repair_rate * dt);
    });
}

void cleanup_destroyed(flecs::world& world)
{
    flecs::entity_t strip[kMaxHullTargets]{};
    u32             strip_count = 0;

    world.each([&](flecs::entity e, RigidBody6DOF& rb) {
        if (!e.has<Destroyed>()) {
            return;
        }
        rb.linear_vel  = glm::vec3{0.f};
        rb.angular_vel = glm::vec3{0.f};
        if (ecs::Velocity* v = e.try_get_mut<ecs::Velocity>()) {
            *v = {};
        }
        if (ThrusterSet* ts = e.try_get_mut<ThrusterSet>()) {
            for (u32 i = 0; i < ts->count; ++i) {
                ts->activation[i] = 0.f;
            }
        }
        if (e.has<ecs::InstanceTag>() && strip_count < kMaxHullTargets) {
            strip[strip_count++] = e.id();
        }
    });

    // Also strip InstanceTag from destroyed hulls without RigidBody (static targets).
    world.each([&](flecs::entity e, ShipHull&) {
        if (!e.has<Destroyed>()) {
            return;
        }
        if (e.has<ecs::InstanceTag>() && strip_count < kMaxHullTargets) {
            // Avoid duplicates
            bool dup = false;
            for (u32 i = 0; i < strip_count; ++i) {
                if (strip[i] == e.id()) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                strip[strip_count++] = e.id();
            }
        }
    });

    for (u32 i = 0; i < strip_count; ++i) {
        if (strip[i] == 0) {
            continue;
        }
        flecs::entity e = world.entity(strip[i]);
        if (e.is_alive() && e.has<ecs::InstanceTag>()) {
            e.remove<ecs::InstanceTag>();
        }
    }
}

}  // namespace

f32 sample_atmosphere(flecs::world& world, const glm::vec3& pos, glm::vec3& out_gravity_accel)
{
    f32 best_density  = 0.f;
    out_gravity_accel = glm::vec3{0.f};

    world.each([&](const AtmosphereVolume& atmo) {
        const glm::vec3 to_center = atmo.center - pos;
        const f32       dist      = glm::length(to_center);
        if (dist >= atmo.outer_radius || atmo.outer_radius <= atmo.inner_radius) {
            return;
        }
        const f32 t = (dist <= atmo.inner_radius)
            ? 1.f
            : 1.f - (dist - atmo.inner_radius) / (atmo.outer_radius - atmo.inner_radius);
        const f32 density = atmo.sea_level_density * t;
        if (density > best_density) {
            best_density = density;
            out_gravity_accel = (dist > 1e-3f)
                ? (to_center / dist) * (atmo.surface_gravity * t)
                : glm::vec3{0.f};
        }
    });
    return best_density;
}

f32 subsystem_efficiency(const ShipSubsystems& subs, combat::Subsystem s)
{
    if (s == combat::Subsystem::None) {
        return 1.f;
    }
    const SubsystemHealth& h = subs.items[combat::subsystem_index(s)];
    return (h.max_hp > 1e-3f) ? clampf(h.hp / h.max_hp, 0.f, 1.f) : 0.f;
}

bool subsystem_operational(const ShipSubsystems& subs, combat::Subsystem s)
{
    if (s == combat::Subsystem::None) {
        return true;
    }
    return subs.items[combat::subsystem_index(s)].hp > 0.f;
}

combat::Subsystem roll_hit_subsystem(u32& rng_state)
{
    const u32 roll = combat::rng_next(rng_state) >> 8; // drop low-quality LCG bits
    const f32 unit = static_cast<f32>(roll % 10000u) / 10000.f;
    if (unit >= kSubsystemHitChance) {
        return combat::Subsystem::None;
    }
    const u32 pick = (combat::rng_next(rng_state) >> 8) % combat::kSubsystemCount;
    return static_cast<combat::Subsystem>(pick + 1u);
}

void map_actions_to_flight_control(
    const input::ActionState& in, FlightControl& ctrl, f32 look_torque_scale)
{
    if (in.just_pressed[static_cast<u16>(input::Action::ToggleCoupled)]) {
        ctrl.coupled = !ctrl.coupled;
    }

    ctrl.thrust_input = glm::vec3{
        in.axes[static_cast<u16>(input::ActionAxis::MoveX)],
        in.axes[static_cast<u16>(input::ActionAxis::MoveY)],
        in.axes[static_cast<u16>(input::ActionAxis::MoveZ)],
    };

    if (in.pressed[static_cast<u16>(input::Action::Thrust)]) {
        ctrl.thrust_input *= 1.35f;
    }

    const f32 look_x = in.axes[static_cast<u16>(input::ActionAxis::LookX)];
    const f32 look_y = in.axes[static_cast<u16>(input::ActionAxis::LookY)];
    const f32 roll =
        (in.pressed[static_cast<u16>(input::Action::RollRight)] ? kRollInputStrength : 0.f)
        - (in.pressed[static_cast<u16>(input::Action::RollLeft)] ? kRollInputStrength : 0.f);

    ctrl.torque_input = glm::vec3{
        clampf(-look_y * look_torque_scale, -1.f, 1.f),
        clampf(-look_x * look_torque_scale, -1.f, 1.f),
        clampf(roll, -1.f, 1.f),
    };
}

void map_flight_control_to_thrusters(
    const FlightControl& ctrl, const glm::vec3& body_linear_vel, ThrusterSet& thrusters)
{
    for (u32 i = 0; i < thrusters.count; ++i) {
        thrusters.activation[i] = 0.f;
    }

    glm::vec3 desired_lin{
        ctrl.thrust_input.x,
        ctrl.thrust_input.y,
        -ctrl.thrust_input.z,
    };

    if (ctrl.coupled) {
        const f32 input_mag2 = glm::dot(ctrl.thrust_input, ctrl.thrust_input);
        if (input_mag2 < 1e-4f) {
            const f32 speed2 = glm::dot(body_linear_vel, body_linear_vel);
            if (speed2 > 0.25f) {
                desired_lin += -safe_normalize(body_linear_vel, glm::vec3{0.f});
            }
        }
    }

    const f32 desired_len = glm::length(desired_lin);
    if (desired_len > 1.f) {
        desired_lin /= desired_len;
    }

    for (u32 i = 0; i < thrusters.count; ++i) {
        const f32 align = glm::dot(thrusters.thrusters[i].direction, desired_lin);
        if (align > 0.05f) {
            thrusters.activation[i] = std::max(thrusters.activation[i], clampf(align, 0.f, 1.f));
        }
    }

    const glm::vec3 desired_torque = ctrl.torque_input;
    const f32       torque_mag     = glm::length(desired_torque);
    if (torque_mag > 1e-4f) {
        const glm::vec3 torque_n = safe_normalize(desired_torque, glm::vec3{0.f});
        for (u32 i = 0; i < thrusters.count; ++i) {
            const Thruster& t          = thrusters.thrusters[i];
            const glm::vec3 torque_dir = glm::cross(t.relative_pos, t.direction);
            const f32       align =
                glm::dot(safe_normalize(torque_dir, glm::vec3{0.f}), torque_n);
            if (align > 0.1f) {
                thrusters.activation[i] = std::max(
                    thrusters.activation[i], clampf(align * torque_mag, 0.f, 1.f));
            }
        }
    }

    if (ctrl.coupled) {
        const f32 input_mag2 = glm::dot(ctrl.thrust_input, ctrl.thrust_input);
        const f32 speed2     = glm::dot(body_linear_vel, body_linear_vel);
        if (input_mag2 < 1e-4f && speed2 > 0.25f) {
            const f32 brake_scale =
                clampf(kCoupledBrakeForceN / kMainThrusterForceN, 0.5f, 1.5f);
            for (u32 i = 0; i < thrusters.count; ++i) {
                thrusters.activation[i] =
                    clampf(thrusters.activation[i] * brake_scale, 0.f, 1.f);
            }
        }
    }
}

void fixed_step(flecs::world& world, f32 dt)
{
    if (dt <= 0.f) {
        return;
    }

    const ecs::InputActions* actions = world.try_get<ecs::InputActions>();
    const ecs::CameraControlParams* cam_params = world.try_get<ecs::CameraControlParams>();
    const f32 look_scale = (cam_params != nullptr)
        ? (cam_params->mouse_sensitivity * 800.f)
        : kLookTorqueScale;

    const ecs::ControlMode* control_mode = world.try_get<ecs::ControlMode>();
    const bool ship_pilot =
        control_mode != nullptr && control_mode->mode == ecs::ControlModeKind::ShipPilot;

    if (actions != nullptr) {
        world.each([&](flecs::entity e, FlightControl& ctrl) {
            if (!e.has<PlayerShip>()) {
                return;
            }
            if (e.has<Destroyed>() || !ship_pilot) {
                ctrl.thrust_input = {};
                ctrl.torque_input = {};
                return;
            }
            map_actions_to_flight_control(actions->state, ctrl, look_scale);
        });
    }

    // Physics + power for ships.
    world.each([&](flecs::entity e, RigidBody6DOF& rb, ThrusterSet& thrusters,
                   FlightControl& ctrl, PowerPlant& plant) {
        if (e.has<Destroyed>()) {
            return;
        }

        distribute_power(plant, dt);

        // P2-02: subsystem damage degrades the pillar it belongs to.
        const ShipSubsystems* subs = e.try_get<ShipSubsystems>();
        const f32             eng_eff = (subs != nullptr)
            ? subsystem_efficiency(*subs, combat::Subsystem::Engines)
            : 1.f;
        const f32 shd_eff = (subs != nullptr)
            ? subsystem_efficiency(*subs, combat::Subsystem::Shields)
            : 1.f;

        if (ShieldGenerator* shield = e.try_get_mut<ShieldGenerator>()) {
            if (shd_eff <= 0.f) {
                shield->current = 0.f; // generator destroyed — no absorption at all
            } else {
                apply_shield_regen(*shield, plant.frac_shields * shd_eff, dt);
                plant.stored = std::max(
                    0.f,
                    plant.stored - shield->power_draw * plant.frac_shields * dt * 0.25f);
            }
        }

        // P2-04: atmósfera (arrastre + sustentación + gravedad) vs vacío.
        glm::vec3 atmo_gravity{};
        const f32 atmo_density = sample_atmosphere(world, rb.position, atmo_gravity);
        glm::vec3 aero_force_world{0.f};
        if (atmo_density > 0.f) {
            AeroProfile aero{};
            if (const AeroProfile* ap = e.try_get<AeroProfile>()) {
                aero = *ap;
            }
            const glm::vec3 v     = rb.linear_vel;
            const f32       speed = glm::length(v);
            if (speed > 0.5f) {
                // Quadratic drag against velocity.
                aero_force_world += v * (-0.5f * atmo_density * speed * aero.drag_area);
                // Simplified lift: forward airspeed² along body-up.
                const glm::vec3 fwd = body_to_world(rb.orientation, kShipForward);
                const glm::vec3 up  = body_to_world(rb.orientation, glm::vec3{0.f, 1.f, 0.f});
                const f32       vf  = glm::dot(v, fwd);
                if (vf > 0.f) {
                    aero_force_world += up * (0.5f * atmo_density * vf * vf * aero.lift_area);
                }
            }
            aero_force_world += atmo_gravity * rb.mass;
        }

        // Unpiloted player ship coasts (no thruster wrench) so LocalToShip interiors move.
        if (e.has<PlayerShip>() && !ship_pilot) {
            for (u32 i = 0; i < thrusters.count; ++i) {
                thrusters.activation[i] = 0.f;
            }
            sync_rigid_to_ecs(e, rb, true);
            integrate_rigid_body(rb, aero_force_world, glm::vec3{0.f}, dt);
            sync_rigid_to_ecs(e, rb, false);
            return;
        }

        const glm::vec3 body_vel = world_to_body(rb.orientation, rb.linear_vel);
        map_flight_control_to_thrusters(ctrl, body_vel, thrusters);

        glm::vec3 force_body{};
        glm::vec3 torque_body{};
        accumulate_thruster_wrench(
            thrusters, plant.frac_thrusters * eng_eff, force_body, torque_body);
        torque_body += ctrl.torque_input * kMaxTorqueNm * plant.frac_thrusters * eng_eff;

        const glm::vec3 force_world =
            body_to_world(rb.orientation, force_body) + aero_force_world;
        const glm::vec3 torque_world = body_to_world(rb.orientation, torque_body);

        sync_rigid_to_ecs(e, rb, true);
        integrate_rigid_body(rb, force_world, torque_world, dt);
        sync_rigid_to_ecs(e, rb, false);
    });

    // P2-04: HUD sample for the player ship (density this tick).
    {
        AtmosphereSample sample{};
        world.each([&](flecs::entity e, const RigidBody6DOF& rb) {
            if (sample.in_atmosphere || !e.has<PlayerShip>()) {
                return;
            }
            glm::vec3 g{};
            sample.density       = sample_atmosphere(world, rb.position, g);
            sample.in_atmosphere = sample.density > 0.f;
        });
        world.set<AtmosphereSample>(sample);
    }

    // NPC ships (no FlightControl): power + shields still tick (P2-03/P2-12).
    world.each([&](flecs::entity e, RigidBody6DOF&, PowerPlant& plant) {
        if (e.has<FlightControl>() || e.has<Destroyed>()) {
            return;
        }
        distribute_power(plant, dt);
        const ShipSubsystems* subs = e.try_get<ShipSubsystems>();
        const f32             shd_eff = (subs != nullptr)
            ? subsystem_efficiency(*subs, combat::Subsystem::Shields)
            : 1.f;
        if (ShieldGenerator* shield = e.try_get_mut<ShieldGenerator>()) {
            if (shd_eff <= 0.f) {
                shield->current = 0.f;
            } else {
                apply_shield_regen(*shield, plant.frac_shields * shd_eff, dt);
            }
        }
    });

    HullTarget targets[kMaxHullTargets]{};
    const u32  target_count = collect_hull_targets(world, targets, kMaxHullTargets);

    combat::DamageEventQueue* dmg_q = world.try_get_mut<combat::DamageEventQueue>();
    ProjectilePool*           pool  = world.try_get_mut<ProjectilePool>();
    const ecs::InputActions*  in    = world.try_get<ecs::InputActions>();
    const bool fire_held = ship_pilot && in != nullptr
        && in->state.pressed[static_cast<u16>(input::Action::Fire)];

    // Weapon cooldown/heat + fire (separate pass — no nested queries).
    world.each([&](flecs::entity e, RigidBody6DOF& rb, PowerPlant& plant,
                   WeaponMountSet& weapons) {
        for (u32 i = 0; i < weapons.count; ++i) {
            WeaponMount& w = weapons.mounts[i];
            if (w.cooldown_remaining > 0.f) {
                w.cooldown_remaining = std::max(0.f, w.cooldown_remaining - dt);
            }
            w.heat = std::max(0.f, w.heat - 15.f * dt);
        }
        if (!fire_held || e.has<Destroyed>() || !e.has<PlayerShip>()) {
            return;
        }
        // P2-02: destroyed weapons bank keeps all mounts offline.
        if (const ShipSubsystems* subs = e.try_get<ShipSubsystems>()) {
            if (!subsystem_operational(*subs, combat::Subsystem::Weapons)) {
                return;
            }
        }
        for (u32 i = 0; i < weapons.count; ++i) {
            try_fire_mount(
                world, e, rb, plant, weapons.mounts[i], targets, target_count, dmg_q, pool);
        }
    });

    // P2-03: turrets slew/fire after ship poses settle, before projectiles step.
    step_turrets(
        world,
        dt,
        targets,
        target_count,
        dmg_q,
        pool,
        (actions != nullptr) ? &actions->state : nullptr,
        look_scale * 0.25f);

    step_projectiles(world, dt, targets, target_count);
    apply_damage_events(world);
    step_crew(world, dt); // P2-01: engineer repairs after this tick's damage
    cleanup_destroyed(world);
}

void register_systems(flecs::world& world)
{
    if (world.try_get<combat::DamageEventQueue>() == nullptr) {
        world.set<combat::DamageEventQueue>(combat::DamageEventQueue{});
    }
    if (world.try_get<combat::CombatRng>() == nullptr) {
        world.set<combat::CombatRng>(combat::CombatRng{});
    }
    if (world.try_get<ActiveTurretControl>() == nullptr) {
        world.set<ActiveTurretControl>(ActiveTurretControl{});
    }
    if (world.try_get<ecs::ControlMode>() == nullptr) {
        world.set<ecs::ControlMode>(ecs::ControlMode{});
    }

    world.system("ShipChaseCameraSystem")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world w = it.world();
            update_chase_camera(w);
        });
}

flecs::entity spawn_player_ship(flecs::world& world, const glm::vec3& position)
{
    RigidBody6DOF rb{};
    rb.position     = position;
    rb.orientation  = glm::quat{1.f, 0.f, 0.f, 0.f};
    rb.mass         = kShipMassKg;
    rb.inertia_diag = glm::vec3{180000.f, 220000.f, 90000.f};

    ThrusterSet thrusters{};
    build_default_thruster_set(thrusters);

    PowerPlant plant{};
    plant.output_rate = 450.f;
    plant.capacity    = 1200.f;
    plant.stored      = 1200.f;

    ShieldGenerator shield{};
    shield.max_capacity = 600.f;
    shield.current      = 600.f;
    shield.regen_rate   = 35.f;
    shield.power_draw   = 90.f;

    ShipHull hull{};
    hull.max_hp = 1000.f;
    hull.hp     = 1000.f;
    hull.radius = 3.5f;

    WeaponMountSet weapons{};
    weapons.count                    = 1;
    weapons.mounts[0].local_offset   = {0.f, -0.5f, -3.f};
    weapons.mounts[0].cooldown       = 0.22f;
    weapons.mounts[0].energy_cost    = 12.f;
    weapons.mounts[0].damage         = 60.f;
    weapons.mounts[0].range          = 500.f;
    weapons.mounts[0].hitscan        = false;

    FlightControl ctrl{};
    ctrl.coupled = true;

    // P2-02: independently damageable banks (ENG / SHD / WPN / SEN).
    ShipSubsystems subsystems{};
    subsystems.items[combat::subsystem_index(combat::Subsystem::Engines)] = {300.f, 300.f};
    subsystems.items[combat::subsystem_index(combat::Subsystem::Shields)] = {250.f, 250.f};
    subsystems.items[combat::subsystem_index(combat::Subsystem::Weapons)] = {200.f, 200.f};
    subsystems.items[combat::subsystem_index(combat::Subsystem::Sensors)] = {150.f, 150.f};

    const ecs::Position pos{position.x, position.y, position.z};

    flecs::entity ship =
        world.entity("PlayerShip")
            .set<RigidBody6DOF>(rb)
            .set<ThrusterSet>(thrusters)
            .set<PowerPlant>(plant)
            .set<ShieldGenerator>(shield)
            .set<ShipHull>(hull)
            .set<ShipSubsystems>(subsystems)
            .set<AeroProfile>(AeroProfile{})
            .set<WeaponMountSet>(weapons)
            .set<FlightControl>(ctrl)
            .set<ecs::Position>(pos)
            .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
            .set<ecs::Velocity>({0.f, 0.f, 0.f})
            .set<ecs::Orientation>({rb.orientation})
            .set<ecs::Scale>({2.2f})
            .add<ecs::InstanceTag>()
            .add<ecs::KinematicFromRigidBody>()
            .add<PlayerShip>();

    economy::attach_cargo_hold_if_missing(ship);
    (void)save::assign_persistent_id(world, ship);

    log::log_info(
        log::LogCategory::Core,
        "Spawned player ship at (%.1f, %.1f, %.1f) mass=%.0f thrusters=%u",
        static_cast<double>(position.x),
        static_cast<double>(position.y),
        static_cast<double>(position.z),
        static_cast<double>(rb.mass),
        thrusters.count);

    return ship;
}

flecs::entity spawn_damage_target(flecs::world& world, const glm::vec3& position, f32 scale)
{
    ShipHull hull{};
    hull.max_hp = 400.f;
    hull.hp     = 400.f;
    hull.radius = scale * 0.9f;

    const ecs::Position pos{position.x, position.y, position.z};

    // P2-02: targets expose subsystem banks so hits can disable them one by one.
    ShipSubsystems subsystems{};
    subsystems.items[combat::subsystem_index(combat::Subsystem::Engines)] = {120.f, 120.f};
    subsystems.items[combat::subsystem_index(combat::Subsystem::Shields)] = {100.f, 100.f};
    subsystems.items[combat::subsystem_index(combat::Subsystem::Weapons)] = {80.f, 80.f};
    subsystems.items[combat::subsystem_index(combat::Subsystem::Sensors)] = {60.f, 60.f};

    flecs::entity target = world.entity("DamageTarget")
                               .set<ShipHull>(hull)
                               .set<ShipSubsystems>(subsystems)
                               .set<ecs::Position>(pos)
                               .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
                               .set<ecs::Velocity>({0.f, 0.f, 0.f})
                               .set<ecs::Scale>({scale})
                               .add<ecs::InstanceTag>();

    log::log_info(
        log::LogCategory::Core,
        "Spawned damage target at (%.1f, %.1f, %.1f) hp=%.0f",
        static_cast<double>(position.x),
        static_cast<double>(position.y),
        static_cast<double>(position.z),
        static_cast<double>(hull.hp));

    return target;
}

flecs::entity spawn_crew_member(
    flecs::world&    world,
    flecs::entity_t  ship,
    CrewRole         role,
    const glm::vec3& local_seat,
    const char*      name)
{
    CrewMember crew{};
    crew.role = role;
    crew.ship = ship;

    character::LocalToShip seat{};
    seat.ship_entity    = ship;
    seat.local_position = local_seat;

    character::Health hp{};
    hp.max_hp = 100.f;
    hp.hp     = 100.f;

    // World pose is derived by sync_local_to_ship_world (LocalToShip contract).
    const ecs::Position pos{0.f, 0.f, 0.f};

    flecs::entity crew_e = world.entity(name)
                               .set<CrewMember>(crew)
                               .set<character::LocalToShip>(seat)
                               .set<character::Health>(hp)
                               .set<ecs::Position>(pos)
                               .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
                               .set<ecs::Velocity>({0.f, 0.f, 0.f})
                               .set<ecs::Orientation>({})
                               .set<ecs::Scale>({0.7f})
                               .add<ecs::InstanceTag>()
                               .add<ecs::KinematicFromRigidBody>();

    log::log_info(
        log::LogCategory::Game,
        "Spawned crew '%s' role=%s seat=(%.1f, %.1f, %.1f)",
        name,
        (role == CrewRole::Engineer) ? "engineer" : "turret-gunner",
        static_cast<double>(local_seat.x),
        static_cast<double>(local_seat.y),
        static_cast<double>(local_seat.z));

    return crew_e;
}

flecs::entity spawn_turret(
    flecs::world&    world,
    flecs::entity_t  ship,
    const glm::vec3& local_offset,
    u32              faction_id,
    bool             requires_gunner,
    const char*      name)
{
    TurretMount mount{};
    mount.ship            = ship;
    mount.local_offset    = local_offset;
    mount.requires_gunner = requires_gunner;
    mount.weapon.cooldown = 0.5f;
    mount.weapon.energy_cost = 10.f;
    mount.weapon.damage      = 35.f;
    mount.weapon.range       = 350.f;
    mount.weapon.hitscan     = false;

    ai::AiAgent agent{};
    agent.detection_range      = 400.f;
    agent.attack_range         = mount.weapon.range;
    agent.flee_health_fraction = 0.f; // turrets never flee

    const ecs::Position pos{0.f, 0.f, 0.f}; // synced from ship each fixed step

    flecs::entity turret = world.entity(name)
                               .set<TurretMount>(mount)
                               .set<ai::AiAgent>(agent)
                               .set<ai::FactionMember>({faction_id})
                               .set<ai::SensorLink>({ship})
                               .set<ecs::Position>(pos)
                               .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
                               .set<ecs::Velocity>({0.f, 0.f, 0.f})
                               .set<ecs::Orientation>({})
                               .set<ecs::Scale>({0.9f})
                               .add<ecs::InstanceTag>()
                               .add<ecs::KinematicFromRigidBody>();

    log::log_info(
        log::LogCategory::Game,
        "Spawned turret '%s' on ship=%llu faction=%u requires_gunner=%d",
        name,
        static_cast<unsigned long long>(ship),
        faction_id,
        requires_gunner ? 1 : 0);

    return turret;
}

flecs::entity spawn_npc_ship(
    flecs::world&    world,
    const glm::vec3& position,
    u32              faction_id,
    const char*      name)
{
    RigidBody6DOF rb{};
    rb.position     = position;
    rb.mass         = kShipMassKg;
    rb.inertia_diag = glm::vec3{180000.f, 220000.f, 90000.f};

    PowerPlant plant{};
    plant.output_rate = 350.f;
    plant.capacity    = 900.f;
    plant.stored      = 900.f;

    ShieldGenerator shield{};
    shield.max_capacity = 350.f;
    shield.current      = 350.f;
    shield.regen_rate   = 25.f;

    ShipHull hull{};
    hull.max_hp = 600.f;
    hull.hp     = 600.f;
    hull.radius = 3.5f;

    ShipSubsystems subsystems{};
    subsystems.items[combat::subsystem_index(combat::Subsystem::Engines)] = {200.f, 200.f};
    subsystems.items[combat::subsystem_index(combat::Subsystem::Shields)] = {160.f, 160.f};
    subsystems.items[combat::subsystem_index(combat::Subsystem::Weapons)] = {140.f, 140.f};
    subsystems.items[combat::subsystem_index(combat::Subsystem::Sensors)] = {100.f, 100.f};

    const ecs::Position pos{position.x, position.y, position.z};

    flecs::entity ship = world.entity(name)
                             .set<RigidBody6DOF>(rb)
                             .set<PowerPlant>(plant)
                             .set<ShieldGenerator>(shield)
                             .set<ShipHull>(hull)
                             .set<ShipSubsystems>(subsystems)
                             .set<ai::FactionMember>({faction_id})
                             .set<ecs::Position>(pos)
                             .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
                             .set<ecs::Velocity>({0.f, 0.f, 0.f})
                             .set<ecs::Orientation>({rb.orientation})
                             .set<ecs::Scale>({2.2f})
                             .add<ecs::InstanceTag>()
                             .add<ecs::KinematicFromRigidBody>();

    log::log_info(
        log::LogCategory::Game,
        "Spawned NPC ship '%s' faction=%u at (%.0f, %.0f, %.0f)",
        name,
        faction_id,
        static_cast<double>(position.x),
        static_cast<double>(position.y),
        static_cast<double>(position.z));

    return ship;
}

void spawn_projectile_pool(flecs::world& world)
{
    ProjectilePool pool{};
    pool.count = kProjectilePoolSize;

    for (u32 i = 0; i < kProjectilePoolSize; ++i) {
        Projectile proj{};
        proj.alive = false;

        const ecs::Position pos{0.f, -1000.f, 0.f};
        flecs::entity       e = world.entity()
                              .set<Projectile>(proj)
                              .set<ecs::Position>(pos)
                              .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
                              .set<ecs::Velocity>({0.f, 0.f, 0.f})
                              .set<ecs::Scale>({kProjectileRadius * 2.f})
                              .add<ecs::KinematicFromRigidBody>();
        pool.entities[i] = e.id();
    }

    world.set<ProjectilePool>(pool);

    if (world.try_get<combat::DamageEventQueue>() == nullptr) {
        world.set<combat::DamageEventQueue>(combat::DamageEventQueue{});
    }
}

void fill_player_subsystem_telemetry(flecs::world& world, SubsystemTelemetry& out)
{
    out = SubsystemTelemetry{};
    world.each([&](flecs::entity e, const ShipSubsystems& subs) {
        if (out.found || !e.has<PlayerShip>()) {
            return;
        }
        out.found = true;
        for (u32 i = 0; i < combat::kSubsystemCount; ++i) {
            out.efficiency[i] =
                subsystem_efficiency(subs, static_cast<combat::Subsystem>(i + 1u));
        }
    });
}

void fill_player_telemetry(
    flecs::world& world,
    f32&          speed,
    f32&          energy,
    f32&          energy_capacity,
    f32&          shield_pct,
    f32&          hull_hp,
    f32&          hull_max_hp,
    bool&         coupled,
    bool&         found)
{
    found           = false;
    speed           = 0.f;
    energy          = 0.f;
    energy_capacity = 0.f;
    shield_pct      = 0.f;
    hull_hp         = -1.f;
    hull_max_hp     = 0.f;
    coupled         = false;

    world.each([&](flecs::entity e,
                   const RigidBody6DOF& rb,
                   const PowerPlant& plant,
                   const ShieldGenerator& shield,
                   const ShipHull& hull,
                   const FlightControl& ctrl) {
        if (found || !e.has<PlayerShip>()) {
            return;
        }
        found           = true;
        speed           = glm::length(rb.linear_vel);
        energy          = plant.stored;
        energy_capacity = plant.capacity;
        shield_pct =
            (shield.max_capacity > 1e-3f) ? (shield.current / shield.max_capacity) : 0.f;
        hull_hp     = hull.hp;
        hull_max_hp = hull.max_hp;
        coupled     = ctrl.coupled;
    });
}

}  // namespace csc::game::flight
