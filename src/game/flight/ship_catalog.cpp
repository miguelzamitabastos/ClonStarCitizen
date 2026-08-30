#include "game/flight/ship_catalog.hpp"

#include "engine/log/log.hpp"

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

/// Parse up to `max` comma-separated floats from `value` into `out`.
/// Returns the count actually parsed.
u32 parse_f32_list(const char* value, f32* out, u32 max)
{
    u32         n = 0;
    const char* p = value;
    while (n < max && *p != '\0') {
        char* endp = nullptr;
        const f32 v = std::strtof(p, &endp);
        if (endp == p) {
            break;
        }
        out[n++] = v;
        p        = endp;
        while (*p == ' ' || *p == '\t' || *p == ',') {
            ++p;
        }
    }
    return n;
}

[[nodiscard]] bool parse_vec3(const char* value, glm::vec3& out)
{
    f32 v[3]{};
    if (parse_f32_list(value, v, 3) != 3) {
        return false;
    }
    out = glm::vec3{v[0], v[1], v[2]};
    return true;
}

/// key like "weapon_mount_2" → index 2. Returns false if the suffix is absent
/// or not a number.
[[nodiscard]] bool indexed_key(const char* key, const char* prefix, u32& out_index)
{
    const std::size_t plen = std::strlen(prefix);
    if (std::strncmp(key, prefix, plen) != 0 || key[plen] == '\0') {
        return false;
    }
    char* endp = nullptr;
    const unsigned long idx = std::strtoul(key + plen, &endp, 10);
    if (endp == key + plen || *endp != '\0') {
        return false;
    }
    out_index = static_cast<u32>(idx);
    return true;
}

/// Apply one key=value to `def`. Returns false for an unrecognised key so the
/// loader can warn with a line number (the entry is still kept).
bool apply_ship_kv(ShipDef& def, const char* key, const char* value)
{
    u32 idx = 0;

    if (std::strcmp(key, "id") == 0) {
        copy_str(def.id, sizeof(def.id), value);
        return true;
    }
    if (std::strcmp(key, "name") == 0) {
        copy_str(def.display_name, sizeof(def.display_name), value);
        return true;
    }
    if (std::strcmp(key, "role") == 0) {
        copy_str(def.role, sizeof(def.role), value);
        return true;
    }
    if (std::strcmp(key, "mass_kg") == 0) {
        def.mass_kg = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "inertia") == 0) {
        return parse_vec3(value, def.inertia_diag);
    }
    if (std::strcmp(key, "hull_radius") == 0) {
        def.hull_radius = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "render_scale") == 0) {
        def.render_scale = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "main_thrust_n") == 0) {
        def.main_thrust_n = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "maneuver_thrust_n") == 0) {
        def.maneuver_thrust_n = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "retro_thrust_n") == 0) {
        def.retro_thrust_n = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "max_torque_nm") == 0) {
        def.max_torque_nm = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "hull_hp") == 0) {
        def.hull_hp = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "power_output") == 0) {
        def.power_output = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "power_capacity") == 0) {
        def.power_capacity = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "shield_capacity") == 0) {
        def.shield_capacity = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "shield_regen") == 0) {
        def.shield_regen = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "shield_power_draw") == 0) {
        def.shield_power_draw = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "subsystem_hp") == 0) {
        return parse_f32_list(value, def.subsystem_hp, combat::kSubsystemCount)
               == combat::kSubsystemCount;
    }
    if (std::strcmp(key, "cargo_volume") == 0) {
        def.cargo_volume = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "cargo_mass") == 0) {
        def.cargo_mass = std::strtof(value, nullptr);
        return true;
    }
    if (std::strcmp(key, "weapon_mounts") == 0) {
        const unsigned long n = std::strtoul(value, nullptr, 10);
        def.weapon_mount_count = (n > kMaxWeaponMounts) ? kMaxWeaponMounts : static_cast<u32>(n);
        return true;
    }
    if (std::strcmp(key, "turret_hardpoints") == 0) {
        const unsigned long n = std::strtoul(value, nullptr, 10);
        def.turret_hardpoint_count =
            (n > kMaxTurretHardpoints) ? kMaxTurretHardpoints : static_cast<u32>(n);
        return true;
    }
    if (indexed_key(key, "weapon_mount_", idx)) {
        if (idx >= kMaxWeaponMounts) {
            return false;
        }
        return parse_vec3(value, def.weapon_mount_offset[idx]);
    }
    if (indexed_key(key, "turret_hardpoint_", idx)) {
        if (idx >= kMaxTurretHardpoints) {
            return false;
        }
        return parse_vec3(value, def.turret_hardpoint_offset[idx]);
    }
    return false;
}

}  // namespace

bool load_ship_catalog_file(ShipCatalog& out, const char* path)
{
    out = ShipCatalog{};

    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        log::log_error(log::LogCategory::Config, "ship catalog missing: %s", path);
        return false;
    }

    bool    ok      = true;   ///< false = load rejected (dup / bad id / overflow)
    bool    in_ship = false;
    ShipDef current{};

    auto flush = [&]() {
        if (!in_ship) {
            return;
        }
        in_ship = false;

        if (current.id[0] == '\0') {
            log::log_error(
                log::LogCategory::Config, "%s: [[ship]] entry without an id", path);
            ok = false;
            return;
        }
        for (u32 i = 0; i < out.count; ++i) {
            if (std::strcmp(out.items[i].id, current.id) == 0) {
                log::log_error(
                    log::LogCategory::Config, "%s: duplicate ship id '%s'", path, current.id);
                ok = false;
                return;
            }
        }
        if (out.count >= kMaxShipDefs) {
            log::log_error(
                log::LogCategory::Config,
                "%s: more than %u ships (id '%s' dropped)",
                path,
                kMaxShipDefs,
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
        if (std::strcmp(trimmed, "[[ship]]") == 0) {
            flush();
            current = ShipDef{};
            in_ship = true;
            continue;
        }
        if (!in_ship) {
            log::log_warn(
                log::LogCategory::Config, "%s:%d: key outside a [[ship]] block", path, line_no);
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
        if (!apply_ship_kv(current, key, value)) {
            log::log_warn(
                log::LogCategory::Config, "%s:%d: bad key/value '%s'", path, line_no, key);
        }
    }
    flush();
    std::fclose(file);

    if (out.count == 0) {
        log::log_error(log::LogCategory::Config, "%s: no ships loaded", path);
        return false;
    }
    if (ok) {
        log::log_info(
            log::LogCategory::Config, "Loaded %u ships from %s", out.count, path);
    } else {
        log::log_error(
            log::LogCategory::Config,
            "%s: %u ships kept but the catalog is INVALID (see errors above)",
            path,
            out.count);
    }
    return ok;
}

bool load_ship_catalog(flecs::world& world)
{
    ShipCatalog catalog{};
    const bool  ok = load_ship_catalog_file(catalog, kShipCatalogPath);
    // Store whatever parsed cleanly so spawns can still resolve valid ids even
    // if one entry was rejected.
    world.set<ShipCatalog>(catalog);
    if (!ok) {
        log::log_error(
            log::LogCategory::Config,
            "ship catalog validation FAILED — spawns fall back to built-in defaults");
    }
    return ok;
}

const ShipDef* find_ship_def(const ShipCatalog& cat, const char* id)
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

const ShipDef* find_ship_def(flecs::world& world, const char* id)
{
    const ShipCatalog* cat = world.try_get<ShipCatalog>();
    return (cat != nullptr) ? find_ship_def(*cat, id) : nullptr;
}

}  // namespace csc::game::flight
