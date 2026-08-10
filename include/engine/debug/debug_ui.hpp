#pragma once

#include "engine/core/types.hpp"
#include "engine/vulkan/device.hpp"
#include "engine/vulkan/instance.hpp"

#include <vulkan/vulkan.h>

struct GLFWwindow;

#ifndef CSC_DEBUG_UI
#define CSC_DEBUG_UI 0
#endif

namespace csc::vulkan {
struct RendererState;
}

namespace csc::debug {

/// Per-frame stats for the debug overlay (filled by main; no heap).
struct DebugUiStats {
    f32  fps            = 0.f;
    u32  entity_count   = 0;
    u32  instance_count = 0;
    u32  pipeline_count = 0;
    bool mesh_ready     = false;
    f32  cam_x          = 0.f;
    f32  cam_y          = 0.f;
    f32  cam_z          = 0.f;

    /// Flight telemetry (P1A) — shown when has_ship_telemetry is true.
    bool has_ship_telemetry = false;
    f32  speed              = 0.f;
    f32  energy             = 0.f;
    f32  energy_capacity    = 0.f;
    f32  shield_pct         = 0.f;
    f32  hull_hp            = -1.f;
    f32  hull_max_hp        = 0.f;
    bool coupled            = false;
    u32  rebase_count       = 0; // P1D floating-origin rebases (FloatingOrigin)

    /// On-foot telemetry (P1B) — shown when has_character_telemetry is true.
    bool has_character_telemetry = false;
    f32  health                  = -1.f;
    f32  health_max              = 0.f;
    u32  ammo                    = 0;
    u32  ammo_max                = 0;
    bool grounded                = false;
    bool eva                     = false;
};

/// ImGui overlay state — plain POD + Vulkan resources owned here.
struct DebugUiState {
    bool ready            = false;
    bool visible          = true;
    bool cursor_for_ui    = false;
    bool f1_was_down      = false;
    GLFWwindow* window    = nullptr;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    u32 cached_image_count = 0;
};

[[nodiscard]] bool debug_ui_init(
    DebugUiState& state,
    GLFWwindow* window,
    const vulkan::InstanceState& instance,
    const vulkan::DeviceState& device,
    vulkan::RendererState& renderer);

void debug_ui_shutdown(DebugUiState& state, const vulkan::DeviceState& device);

void debug_ui_begin_frame(DebugUiState& state);
void debug_ui_build(DebugUiState& state, const DebugUiStats& stats);
void debug_ui_render(DebugUiState& state, VkCommandBuffer cmd);

/// Sync ImGui swapchain image-count hint after recreate (safe no-op if inactive).
void debug_ui_on_swapchain_recreated(DebugUiState& state, u32 image_count);

[[nodiscard]] bool debug_ui_want_capture_mouse(const DebugUiState& state);
[[nodiscard]] bool debug_ui_want_capture_keyboard(const DebugUiState& state);

}  // namespace csc::debug
