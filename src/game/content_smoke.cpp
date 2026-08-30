#include "game/content_smoke.hpp"

#include "engine/core/types.hpp"
#include "engine/log/log.hpp"
#include "game/character/suit_catalog.hpp"
#include "game/economy/economy.hpp"
#include "game/economy/location_catalog.hpp"
#include "game/flight/ship_catalog.hpp"
#include "game/flight/weapon_catalog.hpp"

namespace csc::game::content {

bool run_content_smoke_test(flecs::world& world)
{
    const auto* wcat = world.try_get<flight::WeaponCatalog>();
    const auto* scat = world.try_get<flight::ShipCatalog>();
    const auto* sui  = world.try_get<character::SuitCatalog>();
    const auto* loc  = world.try_get<economy::LocationCatalog>();
    const auto* comm = world.try_get<economy::CommodityTable>();
    const auto* mk   = world.try_get<economy::MarketTable>();
    const auto* mt   = world.try_get<economy::MissionTemplateTable>();

    const u32 nw = (wcat != nullptr) ? wcat->count : 0u;
    const u32 ns = (scat != nullptr) ? scat->count : 0u;
    const u32 nu = (sui != nullptr) ? sui->count : 0u;
    const u32 nl = (loc != nullptr) ? loc->count : 0u;
    const u32 nc = (comm != nullptr) ? comm->count : 0u;
    const u32 nm = (mk != nullptr) ? mk->count : 0u;
    const u32 nt = (mt != nullptr) ? mt->count : 0u;

    log::log_info(
        log::LogCategory::Config,
        "content: %u ships, %u weapons, %u suits, %u locations, %u commodities, "
        "%u markets, %u missions",
        ns, nw, nu, nl, nc, nm, nt);

    bool ok = true;

    if (nw == 0) {
        log::log_error(log::LogCategory::Config, "content: weapon catalog empty/missing");
        ok = false;
    }
    if (ns == 0) {
        log::log_error(log::LogCategory::Config, "content: ship catalog empty/missing");
        ok = false;
    }
    if (nl == 0) {
        log::log_error(log::LogCategory::Config, "content: location catalog empty/missing");
        ok = false;
    }

    // Ids the engine spawns by name — must exist.
    static const char* const kReqShips[]   = {"ship.player.default", "ship.npc.skiff"};
    static const char* const kReqWeapons[] = {"weapon.fixed.repeater", "weapon.turret.repeater"};
    for (const char* id : kReqShips) {
        if (scat != nullptr && flight::find_ship_def(*scat, id) == nullptr) {
            log::log_error(log::LogCategory::Config, "content: missing required ship '%s'", id);
            ok = false;
        }
    }
    for (const char* id : kReqWeapons) {
        if (wcat != nullptr && flight::find_weapon_def(*wcat, id) == nullptr) {
            log::log_error(log::LogCategory::Config, "content: missing required weapon '%s'", id);
            ok = false;
        }
    }

    // Per-domain integrity checks (each logs its own problems).
    ok = flight::validate_ship_weapon_refs(world) && ok;
    ok = character::validate_suit_catalog(world) && ok;
    ok = economy::validate_economy_content(world) && ok;

    log::log_info(log::LogCategory::Config, "CONTENT_SMOKE: %s", ok ? "PASS" : "FAIL");
    return ok;
}

}  // namespace csc::game::content
