#pragma once

#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"
#include "engine/platform/window.hpp"
#include "engine/render/grid.hpp"
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

    /// Depth attachment (P0-01/P0-02): one image shared across swapchain framebuffers.
    /// Created at init; destroyed/recreated with swapchain on resize (not in steady-state draw).
    VkImage          depth_image     = VK_NULL_HANDLE;
    VkDeviceMemory   depth_memory    = VK_NULL_HANDLE;
    VkImageView      depth_view      = VK_NULL_HANDLE;
    VkFormat         depth_format    = VK_FORMAT_UNDEFINED;

    VkRenderPass     render_pass     = VK_NULL_HANDLE;
    VkCommandPool    command_pool    = VK_NULL_HANDLE;

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline       pipeline        = VK_NULL_HANDLE;
    VkBuffer         vertex_buffer   = VK_NULL_HANDLE;
    VkDeviceMemory   vertex_memory   = VK_NULL_HANDLE;

    /// Ground grid: LINE_LIST pipeline + pre-baked VB (uploaded once at level load).
    VkPipeline       grid_pipeline      = VK_NULL_HANDLE;
    VkBuffer         grid_vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory   grid_vertex_memory = VK_NULL_HANDLE;
    u32              grid_vertex_count  = 0;
    bool             grid_visible       = false;

    VkSemaphore      image_available[kMaxFramesInFlight]{};
    VkSemaphore      render_finished[kMaxFramesInFlight]{};
    VkFence          in_flight_fences[kMaxFramesInFlight]{};
    u32              current_frame   = 0;
};

[[nodiscard]] bool renderer_create(
    RendererState& state,
    const DeviceState& device,
    const platform::Window& window,
    const render::GroundGridDesc& grid = {});

void renderer_destroy(RendererState& state, const DeviceState& device);

/// Reconstruct swapchain + depth + image views + framebuffers for the current window size.
/// Keeps render pass / pipelines / layout / VBs / sync when `image_format` is unchanged
/// (`choose_surface_format` prefers a stable B8G8R8A8_SRGB). If the format ever changes,
/// recreates render pass + pipelines as well (documented in STATUS.md).
/// Reallocates command buffers only when `image_count` changes. No-op skip when extent is 0x0.
[[nodiscard]] bool renderer_recreate_swapchain(
    RendererState& state,
    const DeviceState& device,
    const platform::Window& window);

/// Acquire → clear → draw with per-frame view/projection push constants → submit → present.
/// Recreates swapchain on OUT_OF_DATE / SUBOPTIMAL / window.framebuffer_resized.
/// Minimized (framebuffer 0x0): skip frame, return true. No heap growth in the steady path.
[[nodiscard]] bool renderer_draw_frame(
    RendererState& state,
    const DeviceState& device,
    platform::Window& window,
    const glm::mat4& view,
    const glm::mat4& projection);

}  // namespace csc::vulkan
