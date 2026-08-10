#include "game/flight/flight.hpp"

#include "engine/ecs/world.hpp"
#include "engine/log/log.hpp"

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
    set = {};
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

void update_chase_camera(flecs::world& world)
{
    const ecs::ControlMode* mode = world.try_get<ecs::ControlMode>();
    if (mode == nullptr || mode->mode != ecs::ControlModeKind::ShipPilot) {
        return;
    }

    const ecs::FrameInterpolation* fi = world.try_get<ecs::FrameInterpolation>();
    const f32 alpha = (fi != nullptr) ? fi->alpha : 1.f;

    bool      ship_found = false;
    glm::vec3 ship_pos{0.f};
    glm::quat ship_ori{1.f, 0.f, 0.f, 0.f};

    world.each([&](flecs::entity e, const RigidBody6DOF& rb, const PlayerShip&) {
        if (ship_found || e.has<Destroyed>()) {
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
    if (w.cooldown_remaining > 0.f || w.heat >= w.heat_max) {
        return;
    }
    if (plant.frac_weapons < 0.15f || plant.stored < w.energy_cost) {
        return;
    }

    plant.stored             = std::max(0.f, plant.stored - w.energy_cost);
    w.cooldown_remaining     = w.cooldown;
    w.heat                   = std::min(w.heat_max, w.heat + 12.f);

    const glm::vec3 muzzle = rb.position + body_to_world(rb.orientation, w.local_offset);
    const glm::vec3 dir    = body_to_world(rb.orientation, kShipForward);

    if (w.hitscan) {
        const flecs::entity_t hit =
            raycast_hull(targets, target_count, ship.id(), muzzle, dir, w.range);
        if (hit != 0 && dmg_q != nullptr) {
            (void)dmg_q->push(combat::DamageEvent{hit, ship.id(), w.damage});
        }
        return;
    }

    if (pool == nullptr) {
        return;
    }
    for (u32 pi = 0; pi < pool->count; ++pi) {
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
        proj->source         = ship.id();

        const ecs::Position pos{muzzle.x, muzzle.y, muzzle.z};
        pe.set<ecs::Position>(pos);
        pe.set<ecs::PreviousPosition>({pos.x, pos.y, pos.z});
        pe.set<ecs::Velocity>({proj->velocity.x, proj->velocity.y, proj->velocity.z});
        pe.set<ecs::Scale>({kProjectileRadius * 2.f});
        pe.add<ecs::InstanceTag>();
        break;
    }
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
            (void)dmg_q->push(combat::DamageEvent{hit_target, proj.source, proj.damage});
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

    combat::DamageEvent ev{};
    while (q->try_pop(ev)) {
        flecs::entity target = world.entity(ev.target);
        if (!target.is_alive() || target.has<Destroyed>()) {
            continue;
        }

        f32 remaining = ev.amount;
        if (ShieldGenerator* shield = target.try_get_mut<ShieldGenerator>()) {
            const f32 absorbed = std::min(shield->current, remaining);
            shield->current -= absorbed;
            remaining -= absorbed;
        }
        if (remaining <= 0.f) {
            continue;
        }
        if (ShipHull* hull = target.try_get_mut<ShipHull>()) {
            hull->hp = std::max(0.f, hull->hp - remaining);
            if (hull->hp <= 0.f && destroyed_count < kMaxHullTargets) {
                newly_destroyed[destroyed_count++] = target.id();
            }
        }
    }

    for (u32 i = 0; i < destroyed_count; ++i) {
        flecs::entity e = world.entity(newly_destroyed[i]);
        if (e.is_alive() && !e.has<Destroyed>()) {
            e.add<Destroyed>();
        }
    }
}

void cleanup_destroyed(flecs::world& world)
{
    flecs::entity_t strip[kMaxHullTargets]{};
    u32             strip_count = 0;

    world.each([&](flecs::entity e, Destroyed, RigidBody6DOF& rb) {
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
    world.each([&](flecs::entity e, Destroyed, ShipHull&) {
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
        flecs::entity e = world.entity(strip[i]);
        if (e.is_alive() && e.has<ecs::InstanceTag>()) {
            e.remove<ecs::InstanceTag>();
        }
    }
}

}  // namespace

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

    if (actions != nullptr) {
        world.each([&](flecs::entity e, FlightControl& ctrl, const PlayerShip&) {
            if (e.has<Destroyed>()) {
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

        if (ShieldGenerator* shield = e.try_get_mut<ShieldGenerator>()) {
            apply_shield_regen(*shield, plant.frac_shields, dt);
            plant.stored = std::max(
                0.f, plant.stored - shield->power_draw * plant.frac_shields * dt * 0.25f);
        }

        const glm::vec3 body_vel = world_to_body(rb.orientation, rb.linear_vel);
        map_flight_control_to_thrusters(ctrl, body_vel, thrusters);

        glm::vec3 force_body{};
        glm::vec3 torque_body{};
        accumulate_thruster_wrench(thrusters, plant.frac_thrusters, force_body, torque_body);
        torque_body += ctrl.torque_input * kMaxTorqueNm * plant.frac_thrusters;

        const glm::vec3 force_world  = body_to_world(rb.orientation, force_body);
        const glm::vec3 torque_world = body_to_world(rb.orientation, torque_body);

        sync_rigid_to_ecs(e, rb, true);
        integrate_rigid_body(rb, force_world, torque_world, dt);
        sync_rigid_to_ecs(e, rb, false);
    });

    HullTarget targets[kMaxHullTargets]{};
    const u32  target_count = collect_hull_targets(world, targets, kMaxHullTargets);

    combat::DamageEventQueue* dmg_q = world.try_get_mut<combat::DamageEventQueue>();
    ProjectilePool*           pool  = world.try_get_mut<ProjectilePool>();
    const ecs::InputActions*  in    = world.try_get<ecs::InputActions>();
    const bool fire_held =
        in != nullptr && in->state.pressed[static_cast<u16>(input::Action::Fire)];

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
        for (u32 i = 0; i < weapons.count; ++i) {
            try_fire_mount(
                world, e, rb, plant, weapons.mounts[i], targets, target_count, dmg_q, pool);
        }
    });

    step_projectiles(world, dt, targets, target_count);
    apply_damage_events(world);
    cleanup_destroyed(world);
}

void register_systems(flecs::world& world)
{
    if (world.try_get<combat::DamageEventQueue>() == nullptr) {
        world.set<combat::DamageEventQueue>(combat::DamageEventQueue{});
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

    const ecs::Position pos{position.x, position.y, position.z};

    flecs::entity ship =
        world.entity("PlayerShip")
            .set<RigidBody6DOF>(rb)
            .set<ThrusterSet>(thrusters)
            .set<PowerPlant>(plant)
            .set<ShieldGenerator>(shield)
            .set<ShipHull>(hull)
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

    flecs::entity target = world.entity("DamageTarget")
                               .set<ShipHull>(hull)
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

    world.each([&](flecs::entity /*e*/,
                   const RigidBody6DOF& rb,
                   const PowerPlant& plant,
                   const ShieldGenerator& shield,
                   const ShipHull& hull,
                   const FlightControl& ctrl,
                   const PlayerShip&) {
        if (found) {
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
