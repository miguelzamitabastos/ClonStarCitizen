#pragma once

#include "engine/core/types.hpp"
#include "engine/input/actions.hpp"

#include <flecs.h>

namespace csc::game::save {

/// P1F: versioned binary save (magic + schema_version). I/O may allocate outside the
/// frame loop; never call save_game/load_game from fixed_step mid-iteration without
/// SaveInProgress gating.

inline constexpr u32 kSaveMagic         = 0x31565343u; // 'CSV1' LE
inline constexpr u32 kSaveSchemaVersion = 1u;

inline constexpr const char* kSaveDir          = "saves";
inline constexpr const char* kSlot0Path        = "saves/slot0.sav";
inline constexpr const char* kAutosavePath     = "saves/autosave.sav";
inline constexpr const char* kQuicksavePath    = "saves/quicksave.sav";
inline constexpr const char* kSmokeSavePath    = "saves/smoke.sav";

/// Stable identity across sessions — never serialize flecs entity handles.
struct PersistentId {
    u64 id = 0;
};

/// Singleton: next id to assign (starts at 1).
struct PersistentIdCounter {
    u64 next_id = 1;
};

/// Singleton: true while a save/load I/O is in progress (HUD + input gate).
struct SaveInProgress {
    bool active = false;
};

enum class SaveSlot : u8 {
    Slot0    = 0,
    Autosave = 1,
    Quick    = 2,
};

[[nodiscard]] const char* slot_path(SaveSlot slot);

void register_systems(flecs::world& world);

/// Ensure SaveInProgress + PersistentIdCounter singletons (init / level load).
void ensure_singletons(flecs::world& world);

/// Assign a new PersistentId if missing; returns the id. Level-load / spawn only.
[[nodiscard]] u64 assign_persistent_id(flecs::world& world, flecs::entity e);

/// Create `saves/` if missing (I/O — not in frame loop).
[[nodiscard]] bool ensure_save_directory();

[[nodiscard]] bool save_game(flecs::world& world, const char* path);
[[nodiscard]] bool load_game(flecs::world& world, const char* path);

[[nodiscard]] bool save_to_slot(flecs::world& world, SaveSlot slot);
[[nodiscard]] bool load_from_slot(flecs::world& world, SaveSlot slot);

/// Poll QuickSave / QuickLoad (and finish any deferred work). Safe outside fixed_step.
void frame_poll(flecs::world& world, const input::ActionState& actions);

/// CSC_SAVE_SMOKE=1: save → mutate → load → verify; logs PASS/FAIL. Returns success.
[[nodiscard]] bool run_smoke_test(flecs::world& world);

}  // namespace csc::game::save
