#pragma once

#include "engine/core/types.hpp"
#include "engine/input/actions.hpp"

#include <flecs.h>

struct GLFWwindow;

namespace csc::game::ui {

/// P1E-01 decision: Dear ImGui for game HUD/menus this phase (rework look in Fase 6).

enum class HudDisplayMode : u8 {
    Auto = 0, ///< Follow ControlMode (ShipPilot→flight, OnFoot→character)
    Flight,
    OnFoot,
    Both, ///< ui_audio_test: show both HUDs
};

/// Flecs singleton — pause freezes fixed_step / physics accumulator (render continues).
struct SimulationPaused {
    bool paused = false;
};

struct UiMenuState {
    bool pause_open       = false;
    bool system_map_open  = false;
    bool cargo_open       = false;
    bool mission_log_open = false;
    HudDisplayMode hud_mode = HudDisplayMode::Auto;
};

void register_systems(flecs::world& world);

/// Ensure SimulationPaused + UiMenuState singletons exist (level load / init).
void ensure_singletons(flecs::world& world);

[[nodiscard]] bool is_simulation_paused(const flecs::world& world);

/// Handle Esc / menu toggles before sim tick. Unlocks cursor while pause menu open.
void frame_update(
    flecs::world&             world,
    const input::ActionState& actions,
    GLFWwindow*               window,
    bool&                     cursor_unlocked_out);

/// Build game HUD + menus into the current ImGui frame (after debug_ui_begin_frame).
/// Zero heap: char buffers + snprintf only.
void frame_draw(flecs::world& world);

}  // namespace csc::game::ui
