#include "game/world/resources.hpp"

#include "engine/ecs/world.hpp"
#include "engine/log/log.hpp"
#include "game/flight/flight.hpp"
#include "game/world/rng64.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace csc::game::world {
namespace {

inline constexpr f32 kTwoPi = 6.28318530717958647692f;

}  // namespace

u32 generate_asteroid_field(
    u64 system_seed, u32 commodity_count, ResourceDeposit* out, glm::vec3* out_pos, u32 max_out)
{
    if (out == nullptr || out_pos == nullptr || max_out == 0) {
        return 0;
    }
    Rng64 rng(system_seed ^ 0xA57E401D0BE17E57ull);  // "asteroid"

    u32 n = rng.range_u32(8u, kMaxDeposits);
    if (n > max_out) {
        n = max_out;
    }
    if (n > kMaxDeposits) {
        n = kMaxDeposits;
    }

    for (u32 i = 0; i < n; ++i) {
        const f32 ang = rng.next_unit() * kTwoPi;
        const f32 rad = rng.range(4000.f, 9000.f);
        const f32 y   = rng.range(-220.f, 220.f);
        out_pos[i]    = glm::vec3{rad * std::cos(ang), y, rad * std::sin(ang)};

        ResourceDeposit& d = out[i];
        d.commodity_id     = 0;  // ore is the common case
        if (commodity_count > 1 && rng.next_unit() < 0.25f) {
            d.commodity_id = rng.range_u32(1u, commodity_count - 1u);
        }
        d.total        = rng.range(80.f, 400.f);
        d.remaining    = d.total;
        d.yield_per_sec = rng.range(2.5f, 5.0f);
        d.carry        = 0.f;
    }
    return n;
}

void spawn_asteroid_field(flecs::world& world, u64 system_seed)
{
    const economy::CommodityTable* table = world.try_get<economy::CommodityTable>();
    const u32 ncomm = (table != nullptr && table->count > 0) ? table->count : 1u;

    ResourceDeposit deps[kMaxDeposits];
    glm::vec3       pos[kMaxDeposits];
    const u32       n = generate_asteroid_field(system_seed, ncomm, deps, pos, kMaxDeposits);

    for (u32 i = 0; i < n; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "Asteroid_%02u", i);
        world.entity(name)
            .set<ResourceDeposit>(deps[i])
            .set<ecs::Position>({pos[i].x, pos[i].y, pos[i].z})
            .set<ecs::PreviousPosition>({pos[i].x, pos[i].y, pos[i].z})
            .set<ecs::Velocity>({0.f, 0.f, 0.f})
            .set<ecs::Scale>({6.f})
            .add<ecs::InstanceTag>();
    }
    log::log_info(log::LogCategory::Game, "Spawned %u minable deposits", n);
}

u32 mine_deposit(
    ResourceDeposit& d, economy::CargoHold& hold, const economy::CommodityTable& table, f32 dt)
{
    if (d.remaining <= 0.f || dt <= 0.f) {
        return 0;
    }
    d.carry += d.yield_per_sec * dt;
    if (d.carry > d.remaining) {
        d.carry = d.remaining;
    }
    u32 want = static_cast<u32>(d.carry);
    if (want == 0) {
        return 0;
    }
    if (want > 64u) {
        want = 64u;  // clamp a huge dt
    }

    u32 got = want;
    while (got > 0 && !economy::cargo_add(hold, table, d.commodity_id, got)) {
        --got;
    }
    if (got == 0) {
        return 0;  // cargo full
    }
    d.carry     -= static_cast<f32>(got);
    d.remaining -= static_cast<f32>(got);
    if (d.remaining < 0.f) {
        d.remaining = 0.f;
    }
    return got;
}

void update_mining(flecs::world& world, f32 dt)
{
    if (dt <= 0.f) {
        return;
    }
    const economy::CommodityTable* table = world.try_get<economy::CommodityTable>();
    if (table == nullptr) {
        return;
    }

    glm::vec3            ship_pos{0.f};
    f32                  ship_speed = 1e30f;
    economy::CargoHold*  hold       = nullptr;
    bool                 have_ship  = false;
    world.each([&](flecs::entity e, flight::RigidBody6DOF& rb, economy::CargoHold& ch) {
        if (have_ship || !e.has<flight::PlayerShip>()) {
            return;
        }
        ship_pos   = rb.position;
        ship_speed = std::sqrt(glm::dot(rb.linear_vel, rb.linear_vel));
        hold       = &ch;
        have_ship  = true;
    });
    if (!have_ship || hold == nullptr || ship_speed > kDepositMaxShipSpeed) {
        return;
    }

    flecs::entity_t depleted[kMaxDeposits];
    u32            depleted_count = 0;

    world.each([&](flecs::entity e, ResourceDeposit& d) {
        if (e.has<DepositDepleted>() || d.remaining <= 0.f) {
            return;
        }
        const ecs::Position* p = e.try_get<ecs::Position>();
        if (p == nullptr) {
            return;
        }
        const glm::vec3 to{ship_pos.x - p->x, ship_pos.y - p->y, ship_pos.z - p->z};
        if (glm::dot(to, to) > kDepositMineRange * kDepositMineRange) {
            return;
        }
        (void)mine_deposit(d, *hold, *table, dt);
        if (d.remaining <= 0.f && depleted_count < kMaxDeposits) {
            depleted[depleted_count++] = e.id();
        }
    });

    for (u32 i = 0; i < depleted_count; ++i) {
        flecs::entity e = world.entity(depleted[i]);
        if (e.is_alive() && !e.has<DepositDepleted>()) {
            e.add<DepositDepleted>();
        }
    }
}

bool resources_smoke_test()
{
    bool ok = true;

    // A tiny throwaway commodity table (ore / electronics / medical).
    economy::CommodityTable table{};
    table.count = 3;
    for (u32 i = 0; i < 3; ++i) {
        table.items[i].id         = i;
        table.items[i].base_price = 10.f;
        table.items[i].volume     = 0.5f;
        table.items[i].mass       = 1.0f;
    }
    std::snprintf(table.items[0].name, sizeof(table.items[0].name), "Ore");
    std::snprintf(table.items[1].name, sizeof(table.items[1].name), "Electronics");
    std::snprintf(table.items[2].name, sizeof(table.items[2].name), "Medical");

    // Determinism.
    ResourceDeposit da[kMaxDeposits];
    ResourceDeposit db[kMaxDeposits];
    glm::vec3       pa[kMaxDeposits];
    glm::vec3       pb[kMaxDeposits];
    const u32       na = generate_asteroid_field(777ull, table.count, da, pa, kMaxDeposits);
    const u32       nb = generate_asteroid_field(777ull, table.count, db, pb, kMaxDeposits);
    if (na != nb || na == 0
        || std::memcmp(da, db, na * sizeof(ResourceDeposit)) != 0
        || std::memcmp(pa, pb, na * sizeof(glm::vec3)) != 0) {
        log::log_error(log::LogCategory::Game, "resources: field generation not deterministic");
        ok = false;
    }
    for (u32 i = 0; i < na; ++i) {
        if (da[i].commodity_id >= table.count || da[i].total <= 0.f) {
            log::log_error(log::LogCategory::Game, "resources: deposit %u has bad commodity/total", i);
            ok = false;
        }
    }

    // Mining conserves units and depletes the deposit.
    {
        ResourceDeposit d{};
        d.commodity_id  = 0;
        d.total         = 50.f;
        d.remaining     = 50.f;
        d.yield_per_sec = 5.f;
        economy::CargoHold hold{};
        hold.capacity_volume = 1e6f;  // effectively unlimited
        hold.capacity_mass   = 1e6f;
        u32 total_mined = 0;
        for (u32 k = 0; k < 200 && d.remaining > 0.f; ++k) {
            total_mined += mine_deposit(d, hold, table, 1.0f);
        }
        const u32 in_hold = economy::cargo_count(hold, 0u);
        if (total_mined != 50u || in_hold != 50u || d.remaining > 0.001f) {
            log::log_error(
                log::LogCategory::Game,
                "resources: mining not conserved (mined %u, hold %u, remaining %.2f)",
                total_mined, in_hold, static_cast<double>(d.remaining));
            ok = false;
        }
    }

    // A full cargo stops mining without losing the deposit.
    {
        ResourceDeposit d{};
        d.commodity_id  = 0;
        d.total         = 100.f;
        d.remaining     = 100.f;
        d.yield_per_sec = 5.f;
        economy::CargoHold hold{};
        hold.capacity_volume = 5.f * 0.5f;  // room for ~5 ore units
        hold.capacity_mass   = 1e6f;
        for (u32 k = 0; k < 50; ++k) {
            (void)mine_deposit(d, hold, table, 1.0f);
        }
        const u32 in_hold = economy::cargo_count(hold, 0u);
        if (in_hold == 0u || in_hold > 6u || d.remaining < 90.f) {
            log::log_error(
                log::LogCategory::Game,
                "resources: full-cargo mining wrong (hold %u, remaining %.1f)",
                in_hold, static_cast<double>(d.remaining));
            ok = false;
        }
    }

    log::log_info(log::LogCategory::Game, "CSC_RESOURCES_SMOKE: %s", ok ? "PASS" : "FAIL");
    return ok;
}

}  // namespace csc::game::world
