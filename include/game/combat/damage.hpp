#pragma once

#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"

#include <flecs.h>

namespace csc::game::combat {

/// P2-02: damageable ship subsystem addressed by a DamageEvent.
/// `None` keeps the Fase 1 behaviour (shield absorb → hull HP).
enum class Subsystem : u8 {
    None    = 0, ///< plain hull hit
    Engines = 1,
    Shields = 2,
    Weapons = 3,
    Sensors = 4,
};

inline constexpr u32 kSubsystemCount = 4; ///< Engines..Sensors (excludes None)

/// [0, kSubsystemCount) slot for a concrete subsystem (None is not indexable).
[[nodiscard]] inline u32 subsystem_index(Subsystem s)
{
    return static_cast<u32>(s) - 1u;
}

/// Instantaneous damage request — consumed from a fixed ring (no heap).
/// P2-02: `subsystem` extends the same event (no per-component event types).
struct DamageEvent {
    flecs::entity_t target    = 0;
    flecs::entity_t source    = 0;
    f32             amount    = 0.f;
    Subsystem       subsystem = Subsystem::None;
};

/// Flecs singleton: deterministic LCG used by combat hit-location rolls (no heap).
struct CombatRng {
    u32 state = 0x9E3779B9u;
};

/// Numerical Recipes LCG — same generator family as economy's mission RNG.
[[nodiscard]] inline u32 rng_next(u32& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

inline constexpr u32 kDamageEventCapacity = 64;

/// Flecs singleton: fixed ring buffer of DamageEvent (push in weapons, drain in hull).
struct DamageEventQueue {
    DamageEvent events[kDamageEventCapacity]{};
    u32         head  = 0; // next pop index
    u32         count = 0;

    [[nodiscard]] bool push(const DamageEvent& ev)
    {
        if (count >= kDamageEventCapacity) {
            return false;
        }
        const u32 idx = (head + count) % kDamageEventCapacity;
        events[idx]   = ev;
        ++count;
        return true;
    }

    [[nodiscard]] bool try_pop(DamageEvent& out)
    {
        if (count == 0) {
            return false;
        }
        out = events[head];
        head = (head + 1u) % kDamageEventCapacity;
        --count;
        return true;
    }

    void clear()
    {
        head  = 0;
        count = 0;
    }
};

}  // namespace csc::game::combat
