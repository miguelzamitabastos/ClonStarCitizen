#pragma once

#include <flecs.h>

namespace csc::game::character {

/// On-foot / FPS character locomotion and interaction (Fase 1B).
void register_systems(flecs::world& world);

}  // namespace csc::game::character
