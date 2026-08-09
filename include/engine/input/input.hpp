#pragma once

#include "engine/core/types.hpp"
#include "engine/input/actions.hpp"

struct GLFWwindow;

namespace csc::input {

inline constexpr u16 kMaxBindings = 32;

enum class BindingDevice : u8 {
    Keyboard = 0,
    MouseButton,
};

/// One hardware source → one logical Action (table filled at init only).
struct ActionBinding {
    BindingDevice device = BindingDevice::Keyboard;
    i32           code   = 0; // GLFW_KEY_* or GLFW_MOUSE_BUTTON_*
    Action        action = Action::MoveForward;
    bool          active = false;
};

/// Tunables copied from AppConfig at startup (not read every frame from config file).
struct InputConfig {
    f32 mouse_sensitivity = 0.0025f;
    f32 move_speed        = 8.0f;
};

/// Engine-owned input state: fixed binding table + cursor tracking.
/// GLFW lives here; gameplay systems only read ActionState / InputActions.
struct InputSystem {
    GLFWwindow*  window = nullptr;
    InputConfig  config{};
    ActionBinding bindings[kMaxBindings]{};
    u16          binding_count = 0;

    double last_cursor_x   = 0.0;
    double last_cursor_y   = 0.0;
    bool   has_last_cursor = false;

    bool prev_pressed[kActionCount]{};
    ActionState state{};
};

void input_init(InputSystem& sys, GLFWwindow* window);
void input_shutdown(InputSystem& sys);
void input_set_default_bindings(InputSystem& sys);
void input_set_config(InputSystem& sys, const InputConfig& config);

/// Poll GLFW once per frame into `out` (and `sys.state`). No heap allocations.
void input_poll(InputSystem& sys, ActionState& out);

[[nodiscard]] const ActionState& input_actions(const InputSystem& sys);

}  // namespace csc::input
