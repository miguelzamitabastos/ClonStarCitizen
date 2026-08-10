#pragma once

#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"
#include "game/economy/economy.hpp"

#include <flecs.h>

namespace csc::game::ai {

// -----------------------------------------------------------------------------
// P2-11 — SHARED AI framework (arquitectura obligatoria, ver 08-FASE-2 §Restricciones):
// esta es la ÚNICA máquina de decisión del juego. Torretas (P2-03), NPCs a pie
// (P2-06) y naves NPC (P2-12) consumen `AiAgent.state` / `AiAgent.target` y hacen
// SOLO actuación en sus propios sistemas (TurretMount / CharacterController /
// RigidBody6DOF). Si un contexto necesita algo nuevo: extender aquí, no bifurcar.
// -----------------------------------------------------------------------------

// --- Tunables ------------------------------------------------------------------
inline constexpr u32 kMaxTargetCandidates   = 32;
inline constexpr f32 kHostileRepThreshold   = -10.f; ///< rep[faction] < esto → hostil
inline constexpr f32 kDefaultDetectionRange = 90.f;
inline constexpr f32 kDefaultAttackRange    = 55.f;
inline constexpr f32 kDefaultAlertDwell     = 1.2f;  ///< s en Alert antes de Combat
inline constexpr f32 kDefaultLoseTargetTime = 4.f;   ///< s sin ver → degradar estado
inline constexpr f32 kMinSensorRangeFactor  = 0.3f;  ///< SEN destruido no ciega del todo
inline constexpr u32 kPirateFaction         = 3;     ///< facción sin ley (hostil a todos)
inline constexpr u32 kSecurityFaction       = 1;     ///< patrullas (hostil a piratas)

// --- Components (POD) ----------------------------------------------------------

enum class AiState : u8 {
    Patrol = 0,
    Alert  = 1,
    Combat = 2,
    Flee   = 3,
};

/// Pertenencia a facción de un NPC / torreta / nave (el jugador NO lleva esto;
/// su relación con cada facción vive en economy::FactionReputation).
struct FactionMember {
    u32 faction_id = 0;
};

/// Estado de decisión compartido. La actuación vive en cada contexto.
struct AiAgent {
    AiState         state                = AiState::Patrol;
    flecs::entity_t target               = 0;
    f32             detection_range      = kDefaultDetectionRange;
    f32             attack_range         = kDefaultAttackRange;
    f32             flee_health_fraction = 0.f; ///< 0 = nunca huye (torretas)
    f32             state_time           = 0.f;
    f32             alert_dwell          = kDefaultAlertDwell;
    f32             lose_target_time     = kDefaultLoseTargetTime;
    f32             time_since_seen      = 0.f;
};

/// Tag: entidad que la IA puede considerar objetivo aunque no sea el jugador
/// (ej. nave escoltada en P2-10). Requiere Position + FactionMember opcional.
struct AiThreatTarget {};

/// Opcional: la percepción usa los sensores (P2-02) de otra entidad anfitriona
/// (torreta → nave que la monta). Sin esto se leen los ShipSubsystems propios.
struct SensorLink {
    flecs::entity_t host = 0;
};

/// Snapshot de un candidato a objetivo (buffer fijo, cero heap).
struct TargetCandidate {
    flecs::entity_t id          = 0;
    glm::vec3       position{0.f};
    u32             faction_id  = 0;    ///< facción del candidato
    bool            player_side = true; ///< jugador o aliado del jugador
};

// --- Pure decision helpers -------------------------------------------------------

/// Hostilidad de una facción hacia el JUGADOR según reputación (DoD Fase 2:
/// bajar rep provoca hostilidad en todos los contextos con esta única función).
[[nodiscard]] bool faction_hostile_to_player(
    const economy::FactionReputation& rep, u32 faction_id);

/// Hostilidad facción↔facción para NPC vs NPC (piratas vs seguridad).
[[nodiscard]] bool factions_mutually_hostile(u32 a, u32 b);

/// Selección de objetivo: hostil más cercano dentro de `range`. Devuelve 0 si nada.
[[nodiscard]] flecs::entity_t select_target(
    flecs::entity_t                   self,
    const glm::vec3&                  self_pos,
    u32                               self_faction,
    const TargetCandidate*            candidates,
    u32                               candidate_count,
    f32                               range,
    const economy::FactionReputation& player_rep,
    f32&                              out_distance);

/// Paso puro de la FSM Patrol/Alert/Combat/Flee.
void agent_step(
    AiAgent& agent,
    bool     target_valid,
    f32      target_distance,
    f32      own_health_fraction,
    f32      dt);

// --- Systems / API ---------------------------------------------------------------

void register_systems(flecs::world& world);

/// Fixed-step de decisión: percepción (rango, SEN de P2-02), selección de objetivo
/// y FSM para todos los AiAgent. Correr ANTES de la actuación de cada contexto.
void fixed_step(flecs::world& world, f32 dt);

/// Recolecta candidatos (nave jugador, personaje jugador, AiThreatTarget y
/// NPCs con FactionMember) en un buffer fijo. Devuelve nº escrito.
[[nodiscard]] u32 collect_target_candidates(
    flecs::world& world, TargetCandidate* out, u32 capacity);

/// Telemetría debug: nº agentes por estado (HUD / overlay).
struct AiTelemetry {
    u32 patrol = 0;
    u32 alert  = 0;
    u32 combat = 0;
    u32 flee   = 0;
};

void fill_ai_telemetry(flecs::world& world, AiTelemetry& out);

}  // namespace csc::game::ai
