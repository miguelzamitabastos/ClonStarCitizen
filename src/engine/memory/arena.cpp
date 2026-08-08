#include "engine/memory/arena.hpp"

#include <cstdint>
#include <cstdlib>

namespace csc::memory {

bool arena_create(Arena& arena, std::size_t capacity_bytes)
{
    if (capacity_bytes == 0) {
        return false;
    }

    void* block = std::malloc(capacity_bytes);
    if (block == nullptr) {
        return false;
    }

    arena.base     = static_cast<u8*>(block);
    arena.capacity = capacity_bytes;
    arena.offset   = 0;
    return true;
}

void arena_destroy(Arena& arena)
{
    std::free(arena.base);
    arena.base     = nullptr;
    arena.capacity = 0;
    arena.offset   = 0;
}

void arena_reset(Arena& arena)
{
    arena.offset = 0;
}

void* arena_alloc(Arena& arena, std::size_t bytes, std::size_t alignment)
{
    if (arena.base == nullptr || bytes == 0 || alignment == 0) {
        return nullptr;
    }

    const std::uintptr_t current = reinterpret_cast<std::uintptr_t>(arena.base) + arena.offset;
    const std::uintptr_t aligned = (current + (alignment - 1u)) & ~(static_cast<std::uintptr_t>(alignment) - 1u);
    const std::size_t    padding = static_cast<std::size_t>(aligned - current);
    const std::size_t    total   = padding + bytes;

    if (arena.offset + total > arena.capacity) {
        return nullptr;
    }

    arena.offset += total;
    return reinterpret_cast<void*>(aligned);
}

}  // namespace csc::memory
