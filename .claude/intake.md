# Intake checklist — ClonStarCitizen

## 1. Identificación

- **Nombre del proyecto:** ClonStarCitizen
- **Ruta:** `/home/tenismiguel/ClonStarCitizen` (no bajo `~/proyects/`; decisión
  explícita de Miguel el 2026-08-29 para no mover working dir/build en esta máquina)
- **Repo git ya existente:** sí — `github.com/miguelzamitabastos/ClonStarCitizen`
- **Documentación de partida revisada:** `STATUS.md`, `AGENTS.md`, `.cursorrules`,
  `.cursor/agents/*.md`, `.cursor/skills/fast_compile/`, `docs/roadmap/files/*`,
  `docs/game-modules.md`

## 2. Clasificación de dominio

- **Dominio detectado:** juego / motor gráfico (C++20 ECS/DOD + Vulkan)
- **Justificación:** `.cursorrules` fija reglas de motor (DOD/ECS, cero
  allocation en el loop, GLM, instancing); `docs/roadmap/files/` estructura el
  desarrollo en fases de un motor de vuelo espacial + FPS a pie tipo Star Citizen

## 3. Roles propuestos

| Rol | ¿Aplica? | Notas de adaptación al dominio |
|---|---|---|
| `architect` | Sí | Mantiene `STATUS.md` (bitácora/progreso) y roadmap por fases |
| `implementer` | Sí | Tareas de propósito general |
| `vulkan-pipeline-expert` (domain-specialist) | Sí | Portado tal cual desde `.cursor/agents/` |
| `ecs-gameplay-programmer` (domain-specialist) | Sí | Portado tal cual desde `.cursor/agents/` |
| `tester` | Sí, adaptado | Sin suite automatizada: build limpio + smoke test headless (Xvfb/lavapipe) |
| `code-reviewer` | Sí | Incluye chequeo de `.cursorrules` como criterio bloqueante |
| `devops-pr-manager` | No, por ahora | Proyecto en solitario, ramas simples `release/fase-N`; se añade si hace falta |
| `researcher` | Sí | Vulkan/Flecs/GLM/GLFW puntual |

## 4. Estructura de carpeta

- [x] `.claude/agents/` creado con los 7 roles aprobados
- [x] `.claude/hooks/` creado (hooks base copiados)
- [x] `CLAUDE.md` generado a partir de `_framework/CLAUDE.md.template`
- [x] Repo ya existía: no se tocó código existente, solo se añadió `.claude/` y `CLAUDE.md`

## 5. CLAUDE.md del proyecto

- [x] Sección "Qué es" rellenada
- [x] Estado inicial: **Activo** (retomado desde "Pausado")
- [x] Fases extraídas de `docs/roadmap/files/00-MASTER-ROADMAP.md`, enlazadas con wikilinks
- [x] Reglas no negociables específicas del dominio (`.cursorrules`) añadidas

## 6. Canal de desarrollo remoto y documentación de continuidad

- **Canal recomendado:** daemon local (esta máquina, Windows+WSL con RTX 5060 Ti)
  — justificación: necesita SDK Vulkan real y, para validar render con GPU física,
  la máquina concreta. `RemoteTrigger` queda anotado como viable solo para build +
  smoke test headless (lavapipe), no como canal principal.
- **Requisito de hardware:** GPU / hardware específico (RTX 5060 Ti)
- **Documentación de continuidad:** `STATUS.md` (no hay `phase-gates.json` en
  este proyecto — el avance de fase lo decide Miguel en conversación directa, no
  hay mecanismo de Telegram montado en esta máquina)
- [ ] Proyecto añadido a `PROJECTS` en `_framework/mobile-control/daemon.py` — **no
  aplica todavía**: el daemon de Telegram es infraestructura de macOS (launchd) y
  no se ha montado en esta máquina Windows/WSL; queda pendiente si se decide
  replicarlo aquí más adelante

## 7. Skills y MCPs sugeridos

- **Skills de oficina ya disponibles que aplican:** `fast_compile` (build,
  portado también como skill de proyecto), `obsidian-markdown`/`obsidian-bases`/
  `json-canvas`/`defuddle` (`kepano/obsidian-skills`, instalado a nivel de
  oficina el 2026-08-29 — ver `framework-universal-oficina-claude.md` sección 21)
- **MCPs específicos del dominio:** ninguno instalado por ahora
- **Nota:** `obsidian-cli` (del mismo plugin) no es utilizable desde esta sesión
  WSL contra el Obsidian de Windows (IPC local, mismo SO) — limitación conocida,
  no una tarea pendiente

## 8. Hooks

- [x] Hooks base copiados desde `_framework/hooks-base/`: `block-secrets.sh`,
  `require-tests-before-merge.sh`, `telegram-notify.sh` (instalado pero inactivo:
  no hay `_framework/office-notify.env` configurado en esta máquina)
- **Hooks específicos del dominio:** ninguno por ahora

## 9. Remoto de GitHub

- [x] Repo ya existente: N/A, ya tenía remoto

## 10. Directorio de confianza

- [ ] **Pendiente que Miguel confirme** `hasTrustDialogAccepted` para
  `/home/tenismiguel/ClonStarCitizen` en `~/.claude.json` de esta máquina — no
  automatizable por ningún agente (bloqueado por el clasificador de Auto Mode)

## 11. Portfolio

- [x] `PORTFOLIO.md` de `ClaudeWorkstation` actualizado: ClonStarCitizen pasa de
  "Pausado" a "Activo"

## 12. Aprobación

- **Propuesta presentada el:** 2026-08-29
- **Aprobada por el usuario el:** 2026-08-29
- **Ajustes pedidos antes de aprobar:** ninguno sobre la propuesta de oficina en
  sí; se añadió en la misma sesión la incorporación del skill `obsidian-skills`
