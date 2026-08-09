#pragma once

#include "engine/assets/mesh.hpp"
#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"
#include "engine/platform/window.hpp"
#include "engine/render/grid.hpp"
#include "engine/render/pipeline.hpp"
#include "engine/vulkan/device.hpp"

#include <vulkan/vulkan.h>

namespace csc::vulkan {

inline constexpr u32 kMaxSwapchainImages = 8;
inline constexpr u32 kMaxFramesInFlight  = 2;

/// Fixed instance capacity — one draw call, no heap growth in the frame loop.
inline constexpr u32 kMaxInstancesPerDrawCall = 1024u;

/// Clear color: space gray (~0.1, 0.1, 0.1).
inline constexpr f32 kClearR = 0.1f;
inline constexpr f32 kClearG = 0.1f;
inline constexpr f32 kClearB = 0.1f;
inline constexpr f32 kClearA = 1.0f;

/// Per-frame UBO (std140-compatible mat4).
struct FrameUBO {
    glm::mat4 view_proj;
};
static_assert(sizeof(FrameUBO) == 64u);

/// GPU mesh uploaded from CPU arena data (main thread only).
struct GpuMesh {
    VkBuffer       vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
    VkBuffer       index_buffer  = VK_NULL_HANDLE;
    VkDeviceMemory index_memory  = VK_NULL_HANDLE;
    u32            index_count   = 0;
    u32            vertex_count  = 0;
    bool           ready         = false;
};

/// Swapchain + pass + pipeline catalog + descriptors/UBO + instancing — fixed capacity.
struct RendererState {
    VkSwapchainKHR   swapchain       = VK_NULL_HANDLE;
    VkFormat         image_format    = VK_FORMAT_UNDEFINED;
    VkExtent2D       extent          = {0, 0};
    u32              image_count     = 0;

    VkImage          images[kMaxSwapchainImages]{};
    VkImageView      image_views[kMaxSwapchainImages]{};
    VkFramebuffer    framebuffers[kMaxSwapchainImages]{};
    VkCommandBuffer  command_buffers[kMaxSwapchainImages]{};

    VkImage          depth_image   = VK_NULL_HANDLE;
    VkDeviceMemory   depth_memory  = VK_NULL_HANDLE;
    VkImageView      depth_view    = VK_NULL_HANDLE;
    VkFormat         depth_format  = VK_FORMAT_UNDEFINED;

    VkRenderPass     render_pass  = VK_NULL_HANDLE;
    VkCommandPool    command_pool = VK_NULL_HANDLE;

    /// P0-10: bind by handle; raw VkPipeline lives in the catalog.
    render::PipelineCatalog pipelines{};
    render::MaterialCatalog materials{};
    render::PipelineHandle  triangle_pipeline{};
    render::PipelineHandle  grid_pipeline{};
    render::PipelineHandle  mesh_pipeline{};
    render::MaterialHandle  default_material{};

    /// P0-08: descriptor set layout (UBO + combined image sampler).
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool      descriptor_pool       = VK_NULL_HANDLE;
    VkBuffer              ubo_buffers[kMaxFramesInFlight]{};
    VkDeviceMemory        ubo_memories[kMaxFramesInFlight]{};
    void*                 ubo_mapped[kMaxFramesInFlight]{};
    VkDescriptorSet       descriptor_sets[kMaxFramesInFlight]{};

    VkImage        default_texture_image  = VK_NULL_HANDLE;
    VkDeviceMemory default_texture_memory = VK_NULL_HANDLE;
    VkImageView    default_texture_view   = VK_NULL_HANDLE;
    VkSampler      default_sampler        = VK_NULL_HANDLE;

    /// P0-09: host-visible instance buffer (filled each frame via one memcpy).
    VkBuffer       instance_buffer = VK_NULL_HANDLE;
    VkDeviceMemory instance_memory = VK_NULL_HANDLE;
    void*          instance_mapped = nullptr;
    u32            instance_count  = 0;

    GpuMesh demo_mesh{};

    /// Ground grid: pre-baked VB (LINE_LIST via grid_pipeline handle).
    VkBuffer       grid_vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory grid_vertex_memory = VK_NULL_HANDLE;
    u32            grid_vertex_count  = 0;
    bool           grid_visible       = false;

    /// Legacy single triangle (kept in catalog; not drawn once instancing is live).
    VkBuffer       vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory = VK_NULL_HANDLE;

    VkSemaphore image_available[kMaxFramesInFlight]{};
    VkSemaphore render_finished[kMaxFramesInFlight]{};
    VkFence     in_flight_fences[kMaxFramesInFlight]{};
    u32         current_frame = 0;
};

[[nodiscard]] bool renderer_create(
    RendererState& state,
    const DeviceState& device,
    const platform::Window& window,
    const render::GroundGridDesc& grid = {});

void renderer_destroy(RendererState& state, const DeviceState& device);

[[nodiscard]] bool renderer_recreate_swapchain(
    RendererState& state,
    const DeviceState& device,
    const platform::Window& window);

/// Copy `count` model matrices into fixed staging + GPU instance buffer (one memcpy).
/// Clamps to kMaxInstancesPerDrawCall. Safe to call each frame (no realloc).
void renderer_set_instances(RendererState& state, const glm::mat4* models, u32 count);

/// Main-thread GPU upload of a Ready CPU mesh (destroys previous demo_mesh if any).
[[nodiscard]] bool renderer_upload_mesh(
    RendererState& state,
    const DeviceState& device,
    const assets::MeshCpu& mesh);

/// Acquire → clear → grid + optional instanced mesh → submit → present.
[[nodiscard]] bool renderer_draw_frame(
    RendererState& state,
    const DeviceState& device,
    platform::Window& window,
    const glm::mat4& view,
    const glm::mat4& projection);

}  // namespace csc::vulkan
