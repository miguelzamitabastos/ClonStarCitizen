#include "engine/ecs/world.hpp"

#include <algorithm>
#include <cmath>

namespace csc::ecs {
namespace {

constexpr float kPitchLimit = 1.55334306f;   // ~89 deg

/// Stored by world_set_fixed_dt; world_tick prefers FrameTimeState::fixed_dt from the caller.
struct FixedDt {
    f32 seconds = 1.f / 60.f;
};

FixedStepFn g_fixed_step_hook = nullptr;

[[nodiscard]] glm::vec3 forward_from_yaw_pitch(float yaw, float pitch)
{
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    return glm::normalize(glm::vec3{cy * cp, sp, sy * cp});
}

}  // namespace

void world_register_systems(flecs::world& world)
{
    // P0-04: Physics is NOT a Flecs OnUpdate system. A manual fixed-dt accumulator in
    // world_tick calls physics_integrate_positions so camera systems can keep variable
    // frame_dt via world.progress (responsive look). Flecs phases alone cannot give one
    // progress() call two different dts.

    // Ordered phases: control eye/target first, then rebuild view/proj.
    const flecs::entity camera_control_phase = world.entity("CameraControlPhase")
        .add(flecs::Phase)
        .depends_on(flecs::OnUpdate);

    const flecs::entity camera_matrices_phase = world.entity("CameraMatricesPhase")
        .add(flecs::Phase)
        .depends_on(camera_control_phase);

    world.system<Camera3D>("CameraControlSystem")
        .kind(camera_control_phase)
        .each([](flecs::iter& it, size_t /*index*/, Camera3D& cam) {
            const ControlMode* mode = it.world().try_get<ControlMode>();
            if (mode != nullptr && mode->mode != ControlModeKind::FreeLook) {
                return;
            }

            const InputActions* actions = it.world().try_get<InputActions>();
            if (actions == nullptr) {
                return;
            }

            const CameraControlParams* params = it.world().try_get<CameraControlParams>();
            const f32 move_speed = (params != nullptr) ? params->move_speed : 8.0f;
            const f32 look_sens  = (params != nullptr) ? params->mouse_sensitivity : 0.0025f;

            const input::ActionState& in = actions->state;
            const f32 dt = it.delta_time();

            const f32 look_x = in.axes[static_cast<u16>(input::ActionAxis::LookX)];
            const f32 look_y = in.axes[static_cast<u16>(input::ActionAxis::LookY)];
            cam.yaw += look_x * look_sens;
            cam.pitch -= look_y * look_sens;
            cam.pitch = std::clamp(cam.pitch, -kPitchLimit, kPitchLimit);

            const glm::vec3 forward = forward_from_yaw_pitch(cam.yaw, cam.pitch);
            const glm::vec3 world_up{0.f, 1.f, 0.f};
            glm::vec3 right = glm::cross(forward, world_up);
            const float right_len2 = glm::dot(right, right);
            if (right_len2 > 1e-8f) {
                right *= 1.f / std::sqrt(right_len2);
            } else {
                right = glm::vec3{1.f, 0.f, 0.f};
            }

            // Axes from InputSystem: MoveX=straferight, MoveY=up, MoveZ=forward.
            const f32 axis_x = in.axes[static_cast<u16>(input::ActionAxis::MoveX)];
            const f32 axis_y = in.axes[static_cast<u16>(input::ActionAxis::MoveY)];
            const f32 axis_z = in.axes[static_cast<u16>(input::ActionAxis::MoveZ)];

            glm::vec3 move = forward * axis_z + right * axis_x + world_up * axis_y;

            // Optional thrust boost (logical action — not a raw key).
            f32 speed = move_speed;
            if (in.pressed[static_cast<u16>(input::Action::Thrust)]) {
                speed *= 2.f;
            }

            const float move_len2 = glm::dot(move, move);
            if (move_len2 > 1e-8f) {
                move *= (speed * dt) / std::sqrt(move_len2);
                cam.eye += move;
            }

            cam.target = cam.eye + forward;
            cam.up = world_up;
        });

    world.system<Camera3D>("UpdateCameraMatrices")
        .kind(camera_matrices_phase)
        .each([](Camera3D& cam) {
            cam.view = glm::lookAt(cam.eye, cam.target, cam.up);
            cam.projection = glm::perspective(
                cam.fov_y_radians, cam.aspect, cam.near_plane, cam.far_plane);
        });
}

void world_bind_camera_input(flecs::world& world, const CameraControlParams& params)
{
    world.set<CameraControlParams>(params);
    world.set<InputActions>(InputActions{});
    if (world.try_get<ControlMode>() == nullptr) {
        world.set<ControlMode>(ControlMode{});
    }
}

void world_set_fixed_dt(flecs::world& world, f32 fixed_dt)
{
    const f32 dt = (fixed_dt > 0.f) ? fixed_dt : (1.f / 60.f);
    world.set<FixedDt>({dt});
    world.set<FrameInterpolation>({0.f});
}

void world_set_fixed_step_hook(FixedStepFn fn)
{
    g_fixed_step_hook = fn;
}

void physics_integrate_positions(flecs::world& world, f32 fixed_dt)
{
    // Contiguous archetype iteration — no heap. Snapshot then integrate with fixed_dt.
    // Ships tagged KinematicFromRigidBody are integrated by flight::fixed_step instead.
    world.each([fixed_dt](flecs::entity e, PreviousPosition& prev, Position& p, const Velocity& v) {
        if (e.has<KinematicFromRigidBody>()) {
            return;
        }
        prev.x = p.x;
        prev.y = p.y;
        prev.z = p.z;
        p.x += v.x * fixed_dt;
        p.y += v.y * fixed_dt;
        p.z += v.z * fixed_dt;
    });
}

void world_tick(flecs::world& world, FrameTimeState& ft, f32 frame_dt)
{
    if (const FixedDt* stored = world.try_get<FixedDt>()) {
        if (stored->seconds > 0.f) {
            ft.fixed_dt = stored->seconds;
        }
    }
    if (ft.fixed_dt <= 0.f) {
        ft.fixed_dt = 1.f / 60.f;
    }
    if (ft.max_steps_per_frame == 0) {
        ft.max_steps_per_frame = 1;
    }

    const f32 clamped_frame = (frame_dt > 0.f) ? frame_dt : 0.f;
    ft.accumulator += clamped_frame;

    // Spiral-of-death guard: never simulate more than max_steps worth of time.
    const f32 max_acc = ft.fixed_dt * static_cast<f32>(ft.max_steps_per_frame);
    if (ft.accumulator > max_acc) {
        ft.accumulator = max_acc;
    }

    u32 steps = 0;
    while (ft.accumulator >= ft.fixed_dt && steps < ft.max_steps_per_frame) {
        physics_integrate_positions(world, ft.fixed_dt);
        if (g_fixed_step_hook != nullptr) {
            g_fixed_step_hook(world, ft.fixed_dt);
        }
        ft.accumulator -= ft.fixed_dt;
        ++steps;
    }

    ft.alpha = ft.accumulator / ft.fixed_dt;
    world.set<FrameInterpolation>({ft.alpha});

    // Variable frame dt: camera control stays responsive (not locked to physics hz).
    world_progress(world, clamped_frame);
}

void world_spawn_demo_entities(flecs::world& world, int count)
{
    for (int i = 0; i < count; ++i) {
        const f32 fi = static_cast<f32>(i);
        const Position pos{fi, 0.f, 0.f};
        world.entity()
            .set<Position>(pos)
            .set<PreviousPosition>({pos.x, pos.y, pos.z})
            .set<Velocity>({0.1f * (fi + 1.f), 0.f, 0.f});
    }
}

void world_spawn_demo_instances(flecs::world& world, u32 count)
{
    // Grid on XZ above the ground plane — level load only, no frame-loop growth.
    constexpr f32 kSpacing = 1.25f;
    constexpr f32 kY       = 0.5f;
    const u32 side = static_cast<u32>(std::ceil(std::sqrt(static_cast<f32>(count))));

    for (u32 i = 0; i < count; ++i) {
        const u32 gx = i % side;
        const u32 gz = i / side;
        const f32 x = (static_cast<f32>(gx) - static_cast<f32>(side) * 0.5f) * kSpacing;
        const f32 z = (static_cast<f32>(gz) - static_cast<f32>(side) * 0.5f) * kSpacing;
        const Position pos{x, kY, z};

        // Gentle drift so interpolation is visible without heap work.
        const f32 phase = static_cast<f32>(i) * 0.017f;
        const Velocity vel{
            0.15f * std::cos(phase),
            0.f,
            0.15f * std::sin(phase),
        };

        world.entity()
            .set<Position>(pos)
            .set<PreviousPosition>({pos.x, pos.y, pos.z})
            .set<Velocity>(vel)
            .set<Scale>({0.55f})
            .add<InstanceTag>();
    }
}

void world_spawn_centered_instance(flecs::world& world, f32 scale)
{
    const Position pos{0.f, 0.75f, 0.f};
    world.entity()
        .set<Position>(pos)
        .set<PreviousPosition>({pos.x, pos.y, pos.z})
        .set<Velocity>({0.f, 0.f, 0.f})
        .set<Scale>({scale})
        .add<InstanceTag>();
}

u32 world_gather_instance_transforms(
    flecs::world& world, f32 alpha, glm::mat4* out_models, u32 capacity)
{
    if (out_models == nullptr || capacity == 0) {
        return 0;
    }

    u32 written = 0;
    // Scale is only on InstanceTag entities (level-load spawn). Avoid binding empty tags.
    world.each([&](flecs::entity e, const Position& p, const PreviousPosition& prev, const Scale& scale) {
        if (written >= capacity) {
            return;
        }
        if (!e.has<InstanceTag>()) {
            return;
        }
        const Position lerped = lerp_position(prev, p, alpha);
        const glm::mat4 T = glm::translate(
            glm::mat4(1.f), glm::vec3{lerped.x, lerped.y, lerped.z});
        const glm::mat4 S = glm::scale(glm::mat4(1.f), glm::vec3{scale.value});
        if (const Orientation* ori = e.try_get<Orientation>()) {
            const glm::mat4 R = glm::mat4_cast(ori->q);
            out_models[written++] = T * R * S;
        } else {
            out_models[written++] = T * S;
        }
    });
    return written;
}

u32 world_instance_count(const flecs::world& world)
{
    return static_cast<u32>(world.count<InstanceTag>());
}

void world_spawn_default_camera(flecs::world& world, float aspect)
{
    Camera3D cam{};
    cam.aspect = aspect;
    const glm::vec3 forward = forward_from_yaw_pitch(cam.yaw, cam.pitch);
    cam.target = cam.eye + forward;
    world.entity().set<Camera3D>(cam);
}

void world_spawn_default_grid(flecs::world& world)
{
    Grid3D grid{};
    grid.desc.half_extent = 20.f;
    grid.desc.cell_size   = 1.f;
    grid.desc.y           = 0.f;
    grid.desc.visible     = true;
    world.entity().set<Grid3D>(grid);
}

bool world_try_get_primary_camera(const flecs::world& world, Camera3D& out)
{
    bool found = false;
    world.each([&](const Camera3D& cam) {
        if (found) {
            return;
        }
        out = cam;
        found = true;
    });
    return found;
}

void world_set_primary_camera_aspect(flecs::world& world, float aspect)
{
    bool updated = false;
    world.each([&](Camera3D& cam) {
        if (updated) {
            return;
        }
        cam.aspect = aspect;
        updated    = true;
    });
}

bool world_try_get_primary_grid(const flecs::world& world, Grid3D& out)
{
    bool found = false;
    world.each([&](const Grid3D& grid) {
        if (found) {
            return;
        }
        out = grid;
        found = true;
    });
    return found;
}

void world_progress(flecs::world& world, f32 dt)
{
    world.progress(dt);
}

std::size_t world_alive_count(const flecs::world& world)
{
    return static_cast<std::size_t>(world.count<Position>());
}

}  // namespace csc::ecs
