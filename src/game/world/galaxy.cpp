#include "game/world/galaxy.hpp"

#include "engine/log/log.hpp"
#include "game/world/rng64.hpp"
#include "game/world/star_system_gen.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace csc::game::world {
namespace {

void add_link(GalaxySystem& s, u32 other)
{
    for (u32 i = 0; i < s.link_count; ++i) {
        if (s.links[i] == other) {
            return;
        }
    }
    if (s.link_count < kMaxSystemLinks) {
        s.links[s.link_count++] = other;
    }
}

void link_both(GalaxyMap& g, u32 a, u32 b)
{
    if (a == b || a >= g.count || b >= g.count) {
        return;
    }
    add_link(g.systems[a], b);
    add_link(g.systems[b], a);
}

[[nodiscard]] f32 map_dist2(const GalaxyMap& g, u32 a, u32 b)
{
    const glm::vec2 d = g.systems[a].map_pos - g.systems[b].map_pos;
    return d.x * d.x + d.y * d.y;
}

}  // namespace

void generate_galaxy(u64 galaxy_seed, u64 home_system_seed, GalaxyMap& out)
{
    out = GalaxyMap{};
    out.galaxy_seed = galaxy_seed;

    Rng64 rng(galaxy_seed);
    out.count = rng.range_u32(5u, 9u);
    if (out.count > kMaxGalaxySystems) {
        out.count = kMaxGalaxySystems;
    }

    for (u32 i = 0; i < out.count; ++i) {
        GalaxySystem& s = out.systems[i];
        s.seed = (i == 0) ? home_system_seed : rng.next_u64();
        generated_system_name(s.seed, s.name, sizeof(s.name));
        // Poisson-ish scatter on a disc: angle + radius from the rng.
        const f32 ang = rng.next_unit() * 6.2831853f;
        const f32 rad = 20.f + 80.f * std::sqrt(rng.next_unit());
        s.map_pos = glm::vec2{rad * std::cos(ang), rad * std::sin(ang)};
    }

    // Link each system to its 2 nearest neighbours.
    for (u32 i = 0; i < out.count; ++i) {
        u32 best[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};
        f32 bd[2]   = {1e30f, 1e30f};
        for (u32 j = 0; j < out.count; ++j) {
            if (j == i) {
                continue;
            }
            const f32 d = map_dist2(out, i, j);
            if (d < bd[0]) {
                bd[1]   = bd[0];
                best[1] = best[0];
                bd[0]   = d;
                best[0] = j;
            } else if (d < bd[1]) {
                bd[1]   = d;
                best[1] = j;
            }
        }
        if (best[0] != 0xFFFFFFFFu) {
            link_both(out, i, best[0]);
        }
        if (best[1] != 0xFFFFFFFFu) {
            link_both(out, i, best[1]);
        }
    }

    // Guarantee connectivity: BFS from 0; for any unreached node, link it to the
    // nearest reached node.
    for (;;) {
        bool reached[kMaxGalaxySystems] = {};
        u32  queue[kMaxGalaxySystems];
        u32  qh = 0, qt = 0;
        reached[0]   = true;
        queue[qt++]  = 0;
        while (qh < qt) {
            const GalaxySystem& s = out.systems[queue[qh++]];
            for (u32 k = 0; k < s.link_count; ++k) {
                if (!reached[s.links[k]]) {
                    reached[s.links[k]] = true;
                    queue[qt++]         = s.links[k];
                }
            }
        }
        u32 orphan = 0xFFFFFFFFu;
        for (u32 i = 0; i < out.count; ++i) {
            if (!reached[i]) {
                orphan = i;
                break;
            }
        }
        if (orphan == 0xFFFFFFFFu) {
            break;
        }
        u32 nearest = 0xFFFFFFFFu;
        f32 nd      = 1e30f;
        for (u32 i = 0; i < out.count; ++i) {
            if (reached[i]) {
                const f32 d = map_dist2(out, orphan, i);
                if (d < nd) {
                    nd      = d;
                    nearest = i;
                }
            }
        }
        link_both(out, orphan, (nearest == 0xFFFFFFFFu) ? 0u : nearest);
    }

    out.current = 0;
}

bool galaxy_linked(const GalaxyMap& g, u32 a, u32 b)
{
    if (a >= g.count) {
        return false;
    }
    for (u32 i = 0; i < g.systems[a].link_count; ++i) {
        if (g.systems[a].links[i] == b) {
            return true;
        }
    }
    return false;
}

bool galaxy_jump(GalaxyMap& g, u32 target)
{
    if (target >= g.count || target == g.current || !galaxy_linked(g, g.current, target)) {
        return false;
    }
    g.current = target;
    return true;
}

u64 galaxy_current_seed(const GalaxyMap& g)
{
    return (g.current < g.count) ? g.systems[g.current].seed : 0ull;
}

void galaxy_log(const GalaxyMap& g)
{
    log::log_info(
        log::LogCategory::Game, "Galaxy (seed %llu): %u systems, current=%u (%s)",
        static_cast<unsigned long long>(g.galaxy_seed), g.count, g.current,
        (g.current < g.count) ? g.systems[g.current].name : "?");
    for (u32 i = 0; i < g.count; ++i) {
        const GalaxySystem& s = g.systems[i];
        char links[64];
        int  off = 0;
        for (u32 k = 0; k < s.link_count && off < 56; ++k) {
            off += std::snprintf(links + off, sizeof(links) - static_cast<std::size_t>(off),
                                 "%s%u", (k == 0) ? "" : ",", s.links[k]);
        }
        log::log_info(
            log::LogCategory::Game, "  [%u] %-14s at (%.0f, %.0f)  -> %s%s",
            i, s.name, static_cast<double>(s.map_pos.x), static_cast<double>(s.map_pos.y),
            links, (i == g.current) ? "   <== here" : "");
    }
}

bool galaxy_smoke_test()
{
    GalaxyMap a{};
    GalaxyMap b{};
    generate_galaxy(kDefaultGalaxySeed, kHomeSystemSeed, a);
    generate_galaxy(kDefaultGalaxySeed, kHomeSystemSeed, b);

    bool ok = true;

    if (a.count < 5 || a.count > kMaxGalaxySystems) {
        log::log_error(log::LogCategory::Game, "galaxy: bad system count %u", a.count);
        ok = false;
    }
    if (std::memcmp(&a, &b, sizeof(GalaxyMap)) != 0) {
        log::log_error(log::LogCategory::Game, "galaxy: generation is not deterministic");
        ok = false;
    }
    if (a.systems[0].seed != kHomeSystemSeed) {
        log::log_error(log::LogCategory::Game, "galaxy: node 0 is not the home system");
        ok = false;
    }

    // Links symmetric.
    for (u32 i = 0; i < a.count; ++i) {
        for (u32 k = 0; k < a.systems[i].link_count; ++k) {
            if (!galaxy_linked(a, a.systems[i].links[k], i)) {
                log::log_error(log::LogCategory::Game, "galaxy: link %u->%u not symmetric",
                               i, a.systems[i].links[k]);
                ok = false;
            }
        }
    }

    // Connected (BFS from 0).
    bool seen[kMaxGalaxySystems] = {};
    u32  q[kMaxGalaxySystems];
    u32  h = 0, t = 0;
    seen[0] = true;
    q[t++]  = 0;
    while (h < t) {
        const GalaxySystem& s = a.systems[q[h++]];
        for (u32 k = 0; k < s.link_count; ++k) {
            if (!seen[s.links[k]]) {
                seen[s.links[k]] = true;
                q[t++]           = s.links[k];
            }
        }
    }
    for (u32 i = 0; i < a.count; ++i) {
        if (!seen[i]) {
            log::log_error(log::LogCategory::Game, "galaxy: node %u unreachable from 0", i);
            ok = false;
        }
    }

    // Jump rules: reject non-adjacent, accept adjacent, round-trip.
    GalaxyMap g = a;
    if (galaxy_jump(g, g.current)) {
        log::log_error(log::LogCategory::Game, "galaxy: jump to self accepted");
        ok = false;
    }
    const u32 nb = (a.systems[0].link_count > 0) ? a.systems[0].links[0] : 0u;
    if (a.systems[0].link_count > 0) {
        if (!galaxy_jump(g, nb) || g.current != nb) {
            log::log_error(log::LogCategory::Game, "galaxy: jump to a neighbour rejected");
            ok = false;
        }
        if (!galaxy_jump(g, 0u) || g.current != 0u) {
            log::log_error(log::LogCategory::Game, "galaxy: return jump rejected");
            ok = false;
        }
    }
    // A definitely-non-adjacent target (find one not linked to current, != current).
    for (u32 i = 0; i < a.count; ++i) {
        if (i != g.current && !galaxy_linked(g, g.current, i)) {
            if (galaxy_jump(g, i)) {
                log::log_error(log::LogCategory::Game, "galaxy: jump to non-adjacent %u accepted", i);
                ok = false;
            }
            break;
        }
    }

    log::log_info(log::LogCategory::Game, "CSC_GALAXY_SMOKE: %s", ok ? "PASS" : "FAIL");
    return ok;
}

}  // namespace csc::game::world
