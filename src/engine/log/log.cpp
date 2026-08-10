#include "engine/log/log.hpp"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <mutex>

#ifndef CSC_DEBUG
#define CSC_DEBUG 0
#endif

namespace csc::log {
namespace {

std::mutex g_log_mutex;

#if CSC_DEBUG
LogLevel g_min_level = LogLevel::Trace;
#else
LogLevel g_min_level = LogLevel::Info;
#endif

const char* level_tag(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO";
    case LogLevel::Warn:  return "WARN";
    case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

const char* category_tag(LogCategory category)
{
    switch (category) {
    case LogCategory::Core:   return "core";
    case LogCategory::Vulkan: return "vulkan";
    case LogCategory::Ecs:    return "ecs";
    case LogCategory::Input:  return "input";
    case LogCategory::Assets: return "assets";
    case LogCategory::Config: return "config";
    case LogCategory::Game:   return "game";
    }
    return "core";
}

void log_v(LogLevel level, LogCategory category, const char* fmt, va_list args)
{
    if (static_cast<i32>(level) < static_cast<i32>(g_min_level)) {
        return;
    }

#if !CSC_DEBUG
    if (level == LogLevel::Trace || level == LogLevel::Debug) {
        return;
    }
#endif

    char message[1024];
    const int written = std::vsnprintf(message, sizeof(message), fmt, args);
    if (written < 0) {
        message[0] = '\0';
    } else if (static_cast<std::size_t>(written) >= sizeof(message)) {
        message[sizeof(message) - 1] = '\0';
    }

    const std::lock_guard<std::mutex> lock(g_log_mutex);
    std::fprintf(stderr, "[%s][%s] %s\n", level_tag(level), category_tag(category), message);
}

}  // namespace

void log_set_min_level(LogLevel level)
{
    const std::lock_guard<std::mutex> lock(g_log_mutex);
    g_min_level = level;
}

LogLevel log_min_level()
{
    const std::lock_guard<std::mutex> lock(g_log_mutex);
    return g_min_level;
}

void log_trace(LogCategory category, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_v(LogLevel::Trace, category, fmt, args);
    va_end(args);
}

void log_debug(LogCategory category, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_v(LogLevel::Debug, category, fmt, args);
    va_end(args);
}

void log_info(LogCategory category, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_v(LogLevel::Info, category, fmt, args);
    va_end(args);
}

void log_warn(LogCategory category, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_v(LogLevel::Warn, category, fmt, args);
    va_end(args);
}

void log_error(LogCategory category, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_v(LogLevel::Error, category, fmt, args);
    va_end(args);
}

}  // namespace csc::log
