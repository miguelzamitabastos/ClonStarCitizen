# Fase 1F — Persistencia y Guardado

Depende de: Fase 0 completa, y de que P1A/P1B/P1C/P1D tengan sus estructuras de
datos razonablemente estables (no hace falta que estén "terminadas": empieza por lo
que ya exista y añade campos según se estabilicen los demás documentos).
Carpeta de trabajo: `src/game/save/`, `include/game/save/`.
Escena demo objetivo: `--scene=save_load_test`.

## Objetivo de esta fase

Guardar y cargar el estado completo de una partida: nave y su carga, posición del
jugador (nave o a pie), misiones activas, reputación, y estado del sistema estelar
(qué se ha explorado/desbloqueado). Cierra el bucle de juego de la Fase 1: sin esto
no hay "partida" real, solo una sesión que se pierde al cerrar.

## Tareas

| ID | Tarea | Depende de |
|---|---|---|
| P1F-01 | Diseño del formato de guardado (versión de esquema, binario vs texto) | — |
| P1F-02 | IDs estables para entidades guardables (independientes del handle interno de Flecs) | P1F-01 |
| P1F-03 | Serialización del estado ECS relevante (nave, carga, misiones, reputación, jugador) | P1F-02, P1A/P1B/P1C |
| P1F-04 | Sistema de guardado/carga a disco: slots + autosave | P1F-03 |
| P1F-05 | Manejo de versión de esquema (rechazo controlado o migración mínima) | P1F-01 |
| P1F-06 | Escena demo `save_load_test`: guardar en pleno vuelo/misión activa, cerrar, recargar | P1F-01..05 |

## Restricciones de arquitectura específicas

- **IDs estables (P1F-02) antes que nada.** El handle de entidad de Flecs no es
  estable entre sesiones ni fiable para referenciar en un archivo de guardado. Añade
  un componente `PersistentId { u64 id }` a toda entidad que deba sobrevivir a un
  guardado (nave del jugador, misiones activas, NPCs relevantes), asignado de forma
  determinista o incremental al crearse, y usa ese ID — nunca el handle nativo —
  como clave en el archivo de guardado y en cualquier referencia cruzada
  (ej. "la misión X apunta al NPC Y").
- El guardado/carga **no es parte del bucle de frame** y es, de hecho, uno de los
  pocos sitios del proyecto donde una alocación dinámica puntual (para construir el
  buffer serializado) es aceptable — pero aun así no debe bloquear el hilo principal
  de forma perceptible: si el volumen de datos crece, mueve la escritura a disco a
  un hilo de fondo y pausa/congela la simulación (no el render de UI) mientras tanto,
  con un estado explícito `SaveInProgress` reflejado en el HUD (P1E) para que el
  jugador no interactúe a medias de un guardado.
- El formato (P1F-01) debe llevar un número de versión de esquema desde el primer
  byte. No hace falta un sistema de migración sofisticado en esta fase: basta con
  que cargar un guardado de versión desconocida falle de forma controlada y visible
  (mensaje claro), nunca con datos corruptos silenciosos o un crash.
- Serializa solo lo que define el **estado del jugador y del mundo persistente**
  (nave, carga, misiones, reputación, flags de progreso/exploración del sistema),
  no datos derivables en carga (ej. no guardes posiciones de partículas de escombros
  temporales, ni el estado exacto de animaciones en curso).
- Si para esta fase se elige un formato basado en texto (ej. JSON) por simplicidad
  de depuración durante el desarrollo con el agente, documenta en `STATUS.md` que es
  una decisión temporal y que un formato binario más compacto es candidato para
  Fase 6 (pulido) si el tamaño de guardado se vuelve un problema.

## Definition of Done específico

- `save_load_test` permite: jugar (volar, tener carga en bodega, tener una misión
  activa, haber ganado/perdido reputación), guardar, cerrar el proceso por completo,
  reabrir, cargar, y verificar que todo el estado relevante coincide exactamente con
  el momento del guardado.
- Cargar un archivo de guardado con un número de versión distinto al esperado no
  crashea ni corrompe el estado: falla de forma clara y visible.
- El guardado de una partida con carga máxima en bodega y varias misiones activas no
  introduce una pausa perceptible mayor a la explícitamente comunicada en el HUD
  como "guardando".

---
Cierre de Fase 1: cuando P1A, P1B, P1C, P1D, P1E y P1F tengan su Definition of Done
cumplida y sus escenas demo integradas, `STATUS.md` debe declarar la Fase 1 cerrada
con un resumen de qué bucle de juego completo es jugable de principio a fin antes de
pasar a `08-FASE-2-PROFUNDIDAD-DE-SISTEMAS.md`.
