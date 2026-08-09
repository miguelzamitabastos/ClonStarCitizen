# STATUS

## Fase activa: Fase 0 — Cimientos del Motor

## Progreso por documento
- P0 Cimientos del Motor: [###########..] 11/13 tareas
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
  - [ ] P0-11 Overlay depuración ImGui
  - [ ] P0-12 Harness escenas demo
  - [x] P0-13 Esqueleto src/game/*

## Bloqueado (requiere decisión de Miguel)
- (ninguno)

## Bitácora (más reciente arriba, una línea por tarea)
- 2026-08-09 [P0-09] Instancing indexado: 600 cubos / 1 draw. kMaxInstancesPerDrawCall=1024. Captura: artifacts/screenshots/p0-09-instancing.png.
- 2026-08-09 [P0-10] PipelineCatalog/MaterialCatalog (cap 16); handles en lugar de VkPipeline crudos en gameplay.
- 2026-08-09 [P0-08] UBO per-frame + sampler; textura blanca 1x1; shaders mesh.vert/frag.
- 2026-08-09 [P0-07] tinygltf + MeshLoader async sobre asset Arena; `assets/meshes/cube.gltf`; upload GPU en hilo principal.
- 2026-08-09 [P0-04] Scheduler fixed-dt + PreviousPosition + FrameInterpolation.alpha.
- 2026-08-09 [P0-13] Esqueleto src/game/* + docs/game-modules.md.
- 2026-08-09 [P0-06] Logging por categorías.
- 2026-08-09 [P0-05] AppConfig + default.cfg + --scene=.
- 2026-08-09 [P0-03] Input acciones lógicas; cámara sin glfwGetKey.
- 2026-08-09 [P0-02] Resize swapchain+depth; skip 0x0.
- 2026-08-09 [P0-01] Depth buffer. Captura: artifacts/screenshots/p0-01-depth-grid.png.
- 2026-08-09 [BOOT] Rama release/fase-0-cimientos-del-motor.
