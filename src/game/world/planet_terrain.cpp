#include "game/world/planet_terrain.hpp"

#include "engine/log/log.hpp"
#include "game/world/rng64.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace csc::game::world {
namespace {

[[nodiscard]] f32 clampf(f32 v, f32 lo, f32 hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

// --- deterministic gradient (Perlin-style) noise, no libs -------------------

[[nodiscard]] u64 mix64(u64 x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

[[nodiscard]] u64 lattice_hash(i64 ix, i64 iy, i64 iz, u64 seed)
{
    u64 h = mix64(seed ^ 0xA24BAED4963EE407ull);
    h     = mix64(h ^ static_cast<u64>(ix));
    h     = mix64(h ^ (static_cast<u64>(iy) << 21));
    h     = mix64(h ^ (static_cast<u64>(iz) << 42));
    return h;
}

[[nodiscard]] glm::vec3 grad12(u64 h)
{
    static const glm::vec3 kG[12] = {
        {1.f, 1.f, 0.f}, {-1.f, 1.f, 0.f}, {1.f, -1.f, 0.f}, {-1.f, -1.f, 0.f},
        {1.f, 0.f, 1.f}, {-1.f, 0.f, 1.f}, {1.f, 0.f, -1.f}, {-1.f, 0.f, -1.f},
        {0.f, 1.f, 1.f}, {0.f, -1.f, 1.f}, {0.f, 1.f, -1.f}, {0.f, -1.f, -1.f}};
    return kG[h % 12u];
}

[[nodiscard]] f32 fade(f32 t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }
[[nodiscard]] f32 lerpf(f32 a, f32 b, f32 t) { return a + t * (b - a); }

[[nodiscard]] f32 perlin3(const glm::vec3& p, u64 seed)
{
    const f32 fx0 = std::floor(p.x);
    const f32 fy0 = std::floor(p.y);
    const f32 fz0 = std::floor(p.z);
    const auto x0 = static_cast<i64>(fx0);
    const auto y0 = static_cast<i64>(fy0);
    const auto z0 = static_cast<i64>(fz0);
    const f32 dx = p.x - fx0;
    const f32 dy = p.y - fy0;
    const f32 dz = p.z - fz0;
    const f32 u = fade(dx);
    const f32 v = fade(dy);
    const f32 w = fade(dz);

    const auto dotg = [&](i64 cx, i64 cy, i64 cz, f32 rx, f32 ry, f32 rz) {
        const glm::vec3 g = grad12(lattice_hash(cx, cy, cz, seed));
        return g.x * rx + g.y * ry + g.z * rz;
    };

    const f32 n000 = dotg(x0, y0, z0, dx, dy, dz);
    const f32 n100 = dotg(x0 + 1, y0, z0, dx - 1.f, dy, dz);
    const f32 n010 = dotg(x0, y0 + 1, z0, dx, dy - 1.f, dz);
    const f32 n110 = dotg(x0 + 1, y0 + 1, z0, dx - 1.f, dy - 1.f, dz);
    const f32 n001 = dotg(x0, y0, z0 + 1, dx, dy, dz - 1.f);
    const f32 n101 = dotg(x0 + 1, y0, z0 + 1, dx - 1.f, dy, dz - 1.f);
    const f32 n011 = dotg(x0, y0 + 1, z0 + 1, dx, dy - 1.f, dz - 1.f);
    const f32 n111 = dotg(x0 + 1, y0 + 1, z0 + 1, dx - 1.f, dy - 1.f, dz - 1.f);

    const f32 nx00 = lerpf(n000, n100, u);
    const f32 nx10 = lerpf(n010, n110, u);
    const f32 nx01 = lerpf(n001, n101, u);
    const f32 nx11 = lerpf(n011, n111, u);
    const f32 nxy0 = lerpf(nx00, nx10, v);
    const f32 nxy1 = lerpf(nx01, nx11, v);
    return lerpf(nxy0, nxy1, w);
}

[[nodiscard]] f32 fbm3(glm::vec3 p, u64 seed, u32 octaves, f32 lacunarity, f32 gain)
{
    f32 amp  = 0.5f;
    f32 sum  = 0.f;
    f32 norm = 0.f;
    for (u32 o = 0; o < octaves; ++o) {
        sum  += amp * perlin3(p, seed + static_cast<u64>(o) * 0x100000001B3ull);
        norm += amp;
        p *= lacunarity;
        amp *= gain;
    }
    return (norm > 0.f) ? (sum / norm) : 0.f;
}

[[nodiscard]] glm::vec3 height_ramp(f32 h, const PlanetTerrainParams& pp)
{
    if (h < pp.sea_level) {
        return glm::vec3{0.08f, 0.18f, 0.38f};  // deep water
    }
    const f32 span = std::max(1e-3f, pp.elevation_scale - pp.sea_level);
    const f32 t    = clampf((h - pp.sea_level) / span, 0.f, 1.f);
    if (t < 0.05f) {
        return glm::vec3{0.76f, 0.70f, 0.50f};  // beach
    }
    if (t < 0.45f) {
        return glm::vec3{0.20f, 0.45f, 0.18f};  // grass
    }
    if (t < 0.80f) {
        return glm::vec3{0.42f, 0.36f, 0.30f};  // rock
    }
    return glm::vec3{0.92f, 0.92f, 0.95f};      // snow
}

}  // namespace

PlanetTerrainParams planet_terrain_params(u64 body_seed, f32 radius, bool has_atmosphere)
{
    Rng64 rng(body_seed ^ 0x50DEC0DE5EED0001ull);

    PlanetTerrainParams pp{};
    pp.seed            = body_seed;
    pp.radius          = radius;
    pp.elevation_scale = radius * rng.range(0.015f, 0.06f);
    pp.base_frequency  = rng.range(1.4f, 3.4f);
    pp.octaves         = rng.range_u32(4u, 6u);
    pp.lacunarity      = rng.range(1.9f, 2.2f);
    pp.gain            = rng.range(0.42f, 0.55f);
    pp.sea_level       = has_atmosphere ? rng.range(-0.10f, 0.20f) * pp.elevation_scale
                                        : -pp.elevation_scale;
    return pp;
}

f32 planet_height(const PlanetTerrainParams& pp, const glm::vec3& unit_dir)
{
    f32 n = fbm3(unit_dir * pp.base_frequency, pp.seed, pp.octaves, pp.lacunarity, pp.gain);
    n     = clampf(n, -1.f, 1.f);
    f32 h = n * pp.elevation_scale;
    if (h < pp.sea_level) {
        // Compress relief below sea level into a shallow ocean floor.
        h = pp.sea_level - (pp.sea_level - h) * 0.18f;
    }
    return h;
}

glm::vec3 planet_surface_point(
    const PlanetTerrainParams& pp, const glm::vec3& planet_center, const glm::vec3& unit_dir)
{
    return planet_center + unit_dir * (pp.radius + planet_height(pp, unit_dir));
}

glm::vec3 cube_sphere_dir(u32 face, f32 s, f32 t)
{
    const f32 a = s * 2.f - 1.f;
    const f32 b = t * 2.f - 1.f;

    glm::vec3 p{0.f};
    switch (face % kCubeSphereFaces) {
    case 0: p = glm::vec3{1.f, b, -a}; break;   // +X
    case 1: p = glm::vec3{-1.f, b, a}; break;   // -X
    case 2: p = glm::vec3{a, 1.f, -b}; break;   // +Y
    case 3: p = glm::vec3{a, -1.f, b}; break;   // -Y
    case 4: p = glm::vec3{a, b, 1.f}; break;    // +Z
    default: p = glm::vec3{-a, b, -1.f}; break; // -Z
    }

    // Cobe / Rideout "spherify" — lower area distortion than plain normalize.
    const f32 x2 = p.x * p.x;
    const f32 y2 = p.y * p.y;
    const f32 z2 = p.z * p.z;
    const glm::vec3 sp{
        p.x * std::sqrt(std::max(0.f, 1.f - y2 * 0.5f - z2 * 0.5f + y2 * z2 / 3.f)),
        p.y * std::sqrt(std::max(0.f, 1.f - z2 * 0.5f - x2 * 0.5f + z2 * x2 / 3.f)),
        p.z * std::sqrt(std::max(0.f, 1.f - x2 * 0.5f - y2 * 0.5f + x2 * y2 / 3.f))};

    const f32 len = std::sqrt(sp.x * sp.x + sp.y * sp.y + sp.z * sp.z);
    return (len > 1e-8f) ? (sp / len) : glm::vec3{0.f, 1.f, 0.f};
}

bool build_planet_patch(
    const PlanetTerrainParams& pp,
    const glm::vec3&           planet_center,
    const TerrainPatchSpec&    spec,
    assets::MeshVertex*        out_verts,
    u32&                       out_vertex_count,
    u32*                       out_indices,
    u32&                       out_index_count)
{
    out_vertex_count = 0;
    out_index_count  = 0;
    if (out_verts == nullptr || out_indices == nullptr) {
        return false;
    }
    if (spec.resolution == 0 || spec.resolution > kMaxPatchResolution) {
        return false;
    }

    const u32 n    = spec.resolution;
    const u32 side = n + 1u;
    const f32 du   = (spec.u1 - spec.u0) / static_cast<f32>(n);
    const f32 dv   = (spec.v1 - spec.v0) / static_cast<f32>(n);
    const f32 eps_s = du * 0.25f;
    const f32 eps_t = dv * 0.25f;

    for (u32 j = 0; j < side; ++j) {
        const f32 vv = spec.v0 + dv * static_cast<f32>(j);
        for (u32 i = 0; i < side; ++i) {
            const f32       uu  = spec.u0 + du * static_cast<f32>(i);
            const glm::vec3 dir = cube_sphere_dir(spec.face, uu, vv);
            const glm::vec3 p   = planet_surface_point(pp, planet_center, dir);

            // Normal from tangent finite differences on the displaced surface.
            const glm::vec3 p_s =
                planet_surface_point(pp, planet_center, cube_sphere_dir(spec.face, uu + eps_s, vv));
            const glm::vec3 p_t =
                planet_surface_point(pp, planet_center, cube_sphere_dir(spec.face, uu, vv + eps_t));
            glm::vec3 nrm = glm::cross(p_s - p, p_t - p);
            const f32 nl  = std::sqrt(nrm.x * nrm.x + nrm.y * nrm.y + nrm.z * nrm.z);
            nrm           = (nl > 1e-8f) ? (nrm / nl) : dir;
            if (nrm.x * dir.x + nrm.y * dir.y + nrm.z * dir.z < 0.f) {
                nrm = -nrm;  // keep it outward-facing
            }

            const glm::vec3 col = height_ramp(planet_height(pp, dir), pp);

            assets::MeshVertex& mv = out_verts[j * side + i];
            mv.pos[0]    = p.x;   mv.pos[1]    = p.y;   mv.pos[2]    = p.z;
            mv.normal[0] = nrm.x; mv.normal[1] = nrm.y; mv.normal[2] = nrm.z;
            mv.color[0]  = col.x; mv.color[1]  = col.y; mv.color[2]  = col.z;
            mv.uv[0]     = uu;    mv.uv[1]     = vv;
        }
    }

    u32 k = 0;
    for (u32 j = 0; j < n; ++j) {
        for (u32 i = 0; i < n; ++i) {
            const u32 a = j * side + i;
            const u32 b = j * side + i + 1u;
            const u32 c = (j + 1u) * side + i;
            const u32 d = (j + 1u) * side + i + 1u;
            out_indices[k++] = a; out_indices[k++] = c; out_indices[k++] = b;
            out_indices[k++] = b; out_indices[k++] = c; out_indices[k++] = d;
        }
    }

    out_vertex_count = side * side;
    out_index_count  = n * n * 6u;
    return true;
}

bool planet_terrain_smoke_test()
{
    static const u64 kSeeds[] = {7ull, 101ull, 0xABCDEF01ull, 555ull};

    static assets::MeshVertex vbuf_a[kMaxPatchVertices];
    static assets::MeshVertex vbuf_b[kMaxPatchVertices];
    static u32                ibuf_a[kMaxPatchIndices];
    static u32                ibuf_b[kMaxPatchIndices];

    bool ok = true;

    for (u64 seed : kSeeds) {
        const PlanetTerrainParams pp = planet_terrain_params(seed, 220.f, true);

        // Height stays within the declared elevation scale everywhere sampled.
        f32 hmax = 0.f;
        for (u32 f = 0; f < kCubeSphereFaces; ++f) {
            for (u32 gi = 0; gi < 25; ++gi) {
                const glm::vec3 d =
                    cube_sphere_dir(f, static_cast<f32>(gi % 5) * 0.25f,
                                    static_cast<f32>(gi / 5) * 0.25f);
                hmax = std::max(hmax, std::fabs(planet_height(pp, d)));
            }
        }
        if (hmax > pp.elevation_scale * 1.0005f) {
            log::log_error(
                log::LogCategory::Core,
                "planetgen: seed %llu height %.2f exceeds elevation_scale %.2f",
                static_cast<unsigned long long>(seed),
                static_cast<double>(hmax), static_cast<double>(pp.elevation_scale));
            ok = false;
        }

        // Patch build + byte-identical regeneration.
        TerrainPatchSpec sp{};
        sp.face = 2; sp.u0 = 0.f; sp.u1 = 0.5f; sp.v0 = 0.f; sp.v1 = 1.f; sp.resolution = 24;
        u32 vca = 0, ica = 0, vcb = 0, icb = 0;
        ok = ok && build_planet_patch(pp, glm::vec3{0.f}, sp, vbuf_a, vca, ibuf_a, ica);
        ok = ok && build_planet_patch(pp, glm::vec3{0.f}, sp, vbuf_b, vcb, ibuf_b, icb);
        ok = ok && vca == vcb && ica == icb && vca > 0;
        ok = ok && std::memcmp(vbuf_a, vbuf_b, vca * sizeof(assets::MeshVertex)) == 0;
        ok = ok && std::memcmp(ibuf_a, ibuf_b, ica * sizeof(u32)) == 0;

        // Normals are unit length.
        for (u32 i = 0; i < vca && ok; ++i) {
            const assets::MeshVertex& mv = vbuf_a[i];
            const f32 nl = std::sqrt(mv.normal[0] * mv.normal[0] + mv.normal[1] * mv.normal[1]
                                     + mv.normal[2] * mv.normal[2]);
            if (std::fabs(nl - 1.f) > 2e-3f) {
                log::log_error(
                    log::LogCategory::Core, "planetgen: seed %llu non-unit normal (%.4f)",
                    static_cast<unsigned long long>(seed), static_cast<double>(nl));
                ok = false;
            }
        }

        // Seam: an adjacent patch on the same face must agree exactly on the
        // shared edge (both build from identical directions there).
        TerrainPatchSpec sp2 = sp;
        sp2.u0 = 0.5f;
        sp2.u1 = 1.0f;
        (void)build_planet_patch(pp, glm::vec3{0.f}, sp2, vbuf_b, vcb, ibuf_b, icb);
        const u32 side = sp.resolution + 1u;
        for (u32 j = 0; j < side && ok; ++j) {
            const assets::MeshVertex& av = vbuf_a[j * side + (side - 1u)];  // A right column
            const assets::MeshVertex& bv = vbuf_b[j * side + 0u];           // B left column
            const f32 dd = std::fabs(av.pos[0] - bv.pos[0]) + std::fabs(av.pos[1] - bv.pos[1])
                           + std::fabs(av.pos[2] - bv.pos[2]);
            if (dd > 1e-3f) {
                log::log_error(
                    log::LogCategory::Core, "planetgen: seed %llu patch seam mismatch (%.4f)",
                    static_cast<unsigned long long>(seed), static_cast<double>(dd));
                ok = false;
            }
        }
    }

    log::log_info(log::LogCategory::Core, "CSC_PLANETGEN_SMOKE: %s", ok ? "PASS" : "FAIL");
    return ok;
}

}  // namespace csc::game::world
