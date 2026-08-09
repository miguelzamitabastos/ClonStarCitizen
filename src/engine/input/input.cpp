#include "engine/input/input.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace csc::input {
namespace {

void clear_action_state(ActionState& state)
{
    for (u16 i = 0; i < kActionAxisCount; ++i) {
        state.axes[i] = 0.f;
    }
    for (u16 i = 0; i < kActionCount; ++i) {
        state.pressed[i]       = false;
        state.just_pressed[i]  = false;
        state.just_released[i] = false;
    }
}

void add_binding(InputSystem& sys, BindingDevice device, i32 code, Action action)
{
    if (sys.binding_count >= kMaxBindings) {
        return;
    }
    ActionBinding& b = sys.bindings[sys.binding_count++];
    b.device = device;
    b.code   = code;
    b.action = action;
    b.active = true;
}

[[nodiscard]] bool device_down(GLFWwindow* window, const ActionBinding& b)
{
    if (window == nullptr || !b.active) {
        return false;
    }
    switch (b.device) {
    case BindingDevice::Keyboard:
        return glfwGetKey(window, b.code) == GLFW_PRESS;
    case BindingDevice::MouseButton:
        return glfwGetMouseButton(window, b.code) == GLFW_PRESS;
    }
    return false;
}

}  // namespace

void input_set_default_bindings(InputSystem& sys)
{
    sys.binding_count = 0;

    // WASD movement
    add_binding(sys, BindingDevice::Keyboard, GLFW_KEY_W, Action::MoveForward);
    add_binding(sys, BindingDevice::Keyboard, GLFW_KEY_S, Action::MoveBack);
    add_binding(sys, BindingDevice::Keyboard, GLFW_KEY_A, Action::MoveLeft);
    add_binding(sys, BindingDevice::Keyboard, GLFW_KEY_D, Action::MoveRight);

    // Vertical: Space/E up, Ctrl/Q down. Interact uses F (E is MoveUp).
    add_binding(sys, BindingDevice::Keyboard, GLFW_KEY_SPACE, Action::MoveUp);
    add_binding(sys, BindingDevice::Keyboard, GLFW_KEY_E, Action::MoveUp);
    add_binding(sys, BindingDevice::Keyboard, GLFW_KEY_LEFT_CONTROL, Action::MoveDown);
    add_binding(sys, BindingDevice::Keyboard, GLFW_KEY_Q, Action::MoveDown);

    add_binding(sys, BindingDevice::MouseButton, GLFW_MOUSE_BUTTON_LEFT, Action::Fire);
    add_binding(sys, BindingDevice::Keyboard, GLFW_KEY_F, Action::Interact);
    add_binding(sys, BindingDevice::Keyboard, GLFW_KEY_LEFT_SHIFT, Action::Thrust);
}

void input_set_config(InputSystem& sys, const InputConfig& config)
{
    sys.config = config;
}

void input_init(InputSystem& sys, GLFWwindow* window)
{
    sys.window           = window;
    sys.has_last_cursor  = false;
    sys.last_cursor_x    = 0.0;
    sys.last_cursor_y    = 0.0;
    clear_action_state(sys.state);
    for (u16 i = 0; i < kActionCount; ++i) {
        sys.prev_pressed[i] = false;
    }

    input_set_default_bindings(sys);

    if (window != nullptr) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
    }
}

void input_shutdown(InputSystem& sys)
{
    if (sys.window != nullptr) {
        glfwSetInputMode(sys.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
            glfwSetInputMode(sys.window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        }
    }
    sys.window          = nullptr;
    sys.binding_count   = 0;
    sys.has_last_cursor = false;
    clear_action_state(sys.state);
}

void input_poll(InputSystem& sys, ActionState& out)
{
    clear_action_state(out);

    GLFWwindow* win = sys.window;
    if (win == nullptr) {
        sys.state = out;
        return;
    }

    // Aggregate bindings: OR multiple keys bound to the same action.
    bool down[kActionCount]{};
    for (u16 i = 0; i < sys.binding_count; ++i) {
        const ActionBinding& b = sys.bindings[i];
        if (!b.active) {
            continue;
        }
        const u16 ai = static_cast<u16>(b.action);
        if (ai >= kActionCount) {
            continue;
        }
        if (device_down(win, b)) {
            down[ai] = true;
        }
    }

    for (u16 i = 0; i < kActionCount; ++i) {
        out.pressed[i]       = down[i];
        out.just_pressed[i]  = down[i] && !sys.prev_pressed[i];
        out.just_released[i] = !down[i] && sys.prev_pressed[i];
        sys.prev_pressed[i]  = down[i];
    }

    // Synthesize movement axes from buttons (−1 / 0 / +1).
    const f32 move_x =
        (out.pressed[static_cast<u16>(Action::MoveRight)] ? 1.f : 0.f) -
        (out.pressed[static_cast<u16>(Action::MoveLeft)] ? 1.f : 0.f);
    const f32 move_y =
        (out.pressed[static_cast<u16>(Action::MoveUp)] ? 1.f : 0.f) -
        (out.pressed[static_cast<u16>(Action::MoveDown)] ? 1.f : 0.f);
    const f32 move_z =
        (out.pressed[static_cast<u16>(Action::MoveForward)] ? 1.f : 0.f) -
        (out.pressed[static_cast<u16>(Action::MoveBack)] ? 1.f : 0.f);

    out.axes[static_cast<u16>(ActionAxis::MoveX)] = move_x;
    out.axes[static_cast<u16>(ActionAxis::MoveY)] = move_y;
    out.axes[static_cast<u16>(ActionAxis::MoveZ)] = move_z;

    // Mouse look deltas (pixels). Sensitivity applied by CameraControlSystem.
    double cursor_x = 0.0;
    double cursor_y = 0.0;
    glfwGetCursorPos(win, &cursor_x, &cursor_y);
    if (sys.has_last_cursor) {
        out.axes[static_cast<u16>(ActionAxis::LookX)] =
            static_cast<f32>(cursor_x - sys.last_cursor_x);
        out.axes[static_cast<u16>(ActionAxis::LookY)] =
            static_cast<f32>(cursor_y - sys.last_cursor_y);
    }
    sys.last_cursor_x   = cursor_x;
    sys.last_cursor_y   = cursor_y;
    sys.has_last_cursor = true;

    sys.state = out;
}

const ActionState& input_actions(const InputSystem& sys)
{
    return sys.state;
}

}  // namespace csc::input
