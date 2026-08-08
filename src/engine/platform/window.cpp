#include "engine/platform/window.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace csc::platform {

bool window_init_subsystem()
{
    return glfwInit() == GLFW_TRUE;
}

void window_shutdown_subsystem()
{
    glfwTerminate();
}

bool window_create(Window& window, const WindowDesc& desc)
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* handle = glfwCreateWindow(desc.width, desc.height, desc.title, nullptr, nullptr);
    if (handle == nullptr) {
        return false;
    }

    window.handle = handle;
    window.width  = desc.width;
    window.height = desc.height;
    return true;
}

void window_destroy(Window& window)
{
    if (window.handle != nullptr) {
        glfwDestroyWindow(window.handle);
        window.handle = nullptr;
    }
    window.width  = 0;
    window.height = 0;
}

bool window_should_close(const Window& window)
{
    return window.handle == nullptr || glfwWindowShouldClose(window.handle) == GLFW_TRUE;
}

void window_poll_events()
{
    glfwPollEvents();
}

}  // namespace csc::platform
