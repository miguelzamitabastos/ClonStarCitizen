# ClonStarCitizen — Roadmap Maestro y Manual de Operación del Agente

Este documento es el punto de entrada. Léelo primero, siempre, antes de tocar código.
Está pensado para que un agente de IA autónomo pueda trabajar durante horas/días sin
supervisión constante, tomando sus propias decisiones de implementación dentro de
unos límites claros, y dejando rastro legible para que Miguel pueda revisar el
progreso desde el móvil en segundos.

## 0. Filosofía del proyecto (léelo antes de escribir una sola línea)

**No estamos clonando Star Citizen al 100%.** Star Citizen es un producto de +12 años,
cientos de desarrolladores y +700M$ de financiación. Lo que este proyecto persigue es
un **"spiritual clone" jugable en solitario**: un motor propio en C++/Vulkan con
arquitectura ECS/DOD (ya en marcha), que reproduzca los pilares jugables de Star
Citizen — vuelo espacial de precisión, naves con sistemas internos, a pie/FPS dentro
y fuera de las naves, economía y comercio, misiones, un universo persistente — con un
alcance que una persona + un agente de IA pueden mantener y hacer crecer.

Decisiones de alcance ya tomadas (no las cuestiones sin motivo fundado; si crees que
alguna es un error, anótalo en `STATUS.md` y sigue con lo siguiente, no bloquees el
progreso esperando confirmación salvo que la Fase lo exija explícitamente):

1. **Un solo jugador primero.** Toda la arquitectura de simulación se escribe con
   una separación clara entre "estado de simulación" y "presentación/input", como si
   ya hubiera un servidor autoritativo, aunque de momento cliente y "servidor" corran
   en el mismo proceso. Esto es lo que permite añadir red en la Fase 5 sin reescribir
   medio motor. Ver `11-FASE-5-MULTIJUGADOR-Y-RED.md` para el contrato de arquitectura
   que hay que respetar desde ya (no implementar red todavía, solo no bloquearla).
2. **Universo fijo, hecho a mano, al principio.** Un sistema estelar único con
   planetas/estaciones definidos por datos (no algorítmicamente) hasta la Fase 4.
   La generación procedural se añade después, cuando ya hay pilares jugables sólidos
   sobre los que generar contenido.
3. **La Fase 1 es una "vertical slice" ancha, no profunda.** En vez de perfeccionar
   el vuelo espacial durante meses antes de tocar nada más, la Fase 1 implementa una
   versión mínima pero jugable de *todos* los pilares (vuelo, a pie, economía,
   universo, UI, guardado) para tener cuanto antes un bucle de juego completo de
   principio a fin. La profundidad de cada sistema llega en la Fase 2 en adelante.

## 1. Mapa de documentos

Lee los documentos en este orden. Cada uno declara sus dependencias de fases
anteriores al principio.

| Archivo | Fase | Contenido |
|---|---|---|
| `00-MASTER-ROADMAP.md` | — | Este documento |
| `01-FASE-0-CIMIENTOS-DEL-MOTOR.md` | 0 | Terminar el motor base (input, cámara, scheduler ECS, config, logging, asset pipeline) |
| `02-FASE-1A-VUELO-Y-NAVES.md` | 1 | Modelo de vuelo, física de naves, sistemas internos (energía/escudos/armas) |
| `03-FASE-1B-A-PIE-Y-FPS.md` | 1 | Personaje a pie, EVA/gravedad cero, combate FPS básico, interiores de nave |
| `04-FASE-1C-ECONOMIA-Y-MISIONES.md` | 1 | Comercio, carga, misiones, reputación básica, NPCs simples |
| `05-FASE-1D-UNIVERSO-FIJO-Y-MUNDO.md` | 1 | Sistema estelar fijo, planetas/estaciones, streaming de niveles, coordenadas a escala espacial |
| `06-FASE-1E-UI-HUD-AUDIO.md` | 1 | HUD de vuelo/a pie, menús, sistema de audio 3D |
| `07-FASE-1F-PERSISTENCIA-Y-GUARDADO.md` | 1 | Serialización de estado de juego, guardado/carga |
| `08-FASE-2-PROFUNDIDAD-DE-SISTEMAS.md` | 2 | Multi-tripulación, IA de combate, inventario/crafting, daño por componentes |
| `09-FASE-3-EXPANSION-DE-CONTENIDO.md` | 3 | Más naves, más localizaciones, más misiones, ciudades/estaciones grandes |
| `10-FASE-4-UNIVERSO-PROCEDURAL.md` | 4 | Generación procedural de sistemas/planetas, streaming a escala, LOD |
| `11-FASE-5-MULTIJUGADOR-Y-RED.md` | 5 | Cliente-servidor autoritativo, replicación, migración desde single-player |
| `12-FASE-6-PULIDO-HERRAMIENTAS-RELEASE.md` | 6 | Optimización, herramientas internas, QA, empaquetado |

Dentro de cada documento de fase, las tareas están en orden de dependencia, no de
importancia. No hay que hacerlas todas antes de pasar a otro documento de la misma
fase: la Fase 1 (1A–1F) está pensada para avanzar **en paralelo por sistemas**, cada
uno tocando su propia carpeta bajo `src/game/<sistema>/`, integrándose por eventos
ECS, no por llamadas directas entre sistemas.

## 2. Cómo debe trabajar el agente (bucle operativo)

1. Abre `STATUS.md` (raíz del repo; créalo si no existe con la plantilla de la
   sección 5) y revisa qué tareas están `done`, `in_progress` o `blocked`.
2. Elige la siguiente tarea **no bloqueada** de menor ID dentro de la fase activa.
   Si hay varias tareas de sistemas distintos sin dependencias pendientes, prioriza
   terminar un sistema hasta un estado compilable/jugable antes de saltar a otro:
   es mejor un sistema completo que cinco a medias.
3. Antes de escribir código, relee `.cursorrules` y la sección "Restricciones de
   arquitectura" del documento de fase correspondiente.
4. Implementa la tarea completa (código + si el documento lo pide, una escena o
   modo de prueba mínimo para verificarla manualmente).
5. Compila (`cmake --build build`) con warnings tratados como señal de alarma real,
   no ruido: `-Wall -Wextra -Wpedantic` deben quedar limpios en el código nuevo.
6. Verifica el "Definition of Done" genérico (sección 4) y el específico de la tarea.
7. Haz commit atómico: `git commit -m "[P1A-03] Integrador semi-implícito de física de nave"`
   (prefijo = ID de tarea, ver convención en sección 3).
8. Actualiza `STATUS.md`: marca la tarea como `done`, añade una línea de una frase
   en la bitácora con fecha, y dos únicas cosas más: qué decidiste que no estaba
   100% especificado en el documento (y por qué), y qué haría falta para la siguiente
   tarea relacionada. Esto es lo único que Miguel va a leer desde el móvil la mayoría
   de días — que sea breve y concreto, no un informe.
9. Repite. No esperes confirmación entre tareas salvo que se cumpla una condición
   de parada (sección 6).

## 3. Convención de IDs de tarea

Formato `P<fase><doc>-<número>`, por ejemplo `P1A-03` = Fase 1, documento A (Vuelo y
Naves), tarea 3. `P0-05` = Fase 0, tarea 5. `P2-11` = Fase 2, tarea 11.
Los commits siempre llevan el ID entre corchetes al principio del mensaje.
Las ramas, si se usan, siguen `feature/P1A-03-integrador-fisica`.

## 4. Definition of Done genérico (aplica a toda tarea, además del específico)

- Compila sin warnings nuevos en `-Wall -Wextra -Wpedantic` (o `/W4 /permissive-` en MSVC).
- Cero asignación dinámica dentro del bucle Update/Render (regla `.cursorrules` §2).
  Toda estructura nueva de tamaño variable en runtime usa `Arena` o `Pool` ya
  existentes, o se pre-dimensiona en carga de nivel.
- Componentes ECS son datos planos (POD-like), sin métodos virtuales, sin herencia
  de entidades. La lógica vive en sistemas (funciones/queries de Flecs), no en clases.
- Nuevos vectores/matrices usan exclusivamente `engine/math/glm.hpp`.
- Si la tarea añade geometría repetida (proyectiles, asteroides, naves NPC, escombros),
  usa instancing, no un draw call por objeto (regla `.cursorrules` §3).
- Carga de texturas/mallas nuevas es asíncrona si el documento de fase no dice lo
  contrario explícitamente para una tarea concreta marcada como "carga síncrona
  aceptada temporalmente".
- Código nuevo vive donde el documento de fase indica en su tabla de "Archivos
  esperados"; si hace falta desviarse, se anota el motivo en `STATUS.md`.
- Si la tarea introduce una decisión de diseño de juego no especificada en el
  documento (ej. "¿cuánto daño hace este arma?"), se toma la decisión más simple y
  reversible posible, se documenta en `STATUS.md`, y se sigue: no es motivo de parada.

## 5. Plantilla de `STATUS.md` (bitácora para supervisión desde el móvil)

```markdown
# STATUS

## Fase activa: <ej. Fase 1 — Vertical Slice>

## Progreso por documento
- P1A Vuelo y Naves:      [########..] 8/10 tareas
- P1B A pie y FPS:        [#####.....] 5/10 tareas
- P1C Economía/Misiones:  [##........] 2/9  tareas
...

## Bloqueado (requiere decisión de Miguel)
- P1D-04: necesita definir el nombre/tema del sistema estelar inicial y nº de
  planetas jugables (ver condición de parada #2 en 00-MASTER-ROADMAP.md).

## Bitácora (más reciente arriba, una línea por tarea)
- 2026-08-09 [P1A-03] Integrador semi-implícito de física de nave. Decisión no
  especificada: masa base de la nave inicial = 45.000 kg (valor de referencia
  tipo caza ligero). Siguiente tarea relacionada (P1A-04) necesita el modelo de
  arrastre atmosférico definido aquí.
```

## 6. Condiciones de PARADA explícitas (aquí sí espera a Miguel)

El agente se detiene y deja el bloqueo anotado en `STATUS.md` **solo** si:

1. La tarea requiere una clave/credencial externa (API keys, cuentas de servicios,
   assets de pago) que no está en el repo ni en variables de entorno documentadas.
2. La tarea requiere una decisión de identidad/branding del juego (nombre final,
   arte conceptual definitivo, dirección artística) que no es puramente técnica.
3. Completar la tarea implicaría romper una regla de `.cursorrules` y no hay forma
   de cumplirla sin una excepción explícita (ej. una librería de terceros que
   internamente hace allocs). En ese caso: para, documenta el conflicto exacto y
   dos alternativas, no lo decidas unilateralmente.
4. Se ha llegado al final de todas las tareas no bloqueadas de la fase activa.
   Antes de pasar a la siguiente fase sin más, deja un resumen de cierre de fase en
   `STATUS.md` (qué quedó fuera, qué se probó manualmente, qué no).
5. Un cambio de arquitectura de una tarea obligaría a reescribir código de una tarea
   ya cerrada de una fase anterior de forma no trivial (>1 archivo grande). Para y
   plantea el trade-off en vez de reescribir silenciosamente.

Fuera de estos cinco casos: sigue trabajando sin esperar confirmación.

## 7. Glosario rápido de sistemas objetivo

- **Vuelo (flight model):** física de 6 grados de libertad con asistencia de vuelo
  opcional (tipo Star Citizen "coupled/decoupled"), masa/inercia por nave.
- **Sistemas de nave:** energía (power), escudos, armas, propulsión, refrigeración,
  como componentes ECS independientes que consumen/producen recursos entre sí.
- **A pie:** locomoción con gravedad variable (planeta / estación con gravedad
  artificial / EVA sin gravedad), primera y tercera persona.
- **Economía:** bienes, precios dinámicos por localización, contratos de carga y
  misiones generadas a partir de plantillas + parámetros.
- **Universo:** sistema de coordenadas de doble precisión (o "floating origin") para
  distancias astronómicas sin perder precisión de punto flotante — decisión de
  arquitectura obligatoria desde la Fase 1D, no un "ya lo veremos".

---
Siguiente lectura: `01-FASE-0-CIMIENTOS-DEL-MOTOR.md`.
