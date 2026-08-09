#pragma once

#include "engine/core/types.hpp"
#include "engine/input/actions.hpp"
#include "engine/math/glm.hpp"
#include "engine/render/grid.hpp"

#include <flecs.h>

#include <cstddef>

namespace csc::ecs {

/// Plain POD components — Flecs stores them contiguously in archetypes.
struct Position {
    f32 x = 0.f;
    f32 y = 0.f;
    f32 z = 0.f;
};

struct Velocity {
    f32 x = 0.f;
    f32 y = 0.f;
    f32 z = 0.f;
};

/// Spatial reference grid on the ground plane (XZ). Contiguous Flecs component storage.
struct Grid3D {
    render::GroundGridDesc desc{};
};

struct Camera3D {
    /// Elevated default so the ground grid is visible at startup.
    glm::vec3 eye{0.f, 8.f, 12.f};
    glm::vec3 target{0.f, 0.f, 0.f};
    glm::vec3 up{0.f, 1.f, 0.f};
    /// Radians; yaw = -pi/2 looks down -Z with the default eye/target.
    float yaw = -1.57079637f;
    /// Slight downward pitch to frame the XZ grid.
    float pitch = -0.55f;
    float fov_y_radians = 1.04719755f; // ~60 deg
    float aspect = 16.f / 9.f;
    float near_plane = 0.1f;
    float far_plane = 250.f;
    /// Written by UpdateCameraMatrices after CameraControlSystem (same frame).
    glm::mat4 view{1.f};
    /// GLM perspective with depth [0,1]; renderer applies Vulkan NDC Y-flip on push.
    glm::mat4 projection{1.f};
};

/// Flecs singleton: logical action snapshot polled once per frame in main.
struct InputActions {
    input::ActionState state{};
};

/// Flecs singleton: camera move/look tunables from AppConfig (set at init).
struct CameraControlParams {
    f32 move_speed        = 8.0f;
    f32 mouse_sensitivity = 0.0025f;
};

/// Register gameplay systems (call once at init).
void world_register_systems(flecs::world& world);

/// Set camera tunables and ensure InputActions singleton exists. Cursor disable is in input_init.
void world_bind_camera_input(flecs::world& world, const CameraControlParams& params);

/// Pre-create demo entities at level load (not in the frame loop).
void world_spawn_demo_entities(flecs::world& world, int count = 3);

/// Spawn a default Camera3D entity with the given aspect ratio (level load).
void world_spawn_default_camera(flecs::world& world, float aspect);

/// Spawn a default Grid3D ground reference (level load — not in the frame loop).
void world_spawn_default_grid(flecs::world& world);

/// Copy the first Camera3D found into `out`. Returns false if none exist.
[[nodiscard]] bool world_try_get_primary_camera(const flecs::world& world, Camera3D& out);

/// Update aspect on the first Camera3D (call when the framebuffer size changes).
/// Projection is recomputed by UpdateCameraMatrices on the next world_progress.
void world_set_primary_camera_aspect(flecs::world& world, float aspect);

/// Copy the first Grid3D found into `out`. Returns false if none exist.
[[nodiscard]] bool world_try_get_primary_grid(const flecs::world& world, Grid3D& out);

/// Advance Flecs systems by `dt` seconds (main loop).
void world_progress(flecs::world& world, f32 dt);

[[nodiscard]] std::size_t world_alive_count(const flecs::world& world);

}  // namespace csc::ecs
