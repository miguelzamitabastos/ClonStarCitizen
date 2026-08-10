#include "game/tick.hpp"

#include "game/audio/audio.hpp"
#include "game/character/character.hpp"
#include "game/economy/economy.hpp"
#include "game/flight/flight.hpp"
#include "game/ui/ui.hpp"
#include "game/world/world.hpp"

namespace csc::game {

void fixed_step(flecs::world& world, f32 dt)
{
    // Pause: skip all sim (P1E) — render + audio listener still run from main.
    if (ui::is_simulation_paused(world)) {
        return;
    }

    // Character first so FPS DamageEvents land in the same tick as flight's drain.
    character::fixed_step(world, dt);
    flight::fixed_step(world, dt);
    economy::fixed_step(world, dt);
    // Floating-origin rebase + proximity streaming (P1D) after sim poses settle.
    world::fixed_step(world, dt);
    // Audio cues + positional loops (after poses / damage this tick).
    audio::fixed_step(world, dt);
}

}  // namespace csc::game
