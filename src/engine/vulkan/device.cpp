#include "engine/vulkan/device.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace csc::vulkan {
namespace {

struct QueueFamilyIndices {
    u32  graphics_family = 0;
    u32  present_family  = 0;
    bool has_graphics    = false;
    bool has_present     = false;
};

QueueFamilyIndices find_queue_families(VkPhysicalDevice physical, VkSurfaceKHR surface)
{
    QueueFamilyIndices indices{};

    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());

    for (u32 i = 0; i < count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics_family = i;
            indices.has_graphics    = true;
        }

        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &present_support);
        if (present_support == VK_TRUE) {
            indices.present_family = i;
            indices.has_present    = true;
        }

        if (indices.has_graphics && indices.has_present) {
            break;
        }
    }

    return indices;
}

bool device_supports_swapchain(VkPhysicalDevice physical)
{
    u32 count = 0;
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, available.data());

    for (const VkExtensionProperties& ext : available) {
        if (std::strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            return true;
        }
    }
    return false;
}

bool is_device_suitable(VkPhysicalDevice physical, VkSurfaceKHR surface)
{
    const QueueFamilyIndices indices = find_queue_families(physical, surface);
    if (!indices.has_graphics || !indices.has_present) {
        return false;
    }
    if (!device_supports_swapchain(physical)) {
        return false;
    }

    u32 format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, nullptr);
    u32 present_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &present_count, nullptr);
    return format_count > 0 && present_count > 0;
}

}  // namespace

bool device_create(DeviceState& state, const InstanceState& instance, const platform::Window& window)
{
    state = {};

    if (window.handle == nullptr || instance.instance == VK_NULL_HANDLE) {
        return false;
    }

    if (glfwCreateWindowSurface(instance.instance, window.handle, nullptr, &state.surface) != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan] Failed to create window surface.\n");
        return false;
    }

    u32 device_count = 0;
    vkEnumeratePhysicalDevices(instance.instance, &device_count, nullptr);
    if (device_count == 0) {
        std::fprintf(stderr, "[vulkan] No physical devices with Vulkan support.\n");
        device_destroy(state, instance);
        return false;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance.instance, &device_count, devices.data());

    for (VkPhysicalDevice candidate : devices) {
        if (is_device_suitable(candidate, state.surface)) {
            state.physical_device = candidate;
            break;
        }
    }

    if (state.physical_device == VK_NULL_HANDLE) {
        std::fprintf(stderr, "[vulkan] No suitable GPU found.\n");
        device_destroy(state, instance);
        return false;
    }

    const QueueFamilyIndices indices = find_queue_families(state.physical_device, state.surface);
    state.graphics_queue_family  = indices.graphics_family;
    state.present_queue_family   = indices.present_family;
    state.has_distinct_present_q = indices.graphics_family != indices.present_family;

    const f32 queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_infos[2]{};
    u32 queue_info_count = 0;

    queue_infos[queue_info_count].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_infos[queue_info_count].queueFamilyIndex = indices.graphics_family;
    queue_infos[queue_info_count].queueCount       = 1;
    queue_infos[queue_info_count].pQueuePriorities = &queue_priority;
    ++queue_info_count;

    if (state.has_distinct_present_q) {
        queue_infos[queue_info_count].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_infos[queue_info_count].queueFamilyIndex = indices.present_family;
        queue_infos[queue_info_count].queueCount       = 1;
        queue_infos[queue_info_count].pQueuePriorities = &queue_priority;
        ++queue_info_count;
    }

    VkPhysicalDeviceFeatures features{};

    static constexpr const char* kDeviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    VkDeviceCreateInfo create_info{};
    create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount    = queue_info_count;
    create_info.pQueueCreateInfos       = queue_infos;
    create_info.pEnabledFeatures        = &features;
    create_info.enabledExtensionCount   = 1;
    create_info.ppEnabledExtensionNames = kDeviceExtensions;

#if CSC_DEBUG
    static constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
    if (instance.validation_enabled) {
        create_info.enabledLayerCount   = 1;
        create_info.ppEnabledLayerNames = &kValidationLayer;
    }
#endif

    if (vkCreateDevice(state.physical_device, &create_info, nullptr, &state.device) != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan] vkCreateDevice failed.\n");
        device_destroy(state, instance);
        return false;
    }

    vkGetDeviceQueue(state.device, indices.graphics_family, 0, &state.graphics_queue);
    vkGetDeviceQueue(state.device, indices.present_family, 0, &state.present_queue);
    return true;
}

void device_destroy(DeviceState& state, const InstanceState& instance)
{
    if (state.device != VK_NULL_HANDLE) {
        vkDestroyDevice(state.device, nullptr);
        state.device = VK_NULL_HANDLE;
    }

    if (state.surface != VK_NULL_HANDLE && instance.instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance.instance, state.surface, nullptr);
        state.surface = VK_NULL_HANDLE;
    }

    state.physical_device         = VK_NULL_HANDLE;
    state.graphics_queue          = VK_NULL_HANDLE;
    state.present_queue           = VK_NULL_HANDLE;
    state.graphics_queue_family   = 0;
    state.present_queue_family    = 0;
    state.has_distinct_present_q  = false;
}

}  // namespace csc::vulkan
