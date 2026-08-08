#include "engine/vulkan/renderer.hpp"

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

/// Matches `shaders/triangle.vert` push_constant block (std430-compatible mat4).
struct ViewProjPushConstants {
    glm::mat4 view_proj;
};
static_assert(sizeof(ViewProjPushConstants) == sizeof(glm::mat4));
static_assert(sizeof(ViewProjPushConstants) == 64u);

/// GLM perspective is OpenGL NDC (Y up); Vulkan NDC has Y down.
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

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &color_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo pass_info{};
    pass_info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass_info.attachmentCount = 1;
    pass_info.pAttachments    = &color_attachment;
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

        VkImageView attachments[] = {state.image_views[i]};

        VkFramebufferCreateInfo fb_info{};
        fb_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass      = state.render_pass;
        fb_info.attachmentCount = 1;
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
        std::fprintf(stderr, "[vulkan] Failed to read SPIR-V: %s\n", path);
        out_words.clear();
        return false;
    }
    return true;
}

[[nodiscard]] bool create_shader_module(
    VkDevice device,
    const std::vector<u32>& words,
    VkShaderModule* out_module)
{
    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = words.size() * sizeof(u32);
    info.pCode    = words.data();
    return vkCreateShaderModule(device, &info, nullptr, out_module) == VK_SUCCESS;
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

[[nodiscard]] bool create_host_vertex_buffer(
    const DeviceState& device,
    const void* data,
    VkDeviceSize buffer_size,
    VkBuffer* out_buffer,
    VkDeviceMemory* out_memory)
{
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size        = buffer_size;
    buffer_info.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
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
    std::memcpy(mapped, data, static_cast<std::size_t>(buffer_size));
    vkUnmapMemory(device.device, *out_memory);
    return true;
}

[[nodiscard]] bool create_vertex_buffer(RendererState& state, const DeviceState& device)
{
    // Colored triangle near the origin (XY plane, Z=0). CCW from +Z for back-face cull.
    const TriangleVertex vertices[3] = {
        {{ 0.0f, -0.5f, 0.0f}, {1.0f, 0.2f, 0.2f}},
        {{-0.5f,  0.5f, 0.0f}, {0.2f, 0.2f, 1.0f}},
        {{ 0.5f,  0.5f, 0.0f}, {0.2f, 1.0f, 0.2f}},
    };
    return create_host_vertex_buffer(
        device, vertices, sizeof(vertices), &state.vertex_buffer, &state.vertex_memory);
}

[[nodiscard]] bool create_grid_vertex_buffer(
    RendererState& state,
    const DeviceState& device,
    const render::GroundGridDesc& grid)
{
    state.grid_vertex_count = 0;
    state.grid_visible = grid.visible;

    if (!grid.visible) {
        return true;
    }

    // Stack-fixed bake — no heap in level load path beyond Vulkan allocations.
    std::array<render::GridVertex, render::kMaxGridVertices> verts{};
    u32 count = 0;
    if (!render::grid_bake_vertices(grid, verts.data(), render::kMaxGridVertices, count)) {
        std::fprintf(stderr, "[vulkan] Grid bake exceeded kMaxGridVertices (%u).\n",
            render::kMaxGridVertices);
        return false;
    }
    if (count == 0) {
        state.grid_visible = false;
        return true;
    }

    const VkDeviceSize buffer_size = sizeof(render::GridVertex) * count;
    if (!create_host_vertex_buffer(
            device, verts.data(), buffer_size, &state.grid_vertex_buffer, &state.grid_vertex_memory)) {
        return false;
    }
    state.grid_vertex_count = count;
    return true;
}

[[nodiscard]] bool create_graphics_pipelines(RendererState& state, VkDevice device)
{
    // Shared colored-mesh shaders: triangle list + ground grid line list.
    const std::string vert_path = std::string(CSC_SHADER_DIR) + "/triangle.vert.spv";
    const std::string frag_path = std::string(CSC_SHADER_DIR) + "/triangle.frag.spv";

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

    static_assert(sizeof(TriangleVertex) == sizeof(render::GridVertex));
    static_assert(offsetof(TriangleVertex, pos) == offsetof(render::GridVertex, pos));
    static_assert(offsetof(TriangleVertex, color) == offsetof(render::GridVertex, color));

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(TriangleVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].binding  = 0;
    attrs[0].location = 0;
    attrs[0].format  = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset  = offsetof(TriangleVertex, pos);
    attrs[1].binding  = 0;
    attrs[1].location = 1;
    attrs[1].format  = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset  = offsetof(TriangleVertex, color);

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount   = 1;
    vertex_input.pVertexBindingDescriptions      = &binding;
    vertex_input.vertexAttributeDescriptionCount = 2;
    vertex_input.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo triangle_assembly{};
    triangle_assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    triangle_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineInputAssemblyStateCreateInfo line_assembly{};
    line_assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    line_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

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
    layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges    = &push_range;

    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &state.pipeline_layout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vert_module, nullptr);
        vkDestroyShaderModule(device, frag_module, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount          = 2;
    pipeline_info.pStages             = stages;
    pipeline_info.pVertexInputState   = &vertex_input;
    pipeline_info.pInputAssemblyState = &triangle_assembly;
    pipeline_info.pViewportState      = &viewport_state;
    pipeline_info.pRasterizationState = &raster;
    pipeline_info.pMultisampleState   = &multisample;
    pipeline_info.pColorBlendState    = &color_blend;
    pipeline_info.pDynamicState       = &dynamic;
    pipeline_info.layout              = state.pipeline_layout;
    pipeline_info.renderPass          = state.render_pass;
    pipeline_info.subpass             = 0;

    const VkResult triangle_result = vkCreateGraphicsPipelines(
        device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &state.pipeline);
    if (triangle_result != VK_SUCCESS) {
        vkDestroyShaderModule(device, vert_module, nullptr);
        vkDestroyShaderModule(device, frag_module, nullptr);
        return false;
    }

    pipeline_info.pInputAssemblyState = &line_assembly;
    const VkResult grid_result = vkCreateGraphicsPipelines(
        device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &state.grid_pipeline);

    vkDestroyShaderModule(device, vert_module, nullptr);
    vkDestroyShaderModule(device, frag_module, nullptr);

    return grid_result == VK_SUCCESS;
}

[[nodiscard]] bool record_draw_commands(
    RendererState& state,
    u32 image_index,
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

    VkClearValue clear_value{};
    clear_value.color.float32[0] = kClearR;
    clear_value.color.float32[1] = kClearG;
    clear_value.color.float32[2] = kClearB;
    clear_value.color.float32[3] = kClearA;

    VkRenderPassBeginInfo rp_begin{};
    rp_begin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass        = state.render_pass;
    rp_begin.framebuffer       = state.framebuffers[image_index];
    rp_begin.renderArea.offset = {0, 0};
    rp_begin.renderArea.extent = state.extent;
    rp_begin.clearValueCount   = 1;
    rp_begin.pClearValues      = &clear_value;

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

    // Fresh VP every frame — no UBO map, no pipeline recreate, no buffer realloc.
    vkCmdPushConstants(
        cmd,
        state.pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(ViewProjPushConstants),
        &push);

    const VkDeviceSize offset = 0;

    // Ground reference grid first (line list, pre-baked VB).
    if (state.grid_visible
        && state.grid_pipeline != VK_NULL_HANDLE
        && state.grid_vertex_buffer != VK_NULL_HANDLE
        && state.grid_vertex_count > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state.grid_pipeline);
        vkCmdBindVertexBuffers(cmd, 0, 1, &state.grid_vertex_buffer, &offset);
        vkCmdDraw(cmd, state.grid_vertex_count, 1, 0, 0);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline);
    vkCmdBindVertexBuffers(cmd, 0, 1, &state.vertex_buffer, &offset);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);

    return vkEndCommandBuffer(cmd) == VK_SUCCESS;
}

}  // namespace

bool renderer_create(
    RendererState& state,
    const DeviceState& device,
    const platform::Window& window,
    const render::GroundGridDesc& grid)
{
    state = {};

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.physical_device, device.surface, &caps);

    u32 format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device.physical_device, device.surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device.physical_device, device.surface, &format_count, formats.data());

    u32 present_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device.physical_device, device.surface, &present_count, nullptr);
    std::vector<VkPresentModeKHR> present_modes(present_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device.physical_device, device.surface, &present_count, present_modes.data());

    if (formats.empty() || present_modes.empty()) {
        std::fprintf(stderr, "[vulkan] Inadequate swapchain support.\n");
        return false;
    }

    const VkSurfaceFormatKHR surface_format = choose_surface_format(formats);
    const VkPresentModeKHR present_mode     = choose_present_mode(present_modes);
    const VkExtent2D extent                 = choose_extent(caps, window);

    if (extent.width == 0 || extent.height == 0) {
        std::fprintf(stderr, "[vulkan] Swapchain extent is zero (minimized?).\n");
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
    swap_info.oldSwapchain   = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device.device, &swap_info, nullptr, &state.swapchain) != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan] vkCreateSwapchainKHR failed.\n");
        return false;
    }

    state.image_format = surface_format.format;
    state.extent       = extent;

    u32 actual_count = 0;
    vkGetSwapchainImagesKHR(device.device, state.swapchain, &actual_count, nullptr);
    if (actual_count == 0 || actual_count > kMaxSwapchainImages) {
        std::fprintf(stderr, "[vulkan] Unexpected swapchain image count (%u).\n", actual_count);
        renderer_destroy(state, device);
        return false;
    }
    state.image_count = actual_count;
    vkGetSwapchainImagesKHR(device.device, state.swapchain, &actual_count, state.images);

    if (!create_render_pass(state, device.device)) {
        std::fprintf(stderr, "[vulkan] Failed to create render pass.\n");
        renderer_destroy(state, device);
        return false;
    }

    if (!create_image_views_and_framebuffers(state, device.device)) {
        std::fprintf(stderr, "[vulkan] Failed to create image views / framebuffers.\n");
        renderer_destroy(state, device);
        return false;
    }

    if (!create_vertex_buffer(state, device)) {
        std::fprintf(stderr, "[vulkan] Failed to create vertex buffer.\n");
        renderer_destroy(state, device);
        return false;
    }

    if (!create_grid_vertex_buffer(state, device, grid)) {
        std::fprintf(stderr, "[vulkan] Failed to create ground grid vertex buffer.\n");
        renderer_destroy(state, device);
        return false;
    }

    if (!create_graphics_pipelines(state, device.device)) {
        std::fprintf(stderr, "[vulkan] Failed to create graphics pipelines "
                             "(expect %s/triangle.*.spv).\n",
            CSC_SHADER_DIR);
        renderer_destroy(state, device);
        return false;
    }

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = device.graphics_queue_family;

    if (vkCreateCommandPool(device.device, &pool_info, nullptr, &state.command_pool) != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan] Failed to create command pool.\n");
        renderer_destroy(state, device);
        return false;
    }

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool        = state.command_pool;
    alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = state.image_count;

    if (vkAllocateCommandBuffers(device.device, &alloc_info, state.command_buffers) != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan] Failed to allocate command buffers.\n");
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
            || vkCreateSemaphore(device.device, &sem_info, nullptr, &state.render_finished[i]) != VK_SUCCESS
            || vkCreateFence(device.device, &fence_info, nullptr, &state.in_flight_fences[i]) != VK_SUCCESS) {
            std::fprintf(stderr, "[vulkan] Failed to create sync objects.\n");
            renderer_destroy(state, device);
            return false;
        }
    }

    state.current_frame = 0;
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

    if (state.command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device.device, state.command_pool, nullptr);
    }

    if (state.grid_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device.device, state.grid_pipeline, nullptr);
    }
    if (state.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device.device, state.pipeline, nullptr);
    }
    if (state.pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device.device, state.pipeline_layout, nullptr);
    }

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
    const glm::mat4& view,
    const glm::mat4& projection)
{
    if (state.extent.width == 0 || state.extent.height == 0) {
        return true;
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
        // Fixed-size window path: skip frame rather than crash; full recreate is a later milestone.
        return true;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "[vulkan] vkAcquireNextImageKHR failed (%d).\n", static_cast<int>(acquire));
        return false;
    }

    vkResetFences(device.device, 1, &state.in_flight_fences[frame]);

    // Column-major: clip = P * V * world. Y-flip is renderer-only (Vulkan NDC).
    const ViewProjPushConstants push{
        vulkan_clip_projection(projection) * view,
    };
    if (!record_draw_commands(state, image_index, push)) {
        std::fprintf(stderr, "[vulkan] Failed to record draw commands.\n");
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
        std::fprintf(stderr, "[vulkan] vkQueueSubmit failed.\n");
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
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
        return true;
    }
    if (present_result != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan] vkQueuePresentKHR failed (%d).\n", static_cast<int>(present_result));
        return false;
    }

    state.current_frame = (frame + 1u) % kMaxFramesInFlight;
    return true;
}

}  // namespace csc::vulkan
