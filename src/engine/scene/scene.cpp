#include "engine/scene/scene.hpp"

#include "engine/log/log.hpp"
#include "engine/vulkan/device.hpp"
#include "engine/vulkan/renderer.hpp"
#include "game/ai/ai.hpp"
#include "game/audio/audio.hpp"
#include "game/character/character.hpp"
#include "game/character/suit_catalog.hpp"
#include "game/content_smoke.hpp"
#include "game/economy/economy.hpp"
#include "game/economy/location_catalog.hpp"
#include "game/flight/flight.hpp"
#include "game/flight/ship_catalog.hpp"
#include "game/flight/weapon_catalog.hpp"
#include "game/save/save.hpp"
#include "game/ui/ui.hpp"
#include "game/world/galaxy.hpp"
#include "game/world/planet_terrain.hpp"
#include "game/world/poi_catalog.hpp"
#include "game/world/resources.hpp"
#include "game/world/star_system_gen.hpp"
#include "game/world/terrain_stream.hpp"
#include "game/world/world.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace csc::scene {
namespace {

bool setup_grid_freelook(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }
    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);
    ctx.needs_shared_mesh = false;
    ctx.instance_count    = 0;
    return true;
}

bool setup_instancing_stress(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }
    constexpr u32 kInstances = 600u;
    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);
    ecs::world_spawn_demo_instances(*ctx.world, kInstances);
    ctx.needs_shared_mesh = true;
    ctx.instance_count    = kInstances;
    return true;
}

bool setup_mesh_viewer(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }
    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);
    ecs::world_spawn_centered_instance(*ctx.world, 1.5f);
    ctx.needs_shared_mesh = true;
    ctx.instance_count    = 1;
    return true;
}

bool setup_flight_test(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }

    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);

    // Ship at (0,5,0); static asteroid ~50m ahead along -Z (ship forward).
    game::flight::spawn_projectile_pool(*ctx.world);
    (void)game::flight::spawn_player_ship(
        *ctx.world, glm::vec3{0.f, 5.f, 0.f}, ctx.player_ship_id);
    (void)game::flight::spawn_damage_target(*ctx.world, glm::vec3{0.f, 5.f, -50.f}, 5.f);

    ctx.world->set<ecs::ControlMode>({ecs::ControlModeKind::ShipPilot});

    ctx.needs_shared_mesh = true;
    // Player + target + up to kProjectilePoolSize when firing (capacity for gather).
    ctx.instance_count =
        2u + static_cast<u32>(game::flight::kProjectilePoolSize);
    return true;
}

bool setup_on_foot_test(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }

    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);

    game::flight::spawn_projectile_pool(*ctx.world);

    // Moving ship — coasts while OnFoot so LocalToShip interior is visible.
    flecs::entity ship = game::flight::spawn_player_ship(
        *ctx.world, glm::vec3{0.f, 8.f, 0.f}, ctx.player_ship_id);
    if (game::flight::RigidBody6DOF* rb = ship.try_get_mut<game::flight::RigidBody6DOF>()) {
        rb->linear_vel = glm::vec3{1.8f, 0.f, 0.f};
    }
    if (game::flight::FlightControl* fc = ship.try_get_mut<game::flight::FlightControl>()) {
        fc->coupled = false;
    }

    // Player starts inside the ship (authoritative LocalToShip).
    (void)game::character::spawn_player_character(
        *ctx.world, glm::vec3{0.f, 0.9f, 0.f}, ship.id(), ctx.player_suit_id);

    (void)game::character::spawn_ship_hatch(
        *ctx.world, ship.id(), glm::vec3{0.f, 0.9f, 3.2f}, "Exit / Enter hatch");
    (void)game::character::spawn_pilot_seat(
        *ctx.world, ship.id(), glm::vec3{0.f, 0.9f, -1.5f}, "Pilot seat");

    // Interior gravity is artificial (LocalToShip path). Station pad has a world zone.
    constexpr glm::vec3 kStationCenter{45.f, 2.f, 0.f};
    (void)game::character::spawn_gravity_zone_box(
        *ctx.world,
        kStationCenter,
        glm::vec3{12.f, 6.f, 12.f},
        glm::vec3{0.f, -1.f, 0.f},
        game::character::kGravityDefault,
        "StationGravity");

    // Station deck marker + terminal interactable.
    {
        const ecs::Position pos{kStationCenter.x, 0.5f, kStationCenter.z};
        ctx.world->entity("StationDeck")
            .set<ecs::Position>(pos)
            .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
            .set<ecs::Velocity>({0.f, 0.f, 0.f})
            .set<ecs::Scale>({8.f})
            .add<ecs::InstanceTag>();
    }
    (void)game::character::spawn_station_interactable(
        *ctx.world,
        glm::vec3{kStationCenter.x, 1.2f, kStationCenter.z - 3.f},
        "Station terminal");

    // Dummy FPS combat target on the station pad.
    (void)game::character::spawn_health_target(
        *ctx.world, glm::vec3{kStationCenter.x + 4.f, 1.2f, kStationCenter.z}, 1.8f);

    // P2-05: pickable items on the station pad (medkit / ammo / pistol).
    (void)game::character::spawn_item_pickup(
        *ctx.world,
        glm::vec3{kStationCenter.x - 3.f, 0.9f, kStationCenter.z + 2.f},
        game::character::kItemMedkit,
        1u);
    (void)game::character::spawn_item_pickup(
        *ctx.world,
        glm::vec3{kStationCenter.x - 4.5f, 0.9f, kStationCenter.z + 2.f},
        game::character::kItemAmmoPack,
        2u);
    (void)game::character::spawn_item_pickup(
        *ctx.world,
        glm::vec3{kStationCenter.x - 6.f, 0.9f, kStationCenter.z + 2.f},
        game::character::kItemPistol,
        1u);

    // P2-06: hostile pirate NPCs patrolling the station pad + cover crates.
    {
        game::character::PatrolRoute route{};
        route.points[0] = glm::vec3{kStationCenter.x + 7.f, 0.9f, kStationCenter.z + 6.f};
        route.points[1] = glm::vec3{kStationCenter.x + 7.f, 0.9f, kStationCenter.z - 6.f};
        route.points[2] = glm::vec3{kStationCenter.x - 2.f, 0.9f, kStationCenter.z - 6.f};
        route.count     = 3;
        (void)game::character::spawn_npc_combatant(
            *ctx.world,
            route.points[0],
            game::ai::kPirateFaction,
            route,
            "PirateGrunt");

        game::character::PatrolRoute route2{};
        route2.points[0] = glm::vec3{kStationCenter.x - 8.f, 0.9f, kStationCenter.z - 4.f};
        route2.points[1] = glm::vec3{kStationCenter.x - 8.f, 0.9f, kStationCenter.z + 6.f};
        route2.count     = 2;
        (void)game::character::spawn_npc_combatant(
            *ctx.world,
            route2.points[0],
            game::ai::kPirateFaction,
            route2,
            "PirateGrunt2");

        (void)game::character::spawn_cover_point(
            *ctx.world, glm::vec3{kStationCenter.x + 3.f, 0.9f, kStationCenter.z + 5.f});
        (void)game::character::spawn_cover_point(
            *ctx.world, glm::vec3{kStationCenter.x - 4.f, 0.9f, kStationCenter.z - 5.f});
    }

    ctx.world->set<ecs::ControlMode>({ecs::ControlModeKind::OnFoot});

    ctx.needs_shared_mesh = true;
    // ship + player + hatch + seat + deck + terminal + target + 3 pickups
    // + 2 NPCs + 2 cover crates + projectiles
    ctx.instance_count = 14u + static_cast<u32>(game::flight::kProjectilePoolSize);
    return true;
}

bool setup_crew_turret_test(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }

    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);

    game::flight::spawn_projectile_pool(*ctx.world);

    // Player ship with full NPC crew: engineer + turret gunner (P2-01).
    flecs::entity ship = game::flight::spawn_player_ship(
        *ctx.world, glm::vec3{0.f, 5.f, 0.f}, ctx.player_ship_id);

    flecs::entity turret = game::flight::spawn_turret(
        *ctx.world, ship.id(), glm::vec3{0.f, 1.8f, 0.f}, 0u, true, "PlayerTurret");

    (void)game::flight::spawn_crew_member(
        *ctx.world,
        ship.id(),
        game::flight::CrewRole::Engineer,
        glm::vec3{-1.2f, 0.9f, 0.5f},
        "CrewEngineer");
    flecs::entity gunner = game::flight::spawn_crew_member(
        *ctx.world,
        ship.id(),
        game::flight::CrewRole::TurretGunner,
        glm::vec3{1.2f, 0.9f, 0.5f},
        "CrewGunner");
    if (game::flight::CrewMember* cm = gunner.try_get_mut<game::flight::CrewMember>()) {
        cm->turret = turret.id();
    }

    // Seat so the player can take the turret manually (P2-03 seat change).
    (void)game::character::spawn_turret_seat(
        *ctx.world, ship.id(), turret.id(), glm::vec3{0.f, 0.9f, 1.2f}, "Turret seat");
    (void)game::character::spawn_pilot_seat(
        *ctx.world, ship.id(), glm::vec3{0.f, 0.9f, -1.5f}, "Pilot seat");

    // Hostile pirate platform ahead: its AI turret opens fire on the player
    // (faction 3 = pirates, hostile a todos — mismo framework P2-11).
    flecs::entity pirate = game::flight::spawn_npc_ship(
        *ctx.world, glm::vec3{0.f, 6.f, -70.f}, game::ai::kPirateFaction, "PirateSkiff");
    // Face the player so its turret arc (±120° around rest -Z) covers us.
    {
        const glm::quat face_player =
            glm::angleAxis(glm::pi<f32>(), glm::vec3{0.f, 1.f, 0.f});
        if (game::flight::RigidBody6DOF* rb =
                pirate.try_get_mut<game::flight::RigidBody6DOF>()) {
            rb->orientation = face_player;
        }
        if (ecs::Orientation* o = pirate.try_get_mut<ecs::Orientation>()) {
            o->q = face_player;
        }
    }
    (void)game::flight::spawn_turret(
        *ctx.world,
        pirate.id(),
        glm::vec3{0.f, 1.8f, 0.f},
        game::ai::kPirateFaction,
        false,
        "PirateTurret");

    ctx.world->set<ecs::ControlMode>({ecs::ControlModeKind::ShipPilot});

    ctx.needs_shared_mesh = true;
    // ship + turret + 2 crew + 2 seats + pirate + pirate turret + projectiles
    ctx.instance_count = 8u + static_cast<u32>(game::flight::kProjectilePoolSize);

    log::log_info(
        log::LogCategory::Game,
        "crew_turret_test: pirate turret vs crewed player ship | subsystem HUD | "
        "engineer repairs | turret seat via [F]");
    return true;
}

bool setup_npc_combat_test(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }

    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);

    // Ground gravity over the whole arena (player + NPCs walk, no EVA).
    (void)game::character::spawn_gravity_zone_box(
        *ctx.world,
        glm::vec3{0.f, 2.f, 0.f},
        glm::vec3{40.f, 6.f, 40.f},
        glm::vec3{0.f, -1.f, 0.f},
        game::character::kGravityDefault,
        "ArenaGravity");

    // Deck marker sunk so only its top face reads as the arena floor.
    {
        const ecs::Position pos{0.f, -5.f, -10.f};
        ctx.world->entity("ArenaDeck")
            .set<ecs::Position>(pos)
            .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
            .set<ecs::Velocity>({0.f, 0.f, 0.f})
            .set<ecs::Scale>({10.f})
            .add<ecs::InstanceTag>();
    }

    // Player on foot, world space, looking down -Z at the pirates.
    (void)game::character::spawn_player_character(
        *ctx.world, glm::vec3{0.f, 0.9f, 8.f}, 0, ctx.player_suit_id);

    // P2-06: two pirate grunts patrolling ahead + cover crates between them.
    {
        game::character::PatrolRoute route{};
        route.points[0] = glm::vec3{6.f, 0.9f, -14.f};
        route.points[1] = glm::vec3{-6.f, 0.9f, -14.f};
        route.count     = 2;
        (void)game::character::spawn_npc_combatant(
            *ctx.world, route.points[0], game::ai::kPirateFaction, route, "PirateGrunt");

        game::character::PatrolRoute route2{};
        route2.points[0] = glm::vec3{-10.f, 0.9f, -20.f};
        route2.points[1] = glm::vec3{10.f, 0.9f, -20.f};
        route2.count     = 2;
        (void)game::character::spawn_npc_combatant(
            *ctx.world, route2.points[0], game::ai::kPirateFaction, route2,
            "PirateGrunt2");

        (void)game::character::spawn_cover_point(*ctx.world, glm::vec3{4.f, 0.9f, -18.f});
        (void)game::character::spawn_cover_point(*ctx.world, glm::vec3{-4.f, 0.9f, -18.f});
    }

    // Ammo on the floor for the fight.
    (void)game::character::spawn_item_pickup(
        *ctx.world, glm::vec3{2.f, 0.9f, 5.f}, game::character::kItemAmmoPack, 2u);

    ctx.world->set<ecs::ControlMode>({ecs::ControlModeKind::OnFoot});

    ctx.needs_shared_mesh = true;
    // deck + player + 2 NPCs + 2 cover crates + pickup
    ctx.instance_count = 7u;

    log::log_info(
        log::LogCategory::Game,
        "npc_combat_test: 2 pirate grunts (shared AI FSM) patrol → detect → combat | "
        "cover crates when hurt | flee at low HP");
    return true;
}

bool setup_economy_test(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }
    if (!game::economy::setup_economy_test_scene(*ctx.world, ctx.aspect)) {
        return false;
    }
    ctx.needs_shared_mesh = true;
    // 2 decks + ship + player + 2 traders + 2 mission NPCs + 2 pads + projectiles
    ctx.instance_count = 10u + static_cast<u32>(game::flight::kProjectilePoolSize);
    return true;
}

bool setup_universe_test(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }

    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);

    // P4-06: mining pulls commodities into the ship's CargoHold — needs the
    // economy tables + a wallet/cargo like the trade scenes.
    (void)game::economy::load_economy_data(*ctx.world);

    if (std::getenv("CSC_SYSTEMGEN_SMOKE") != nullptr) {
        (void)game::world::star_system_gen_smoke_test();  // P4-01
    }
    if (std::getenv("CSC_PLANETGEN_SMOKE") != nullptr) {
        (void)game::world::planet_terrain_smoke_test();   // P4-02
    }
    if (std::getenv("CSC_TERRAINSTREAM_SMOKE") != nullptr) {
        (void)game::world::terrain_stream_smoke_test();   // P4-03
    }
    if (std::getenv("CSC_FIXEDSYS_SMOKE") != nullptr) {
        (void)game::world::fixed_system_smoke_test("assets/data/star_system.cfg");  // P4-04
    }
    if (std::getenv("CSC_GALAXY_SMOKE") != nullptr) {
        (void)game::world::galaxy_smoke_test();  // P4-05
    }
    if (std::getenv("CSC_RESOURCES_SMOKE") != nullptr) {
        (void)game::world::resources_smoke_test();  // P4-06
    }
    if (std::getenv("CSC_POI_SMOKE") != nullptr) {
        (void)game::world::poi_smoke_test();  // P4-07
    }

    // P4-05: the galaxy this system belongs to (node 0 = the composed home
    // system from P4-04). `--system=<n>` picks another node as a load transition.
    game::world::GalaxyMap galaxy{};
    game::world::generate_galaxy(
        game::world::kDefaultGalaxySeed, game::world::kHomeSystemSeed, galaxy);
    u32 node = 0;
    if (ctx.galaxy_system != nullptr) {
        node = static_cast<u32>(std::strtoul(ctx.galaxy_system, nullptr, 0));
        if (node >= galaxy.count) {
            node = 0;
        }
    }
    galaxy.current = node;
    game::world::galaxy_log(galaxy);
    ctx.world->set<game::world::GalaxyMap>(galaxy);

    game::world::StarSystemData system{};
    csc::u64                    system_seed = game::world::kHomeSystemSeed;
    if (ctx.world_seed != nullptr) {
        // P4-01: procedural star system from --seed=<n>.
        system_seed      = static_cast<csc::u64>(std::strtoull(ctx.world_seed, nullptr, 0));
        const csc::u32 n = game::world::generate_star_system(system_seed, system);
        log::log_info(
            log::LogCategory::Core,
            "universe_test: generated system '%s' from seed %llu (%u bodies)",
            system.system_name,
            static_cast<unsigned long long>(system_seed),
            n);
    } else if (node == 0) {
        (void)game::world::load_star_system_config(system, "assets/data/star_system.cfg");
    } else {
        system_seed      = galaxy.systems[node].seed;
        const csc::u32 n = game::world::generate_star_system(system_seed, system);
        log::log_info(
            log::LogCategory::Core,
            "universe_test: jumped to galaxy node %u '%s' (%u bodies)",
            node, system.system_name, n);
    }

    (void)game::world::spawn_universe_test(
        *ctx.world, system, ctx.player_ship_id, system_seed);

    ctx.needs_shared_mesh = true;
    // Star + planets + LZ/stations + ship + streamed props + projectiles.
    ctx.instance_count = 20u + static_cast<u32>(game::flight::kProjectilePoolSize);
    return true;
}

bool setup_ui_audio_test(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }

    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);

    game::ui::ensure_singletons(*ctx.world);
    {
        game::ui::UiMenuState menus{};
        menus.hud_mode = game::ui::HudDisplayMode::Both;
        ctx.world->set<game::ui::UiMenuState>(menus);
        ctx.world->set<game::ui::SimulationPaused>({false});
    }

    // Star system for pause → System Map.
    game::world::StarSystemData system{};
    (void)game::world::load_star_system_config(system, "assets/data/star_system.cfg");
    ctx.world->set<game::world::StarSystemData>(system);
    ctx.world->set<game::world::FloatingOrigin>(game::world::FloatingOrigin{});

    (void)game::economy::load_economy_data(*ctx.world);

    game::flight::spawn_projectile_pool(*ctx.world);
    flecs::entity ship = game::flight::spawn_player_ship(
        *ctx.world, glm::vec3{0.f, 5.f, 0.f}, ctx.player_ship_id);
    game::economy::attach_cargo_hold_if_missing(ship);
    (void)game::flight::spawn_damage_target(*ctx.world, glm::vec3{0.f, 5.f, -40.f}, 4.f);

    // Seed cargo + an active mission for inventory / mission-log menus.
    if (game::economy::CargoHold* hold = ship.try_get_mut<game::economy::CargoHold>()) {
        const game::economy::CommodityTable* table =
            ctx.world->try_get<game::economy::CommodityTable>();
        if (table != nullptr) {
            (void)game::economy::cargo_add(*hold, *table, 1u, 3u);
        }
    }
    if (game::economy::MissionActivePool* pool =
            ctx.world->try_get_mut<game::economy::MissionActivePool>()) {
        const game::economy::MissionTemplateTable* templates =
            ctx.world->try_get<game::economy::MissionTemplateTable>();
        game::economy::CompletedMissions* completed =
            ctx.world->try_get_mut<game::economy::CompletedMissions>();
        if (templates != nullptr && completed != nullptr) {
            (void)game::economy::mission_try_accept(*pool, *templates, 1u, *completed);
        }
    }

    (void)game::character::spawn_player_character(
        *ctx.world, glm::vec3{2.f, 1.f, 4.f}, 0, ctx.player_suit_id);

    // Three simultaneous looping positional tones at different ranges (P1E-07).
    auto spawn_emitter = [&](const char* name, const glm::vec3& p, game::audio::SoundId snd,
                             u32 key, f32 scale) {
        game::audio::PositionalEmitter em{};
        em.sound       = snd;
        em.priority    = game::audio::kPriorityNormal;
        em.gain        = 0.7f;
        em.looping     = true;
        em.emitter_key = key;
        const ecs::Position pos{p.x, p.y, p.z};
        ctx.world->entity(name)
            .set<ecs::Position>(pos)
            .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
            .set<ecs::Velocity>({0.f, 0.f, 0.f})
            .set<ecs::Scale>({scale})
            .set<game::audio::PositionalEmitter>(em)
            .add<ecs::InstanceTag>();
    };
    spawn_emitter(
        "AudioToneNear", glm::vec3{6.f, 2.f, -4.f}, game::audio::SoundId::ToneNear, 100u, 0.8f);
    spawn_emitter(
        "AudioToneMid", glm::vec3{-18.f, 3.f, -12.f}, game::audio::SoundId::ToneMid, 101u, 1.2f);
    spawn_emitter(
        "AudioToneFar", glm::vec3{35.f, 4.f, -50.f}, game::audio::SoundId::ToneFar, 102u, 1.6f);

    ctx.world->set<ecs::ControlMode>({ecs::ControlModeKind::ShipPilot});

    ctx.needs_shared_mesh = true;
    // ship + target + character + 3 emitters + projectiles
    ctx.instance_count = 6u + static_cast<u32>(game::flight::kProjectilePoolSize);

    log::log_info(
        log::LogCategory::Game,
        "ui_audio_test: HUD Both | Esc=pause | 3 positional tones | fire/thrust SFX");
    return true;
}

bool setup_save_load_test(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }

    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);

    game::save::ensure_singletons(*ctx.world);
    game::ui::ensure_singletons(*ctx.world);

    game::world::StarSystemData system{};
    (void)game::world::load_star_system_config(system, "assets/data/star_system.cfg");
    ctx.world->set<game::world::StarSystemData>(system);

    game::world::FloatingOrigin fo{};
    fo.threshold     = game::world::kFloatingOriginThreshold;
    fo.rebase_count  = 1; // non-zero so save/load can prove restore
    fo.origin_offset = glm::vec3{100.f, 0.f, -50.f};
    ctx.world->set<game::world::FloatingOrigin>(fo);

    (void)game::economy::load_economy_data(*ctx.world);

    game::flight::spawn_projectile_pool(*ctx.world);
    flecs::entity ship = game::flight::spawn_player_ship(
        *ctx.world, glm::vec3{12.f, 8.f, -30.f}, ctx.player_ship_id);
    game::economy::attach_cargo_hold_if_missing(ship);

    // Coasting in flight with modified hull / shield / power.
    if (game::flight::RigidBody6DOF* rb = ship.try_get_mut<game::flight::RigidBody6DOF>()) {
        rb->linear_vel = glm::vec3{0.f, 0.f, -18.f};
    }
    if (game::flight::FlightControl* fc = ship.try_get_mut<game::flight::FlightControl>()) {
        fc->coupled = false;
    }
    if (game::flight::ShipHull* hull = ship.try_get_mut<game::flight::ShipHull>()) {
        hull->hp = 720.f;
    }
    if (game::flight::ShieldGenerator* sh = ship.try_get_mut<game::flight::ShieldGenerator>()) {
        sh->current = 210.f;
    }
    if (game::flight::PowerPlant* pp = ship.try_get_mut<game::flight::PowerPlant>()) {
        pp->stored = 640.f;
    }

    // Cargo + wallet + reputation + active mission.
    if (game::economy::CargoHold* hold = ship.try_get_mut<game::economy::CargoHold>()) {
        const game::economy::CommodityTable* table =
            ctx.world->try_get<game::economy::CommodityTable>();
        if (table != nullptr) {
            (void)game::economy::cargo_add(*hold, *table, 1u, 5u);
            (void)game::economy::cargo_add(*hold, *table, 2u, 2u);
        }
    }
    ctx.world->set<game::economy::PlayerWallet>({750});
    {
        game::economy::FactionReputation rep{};
        rep.values[0] = 12.5f;
        rep.values[1] = -3.f;
        ctx.world->set<game::economy::FactionReputation>(rep);
    }
    if (game::economy::MissionActivePool* pool =
            ctx.world->try_get_mut<game::economy::MissionActivePool>()) {
        const game::economy::MissionTemplateTable* templates =
            ctx.world->try_get<game::economy::MissionTemplateTable>();
        game::economy::CompletedMissions* completed =
            ctx.world->try_get_mut<game::economy::CompletedMissions>();
        if (templates != nullptr && completed != nullptr) {
            (void)game::economy::mission_try_accept(*pool, *templates, 1u, *completed);
        }
    }

    // Character in EVA near the ship (world space — proves Health restore).
    (void)game::character::spawn_player_character(
        *ctx.world, glm::vec3{14.f, 7.5f, -28.f}, 0, ctx.player_suit_id);

    ctx.world->set<ecs::ControlMode>({ecs::ControlModeKind::ShipPilot});

    ctx.needs_shared_mesh = true;
    ctx.instance_count    = 3u + static_cast<u32>(game::flight::kProjectilePoolSize);

    log::log_info(
        log::LogCategory::Game,
        "save_load_test: F5=QuickSave F9=QuickLoad | Esc pause → Save/Load Slot0 | "
        "ship in flight + cargo + mission + modified reputation");
    return true;
}

bool setup_ship_hangar_test(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }

    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);

    (void)game::economy::load_economy_data(*ctx.world);
    ctx.world->set<game::economy::PlayerWallet>({200000});
    ctx.world->set<game::economy::FactionReputation>(game::economy::FactionReputation{});

    game::flight::spawn_projectile_pool(*ctx.world);

    // Parked ship: buying / switching at a kiosk reconfigures THIS entity in
    // place (P3-03) — same entity the hatch and pilot seat below stay bound to.
    flecs::entity ship = game::flight::spawn_player_ship(
        *ctx.world, glm::vec3{0.f, 1.6f, -6.f}, ctx.player_ship_id);
    if (game::flight::FlightControl* fc = ship.try_get_mut<game::flight::FlightControl>()) {
        fc->coupled = true;
    }
    game::flight::ship_ownership_init(
        *ctx.world,
        (ctx.player_ship_id != nullptr) ? ctx.player_ship_id
                                        : game::flight::kDefaultPlayerShipId);

    // On-foot player on a hangar deck with artificial gravity.
    constexpr glm::vec3 kDeck{0.f, 2.f, 0.f};
    (void)game::character::spawn_gravity_zone_box(
        *ctx.world,
        kDeck,
        glm::vec3{18.f, 6.f, 18.f},
        glm::vec3{0.f, -1.f, 0.f},
        game::character::kGravityDefault,
        "HangarGravity");
    {
        const ecs::Position pos{kDeck.x, 0.5f, kDeck.z};
        ctx.world->entity("HangarDeck")
            .set<ecs::Position>(pos)
            .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
            .set<ecs::Velocity>({0.f, 0.f, 0.f})
            .set<ecs::Scale>({10.f})
            .add<ecs::InstanceTag>();
    }
    (void)game::character::spawn_player_character(
        *ctx.world, glm::vec3{0.f, 0.9f, 5.f}, 0, ctx.player_suit_id);

    // Board + fly the (reconfigured) ship to feel the difference.
    (void)game::character::spawn_ship_hatch(
        *ctx.world, ship.id(), glm::vec3{0.f, 0.9f, 3.2f}, "Enter / exit ship");
    (void)game::character::spawn_pilot_seat(
        *ctx.world, ship.id(), glm::vec3{0.f, 0.9f, -1.5f}, "Pilot seat");

    // Dealer kiosks — walk the rank, press F (see console for buy/switch/sell).
    (void)game::flight::spawn_ship_dealer(
        *ctx.world, glm::vec3{-4.5f, 1.2f, 2.f}, "ship.fighter.wasp", 42000);
    (void)game::flight::spawn_ship_dealer(
        *ctx.world, glm::vec3{-1.5f, 1.2f, 2.f}, "ship.freighter.mule", 88000);
    (void)game::flight::spawn_ship_dealer(
        *ctx.world, glm::vec3{1.5f, 1.2f, 2.f}, "ship.explorer.pathfinder", 65000);
    (void)game::flight::spawn_ship_dealer(
        *ctx.world, glm::vec3{4.5f, 1.2f, 2.f}, "ship.heavy.bulwark", 120000);

    // P3-06: suit lockers behind the player — F equips that suit (free).
    (void)game::character::spawn_suit_locker(
        *ctx.world, glm::vec3{-3.f, 1.2f, 8.f}, "suit.eva.explorer");
    (void)game::character::spawn_suit_locker(
        *ctx.world, glm::vec3{0.f, 1.2f, 8.f}, "suit.armor.heavy");
    (void)game::character::spawn_suit_locker(
        *ctx.world, glm::vec3{3.f, 1.2f, 8.f}, "suit.light.scout");
    (void)game::character::spawn_suit_locker(
        *ctx.world, glm::vec3{6.f, 1.2f, 8.f}, "suit.flight.standard");

    ctx.world->set<ecs::ControlMode>({ecs::ControlModeKind::OnFoot});

    if (const char* smoke = std::getenv("CSC_HANGAR_SMOKE");
        smoke != nullptr && smoke[0] == '1') {
        (void)game::flight::hangar_smoke_test(*ctx.world);
    }
    if (const char* smoke = std::getenv("CSC_SUIT_SMOKE");
        smoke != nullptr && smoke[0] == '1') {
        (void)game::character::suit_smoke_test(*ctx.world);
    }

    ctx.needs_shared_mesh = true;
    // ship + player + deck + hatch + seat + 4 ship kiosks + 4 suit lockers + projectiles.
    ctx.instance_count = 16u + static_cast<u32>(game::flight::kProjectilePoolSize);

    log::log_info(
        log::LogCategory::Game,
        "ship_hangar_test: F on a kiosk to buy/switch/sell ships; F on a locker "
        "to change suit; F on the seat to fly (200000 cr to spend)");
    return true;
}

bool setup_content_smoke_test(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }

    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);

    // scene_setup_by_name already loaded the weapon / ship / suit / location
    // catalogs; the economy tables (commodities / markets / missions) need this.
    (void)game::economy::load_economy_data(*ctx.world);

    // P3-09: load-and-validate the whole data catalog through the real engine
    // loaders. Result also in the `CONTENT_SMOKE: PASS|FAIL` log line.
    (void)game::content::run_content_smoke_test(*ctx.world);

    ctx.needs_shared_mesh = false;
    ctx.instance_count    = 0;
    return true;
}

// --- P4-08: procedural_test — galaxy + generated system + streamed terrain ---

struct ProceduralState {
    game::world::TerrainStreamer     streamer{};
    game::world::PlanetTerrainParams params{};
    bool                             streamer_started = false;
    char                             planet_name[64]{};
    glm::vec3                        planet_pos{0.f};
    bool                             slot_used[vulkan::kMaxTerrainDrawChunks]{};
    vulkan::RendererState*           renderer = nullptr;
    const vulkan::DeviceState*       device   = nullptr;
};
ProceduralState g_proc{};

void proc_upload_cb(void* ctx, game::world::TerrainChunk& c)
{
    auto* p = static_cast<ProceduralState*>(ctx);
    if (p->renderer == nullptr || p->device == nullptr || c.vert_count == 0) {
        return;
    }
    u32 slot = vulkan::kMaxTerrainDrawChunks;
    for (u32 i = 0; i < vulkan::kMaxTerrainDrawChunks; ++i) {
        if (!p->slot_used[i]) {
            slot = i;
            break;
        }
    }
    if (slot == vulkan::kMaxTerrainDrawChunks) {
        return;
    }
    assets::MeshCpu m{};
    m.vertices     = c.verts;
    m.indices      = c.indices;
    m.vertex_count = c.vert_count;
    m.index_count  = c.index_count;
    if (vulkan::renderer_upload_terrain_chunk(*p->renderer, *p->device, slot, m)) {
        p->slot_used[slot] = true;
        c.gpu_handle       = slot;
        if (p->streamer.uploads_done < 12u) {  // enough to see it working, not spam
            log::log_info(
                log::LogCategory::Vulkan,
                "terrain chunk -> gpu slot %u (verts=%u idx=%u depth=%u)",
                slot, c.vert_count, c.index_count, c.lod_depth);
        }
    }
}

void proc_retire_cb(void* ctx, game::world::TerrainChunk& c)
{
    auto* p = static_cast<ProceduralState*>(ctx);
    if (p->renderer == nullptr || p->device == nullptr
        || c.gpu_handle >= vulkan::kMaxTerrainDrawChunks) {
        return;
    }
    vulkan::renderer_retire_terrain_chunk(*p->renderer, *p->device, c.gpu_handle);
    p->slot_used[c.gpu_handle] = false;
    c.gpu_handle               = 0xFFFFFFFFu;
}

void proc_on_frame(void* renderer, void* device, flecs::world& world, f32 /*dt*/)
{
    g_proc.renderer = static_cast<vulkan::RendererState*>(renderer);
    g_proc.device   = static_cast<const vulkan::DeviceState*>(device);

    if (!g_proc.streamer_started) {
        game::world::terrain_streamer_init(
            g_proc.streamer, g_proc.params, glm::vec3{0.f},
            &proc_upload_cb, &proc_retire_cb, &g_proc);
        g_proc.streamer_started = true;
    }

    // Current planet position (follows floating-origin rebases).
    if (g_proc.planet_name[0] != '\0') {
        const flecs::entity pe = world.lookup(g_proc.planet_name);
        if (pe.is_alive()) {
            if (const ecs::Position* pp = pe.try_get<ecs::Position>()) {
                g_proc.planet_pos = glm::vec3{pp->x, pp->y, pp->z};
            }
        }
    }

    glm::vec3 player{0.f};
    world.each([&](flecs::entity e, game::flight::RigidBody6DOF& rb) {
        if (e.has<game::flight::PlayerShip>()) {
            player = rb.position;
        }
    });

    glm::mat4 model(1.f);
    model[3] = glm::vec4(g_proc.planet_pos, 1.f);
    vulkan::renderer_set_terrain_model(*g_proc.renderer, model);

    game::world::terrain_streamer_update(g_proc.streamer, player - g_proc.planet_pos);
}

bool setup_procedural_test(SceneContext& ctx)
{
    if (ctx.world == nullptr) {
        return false;
    }

    ecs::world_spawn_default_camera(*ctx.world, ctx.aspect);
    ecs::world_spawn_default_grid(*ctx.world);
    (void)game::economy::load_economy_data(*ctx.world);

    game::world::GalaxyMap galaxy{};
    game::world::generate_galaxy(
        game::world::kDefaultGalaxySeed, game::world::kHomeSystemSeed, galaxy);
    u32 node = 0;
    if (ctx.galaxy_system != nullptr) {
        node = static_cast<u32>(std::strtoul(ctx.galaxy_system, nullptr, 0));
        if (node >= galaxy.count) {
            node = 0;
        }
    }
    galaxy.current = node;
    game::world::galaxy_log(galaxy);
    ctx.world->set<game::world::GalaxyMap>(galaxy);

    game::world::StarSystemData system{};
    csc::u64                    seed = game::world::kHomeSystemSeed;
    if (ctx.world_seed != nullptr) {
        seed = static_cast<csc::u64>(std::strtoull(ctx.world_seed, nullptr, 0));
        (void)game::world::generate_star_system(seed, system);
    } else if (node == 0) {
        (void)game::world::load_star_system_config(system, "assets/data/star_system.cfg");
    } else {
        seed = galaxy.systems[node].seed;
        (void)game::world::generate_star_system(seed, system);
    }

    (void)game::world::spawn_universe_test(*ctx.world, system, ctx.player_ship_id, seed);

    // Stream terrain for the first Planet body. Re-derive its terrain params
    // from the generator's per-body info (matched by position).
    game::world::StarSystemData      gen{};
    game::world::GeneratedSystemInfo info{};
    (void)game::world::generate_star_system(seed, gen, &info);

    // Reset (TerrainStreamer holds a thread/mutex — cannot be wholesale-assigned).
    if (g_proc.streamer_started) {
        game::world::terrain_streamer_shutdown(g_proc.streamer);
        g_proc.streamer_started = false;
    }
    g_proc.renderer       = nullptr;
    g_proc.device         = nullptr;
    g_proc.planet_name[0] = '\0';
    g_proc.planet_pos     = glm::vec3{0.f};
    g_proc.params         = game::world::PlanetTerrainParams{};
    for (bool& u : g_proc.slot_used) {
        u = false;
    }

    for (u32 i = 0; i < system.body_count; ++i) {
        if (system.bodies[i].type != game::world::CelestialBodyType::Planet) {
            continue;
        }
        std::snprintf(g_proc.planet_name, sizeof(g_proc.planet_name), "%s", system.bodies[i].name);
        g_proc.planet_pos = system.bodies[i].position;

        csc::u64 body_seed = seed ^ (static_cast<csc::u64>(i + 1) * 0x9E3779B97F4A7C15ull);
        bool     atmo      = true;
        for (u32 k = 0; k < gen.body_count; ++k) {
            if (gen.bodies[k].type == game::world::CelestialBodyType::Planet) {
                const glm::vec3 d = gen.bodies[k].position - system.bodies[i].position;
                if (glm::dot(d, d) < 1.f) {
                    body_seed = info.bodies[k].body_seed;
                    atmo      = info.bodies[k].has_atmosphere;
                    break;
                }
            }
        }
        g_proc.params = game::world::planet_terrain_params(
            body_seed, system.bodies[i].radius, atmo);
        log::log_info(
            log::LogCategory::Core,
            "procedural_test: streaming terrain for '%s' (r=%.0f, elev=%.1f) at (%.0f, %.0f, %.0f)",
            g_proc.planet_name, static_cast<double>(system.bodies[i].radius),
            static_cast<double>(g_proc.params.elevation_scale),
            static_cast<double>(g_proc.planet_pos.x),
            static_cast<double>(g_proc.planet_pos.y),
            static_cast<double>(g_proc.planet_pos.z));
        break;
    }

    ctx.on_frame    = &proc_on_frame;
    ctx.on_shutdown = [] {
        if (g_proc.streamer_started) {
            game::world::terrain_streamer_shutdown(g_proc.streamer);
            g_proc.streamer_started = false;
        }
    };

    ctx.needs_shared_mesh = true;
    ctx.instance_count    = 24u + static_cast<u32>(game::flight::kProjectilePoolSize);
    log::log_info(
        log::LogCategory::Game,
        "procedural_test: fly toward the planet — terrain LOD streams in; "
        "--system=<n> / --seed=<n> for other systems");
    return true;
}

constexpr SceneDesc kScenes[] = {
    {"grid_freelook",
     "Free-look camera + ground grid (minimal baseline)",
     &setup_grid_freelook},
    {"instancing_stress",
     ">=500 shared-mesh instances in one draw + grid + camera",
     &setup_instancing_stress},
    {"mesh_viewer",
     "Single centered cube mesh + free-look (glTF upload check)",
     &setup_mesh_viewer},
    {"flight_test",
     "Pilotable ship + thrusters/shields/weapons + damage target (P1A)",
     &setup_flight_test},
    {"on_foot_test",
     "Ship interior → EVA → station gravity + FPS combat (P1B)",
     &setup_on_foot_test},
    {"crew_turret_test",
     "NPC crew + AI turrets + subsystem damage (P2-01/02/03)",
     &setup_crew_turret_test},
    {"npc_combat_test",
     "On-foot pirates: patrol/alert/combat/cover/flee via shared AI (P2-06)",
     &setup_npc_combat_test},
    {"economy_test",
     "Buy ore@A → travel B → sell margin + delivery mission (P1C)",
     &setup_economy_test},
    {"universe_test",
     "Fixed star system: station ↔ open space (rebase) ↔ planetary LZ (P1D)",
     &setup_universe_test},
    {"ui_audio_test",
     "Flight+on-foot HUD, pause menus, ≥3 positional tones (P1E)",
     &setup_ui_audio_test},
    {"save_load_test",
     "Persist ship/cargo/mission/rep; F5 save F9 load (P1F)",
     &setup_save_load_test},
    {"ship_hangar_test",
     "Buy / switch / sell player ships at a station hangar (P3-03)",
     &setup_ship_hangar_test},
    {"content_smoke_test",
     "P3-09 — load the full data catalog + integrity check (CONTENT_SMOKE)",
     &setup_content_smoke_test},
    {"procedural_test",
     "P4-08 — galaxy + generated system + LOD-streamed planetary terrain",
     &setup_procedural_test},
};

constexpr std::size_t kSceneCount = sizeof(kScenes) / sizeof(kScenes[0]);

}  // namespace

const SceneDesc* scene_find(const char* name)
{
    if (name == nullptr || name[0] == '\0') {
        return nullptr;
    }
    for (std::size_t i = 0; i < kSceneCount; ++i) {
        if (std::strcmp(kScenes[i].name, name) == 0) {
            return &kScenes[i];
        }
    }
    return nullptr;
}

void scene_list(char* out, std::size_t cap)
{
    if (out == nullptr || cap == 0) {
        return;
    }
    out[0] = '\0';
    std::size_t used = 0;
    for (std::size_t i = 0; i < kSceneCount; ++i) {
        const int n = std::snprintf(
            out + used,
            (used < cap) ? (cap - used) : 0,
            "%s%s",
            (i == 0) ? "" : ", ",
            kScenes[i].name);
        if (n < 0) {
            break;
        }
        used += static_cast<std::size_t>(n);
        if (used >= cap) {
            out[cap - 1] = '\0';
            break;
        }
    }
}

bool scene_setup_by_name(const char* name, SceneContext& ctx)
{
    const SceneDesc* desc = scene_find(name);
    if (desc == nullptr) {
        char available[256]{};
        scene_list(available, sizeof(available));
        log::log_warn(
            log::LogCategory::Core,
            "Unknown scene '%s' — available: [%s]. Falling back to grid_freelook.",
            (name != nullptr) ? name : "(null)",
            available);
        desc = scene_find("grid_freelook");
    }
    if (desc == nullptr || desc->setup == nullptr) {
        log::log_error(log::LogCategory::Core, "Scene registry missing grid_freelook.");
        return false;
    }
    log::log_info(
        log::LogCategory::Core,
        "Scene setup: %s — %s",
        desc->name,
        desc->description);

    // Content catalogs must be live before any scene spawns a ship or turret.
    // Weapons first so the ship↔weapon reference check (P3-05) can run.
    if (ctx.world != nullptr) {
        (void)game::flight::load_weapon_catalog(*ctx.world);    // P3-05
        (void)game::flight::load_ship_catalog(*ctx.world);      // P3-01
        (void)game::flight::validate_ship_weapon_refs(*ctx.world);
        (void)game::economy::load_location_catalog(*ctx.world); // P3-04
        (void)game::character::load_suit_catalog(*ctx.world);   // P3-06
        (void)game::world::load_poi_catalog(*ctx.world);        // P4-07
    }

    return desc->setup(ctx);
}

}  // namespace csc::scene
