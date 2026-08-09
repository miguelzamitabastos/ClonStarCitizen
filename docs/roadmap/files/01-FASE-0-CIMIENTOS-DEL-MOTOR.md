# Fase 0 — Cimientos del Motor

Depende de: nada (es la fase actual del repo).
Bloquea a: todas las demás. Ninguna tarea de Fase 1 debería empezar mientras haya
tareas de Fase 0 marcadas `P0-*` con prioridad "bloqueante" sin cerrar.

## Estado actual verificado en el repo (no repetir, ya existe)

- Bootstrap Vulkan: instance + debug messenger, physical/logical device, swapchain
  fijo (`kMaxSwapchainImages = 8`), render pass, pipeline de triángulo y pipeline de
  línea para el grid, semáforos/fences por frame (`kMaxFramesInFlight = 2`).
- ECS: wrapper sobre Flecs (`world_register_systems`, `world_progress`), componentes
  `Position`, `Velocity`, `Camera3D`, `Grid3D`, cámara free-look ligada a GLFW.
- `Arena` y `Pool<T, Capacity>` genéricos y probados en memoria.
- Ventana GLFW básica (`window_create`, `window_should_close`, `window_poll_events`).
- Grid de suelo horneado en un buffer fijo, sin heap.
- Push constants para `view_proj` (una mat4, 64 bytes) por frame.

## Lo que falta para que Fase 1 sea viable

Todo lo de abajo es prerrequisito real: sin depth buffer no se puede pintar una nave
sobre un planeta; sin input abstraído no se puede mapear "impulso adelante" a nada;
sin scheduler de física a paso fijo, el modelo de vuelo será inestable según el
framerate; sin instancing no se puede cumplir `.cursorrules` §3 en cuanto haya más
de un objeto en pantalla.

| ID | Tarea | Bloqueante para |
|---|---|---|
| P0-01 | Depth buffer + depth testing en el render pass | Todo lo que dibuje >1 objeto con oclusión |
| P0-02 | Resize de swapchain / minimizado robusto | Jugabilidad básica en ventana redimensionable |
| P0-03 | Sistema de input abstraído (acciones, no teclas crudas) | P1A (vuelo), P1B (locomoción) |
| P0-04 | Scheduler ECS: fases fixed-timestep (física) vs variable (render/interp) | P1A (física de vuelo estable) |
| P0-05 | Sistema de configuración (carga única, sin heap en loop) | Todo lo parametrizable (sensibilidad, FOV, binds) |
| P0-06 | Logging estructurado por categorías | Depuración del agente durante toda la vida del proyecto |
| P0-07 | Pipeline de carga de mallas (glTF) sobre Arena, async | P1A (modelos de nave), P1D (props del mundo) |
| P0-08 | Texturas + descriptor sets/UBO (más allá de push constants) | Cualquier material no monocolor |
| P0-09 | Draw indexed instanced genérico | Asteroides, escombros, flotas NPC, proyectiles |
| P0-10 | Abstracción mínima de "material/pipeline" reutilizable | Evitar que cada sistema futuro hand-rollee un VkPipeline |
| P0-11 | Overlay de depuración (Dear ImGui): FPS, nº entidades vivas, inspector básico | Supervisión eficaz del agente y de Miguel |
| P0-12 | Harness de "escenas demo" seleccionables por flag/config | Verificar cada tarea de Fase 1 de forma aislada |
| P0-13 | Esqueleto de carpetas `src/game/*` y convención de módulos de gameplay | Organización de Fase 1 en paralelo |

## Restricciones de arquitectura específicas de esta fase

- El depth buffer (P0-01) es una imagen más pre-creada junto al swapchain: nada de
  crearla/destruirla fuera de `renderer_create`/`renderer_destroy` o del resize.
- El resize (P0-02) debe reconstruir swapchain + depth + framebuffers sin tocar
  pipelines si el `image_format` no cambia; si cambia, documenta por qué en `STATUS.md`.
- El sistema de input (P0-03) expone **acciones lógicas** (`Thrust`, `Pitch`, `Fire`,
  `Interact`) mapeadas desde teclado/ratón/mando por una tabla de bindings cargada en
  init, no consultas directas a `glfwGetKey` desde sistemas de gameplay: eso acopla
  cada sistema al hardware de input y rompe el día que se añada mando o VR.
- El scheduler (P0-04) usa las fases nativas de Flecs (`OnUpdate`, `PostUpdate`,
  `PreStore`, etc.) o un acumulador de tiempo manual con paso fijo (ej. 1/60) para
  cualquier sistema que integre física; el render interpola entre el estado anterior
  y el actual con el remanente (`alpha`) para que el movimiento no tartamudee a
  framerates variables. Esta decisión es estructural — una vez que P1A dependa de
  ella, cambiarla es carísimo, así que no se improvisa por tarea.
- P0-07/P0-08: la carga async no puede usar `new`/heap sin control en el hilo de
  carga tampoco — usa un allocator dedicado para assets (puede ser un `Arena` por
  nivel, distinto del de gameplay), y sincroniza con el hilo principal solo para la
  subida a GPU (que sí requiere comandos en el command pool principal).
- P0-09: el instancing necesita un buffer de matrices/transform por instancia con
  capacidad fija (ej. `kMaxInstancesPerDrawCall`), rellenado cada frame desde
  componentes ECS vía un solo `memcpy` por draw call, no un buffer por objeto.
- P0-11 (ImGui): añade una dependencia nueva vía `FetchContent`, igual que Flecs/GLM
  en `CMakeLists.txt`. El overlay se compila siempre en `CSC_DEBUG=1` y se puede
  compilar fuera en release con una definición (`CSC_ENABLE_DEBUG_UI`).
- P0-12: el harness de escenas demo es importante para el propio agente: cada tarea
  de Fase 1 en adelante debe poder lanzarse con algo como
  `./clon_star_citizen --scene=flight_test` para verificar visualmente sin montar
  todo el juego. Documenta el nombre de escena que añade cada tarea en su propio
  documento de fase.

## Definition of Done específico de Fase 0

Además del genérico (ver `00-MASTER-ROADMAP.md` §4):

- P0-01 a P0-04 deben poder demostrarse con la escena demo existente (grid + cámara
  free-look) funcionando igual que ahora, sin regresión visual ni de framerate.
- P0-09 se considera hecho cuando la escena demo puede pintar >500 entidades con
  mesh compartida en una sola llamada de draw, medible en el overlay de P0-11.
- Cierre de fase: `STATUS.md` debe listar explícitamente qué escenas demo existen y
  cómo lanzarlas, porque Fase 1 las va a reutilizar constantemente.

---
Siguiente lectura: `02-FASE-1A-VUELO-Y-NAVES.md` (y en paralelo, 03 a 07).
