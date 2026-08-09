#pragma once

#include <flecs.h>

namespace csc::game::ui {

/// HUD / menus / interaction prompts (Fase 1E). Thin presentation over ECS queries.
void register_systems(flecs::world& world);

}  // namespace csc::game::ui
