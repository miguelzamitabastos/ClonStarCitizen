# Fase 1C — Economía y Misiones

Depende de: Fase 0 completa. Depende parcialmente de P1A (`ShipHull`/carga vive en la
nave del jugador) pero puede desarrollarse con una nave de prueba mínima sin esperar
a que P1A esté al 100%.
Carpeta de trabajo: `src/game/economy/`, `include/game/economy/`.
Escena demo objetivo: `--scene=economy_test`.

## Objetivo de esta fase

Un bucle de comercio y misiones jugable de principio a fin: comprar mercancía barata
en un mercado, transportarla, venderla más cara en otro, y aceptar/completar
contratos de misión simples con recompensa. Sin simulación económica profunda
todavía (eso es Fase 2/3): precios simples con variación por oferta/demanda básica.

## Tareas

| ID | Tarea | Depende de |
|---|---|---|
| P1C-01 | Datos de economía: `Commodity`, `Market`, tabla de precios cargada desde datos (no hardcode disperso) | P0-05 |
| P1C-02 | Sistema de carga (`CargoHold`): capacidad fija por nave, peso/volumen por commodity | P1A-01 (o nave de prueba mínima) |
| P1C-03 | Transacciones de compra/venta + ajuste simple de precio local por oferta/demanda | P1C-01, P1C-02 |
| P1C-04 | Plantillas de misión + generador simple (parámetros aleatorios sobre plantilla) | P0-05 |
| P1C-05 | Misiones activas del jugador: aceptar, trackear objetivos, completar, recompensa | P1C-04 |
| P1C-06 | NPCs simples: comerciantes/contactos de misión, diálogo por menú (sin IA compleja) | P1B-07 (interacción) |
| P1C-07 | Reputación básica por facción (un escalar por facción) | P1C-05 |
| P1C-09 | Escena demo `economy_test`: comprar → transportar → vender con margen → aceptar y completar una misión | P1C-01..07 |

(No hay P1C-08 numerada aparte: la serialización real vive en Fase 1F; aquí solo
diseña las estructuras pensando en que sean triviales de volcar a disco — evita
punteros crudos o referencias no indexables dentro de `Commodity`/`Market`/`Mission`.)

## Restricciones de arquitectura específicas

- `Commodity` y `Market` son **datos de configuración**, cargados una vez al inicio
  desde archivos (JSON/CSV/tu formato preferido, vía el sistema de config de P0-05),
  no structs hardcodeadas repartidas por el código. Añadir un bien nuevo al juego
  debe ser editar datos, no recompilar lógica.
- `Market` tiene un array fijo de precios por commodity (tamaño = nº total de
  commodities definidas, conocido en carga), no un diccionario/hashmap dinámico
  creado en runtime dentro del loop de juego. La actualización de precio por
  oferta/demanda es una función pura: `nuevo_precio = f(precio_base, stock_actual)`.
- `CargoHold` es un array fijo `{ commodity_id, cantidad }[kMaxCargoSlots]` por nave,
  igual que el resto de componentes del proyecto: cero contenedores dinámicos en
  caliente.
- Las misiones activas usan un `Pool<Mission, kMaxActiveMissions>` (mismo patrón que
  proyectiles/entidades en Fase 1A/1B), nunca un `std::vector` que crezca en el loop.
  La generación de una misión nueva (P1C-04) ocurre solo al interactuar con un NPC
  (evento discreto), no cada frame, así que ahí sí es aceptable usar el `Arena`
  temporal de nivel para cualquier buffer de trabajo si hiciera falta.
- La reputación (P1C-07) es un array fijo `float[kNumFactions]` en el estado del
  jugador, no una estructura por-facción dinámica: el número de facciones se conoce
  en tiempo de compilación o de carga de datos, no crece en runtime.
- Los NPCs de P1C-06 en esta fase son entidades ECS estáticas (posición fija +
  componente `Dialogue`/`TradeOffer`) interactuables vía el sistema de P1B-07, sin
  IA de movimiento: la IA de patrulla/combate llega en Fase 2 (`08-FASE-2-...md`).

## Definition of Done específico

- `economy_test` permite completar el ciclo completo: cargar commodity en el
  `CargoHold` en el Mercado A a precio X, volar/transportar a Mercado B (puede ser
  tan simple como dos ubicaciones fijas en el sistema demo de P1D), vender a precio
  Y > X, y ver el margen reflejado en el "dinero" del jugador.
- Aceptar una misión generada desde plantilla, cumplir su condición de completado
  (ej. entregar N unidades de un bien, o visitar una localización), y recibir la
  recompensa, actualizando la reputación de la facción correspondiente.
- Ninguna estructura de economía o misión usa contenedores de tamaño dinámico
  (`std::vector`, `std::map`, etc.) dentro del bucle de juego — solo en carga inicial
  de datos si es imprescindible, y documentado como tal en `STATUS.md`.

---
En paralelo con: `02-FASE-1A-VUELO-Y-NAVES.md`, `03-FASE-1B-A-PIE-Y-FPS.md`,
`05-FASE-1D-UNIVERSO-FIJO-Y-MUNDO.md`.
