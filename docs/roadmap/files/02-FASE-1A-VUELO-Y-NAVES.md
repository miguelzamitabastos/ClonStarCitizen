# Fase 1A — Vuelo y Naves

Depende de: Fase 0 completa (en especial P0-03 input, P0-04 scheduler fixed-timestep,
P0-09 instancing, P0-12 harness de escenas).
Carpeta de trabajo: `src/game/flight/`, `include/game/flight/`.
Escena demo objetivo: `--scene=flight_test`.

## Objetivo de esta fase

Una nave pilotable, con física de vuelo creíble (no arcade, tampoco simulación
orbital completa), sistemas internos básicos (energía/escudos/armas) y capacidad de
ser destruida. Es la base sobre la que la Fase 2 añade multi-tripulación, daño por
componente y combate IA.

## Tareas

| ID | Tarea | Depende de |
|---|---|---|
| P1A-01 | Componentes ECS de nave: `RigidBody6DOF`, `ShipHull`, `ThrusterSet` | P0-04 |
| P1A-02 | Integrador de física 6DOF a paso fijo (fuerzas + torques → velocidad → posición) | P1A-01 |
| P1A-03 | Mapeo de input de vuelo → fuerzas de propulsores | P0-03, P1A-02 |
| P1A-04 | Modo de vuelo acoplado (auto-freno) vs desacoplado (inercia libre), toggle en runtime | P1A-03 |
| P1A-05 | Cámara de cabina/persecución enganchada a la nave del jugador | P0-04, P1A-01 |
| P1A-06 | Sistema de energía (`PowerPlant`): generación, capacidad, consumo por subsistema | P1A-01 |
| P1A-07 | Sistema de escudos: capacidad, regeneración, consumo de energía, absorción de daño | P1A-06 |
| P1A-08 | Sistema de armas fijas: disparo, consumo de energía/calor, cooldown, impacto | P1A-06, P0-09 |
| P1A-09 | Daño de casco y destrucción básica (HP, umbral, evento de explosión) | P1A-07, P1A-08 |
| P1A-10 | Escena demo `flight_test`: nave pilotable + HUD de depuración (velocidad, energía, escudo) | P1A-01..09, P0-11, P0-12 |

## Restricciones de arquitectura específicas

- `RigidBody6DOF` es un componente POD: posición (ver nota de precisión abajo),
  orientación (quaternion vía GLM), velocidad lineal, velocidad angular, masa,
  tensor de inercia (puede simplificarse a un vector diagonal por ahora — inercia
  por eje sin productos cruzados — y refinarse en Fase 2 si hace falta realismo).
- **Nota de precisión de posición:** en esta fase, con un único sistema estelar
  pequeño y sin distancias astronómicas reales todavía, `f32` es aceptable para
  posición. Antes de la Fase 1D (universo fijo a escala real) hay que decidir el
  esquema de "floating origin" o doble precisión — no lo resuelvas aquí, pero no
  hardcodees supuestos que lo hagan imposible más tarde (evita, por ejemplo, guardar
  posiciones absolutas en shaders sin pasar por un origen relativo a cámara).
- El integrador (P1A-02) corre en la fase fixed-timestep del scheduler de P0-04.
  Nunca leas `dt` variable del frame para integrar física: usa el paso fijo definido
  en Fase 0. El render lee la posición interpolada, no la del último paso de física.
- `ThrusterSet` es un array fijo de propulsores (main + retro + maniobra), cada uno
  con posición relativa al centro de masa, dirección, fuerza máxima. El mapeo de
  input a activación de propulsores es una función pura testeable sin GPU ni ventana.
- El sistema de energía es un grafo simple productor→consumidores por frame fixed-
  timestep: `PowerPlant.output` se reparte entre `ThrusterSet`, `ShieldGenerator`,
  `WeaponMount[]` según prioridad configurable (para más adelante permitir "power
  triangle" tipo Star Citizen: más energía a escudos = menos a armas/motor).
- Las armas fijas (P1A-08) para esta fase pueden ser hitscan (raycast instantáneo)
  o proyectil simple con `Pool<Projectile, N>` — nunca `new Projectile()`. Si son
  proyectiles, se dibujan con el instancing de P0-09.
- La destrucción (P1A-09) no borra la entidad al llegar a 0 HP: la marca con un tag
  `Destroyed` y dispara un evento; la limpieza real de la entidad (devolver el slot
  al pool) ocurre en un sistema aparte al final del frame, nunca a mitad de la
  iteración de otros sistemas sobre el mismo archetype.

## Definition of Done específico

- `flight_test` permite despegar (o partir ya en vuelo), acelerar, girar en los 6
  ejes, disparar, y recibir daño de una fuente de prueba (asteroide estático o nave
  NPC inmóvil) hasta destruirse, sin crashear ni fugar memoria (verificar con
  `valgrind`/ASan en build debug si está disponible en el entorno del agente).
- El HUD de depuración de P0-11 muestra en tiempo real: velocidad, energía
  disponible, % de escudo, HP de casco — esto es lo que Miguel va a mirar desde el
  móvil para verificar que la fase funciona sin tener que leer código.
- El modo acoplado/desacoplado (P1A-04) es perceptible: en acoplado la nave frena
  sola al soltar el input; en desacoplado conserva inercia indefinidamente.

---
En paralelo con: `03-FASE-1B-A-PIE-Y-FPS.md`, `04-FASE-1C-ECONOMIA-Y-MISIONES.md`,
`05-FASE-1D-UNIVERSO-FIJO-Y-MUNDO.md`.
