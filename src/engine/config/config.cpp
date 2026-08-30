#include "engine/config/config.hpp"

#include "engine/log/log.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace csc::config {
namespace {

void copy_fixed(char* dest, std::size_t dest_bytes, const char* src)
{
    if (dest == nullptr || dest_bytes == 0) {
        return;
    }
    if (src == nullptr) {
        dest[0] = '\0';
        return;
    }
    std::snprintf(dest, dest_bytes, "%s", src);
}

char* trim_inplace(char* s)
{
    while (*s != '\0' && std::isspace(static_cast<unsigned char>(*s))) {
        ++s;
    }
    if (*s == '\0') {
        return s;
    }
    char* end = s + std::strlen(s);
    while (end > s && std::isspace(static_cast<unsigned char>(end[-1]))) {
        --end;
    }
    *end = '\0';
    return s;
}

bool parse_log_level(const char* text, log::LogLevel& out)
{
    if (text == nullptr) {
        return false;
    }
    if (std::strcmp(text, "trace") == 0 || std::strcmp(text, "TRACE") == 0) {
        out = log::LogLevel::Trace;
        return true;
    }
    if (std::strcmp(text, "debug") == 0 || std::strcmp(text, "DEBUG") == 0) {
        out = log::LogLevel::Debug;
        return true;
    }
    if (std::strcmp(text, "info") == 0 || std::strcmp(text, "INFO") == 0) {
        out = log::LogLevel::Info;
        return true;
    }
    if (std::strcmp(text, "warn") == 0 || std::strcmp(text, "WARN") == 0
        || std::strcmp(text, "warning") == 0 || std::strcmp(text, "WARNING") == 0) {
        out = log::LogLevel::Warn;
        return true;
    }
    if (std::strcmp(text, "error") == 0 || std::strcmp(text, "ERROR") == 0) {
        out = log::LogLevel::Error;
        return true;
    }
    return false;
}

bool apply_kv(AppConfig& out, const char* key, const char* value)
{
    if (key == nullptr || value == nullptr || key[0] == '\0') {
        return false;
    }

    if (std::strcmp(key, "window_width") == 0) {
        int v = 0;
        if (std::sscanf(value, "%d", &v) == 1 && v > 0) {
            out.window_width = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "window_height") == 0) {
        int v = 0;
        if (std::sscanf(value, "%d", &v) == 1 && v > 0) {
            out.window_height = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "window_title") == 0) {
        copy_fixed(out.window_title, sizeof(out.window_title), value);
        return true;
    }
    if (std::strcmp(key, "mouse_sensitivity") == 0) {
        float v = 0.f;
        if (std::sscanf(value, "%f", &v) == 1 && v > 0.f) {
            out.mouse_sensitivity = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "move_speed") == 0) {
        float v = 0.f;
        if (std::sscanf(value, "%f", &v) == 1 && v > 0.f) {
            out.move_speed = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "fov_y_degrees") == 0) {
        float v = 0.f;
        if (std::sscanf(value, "%f", &v) == 1 && v > 1.f && v < 179.f) {
            out.fov_y_degrees = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "physics_fixed_hz") == 0) {
        float v = 0.f;
        if (std::sscanf(value, "%f", &v) == 1 && v > 0.f) {
            out.physics_fixed_hz = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "scene_name") == 0 || std::strcmp(key, "scene") == 0) {
        copy_fixed(out.scene_name, sizeof(out.scene_name), value);
        return true;
    }
    if (std::strcmp(key, "player_ship_id") == 0 || std::strcmp(key, "ship_id") == 0) {
        copy_fixed(out.player_ship_id, sizeof(out.player_ship_id), value);
        return true;
    }
    if (std::strcmp(key, "player_suit_id") == 0 || std::strcmp(key, "suit_id") == 0) {
        copy_fixed(out.player_suit_id, sizeof(out.player_suit_id), value);
        return true;
    }
    if (std::strcmp(key, "log_level") == 0) {
        log::LogLevel level = out.log_level;
        if (parse_log_level(value, level)) {
            out.log_level = level;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "borderless_fullscreen") == 0) {
        if (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0
            || std::strcmp(value, "yes") == 0 || std::strcmp(value, "on") == 0) {
            out.borderless_fullscreen = true;
            return true;
        }
        if (std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0
            || std::strcmp(value, "no") == 0 || std::strcmp(value, "off") == 0) {
            out.borderless_fullscreen = false;
            return true;
        }
        return false;
    }
    return false;
}

}  // namespace

void config_load_defaults(AppConfig& out)
{
    out = AppConfig{};
    copy_fixed(out.window_title, sizeof(out.window_title), "ClonStarCitizen");
    copy_fixed(out.scene_name, sizeof(out.scene_name), "grid_freelook");
    out.window_width      = 1280;
    out.window_height     = 720;
    out.mouse_sensitivity = 0.0025f;
    out.move_speed        = 8.0f;
    out.fov_y_degrees     = 60.0f;
    out.physics_fixed_hz  = 60.0f;
#if CSC_DEBUG
    out.log_level = log::LogLevel::Debug;
#else
    out.log_level = log::LogLevel::Info;
#endif
}

bool config_load_file(AppConfig& out, const char* path)
{
    if (path == nullptr || path[0] == '\0') {
        log::log_error(log::LogCategory::Config, "config_load_file: empty path");
        return false;
    }

    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        log::log_warn(log::LogCategory::Config, "Config file not found: %s (keeping current values)", path);
        return false;
    }

    char line[512];
    int line_no = 0;
    while (std::fgets(line, static_cast<int>(sizeof(line)), file) != nullptr) {
        ++line_no;
        char* trimmed = trim_inplace(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        char* eq = std::strchr(trimmed, '=');
        if (eq == nullptr) {
            log::log_warn(log::LogCategory::Config, "%s:%d: missing '='", path, line_no);
            continue;
        }
        *eq = '\0';
        char* key = trim_inplace(trimmed);
        char* value = trim_inplace(eq + 1);
        if (!apply_kv(out, key, value)) {
            log::log_warn(log::LogCategory::Config, "%s:%d: unknown or invalid key '%s'", path, line_no, key);
        }
    }

    std::fclose(file);
    log::log_info(log::LogCategory::Config, "Loaded config from %s", path);
    return true;
}

void config_apply_argv(AppConfig& out, int argc, char** argv)
{
    if (argv == nullptr) {
        return;
    }

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (arg == nullptr) {
            continue;
        }

        static constexpr const char kScenePrefix[] = "--scene=";
        static constexpr const char kShipPrefix[] = "--ship=";
        static constexpr const char kSuitPrefix[] = "--suit=";
        static constexpr const char kConfigPrefix[] = "--config=";
        static constexpr const char kLogPrefix[] = "--log-level=";

        if (std::strncmp(arg, kScenePrefix, sizeof(kScenePrefix) - 1) == 0) {
            apply_kv(out, "scene_name", arg + (sizeof(kScenePrefix) - 1));
            continue;
        }
        if (std::strcmp(arg, "--scene") == 0 && i + 1 < argc && argv[i + 1] != nullptr) {
            apply_kv(out, "scene_name", argv[++i]);
            continue;
        }
        if (std::strncmp(arg, kShipPrefix, sizeof(kShipPrefix) - 1) == 0) {
            apply_kv(out, "player_ship_id", arg + (sizeof(kShipPrefix) - 1));
            continue;
        }
        if (std::strcmp(arg, "--ship") == 0 && i + 1 < argc && argv[i + 1] != nullptr) {
            apply_kv(out, "player_ship_id", argv[++i]);
            continue;
        }
        if (std::strncmp(arg, kSuitPrefix, sizeof(kSuitPrefix) - 1) == 0) {
            apply_kv(out, "player_suit_id", arg + (sizeof(kSuitPrefix) - 1));
            continue;
        }
        if (std::strcmp(arg, "--suit") == 0 && i + 1 < argc && argv[i + 1] != nullptr) {
            apply_kv(out, "player_suit_id", argv[++i]);
            continue;
        }
        if (std::strncmp(arg, kConfigPrefix, sizeof(kConfigPrefix) - 1) == 0) {
            (void)config_load_file(out, arg + (sizeof(kConfigPrefix) - 1));
            continue;
        }
        if (std::strncmp(arg, kLogPrefix, sizeof(kLogPrefix) - 1) == 0) {
            apply_kv(out, "log_level", arg + (sizeof(kLogPrefix) - 1));
            continue;
        }
        if (std::strcmp(arg, "--log-level") == 0 && i + 1 < argc && argv[i + 1] != nullptr) {
            apply_kv(out, "log_level", argv[++i]);
            continue;
        }
        if (std::strcmp(arg, "--borderless") == 0) {
            out.borderless_fullscreen = true;
            continue;
        }
        if (std::strcmp(arg, "--windowed") == 0) {
            out.borderless_fullscreen = false;
            continue;
        }
    }
}

}  // namespace csc::config
