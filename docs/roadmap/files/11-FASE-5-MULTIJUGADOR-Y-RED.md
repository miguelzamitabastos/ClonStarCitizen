# Fase 5 — Multijugador y Red

Depende de: Fase 4 completa. Esta fase es donde se cobra (o se paga caro) la
decisión tomada desde `00-MASTER-ROADMAP.md` §0.1: si la separación entre "estado
de simulación" y "presentación/input" se respetó de verdad desde la Fase 1, esta
fase es una extensión; si no, es una reescritura. La primera tarea es, precisamente,
comprobarlo.

## Objetivo de esta fase

Arquitectura cliente-servidor autoritativa: el servidor es la única fuente de
verdad del estado del juego (posición, daño, economía, inventario), los clientes
envían intención (input) y reciben snapshots del estado, con interpolación para que
se vea fluido. Alcance realista para un proyecto de este tamaño: sesiones
multijugador pequeñas (varios jugadores en el mismo sistema/instancia), no
"server meshing" a escala de cientos de jugadores simultáneos por sistema — eso
excede lo que un proyecto de este tamaño puede mantener y no es un objetivo honesto
(ver framing de escala en `00-MASTER-ROADMAP.md` §0).

## Tareas

| ID | Tarea | Depende de |
|---|---|---|
| P5-01 | Auditoría: ¿qué sistemas violan la separación simulación/input? Refactor de lo necesario | Todo lo anterior |
| P5-02 | Elección y documentación de arquitectura de red (protocolo, librería) | P5-01 |
| P5-03 | Serialización de red (snapshots de estado, no el formato de guardado de P1F) | P5-02 |
| P5-04 | Loop de servidor autoritativo (mismo binario en modo servidor, o proceso separado) | P5-02 |
| P5-05 | Replicación de entidades: qué se envía, a qué frecuencia, interpolación en cliente | P5-03, P5-04 |
| P5-06 | Input del cliente → comandos al servidor; reconciliación (empezar sin predicción) | P5-04, P5-05 |
| P5-07 | Migración de sistemas de Fase 1-4 a contexto multi-cliente (economía compartida, instancias de misión) | P5-01, P5-05 |
| P5-08 | Conexión/autenticación básica (sesiones, sin infraestructura de cuentas compleja) | P5-04 |
| P5-09 | Validación server-side de acciones críticas (daño, comercio, posición) — anti-cheat mínimo | P5-05, P5-07 |
| P5-10 | Escena/test `multiplayer_test`: dos clientes locales contra un servidor local | P5-01..09 |

## Restricciones de arquitectura específicas

- **El servidor es la única fuente de verdad.** Ningún cliente decide si un disparo
  ha hecho daño, si una transacción de comercio es válida, o dónde está una nave: el
  cliente envía intención (`"disparar"`, `"comprar X"`, `"empuje = (1,0,0)"`) y el
  servidor aplica la lógica ya existente de Fase 1-4 (los mismos sistemas de física,
  daño y economía, sin duplicarlos) y difunde el resultado. Si P5-01 descubre un
  sistema que calcula resultados en el cliente y solo "informa" al servidor, hay que
  invertir esa relación.
- El servidor corre su propio bucle a paso fijo (reutiliza el scheduler de P0-04),
  con las mismas restricciones de cero-alocación-en-el-loop que el cliente: un
  servidor con varios jugadores es más sensible a latencia de GC/allocs que un
  cliente en solitario, no menos.
- La replicación (P5-05) no envía el estado ECS completo cada tick: define qué
  componentes son "replicables" (posición, orientación, HP, subsistemas dañados) y a
  qué frecuencia, con delta-compression básica (solo enviar lo que cambió) desde el
  principio si la librería elegida en P5-02 lo facilita, o como mejora inmediata si
  no.
- Empieza P5-06 **sin client-side prediction.** Interpola en el cliente entre
  snapshots del servidor (más simple, más fácil de depurar con el agente trabajando
  sin supervisión constante) y añade predicción de movimiento del jugador local solo
  si la latencia percibida en pruebas reales lo justifica — documenta esa decisión
  en `STATUS.md` en vez de implementar predicción especulativamente.
- P5-07 es donde se decide qué es "compartido" (la economía del sistema, el estado
  del mundo) y qué es "por instancia de misión" (una misión de combate puede generar
  su propia instancia aislada). Esta decisión de diseño de juego, si no está clara,
  cae bajo la condición de parada #2 de `00-MASTER-ROADMAP.md` solo si afecta a
  identidad/branding; las decisiones puramente técnicas de instancing se toman y se
  documentan, no se bloquean.
- P5-09: no implementes un sistema anti-cheat sofisticado (heurísticas de detección,
  etc.) — el objetivo mínimo es que el servidor **nunca confíe en un valor que el
  cliente podría falsificar** (posición absoluta, cantidad de dinero, daño infligido)
  sin validarlo contra su propia simulación.

## Definition of Done específico

- `multiplayer_test` permite a dos clientes locales conectarse al mismo servidor,
  verse mutuamente moverse con interpolación fluida, y que una acción de un cliente
  (disparo, transacción) se refleje correctamente en el otro cliente vía el servidor.
- Modificar manualmente un valor en un cliente de prueba (ej. forzar posición o
  dinero por depuración) no tiene efecto en el estado real una vez el servidor lo
  rechaza — demuestra que el servidor es autoritativo.
- `STATUS.md` documenta la arquitectura de red elegida (protocolo, librería,
  frecuencia de tick de red) porque es la referencia para cualquier optimización de
  Fase 6.

---
Siguiente: `12-FASE-6-PULIDO-HERRAMIENTAS-RELEASE.md`.
