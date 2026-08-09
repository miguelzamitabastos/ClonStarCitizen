# Fase 1B — A Pie y FPS

Depende de: Fase 0 completa. Comparte con P1A la cámara/scheduler pero es un módulo
independiente: no depende de que P1A esté terminada, solo de sus componentes base
(`RigidBody6DOF` de la nave, para el problema de "interior de nave en movimiento").
Carpeta de trabajo: `src/game/character/`, `include/game/character/`.
Escena demo objetivo: `--scene=on_foot_test`.

## Objetivo de esta fase

Personaje controlable a pie con locomoción creíble en tres contextos de gravedad
(superficie planetaria, gravedad artificial de estación/nave, EVA en gravedad cero),
combate FPS mínimo, e interacción básica con el mundo. Incluye el problema técnico
más delicado de esta fase: moverse dentro de una nave que a su vez se está moviendo.

## Tareas

| ID | Tarea | Depende de |
|---|---|---|
| P1B-01 | Componentes ECS de personaje: `CharacterController`, `Health`, `EquippedItem` | P0-04 |
| P1B-02 | Locomoción kinemática (cápsula) en gravedad de superficie planetaria | P1B-01, P0-03 |
| P1B-03 | Zonas de gravedad (`GravityZone`) y transición a EVA sin gravedad | P1B-02 |
| P1B-04 | Cámara primera/tercera persona para el personaje | P0-04, P1B-01 |
| P1B-05 | Entrar/salir de nave: transferencia de control de input/cámara entre modo vuelo (P1A) y modo a pie | P1A-05, P1B-04 |
| P1B-06 | Combate FPS básico: arma equipable, disparo, daño, muerte del personaje | P1B-01, P0-09 |
| P1B-07 | Interacción con el mundo: raycast desde cámara + tag `Interactable` (puertas, terminales) | P1B-04 |
| P1B-08 | Interior de nave navegable: personaje en espacio local de la nave en movimiento | P1A-01, P1B-02 |
| P1B-09 | Escena demo `on_foot_test`: interior de nave → EVA → exterior de estación, en una sesión | P1B-01..08 |

## Restricciones de arquitectura específicas

- El `CharacterController` es **kinemático**, no un rigid body simulado por el mismo
  integrador que las naves: resuelve colisión por barrido de cápsula contra el mundo
  y aplica gravedad como una velocidad vertical acumulada, no como fuerza en un
  solver físico completo. Esto es intencional — es lo estándar en shooters/juegos de
  personaje y evita acoplar la locomoción a la complejidad del integrador 6DOF de
  P1A.
- `GravityZone` es un componente en volúmenes del mundo (esfera/caja) con un vector
  de gravedad y una magnitud; el personaje consulta la zona en la que está cada
  fixed-tick y ajusta su modelo de movimiento: superficie = gravedad hacia "abajo"
  fija, EVA = gravedad cero y el movimiento pasa a ser por impulsos direccionales
  tipo "mini-thrusters" del traje (reutiliza el mismo patrón de mapeo input→fuerza
  de `ThrusterSet` de P1A si tiene sentido, no dupliques la lógica).
- **Interior de nave en movimiento (P1B-08) — el punto más delicado de esta fase:**
  el personaje dentro de una nave no debe simularse en espacio mundial absoluto y
  luego "perseguir" a la nave. La posición del personaje dentro de una nave se
  almacena en el espacio local de esa nave (parent transform = `RigidBody6DOF` de
  la nave), y solo se transforma a espacio mundial para render/audio, nunca para la
  lógica de colisión interna. Decide y documenta en `STATUS.md` el mecanismo exacto
  (componente `LocalToShip { ship_entity, local_position }` o similar) porque Fase 2
  (multi-tripulación) y Fase 5 (red) dependen directamente de este contrato.
- La transición P1B-05 (entrar/salir de nave) es un cambio de "modo de cámara/input
  activo" sobre la misma entidad jugador, no una entidad nueva: el jugador conserva
  su `Health`/inventario al entrar y salir de la nave.
- El combate FPS (P1B-06) reutiliza el mismo patrón de armas/proyectiles de P1A-08
  donde tenga sentido (mismo `Pool<Projectile,N>`, mismo sistema de daño genérico
  aplicable tanto a `ShipHull` como a `Health` vía un evento de daño compartido
  `DamageEvent{ target, amount, source }` en vez de dos sistemas de daño paralelos).
- La interacción (P1B-07) es un raycast corto desde la cámara cada frame (barato,
  no hace falta optimizarlo todavía) contra entidades con tag `Interactable`; la
  acción resultante (abrir puerta, activar terminal) se publica como evento ECS, no
  como llamada directa a la lógica de otro sistema.

## Definition of Done específico

- `on_foot_test` permite: caminar por el interior de una nave que se está moviendo
  (ver el exterior desplazarse por las ventanas sin que el personaje "resbale"),
  salir por una escotilla a EVA (gravedad cero, movimiento por impulsos), y entrar
  caminando en una estación con gravedad artificial — las tres transiciones sin
  teletransportes ni saltos de cámara bruscos.
- Disparar un arma FPS reduce `Health` de otro personaje/objetivo de prueba hasta la
  muerte, usando el mismo `DamageEvent` que P1A-09, verificable leyendo el código de
  ambos sistemas de daño (deben converger en el mismo evento, no reimplementarlo).
- Cero asignación dinámica por disparo o por paso de locomoción (verificar con el
  overlay de P0-11 o un contador de allocs si el entorno lo permite).

---
En paralelo con: `02-FASE-1A-VUELO-Y-NAVES.md`, `04-FASE-1C-ECONOMIA-Y-MISIONES.md`,
`05-FASE-1D-UNIVERSO-FIJO-Y-MUNDO.md`.
