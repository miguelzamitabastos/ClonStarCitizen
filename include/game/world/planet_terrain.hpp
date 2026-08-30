#pragma once

#include "engine/assets/mesh.hpp"
#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"

namespace csc::game::world {

// --- P4-02: procedural planetary terrain (noise heightmap on a sphere) ------
// Pure, deterministic, no heap. Given a planet's body_seed (from P4-01's
// GeneratedBodyInfo) + radius, `planet_height` is a fBm noise field over the
// unit sphere and `build_planet_patch` fills caller-provided fixed buffers with
// a displaced cube-sphere mesh patch — the exact unit P4-03 will stream into a
// fixed chunk pool. No FILE I/O, no globals; the RNG (rng64.hpp / SplitMix64) is
// bit-identical across platforms (positions still go through libm cos/sin, so
// ULP-identical cross-platform is not guaranteed — reproducible-per-build is).

/// Terrain shape for one planet, derived deterministically from its body_seed.
struct PlanetTerrainParams {
    u64 seed            = 0;
    f32 radius          = 200.f;
    f32 elevation_scale = 8.f;   ///< metres; |planet_height| <= this
    f32 base_frequency  = 2.0f;  ///< noise cycles across the unit sphere
    u32 octaves         = 5;
    f32 lacunarity      = 2.0f;
    f32 gain            = 0.5f;
    f32 sea_level       = 0.f;   ///< heights below this are flattened (ocean floor)
};

/// Derive the terrain params for a planet. `has_atmosphere` (P4-01) makes the
/// planet wetter (a sea level near 0) vs a dry rock (sea level at the floor).
[[nodiscard]] PlanetTerrainParams planet_terrain_params(
    u64 body_seed, f32 radius, bool has_atmosphere);

/// Elevation offset (metres, in [-elevation_scale, +elevation_scale]) at a point
/// on the unit sphere. `unit_dir` must be normalised.
[[nodiscard]] f32 planet_height(const PlanetTerrainParams& pp, const glm::vec3& unit_dir);

/// World-space surface point at `unit_dir` (planet_center + dir*(radius+height)).
[[nodiscard]] glm::vec3 planet_surface_point(
    const PlanetTerrainParams& pp, const glm::vec3& planet_center, const glm::vec3& unit_dir);

/// Unit direction on the sphere for cube face `face` (0..5) at face-local
/// (s, t) in [0, 1]. The six faces tile the whole sphere.
[[nodiscard]] glm::vec3 cube_sphere_dir(u32 face, f32 s, f32 t);

inline constexpr u32 kCubeSphereFaces     = 6;
inline constexpr u32 kMaxPatchResolution  = 64;  ///< quads per patch side
inline constexpr u32 kMaxPatchVertices    = (kMaxPatchResolution + 1) * (kMaxPatchResolution + 1);
inline constexpr u32 kMaxPatchIndices     = kMaxPatchResolution * kMaxPatchResolution * 6;

/// A rectangular region of one cube face, at a given tessellation.
struct TerrainPatchSpec {
    u32 face       = 0;
    f32 u0         = 0.f;
    f32 v0         = 0.f;
    f32 u1         = 1.f;
    f32 v1         = 1.f;
    u32 resolution = 32;  ///< quads per side; <= kMaxPatchResolution
};

/// Build a displaced cube-sphere mesh patch into `out_verts` / `out_indices`
/// (capacity >= kMaxPatchVertices / kMaxPatchIndices). Positions are
/// planet-center-relative world units; normals from tangent finite-difference;
/// colour is a height ramp; uv is face-local. Returns false if
/// `spec.resolution > kMaxPatchResolution`.
[[nodiscard]] bool build_planet_patch(
    const PlanetTerrainParams& pp,
    const glm::vec3&           planet_center,
    const TerrainPatchSpec&    spec,
    assets::MeshVertex*        out_verts,
    u32&                       out_vertex_count,
    u32*                       out_indices,
    u32&                       out_index_count);

/// P4-02 headless check (scene gate CSC_PLANETGEN_SMOKE=1): for a few planet
/// seeds — height stays within elevation_scale, normals are unit length,
/// regenerating a patch is byte-identical, and two patches sharing a face edge
/// agree exactly on the seam vertices. Logs `CSC_PLANETGEN_SMOKE: PASS|FAIL`.
[[nodiscard]] bool planet_terrain_smoke_test();

}  // namespace csc::game::world
