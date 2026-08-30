#include "game/economy/economy.hpp"

#include "engine/ecs/world.hpp"
#include "engine/log/log.hpp"
#include "game/ai/ai.hpp"
#include "game/character/character.hpp"
#include "game/economy/location_catalog.hpp"
#include "game/flight/flight.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace csc::game::economy {
namespace {

void copy_name(char* dest, std::size_t dest_bytes, const char* src)
{
    if (dest == nullptr || dest_bytes == 0) {
        return;
    }
    if (src == nullptr) {
        dest[0] = '\0';
        return;
    }
    std::snprintf(dest, dest_bytes, "%s", src);
}

char* trim_inplace(char* s)
{
    while (*s != '\0' && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) {
        ++s;
    }
    if (*s == '\0') {
        return s;
    }
    char* end = s + std::strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        --end;
    }
    *end = '\0';
    return s;
}

[[nodiscard]] f32 clampf(f32 v, f32 lo, f32 hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

[[nodiscard]] u32 lcg_next(u32& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

[[nodiscard]] u32 roll_u32(u32& state, u32 lo, u32 hi)
{
    if (hi <= lo) {
        return lo;
    }
    const u32 span = hi - lo + 1u;
    return lo + (lcg_next(state) % span);
}

[[nodiscard]] i32 roll_i32(u32& state, i32 lo, i32 hi)
{
    if (hi <= lo) {
        return lo;
    }
    const u32 span = static_cast<u32>(hi - lo + 1);
    return lo + static_cast<i32>(lcg_next(state) % span);
}

[[nodiscard]] MissionType parse_mission_type(const char* text)
{
    if (text == nullptr) {
        return MissionType::Delivery;
    }
    if (std::strcmp(text, "visit") == 0 || std::strcmp(text, "Visit") == 0) {
        return MissionType::Visit;
    }
    // P2-10: combat/escort reuse the shared AI (P2-06 on-foot NpcCombatant +
    // P2-11 AiThreatTarget) — no parallel AI, just a new MissionType.
    if (std::strcmp(text, "combat") == 0 || std::strcmp(text, "Combat") == 0) {
        return MissionType::Combat;
    }
    if (std::strcmp(text, "escort") == 0 || std::strcmp(text, "Escort") == 0) {
        return MissionType::Escort;
    }
    return MissionType::Delivery;
}

enum class LoadSection : u8 {
    None = 0,
    Commodity,
    Market,
    Template,
};

bool apply_commodity_kv(Commodity& c, const char* key, const char* value)
{
    if (std::strcmp(key, "id") == 0) {
        unsigned v = 0;
        if (std::sscanf(value, "%u", &v) == 1) {
            c.id = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "name") == 0) {
        copy_name(c.name, sizeof(c.name), value);
        return true;
    }
    if (std::strcmp(key, "base_price") == 0) {
        float v = 0.f;
        if (std::sscanf(value, "%f", &v) == 1 && v >= 0.f) {
            c.base_price = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "volume") == 0) {
        float v = 0.f;
        if (std::sscanf(value, "%f", &v) == 1 && v >= 0.f) {
            c.volume = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "mass") == 0) {
        float v = 0.f;
        if (std::sscanf(value, "%f", &v) == 1 && v >= 0.f) {
            c.mass = v;
            return true;
        }
        return false;
    }
    return false;
}

bool apply_market_kv(Market& m, const char* key, const char* value)
{
    if (std::strcmp(key, "id") == 0) {
        unsigned v = 0;
        if (std::sscanf(value, "%u", &v) == 1) {
            m.id = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "name") == 0) {
        copy_name(m.name, sizeof(m.name), value);
        return true;
    }
    if (std::strcmp(key, "location_id") == 0) {
        unsigned v = 0;
        if (std::sscanf(value, "%u", &v) == 1) {
            m.location_id = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "ideal_stock") == 0) {
        float v = 0.f;
        if (std::sscanf(value, "%f", &v) == 1 && v > 0.f) {
            m.ideal_stock = v;
            return true;
        }
        return false;
    }

    unsigned idx = 0;
    if (std::sscanf(key, "stock_%u", &idx) == 1 && idx < kMaxCommodities) {
        int v = 0;
        if (std::sscanf(value, "%d", &v) == 1 && v >= 0) {
            m.stock[idx] = v;
            return true;
        }
        return false;
    }
    if (std::sscanf(key, "price_mod_%u", &idx) == 1 && idx < kMaxCommodities) {
        float v = 0.f;
        if (std::sscanf(value, "%f", &v) == 1 && v > 0.f) {
            m.price_mod[idx] = v;
            return true;
        }
        return false;
    }
    // P2-08: units/second this market produces (positive) or consumes
    // (negative) in the background — sign allowed, unlike stock_N/price_mod_N.
    if (std::sscanf(key, "rate_%u", &idx) == 1 && idx < kMaxCommodities) {
        float v = 0.f;
        if (std::sscanf(value, "%f", &v) == 1) {
            m.production_rate[idx] = v;
            return true;
        }
        return false;
    }
    return false;
}

bool apply_template_kv(MissionTemplate& t, const char* key, const char* value)
{
    if (std::strcmp(key, "id") == 0) {
        unsigned v = 0;
        if (std::sscanf(value, "%u", &v) == 1) {
            t.id = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "type") == 0) {
        t.type = parse_mission_type(value);
        return true;
    }
    if (std::strcmp(key, "name") == 0) {
        copy_name(t.name, sizeof(t.name), value);
        return true;
    }
    if (std::strcmp(key, "title") == 0) {  // P3-07: optional flavour one-liner
        copy_name(t.title, sizeof(t.title), value);
        return true;
    }
    if (std::strcmp(key, "commodity_id") == 0) {
        unsigned v = 0;
        if (std::sscanf(value, "%u", &v) == 1) {
            t.commodity_id = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "qty_min") == 0) {
        unsigned v = 0;
        if (std::sscanf(value, "%u", &v) == 1) {
            t.qty_min = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "qty_max") == 0) {
        unsigned v = 0;
        if (std::sscanf(value, "%u", &v) == 1) {
            t.qty_max = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "reward_min") == 0) {
        int v = 0;
        if (std::sscanf(value, "%d", &v) == 1) {
            t.reward_min = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "reward_max") == 0) {
        int v = 0;
        if (std::sscanf(value, "%d", &v) == 1) {
            t.reward_max = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "faction_id") == 0) {
        unsigned v = 0;
        if (std::sscanf(value, "%u", &v) == 1) {
            t.faction_id = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "rep_delta") == 0) {
        float v = 0.f;
        if (std::sscanf(value, "%f", &v) == 1) {
            t.rep_delta = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "from_market") == 0) {
        unsigned v = 0;
        if (std::sscanf(value, "%u", &v) == 1) {
            t.from_market = v;
            return true;
        }
        return false;
    }
    if (std::strcmp(key, "to_market") == 0) {
        unsigned v = 0;
        if (std::sscanf(value, "%u", &v) == 1) {
            t.to_market = v;
            return true;
        }
        return false;
    }
    // P2-09: chained missions — omit for "always offerable" (default sentinel).
    if (std::strcmp(key, "requires_completed_id") == 0) {
        unsigned v = 0;
        if (std::sscanf(value, "%u", &v) == 1) {
            t.requires_completed_id = v;
            return true;
        }
        return false;
    }
    // P2-09: simple branching — templates sharing a nonzero group are
    // mutually exclusive (accepting one locks out the rest permanently).
    if (std::strcmp(key, "branch_group") == 0) {
        unsigned v = 0;
        if (std::sscanf(value, "%u", &v) == 1) {
            t.branch_group = v;
            return true;
        }
        return false;
    }
    // P2-10: hostile faction spawned for Combat/Escort (unused otherwise).
    if (std::strcmp(key, "target_faction_id") == 0) {
        unsigned v = 0;
        if (std::sscanf(value, "%u", &v) == 1) {
            t.target_faction_id = v;
            return true;
        }
        return false;
    }
    return false;
}

void init_market_defaults(Market& m)
{
    m.ideal_stock = 50.f;
    for (u32 i = 0; i < kMaxCommodities; ++i) {
        m.stock[i]            = 0;
        m.price_mod[i]        = 1.f;
        m.production_rate[i]  = 0.f;
    }
}

[[nodiscard]] bool load_commodities_file(CommodityTable& table, const char* path)
{
    table = CommodityTable{};
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        log::log_warn(log::LogCategory::Config, "commodities file missing: %s", path);
        return false;
    }

    LoadSection section = LoadSection::None;
    Commodity   current{};
    bool        have = false;

    auto flush = [&]() {
        if (!have) {
            return;
        }
        if (table.count >= kMaxCommodities) {
            log::log_warn(log::LogCategory::Config, "CommodityTable full; skipping id=%u", current.id);
            have = false;
            return;
        }
        table.items[table.count++] = current;
        have                       = false;
    };

    char line[512];
    int  line_no = 0;
    while (std::fgets(line, static_cast<int>(sizeof(line)), file) != nullptr) {
        ++line_no;
        char* trimmed = trim_inplace(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        if (std::strcmp(trimmed, "[[commodity]]") == 0) {
            flush();
            current = Commodity{};
            have    = true;
            section = LoadSection::Commodity;
            continue;
        }
        if (section != LoadSection::Commodity) {
            continue;
        }
        char* eq = std::strchr(trimmed, '=');
        if (eq == nullptr) {
            log::log_warn(log::LogCategory::Config, "%s:%d: missing '='", path, line_no);
            continue;
        }
        *eq         = '\0';
        char* key   = trim_inplace(trimmed);
        char* value = trim_inplace(eq + 1);
        if (!apply_commodity_kv(current, key, value)) {
            log::log_warn(log::LogCategory::Config, "%s:%d: bad key '%s'", path, line_no, key);
        }
    }
    flush();
    std::fclose(file);
    log::log_info(
        log::LogCategory::Config, "Loaded %u commodities from %s", table.count, path);
    return table.count > 0;
}

[[nodiscard]] bool load_markets_file(MarketTable& table, const char* path)
{
    table = MarketTable{};
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        log::log_warn(log::LogCategory::Config, "markets file missing: %s", path);
        return false;
    }

    LoadSection section = LoadSection::None;
    Market      current{};
    bool        have = false;

    auto flush = [&]() {
        if (!have) {
            return;
        }
        if (table.count >= kMaxMarkets) {
            log::log_warn(log::LogCategory::Config, "MarketTable full; skipping id=%u", current.id);
            have = false;
            return;
        }
        table.items[table.count++] = current;
        have                       = false;
    };

    char line[512];
    int  line_no = 0;
    while (std::fgets(line, static_cast<int>(sizeof(line)), file) != nullptr) {
        ++line_no;
        char* trimmed = trim_inplace(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        if (std::strcmp(trimmed, "[[market]]") == 0) {
            flush();
            current = Market{};
            init_market_defaults(current);
            have    = true;
            section = LoadSection::Market;
            continue;
        }
        if (section != LoadSection::Market) {
            continue;
        }
        char* eq = std::strchr(trimmed, '=');
        if (eq == nullptr) {
            log::log_warn(log::LogCategory::Config, "%s:%d: missing '='", path, line_no);
            continue;
        }
        *eq         = '\0';
        char* key   = trim_inplace(trimmed);
        char* value = trim_inplace(eq + 1);
        if (!apply_market_kv(current, key, value)) {
            log::log_warn(log::LogCategory::Config, "%s:%d: bad key '%s'", path, line_no, key);
        }
    }
    flush();
    std::fclose(file);
    log::log_info(log::LogCategory::Config, "Loaded %u markets from %s", table.count, path);
    return table.count > 0;
}

[[nodiscard]] bool load_templates_file(MissionTemplateTable& table, const char* path)
{
    table = MissionTemplateTable{};
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        log::log_warn(log::LogCategory::Config, "mission templates missing: %s", path);
        return false;
    }

    LoadSection     section = LoadSection::None;
    MissionTemplate current{};
    bool            have = false;

    auto flush = [&]() {
        if (!have) {
            return;
        }
        if (table.count >= kMaxMissionTemplates) {
            log::log_warn(
                log::LogCategory::Config, "MissionTemplateTable full; skipping id=%u", current.id);
            have = false;
            return;
        }
        table.items[table.count++] = current;
        have                       = false;
    };

    char line[512];
    int  line_no = 0;
    while (std::fgets(line, static_cast<int>(sizeof(line)), file) != nullptr) {
        ++line_no;
        char* trimmed = trim_inplace(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        if (std::strcmp(trimmed, "[[template]]") == 0) {
            flush();
            current = MissionTemplate{};
            have    = true;
            section = LoadSection::Template;
            continue;
        }
        if (section != LoadSection::Template) {
            continue;
        }
        char* eq = std::strchr(trimmed, '=');
        if (eq == nullptr) {
            log::log_warn(log::LogCategory::Config, "%s:%d: missing '='", path, line_no);
            continue;
        }
        *eq         = '\0';
        char* key   = trim_inplace(trimmed);
        char* value = trim_inplace(eq + 1);
        if (!apply_template_kv(current, key, value)) {
            log::log_warn(log::LogCategory::Config, "%s:%d: bad key '%s'", path, line_no, key);
        }
    }
    flush();
    std::fclose(file);
    log::log_info(
        log::LogCategory::Config, "Loaded %u mission templates from %s", table.count, path);
    return table.count > 0;
}

[[nodiscard]] Market* find_market_mut(MarketTable& table, u32 market_id)
{
    for (u32 i = 0; i < table.count; ++i) {
        if (table.items[i].id == market_id) {
            return &table.items[i];
        }
    }
    return nullptr;
}

[[nodiscard]] const Commodity* find_commodity(const CommodityTable& table, u32 id)
{
    for (u32 i = 0; i < table.count; ++i) {
        if (table.items[i].id == id) {
            return &table.items[i];
        }
    }
    return nullptr;
}

[[nodiscard]] const MissionTemplate* find_template(const MissionTemplateTable& table, u32 id)
{
    for (u32 i = 0; i < table.count; ++i) {
        if (table.items[i].id == id) {
            return &table.items[i];
        }
    }
    return nullptr;
}

[[nodiscard]] CargoHold* find_player_cargo(flecs::world& world)
{
    CargoHold* found = nullptr;
    world.each([&](flecs::entity e, CargoHold& hold) {
        if (found != nullptr) {
            return;
        }
        if (e.has<flight::PlayerShip>()) {
            found = &hold;
        }
    });
    if (found != nullptr) {
        return found;
    }
    world.each([&](flecs::entity /*e*/, CargoHold& hold) {
        if (found == nullptr) {
            found = &hold;
        }
    });
    return found;
}

void teleport_actor(flecs::entity actor, const glm::vec3& dest)
{
    if (actor.has<character::LocalToShip>()) {
        actor.remove<character::LocalToShip>();
    }
    const ecs::Position pos{dest.x, dest.y, dest.z};
    actor.set<ecs::Position>(pos);
    actor.set<ecs::PreviousPosition>({pos.x, pos.y, pos.z});
    if (character::CharacterController* cc = actor.try_get_mut<character::CharacterController>()) {
        cc->velocity = {};
        cc->grounded = true;
    }
}

void mark_visit_missions(MissionActivePool& pool, u32 location_id)
{
    for (std::size_t i = 0; i < kMaxActiveMissions; ++i) {
        if (!pool.pool.is_active(i)) {
            continue;
        }
        MissionActive& m = pool.pool.slots[i];
        if (m.type == MissionType::Visit && m.location_id == location_id) {
            m.qty_delivered = 1;
        }
    }
}

// --- P2-08: dynamic economy background simulation -----------------------------

/// Applies one PriceEvent directly to Market::stock (clamped). No-op if the
/// market id or commodity index is invalid — events can arrive after a scene
/// change, never trust the ids blindly.
void apply_price_event(MarketTable& markets, const PriceEvent& ev)
{
    Market* m = find_market_mut(markets, ev.market_id);
    if (m == nullptr || ev.commodity_id >= kMaxCommodities) {
        return;
    }
    const i32 next        = m->stock[ev.commodity_id] + ev.stock_delta;
    m->stock[ev.commodity_id] = next < 0 ? 0 : (next > kMaxMarketStock ? kMaxMarketStock : next);
}

/// Background production/consumption: nudges every market's stock toward
/// (production_rate > 0) or away from (< 0) its current level, independent of
/// player trades. Called once per economic tick, not per frame.
void apply_production_drift(
    MarketTable& markets, const CommodityTable& commodities, f32 tick_seconds)
{
    for (u32 mi = 0; mi < markets.count; ++mi) {
        Market& m = markets.items[mi];
        for (u32 ci = 0; ci < commodities.count && ci < kMaxCommodities; ++ci) {
            if (m.production_rate[ci] == 0.f) {
                continue;
            }
            const f32 next =
                static_cast<f32>(m.stock[ci]) + m.production_rate[ci] * tick_seconds;
            m.stock[ci] = static_cast<i32>(clampf(next, 0.f, static_cast<f32>(kMaxMarketStock)));
        }
    }
}

/// P2-08: with kRandomEventChance probability, rolls a scarcity (shortage) or
/// glut (surplus) at a random market/commodity — the "eventos (escasez, ...)"
/// half of the task, distinct from mission-driven events (which are pushed by
/// handle_interact instead). Deterministic LCG, same family as combat/mission
/// RNGs but its own state (EconomyClock::rng_state).
void maybe_roll_random_event(
    MarketTable& markets, const CommodityTable& commodities, u32& rng_state)
{
    if (markets.count == 0 || commodities.count == 0) {
        return;
    }
    const f32 roll = static_cast<f32>(lcg_next(rng_state) % 10000u) / 10000.f;
    if (roll >= kRandomEventChance) {
        return;
    }

    const u32 market_idx    = roll_u32(rng_state, 0, markets.count - 1);
    const u32 commodity_idx = roll_u32(rng_state, 0, commodities.count - 1);
    const bool scarcity     = (lcg_next(rng_state) & 1u) == 0u;
    const i32 delta         = scarcity
        ? roll_i32(rng_state, kScarcityDeltaMin, kScarcityDeltaMax)
        : roll_i32(rng_state, kGlutDeltaMin, kGlutDeltaMax);

    const Market&    m = markets.items[market_idx];
    const Commodity& c = commodities.items[commodity_idx];
    apply_price_event(markets, PriceEvent{m.id, c.id, delta});

    log::log_info(
        log::LogCategory::Game,
        "Economy event: %s of %s at market '%s' (stock delta=%d)",
        scarcity ? "scarcity" : "glut",
        c.name,
        m.name,
        delta);
}

/// One background economic step: drain pending PriceEvents (pushed by other
/// systems, e.g. mission completion), apply production/consumption drift,
/// then maybe roll a fresh random event. Called from fixed_step, gated by
/// EconomyClock so it runs every kEconomyTickSeconds — NOT every frame.
void run_economic_tick(flecs::world& world)
{
    MarketTable* markets = world.try_get_mut<MarketTable>();
    const CommodityTable* commodities = world.try_get<CommodityTable>();
    if (markets == nullptr || commodities == nullptr) {
        return;
    }

    if (PriceEventQueue* queue = world.try_get_mut<PriceEventQueue>()) {
        PriceEvent ev{};
        while (queue->try_pop(ev)) {
            apply_price_event(*markets, ev);
        }
    }

    apply_production_drift(*markets, *commodities, kEconomyTickSeconds);

    if (EconomyClock* clock = world.try_get_mut<EconomyClock>()) {
        maybe_roll_random_event(*markets, *commodities, clock->rng_state);
    }
}

// --- P2-10: combat/escort mission encounters (reuses P2-06/P2-11 AI) --------

/// Spawns the hostiles (and, for Escort, the protected NPC) for a freshly
/// accepted Combat/Escort mission, all tagged with MissionLink{slot} so
/// apply_damage_events (flight.cpp) can report kills/failure back to it.
/// Reuses character::spawn_npc_combatant (P2-06) and ai::AiThreatTarget
/// (P2-11) verbatim — no new AI, per the roadmap's explicit constraint.
void spawn_mission_encounter(
    flecs::world& world, const MissionActive& m, u32 slot_index, const glm::vec3& near_pos)
{
    const character::PatrolRoute idle_route{}; // count=0: holds position until it detects a target
    for (u32 i = 0; i < m.qty_required && i < kMaxMissionHostiles; ++i) {
        // Spread hostiles around near_pos instead of stacking them on one spot.
        const f32 angle = static_cast<f32>(i) * (6.2831853f / static_cast<f32>(kMaxMissionHostiles));
        const glm::vec3 offset{std::cos(angle) * 4.f, 0.f, std::sin(angle) * 4.f};
        flecs::entity hostile = character::spawn_npc_combatant(
            world, near_pos + offset, m.target_faction_id, idle_route, "MissionHostile");
        hostile.set<MissionLink>({slot_index, false});
    }

    if (m.type == MissionType::Escort) {
        flecs::entity escort =
            character::spawn_health_target(world, near_pos + glm::vec3{0.f, 0.f, -3.f}, 1.f);
        escort.set<MissionLink>({slot_index, true});
        // AiThreatTarget is an empty tag — flecs rejects .set() on zero-size
        // types ("operation invalid for empty type"); .add() is the correct
        // call for tags (same family of gotcha as the each()-by-ref one
        // fixed above in collect_target_candidates).
        escort.add<ai::AiThreatTarget>();
        escort.set<ai::FactionMember>({m.faction_id});
    }

    log::log_info(
        log::LogCategory::Game,
        "Mission encounter spawned (slot=%u type=%s hostiles=%u%s)",
        slot_index,
        m.type == MissionType::Escort ? "Escort" : "Combat",
        m.qty_required,
        m.type == MissionType::Escort ? " + escort target" : "");
}

}  // namespace

f32 market_unit_price(const Commodity& commodity, const Market& market, u32 commodity_id)
{
    if (commodity_id >= kMaxCommodities) {
        return commodity.base_price;
    }
    const f32 mod = (market.price_mod[commodity_id] > 0.f) ? market.price_mod[commodity_id] : 1.f;
    const f32 stock = static_cast<f32>(market.stock[commodity_id]);
    const f32 ideal = (market.ideal_stock > 0.f) ? market.ideal_stock : 50.f;
    const f32 supply_factor = clampf(1.5f - (stock / ideal), 0.5f, 2.0f);
    const f32 price = commodity.base_price * mod * supply_factor;
    return price < 1.f ? 1.f : price;
}

bool cargo_used(
    const CargoHold& hold, const CommodityTable& table, f32& out_volume, f32& out_mass)
{
    out_volume = 0.f;
    out_mass   = 0.f;
    for (u32 i = 0; i < kMaxCargoSlots; ++i) {
        const CargoSlot& s = hold.slots[i];
        if (s.qty == 0) {
            continue;
        }
        const Commodity* c = find_commodity(table, s.commodity_id);
        if (c == nullptr) {
            continue;
        }
        out_volume += c->volume * static_cast<f32>(s.qty);
        out_mass += c->mass * static_cast<f32>(s.qty);
    }
    return true;
}

bool cargo_can_add(
    const CargoHold& hold, const CommodityTable& table, u32 commodity_id, u32 qty)
{
    if (qty == 0) {
        return true;
    }
    const Commodity* c = find_commodity(table, commodity_id);
    if (c == nullptr) {
        return false;
    }
    f32 used_v = 0.f;
    f32 used_m = 0.f;
    (void)cargo_used(hold, table, used_v, used_m);
    const f32 add_v = c->volume * static_cast<f32>(qty);
    const f32 add_m = c->mass * static_cast<f32>(qty);
    if (used_v + add_v > hold.capacity_volume + 0.0001f) {
        return false;
    }
    if (used_m + add_m > hold.capacity_mass + 0.0001f) {
        return false;
    }
    for (u32 i = 0; i < kMaxCargoSlots; ++i) {
        if (hold.slots[i].qty > 0 && hold.slots[i].commodity_id == commodity_id) {
            return true;
        }
    }
    for (u32 i = 0; i < kMaxCargoSlots; ++i) {
        if (hold.slots[i].qty == 0) {
            return true;
        }
    }
    return false;
}

bool cargo_add(CargoHold& hold, const CommodityTable& table, u32 commodity_id, u32 qty)
{
    if (!cargo_can_add(hold, table, commodity_id, qty)) {
        return false;
    }
    for (u32 i = 0; i < kMaxCargoSlots; ++i) {
        if (hold.slots[i].qty > 0 && hold.slots[i].commodity_id == commodity_id) {
            hold.slots[i].qty += qty;
            return true;
        }
    }
    for (u32 i = 0; i < kMaxCargoSlots; ++i) {
        if (hold.slots[i].qty == 0) {
            hold.slots[i].commodity_id = commodity_id;
            hold.slots[i].qty          = qty;
            if (hold.slot_count < kMaxCargoSlots) {
                ++hold.slot_count;
            }
            return true;
        }
    }
    return false;
}

bool cargo_remove(CargoHold& hold, u32 commodity_id, u32 qty)
{
    if (qty == 0) {
        return true;
    }
    for (u32 i = 0; i < kMaxCargoSlots; ++i) {
        CargoSlot& s = hold.slots[i];
        if (s.qty == 0 || s.commodity_id != commodity_id) {
            continue;
        }
        if (s.qty < qty) {
            return false;
        }
        s.qty -= qty;
        if (s.qty == 0) {
            s.commodity_id = 0;
            if (hold.slot_count > 0) {
                --hold.slot_count;
            }
        }
        return true;
    }
    return false;
}

u32 cargo_count(const CargoHold& hold, u32 commodity_id)
{
    for (u32 i = 0; i < kMaxCargoSlots; ++i) {
        if (hold.slots[i].qty > 0 && hold.slots[i].commodity_id == commodity_id) {
            return hold.slots[i].qty;
        }
    }
    return 0;
}

bool try_buy(
    PlayerWallet& wallet,
    CargoHold& hold,
    Market& market,
    const CommodityTable& table,
    u32 commodity_id,
    u32 qty,
    i32& out_paid)
{
    out_paid = 0;
    if (qty == 0 || commodity_id >= kMaxCommodities) {
        return false;
    }
    const Commodity* c = find_commodity(table, commodity_id);
    if (c == nullptr) {
        return false;
    }
    if (market.stock[commodity_id] < static_cast<i32>(qty)) {
        return false;
    }
    if (!cargo_can_add(hold, table, commodity_id, qty)) {
        return false;
    }

    const f32 unit = market_unit_price(*c, market, commodity_id);
    const i32 cost = static_cast<i32>(unit * static_cast<f32>(qty) + 0.5f);
    if (cost < 0 || wallet.credits < cost) {
        return false;
    }
    if (!cargo_add(hold, table, commodity_id, qty)) {
        return false;
    }
    wallet.credits -= cost;
    market.stock[commodity_id] -= static_cast<i32>(qty);
    out_paid = cost;
    return true;
}

bool try_sell(
    PlayerWallet& wallet,
    CargoHold& hold,
    Market& market,
    const CommodityTable& table,
    u32 commodity_id,
    u32 qty,
    i32& out_earned)
{
    out_earned = 0;
    if (qty == 0 || commodity_id >= kMaxCommodities) {
        return false;
    }
    const Commodity* c = find_commodity(table, commodity_id);
    if (c == nullptr) {
        return false;
    }
    if (cargo_count(hold, commodity_id) < qty) {
        return false;
    }

    if (!cargo_remove(hold, commodity_id, qty)) {
        return false;
    }
    const f32 unit   = market_unit_price(*c, market, commodity_id);
    const i32 earned = static_cast<i32>(unit * static_cast<f32>(qty) + 0.5f);
    wallet.credits += earned;
    market.stock[commodity_id] += static_cast<i32>(qty);
    out_earned = earned;
    return true;
}

bool mission_generate_from_template(
    const MissionTemplate& tmpl, MissionActive& out, u32& rng_state)
{
    out                = MissionActive{};
    out.template_id    = tmpl.id;
    out.type           = tmpl.type;
    out.commodity_id   = tmpl.commodity_id;
    out.qty_required   = roll_u32(rng_state, tmpl.qty_min, tmpl.qty_max);
    out.qty_delivered  = 0;
    out.reward_credits = roll_i32(rng_state, tmpl.reward_min, tmpl.reward_max);
    out.faction_id     = tmpl.faction_id;
    out.rep_delta      = tmpl.rep_delta;
    out.from_market    = tmpl.from_market;
    out.to_market      = tmpl.to_market;
    out.location_id    = tmpl.to_market;
    out.completed      = false;
    if (out.type == MissionType::Visit) {
        out.qty_required = 1;
    }
    if (out.type == MissionType::Combat || out.type == MissionType::Escort) {
        // qty_required doubles as "hostiles to defeat" for these types —
        // capped so spawn_mission_encounter never exceeds kMaxMissionHostiles.
        out.qty_required =
            (out.qty_required > kMaxMissionHostiles) ? kMaxMissionHostiles : out.qty_required;
        out.target_faction_id = tmpl.target_faction_id;
    }
    return true;
}

bool mission_template_available(const MissionTemplate& tmpl, const CompletedMissions& completed)
{
    if (tmpl.requires_completed_id != kNoMissionRequirement) {
        if (tmpl.requires_completed_id >= kMaxMissionTemplates
            || !completed.done[tmpl.requires_completed_id]) {
            return false;
        }
    }
    if (tmpl.branch_group != 0 && tmpl.branch_group < kMaxBranchGroups
        && completed.branch_locked[tmpl.branch_group]) {
        return false;
    }
    return true;
}

bool mission_try_accept(
    MissionActivePool& pool,
    const MissionTemplateTable& templates,
    u32 template_id,
    CompletedMissions& completed,
    u32* out_slot_index)
{
    const MissionTemplate* tmpl = find_template(templates, template_id);
    if (tmpl == nullptr || !mission_template_available(*tmpl, completed)) {
        return false;
    }
    for (std::size_t i = 0; i < kMaxActiveMissions; ++i) {
        if (pool.pool.is_active(i) && pool.pool.slots[i].template_id == template_id) {
            return false;
        }
    }
    const std::size_t idx = pool.pool.acquire();
    if (idx >= kMaxActiveMissions) {
        return false;
    }
    MissionActive generated{};
    if (!mission_generate_from_template(*tmpl, generated, pool.rng_state)) {
        pool.pool.release(idx);
        return false;
    }
    pool.pool.slots[idx] = generated;
    // P2-09: choice locks in immediately, not just on completion.
    if (tmpl->branch_group != 0 && tmpl->branch_group < kMaxBranchGroups) {
        completed.branch_locked[tmpl->branch_group] = true;
    }
    if (out_slot_index != nullptr) {
        *out_slot_index = static_cast<u32>(idx);
    }
    return true;
}

bool mission_try_complete_at_market(
    MissionActivePool& pool,
    CargoHold& hold,
    PlayerWallet& wallet,
    FactionReputation& rep,
    CompletedMissions& completed,
    u32 market_id,
    u32& out_commodity_id,
    u32& out_qty)
{
    out_commodity_id = 0;
    out_qty          = 0;

    for (std::size_t i = 0; i < kMaxActiveMissions; ++i) {
        if (!pool.pool.is_active(i)) {
            continue;
        }
        MissionActive& m = pool.pool.slots[i];
        if (m.completed || m.to_market != market_id) {
            continue;
        }

        if (m.type == MissionType::Delivery) {
            if (cargo_count(hold, m.commodity_id) < m.qty_required) {
                continue;
            }
            if (!cargo_remove(hold, m.commodity_id, m.qty_required)) {
                continue;
            }
            m.qty_delivered  = m.qty_required;
            out_commodity_id = m.commodity_id;
            out_qty          = m.qty_delivered; // P2-08: caller queues a PriceEvent
        } else if (m.type == MissionType::Visit) {
            if (m.qty_delivered < 1) {
                continue;
            }
        } else if (m.type == MissionType::Combat || m.type == MissionType::Escort) {
            // P2-10: reuses the shared AI's kill reporting (MissionLink hook
            // in flight.cpp's apply_damage_events) — nothing to check here
            // beyond "enough kills confirmed". A failed Escort never reaches
            // this loop: the death hook releases the slot immediately.
            if (m.kills_confirmed < m.qty_required) {
                continue;
            }
        } else {
            continue;
        }

        wallet.credits += m.reward_credits;
        if (m.faction_id < kNumFactions) {
            rep.values[m.faction_id] =
                clampf(rep.values[m.faction_id] + m.rep_delta, -1.f, 1.f);
        }
        m.completed = true;
        // P2-09: unlock any template chained off this one before releasing —
        // the slot's data is gone after release(), record what mattered now.
        if (m.template_id < kMaxMissionTemplates) {
            completed.done[m.template_id] = true;
        }
        pool.pool.release(i);
        return true;
    }
    return false;
}

void register_systems(flecs::world& world)
{
    if (world.try_get<PlayerWallet>() == nullptr) {
        world.set<PlayerWallet>(PlayerWallet{});
    }
    if (world.try_get<FactionReputation>() == nullptr) {
        world.set<FactionReputation>(FactionReputation{});
    }
    if (world.try_get<MissionActivePool>() == nullptr) {
        MissionActivePool pool{};
        pool.pool.init();
        pool.rng_state = 1;
        world.set<MissionActivePool>(pool);
    }
    if (world.try_get<PriceEventQueue>() == nullptr) {
        world.set<PriceEventQueue>(PriceEventQueue{});
    }
    if (world.try_get<EconomyClock>() == nullptr) {
        world.set<EconomyClock>(EconomyClock{});
    }
    if (world.try_get<CompletedMissions>() == nullptr) {
        world.set<CompletedMissions>(CompletedMissions{});
    }
}

bool load_economy_data(flecs::world& world)
{
    CommodityTable       commodities{};
    MarketTable          markets{};
    MissionTemplateTable templates{};

    const bool ok_c = load_commodities_file(commodities, kCommoditiesPath);
    const bool ok_m = load_markets_file(markets, kMarketsPath);
    const bool ok_t = load_templates_file(templates, kMissionTemplatesPath);

    world.set<CommodityTable>(commodities);
    world.set<MarketTable>(markets);
    world.set<MissionTemplateTable>(templates);

    if (world.try_get<PlayerWallet>() == nullptr) {
        world.set<PlayerWallet>(PlayerWallet{kStartingCredits});
    }
    if (world.try_get<FactionReputation>() == nullptr) {
        world.set<FactionReputation>(FactionReputation{});
    }
    if (world.try_get<MissionActivePool>() == nullptr) {
        MissionActivePool pool{};
        pool.pool.init();
        pool.rng_state = 1;
        world.set<MissionActivePool>(pool);
    }
    if (world.try_get<PriceEventQueue>() == nullptr) {
        world.set<PriceEventQueue>(PriceEventQueue{});
    }
    if (world.try_get<EconomyClock>() == nullptr) {
        world.set<EconomyClock>(EconomyClock{});
    }
    if (world.try_get<CompletedMissions>() == nullptr) {
        world.set<CompletedMissions>(CompletedMissions{});
    }

    return ok_c && ok_m && ok_t;
}

void fixed_step(flecs::world& world, f32 dt)
{
    // P2-08: background economic simulation — NOT per-frame. Most ticks this
    // is just an accumulator add; the actual simulation step (drain price
    // events, apply production/consumption drift, maybe roll a random event)
    // runs once every kEconomyTickSeconds of game time.
    EconomyClock* clock = world.try_get_mut<EconomyClock>();
    if (clock == nullptr) {
        return;
    }
    clock->accumulated += dt;
    if (clock->accumulated < kEconomyTickSeconds) {
        return;
    }
    clock->accumulated -= kEconomyTickSeconds; // keep remainder, avoid cumulative drift
    run_economic_tick(world);
}

bool handle_interact(flecs::world& world, flecs::entity_t actor_id, flecs::entity_t target_id)
{
    if (actor_id == 0 || target_id == 0) {
        return false;
    }
    flecs::entity actor  = world.entity(actor_id);
    flecs::entity target = world.entity(target_id);
    if (!actor.is_alive() || !target.is_alive()) {
        return false;
    }

    CommodityTable*          commodities = world.try_get_mut<CommodityTable>();
    MarketTable*             markets     = world.try_get_mut<MarketTable>();
    PlayerWallet*            wallet      = world.try_get_mut<PlayerWallet>();
    MissionActivePool*       missions    = world.try_get_mut<MissionActivePool>();
    MissionTemplateTable*    templates   = world.try_get_mut<MissionTemplateTable>();
    FactionReputation*       rep         = world.try_get_mut<FactionReputation>();
    CompletedMissions*       completed   = world.try_get_mut<CompletedMissions>();
    CargoHold*               hold        = find_player_cargo(world);

    if (const TravelPad* pad = target.try_get<TravelPad>()) {
        teleport_actor(actor, pad->destination);
        if (missions != nullptr) {
            mark_visit_missions(*missions, pad->location_id);
        }
        world.each([&](flecs::entity e, flight::RigidBody6DOF& rb) {
            if (!e.has<flight::PlayerShip>()) {
                return;
            }
            rb.position    = pad->destination + glm::vec3{6.f, 4.f, 0.f};
            rb.linear_vel  = {};
            rb.angular_vel = {};
            e.set<ecs::Position>({rb.position.x, rb.position.y, rb.position.z});
            e.set<ecs::PreviousPosition>({rb.position.x, rb.position.y, rb.position.z});
        });
        log::log_info(
            log::LogCategory::Game,
            "TravelPad -> location %u (%.1f, %.1f, %.1f)",
            pad->location_id,
            static_cast<double>(pad->destination.x),
            static_cast<double>(pad->destination.y),
            static_cast<double>(pad->destination.z));
        return true;
    }

    if (const TradeOffer* offer = target.try_get<TradeOffer>()) {
        if (commodities == nullptr || markets == nullptr || wallet == nullptr || hold == nullptr) {
            return true;
        }
        Market* market = find_market_mut(*markets, offer->market_id);
        if (market == nullptr) {
            log::log_warn(log::LogCategory::Game, "TradeOffer: unknown market %u", offer->market_id);
            return true;
        }

        const u32 have = cargo_count(*hold, offer->commodity_id);
        if (offer->allow_sell && have > 0) {
            i32 earned = 0;
            if (try_sell(*wallet, *hold, *market, *commodities, offer->commodity_id, 1, earned)) {
                log::log_info(
                    log::LogCategory::Game,
                    "Sold 1 commodity %u at market %u for %d cr (wallet=%d, stock=%d)",
                    offer->commodity_id,
                    offer->market_id,
                    earned,
                    wallet->credits,
                    market->stock[offer->commodity_id]);
            } else {
                log::log_info(log::LogCategory::Game, "Sell failed (cargo/market)");
            }
            return true;
        }
        if (offer->allow_buy) {
            i32 paid = 0;
            if (try_buy(*wallet, *hold, *market, *commodities, offer->commodity_id, 1, paid)) {
                log::log_info(
                    log::LogCategory::Game,
                    "Bought 1 commodity %u at market %u for %d cr (wallet=%d, stock=%d)",
                    offer->commodity_id,
                    offer->market_id,
                    paid,
                    wallet->credits,
                    market->stock[offer->commodity_id]);
            } else {
                log::log_info(
                    log::LogCategory::Game,
                    "Buy failed (credits=%d stock=%d)",
                    wallet->credits,
                    market->stock[offer->commodity_id]);
            }
            return true;
        }
        return true;
    }

    if (const Dialogue* dlg = target.try_get<Dialogue>()) {
        if (missions == nullptr || templates == nullptr || wallet == nullptr || hold == nullptr
            || rep == nullptr || completed == nullptr) {
            return true;
        }

        if (dlg->is_turn_in) {
            u32 delivered_commodity = 0;
            u32 delivered_qty       = 0;
            if (mission_try_complete_at_market(
                    *missions, *hold, *wallet, *rep, *completed, dlg->market_id,
                    delivered_commodity, delivered_qty)) {
                // P2-08: goods delivered in bulk flood the local market —
                // queued, applied at the next economic tick (never immediate).
                if (delivered_qty > 0) {
                    if (PriceEventQueue* queue = world.try_get_mut<PriceEventQueue>()) {
                        (void)queue->push(
                            PriceEvent{
                                dlg->market_id, delivered_commodity,
                                static_cast<i32>(delivered_qty)});
                    }
                }
                log::log_info(
                    log::LogCategory::Game,
                    "Mission complete at market %u — credits=%d",
                    dlg->market_id,
                    wallet->credits);
                log::log_info(
                    log::LogCategory::Game,
                    "FactionReputation: [%.2f, %.2f, %.2f, %.2f]",
                    static_cast<double>(rep->values[0]),
                    static_cast<double>(rep->values[1]),
                    static_cast<double>(rep->values[2]),
                    static_cast<double>(rep->values[3]));
            } else {
                log::log_info(
                    log::LogCategory::Game,
                    "Turn-in failed (missing cargo or no matching mission)");
            }
            return true;
        }

        if (dlg->is_mission_giver) {
            u32 slot_index = 0;
            if (mission_try_accept(
                    *missions, *templates, dlg->template_id, *completed, &slot_index)) {
                // P2-10: Combat/Escort spawn their encounter right where the
                // player accepted — reuses P2-06's NpcCombatant / P2-11's
                // AiThreatTarget, no location registry needed for this scope.
                const MissionActive& generated = missions->pool.slots[slot_index];
                if (generated.type == MissionType::Combat
                    || generated.type == MissionType::Escort) {
                    glm::vec3 near_pos{0.f};
                    if (const ecs::Position* p = actor.try_get<ecs::Position>()) {
                        near_pos = glm::vec3{p->x, p->y, p->z} + glm::vec3{0.f, 0.f, -8.f};
                    }
                    spawn_mission_encounter(world, generated, slot_index, near_pos);
                }
                log::log_info(
                    log::LogCategory::Game,
                    "Accepted mission template %u (active=%zu)",
                    dlg->template_id,
                    missions->pool.alive);
                // P3-07: surface the narrative one-liner, if any.
                if (const MissionTemplate* tmpl =
                        find_template(*templates, dlg->template_id);
                    tmpl != nullptr && tmpl->title[0] != '\0') {
                    log::log_info(log::LogCategory::Game, "  \"%s\"", tmpl->title);
                }
            } else {
                log::log_info(
                    log::LogCategory::Game,
                    "Could not accept mission template %u (duplicate, pool full, "
                    "prerequisite not met, or branch already locked)",
                    dlg->template_id);
            }
            return true;
        }
        return true;
    }

    return false;
}

void attach_cargo_hold_if_missing(flecs::entity ship)
{
    if (!ship.is_alive() || ship.has<CargoHold>()) {
        return;
    }
    CargoHold hold{};
    hold.capacity_volume = kDefaultCargoVolume;
    hold.capacity_mass   = kDefaultCargoMass;
    hold.slot_count      = 0;
    ship.set<CargoHold>(hold);
}

flecs::entity spawn_trader_npc(
    flecs::world&    world,
    const glm::vec3& position,
    u32              market_id,
    u32              commodity_id,
    bool             allow_buy,
    bool             allow_sell,
    const char*      prompt)
{
    character::InteractablePrompt pr{};
    copy_name(pr.label, sizeof(pr.label), prompt != nullptr ? prompt : "Trader");

    TradeOffer offer{};
    offer.market_id    = market_id;
    offer.commodity_id = commodity_id;
    offer.allow_buy    = allow_buy;
    offer.allow_sell   = allow_sell;

    const ecs::Position pos{position.x, position.y, position.z};
    return world.entity()
        .set<TradeOffer>(offer)
        .set<character::InteractablePrompt>(pr)
        .set<ecs::Position>(pos)
        .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
        .set<ecs::Velocity>({0.f, 0.f, 0.f})
        .set<ecs::Scale>({0.55f})
        .add<character::Interactable>()
        .add<ecs::InstanceTag>();
}

flecs::entity spawn_mission_npc(
    flecs::world&    world,
    const glm::vec3& position,
    u32              template_id,
    bool             is_giver,
    bool             is_turn_in,
    u32              market_id,
    const char*      prompt)
{
    character::InteractablePrompt pr{};
    copy_name(pr.label, sizeof(pr.label), prompt != nullptr ? prompt : "Contact");

    Dialogue dlg{};
    dlg.template_id      = template_id;
    dlg.is_mission_giver = is_giver;
    dlg.is_turn_in       = is_turn_in;
    dlg.market_id        = market_id;

    const ecs::Position pos{position.x, position.y, position.z};
    return world.entity()
        .set<Dialogue>(dlg)
        .set<character::InteractablePrompt>(pr)
        .set<ecs::Position>(pos)
        .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
        .set<ecs::Velocity>({0.f, 0.f, 0.f})
        .set<ecs::Scale>({0.55f})
        .add<character::Interactable>()
        .add<ecs::InstanceTag>();
}

flecs::entity spawn_travel_pad(
    flecs::world&    world,
    const glm::vec3& position,
    const glm::vec3& destination,
    u32              location_id,
    const char*      prompt)
{
    character::InteractablePrompt pr{};
    copy_name(pr.label, sizeof(pr.label), prompt != nullptr ? prompt : "Travel");

    TravelPad pad{};
    pad.destination = destination;
    pad.location_id = location_id;

    const ecs::Position pos{position.x, position.y, position.z};
    return world.entity()
        .set<TravelPad>(pad)
        .set<character::InteractablePrompt>(pr)
        .set<ecs::Position>(pos)
        .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
        .set<ecs::Velocity>({0.f, 0.f, 0.f})
        .set<ecs::Scale>({0.7f})
        .add<character::Interactable>()
        .add<ecs::InstanceTag>();
}

bool setup_economy_test_scene(flecs::world& world, f32 aspect)
{
    if (!load_economy_data(world)) {
        log::log_warn(
            log::LogCategory::Game,
            "economy_test: data load incomplete — continuing with whatever loaded");
    }

    world.set<PlayerWallet>(PlayerWallet{kStartingCredits});
    world.set<FactionReputation>(FactionReputation{});
    {
        MissionActivePool pool{};
        pool.pool.init();
        pool.rng_state = 42;
        world.set<MissionActivePool>(pool);
    }

    ecs::world_spawn_default_camera(world, aspect);
    ecs::world_spawn_default_grid(world);

    flight::spawn_projectile_pool(world);

    // P3-04: locations + their NPCs / shops / mission givers come from
    // assets/data/locations.cfg. The scene only places each location in the
    // world; loc.market-a / loc.market-b reproduce the Fase 1C/2 hand-placed
    // set exactly, loc.outpost-c is the new populated stop.
    constexpr glm::vec3 kMarketA{0.f, 0.f, 0.f};
    constexpr glm::vec3 kMarketB{70.f, 0.f, 0.f};
    constexpr glm::vec3 kOutpostC{0.f, 0.f, -70.f};

    flecs::entity ship =
        flight::spawn_player_ship(world, kMarketA + glm::vec3{8.f, 5.f, 4.f});
    attach_cargo_hold_if_missing(ship);

    (void)character::spawn_player_character(
        world, kMarketA + glm::vec3{0.f, 0.9f, 1.5f}, 0);

    const LocationPlacement kPlacements[] = {
        {"loc.market-a", kMarketA},
        {"loc.market-b", kMarketB},
        {"loc.outpost-c", kOutpostC},
    };
    populate_locations(
        world, kPlacements,
        static_cast<u32>(sizeof(kPlacements) / sizeof(kPlacements[0])));

    world.set<ecs::ControlMode>({ecs::ControlModeKind::OnFoot});

    if (const char* smoke = std::getenv("CSC_MISSION_SMOKE");
        smoke != nullptr && smoke[0] == '1') {
        (void)mission_content_smoke_test(world);
    }

    log::log_info(
        log::LogCategory::Game,
        "economy_test ready: buy ore@A -> travel B -> sell/turn-in | F=Interact wallet=%d",
        kStartingCredits);
    return true;
}

bool mission_content_smoke_test(flecs::world& world)
{
    const MissionTemplateTable* templates = world.try_get<MissionTemplateTable>();
    if (templates == nullptr || templates->count == 0) {
        log::log_error(log::LogCategory::Game, "CSC_MISSION_SMOKE: FAIL (no templates)");
        return false;
    }

    const MissionTemplate* t6 = find_template(*templates, 6);
    const MissionTemplate* t7 = find_template(*templates, 7);
    const MissionTemplate* t8 = find_template(*templates, 8);
    const MissionTemplate* t9 = find_template(*templates, 9);

    bool ok = t6 != nullptr && t7 != nullptr && t8 != nullptr && t9 != nullptr;
    if (ok) {
        // The arc carries flavour text.
        ok = ok && t6->title[0] != '\0' && t7->title[0] != '\0'
             && t8->title[0] != '\0' && t9->title[0] != '\0';
        // Chain shape: 6 is free, 7←6, 8←7, 9←8.
        ok = ok && t6->requires_completed_id == kNoMissionRequirement;
        ok = ok && t7->requires_completed_id == 6;
        ok = ok && t8->requires_completed_id == 7;
        ok = ok && t9->requires_completed_id == 8;

        CompletedMissions done{};
        ok = ok && mission_template_available(*t6, done);
        ok = ok && !mission_template_available(*t7, done);  // 6 not done yet
        done.done[6] = true;
        ok = ok && mission_template_available(*t7, done);   // 6 done → 7 opens
        ok = ok && !mission_template_available(*t8, done);  // 7 still pending
    }

    log::log_info(log::LogCategory::Game, "CSC_MISSION_SMOKE: %s", ok ? "PASS" : "FAIL");
    return ok;
}

void fill_economy_telemetry(flecs::world& world, EconomyDebugSnapshot& out)
{
    out = EconomyDebugSnapshot{};
    const PlayerWallet* wallet = world.try_get<PlayerWallet>();
    if (wallet == nullptr) {
        return;
    }
    out.has_data = true;
    out.credits  = wallet->credits;

    const CargoHold* hold  = find_player_cargo(world);
    u32              units = 0;
    if (hold != nullptr) {
        char*       p    = out.cargo_summary;
        std::size_t cap  = sizeof(out.cargo_summary);
        int         used = 0;
        for (u32 i = 0; i < kMaxCargoSlots; ++i) {
            if (hold->slots[i].qty == 0) {
                continue;
            }
            units += hold->slots[i].qty;
            if (cap > 1) {
                const int n = std::snprintf(
                    p + used,
                    cap - static_cast<std::size_t>(used),
                    "%s%u:%u",
                    (used > 0) ? " " : "",
                    hold->slots[i].commodity_id,
                    hold->slots[i].qty);
                if (n > 0) {
                    used += n;
                }
            }
        }
        if (used == 0) {
            copy_name(out.cargo_summary, sizeof(out.cargo_summary), "(empty)");
        }
    } else {
        copy_name(out.cargo_summary, sizeof(out.cargo_summary), "(no hold)");
    }
    out.cargo_units = units;

    const MissionActivePool* pool = world.try_get<MissionActivePool>();
    if (pool != nullptr) {
        out.active_missions = static_cast<u32>(pool->pool.alive);
        if (pool->pool.alive == 0) {
            copy_name(out.mission_summary, sizeof(out.mission_summary), "(none)");
        } else {
            for (std::size_t i = 0; i < kMaxActiveMissions; ++i) {
                if (!pool->pool.is_active(i)) {
                    continue;
                }
                const MissionActive& m = pool->pool.slots[i];
                std::snprintf(
                    out.mission_summary,
                    sizeof(out.mission_summary),
                    "t%u %u/%u ->M%u",
                    m.template_id,
                    m.qty_delivered,
                    m.qty_required,
                    m.to_market);
                break;
            }
        }
    }
}

}  // namespace csc::game::economy
