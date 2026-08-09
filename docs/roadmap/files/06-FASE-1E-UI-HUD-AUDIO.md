# Fase 1E — UI, HUD y Audio

Depende de: Fase 0 completa (en especial P0-11, el overlay de Dear ImGui). Consume
telemetría de P1A/P1B/P1C pero no bloquea su desarrollo — puede construirse contra
datos de prueba hasta que esos sistemas existan.
Carpeta de trabajo: `src/game/ui/`, `src/game/audio/`, `include/game/ui/`, `include/game/audio/`.
Escena demo objetivo: `--scene=ui_audio_test`.

## Objetivo de esta fase

HUD de vuelo y a pie, menús de navegación mínimos (pausa, carga/inventario,
misiones), y un sistema de audio 3D con las categorías básicas (SFX, música, UI)
enganchado a los eventos de gameplay ya existentes.

## Tareas

| ID | Tarea | Depende de |
|---|---|---|
| P1E-01 | Decisión y base de framework de UI para juego (¿ImGui reutilizado o capa propia?) | P0-11 |
| P1E-02 | HUD de vuelo: velocidad, energía, escudo, radar simple | P1E-01, P1A-06/07 |
| P1E-03 | HUD a pie: salud, munición, prompt de interacción contextual | P1E-01, P1B-01/06/07 |
| P1E-04 | Menús: pausa, mapa de sistema simple, inventario/carga, log de misiones | P1E-01, P1C-05 |
| P1E-05 | Motor de audio 3D: fuentes posicionales, categorías (SFX/música/UI), voice pool limitado | P0-07 (carga async) |
| P1E-06 | Eventos de audio enganchados a gameplay (disparo, motor, impacto, UI) | P1E-05, P1A-08, P1B-06 |
| P1E-07 | Escena demo `ui_audio_test`: HUDs + menú + sonido posicional simultáneos | P1E-01..06 |

## Restricciones de arquitectura específicas

- **P1E-01 — decisión explícita a tomar y documentar:** Dear ImGui (ya integrado en
  P0-11 como overlay de depuración) es pragmático para un proyecto solo+agente, pero
  no está pensado para UI final de un juego (estilo, layout, controles con mando).
  Dos caminos válidos: (a) usar ImGui también para la UI de juego en esta fase,
  aceptando que habrá que rehacer el look-and-feel en Fase 6 (pulido), o (b) montar
  ya una capa de UI propia mínima sobre geometría 2D + el pipeline de render
  existente. Para no bloquear el avance de la vertical slice, la recomendación por
  defecto es (a) — documenta en `STATUS.md` si se decide lo contrario y por qué.
- Sea cual sea el framework, **cero alocación dinámica por frame en el HUD**: los
  widgets se construyen con datos ya existentes en componentes ECS, sin crear
  strings/contenedores nuevos cada tick (usa buffers fijos reutilizados para texto
  formateado, ej. `char[64]` + `snprintf`, no `std::string` construido en el loop).
- El motor de audio (P1E-05) usa una librería de audio de bajo nivel apta para C++
  (evalúa `miniaudio` por ser header-only y sin dependencias del sistema pesadas;
  documenta en `CMakeLists.txt`/`STATUS.md` la elegida). Los buffers de sonido se
  cargan async igual que texturas/mallas (P0-07), a un allocator dedicado, y se
  reproducen desde un **voice pool de tamaño fijo** (`kMaxConcurrentVoices`): si se
  agota, se descarta la petición de menor prioridad, nunca se crea una voz extra.
- La atenuación 3D usa la posición de la entidad emisora relativa al listener
  (cámara del jugador) con las mismas utilidades de `engine/math/glm.hpp`, y debe
  ser consciente del floating origin de P1D-07 si ya está implementado (o preparada
  para serlo) — un emisor de audio a un rebase de origen no puede saltar de volumen.
- Categorías de audio (SFX/música/UI) tienen buses de volumen independientes desde
  el principio, aunque el mezclador sea simple, porque añadirlo después implica
  tocar cada punto de reproducción de sonido del juego.

## Definition of Done específico

- `ui_audio_test` muestra el HUD de vuelo y el de a pie con datos en vivo (no
  estáticos/hardcodeados), un menú de pausa funcional que detiene la simulación, y
  al menos tres sonidos posicionales simultáneos con atenuación por distancia
  audible.
- El voice pool no permite superar `kMaxConcurrentVoices` bajo ninguna circunstancia
  (verificar disparando más eventos de sonido que voces disponibles).
- Ningún widget de HUD/menú asigna memoria dinámica por frame (revisar con el mismo
  criterio de P0-11/ASan usado en fases anteriores).

---
En paralelo con: `02` a `05`. Depende conceptualmente de que existan datos reales
que mostrar, así que conviene avanzarla un poco por detrás de P1A/P1B/P1C.
