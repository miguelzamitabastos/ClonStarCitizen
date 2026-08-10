#pragma once

#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"

#include <flecs.h>

namespace csc::game::combat {

/// Instantaneous damage request — consumed from a fixed ring (no heap).
struct DamageEvent {
    flecs::entity_t target = 0;
    flecs::entity_t source = 0;
    f32             amount = 0.f;
};

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
