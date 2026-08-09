#include "engine/vulkan/renderer.hpp"

#include "engine/log/log.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#ifndef CSC_SHADER_DIR
#define CSC_SHADER_DIR "shaders"
#endif

namespace csc::vulkan {
namespace {

struct TriangleVertex {
    f32 pos[3];
    f32 color[3];
};

struct ViewProjPushConstants {
    glm::mat4 view_proj;
};
static_assert(sizeof(ViewProjPushConstants) == 64u);

[[nodiscard]] glm::mat4 vulkan_clip_projection(const glm::mat4& projection)
{
    glm::mat4 proj = projection;
    proj[1][1] *= -1.0f;
    return proj;
}

VkSurfaceFormatKHR choose_surface_format(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB
            && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats[0];
}

VkPresentModeKHR choose_present_mode(const std::vector<VkPresentModeKHR>& modes)
{
    for (const VkPresentModeKHR mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR& caps, const platform::Window& window)
{
    if (caps.currentExtent.width != std::numeric_limits<u32>::max()) {
        return caps.currentExtent;
    }

    VkExtent2D extent{
        static_cast<u32>(window.width),
        static_cast<u32>(window.height),
    };
    extent.width  = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return extent;
}

[[nodiscard]] u32 find_memory_type(
    VkPhysicalDevice physical_device,
    u32 type_filter,
    VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

    for (u32 i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) != 0
            && (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

[[nodiscard]] bool has_stencil_component(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

[[nodiscard]] VkFormat find_depth_format(VkPhysicalDevice physical_device)
{
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };

    for (const VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(physical_device, format, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

void destroy_depth_resources(RendererState& state, VkDevice device)
{
    if (device == VK_NULL_HANDLE) {
        state.depth_view   = VK_NULL_HANDLE;
        state.depth_image  = VK_NULL_HANDLE;
        state.depth_memory = VK_NULL_HANDLE;
        state.depth_format = VK_FORMAT_UNDEFINED;
        return;
    }

    if (state.depth_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, state.depth_view, nullptr);
        state.depth_view = VK_NULL_HANDLE;
    }
    if (state.depth_image != VK_NULL_HANDLE) {
        vkDestroyImage(device, state.depth_image, nullptr);
        state.depth_image = VK_NULL_HANDLE;
    }
    if (state.depth_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, state.depth_memory, nullptr);
        state.depth_memory = VK_NULL_HANDLE;
    }
    state.depth_format = VK_FORMAT_UNDEFINED;
}

[[nodiscard]] bool create_depth_resources(RendererState& state, const DeviceState& device)
{
    state.depth_format = find_depth_format(device.physical_device);
    if (state.depth_format == VK_FORMAT_UNDEFINED) {
        std::fprintf(stderr, "[vulkan] No suitable depth format.\n");
        return false;
    }

    VkImageCreateInfo image_info{};
    image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.extent.width  = state.extent.width;
    image_info.extent.height = state.extent.height;
    image_info.extent.depth  = 1;
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.format        = state.depth_format;
    image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device.device, &image_info, nullptr, &state.depth_image) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements mem_reqs{};
    vkGetImageMemoryRequirements(device.device, state.depth_image, &mem_reqs);

    const u32 memory_type = find_memory_type(
        device.physical_device, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        destroy_depth_resources(state, device.device);
        return false;
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = mem_reqs.size;
    alloc_info.memoryTypeIndex = memory_type;

    if (vkAllocateMemory(device.device, &alloc_info, nullptr, &state.depth_memory) != VK_SUCCESS) {
        destroy_depth_resources(state, device.device);
        return false;
    }

    if (vkBindImageMemory(device.device, state.depth_image, state.depth_memory, 0) != VK_SUCCESS) {
        destroy_depth_resources(state, device.device);
        return false;
    }

    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (has_stencil_component(state.depth_format)) {
        aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image                           = state.depth_image;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format                          = state.depth_format;
    view_info.subresourceRange.aspectMask     = aspect;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device.device, &view_info, nullptr, &state.depth_view) != VK_SUCCESS) {
        destroy_depth_resources(state, device.device);
        return false;
    }

    return true;
}

bool create_render_pass(RendererState& state, VkDevice device)
{
    VkAttachmentDescription color_attachment{};
    color_attachment.format         = state.image_format;
    color_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth_attachment{};
    depth_attachment.format         = state.depth_format;
    depth_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    const VkAttachmentDescription attachments[] = {color_attachment, depth_attachment};

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_ref{};
    depth_ref.attachment = 1;
    depth_ref.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                             | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                             | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                             | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                             | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                             | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo pass_info{};
    pass_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass_info.attachmentCount = 2;
    pass_info.pAttachments    = attachments;
    pass_info.subpassCount    = 1;
    pass_info.pSubpasses      = &subpass;
    pass_info.dependencyCount = 1;
    pass_info.pDependencies   = &dependency;

    return vkCreateRenderPass(device, &pass_info, nullptr, &state.render_pass) == VK_SUCCESS;
}

bool create_image_views_and_framebuffers(RendererState& state, VkDevice device)
{
    for (u32 i = 0; i < state.image_count; ++i) {
        VkImageViewCreateInfo view_info{};
        view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image                           = state.images[i];
        view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format                          = state.image_format;
        view_info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel   = 0;
        view_info.subresourceRange.levelCount     = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device, &view_info, nullptr, &state.image_views[i]) != VK_SUCCESS) {
            return false;
        }

        const VkImageView attachments[] = {state.image_views[i], state.depth_view};

        VkFramebufferCreateInfo fb_info{};
        fb_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass      = state.render_pass;
        fb_info.attachmentCount = 2;
        fb_info.pAttachments    = attachments;
        fb_info.width           = state.extent.width;
        fb_info.height          = state.extent.height;
        fb_info.layers          = 1;

        if (vkCreateFramebuffer(device, &fb_info, nullptr, &state.framebuffers[i]) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool load_spirv_file(const char* path, std::vector<u32>& out_words)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::fprintf(stderr, "[vulkan] Failed to open SPIR-V: %s\n", path);
        return false;
    }

    const std::streamsize file_size = file.tellg();
    if (file_size <= 0 || (file_size % 4) != 0) {
        std::fprintf(stderr, "[vulkan] Invalid SPIR-V size for %s\n", path);
        return false;
    }

    out_words.resize(static_cast<std::size_t>(file_size) / sizeof(u32));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(out_words.data()), file_size);
    if (!file) {
        out_words.clear();
        return false;
    }
    return true;
}

[[nodiscard]] bool create_shader_module(
    VkDevice device, const std::vector<u32>& words, VkShaderModule* out_module)
{
    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = words.size() * sizeof(u32);
    info.pCode    = words.data();
    return vkCreateShaderModule(device, &info, nullptr, out_module) == VK_SUCCESS;
}

[[nodiscard]] bool create_host_buffer(
    const DeviceState& device,
    const void* data,
    VkDeviceSize buffer_size,
    VkBufferUsageFlags usage,
    VkBuffer* out_buffer,
    VkDeviceMemory* out_memory,
    void** out_persistent_map = nullptr)
{
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size        = buffer_size;
    buffer_info.usage       = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device.device, &buffer_info, nullptr, out_buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements mem_reqs{};
    vkGetBufferMemoryRequirements(device.device, *out_buffer, &mem_reqs);

    const u32 memory_type = find_memory_type(
        device.physical_device,
        mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_type == UINT32_MAX) {
        return false;
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = mem_reqs.size;
    alloc_info.memoryTypeIndex = memory_type;

    if (vkAllocateMemory(device.device, &alloc_info, nullptr, out_memory) != VK_SUCCESS) {
        return false;
    }

    if (vkBindBufferMemory(device.device, *out_buffer, *out_memory, 0) != VK_SUCCESS) {
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(device.device, *out_memory, 0, buffer_size, 0, &mapped) != VK_SUCCESS) {
        return false;
    }
    if (data != nullptr) {
        std::memcpy(mapped, data, static_cast<std::size_t>(buffer_size));
    } else {
        std::memset(mapped, 0, static_cast<std::size_t>(buffer_size));
    }

    if (out_persistent_map != nullptr) {
        *out_persistent_map = mapped;
    } else {
        vkUnmapMemory(device.device, *out_memory);
    }
    return true;
}

[[nodiscard]] bool create_vertex_buffer(RendererState& state, const DeviceState& device)
{
    const TriangleVertex vertices[3] = {
        {{ 0.0f, -0.5f, 0.0f}, {1.0f, 0.2f, 0.2f}},
        {{-0.5f,  0.5f, 0.0f}, {0.2f, 0.2f, 1.0f}},
        {{ 0.5f,  0.5f, 0.0f}, {0.2f, 1.0f, 0.2f}},
    };
    return create_host_buffer(
        device,
        vertices,
        sizeof(vertices),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        &state.vertex_buffer,
        &state.vertex_memory);
}

[[nodiscard]] bool create_grid_vertex_buffer(
    RendererState& state, const DeviceState& device, const render::GroundGridDesc& grid)
{
    state.grid_vertex_count = 0;
    state.grid_visible = grid.visible;

    if (!grid.visible) {
        return true;
    }

    std::array<render::GridVertex, render::kMaxGridVertices> verts{};
    u32 count = 0;
    if (!render::grid_bake_vertices(grid, verts.data(), render::kMaxGridVertices, count)) {
        std::fprintf(stderr, "[vulkan] Grid bake exceeded kMaxGridVertices.\n");
        return false;
    }
    if (count == 0) {
        state.grid_visible = false;
        return true;
    }

    const VkDeviceSize buffer_size = sizeof(render::GridVertex) * count;
    if (!create_host_buffer(
            device,
            verts.data(),
            buffer_size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            &state.grid_vertex_buffer,
            &state.grid_vertex_memory)) {
        return false;
    }
    state.grid_vertex_count = count;
    return true;
}

[[nodiscard]] bool create_descriptor_set_layout(RendererState& state, VkDevice device)
{
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 2;
    info.pBindings    = bindings;

    return vkCreateDescriptorSetLayout(device, &info, nullptr, &state.descriptor_set_layout)
        == VK_SUCCESS;
}

[[nodiscard]] bool create_default_white_texture(RendererState& state, const DeviceState& device)
{
    const u8 white_pixel[4] = {255, 255, 255, 255};

    // Staging buffer (host-visible) → OPTIMAL sampled image (portable on lavapipe/GPU).
    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    if (!create_host_buffer(
            device,
            white_pixel,
            sizeof(white_pixel),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            &staging_buffer,
            &staging_memory)) {
        return false;
    }

    VkImageCreateInfo image_info{};
    image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.extent        = {1, 1, 1};
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.format        = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device.device, &image_info, nullptr, &state.default_texture_image) != VK_SUCCESS) {
        vkDestroyBuffer(device.device, staging_buffer, nullptr);
        vkFreeMemory(device.device, staging_memory, nullptr);
        return false;
    }

    VkMemoryRequirements mem_reqs{};
    vkGetImageMemoryRequirements(device.device, state.default_texture_image, &mem_reqs);

    const u32 memory_type = find_memory_type(
        device.physical_device, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == UINT32_MAX) {
        vkDestroyImage(device.device, state.default_texture_image, nullptr);
        state.default_texture_image = VK_NULL_HANDLE;
        vkDestroyBuffer(device.device, staging_buffer, nullptr);
        vkFreeMemory(device.device, staging_memory, nullptr);
        return false;
    }

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = mem_reqs.size;
    alloc_info.memoryTypeIndex = memory_type;

    if (vkAllocateMemory(device.device, &alloc_info, nullptr, &state.default_texture_memory)
        != VK_SUCCESS) {
        vkDestroyImage(device.device, state.default_texture_image, nullptr);
        state.default_texture_image = VK_NULL_HANDLE;
        vkDestroyBuffer(device.device, staging_buffer, nullptr);
        vkFreeMemory(device.device, staging_memory, nullptr);
        return false;
    }
    if (vkBindImageMemory(device.device, state.default_texture_image, state.default_texture_memory, 0)
        != VK_SUCCESS) {
        vkDestroyImage(device.device, state.default_texture_image, nullptr);
        vkFreeMemory(device.device, state.default_texture_memory, nullptr);
        state.default_texture_image  = VK_NULL_HANDLE;
        state.default_texture_memory = VK_NULL_HANDLE;
        vkDestroyBuffer(device.device, staging_buffer, nullptr);
        vkFreeMemory(device.device, staging_memory, nullptr);
        return false;
    }

    VkCommandBufferAllocateInfo cmd_alloc{};
    cmd_alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_alloc.commandPool        = state.command_pool;
    cmd_alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device.device, &cmd_alloc, &cmd) != VK_SUCCESS) {
        vkDestroyBuffer(device.device, staging_buffer, nullptr);
        vkFreeMemory(device.device, staging_memory, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    VkImageMemoryBarrier to_dst{};
    to_dst.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_dst.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
    to_dst.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_dst.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    to_dst.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    to_dst.image                           = state.default_texture_image;
    to_dst.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    to_dst.subresourceRange.baseMipLevel   = 0;
    to_dst.subresourceRange.levelCount     = 1;
    to_dst.subresourceRange.baseArrayLayer = 0;
    to_dst.subresourceRange.layerCount     = 1;
    to_dst.srcAccessMask                   = 0;
    to_dst.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &to_dst);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent                 = {1, 1, 1};

    vkCmdCopyBufferToImage(
        cmd,
        staging_buffer,
        state.default_texture_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);

    VkImageMemoryBarrier to_shader = to_dst;
    to_shader.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_shader.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &to_shader);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(device.graphics_queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(device.graphics_queue);
    vkFreeCommandBuffers(device.device, state.command_pool, 1, &cmd);

    vkDestroyBuffer(device.device, staging_buffer, nullptr);
    vkFreeMemory(device.device, staging_memory, nullptr);

    VkImageViewCreateInfo view_info{};
    view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image                           = state.default_texture_image;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format                          = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device.device, &view_info, nullptr, &state.default_texture_view)
        != VK_SUCCESS) {
        return false;
    }

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter    = VK_FILTER_NEAREST;
    sampler_info.minFilter    = VK_FILTER_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.maxLod       = 0.0f;

    return vkCreateSampler(device.device, &sampler_info, nullptr, &state.default_sampler)
        == VK_SUCCESS;
}

[[nodiscard]] bool create_frame_ubo_and_descriptors(RendererState& state, const DeviceState& device)
{
    for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
        if (!create_host_buffer(
                device,
                nullptr,
                sizeof(FrameUBO),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                &state.ubo_buffers[i],
                &state.ubo_memories[i],
                &state.ubo_mapped[i])) {
            return false;
        }
    }

    // Pool sized for frames + a few materials (fixed).
    VkDescriptorPoolSize pool_sizes[2]{};
    pool_sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = kMaxFramesInFlight + 8;
    pool_sizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount = kMaxFramesInFlight + 8;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes    = pool_sizes;
    pool_info.maxSets       = kMaxFramesInFlight + 8;

    if (vkCreateDescriptorPool(device.device, &pool_info, nullptr, &state.descriptor_pool)
        != VK_SUCCESS) {
        return false;
    }

    VkDescriptorSetLayout layouts[kMaxFramesInFlight]{};
    for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
        layouts[i] = state.descriptor_set_layout;
    }

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool     = state.descriptor_pool;
    alloc_info.descriptorSetCount = kMaxFramesInFlight;
    alloc_info.pSetLayouts        = layouts;

    if (vkAllocateDescriptorSets(device.device, &alloc_info, state.descriptor_sets) != VK_SUCCESS) {
        return false;
    }

    for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = state.ubo_buffers[i];
        buffer_info.offset = 0;
        buffer_info.range  = sizeof(FrameUBO);

        VkDescriptorImageInfo image_info{};
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_info.imageView   = state.default_texture_view;
        image_info.sampler     = state.default_sampler;

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = state.descriptor_sets[i];
        writes[0].dstBinding      = 0;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo     = &buffer_info;

        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = state.descriptor_sets[i];
        writes[1].dstBinding      = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo      = &image_info;

        vkUpdateDescriptorSets(device.device, 2, writes, 0, nullptr);
    }

    return true;
}

[[nodiscard]] bool create_instance_buffer(RendererState& state, const DeviceState& device)
{
    const VkDeviceSize size = sizeof(glm::mat4) * kMaxInstancesPerDrawCall;
    return create_host_buffer(
        device,
        nullptr,
        size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        &state.instance_buffer,
        &state.instance_memory,
        &state.instance_mapped);
}

[[nodiscard]] bool shader_paths_for(
    render::BuiltinShaderId id, std::string& vert_path, std::string& frag_path)
{
    switch (id) {
    case render::BuiltinShaderId::TriangleColored:
        vert_path = std::string(CSC_SHADER_DIR) + "/triangle.vert.spv";
        frag_path = std::string(CSC_SHADER_DIR) + "/triangle.frag.spv";
        return true;
    case render::BuiltinShaderId::MeshInstanced:
        vert_path = std::string(CSC_SHADER_DIR) + "/mesh.vert.spv";
        frag_path = std::string(CSC_SHADER_DIR) + "/mesh.frag.spv";
        return true;
    default:
        return false;
    }
}

void fill_vertex_input_pos_color(
    VkVertexInputBindingDescription& binding,
    VkVertexInputAttributeDescription attrs[2],
    VkPipelineVertexInputStateCreateInfo& vertex_input)
{
    binding.binding   = 0;
    binding.stride    = sizeof(TriangleVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    attrs[0].binding  = 0;
    attrs[0].location = 0;
    attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset   = offsetof(TriangleVertex, pos);
    attrs[1].binding  = 0;
    attrs[1].location = 1;
    attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset   = offsetof(TriangleVertex, color);

    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount   = 1;
    vertex_input.pVertexBindingDescriptions      = &binding;
    vertex_input.vertexAttributeDescriptionCount = 2;
    vertex_input.pVertexAttributeDescriptions    = attrs;
}

void fill_vertex_input_mesh_instanced(
    VkVertexInputBindingDescription bindings[2],
    VkVertexInputAttributeDescription attrs[8],
    VkPipelineVertexInputStateCreateInfo& vertex_input)
{
    bindings[0].binding   = 0;
    bindings[0].stride    = sizeof(assets::MeshVertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    bindings[1].binding   = 1;
    bindings[1].stride    = sizeof(glm::mat4);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(assets::MeshVertex, pos)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(assets::MeshVertex, normal)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(assets::MeshVertex, color)};
    attrs[3] = {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(assets::MeshVertex, uv)};
    attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0};
    attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16};
    attrs[6] = {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32};
    attrs[7] = {7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48};

    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount   = 2;
    vertex_input.pVertexBindingDescriptions      = bindings;
    vertex_input.vertexAttributeDescriptionCount = 8;
    vertex_input.pVertexAttributeDescriptions    = attrs;
}

[[nodiscard]] bool create_one_pipeline(
    RendererState& state,
    VkDevice device,
    render::PipelineEntry& entry)
{
    std::string vert_path;
    std::string frag_path;
    if (!shader_paths_for(entry.desc.shader_id, vert_path, frag_path)) {
        return false;
    }

    std::vector<u32> vert_words;
    std::vector<u32> frag_words;
    if (!load_spirv_file(vert_path.c_str(), vert_words)
        || !load_spirv_file(frag_path.c_str(), frag_words)) {
        return false;
    }

    VkShaderModule vert_module = VK_NULL_HANDLE;
    VkShaderModule frag_module = VK_NULL_HANDLE;
    if (!create_shader_module(device, vert_words, &vert_module)
        || !create_shader_module(device, frag_words, &frag_module)) {
        if (vert_module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, vert_module, nullptr);
        }
        if (frag_module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, frag_module, nullptr);
        }
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_module;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_module;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription bindings[2]{};
    VkVertexInputAttributeDescription attrs[8]{};
    VkPipelineVertexInputStateCreateInfo vertex_input{};

    if (entry.desc.vertex_layout == render::VertexLayoutId::MeshPosNormColorUv) {
        fill_vertex_input_mesh_instanced(bindings, attrs, vertex_input);
    } else {
        fill_vertex_input_pos_color(bindings[0], attrs, vertex_input);
    }

    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = entry.desc.topology;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable  = entry.desc.depth_test ? VK_TRUE : VK_FALSE;
    depth_stencil.depthWriteEnable = entry.desc.depth_write ? VK_TRUE : VK_FALSE;
    depth_stencil.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blend_attachment{};
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                    | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments    = &blend_attachment;

    const VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dynamic_states;

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset     = 0;
    push_range.size       = sizeof(ViewProjPushConstants);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (entry.desc.use_descriptors) {
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts    = &state.descriptor_set_layout;
    } else {
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges    = &push_range;
    }

    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &entry.layout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vert_module, nullptr);
        vkDestroyShaderModule(device, frag_module, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount          = 2;
    pipeline_info.pStages             = stages;
    pipeline_info.pVertexInputState   = &vertex_input;
    pipeline_info.pInputAssemblyState = &assembly;
    pipeline_info.pViewportState      = &viewport_state;
    pipeline_info.pRasterizationState = &raster;
    pipeline_info.pMultisampleState   = &multisample;
    pipeline_info.pDepthStencilState  = &depth_stencil;
    pipeline_info.pColorBlendState    = &color_blend;
    pipeline_info.pDynamicState       = &dynamic;
    pipeline_info.layout              = entry.layout;
    pipeline_info.renderPass          = state.render_pass;
    pipeline_info.subpass             = 0;

    const VkResult result = vkCreateGraphicsPipelines(
        device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &entry.pipeline);

    vkDestroyShaderModule(device, vert_module, nullptr);
    vkDestroyShaderModule(device, frag_module, nullptr);

    return result == VK_SUCCESS;
}

[[nodiscard]] bool create_graphics_pipelines(RendererState& state, VkDevice device)
{
    // Register catalog entries once; on format-change recreate only GPU objects.
    if (state.pipelines.count == 0) {
        render::PipelineDesc tri{};
        tri.topology        = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        tri.shader_id       = render::BuiltinShaderId::TriangleColored;
        tri.vertex_layout   = render::VertexLayoutId::PosColor;
        tri.depth_test      = true;
        tri.depth_write     = true;
        tri.use_descriptors = false;
        state.triangle_pipeline = render::pipeline_catalog_register(state.pipelines, tri);

        render::PipelineDesc grid{};
        grid.topology        = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        grid.shader_id       = render::BuiltinShaderId::TriangleColored;
        grid.vertex_layout   = render::VertexLayoutId::PosColor;
        grid.depth_test      = true;
        grid.depth_write     = true;
        grid.use_descriptors = false;
        state.grid_pipeline = render::pipeline_catalog_register(state.pipelines, grid);

        render::PipelineDesc mesh{};
        mesh.topology        = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        mesh.shader_id       = render::BuiltinShaderId::MeshInstanced;
        mesh.vertex_layout   = render::VertexLayoutId::MeshPosNormColorUv;
        mesh.depth_test      = true;
        mesh.depth_write     = true;
        mesh.use_descriptors = true;
        state.mesh_pipeline = render::pipeline_catalog_register(state.pipelines, mesh);

        render::MaterialDesc mat{};
        mat.pipeline = state.mesh_pipeline;
        state.default_material = render::material_catalog_register(state.materials, mat);
    }

    for (u32 i = 0; i < state.pipelines.count; ++i) {
        render::PipelineEntry& entry = state.pipelines.entries[i];
        if (!entry.alive) {
            continue;
        }
        if (entry.pipeline != VK_NULL_HANDLE || entry.layout != VK_NULL_HANDLE) {
            continue;
        }
        if (!create_one_pipeline(state, device, entry)) {
            log::log_error(
                log::LogCategory::Vulkan,
                "Failed to create catalog pipeline index %u (shader=%u).",
                i,
                static_cast<u32>(entry.desc.shader_id));
            return false;
        }
    }
    return true;
}

void destroy_gpu_mesh(GpuMesh& mesh, VkDevice device)
{
    if (device == VK_NULL_HANDLE) {
        mesh = {};
        return;
    }
    if (mesh.vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, mesh.vertex_buffer, nullptr);
    }
    if (mesh.vertex_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, mesh.vertex_memory, nullptr);
    }
    if (mesh.index_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, mesh.index_buffer, nullptr);
    }
    if (mesh.index_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, mesh.index_memory, nullptr);
    }
    mesh = {};
}

[[nodiscard]] bool record_draw_commands(
    RendererState& state,
    u32 image_index,
    u32 frame_index,
    const ViewProjPushConstants& push)
{
    VkCommandBuffer cmd = state.command_buffers[image_index];

    if (vkResetCommandBuffer(cmd, 0) != VK_SUCCESS) {
        return false;
    }

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
        return false;
    }

    VkClearValue clear_values[2]{};
    clear_values[0].color.float32[0] = kClearR;
    clear_values[0].color.float32[1] = kClearG;
    clear_values[0].color.float32[2] = kClearB;
    clear_values[0].color.float32[3] = kClearA;
    clear_values[1].depthStencil.depth   = 1.0f;
    clear_values[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo rp_begin{};
    rp_begin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass        = state.render_pass;
    rp_begin.framebuffer       = state.framebuffers[image_index];
    rp_begin.renderArea.offset = {0, 0};
    rp_begin.renderArea.extent = state.extent;
    rp_begin.clearValueCount   = 2;
    rp_begin.pClearValues      = clear_values;

    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<f32>(state.extent.width);
    viewport.height   = static_cast<f32>(state.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = state.extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    const VkDeviceSize offset0 = 0;

    // Ground grid via catalog handle (push-constant VP).
    if (state.grid_visible && state.grid_vertex_buffer != VK_NULL_HANDLE
        && state.grid_vertex_count > 0) {
        if (const render::PipelineEntry* grid_pipe =
                render::pipeline_catalog_get(state.pipelines, state.grid_pipeline)) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, grid_pipe->pipeline);
            vkCmdPushConstants(
                cmd,
                grid_pipe->layout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(ViewProjPushConstants),
                &push);
            vkCmdBindVertexBuffers(cmd, 0, 1, &state.grid_vertex_buffer, &offset0);
            vkCmdDraw(cmd, state.grid_vertex_count, 1, 0, 0);
        }
    }

    // Instanced mesh: one drawIndexed with instanceCount (P0-09).
    if (state.demo_mesh.ready && state.instance_count > 0
        && state.demo_mesh.vertex_buffer != VK_NULL_HANDLE
        && state.demo_mesh.index_buffer != VK_NULL_HANDLE) {
        if (const render::PipelineEntry* mesh_pipe =
                render::pipeline_catalog_get(state.pipelines, state.mesh_pipeline)) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh_pipe->pipeline);
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                mesh_pipe->layout,
                0,
                1,
                &state.descriptor_sets[frame_index],
                0,
                nullptr);

            const VkBuffer vertex_buffers[] = {
                state.demo_mesh.vertex_buffer,
                state.instance_buffer,
            };
            const VkDeviceSize offsets[] = {0, 0};
            vkCmdBindVertexBuffers(cmd, 0, 2, vertex_buffers, offsets);
            vkCmdBindIndexBuffer(cmd, state.demo_mesh.index_buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(
                cmd, state.demo_mesh.index_count, state.instance_count, 0, 0, 0);
        }
    }

    vkCmdEndRenderPass(cmd);
    return vkEndCommandBuffer(cmd) == VK_SUCCESS;
}

void destroy_swapchain_views_and_framebuffers(RendererState& state, VkDevice device)
{
    for (u32 i = 0; i < state.image_count; ++i) {
        if (state.framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, state.framebuffers[i], nullptr);
            state.framebuffers[i] = VK_NULL_HANDLE;
        }
        if (state.image_views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, state.image_views[i], nullptr);
            state.image_views[i] = VK_NULL_HANDLE;
        }
        state.images[i] = VK_NULL_HANDLE;
    }
}

void free_command_buffers(RendererState& state, VkDevice device, u32 count)
{
    if (state.command_pool == VK_NULL_HANDLE || count == 0) {
        return;
    }
    vkFreeCommandBuffers(device, state.command_pool, count, state.command_buffers);
    for (u32 i = 0; i < kMaxSwapchainImages; ++i) {
        state.command_buffers[i] = VK_NULL_HANDLE;
    }
}

[[nodiscard]] bool allocate_command_buffers(RendererState& state, VkDevice device)
{
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool        = state.command_pool;
    alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = state.image_count;

    return vkAllocateCommandBuffers(device, &alloc_info, state.command_buffers) == VK_SUCCESS;
}

[[nodiscard]] bool create_swapchain(
    RendererState& state,
    const DeviceState& device,
    const platform::Window& window,
    VkSwapchainKHR old_swapchain)
{
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.physical_device, device.surface, &caps);

    u32 format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device.physical_device, device.surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        device.physical_device, device.surface, &format_count, formats.data());

    u32 present_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device.physical_device, device.surface, &present_count, nullptr);
    std::vector<VkPresentModeKHR> present_modes(present_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device.physical_device, device.surface, &present_count, present_modes.data());

    if (formats.empty() || present_modes.empty()) {
        return false;
    }

    const VkSurfaceFormatKHR surface_format = choose_surface_format(formats);
    const VkPresentModeKHR present_mode     = choose_present_mode(present_modes);
    const VkExtent2D extent                 = choose_extent(caps, window);

    if (extent.width == 0 || extent.height == 0) {
        return false;
    }

    u32 image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }
    if (image_count > kMaxSwapchainImages) {
        image_count = kMaxSwapchainImages;
    }

    VkSwapchainCreateInfoKHR swap_info{};
    swap_info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swap_info.surface          = device.surface;
    swap_info.minImageCount    = image_count;
    swap_info.imageFormat      = surface_format.format;
    swap_info.imageColorSpace  = surface_format.colorSpace;
    swap_info.imageExtent      = extent;
    swap_info.imageArrayLayers = 1;
    swap_info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const u32 queue_families[] = {
        device.graphics_queue_family,
        device.present_queue_family,
    };

    if (device.has_distinct_present_q) {
        swap_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        swap_info.queueFamilyIndexCount = 2;
        swap_info.pQueueFamilyIndices   = queue_families;
    } else {
        swap_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    swap_info.preTransform   = caps.currentTransform;
    swap_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swap_info.presentMode    = present_mode;
    swap_info.clipped        = VK_TRUE;
    swap_info.oldSwapchain   = old_swapchain;

    VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device.device, &swap_info, nullptr, &new_swapchain) != VK_SUCCESS) {
        return false;
    }

    state.swapchain    = new_swapchain;
    state.image_format = surface_format.format;
    state.extent       = extent;

    u32 actual_count = 0;
    vkGetSwapchainImagesKHR(device.device, state.swapchain, &actual_count, nullptr);
    if (actual_count == 0 || actual_count > kMaxSwapchainImages) {
        return false;
    }
    state.image_count = actual_count;
    vkGetSwapchainImagesKHR(device.device, state.swapchain, &actual_count, state.images);
    return true;
}

}  // namespace

bool renderer_create(
    RendererState& state,
    const DeviceState& device,
    const platform::Window& window,
    const render::GroundGridDesc& grid)
{
    state = {};

    if (!create_swapchain(state, device, window, VK_NULL_HANDLE)) {
        log::log_error(log::LogCategory::Vulkan, "Swapchain create failed.");
        return false;
    }

    if (!create_depth_resources(state, device)) {
        renderer_destroy(state, device);
        return false;
    }

    if (!create_render_pass(state, device.device)) {
        renderer_destroy(state, device);
        return false;
    }

    if (!create_image_views_and_framebuffers(state, device.device)) {
        renderer_destroy(state, device);
        return false;
    }

    if (!create_vertex_buffer(state, device)) {
        renderer_destroy(state, device);
        return false;
    }

    if (!create_grid_vertex_buffer(state, device, grid)) {
        renderer_destroy(state, device);
        return false;
    }

    if (!create_descriptor_set_layout(state, device.device)) {
        log::log_error(log::LogCategory::Vulkan, "Failed to create descriptor set layout.");
        renderer_destroy(state, device);
        return false;
    }

    // Command pool needed before texture layout transition.
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = device.graphics_queue_family;

    if (vkCreateCommandPool(device.device, &pool_info, nullptr, &state.command_pool) != VK_SUCCESS) {
        renderer_destroy(state, device);
        return false;
    }

    if (!create_default_white_texture(state, device)) {
        log::log_error(log::LogCategory::Vulkan, "Failed to create default 1x1 white texture.");
        renderer_destroy(state, device);
        return false;
    }

    if (!create_frame_ubo_and_descriptors(state, device)) {
        log::log_error(log::LogCategory::Vulkan, "Failed to create frame UBOs / descriptors.");
        renderer_destroy(state, device);
        return false;
    }

    if (!create_instance_buffer(state, device)) {
        log::log_error(log::LogCategory::Vulkan, "Failed to create instance buffer.");
        renderer_destroy(state, device);
        return false;
    }

    if (!create_graphics_pipelines(state, device.device)) {
        log::log_error(log::LogCategory::Vulkan, "Failed to create pipeline catalog.");
        renderer_destroy(state, device);
        return false;
    }

    if (!allocate_command_buffers(state, device.device)) {
        renderer_destroy(state, device);
        return false;
    }

    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
        if (vkCreateSemaphore(device.device, &sem_info, nullptr, &state.image_available[i]) != VK_SUCCESS
            || vkCreateSemaphore(device.device, &sem_info, nullptr, &state.render_finished[i])
                != VK_SUCCESS
            || vkCreateFence(device.device, &fence_info, nullptr, &state.in_flight_fences[i])
                != VK_SUCCESS) {
            renderer_destroy(state, device);
            return false;
        }
    }

    state.current_frame = 0;
    log::log_info(
        log::LogCategory::Vulkan,
        "Renderer ready: pipelines=%u materials=%u max_instances=%u",
        state.pipelines.count,
        state.materials.count,
        kMaxInstancesPerDrawCall);
    return true;
}

bool renderer_recreate_swapchain(
    RendererState& state,
    const DeviceState& device,
    const platform::Window& window)
{
    if (window.width <= 0 || window.height <= 0) {
        return true;
    }

    vkDeviceWaitIdle(device.device);

    const u32 old_image_count    = state.image_count;
    const VkFormat old_format    = state.image_format;
    const VkFormat old_depth_fmt = state.depth_format;
    const VkSwapchainKHR old_swap = state.swapchain;

    destroy_swapchain_views_and_framebuffers(state, device.device);
    destroy_depth_resources(state, device.device);

    state.swapchain = VK_NULL_HANDLE;

    if (!create_swapchain(state, device, window, old_swap)) {
        if (old_swap != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device.device, old_swap, nullptr);
        }
        state.swapchain   = VK_NULL_HANDLE;
        state.image_count = 0;
        state.extent      = {0, 0};
        return false;
    }

    if (old_swap != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device.device, old_swap, nullptr);
    }

    if (!create_depth_resources(state, device)) {
        return false;
    }

    const bool format_changed =
        state.image_format != old_format || state.depth_format != old_depth_fmt;
    if (format_changed) {
        log::log_warn(
            log::LogCategory::Vulkan,
            "Swapchain/depth format changed; recreating render pass + pipelines.");

        render::pipeline_catalog_destroy_gpu(state.pipelines, device.device);
        if (state.render_pass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device.device, state.render_pass, nullptr);
            state.render_pass = VK_NULL_HANDLE;
        }
        if (!create_render_pass(state, device.device)
            || !create_graphics_pipelines(state, device.device)) {
            return false;
        }
    }

    if (!create_image_views_and_framebuffers(state, device.device)) {
        return false;
    }

    if (state.image_count != old_image_count) {
        free_command_buffers(state, device.device, old_image_count);
        if (!allocate_command_buffers(state, device.device)) {
            return false;
        }
    }

    return true;
}

void renderer_set_instances(RendererState& state, const glm::mat4* models, u32 count)
{
    u32 n = count;
    if (n > kMaxInstancesPerDrawCall) {
        n = kMaxInstancesPerDrawCall;
    }
    state.instance_count = n;
    if (n == 0 || models == nullptr || state.instance_mapped == nullptr) {
        return;
    }
    // One memcpy into fixed GPU-mapped staging — no realloc / no heap.
    std::memcpy(state.instance_mapped, models, sizeof(glm::mat4) * n);
}

bool renderer_upload_mesh(
    RendererState& state, const DeviceState& device, const assets::MeshCpu& mesh)
{
    if (mesh.vertices == nullptr || mesh.indices == nullptr || mesh.vertex_count == 0
        || mesh.index_count == 0) {
        return false;
    }

    vkDeviceWaitIdle(device.device);
    destroy_gpu_mesh(state.demo_mesh, device.device);

    const VkDeviceSize vb_size = sizeof(assets::MeshVertex) * mesh.vertex_count;
    const VkDeviceSize ib_size = sizeof(u32) * mesh.index_count;

    if (!create_host_buffer(
            device,
            mesh.vertices,
            vb_size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            &state.demo_mesh.vertex_buffer,
            &state.demo_mesh.vertex_memory)) {
        destroy_gpu_mesh(state.demo_mesh, device.device);
        return false;
    }

    if (!create_host_buffer(
            device,
            mesh.indices,
            ib_size,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            &state.demo_mesh.index_buffer,
            &state.demo_mesh.index_memory)) {
        destroy_gpu_mesh(state.demo_mesh, device.device);
        return false;
    }

    state.demo_mesh.vertex_count = mesh.vertex_count;
    state.demo_mesh.index_count  = mesh.index_count;
    state.demo_mesh.ready        = true;

    log::log_info(
        log::LogCategory::Vulkan,
        "Uploaded GPU mesh: verts=%u indices=%u",
        mesh.vertex_count,
        mesh.index_count);
    return true;
}

void renderer_destroy(RendererState& state, const DeviceState& device)
{
    if (device.device == VK_NULL_HANDLE) {
        state = {};
        return;
    }

    vkDeviceWaitIdle(device.device);

    for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
        if (state.render_finished[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device.device, state.render_finished[i], nullptr);
        }
        if (state.image_available[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device.device, state.image_available[i], nullptr);
        }
        if (state.in_flight_fences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device.device, state.in_flight_fences[i], nullptr);
        }
    }

    destroy_gpu_mesh(state.demo_mesh, device.device);

    if (state.instance_mapped != nullptr && state.instance_memory != VK_NULL_HANDLE) {
        vkUnmapMemory(device.device, state.instance_memory);
        state.instance_mapped = nullptr;
    }
    if (state.instance_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device.device, state.instance_buffer, nullptr);
    }
    if (state.instance_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device.device, state.instance_memory, nullptr);
    }

    for (u32 i = 0; i < kMaxFramesInFlight; ++i) {
        if (state.ubo_mapped[i] != nullptr && state.ubo_memories[i] != VK_NULL_HANDLE) {
            vkUnmapMemory(device.device, state.ubo_memories[i]);
            state.ubo_mapped[i] = nullptr;
        }
        if (state.ubo_buffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device.device, state.ubo_buffers[i], nullptr);
        }
        if (state.ubo_memories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device.device, state.ubo_memories[i], nullptr);
        }
    }

    if (state.descriptor_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device.device, state.descriptor_pool, nullptr);
    }
    if (state.descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device.device, state.descriptor_set_layout, nullptr);
    }

    if (state.default_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device.device, state.default_sampler, nullptr);
    }
    if (state.default_texture_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device.device, state.default_texture_view, nullptr);
    }
    if (state.default_texture_image != VK_NULL_HANDLE) {
        vkDestroyImage(device.device, state.default_texture_image, nullptr);
    }
    if (state.default_texture_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device.device, state.default_texture_memory, nullptr);
    }

    if (state.command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device.device, state.command_pool, nullptr);
    }

    render::pipeline_catalog_destroy_gpu(state.pipelines, device.device);

    if (state.grid_vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device.device, state.grid_vertex_buffer, nullptr);
    }
    if (state.grid_vertex_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device.device, state.grid_vertex_memory, nullptr);
    }

    if (state.vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device.device, state.vertex_buffer, nullptr);
    }
    if (state.vertex_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device.device, state.vertex_memory, nullptr);
    }

    for (u32 i = 0; i < state.image_count; ++i) {
        if (state.framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device.device, state.framebuffers[i], nullptr);
        }
        if (state.image_views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device.device, state.image_views[i], nullptr);
        }
    }

    destroy_depth_resources(state, device.device);

    if (state.render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device.device, state.render_pass, nullptr);
    }

    if (state.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device.device, state.swapchain, nullptr);
    }

    state = {};
}

bool renderer_draw_frame(
    RendererState& state,
    const DeviceState& device,
    platform::Window& window,
    const glm::mat4& view,
    const glm::mat4& projection)
{
    platform::window_query_framebuffer_size(window);

    if (window.width <= 0 || window.height <= 0) {
        return true;
    }

    if (window.framebuffer_resized) {
        if (!renderer_recreate_swapchain(state, device, window)) {
            return false;
        }
        window.framebuffer_resized = false;
        if (state.extent.width == 0 || state.extent.height == 0) {
            return true;
        }
    }

    const u32 frame = state.current_frame;

    vkWaitForFences(device.device, 1, &state.in_flight_fences[frame], VK_TRUE, UINT64_MAX);

    u32 image_index = 0;
    const VkResult acquire = vkAcquireNextImageKHR(
        device.device,
        state.swapchain,
        UINT64_MAX,
        state.image_available[frame],
        VK_NULL_HANDLE,
        &image_index);

    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        window.framebuffer_resized = false;
        return renderer_recreate_swapchain(state, device, window);
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        log::log_error(
            log::LogCategory::Vulkan,
            "vkAcquireNextImageKHR failed (%d).",
            static_cast<int>(acquire));
        return false;
    }

    vkResetFences(device.device, 1, &state.in_flight_fences[frame]);

    const glm::mat4 view_proj = vulkan_clip_projection(projection) * view;

    // Update per-frame UBO in-place (persistent map — no realloc).
    if (state.ubo_mapped[frame] != nullptr) {
        FrameUBO ubo{};
        ubo.view_proj = view_proj;
        std::memcpy(state.ubo_mapped[frame], &ubo, sizeof(FrameUBO));
    }

    const ViewProjPushConstants push{view_proj};
    if (!record_draw_commands(state, image_index, frame, push)) {
        log::log_error(log::LogCategory::Vulkan, "Failed to record draw commands.");
        return false;
    }

    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &state.image_available[frame];
    submit.pWaitDstStageMask    = &wait_stage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &state.command_buffers[image_index];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &state.render_finished[frame];

    if (vkQueueSubmit(device.graphics_queue, 1, &submit, state.in_flight_fences[frame]) != VK_SUCCESS) {
        return false;
    }

    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &state.render_finished[frame];
    present.swapchainCount     = 1;
    present.pSwapchains        = &state.swapchain;
    present.pImageIndices      = &image_index;

    const VkResult present_result = vkQueuePresentKHR(device.present_queue, &present);
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR
        || window.framebuffer_resized) {
        window.framebuffer_resized = false;
        if (!renderer_recreate_swapchain(state, device, window)) {
            return false;
        }
    } else if (present_result != VK_SUCCESS) {
        return false;
    }

    state.current_frame = (frame + 1u) % kMaxFramesInFlight;
    return true;
}

}  // namespace csc::vulkan
