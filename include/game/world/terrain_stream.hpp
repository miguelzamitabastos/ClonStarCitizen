#pragma once

#include "engine/assets/mesh.hpp"
#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"
#include "engine/memory/pool.hpp"
#include "game/world/planet_terrain.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

namespace csc::game::world {

// --- P4-03: LOD-chunked planetary terrain streaming ------------------------
// Extends the P1D-03 proximity-streaming idea to the sphere: a cube-sphere
// quadtree picks which patches should be resident given the player position; a
// FIXED chunk pool holds them; a background worker runs P4-02's
// build_planet_patch into a chunk's pre-reserved CPU buffers; the main thread
// picks up CpuReady chunks and hands them to a GPU-upload callback (P4-08 wires
// the Vulkan side — this task is the streaming logic + its headless proof).
//
// Fase 4 rules honoured: the pool never grows (kMaxLoadedChunks); if the desired
// set is larger, the farthest patches simply don't get a slot (coarser far
// coverage) rather than exceeding capacity; generation is off the main thread,
// GPU upload is on it.

inline constexpr u32 kMaxLoadedChunks      = 48;
inline constexpr u32 kMaxLodDepth          = 6;
inline constexpr u32 kMaxDesiredSpecs      = 128;
inline constexpr u32 kTerrainChunkQuads    = 24;  ///< quads per chunk side (fixed tessellation)
inline constexpr u32 kTerrainChunkVerts    = (kTerrainChunkQuads + 1) * (kTerrainChunkQuads + 1);
inline constexpr u32 kTerrainChunkIndices  = kTerrainChunkQuads * kTerrainChunkQuads * 6;

enum class ChunkState : u32 {
    Empty      = 0,
    Queued     = 1,  ///< main thread reserved it, worker not started
    Generating = 2,  ///< worker is filling cpu buffers
    CpuReady   = 3,  ///< cpu buffers filled, awaiting GPU upload
    GpuReady   = 4,  ///< uploaded, drawable
    Retiring   = 5,  ///< main thread wants this slot back
};

/// One resident terrain chunk. CPU buffers are reserved in-place (never heap).
struct TerrainChunk {
    TerrainPatchSpec      spec{};
    u64                   spec_key   = 0;  ///< dedup / stable identity of the patch
    u32                   lod_depth  = 0;
    f32                   priority   = 0.f;  ///< distance to player (lower = keep)
    std::atomic<u32>      state{static_cast<u32>(ChunkState::Empty)};
    std::atomic<bool>     wants_retire{false};  ///< main wants the slot back; worker ignores it
    u32                   gpu_handle = 0xFFFFFFFFu;  ///< opaque, owned by the upload callback

    assets::MeshVertex    verts[kTerrainChunkVerts]{};
    u32                   indices[kTerrainChunkIndices]{};
    u32                   vert_count  = 0;
    u32                   index_count = 0;
};

/// Called on the main thread when a chunk's CPU data is ready (upload to GPU)
/// and when a chunk retires (free its GPU resources). `ctx` is the pointer given
/// to terrain_streamer_init. Either may be null (headless).
using ChunkUploadFn = void (*)(void* ctx, TerrainChunk& chunk);
using ChunkRetireFn = void (*)(void* ctx, TerrainChunk& chunk);

struct TerrainStreamer {
    PlanetTerrainParams params{};
    glm::vec3           planet_center{0.f};

    memory::Pool<TerrainChunk, kMaxLoadedChunks> pool{};

    // Background worker + job queue (indices into pool.slots).
    std::thread             worker;
    std::mutex              mtx;
    std::condition_variable cv;
    u32                     job_ring[kMaxLoadedChunks]{};
    u32                     job_head   = 0;
    u32                     job_count  = 0;
    std::atomic<bool>       running{false};

    ChunkUploadFn upload_cb = nullptr;
    ChunkRetireFn retire_cb = nullptr;
    void*         cb_ctx    = nullptr;

    // Telemetry (main thread).
    u32 last_desired    = 0;
    u32 last_max_depth  = 0;
    u32 uploads_done    = 0;
};

/// Pure: fill `out` with the patch specs that should be resident for a player at
/// `player_pos` (planet-center-relative frame). Returns the count (<= max_out).
/// Deterministic in (params, planet_center, player_pos). `out_max_depth` (opt)
/// reports the deepest LOD level selected.
[[nodiscard]] u32 select_terrain_lod(
    const PlanetTerrainParams& params,
    const glm::vec3&           planet_center,
    const glm::vec3&           player_pos,
    TerrainPatchSpec*          out,
    u32                        max_out,
    u32*                       out_max_depth = nullptr);

/// Start the streamer for one planet and spin up its worker thread.
void terrain_streamer_init(
    TerrainStreamer&           s,
    const PlanetTerrainParams& params,
    const glm::vec3&           planet_center,
    ChunkUploadFn              upload_cb,
    ChunkRetireFn              retire_cb,
    void*                      cb_ctx);

/// Main-thread tick: reselect LOD for `player_pos`, enqueue new chunks (never
/// past kMaxLoadedChunks), retire chunks no longer wanted, and upload any that
/// finished generating. Cheap; safe to call every frame.
void terrain_streamer_update(TerrainStreamer& s, const glm::vec3& player_pos);

/// Stop the worker and release every chunk (invokes retire_cb). Idempotent.
void terrain_streamer_shutdown(TerrainStreamer& s);

/// P4-03 headless check (scene gate CSC_TERRAINSTREAM_SMOKE=1): drive the
/// streamer through an approach to a planet and assert the pool never exceeds
/// capacity, LOD deepens as the player nears, every resident chunk reaches
/// GpuReady with a non-empty mesh, and the resident set is deterministic for a
/// given player position. Logs `CSC_TERRAINSTREAM_SMOKE: PASS|FAIL`.
[[nodiscard]] bool terrain_stream_smoke_test();

}  // namespace csc::game::world
