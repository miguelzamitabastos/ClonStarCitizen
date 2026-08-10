#include "engine/platform/window.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace csc::platform {
namespace {

void framebuffer_size_callback(GLFWwindow* handle, int width, int height)
{
    void* user = glfwGetWindowUserPointer(handle);
    if (user == nullptr) {
        return;
    }
    Window& window = *static_cast<Window*>(user);
    window.width               = width;
    window.height              = height;
    window.framebuffer_resized = true;
}

}  // namespace

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
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* handle = glfwCreateWindow(desc.width, desc.height, desc.title, nullptr, nullptr);
    if (handle == nullptr) {
        return false;
    }

    window.handle              = handle;
    window.width               = desc.width;
    window.height              = desc.height;
    window.framebuffer_resized = false;

    glfwSetWindowUserPointer(handle, &window);
    glfwSetFramebufferSizeCallback(handle, framebuffer_size_callback);
    window_query_framebuffer_size(window);
    return true;
}

void window_destroy(Window& window)
{
    if (window.handle != nullptr) {
        glfwSetFramebufferSizeCallback(window.handle, nullptr);
        glfwSetWindowUserPointer(window.handle, nullptr);
        glfwDestroyWindow(window.handle);
        window.handle = nullptr;
    }
    window.width               = 0;
    window.height              = 0;
    window.framebuffer_resized = false;
}

bool window_should_close(const Window& window)
{
    return window.handle == nullptr || glfwWindowShouldClose(window.handle) == GLFW_TRUE;
}

void window_poll_events()
{
    glfwPollEvents();
}

void window_query_framebuffer_size(Window& window)
{
    if (window.handle == nullptr) {
        window.width  = 0;
        window.height = 0;
        return;
    }
    int fb_w = 0;
    int fb_h = 0;
    glfwGetFramebufferSize(window.handle, &fb_w, &fb_h);
    window.width  = fb_w;
    window.height = fb_h;
}

}  // namespace csc::platform
