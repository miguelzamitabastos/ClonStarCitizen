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
/// While the OS cursor is disabled (GLFW_CURSOR_DISABLED, set by whoever owns
/// capture this frame — input_init, debug UI's F1 toggle, or the pause/menu
/// cursor unlock in game::ui), look deltas are measured from the window centre
/// and the cursor is warped back every frame instead of trusting GLFW's raw
/// virtual position. A delta is only ever applied if it's a plausible single-
/// frame human motion; anything bigger is dropped outright (not clamped) and
/// the recentre is retried next frame — under WSLg the compositor can take an
/// arbitrary number of frames after launch/focus before it actually starts
/// honouring the cursor warp, and until it does, the raw read is a stale
/// value with no relation to the real cursor. Clamping that instead of
/// dropping it would still apply the same maxed-out delta every single frame
/// for as long as the desync lasts, slamming the camera to its pitch limit
/// almost instantly — confirmed live against a real WSLg window/mouse.
void input_poll(InputSystem& sys, ActionState& out);

[[nodiscard]] const ActionState& input_actions(const InputSystem& sys);

}  // namespace csc::input
