#pragma once

#include "engine/core/types.hpp"
#include "engine/platform/window.hpp"
#include "engine/vulkan/instance.hpp"

#include <vulkan/vulkan.h>

namespace csc::vulkan {

/// Physical + logical device, queues, and window surface (plain data).
struct DeviceState {
    VkSurfaceKHR     surface                 = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device          = VK_NULL_HANDLE;
    VkDevice         device                  = VK_NULL_HANDLE;
    VkQueue          graphics_queue          = VK_NULL_HANDLE;
    VkQueue          present_queue           = VK_NULL_HANDLE;
    u32              graphics_queue_family   = 0;
    u32              present_queue_family    = 0;
    bool             has_distinct_present_q  = false;
};

[[nodiscard]] bool device_create(
    DeviceState& state,
    const InstanceState& instance,
    const platform::Window& window);

void device_destroy(DeviceState& state, const InstanceState& instance);

}  // namespace csc::vulkan
