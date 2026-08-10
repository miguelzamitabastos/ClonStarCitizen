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

/// State at the start of the last fixed physics step (for render interpolation).
struct PreviousPosition {
    f32 x = 0.f;
    f32 y = 0.f;
    f32 z = 0.f;
};

struct Velocity {
    f32 x = 0.f;
    f32 y = 0.f;
    f32 z = 0.f;
};

/// Tag: entity participates in the shared-mesh instanced draw (P0-09).
struct InstanceTag {};

/// Uniform scale for instanced demo meshes (model = T * S).
struct Scale {
    f32 value = 1.f;
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

/// Flecs singleton: remnant alpha for render interpolation (updated by world_tick).
struct FrameInterpolation {
    f32 alpha = 0.f;
};

/// Stack POD accumulator for the fixed-timestep scheduler (no heap in the frame loop).
struct FrameTimeState {
    f32 accumulator         = 0.f;
    f32 fixed_dt            = 1.f / 60.f;
    f32 alpha               = 0.f; // remnant / fixed_dt for render interp
    u32 max_steps_per_frame = 5;   // spiral-of-death guard
};

/// Linear blend Previous→current for renderables (alpha from FrameInterpolation).
[[nodiscard]] inline Position lerp_position(
    const PreviousPosition& prev, const Position& curr, f32 alpha)
{
    return Position{
        prev.x + (curr.x - prev.x) * alpha,
        prev.y + (curr.y - prev.y) * alpha,
        prev.z + (curr.z - prev.z) * alpha,
    };
}

/// Register variable-dt frame systems (camera). Call once at init.
/// Physics integration is NOT registered here — see physics_integrate_positions.
void world_register_systems(flecs::world& world);

/// Set camera tunables and ensure InputActions singleton exists. Cursor disable is in input_init.
void world_bind_camera_input(flecs::world& world, const CameraControlParams& params);

/// Store fixed step duration (from AppConfig.physics_fixed_hz) and ensure FrameInterpolation.
void world_set_fixed_dt(flecs::world& world, f32 fixed_dt);

/// Fixed-step integrate: snapshot Position→PreviousPosition, then apply Velocity * fixed_dt.
/// Call only from the fixed-timestep loop (not from world_progress).
void physics_integrate_positions(flecs::world& world, f32 fixed_dt);

/// Fixed physics steps (accumulator) + one variable-dt world_progress for camera/render systems.
void world_tick(flecs::world& world, FrameTimeState& ft, f32 frame_dt);

/// Pre-create demo entities at level load (not in the frame loop).
void world_spawn_demo_entities(flecs::world& world, int count = 3);

/// Spawn `count` instanced mesh entities on a grid (level load — preallocated slots).
/// Prefer >= 512 (demo uses 600). Entities get Position + PreviousPosition + InstanceTag + Scale.
void world_spawn_demo_instances(flecs::world& world, u32 count);

/// Spawn a single centered instance (mesh_viewer scene) at level load.
void world_spawn_centered_instance(flecs::world& world, f32 scale = 1.5f);

/// Gather interpolated model matrices for InstanceTag entities into a fixed caller buffer.
/// Returns number written (clamped to capacity). No heap — writes into `out_models`.
[[nodiscard]] u32 world_gather_instance_transforms(
    flecs::world& world,
    f32 alpha,
    glm::mat4* out_models,
    u32 capacity);

[[nodiscard]] u32 world_instance_count(const flecs::world& world);

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

/// Advance variable-dt frame systems (camera control + matrices). Physics is separate.
void world_progress(flecs::world& world, f32 dt);

[[nodiscard]] std::size_t world_alive_count(const flecs::world& world);

}  // namespace csc::ecs
