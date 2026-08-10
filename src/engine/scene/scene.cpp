#include "engine/scene/scene.hpp"

#include "engine/log/log.hpp"
#include "game/flight/flight.hpp"

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
    game::flight::spawn_player_ship(*ctx.world, glm::vec3{0.f, 5.f, 0.f});
    game::flight::spawn_damage_target(*ctx.world, glm::vec3{0.f, 5.f, -50.f}, 5.f);

    ctx.world->set<ecs::ControlMode>({ecs::ControlModeKind::ShipPilot});

    ctx.needs_shared_mesh = true;
    // Player + target + up to kProjectilePoolSize when firing (capacity for gather).
    ctx.instance_count =
        2u + static_cast<u32>(game::flight::kProjectilePoolSize);
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
