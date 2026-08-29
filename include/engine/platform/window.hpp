#pragma once

#include "engine/core/types.hpp"

struct GLFWwindow;

namespace csc::platform {

struct WindowDesc {
    i32         width  = 1280;
    i32         height = 720;
    const char* title  = "ClonStarCitizen";
    /// Undecorated window covering the primary monitor (borderless fullscreen).
    bool        borderless_fullscreen = false;
};

/// Plain window state — no OOP hierarchy; systems operate on this struct.
struct Window {
    GLFWwindow* handle = nullptr;
    i32         width  = 0;
    i32         height = 0;
    /// Set by GLFW framebuffer-size callback; cleared by the consumer after handling.
    bool        framebuffer_resized = false;
};

[[nodiscard]] bool window_init_subsystem();
void window_shutdown_subsystem();

[[nodiscard]] bool window_create(Window& window, const WindowDesc& desc);
void window_destroy(Window& window);

[[nodiscard]] bool window_should_close(const Window& window);
void window_poll_events();

/// Refresh `width`/`height` from `glfwGetFramebufferSize` (pixel size for Vulkan).
void window_query_framebuffer_size(Window& window);

}  // namespace csc::platform
