#pragma once

#include <flecs.h>

namespace csc::game::save {

/// Persistence / save-load serialization of ECS world snapshots (Fase 1F).
void register_systems(flecs::world& world);

}  // namespace csc::game::save
