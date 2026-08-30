#include "game/world/poi_catalog.hpp"

#include "engine/ecs/world.hpp"
#include "engine/log/log.hpp"
#include "game/economy/location_catalog.hpp"
#include "game/world/galaxy.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace csc::game::world {
namespace {

inline constexpr f32 kDeg2Rad = 0.01745329251994329577f;

char* trim_inplace(char* s)
{
    while (*s != '\0' && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) {
        ++s;
    }
    if (*s == '\0') {
        return s;
    }
    char* end = s + std::strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        --end;
    }
    *end = '\0';
    return s;
}

void copy_str(char* dst, std::size_t cap, const char* src)
{
    if (dst != nullptr && cap != 0) {
        std::snprintf(dst, cap, "%s", (src != nullptr) ? src : "");
    }
}

[[nodiscard]] PoiKind parse_kind(const char* v)
{
    if (std::strcmp(v, "station") == 0) return PoiKind::Station;
    if (std::strcmp(v, "beacon") == 0) return PoiKind::Beacon;
    if (std::strcmp(v, "wreck") == 0) return PoiKind::Wreck;
    return PoiKind::Outpost;
}

[[nodiscard]] PoiPlacement parse_placement(const char* v)
{
    if (std::strcmp(v, "planet_surface") == 0) return PoiPlacement::PlanetSurface;
    if (std::strcmp(v, "absolute") == 0) return PoiPlacement::Absolute;
    return PoiPlacement::Orbit;
}

[[nodiscard]] bool parse_vec3(const char* value, glm::vec3& out)
{
    f32 v[3]{};
    u32 n = 0;
    const char* p = value;
    while (n < 3 && *p != '\0') {
        char* e = nullptr;
        const f32 f = std::strtof(p, &e);
        if (e == p) {
            break;
        }
        v[n++] = f;
        p = e;
        while (*p == ' ' || *p == '\t' || *p == ',') {
            ++p;
        }
    }
    if (n != 3) {
        return false;
    }
    out = glm::vec3{v[0], v[1], v[2]};
    return true;
}

bool apply_poi_kv(PoiDef& d, const char* key, const char* value)
{
    if (std::strcmp(key, "id") == 0)   { copy_str(d.id, sizeof(d.id), value); return true; }
    if (std::strcmp(key, "name") == 0) { copy_str(d.name, sizeof(d.name), value); return true; }
    if (std::strcmp(key, "kind") == 0) { d.kind = parse_kind(value); return true; }
    if (std::strcmp(key, "placement") == 0) { d.placement = parse_placement(value); return true; }
    if (std::strcmp(key, "system") == 0) {
        d.system_seed = (std::strcmp(value, "home") == 0)
                            ? 0ull
                            : std::strtoull(value, nullptr, 0);
        return true;
    }
    if (std::strcmp(key, "orbit_radius") == 0)    { d.orbit_radius = std::strtof(value, nullptr); return true; }
    if (std::strcmp(key, "orbit_angle_deg") == 0) { d.orbit_angle_deg = std::strtof(value, nullptr); return true; }
    if (std::strcmp(key, "orbit_y") == 0)         { d.orbit_y = std::strtof(value, nullptr); return true; }
    if (std::strcmp(key, "planet_index") == 0)    { d.planet_index = static_cast<u32>(std::strtoul(value, nullptr, 10)); return true; }
    if (std::strcmp(key, "lat_deg") == 0)         { d.lat_deg = std::strtof(value, nullptr); return true; }
    if (std::strcmp(key, "lon_deg") == 0)         { d.lon_deg = std::strtof(value, nullptr); return true; }
    if (std::strcmp(key, "altitude") == 0)        { d.altitude = std::strtof(value, nullptr); return true; }
    if (std::strcmp(key, "pos") == 0)             { return parse_vec3(value, d.pos); }
    if (std::strcmp(key, "marker_radius") == 0)   { d.marker_radius = std::strtof(value, nullptr); return true; }
    if (std::strcmp(key, "location_id") == 0)     { copy_str(d.location_id, sizeof(d.location_id), value); return true; }
    return false;
}

[[nodiscard]] glm::vec3 latlon_dir(f32 lat_deg, f32 lon_deg)
{
    const f32 la = lat_deg * kDeg2Rad;
    const f32 lo = lon_deg * kDeg2Rad;
    return glm::vec3{std::cos(la) * std::cos(lo), std::sin(la), std::cos(la) * std::sin(lo)};
}

[[nodiscard]] u64 effective_seed(u64 def_seed)
{
    return (def_seed == 0ull) ? kHomeSystemSeed : def_seed;
}

/// World position of a POI given its system's body table.
[[nodiscard]] glm::vec3 resolve_poi_pos(const PoiDef& d, const StarSystemData& data)
{
    switch (d.placement) {
    case PoiPlacement::Absolute:
        return d.pos;
    case PoiPlacement::PlanetSurface: {
        u32 seen = 0;
        for (u32 i = 0; i < data.body_count; ++i) {
            if (data.bodies[i].type != CelestialBodyType::Planet) {
                continue;
            }
            if (seen == d.planet_index) {
                const CelestialBody& pl = data.bodies[i];
                const glm::vec3 dir = latlon_dir(d.lat_deg, d.lon_deg);
                // Mean-sphere surface + altitude. Exact terrain-height snapping
                // (P4-02 planet_height) is a P4-08 concern once terrain renders.
                return pl.position + dir * (pl.radius + d.altitude);
            }
            ++seen;
        }
        return glm::vec3{0.f};  // no such planet — falls back to origin
    }
    case PoiPlacement::Orbit:
    default: {
        const f32 a = d.orbit_angle_deg * kDeg2Rad;
        return glm::vec3{d.orbit_radius * std::cos(a), d.orbit_y, d.orbit_radius * std::sin(a)};
    }
    }
}

}  // namespace

bool load_poi_catalog_file(PoiCatalog& out, const char* path)
{
    out = PoiCatalog{};
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        log::log_warn(log::LogCategory::Config, "poi catalog missing: %s", path);
        return false;
    }

    bool   ok    = true;
    bool   in_poi = false;
    PoiDef cur{};

    auto flush = [&]() {
        if (!in_poi) {
            return;
        }
        in_poi = false;
        if (cur.id[0] == '\0') {
            log::log_error(log::LogCategory::Config, "%s: [[poi]] without an id", path);
            ok = false;
            return;
        }
        for (u32 i = 0; i < out.count; ++i) {
            if (std::strcmp(out.items[i].id, cur.id) == 0) {
                log::log_error(log::LogCategory::Config, "%s: duplicate poi id '%s'", path, cur.id);
                ok = false;
                return;
            }
        }
        if (out.count >= kMaxPoiDefs) {
            log::log_error(log::LogCategory::Config, "%s: more than %u pois", path, kMaxPoiDefs);
            ok = false;
            return;
        }
        if (cur.name[0] == '\0') {
            copy_str(cur.name, sizeof(cur.name), cur.id);
        }
        out.items[out.count++] = cur;
    };

    char line[512];
    int  line_no = 0;
    while (std::fgets(line, static_cast<int>(sizeof(line)), file) != nullptr) {
        ++line_no;
        char* t = trim_inplace(line);
        if (t[0] == '\0' || t[0] == '#' || t[0] == ';') {
            continue;
        }
        if (std::strcmp(t, "[[poi]]") == 0) {
            flush();
            cur    = PoiDef{};
            in_poi = true;
            continue;
        }
        if (!in_poi) {
            continue;
        }
        char* eq = std::strchr(t, '=');
        if (eq == nullptr) {
            continue;
        }
        *eq = '\0';
        char* k = trim_inplace(t);
        char* v = trim_inplace(eq + 1);
        if (!apply_poi_kv(cur, k, v)) {
            log::log_warn(log::LogCategory::Config, "%s:%d: bad poi key '%s'", path, line_no, k);
        }
    }
    flush();
    std::fclose(file);

    if (ok) {
        log::log_info(log::LogCategory::Config, "Loaded %u POIs from %s", out.count, path);
    } else {
        log::log_error(log::LogCategory::Config, "%s: POI catalog INVALID", path);
    }
    return ok;
}

bool load_poi_catalog(flecs::world& world)
{
    PoiCatalog cat{};
    const bool ok = load_poi_catalog_file(cat, kPoiCatalogPath);
    world.set<PoiCatalog>(cat);
    return ok;
}

void spawn_pois_for_system(flecs::world& world, u64 system_seed, const StarSystemData& data)
{
    const PoiCatalog* cat = world.try_get<PoiCatalog>();
    if (cat == nullptr) {
        return;
    }
    const economy::LocationCatalog* loc = world.try_get<economy::LocationCatalog>();

    u32 spawned = 0;
    for (u32 i = 0; i < cat->count; ++i) {
        const PoiDef& d = cat->items[i];
        if (effective_seed(d.system_seed) != system_seed) {
            continue;
        }
        const glm::vec3 p = resolve_poi_pos(d, data);

        world.entity(d.id)
            .set<ecs::Position>({p.x, p.y, p.z})
            .set<ecs::PreviousPosition>({p.x, p.y, p.z})
            .set<ecs::Velocity>({0.f, 0.f, 0.f})
            .set<ecs::Scale>({d.marker_radius})
            .add<ecs::InstanceTag>();

        // P3-04 bridge: a POI that names a locations.cfg entry also gets its NPCs.
        if (d.location_id[0] != '\0' && loc != nullptr
            && economy::find_location_def(*loc, d.location_id) != nullptr) {
            const economy::LocationPlacement pl{d.location_id, p};
            economy::populate_locations(world, &pl, 1u);
        }
        ++spawned;
    }
    if (spawned > 0) {
        log::log_info(log::LogCategory::Game, "Spawned %u curated POI(s)", spawned);
    }
}

bool poi_smoke_test()
{
    PoiCatalog cat{};
    const bool loaded = load_poi_catalog_file(cat, kPoiCatalogPath);

    bool ok = loaded && cat.count > 0;
    if (!loaded) {
        log::log_error(log::LogCategory::Game, "poi: catalog failed to load / invalid");
    }

    for (u32 i = 0; i < cat.count; ++i) {
        for (u32 j = i + 1; j < cat.count; ++j) {
            if (std::strcmp(cat.items[i].id, cat.items[j].id) == 0) {
                log::log_error(log::LogCategory::Game, "poi: duplicate id '%s'", cat.items[i].id);
                ok = false;
            }
        }
        if (cat.items[i].placement == PoiPlacement::PlanetSurface
            && cat.items[i].planet_index > 8u) {
            log::log_error(
                log::LogCategory::Game, "poi '%s': implausible planet_index %u",
                cat.items[i].id, cat.items[i].planet_index);
            ok = false;
        }
    }

    // Placement resolution is deterministic for a fixed body table.
    StarSystemData data{};
    star_system_set_placeholders(data);
    for (u32 i = 0; i < cat.count; ++i) {
        const glm::vec3 a = resolve_poi_pos(cat.items[i], data);
        const glm::vec3 b = resolve_poi_pos(cat.items[i], data);
        if (a != b) {
            log::log_error(log::LogCategory::Game, "poi '%s': non-deterministic placement",
                           cat.items[i].id);
            ok = false;
        }
    }

    log::log_info(log::LogCategory::Game, "CSC_POI_SMOKE: %s", ok ? "PASS" : "FAIL");
    return ok;
}

}  // namespace csc::game::world
