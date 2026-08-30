#include "game/world/world.hpp"

#include "engine/ecs/world.hpp"
#include "engine/log/log.hpp"
#include "game/ai/ai.hpp"
#include "game/character/character.hpp"
#include "game/flight/flight.hpp"
#include "game/world/poi_catalog.hpp"
#include "game/world/resources.hpp"
#include "game/world/star_system_gen.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace csc::game::world {
namespace {

void copy_fixed(char* dest, std::size_t dest_bytes, const char* src)
{
    if (dest == nullptr || dest_bytes == 0) {
        return;
    }
    if (src == nullptr) {
        dest[0] = '\0';
        return;
    }
    std::snprintf(dest, dest_bytes, "%s", src);
}

char* trim_inplace(char* s)
{
    while (*s != '\0' && std::isspace(static_cast<unsigned char>(*s))) {
        ++s;
    }
    if (*s == '\0') {
        return s;
    }
    char* end = s + std::strlen(s);
    while (end > s && std::isspace(static_cast<unsigned char>(end[-1]))) {
        --end;
    }
    *end = '\0';
    return s;
}

[[nodiscard]] bool parse_body_type(const char* text, CelestialBodyType& out)
{
    if (text == nullptr) {
        return false;
    }
    if (std::strcmp(text, "star") == 0) {
        out = CelestialBodyType::Star;
        return true;
    }
    if (std::strcmp(text, "planet") == 0) {
        out = CelestialBodyType::Planet;
        return true;
    }
    if (std::strcmp(text, "station") == 0) {
        out = CelestialBodyType::Station;
        return true;
    }
    if (std::strcmp(text, "landing_zone") == 0 || std::strcmp(text, "landingzone") == 0) {
        out = CelestialBodyType::LandingZone;
        return true;
    }
    return false;
}

[[nodiscard]] bool parse_body_index_key(
    const char* key, u32& out_index, char* field_out, std::size_t field_cap)
{
    // body.N.field
    if (key == nullptr || std::strncmp(key, "body.", 5) != 0) {
        return false;
    }
    const char* p = key + 5;
    char*       end = nullptr;
    const long  idx = std::strtol(p, &end, 10);
    if (end == p || idx < 0 || *end != '.') {
        return false;
    }
    out_index = static_cast<u32>(idx);
    const char* field = end + 1;
    copy_fixed(field_out, field_cap, field);
    return field_out[0] != '\0';
}

[[nodiscard]] bool try_player_world_pos(flecs::world& world, glm::vec3& out)
{
    const ecs::ControlMode* mode = world.try_get<ecs::ControlMode>();
    const bool              prefer_ship =
        mode == nullptr || mode->mode == ecs::ControlModeKind::ShipPilot
        || mode->mode == ecs::ControlModeKind::FreeLook;

    if (prefer_ship) {
        bool found = false;
        world.each([&](flecs::entity e, const flight::RigidBody6DOF& rb) {
            if (found || !e.has<flight::PlayerShip>()) {
                return;
            }
            out   = rb.position;
            found = true;
        });
        if (found) {
            return true;
        }
    }

    bool char_found = false;
    world.each([&](flecs::entity e, const character::CharacterController& /*cc*/) {
        if (char_found || !e.has<character::PlayerCharacter>()) {
            return;
        }
        if (const character::LocalToShip* local = e.try_get<character::LocalToShip>()) {
            flecs::entity ship = world.entity(local->ship_entity);
            if (ship.is_alive()) {
                if (const flight::RigidBody6DOF* rb = ship.try_get<flight::RigidBody6DOF>()) {
                    out = rb->position + (rb->orientation * local->local_position);
                    char_found = true;
                    return;
                }
            }
        }
        if (const ecs::Position* p = e.try_get<ecs::Position>()) {
            out        = glm::vec3{p->x, p->y, p->z};
            char_found = true;
        }
    });
    if (char_found) {
        return true;
    }

    // Fallback: any PlayerShip.
    bool ship_found = false;
    world.each([&](flecs::entity e, const flight::RigidBody6DOF& rb) {
        if (ship_found || !e.has<flight::PlayerShip>()) {
            return;
        }
        out        = rb.position;
        ship_found = true;
    });
    return ship_found;
}

void shift_vec3(glm::vec3& v, const glm::vec3& delta)
{
    v -= delta;
}

void rebase_shift_all(flecs::world& world, const glm::vec3& delta)
{
    // In-place field writes only — no add/remove components (archetype-safe).
    world.each([&](ecs::Position& p) {
        p.x -= delta.x;
        p.y -= delta.y;
        p.z -= delta.z;
    });
    world.each([&](ecs::PreviousPosition& p) {
        p.x -= delta.x;
        p.y -= delta.y;
        p.z -= delta.z;
    });
    world.each([&](flight::RigidBody6DOF& rb) { shift_vec3(rb.position, delta); });
    world.each([&](character::GravityZone& z) { shift_vec3(z.center, delta); });
    world.each([&](LevelStreamTrigger& t) { shift_vec3(t.center, delta); });
    // P2-04: atmósferas viven en el frame relativo, se desplazan igual.
    world.each([&](flight::AtmosphereVolume& a) { shift_vec3(a.center, delta); });
    world.each([&](ecs::Camera3D& cam) {
        shift_vec3(cam.eye, delta);
        shift_vec3(cam.target, delta);
    });
}

void stream_set_loaded(flecs::world& world, LevelStreamTrigger& trig, bool want_loaded)
{
    if (trig.loaded == want_loaded) {
        return;
    }
    trig.loaded = want_loaded;

    for (u32 i = 0; i < trig.content_count; ++i) {
        flecs::entity e = world.entity(trig.content[i]);
        if (!e.is_alive()) {
            continue;
        }
        if (want_loaded) {
            if (!e.has<InteriorLoaded>()) {
                e.add<InteriorLoaded>();
            }
            // Soft mesh participation: show streamed markers when loaded.
            if (!e.has<ecs::InstanceTag>()) {
                e.add<ecs::InstanceTag>();
            }
        } else {
            if (e.has<InteriorLoaded>()) {
                e.remove<InteriorLoaded>();
            }
            if (e.has<ecs::InstanceTag>()) {
                e.remove<ecs::InstanceTag>();
            }
        }
    }
}

void update_stream_triggers(flecs::world& world)
{
    glm::vec3 player{};
    if (!try_player_world_pos(world, player)) {
        return;
    }

    world.each([&](LevelStreamTrigger& trig) {
        const glm::vec3 d    = player - trig.center;
        const f32       dist = std::sqrt(glm::dot(d, d));
        if (!trig.loaded && dist <= trig.load_radius) {
            stream_set_loaded(world, trig, true);
        } else if (trig.loaded && dist > trig.unload_radius) {
            stream_set_loaded(world, trig, false);
        }
    });
}

[[nodiscard]] flecs::entity spawn_marker(
    flecs::world& world,
    const char*   name,
    const glm::vec3& pos,
    f32           scale,
    bool          instance_active)
{
    const ecs::Position p{pos.x, pos.y, pos.z};
    flecs::entity       e = world.entity(name)
                          .set<ecs::Position>(p)
                          .set<ecs::PreviousPosition>({p.x, p.y, p.z})
                          .set<ecs::Velocity>({0.f, 0.f, 0.f})
                          .set<ecs::Scale>({scale});
    if (instance_active) {
        e.add<ecs::InstanceTag>();
    }
    return e;
}

void attach_stream_content(LevelStreamTrigger& trig, flecs::entity e)
{
    if (trig.content_count >= kMaxStreamContentPerTrig) {
        return;
    }
    trig.content[trig.content_count++] = e.id();
}

[[nodiscard]] const CelestialBody* find_body(
    const StarSystemData& data, CelestialBodyType type)
{
    for (u32 i = 0; i < data.body_count; ++i) {
        if (data.bodies[i].type == type) {
            return &data.bodies[i];
        }
    }
    return nullptr;
}

// --- P2-13: more locations, same P1D-02 schema/architecture -------------------
// Refactored out of spawn_universe_test so ANY number of Station/LandingZone
// bodies in StarSystemData gets the same treatment the first one always had —
// no new content kind, no change to the streaming/floating-origin contract,
// just looping instead of find_body()'s single-match lookup. Entity/trigger
// names are derived from body->name (already unique per the P1D-02 schema)
// so multiple locations don't collide on flecs' by-name entity lookup.

/// Station host + interior gravity/stream trigger (deck, terminal, hatch) —
/// exactly what the original single-station block built, parameterized.
void spawn_station_location(flecs::world& world, const CelestialBody& body)
{
    char name_buf[world::kNameBytes + 24]{};

    flight::RigidBody6DOF station_rb{};
    station_rb.position     = body.position + glm::vec3{0.f, 8.f, 0.f};
    station_rb.orientation  = glm::quat{1.f, 0.f, 0.f, 0.f};
    station_rb.mass         = 1.e9f;
    station_rb.inertia_diag = glm::vec3{1.e9f, 1.e9f, 1.e9f};

    flecs::entity station = world.entity(body.name)
                                 .set<flight::RigidBody6DOF>(station_rb)
                                 .set<ecs::Position>(
                                     {station_rb.position.x, station_rb.position.y,
                                      station_rb.position.z})
                                 .set<ecs::PreviousPosition>(
                                     {station_rb.position.x, station_rb.position.y,
                                      station_rb.position.z})
                                 .set<ecs::Orientation>({station_rb.orientation})
                                 .set<ecs::Scale>({6.f})
                                 .add<ecs::InstanceTag>()
                                 .add<ecs::KinematicFromRigidBody>()
                                 .add<StationRoot>();

    std::snprintf(name_buf, sizeof(name_buf), "%sGravity", body.name);
    (void)character::spawn_gravity_zone_box(
        world, body.position + glm::vec3{0.f, 2.f, 0.f}, glm::vec3{18.f, 8.f, 18.f},
        glm::vec3{0.f, -1.f, 0.f}, character::kGravityDefault, name_buf);

    LevelStreamTrigger trig{};
    trig.center        = body.position;
    trig.load_radius   = 100.f;
    trig.unload_radius = 160.f;
    trig.kind          = StreamContentKind::StationInterior;
    trig.loaded        = false;

    std::snprintf(name_buf, sizeof(name_buf), "%sDeck", body.name);
    flecs::entity deck =
        spawn_marker(world, name_buf, body.position + glm::vec3{0.f, 0.4f, 0.f}, 10.f, false);
    deck.set<StreamContent>({StreamContentKind::StationInterior});
    attach_stream_content(trig, deck);

    std::snprintf(name_buf, sizeof(name_buf), "%s terminal", body.name);
    flecs::entity terminal =
        character::spawn_station_interactable(world, body.position + glm::vec3{0.f, 1.2f, -4.f}, name_buf);
    terminal.set<StreamContent>({StreamContentKind::StationInterior});
    if (terminal.has<ecs::InstanceTag>()) {
        terminal.remove<ecs::InstanceTag>(); // inactive until the trigger loads it
    }
    attach_stream_content(trig, terminal);

    std::snprintf(name_buf, sizeof(name_buf), "%s hatch", body.name);
    flecs::entity hatch =
        character::spawn_ship_hatch(world, station.id(), glm::vec3{0.f, 0.9f, 4.f}, name_buf);
    hatch.set<StreamContent>({StreamContentKind::StationInterior});
    if (hatch.has<ecs::InstanceTag>()) {
        hatch.remove<ecs::InstanceTag>();
    }
    attach_stream_content(trig, hatch);

    std::snprintf(name_buf, sizeof(name_buf), "Stream_%s", body.name);
    world.entity(name_buf).set<LevelStreamTrigger>(trig);
}

/// Landing pad + beacon + gravity (+ atmosphere if a Planet body is at the
/// same location) — exactly what the original single-LZ block built.
void spawn_landing_zone_location(
    flecs::world& world, const CelestialBody& body, const CelestialBody* co_located_planet)
{
    char name_buf[world::kNameBytes + 24]{};

    LevelStreamTrigger trig{};
    trig.center        = body.position;
    trig.load_radius   = 150.f;
    trig.unload_radius = 220.f;
    trig.kind          = StreamContentKind::LandingZone;
    trig.loaded        = false;

    std::snprintf(name_buf, sizeof(name_buf), "%sPad", body.name);
    flecs::entity lz_root =
        spawn_marker(world, name_buf, body.position + glm::vec3{0.f, 0.5f, 0.f}, 14.f, false);
    lz_root.add<LandingZoneRoot>();
    lz_root.set<StreamContent>({StreamContentKind::LandingZone});
    attach_stream_content(trig, lz_root);

    std::snprintf(name_buf, sizeof(name_buf), "%sBeacon", body.name);
    flecs::entity lz_beacon =
        spawn_marker(world, name_buf, body.position + glm::vec3{8.f, 2.f, 0.f}, 2.5f, false);
    lz_beacon.set<StreamContent>({StreamContentKind::LandingZone});
    attach_stream_content(trig, lz_beacon);

    std::snprintf(name_buf, sizeof(name_buf), "%sGravity", body.name);
    (void)character::spawn_gravity_zone_box(
        world, body.position + glm::vec3{0.f, 2.f, 0.f}, glm::vec3{40.f, 12.f, 40.f},
        glm::vec3{0.f, -1.f, 0.f}, character::kGravityDefault * 0.85f, name_buf);

    std::snprintf(name_buf, sizeof(name_buf), "Stream_%s", body.name);
    world.entity(name_buf).set<LevelStreamTrigger>(trig);

    // P2-04: atmósfera esférica si esta LZ tiene un planeta asociado (misma
    // posición X/Z en el esquema P1D-02 — el espacio abierto sigue vacío).
    if (co_located_planet != nullptr) {
        flight::AtmosphereVolume atmo{};
        atmo.center            = co_located_planet->position;
        atmo.inner_radius      = co_located_planet->radius;
        atmo.outer_radius      = co_located_planet->radius * 3.f;
        atmo.sea_level_density = flight::kDefaultSeaLevelDensity;
        atmo.surface_gravity   = flight::kDefaultSurfaceGravity * 0.85f;
        std::snprintf(name_buf, sizeof(name_buf), "%sAtmosphere", co_located_planet->name);
        world.entity(name_buf).set<flight::AtmosphereVolume>(atmo);
    }
}

}  // namespace

void register_systems(flecs::world& /*world*/)
{
    // Systems run via fixed_step from game::fixed_step — no Flecs OnUpdate systems.
}

void rebase_if_needed(flecs::world& world)
{
    FloatingOrigin* fo = world.try_get_mut<FloatingOrigin>();
    if (fo == nullptr) {
        return;
    }

    glm::vec3 player{};
    if (!try_player_world_pos(world, player)) {
        return;
    }

    const f32 dist2 = glm::dot(player, player);
    const f32 thr   = fo->threshold;
    if (dist2 <= thr * thr) {
        return;
    }

    // Recenter on player: relative positions -= player; origin_offset += player.
    const glm::vec3 delta = player;
    fo->origin_offset += delta;
    ++fo->rebase_count;
    rebase_shift_all(world, delta);

    log::log_info(
        log::LogCategory::Ecs,
        "FloatingOrigin rebase #%u — offset now (%.1f, %.1f, %.1f)",
        fo->rebase_count,
        static_cast<double>(fo->origin_offset.x),
        static_cast<double>(fo->origin_offset.y),
        static_cast<double>(fo->origin_offset.z));
}

void fixed_step(flecs::world& world, f32 dt)
{
    rebase_if_needed(world);
    update_stream_triggers(world);
    update_mining(world, dt);  // P4-06
}

void star_system_set_placeholders(StarSystemData& out)
{
    out = StarSystemData{};
    copy_fixed(out.system_name, sizeof(out.system_name), "Sistema-01");

    // Star at origin of the system.
    {
        CelestialBody& b = out.bodies[out.body_count++];
        copy_fixed(b.name, sizeof(b.name), "Sistema-01-Star");
        b.type     = CelestialBodyType::Star;
        b.position = glm::vec3{0.f, 0.f, 500.f};
        b.radius   = 80.f;
    }
    // Station near spawn.
    {
        CelestialBody& b = out.bodies[out.body_count++];
        copy_fixed(b.name, sizeof(b.name), "Estacion-Alfa");
        b.type     = CelestialBodyType::Station;
        b.position = glm::vec3{0.f, 0.f, 0.f};
        b.radius   = 40.f;
    }
    // Planet marker (visual only at this phase).
    {
        CelestialBody& b = out.bodies[out.body_count++];
        copy_fixed(b.name, sizeof(b.name), "Planeta-01");
        b.type     = CelestialBodyType::Planet;
        b.position = glm::vec3{0.f, -80.f, -2500.f};
        b.radius   = 200.f;
    }
    // Landing zone beyond rebase threshold (kFloatingOriginThreshold = 2000).
    {
        CelestialBody& b = out.bodies[out.body_count++];
        copy_fixed(b.name, sizeof(b.name), "Planeta-01-LZ");
        b.type     = CelestialBodyType::LandingZone;
        b.position = glm::vec3{0.f, 0.f, -2500.f};
        b.radius   = 60.f;
    }
}

bool load_star_system_config(StarSystemData& out, const char* path)
{
    star_system_set_placeholders(out);

    if (path == nullptr || path[0] == '\0') {
        return false;
    }

    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        log::log_warn(
            log::LogCategory::Config,
            "star_system.cfg not found (%s) — using placeholders",
            path);
        return false;
    }

    // `loaded` accumulates the curated (hand-authored) bodies. If the file
    // carries a `base_seed`, those are merged onto a procedurally generated
    // base after the loop (P4-04) — the fixed system becomes a particular seed
    // of the P4-01 generator plus its curated overlay, nothing lost.
    StarSystemData loaded{};
    copy_fixed(loaded.system_name, sizeof(loaded.system_name), "Sistema-01");
    bool has_base_seed    = false;
    bool has_system_name  = false;
    u64  base_seed        = 0;

    char line[512];
    int  line_no = 0;
    while (std::fgets(line, static_cast<int>(sizeof(line)), file) != nullptr) {
        ++line_no;
        char* trimmed = trim_inplace(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        char* eq = std::strchr(trimmed, '=');
        if (eq == nullptr) {
            continue;
        }
        *eq         = '\0';
        char* key   = trim_inplace(trimmed);
        char* value = trim_inplace(eq + 1);

        if (std::strcmp(key, "system_name") == 0) {
            copy_fixed(loaded.system_name, sizeof(loaded.system_name), value);
            has_system_name = true;
            continue;
        }
        if (std::strcmp(key, "base_seed") == 0) {  // P4-04
            base_seed     = std::strtoull(value, nullptr, 0);
            has_base_seed = true;
            continue;
        }
        if (std::strcmp(key, "body.count") == 0) {
            unsigned v = 0;
            if (std::sscanf(value, "%u", &v) == 1) {
                loaded.body_count = std::min(v, kMaxCelestialBodies);
            }
            continue;
        }

        u32  idx = 0;
        char field[32]{};
        if (!parse_body_index_key(key, idx, field, sizeof(field))) {
            log::log_warn(
                log::LogCategory::Config, "%s:%d: unknown key '%s'", path, line_no, key);
            continue;
        }
        if (idx >= kMaxCelestialBodies) {
            continue;
        }
        if (idx >= loaded.body_count) {
            loaded.body_count = idx + 1;
        }
        CelestialBody& b = loaded.bodies[idx];
        if (std::strcmp(field, "name") == 0) {
            copy_fixed(b.name, sizeof(b.name), value);
        } else if (std::strcmp(field, "type") == 0) {
            CelestialBodyType t{};
            if (parse_body_type(value, t)) {
                b.type = t;
            }
        } else if (std::strcmp(field, "x") == 0) {
            float v = 0.f;
            if (std::sscanf(value, "%f", &v) == 1) {
                b.position.x = v;
            }
        } else if (std::strcmp(field, "y") == 0) {
            float v = 0.f;
            if (std::sscanf(value, "%f", &v) == 1) {
                b.position.y = v;
            }
        } else if (std::strcmp(field, "z") == 0) {
            float v = 0.f;
            if (std::sscanf(value, "%f", &v) == 1) {
                b.position.z = v;
            }
        } else if (std::strcmp(field, "radius") == 0) {
            float v = 0.f;
            if (std::sscanf(value, "%f", &v) == 1) {
                b.radius = v;
            }
        }
    }

    std::fclose(file);

    if (loaded.body_count == 0 && !has_base_seed) {
        log::log_warn(
            log::LogCategory::Config,
            "%s: no bodies — keeping placeholders",
            path);
        return false;
    }

    if (has_base_seed) {
        // P4-04: procedural base + curated overlay. A curated body whose name
        // matches a generated one replaces it; otherwise it is appended.
        StarSystemData composed{};
        const u32      gen_count = generate_star_system(base_seed, composed);
        if (has_system_name) {
            copy_fixed(composed.system_name, sizeof(composed.system_name), loaded.system_name);
        }
        u32 merged   = 0;
        u32 appended = 0;
        for (u32 c = 0; c < loaded.body_count; ++c) {
            const CelestialBody& cb = loaded.bodies[c];
            if (cb.name[0] == '\0') {
                continue;
            }
            i32 hit = -1;
            for (u32 g = 0; g < composed.body_count; ++g) {
                // A system has one star: a curated Star replaces the generated
                // one regardless of name. Everything else merges by name.
                const bool star_match = cb.type == CelestialBodyType::Star
                                        && composed.bodies[g].type == CelestialBodyType::Star;
                if (star_match || std::strcmp(composed.bodies[g].name, cb.name) == 0) {
                    hit = static_cast<i32>(g);
                    break;
                }
            }
            if (hit >= 0) {
                composed.bodies[hit] = cb;
                ++merged;
            } else if (composed.body_count < kMaxCelestialBodies) {
                composed.bodies[composed.body_count++] = cb;
                ++appended;
            } else {
                log::log_warn(
                    log::LogCategory::Config,
                    "%s: system full (%u) — curated body '%s' dropped",
                    path, kMaxCelestialBodies, cb.name);
            }
        }
        out = composed;
        log::log_info(
            log::LogCategory::Config,
            "Composed star system '%s' from seed %llu: %u generated + %u curated "
            "(%u merged, %u appended) = %u bodies",
            out.system_name,
            static_cast<unsigned long long>(base_seed),
            gen_count, merged + appended, merged, appended, out.body_count);
        return true;
    }

    out = loaded;
    log::log_info(
        log::LogCategory::Config,
        "Loaded star system '%s' (%u bodies) from %s",
        out.system_name,
        out.body_count,
        path);
    return true;
}

bool fixed_system_smoke_test(const char* path)
{
    StarSystemData a{};
    StarSystemData b{};
    const bool     ok_a = load_star_system_config(a, path);
    const bool     ok_b = load_star_system_config(b, path);

    bool ok = ok_a && ok_b;

    const auto has_body = [](const StarSystemData& s, const char* name) {
        for (u32 i = 0; i < s.body_count; ++i) {
            if (std::strcmp(s.bodies[i].name, name) == 0) {
                return true;
            }
        }
        return false;
    };
    const auto has_generated = [](const StarSystemData& s) {
        for (u32 i = 0; i < s.body_count; ++i) {
            if (std::strncmp(s.bodies[i].name, "Sys-", 4) == 0) {
                return true;
            }
        }
        return false;
    };

    for (const char* name : {"Estacion-Alfa", "Estacion-Beta", "Planeta-01", "Planeta-02"}) {
        if (!has_body(a, name)) {
            log::log_error(log::LogCategory::Config, "fixedsys: curated body '%s' missing", name);
            ok = false;
        }
    }
    if (!has_generated(a)) {
        log::log_error(log::LogCategory::Config, "fixedsys: no generated body ('Sys-*') present");
        ok = false;
    }
    if (a.body_count <= 7) {
        log::log_error(
            log::LogCategory::Config, "fixedsys: only %u bodies — base_seed not composing?",
            a.body_count);
        ok = false;
    }
    if (a.body_count != b.body_count
        || std::memcmp(a.bodies, b.bodies, a.body_count * sizeof(CelestialBody)) != 0) {
        log::log_error(log::LogCategory::Config, "fixedsys: composed system is not deterministic");
        ok = false;
    }

    log::log_info(log::LogCategory::Config, "CSC_FIXEDSYS_SMOKE: %s", ok ? "PASS" : "FAIL");
    return ok;
}

flecs::entity spawn_universe_test(
    flecs::world& world, const StarSystemData& data, const char* player_ship_id, u64 system_seed)
{
    FloatingOrigin fo{};
    fo.threshold    = kFloatingOriginThreshold;
    fo.rebase_count = 0;
    fo.origin_offset = glm::vec3{0.f};
    world.set<FloatingOrigin>(fo);
    world.set<StarSystemData>(data);

    // Extend camera far plane for system-scale travel.
    world.each([&](ecs::Camera3D& cam) {
        cam.far_plane = 12000.f;
    });

    // P2-13: spawn EVERY Station/LandingZone/Planet/Star body in the data,
    // not just the first of each — same P1D-02 schema, same per-body content
    // the original single-location code built (now in the two helpers above),
    // just looped. `station_pos`/`lz_pos` (first of each) stay the anchor for
    // player spawn + the status log below, matching the original behaviour.
    const CelestialBody* station_body = find_body(data, CelestialBodyType::Station);
    const CelestialBody* lz_body      = find_body(data, CelestialBodyType::LandingZone);
    const glm::vec3 station_pos =
        station_body != nullptr ? station_body->position : glm::vec3{0.f, 0.f, 0.f};
    const glm::vec3 lz_pos =
        lz_body != nullptr ? lz_body->position : glm::vec3{0.f, 0.f, -2500.f};

    u32 station_count = 0;
    u32 lz_count      = 0;
    u32 planet_count  = 0;
    u32 star_count    = 0;
    for (u32 i = 0; i < data.body_count; ++i) {
        const CelestialBody& body = data.bodies[i];
        switch (body.type) {
        case CelestialBodyType::Station:
            spawn_station_location(world, body);
            ++station_count;
            break;
        case CelestialBodyType::LandingZone: {
            // Co-located planet: same X/Z within a small tolerance (P1D-02
            // convention — the LZ sits at the planet's surface, y differs).
            const CelestialBody* planet = nullptr;
            for (u32 j = 0; j < data.body_count; ++j) {
                if (data.bodies[j].type != CelestialBodyType::Planet) {
                    continue;
                }
                const f32 dx = data.bodies[j].position.x - body.position.x;
                const f32 dz = data.bodies[j].position.z - body.position.z;
                if (std::abs(dx) < 1.f && std::abs(dz) < 1.f) {
                    planet = &data.bodies[j];
                    break;
                }
            }
            spawn_landing_zone_location(world, body, planet);
            ++lz_count;
            break;
        }
        case CelestialBodyType::Planet:
            // Distant visual marker (always active — cheap cube).
            (void)spawn_marker(
                world, body.name, body.position, std::max(20.f, body.radius * 0.15f), true);
            ++planet_count;
            break;
        case CelestialBodyType::Star:
            (void)spawn_marker(world, body.name, body.position, 12.f, true);
            ++star_count;
            break;
        }
    }
    log::log_info(
        log::LogCategory::Game,
        "universe_test locations: %u station(s), %u landing zone(s), %u planet(s), %u star(s)",
        station_count,
        lz_count,
        planet_count,
        star_count);

    // P4-06: procedurally-placed minable asteroids for this system.
    if (system_seed != 0) {
        spawn_asteroid_field(world, system_seed);
        spawn_pois_for_system(world, system_seed, data);  // P4-07
    }

    // Player ship near station, facing -Z toward landing zone (open-space stretch).
    flight::spawn_projectile_pool(world);
    ai::spawn_encounter_pool(world); // P2-12: dormant until proximity activates them
    flecs::entity ship = flight::spawn_player_ship(
        world, station_pos + glm::vec3{0.f, 5.f, 25.f}, player_ship_id);

    // Optional CI smoke: CSC_FORCE_REBASE_SMOKE=1 teleports past threshold so the
    // first fixed_step performs ≥1 rebase (logged + DebugUiStats.rebase_count).
    if (const char* smoke = std::getenv("CSC_FORCE_REBASE_SMOKE");
        smoke != nullptr && smoke[0] == '1') {
        const glm::vec3 far_pos = station_pos + glm::vec3{0.f, 5.f, -(kFloatingOriginThreshold + 100.f)};
        if (flight::RigidBody6DOF* rb = ship.try_get_mut<flight::RigidBody6DOF>()) {
            rb->position = far_pos;
        }
        if (ecs::Position* p = ship.try_get_mut<ecs::Position>()) {
            p->x = far_pos.x;
            p->y = far_pos.y;
            p->z = far_pos.z;
        }
        if (ecs::PreviousPosition* prev = ship.try_get_mut<ecs::PreviousPosition>()) {
            prev->x = far_pos.x;
            prev->y = far_pos.y;
            prev->z = far_pos.z;
        }
        log::log_info(
            log::LogCategory::Core,
            "CSC_FORCE_REBASE_SMOKE: ship placed at z=%.0f (threshold=%.0f)",
            static_cast<double>(far_pos.z),
            static_cast<double>(kFloatingOriginThreshold));
    }

    world.set<ecs::ControlMode>({ecs::ControlModeKind::ShipPilot});

    // Force initial stream evaluation so station interior loads at spawn.
    update_stream_triggers(world);

    log::log_info(
        log::LogCategory::Core,
        "universe_test: system='%s' station=(%.0f,%.0f,%.0f) lz=(%.0f,%.0f,%.0f) "
        "rebase_threshold=%.0f",
        data.system_name,
        static_cast<double>(station_pos.x),
        static_cast<double>(station_pos.y),
        static_cast<double>(station_pos.z),
        static_cast<double>(lz_pos.x),
        static_cast<double>(lz_pos.y),
        static_cast<double>(lz_pos.z),
        static_cast<double>(kFloatingOriginThreshold));

    return ship;
}

void fill_world_telemetry(flecs::world& world, u32& rebase_count, bool& found)
{
    found        = false;
    rebase_count = 0;
    if (const FloatingOrigin* fo = world.try_get<FloatingOrigin>()) {
        rebase_count = fo->rebase_count;
        found        = true;
    }
}

}  // namespace csc::game::world
