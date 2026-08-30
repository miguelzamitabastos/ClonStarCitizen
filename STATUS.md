# STATUS

## Fase activa: Fase 3 — Expansión de Contenido — **EN CURSO (2/9)**

Fase 2 validada físicamente por Miguel el 2026-08-30 (probó las 5 escenas de
`.claude/VERIFICACION-PENDIENTE.md` a mano; solo se ven cuadros porque aún no hay
assets de arte, coherente con las fases del roadmap — sin bloqueo). Con eso se
cierra formalmente Fase 2 (13/13) y se abre Fase 3 según
[[09-FASE-3-EXPANSION-DE-CONTENIDO]]
(`docs/roadmap/files/09-FASE-3-EXPANSION-DE-CONTENIDO.md`).

### Objetivo de Fase 3

Catálogo de naves y equipamiento **dirigido por datos**, poder comprar/poseer
naves distintas, estaciones/ciudades más grandes y pobladas, y más contenido de
misiones — todo dentro del sistema estelar fijo de Fase 1D (ampliar el universo
es Fase 4). El riesgo explícito de la fase: que se hardcodee contenido en C++ por
velocidad. Objetivo contrario: añadir una nave / un arma / una misión = editar un
archivo de datos, nunca tocar el motor.

### Progreso Fase 3 — 2/9

- Pipeline de datos (P3-01, P3-05, P3-08):       [#] 1/3
  - [x] P3-01 Pipeline de datos de nave: stats (masa, empuje, hardpoints, capacidad
        de carga) en archivos de configuración, no en código (dep. P1A-01, P0-05)
  - [ ] P3-05 Catálogo de armas/equipamiento dirigido por datos, mismo patrón que
        P3-01 (dep. P1A-08, P1B-06)
  - [ ] P3-08 Herramienta interna (CLI/script) para validar y/o generar entradas de
        catálogo antes de compilar (dep. P3-01, P3-05)
- Catálogo y naves jugables (P3-02, P3-03):      [#] 1/2
  - [x] P3-02 Catálogo de 4-6 tipos de nave (caza, carguero, exploración,
        multipropósito) usando P3-01 (dep. P3-01)
  - [ ] P3-03 Hangar y tienda de naves: comprar/vender/cambiar de nave activa,
        reutilizando las transacciones de P1C-03 tal cual (dep. P3-02, P1C-03)
- Mundo y personaje (P3-04, P3-06):              [ ] 0/2
  - [ ] P3-04 Expansión de estaciones/ciudades: más NPCs, tiendas y dadores de
        misión por localización (dep. P1D-05, P1C-06)
  - [ ] P3-06 Personalización básica de personaje: trajes/armadura con stats
        (protección, capacidad EVA) (dep. P1B-01, P3-05)
- Misiones y verificación (P3-07, P3-09):        [ ] 0/2
  - [ ] P3-07 Más contenido de misiones: plantillas adicionales + una línea
        narrativa simple opcional (dep. P1C-04, P2-09)
  - [ ] P3-09 Verificación `content_smoke_test`: carga todo el catálogo y valida
        integridad (IDs únicos, referencias resolubles) (dep. P3-01..08)

### Restricciones de arquitectura de Fase 3 (del roadmap, vinculantes)

1. **Todo lo nuevo es datos, no código.** Una nave/arma/misión nueva no puede
   requerir una rama de código nueva por tipo: si al añadir contenido hace falta un
   `if`/`switch` por ID, falta un parámetro de datos, no un caso especial.
2. **ID de texto estable y único por entrada** (ej. `"ship.fighter.hornet_clone"`),
   validado en carga: IDs duplicados o referencias a IDs inexistentes (una nave que
   apunta a un hardpoint de arma que no existe) **fallan la carga de forma clara**,
   nunca en silencio con un valor por defecto.
3. **P3-03 reutiliza P1C-03 tal cual** — una nave es un ítem con precio a efectos de
   la transacción, no un sistema de comercio paralelo.
4. **P3-08 puede ser un script simple** que recorra los archivos de datos y ejecute
   las mismas validaciones que P3-09 antes del build (detectar un ID duplicado en
   segundos en vez de en un build completo).
5. Sigue vigente `.cursorrules`: DOD/ECS estricto, cero heap en Update/Render,
   pools/arenas pre-asignados, GLM vía `include/engine/math/glm.hpp`.

### Definition of Done de Fase 3 (del roadmap)

- `content_smoke_test` carga el catálogo completo (naves + armas + misiones) sin
  errores de validación y reporta un resumen (nº de entradas por tipo) en consola.
- El jugador puede comprar al menos **dos naves distintas** del catálogo en el
  hangar de una estación y pilotarlas, con diferencias de stats perceptibles
  (velocidad, maniobrabilidad, capacidad de carga).
- Añadir una nave nueva al juego, documentado como prueba en este `STATUS.md`, se
  hace **solo** editando el archivo de datos correspondiente — sin tocar `.cpp`/`.hpp`.

### Deuda técnica arrastrada de Fase 2 (pendiente de resolver en un único bump de schema)

El save v1 **no serializa** los componentes nuevos de Fase 2: `ShipSubsystems`
(P2-02), `DamageEvent.zone` / `CharacterDead` / `RespawnTimer` / `SpawnPoint`
(P2-07), `CompletedMissions` (P2-09), `CrewMember` (P2-01). Decisión consolidada
en las decisiones 1/2/5 de Fase 2: se resuelve con **un solo** bump de
`schema_version` que junte todos estos componentes, no de forma incremental.
Bug latente conocido de baja probabilidad: cargar una partida guardada justo en
la ventana de "muerto, esperando respawn" deja `Health.hp=0` sin `CharacterDead`.
Pendiente de decidir si ese bump entra en Fase 3 (al tocar el pipeline de datos de
naves) o se difiere a Fase 6 (pulido).

### Decisiones Fase 3 (documentadas)

1. **P3-01 pipeline de datos de nave:** catálogo `assets/data/ships.cfg` (secciones
   `[[ship]]`, mismo parser de estilo que `commodities.cfg`/`mission_templates.cfg`)
   → singleton `flight::ShipCatalog` de `ShipDef` POD (`kMaxShipDefs=16`, cero heap).
   `id` es texto estable y único (`"ship.<rol>.<nombre>"`); id duplicado o ausente =
   **carga marcada INVÁLIDA con `log_error`** (dos líneas de error claras), las
   entradas válidas se conservan para que los spawns resuelvan. La comprobación
   dura que bloquea build llega en P3-09 (`content_smoke_test`). El catálogo se
   carga en `scene_setup_by_name` **antes** de `desc->setup(ctx)`, así que está vivo
   para cualquier escena que spawnee una nave.
   `spawn_player_ship`/`spawn_npc_ship` ganan un parámetro opcional `ship_id`
   (`nullptr` → `kDefaultPlayerShipId` / `kDefaultNpcShipId`); id desconocido o
   catálogo ausente → warning + `ShipDef{}` por defecto (cuyos valores reproducen
   la nave hardcodeada de Fase 1/2, degradación sin romper nada). Todos los stats
   de nave salen ahora del `ShipDef`: masa/inercia/escala (`RigidBody6DOF`,
   `ecs::Scale`), empuje main/maniobra/retro (escalan la plantilla fija de
   thrusters vía `build_thruster_set`), `max_torque_nm` (nuevo componente
   `flight::ShipSpec` que el integrador lee de la entidad en vez de la constante
   `kMaxTorqueNm`; sin `ShipSpec` → constante, para blancos de daño pelados),
   HP de casco/escudo/planta, bancos de subsistemas P2-02, nº y offset de
   hardpoints de arma (el arma montada sigue siendo la default de Fase 1/2 hasta
   P3-05), hardpoints de torreta (solo colocación), y capacidad de bodega
   (`economy::CargoHold`). `ships.cfg` arranca con solo los 2 ids que el motor
   spawnea por defecto (valores idénticos a hoy → cero cambio de comportamiento);
   las 4-6 naves distintas son P3-02.
   **Fuera de alcance, anotado:** `ShipSpec` no se serializa en el save v1 (misma
   deuda de schema que Fase 2). Hoy sin efecto observable (una sola nave de
   jugador, su `max_torque_nm` == `kMaxTorqueNm`); P3-03 (poseer/cambiar de nave)
   obliga a serializar el `id` de nave y a resolver esta deuda.
   Verificado: build limpio (0 warnings) + smoke headless de las 7 escenas
   (flight/on_foot/crew_turret/npc_combat/economy/universe/ui_audio) sin crash,
   `CSC_SAVE_SMOKE=1` PASS, y test negativo (id duplicado inyectado → `log_error`
   "duplicate ship id" + "catalog INVALID", sin crash).

2. **P3-02 catálogo de naves distintas:** puro contenido sobre el pipeline de
   P3-01 — 4 hulls jugables nuevas en `ships.cfg` además de la multipropósito
   base (`ship.player.default`/Kestrel): `ship.fighter.wasp` (28 t, empuje/torque
   altos, casco 650, bodega 12, 2 cañones), `ship.freighter.mule` (120 t, torque
   90k, casco 1600, bodega 160/400, 2 hardpoints de torreta),
   `ship.explorer.pathfinder` (52 t, planta 620/1800, escudo 750, sensores 300,
   bodega 60), `ship.heavy.bulwark` (90 t, casco 1900, escudo 900, 3 hardpoints
   de torreta). Cero código de gameplay nuevo. Para poder **probarlas antes de
   que exista el hangar (P3-03)** se añadió un único gancho: flag `--ship=<id>`
   (y clave `ship_id=`/`player_ship_id=` en config) → `AppConfig.player_ship_id`
   → `SceneContext.player_ship_id` → `spawn_player_ship` en todas las escenas de
   vuelo (`flight_test`, `on_foot_test`, `crew_turret_test`, `ui_audio_test`,
   `save_load_test`, y `universe_test` vía nuevo parámetro de
   `spawn_universe_test`). `economy_test` no lo cablea (su escena la monta
   `setup_economy_test_scene`, fuera de alcance de esta tarea). id inválido en
   `--ship=` → warning + nave por defecto (mismo fallback que P3-01), sin crash.
   El hangar de P3-03 sustituye este flag por una elección en juego.
   Verificado: build limpio + las 8 escenas headless sin errores + `--ship=` con
   cada hull nueva (masa correcta en el log) + `--ship=id.inexistente` (fallback)
   + `CSC_SAVE_SMOKE=1` PASS + `CSC_FORCE_REBASE_SMOKE=1` dispara.

## Fase 2 — Profundidad de Sistemas — **COMPLETADA y validada** (13/13, validación física 2026-08-30)

## Progreso Fase 2 — 13/13 COMPLETADO
- Naves y vuelo (P2-01..04):   [####] 4/4 — COMPLETADO
  - [x] P2-01 Tripulación NPC (artillero / ingeniero) con asiento fijo LocalToShip
  - [x] P2-02 Daño por componente (ENG/SHD/WPN/SEN) vía DamageEvent.subsystem
  - [x] P2-03 Torretas giratorias (IA o jugador) con arco de disparo
  - [x] P2-04 Modelo de vuelo atmosférico vs vacío (arrastre + sustentación)
- A pie (P2-05..07):           [###] 3/3 — COMPLETADO
  - [x] P2-05 Inventario completo (slots equipo, recogibles, uso de items)
  - [x] P2-06 IA combate a pie (detección, cobertura, disparo) sobre P2-11
  - [x] P2-07 Daño por zona (torso/extremidad) + muerte/reaparición jugador
- Economía y misiones (P2-08..10): [###] 3/3 — COMPLETADO
  - [x] P2-08 Simulación económica dinámica (producción/consumo, eventos de precio)
  - [x] P2-09 Misiones encadenadas con ramificación simple
  - [x] P2-10 Misiones combate/escolta reutilizando IA P2-03/P2-06
- IA y facciones (P2-11..12):  [##] 2/2 — COMPLETADO
  - [x] P2-11 Framework de IA compartido (FSM patrulla/alerta/combate/huida + hostilidad por reputación)
  - [x] P2-12 Encuentros aleatorios por proximidad desde Pool
- Mundo (P2-13):               [#] 1/1 — COMPLETADO
  - [x] P2-13 Más localizaciones en el sistema fijo (esquema P1D-02)

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
2. **P2-07 zona + muerte/reaparición:** `DamageEvent` gana campo `zone`
   (enum `combat::BodyZone`: Torso/Head/Limb, `Torso` = comportamiento previo sin
   cambios) — mismo patrón que `subsystem` (P2-02): rolado por `CombatRng` al
   disparar (`roll_hit_zone`, no geometría de cápsula real), consumido en
   `apply_damage_events` (flight.cpp) como multiplicador sobre `character::Health`
   (Head ×2.5, Limb ×0.5, Torso ×1 — sin efecto sobre naves). Muerte/reaparición
   **solo del jugador** (los NPCs se quedan muertos, sin ciclo): `SpawnPoint`
   capturado en `spawn_player_character` (posición mundo o local a nave);
   `RespawnTimer` (3s) añadido junto a `CharacterDead` en `apply_damage_events`
   cuando el objetivo es el jugador; `step_death_and_respawn` (primer paso de
   `character::fixed_step`) cuenta atrás y restaura HP máxima + posición/LocalToShip
   + `InstanceTag` al llegar a 0. HUD on-foot muestra "YOU DIED — respawning in Xs"
   mientras dura. **Gotcha real (crash) durante la verificación**: `world.each()`
   con un tag vacío (`CharacterDead`, `PlayerCharacter`) pedido **por referencia**
   como término de query revienta flecs (`entity_index.c` assert) — ya documentado
   para `CoverPoint` en este mismo archivo, repetido aquí antes de arreglarlo:
   la consulta de telemetría de muerte usa `Health` (no vacío) + `.has<>()` en el
   cuerpo, nunca el tag como parámetro. **Fuera de alcance, anotado**: `CharacterDead`/
   `RespawnTimer`/`SpawnPoint` no se serializan en el save v1 — cargar una partida
   guardada justo durante la ventana de "muerto, esperando respawn" dejaría
   `Health.hp=0` sin `CharacterDead` (bug latente de baja probabilidad, mismo
   criterio que la nota de subsistemas de la decisión 1: se resuelve en el bump de
   schema que junte todos los componentes nuevos de Fase 2, no antes).
3. **P2-11 IA compartida:** `src/game/ai/` es la ÚNICA máquina de decisión
   (`AiAgent` FSM Patrol/Alert/Combat/Flee + `select_target` + hostilidad).
   Decisión y actuación separadas: torretas/NPCs/naves solo LEEN `AiAgent.state`
   y `AiAgent.target` en sus sistemas. Facciones: 0=Comercio, 1=Seguridad,
   2=Colonos, 3=Piratas (`kPirateFaction`, hostil a todos siempre). Hostilidad
   hacia el jugador = `rep[f] < -10` (`kHostileRepThreshold`) — una sola función
   `faction_hostile_to_player` para los tres contextos (DoD). Percepción por
   distancia (sin line-of-sight esta fase) escalada por sensores P2-02 propios o
   del host (`SensorLink`, floor 30%). `ai::fixed_step` corre ANTES de
   character/flight en `game::fixed_step`. Buffer fijo 32 candidatos, cero heap.
4. **P2-08 economía dinámica:** simulación de fondo, no por frame — `EconomyClock`
   acumula `dt` en `economy::fixed_step` y solo dispara un "tick económico" cada
   `kEconomyTickSeconds` (15s de tiempo de juego), tal como pide la nota de
   arquitectura del roadmap. Un tick hace tres cosas en orden: (1) vacía
   `PriceEventQueue` (cola de tamaño fijo, 16 slots) aplicando cada `PriceEvent`
   como delta directo sobre `Market.stock`; (2) aplica el arrastre de
   producción/consumo (`Market.production_rate[N]`, unidades/s por mercado,
   dato en `markets.cfg` vía `rate_N` — MarketA produce mineral +1.5/s, MarketB
   lo consume -1.0/s, coherente con el "barato aquí / caro allí" ya existente
   de P1C); (3) rueda un evento aleatorio de escasez/superávit (`EconomyRng`
   propio, 15% de probabilidad por tick, ±15..35 unidades) — cubre el caso
   "eventos (escasez...)" del roadmap. El caso "misión completada en masa" no
   es un evento aparte: `mission_try_complete_at_market` devuelve ahora
   `(commodity_id, qty)` de la entrega, y `handle_interact` encola un
   `PriceEvent` con ese `qty` como delta positivo — cuanto más grande la
   entrega, mayor el efecto en precio, sin lógica nueva. Todo pasa por la
   misma cola/tick, nunca se aplica un delta de precio de forma inmediata.
5. **P2-09 misiones encadenadas:** `MissionTemplate` gana dos campos —
   `requires_completed_id` (sentinel `kNoMissionRequirement`, ya que el id 0 es
   una plantilla válida; omitido en cfg = siempre ofertable) y `branch_group`
   (0 = sin rama; no-cero = plantillas del mismo grupo son mutuamente
   excluyentes). Nuevo singleton `CompletedMissions`: `done[template_id]`
   (permite desbloquear encadenadas) + `branch_locked[group]` (se marca al
   **aceptar**, no al completar — la elección del jugador cierra la rama
   alternativa de inmediato, no al final). `mission_try_complete_at_market`
   marca `done` antes de liberar el slot del pool (los datos desaparecen tras
   `release()`). Demo en `economy_test`: completar `OreDelivery` (id=0)
   desbloquea dos NPCs en MarketB — "Aid Colonists" (id=2, facción Colonos) y
   "Security Run" (id=3, facción Seguridad), `branch_group=1` — aceptar
   cualquiera bloquea el otro para siempre; un NPC de turn-in nuevo en
   MarketA cierra la que se haya elegido (el turn-in matchea por `market_id`,
   no por plantilla, así que sirve para cualquiera de las dos ramas).
   **Fuera de alcance, anotado** (mismo criterio que P2-02/P2-07):
   `CompletedMissions` no se serializa en el save v1 todavía — se resuelve en
   el bump de schema que junte todos los componentes nuevos de Fase 2.
6. **P2-10 misiones combate/escolta:** `MissionType` gana `Combat`/`Escort` —
   sin IA nueva, reutilizan `character::spawn_npc_combatant` (P2-06) y
   `ai::AiThreatTarget`/`FactionMember` (P2-11, el tag ya existía pensado
   exactamente para esto). `qty_min/qty_max` doblan como rango de hostiles
   (tope `kMaxMissionHostiles=4`); `target_faction_id` (nuevo campo) es la
   facción hostil a spawnear, separado de `faction_id`/`rep_delta` (siguen
   siendo la facción de recompensa). Nuevo componente `MissionLink{slot,
   is_escort_target}` liga cada hostil/escoltado spawneado a su slot del
   `MissionActivePool`; `apply_damage_events` (flight.cpp) reporta bajas
   (`kills_confirmed++`) o fallo inmediato (`is_escort_target` — libera el
   slot sin recompensa) al morir. El encuentro spawnea junto al jugador en el
   momento de aceptar (sin registro de "ubicación de mundo" para mercados,
   fuera de alcance de esta tarea). Turn-in reutiliza el NPC de mercado ya
   existente (matchea por `market_id`, no por plantilla).
   **Dos bugs reales encontrados y corregidos durante la verificación** (los
   tres viven en el mismo patrón de gotchas de flecs con tipos vacíos, ver
   sección de bitácora):
   - `ai::collect_target_candidates` pedía `AiThreatTarget` (tag vacío) por
     referencia en `world.each()` — dormido desde P2-11 porque nada lo usaba
     todavía; revienta en cuanto se usa. Mismo arreglo que P2-07: consulta por
     `Position` + `.has<>()` en el cuerpo.
   - `.set<ai::AiThreatTarget>({})` sobre un tag vacío también revienta
     flecs (`operation invalid for empty type`) — los tags se añaden con
     `.add<T>()`, nunca `.set<T>({})`.
   - (No-bug, error de implementación propio) se me olvidó añadir el parseo
     de `target_faction_id` en `apply_template_kv` — los hostiles spawneaban
     con facción 0 (Comercio) en vez de la 3 (Piratas) del template.
   Los tres solo salieron a la luz forzando manualmente un accept + spawn de
   escolta en una sesión headless (nada dispara Combat/Escort sin interacción
   real del jugador) — ver bitácora para el procedimiento de verificación.
7. **P2-12 encuentros aleatorios:** `ai::EncounterPool` (`src/game/ai/`, no
   `src/game/world/` — la tarea vive bajo "IA y facciones" en el roadmap) —
   4 pares nave+torreta (`flight::spawn_npc_ship` + `spawn_turret`,
   `requires_gunner=false`) **pre-creados una sola vez** en `universe_test`
   (`ai::spawn_encounter_pool`, junto a `spawn_projectile_pool`) e
   inmediatamente dejados inertes (sin `InstanceTag`/`AiAgent` — invisibles,
   fuera de la colección de candidatos de IA). Cada 10s de tiempo de juego
   (`kEncounterCheckSeconds`, cadencia de fondo tipo P2-08, no por frame),
   `ai::fixed_step` rueda 35% de probabilidad; si hay slot libre, activa uno
   a 150–300m del jugador con facción pirata o patrulla de Seguridad
   (50/50). Se desactiva (vuelve al pool, HP/escudo/subsistemas restaurados)
   si su nave muere o se aleja >450m del jugador — **nunca se crea ni destruye
   una entidad fuera de las 4 pre-creadas**, tal como exige la nota de
   arquitectura del roadmap.
   **Simplificación deliberada, anotada:** la nave del encuentro gana
   `AiAgent` propio (decisión/telemetría vía el mismo framework) pero **no
   tiene actuación de movimiento** — no persigue ni maniobra. Quien de verdad
   amenaza al jugador es su torreta (P2-03 reutilizado tal cual, ya probado
   en `crew_turret_test`). Añadir movimiento real a naves NPC es un hueco
   documentado para una fase futura, no una IA a medias bifurcada aquí.
   Verificado en `universe_test` 65s headless: 2 encuentros de piratas
   activados sin crash, cero regresión en las 7 escenas restantes (todas
   corren `ai::fixed_step`).
8. **P2-13 más localizaciones:** sin tocar el esquema P1D-02 ni la arquitectura
   de streaming/floating-origin — `spawn_universe_test` (world.cpp) pasó de
   `find_body()` (primer match de cada tipo) a **iterar todos** los cuerpos,
   reutilizando el bloque de estación/LZ ya existente refactorizado a
   `spawn_station_location`/`spawn_landing_zone_location` (nombres de entidad/
   trigger derivados de `body.name`, único por convención del esquema — evita
   colisión al tener 2+ estaciones). `star_system.cfg` pasa de 4 a 7 cuerpos:
   `Estacion-Beta` (segunda estación navegable) + `Planeta-02`/`Planeta-02-LZ`
   (segundo planeta con atmósfera + LZ). `station_pos`/`lz_pos` (primer match)
   se mantienen como ancla del spawn del jugador y del log de estado — mismo
   punto de partida que antes, ahora con más sitios que visitar alrededor.
   Verificado en `universe_test`: log confirma "2 estaciones, 2 LZ, 2 planetas,
   1 estrella"; smoke de rebase (`CSC_FORCE_REBASE_SMOKE=1`) sigue disparando
   correctamente; cero regresión en las 7 escenas restantes.

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
- 2026-08-30 [P3-02] Catálogo de naves: 4 hulls jugables nuevas en `ships.cfg`
  (`fighter.wasp` / `freighter.mule` / `explorer.pathfinder` / `heavy.bulwark`)
  con masa/empuje/torque/casco/escudo/bodega/hardpoints perceptiblemente
  distintos, sobre el pipeline P3-01 sin código de gameplay nuevo. Gancho de
  prueba previo al hangar: flag `--ship=<id>` (+ `ship_id=` en config) →
  `AppConfig`/`SceneContext.player_ship_id` → `spawn_player_ship` en todas las
  escenas de vuelo (`spawn_universe_test` gana un parámetro). id inválido →
  fallback a la nave por defecto con warning. Verificado: build limpio + 8
  escenas headless + `--ship=` con cada hull (masa correcta) + fallback +
  `CSC_SAVE_SMOKE`/`CSC_FORCE_REBASE_SMOKE`. **Verificación manual pendiente de
  Miguel:** volar 2+ naves y notar la diferencia de manejo.
- 2026-08-30 [P3-01] Pipeline de datos de nave: `assets/data/ships.cfg` (`[[ship]]`)
  → singleton `flight::ShipCatalog`/`ShipDef` (POD, `kMaxShipDefs=16`, sin heap),
  cargado en `scene_setup_by_name` antes del setup de escena. `spawn_player_ship`/
  `spawn_npc_ship` toman `ship_id` opcional y construyen TODOS los componentes
  (masa/inercia, empuje ×3, `max_torque_nm` vía nuevo `ShipSpec` que lee el
  integrador, casco/escudo/planta, subsistemas P2-02, hardpoints de arma/torreta,
  bodega) desde el `ShipDef`. Id de texto único; duplicado/ausente = carga
  `log_error` INVÁLIDA (la comprobación dura es P3-09). `ships.cfg` con 2 ids
  (valores == naves hardcodeadas Fase 1/2 → cero cambio de comportamiento); las
  4-6 naves distintas son P3-02. `ShipSpec` no se serializa aún (deuda de schema
  Fase 2; P3-03 la fuerza). Verificado: build limpio 0 warnings + 7 escenas
  headless sin crash + `CSC_SAVE_SMOKE=1` PASS + test negativo id duplicado.
- 2026-08-30 [Fase 2 → Fase 3] Miguel valida físicamente las 5 escenas de
  `.claude/VERIFICACION-PENDIENTE.md` (solo cuadros, sin assets de arte todavía —
  esperado por el roadmap). **Fase 2 CERRADA formalmente (13/13).** Abierta Fase 3
  — Expansión de Contenido: 9 tareas (P3-01..09), eje = pipeline de datos para
  naves/armas/misiones (añadir contenido = editar un archivo, no el motor).
  Progreso, restricciones de arquitectura y DoD de la fase arriba en la sección
  "Fase activa". Deuda de schema de save v1 de Fase 2 anotada como pendiente.
  Checklist de verificación consumido — `.claude/VERIFICACION-PENDIENTE.md` eliminado.
- 2026-08-30 [WSL] Segundo fix de captura de ratón — el de P2-07/29d5810 solo se
  había probado sin crash en Xvfb, sin ratón real; Miguel reportó el bug de verdad
  probando a mano en `on_foot_test`: al entrar el cursor real en la ventana, la
  cámara se iba de golpe a mirar hacia abajo del todo (pantalla en blanco).
  **Pendiente de que Miguel confirme que este segundo fix lo resuelve** — no
  verificable desde esta sesión (sin ratón físico). Causa: mientras WSLg tarda un número arbitrario de frames en sincronizar
  el warp del cursor tras `glfwSetCursorPos`, cada frame seguía leyendo el mismo
  valor "stale" y el código anterior lo RECORTABA a ±200px y lo aplicaba igual —
  mismo salto máximo repetido frame tras frame hasta clavar el pitch en su límite en
  un puñado de frames. Arreglo: si el delta no es plausible (>120px), se DESCARTA
  entero (cero look ese frame) en vez de recortarlo y aplicarlo, y se reintenta el
  recentrado sin límite de frames — se autocorrige en cuanto WSLg sincroniza de
  verdad, sin necesidad de adivinar cuántos frames tarda. Simplifica el código:
  fuera `capture_centered`/`suppress_look_frames` (ya no hacen falta con esta regla).
  Verificado headless (sin crash, 6 escenas) — la verificación real de que el look
  ya no se dispara la hace Miguel con ratón físico, es lo único que puede probarlo.
- 2026-08-30 [P2-13] Más localizaciones: `star_system.cfg` 4→7 cuerpos (Estacion-Beta,
  Planeta-02, Planeta-02-LZ). `spawn_universe_test` refactorizado para iterar TODOS
  los cuerpos de cada tipo (antes solo el primero vía `find_body`), sin tocar el
  esquema P1D-02 ni la arquitectura de streaming/floating-origin. **Fase 2
  COMPLETADA — 13/13 tareas.** PARADA: pendiente validación física de Miguel antes
  de abrir Fase 3. Verificado: log confirma 2 estaciones/2 LZ/2 planetas/1 estrella
  en universe_test, rebase smoke OK, cero regresión en 7 escenas + CSC_SAVE_SMOKE PASS.
- 2026-08-30 [P2-12] Encuentros aleatorios: `ai::EncounterPool` (4 pares nave+torreta
  pre-creados en `universe_test`, dormidos hasta activarse por proximidad — nunca se
  crea/destruye fuera del pool). Cadencia de fondo 10s + 35% prob.; activa pirata o
  patrulla de Seguridad a 150–300m del jugador; desactiva por muerte o distancia
  >450m. Nave sin movimiento propio (simplificación anotada) — la torreta P2-03
  reutilizada es la amenaza real. Fase 2 bloque "IA y facciones" COMPLETADO
  (P2-11..12 2/2). Verificado en `universe_test` 65s headless (2 encuentros activados
  sin crash) + regresión limpia en 7 escenas + CSC_SAVE_SMOKE PASS.
- 2026-08-30 [P2-10] Misiones combate/escolta: `MissionType::Combat/Escort` sobre
  la IA compartida existente, sin código nuevo de decisión — `spawn_npc_combatant`
  (P2-06) + `AiThreatTarget`/`FactionMember` (P2-11). `MissionLink` liga cada
  hostil/escoltado a su slot de `MissionActivePool`; `apply_damage_events` reporta
  bajas o fallo (escolta muerta = misión perdida, slot liberado sin recompensa) al
  morir. Fase 2 bloque "Economía y misiones" COMPLETADO (P2-08..10 3/3).
  Verificación forzada (accept manual sin input real, ver procedimiento abajo)
  encontró y corrigió 2 bugs reales de flecs con tipos vacíos — `AiThreatTarget`
  pedido por referencia en `world.each()` (dormido desde P2-11) y
  `.set<AiThreatTarget>({})` en vez de `.add<>()` — más un `target_faction_id` sin
  parsear en `apply_template_kv`. Regresión limpia en 6 escenas + CSC_SAVE_SMOKE
  PASS tras los arreglos.
- 2026-08-30 [P2-09] Misiones encadenadas: `MissionTemplate.requires_completed_id` +
  `branch_group`; singleton `CompletedMissions` (done[] + branch_locked[]). Demo:
  OreDelivery (id=0) desbloquea ColonistAid (id=2) / SecurityRun (id=3) en
  MarketB, `branch_group=1` mutuamente excluyentes, más turn-in nuevo en
  MarketA. Verificado: build limpio + `economy_test`/`ui_audio_test`/
  `save_load_test` 12s headless sin crash ("Loaded 4 mission templates") +
  `CSC_SAVE_SMOKE=1` PASS.
- 2026-08-30 [P2-08] Economía dinámica: `EconomyClock` (tick de fondo cada 15s,
  no por frame) + `PriceEventQueue` (16 slots) + `Market.production_rate[N]`
  (data-driven, `rate_N` en markets.cfg). Un tick vacía la cola de eventos,
  aplica arrastre de producción/consumo y rueda un evento aleatorio de escasez/
  superávit (15% prob., propio `EconomyRng`). Misión completada en masa encola
  un PriceEvent proporcional al qty entregado (`mission_try_complete_at_market`
  ahora devuelve commodity_id+qty). Verificado en `economy_test` 50s headless:
  evento de escasez disparado en el primer tick, sin crash; regresión limpia en
  npc_combat_test/save_load_test.
- 2026-08-29 [P2-07] Daño por zona (`combat::BodyZone` Torso/Head/Limb, rolado por
  CombatRng al disparar, ×2.5/×0.5/×1 sobre Health) + ciclo de muerte/reaparición del
  jugador (`SpawnPoint`+`RespawnTimer`, 3s, restaura HP/posición/InstanceTag). Fase 2
  bloque "A pie" COMPLETADO (P2-05..07 3/3). Verificado con `npc_combat_test` 45s
  headless: 4 ciclos muerte→reaparición sin crash, más regresión de crew_turret_test/
  on_foot_test/flight_test/economy_test. Crash real encontrado y corregido durante la
  verificación (`world.each()` con tag vacío por referencia — ver decisión 2 arriba).
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
