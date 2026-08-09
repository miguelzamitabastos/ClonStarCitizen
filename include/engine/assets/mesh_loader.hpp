#pragma once

#include "engine/assets/mesh.hpp"
#include "engine/memory/arena.hpp"

#include <thread>

namespace csc::assets {

/// Owns a background worker that fills `slot.cpu` from a glTF file into `asset_arena`.
/// Main thread must only upload to GPU after status == Ready. No frame-loop allocations.
struct MeshLoader {
    MeshLoadSlot    slot{};
    memory::Arena*  arena  = nullptr;
    std::thread     worker{};
    bool            joined = true;
};

/// Kick a background load of `path` into `arena`. Joins any previous worker first.
/// Returns false if path is empty or a load is already in flight on this loader.
[[nodiscard]] bool mesh_loader_start(MeshLoader& loader, memory::Arena& arena, const char* path);

/// Block until the worker finishes (call at shutdown or before destroying the arena).
void mesh_loader_join(MeshLoader& loader);

/// Join + reset slot status to Idle (does not reset the arena).
void mesh_loader_shutdown(MeshLoader& loader);

/// Synchronous glTF parse → copy into arena (also used by the background worker).
[[nodiscard]] bool mesh_load_gltf_into_arena(
    memory::Arena& arena,
    const char* path,
    MeshCpu& out_mesh,
    char* error_buf,
    std::size_t error_cap);

}  // namespace csc::assets
