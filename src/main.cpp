#include "engine/core/types.hpp"
#include "engine/ecs/world.hpp"
#include "engine/memory/arena.hpp"
#include "engine/platform/window.hpp"
#include "engine/vulkan/device.hpp"
#include "engine/vulkan/instance.hpp"
#include "engine/vulkan/renderer.hpp"

#include <chrono>
#include <cstdio>

namespace {

/// Startup / level-load budget — arenas/pools for non-Flecs buffers.
constexpr std::size_t kLevelArenaBytes = 16u * 1024u * 1024u;

/// Fixed dt clamp to avoid huge steps after stalls (no heap in loop).
constexpr csc::f32 kMaxDeltaSeconds = 0.05f;

struct FrameScratch {
    csc::u32 frame_index = 0;
};

}  // namespace

int main()
{
    using namespace csc;

    memory::Arena level_arena{};
    if (!memory::arena_create(level_arena, kLevelArenaBytes)) {
        std::fprintf(stderr, "Failed to create level arena.\n");
        return 1;
    }

    // Flecs world: constructed once at init (internal setup), never re-created per frame.
    flecs::world world{};
    ecs::world_register_systems(world);
    ecs::world_spawn_demo_entities(world, 3);

    if (!platform::window_init_subsystem()) {
        std::fprintf(stderr, "Failed to initialize GLFW.\n");
        memory::arena_destroy(level_arena);
        return 1;
    }

    platform::Window window{};
    if (!platform::window_create(window, {})) {
        std::fprintf(stderr, "Failed to create window.\n");
        platform::window_shutdown_subsystem();
        memory::arena_destroy(level_arena);
        return 1;
    }

    ecs::world_bind_camera_input(world, window.handle);

    const f32 aspect = (window.height > 0)
        ? static_cast<f32>(window.width) / static_cast<f32>(window.height)
        : (16.f / 9.f);
    ecs::world_spawn_default_camera(world, aspect);
    ecs::world_spawn_default_grid(world);

    ecs::Grid3D grid_scratch{};
    if (!ecs::world_try_get_primary_grid(world, grid_scratch)) {
        std::fprintf(stderr, "Primary Grid3D missing after spawn.\n");
        platform::window_destroy(window);
        platform::window_shutdown_subsystem();
        memory::arena_destroy(level_arena);
        return 1;
    }

    vulkan::InstanceState vk_instance{};
    vulkan::InstanceCreateInfo vk_info{};
    if (!vulkan::instance_create(vk_instance, vk_info, window)) {
        std::fprintf(stderr, "Failed to create Vulkan instance.\n");
        platform::window_destroy(window);
        platform::window_shutdown_subsystem();
        memory::arena_destroy(level_arena);
        return 1;
    }

    vulkan::DeviceState vk_device{};
    if (!vulkan::device_create(vk_device, vk_instance, window)) {
        std::fprintf(stderr, "Failed to create Vulkan device / surface.\n");
        vulkan::instance_destroy(vk_instance);
        platform::window_destroy(window);
        platform::window_shutdown_subsystem();
        memory::arena_destroy(level_arena);
        return 1;
    }

    vulkan::RendererState vk_renderer{};
    if (!vulkan::renderer_create(vk_renderer, vk_device, window, grid_scratch.desc)) {
        std::fprintf(stderr, "Failed to create Vulkan renderer (swapchain/grid path).\n");
        vulkan::device_destroy(vk_device, vk_instance);
        vulkan::instance_destroy(vk_instance);
        platform::window_destroy(window);
        platform::window_shutdown_subsystem();
        memory::arena_destroy(level_arena);
        return 1;
    }

    std::printf(
        "ClonStarCitizen online — flecs_entities=%zu arena_used=%zu/%zu validation=%s "
        "grid_verts=%u clear=%.1f,%.1f,%.1f\n",
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

    while (!platform::window_should_close(window)) {
        platform::window_poll_events();

        const auto now = std::chrono::steady_clock::now();
        f32 dt = std::chrono::duration<f32>(now - previous).count();
        previous = now;
        if (dt > kMaxDeltaSeconds) {
            dt = kMaxDeltaSeconds;
        }

        // Control + UpdateCameraMatrices run inside progress; read VP only afterward.
        ecs::world_progress(world, dt);

        if (!ecs::world_try_get_primary_camera(world, camera_scratch)) {
            std::fprintf(stderr, "Primary Camera3D missing; exiting.\n");
            break;
        }

        // Push-constant path: fresh view/proj each frame (no GPU buffer realloc).
        if (!vulkan::renderer_draw_frame(
                vk_renderer, vk_device, camera_scratch.view, camera_scratch.projection)) {
            std::fprintf(stderr, "Renderer draw failed; exiting.\n");
            break;
        }

        ++scratch.frame_index;
    }

    vulkan::renderer_destroy(vk_renderer, vk_device);
    vulkan::device_destroy(vk_device, vk_instance);
    vulkan::instance_destroy(vk_instance);
    platform::window_destroy(window);
    platform::window_shutdown_subsystem();
    memory::arena_destroy(level_arena);
    return 0;
}
