#pragma once

#include "engine/core/types.hpp"

namespace csc::input {

/// Logical button actions — gameplay never binds to raw GLFW keys.
enum class Action : u16 {
    MoveForward = 0,
    MoveBack,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    Fire,
    Interact,
    Thrust,
    ToggleCoupled,
    RollLeft,
    RollRight,
    Pause, // Esc — toggle SimulationPaused / pause menu (P1E)
    QuickSave, // F5 — P1F
    QuickLoad, // F9 — P1F
    Count
};

inline constexpr u16 kActionCount = static_cast<u16>(Action::Count);

/// Continuous axes filled once per frame by `input_poll`.
/// Move* are synthesized from opposite button pairs in [-1, 1].
/// Look* are mouse deltas in pixels (sensitivity applied by consumers).
enum class ActionAxis : u16 {
    LookX = 0,
    LookY,
    MoveX, // Right − Left
    MoveY, // Up − Down
    MoveZ, // Forward − Back
    Count
};

inline constexpr u16 kActionAxisCount = static_cast<u16>(ActionAxis::Count);

/// Fixed-size POD snapshot — no heap; safe to store as a Flecs singleton.
struct ActionState {
    f32  axes[kActionAxisCount]{};
    bool pressed[kActionCount]{};
    bool just_pressed[kActionCount]{};
    bool just_released[kActionCount]{};
};

}  // namespace csc::input
