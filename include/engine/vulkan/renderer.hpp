#pragma once

#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"
#include "engine/platform/window.hpp"
#include "engine/vulkan/device.hpp"

#include <vulkan/vulkan.h>

namespace csc::vulkan {

inline constexpr u32 kMaxSwapchainImages   = 8;
inline constexpr u32 kMaxFramesInFlight    = 2;

/// Clear color: space gray (~0.1, 0.1, 0.1).
inline constexpr f32 kClearR = 0.1f;
inline constexpr f32 kClearG = 0.1f;
inline constexpr f32 kClearB = 0.1f;
inline constexpr f32 kClearA = 1.0f;

/// Swapchain + render pass + graphics pipeline + VB + sync — fixed capacity, init-time only.
struct RendererState {
    VkSwapchainKHR   swapchain       = VK_NULL_HANDLE;
    VkFormat         image_format    = VK_FORMAT_UNDEFINED;
    VkExtent2D       extent          = {0, 0};
    u32              image_count     = 0;

    VkImage          images[kMaxSwapchainImages]{};
    VkImageView      image_views[kMaxSwapchainImages]{};
    VkFramebuffer    framebuffers[kMaxSwapchainImages]{};
    VkCommandBuffer  command_buffers[kMaxSwapchainImages]{};

    VkRenderPass     render_pass     = VK_NULL_HANDLE;
    VkCommandPool    command_pool    = VK_NULL_HANDLE;

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline       pipeline        = VK_NULL_HANDLE;
    VkBuffer         vertex_buffer   = VK_NULL_HANDLE;
    VkDeviceMemory   vertex_memory   = VK_NULL_HANDLE;

    VkSemaphore      image_available[kMaxFramesInFlight]{};
    VkSemaphore      render_finished[kMaxFramesInFlight]{};
    VkFence          in_flight_fences[kMaxFramesInFlight]{};
    u32              current_frame   = 0;
};

[[nodiscard]] bool renderer_create(
    RendererState& state,
    const DeviceState& device,
    const platform::Window& window);

void renderer_destroy(RendererState& state, const DeviceState& device);

/// Acquire → clear → draw with per-frame view/projection push constants → submit → present.
/// Composes view_proj = vulkan_Y_flip(projection) * view and pushes 64 bytes (mat4) each frame.
/// No heap growth, no UBO remap, no pipeline recreate. Skip-frame (minimized) returns true.
[[nodiscard]] bool renderer_draw_frame(
    RendererState& state,
    const DeviceState& device,
    const glm::mat4& view,
    const glm::mat4& projection);

}  // namespace csc::vulkan
