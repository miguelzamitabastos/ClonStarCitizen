#include "game/world/star_system_gen.hpp"

#include "engine/log/log.hpp"
#include "game/world/rng64.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace csc::game::world {
namespace {

inline constexpr f32 kTwoPi = 6.28318530717958647692f;

void set_name(char* dst, std::size_t cap, const char* fmt, ...)
{
    if (dst == nullptr || cap == 0) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(dst, cap, fmt, ap);
    va_end(ap);
}

}  // namespace

void generated_system_name(u64 seed, char* out, std::size_t cap)
{
    set_name(out, cap, "Sys-%08X", static_cast<unsigned>(seed & 0xFFFFFFFFull));
}

u32 generate_star_system(u64 seed, StarSystemData& out, GeneratedSystemInfo* info)
{
    out = StarSystemData{};
    if (info != nullptr) {
        *info      = GeneratedSystemInfo{};
        info->seed = seed;
    }

    Rng64 rng(seed);

    char sysname[kNameBytes];
    generated_system_name(seed, sysname, sizeof(sysname));
    std::snprintf(out.system_name, sizeof(out.system_name), "%s", sysname);

    auto push_body = [&](const char* name, CelestialBodyType type, const glm::vec3& pos,
                         f32 radius, const GeneratedBodyInfo& bi) -> bool {
        if (out.body_count >= kMaxCelestialBodies) {
            return false;
        }
        CelestialBody& b = out.bodies[out.body_count];
        std::snprintf(b.name, sizeof(b.name), "%s", name);
        b.type     = type;
        b.position = pos;
        b.radius   = radius;
        if (info != nullptr) {
            info->bodies[out.body_count] = bi;
        }
        ++out.body_count;
        if (info != nullptr) {
            info->body_count = out.body_count;
        }
        return true;
    };

    // --- Star: system origin. --------------------------------------------------
    {
        char name[kNameBytes];
        set_name(name, sizeof(name), "%s-Star", sysname);
        (void)push_body(
            name, CelestialBodyType::Star, glm::vec3{0.f}, rng.range(kStarRadiusMin, kStarRadiusMax),
            GeneratedBodyInfo{});
    }

    // --- Planets on geometric orbits. ---------------------------------------
    const u32 planet_count = rng.range_u32(kMinPlanets, kMaxPlanets);
    f32       orbit        = kFirstOrbitRadius
                     * rng.range(kFirstOrbitJitterMin, kFirstOrbitJitterMax);

    for (u32 p = 0; p < planet_count; ++p) {
        const f32 angle    = rng.next_unit() * kTwoPi;
        const f32 y_jitter = rng.range(-orbit * 0.05f, orbit * 0.05f);
        const glm::vec3 planet_pos{
            orbit * std::cos(angle), y_jitter, orbit * std::sin(angle)};
        const f32 planet_r = rng.range(kPlanetRadiusMin, kPlanetRadiusMax);

        GeneratedBodyInfo bi{};
        bi.orbit_radius     = orbit;
        bi.orbit_angle_rad  = angle;
        bi.has_atmosphere   = rng.next_unit() < 0.60f;
        bi.has_landing_zone = rng.next_unit() < 0.50f;
        bi.body_seed        = rng.next_u64();

        char pname[kNameBytes];
        set_name(pname, sizeof(pname), "%s-P%u", sysname, p + 1u);
        if (!push_body(
                pname, CelestialBodyType::Planet, planet_pos, planet_r, bi)) {
            break;
        }

        if (bi.has_landing_zone) {
            // LZ sits just off the planet on the sunward side (deterministic).
            const f32 len = std::sqrt(
                planet_pos.x * planet_pos.x + planet_pos.z * planet_pos.z);
            const glm::vec3 inward =
                (len > 1e-3f) ? glm::vec3{-planet_pos.x / len, 0.f, -planet_pos.z / len}
                              : glm::vec3{1.f, 0.f, 0.f};
            const glm::vec3 lz_pos =
                glm::vec3{planet_pos.x, 0.f, planet_pos.z} + inward * (planet_r * 1.25f);

            GeneratedBodyInfo lz_bi{};
            lz_bi.orbit_radius = orbit;
            lz_bi.body_seed    = bi.body_seed ^ 0xD1B54A32D192ED03ull;

            char lname[kNameBytes];
            set_name(lname, sizeof(lname), "%s-P%u-LZ", sysname, p + 1u);
            if (!push_body(
                    lname, CelestialBodyType::LandingZone, lz_pos, planet_r * 0.30f, lz_bi)) {
                break;
            }
        }

        orbit *= rng.range(kOrbitStepMin, kOrbitStepMax);
    }

    return out.body_count;
}

bool star_system_gen_smoke_test()
{
    static const u64 kSeeds[] = {1ull, 42ull, 1337ull, 0x9E3779B97F4A7C15ull, 999999999ull};

    bool ok = true;

    for (u64 seed : kSeeds) {
        StarSystemData      a{};
        GeneratedSystemInfo ainfo{};
        const u32           n = generate_star_system(seed, a, &ainfo);

        // Body count in range: star + [kMinPlanets, kMaxPlanets] + up to that
        // many landing zones.
        if (n < 1u + kMinPlanets || n > 1u + 2u * kMaxPlanets || n > kMaxCelestialBodies) {
            log::log_error(
                log::LogCategory::Core, "systemgen: seed %llu produced %u bodies (out of range)",
                static_cast<unsigned long long>(seed), n);
            ok = false;
        }
        // Star at index 0.
        if (n == 0 || a.bodies[0].type != CelestialBodyType::Star) {
            log::log_error(
                log::LogCategory::Core, "systemgen: seed %llu has no Star at index 0",
                static_cast<unsigned long long>(seed));
            ok = false;
        }

        // Orbit radii strictly increasing across planets; every LZ follows a
        // planet at the same orbit.
        f32 prev_orbit = -1.f;
        for (u32 i = 1; i < n; ++i) {
            if (a.bodies[i].type == CelestialBodyType::Planet) {
                const f32 r = ainfo.bodies[i].orbit_radius;
                if (r <= prev_orbit) {
                    log::log_error(
                        log::LogCategory::Core,
                        "systemgen: seed %llu orbit not increasing at body %u",
                        static_cast<unsigned long long>(seed), i);
                    ok = false;
                }
                prev_orbit = r;
            } else if (a.bodies[i].type == CelestialBodyType::LandingZone) {
                if (i == 0 || a.bodies[i - 1].type != CelestialBodyType::Planet) {
                    log::log_error(
                        log::LogCategory::Core,
                        "systemgen: seed %llu landing zone at %u not paired to a planet",
                        static_cast<unsigned long long>(seed), i);
                    ok = false;
                }
            }
        }

        // Determinism: regenerate, must be byte-identical.
        StarSystemData b{};
        (void)generate_star_system(seed, b, nullptr);
        if (std::memcmp(&a, &b, sizeof(StarSystemData)) != 0) {
            log::log_error(
                log::LogCategory::Core, "systemgen: seed %llu is NOT deterministic",
                static_cast<unsigned long long>(seed));
            ok = false;
        }
    }

    // Different seeds must give different systems (names at least).
    StarSystemData s1{};
    StarSystemData s2{};
    (void)generate_star_system(1ull, s1, nullptr);
    (void)generate_star_system(2ull, s2, nullptr);
    if (std::strcmp(s1.system_name, s2.system_name) == 0) {
        log::log_error(log::LogCategory::Core, "systemgen: seeds 1 and 2 collide");
        ok = false;
    }

    log::log_info(log::LogCategory::Core, "CSC_SYSTEMGEN_SMOKE: %s", ok ? "PASS" : "FAIL");
    return ok;
}

}  // namespace csc::game::world
