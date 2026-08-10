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
    log::LogLevel log_level = log::LogLevel::Info;
};

void config_load_defaults(AppConfig& out);
[[nodiscard]] bool config_load_file(AppConfig& out, const char* path);
void config_apply_argv(AppConfig& out, int argc, char** argv);

}  // namespace csc::config
