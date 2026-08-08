#pragma once

#include "engine/core/types.hpp"

namespace csc::ecs {

/// Dense, SoA-friendly component storage over a fixed pre-allocated array.
/// Index into the array matches the entity slot (same generation scheme as World).
template <typename T>
struct ComponentArray {
    T*          data     = nullptr;
    bool*       has      = nullptr;
    std::size_t capacity = 0;

    void bind(T* storage, bool* mask, std::size_t cap)
    {
        data     = storage;
        has      = mask;
        capacity = cap;
    }

    void set(EntityId slot, const T& value)
    {
        if (slot >= capacity) {
            return;
        }
        data[slot] = value;
        has[slot]  = true;
    }

    void remove(EntityId slot)
    {
        if (slot >= capacity) {
            return;
        }
        has[slot] = false;
    }

    [[nodiscard]] bool contains(EntityId slot) const
    {
        return slot < capacity && has[slot];
    }

    [[nodiscard]] T* get(EntityId slot)
    {
        if (!contains(slot)) {
            return nullptr;
        }
        return &data[slot];
    }

    [[nodiscard]] const T* get(EntityId slot) const
    {
        if (!contains(slot)) {
            return nullptr;
        }
        return &data[slot];
    }
};

}  // namespace csc::ecs
