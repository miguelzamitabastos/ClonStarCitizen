#include "engine/assets/mesh_loader.hpp"
#include "engine/config/config.hpp"
#include "engine/core/types.hpp"
#include "engine/ecs/world.hpp"
#include "engine/input/input.hpp"
#include "engine/log/log.hpp"
#include "engine/memory/arena.hpp"
#include "engine/platform/window.hpp"
#include "engine/vulkan/device.hpp"
#include "engine/vulkan/instance.hpp"
#include "engine/vulkan/renderer.hpp"

#include "game/audio/audio.hpp"
#include "game/character/character.hpp"
#include "game/economy/economy.hpp"
#include "game/flight/flight.hpp"
#include "game/save/save.hpp"
#include "game/ui/ui.hpp"
#include "game/world/world.hpp"

#include <chrono>

namespace {

constexpr std::size_t kLevelArenaBytes = 16u * 1024u * 1024u;
constexpr std::size_t kAssetArenaBytes = 8u * 1024u * 1024u;
constexpr csc::f32    kMaxDeltaSeconds = 0.05f;
constexpr csc::u32    kDemoInstanceCount = 600u;
constexpr const char* kCubeMeshPath = "assets/meshes/cube.gltf";

struct FrameScratch {
    csc::u32 frame_index = 0;
};

void register_game_systems(flecs::world& world)
{
    csc::game::flight::register_systems(world);
    csc::game::character::register_systems(world);
    csc::game::economy::register_systems(world);
    csc::game::world::register_systems(world);
    csc::game::ui::register_systems(world);
    csc::game::audio::register_systems(world);
    csc::game::save::register_systems(world);
}

}  // namespace

int main(int argc, char** argv)
{
    using namespace csc;

    config::AppConfig app_config{};
    config::config_load_defaults(app_config);
    (void)config::config_load_file(app_config, "assets/config/default.cfg");
    config::config_apply_argv(app_config, argc, argv);
    log::log_set_min_level(app_config.log_level);

    memory::Arena level_arena{};
    if (!memory::arena_create(level_arena, kLevelArenaBytes)) {
        log::log_error(log::LogCategory::Core, "Failed to create level arena.");
        return 1;
    }

    memory::Arena asset_arena{};
    if (!memory::arena_create(asset_arena, kAssetArenaBytes)) {
        log::log_error(log::LogCategory::Assets, "Failed to create asset arena.");
        memory::arena_destroy(level_arena);
        return 1;
    }

    flecs::world world{};
    ecs::world_register_systems(world);
    register_game_systems(world);

    ecs::FrameTimeState frame_time{};
    const f32 physics_hz =
        (app_config.physics_fixed_hz > 0.f) ? app_config.physics_fixed_hz : 60.f;
    frame_time.fixed_dt = 1.f / physics_hz;
    ecs::world_set_fixed_dt(world, frame_time.fixed_dt);

    // P0-09: >=500 shared-mesh instances, preallocated at level load (not in the loop).
    ecs::world_spawn_demo_instances(world, kDemoInstanceCount);

    if (!platform::window_init_subsystem()) {
        log::log_error(log::LogCategory::Core, "Failed to initialize GLFW.");
        memory::arena_destroy(asset_arena);
        memory::arena_destroy(level_arena);
        return 1;
    }

    platform::WindowDesc window_desc{};
    window_desc.width  = app_config.window_width;
    window_desc.height = app_config.window_height;
    window_desc.title  = app_config.window_title;

    platform::Window window{};
    if (!platform::window_create(window, window_desc)) {
        log::log_error(log::LogCategory::Core, "Failed to create window.");
        platform::window_shutdown_subsystem();
        memory::arena_destroy(asset_arena);
        memory::arena_destroy(level_arena);
        return 1;
    }

    input::InputSystem input_sys{};
    input::InputConfig input_cfg{};
    input_cfg.mouse_sensitivity = app_config.mouse_sensitivity;
    input_cfg.move_speed        = app_config.move_speed;
    input::input_init(input_sys, window.handle);
    input::input_set_config(input_sys, input_cfg);

    ecs::CameraControlParams cam_params{};
    cam_params.move_speed        = app_config.move_speed;
    cam_params.mouse_sensitivity = app_config.mouse_sensitivity;
    ecs::world_bind_camera_input(world, cam_params);

    const f32 aspect = (window.height > 0)
        ? static_cast<f32>(window.width) / static_cast<f32>(window.height)
        : (16.f / 9.f);
    ecs::world_spawn_default_camera(world, aspect);
    ecs::world_spawn_default_grid(world);

    ecs::Grid3D grid_scratch{};
    if (!ecs::world_try_get_primary_grid(world, grid_scratch)) {
        log::log_error(log::LogCategory::Ecs, "Primary Grid3D missing after spawn.");
        input::input_shutdown(input_sys);
        platform::window_destroy(window);
        platform::window_shutdown_subsystem();
        memory::arena_destroy(asset_arena);
        memory::arena_destroy(level_arena);
        return 1;
    }

    vulkan::InstanceState vk_instance{};
    vulkan::InstanceCreateInfo vk_info{};
    if (!vulkan::instance_create(vk_instance, vk_info, window)) {
        log::log_error(log::LogCategory::Vulkan, "Failed to create Vulkan instance.");
        input::input_shutdown(input_sys);
        platform::window_destroy(window);
        platform::window_shutdown_subsystem();
        memory::arena_destroy(asset_arena);
        memory::arena_destroy(level_arena);
        return 1;
    }

    vulkan::DeviceState vk_device{};
    if (!vulkan::device_create(vk_device, vk_instance, window)) {
        log::log_error(log::LogCategory::Vulkan, "Failed to create Vulkan device / surface.");
        vulkan::instance_destroy(vk_instance);
        input::input_shutdown(input_sys);
        platform::window_destroy(window);
        platform::window_shutdown_subsystem();
        memory::arena_destroy(asset_arena);
        memory::arena_destroy(level_arena);
        return 1;
    }

    vulkan::RendererState vk_renderer{};
    if (!vulkan::renderer_create(vk_renderer, vk_device, window, grid_scratch.desc)) {
        log::log_error(log::LogCategory::Vulkan, "Failed to create Vulkan renderer.");
        vulkan::device_destroy(vk_device, vk_instance);
        vulkan::instance_destroy(vk_instance);
        input::input_shutdown(input_sys);
        platform::window_destroy(window);
        platform::window_shutdown_subsystem();
        memory::arena_destroy(asset_arena);
        memory::arena_destroy(level_arena);
        return 1;
    }

    // P0-07: async glTF load into asset arena; main thread uploads when Ready.
    assets::MeshLoader mesh_loader{};
    if (!assets::mesh_loader_start(mesh_loader, asset_arena, kCubeMeshPath)) {
        log::log_error(log::LogCategory::Assets, "Failed to start cube mesh load.");
        vulkan::renderer_destroy(vk_renderer, vk_device);
        vulkan::device_destroy(vk_device, vk_instance);
        vulkan::instance_destroy(vk_instance);
        input::input_shutdown(input_sys);
        platform::window_destroy(window);
        platform::window_shutdown_subsystem();
        memory::arena_destroy(asset_arena);
        memory::arena_destroy(level_arena);
        return 1;
    }

    // Bootstrap: wait for first mesh (async API used; sync join at init is OK).
    assets::mesh_loader_join(mesh_loader);
    const assets::MeshLoadStatus load_status = assets::mesh_load_status(mesh_loader.slot);
    if (load_status != assets::MeshLoadStatus::Ready) {
        log::log_error(
            log::LogCategory::Assets,
            "Cube mesh not ready (status=%u err=%s).",
            static_cast<u32>(load_status),
            mesh_loader.slot.error);
        assets::mesh_loader_shutdown(mesh_loader);
        vulkan::renderer_destroy(vk_renderer, vk_device);
        vulkan::device_destroy(vk_device, vk_instance);
        vulkan::instance_destroy(vk_instance);
        input::input_shutdown(input_sys);
        platform::window_destroy(window);
        platform::window_shutdown_subsystem();
        memory::arena_destroy(asset_arena);
        memory::arena_destroy(level_arena);
        return 1;
    }

    if (!vulkan::renderer_upload_mesh(vk_renderer, vk_device, mesh_loader.slot.cpu)) {
        log::log_error(log::LogCategory::Vulkan, "Failed to upload cube mesh to GPU.");
        assets::mesh_loader_shutdown(mesh_loader);
        vulkan::renderer_destroy(vk_renderer, vk_device);
        vulkan::device_destroy(vk_device, vk_instance);
        vulkan::instance_destroy(vk_instance);
        input::input_shutdown(input_sys);
        platform::window_destroy(window);
        platform::window_shutdown_subsystem();
        memory::arena_destroy(asset_arena);
        memory::arena_destroy(level_arena);
        return 1;
    }

    log::log_info(
        log::LogCategory::Core,
        "ClonStarCitizen online — scene=%s instances=%u flecs_pos=%zu asset_arena=%zu/%zu "
        "grid_verts=%u pipelines=%u mesh_ready=%d",
        app_config.scene_name,
        ecs::world_instance_count(world),
        ecs::world_alive_count(world),
        memory::arena_bytes_used(asset_arena),
        asset_arena.capacity,
        vk_renderer.grid_vertex_count,
        vk_renderer.pipelines.count,
        vk_renderer.demo_mesh.ready ? 1 : 0);

    FrameScratch scratch{};
    auto previous = std::chrono::steady_clock::now();

    ecs::Camera3D camera_scratch{};
    i32 last_fb_w = window.width;
    i32 last_fb_h = window.height;
    input::ActionState action_scratch{};

    // Fixed stack scratch for instance gather — no heap in the loop.
    glm::mat4 instance_scratch[vulkan::kMaxInstancesPerDrawCall]{};

    while (!platform::window_should_close(window)) {
        platform::window_poll_events();
        platform::window_query_framebuffer_size(window);

        if (window.width != last_fb_w || window.height != last_fb_h) {
            last_fb_w = window.width;
            last_fb_h = window.height;
            if (window.width > 0 && window.height > 0) {
                const f32 new_aspect =
                    static_cast<f32>(window.width) / static_cast<f32>(window.height);
                ecs::world_set_primary_camera_aspect(world, new_aspect);
            }
        }

        input::input_poll(input_sys, action_scratch);
        world.set<ecs::InputActions>({action_scratch});

        const auto now = std::chrono::steady_clock::now();
        f32 dt = std::chrono::duration<f32>(now - previous).count();
        previous = now;
        if (dt > kMaxDeltaSeconds) {
            dt = kMaxDeltaSeconds;
        }

        ecs::world_tick(world, frame_time, dt);

        if (!ecs::world_try_get_primary_camera(world, camera_scratch)) {
            log::log_error(log::LogCategory::Ecs, "Primary Camera3D missing; exiting.");
            break;
        }

        const u32 gathered = ecs::world_gather_instance_transforms(
            world,
            frame_time.alpha,
            instance_scratch,
            vulkan::kMaxInstancesPerDrawCall);
        vulkan::renderer_set_instances(vk_renderer, instance_scratch, gathered);

        if (!vulkan::renderer_draw_frame(
                vk_renderer, vk_device, window, camera_scratch.view, camera_scratch.projection)) {
            log::log_error(log::LogCategory::Vulkan, "Renderer draw failed; exiting.");
            break;
        }

        ++scratch.frame_index;
    }

    assets::mesh_loader_shutdown(mesh_loader);
    vulkan::renderer_destroy(vk_renderer, vk_device);
    vulkan::device_destroy(vk_device, vk_instance);
    vulkan::instance_destroy(vk_instance);
    input::input_shutdown(input_sys);
    platform::window_destroy(window);
    platform::window_shutdown_subsystem();
    memory::arena_destroy(asset_arena);
    memory::arena_destroy(level_arena);
    return 0;
}
