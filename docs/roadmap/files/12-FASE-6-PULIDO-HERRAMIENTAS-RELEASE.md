# Fase 6 — Pulido, Herramientas y Release

Depende de: Fase 5 completa (o de cualquier fase anterior, si Miguel decide cerrar
el alcance del proyecto ahí — esta fase aplica a lo que exista en ese momento).
A diferencia de las fases anteriores, **esta fase no tiene un final natural**: es el
punto donde el proyecto entra en un ciclo de mejora continua. Trátala como una lista
de trabajo recurrente, no como una lista que se vacía una vez.

## Objetivo de esta fase

Que lo construido en las fases 0-5 sea estable, razonablemente eficiente, y
distribuible — no que tenga más features. Si en esta fase surge la tentación de
añadir un pilar nuevo, es que se ha vuelto a la Fase 2/3/4 sin decirlo: vuelve al
documento correspondiente.

## Tareas

| ID | Tarea |
|---|---|
| P6-01 | Perfilado de rendimiento (CPU y GPU) e identificación de cuellos de botella reales, no supuestos |
| P6-02 | Pulido de UI/UX final (si P1E-01 eligió ImGui temporal, este es el momento de decidir si se sustituye) |
| P6-03 | Pulido de audio: mezcla final, música dinámica según contexto (combate/exploración) si aplica |
| P6-04 | QA sistemático: checklist de pruebas por sistema + registro simple de bugs conocidos |
| P6-05 | Herramientas internas adicionales según necesidad real detectada (no especular de antemano) |
| P6-06 | Logging/telemetría de crashes persistente entre sesiones (útil para depurar sesiones largas del agente sin supervisión) |
| P6-07 | Build de release y empaquetado para distribución |
| P6-08 | Documentación de usuario final: controles, manual básico de juego |
| P6-09 | Pase de balance de gameplay (economía, dificultad de combate, curva de progresión) |
| P6-10 | Revisión de robustez: edge cases de guardado/carga y de red bajo condiciones adversas (desconexión, archivo corrupto) |

## Restricciones de arquitectura específicas

- P6-01 (perfilado) va antes que cualquier optimización puntual: no "optimices" un
  sistema porque parezca lento, mide primero con una herramienta de profiling real
  (`perf`, Tracy, o el overlay de P0-11 ampliado con timers por sistema) y prioriza
  por impacto medido.
- Cualquier refactor de esta fase debe mantener el Definition of Done genérico de
  `00-MASTER-ROADMAP.md` §4 (sigue siendo DOD/ECS, cero allocs en el loop, etc.) —
  el pulido no es excusa para relajar las reglas de arquitectura que sostienen el
  rendimiento del proyecto.
- P6-04/P6-10: usa `STATUS.md` (o un archivo hermano, ej. `ISSUES.md`, si la lista
  crece demasiado para la bitácora de progreso) con el mismo formato breve que ya
  se viene usando — no montes un sistema de tracking externo para un proyecto de
  este tamaño.
- P6-06 (telemetría de crashes) es especialmente valiosa para este proyecto en
  concreto: si el agente trabaja horas sin supervisión, un crash silencioso a mitad
  de una tarea larga es tiempo perdido sin que Miguel lo sepa hasta revisar el
  móvil. Un log persistente con la última tarea en curso al momento del crash es
  más útil aquí que en un proyecto con desarrollo puramente humano.

## Definition of Done específico

- Existe un build de release (`cmake --build build --config Release` o equivalente)
  que arranca, carga el sistema estelar, y es jugable de principio a fin sin el
  overlay de depuración de P0-11 visible por defecto.
- `STATUS.md`/`ISSUES.md` refleja el estado real de bugs conocidos, no una lista
  vacía por omisión.
- Un crash forzado deliberadamente (ej. desconectar la red a mitad de una sesión
  multijugador, o corromper un archivo de guardado a mano) produce un fallo
  controlado y registrado, nunca un cierre silencioso sin rastro.

---
No hay "siguiente documento": a partir de aquí, vuelve a `STATUS.md` para decidir si
se reabre una fase anterior con más contenido/profundidad o si el ciclo de esta fase
continúa indefinidamente como mantenimiento.
