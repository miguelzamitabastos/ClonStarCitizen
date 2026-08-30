# ClonStarCitizen

## Qué es

Motor de juego propio en C++20, Data-Oriented Design / ECS (Flecs) sobre Vulkan
(ventana GLFW, matemática GLM). No es un client/server: un único ejecutable
nativo (`clon_star_citizen`). Ambición tipo Star Citizen: vuelo espacial 6DOF,
FPS a pie dentro de naves/estaciones, economía/misiones, universo con floating
origin, HUD/audio, guardado/carga persistente.

## Estado

**Activo** — incorporado a la oficina de ClaudeWorkstation el 2026-08-29 (venía
de "Pausado" en `PORTFOLIO.md`, retomado por Miguel). Fase activa: **Fase 5 —
Multijugador y Red** (Fases 2/3/4 cerradas y validadas físicamente por Miguel el
2026-08-30). Progreso detallado, bitácora y contratos técnicos vigentes en
[STATUS.md](STATUS.md) — es la fuente de verdad del estado, se actualiza al
cierre de cada tarea.

## Fases del proyecto

Roadmap completo en `docs/roadmap/files/` (wikilinks + ruta real):

- [[00-MASTER-ROADMAP]] (`docs/roadmap/files/00-MASTER-ROADMAP.md`)
- [[01-FASE-0-CIMIENTOS-DEL-MOTOR]] — completada
- [[02-FASE-1A-VUELO-Y-NAVES]] — completada
- [[03-FASE-1B-A-PIE-Y-FPS]] — completada
- [[04-FASE-1C-ECONOMIA-Y-MISIONES]] — completada
- [[05-FASE-1D-UNIVERSO-FIJO-Y-MUNDO]] — completada
- [[06-FASE-1E-UI-HUD-AUDIO]] — completada
- [[07-FASE-1F-PERSISTENCIA-Y-GUARDADO]] — completada
- [[08-FASE-2-PROFUNDIDAD-DE-SISTEMAS]] — completada
- [[09-FASE-3-EXPANSION-DE-CONTENIDO]] — completada
- [[10-FASE-4-UNIVERSO-PROCEDURAL]] — completada
- [[11-FASE-5-MULTIJUGADOR-Y-RED]] — **en curso** (ver progreso en STATUS.md)
- [[12-FASE-6-PULIDO-HERRAMIENTAS-RELEASE]] — pendiente

## Roles de la oficina en este proyecto

| Rol | Qué hace |
|---|---|
| `architect` | Traduce la fase activa en tareas, mantiene `STATUS.md` al día |
| `implementer` | Implementa tareas de fase de propósito general |
| `vulkan-pipeline-expert` (domain-specialist) | Shaders, pipelines, swapchain, buffers GPU |
| `ecs-gameplay-programmer` (domain-specialist) | Componentes/sistemas Flecs, gameplay ECS |
| `tester` | Build limpio (`fast_compile`) + smoke test headless (Xvfb/lavapipe) de la escena de demo, sin suite automatizada tradicional |
| `code-reviewer` | Revisa cumplimiento de `.cursorrules` y contratos de fase antes de mergear |
| `researcher` | Vulkan/Flecs/GLM/GLFW puntual; usa `defuddle` para research web barato en tokens |

`git-committer` (genérico de oficina) se invoca desde `implementer`/quien toque
código, no tiene rol propio en este proyecto.

## Canal de desarrollo remoto

**Dos canales activos a la vez — leer antes de asumir que uno es el único:**

1. **Daemon local**, en esta misma máquina (Windows + WSL, RTX 5060 Ti) — el motor
   necesita compilar contra un SDK Vulkan real y, para ver render con GPU física,
   la máquina concreta que la tiene. Es el canal de esta oficina (ClaudeWorkstation).
2. **Cursor Cloud** (ver `AGENTS.md`, `.cursor/agents/`) — corre directo contra el
   repo de GitHub, sin GPU, verificando con Vulkan por software (lavapipe/llvmpipe
   vía Xvfb). Ya ha implementado y fusionado a `main` trabajo real de Fase 2 (PR #6)
   de forma independiente a esta oficina.

**Por eso, `git fetch origin` + comparar con `origin/main` es obligatorio antes de
planificar o escribir código de la fase activa, no solo antes de pushear** —
detectado en vivo el 2026-08-29: esta oficina implementó P2-01..05 por duplicado
sobre un checkout desincronizado con lo que Cursor Cloud ya había fusionado,
descubierto solo al hacer `push` (framework `ClaudeWorkstation`, sección 22).

`RemoteTrigger` (routine en la nube de Anthropic) es viable igualmente **solo**
para build + smoke test headless, mismo argumento lavapipe/Xvfb — no sustituye
validar render con hardware real, que sigue siendo cosa del daemon local.

**Requisito de hardware:** GPU / hardware específico (RTX 5060 Ti de esta máquina).

## Documentación de continuidad

[STATUS.md](STATUS.md) (fase activa, progreso por tarea, bitácora con fecha,
contratos técnicos vigentes) es la única fuente de estado entre sesiones sin
conversación de por medio. Toda sesión la lee antes de empezar y la actualiza
al terminar trabajo con impacto real.

**Convención de enlace entre documentos (skill de oficina `obsidian-markdown`,
`kepano/obsidian-skills`):** los documentos de fase y `STATUS.md` se referencian
con wikilinks `[[nombre-sin-extensión]]` además de la ruta real — permite saltar
directo entre documentos relacionados (menos grep, menos tokens) y, si se abre
`docs/roadmap/files/` como vault en Obsidian, los enlaces resuelven igual.
`obsidian-cli` (pilotar una instancia real de Obsidian) **no** funciona desde
esta sesión WSL contra el Obsidian de Windows — IPC local, mismo SO requerido
(detalle en `framework-universal-oficina-claude.md` sección 21 de
`ClaudeWorkstation`); las convenciones de formato (wikilinks, properties,
callouts) sí aplican siempre, con o sin la app abierta.

## Reglas no negociables

- Ningún commit puede contener claves de API en texto plano.
- **Arquitectura del motor (`.cursorrules`, sin excepción):** prohibida la OOP
  clásica para entidades de juego; DOD/ECS estricto. Cero asignación dinámica
  dentro del bucle Update/Render — pools/arenas pre-asignados; destruir = marcar
  slot inactivo, nunca liberar en vivo. Matemática vectorial EXCLUSIVAMENTE GLM
  vía `include/engine/math/glm.hpp`. Minimizar draw calls (instancing).
- No hay suite de tests automatizada: el "tests en verde" del hook
  `require-tests-before-merge.sh` = build limpio (`fast_compile`, sin warnings
  nuevos) + smoke test headless de la escena de demo relevante, registrado por
  `tester` en `.claude/.last-test-run`.
- Si una fase o tarea tiene un criterio de salida que solo Miguel puede validar
  (ej. probar una escena a mano), el último mensaje de la sesión incluye una
  sección `### 🔎 VERIFICACIÓN MANUAL` con pasos concretos — el hook
  `telegram-notify.sh` está instalado pero inactivo en esta máquina (sin
  `_framework/office-notify.env` configurado aquí), así que hoy esa sección solo
  llega por el mensaje de la sesión, no por Telegram.
- Ninguna sesión termina con cambios de código sin commitear — quien los hizo
  invoca el protocolo de `git-committer` (revisar diff, nunca `add -A` a ciegas,
  nunca secretos) antes de dar la tarea por terminada. El `push` a remoto sigue
  necesitando que Miguel lo pida explícitamente en este proyecto (a diferencia
  de `ClaudeWorkstation`, que sí tiene autopush).
- Antes de resolver una tarea desde cero, comprueba si ya existe un skill (de
  oficina o de `.claude/skills/`) que la cubra — incluye `fast_compile` (build)
  y `obsidian-markdown`/`defuddle` (documentación/research).

## Convenciones de código

- **Stack:** C++20, CMake + FetchContent (Flecs 4.1.6, GLM 1.0.1), Vulkan
  (GLFW para ventana/input), Dear ImGui (UI/HUD), miniaudio (audio 3D).
- **Estructura:** `include/engine/` y `src/engine/` (motor: config, ECS, input,
  platform, debug, scene) vs. `include/game/` y `src/game/` (gameplay: character,
  combat, flight, ui, world). Ver `docs/game-modules.md` para el mapa completo.
- **Compilación:** `.claude/skills/fast_compile/SKILL.md` —
  `cmake --build build -j$(nproc)`, es el comando de verificación obligatorio.
- **Contenido dirigido por datos (Fase 3+):** las naves, armas y demás catálogos
  viven en `assets/data/*.cfg` (formato `[[sección]]` + `clave=valor`). Antes de
  tocar un `.cfg` o compilar tras editarlo, corre
  `python3 tools/validate_catalogs.py` (P3-08): comprueba ids únicos y referencias
  cruzadas resolubles en segundos, sin build.
- Documentación y mensajes de commit en español, siguiendo el estilo ya
  existente en `STATUS.md` y el historial de commits del repo.
