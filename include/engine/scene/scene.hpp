#pragma once

#include "engine/core/types.hpp"
#include "engine/ecs/world.hpp"
#include "engine/memory/arena.hpp"

#include <flecs.h>

#include <cstddef>

namespace csc::scene {

/// Shared context for demo scene setup (level load only — no frame-loop work).
struct SceneContext {
    flecs::world*   world       = nullptr;
    memory::Arena*  level_arena = nullptr;
    f32             aspect      = 16.f / 9.f;
    /// Set by setup: true if the scene needs the shared cube mesh uploaded.
    bool            needs_shared_mesh = false;
    /// Instance count requested by the scene (0 = none).
    u32             instance_count = 0;
    /// P3-02: ship catalog id for the player ship in flight-capable scenes
    /// (from `--ship=` / config). nullptr / empty = the scene's own default.
    const char*     player_ship_id = nullptr;
    /// P3-06: suit catalog id for the player character in on-foot scenes
    /// (from `--suit=` / config). nullptr / empty = the scene's own default.
    const char*     player_suit_id = nullptr;
    /// P4-01: star-system seed for procedural scenes (from `--seed=` / config).
    /// nullptr / empty = use the fixed star_system.cfg.
    const char*     world_seed = nullptr;
    /// P4-05: galaxy node index for universe_test (from `--system=` / config).
    /// nullptr / empty = node 0 (home). Ignored when world_seed is set.
    const char*     galaxy_system = nullptr;
    /// P4-08: optional per-frame hook, called by the main loop while the sim
    /// runs. `renderer` / `device` are `vulkan::RendererState*` / `DeviceState*`
    /// (opaque here to keep Vulkan out of most scene TUs). Used by
    /// `procedural_test` to drive terrain streaming + GPU upload.
    void (*on_frame)(void* renderer, void* device, flecs::world& world, f32 dt) = nullptr;
    /// P4-08: optional teardown hook, called by the main loop on exit BEFORE the
    /// renderer is destroyed (procedural_test joins its terrain worker here).
    void (*on_shutdown)() = nullptr;
};

using SceneSetupFn = bool (*)(SceneContext& ctx);

struct SceneDesc {
    const char*  name;
    const char*  description;
    SceneSetupFn setup;
};

/// Resolve scene by name; unknown names log available list and fall back to grid_freelook.
[[nodiscard]] bool scene_setup_by_name(const char* name, SceneContext& ctx);

/// Write comma-separated scene names into a fixed caller buffer (no heap).
void scene_list(char* out, std::size_t cap);

[[nodiscard]] const SceneDesc* scene_find(const char* name);

}  // namespace csc::scene
