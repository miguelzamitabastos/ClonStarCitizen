#pragma once

#include "engine/core/types.hpp"

#include <flecs.h>

namespace csc::game {

/// Composes flight + character fixed steps (and shared damage convergence).
/// Registered as the engine FixedStepFn — do not replace with flight alone.
void fixed_step(flecs::world& world, f32 dt);

}  // namespace csc::game
