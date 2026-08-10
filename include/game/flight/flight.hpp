#pragma once

#include <flecs.h>

namespace csc::game::flight {

/// Ship flight / thruster / attitude systems (Fase 1A). Systems only — no OOP entity trees.
void register_systems(flecs::world& world);

}  // namespace csc::game::flight
