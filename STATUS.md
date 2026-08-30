# STATUS

## Fase activa: Fase 4 — Universo Procedural — **7/8 IMPLEMENTADA, PARADA en P4-08**

P4-01..07 están implementadas y verificadas headless (build limpio + 11 gates de
smoke propios + 11 escenas sin error). **P4-08 (`procedural_test`) es la parada:**
necesita (a) una vía de render multi-malla en el renderer de Vulkan para dibujar
los chunks de terreno de P4-03 — hoy el renderer solo dibuja UNA malla
compartida instanciada, así que esto es trabajo de `vulkan-pipeline-expert` — y
(b) la **RTX 5060 Ti de Miguel** para validar el DoD real de la fase (framerate
objetivo sin pausas al moverse rápido sobre la superficie; lavapipe por software
no puede medir eso). Es el criterio de salida "solo Miguel puede validar" de
Fase 4, igual que cerraron Fases 0/1/2/3. Plan de P4-08 en el mensaje de cierre
de la sesión / `.claude/VERIFICACION-PENDIENTE.md`.

Fase 3 validada físicamente por Miguel el 2026-08-30 (probó las 6 secciones de
`.claude/VERIFICACION-PENDIENTE.md` a mano: `content_smoke_test`, naves/armas por
datos con `--ship=`, hangar + taquillas de traje, protección/EVA con `--suit=`,
localizaciones pobladas + arco "Ashfall Line", y regresión de Fases 1/2 — todo
marcado OK). Con eso se cierra formalmente Fase 3 (9/9) y se abre Fase 4 según
[[10-FASE-4-UNIVERSO-PROCEDURAL]]
(`docs/roadmap/files/10-FASE-4-UNIVERSO-PROCEDURAL.md`).

### Objetivo de Fase 4

Reemplazar la simplificación deliberada de Fase 1D (universo fijo, zonas de
aterrizaje prefabricadas y finitas) por generación real: terreno planetario
esférico procedural con streaming por LOD, más de un sistema estelar navegable, y
suficiente contenido curado insertado sobre lo procedural para que no se sienta
vacío. El sistema fijo de Fase 1 pasa a ser un caso particular (semilla fija) del
generador, no se descarta. Es la fase técnicamente más exigente después de Fase 0.

### Progreso Fase 4 — 7/8

- [x] P4-01 Algoritmo de generación de sistema estelar (semilla → estrella,
      planetas, órbitas) (dep. P1D-02)
- [x] P4-02 Generación procedural de terreno planetario (heightmap por ruido,
      esférico) (dep. —)
- [x] P4-03 Streaming de terreno por chunks con LOD (extiende el streaming de
      P1D-03) (dep. P4-02, P1D-03) — sistema de streaming completo; dibujar los
      chunks en pantalla es el trabajo de renderer de P4-08
- [x] P4-04 Migración del sistema fijo actual a semilla fija del generador (no
      perder contenido) (dep. P4-01)
- [x] P4-05 Navegación entre sistemas (jump points o equivalente) + mapa de
      galaxia simplificado (dep. P4-01)
- [x] P4-06 Distribución procedural de recursos minables (asteroides/superficie)
      (dep. P4-02)
- [x] P4-07 Inserción de puntos de interés curados sobre el terreno procedural
      (estaciones, POIs a mano) (dep. P4-02, P3-04)
- [ ] P4-08 Escena demo `procedural_test`: viaje entre dos sistemas generados +
      aterrizaje en terreno real (dep. P4-01..07) — **PARADA**: render multi-malla
      de terreno en Vulkan (`vulkan-pipeline-expert`) + validación en la RTX de
      Miguel (DoD de framerate). Ver plan abajo.

### Restricciones de arquitectura de Fase 4 (del roadmap, vinculantes)

1. **Todo lo generado es determinista por semilla.** El mismo seed → el mismo
   sistema/planeta/terreno, en cualquier máquina y sesión. Es lo que permite
   guardar (Fase 1F) sin serializar el terreno: se guarda semilla + lista de
   deltas respecto a lo generado (ej. recursos ya minados).
2. **Terreno por chunks = pool de tamaño fijo** (`Pool<TerrainChunk,
   kMaxLoadedChunks>`), nunca contenedores que crezcan sin límite. Si el jugador
   va más rápido de lo que carga el streaming, se degrada el LOD lejano antes que
   exceder la capacidad reservada.
3. **Generación de chunk en hilo de fondo** (extiende el pipeline async de
   P0-07/P1D-03), escribiendo a buffer reservado; la subida a GPU ocurre en el
   hilo principal, igual que el resto de cargas de malla.
4. **Revisar el umbral de floating origin (P1D-07)** explícitamente a distancias
   planetarias/interplanetarias reales; si no aguanta, ajustarlo aquí y
   documentar el cambio — no dejarlo "porque ya funcionaba" en el sistema pequeño.
5. **P4-05 salto entre sistemas** puede ser una transición de carga (descargar
   sistema actual, cargar destino), no simulación continua de viaje interestelar.
6. **P4-06 minería reutiliza `CargoHold`/economía de P1C** — minar = obtener un
   commodity (igual que comprarlo, distinta fuente), no un sistema paralelo.
7. Sigue vigente `.cursorrules`: DOD/ECS estricto, cero heap en Update/Render,
   pools/arenas pre-asignados, GLM vía `include/engine/math/glm.hpp`.

### Definition of Done de Fase 4 (del roadmap)

- `procedural_test` genera dos sistemas estelares distintos de dos semillas
  distintas, con terreno planetario navegable en al menos uno, y permite viajar
  de uno a otro.
- Recargar el mismo seed produce exactamente el mismo sistema/terreno (hash de
  chunks generados o inspección visual reproducible).
- El streaming de terreno mantiene el framerate objetivo de Fase 0 sin pausas
  perceptibles al moverse rápido sobre la superficie (overlay de P0-11).

### Plan de P4-08 (`procedural_test`) — pendiente

Todo lo que consume P4-08 ya existe y está probado headless (P4-01..07). Lo que
falta es el **render del terreno** y el **ensamblaje de la escena**, más la
validación en hardware real. Pasos:

1. **Renderer Vulkan — dibujar N mallas de terreno** (`vulkan-pipeline-expert`).
   Hoy `renderer_upload_mesh` sube UNA malla (`demo_mesh`) dibujada instanciada.
   Añadir a `RendererState` un array fijo `GpuMesh terrain_chunks[kMaxLoadedChunks]`
   + `renderer_upload_terrain_chunk(state, device, slot, MeshCpu)` /
   `renderer_retire_terrain_chunk(state, device, slot)` (retirada diferida N
   frames por los frames-in-flight), y en `renderer_draw_frame`, tras la grid,
   bind `mesh_pipeline` + un buffer de instancia de 1 matriz identidad y
   `vkCmdDrawIndexed` por chunk `GpuReady`. Reutilizar el código de creación de
   VB/IB de `renderer_upload_mesh` factorizado.
2. **Cablear P4-03 al renderer.** El `TerrainStreamer` ya tiene `upload_cb`/
   `retire_cb`/`cb_ctx`: la escena pasa un contexto {renderer, device} y
   callbacks que llaman a las funciones del paso 1, guardando el slot en
   `TerrainChunk.gpu_handle`.
3. **Escena `procedural_test`** (nueva en `scene.cpp`): galaxia (P4-05) con nodo
   0 = home compuesto y al menos otro nodo generado; carga el sistema del nodo
   actual (P4-01/04) con su cinturón de asteroides (P4-06) y POIs (P4-07); por
   cada planeta cercano, un `TerrainStreamer` (P4-02/03) con
   `params = planet_terrain_params(body_seed, radius, has_atmo)`;
   `world::fixed_step`/un sistema nuevo llama a `terrain_streamer_update` con la
   posición del jugador. Un `JumpPoint` interactuable por enlace de galaxia que,
   al pulsar F, hace `galaxy_jump` + reconstruye el sistema en sitio (despawn de
   los `SystemBody` + respawn) — el salto en marcha diferido de P4-05.
4. **Revisar el umbral de floating-origin** (`kFloatingOriginThreshold=2000`) a
   escala planetaria/interplanetaria reales (item del roadmap): con el terreno
   renderizando y volando sobre la superficie, comprobar si 2 km sigue siendo
   razonable; si no, ajustarlo y documentar el cambio.
5. **Verificación de Miguel (RTX 5060 Ti):** `--scene=procedural_test`; volar de
   un sistema a otro por un jump point; aterrizar en un planeta con terreno;
   comprobar con el overlay F1 (P0-11) que el framerate se mantiene sin pausas al
   moverse rápido sobre la superficie; y que relanzar con la misma semilla da el
   mismo sistema/terreno.

### Deuda de schema del save arrastrada a Fase 4 (decidir al empezar el generador)

No serializados aún en el save v1: `ShipSpec` (P3-01), `ShipOwnership` (P3-03),
`Suit` (P3-06), más los de Fase 2 (`ShipSubsystems`, `DamageEvent.zone`/
`CharacterDead`/`RespawnTimer`/`SpawnPoint`, `CompletedMissions`, `CrewMember`).
Fase 4 introduce su propio formato de persistencia (semilla + deltas de terreno),
así que es el momento natural de hacer **un único** bump de `schema_version` que
junte todo esto. Bug latente conocido de baja probabilidad: cargar en la ventana
"muerto, esperando respawn" deja `Health.hp=0` sin `CharacterDead`.

### Decisiones Fase 4 (documentadas)

1. **P4-01 generador de sistema estelar:** módulo nuevo
   `game/world/star_system_gen.{hpp,cpp}` — función pura
   `generate_star_system(u64 seed, StarSystemData& out, GeneratedSystemInfo* info)`
   que **rellena la misma estructura `StarSystemData` de P1D-02** (arrays fijos,
   `kMaxCelestialBodies=16`), sin FILE I/O, sin heap, sin globals. RNG:
   **SplitMix64** (constantes fijas → misma secuencia de bits en cualquier
   plataforma; `next_u64` también sirve para derivar sub-semillas por cuerpo).
   Genera: estrella en el origen del sistema (índice 0, radio 60–150) +
   `[kMinPlanets=2, kMaxPlanets=6]` planetas en **órbitas geométricas**
   (`orbit *= [1.55, 2.10]` cada paso → radios estrictamente crecientes),
   ángulo/inclinación por semilla, radio 110–340, `has_atmosphere` (~60%) y
   `has_landing_zone` (~50%); cada planeta con LZ emite un cuerpo `LandingZone`
   **inmediatamente después** en el array (posición al lado del planeta, cara
   sunward). `GeneratedSystemInfo` expone en paralelo `body_seed` / `orbit_radius`
   / `orbit_angle` / flags para que P4-02 (terreno) / P4-06 (recursos) /
   P4-07 (POIs) reutilicen los mismos números sin re-derivar.
   `generated_system_name(seed)` → `"Sys-XXXXXXXX"` (hex de los 32 bits bajos).
   **Determinismo:** el mismo seed produce salida byte-idéntica en el mismo
   binario (verificado con `memcmp` en el smoke). La bit-identidad
   entre-plataformas de las posiciones depende de `cosf`/`sinf`/`sqrtf` de la
   libm (±1 ULP posible entre libms distintas) — el RNG sí es bit-exacto; la DoD
   pide "reproducible", no ULP-idéntico, así que se acepta y se anota.
   Gancho de prueba: flag `--seed=<n>` (+ `seed=` en config) → `SceneContext.world_seed`
   → `universe_test` genera el sistema en vez de `load_star_system_config` cuando
   está puesto (por defecto sigue el `star_system.cfg` fijo, comportamiento de
   Fase 1D intacto). Gate headless `CSC_SYSTEMGEN_SMOKE=1` (en `setup_universe_test`,
   puro, sin world): 5 semillas → nº de cuerpos en rango, estrella en [0], órbitas
   crecientes, LZ emparejada a su planeta, y re-generar el mismo seed es
   byte-idéntico; semillas distintas no colisionan.
   **Fuera de alcance, anotado:** los sistemas generados no tienen aún estaciones
   ni POIs (los añaden P4-04 migración del fijo + P4-07 curados) — con `--seed=`
   el jugador aparece junto a la estrella (no hay `Station` que ancle el spawn);
   `spawn_universe_test` lo tolera sin crash. Sin órbitas animadas (cuerpos
   colocados estáticos, igual que el sistema fijo hoy).
   Verificado: build limpio (0 warnings) + `CSC_SYSTEMGEN_SMOKE: PASS` + 11 escenas
   headless + los 5 gates de smoke previos PASS + `--seed=12345` reproducible
   (`Sys-00003039`, 8 cuerpos) y distinto de `--seed=99` (`Sys-00000063`, 10).

2. **P4-02 terreno planetario procedural:** módulo nuevo
   `game/world/planet_terrain.{hpp,cpp}` — **librería pura**, sin heap, sin FILE
   I/O, sin globals; el `Rng64` SplitMix64 se extrajo a
   `include/game/world/rng64.hpp` (compartido con P4-01).
   - **Ruido:** Perlin 3D con gradientes de arista (12) hasheados por celda de
     retículo (`SplitMix64`-finalizer sobre `ix/iy/iz ^ seed`), fade quíntico +
     interpolación trilineal; `fbm3` suma `octaves` octavas normalizadas
     (`lacunarity`/`gain`), salida ~[-1,1]. Cero dependencias externas.
   - `PlanetTerrainParams` derivado por `planet_terrain_params(body_seed, radius,
     has_atmosphere)` (usa el `body_seed` que P4-01 ya expone en
     `GeneratedBodyInfo`): `elevation_scale` = 1.5–6% del radio,
     `base_frequency` 1.4–3.4, 4–6 octavas, `sea_level` cerca de 0 si hay
     atmósfera, en el suelo si es roca seca.
   - `planet_height(pp, unit_dir)` → offset de elevación **acotado a
     `[-elevation_scale, +elevation_scale]`** (fbm clampeado a [-1,1] antes de
     escalar; el aplanado bajo `sea_level` solo reduce magnitud). Verificado en el
     smoke.
   - `cube_sphere_dir(face, s, t)` — mapeo cubo→esfera de 6 caras con el
     "spherify" de Cobe/Rideout (menos distorsión de área) + normalize final.
   - `build_planet_patch(pp, center, TerrainPatchSpec, MeshVertex*, u32&, u32*,
     u32&)` — rellena **buffers fijos del llamante** (`kMaxPatchResolution=64` →
     `kMaxPatchVertices`/`kMaxPatchIndices`) con un parche `(N+1)²` de una
     sub-rect de una cara del cubo-esfera, desplazado por `planet_height`,
     normales por diferencia finita tangente, color por rampa de altura
     (agua/playa/hierba/roca/nieve), uv = (s,t). **Es la unidad exacta que P4-03
     va a streamear** a un `Pool<TerrainChunk>`.
   - Gate headless `CSC_PLANETGEN_SMOKE=1` (en `setup_universe_test`, junto al de
     P4-01): 4 semillas → altura dentro de `elevation_scale`, normales unitarias,
     re-generar un parche es byte-idéntico (`memcmp`), y dos parches que comparten
     un borde de cara coinciden **exactamente** en los vértices de la costura.
   **Fuera de alcance, anotado:** aún no hay render del terreno — el renderer solo
   tiene una malla compartida instanciada (`renderer_upload_mesh` → `demo_mesh`),
   y una malla de terreno propia necesita soporte multi-malla / buffers a medida
   en Vulkan (`vulkan-pipeline-expert`), que llega con P4-03 (streaming) y P4-08
   (escena `procedural_test`). Determinismo byte-exacto en el mismo binario;
   posiciones vía `cosf`/`sinf`/`sqrtf` de libm (±1 ULP entre libms distintas).
   Verificado: build limpio (0 warnings) + `CSC_PLANETGEN_SMOKE: PASS` + 11
   escenas headless + los 6 gates de smoke previos PASS + `validate_catalogs.py` OK.

3. **P4-03 streaming de terreno por chunks con LOD:** módulo nuevo
   `game/world/terrain_stream.{hpp,cpp}` — el **sistema de streaming completo**;
   dibujar los chunks en pantalla necesita soporte multi-malla en el renderer de
   Vulkan y se hace al ensamblar P4-08 (`upload_cb`/`retire_cb` son la costura).
   - **`select_terrain_lod(params, center, player_pos, out, max, &depth)`** —
     pura, determinista: quadtree cubo-esfera, 6 raíces ordenadas por distancia
     (detalle cerca del jugador reclama el presupuesto primero), subdivide un
     nodo mientras `dist < radio*3 / 2^depth`, hasta `kMaxLodDepth=6`; emite
     hojas como `TerrainPatchSpec` (rect de cara + resolución fija
     `kTerrainChunkQuads=24`; el "LOD" es el tamaño del rect). Cap
     `kMaxDesiredSpecs=128`.
   - **`memory::Pool<TerrainChunk, kMaxLoadedChunks=48>`** — nunca crece; los
     buffers CPU (`verts[625]`/`indices[3456]`) viven **en el slot**, reservados
     de antemano (~2 MB estático total). Si el conjunto deseado no cabe, los
     parches más lejanos simplemente no consiguen slot (cobertura lejana más
     gruesa) en vez de exceder capacidad — se procesan los deseados
     nearest-first.
   - **Hilo de fondo** (`worker_loop`, un `std::thread` persistente + cola de
     índices bajo `mutex`/`condvar`): saca chunks `Queued`, `CAS Queued→Generating`,
     corre `build_planet_patch` (P4-02) en los buffers del chunk, `→ CpuReady`.
     El **hilo principal** (`terrain_streamer_update`, barato, por frame):
     reselecciona LOD, marca `wants_retire` los chunks no deseados (los libera
     cuando el worker ya no los toca), encola los nuevos (nunca pasa de
     `kMaxLoadedChunks`), y drena `CpuReady → upload_cb → GpuReady`. Sincronía
     por `state` atómico (acquire/release) + `wants_retire` atómico + "no liberar
     un slot en `Generating`".
   - Gate headless `CSC_TERRAINSTREAM_SMOKE=1` (en `setup_universe_test`): en una
     aproximación al planeta el LOD se refina (profundidad y nº de chunks
     crecientes), el pool nunca supera `kMaxLoadedChunks`, todo chunk residente
     llega a `GpuReady` con malla no vacía, la selección de LOD es determinista
     para una posición dada, y el shutdown hace `join` limpio. Pasa 5/5 en
     repeticiones (sin flakiness observada; no verificado con ThreadSanitizer).
   **Fuera de alcance, anotado:** (a) render de los chunks → P4-08 (necesita
   `vulkan-pipeline-expert`: hoy el renderer solo dibuja una malla compartida
   instanciada); (b) revisar el umbral de floating-origin a escala planetaria
   (item del roadmap) — se hace con P4-08, cuando se vuela de verdad sobre la
   superficie; (c) sin morphing/geomorph entre niveles de LOD (pop al cambiar
   de chunk) — pulido de Fase 6.
   Verificado: build limpio (0 warnings) + `CSC_TERRAINSTREAM_SMOKE: PASS` (x5) +
   11 escenas headless + los 7 gates de smoke previos PASS + `validate_catalogs.py`
   OK.

4. **P4-04 sistema fijo = semilla + overlay curado:** `star_system.cfg` gana una
   clave opcional `base_seed=<n>` (añadida: `20260830`). Con ella,
   `load_star_system_config` genera primero la base procedural
   (`generate_star_system`, P4-01 → estrella + planetas + LZs) y **luego
   superpone los `body.N.*`** del archivo: un cuerpo curado cuyo `name` coincide
   con uno generado lo reemplaza; si no, se añade (hasta `kMaxCelestialBodies`).
   Caso especial: un `Star` curado reemplaza al generado sin importar el nombre
   (un sistema = una estrella). Sin `base_seed` → comportamiento 100% a mano de
   antes (retrocompatible). Resultado por defecto en `universe_test`: "Sistema-01"
   = 5 generados + 7 curados (1 merge de estrella + 6 append) = 11 cuerpos, con
   Estacion-Alfa/Beta navegables intactas → **nada de contenido perdido**.
   `--seed=<n>` sigue dando un sistema 100% generado (sin overlay). Gate headless
   `CSC_FIXEDSYS_SMOKE=1` (en `setup_universe_test`): el sistema compuesto tiene
   los 4 nombres curados clave + un cuerpo `Sys-*` generado + >7 cuerpos, y dos
   cargas son byte-idénticas.
   Verificado: build limpio (0 warnings) + `CSC_FIXEDSYS_SMOKE: PASS` + 9 escenas
   headless + `CSC_SAVE_SMOKE`/`CSC_FORCE_REBASE_SMOKE` OK.

5. **P4-05 navegación entre sistemas + mapa de galaxia:** módulo nuevo
   `game/world/galaxy.{hpp,cpp}` — modelo puro y determinista.
   `generate_galaxy(galaxy_seed, home_system_seed, GalaxyMap&)`: 5–9 sistemas
   dispersos en un disco (`Rng64`), **nodo 0 = el sistema home** (su `seed` es
   `kHomeSystemSeed=20260830`, el mismo `base_seed` de `star_system.cfg` de P4-04
   → el sistema fijo ES el nodo 0). Cada sistema enlaza con sus **2 vecinos más
   cercanos** (enlaces simétricos), y una pasada BFS+enlace-al-más-cercano
   **garantiza que el grafo es conexo**. `galaxy_jump(g, target)` valida
   adyacencia antes de mover `g.current` (salto = una transición de carga, como
   permite el roadmap — no simulación de viaje). `galaxy_current_seed`,
   `galaxy_log` (vuelca el grafo — el "mapa simplificado" de esta fase; una vista
   de mapa real es pulido de Fase 6).
   Integración: `setup_universe_test` construye la galaxia, elige el nodo con
   `--system=<n>` (flag nuevo + `system=` en config; por defecto 0), guarda
   `GalaxyMap` como singleton y loguea el grafo. Nodo 0 → carga el sistema
   compuesto de P4-04; nodo >0 → `generate_star_system(nodo.seed)` puro.
   `--seed=<n>` (P4-01) sigue mandando por encima. Gate headless
   `CSC_GALAXY_SMOKE=1`: determinismo (`memcmp`), enlaces simétricos, grafo
   conexo (BFS), nodo 0 = home, y `galaxy_jump` acepta adyacentes / rechaza el
   resto + ida y vuelta.
   **Fuera de alcance, anotado:** el salto **en marcha** (volar a un jump point,
   pulsar F, cambiar de sistema sin reiniciar) lo ensambla P4-08 — reconstruir
   todos los cuerpos + nave + floating origin en vivo es su trabajo; aquí el
   salto es `--system=<n>` al arrancar. El nombre del nodo 0 en el `galaxy_log`
   es el derivado de la semilla (`Sys-XXXX`); el sistema cargado conserva su
   nombre curado ("Sistema-01").
   Verificado: build limpio (0 warnings) + `CSC_GALAXY_SMOKE: PASS` + 11 escenas
   headless + los 8 gates de smoke previos PASS + `validate_catalogs.py` OK +
   `--system=2` carga el nodo 2 generado.

6. **P4-06 recursos minables procedurales:** módulo nuevo
   `game/world/resources.{hpp,cpp}` — **extensión de la economía de P1C, no un
   sistema paralelo:** minar = meter unidades de un commodity en el
   `economy::CargoHold` de la nave (las mismas que comprarías, otra fuente).
   `generate_asteroid_field(system_seed, commodity_count, out, out_pos, max)`:
   determinista, 8–`kMaxDeposits=24` depósitos en un cinturón a 4–9 km de la
   estrella; `commodity_id` sesgado (75% mineral / id 0, 25% uno raro),
   `total` 80–400 u, `yield` 2.5–5 u/s. `spawn_asteroid_field` los instancia como
   marcadores con `ResourceDeposit`. `mine_deposit(d, hold, table, dt)` puro:
   transfiere unidades enteras (acumulador `carry` para sub-unidad), respeta
   `remaining` y la capacidad de bodega (`cargo_add` todo-o-nada → prueba con
   cantidad decreciente), marca `remaining=0` al agotarse. `update_mining(world,
   dt)` (en `world::fixed_step`): cualquier `PlayerShip` **parada** (velocidad <
   `kDepositMaxShipSpeed=12`) dentro de `kDepositMineRange=45` mina el depósito a
   su bodega; los agotados ganan `DepositDepleted` (add diferido tras el
   `world.each`, no mid-iteration). `universe_test` ahora llama
   `load_economy_data` (para que la minería funcione en escena) y
   `spawn_universe_test` recibe el `system_seed` para sembrar el cinturón.
   Gate headless `CSC_RESOURCES_SMOKE=1`: campo determinista (`memcmp`),
   commodity ids válidos, y `mine_deposit` **conserva unidades** (50 minadas = 50
   en bodega, depósito a 0) y una bodega llena corta la minería sin perder el
   depósito.
   **Fuera de alcance, anotado:** minería por proximidad pasiva (sin láser, sin
   apuntado, sin minijuego) — un pilar de minería propiamente dicho es contenido/
   pulido posterior. Sin depósitos en superficie planetaria todavía (solo
   cinturón de asteroides); la superficie se apoya en el terreno de P4-02/03 que
   aún no se renderiza.
   Verificado: build limpio (0 warnings) + `CSC_RESOURCES_SMOKE: PASS` + 11
   escenas headless + los 9 gates de smoke previos PASS + `validate_catalogs.py` OK.

7. **P4-07 POIs curados sobre el universo procedural:** módulo nuevo
   `game/world/poi_catalog.{hpp,cpp}` + `assets/data/pois.cfg` (`[[poi]]`) →
   singleton `PoiCatalog` de `PoiDef` (`kMaxPoiDefs=24`). Cada POI: `id` único,
   `kind` (station/outpost/beacon/wreck), `system` (`home` o una semilla), y una
   de tres colocaciones — `orbit` (radio/ángulo/y), `planet_surface` (Nº de
   planeta + lat/lon + altitud → dirección sobre la esfera del planeta,
   `centro + dir*(radio + altitud)`), `absolute` (`x,y,z`). Opcional
   `location_id`: si resuelve en el `LocationCatalog` de P3-04, se pueblan sus
   NPCs en la posición del POI (`economy::populate_locations` con un solo
   placement) — el puente "estación curada = localización poblada".
   `load_poi_catalog` en `scene_setup_by_name`; `spawn_pois_for_system(world,
   system_seed, data)` en `spawn_universe_test` instancia los POIs cuyo sistema
   coincide. `pois.cfg` arranca con 4 en el sistema home (relé, outpost, pecio,
   campamento en superficie). Gate `CSC_POI_SMOKE=1`: catálogo carga, ids únicos,
   `planet_index` plausible, colocación determinista.
   **Fuera de alcance, anotado:** `planet_surface` usa el radio de esfera media +
   altitud; encajar el POI a la altura exacta del terreno (`planet_height` de
   P4-02) es de P4-08, cuando el terreno se renderiza. Los POI de tipo `station`
   se instancian como marcadores + (si hay `location_id`) NPCs; el interior
   navegable completo lo ensambla P4-08.
   Verificado: build limpio (0 warnings) + `CSC_POI_SMOKE: PASS` + 11 escenas
   headless + los 10 gates de smoke previos PASS + `validate_catalogs.py` OK.

---

## Fase 3 — Expansión de Contenido — **COMPLETADA y validada** (9/9, validación física 2026-08-30)

Todo el contenido de la fase vive en `assets/data/*.cfg` (naves, armas, trajes,
misiones, localizaciones); ningún `.cpp`/`.hpp` se tocó para el contenido, solo
para el pipeline que lo lee. `content_smoke_test` (escena + `CONTENT_SMOKE: PASS`)
carga todo el catálogo por los loaders reales y valida integridad; el gemelo sin
build es `tools/validate_catalogs.py` (P3-08). Escenas nuevas: `ship_hangar_test`
(P3-03/P3-06), `content_smoke_test` (P3-09). Flags nuevos: `--ship=<id>` (P3-02),
`--suit=<id>` (P3-06). Decisiones completas 1-9 abajo.

### Progreso Fase 3 — 9/9 COMPLETADO

- Pipeline de datos (P3-01, P3-05, P3-08):       [###] 3/3
  - [x] P3-01 Pipeline de datos de nave: stats (masa, empuje, hardpoints, capacidad
        de carga) en archivos de configuración, no en código (dep. P1A-01, P0-05)
  - [x] P3-05 Catálogo de armas/equipamiento dirigido por datos, mismo patrón que
        P3-01 (dep. P1A-08, P1B-06)
  - [x] P3-08 Herramienta interna (CLI/script) para validar y/o generar entradas de
        catálogo antes de compilar (dep. P3-01, P3-05)
- Catálogo y naves jugables (P3-02, P3-03):      [##] 2/2
  - [x] P3-02 Catálogo de 4-6 tipos de nave (caza, carguero, exploración,
        multipropósito) usando P3-01 (dep. P3-01)
  - [x] P3-03 Hangar y tienda de naves: comprar/vender/cambiar de nave activa,
        reutilizando las transacciones de P1C-03 tal cual (dep. P3-02, P1C-03)
- Mundo y personaje (P3-04, P3-06):              [##] 2/2
  - [x] P3-04 Expansión de estaciones/ciudades: más NPCs, tiendas y dadores de
        misión por localización (dep. P1D-05, P1C-06)
  - [x] P3-06 Personalización básica de personaje: trajes/armadura con stats
        (protección, capacidad EVA) (dep. P1B-01, P3-05)
- Misiones y verificación (P3-07, P3-09):        [##] 2/2
  - [x] P3-07 Más contenido de misiones: plantillas adicionales + una línea
        narrativa simple opcional (dep. P1C-04, P2-09)
  - [x] P3-09 Verificación `content_smoke_test`: carga todo el catálogo y valida
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

### Definition of Done de Fase 3 (del roadmap) — estado

- ✅ `content_smoke_test` carga el catálogo completo (naves + armas + suits +
  localizaciones + commodities + mercados + misiones) sin errores de validación y
  reporta el resumen por tipo en consola (`CONTENT_SMOKE: PASS`, escena
  `--scene=content_smoke_test`). P3-09.
- ✅ El jugador puede comprar al menos **dos naves distintas** del catálogo en el
  hangar (`ship_hangar_test`) y pilotarlas con diferencias perceptibles — validado
  físicamente por Miguel el 2026-08-30 (Wasp / Mule / Kestrel).
- ✅ Añadir contenido nuevo (nave / arma / suit / misión / localización) se hace
  **solo** editando el `.cfg` correspondiente. Probado en la práctica en P3-02
  (4 hulls), P3-05 (3 armas), P3-06 (4 suits), P3-07 (7 misiones), P3-04 (Outpost C
  + MarketC) — ningún `.cpp`/`.hpp` tocado para el contenido, solo para el
  pipeline que lo lee.

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

3. **P3-03 hangar de naves:** singleton `flight::ShipOwnership` (lista de ids
   poseídos + `active_id`) + componente `flight::ShipDealer{id, price}` en un
   kiosco interactuable (`spawn_ship_dealer`, misma forma que un NPC de economía).
   Un solo kiosco por nave, **sensible al contexto** al pulsar F (dispatch en
   `character::handle_interact_events`, antes del hook de economía): no poseída →
   comprar (si `PlayerWallet.credits >= price`) y equipar; poseída y no activa →
   cambiar la nave activa; poseída y activa → venderla (reembolso
   `price * kShipResaleFraction`, 0.6) y volver a la de inicio — la de inicio
   (`kDefaultPlayerShipId`) nunca se puede vender. **No hay entidad nueva por
   cambio de nave** (respeta `.cursorrules`: ninguna nave se `destruct`a en vivo):
   `apply_ship_def_to_player` **reescribe in situ** todos los componentes de stat
   del `PlayerShip` vivo desde el `ShipDef` (masa/inercia, thrusters, planta,
   escudo, casco, subsistemas, `ShipSpec`, montajes, escala, capacidad de bodega),
   conservando pose, velocidad y **contenido** de la bodega; casco/escudo se
   rellenan al nuevo máximo. `spawn_player_ship` se refactorizó para crear la
   entidad solo con lo de una vez (pose, tags, bodega) y delegar el resto en el
   mismo `apply_ship_def_components` — una nave comprada queda configurada
   exactamente igual que una recién spawneada. La compra/venta usa `PlayerWallet`
   de P1C tal cual (una nave = ítem con precio), sin comercio paralelo.
   Escena nueva `ship_hangar_test`: cubierta con gravedad, jugador a pie, 4
   kioscos (Wasp/Mule/Pathfinder/Bulwark) + escotilla y asiento de piloto para
   embarcar y volar la nave reconfigurada. Gate headless `CSC_HANGAR_SMOKE=1`
   (patrón `CSC_SAVE_SMOKE`): compra el caza y comprueba cartera + masa del
   `PlayerShip` vivo, lo vende y comprueba la reversión, y comprueba que la nave
   de inicio no se vende — `CSC_HANGAR_SMOKE: PASS`.
   **Fuera de alcance, anotado:** `ShipOwnership` (como `ShipSpec`) no está en el
   save v1 — mismo bump de schema pendiente; cargar una partida deja la nave
   activa que spawnee la escena, no la que estuviera equipada al guardar.

4. **P3-05 catálogo de armas:** mismo patrón que P3-01 — `assets/data/weapons.cfg`
   (`[[weapon]]`) → singleton `flight::WeaponCatalog` de `WeaponDef` POD
   (`kMaxWeaponDefs=24`, sin heap; campos: damage/cooldown/energy_cost/heat_max/
   range/hitscan). `id` de texto único (`"weapon.<montaje>.<nombre>"`); duplicado/
   ausente = carga `log_error` INVÁLIDA (dura → P3-09). `ShipDef` gana
   `weapon_id[kMaxWeaponMounts][]` (`weapon_id_N` en `ships.cfg`); vacío →
   `kDefaultShipWeaponId` (`weapon.fixed.repeater`). `apply_ship_def_components`
   (P3-01/P3-03) y `spawn_turret` (nuevo parámetro `weapon_id` opcional, default
   `weapon.turret.repeater`) copian las stats del `WeaponDef` sobre el
   `WeaponMount` vivo; sin catálogo o id desconocido → arma fija de Fase 1/2
   (mismo contrato de fallback que P3-01). `scene_setup_by_name` carga
   **weapons antes que ships** y luego corre `validate_ship_weapon_refs`:
   cada `weapon_id` de cada nave debe resolver en el `WeaponCatalog` o se emite
   `log_error` "ship 'X' mount N references unknown weapon 'Y'" +
   "reference check FAILED" (esta es la comprobación de "referencias resolubles"
   del roadmap; la dura que bloquea build sigue siendo P3-09). Contenido:
   4 armas — `weapon.fixed.repeater` (== gun hardcodeado Fase 1/2: 60/0.22/12/500),
   `weapon.fixed.cannon` (130/0.7/30/650), `weapon.fixed.laser` (32/0.1/8/800,
   hitscan), `weapon.turret.repeater` (== torreta Fase 2: 35/0.5/10/350). Las 6
   naves referencian armas del catálogo (Kestrel/Wasp/Skiff repeater, Mule/Bulwark
   cannon, Pathfinder laser) → cero cambio de comportamiento para las de Fase 1/2.
   **Fuera de alcance, anotado:** el catálogo de ítems a pie (`character.cpp`
   `kItemCatalog`: Rifle/Pistola/Medkit/Munición) sigue siendo una tabla `constexpr`
   en C++ — se pasará a datos con P3-06 (trajes/armadura) o P3-09 si hace falta,
   mismo patrón. `velocidad de proyectil` sigue siendo la global `kProjectileSpeed`.
   Verificado: build limpio (0 warnings) + 10 escenas headless (`ship<->weapon
   references OK` en todas) + `CSC_SAVE_SMOKE`/`CSC_HANGAR_SMOKE` PASS + test
   negativo (`weapon_id_0` colgante inyectado → 2 `log_error`, sin crash).

5. **P3-08 validador de catálogos:** `tools/validate_catalogs.py` — script Python 3
   autónomo (solo stdlib), **sin build**, que recorre `assets/data/*.cfg` con un
   parser del mismo formato `[[sección]] clave=valor` y corre las validaciones que
   hará `content_smoke_test` (P3-09): ids únicos por catálogo; naves y armas
   siguen la convención de id de texto (aviso); `weapon_id_N` de cada nave resuelve
   en `weapons.cfg` (error); existen los ids que el motor spawnea por nombre
   (`ship.player.default`, `ship.npc.skiff`, `weapon.fixed.repeater`,
   `weapon.turret.repeater`); `weapon_mounts`/`turret_hardpoints` dentro de tope;
   `inertia`/`subsystem_hp` con el nº de valores correcto; ids de commodity
   contiguos desde 0; `stock_N`/`price_mod_N`/`rate_N` de mercado indexan un
   commodity existente; misiones con `type` válido y `commodity_id`/`from_market`/
   `to_market`/`requires_completed_id` resolubles, `faction_id`/`target_faction_id`
   en `[0,4)`. Salida: resumen con nº de entradas por catálogo + líneas
   `WARN`/`ERROR` + veredicto `OK`/`FAILED`; exit 0/1 (los avisos nunca fallan).
   Los topes (`kMaxShipDefs`, `kMaxWeaponMounts`, …) están replicados arriba del
   script con un comentario de "mantener en sync" con los headers.
   Descubrimiento: `python3 tools/validate_catalogs.py` directamente, o
   `cmake --build build --target validate_catalogs` (target `add_custom_target`
   opcional, solo si `python3` está en el PATH — **no** es dependencia del build
   normal). Verificado: pasa limpio sobre los catálogos actuales (4 armas / 6
   naves / 3 commodities / 2 mercados / 6 misiones), y un test negativo con
   5 fallos inyectados (id de nave duplicado, `weapon_id` colgante, arma default
   ausente, misión→mercado inexistente, commodity id no contiguo) los detecta
   todos con exit 1.

6. **P3-04 población de localizaciones dirigida por datos:** `assets/data/locations.cfg`
   (`[[location]]` + lista `npc.N.*`) → singleton `economy::LocationCatalog` de
   `LocationDef` POD (`kMaxLocationDefs=12`, `kMaxLocationNpcs=16`, sin heap).
   **Reparto de responsabilidades:** la escena decide DÓNDE va cada localización
   (su origen en el mundo, vía una tabla `LocationPlacement`); el `.cfg` decide QUÉ
   la puebla (traders / dadores de misión / turn-in / travel pads). `id` de texto
   único (`"loc.<nombre>"`); duplicado/ausente = carga `log_error` INVÁLIDA.
   `economy::populate_locations(world, placements, n)` recorre las localizaciones
   colocadas, spawnea sus NPCs con `spawn_trader_npc`/`spawn_mission_npc`/
   `spawn_travel_pad` (P1C-06, reutilizados tal cual) en `origen + npc.offset`, y
   con `gravity_deck=1` añade la zona de gravedad + marcador de cubierta. Los
   `travel_pad` referencian el destino **por id de localización** (`npc.N.dest`),
   resuelto contra la tabla de placements → `destino.origen + kLocationArrivalOffset`
   (`{0,0.9,1.5}`, igual que los pads a mano de P1C) y el `location_id` entero del
   destino (para `mark_visit_missions`).
   `setup_economy_test_scene` pierde ~120 líneas de `spawn_*` a mano: ahora coloca
   3 localizaciones y llama a `populate_locations`. `loc.market-a` (5 NPCs) y
   `loc.market-b` (8 NPCs) **reproducen exactamente** el set de P1C/P2-09/P2-10
   (mismos market/template/flags/offsets) → cero cambio en la coreografía de ramas.
   Expansión P3-04: **`loc.outpost-c`** (5 NPCs) — nuevo `MarketC` (id=2) en
   `markets.cfg` (fab de electrónica: electrónica barata, compra mineral), 2
   traders + un dador de misión "Scout Contract" (template 1) + pads a A y B; A y B
   ganan cada uno un pad "Travel -> C". `spawn_market_marker` (estático, ahora sin
   uso) eliminado de `economy.cpp`.
   El catálogo se carga en `scene_setup_by_name` junto a los de nave/arma;
   `validate_catalogs.py` (P3-08) gana los chequeos de `locations.cfg` (kinds
   válidos, `market`/`commodity`/`template` resolubles, `dest` de travel pad es
   una localización conocida). Las estaciones del sistema fijo (`universe_test`)
   siguen sin poblar — ese escenario es ShipPilot sin personaje a pie, poblarlas no
   sería interactuable; queda para cuando haya modo a pie allí.
   Verificado: build limpio (0 warnings) + 10 escenas headless (economy_test
   pobla 3 localizaciones / 18 NPCs sin crash) + `CSC_SAVE_SMOKE`/`CSC_HANGAR_SMOKE`
   PASS + `validate_catalogs.py` OK + test negativo (`dest` colgante y `market`
   inexistente en `locations.cfg` → 2 `ERROR`, exit 1).

7. **P3-06 trajes/armadura dirigidos por datos:** `assets/data/suits.cfg` (`[[suit]]`)
   → singleton `character::SuitCatalog` de `SuitDef` POD (`kMaxSuitDefs=16`, sin
   heap); stats: `damage_reduction` [0,0.95], `eva_capacity`, `eva_drain_per_sec`,
   `eva_recharge_per_sec`, `move_speed_mult`. Id de texto único; duplicado/ausente
   = carga `log_error` INVÁLIDA. **Nuevo catálogo, no un refactor** del
   `kItemCatalog` a pie (que sigue hardcodeado — anotado en la decisión 4).
   Componente nuevo `character::Suit` (copia del `SuitDef` + `eva_charge` runtime,
   el único campo mutable). `spawn_player_character` gana `suit_id` opcional
   (`nullptr` → `suit.flight.standard`, cuyos valores dejan Fase 1B **sin cambio**:
   0% reducción, EVA generosa, velocidad normal); id desconocido → baseline con
   warning. Flag `--suit=<id>` (+ `suit_id=` en config) plumbed igual que
   `--ship=` (P3-02): `AppConfig`/`SceneContext.player_suit_id` → las 5 escenas a
   pie de `scene.cpp`. `economy_test` no lo cablea (su `spawn_player_character`
   está en `economy.cpp`, sin ctx — mismo criterio que `--ship=`).
   **Ganchos:** (1) protección — `flight::apply_damage_events` multiplica el daño
   ya zonificado (P2-07) por `(1 - clamp(suit.damage_reduction, 0, 0.95))` cuando
   el objetivo lleva `Suit`; (2) EVA — la locomoción de `character::fixed_step`
   drena `eva_charge` (`eva_drain_per_sec`) mientras se empuja en EVA y **corta el
   empuje a 0 de carga**, recarga (`eva_recharge_per_sec`) con gravedad/suelo;
   (3) velocidad — el andar usa `cc.move_speed * suit.move_speed_mult`. NPCs sin
   `Suit` → ilimitado / normal, como antes.
   **Locker en juego:** componente `character::SuitLocker{suit_id}` + dispatch en
   `handle_interact_events` (antes del kiosco de nave); `spawn_suit_locker`.
   `ship_hangar_test` gana 4 lockers (Explorer/Heavy/Scout/Standard) detrás del
   jugador — gratis, es tu taquilla, no una tienda. Gate `CSC_SUIT_SMOKE=1`
   (patrón `CSC_HANGAR_SMOKE`): equipa cada traje del catálogo y comprueba que el
   `Suit` vivo coincide con el `SuitDef` (carga EVA llena), y que un id
   inexistente se rechaza sin tocar el traje.
   4 trajes: `suit.flight.standard` (baseline), `suit.eva.explorer` (10% red.,
   EVA 240, más lento), `suit.armor.heavy` (45% red., EVA 55, x0.8),
   `suit.light.scout` (0% red., x1.2). `validate_catalogs.py` (P3-08) gana los
   chequeos de `suits.cfg` (id único, rangos, presencia del traje por defecto).
   **Fuera de alcance, anotado:** `Suit` no se serializa en el save v1 (misma
   deuda de schema que `ShipSpec`/`ShipOwnership`); no hay HUD de armadura/EVA aún
   (visible por efecto y por el log `Player suit: ...` / `Locker: equipped ...`).
   Verificado: build limpio (0 warnings) + 10 escenas headless + `CSC_SUIT_SMOKE`/
   `CSC_HANGAR_SMOKE`/`CSC_SAVE_SMOKE` PASS + `validate_catalogs.py` OK +
   `--suit=` aplica (log `armour 45%`).

8. **P3-07 más misiones + línea narrativa:** 7 plantillas nuevas en
   `mission_templates.cfg` (ids 6-12; total 13/16). `MissionTemplate` gana un
   campo `title` (`kMissionTitleBytes=80`, clave `title=`) — una frase de sabor
   **opcional**, no serializada (vive solo en la tabla de plantillas), que
   `handle_interact` registra en el log al aceptar la misión. **Arco narrativo
   "The Ashfall Line"** (ids 6→7→8→9, encadenados con `requires_completed_id`,
   `branch_group=0` — no bloquea nada): Silent Beacon (visit) → Patch the Relay
   (delivery, requiere 6) → Ashfall Raiders (combat, requiere 7) → Ashfall
   Debrief (visit, requiere 8). Reutiliza P1C/P2-09/P2-10 tal cual, cero mecánica
   nueva. Más 3 misiones sueltas siempre ofertables (Medbay Resupply / Survey Run
   / Pirate Sweep). Los NPC dadores/turn-in se añaden vía `locations.cfg` (P3-04):
   MarketA +2, MarketB +2, Outpost C +4 (givers 7/8/9 + un turn-in de mercado 2)
   → `economy_test` pasa de 18 a 26 NPCs en 3 localizaciones. Gate headless
   `CSC_MISSION_SMOKE=1` (en `setup_economy_test_scene`): comprueba que el
   catálogo carga, que el arco tiene la forma de cadena correcta (6 libre, 7←6,
   8←7, 9←8) y que cada paso lleva `title`; simula completar 6 y verifica que 7 se
   abre y 8 sigue cerrado. `validate_catalogs.py` (P3-08) ya valida
   `requires_completed_id` resoluble y los refs de `template` en `locations.cfg`.
   **Fuera de alcance, anotado:** sin HUD de descripción de misión (el `title`
   solo sale por log al aceptar). El chequeo de ciclos en las cadenas
   `requires_completed_id` lo añade P3-09. Verificado: build limpio
   (0 warnings) + 10 escenas headless (`economy_test` 26 NPCs) + `CSC_MISSION_SMOKE`/
   `CSC_SAVE_SMOKE`/`CSC_HANGAR_SMOKE`/`CSC_SUIT_SMOKE` PASS + `validate_catalogs.py`
   OK (13 misiones).

9. **P3-09 `content_smoke_test`:** verificación de integridad de TODO el catálogo
   a través de los loaders y componentes reales del motor (complementa a
   `validate_catalogs.py` de P3-08, que hace lo mismo pero sin build — con ambos,
   un desajuste entre el formato del archivo y el parser del motor también se
   pilla). Nuevo módulo `game/content_smoke.{hpp,cpp}`:
   `content::run_content_smoke_test(world)` lee los 7 singletons de catálogo,
   loguea el resumen `content: N ships, N weapons, N suits, N locations, N
   commodities, N markets, N missions` y delega en tres validadores de dominio —
   `flight::validate_ship_weapon_refs` (ya existía), nuevo
   `character::validate_suit_catalog` (no vacío, ids únicos, rangos, traje por
   defecto presente) y nuevo `economy::validate_economy_content` (commodities
   únicos+contiguos; mercados únicos; plantillas de misión únicas, con
   `commodity_id`/`from_market`/`to_market`/`requires_completed_id` resolubles y
   **sin ciclos** en las cadenas de prerequisito; cada NPC de `locations.cfg`
   resuelve su market/commodity/template/dest). También comprueba los ids que el
   motor spawnea por nombre (`ship.player.default`, `ship.npc.skiff`,
   `weapon.fixed.repeater`, `weapon.turret.repeater`). Salida:
   `CONTENT_SMOKE: PASS|FAIL`.
   Escena nueva `content_smoke_test` (sin render): carga `load_economy_data` +
   corre el chequeo. `validate_catalogs.py` (P3-08) gana el mismo chequeo de
   ciclos para no divergir.
   Verificado: `CONTENT_SMOKE: PASS` sobre los catálogos actuales
   (6/4/4/3/3/3/13), 11 escenas headless sin error, los 5 gates de smoke PASS
   (`CONTENT`/`HANGAR`/`SUIT`/`MISSION`/`SAVE`), y test negativo con 3 fallos
   inyectados a la vez (arma colgante en una nave, ciclo en las misiones, market
   inexistente en un NPC de localización) → `CONTENT_SMOKE: FAIL` con las 3 causas
   listadas; `validate_catalogs.py` detecta el ciclo igual (exit 1).

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
| `economy_test` | P1C/P3-04/P3-07 — 3 localizaciones / 26 NPCs desde `locations.cfg`; arco "Ashfall Line"; CSC_MISSION_SMOKE |
| `universe_test` | P1D OK — Estacion-Alfa → espacio (rebase ≥1) → Planeta-01-LZ |
| `ui_audio_test` | P1E OK — HUD Both, Esc pause, 3 positional sine tones (synthetic PCM) |
| `save_load_test` | P1F OK — F5/F9 + pause Save/Load; CSC_SAVE_SMOKE |
| `ship_hangar_test` | P3-03/P3-06 — kioscos de nave + taquillas de traje + asiento piloto; CSC_HANGAR_SMOKE / CSC_SUIT_SMOKE |
| `content_smoke_test` | P3-09 — carga todo el catálogo + chequeo de integridad; `CONTENT_SMOKE: PASS/FAIL` |

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
- 2026-08-30 [Fase 4 7/8 — PARADA P4-08] P4-01..07 implementadas y verificadas
  headless (11 gates de smoke propios + 11 escenas). **P4-08 (`procedural_test`)
  queda parado:** necesita render multi-malla de terreno en Vulkan
  (`vulkan-pipeline-expert` — hoy el renderer solo dibuja una malla compartida
  instanciada) + validación del DoD de framerate en la RTX de Miguel. Plan de 5
  pasos en la sección "Plan de P4-08" de arriba. Push de los commits P4-03..07 a
  `origin/main`.
- 2026-08-30 [P4-07] POIs curados sobre el universo procedural:
  `game/world/poi_catalog.{hpp,cpp}` + `assets/data/pois.cfg` (`[[poi]]`) →
  `PoiCatalog`. Colocación por `orbit` / `planet_surface` (lat/lon sobre la
  esfera del planeta) / `absolute`; `location_id` opcional puebla NPCs de P3-04
  en el POI. `load_poi_catalog` en `scene_setup_by_name`;
  `spawn_pois_for_system` en `spawn_universe_test`. 4 POIs de arranque en el
  sistema home. Gate `CSC_POI_SMOKE=1` PASS (ids únicos, planet_index plausible,
  colocación determinista). Encaje exacto a la altura del terreno → P4-08.
  Verificado: build 0 warnings + POI_SMOKE PASS + 11 escenas + 10 smokes previos
  PASS + validador OK. **Sin verificación manual** (headless; los POIs se ven
  con P4-08).
- 2026-08-30 [P4-06] Recursos minables procedurales: `game/world/resources.{hpp,cpp}`
  — extensión de la economía de P1C (minar = commodity a la `CargoHold`, no
  sistema paralelo). `generate_asteroid_field(system_seed,...)` determinista
  (8-24 depósitos en cinturón, commodity sesgado, total/yield por semilla).
  `mine_deposit` puro conserva unidades y respeta bodega. `update_mining` en
  `world::fixed_step`: nave parada dentro de rango mina el depósito; agotados →
  `DepositDepleted`. `universe_test` carga `load_economy_data` + siembra el
  cinturón con el `system_seed`. Gate `CSC_RESOURCES_SMOKE=1` PASS (determinismo
  + conservación de unidades + bodega llena). Minería pasiva por proximidad (sin
  láser/apuntado); sin depósitos de superficie aún. Verificado: build 0 warnings
  + RESOURCES_SMOKE PASS + 11 escenas + 9 smokes previos PASS. **Sin verificación
  manual** (headless; se aprecia volando a un asteroide con P4-08).
- 2026-08-30 [P4-05] Navegación entre sistemas + mapa de galaxia:
  `game/world/galaxy.{hpp,cpp}` — `generate_galaxy(galaxy_seed, home_seed, out)`
  puro: 5-9 sistemas en un disco, nodo 0 = home (seed = `base_seed` de P4-04),
  cada uno enlaza con 2 vecinos (simétrico), BFS garantiza grafo conexo.
  `galaxy_jump` valida adyacencia (salto = transición de carga). `galaxy_log`
  vuelca el grafo. `setup_universe_test` construye la galaxia, `--system=<n>`
  elige nodo (0 = sistema compuesto P4-04, >0 = generado puro), guarda
  `GalaxyMap` singleton. Gate `CSC_GALAXY_SMOKE=1` PASS (determinismo, simetría,
  conexo, nodo 0 = home, reglas de salto). Salto en marcha (jump point in-run) →
  P4-08. Verificado: build 0 warnings + GALAXY_SMOKE PASS + 11 escenas + 8 smokes
  previos PASS + `--system=2` OK. **Sin verificación manual** (headless; el viaje
  entre sistemas se ve con P4-08).
- 2026-08-30 [P4-04] Sistema fijo → semilla + overlay curado: `star_system.cfg`
  gana `base_seed=20260830`; `load_star_system_config` genera la base procedural
  (P4-01) y superpone los `body.N.*` (reemplaza por nombre, o añade; un `Star`
  curado reemplaza al generado). Sin `base_seed` → 100% a mano (retrocompatible).
  `universe_test` por defecto: 5 generados + 7 curados = 11 cuerpos, Estacion-Alfa/
  Beta intactas. `--seed=<n>` sigue siendo 100% generado. Gate
  `CSC_FIXEDSYS_SMOKE=1` PASS (nombres curados + un `Sys-*` + >7 cuerpos +
  determinista). Verificado: build 0 warnings + 9 escenas + SAVE/REBASE smoke OK.
  **Sin verificación manual** (headless; se aprecia con P4-08).
- 2026-08-30 [P4-03] Streaming de terreno por chunks con LOD:
  `game/world/terrain_stream.{hpp,cpp}` — sistema completo. `select_terrain_lod`
  (quadtree cubo-esfera puro, raíces por distancia, subdivide hasta profundidad
  6). `Pool<TerrainChunk, 48>` fijo con buffers CPU en el slot (~2 MB); los
  parches lejanos se quedan sin slot antes de exceder capacidad. Hilo de fondo
  persistente genera chunks con `build_planet_patch` (P4-02); el hilo principal
  reselecciona LOD, encola/retira y drena `CpuReady → upload_cb → GpuReady`.
  Sincronía por `state`+`wants_retire` atómicos. Gate `CSC_TERRAINSTREAM_SMOKE=1`
  PASS (x5): LOD se refina en la aproximación, pool nunca desborda, todo residente
  llega a `GpuReady`, selección determinista, shutdown con join limpio. **Render
  de los chunks + revisión del umbral de floating-origin → P4-08** (necesita
  soporte multi-malla en el renderer, dominio vulkan-pipeline-expert). Verificado:
  build 0 warnings + TERRAINSTREAM_SMOKE PASS + 11 escenas + 7 smokes previos PASS.
  **Sin verificación manual todavía** (P4-03 es headless; se ve con P4-08).
- 2026-08-30 [P4-02] Terreno planetario procedural: `game/world/planet_terrain.{hpp,cpp}`
  — librería pura sin heap. Ruido Perlin 3D + fBm sin dependencias;
  `planet_terrain_params(body_seed,...)` deriva la forma del planeta (usa el
  body_seed de P4-01); `planet_height(dir)` acotado a `±elevation_scale`;
  `cube_sphere_dir` (6 caras, spherify Cobe); `build_planet_patch` rellena buffers
  fijos del llamante con un parche cubo-esfera desplazado (pos/normal/color por
  altura/uv) — la unidad que P4-03 streameará. `Rng64` extraído a `rng64.hpp`
  compartido. Gate `CSC_PLANETGEN_SMOKE=1` (altura acotada, normales unitarias,
  regeneración byte-idéntica, costura entre parches exacta). Sin render aún (P4-03
  streaming + P4-08 escena). Verificado: build 0 warnings + PLANETGEN_SMOKE PASS +
  11 escenas + 6 smokes previos PASS. **Verificación manual pendiente de Miguel:**
  ninguna todavía (P4-02 es headless; el terreno se ve con P4-08).
- 2026-08-30 [P4-01] Generador de sistema estelar: `game/world/star_system_gen.{hpp,cpp}`
  — `generate_star_system(seed, StarSystemData&, GeneratedSystemInfo*)` pura, sin
  heap, RNG SplitMix64 (bit-exacto entre plataformas). Estrella + 2-6 planetas en
  órbitas geométricas crecientes + LZ ~50% emparejada a su planeta; rellena la
  misma estructura de P1D-02. `GeneratedSystemInfo` expone body_seed/orbit/flags
  para P4-02/06/07. Flag `--seed=<n>` → `universe_test` genera el sistema (por
  defecto sigue el `star_system.cfg` fijo). Gate `CSC_SYSTEMGEN_SMOKE=1` (5
  semillas: rango, estrella[0], órbitas crecientes, LZ emparejada, byte-idéntico
  al regenerar). Sistemas generados aún sin estaciones/POIs (P4-04/P4-07);
  determinismo byte-exacto en el mismo binario, ±1 ULP posible entre libms
  distintas (anotado). Verificado: build 0 warnings + SYSTEMGEN_SMOKE PASS + 11
  escenas + 5 smokes previos PASS. **Verificación manual pendiente de Miguel:**
  `--scene=universe_test --seed=12345` vuela un sistema generado; repetir el mismo
  seed da el mismo sistema, otro seed da otro.
- 2026-08-30 [Fase 3 → Fase 4] Miguel valida físicamente las 6 secciones de
  `.claude/VERIFICACION-PENDIENTE.md` (content_smoke, `--ship=`, hangar +
  taquillas, `--suit=`, Outpost C + arco Ashfall, regresión Fases 1/2 — todo OK).
  **Fase 3 CERRADA formalmente (9/9).** Abierta Fase 4 — Universo Procedural:
  8 tareas (P4-01..08), eje = terreno planetario procedural + streaming por LOD +
  multi-sistema, todo determinista por semilla. Objetivo, restricciones y DoD
  arriba en "Fase activa". Deuda de schema del save (P3-01/03/06 + Fase 2) se
  resuelve en el bump que traiga el formato semilla+deltas de Fase 4.
  Checklist consumido — `.claude/VERIFICACION-PENDIENTE.md` eliminado. Push de los
  10 commits de Fase 3 a `origin/main` (fast-forward, sin divergencia con Cursor
  Cloud).
- 2026-08-30 [P3-09 → Fase 3 9/9] `content_smoke_test`: nuevo módulo
  `game/content_smoke.{hpp,cpp}` + escena `content_smoke_test` que carga TODO el
  catálogo por los loaders reales del motor y valida integridad — resumen por tipo
  + `CONTENT_SMOKE: PASS|FAIL`. Delega en `flight::validate_ship_weapon_refs` +
  nuevos `character::validate_suit_catalog` y `economy::validate_economy_content`
  (ids únicos, refs resolubles, ciclos de `requires_completed_id`, refs de NPC de
  `locations.cfg`, ids requeridos por el motor). `validate_catalogs.py` gana el
  chequeo de ciclos. **Fase 3 IMPLEMENTADA 9/9** — PARADA: pendiente validación
  física de Miguel antes de abrir Fase 4 (mismo criterio que Fases 0/1/2).
  Verificado: `CONTENT_SMOKE: PASS` (6/4/4/3/3/3/13), 11 escenas headless, 5 gates
  de smoke PASS, test negativo con 3 fallos → FAIL con las 3 causas.
- 2026-08-30 [P3-07] Más misiones + narrativa: 7 plantillas nuevas en
  `mission_templates.cfg` (ids 6-12, total 13/16). `MissionTemplate` gana `title`
  (frase de sabor opcional, logueada al aceptar). Arco "The Ashfall Line"
  (6→7→8→9 encadenado con `requires_completed_id`, sin `branch_group`) +
  3 misiones sueltas. NPC dadores/turn-in añadidos vía `locations.cfg` (P3-04):
  `economy_test` 18→26 NPCs. Cero mecánica nueva (reutiliza P1C/P2-09/P2-10).
  Gate `CSC_MISSION_SMOKE=1` (forma de cadena + `title` + unlock por
  `requires_completed_id`). `validate_catalogs.py` ya cubre los refs.
  Verificado: build 0 warnings + 10 escenas headless + 4 smokes PASS + validador
  OK (13 misiones). **Verificación manual pendiente de Miguel:** hacer el arco
  Ashfall entero en `economy_test` (Silent Beacon en MarketA → Outpost C ...) y
  comprobar que cada paso desbloquea el siguiente y sale el `title` en consola.
- 2026-08-30 [P3-06] Trajes/armadura por datos: `assets/data/suits.cfg` (`[[suit]]`)
  → `character::SuitCatalog`/`SuitDef` (POD, `kMaxSuitDefs=16`); componente nuevo
  `character::Suit`. `spawn_player_character` gana `suit_id` opcional; flag
  `--suit=<id>` plumbed como `--ship=`. Ganchos: protección en
  `apply_damage_events` (`daño *= 1 - damage_reduction`), propelente EVA
  drena/recarga en la locomoción y corta el empuje a 0, `move_speed_mult` en el
  andar. Locker en juego: `SuitLocker` + dispatch + `spawn_suit_locker`; 4
  taquillas en `ship_hangar_test`. Gate `CSC_SUIT_SMOKE=1` PASS. 4 trajes
  (standard baseline / explorer / heavy 45% / scout x1.2). `validate_catalogs.py`
  gana chequeos de `suits.cfg`. `Suit` no se serializa aún (deuda de schema). El
  `kItemCatalog` a pie sigue hardcodeado (catálogo aparte, no refactor).
  Verificado: build 0 warnings + 10 escenas headless + `CSC_SUIT_SMOKE`/
  `CSC_HANGAR_SMOKE`/`CSC_SAVE_SMOKE` PASS + validador OK. **Verificación manual
  pendiente de Miguel:** con `--suit=suit.armor.heavy` en `npc_combat_test`
  aguantar más disparos; con `suit.eva.explorer` vs `suit.armor.heavy` en
  `on_foot_test` notar la diferencia de propelente EVA; taquillas de
  `ship_hangar_test`.
- 2026-08-30 [P3-04] Población de localizaciones por datos: `assets/data/locations.cfg`
  (`[[location]]` + `npc.N.*`) → `economy::LocationCatalog`; la escena coloca cada
  localización (tabla `LocationPlacement`), el `.cfg` la puebla.
  `economy::populate_locations` spawnea traders/dadores/turn-in/travel-pads
  (`spawn_*` de P1C-06 tal cual) + zona de gravedad y cubierta con `gravity_deck`.
  Travel pads referencian destino por id de localización. `setup_economy_test_scene`
  pierde ~120 líneas a mano: coloca 3 localizaciones y llama a `populate_locations`;
  `loc.market-a`/`loc.market-b` reproducen exactamente el set de P1C/P2-09/P2-10;
  `loc.outpost-c` es la expansión (nuevo `MarketC` id=2 en `markets.cfg`, 2 traders
  + "Scout Contract" + pads a A/B, y A/B ganan pad a C). `validate_catalogs.py`
  gana chequeos de `locations.cfg`. `universe_test` sigue sin poblar (ShipPilot, sin
  a pie). Verificado: build 0 warnings + 10 escenas headless (economy_test: 3
  localizaciones / 18 NPCs) + `CSC_SAVE_SMOKE`/`CSC_HANGAR_SMOKE` PASS + validador
  OK + test negativo. **Verificación manual pendiente de Miguel:** recorrer las 3
  localizaciones de `economy_test`, comprobar que las tiendas/misiones/pads nuevos
  funcionan y que el flujo P2-09/P2-10 sigue igual.
- 2026-08-30 [P3-08] Validador de catálogos: `tools/validate_catalogs.py` (Python 3,
  solo stdlib, sin build). Recorre `assets/data/*.cfg` y corre las validaciones de
  P3-09: ids únicos, `weapon_id_N` de nave resuelve en `weapons.cfg`, ids del motor
  presentes, topes, `inertia`/`subsystem_hp` bien formados, commodity ids
  contiguos, índices de mercado y referencias de misión resolubles. Resumen +
  `WARN`/`ERROR` + exit 0/1. Target CMake opcional `validate_catalogs` (si hay
  `python3`, no bloquea el build). Bloque "Pipeline de datos" de Fase 3 COMPLETO
  (P3-01/05/08). Verificado: limpio sobre los catálogos actuales + test negativo
  con 5 fallos inyectados detectados (exit 1). Sin verificación manual (herramienta
  de dev, no toca el runtime).
- 2026-08-30 [P3-05] Catálogo de armas: `assets/data/weapons.cfg` (`[[weapon]]`)
  → singleton `flight::WeaponCatalog`/`WeaponDef` (POD, `kMaxWeaponDefs=24`), mismo
  parser/validación que `ship_catalog` (id de texto único, dup/ausente = carga
  INVÁLIDA). `ShipDef.weapon_id[N]` (`weapon_id_N` en `ships.cfg`) y `spawn_turret`
  (nuevo `weapon_id` opcional) nombran el arma; `apply_ship_def_components` /
  `spawn_turret` copian las stats del `WeaponDef` sobre el `WeaponMount`.
  `scene_setup_by_name` carga weapons→ships→`validate_ship_weapon_refs`
  (referencia nave→arma colgante = `log_error`, la dura es P3-09). 4 armas
  (repeater/cannon/laser/turret-repeater); `weapon.fixed.repeater` y
  `weapon.turret.repeater` reproducen exactamente el gun y la torreta de Fase 1/2
  → cero cambio de comportamiento. Catálogo de ítems a pie sigue hardcodeado
  (anotado, se hará con P3-06/P3-09). Verificado: build 0 warnings + 10 escenas
  headless (`ship<->weapon references OK`) + `CSC_SAVE_SMOKE`/`CSC_HANGAR_SMOKE`
  PASS + test negativo (weapon_id colgante). **Verificación manual pendiente de
  Miguel:** notar que Mule/Bulwark (cañón) y Pathfinder (láser) disparan distinto
  al repetidor de la Kestrel.
- 2026-08-30 [P3-03] Hangar de naves: singleton `flight::ShipOwnership` +
  componente `ShipDealer{id,price}` en kioscos (`spawn_ship_dealer`). Un kiosco
  por nave, sensible al contexto al pulsar F (dispatch en
  `character::handle_interact_events`): comprar+equipar / cambiar activa /
  vender (reembolso 0.6×, la de inicio no se vende). Cambiar de nave **no crea
  entidad nueva**: `apply_ship_def_to_player` reescribe in situ todos los stats
  del `PlayerShip` vivo desde el `ShipDef`, conservando pose/velocidad/carga.
  `spawn_player_ship` refactorizado para compartir `apply_ship_def_components`.
  Compra/venta con `PlayerWallet` de P1C tal cual. Escena `ship_hangar_test`
  (cubierta a pie + 4 kioscos + asiento de piloto). Gate `CSC_HANGAR_SMOKE=1`
  → PASS. Verificado: build 0 warnings + 10 escenas headless + `CSC_SAVE_SMOKE`/
  `CSC_FORCE_REBASE_SMOKE` OK. **Verificación manual pendiente de Miguel:**
  comprar 2 naves en el hangar y pilotarlas notando la diferencia (criterio de
  salida del DoD de Fase 3).
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
