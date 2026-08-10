#include "game/character/character.hpp"

#include "engine/ecs/world.hpp"
#include "engine/log/log.hpp"
#include "game/economy/economy.hpp"
#include "game/flight/flight.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace csc::game::character {
namespace {

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

[[nodiscard]] glm::vec3 forward_from_yaw_pitch(f32 yaw, f32 pitch)
{
    const f32 cy = std::cos(yaw);
    const f32 sy = std::sin(yaw);
    const f32 cp = std::cos(pitch);
    const f32 sp = std::sin(pitch);
    return glm::normalize(glm::vec3{cy * cp, sp, sy * cp});
}

void copy_prompt(char* dst, std::size_t cap, const char* src)
{
    if (dst == nullptr || cap == 0) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

[[nodiscard]] bool point_in_zone(const GravityZone& z, const glm::vec3& p)
{
    if (z.shape == GravityZoneShape::Sphere) {
        const glm::vec3 d = p - z.center;
        return glm::dot(d, d) <= z.sphere_radius * z.sphere_radius;
    }
    const glm::vec3 d = p - z.center;
    return std::abs(d.x) <= z.half_extents.x && std::abs(d.y) <= z.half_extents.y
        && std::abs(d.z) <= z.half_extents.z;
}

struct SampledGravity {
    glm::vec3 direction{0.f, -1.f, 0.f};
    f32       magnitude = 0.f;
    bool      in_zone   = false;
};

SampledGravity sample_gravity(flecs::world& world, const glm::vec3& world_pos)
{
    SampledGravity best{};
    f32            best_mag = -1.f;

    world.each([&](const GravityZone& zone) {
        if (!point_in_zone(zone, world_pos)) {
            return;
        }
        if (zone.magnitude > best_mag) {
            best_mag        = zone.magnitude;
            best.magnitude  = zone.magnitude;
            best.direction  = safe_normalize(zone.gravity_vector, glm::vec3{0.f, -1.f, 0.f});
            best.in_zone    = true;
        }
    });
    return best;
}

[[nodiscard]] bool try_get_ship_rb(
    flecs::world& world, flecs::entity_t ship_id, flight::RigidBody6DOF& out)
{
    if (ship_id == 0) {
        return false;
    }
    flecs::entity ship = world.entity(ship_id);
    if (!ship.is_alive()) {
        return false;
    }
    const flight::RigidBody6DOF* rb = ship.try_get<flight::RigidBody6DOF>();
    if (rb == nullptr) {
        return false;
    }
    out = *rb;
    return true;
}

void world_from_local(
    const flight::RigidBody6DOF& ship,
    const glm::vec3&             local_pos,
    const glm::quat&             local_ori,
    glm::vec3&                   out_pos,
    glm::quat&                   out_ori)
{
    out_pos = ship.position + ship.orientation * local_pos;
    out_ori = glm::normalize(ship.orientation * local_ori);
}

void write_world_pose(flecs::entity e, const glm::vec3& pos, const glm::quat& ori, bool snapshot_prev)
{
    ecs::Position*         p    = e.try_get_mut<ecs::Position>();
    ecs::PreviousPosition* prev = e.try_get_mut<ecs::PreviousPosition>();
    ecs::Orientation*      o    = e.try_get_mut<ecs::Orientation>();
    ecs::Velocity*         v    = e.try_get_mut<ecs::Velocity>();

    if (snapshot_prev && p != nullptr && prev != nullptr) {
        prev->x = p->x;
        prev->y = p->y;
        prev->z = p->z;
    }
    if (p != nullptr) {
        p->x = pos.x;
        p->y = pos.y;
        p->z = pos.z;
    }
    if (o != nullptr) {
        o->q = ori;
    }
    if (v != nullptr) {
        // Render interp uses Position delta; zero ECS velocity (kinematic).
        *v = {};
    }
}

void sync_local_to_ship_world(flecs::world& world)
{
    world.each([&](flecs::entity e, LocalToShip& local) {
        flight::RigidBody6DOF ship_rb{};
        if (!try_get_ship_rb(world, local.ship_entity, ship_rb)) {
            return;
        }
        glm::vec3 wpos{};
        glm::quat wori{};
        world_from_local(ship_rb, local.local_position, local.local_orientation, wpos, wori);
        write_world_pose(e, wpos, wori, true);
        write_world_pose(e, wpos, wori, false);
    });
}

struct HealthTarget {
    flecs::entity_t id     = 0;
    glm::vec3       center{0.f};
    f32             radius = 0.f;
};

u32 collect_health_targets(flecs::world& world, HealthTarget* out, u32 capacity)
{
    u32 n = 0;
    world.each([&](flecs::entity e, const Health& /*hp*/, const ecs::Position& p) {
        if (n >= capacity || e.has<CharacterDead>()) {
            return;
        }
        f32 radius = kHealthTargetRadius;
        if (const ecs::Scale* s = e.try_get<ecs::Scale>()) {
            radius = std::max(0.35f, s->value * 0.55f);
        }
        out[n].id     = e.id();
        out[n].center = glm::vec3{p.x, p.y, p.z};
        out[n].radius = radius;
        ++n;
    });
    return n;
}

[[nodiscard]] flecs::entity_t raycast_health(
    const HealthTarget* targets,
    u32                 target_count,
    flecs::entity_t     self,
    const glm::vec3&    origin,
    const glm::vec3&    dir,
    f32                 range)
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

struct InteractHit {
    flecs::entity_t id = 0;
    f32             t  = 0.f;
    char            prompt[32]{};
};

[[nodiscard]] InteractHit raycast_interactable(
    flecs::world& world, const glm::vec3& origin, const glm::vec3& dir, f32 range)
{
    InteractHit best{};
    best.t = range;

    world.each([&](flecs::entity e, const ecs::Position& p) {
        if (!e.has<Interactable>()) {
            return;
        }
        const glm::vec3 center{p.x, p.y, p.z};
        const f32       radius = 0.75f;
        const glm::vec3 oc     = origin - center;
        const f32       b      = glm::dot(oc, dir);
        const f32       c      = glm::dot(oc, oc) - radius * radius;
        const f32       disc   = b * b - c;
        if (disc < 0.f) {
            return;
        }
        const f32 t = -b - std::sqrt(disc);
        if (t > 0.f && t < best.t) {
            best.t  = t;
            best.id = e.id();
            if (const InteractablePrompt* pr = e.try_get<InteractablePrompt>()) {
                copy_prompt(best.prompt, sizeof(best.prompt), pr->label);
            } else {
                copy_prompt(best.prompt, sizeof(best.prompt), "Interact");
            }
        }
    });
    return best;
}

void update_interaction_focus(flecs::world& world)
{
    InteractionFocus focus{};
    const ecs::ControlMode* mode = world.try_get<ecs::ControlMode>();
    if (mode == nullptr
        || (mode->mode != ecs::ControlModeKind::OnFoot
            && mode->mode != ecs::ControlModeKind::ShipPilot)) {
        world.set<InteractionFocus>(focus);
        return;
    }

    ecs::Camera3D cam{};
    if (!ecs::world_try_get_primary_camera(world, cam)) {
        world.set<InteractionFocus>(focus);
        return;
    }

    const glm::vec3 origin = cam.eye;
    const glm::vec3 dir    = safe_normalize(cam.target - cam.eye, glm::vec3{0.f, 0.f, -1.f});
    const InteractHit hit  = raycast_interactable(world, origin, dir, kInteractRange);
    if (hit.id != 0) {
        focus.target = hit.id;
        copy_prompt(focus.prompt, sizeof(focus.prompt), hit.prompt);
    }
    world.set<InteractionFocus>(focus);
}

void handle_interact_events(flecs::world& world)
{
    InteractEventQueue* q = world.try_get_mut<InteractEventQueue>();
    if (q == nullptr) {
        return;
    }

    InteractEvent ev{};
    while (q->try_pop(ev)) {
        if (ev.actor == 0 || ev.target == 0) {
            continue;
        }
        flecs::entity actor  = world.entity(ev.actor);
        flecs::entity target = world.entity(ev.target);
        if (!actor.is_alive() || !target.is_alive()) {
            continue;
        }

        // --- Hatch: interior (LocalToShip) ↔ EVA (world) -------------------
        if (const ShipHatch* hatch = target.try_get<ShipHatch>()) {
            LocalToShip* local = actor.try_get_mut<LocalToShip>();
            if (local != nullptr) {
                // Exit to EVA: bake world pose, drop attachment.
                flight::RigidBody6DOF ship_rb{};
                glm::vec3             wpos = local->local_position;
                glm::quat             wori = local->local_orientation;
                if (try_get_ship_rb(world, local->ship_entity, ship_rb)) {
                    world_from_local(
                        ship_rb, local->local_position, local->local_orientation, wpos, wori);
                    // Nudge outside hatch along ship forward.
                    wpos += ship_rb.orientation * glm::vec3{0.f, 0.f, 2.5f};
                }
                actor.remove<LocalToShip>();
                write_world_pose(actor, wpos, wori, true);
                write_world_pose(actor, wpos, wori, false);
                if (CharacterController* cc = actor.try_get_mut<CharacterController>()) {
                    cc->velocity = {};
                    cc->grounded = false;
                }
                log::log_info(log::LogCategory::Core, "Hatch: exit to EVA");
            } else {
                // Enter interior: attach to ship at hatch local offset.
                flight::RigidBody6DOF ship_rb{};
                if (!try_get_ship_rb(world, hatch->ship, ship_rb)) {
                    continue;
                }
                LocalToShip attach{};
                attach.ship_entity = hatch->ship;
                // Prefer hatch's own LocalToShip pose if present.
                if (const LocalToShip* hatch_local = target.try_get<LocalToShip>()) {
                    attach.local_position    = hatch_local->local_position;
                    attach.local_orientation = hatch_local->local_orientation;
                } else {
                    const ecs::Position* tp = target.try_get<ecs::Position>();
                    if (tp != nullptr) {
                        const glm::vec3 w{tp->x, tp->y, tp->z};
                        attach.local_position =
                            glm::conjugate(ship_rb.orientation) * (w - ship_rb.position);
                    }
                    attach.local_orientation = glm::quat{1.f, 0.f, 0.f, 0.f};
                }
                actor.set<LocalToShip>(attach);
                if (CharacterController* cc = actor.try_get_mut<CharacterController>()) {
                    cc->velocity = {};
                    cc->grounded = true;
                }
                // Ensure OnFoot when boarding as passenger.
                world.set<ecs::ControlMode>({ecs::ControlModeKind::OnFoot});
                log::log_info(log::LogCategory::Core, "Hatch: enter ship interior");
            }
            continue;
        }

        // --- Pilot seat: OnFoot ↔ ShipPilot (same player entity / Health) --
        if (target.try_get<PilotSeat>() != nullptr) {
            ecs::ControlMode* mode = world.try_get_mut<ecs::ControlMode>();
            if (mode == nullptr) {
                continue;
            }
            if (mode->mode == ecs::ControlModeKind::ShipPilot) {
                mode->mode = ecs::ControlModeKind::OnFoot;
                // Exit cockpit to hatch/EVA world pose if not already attached.
                if (!actor.has<LocalToShip>()) {
                    const PilotSeat* seat = target.try_get<PilotSeat>();
                    flight::RigidBody6DOF ship_rb{};
                    if (seat != nullptr && try_get_ship_rb(world, seat->ship, ship_rb)) {
                        glm::vec3 exit_pos =
                            ship_rb.position + ship_rb.orientation * glm::vec3{0.f, 0.9f, 3.f};
                        write_world_pose(
                            actor, exit_pos, ship_rb.orientation, true);
                        write_world_pose(
                            actor, exit_pos, ship_rb.orientation, false);
                    }
                }
                log::log_info(log::LogCategory::Core, "Pilot seat: → OnFoot");
            } else {
                mode->mode = ecs::ControlModeKind::ShipPilot;
                // Keep Health; drop LocalToShip while piloting (camera follows ship).
                if (actor.has<LocalToShip>()) {
                    actor.remove<LocalToShip>();
                }
                log::log_info(log::LogCategory::Core, "Pilot seat: → ShipPilot");
            }
            continue;
        }

        // Economy NPCs (TradeOffer / Dialogue / TravelPad) — P1C-06.
        if (economy::handle_interact(world, ev.actor, ev.target)) {
            continue;
        }

        // Generic interactable — event already published; log for demo terminals.
        if (const InteractablePrompt* pr = target.try_get<InteractablePrompt>()) {
            log::log_info(log::LogCategory::Core, "Interact: %s", pr->label);
        }
    }
}

void step_locomotion(flecs::world& world, f32 dt, const input::ActionState* actions)
{
    const ecs::ControlMode* mode = world.try_get<ecs::ControlMode>();
    const bool on_foot =
        mode != nullptr && mode->mode == ecs::ControlModeKind::OnFoot;

    const ecs::CameraControlParams* cam_params = world.try_get<ecs::CameraControlParams>();
    const f32 look_sens =
        (cam_params != nullptr) ? cam_params->mouse_sensitivity : 0.0025f;

    world.each([&](flecs::entity e, CharacterController& cc) {
        if (e.has<CharacterDead>()) {
            cc.velocity = {};
            return;
        }

        const bool is_player = e.has<PlayerCharacter>();
        const bool driven    = is_player && on_foot && actions != nullptr;

        if (driven) {
            cc.yaw += actions->axes[static_cast<u16>(input::ActionAxis::LookX)] * look_sens;
            cc.pitch -= actions->axes[static_cast<u16>(input::ActionAxis::LookY)] * look_sens;
            cc.pitch = clampf(cc.pitch, -kLookPitchLimit, kLookPitchLimit);

            if (actions->just_pressed[static_cast<u16>(input::Action::ToggleCoupled)]) {
                cc.third_person = !cc.third_person;
            }
        }

        LocalToShip*          local   = e.try_get_mut<LocalToShip>();
        flight::RigidBody6DOF ship_rb{};
        const bool            attached =
            local != nullptr && try_get_ship_rb(world, local->ship_entity, ship_rb);

        // Simulation position in the active frame.
        glm::vec3 sim_pos =
            attached ? local->local_position
                     : [&]() {
                           const ecs::Position* p = e.try_get<ecs::Position>();
                           return p != nullptr ? glm::vec3{p->x, p->y, p->z} : glm::vec3{0.f};
                       }();

        // Gravity: attached → artificial down in ship local; else sample world zones.
        SampledGravity grav{};
        if (attached) {
            grav.direction = glm::vec3{0.f, -1.f, 0.f};
            grav.magnitude = kGravityDefault;
            grav.in_zone   = true;
        } else {
            glm::vec3 world_pos = sim_pos;
            grav               = sample_gravity(world, world_pos);
        }

        const bool eva = !grav.in_zone || grav.magnitude < 0.05f;

        glm::vec3 wish{0.f};
        if (driven) {
            const f32 ax = actions->axes[static_cast<u16>(input::ActionAxis::MoveX)];
            const f32 ay = actions->axes[static_cast<u16>(input::ActionAxis::MoveY)];
            const f32 az = actions->axes[static_cast<u16>(input::ActionAxis::MoveZ)];

            const glm::vec3 look_fwd = forward_from_yaw_pitch(cc.yaw, eva ? cc.pitch : 0.f);
            const glm::vec3 world_up{0.f, 1.f, 0.f};
            glm::vec3       right = glm::cross(look_fwd, world_up);
            const f32       r2    = glm::dot(right, right);
            if (r2 > 1e-8f) {
                right *= 1.f / std::sqrt(r2);
            } else {
                right = glm::vec3{1.f, 0.f, 0.f};
            }

            if (eva) {
                // Suit thrusters: full 6DOF wish from Move axes (same mapping as thrusters).
                wish = look_fwd * az + right * ax + world_up * ay;
            } else {
                // Walk on ground plane of gravity frame (Y-up local / world for demo).
                glm::vec3 flat_fwd = look_fwd;
                flat_fwd.y        = 0.f;
                flat_fwd          = safe_normalize(flat_fwd, glm::vec3{0.f, 0.f, -1.f});
                glm::vec3 flat_r  = right;
                flat_r.y          = 0.f;
                flat_r            = safe_normalize(flat_r, glm::vec3{1.f, 0.f, 0.f});
                wish              = flat_fwd * az + flat_r * ax;
            }
        }

        const f32 wish_len2 = glm::dot(wish, wish);
        if (wish_len2 > 1.f) {
            wish *= 1.f / std::sqrt(wish_len2);
        }

        if (eva) {
            cc.grounded = false;
            if (wish_len2 > 1e-8f) {
                cc.velocity += wish * (cc.eva_thrust * dt);
            }
            // Light damping so EVA is controllable.
            cc.velocity *= std::exp(-0.35f * dt);
        } else {
            const glm::vec3 g_accel = grav.direction * grav.magnitude;
            // Horizontal wish at move_speed; vertical from gravity / jump.
            glm::vec3 horiz = cc.velocity;
            // Project out gravity axis (demo: Y).
            horiz.y = 0.f;
            if (wish_len2 > 1e-8f) {
                horiz = glm::vec3{wish.x, 0.f, wish.z} * cc.move_speed;
            } else {
                horiz *= std::exp(-12.f * dt); // ground friction
            }
            cc.velocity.x = horiz.x;
            cc.velocity.z = horiz.z;
            cc.velocity.y += g_accel.y * dt;

            if (driven && cc.grounded
                && actions->just_pressed[static_cast<u16>(input::Action::MoveUp)]) {
                cc.velocity.y = kJumpSpeed;
                cc.grounded   = false;
            }
        }

        // Integrate in sim space.
        sim_pos += cc.velocity * dt;

        // Simple floor: capsule feet at y=0 in sim space (ship deck / station pad).
        if (!eva) {
            const f32 feet_y = sim_pos.y - (cc.height * 0.5f);
            if (feet_y <= kGroundSnapEpsilon) {
                sim_pos.y     = cc.height * 0.5f;
                if (cc.velocity.y < 0.f) {
                    cc.velocity.y = 0.f;
                }
                cc.grounded = true;
            } else {
                cc.grounded = false;
            }
        }

        // Face look yaw in sim orientation (pitch is camera-only when walking).
        const glm::quat yaw_q =
            glm::angleAxis(cc.yaw, glm::vec3{0.f, 1.f, 0.f});

        if (attached) {
            local->local_position    = sim_pos;
            local->local_orientation = yaw_q;
            glm::vec3 wpos{};
            glm::quat wori{};
            world_from_local(
                ship_rb, local->local_position, local->local_orientation, wpos, wori);
            write_world_pose(e, wpos, wori, true);
            write_world_pose(e, wpos, wori, false);
        } else {
            write_world_pose(e, sim_pos, yaw_q, true);
            write_world_pose(e, sim_pos, yaw_q, false);
        }
    });
}

void step_fps_combat(flecs::world& world, f32 dt, const input::ActionState* actions)
{
    const ecs::ControlMode* mode = world.try_get<ecs::ControlMode>();
    if (mode == nullptr || mode->mode != ecs::ControlModeKind::OnFoot) {
        // Still tick cooldowns.
        world.each([&](EquippedItem& item) {
            if (item.cooldown_remaining > 0.f) {
                item.cooldown_remaining = std::max(0.f, item.cooldown_remaining - dt);
            }
        });
        return;
    }

    HealthTarget targets[kMaxHealthTargets]{};
    const u32    target_count = collect_health_targets(world, targets, kMaxHealthTargets);
    combat::DamageEventQueue* dmg_q = world.try_get_mut<combat::DamageEventQueue>();

    ecs::Camera3D cam{};
    const bool    have_cam = ecs::world_try_get_primary_camera(world, cam);
    const glm::vec3 cam_origin = cam.eye;
    const glm::vec3 cam_dir =
        safe_normalize(cam.target - cam.eye, glm::vec3{0.f, 0.f, -1.f});

    const bool fire =
        actions != nullptr && actions->pressed[static_cast<u16>(input::Action::Fire)];

    world.each([&](flecs::entity e, EquippedItem& item) {
        if (item.cooldown_remaining > 0.f) {
            item.cooldown_remaining = std::max(0.f, item.cooldown_remaining - dt);
        }
        if (!e.has<PlayerCharacter>() || e.has<CharacterDead>()) {
            return;
        }
        if (!fire || item.cooldown_remaining > 0.f || item.ammo == 0 || !have_cam) {
            return;
        }

        item.cooldown_remaining = item.fire_cooldown;
        if (item.ammo > 0) {
            --item.ammo;
        }

        const flecs::entity_t hit =
            raycast_health(targets, target_count, e.id(), cam_origin, cam_dir, kFpsWeaponRange);
        if (hit != 0 && dmg_q != nullptr) {
            (void)dmg_q->push(combat::DamageEvent{hit, e.id(), item.damage});
        }
    });
}

void step_interact_input(flecs::world& world, const input::ActionState* actions)
{
    if (actions == nullptr
        || !actions->just_pressed[static_cast<u16>(input::Action::Interact)]) {
        return;
    }

    const InteractionFocus* focus = world.try_get<InteractionFocus>();
    if (focus == nullptr || focus->target == 0) {
        return;
    }

    InteractEventQueue* q = world.try_get_mut<InteractEventQueue>();
    if (q == nullptr) {
        return;
    }

    flecs::entity_t actor_id = 0;
    world.each([&](flecs::entity e, CharacterController&) {
        if (actor_id != 0 || !e.has<PlayerCharacter>() || e.has<CharacterDead>()) {
            return;
        }
        actor_id = e.id();
    });
    if (actor_id == 0) {
        return;
    }

    (void)q->push(InteractEvent{actor_id, focus->target});
}

void update_on_foot_camera(flecs::world& world)
{
    const ecs::ControlMode* mode = world.try_get<ecs::ControlMode>();
    if (mode == nullptr || mode->mode != ecs::ControlModeKind::OnFoot) {
        return;
    }

    const ecs::FrameInterpolation* fi = world.try_get<ecs::FrameInterpolation>();
    const f32 alpha = (fi != nullptr) ? fi->alpha : 1.f;

    bool      found = false;
    glm::vec3 body_pos{0.f};
    f32       yaw   = 0.f;
    f32       pitch = 0.f;
    bool      third = false;
    f32       height = kDefaultCapsuleHeight;

    world.each([&](flecs::entity e, const CharacterController& cc, const ecs::Position& p,
                   const ecs::PreviousPosition& prev) {
        if (found || !e.has<PlayerCharacter>() || e.has<CharacterDead>()) {
            return;
        }
        const ecs::Position lerped = ecs::lerp_position(prev, p, alpha);
        body_pos = glm::vec3{lerped.x, lerped.y, lerped.z};
        yaw      = cc.yaw;
        pitch    = cc.pitch;
        third    = cc.third_person;
        height   = cc.height;
        found    = true;
    });

    if (!found) {
        return;
    }

    const glm::vec3 forward = forward_from_yaw_pitch(yaw, pitch);
    const glm::vec3 up{0.f, 1.f, 0.f};
    const glm::vec3 head = body_pos + glm::vec3{0.f, height * 0.5f * 0.85f, 0.f};

    bool cam_updated = false;
    world.each([&](ecs::Camera3D& cam) {
        if (cam_updated) {
            return;
        }
        if (third) {
            cam.eye    = head - forward * kThirdPersonDistance + up * (kThirdPersonHeight * 0.25f);
            cam.target = head + forward * 2.f;
        } else {
            cam.eye    = head;
            cam.target = head + forward;
        }
        cam.up    = up;
        cam.yaw   = yaw;
        cam.pitch = pitch;
        cam_updated = true;
    });
}

void cleanup_dead_characters(flecs::world& world)
{
    flecs::entity_t strip[kMaxHealthTargets]{};
    u32             strip_count = 0;

    world.each([&](flecs::entity e, CharacterController& cc) {
        if (!e.has<CharacterDead>()) {
            return;
        }
        cc.velocity = {};
        if (e.has<ecs::InstanceTag>() && strip_count < kMaxHealthTargets) {
            strip[strip_count++] = e.id();
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

void fixed_step(flecs::world& world, f32 dt)
{
    if (dt <= 0.f) {
        return;
    }

    const ecs::InputActions* in = world.try_get<ecs::InputActions>();
    const input::ActionState* actions = (in != nullptr) ? &in->state : nullptr;

    // Sync attached props (hatches) before interact ray / locomotion reads world poses.
    sync_local_to_ship_world(world);

    step_locomotion(world, dt, actions);
    update_interaction_focus(world);
    step_interact_input(world, actions);
    handle_interact_events(world);
    step_fps_combat(world, dt, actions);
    cleanup_dead_characters(world);
}

void register_systems(flecs::world& world)
{
    if (world.try_get<InteractEventQueue>() == nullptr) {
        world.set<InteractEventQueue>(InteractEventQueue{});
    }
    if (world.try_get<InteractionFocus>() == nullptr) {
        world.set<InteractionFocus>(InteractionFocus{});
    }
    if (world.try_get<combat::DamageEventQueue>() == nullptr) {
        world.set<combat::DamageEventQueue>(combat::DamageEventQueue{});
    }
    if (world.try_get<ecs::ControlMode>() == nullptr) {
        world.set<ecs::ControlMode>(ecs::ControlMode{});
    }

    world.system("OnFootCameraSystem")
        .kind(flecs::OnUpdate)
        .run([](flecs::iter& it) {
            flecs::world w = it.world();
            update_on_foot_camera(w);
        });
}

flecs::entity spawn_player_character(
    flecs::world& world, const glm::vec3& world_or_local_pos, flecs::entity_t ship_or_zero)
{
    CharacterController cc{};
    cc.radius     = kDefaultCapsuleRadius;
    cc.height     = kDefaultCapsuleHeight;
    cc.move_speed = kDefaultMoveSpeed;
    cc.eva_thrust = kDefaultEvaThrust;
    cc.yaw        = -1.57079637f; // look down -Z
    cc.grounded   = true;

    Health hp{};
    hp.max_hp = 100.f;
    hp.hp     = 100.f;

    EquippedItem gun{};
    gun.item_id       = 1;
    gun.ammo          = 60;
    gun.ammo_max      = 60;
    gun.damage        = 28.f;
    gun.fire_cooldown = 0.16f;

    glm::vec3 spawn_world = world_or_local_pos;
    glm::quat spawn_ori{1.f, 0.f, 0.f, 0.f};

    flecs::entity e = world.entity("PlayerCharacter")
                          .set<CharacterController>(cc)
                          .set<Health>(hp)
                          .set<EquippedItem>(gun)
                          .set<ecs::Position>(
                              {spawn_world.x, spawn_world.y, spawn_world.z})
                          .set<ecs::PreviousPosition>(
                              {spawn_world.x, spawn_world.y, spawn_world.z})
                          .set<ecs::Velocity>({0.f, 0.f, 0.f})
                          .set<ecs::Orientation>({spawn_ori})
                          .set<ecs::Scale>({0.7f})
                          .add<ecs::InstanceTag>()
                          .add<ecs::KinematicFromRigidBody>()
                          .add<PlayerCharacter>();

    if (ship_or_zero != 0) {
        LocalToShip local{};
        local.ship_entity       = ship_or_zero;
        local.local_position    = world_or_local_pos;
        local.local_orientation = spawn_ori;
        e.set<LocalToShip>(local);

        flight::RigidBody6DOF ship_rb{};
        if (try_get_ship_rb(world, ship_or_zero, ship_rb)) {
            glm::vec3 wpos{};
            glm::quat wori{};
            world_from_local(
                ship_rb, local.local_position, local.local_orientation, wpos, wori);
            write_world_pose(e, wpos, wori, true);
            write_world_pose(e, wpos, wori, false);
        }
    }

    log::log_info(
        log::LogCategory::Core,
        "Spawned player character (ship_attach=%llu) at (%.2f, %.2f, %.2f)",
        static_cast<unsigned long long>(ship_or_zero),
        static_cast<double>(world_or_local_pos.x),
        static_cast<double>(world_or_local_pos.y),
        static_cast<double>(world_or_local_pos.z));

    return e;
}

flecs::entity spawn_health_target(flecs::world& world, const glm::vec3& position, f32 scale)
{
    Health hp{};
    hp.max_hp = 120.f;
    hp.hp     = 120.f;

    const ecs::Position pos{position.x, position.y, position.z};
    flecs::entity target = world.entity("HealthTarget")
                               .set<Health>(hp)
                               .set<ecs::Position>(pos)
                               .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
                               .set<ecs::Velocity>({0.f, 0.f, 0.f})
                               .set<ecs::Scale>({scale})
                               .add<ecs::InstanceTag>();

    log::log_info(
        log::LogCategory::Core,
        "Spawned health target at (%.1f, %.1f, %.1f) hp=%.0f",
        static_cast<double>(position.x),
        static_cast<double>(position.y),
        static_cast<double>(position.z),
        static_cast<double>(hp.hp));
    return target;
}

flecs::entity spawn_gravity_zone_box(
    flecs::world&    world,
    const glm::vec3& center,
    const glm::vec3& half_extents,
    const glm::vec3& gravity_dir,
    f32              magnitude,
    const char*      name)
{
    GravityZone zone{};
    zone.center        = center;
    zone.half_extents  = half_extents;
    zone.gravity_vector = safe_normalize(gravity_dir, glm::vec3{0.f, -1.f, 0.f});
    zone.magnitude     = magnitude;
    zone.shape         = GravityZoneShape::Box;

    flecs::entity e = world.entity(name != nullptr ? name : "GravityZone")
                          .set<GravityZone>(zone)
                          .set<ecs::Position>({center.x, center.y, center.z});
    return e;
}

flecs::entity spawn_ship_hatch(
    flecs::world&    world,
    flecs::entity_t  ship,
    const glm::vec3& local_pos,
    const char*      prompt)
{
    InteractablePrompt pr{};
    copy_prompt(pr.label, sizeof(pr.label), prompt);

    LocalToShip local{};
    local.ship_entity    = ship;
    local.local_position = local_pos;

    ShipHatch hatch{};
    hatch.ship = ship;

    flecs::entity e = world.entity("ShipHatch")
                          .set<LocalToShip>(local)
                          .set<ShipHatch>(hatch)
                          .set<InteractablePrompt>(pr)
                          .set<ecs::Position>({local_pos.x, local_pos.y, local_pos.z})
                          .set<ecs::PreviousPosition>({local_pos.x, local_pos.y, local_pos.z})
                          .set<ecs::Velocity>({0.f, 0.f, 0.f})
                          .set<ecs::Scale>({0.45f})
                          .add<Interactable>()
                          .add<ecs::InstanceTag>()
                          .add<ecs::KinematicFromRigidBody>();

    flight::RigidBody6DOF ship_rb{};
    if (try_get_ship_rb(world, ship, ship_rb)) {
        glm::vec3 wpos{};
        glm::quat wori{};
        world_from_local(ship_rb, local.local_position, local.local_orientation, wpos, wori);
        write_world_pose(e, wpos, wori, true);
        write_world_pose(e, wpos, wori, false);
    }
    return e;
}

flecs::entity spawn_pilot_seat(
    flecs::world&    world,
    flecs::entity_t  ship,
    const glm::vec3& local_pos,
    const char*      prompt)
{
    InteractablePrompt pr{};
    copy_prompt(pr.label, sizeof(pr.label), prompt);

    LocalToShip local{};
    local.ship_entity    = ship;
    local.local_position = local_pos;

    PilotSeat seat{};
    seat.ship = ship;

    flecs::entity e = world.entity("PilotSeat")
                          .set<LocalToShip>(local)
                          .set<PilotSeat>(seat)
                          .set<InteractablePrompt>(pr)
                          .set<ecs::Position>({local_pos.x, local_pos.y, local_pos.z})
                          .set<ecs::PreviousPosition>({local_pos.x, local_pos.y, local_pos.z})
                          .set<ecs::Velocity>({0.f, 0.f, 0.f})
                          .set<ecs::Scale>({0.4f})
                          .add<Interactable>()
                          .add<ecs::InstanceTag>()
                          .add<ecs::KinematicFromRigidBody>();

    flight::RigidBody6DOF ship_rb{};
    if (try_get_ship_rb(world, ship, ship_rb)) {
        glm::vec3 wpos{};
        glm::quat wori{};
        world_from_local(ship_rb, local.local_position, local.local_orientation, wpos, wori);
        write_world_pose(e, wpos, wori, true);
        write_world_pose(e, wpos, wori, false);
    }
    return e;
}

flecs::entity spawn_station_interactable(
    flecs::world& world, const glm::vec3& position, const char* prompt)
{
    InteractablePrompt pr{};
    copy_prompt(pr.label, sizeof(pr.label), prompt);

    const ecs::Position pos{position.x, position.y, position.z};
    return world.entity("StationTerminal")
        .set<InteractablePrompt>(pr)
        .set<ecs::Position>(pos)
        .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
        .set<ecs::Velocity>({0.f, 0.f, 0.f})
        .set<ecs::Scale>({0.6f})
        .add<Interactable>()
        .add<ecs::InstanceTag>();
}

void fill_player_telemetry(
    flecs::world& world,
    f32&          health,
    f32&          health_max,
    u32&          ammo,
    u32&          ammo_max,
    bool&         grounded,
    bool&         eva,
    bool&         found)
{
    found      = false;
    health     = -1.f;
    health_max = 0.f;
    ammo       = 0;
    ammo_max   = 0;
    grounded   = false;
    eva        = false;

    world.each([&](flecs::entity e,
                   const CharacterController& cc,
                   const Health& hp,
                   const EquippedItem& item,
                   const ecs::Position& p) {
        if (found || !e.has<PlayerCharacter>()) {
            return;
        }
        found      = true;
        health     = hp.hp;
        health_max = hp.max_hp;
        ammo       = item.ammo;
        ammo_max   = item.ammo_max;
        grounded   = cc.grounded;

        if (e.has<LocalToShip>()) {
            eva = false;
        } else {
            const SampledGravity g = sample_gravity(world, glm::vec3{p.x, p.y, p.z});
            eva = !g.in_zone || g.magnitude < 0.05f;
        }
    });
}

}  // namespace csc::game::character
