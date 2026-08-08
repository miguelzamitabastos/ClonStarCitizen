#include "engine/ecs/world.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace csc::ecs {
namespace {

constexpr float kMoveSpeedUnitsPerSec = 5.f;
constexpr float kMouseSensitivity = 0.0025f; // radians per pixel
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
            CameraInputContext* ctx = it.world().try_get_mut<CameraInputContext>();
            if (ctx == nullptr || ctx->window == nullptr) {
                return;
            }

            GLFWwindow* win = ctx->window;
            const f32 dt = it.delta_time();

            double cursor_x = 0.0;
            double cursor_y = 0.0;
            glfwGetCursorPos(win, &cursor_x, &cursor_y);
            if (ctx->has_last_cursor) {
                const float dx = static_cast<float>(cursor_x - ctx->last_cursor_x);
                const float dy = static_cast<float>(cursor_y - ctx->last_cursor_y);
                cam.yaw += dx * kMouseSensitivity;
                cam.pitch -= dy * kMouseSensitivity;
                cam.pitch = std::clamp(cam.pitch, -kPitchLimit, kPitchLimit);
            }
            ctx->last_cursor_x = cursor_x;
            ctx->last_cursor_y = cursor_y;
            ctx->has_last_cursor = true;

            const glm::vec3 forward = forward_from_yaw_pitch(cam.yaw, cam.pitch);
            const glm::vec3 world_up{0.f, 1.f, 0.f};
            glm::vec3 right = glm::cross(forward, world_up);
            const float right_len2 = glm::dot(right, right);
            if (right_len2 > 1e-8f) {
                right *= 1.f / std::sqrt(right_len2);
            } else {
                right = glm::vec3{1.f, 0.f, 0.f};
            }

            // FPS-style: WASD along look-relative axes; Space/E up, Ctrl/Q down.
            glm::vec3 move{0.f, 0.f, 0.f};
            if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) {
                move += forward;
            }
            if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) {
                move -= forward;
            }
            if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) {
                move += right;
            }
            if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) {
                move -= right;
            }
            if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS ||
                glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS) {
                move += world_up;
            }
            if (glfwGetKey(win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS) {
                move -= world_up;
            }

            const float move_len2 = glm::dot(move, move);
            if (move_len2 > 1e-8f) {
                move *= (kMoveSpeedUnitsPerSec * dt) / std::sqrt(move_len2);
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

void world_bind_camera_input(flecs::world& world, GLFWwindow* window)
{
    CameraInputContext ctx{};
    ctx.window = window;
    ctx.has_last_cursor = false;
    world.set<CameraInputContext>(ctx);

    if (window != nullptr) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
    }
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

void world_progress(flecs::world& world, f32 dt)
{
    world.progress(dt);
}

std::size_t world_alive_count(const flecs::world& world)
{
    return static_cast<std::size_t>(world.count<Position>());
}

}  // namespace csc::ecs
