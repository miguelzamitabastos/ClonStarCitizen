#include "game/economy/location_catalog.hpp"

#include "engine/ecs/world.hpp"
#include "engine/log/log.hpp"
#include "game/character/character.hpp"
#include "game/economy/economy.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace csc::game::economy {
namespace {

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

void copy_str(char* dest, std::size_t dest_bytes, const char* src)
{
    if (dest == nullptr || dest_bytes == 0) {
        return;
    }
    std::snprintf(dest, dest_bytes, "%s", (src != nullptr) ? src : "");
}

[[nodiscard]] bool parse_bool(const char* v)
{
    return std::strcmp(v, "1") == 0 || std::strcmp(v, "true") == 0
           || std::strcmp(v, "yes") == 0 || std::strcmp(v, "on") == 0;
}

[[nodiscard]] bool parse_vec3(const char* value, glm::vec3& out)
{
    f32         v[3]{};
    u32         n = 0;
    const char* p = value;
    while (n < 3 && *p != '\0') {
        char*     endp = nullptr;
        const f32 f    = std::strtof(p, &endp);
        if (endp == p) {
            break;
        }
        v[n++] = f;
        p      = endp;
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

/// key "npc.N.field" → index N + field name. false if it is not that shape.
[[nodiscard]] bool parse_npc_key(const char* key, u32& out_index, char* field_out, std::size_t cap)
{
    if (std::strncmp(key, "npc.", 4) != 0) {
        return false;
    }
    const char* p   = key + 4;
    char*       end = nullptr;
    const long  idx = std::strtol(p, &end, 10);
    if (end == p || idx < 0 || *end != '.') {
        return false;
    }
    out_index = static_cast<u32>(idx);
    copy_str(field_out, cap, end + 1);
    return field_out[0] != '\0';
}

[[nodiscard]] LocationNpcKind parse_npc_kind(const char* v, bool& ok)
{
    ok = true;
    if (std::strcmp(v, "trader") == 0) {
        return LocationNpcKind::Trader;
    }
    if (std::strcmp(v, "mission_giver") == 0) {
        return LocationNpcKind::MissionGiver;
    }
    if (std::strcmp(v, "turn_in") == 0) {
        return LocationNpcKind::TurnIn;
    }
    if (std::strcmp(v, "travel_pad") == 0) {
        return LocationNpcKind::TravelPad;
    }
    ok = false;
    return LocationNpcKind::Trader;
}

/// Apply a top-level (non-npc) key to `def`. Returns false for an unknown key.
bool apply_location_kv(LocationDef& def, const char* key, const char* value)
{
    if (std::strcmp(key, "id") == 0) {
        copy_str(def.id, sizeof(def.id), value);
        return true;
    }
    if (std::strcmp(key, "name") == 0) {
        copy_str(def.name, sizeof(def.name), value);
        return true;
    }
    if (std::strcmp(key, "location_id") == 0) {
        def.location_id = static_cast<u32>(std::strtoul(value, nullptr, 10));
        return true;
    }
    if (std::strcmp(key, "gravity_deck") == 0) {
        def.gravity_deck = parse_bool(value);
        return true;
    }
    return false;
}

/// Apply an "npc.N.field" key. Returns false for an unknown field.
bool apply_npc_kv(LocationNpc& npc, const char* field, const char* value, const char* where)
{
    if (std::strcmp(field, "kind") == 0) {
        bool ok        = false;
        npc.kind       = parse_npc_kind(value, ok);
        if (!ok) {
            log::log_warn(log::LogCategory::Config, "%s: unknown npc kind '%s'", where, value);
        }
        return true;
    }
    if (std::strcmp(field, "offset") == 0) {
        return parse_vec3(value, npc.offset);
    }
    if (std::strcmp(field, "label") == 0) {
        copy_str(npc.label, sizeof(npc.label), value);
        return true;
    }
    if (std::strcmp(field, "market") == 0) {
        npc.market_id = static_cast<u32>(std::strtoul(value, nullptr, 10));
        return true;
    }
    if (std::strcmp(field, "commodity") == 0) {
        npc.commodity_id = static_cast<u32>(std::strtoul(value, nullptr, 10));
        return true;
    }
    if (std::strcmp(field, "buy") == 0) {
        npc.allow_buy = parse_bool(value);
        return true;
    }
    if (std::strcmp(field, "sell") == 0) {
        npc.allow_sell = parse_bool(value);
        return true;
    }
    if (std::strcmp(field, "template") == 0) {
        npc.template_id = static_cast<u32>(std::strtoul(value, nullptr, 10));
        return true;
    }
    if (std::strcmp(field, "mission_market") == 0) {
        npc.mission_market_id = static_cast<u32>(std::strtoul(value, nullptr, 10));
        return true;
    }
    if (std::strcmp(field, "dest") == 0) {
        copy_str(npc.dest_location, sizeof(npc.dest_location), value);
        return true;
    }
    return false;
}

[[nodiscard]] const LocationPlacement* find_placement(
    const LocationPlacement* placements, u32 count, const char* id)
{
    if (id == nullptr || id[0] == '\0') {
        return nullptr;
    }
    for (u32 i = 0; i < count; ++i) {
        if (placements[i].location_id != nullptr
            && std::strcmp(placements[i].location_id, id) == 0) {
            return &placements[i];
        }
    }
    return nullptr;
}

void spawn_deck(flecs::world& world, const LocationDef& def, const glm::vec3& origin)
{
    char name[64];
    std::snprintf(name, sizeof(name), "%sGravity", def.id);
    (void)character::spawn_gravity_zone_box(
        world,
        origin + glm::vec3{0.f, 2.f, 0.f},
        glm::vec3{16.f, 6.f, 16.f},
        glm::vec3{0.f, -1.f, 0.f},
        character::kGravityDefault,
        name);

    std::snprintf(name, sizeof(name), "%sDeck", def.id);
    const ecs::Position pos{origin.x, origin.y + 0.4f, origin.z};
    world.entity(name)
        .set<ecs::Position>(pos)
        .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
        .set<ecs::Velocity>({0.f, 0.f, 0.f})
        .set<ecs::Scale>({8.f})
        .add<ecs::InstanceTag>();
}

}  // namespace

bool load_location_catalog_file(LocationCatalog& out, const char* path)
{
    out = LocationCatalog{};

    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        log::log_error(log::LogCategory::Config, "location catalog missing: %s", path);
        return false;
    }

    bool        ok       = true;
    bool        in_loc   = false;
    LocationDef current{};

    auto flush = [&]() {
        if (!in_loc) {
            return;
        }
        in_loc = false;
        if (current.id[0] == '\0') {
            log::log_error(log::LogCategory::Config, "%s: [[location]] without an id", path);
            ok = false;
            return;
        }
        for (u32 i = 0; i < out.count; ++i) {
            if (std::strcmp(out.items[i].id, current.id) == 0) {
                log::log_error(
                    log::LogCategory::Config, "%s: duplicate location id '%s'", path, current.id);
                ok = false;
                return;
            }
        }
        if (out.count >= kMaxLocationDefs) {
            log::log_error(
                log::LogCategory::Config,
                "%s: more than %u locations (id '%s' dropped)",
                path,
                kMaxLocationDefs,
                current.id);
            ok = false;
            return;
        }
        if (current.name[0] == '\0') {
            copy_str(current.name, sizeof(current.name), current.id);
        }
        out.items[out.count++] = current;
    };

    char line[512];
    int  line_no = 0;
    while (std::fgets(line, static_cast<int>(sizeof(line)), file) != nullptr) {
        ++line_no;
        char* trimmed = trim_inplace(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        if (std::strcmp(trimmed, "[[location]]") == 0) {
            flush();
            current = LocationDef{};
            in_loc  = true;
            continue;
        }
        if (!in_loc) {
            log::log_warn(
                log::LogCategory::Config, "%s:%d: key outside a [[location]] block", path, line_no);
            continue;
        }
        char* eq = std::strchr(trimmed, '=');
        if (eq == nullptr) {
            log::log_warn(log::LogCategory::Config, "%s:%d: missing '='", path, line_no);
            continue;
        }
        *eq         = '\0';
        char* key   = trim_inplace(trimmed);
        char* value = trim_inplace(eq + 1);

        u32  npc_idx = 0;
        char field[32]{};
        if (parse_npc_key(key, npc_idx, field, sizeof(field))) {
            if (npc_idx >= kMaxLocationNpcs) {
                log::log_warn(
                    log::LogCategory::Config,
                    "%s:%d: npc index %u exceeds cap %u",
                    path,
                    line_no,
                    npc_idx,
                    kMaxLocationNpcs);
                continue;
            }
            char where[96];
            std::snprintf(where, sizeof(where), "%s:%d", path, line_no);
            if (!apply_npc_kv(current.npcs[npc_idx], field, value, where)) {
                log::log_warn(
                    log::LogCategory::Config, "%s:%d: bad npc field '%s'", path, line_no, field);
            }
            if (npc_idx + 1 > current.npc_count) {
                current.npc_count = npc_idx + 1;
            }
            continue;
        }
        if (!apply_location_kv(current, key, value)) {
            log::log_warn(
                log::LogCategory::Config, "%s:%d: bad key '%s'", path, line_no, key);
        }
    }
    flush();
    std::fclose(file);

    if (out.count == 0) {
        log::log_error(log::LogCategory::Config, "%s: no locations loaded", path);
        return false;
    }
    if (ok) {
        log::log_info(
            log::LogCategory::Config, "Loaded %u locations from %s", out.count, path);
    } else {
        log::log_error(
            log::LogCategory::Config,
            "%s: %u locations kept but the catalog is INVALID (see errors above)",
            path,
            out.count);
    }
    return ok;
}

bool load_location_catalog(flecs::world& world)
{
    LocationCatalog catalog{};
    const bool      ok = load_location_catalog_file(catalog, kLocationCatalogPath);
    world.set<LocationCatalog>(catalog);
    if (!ok) {
        log::log_error(
            log::LogCategory::Config, "location catalog validation FAILED");
    }
    return ok;
}

const LocationDef* find_location_def(const LocationCatalog& cat, const char* id)
{
    if (id == nullptr || id[0] == '\0') {
        return nullptr;
    }
    for (u32 i = 0; i < cat.count; ++i) {
        if (std::strcmp(cat.items[i].id, id) == 0) {
            return &cat.items[i];
        }
    }
    return nullptr;
}

const LocationDef* find_location_def(flecs::world& world, const char* id)
{
    const LocationCatalog* cat = world.try_get<LocationCatalog>();
    return (cat != nullptr) ? find_location_def(*cat, id) : nullptr;
}

void populate_locations(
    flecs::world& world, const LocationPlacement* placements, u32 count)
{
    const LocationCatalog* cat = world.try_get<LocationCatalog>();
    if (cat == nullptr) {
        log::log_warn(log::LogCategory::Game, "populate_locations: no LocationCatalog");
        return;
    }

    for (u32 p = 0; p < count; ++p) {
        const LocationPlacement& place = placements[p];
        const LocationDef*       def   = find_location_def(*cat, place.location_id);
        if (def == nullptr) {
            log::log_warn(
                log::LogCategory::Game,
                "populate_locations: '%s' not in catalog — skipped",
                (place.location_id != nullptr) ? place.location_id : "(null)");
            continue;
        }

        if (def->gravity_deck) {
            spawn_deck(world, *def, place.world_origin);
        }

        for (u32 n = 0; n < def->npc_count && n < kMaxLocationNpcs; ++n) {
            const LocationNpc& npc = def->npcs[n];
            const glm::vec3    pos = place.world_origin + npc.offset;
            const char*        label =
                (npc.label[0] != '\0') ? npc.label : def->name;

            switch (npc.kind) {
            case LocationNpcKind::Trader:
                (void)spawn_trader_npc(
                    world, pos, npc.market_id, npc.commodity_id, npc.allow_buy,
                    npc.allow_sell, label);
                break;
            case LocationNpcKind::MissionGiver:
                (void)spawn_mission_npc(
                    world, pos, npc.template_id, true, false, npc.mission_market_id, label);
                break;
            case LocationNpcKind::TurnIn:
                (void)spawn_mission_npc(
                    world, pos, npc.template_id, false, true, npc.mission_market_id, label);
                break;
            case LocationNpcKind::TravelPad: {
                const LocationPlacement* dest =
                    find_placement(placements, count, npc.dest_location);
                if (dest == nullptr) {
                    log::log_warn(
                        log::LogCategory::Game,
                        "location '%s': travel pad dest '%s' not placed — skipped",
                        def->id,
                        npc.dest_location);
                    break;
                }
                const LocationDef* dest_def = find_location_def(*cat, dest->location_id);
                const u32 dest_loc_id =
                    (dest_def != nullptr) ? dest_def->location_id : 0u;
                (void)spawn_travel_pad(
                    world, pos, dest->world_origin + kLocationArrivalOffset, dest_loc_id,
                    label);
                break;
            }
            }
        }

        log::log_info(
            log::LogCategory::Game,
            "Populated location '%s' (%u NPCs) at (%.1f, %.1f, %.1f)",
            def->id,
            def->npc_count,
            static_cast<double>(place.world_origin.x),
            static_cast<double>(place.world_origin.y),
            static_cast<double>(place.world_origin.z));
    }
}

}  // namespace csc::game::economy
