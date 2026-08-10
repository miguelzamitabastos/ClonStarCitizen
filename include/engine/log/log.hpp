#pragma once

#include "engine/core/types.hpp"

namespace csc::log {

enum class LogLevel : i32 {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
};

enum class LogCategory : i32 {
    Core = 0,
    Vulkan,
    Ecs,
    Input,
    Assets,
    Config,
    Game,
};

/// Minimum level that will be emitted. Trace/Debug respect CSC_DEBUG by default.
void log_set_min_level(LogLevel level);

[[nodiscard]] LogLevel log_min_level();

void log_trace(LogCategory category, const char* fmt, ...);
void log_debug(LogCategory category, const char* fmt, ...);
void log_info(LogCategory category, const char* fmt, ...);
void log_warn(LogCategory category, const char* fmt, ...);
void log_error(LogCategory category, const char* fmt, ...);

}  // namespace csc::log
