#include "game/world/terrain_stream.hpp"

#include "engine/log/log.hpp"

#include <chrono>
#include <cmath>
#include <cstring>

namespace csc::game::world {
namespace {

/// Stable identity of a patch (face + quantised uv rect). Resolution is fixed
/// (kTerrainChunkQuads) so it is not part of the key.
[[nodiscard]] u64 patch_spec_key(const TerrainPatchSpec& s)
{
    const auto q = [](f32 v) {
        return static_cast<u64>(std::lround(static_cast<double>(v) * 65536.0)) & 0x1FFFFFull;
    };
    u64 k = static_cast<u64>(s.face & 7u);
    k = (k << 21) | q(s.u0);
    k = (k << 21) | q(s.v0);
    // fold the far corner in via xor so the whole rect matters without overflow
    k ^= (q(s.u1) << 11) ^ (q(s.v1) << 3);
    return k;
}

[[nodiscard]] u32 rect_depth(const TerrainPatchSpec& s)
{
    u32 depth = 0;
    f32 w     = s.u1 - s.u0;
    while (w < 0.75f && depth < kMaxLodDepth) {
        w *= 2.f;
        ++depth;
    }
    return depth;
}

struct LodNode {
    u32 face;
    f32 u0, v0, u1, v1;
    u32 depth;
};

/// Subdivide a node while the player is within this of its center.
[[nodiscard]] f32 split_distance(const PlanetTerrainParams& pp, u32 depth)
{
    return pp.radius * 3.0f / static_cast<f32>(1u << depth);
}

void lod_recurse(
    const PlanetTerrainParams& pp,
    const glm::vec3&           center,
    const glm::vec3&           player,
    const LodNode&             node,
    TerrainPatchSpec*          out,
    u32                        max_out,
    u32&                       count,
    u32&                       max_depth)
{
    if (count >= max_out) {
        return;
    }
    const f32       cs   = 0.5f * (node.u0 + node.u1);
    const f32       ct   = 0.5f * (node.v0 + node.v1);
    const glm::vec3 dir  = cube_sphere_dir(node.face, cs, ct);
    const glm::vec3 surf = planet_surface_point(pp, center, dir);
    const f32       dist = glm::length(player - surf);

    const bool split = node.depth < kMaxLodDepth && dist < split_distance(pp, node.depth)
                       && count + 4u <= max_out;
    if (split) {
        const f32     mu = cs;
        const f32     mv = ct;
        const LodNode kids[4] = {
            {node.face, node.u0, node.v0, mu, mv, node.depth + 1u},
            {node.face, mu, node.v0, node.u1, mv, node.depth + 1u},
            {node.face, node.u0, mv, mu, node.v1, node.depth + 1u},
            {node.face, mu, mv, node.u1, node.v1, node.depth + 1u},
        };
        for (const LodNode& k : kids) {
            lod_recurse(pp, center, player, k, out, max_out, count, max_depth);
        }
        return;
    }

    TerrainPatchSpec& s = out[count++];
    s.face       = node.face;
    s.u0         = node.u0;
    s.v0         = node.v0;
    s.u1         = node.u1;
    s.v1         = node.v1;
    s.resolution = kTerrainChunkQuads;
    if (node.depth > max_depth) {
        max_depth = node.depth;
    }
}

void worker_loop(TerrainStreamer* s)
{
    for (;;) {
        u32 idx = 0;
        {
            std::unique_lock<std::mutex> lk(s->mtx);
            s->cv.wait(lk, [s] { return !s->running.load() || s->job_count > 0; });
            if (s->job_count == 0) {
                if (!s->running.load()) {
                    return;
                }
                continue;
            }
            idx           = s->job_ring[s->job_head];
            s->job_head    = (s->job_head + 1u) % kMaxLoadedChunks;
            --s->job_count;
        }

        TerrainChunk& c        = s->pool.slots[idx];
        u32           expected = static_cast<u32>(ChunkState::Queued);
        if (!c.state.compare_exchange_strong(
                expected, static_cast<u32>(ChunkState::Generating),
                std::memory_order_acq_rel)) {
            continue;  // slot was retired / reused before we got to it
        }

        u32        vc = 0;
        u32        ic = 0;
        const bool ok = build_planet_patch(
            s->params, s->planet_center, c.spec, c.verts, vc, c.indices, ic);
        c.vert_count  = ok ? vc : 0u;
        c.index_count = ok ? ic : 0u;

        u32 gen = static_cast<u32>(ChunkState::Generating);
        c.state.compare_exchange_strong(
            gen,
            static_cast<u32>(ok ? ChunkState::CpuReady : ChunkState::Empty),
            std::memory_order_release);
    }
}

}  // namespace

u32 select_terrain_lod(
    const PlanetTerrainParams& params,
    const glm::vec3&           planet_center,
    const glm::vec3&           player_pos,
    TerrainPatchSpec*          out,
    u32                        max_out,
    u32*                       out_max_depth)
{
    // Visit faces nearest-first so near-player detail claims the budget before
    // the far side does.
    u32 face_order[kCubeSphereFaces] = {0, 1, 2, 3, 4, 5};
    f32 face_dist[kCubeSphereFaces]  = {};
    for (u32 f = 0; f < kCubeSphereFaces; ++f) {
        const glm::vec3 d = cube_sphere_dir(f, 0.5f, 0.5f);
        face_dist[f] = glm::length(player_pos - planet_surface_point(params, planet_center, d));
    }
    for (u32 a = 1; a < kCubeSphereFaces; ++a) {
        const u32 fo = face_order[a];
        const f32 fd = face_dist[fo];
        i32       b  = static_cast<i32>(a) - 1;
        while (b >= 0 && face_dist[face_order[b]] > fd) {
            face_order[b + 1] = face_order[b];
            --b;
        }
        face_order[b + 1] = fo;
    }

    u32 count     = 0;
    u32 max_depth = 0;
    for (u32 i = 0; i < kCubeSphereFaces && count < max_out; ++i) {
        const LodNode root{face_order[i], 0.f, 0.f, 1.f, 1.f, 0u};
        lod_recurse(params, planet_center, player_pos, root, out, max_out, count, max_depth);
    }
    if (out_max_depth != nullptr) {
        *out_max_depth = max_depth;
    }
    return count;
}

void terrain_streamer_init(
    TerrainStreamer&           s,
    const PlanetTerrainParams& params,
    const glm::vec3&           planet_center,
    ChunkUploadFn              upload_cb,
    ChunkRetireFn              retire_cb,
    void*                      cb_ctx)
{
    s.params        = params;
    s.planet_center = planet_center;
    s.upload_cb     = upload_cb;
    s.retire_cb     = retire_cb;
    s.cb_ctx        = cb_ctx;
    s.pool.init();
    s.job_head      = 0;
    s.job_count     = 0;
    s.last_desired  = 0;
    s.last_max_depth = 0;
    s.uploads_done  = 0;
    s.running.store(true);
    s.worker = std::thread(worker_loop, &s);
}

void terrain_streamer_update(TerrainStreamer& s, const glm::vec3& player_pos)
{
    if (!s.running.load()) {
        return;
    }

    TerrainPatchSpec desired[kMaxDesiredSpecs];
    u32              max_depth = 0;
    const u32        n =
        select_terrain_lod(s.params, s.planet_center, player_pos, desired, kMaxDesiredSpecs, &max_depth);
    s.last_desired   = n;
    s.last_max_depth = max_depth;

    u64 desired_keys[kMaxDesiredSpecs];
    f32 desired_prio[kMaxDesiredSpecs];
    u32 order[kMaxDesiredSpecs];
    for (u32 d = 0; d < n; ++d) {
        desired_keys[d] = patch_spec_key(desired[d]);
        const glm::vec3 dir = cube_sphere_dir(
            desired[d].face, 0.5f * (desired[d].u0 + desired[d].u1),
            0.5f * (desired[d].v0 + desired[d].v1));
        desired_prio[d] = glm::length(player_pos - planet_surface_point(s.params, s.planet_center, dir));
        order[d]        = d;
    }
    for (u32 a = 1; a < n; ++a) {
        const u32 o = order[a];
        const f32 p = desired_prio[o];
        i32       b = static_cast<i32>(a) - 1;
        while (b >= 0 && desired_prio[order[b]] > p) {
            order[b + 1] = order[b];
            --b;
        }
        order[b + 1] = o;
    }

    // --- retire pass: finish pending retires, flag chunks no longer wanted ---
    for (std::size_t i = 0; i < kMaxLoadedChunks; ++i) {
        if (!s.pool.is_active(i)) {
            continue;
        }
        TerrainChunk& c  = s.pool.slots[i];
        const u32     st = c.state.load(std::memory_order_acquire);

        if (c.wants_retire.load(std::memory_order_acquire)) {
            if (st == static_cast<u32>(ChunkState::Generating)) {
                continue;  // worker still owns the buffers — retire next tick
            }
            if (s.retire_cb != nullptr) {
                s.retire_cb(s.cb_ctx, c);
            }
            c.state.store(static_cast<u32>(ChunkState::Empty), std::memory_order_release);
            c.wants_retire.store(false, std::memory_order_release);
            c.gpu_handle  = 0xFFFFFFFFu;
            c.spec_key    = 0;
            c.vert_count  = 0;
            c.index_count = 0;
            s.pool.release(i);
            continue;
        }

        bool wanted = false;
        for (u32 d = 0; d < n; ++d) {
            if (desired_keys[d] == c.spec_key) {
                wanted = true;
                break;
            }
        }
        if (!wanted) {
            c.wants_retire.store(true, std::memory_order_release);
        }
    }

    // --- enqueue new chunks, nearest-first, never past capacity -------------
    for (u32 oi = 0; oi < n; ++oi) {
        const u32               di  = order[oi];
        const TerrainPatchSpec&  sp  = desired[di];
        const u64                key = desired_keys[di];

        bool resident = false;
        for (std::size_t i = 0; i < kMaxLoadedChunks; ++i) {
            if (s.pool.is_active(i) && s.pool.slots[i].spec_key == key
                && !s.pool.slots[i].wants_retire.load(std::memory_order_acquire)) {
                resident = true;
                break;
            }
        }
        if (resident) {
            continue;
        }

        const std::size_t slot = s.pool.acquire();
        if (slot == kMaxLoadedChunks) {
            break;  // pool full — the farthest desired patches just miss out
        }
        TerrainChunk& c = s.pool.slots[slot];
        c.spec        = sp;
        c.spec_key    = key;
        c.lod_depth   = rect_depth(sp);
        c.priority    = desired_prio[di];
        c.vert_count  = 0;
        c.index_count = 0;
        c.gpu_handle  = 0xFFFFFFFFu;
        c.wants_retire.store(false, std::memory_order_release);
        c.state.store(static_cast<u32>(ChunkState::Queued), std::memory_order_release);
        {
            std::lock_guard<std::mutex> lk(s.mtx);
            s.job_ring[(s.job_head + s.job_count) % kMaxLoadedChunks] = static_cast<u32>(slot);
            ++s.job_count;
        }
        s.cv.notify_one();
    }

    // --- drain CpuReady -> upload -> GpuReady (main thread) ----------------
    for (std::size_t i = 0; i < kMaxLoadedChunks; ++i) {
        if (!s.pool.is_active(i)) {
            continue;
        }
        TerrainChunk& c        = s.pool.slots[i];
        u32           expected = static_cast<u32>(ChunkState::CpuReady);
        if (c.state.compare_exchange_strong(
                expected, static_cast<u32>(ChunkState::GpuReady), std::memory_order_acq_rel)) {
            if (s.upload_cb != nullptr) {
                s.upload_cb(s.cb_ctx, c);
            }
            ++s.uploads_done;
        }
    }
}

void terrain_streamer_shutdown(TerrainStreamer& s)
{
    if (!s.running.exchange(false)) {
        return;
    }
    s.cv.notify_all();
    if (s.worker.joinable()) {
        s.worker.join();
    }
    for (std::size_t i = 0; i < kMaxLoadedChunks; ++i) {
        if (!s.pool.is_active(i)) {
            continue;
        }
        if (s.retire_cb != nullptr) {
            s.retire_cb(s.cb_ctx, s.pool.slots[i]);
        }
        s.pool.slots[i].state.store(static_cast<u32>(ChunkState::Empty));
        s.pool.release(i);
    }
}

bool terrain_stream_smoke_test()
{
    static TerrainStreamer s;

    const PlanetTerrainParams pp     = planet_terrain_params(4242ull, 300.f, true);
    const glm::vec3           center{0.f};

    // --- LOD selection deepens as the player approaches. -------------------
    TerrainPatchSpec specs[kMaxDesiredSpecs];
    const glm::vec3  approach[4] = {
        glm::vec3{9000.f, 0.f, 0.f}, glm::vec3{2000.f, 0.f, 0.f},
        glm::vec3{600.f, 0.f, 0.f}, glm::vec3{pp.radius + 25.f, 0.f, 0.f}};

    bool ok        = true;
    u32  prev_depth = 0;
    u32  prev_count = 0;
    for (u32 i = 0; i < 4; ++i) {
        u32 depth = 0;
        const u32 c = select_terrain_lod(pp, center, approach[i], specs, kMaxDesiredSpecs, &depth);
        if (c == 0 || c > kMaxDesiredSpecs) {
            log::log_error(log::LogCategory::Core, "terrainstream: bad spec count %u", c);
            ok = false;
        }
        if (i > 0 && (depth < prev_depth || c < prev_count)) {
            log::log_error(
                log::LogCategory::Core,
                "terrainstream: LOD did not refine on approach (depth %u->%u, count %u->%u)",
                prev_depth, depth, prev_count, c);
            ok = false;
        }
        prev_depth = depth;
        prev_count = c;
    }

    // --- determinism: same player pos -> same resident spec set. ----------
    TerrainPatchSpec a[kMaxDesiredSpecs];
    TerrainPatchSpec b[kMaxDesiredSpecs];
    const u32        na = select_terrain_lod(pp, center, approach[2], a, kMaxDesiredSpecs, nullptr);
    const u32        nb = select_terrain_lod(pp, center, approach[2], b, kMaxDesiredSpecs, nullptr);
    if (na != nb || std::memcmp(a, b, na * sizeof(TerrainPatchSpec)) != 0) {
        log::log_error(log::LogCategory::Core, "terrainstream: LOD selection is not deterministic");
        ok = false;
    }

    // --- drive the streamer through the approach; pump the worker. --------
    u32 upload_count = 0;
    terrain_streamer_init(
        s, pp, center,
        [](void* ctx, TerrainChunk& c) {
            if (c.vert_count > 0 && c.index_count > 0) {
                ++*static_cast<u32*>(ctx);
            }
        },
        nullptr, &upload_count);

    for (u32 i = 0; i < 4; ++i) {
        for (u32 tick = 0; tick < 200; ++tick) {
            terrain_streamer_update(s, approach[i]);
            if (s.pool.alive > kMaxLoadedChunks) {
                log::log_error(
                    log::LogCategory::Core, "terrainstream: pool overflow (%zu > %u)",
                    s.pool.alive, kMaxLoadedChunks);
                ok = false;
            }
            // settled?
            bool pending = false;
            for (std::size_t k = 0; k < kMaxLoadedChunks; ++k) {
                if (!s.pool.is_active(k)) {
                    continue;
                }
                const u32 st = s.pool.slots[k].state.load();
                if (st != static_cast<u32>(ChunkState::GpuReady)) {
                    pending = true;
                    break;
                }
            }
            if (!pending && tick > 2) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    // Everything resident should be drawable with a real mesh.
    for (std::size_t k = 0; k < kMaxLoadedChunks; ++k) {
        if (!s.pool.is_active(k)) {
            continue;
        }
        const TerrainChunk& c = s.pool.slots[k];
        if (c.state.load() != static_cast<u32>(ChunkState::GpuReady) || c.vert_count == 0
            || c.index_count == 0) {
            log::log_error(
                log::LogCategory::Core, "terrainstream: resident chunk not GpuReady / empty");
            ok = false;
            break;
        }
    }
    if (s.uploads_done == 0 || upload_count == 0) {
        log::log_error(log::LogCategory::Core, "terrainstream: no chunks uploaded");
        ok = false;
    }

    terrain_streamer_shutdown(s);

    log::log_info(log::LogCategory::Core, "CSC_TERRAINSTREAM_SMOKE: %s", ok ? "PASS" : "FAIL");
    return ok;
}

}  // namespace csc::game::world
