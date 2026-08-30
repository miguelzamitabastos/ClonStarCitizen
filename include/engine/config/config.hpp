#pragma once

#include "engine/core/types.hpp"
#include "engine/log/log.hpp"

namespace csc::config {

inline constexpr std::size_t kConfigStringBytes = 64;

/// POD application settings — filled once at startup (defaults → file → argv).
/// Fixed char buffers; no heap growth after load.
struct AppConfig {
    i32 window_width  = 1280;
    i32 window_height = 720;
    char window_title[kConfigStringBytes]{};

    f32 mouse_sensitivity = 0.0025f;
    f32 move_speed        = 8.0f;
    f32 fov_y_degrees     = 60.0f;
    f32 physics_fixed_hz  = 60.0f;

    char scene_name[kConfigStringBytes]{};
    /// P3-02: ship catalog id the player spawns with in flight-capable demo
    /// scenes (`--ship=<id>` / `ship_id=` in a config file). Empty = the scene's
    /// own default (ship.player.default). The hangar (P3-03) supersedes this.
    char player_ship_id[kConfigStringBytes]{};
    /// P3-06: suit catalog id the player spawns with in on-foot demo scenes
    /// (`--suit=<id>` / `suit_id=`). Empty = suit.flight.standard. A suit locker
    /// (P3-06) supersedes this in-scene.
    char player_suit_id[kConfigStringBytes]{};
    /// P4-01: star-system seed for procedural scenes (`--seed=<n>` / `seed=`).
    /// Empty = use the fixed assets/data/star_system.cfg (Fase 1D behaviour).
    char world_seed[kConfigStringBytes]{};
    /// P4-05: galaxy node index to load in universe_test (`--system=<n>` /
    /// `system=`). Empty = node 0 (the composed home system). Ignored if
    /// world_seed is set.
    char galaxy_system[kConfigStringBytes]{};
    log::LogLevel log_level = log::LogLevel::Info;
    /// Undecorated window at primary monitor resolution — avoids WSLg/WM
    /// decoration quirks with a bordered resizable window.
    bool borderless_fullscreen = false;
};

void config_load_defaults(AppConfig& out);
[[nodiscard]] bool config_load_file(AppConfig& out, const char* path);
void config_apply_argv(AppConfig& out, int argc, char** argv);

}  // namespace csc::config
