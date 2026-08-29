#include "game/ai/ai.hpp"

#include "engine/ecs/world.hpp"
#include "game/character/character.hpp"
#include "game/flight/flight.hpp"

#include <algorithm>
#include <cmath>

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
