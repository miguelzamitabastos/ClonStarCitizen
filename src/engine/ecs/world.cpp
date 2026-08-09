#include "engine/ecs/world.hpp"

#include <algorithm>
#include <cmath>

namespace csc::ecs {
namespace {

constexpr float kPitchLimit = 1.55334306f;   // ~89 deg

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
    world.system<Position, const Velocity>("IntegratePosition")
        .each([](flecs::iter& it, size_t /*index*/, Position& p, const Velocity& v) {
            const f32 dt = it.delta_time();
            p.x += v.x * dt;
            p.y += v.y * dt;
            p.z += v.z * dt;
        });

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
}

void world_spawn_demo_entities(flecs::world& world, int count)
{
    for (int i = 0; i < count; ++i) {
        const f32 fi = static_cast<f32>(i);
        world.entity()
            .set<Position>({fi, 0.f, 0.f})
            .set<Velocity>({0.1f * (fi + 1.f), 0.f, 0.f});
    }
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
