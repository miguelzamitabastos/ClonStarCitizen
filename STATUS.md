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
- P1B A pie y FPS:        [.........] 0/9  tareas
- P1C Economía/Misiones:  [.......] 0/8  tareas
- P1D Universo fijo:      [........] 0/8  tareas
- P1E UI/HUD/Audio:       [.......] 0/7  tareas
- P1F Persistencia:       [......] 0/6  tareas

## Escenas demo
| Escena | Estado |
|---|---|
| `grid_freelook` / `instancing_stress` / `mesh_viewer` | Fase 0 OK |
| `flight_test` | P1A OK — nave + objetivo a 50m |
| `on_foot_test` | pendiente P1B-09 |
| `economy_test` | pendiente P1C-09 |
| `universe_test` | pendiente P1D-08 |
| `ui_audio_test` | pendiente P1E-07 |
| `save_load_test` | pendiente P1F-06 |

## Bloqueado (requiere decisión de Miguel)
- P1D-04 naming final del sistema estelar (placeholders OK: Sistema-01 / Estacion-Alfa / Planeta-01).

## Bitácora (más reciente arriba, una línea por tarea)
- 2026-08-10 [P1A-01..10] Flight: RigidBody6DOF mass=45t, main thrust 320kN, coupled brake 280kN; projectile pool 32; target @ z=-50; scene `--scene=flight_test`. Engine: Orientation, KinematicFromRigidBody, FixedStepHook, ControlMode, Actions Roll/ToggleCoupled.
- 2026-08-10 Fase 0 validada por usuario ("Ok"). Apertura Fase 1 — rama `release/fase-1-vertical-slice`.
