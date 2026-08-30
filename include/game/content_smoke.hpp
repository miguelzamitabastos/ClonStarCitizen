#pragma once

#include <flecs.h>

namespace csc::game::content {

// --- P3-09: content_smoke_test -------------------------------------------
// Load the full data catalog into the engine and check its integrity in one
// pass — unique ids, every cross-catalog reference resolvable, engine-required
// ids present, no cycles in mission chains. Complements tools/validate_catalogs.py
// (P3-08): same checks, but through the real loaders/components, so a mismatch
// between the file format and the engine's parser is also caught.

/// Run the aggregate content check. Assumes every catalog is already loaded
/// (scene_setup_by_name loads weapon/ship/suit/location; the caller must also
/// have run economy::load_economy_data for the commodity/market/mission tables).
/// Logs a per-type count summary and `CONTENT_SMOKE: PASS|FAIL`. Returns true
/// iff every catalog loaded and every reference resolves.
[[nodiscard]] bool run_content_smoke_test(flecs::world& world);

}  // namespace csc::game::content
