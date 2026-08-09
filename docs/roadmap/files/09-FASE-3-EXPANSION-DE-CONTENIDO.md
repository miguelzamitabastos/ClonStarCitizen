# Fase 3 — Expansión de Contenido

Depende de: Fase 2 completa. A partir de aquí el trabajo deja de ser
mayoritariamente "nuevos sistemas" y pasa a ser "más contenido sobre sistemas ya
sólidos" — el riesgo de esta fase es que el agente empiece a hardcodear contenido en
C++ por velocidad; el objetivo explícito es lo contrario: montar un pipeline de
datos para que añadir una nave, un arma o una misión sea editar un archivo, no tocar
el motor.

## Objetivo de esta fase

Catálogo de naves y equipamiento dirigido por datos, capacidad de poseer/comprar
naves distintas, estaciones/ciudades más grandes y pobladas, y más contenido de
misiones — todo ello sin ampliar el universo fuera del sistema estelar fijo de la
Fase 1D (eso es Fase 4).

## Tareas

| ID | Tarea | Depende de |
|---|---|---|
| P3-01 | Pipeline de datos de nave: stats (masa, empuje, hardpoints, capacidad de carga) en archivos de configuración, no en código | P1A-01, P0-05 |
| P3-02 | Catálogo de 4-6 tipos de nave distintos (caza, carguero, exploración, multipropósito) usando P3-01 | P3-01 |
| P3-03 | Hangar y tienda de naves: comprar/vender/cambiar de nave activa | P3-02, P1C-03 |
| P3-04 | Expansión de estaciones/ciudades: más NPCs, tiendas, dadores de misión por localización | P1D-05, P1C-06 |
| P3-05 | Catálogo de armas/equipamiento dirigido por datos (mismo patrón que P3-01) | P1A-08, P1B-06 |
| P3-06 | Personalización básica de personaje: trajes/armadura con stats (protección, capacidad EVA) | P1B-01, P3-05 |
| P3-07 | Más contenido de misiones: plantillas adicionales + una línea narrativa simple opcional | P1C-04, P2-09 |
| P3-08 | Herramienta interna (CLI/script) para validar y/o generar entradas de catálogo | P3-01, P3-05 |
| P3-09 | Verificación `content_smoke_test`: carga todo el catálogo y valida integridad (IDs únicos, referencias resolubles) | P3-01..08 |

## Restricciones de arquitectura específicas

- **Todo lo nuevo de esta fase es datos, no código.** Una nave nueva, un arma nueva
  o una misión nueva no deben requerir una recompilación con lógica específica: son
  entradas nuevas en un catálogo (JSON/tu formato) leídas por el mismo sistema
  genérico ya construido en Fase 1/2. Si al añadir contenido descubres que hace
  falta una rama de código nueva por tipo de objeto, es señal de que falta un
  parámetro de datos, no de que haga falta un `if`/`switch` por ID.
- Cada entrada de catálogo (nave, arma, misión) tiene un ID de texto estable y único
  (ej. `"ship.fighter.hornet_clone"`), validado en carga: IDs duplicados o
  referencias a IDs inexistentes (ej. una nave que referencia un hardpoint de arma
  que no existe en el catálogo de armas) deben fallar la carga de forma clara, nunca
  silenciosamente con un valor por defecto que enmascare el error.
- P3-08 (herramienta interna) puede ser tan simple como un script que recorra los
  archivos de datos y ejecute las mismas validaciones que P3-09 antes de compilar —
  esto ahorra ciclos al agente: detectar un ID duplicado en segundos en vez de en un
  build completo.
- La compra/venta de naves (P3-03) reutiliza el sistema de transacciones de P1C-03
  tal cual (una nave es, a efectos de la transacción, un ítem con precio), no un
  sistema de comercio paralelo.

## Definition of Done específico

- `content_smoke_test` carga el catálogo completo (naves + armas + misiones) sin
  errores de validación, y reporta un resumen (nº de entradas por tipo) en consola.
- El jugador puede comprar al menos dos naves distintas del catálogo en el hangar de
  una estación y pilotarlas, con diferencias de stats perceptibles (velocidad,
  maniobrabilidad, capacidad de carga).
- Añadir una nave nueva al juego, documentado como prueba en `STATUS.md`, se hace
  únicamente editando el archivo de datos correspondiente — sin tocar `.cpp`/`.hpp`.

---
Siguiente: `10-FASE-4-UNIVERSO-PROCEDURAL.md`.
