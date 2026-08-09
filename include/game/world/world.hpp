#pragma once

#include <flecs.h>

namespace csc::game::world {

/// Fixed star-system content, zones, and world streaming hooks (Fase 1D).
void register_systems(flecs::world& world);

}  // namespace csc::game::world
