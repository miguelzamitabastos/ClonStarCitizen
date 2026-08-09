# STATUS

## Fase activa: Fase 0 — Cimientos del Motor

## Progreso por documento
- P0 Cimientos del Motor: [######.......] 6/13 tareas
  - [x] P0-01 Depth buffer + depth testing
  - [x] P0-02 Resize de swapchain / minimizado
  - [ ] P0-03 Sistema de input abstraído
  - [x] P0-04 Scheduler ECS fixed-timestep
  - [x] P0-05 Sistema de configuración
  - [x] P0-06 Logging estructurado
  - [ ] P0-07 Pipeline carga mallas glTF async
  - [ ] P0-08 Texturas + descriptor sets/UBO
  - [ ] P0-09 Draw indexed instanced
  - [ ] P0-10 Abstracción material/pipeline
  - [ ] P0-11 Overlay depuración ImGui
  - [ ] P0-12 Harness escenas demo
  - [x] P0-13 Esqueleto src/game/*

## Bloqueado (requiere decisión de Miguel)
- (ninguno)

## Bitácora (más reciente arriba, una línea por tarea)
- 2026-08-09 [P0-04] Scheduler fixed-timestep: acumulador manual + `physics_integrate_positions` (no fase Flecs única). Cámara en `world_progress` con dt variable; `PreviousPosition` + `FrameInterpolation.alpha` para interp de render. `physics_fixed_hz` desde AppConfig.
- 2026-08-09 [P0-13] Esqueleto `src/game/*` + `docs/game-modules.md`. Módulos: flight/character/economy/world/ui/audio/save con `register_systems` stub. Convención: comunicación vía ECS, no llamadas cruzadas.
- 2026-08-09 [P0-06] Logging por categorías (Core/Vulkan/Ecs/Input/Assets/Config/Game). printf-style a stderr; min level desde config.
- 2026-08-09 [P0-05] Config POD + `assets/config/default.cfg` + argv `--scene=`. Decisión: title en buffer fijo 64 chars; physics_fixed_hz=60 por defecto (P0-04 lo usará).
- 2026-08-09 [P0-02] Resize swapchain+depth+FB; ventana resizable; skip 0x0. Decisión: si `image_format` no cambia se conservan pass/pipelines; si cambia se recrean (preferimos B8G8R8A8_SRGB estable).
- 2026-08-09 [P0-01] Depth buffer + depth testing. Decisión: D32_SFLOAT→D24; compareOp=LESS. Captura: artifacts/screenshots/p0-01-depth-grid.png.
- 2026-08-09 [BOOT] Rama `release/fase-0-cimientos-del-motor` creada.
