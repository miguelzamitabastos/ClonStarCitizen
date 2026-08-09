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

/// Startup / level-load budget — arenas/pools for non-Flecs buffers.
constexpr std::size_t kLevelArenaBytes = 16u * 1024u * 1024u;

/// Fixed dt clamp to avoid huge steps after stalls (no heap in loop).
constexpr csc::f32 kMaxDeltaSeconds = 0.05f;

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

    // Flecs world: constructed once at init (internal setup), never re-created per frame.
    flecs::world world{};
    ecs::world_register_systems(world);
    register_game_systems(world);
    ecs::world_spawn_demo_entities(world, 3);

    if (!platform::window_init_subsystem()) {
        log::log_error(log::LogCategory::Core, "Failed to initialize GLFW.");
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
        memory::arena_destroy(level_arena);
        return 1;
    }

    vulkan::RendererState vk_renderer{};
    if (!vulkan::renderer_create(vk_renderer, vk_device, window, grid_scratch.desc)) {
        log::log_error(log::LogCategory::Vulkan, "Failed to create Vulkan renderer (swapchain/grid path).");
        vulkan::device_destroy(vk_device, vk_instance);
        vulkan::instance_destroy(vk_instance);
        input::input_shutdown(input_sys);
        platform::window_destroy(window);
        platform::window_shutdown_subsystem();
        memory::arena_destroy(level_arena);
        return 1;
    }

    log::log_info(
        log::LogCategory::Core,
        "ClonStarCitizen online — scene=%s flecs_entities=%zu arena_used=%zu/%zu validation=%s "
        "grid_verts=%u clear=%.1f,%.1f,%.1f",
        app_config.scene_name,
        ecs::world_alive_count(world),
        memory::arena_bytes_used(level_arena),
        level_arena.capacity,
        vk_instance.validation_enabled ? "on" : "off",
        vk_renderer.grid_vertex_count,
        vulkan::kClearR,
        vulkan::kClearG,
        vulkan::kClearB);

    FrameScratch scratch{};
    auto previous = std::chrono::steady_clock::now();

    // Stack POD for camera read — no heap in the loop.
    ecs::Camera3D camera_scratch{};
    i32 last_fb_w = window.width;
    i32 last_fb_h = window.height;

    // Stack POD for action snapshot — filled once per frame, no heap in the loop.
    input::ActionState action_scratch{};

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

        // Poll hardware → logical actions once; gameplay reads InputActions only.
        input::input_poll(input_sys, action_scratch);
        world.set<ecs::InputActions>({action_scratch});

        const auto now = std::chrono::steady_clock::now();
        f32 dt = std::chrono::duration<f32>(now - previous).count();
        previous = now;
        if (dt > kMaxDeltaSeconds) {
            dt = kMaxDeltaSeconds;
        }

        // Control + UpdateCameraMatrices run inside progress; read VP only afterward.
        ecs::world_progress(world, dt);

        if (!ecs::world_try_get_primary_camera(world, camera_scratch)) {
            log::log_error(log::LogCategory::Ecs, "Primary Camera3D missing; exiting.");
            break;
        }

        // Push-constant path: fresh view/proj each frame (no GPU buffer realloc).
        // Recreates swapchain on resize / OUT_OF_DATE; skips when minimized (0x0).
        if (!vulkan::renderer_draw_frame(
                vk_renderer, vk_device, window, camera_scratch.view, camera_scratch.projection)) {
            log::log_error(log::LogCategory::Vulkan, "Renderer draw failed; exiting.");
            break;
        }

        ++scratch.frame_index;
    }

    vulkan::renderer_destroy(vk_renderer, vk_device);
    vulkan::device_destroy(vk_device, vk_instance);
    vulkan::instance_destroy(vk_instance);
    input::input_shutdown(input_sys);
    platform::window_destroy(window);
    platform::window_shutdown_subsystem();
    memory::arena_destroy(level_arena);
    return 0;
}
