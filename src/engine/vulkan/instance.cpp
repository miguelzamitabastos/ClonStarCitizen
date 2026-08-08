#include "engine/vulkan/instance.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace csc::vulkan {
namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT /*severity*/,
    VkDebugUtilsMessageTypeFlagsEXT /*types*/,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* /*user_data*/)
{
    std::fprintf(stderr, "[vulkan] %s\n", callback_data->pMessage);
    return VK_FALSE;
}

bool check_validation_layer_support(const char* layer_name)
{
    u32 count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> available(count);
    vkEnumerateInstanceLayerProperties(&count, available.data());

    for (const VkLayerProperties& layer : available) {
        if (std::strcmp(layer.layerName, layer_name) == 0) {
            return true;
        }
    }
    return false;
}

VkResult create_debug_messenger(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* create_info,
    VkDebugUtilsMessengerEXT* messenger)
{
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (func == nullptr) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    return func(instance, create_info, nullptr, messenger);
}

void destroy_debug_messenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
{
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (func != nullptr && messenger != VK_NULL_HANDLE) {
        func(instance, messenger, nullptr);
    }
}

void fill_debug_create_info(VkDebugUtilsMessengerCreateInfoEXT& info)
{
    info                 = {};
    info.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debug_callback;
}

}  // namespace

bool instance_create(InstanceState& state, const InstanceCreateInfo& info, const platform::Window& window)
{
    state = {};

#if CSC_DEBUG
    const bool want_validation = info.enable_validation;
#else
    const bool want_validation = false;
    (void)info.enable_validation;
#endif

    static constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
    const bool validation_ok = want_validation && check_validation_layer_support(kValidationLayer);
    state.validation_enabled = validation_ok;

    if (want_validation && !validation_ok) {
        std::fprintf(stderr, "[vulkan] Validation layers requested but not available; continuing without them.\n");
    }

    VkApplicationInfo app_info{};
    app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName   = info.app_name;
    app_info.applicationVersion = info.app_version;
    app_info.pEngineName        = "ClonStarCitizen";
    app_info.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion         = VK_API_VERSION_1_2;

    u32 glfw_ext_count = 0;
    const char** glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_ext_count);
    if (glfw_exts == nullptr) {
        std::fprintf(stderr, "[vulkan] glfwGetRequiredInstanceExtensions failed.\n");
        return false;
    }

    std::vector<const char*> extensions(glfw_exts, glfw_exts + glfw_ext_count);
    if (validation_ok) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo create_info{};
    create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo        = &app_info;
    create_info.enabledExtensionCount   = static_cast<u32>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info{};
    if (validation_ok) {
        create_info.enabledLayerCount   = 1;
        create_info.ppEnabledLayerNames = &kValidationLayer;
        fill_debug_create_info(debug_create_info);
        create_info.pNext = &debug_create_info;
    }

    (void)window;  // Window ensures GLFW is up; surface creation comes next milestone.

    const VkResult result = vkCreateInstance(&create_info, nullptr, &state.instance);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan] vkCreateInstance failed (%d).\n", static_cast<int>(result));
        return false;
    }

    if (validation_ok) {
        if (create_debug_messenger(state.instance, &debug_create_info, &state.debug_messenger) != VK_SUCCESS) {
            std::fprintf(stderr, "[vulkan] Failed to set up debug messenger.\n");
            instance_destroy(state);
            return false;
        }
    }

    return true;
}

void instance_destroy(InstanceState& state)
{
    if (state.instance != VK_NULL_HANDLE) {
        destroy_debug_messenger(state.instance, state.debug_messenger);
        state.debug_messenger = VK_NULL_HANDLE;
        vkDestroyInstance(state.instance, nullptr);
        state.instance = VK_NULL_HANDLE;
    }
    state.validation_enabled = false;
}

}  // namespace csc::vulkan
