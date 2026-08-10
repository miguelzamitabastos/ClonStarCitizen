#include "engine/debug/debug_ui.hpp"

#include "engine/log/log.hpp"
#include "engine/vulkan/renderer.hpp"

#if CSC_DEBUG_UI
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif

namespace csc::debug {
namespace {

#if CSC_DEBUG_UI

void set_cursor_mode(GLFWwindow* window, bool unlocked)
{
    if (window == nullptr) {
        return;
    }
    if (unlocked) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        }
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
    }
}

[[nodiscard]] bool create_imgui_descriptor_pool(VkDevice device, VkDescriptorPool* out_pool)
{
    // ImGui demo-style pool: FREE_DESCRIPTOR_SET + room for font + extra textures.
    const VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    };

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets       = 1000;
    pool_info.poolSizeCount = static_cast<u32>(sizeof(pool_sizes) / sizeof(pool_sizes[0]));
    pool_info.pPoolSizes    = pool_sizes;

    return vkCreateDescriptorPool(device, &pool_info, nullptr, out_pool) == VK_SUCCESS;
}

void handle_f1_toggle(DebugUiState& state)
{
    if (state.window == nullptr) {
        return;
    }
    const bool f1_down = glfwGetKey(state.window, GLFW_KEY_F1) == GLFW_PRESS;
    if (f1_down && !state.f1_was_down) {
        state.cursor_for_ui = !state.cursor_for_ui;
        set_cursor_mode(state.window, state.cursor_for_ui);
    }
    state.f1_was_down = f1_down;
}

#endif  // CSC_DEBUG_UI

}  // namespace

bool debug_ui_init(
    DebugUiState& state,
    GLFWwindow* window,
    const vulkan::InstanceState& instance,
    const vulkan::DeviceState& device,
    vulkan::RendererState& renderer)
{
#if !CSC_DEBUG_UI
    (void)state;
    (void)window;
    (void)instance;
    (void)device;
    (void)renderer;
    return false;
#else
    state = {};
    state.window  = window;
    state.visible = true;

    if (window == nullptr || instance.instance == VK_NULL_HANDLE
        || device.device == VK_NULL_HANDLE || renderer.render_pass == VK_NULL_HANDLE) {
        log::log_error(log::LogCategory::Vulkan, "Debug UI init failed: invalid Vulkan/window.");
        return false;
    }

    if (!create_imgui_descriptor_pool(device.device, &state.descriptor_pool)) {
        log::log_error(log::LogCategory::Vulkan, "Debug UI: failed to create descriptor pool.");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
        log::log_error(log::LogCategory::Vulkan, "Debug UI: ImGui_ImplGlfw_InitForVulkan failed.");
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(device.device, state.descriptor_pool, nullptr);
        state.descriptor_pool = VK_NULL_HANDLE;
        return false;
    }

    const u32 image_count =
        (renderer.image_count >= 2u) ? renderer.image_count : 2u;

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance        = instance.instance;
    init_info.PhysicalDevice  = device.physical_device;
    init_info.Device          = device.device;
    init_info.QueueFamily     = device.graphics_queue_family;
    init_info.Queue           = device.graphics_queue;
    init_info.DescriptorPool  = state.descriptor_pool;
    init_info.RenderPass      = renderer.render_pass;
    init_info.MinImageCount   = image_count;
    init_info.ImageCount      = image_count;
    init_info.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;
    init_info.Subpass         = 0;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        log::log_error(log::LogCategory::Vulkan, "Debug UI: ImGui_ImplVulkan_Init failed.");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(device.device, state.descriptor_pool, nullptr);
        state.descriptor_pool = VK_NULL_HANDLE;
        return false;
    }

    // v1.91.0: backend uploads fonts via its own command pool/buffer.
    if (!ImGui_ImplVulkan_CreateFontsTexture()) {
        log::log_error(log::LogCategory::Vulkan, "Debug UI: CreateFontsTexture failed.");
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(device.device, state.descriptor_pool, nullptr);
        state.descriptor_pool = VK_NULL_HANDLE;
        return false;
    }

    state.cached_image_count = image_count;
    state.ready              = true;

    log::log_info(log::LogCategory::Core, "Debug UI init OK (Dear ImGui v1.91.0, F1=cursor).");
    return true;
#endif
}

void debug_ui_shutdown(DebugUiState& state, const vulkan::DeviceState& device)
{
#if !CSC_DEBUG_UI
    (void)state;
    (void)device;
#else
    if (!state.ready && state.descriptor_pool == VK_NULL_HANDLE) {
        return;
    }

    if (device.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device.device);
    }

    if (state.ready) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    if (state.descriptor_pool != VK_NULL_HANDLE && device.device != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device.device, state.descriptor_pool, nullptr);
        state.descriptor_pool = VK_NULL_HANDLE;
    }

    if (state.window != nullptr && state.cursor_for_ui) {
        set_cursor_mode(state.window, false);
    }

    state = {};
#endif
}

void debug_ui_begin_frame(DebugUiState& state)
{
#if !CSC_DEBUG_UI
    (void)state;
#else
    if (!state.ready) {
        return;
    }
    handle_f1_toggle(state);
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
#endif
}

void debug_ui_build(DebugUiState& state, const DebugUiStats& stats)
{
#if !CSC_DEBUG_UI
    (void)state;
    (void)stats;
#else
    if (!state.ready || !state.visible) {
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(12.f, 12.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.72f);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::Begin("ClonStarCitizen Debug", &state.visible, flags)) {
        ImGui::Text("FPS: %.1f", static_cast<double>(stats.fps));
        ImGui::Text("Entities (Position): %u", stats.entity_count);
        ImGui::Text("Instances: %u", stats.instance_count);
        ImGui::Text("Pipelines: %u", stats.pipeline_count);
        ImGui::Text("Mesh ready: %s", stats.mesh_ready ? "yes" : "no");
        ImGui::Separator();
        ImGui::Text(
            "Camera eye: (%.2f, %.2f, %.2f)",
            static_cast<double>(stats.cam_x),
            static_cast<double>(stats.cam_y),
            static_cast<double>(stats.cam_z));
        if (stats.has_ship_telemetry || stats.hull_hp >= 0.f) {
            ImGui::Separator();
            ImGui::Text("— Flight —");
            ImGui::Text("Speed: %.1f m/s", static_cast<double>(stats.speed));
            ImGui::Text(
                "Energy: %.0f / %.0f",
                static_cast<double>(stats.energy),
                static_cast<double>(stats.energy_capacity));
            ImGui::Text("Shield: %.0f%%", static_cast<double>(stats.shield_pct * 100.f));
            ImGui::Text(
                "Hull: %.0f / %.0f",
                static_cast<double>(stats.hull_hp),
                static_cast<double>(stats.hull_max_hp));
            ImGui::Text("Coupled: %s", stats.coupled ? "ON" : "OFF");
        }
        if (stats.has_character_telemetry || stats.health >= 0.f) {
            ImGui::Separator();
            ImGui::Text("— On Foot —");
            ImGui::Text(
                "Health: %.0f / %.0f",
                static_cast<double>(stats.health),
                static_cast<double>(stats.health_max));
            ImGui::Text("Ammo: %u / %u", stats.ammo, stats.ammo_max);
            ImGui::Text("Grounded: %s", stats.grounded ? "yes" : "no");
            ImGui::Text("EVA: %s", stats.eva ? "yes" : "no");
        }
        ImGui::Separator();
        ImGui::Text("— World (P1D) —");
        ImGui::Text("Floating-origin rebases: %u", stats.rebase_count);
        ImGui::Separator();
        ImGui::Text("F1: %s cursor for UI", state.cursor_for_ui ? "unlock" : "lock");
        ImGui::Text(
            "WantCaptureMouse: %s",
            ImGui::GetIO().WantCaptureMouse ? "yes" : "no");
    }
    ImGui::End();
#endif
}

void debug_ui_render(DebugUiState& state, VkCommandBuffer cmd)
{
#if !CSC_DEBUG_UI
    (void)state;
    (void)cmd;
#else
    if (!state.ready) {
        return;
    }
    // Always end the frame so NewFrame pairing stays valid (minimized / skipped draws).
    ImGui::Render();
    if (!state.visible || cmd == VK_NULL_HANDLE) {
        return;
    }
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data != nullptr) {
        ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
    }
#endif
}

void debug_ui_on_swapchain_recreated(DebugUiState& state, u32 image_count)
{
#if !CSC_DEBUG_UI
    (void)state;
    (void)image_count;
#else
    if (!state.ready) {
        return;
    }
    const u32 count = (image_count >= 2u) ? image_count : 2u;
    if (count != state.cached_image_count) {
        ImGui_ImplVulkan_SetMinImageCount(count);
        state.cached_image_count = count;
    }
#endif
}

bool debug_ui_want_capture_mouse(const DebugUiState& state)
{
#if !CSC_DEBUG_UI
    (void)state;
    return false;
#else
    if (!state.ready) {
        return false;
    }
    return ImGui::GetIO().WantCaptureMouse;
#endif
}

bool debug_ui_want_capture_keyboard(const DebugUiState& state)
{
#if !CSC_DEBUG_UI
    (void)state;
    return false;
#else
    if (!state.ready) {
        return false;
    }
    return ImGui::GetIO().WantCaptureKeyboard;
#endif
}

}  // namespace csc::debug
