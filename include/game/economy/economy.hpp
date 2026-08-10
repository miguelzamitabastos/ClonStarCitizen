#pragma once

#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"
#include "engine/memory/pool.hpp"

#include <flecs.h>

namespace csc::game::economy {

// --- Caps (compile-time; tables filled at load) -------------------------------
inline constexpr u32 kMaxCommodities      = 16;
inline constexpr u32 kMaxMarkets          = 8;
inline constexpr u32 kMaxCargoSlots       = 8;
inline constexpr u32 kMaxMissionTemplates = 16;
inline constexpr u32 kMaxActiveMissions   = 8;
inline constexpr u32 kNumFactions         = 4;
inline constexpr u32 kMaxNameBytes        = 32;

inline constexpr f32 kDefaultCargoVolume = 40.f;
inline constexpr f32 kDefaultCargoMass   = 100.f;
inline constexpr i32 kStartingCredits    = 500;

inline constexpr const char* kCommoditiesPath      = "assets/data/commodities.cfg";
inline constexpr const char* kMarketsPath          = "assets/data/markets.cfg";
inline constexpr const char* kMissionTemplatesPath = "assets/data/mission_templates.cfg";

// --- Config POD (index-based — save-friendly, no raw pointers) ---------------

struct Commodity {
    u32  id = 0;
    char name[kMaxNameBytes]{};
    f32  base_price = 1.f;
    f32  volume     = 1.f;
    f32  mass       = 1.f;
};

struct Market {
    u32  id          = 0;
    char name[kMaxNameBytes]{};
    u32  location_id = 0;
    f32  ideal_stock = 50.f;
    i32  stock[kMaxCommodities]{};
    f32  price_mod[kMaxCommodities]{};
};

struct CommodityTable {
    Commodity items[kMaxCommodities]{};
    u32       count = 0;
};

struct MarketTable {
    Market items[kMaxMarkets]{};
    u32    count = 0;
};

enum class MissionType : u8 {
    Delivery = 0,
    Visit    = 1,
};

struct MissionTemplate {
    u32         id            = 0;
    MissionType type          = MissionType::Delivery;
    char        name[kMaxNameBytes]{};
    u32         commodity_id  = 0;
    u32         qty_min       = 1;
    u32         qty_max       = 1;
    i32         reward_min    = 0;
    i32         reward_max    = 0;
    u32         faction_id    = 0;
    f32         rep_delta     = 0.f;
    u32         from_market   = 0;
    u32         to_market     = 0;
};

struct MissionTemplateTable {
    MissionTemplate items[kMaxMissionTemplates]{};
    u32             count = 0;
};

struct MissionActive {
    u32         template_id    = 0;
    MissionType type           = MissionType::Delivery;
    u32         commodity_id   = 0;
    u32         qty_required   = 0;
    u32         qty_delivered  = 0;
    i32         reward_credits = 0;
    u32         faction_id     = 0;
    f32         rep_delta      = 0.f;
    u32         from_market    = 0;
    u32         to_market      = 0;
    u32         location_id    = 0;
    bool        completed      = false;
};

struct MissionActivePool {
    memory::Pool<MissionActive, kMaxActiveMissions> pool{};
    u32                                             rng_state = 1;
};

struct CargoSlot {
    u32 commodity_id = 0;
    u32 qty          = 0;
};

struct CargoHold {
    CargoSlot slots[kMaxCargoSlots]{};
    u32       slot_count      = 0;
    f32       capacity_volume = kDefaultCargoVolume;
    f32       capacity_mass   = kDefaultCargoMass;
};

struct PlayerWallet {
    i32 credits = kStartingCredits;
};

struct FactionReputation {
    f32 values[kNumFactions]{};
};

struct TradeOffer {
    u32  market_id    = 0;
    u32  commodity_id = 0;
    bool allow_buy    = true;
    bool allow_sell   = true;
};

struct Dialogue {
    u32  template_id      = 0;
    bool is_mission_giver = true;
    bool is_turn_in       = false;
    u32  market_id        = 0;
};

struct TravelPad {
    glm::vec3 destination{0.f};
    u32       location_id = 0;
};

struct EconomyDebugSnapshot {
    bool has_data        = false;
    i32  credits         = 0;
    u32  cargo_units     = 0;
    u32  active_missions = 0;
    char cargo_summary[64]{};
    char mission_summary[48]{};
};

// --- Pure helpers ------------------------------------------------------------

[[nodiscard]] f32 market_unit_price(
    const Commodity& commodity, const Market& market, u32 commodity_id);

[[nodiscard]] bool cargo_used(
    const CargoHold& hold, const CommodityTable& table, f32& out_volume, f32& out_mass);

[[nodiscard]] bool cargo_can_add(
    const CargoHold& hold, const CommodityTable& table, u32 commodity_id, u32 qty);

[[nodiscard]] bool cargo_add(
    CargoHold& hold, const CommodityTable& table, u32 commodity_id, u32 qty);

[[nodiscard]] bool cargo_remove(CargoHold& hold, u32 commodity_id, u32 qty);

[[nodiscard]] u32 cargo_count(const CargoHold& hold, u32 commodity_id);

[[nodiscard]] bool try_buy(
    PlayerWallet&         wallet,
    CargoHold&            hold,
    Market&               market,
    const CommodityTable& table,
    u32                   commodity_id,
    u32                   qty,
    i32&                  out_paid);

[[nodiscard]] bool try_sell(
    PlayerWallet&         wallet,
    CargoHold&            hold,
    Market&               market,
    const CommodityTable& table,
    u32                   commodity_id,
    u32                   qty,
    i32&                  out_earned);

[[nodiscard]] bool mission_generate_from_template(
    const MissionTemplate& tmpl, MissionActive& out, u32& rng_state);

[[nodiscard]] bool mission_try_accept(
    MissionActivePool& pool, const MissionTemplateTable& templates, u32 template_id);

[[nodiscard]] bool mission_try_complete_at_market(
    MissionActivePool&   pool,
    CargoHold&           hold,
    PlayerWallet&        wallet,
    FactionReputation&   rep,
    u32                  market_id);

// --- Systems / scene API -----------------------------------------------------

void register_systems(flecs::world& world);

[[nodiscard]] bool load_economy_data(flecs::world& world);

void fixed_step(flecs::world& world, f32 dt);

[[nodiscard]] bool handle_interact(
    flecs::world& world, flecs::entity_t actor, flecs::entity_t target);

void attach_cargo_hold_if_missing(flecs::entity ship);

[[nodiscard]] flecs::entity spawn_trader_npc(
    flecs::world&    world,
    const glm::vec3& position,
    u32              market_id,
    u32              commodity_id,
    bool             allow_buy,
    bool             allow_sell,
    const char*      prompt);

[[nodiscard]] flecs::entity spawn_mission_npc(
    flecs::world&    world,
    const glm::vec3& position,
    u32              template_id,
    bool             is_giver,
    bool             is_turn_in,
    u32              market_id,
    const char*      prompt);

[[nodiscard]] flecs::entity spawn_travel_pad(
    flecs::world&    world,
    const glm::vec3& position,
    const glm::vec3& destination,
    u32              location_id,
    const char*      prompt);

[[nodiscard]] bool setup_economy_test_scene(flecs::world& world, f32 aspect);

void fill_economy_telemetry(flecs::world& world, EconomyDebugSnapshot& out);

}  // namespace csc::game::economy
