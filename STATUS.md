# STATUS

## Fase activa: Fase 0 — Cimientos del Motor — **COMPLETADA**

## Progreso por documento
- P0 Cimientos del Motor: [#############] 13/13 tareas
  - [x] P0-01 Depth buffer + depth testing
  - [x] P0-02 Resize de swapchain / minimizado
  - [x] P0-03 Sistema de input abstraído
  - [x] P0-04 Scheduler ECS fixed-timestep
  - [x] P0-05 Sistema de configuración
  - [x] P0-06 Logging estructurado
  - [x] P0-07 Pipeline carga mallas glTF async
  - [x] P0-08 Texturas + descriptor sets/UBO
  - [x] P0-09 Draw indexed instanced
  - [x] P0-10 Abstracción material/pipeline
  - [x] P0-11 Overlay depuración ImGui
  - [x] P0-12 Harness escenas demo
  - [x] P0-13 Esqueleto src/game/*

## Escenas demo (P0-12)
| Escena | Lanzamiento | Qué verifica |
|---|---|---|
| `grid_freelook` (default) | `./clon_star_citizen` o `--scene=grid_freelook` | Cámara free-look + grid |
| `instancing_stress` | `./clon_star_citizen --scene=instancing_stress` | 600 cubos / 1 draw (P0-09) |
| `mesh_viewer` | `./clon_star_citizen --scene=mesh_viewer` | glTF cube upload (P0-07) |

Config: `assets/config/default.cfg` (`scene_name=...`). Nombre desconocido → fallback a `grid_freelook`.

## Cierre de Fase 0
- **Probado en cloud:** lavapipe + Xvfb; capturas en `artifacts/screenshots/`.
- **No probado aquí:** resize real con ratón, mando, GPU discreta, validación layers.
- **Fuera de alcance (consciente):** materiales PBR reales, async GPU transfer queue dedicada, ImGui dock completo.
- **Nota terceros:** Dear ImGui puede allocar internamente (FetchContent permitido por roadmap).
- **PARADA:** no iniciar Fase 1 hasta validación física del usuario (`git pull` + run en PC).

## Bloqueado (requiere decisión de Miguel)
- Validación física de Fase 0 en PC del usuario antes de abrir Fase 1.

## Bitácora (más reciente arriba, una línea por tarea)
- 2026-08-09 [P0-12] Harness escenas: grid_freelook / instancing_stress / mesh_viewer. Capturas p0-12-*.png. **Fase 0 completa — a la espera de validación física.**
- 2026-08-09 [P0-11] ImGui overlay FPS/entidades/inspector. F1 cursor. Captura: p0-11-imgui-overlay.png.
- 2026-08-09 [P0-09] Instancing 600 cubos / 1 draw. Captura: p0-09-instancing.png.
- 2026-08-09 [P0-10] PipelineCatalog/MaterialCatalog (cap 16).
- 2026-08-09 [P0-08] UBO + sampler + textura blanca 1x1; mesh shaders.
- 2026-08-09 [P0-07] tinygltf + MeshLoader async + cube.gltf.
- 2026-08-09 [P0-04] Fixed-dt + PreviousPosition + FrameInterpolation.alpha.
- 2026-08-09 [P0-13] Esqueleto src/game/* + docs/game-modules.md.
- 2026-08-09 [P0-06] Logging por categorías.
- 2026-08-09 [P0-05] AppConfig + default.cfg + --scene=.
- 2026-08-09 [P0-03] Input acciones lógicas.
- 2026-08-09 [P0-02] Resize swapchain+depth; skip 0x0.
- 2026-08-09 [P0-01] Depth buffer. Captura: p0-01-depth-grid.png.
