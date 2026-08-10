#include "game/economy/economy.hpp"

#include "engine/ecs/world.hpp"
#include "engine/log/log.hpp"
#include "game/character/character.hpp"
#include "game/flight/flight.hpp"

#include <cstdio>
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
    if (text != nullptr
        && (std::strcmp(text, "visit") == 0 || std::strcmp(text, "Visit") == 0)) {
        return MissionType::Visit;
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
    return false;
}

void init_market_defaults(Market& m)
{
    m.ideal_stock = 50.f;
    for (u32 i = 0; i < kMaxCommodities; ++i) {
        m.stock[i]     = 0;
        m.price_mod[i] = 1.f;
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

void spawn_market_marker(
    flecs::world& world, const char* name, const glm::vec3& center, f32 scale)
{
    const ecs::Position pos{center.x, center.y, center.z};
    world.entity(name)
        .set<ecs::Position>(pos)
        .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
        .set<ecs::Velocity>({0.f, 0.f, 0.f})
        .set<ecs::Scale>({scale})
        .add<ecs::InstanceTag>();
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
    return true;
}

bool mission_try_accept(
    MissionActivePool& pool, const MissionTemplateTable& templates, u32 template_id)
{
    const MissionTemplate* tmpl = find_template(templates, template_id);
    if (tmpl == nullptr) {
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
    return true;
}

bool mission_try_complete_at_market(
    MissionActivePool& pool,
    CargoHold& hold,
    PlayerWallet& wallet,
    FactionReputation& rep,
    u32 market_id)
{
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
            m.qty_delivered = m.qty_required;
        } else if (m.type == MissionType::Visit) {
            if (m.qty_delivered < 1) {
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

    return ok_c && ok_m && ok_t;
}

void fixed_step(flecs::world& /*world*/, f32 /*dt*/)
{
    // Event-driven (interact). Visit progress marked on TravelPad.
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
            || rep == nullptr) {
            return true;
        }

        if (dlg->is_turn_in) {
            if (mission_try_complete_at_market(
                    *missions, *hold, *wallet, *rep, dlg->market_id)) {
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
            if (mission_try_accept(*missions, *templates, dlg->template_id)) {
                log::log_info(
                    log::LogCategory::Game,
                    "Accepted mission template %u (active=%zu)",
                    dlg->template_id,
                    missions->pool.alive);
            } else {
                log::log_info(
                    log::LogCategory::Game,
                    "Could not accept mission template %u (duplicate or pool full)",
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

    constexpr glm::vec3 kMarketA{0.f, 0.f, 0.f};
    constexpr glm::vec3 kMarketB{70.f, 0.f, 0.f};

    (void)character::spawn_gravity_zone_box(
        world,
        kMarketA + glm::vec3{0.f, 2.f, 0.f},
        glm::vec3{14.f, 6.f, 14.f},
        glm::vec3{0.f, -1.f, 0.f},
        character::kGravityDefault,
        "MarketAGravity");
    (void)character::spawn_gravity_zone_box(
        world,
        kMarketB + glm::vec3{0.f, 2.f, 0.f},
        glm::vec3{14.f, 6.f, 14.f},
        glm::vec3{0.f, -1.f, 0.f},
        character::kGravityDefault,
        "MarketBGravity");

    spawn_market_marker(world, "MarketADeck", kMarketA + glm::vec3{0.f, 0.4f, 0.f}, 7.f);
    spawn_market_marker(world, "MarketBDeck", kMarketB + glm::vec3{0.f, 0.4f, 0.f}, 7.f);

    flecs::entity ship =
        flight::spawn_player_ship(world, kMarketA + glm::vec3{8.f, 5.f, 4.f});
    attach_cargo_hold_if_missing(ship);

    (void)character::spawn_player_character(
        world, kMarketA + glm::vec3{0.f, 0.9f, 1.5f}, 0);

    (void)spawn_trader_npc(
        world,
        kMarketA + glm::vec3{0.f, 1.2f, -1.5f},
        0,
        0,
        true,
        false,
        "Buy Ore (A)");
    (void)spawn_mission_npc(
        world,
        kMarketA + glm::vec3{-2.f, 1.2f, -1.5f},
        0,
        true,
        false,
        0,
        "Accept Mission");
    (void)spawn_travel_pad(
        world,
        kMarketA + glm::vec3{2.5f, 1.1f, 0.f},
        kMarketB + glm::vec3{0.f, 0.9f, 1.5f},
        1,
        "Travel -> B");

    (void)spawn_trader_npc(
        world,
        kMarketB + glm::vec3{0.f, 1.2f, -1.5f},
        1,
        0,
        false,
        true,
        "Sell Ore (B)");
    (void)spawn_mission_npc(
        world,
        kMarketB + glm::vec3{-2.f, 1.2f, -1.5f},
        0,
        false,
        true,
        1,
        "Turn In Mission");
    (void)spawn_travel_pad(
        world,
        kMarketB + glm::vec3{2.5f, 1.1f, 0.f},
        kMarketA + glm::vec3{0.f, 0.9f, 1.5f},
        0,
        "Travel -> A");

    world.set<ecs::ControlMode>({ecs::ControlModeKind::OnFoot});

    log::log_info(
        log::LogCategory::Game,
        "economy_test ready: buy ore@A -> travel B -> sell/turn-in | F=Interact wallet=%d",
        kStartingCredits);
    return true;
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
