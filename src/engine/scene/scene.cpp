#include "engine/scene/scene.hpp"

#include "engine/log/log.hpp"
#include "game/character/character.hpp"
#include "game/flight/flight.hpp"
#include "game/world/world.hpp"

#include <cstdio>
#include <cstring>

namespace csc::scene {
namespace {

bool setup_grid_freelook(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }
    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);
    ctx.needs_shared_mesh = false;
    ctx.instance_count    = 0;
    return true;
}

bool setup_instancing_stress(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }
    constexpr u32 kInstances = 600u;
    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);
    ecs::world_spawn_demo_instances(*ctx.world, kInstances);
    ctx.needs_shared_mesh = true;
    ctx.instance_count    = kInstances;
    return true;
}

bool setup_mesh_viewer(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }
    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);
    ecs::world_spawn_centered_instance(*ctx.world, 1.5f);
    ctx.needs_shared_mesh = true;
    ctx.instance_count    = 1;
    return true;
}

bool setup_flight_test(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }

    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);

    // Ship at (0,5,0); static asteroid ~50m ahead along -Z (ship forward).
    game::flight::spawn_projectile_pool(*ctx.world);
    (void)game::flight::spawn_player_ship(*ctx.world, glm::vec3{0.f, 5.f, 0.f});
    (void)game::flight::spawn_damage_target(*ctx.world, glm::vec3{0.f, 5.f, -50.f}, 5.f);

    ctx.world->set<ecs::ControlMode>({ecs::ControlModeKind::ShipPilot});

    ctx.needs_shared_mesh = true;
    // Player + target + up to kProjectilePoolSize when firing (capacity for gather).
    ctx.instance_count =
        2u + static_cast<u32>(game::flight::kProjectilePoolSize);
    return true;
}

bool setup_on_foot_test(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }

    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);

    game::flight::spawn_projectile_pool(*ctx.world);

    // Moving ship — coasts while OnFoot so LocalToShip interior is visible.
    flecs::entity ship =
        game::flight::spawn_player_ship(*ctx.world, glm::vec3{0.f, 8.f, 0.f});
    if (game::flight::RigidBody6DOF* rb = ship.try_get_mut<game::flight::RigidBody6DOF>()) {
        rb->linear_vel = glm::vec3{1.8f, 0.f, 0.f};
    }
    if (game::flight::FlightControl* fc = ship.try_get_mut<game::flight::FlightControl>()) {
        fc->coupled = false;
    }

    // Player starts inside the ship (authoritative LocalToShip).
    (void)game::character::spawn_player_character(
        *ctx.world, glm::vec3{0.f, 0.9f, 0.f}, ship.id());

    (void)game::character::spawn_ship_hatch(
        *ctx.world, ship.id(), glm::vec3{0.f, 0.9f, 3.2f}, "Exit / Enter hatch");
    (void)game::character::spawn_pilot_seat(
        *ctx.world, ship.id(), glm::vec3{0.f, 0.9f, -1.5f}, "Pilot seat");

    // Interior gravity is artificial (LocalToShip path). Station pad has a world zone.
    constexpr glm::vec3 kStationCenter{45.f, 2.f, 0.f};
    (void)game::character::spawn_gravity_zone_box(
        *ctx.world,
        kStationCenter,
        glm::vec3{12.f, 6.f, 12.f},
        glm::vec3{0.f, -1.f, 0.f},
        game::character::kGravityDefault,
        "StationGravity");

    // Station deck marker + terminal interactable.
    {
        const ecs::Position pos{kStationCenter.x, 0.5f, kStationCenter.z};
        ctx.world->entity("StationDeck")
            .set<ecs::Position>(pos)
            .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
            .set<ecs::Velocity>({0.f, 0.f, 0.f})
            .set<ecs::Scale>({8.f})
            .add<ecs::InstanceTag>();
    }
    (void)game::character::spawn_station_interactable(
        *ctx.world,
        glm::vec3{kStationCenter.x, 1.2f, kStationCenter.z - 3.f},
        "Station terminal");

    // Dummy FPS combat target on the station pad.
    (void)game::character::spawn_health_target(
        *ctx.world, glm::vec3{kStationCenter.x + 4.f, 1.2f, kStationCenter.z}, 1.8f);

    ctx.world->set<ecs::ControlMode>({ecs::ControlModeKind::OnFoot});

    ctx.needs_shared_mesh = true;
    // ship + player + hatch + seat + station deck + terminal + health target + projectiles
    ctx.instance_count = 7u + static_cast<u32>(game::flight::kProjectilePoolSize);
    return true;
}

bool setup_universe_test(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }

    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);

    game::world::StarSystemData system{};
    (void)game::world::load_star_system_config(system, "assets/data/star_system.cfg");

    (void)game::world::spawn_universe_test(*ctx.world, system);

    ctx.needs_shared_mesh = true;
    // Station + star + planet + ship + streamed props (when loaded) + projectiles.
    ctx.instance_count = 16u + static_cast<u32>(game::flight::kProjectilePoolSize);
    return true;
}

constexpr SceneDesc kScenes[] = {
    {"grid_freelook",
     "Free-look camera + ground grid (minimal baseline)",
     &setup_grid_freelook},
    {"instancing_stress",
     ">=500 shared-mesh instances in one draw + grid + camera",
     &setup_instancing_stress},
    {"mesh_viewer",
     "Single centered cube mesh + free-look (glTF upload check)",
     &setup_mesh_viewer},
    {"flight_test",
     "Pilotable ship + thrusters/shields/weapons + damage target (P1A)",
     &setup_flight_test},
    {"on_foot_test",
     "Ship interior → EVA → station gravity + FPS combat (P1B)",
     &setup_on_foot_test},
    {"universe_test",
     "Fixed star system: station ↔ open space (rebase) ↔ planetary LZ (P1D)",
     &setup_universe_test},
};

constexpr std::size_t kSceneCount = sizeof(kScenes) / sizeof(kScenes[0]);

}  // namespace

const SceneDesc* scene_find(const char* name)
{
    if (name == nullptr || name[0] == '\0') {
        return nullptr;
    }
    for (std::size_t i = 0; i < kSceneCount; ++i) {
        if (std::strcmp(kScenes[i].name, name) == 0) {
            return &kScenes[i];
        }
    }
    return nullptr;
}

void scene_list(char* out, std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return;
    }
    out[0] = '\0';
    std::size_t used = 0;
    for (std::size_t i = 0; i < kSceneCount; ++i) {
        const int n = std::snprintf(
            out + used,
            (used < cap) ? (cap - used) : 0,
            "%s%s",
            (i == 0) ? "" : ", ",
            kScenes[i].name);
        if (n < 0) {
            break;
        }
        used += static_cast<std::size_t>(n);
        if (used >= cap) {
            out[cap - 1] = '\0';
            break;
        }
    }
}

bool scene_setup_by_name(const char* name, SceneContext& ctx)
{
    const SceneDesc* desc = scene_find(name);
    if (desc == nullptr) {
        char available[256]{};
        scene_list(available, sizeof(available));
        log::log_warn(
            log::LogCategory::Core,
            "Unknown scene '%s' — available: [%s]. Falling back to grid_freelook.",
            (name != nullptr) ? name : "(null)",
            available);
        desc = scene_find("grid_freelook");
    }
    if (desc == nullptr || desc->setup == nullptr) {
        log::log_error(log::LogCategory::Core, "Scene registry missing grid_freelook.");
        return false;
    }
    log::log_info(
        log::LogCategory::Core,
        "Scene setup: %s — %s",
        desc->name,
        desc->description);
    return desc->setup(ctx);
}

}  // namespace csc::scene
