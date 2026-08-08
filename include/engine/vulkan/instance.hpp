#pragma once

#include "engine/core/types.hpp"
#include "engine/platform/window.hpp"

#ifndef CSC_DEBUG
#define CSC_DEBUG 0
#endif

#include <vulkan/vulkan.h>

namespace csc::vulkan {

/// Vulkan bootstrap state as plain data (instance + debug messenger).
/// Device/swapchain come later; keep this free of entity class hierarchies.
struct InstanceState {
    VkInstance               instance         = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger  = VK_NULL_HANDLE;
    bool                     validation_enabled = false;
};

struct InstanceCreateInfo {
    const char* app_name    = "ClonStarCitizen";
    u32         app_version = VK_MAKE_VERSION(0, 1, 0);
    bool        enable_validation = true;  // honored only when CSC_DEBUG != 0
};

[[nodiscard]] bool instance_create(InstanceState& state, const InstanceCreateInfo& info, const platform::Window& window);
void instance_destroy(InstanceState& state);

}  // namespace csc::vulkan
