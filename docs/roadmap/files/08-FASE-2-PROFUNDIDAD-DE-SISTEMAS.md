# Fase 2 — Profundidad de Sistemas

Depende de: Fase 1 completa y cerrada en `STATUS.md` (bucle de juego jugable de
principio a fin ya existe). Esta fase no añade pilares nuevos, profundiza los que ya
funcionan de forma mínima.

## Objetivo de esta fase

Pasar de "funciona de forma mínima" a "tiene la profundidad reconocible de Star
Citizen" en cada pilar: naves con tripulación y daño por componente, IA de combate
tanto en el espacio como a pie, una economía que reacciona a eventos, y misiones
encadenadas. Es la fase más larga en nº de tareas; se puede trabajar en paralelo por
subsistema igual que en Fase 1, cada uno en su carpeta ya existente.

## Bloque: Naves y vuelo (`src/game/flight/`)

| ID | Tarea |
|---|---|
| P2-01 | Tripulación NPC: roles (artillero de torreta, ingeniero) como entidades con asiento fijo en la nave, controlando subsistemas mientras el jugador pilota |
| P2-02 | Daño por componente: motor, escudo, arma, sensores dañables independientemente (no solo HP de casco global) |
| P2-03 | Torretas: montaje giratorio controlado por IA o por el jugador (cambio de asiento), con su propio arco de disparo |
| P2-04 | Diferenciación de modelo de vuelo atmosférico (arrastre, sustentación simplificada) vs vacío |

## Bloque: A pie (`src/game/character/`)

| ID | Tarea |
|---|---|
| P2-05 | Inventario completo: slots de equipamiento, objetos recogibles, uso de items |
| P2-06 | IA de combate a pie: NPCs hostiles con detección, cobertura básica, disparo |
| P2-07 | Daño por zona (torso/extremidad) y ciclo de muerte/reaparición del jugador |

## Bloque: Economía y misiones (`src/game/economy/`)

| ID | Tarea |
|---|---|
| P2-08 | Simulación económica dinámica: producción/consumo por localización, precios que reaccionan a eventos (escasez, misión completada en masa, etc.) |
| P2-09 | Misiones encadenadas: una misión desbloquea la siguiente, ramificación simple según elección del jugador |
| P2-10 | Misiones de combate/escolta que reutilizan la IA de P2-03/P2-06, no una IA nueva paralela |

## Bloque: IA y facciones (nuevo: `src/game/ai/`)

| ID | Tarea |
|---|---|
| P2-11 | Framework de IA compartido: selección de objetivo, hostilidad por reputación de facción, máquina de estados simple (patrulla/alerta/combate/huida) reutilizable tanto por torretas (P2-03) como por NPCs a pie (P2-06) como por naves NPC en espacio abierto |
| P2-12 | Encuentros aleatorios en espacio abierto (piratas, patrullas de facción) generados por proximidad, no precolocados a mano |

## Bloque: Mundo (`src/game/world/`)

| ID | Tarea |
|---|---|
| P2-13 | Más localizaciones dentro del sistema fijo (estaciones/zonas adicionales, siguiendo el esquema de datos de P1D-02, sin tocar su arquitectura) |

## Restricciones de arquitectura específicas

- **No dupliques IA.** La tentación natural es escribir una IA de torreta, otra de
  NPC a pie y otra de nave de patrulla como tres sistemas independientes. No lo
  hagas: P2-11 debe ser el único framework de decisión (selección de objetivo,
  transición de estados, umbral de hostilidad por reputación), consumido por los
  tres contextos mediante los componentes específicos de cada uno (`TurretMount`,
  `CharacterController`, `RigidBody6DOF`). Si un contexto necesita algo que el
  framework no cubre, extiende el framework, no bifurques la lógica.
- El daño por componente (P2-02) generaliza el `DamageEvent` ya introducido en Fase
  1 (P1A-09/P1B-06): añade un campo de "subsistema objetivo" en vez de crear un tipo
  de evento nuevo por componente dañable.
- La tripulación NPC (P2-01) usa el mismo patrón de "espacio local de la nave" que
  el jugador a pie dentro de una nave (P1B-08) — un NPC de tripulación no es una
  entidad con física propia flotando junto a la nave, es una entidad en el espacio
  local de la nave igual que el jugador.
- La simulación económica dinámica (P2-08) sigue sin contenedores de tamaño
  dinámico en el loop: los eventos que afectan precios se procesan como una cola de
  tamaño fijo consumida una vez por tick económico (que no tiene por qué correr cada
  frame — puede ser cada N segundos de tiempo de juego, es una simulación de fondo,
  no un sistema de tiempo real estricto).
- Los encuentros aleatorios (P2-12) usan el `Pool` de entidades de nave/NPC ya
  existente: aparecen ocupando slots del pool según proximidad del jugador, y
  liberan el slot al alejarse o ser destruidos — nunca instancian por fuera del pool.

## Definition of Done específico

- Una nave con tripulación NPC completa puede perder un subsistema concreto en
  combate (ej. escudo) y seguir volando con el resto de sistemas operativos,
  demostrable en el HUD (P1E-02) mostrando el estado por subsistema.
- Un NPC hostil a pie y una nave NPC hostil en espacio reaccionan de forma
  coherente al mismo framework de reputación: bajar la reputación de una facción
  provoca hostilidad en ambos contextos sin tocar código distinto para cada uno.
- `STATUS.md` cierra la fase con una lista de qué subsistemas de daño existen por
  tipo de entidad (nave/personaje) y qué misiones encadenadas quedaron implementadas.

---
Siguiente: `09-FASE-3-EXPANSION-DE-CONTENIDO.md`.
