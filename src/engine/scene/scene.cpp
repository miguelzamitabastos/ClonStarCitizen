#include "engine/scene/scene.hpp"

#include "engine/log/log.hpp"
#include "game/ai/ai.hpp"
#include "game/audio/audio.hpp"
#include "game/character/character.hpp"
#include "game/economy/economy.hpp"
#include "game/flight/flight.hpp"
#include "game/flight/ship_catalog.hpp"
#include "game/save/save.hpp"
#include "game/ui/ui.hpp"
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
        *ctx.world, glm::vec3{0.f, 0.9f, 0.f}, ship.id());

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
        *ctx.world, glm::vec3{0.f, 0.9f, 8.f}, 0);

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

    game::world::StarSystemData system{};
    (void)game::world::load_star_system_config(system, "assets/data/star_system.cfg");

    (void)game::world::spawn_universe_test(*ctx.world, system, ctx.player_ship_id);

    ctx.needs_shared_mesh = true;
    // Station + star + planet + ship + streamed props (when loaded) + projectiles.
    ctx.instance_count = 16u + static_cast<u32>(game::flight::kProjectilePoolSize);
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
        *ctx.world, glm::vec3{2.f, 1.f, 4.f}, 0);

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
        *ctx.world, glm::vec3{14.f, 7.5f, -28.f}, 0);

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
        *ctx.world, glm::vec3{0.f, 0.9f, 5.f}, 0);

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

    ctx.world->set<ecs::ControlMode>({ecs::ControlModeKind::OnFoot});

    if (const char* smoke = std::getenv("CSC_HANGAR_SMOKE");
        smoke != nullptr && smoke[0] == '1') {
        (void)game::flight::hangar_smoke_test(*ctx.world);
    }

    ctx.needs_shared_mesh = true;
    // ship + player + deck + hatch + seat + 4 kiosks + projectiles.
    ctx.instance_count = 12u + static_cast<u32>(game::flight::kProjectilePoolSize);

    log::log_info(
        log::LogCategory::Game,
        "ship_hangar_test: F on a kiosk to buy/switch/sell; F on the seat to fly "
        "(200000 cr to spend)");
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

    // P3-01: ship catalog must be live before any scene spawns a ship.
    if (ctx.world != nullptr) {
        (void)game::flight::load_ship_catalog(*ctx.world);
    }

    return desc->setup(ctx);
}

}  // namespace csc::scene
