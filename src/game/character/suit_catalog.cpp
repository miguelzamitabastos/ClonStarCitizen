#include "game/character/suit_catalog.hpp"

#include "engine/ecs/world.hpp"
#include "engine/log/log.hpp"
#include "game/character/character.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace csc::game::character {
namespace {

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

void copy_str(char* dest, std::size_t dest_bytes, const char* src)
{
    if (dest == nullptr || dest_bytes == 0) {
        return;
    }
    std::snprintf(dest, dest_bytes, "%s", (src != nullptr) ? src : "");
}

bool apply_suit_kv(SuitDef& def, const char* key, const char* value)
{
    if (std::strcmp(key, "id") == 0) {
        copy_str(def.id, sizeof(def.id), value);
        return true;
    }
    if (std::strcmp(key, "name") == 0) {
        copy_str(def.display_name, sizeof(def.display_name), value);
        return true;
    }
    if (std::strcmp(key, "damage_reduction") == 0) {
        def.damage_reduction = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "eva_capacity") == 0) {
        def.eva_capacity = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "eva_drain_per_sec") == 0) {
        def.eva_drain_per_sec = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "eva_recharge_per_sec") == 0) {
        def.eva_recharge_per_sec = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "move_speed_mult") == 0) {
        def.move_speed_mult = std::strtof(value, nullptr);
        return true;
    }
    return false;
}

}  // namespace

bool load_suit_catalog_file(SuitCatalog& out, const char* path)
{
    out = SuitCatalog{};

    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        log::log_error(log::LogCategory::Config, "suit catalog missing: %s", path);
        return false;
    }

    bool    ok      = true;
    bool    in_suit = false;
    SuitDef current{};

    auto flush = [&]() {
        if (!in_suit) {
            return;
        }
        in_suit = false;
        if (current.id[0] == '\0') {
            log::log_error(log::LogCategory::Config, "%s: [[suit]] entry without an id", path);
            ok = false;
            return;
        }
        for (u32 i = 0; i < out.count; ++i) {
            if (std::strcmp(out.items[i].id, current.id) == 0) {
                log::log_error(
                    log::LogCategory::Config, "%s: duplicate suit id '%s'", path, current.id);
                ok = false;
                return;
            }
        }
        if (out.count >= kMaxSuitDefs) {
            log::log_error(
                log::LogCategory::Config,
                "%s: more than %u suits (id '%s' dropped)",
                path,
                kMaxSuitDefs,
                current.id);
            ok = false;
            return;
        }
        if (current.display_name[0] == '\0') {
            copy_str(current.display_name, sizeof(current.display_name), current.id);
        }
        out.items[out.count++] = current;
    };

    char line[512];
    int  line_no = 0;
    while (std::fgets(line, static_cast<int>(sizeof(line)), file) != nullptr) {
        ++line_no;
        char* trimmed = trim_inplace(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        if (std::strcmp(trimmed, "[[suit]]") == 0) {
            flush();
            current = SuitDef{};
            in_suit = true;
            continue;
        }
        if (!in_suit) {
            log::log_warn(
                log::LogCategory::Config, "%s:%d: key outside a [[suit]] block", path, line_no);
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
        if (!apply_suit_kv(current, key, value)) {
            log::log_warn(
                log::LogCategory::Config, "%s:%d: bad key/value '%s'", path, line_no, key);
        }
    }
    flush();
    std::fclose(file);

    if (out.count == 0) {
        log::log_error(log::LogCategory::Config, "%s: no suits loaded", path);
        return false;
    }
    if (ok) {
        log::log_info(log::LogCategory::Config, "Loaded %u suits from %s", out.count, path);
    } else {
        log::log_error(
            log::LogCategory::Config,
            "%s: %u suits kept but the catalog is INVALID (see errors above)",
            path,
            out.count);
    }
    return ok;
}

bool load_suit_catalog(flecs::world& world)
{
    SuitCatalog catalog{};
    const bool  ok = load_suit_catalog_file(catalog, kSuitCatalogPath);
    world.set<SuitCatalog>(catalog);
    if (!ok) {
        log::log_error(
            log::LogCategory::Config,
            "suit catalog validation FAILED — characters fall back to the baseline suit");
    }
    return ok;
}

const SuitDef* find_suit_def(const SuitCatalog& cat, const char* id)
{
    if (id == nullptr || id[0] == '\0') {
        return nullptr;
    }
    for (u32 i = 0; i < cat.count; ++i) {
        if (std::strcmp(cat.items[i].id, id) == 0) {
            return &cat.items[i];
        }
    }
    return nullptr;
}

const SuitDef* find_suit_def(flecs::world& world, const char* id)
{
    const SuitCatalog* cat = world.try_get<SuitCatalog>();
    return (cat != nullptr) ? find_suit_def(*cat, id) : nullptr;
}

Suit suit_from_def(const SuitDef* def)
{
    Suit s{};
    if (def == nullptr) {
        return s;  // SuitDef{} baseline
    }
    std::snprintf(s.id, sizeof(s.id), "%s", def->id);
    s.damage_reduction     = def->damage_reduction;
    s.eva_capacity         = def->eva_capacity;
    s.eva_drain_per_sec    = def->eva_drain_per_sec;
    s.eva_recharge_per_sec = def->eva_recharge_per_sec;
    s.move_speed_mult      = def->move_speed_mult;
    s.eva_charge           = def->eva_capacity;
    return s;
}

bool equip_player_suit(flecs::world& world, const char* suit_id)
{
    flecs::entity player{};
    world.each([&](flecs::entity e, PlayerCharacter) {
        if (!player.is_alive()) {
            player = e;
        }
    });
    if (!player.is_alive()) {
        log::log_warn(log::LogCategory::Game, "equip_player_suit: no PlayerCharacter");
        return false;
    }

    const char* id = (suit_id != nullptr && suit_id[0] != '\0') ? suit_id : kDefaultPlayerSuitId;
    const SuitDef* def = find_suit_def(world, id);
    if (def == nullptr) {
        log::log_warn(log::LogCategory::Game, "equip_player_suit: '%s' not in catalog", id);
        return false;
    }

    if (const Suit* worn = player.try_get<Suit>()) {
        if (std::strcmp(worn->id, def->id) == 0) {
            log::log_info(log::LogCategory::Game, "Locker: already wearing %s", def->display_name);
            return false;
        }
    }
    player.set<Suit>(suit_from_def(def));
    log::log_info(
        log::LogCategory::Game,
        "Locker: equipped %s (armour %.0f%%, EVA %.0f, speed x%.2f)",
        def->display_name,
        static_cast<double>(def->damage_reduction * 100.f),
        static_cast<double>(def->eva_capacity),
        static_cast<double>(def->move_speed_mult));
    return true;
}

bool suit_smoke_test(flecs::world& world)
{
    const SuitCatalog* cat = world.try_get<SuitCatalog>();
    if (cat == nullptr || cat->count == 0) {
        log::log_error(log::LogCategory::Game, "CSC_SUIT_SMOKE: FAIL (no catalog)");
        return false;
    }

    flecs::entity player{};
    world.each([&](flecs::entity e, PlayerCharacter) {
        if (!player.is_alive()) {
            player = e;
        }
    });
    if (!player.is_alive()) {
        log::log_error(log::LogCategory::Game, "CSC_SUIT_SMOKE: FAIL (no player)");
        return false;
    }

    const auto near_eq = [](f32 a, f32 b) { return std::fabs(a - b) < 0.01f; };

    bool ok = true;
    for (u32 i = 0; i < cat->count; ++i) {
        const SuitDef& def = cat->items[i];
        (void)equip_player_suit(world, def.id);
        const Suit* worn = player.try_get<Suit>();
        ok = ok && worn != nullptr;
        if (worn == nullptr) {
            break;
        }
        ok = ok && std::strcmp(worn->id, def.id) == 0;
        ok = ok && near_eq(worn->damage_reduction, def.damage_reduction);
        ok = ok && near_eq(worn->eva_capacity, def.eva_capacity);
        ok = ok && near_eq(worn->move_speed_mult, def.move_speed_mult);
        ok = ok && near_eq(worn->eva_charge, def.eva_capacity);  // spawns full
    }

    // Unknown id is rejected, worn suit unchanged.
    const Suit before = *player.try_get<Suit>();
    ok = ok && !equip_player_suit(world, "suit.does.not.exist");
    const Suit* after = player.try_get<Suit>();
    ok = ok && after != nullptr && std::strcmp(after->id, before.id) == 0;

    log::log_info(log::LogCategory::Game, "CSC_SUIT_SMOKE: %s", ok ? "PASS" : "FAIL");
    return ok;
}

flecs::entity spawn_suit_locker(
    flecs::world& world, const glm::vec3& position, const char* suit_id)
{
    SuitLocker lk{};
    std::snprintf(lk.suit_id, sizeof(lk.suit_id), "%s", (suit_id != nullptr) ? suit_id : "");

    const SuitDef* def = find_suit_def(world, lk.suit_id);

    InteractablePrompt pr{};
    std::snprintf(
        pr.label,
        sizeof(pr.label),
        "Suit: %.20s",
        (def != nullptr) ? def->display_name : lk.suit_id);

    const ecs::Position pos{position.x, position.y, position.z};
    return world.entity()
        .set<SuitLocker>(lk)
        .set<InteractablePrompt>(pr)
        .set<ecs::Position>(pos)
        .set<ecs::PreviousPosition>({pos.x, pos.y, pos.z})
        .set<ecs::Velocity>({0.f, 0.f, 0.f})
        .set<ecs::Scale>({0.55f})
        .add<Interactable>()
        .add<ecs::InstanceTag>();
}

}  // namespace csc::game::character
