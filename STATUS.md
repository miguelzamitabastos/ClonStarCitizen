# STATUS

## Fase activa: Fase 1 — Vertical Slice — **EN PROGRESO**

## Progreso por documento
- P0 Cimientos del Motor: [#############] 13/13 tareas — COMPLETADA
- P1A Vuelo y Naves:      [##########] 10/10 tareas — COMPLETADA
  - [x] P1A-01 Componentes ECS de nave (RigidBody6DOF, ShipHull, ThrusterSet, …)
  - [x] P1A-02 Integrador 6DOF fixed-dt (semi-implicit Euler)
  - [x] P1A-03 Input → FlightControl → thruster activations (función pura)
  - [x] P1A-04 Modo acoplado/desacoplado (toggle Left Alt)
  - [x] P1A-05 Cámara chase/cockpit (ControlMode::ShipPilot)
  - [x] P1A-06 PowerPlant distribución por prioridades
  - [x] P1A-07 Escudos (regen + absorción antes que casco)
  - [x] P1A-08 Armas fijas (Pool proyectiles + InstanceTag)
  - [x] P1A-09 Daño casco + tag Destroyed (sin delete mid-iteration)
  - [x] P1A-10 Escena `flight_test` + HUD telemetría
- P1B A pie y FPS:        [#########] 9/9  tareas — COMPLETADA
  - [x] P1B-01 Componentes ECS de personaje (CharacterController, Health, EquippedItem, …)
  - [x] P1B-02 Locomoción kinemática (cápsula) en gravedad de superficie
  - [x] P1B-03 Zonas de gravedad (GravityZone) + transición EVA
  - [x] P1B-04 Cámara 1ª/3ª persona (ControlMode::OnFoot; Alt toggle 3ª)
  - [x] P1B-05 Entrar/salir nave (PilotSeat ↔ ShipPilot/OnFoot; Health persiste)
  - [x] P1B-06 Combate FPS (EquippedItem → DamageEvent compartido → Health/CharacterDead)
  - [x] P1B-07 Interacción raycast + Interactable / InteractEventQueue
  - [x] P1B-08 Interior nave: LocalToShip (sim local; world Pose solo render)
  - [x] P1B-09 Escena `on_foot_test` (interior → EVA → estación)
- P1C Economía/Misiones:  [########] 8/8  tareas — COMPLETADA (sin P1C-08; serialización → 1F)
  - [x] P1C-01 Commodity/Market tablas desde `assets/data/*.cfg` (fixed arrays)
  - [x] P1C-02 CargoHold slots fijos + volume/mass capacity
  - [x] P1C-03 buy/sell puros + precio oferta/demanda `f(base, stock)`
  - [x] P1C-04 MissionTemplate table + generador LCG al aceptar
  - [x] P1C-05 MissionActive Pool + accept/complete + recompensa
  - [x] P1C-06 NPCs TradeOffer/Dialogue/TravelPad vía Interactable
  - [x] P1C-07 FactionReputation float[kNumFactions]
  - [x] P1C-09 Escena `economy_test` (buy A → travel B → sell/turn-in)
- P1D Universo fijo:      [########] 8/8  tareas — COMPLETADA (P1D-04 naming parcial)
  - [x] P1D-01 Esquema coordenadas: **floating origin** (f32 relativo; sin f64 mundo)
  - [x] P1D-07 Rebase FloatingOrigin cuando |player| > threshold
  - [x] P1D-02 StarSystemData desde `assets/data/star_system.cfg`
  - [x] P1D-04 Esqueleto + placeholders (naming final bloqueado)
  - [x] P1D-03 LevelStreamTrigger proximidad (InteriorLoaded / InstanceTag soft)
  - [x] P1D-05 Estación como interior navegable (StationRoot + LocalToShip / GravityZone)
  - [x] P1D-06 Transición espacio → zona aterrizaje prefab (Planeta-01-LZ)
  - [x] P1D-08 Escena `universe_test` + overlay rebase_count
- P1E UI/HUD/Audio:       [.......] 0/7  tareas
- P1F Persistencia:       [......] 0/6  tareas

## Escenas demo
| Escena | Estado |
|---|---|
| `grid_freelook` / `instancing_stress` / `mesh_viewer` | Fase 0 OK |
| `flight_test` | P1A OK — nave + objetivo a 50m |
| `on_foot_test` | P1B OK — interior LocalToShip → hatch EVA → estación + FPS target |
| `economy_test` | P1C OK — MarketA cheap ore → TravelPad → MarketB sell/turn-in |
| `universe_test` | P1D OK — Estacion-Alfa → espacio (rebase ≥1) → Planeta-01-LZ |
| `ui_audio_test` | pendiente P1E-07 |
| `save_load_test` | pendiente P1F-06 |

## Bloqueado (requiere decisión de Miguel)
- P1D-04 naming final del sistema estelar (placeholders OK: Sistema-01 / Estacion-Alfa / Planeta-01).
  Esqueleto técnico + cfg + escena listos; solo falta identidad/lore/nombres finales.

## Contrato floating origin (P1D-01 / P1D-07) — Fase 4 / Fase 5 dependen de esto

**Decisión elegida: floating origin (f32 relativo).** Alternativa f64 world-pos **rechazada** — no mezclar.

| Campo | Valor |
|---|---|
| Esquema | f32 relative to current origin; absolute ≈ relative + `FloatingOrigin.origin_offset` |
| Umbral de rebase | **2000 m** (`kFloatingOriginThreshold` / `FloatingOrigin.threshold`) |
| Disparo | `\|player_pos_relative\| > threshold` (nave `RigidBody6DOF` o personaje world / LocalToShip→world) |
| Acción | Una pasada O(n): resta `player_pos` de Position, PreviousPosition, RigidBody6DOF.position, GravityZone.center, LevelStreamTrigger.center, Camera3D eye/target; `origin_offset += delta`; `rebase_count++` |
| LocalToShip | `local_position` **no** se desplaza (espacio nave); caches world se re-sincronizan |
| Cuándo | Solo en `game::world::fixed_step` (vía `game::fixed_step`) — nunca en el render loop |
| Archetypes | Solo writes in-place; sin delete mid-iteration |
| Overlay | Debug UI muestra `rebase_count` (sección World) |

Headers de contrato: `include/game/world/world.hpp`, comentario en `ecs::Position` (`include/engine/ecs/world.hpp`).

## Contrato LocalToShip (P1B-08) — Fase 2 / Fase 5 dependen de esto
Cuando una entidad lleva `LocalToShip { ship_entity, local_position, local_orientation }`:
1. La simulación de locomoción/colisión del personaje corre **solo** en el frame local de la nave (`RigidBody6DOF` de `ship_entity`).
2. `local_position` / `local_orientation` son el estado autoritativo.
3. `ecs::Position` / `ecs::Orientation` se derivan **solo** para render/audio/cámara:
   `world_pos = ship.position + ship.orientation * local_position`
   `world_ori = ship.orientation * local_orientation`
4. No realimentar `Position` mundial a la locomoción mientras el componente exista.
5. Al salir (quitar `LocalToShip`), se bakea la pose mundial y la sim pasa a espacio mundo / GravityZone.

## Bitácora (más reciente arriba, una línea por tarea)
- 2026-08-10 [P1C-01..07,09] Economy: cfg Commodity/Market/MissionTemplate; CargoHold+Wallet; buy/sell supply curve; MissionActive Pool; FactionReputation; NPC Interact; `--scene=economy_test`. Load-time parsers only (no heap in loop).
- 2026-08-10 [P1D-01..08] Floating origin (threshold 2000 m); StarSystemData+cfg placeholders; LevelStreamTrigger soft load; station LocalToShip+GravityZone; landing LZ; `--scene=universe_test`; debug rebase_count.
- 2026-08-10 [P1B-01..09] Character: kinematic capsule + GravityZone/EVA; LocalToShip interior; OnFoot cam 1st/3rd; hatch/pilot Interact; FPS→DamageEvent+Health; scene `--scene=on_foot_test`; `game::fixed_step` = character then flight.
- 2026-08-10 [P1A-01..10] Flight: RigidBody6DOF mass=45t, main thrust 320kN, coupled brake 280kN; projectile pool 32; target @ z=-50; scene `--scene=flight_test`. Engine: Orientation, KinematicFromRigidBody, FixedStepHook, ControlMode, Actions Roll/ToggleCoupled.
- 2026-08-10 Fase 0 validada por usuario ("Ok"). Apertura Fase 1 — rama `release/fase-1-vertical-slice`.
