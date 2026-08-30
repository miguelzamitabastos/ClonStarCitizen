#include "game/flight/weapon_catalog.hpp"

#include "engine/log/log.hpp"
#include "game/flight/ship_catalog.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace csc::game::flight {
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

[[nodiscard]] bool parse_bool(const char* v)
{
    return std::strcmp(v, "1") == 0 || std::strcmp(v, "true") == 0
           || std::strcmp(v, "yes") == 0 || std::strcmp(v, "on") == 0;
}

/// Apply one key=value to `def`. Returns false for an unrecognised key so the
/// loader can warn with a line number (the entry is still kept).
bool apply_weapon_kv(WeaponDef& def, const char* key, const char* value)
{
    if (std::strcmp(key, "id") == 0) {
        copy_str(def.id, sizeof(def.id), value);
        return true;
    }
    if (std::strcmp(key, "name") == 0) {
        copy_str(def.display_name, sizeof(def.display_name), value);
        return true;
    }
    if (std::strcmp(key, "mount") == 0) {
        copy_str(def.mount, sizeof(def.mount), value);
        return true;
    }
    if (std::strcmp(key, "damage") == 0) {
        def.damage = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "cooldown") == 0) {
        def.cooldown = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "energy_cost") == 0) {
        def.energy_cost = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "heat_max") == 0) {
        def.heat_max = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "range") == 0) {
        def.range = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "hitscan") == 0) {
        def.hitscan = parse_bool(value);
        return true;
    }
    return false;
}

}  // namespace

bool load_weapon_catalog_file(WeaponCatalog& out, const char* path)
{
    out = WeaponCatalog{};

    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        log::log_error(log::LogCategory::Config, "weapon catalog missing: %s", path);
        return false;
    }

    bool      ok        = true;
    bool      in_weapon = false;
    WeaponDef current{};

    auto flush = [&]() {
        if (!in_weapon) {
            return;
        }
        in_weapon = false;

        if (current.id[0] == '\0') {
            log::log_error(
                log::LogCategory::Config, "%s: [[weapon]] entry without an id", path);
            ok = false;
            return;
        }
        for (u32 i = 0; i < out.count; ++i) {
            if (std::strcmp(out.items[i].id, current.id) == 0) {
                log::log_error(
                    log::LogCategory::Config, "%s: duplicate weapon id '%s'", path, current.id);
                ok = false;
                return;
            }
        }
        if (out.count >= kMaxWeaponDefs) {
            log::log_error(
                log::LogCategory::Config,
                "%s: more than %u weapons (id '%s' dropped)",
                path,
                kMaxWeaponDefs,
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
        if (std::strcmp(trimmed, "[[weapon]]") == 0) {
            flush();
            current   = WeaponDef{};
            in_weapon = true;
            continue;
        }
        if (!in_weapon) {
            log::log_warn(
                log::LogCategory::Config, "%s:%d: key outside a [[weapon]] block", path, line_no);
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
        if (!apply_weapon_kv(current, key, value)) {
            log::log_warn(
                log::LogCategory::Config, "%s:%d: bad key/value '%s'", path, line_no, key);
        }
    }
    flush();
    std::fclose(file);

    if (out.count == 0) {
        log::log_error(log::LogCategory::Config, "%s: no weapons loaded", path);
        return false;
    }
    if (ok) {
        log::log_info(
            log::LogCategory::Config, "Loaded %u weapons from %s", out.count, path);
    } else {
        log::log_error(
            log::LogCategory::Config,
            "%s: %u weapons kept but the catalog is INVALID (see errors above)",
            path,
            out.count);
    }
    return ok;
}

bool load_weapon_catalog(flecs::world& world)
{
    WeaponCatalog catalog{};
    const bool    ok = load_weapon_catalog_file(catalog, kWeaponCatalogPath);
    world.set<WeaponCatalog>(catalog);
    if (!ok) {
        log::log_error(
            log::LogCategory::Config,
            "weapon catalog validation FAILED — mounts fall back to built-in defaults");
    }
    return ok;
}

const WeaponDef* find_weapon_def(const WeaponCatalog& cat, const char* id)
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

const WeaponDef* find_weapon_def(flecs::world& world, const char* id)
{
    const WeaponCatalog* cat = world.try_get<WeaponCatalog>();
    return (cat != nullptr) ? find_weapon_def(*cat, id) : nullptr;
}

bool validate_ship_weapon_refs(flecs::world& world)
{
    const ShipCatalog*   ships   = world.try_get<ShipCatalog>();
    const WeaponCatalog* weapons = world.try_get<WeaponCatalog>();
    if (ships == nullptr || weapons == nullptr) {
        return true;  // nothing to cross-check yet
    }

    bool ok = true;
    for (u32 s = 0; s < ships->count; ++s) {
        const ShipDef& d = ships->items[s];
        for (u32 m = 0; m < kMaxWeaponMounts; ++m) {
            if (d.weapon_id[m][0] == '\0') {
                continue;
            }
            if (find_weapon_def(*weapons, d.weapon_id[m]) == nullptr) {
                log::log_error(
                    log::LogCategory::Config,
                    "ship '%s' mount %u references unknown weapon '%s'",
                    d.id,
                    m,
                    d.weapon_id[m]);
                ok = false;
            }
        }
    }
    if (ok) {
        log::log_info(log::LogCategory::Config, "ship<->weapon references OK");
    } else {
        log::log_error(log::LogCategory::Config, "ship<->weapon reference check FAILED");
    }
    return ok;
}

}  // namespace csc::game::flight
