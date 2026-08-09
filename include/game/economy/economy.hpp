#pragma once

#include <flecs.h>

namespace csc::game::economy {

/// Commodities, markets, and mission economy data/systems (Fase 1C).
void register_systems(flecs::world& world);

}  // namespace csc::game::economy
