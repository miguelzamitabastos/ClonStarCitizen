#pragma once

#include "engine/core/types.hpp"

#include <flecs.h>

namespace csc::game {

/// Composes character + flight + world (floating origin / streaming) fixed steps.
/// Registered as the engine FixedStepFn — do not replace with flight alone.
void fixed_step(flecs::world& world, f32 dt);

}  // namespace csc::game
