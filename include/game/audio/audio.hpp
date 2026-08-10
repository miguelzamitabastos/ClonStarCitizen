#pragma once

#include <flecs.h>

namespace csc::game::audio {

/// Spatial / UI audio triggers driven by ECS events (Fase 1E).
void register_systems(flecs::world& world);

}  // namespace csc::game::audio
