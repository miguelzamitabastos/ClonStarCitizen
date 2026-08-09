#include "game/audio/audio.hpp"

namespace csc::game::audio {

void register_systems(flecs::world& /*world*/)
{
    // Fase 1E: audio reactions to ECS events — no heap in the hot path.
}

}  // namespace csc::game::audio
