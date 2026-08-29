# STATUS

## Fase activa: Fase 2 — Profundidad de Sistemas (rama `release/fase-2-profundidad-de-sistemas`)

Fase 1 validada físicamente por el usuario (2026-08-10) — apertura de Fase 2.

## Progreso Fase 2
- Naves y vuelo (P2-01..04):   [####] 4/4 — COMPLETADO
  - [x] P2-01 Tripulación NPC (artillero / ingeniero) con asiento fijo LocalToShip
  - [x] P2-02 Daño por componente (ENG/SHD/WPN/SEN) vía DamageEvent.subsystem
  - [x] P2-03 Torretas giratorias (IA o jugador) con arco de disparo
  - [x] P2-04 Modelo de vuelo atmosférico vs vacío (arrastre + sustentación)
- A pie (P2-05..07):           [##] 2/3
  - [x] P2-05 Inventario completo (slots equipo, recogibles, uso de items)
  - [x] P2-06 IA combate a pie (detección, cobertura, disparo) sobre P2-11
  - [ ] P2-07 Daño por zona (torso/extremidad) + muerte/reaparición jugador
- Economía y misiones (P2-08..10): [.] 0/3
  - [ ] P2-08 Simulación económica dinámica (producción/consumo, eventos de precio)
  - [ ] P2-09 Misiones encadenadas con ramificación simple
  - [ ] P2-10 Misiones combate/escolta reutilizando IA P2-03/P2-06
- IA y facciones (P2-11..12):  [#] 1/2
  - [x] P2-11 Framework de IA compartido (FSM patrulla/alerta/combate/huida + hostilidad por reputación)
  - [ ] P2-12 Encuentros aleatorios por proximidad desde Pool
- Mundo (P2-13):               [.] 0/1
  - [ ] P2-13 Más localizaciones en el sistema fijo (esquema P1D-02)

## Decisiones Fase 2 (documentadas)
1. **P2-02 subsistemas:** 4 bancos fijos por nave (`ShipSubsystems`): Engines 300 HP,
   Shields 250, Weapons 200, Sensors 150 (jugador). `DamageEvent` gana campo
   `subsystem` (enum `combat::Subsystem`, `None` = casco puro — cero tipos de evento
   nuevos). Tras absorción de escudo: 60% de los impactos de arma eligen subsistema
   (LCG `CombatRng` singleton, determinista); el subsistema recibe 65% y el casco 35%
   (bleed-through). Efectos: ENG escala empuje/torque linealmente con HP; SHD a 0 →
   escudo forzado a 0 sin regen; WPN a 0 → todos los montajes offline; SEN reservado
   para detección IA (P2-11) y HUD. HUD vuelo muestra ENG/SGN/WPN/SEN u OFFLINE.
   El formato de guardado v1 NO serializa subsistemas todavía (decisión: bump de
   schema al cerrar más componentes de Fase 2, una sola migración).
2. **P2-11 IA compartida:** `src/game/ai/` es la ÚNICA máquina de decisión
   (`AiAgent` FSM Patrol/Alert/Combat/Flee + `select_target` + hostilidad).
   Decisión y actuación separadas: torretas/NPCs/naves solo LEEN `AiAgent.state`
   y `AiAgent.target` en sus sistemas. Facciones: 0=Comercio, 1=Seguridad,
   2=Colonos, 3=Piratas (`kPirateFaction`, hostil a todos siempre). Hostilidad
   hacia el jugador = `rep[f] < -10` (`kHostileRepThreshold`) — una sola función
   `faction_hostile_to_player` para los tres contextos (DoD). Percepción por
   distancia (sin line-of-sight esta fase) escalada por sensores P2-02 propios o
   del host (`SensorLink`, floor 30%). `ai::fixed_step` corre ANTES de
   character/flight en `game::fixed_step`. Buffer fijo 32 candidatos, cero heap.

## Fase 1 — Vertical Slice — **COMPLETADA y validada** (histórico)

## Cierre Fase 1 — resumen del bucle jugable

Con P1A–P1F cerrados, el vertical slice permite de punta a punta:

1. **Volar** una nave 6DOF (acoplado/desacoplado, potencia, escudos, armas, daño).
2. **Bajar a pie** (interior LocalToShip → EVA → gravedad de estación / combate FPS).
3. **Comerciar y misiones** (compra/venta, bodega, reputación, MissionActive).
4. **Navegar el sistema fijo** (floating origin + rebase, estación, LZ planetaria).
5. **HUD / pausa / audio** (ImGui + miniaudio cues).
6. **Guardar y cargar** la partida (F5/F9 o menú Esc → Slot0) y recuperar nave,
   carga, misión, reputación, ControlMode y FloatingOrigin.

**PARADA:** no se inicia Fase 2 hasta validación física del usuario sobre las escenas
demo (`flight_test`, `on_foot_test`, `economy_test`, `universe_test`, `ui_audio_test`,
`save_load_test`). Siguiente documento pendiente: `08-FASE-2-PROFUNDIDAD-DE-SISTEMAS.md`
(solo tras OK explícito).

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
- P1E UI/HUD/Audio:       [#######] 7/7  tareas — COMPLETADA
  - [x] P1E-01 UI framework = **Dear ImGui** (opción a); look-and-feel se rehace en Fase 6
  - [x] P1E-02 HUD vuelo: speed / energy / shield% / hull (`flight::fill_player_telemetry`)
  - [x] P1E-03 HUD a pie: health / ammo / interact prompt (`InteractionFocus`)
  - [x] P1E-04 Menús: pausa (Esc→`SimulationPaused`), system map, cargo, mission log
  - [x] P1E-05 Audio 3D **miniaudio** 0.11.21; voice pool `kMaxConcurrentVoices=16`; buses SFX/Music/UI
  - [x] P1E-06 Cues: fire / thruster loop / impact (integrity drop) / UI click
  - [x] P1E-07 Escena `ui_audio_test` (HUDs Both + pause + ≥3 tonos posicionales)
- P1F Persistencia:       [######] 6/6  tareas — COMPLETADA
  - [x] P1F-01 Formato versionado binario (`magic='CSV1'` + `schema_version`)
  - [x] P1F-02 `PersistentId { u64 }` + contador singleton (nunca handles Flecs en archivo)
  - [x] P1F-03 Serialización: RigidBody/hull/shield/power, CargoHold, Wallet, misiones,
        FactionReputation, Health/LocalToShip, ControlMode, FloatingOrigin
  - [x] P1F-04 Slots `saves/slot0.sav` + autosave + quicksave; `SaveInProgress` HUD
  - [x] P1F-05 Rechazo controlado de versión desconocida (log error, sin crash)
  - [x] P1F-06 Escena `save_load_test` + `CSC_SAVE_SMOKE=1` PASS/FAIL

## Escenas demo
| Escena | Estado |
|---|---|
| `grid_freelook` / `instancing_stress` / `mesh_viewer` | Fase 0 OK |
| `flight_test` | P1A OK — nave + objetivo a 50m |
| `on_foot_test` | P1B OK — interior LocalToShip → hatch EVA → estación + FPS target |
| `economy_test` | P1C OK — MarketA cheap ore → TravelPad → MarketB sell/turn-in |
| `universe_test` | P1D OK — Estacion-Alfa → espacio (rebase ≥1) → Planeta-01-LZ |
| `ui_audio_test` | P1E OK — HUD Both, Esc pause, 3 positional sine tones (synthetic PCM) |
| `save_load_test` | P1F OK — F5/F9 + pause Save/Load; CSC_SAVE_SMOKE |

## Decisiones P1F (documentadas)
1. **Formato (P1F-01):** binario versionado poco-endian con cabecera
   `SaveHeader { u32 magic = 0x31565343 ('CSV1'), u32 schema_version = 1 }`.
   Payload de longitud fija por esquema (campos POD escritos campo-a-campo).
   **No** es formato texto temporal: candidato a compactar/migrar en Fase 6 si el
   tamaño crece; por ahora v1 basta y el rechazo de versión desconocida es estricto.
2. **IDs (P1F-02):** `PersistentId { u64 id }` en entidades guardables; asignación
   incremental vía singleton `PersistentIdCounter`. Referencias cruzadas en save
   (ej. LocalToShip → nave) usan PersistentId, nunca `flecs::entity_t`.
3. **I/O / frame (P1F-04):** `save_game` / `load_game` pueden alocar buffers fuera del
   fixed-step; `SaveInProgress` bloquea solapes y se refleja en HUD. F5/F9 =
   `Action::QuickSave` / `QuickLoad`; menú pausa expone Slot0 + quick.

## Decisiones P1E (documentadas)
1. **UI framework (P1E-01):** opción **(a) Dear ImGui** también para UI de juego esta fase.
   Ya estaba en P0-11 como overlay de depuración. Se acepta rehacer estilo/layout/gamepad
   en **Fase 6 (pulido)**. Widgets HUD/menú usan `char[]` + `snprintf` (cero heap/frame).
2. **Audio (P1E-05):** **miniaudio** 0.11.21 vía FetchContent (`mackron/miniaudio`).
   Clips sintéticos en buffers fijos (sin WAV externos). Voice pool fijo 16; si se agota
   se descarta la petición de menor prioridad. Buses SFX / Music / UI independientes.
   Listener = cámara (`Camera3D.eye`); atenuación por distancia en coords **relativas**
   (mismo frame que entidades post-rebase floating origin).

## Bloqueado (requiere decisión de Miguel)
- P1D-04 naming final del sistema estelar (placeholders OK: Sistema-01 / Estacion-Alfa / Planeta-01).
  Esqueleto técnico + cfg + escena listos; solo falta identidad/lore/nombres finales.

## Contrato floating origin (P1D-01 / P1D-07) — Fase 4 / Fase 5 dependen de esto

**Decisión elegida: floating origin (f32 relativo).** Alternativa f64 world-pos **rechazada** — no mezclar.

| Campo | Valor |
|---|---|
| Esquema | f32 relative to current origin; absolute ≈ relative + `FloatingOrigin.origin_offset` |
| Umbral de rebase | **2000 m** (`kFloatingOriginThreshold` / `FloatingOrigin.threshold`) |
| Disparo | `|player_pos_relative| > threshold` (nave `RigidBody6DOF` o personaje world / LocalToShip→world) |
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
- 2026-08-29 [WSL] Captura de ratón robustecida (`engine/input/input.cpp`): mientras
  `GLFW_CURSOR_DISABLED`, el look se mide recentrando el cursor a mitad de ventana cada
  frame (en vez de fiarse del delta virtual crudo de GLFW) + 2 frames de guarda tras
  cada (re)captura — algunos compositores WSLg clampan/reenvían la posición al borde de
  ventana bajo cursor disabled, lo que antes cortaba o disparaba el look. No toca quién
  decide activar/desactivar la captura (F1 debug UI, pausa/menús) — solo lee el modo
  GLFW vigente. Añadido `borderless_fullscreen` (config + ventana sin decorar a
  resolución de monitor, activado por defecto en `default.cfg` de esta máquina;
  `--windowed` para desactivar). Verificado con build limpio (0 warnings propios) +
  smoke test headless (Xvfb/lavapipe) de `crew_turret_test` y `npc_combat_test`.
- 2026-08-29 [reconciliación] Este checkout (WSL, RTX 5060 Ti) había hecho en paralelo
  una implementación propia y duplicada de P2-01..05 sin saber que Cursor Cloud ya
  había completado P2-01..06+P2-11 y fusionado a `main` (PR #6). Se descartó la
  duplicada (queda en la rama local `old-local-p2-duplicate` por si hace falta
  recuperar algo puntual) y se adoptó la de `main` como única fuente de verdad; solo
  se rescató el fix de captura de ratón/WSL de arriba, que no solapaba con nada.
- 2026-08-10 [P2-06] IA combate a pie: NpcCombatant (actuación) sobre AiAgent P2-11 (decisión).
  Patrol=PatrolRoute waypoints, Alert=encara, Combat=avanza hasta preferred_range + dispara
  (cadencia EquippedItem, roll de precisión CombatRng, mismo DamageEvent que el jugador),
  herido <60% → CoverPoint más cercano, Flee correr. move_wish entra por la MISMA locomoción
  que el jugador. Overlay AI por estado. Escena `npc_combat_test` verificada (captura).
- 2026-08-10 [P2-05] Inventario: catálogo fijo (Rifle/Pistola/Medkit/Munición), Inventory
  8 slots POD, ItemPickup vía Interact (retira tags, sin delete en tick), teclas G/T/R
  (medkit/cambiar arma/recargar; equipar resetea cargador — simplificación documentada),
  HUD WPN+consumibles, pickups en on_foot_test. Siguiente: P2-06 usa mismos NPC Health.
- 2026-08-10 [P2-04] Atmósfera vs vacío: AtmosphereVolume esférico (densidad lineal
  inner→outer), arrastre cuadrático + sustentación simplificada (AeroProfile Cd·A=25 /
  Cl·A=8) + gravedad planetaria para naves dentro del volumen. Rebase P1D-07 desplaza
  centros. HUD línea ATM (kg/m³ o VACUUM). Verificado en universe_test (captura).
- 2026-08-10 [P2-03] Torretas: TurretMount (arco ±120° yaw / ±60° pitch, slew 2.4 rad/s),
  actuación IA desde AiAgent compartido o jugador (TurretSeat → ControlMode::TurretControl,
  Interact sale). Disparo compartido weapon_try_consume/weapon_emit (cero duplicación P1A).
  Gate: banco Weapons P2-02 + gunner vivo (requires_gunner). Escena `crew_turret_test`
  verificada en lavapipe: pirata daña subsistemas del jugador (captura en artifacts/).
- 2026-08-10 [P2-01] Tripulación NPC: CrewMember (gunner/engineer) sentado vía LocalToShip
  (mismo patrón que jugador a pie, sin física propia). Ingeniero repara el banco más dañado
  a 6 HP/s tras el daño del tick. Gunner enlaza turret (actuación en P2-03). Crew no
  persiste en save v1 (misma decisión que subsistemas). Siguiente: P2-03 torretas.
- 2026-08-10 [P2-11] Framework IA compartido (src/game/ai/): FSM + select_target + reputación,
  SensorLink para torretas, telemetría por estado. Siguiente: P2-01/P2-03 consumen AiAgent.
- 2026-08-10 [P2-02] Daño por componente: ShipSubsystems (4 bancos POD), DamageEvent.subsystem,
  roll de localización LCG, degradación ENG/SHD/WPN en fixed_step, HUD por subsistema.
  Siguiente (P2-11) necesita SEN para radio de detección IA.
- 2026-08-10 Fase 1 validada por usuario. Apertura Fase 2 — rama `release/fase-2-profundidad-de-sistemas`.
- 2026-08-10 [P1F-01..06] Binary save schema v1 + PersistentId; slots/quicksave; save_load_test; CSC_SAVE_SMOKE; Fase 1 COMPLETADA — PARADA validación física.
- 2026-08-10 [P1E-01..07] UI=ImGui (Fase 6 rework); HUD flight/on-foot; pause+menus; miniaudio voice pool 16 + SFX/Music/UI buses; `--scene=ui_audio_test`.
- 2026-08-10 [P1C-01..07,09] Economy: cfg Commodity/Market/MissionTemplate; CargoHold+Wallet; buy/sell supply curve; MissionActive Pool; FactionReputation; NPC Interact; `--scene=economy_test`. Load-time parsers only (no heap in loop).
- 2026-08-10 [P1D-01..08] Floating origin (threshold 2000 m); StarSystemData+cfg placeholders; LevelStreamTrigger soft load; station LocalToShip+GravityZone; landing LZ; `--scene=universe_test`; debug rebase_count.
- 2026-08-10 [P1B-01..09] Character: kinematic capsule + GravityZone/EVA; LocalToShip interior; OnFoot cam 1st/3rd; hatch/pilot Interact; FPS→DamageEvent+Health; scene `--scene=on_foot_test`; `game::fixed_step` = character then flight.
- 2026-08-10 [P1A-01..10] Flight: RigidBody6DOF mass=45t, main thrust 320kN, coupled brake 280kN; projectile pool 32; target @ z=-50; scene `--scene=flight_test`. Engine: Orientation, KinematicFromRigidBody, FixedStepHook, ControlMode, Actions Roll/ToggleCoupled.
- 2026-08-10 Fase 0 validada por usuario ("Ok"). Apertura Fase 1 — rama `release/fase-1-vertical-slice`.
