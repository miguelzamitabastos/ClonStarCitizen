# Fase 1D — Universo Fijo y Mundo

Depende de: Fase 0 completa. P1D-05 depende de la técnica de "interior navegable" de
P1B-08. No depende de P1A/P1C para su parte técnica, pero el resultado de esta fase
es el escenario donde P1A/P1B/P1C se demuestran juntas.
Carpeta de trabajo: `src/game/world/`, `include/game/world/`.
Escena demo objetivo: `--scene=universe_test`.

## Objetivo de esta fase

Un único sistema estelar, hecho a mano, con un puñado de localizaciones (una
estación, una o dos "zonas de aterrizaje" planetarias simplificadas, espacio abierto
entre ellas) navegable sin fisuras y sin errores de precisión de punto flotante a
pesar de las distancias. Esta fase NO incluye generación procedural ni planetas
esféricos completos con terreno — eso es Fase 4 (`10-FASE-4-UNIVERSO-PROCEDURAL.md`).

## Tareas

| ID | Tarea | Depende de |
|---|---|---|
| P1D-01 | Esquema de coordenadas del universo: floating origin (decisión y contrato) | P1A-01 (nota de precisión) |
| P1D-07 | Implementación de floating origin: rebase del origen relativo a cámara/jugador | P1D-01 |
| P1D-02 | Datos del sistema estelar fijo, cargados por configuración (P0-05) | P1D-01 |
| P1D-04 | Contenido concreto del sistema inicial (nº de planetas, nombres, tema) — **ver nota de bloqueo** | P1D-02 |
| P1D-03 | Streaming de niveles por proximidad (espacio / superficie / interior) | P0-07 |
| P1D-05 | Estaciones espaciales como interiores navegables (reutiliza P1B-08) | P1B-08, P1D-03 |
| P1D-06 | Transición espacio abierto → zona de aterrizaje planetaria simplificada | P1D-03 |
| P1D-08 | Escena demo `universe_test`: viaje completo espacio ↔ estación ↔ superficie | P1D-01..06 |

## Nota de bloqueo explícita — P1D-04

El **esqueleto técnico** de P1D-04 (cuántos slots de planeta/estación soporta el
sistema de datos, estructura del archivo de configuración) se implementa sin
esperar a nadie. Pero el **contenido** (nombres del sistema/planetas/estaciones,
tema visual, lore mínimo) es una decisión de identidad del juego — cae dentro de la
condición de parada #2 de `00-MASTER-ROADMAP.md`. Usa nombres placeholder
(`Sistema-01`, `Estacion-Alfa`, `Planeta-01`) y funcionalmente completa la tarea;
anota en `STATUS.md` como bloqueado-parcial que el naming final necesita input de
Miguel, y sigue con el resto de tareas — no es motivo para parar toda la fase.

## Restricciones de arquitectura específicas

- **Floating origin (P1D-01/P1D-07) es la decisión más importante de esta fase.**
  Todas las entidades siguen almacenando su posición como `f32` relativa a un
  "origen actual", que se re-centra (rebase) cuando el jugador se aleja más de un
  umbral configurable del origen vigente. El rebase desplaza todas las posiciones
  activas de golpe (una pasada O(n) sobre entidades vivas, no por frame — solo
  cuando se cruza el umbral) y nunca cambia la posición *relativa* entre entidades,
  solo el marco de referencia. Documenta el contrato exacto en un comentario de
  cabecera del componente de posición para que Fase 1A/1B/4/5 lo respeten.
- Alternativa aceptable si se justifica en `STATUS.md`: doble precisión (`f64`) para
  la posición "de mundo" con conversión a `f32` relativo-a-cámara solo antes de
  subir a GPU. Elige una de las dos y no las mezcles a medias.
- Los datos del sistema estelar (P1D-02) son de nuevo configuración cargada una vez,
  no código: lista fija de cuerpos celestes con posición/tipo/radio/localizaciones
  asociadas. El nº máximo de cuerpos es una constante conocida en compilación
  (`kMaxCelestialBodies`), consistente con el resto del proyecto (arrays fijos).
- El streaming por proximidad (P1D-03) carga/descarga el contenido pesado (mallas,
  texturas del interior de una estación, de una zona de aterrizaje) usando el
  pipeline async de P0-07, activado por distancia del jugador a un volumen de
  "trigger" — nunca bloquea el hilo principal ni el frame loop.
- Las zonas de aterrizaje planetarias (P1D-06) en esta fase son **prefabricadas y
  finitas** (un área jugable delimitada, no un planeta esférico real): es
  intencionalmente una simplificación hasta la Fase 4. No inviertas esfuerzo en
  curvatura de planeta o terreno infinito ahora.

## Definition of Done específico

- `universe_test` permite: despegar de una estación, volar por espacio abierto una
  distancia significativa (suficiente para forzar al menos un rebase de floating
  origin — verificable en el overlay de P0-11 mostrando el nº de rebases), aterrizar
  en una zona planetaria, y volver — sin saltos de posición visibles ni pérdida de
  precisión (temblor/jitter) en ningún punto del trayecto.
- El streaming de niveles no introduce pausas perceptibles (medir frame time en el
  overlay durante una carga/descarga).
- `STATUS.md` documenta explícitamente qué esquema de coordenadas se eligió
  (floating origin vs doble precisión) y el umbral de rebase usado, porque Fase 4 y
  Fase 5 dependen directamente de ese contrato.

---
En paralelo con: `02-FASE-1A-VUELO-Y-NAVES.md`, `03-FASE-1B-A-PIE-Y-FPS.md`,
`04-FASE-1C-ECONOMIA-Y-MISIONES.md`.
