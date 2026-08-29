#include "game/ai/ai.hpp"

#include "engine/ecs/world.hpp"
#include "engine/log/log.hpp"
#include "game/character/character.hpp"
#include "game/flight/flight.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace csc::game::ai {
namespace {

[[nodiscard]] glm::vec3 entity_world_pos(flecs::entity e)
{
    if (const flight::RigidBody6DOF* rb = e.try_get<flight::RigidBody6DOF>()) {
        return rb->position;
    }
    if (const ecs::Position* p = e.try_get<ecs::Position>()) {
        return glm::vec3{p->x, p->y, p->z};
    }
    return glm::vec3{0.f};
}

/// [0,1] health fraction: ShipHull para naves/torretas, Health para personajes.
[[nodiscard]] f32 entity_health_fraction(flecs::entity e)
{
    if (const flight::ShipHull* hull = e.try_get<flight::ShipHull>()) {
        return (hull->max_hp > 1e-3f) ? std::max(0.f, hull->hp / hull->max_hp) : 0.f;
    }
    if (const character::Health* hp = e.try_get<character::Health>()) {
        return (hp->max_hp > 1e-3f) ? std::max(0.f, hp->hp / hp->max_hp) : 0.f;
    }
    return 1.f;
}

/// Rango efectivo de detección: sensores P2-02 propios o del host (SensorLink).
[[nodiscard]] f32 effective_detection_range(flecs::world& world, flecs::entity e, f32 base)
{
    const flight::ShipSubsystems* subs = e.try_get<flight::ShipSubsystems>();
    if (subs == nullptr) {
        if (const SensorLink* link = e.try_get<SensorLink>()) {
            if (link->host != 0) {
                flecs::entity host = world.entity(link->host);
                if (host.is_alive()) {
                    subs = host.try_get<flight::ShipSubsystems>();
                }
            }
        }
    }
    if (subs == nullptr) {
        return base;
    }
    const f32 sen =
        flight::subsystem_efficiency(*subs, combat::Subsystem::Sensors);
    return base * std::max(kMinSensorRangeFactor, sen);
}

[[nodiscard]] bool entity_disabled(flecs::entity e)
{
    return e.has<flight::Destroyed>() || e.has<character::CharacterDead>();
}

void enter_state(AiAgent& agent, AiState next)
{
    agent.state           = next;
    agent.state_time      = 0.f;
    agent.time_since_seen = 0.f;
}

// --- P2-12: random encounters --------------------------------------------------

[[nodiscard]] bool find_player_ship_pos(flecs::world& world, glm::vec3& out_pos)
{
    bool found = false;
    world.each([&](flecs::entity e, const flight::RigidBody6DOF& rb) {
        if (found || !e.has<flight::PlayerShip>()) {
            return;
        }
        out_pos = rb.position;
        found   = true;
    });
    return found;
}

/// Strips a slot's ship+turret back to inert/invisible without destroying
/// them — restores full health/shield/subsystems so the next activation
/// starts fresh, exactly like a freshly-loaded pool entry would.
void dormant_encounter_slot(flecs::world& world, EncounterSlot& slot)
{
    if (slot.ship != 0) {
        flecs::entity ship = world.entity(slot.ship);
        if (ship.is_alive()) {
            if (flight::ShipHull* hull = ship.try_get_mut<flight::ShipHull>()) {
                hull->hp = hull->max_hp;
            }
            if (flight::ShieldGenerator* shield = ship.try_get_mut<flight::ShieldGenerator>()) {
                shield->current = shield->max_capacity;
            }
            if (flight::ShipSubsystems* subs = ship.try_get_mut<flight::ShipSubsystems>()) {
                for (flight::SubsystemHealth& bank : subs->items) {
                    bank.hp = bank.max_hp;
                }
            }
            ship.remove<flight::Destroyed>();
            ship.remove<AiAgent>();
            ship.remove<ecs::InstanceTag>();
        }
    }
    if (slot.turret != 0) {
        flecs::entity turret = world.entity(slot.turret);
        if (turret.is_alive()) {
            turret.remove<AiAgent>();
            turret.remove<ecs::InstanceTag>();
        }
    }
    slot.active = false;
}

/// Brings a dormant slot to life at `pos` with `faction_id` — re-adds the
/// AiAgent components dormant_encounter_slot() stripped, nothing more.
void activate_encounter_slot(
    flecs::world& world, EncounterSlot& slot, const glm::vec3& pos, u32 faction_id)
{
    flecs::entity ship = world.entity(slot.ship);
    if (!ship.is_alive()) {
        return;
    }
    if (flight::RigidBody6DOF* rb = ship.try_get_mut<flight::RigidBody6DOF>()) {
        rb->position    = pos;
        rb->linear_vel  = glm::vec3{0.f};
        rb->angular_vel = glm::vec3{0.f};
        rb->orientation = glm::quat{1.f, 0.f, 0.f, 0.f};
    }
    ship.set<ecs::Position>({pos.x, pos.y, pos.z});
    ship.set<ecs::PreviousPosition>({pos.x, pos.y, pos.z});
    ship.set<ai::FactionMember>({faction_id});
    AiAgent ship_agent{};
    ship.set<AiAgent>(ship_agent);
    ship.add<ecs::InstanceTag>();

    if (slot.turret != 0) {
        flecs::entity turret = world.entity(slot.turret);
        if (turret.is_alive()) {
            turret.set<ai::FactionMember>({faction_id});
            AiAgent turret_agent{};
            turret_agent.detection_range = 400.f;
            turret_agent.attack_range    = 350.f;
            turret.set<AiAgent>(turret_agent);
            turret.add<ecs::InstanceTag>();
        }
    }
    slot.active = true;
}

void step_random_encounters(flecs::world& world, f32 dt)
{
    EncounterPool* pool = world.try_get_mut<EncounterPool>();
    if (pool == nullptr) {
        return;
    }

    glm::vec3  player_pos{};
    const bool have_player = find_player_ship_pos(world, player_pos);

    // Despawn: destroyed, or too far from the player to matter.
    for (EncounterSlot& slot : pool->slots) {
        if (!slot.active || slot.ship == 0) {
            continue;
        }
        flecs::entity ship = world.entity(slot.ship);
        if (!ship.is_alive()) {
            slot.active = false;
            continue;
        }
        const bool destroyed = ship.has<flight::Destroyed>();
        bool       too_far   = false;
        if (have_player) {
            if (const flight::RigidBody6DOF* rb = ship.try_get<flight::RigidBody6DOF>()) {
                too_far = glm::length(rb->position - player_pos) > kEncounterDespawnDist;
            }
        }
        if (destroyed || too_far) {
            dormant_encounter_slot(world, slot);
            log::log_info(
                log::LogCategory::Game,
                "Encounter despawned (ship=%llu reason=%s)",
                static_cast<unsigned long long>(slot.ship),
                destroyed ? "destroyed" : "distance");
        }
    }

    if (!have_player) {
        return;
    }

    // Background cadence — not per frame, matches P2-08's EconomyClock pattern.
    pool->check_accum += dt;
    if (pool->check_accum < kEncounterCheckSeconds) {
        return;
    }
    pool->check_accum -= kEncounterCheckSeconds;

    u32 free_idx = kMaxEncounterSlots;
    for (u32 i = 0; i < kMaxEncounterSlots; ++i) {
        if (!pool->slots[i].active) {
            free_idx = i;
            break;
        }
    }
    if (free_idx == kMaxEncounterSlots) {
        return; // pool full — never spawn outside it
    }

    // `% N` binds tighter than `>>` in C++ — parenthesize the shift or the
    // modulo silently applies to the shift amount (8), not the LCG output.
    const f32 roll =
        static_cast<f32>((combat::rng_next(pool->rng_state) >> 8) % 10000u) / 10000.f;
    if (roll >= kEncounterSpawnChance) {
        return;
    }

    const f32 angle =
        static_cast<f32>((combat::rng_next(pool->rng_state) >> 8) % 6283u) / 1000.f; // ~[0, 2π)
    const f32 dist_unit =
        static_cast<f32>((combat::rng_next(pool->rng_state) >> 8) % 10000u) / 10000.f;
    const f32 dist = kEncounterMinSpawnDist + dist_unit * (kEncounterMaxSpawnDist - kEncounterMinSpawnDist);
    const glm::vec3 spawn_pos =
        player_pos + glm::vec3{std::cos(angle) * dist, 0.f, std::sin(angle) * dist};

    const bool pirate  = ((combat::rng_next(pool->rng_state) >> 8) & 1u) == 0u;
    const u32  faction = pirate ? kPirateFaction : kSecurityFaction;

    activate_encounter_slot(world, pool->slots[free_idx], spawn_pos, faction);
    log::log_info(
        log::LogCategory::Game,
        "Random encounter: %s at (%.0f, %.0f, %.0f) dist=%.0f slot=%u",
        pirate ? "pirates" : "security patrol",
        static_cast<double>(spawn_pos.x),
        static_cast<double>(spawn_pos.y),
        static_cast<double>(spawn_pos.z),
        static_cast<double>(dist),
        free_idx);
}

}  // namespace

bool faction_hostile_to_player(const economy::FactionReputation& rep, u32 faction_id)
{
    if (faction_id == kPirateFaction) {
        return true;
    }
    if (faction_id >= economy::kNumFactions) {
        return false;
    }
    return rep.values[faction_id] < kHostileRepThreshold;
}

bool factions_mutually_hostile(u32 a, u32 b)
{
    // Piratas contra todos; nadie más se pelea entre facciones (por ahora).
    return (a == kPirateFaction) != (b == kPirateFaction);
}

flecs::entity_t select_target(
    flecs::entity_t                   self,
    const glm::vec3&                  self_pos,
    u32                               self_faction,
    const TargetCandidate*            candidates,
    u32                               candidate_count,
    f32                               range,
    const economy::FactionReputation& player_rep,
    f32&                              out_distance)
{
    flecs::entity_t best      = 0;
    f32             best_dist = range;
    for (u32 i = 0; i < candidate_count; ++i) {
        const TargetCandidate& c = candidates[i];
        if (c.id == 0 || c.id == self) {
            continue;
        }
        const bool hostile = c.player_side
            ? faction_hostile_to_player(player_rep, self_faction)
            : factions_mutually_hostile(self_faction, c.faction_id);
        if (!hostile) {
            continue;
        }
        const f32 dist = glm::length(c.position - self_pos);
        if (dist < best_dist) {
            best_dist = dist;
            best      = c.id;
        }
    }
    out_distance = best_dist;
    return best;
}

void agent_step(
    AiAgent& agent, bool target_valid, f32 target_distance, f32 own_health_fraction, f32 dt)
{
    (void)target_distance;
    agent.state_time += dt;
    if (target_valid) {
        agent.time_since_seen = 0.f;
    } else {
        agent.time_since_seen += dt;
    }

    const bool should_flee = agent.flee_health_fraction > 0.f
        && own_health_fraction < agent.flee_health_fraction;

    switch (agent.state) {
    case AiState::Patrol:
        if (should_flee) {
            enter_state(agent, AiState::Flee);
        } else if (target_valid) {
            enter_state(agent, AiState::Alert);
        }
        break;
    case AiState::Alert:
        if (should_flee) {
            enter_state(agent, AiState::Flee);
        } else if (!target_valid && agent.time_since_seen > agent.lose_target_time) {
            enter_state(agent, AiState::Patrol);
        } else if (target_valid && agent.state_time >= agent.alert_dwell) {
            enter_state(agent, AiState::Combat);
        }
        break;
    case AiState::Combat:
        if (should_flee) {
            enter_state(agent, AiState::Flee);
        } else if (!target_valid && agent.time_since_seen > agent.lose_target_time) {
            enter_state(agent, AiState::Alert);
        }
        break;
    case AiState::Flee:
        // Estado absorbente hasta perder al objetivo un buen rato (no se cura solo).
        if (agent.time_since_seen > agent.lose_target_time * 2.f) {
            enter_state(agent, AiState::Patrol);
        }
        break;
    }
}

u32 collect_target_candidates(flecs::world& world, TargetCandidate* out, u32 capacity)
{
    u32 n = 0;

    // Nave del jugador.
    world.each([&](flecs::entity e, const flight::RigidBody6DOF& rb) {
        if (n >= capacity || !e.has<flight::PlayerShip>() || entity_disabled(e)) {
            return;
        }
        out[n] = TargetCandidate{e.id(), rb.position, 0u, true};
        ++n;
    });

    // Personaje del jugador (Position mundial ya sincronizada aunque haya LocalToShip).
    world.each([&](flecs::entity e, const character::CharacterController&,
                   const ecs::Position& p) {
        if (n >= capacity || !e.has<character::PlayerCharacter>() || entity_disabled(e)) {
            return;
        }
        out[n] = TargetCandidate{e.id(), glm::vec3{p.x, p.y, p.z}, 0u, true};
        ++n;
    });

    // Objetivos marcados (escoltas P2-10) — lado del jugador.
    // AiThreatTarget es un tag vacío: pedirlo POR REFERENCIA como término de
    // query revienta flecs (entity_index.c assert) — mismo gotcha ya pisado
    // con CharacterDead/PlayerCharacter (ver character.cpp, P2-07). Se
    // consulta por Position (el único componente que el tag garantiza, según
    // su propio comentario) y se filtra con has<>() en el cuerpo.
    world.each([&](flecs::entity e, const ecs::Position& p) {
        if (n >= capacity || !e.has<AiThreatTarget>() || entity_disabled(e)) {
            return;
        }
        u32 faction = 0;
        if (const FactionMember* fm = e.try_get<FactionMember>()) {
            faction = fm->faction_id;
        }
        out[n] = TargetCandidate{e.id(), glm::vec3{p.x, p.y, p.z}, faction, true};
        ++n;
    });

    // NPCs con facción (para hostilidad NPC↔NPC: seguridad vs piratas).
    world.each([&](flecs::entity e, const FactionMember& fm, const AiAgent&,
                   const ecs::Position& p) {
        if (n >= capacity || entity_disabled(e)) {
            return;
        }
        out[n] = TargetCandidate{e.id(), glm::vec3{p.x, p.y, p.z}, fm.faction_id, false};
        ++n;
    });

    return n;
}

void fixed_step(flecs::world& world, f32 dt)
{
    if (dt <= 0.f) {
        return;
    }

    // P2-12: activate/deactivate pooled encounters first, so anything spawned
    // this tick is already a valid decision-maker/candidate below.
    step_random_encounters(world, dt);

    TargetCandidate candidates[kMaxTargetCandidates]{};
    const u32 candidate_count =
        collect_target_candidates(world, candidates, kMaxTargetCandidates);

    economy::FactionReputation rep{};
    if (const economy::FactionReputation* r = world.try_get<economy::FactionReputation>()) {
        rep = *r;
    }

    world.each([&](flecs::entity e, AiAgent& agent) {
        if (entity_disabled(e)) {
            agent.target = 0;
            return;
        }

        u32 faction = 0;
        if (const FactionMember* fm = e.try_get<FactionMember>()) {
            faction = fm->faction_id;
        }

        const glm::vec3 self_pos = entity_world_pos(e);
        const f32       range =
            effective_detection_range(world, e, agent.detection_range);

        f32 dist = 0.f;
        const flecs::entity_t target = select_target(
            e.id(), self_pos, faction, candidates, candidate_count, range, rep, dist);

        agent.target = target;
        agent_step(agent, target != 0, dist, entity_health_fraction(e), dt);
    });
}

void register_systems(flecs::world& world)
{
    if (world.try_get<economy::FactionReputation>() == nullptr) {
        world.set<economy::FactionReputation>(economy::FactionReputation{});
    }
}

void spawn_encounter_pool(flecs::world& world)
{
    EncounterPool pool{};
    char name_buf[32]{};
    for (u32 i = 0; i < kMaxEncounterSlots; ++i) {
        std::snprintf(name_buf, sizeof(name_buf), "EncounterShip%u", i);
        flecs::entity ship = flight::spawn_npc_ship(world, glm::vec3{0.f}, 0, name_buf);

        std::snprintf(name_buf, sizeof(name_buf), "EncounterTurret%u", i);
        flecs::entity turret =
            flight::spawn_turret(world, ship.id(), glm::vec3{0.f, 1.5f, 0.f}, 0, false, name_buf);

        pool.slots[i].ship   = ship.id();
        pool.slots[i].turret = turret.id();
        // Freshly spawned = "active" per spawn_npc_ship/spawn_turret's own
        // defaults; immediately dormant it so it starts invisible/inert like
        // every other pool in this codebase (spawn_projectile_pool included).
        dormant_encounter_slot(world, pool.slots[i]);
    }
    world.set<EncounterPool>(pool);

    log::log_info(
        log::LogCategory::Game,
        "Encounter pool ready: %u dormant ship+turret pairs",
        kMaxEncounterSlots);
}

void fill_ai_telemetry(flecs::world& world, AiTelemetry& out)
{
    out = AiTelemetry{};
    world.each([&](flecs::entity e, const AiAgent& agent) {
        if (entity_disabled(e)) {
            return;
        }
        switch (agent.state) {
        case AiState::Patrol:
            ++out.patrol;
            break;
        case AiState::Alert:
            ++out.alert;
            break;
        case AiState::Combat:
            ++out.combat;
            break;
        case AiState::Flee:
            ++out.flee;
            break;
        }
    });
}

}  // namespace csc::game::ai
