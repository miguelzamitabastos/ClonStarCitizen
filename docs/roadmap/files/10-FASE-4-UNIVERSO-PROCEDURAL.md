# Fase 4 — Universo Procedural

Depende de: Fase 3 completa. Esta fase reemplaza la simplificación deliberada de la
Fase 1D (universo fijo, zonas de aterrizaje prefabricadas y finitas) por generación
real. Es la fase técnicamente más exigente después de la Fase 0.

## Objetivo de esta fase

Terreno planetario real (esférico, con streaming por LOD) generado proceduralmente,
más de un sistema estelar navegable, y suficiente contenido curado insertado sobre
lo procedural para que no se sienta vacío. El sistema estelar fijo de la Fase 1
pasa a ser un caso particular (semilla fija) del generador, no se descarta.

## Tareas

| ID | Tarea | Depende de |
|---|---|---|
| P4-01 | Algoritmo de generación de sistema estelar (semilla → estrella, planetas, órbitas) | P1D-02 |
| P4-02 | Generación procedural de terreno planetario (heightmap por ruido, esférico) | — |
| P4-03 | Streaming de terreno por chunks con LOD (extiende el streaming de P1D-03) | P4-02, P1D-03 |
| P4-04 | Migración del sistema fijo actual a semilla fija del generador (no perder contenido) | P4-01 |
| P4-05 | Navegación entre sistemas (puntos de salto / jump points o equivalente) + mapa de galaxia simplificado | P4-01 |
| P4-06 | Distribución procedural de recursos minables (asteroides/superficie) | P4-02 |
| P4-07 | Inserción de puntos de interés curados sobre el terreno procedural (estaciones, POIs a mano) | P4-02, P3-04 |
| P4-08 | Escena demo `procedural_test`: viaje entre dos sistemas generados + aterrizaje en terreno real | P4-01..07 |

## Restricciones de arquitectura específicas

- **Todo generado debe ser determinista por semilla.** Dado el mismo seed, el mismo
  sistema/planeta/terreno debe reproducirse exactamente igual en cualquier máquina y
  en cualquier sesión. Esto es lo que permite guardar partidas (Fase 1F) sin tener
  que serializar el terreno entero: se guarda la semilla + una lista de
  modificaciones/deltas respecto a lo generado (ej. recursos ya minados), no el
  terreno completo.
- El terreno por chunks (P4-03) usa un **pool de chunks de tamaño fijo** en memoria
  (ej. `Pool<TerrainChunk, kMaxLoadedChunks>`), nunca contenedores que crezcan sin
  límite: si el jugador se mueve más rápido de lo que el streaming puede cargar, se
  degrada el LOD lejano antes que exceder la capacidad reservada.
- La generación de un chunk nuevo corre en un hilo de fondo (extiende el pipeline
  async de P0-07/P1D-03), escribiendo a un buffer reservado de antemano; la subida a
  GPU (vertex buffer del chunk) ocurre en el hilo principal, igual que el resto de
  cargas de malla del proyecto.
- El floating origin de P1D-07 pasa a ser crítico a esta escala: verifica
  explícitamente que el umbral de rebase elegido en Fase 1 sigue siendo razonable a
  distancias planetarias/interplanetarias reales; si no, ajústalo aquí y documenta
  el cambio — no lo dejes como estaba "porque ya funcionaba" en el sistema pequeño
  de la Fase 1.
- P4-05 (navegación entre sistemas): el "salto" entre sistemas puede implementarse
  como una transición de carga (descargar sistema actual, cargar destino) en vez de
  simulación continua de viaje interestelar — es una simplificación razonable y
  coherente con cómo funcionan los jump points en Star Citizen a nivel jugable.
- Los recursos minables (P4-06) son la base de una mecánica de minería que hasta
  ahora no existía como pilar propio: si se implementa aquí, hazlo como una
  extensión del sistema de `CargoHold`/economía de Fase 1C (minar = obtener
  commodity, igual que comprarlo, con distinta fuente), no como un sistema paralelo.

## Definition of Done específico

- `procedural_test` genera dos sistemas estelares distintos a partir de dos semillas
  distintas, con terreno planetario navegable en al menos uno de ellos, y permite
  viajar de uno a otro.
- Recargar el mismo seed produce exactamente el mismo sistema/terreno (verificable
  comparando un hash de los chunks generados, o inspección visual reproducible).
- El streaming de terreno mantiene el framerate objetivo definido en Fase 0 sin
  pausas perceptibles al moverse rápido sobre la superficie (medir con el overlay
  de P0-11).

---
Siguiente: `11-FASE-5-MULTIJUGADOR-Y-RED.md`.
