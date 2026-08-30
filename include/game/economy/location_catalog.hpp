#pragma once

#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"

#include <flecs.h>

namespace csc::game::economy {

// --- P3-04: data-driven location population --------------------------------
// A location's NPCs — traders, mission givers/turn-ins, travel pads — live in
// assets/data/locations.cfg, not in scene C++. The scene decides WHERE each
// location sits in the world (its origin); the catalog decides WHAT is in it.
// Same section/key parser style as ships.cfg / weapons.cfg.

inline constexpr u32 kMaxLocationDefs    = 12;
inline constexpr u32 kMaxLocationNpcs    = 16;
inline constexpr u32 kLocationIdBytes    = 40;   ///< "loc.<name>"
inline constexpr u32 kLocationLabelBytes = 32;   ///< == character::InteractablePrompt.label

inline constexpr const char* kLocationCatalogPath = "assets/data/locations.cfg";

/// Standard on-foot arrival offset from a destination location's origin, used
/// when a TravelPad resolves its destination (matches the Fase 1C hand-placed
/// pads: `origin + (0, 0.9, 1.5)`).
inline constexpr glm::vec3 kLocationArrivalOffset{0.f, 0.9f, 1.5f};

enum class LocationNpcKind : u8 {
    Trader       = 0,  ///< economy::spawn_trader_npc (one commodity, buy/sell)
    MissionGiver = 1,  ///< economy::spawn_mission_npc, is_giver
    TurnIn       = 2,  ///< economy::spawn_mission_npc, is_turn_in
    TravelPad    = 3,  ///< economy::spawn_travel_pad (dest = another location id)
};

struct LocationNpc {
    LocationNpcKind kind = LocationNpcKind::Trader;
    glm::vec3       offset{0.f};  ///< relative to the location origin
    char            label[kLocationLabelBytes]{};

    // Trader
    u32  market_id    = 0;
    u32  commodity_id = 0;
    bool allow_buy    = true;
    bool allow_sell   = true;

    // MissionGiver / TurnIn
    u32  template_id       = 0;
    u32  mission_market_id = 0;

    // TravelPad — text id of the destination location (resolved via placements).
    char dest_location[kLocationIdBytes]{};
};

struct LocationDef {
    char        id[kLocationIdBytes]{};
    char        name[kLocationLabelBytes]{};
    u32         location_id  = 0;   ///< integer id bridging markets / travel / visit missions
    bool        gravity_deck = false;  ///< populate_locations also spawns a gravity zone + deck
    LocationNpc npcs[kMaxLocationNpcs]{};
    u32         npc_count = 0;
};

struct LocationCatalog {
    LocationDef items[kMaxLocationDefs]{};
    u32         count = 0;
};

/// Where a catalog location sits in the current scene. The scene owns geography;
/// travel-pad NPCs resolve their destination by matching `dest_location` here.
struct LocationPlacement {
    const char* location_id = nullptr;
    glm::vec3   world_origin{0.f};
};

/// Parse `path` into `out`. Returns false — logging each problem — on: file
/// missing, zero entries, a duplicate / absent location id.
[[nodiscard]] bool load_location_catalog_file(LocationCatalog& out, const char* path);

/// Load locations.cfg into a LocationCatalog singleton on `world`. Loud on failure.
bool load_location_catalog(flecs::world& world);

[[nodiscard]] const LocationDef* find_location_def(const LocationCatalog& cat, const char* id);
[[nodiscard]] const LocationDef* find_location_def(flecs::world& world, const char* id);

/// Spawn every NPC of each placed location from the LocationCatalog singleton
/// (plus a gravity zone + deck marker when `gravity_deck`). Travel-pad
/// destinations resolve against `placements`; a placement whose id is not in the
/// catalog, or a travel dest not among the placements, is skipped with a warning.
void populate_locations(
    flecs::world& world, const LocationPlacement* placements, u32 count);

}  // namespace csc::game::economy
