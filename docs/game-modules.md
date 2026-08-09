# Game modules convention (P0-13)

Gameplay lives under `include/game/<module>/` and `src/game/<module>/`.

## Layout

| Module | Purpose |
|--------|---------|
| `flight` | Ship flight / thrusters (Fase 1A) |
| `character` | On-foot / FPS (Fase 1B) |
| `economy` | Markets, commodities, missions (Fase 1C) |
| `world` | Fixed star system / zones (Fase 1D) |
| `ui` | HUD and menus (Fase 1E) |
| `audio` | Audio reactions (Fase 1E) |
| `save` | Persistence (Fase 1F) |

## Rules

1. **One folder per system family** — keep related systems, components, and helpers colocated.
2. **Communicate via ECS** — modules exchange data through Flecs components and events, not direct cross-module function calls into each other's internals.
3. **No OOP entity hierarchies** — entities are IDs plus contiguous component data (DOD/ECS). No deep inheritance trees for ships, characters, or world objects.
4. **Registration stub** — each module exposes `csc::game::<name>::register_systems(flecs::world&)` called once at startup (never from the frame loop).
5. **Memory** — no dynamic allocation in the main update/render loop; pools/arenas at level load only.
