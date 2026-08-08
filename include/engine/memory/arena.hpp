#pragma once

#include "engine/core/types.hpp"

#include <cstddef>

namespace csc::memory {

/// Bump/arena allocator: one contiguous block, allocate forward, reset as a whole.
/// Pre-allocate at level load; never free individual slots during the frame loop.
struct Arena {
    u8*         base      = nullptr;
    std::size_t capacity  = 0;
    std::size_t offset    = 0;
};

/// Owns a heap buffer for the arena lifetime (startup / level load only — not per-frame).
[[nodiscard]] bool arena_create(Arena& arena, std::size_t capacity_bytes);
void arena_destroy(Arena& arena);

/// Reset offset to zero; does not free the underlying block.
void arena_reset(Arena& arena);

/// Allocate `bytes` aligned to `alignment`. Returns nullptr if out of space.
[[nodiscard]] void* arena_alloc(Arena& arena, std::size_t bytes, std::size_t alignment = alignof(std::max_align_t));

template <typename T>
[[nodiscard]] T* arena_alloc_array(Arena& arena, std::size_t count)
{
    return static_cast<T*>(arena_alloc(arena, sizeof(T) * count, alignof(T)));
}

[[nodiscard]] inline std::size_t arena_bytes_used(const Arena& arena)
{
    return arena.offset;
}

[[nodiscard]] inline std::size_t arena_bytes_remaining(const Arena& arena)
{
    return arena.capacity - arena.offset;
}

}  // namespace csc::memory
