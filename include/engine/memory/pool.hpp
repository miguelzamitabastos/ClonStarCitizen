#pragma once

#include "engine/core/types.hpp"

#include <cstddef>

namespace csc::memory {

/// Fixed-capacity object pool over contiguous slots.
/// Destroy = mark inactive; never frees memory at runtime.
template <typename T, std::size_t Capacity>
struct Pool {
    T           slots[Capacity]{};
    bool        active[Capacity]{};
    std::size_t free_stack[Capacity]{};
    std::size_t free_count = Capacity;
    std::size_t alive      = 0;

    void init()
    {
        alive      = 0;
        free_count = Capacity;
        for (std::size_t i = 0; i < Capacity; ++i) {
            active[i]     = false;
            free_stack[i] = Capacity - 1u - i;
        }
    }

    /// Acquire a free slot index, or Capacity if exhausted.
    [[nodiscard]] std::size_t acquire()
    {
        if (free_count == 0) {
            return Capacity;
        }
        const std::size_t index = free_stack[--free_count];
        active[index]           = true;
        ++alive;
        return index;
    }

    void release(std::size_t index)
    {
        if (index >= Capacity || !active[index]) {
            return;
        }
        active[index] = false;
        --alive;
        free_stack[free_count++] = index;
    }

    [[nodiscard]] bool is_active(std::size_t index) const
    {
        return index < Capacity && active[index];
    }
};

}  // namespace csc::memory
