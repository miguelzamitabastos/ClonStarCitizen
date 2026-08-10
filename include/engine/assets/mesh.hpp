#pragma once

#include "engine/core/types.hpp"

#include <atomic>
#include <cstddef>

namespace csc::assets {

/// Interleaved CPU/GPU vertex layout for mesh.vert (binding 0).
struct MeshVertex {
    f32 pos[3];
    f32 normal[3];
    f32 color[3];
    f32 uv[2];
};

static_assert(sizeof(MeshVertex) == 44u);

/// CPU-side mesh — all pointers must live in an asset Arena (not new/malloc per mesh).
struct MeshCpu {
    MeshVertex* vertices     = nullptr;
    u32*        indices      = nullptr;
    u32         vertex_count = 0;
    u32         index_count  = 0;
};

enum class MeshLoadStatus : u32 {
    Idle     = 0,
    Loading  = 1,
    Ready    = 2, ///< CPU mesh filled in arena; main thread may upload to GPU
    Failed   = 3,
};

inline constexpr std::size_t kMeshPathMax = 256;
inline constexpr std::size_t kMeshErrorMax = 128;

/// One async load slot — status is atomic; CPU data owned by the asset arena.
struct MeshLoadSlot {
    char                  path[kMeshPathMax]{};
    char                  error[kMeshErrorMax]{};
    std::atomic<u32>      status{static_cast<u32>(MeshLoadStatus::Idle)};
    MeshCpu               cpu{};
};

[[nodiscard]] inline MeshLoadStatus mesh_load_status(const MeshLoadSlot& slot)
{
    return static_cast<MeshLoadStatus>(slot.status.load(std::memory_order_acquire));
}

}  // namespace csc::assets
