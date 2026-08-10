#include "game/save/save.hpp"

#include "engine/core/types.hpp"
#include "engine/ecs/world.hpp"
#include "engine/input/actions.hpp"
#include "engine/log/log.hpp"
#include "game/character/character.hpp"
#include "game/economy/economy.hpp"
#include "game/flight/flight.hpp"
#include "game/world/world.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <vector>

namespace csc::game::save {
namespace {

// --- Binary writer / reader (heap OK — outside frame loop) -------------------

struct Writer {
    std::vector<u8> buf;

    void write_bytes(const void* data, std::size_t n)
    {
        const auto* p = static_cast<const u8*>(data);
        buf.insert(buf.end(), p, p + n);
    }

    void write_u8(u8 v) { write_bytes(&v, 1); }
    void write_u32(u32 v) { write_bytes(&v, sizeof(v)); }
    void write_u64(u64 v) { write_bytes(&v, sizeof(v)); }
    void write_i32(i32 v) { write_bytes(&v, sizeof(v)); }
    void write_f32(f32 v) { write_bytes(&v, sizeof(v)); }

    void write_vec3(const glm::vec3& v)
    {
        write_f32(v.x);
        write_f32(v.y);
        write_f32(v.z);
    }

    void write_quat(const glm::quat& q)
    {
        write_f32(q.w);
        write_f32(q.x);
        write_f32(q.y);
        write_f32(q.z);
    }
};

struct Reader {
    const u8* data = nullptr;
    std::size_t size = 0;
    std::size_t pos  = 0;
    bool        ok   = true;

    [[nodiscard]] bool remain(std::size_t n) const { return ok && pos + n <= size; }

    bool read_bytes(void* out, std::size_t n)
    {
        if (!remain(n)) {
            ok = false;
            return false;
        }
        std::memcpy(out, data + pos, n);
        pos += n;
        return true;
    }

    bool read_u8(u8& v) { return read_bytes(&v, 1); }
    bool read_u32(u32& v) { return read_bytes(&v, sizeof(v)); }
    bool read_u64(u64& v) { return read_bytes(&v, sizeof(v)); }
    bool read_i32(i32& v) { return read_bytes(&v, sizeof(v)); }
    bool read_f32(f32& v) { return read_bytes(&v, sizeof(v)); }

    bool read_vec3(glm::vec3& v)
    {
        return read_f32(v.x) && read_f32(v.y) && read_f32(v.z);
    }

    bool read_quat(glm::quat& q)
    {
        return read_f32(q.w) && read_f32(q.x) && read_f32(q.y) && read_f32(q.z);
    }
};

void write_rb(Writer& w, const flight::RigidBody6DOF& rb)
{
    w.write_vec3(rb.position);
    w.write_quat(rb.orientation);
    w.write_vec3(rb.linear_vel);
    w.write_vec3(rb.angular_vel);
    w.write_f32(rb.mass);
    w.write_vec3(rb.inertia_diag);
}

bool read_rb(Reader& r, flight::RigidBody6DOF& rb)
{
    return r.read_vec3(rb.position) && r.read_quat(rb.orientation)
        && r.read_vec3(rb.linear_vel) && r.read_vec3(rb.angular_vel) && r.read_f32(rb.mass)
        && r.read_vec3(rb.inertia_diag);
}

void write_cargo(Writer& w, const economy::CargoHold& hold)
{
    w.write_u32(hold.slot_count);
    w.write_f32(hold.capacity_volume);
    w.write_f32(hold.capacity_mass);
    for (u32 i = 0; i < economy::kMaxCargoSlots; ++i) {
        w.write_u32(hold.slots[i].commodity_id);
        w.write_u32(hold.slots[i].qty);
    }
}

bool read_cargo(Reader& r, economy::CargoHold& hold)
{
    if (!r.read_u32(hold.slot_count) || !r.read_f32(hold.capacity_volume)
        || !r.read_f32(hold.capacity_mass)) {
        return false;
    }
    for (u32 i = 0; i < economy::kMaxCargoSlots; ++i) {
        if (!r.read_u32(hold.slots[i].commodity_id) || !r.read_u32(hold.slots[i].qty)) {
            return false;
        }
    }
    if (hold.slot_count > economy::kMaxCargoSlots) {
        hold.slot_count = economy::kMaxCargoSlots;
    }
    return true;
}

void write_mission(Writer& w, const economy::MissionActive& m)
{
    w.write_u32(m.template_id);
    w.write_u8(static_cast<u8>(m.type));
    w.write_u32(m.commodity_id);
    w.write_u32(m.qty_required);
    w.write_u32(m.qty_delivered);
    w.write_i32(m.reward_credits);
    w.write_u32(m.faction_id);
    w.write_f32(m.rep_delta);
    w.write_u32(m.from_market);
    w.write_u32(m.to_market);
    w.write_u32(m.location_id);
    w.write_u8(m.completed ? 1u : 0u);
}

bool read_mission(Reader& r, economy::MissionActive& m)
{
    u8 type = 0;
    u8 done = 0;
    if (!r.read_u32(m.template_id) || !r.read_u8(type) || !r.read_u32(m.commodity_id)
        || !r.read_u32(m.qty_required) || !r.read_u32(m.qty_delivered)
        || !r.read_i32(m.reward_credits) || !r.read_u32(m.faction_id)
        || !r.read_f32(m.rep_delta) || !r.read_u32(m.from_market) || !r.read_u32(m.to_market)
        || !r.read_u32(m.location_id) || !r.read_u8(done)) {
        return false;
    }
    m.type      = static_cast<economy::MissionType>(type);
    m.completed = done != 0;
    return true;
}

[[nodiscard]] flecs::entity find_by_persistent_id(flecs::world& world, u64 id)
{
    flecs::entity found{};
    if (id == 0) {
        return found;
    }
    world.each([&](flecs::entity e, const PersistentId& pid) {
        if (!found.is_alive() && pid.id == id) {
            found = e;
        }
    });
    return found;
}

[[nodiscard]] flecs::entity find_player_ship(flecs::world& world)
{
    flecs::entity found{};
    world.each([&](flecs::entity e, flight::PlayerShip) {
        if (!found.is_alive()) {
            found = e;
        }
    });
    return found;
}

[[nodiscard]] flecs::entity find_player_character(flecs::world& world)
{
    flecs::entity found{};
    world.each([&](flecs::entity e, character::PlayerCharacter) {
        if (!found.is_alive()) {
            found = e;
        }
    });
    return found;
}

struct SnapshotKey {
    i32       credits         = 0;
    u32       cargo_units     = 0;
    u32       active_missions = 0;
    f32       rep0            = 0.f;
    f32       ship_x          = 0.f;
    f32       ship_y          = 0.f;
    f32       ship_z          = 0.f;
    f32       hull_hp         = 0.f;
    f32       shield          = 0.f;
    f32       power_stored    = 0.f;
    u8        control_mode    = 0;
    u32       rebase_count    = 0;
    f32       origin_x        = 0.f;
    f32       char_hp         = 0.f;
    bool      char_attached   = false;
};

void capture_key(flecs::world& world, SnapshotKey& out)
{
    out = SnapshotKey{};
    if (const economy::PlayerWallet* w = world.try_get<economy::PlayerWallet>()) {
        out.credits = w->credits;
    }
    if (const economy::FactionReputation* r = world.try_get<economy::FactionReputation>()) {
        out.rep0 = r->values[0];
    }
    if (const economy::MissionActivePool* pool = world.try_get<economy::MissionActivePool>()) {
        out.active_missions = static_cast<u32>(pool->pool.alive);
    }
    if (const ecs::ControlMode* mode = world.try_get<ecs::ControlMode>()) {
        out.control_mode = static_cast<u8>(mode->mode);
    }
    if (const world::FloatingOrigin* fo = world.try_get<world::FloatingOrigin>()) {
        out.rebase_count = fo->rebase_count;
        out.origin_x     = fo->origin_offset.x;
    }

    flecs::entity ship = find_player_ship(world);
    if (ship.is_alive()) {
        if (const flight::RigidBody6DOF* rb = ship.try_get<flight::RigidBody6DOF>()) {
            out.ship_x = rb->position.x;
            out.ship_y = rb->position.y;
            out.ship_z = rb->position.z;
        }
        if (const flight::ShipHull* h = ship.try_get<flight::ShipHull>()) {
            out.hull_hp = h->hp;
        }
        if (const flight::ShieldGenerator* s = ship.try_get<flight::ShieldGenerator>()) {
            out.shield = s->current;
        }
        if (const flight::PowerPlant* p = ship.try_get<flight::PowerPlant>()) {
            out.power_stored = p->stored;
        }
        if (const economy::CargoHold* hold = ship.try_get<economy::CargoHold>()) {
            for (u32 i = 0; i < hold->slot_count && i < economy::kMaxCargoSlots; ++i) {
                out.cargo_units += hold->slots[i].qty;
            }
        }
    }

    flecs::entity ch = find_player_character(world);
    if (ch.is_alive()) {
        if (const character::Health* hp = ch.try_get<character::Health>()) {
            out.char_hp = hp->hp;
        }
        out.char_attached = ch.has<character::LocalToShip>();
    }
}

[[nodiscard]] bool keys_match(const SnapshotKey& a, const SnapshotKey& b)
{
    auto near = [](f32 x, f32 y) {
        const f32 d = x - y;
        return d < 0.01f && d > -0.01f;
    };
    return a.credits == b.credits && a.cargo_units == b.cargo_units
        && a.active_missions == b.active_missions && near(a.rep0, b.rep0)
        && near(a.ship_x, b.ship_x) && near(a.ship_y, b.ship_y) && near(a.ship_z, b.ship_z)
        && near(a.hull_hp, b.hull_hp) && near(a.shield, b.shield)
        && near(a.power_stored, b.power_stored) && a.control_mode == b.control_mode
        && a.rebase_count == b.rebase_count && near(a.origin_x, b.origin_x)
        && near(a.char_hp, b.char_hp) && a.char_attached == b.char_attached;
}

void set_in_progress(flecs::world& world, bool active)
{
    if (SaveInProgress* s = world.try_get_mut<SaveInProgress>()) {
        s->active = active;
    } else {
        world.set<SaveInProgress>({active});
    }
}

bool write_file(const char* path, const Writer& w)
{
    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        log::log_error(
            log::LogCategory::Game,
            "save: failed to open '%s' for write (errno=%d)",
            path,
            errno);
        return false;
    }
    const std::size_t n = w.buf.size();
    const std::size_t wrote =
        n == 0 ? 0 : std::fwrite(w.buf.data(), 1, n, f);
    const int cerr = std::fclose(f);
    if (wrote != n || cerr != 0) {
        log::log_error(log::LogCategory::Game, "save: write incomplete for '%s'", path);
        return false;
    }
    return true;
}

bool read_file(const char* path, std::vector<u8>& out)
{
    FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        log::log_error(
            log::LogCategory::Game,
            "load: failed to open '%s' (errno=%d)",
            path,
            errno);
        return false;
    }
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    const long sz = std::ftell(f);
    if (sz < 0) {
        std::fclose(f);
        return false;
    }
    if (std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        return false;
    }
    out.resize(static_cast<std::size_t>(sz));
    if (sz > 0) {
        const std::size_t got = std::fread(out.data(), 1, out.size(), f);
        if (got != out.size()) {
            std::fclose(f);
            log::log_error(log::LogCategory::Game, "load: short read '%s'", path);
            return false;
        }
    }
    std::fclose(f);
    return true;
}

bool serialize_world(flecs::world& world, Writer& w)
{
    w.write_u32(kSaveMagic);
    w.write_u32(kSaveSchemaVersion);

    // Control mode
    u8 mode = static_cast<u8>(ecs::ControlModeKind::FreeLook);
    if (const ecs::ControlMode* cm = world.try_get<ecs::ControlMode>()) {
        mode = static_cast<u8>(cm->mode);
    }
    w.write_u8(mode);

    // Floating origin
    world::FloatingOrigin fo{};
    if (const world::FloatingOrigin* p = world.try_get<world::FloatingOrigin>()) {
        fo = *p;
    }
    w.write_vec3(fo.origin_offset);
    w.write_u32(fo.rebase_count);
    w.write_f32(fo.threshold);

    // PersistentId counter
    u64 next_id = 1;
    if (const PersistentIdCounter* c = world.try_get<PersistentIdCounter>()) {
        next_id = c->next_id;
    }
    w.write_u64(next_id);

    // Wallet
    i32 credits = economy::kStartingCredits;
    if (const economy::PlayerWallet* wallet = world.try_get<economy::PlayerWallet>()) {
        credits = wallet->credits;
    }
    w.write_i32(credits);

    // Faction reputation
    economy::FactionReputation rep{};
    if (const economy::FactionReputation* r = world.try_get<economy::FactionReputation>()) {
        rep = *r;
    }
    for (u32 i = 0; i < economy::kNumFactions; ++i) {
        w.write_f32(rep.values[i]);
    }

    // Missions
    u32 rng_state = 1;
    u32 mission_count = 0;
    economy::MissionActive mission_scratch[economy::kMaxActiveMissions]{};
    bool mission_active[economy::kMaxActiveMissions]{};
    if (const economy::MissionActivePool* pool = world.try_get<economy::MissionActivePool>()) {
        rng_state = pool->rng_state;
        for (std::size_t i = 0; i < economy::kMaxActiveMissions; ++i) {
            if (pool->pool.is_active(i)) {
                mission_active[i]   = true;
                mission_scratch[i]  = pool->pool.slots[i];
                ++mission_count;
            }
        }
    }
    w.write_u32(rng_state);
    w.write_u32(mission_count);
    for (u32 i = 0; i < economy::kMaxActiveMissions; ++i) {
        if (!mission_active[i]) {
            continue;
        }
        w.write_u32(i);
        write_mission(w, mission_scratch[i]);
    }

    // Player ship
    flecs::entity ship = find_player_ship(world);
    const u8 has_ship  = ship.is_alive() ? 1u : 0u;
    w.write_u8(has_ship);
    if (has_ship) {
        const u64 pid = assign_persistent_id(world, ship);
        w.write_u64(pid);

        flight::RigidBody6DOF rb{};
        if (const flight::RigidBody6DOF* p = ship.try_get<flight::RigidBody6DOF>()) {
            rb = *p;
        }
        write_rb(w, rb);

        flight::ShipHull hull{};
        if (const flight::ShipHull* p = ship.try_get<flight::ShipHull>()) {
            hull = *p;
        }
        w.write_f32(hull.max_hp);
        w.write_f32(hull.hp);
        w.write_f32(hull.radius);

        flight::PowerPlant plant{};
        if (const flight::PowerPlant* p = ship.try_get<flight::PowerPlant>()) {
            plant = *p;
        }
        w.write_f32(plant.output_rate);
        w.write_f32(plant.capacity);
        w.write_f32(plant.stored);
        w.write_f32(plant.weight_thrusters);
        w.write_f32(plant.weight_shields);
        w.write_f32(plant.weight_weapons);

        flight::ShieldGenerator shield{};
        if (const flight::ShieldGenerator* p = ship.try_get<flight::ShieldGenerator>()) {
            shield = *p;
        }
        w.write_f32(shield.max_capacity);
        w.write_f32(shield.current);
        w.write_f32(shield.regen_rate);
        w.write_f32(shield.power_draw);

        u8 coupled = 1;
        if (const flight::FlightControl* p = ship.try_get<flight::FlightControl>()) {
            coupled = p->coupled ? 1u : 0u;
        }
        w.write_u8(coupled);

        economy::CargoHold hold{};
        if (const economy::CargoHold* p = ship.try_get<economy::CargoHold>()) {
            hold = *p;
        }
        write_cargo(w, hold);
    }

    // Player character
    flecs::entity ch = find_player_character(world);
    const u8 has_char = ch.is_alive() ? 1u : 0u;
    w.write_u8(has_char);
    if (has_char) {
        const u64 pid = assign_persistent_id(world, ch);
        w.write_u64(pid);

        character::Health hp{};
        if (const character::Health* p = ch.try_get<character::Health>()) {
            hp = *p;
        }
        w.write_f32(hp.max_hp);
        w.write_f32(hp.hp);

        character::CharacterController cc{};
        if (const character::CharacterController* p = ch.try_get<character::CharacterController>()) {
            cc = *p;
        }
        w.write_f32(cc.yaw);
        w.write_f32(cc.pitch);
        w.write_u8(cc.third_person ? 1u : 0u);
        w.write_vec3(cc.velocity);

        character::EquippedItem item{};
        if (const character::EquippedItem* p = ch.try_get<character::EquippedItem>()) {
            item = *p;
        }
        w.write_u32(item.item_id);
        w.write_u32(item.ammo);
        w.write_u32(item.ammo_max);

        const character::LocalToShip* local = ch.try_get<character::LocalToShip>();
        if (local != nullptr) {
            w.write_u8(1);
            u64 ship_pid = 0;
            flecs::entity ship_e = world.entity(local->ship_entity);
            if (ship_e.is_alive()) {
                if (const PersistentId* sp = ship_e.try_get<PersistentId>()) {
                    ship_pid = sp->id;
                } else {
                    ship_pid = assign_persistent_id(world, ship_e);
                }
            }
            w.write_u64(ship_pid);
            w.write_vec3(local->local_position);
            w.write_quat(local->local_orientation);
        } else {
            w.write_u8(0);
            glm::vec3 pos{0.f};
            if (const ecs::Position* p = ch.try_get<ecs::Position>()) {
                pos = {p->x, p->y, p->z};
            }
            w.write_vec3(pos);
        }
    }

    return true;
}

bool apply_loaded(flecs::world& world, Reader& r)
{
    u32 magic = 0;
    u32 version = 0;
    if (!r.read_u32(magic) || !r.read_u32(version)) {
        log::log_error(log::LogCategory::Game, "load: truncated header");
        return false;
    }
    if (magic != kSaveMagic) {
        log::log_error(
            log::LogCategory::Game,
            "load: bad magic 0x%08X (expected 0x%08X) — file rejected",
            magic,
            kSaveMagic);
        return false;
    }
    if (version != kSaveSchemaVersion) {
        log::log_error(
            log::LogCategory::Game,
            "load: unsupported schema version %u (engine expects %u) — rejected, "
            "world left unchanged",
            version,
            kSaveSchemaVersion);
        return false;
    }

    u8 mode = 0;
    if (!r.read_u8(mode)) {
        return false;
    }

    world::FloatingOrigin fo{};
    if (!r.read_vec3(fo.origin_offset) || !r.read_u32(fo.rebase_count)
        || !r.read_f32(fo.threshold)) {
        return false;
    }

    u64 next_id = 1;
    if (!r.read_u64(next_id)) {
        return false;
    }

    i32 credits = 0;
    if (!r.read_i32(credits)) {
        return false;
    }

    economy::FactionReputation rep{};
    for (u32 i = 0; i < economy::kNumFactions; ++i) {
        if (!r.read_f32(rep.values[i])) {
            return false;
        }
    }

    u32 rng_state = 1;
    u32 mission_count = 0;
    if (!r.read_u32(rng_state) || !r.read_u32(mission_count)) {
        return false;
    }
    if (mission_count > economy::kMaxActiveMissions) {
        log::log_error(log::LogCategory::Game, "load: mission_count too large");
        return false;
    }

    economy::MissionActive loaded_missions[economy::kMaxActiveMissions]{};
    u32 loaded_indices[economy::kMaxActiveMissions]{};
    for (u32 i = 0; i < mission_count; ++i) {
        u32 idx = 0;
        if (!r.read_u32(idx) || idx >= economy::kMaxActiveMissions
            || !read_mission(r, loaded_missions[i])) {
            return false;
        }
        loaded_indices[i] = idx;
    }

    u8 has_ship = 0;
    if (!r.read_u8(has_ship)) {
        return false;
    }

    u64 ship_pid = 0;
    flight::RigidBody6DOF ship_rb{};
    flight::ShipHull ship_hull{};
    flight::PowerPlant ship_plant{};
    flight::ShieldGenerator ship_shield{};
    u8 ship_coupled = 1;
    economy::CargoHold ship_cargo{};
    if (has_ship) {
        if (!r.read_u64(ship_pid) || !read_rb(r, ship_rb) || !r.read_f32(ship_hull.max_hp)
            || !r.read_f32(ship_hull.hp) || !r.read_f32(ship_hull.radius)
            || !r.read_f32(ship_plant.output_rate) || !r.read_f32(ship_plant.capacity)
            || !r.read_f32(ship_plant.stored) || !r.read_f32(ship_plant.weight_thrusters)
            || !r.read_f32(ship_plant.weight_shields) || !r.read_f32(ship_plant.weight_weapons)
            || !r.read_f32(ship_shield.max_capacity) || !r.read_f32(ship_shield.current)
            || !r.read_f32(ship_shield.regen_rate) || !r.read_f32(ship_shield.power_draw)
            || !r.read_u8(ship_coupled) || !read_cargo(r, ship_cargo)) {
            return false;
        }
    }

    u8 has_char = 0;
    if (!r.read_u8(has_char)) {
        return false;
    }

    u64 char_pid = 0;
    character::Health char_hp{};
    character::CharacterController char_cc{};
    character::EquippedItem char_item{};
    u8 char_attached = 0;
    u64 char_ship_pid = 0;
    glm::vec3 char_local_pos{0.f};
    glm::quat char_local_ori{1.f, 0.f, 0.f, 0.f};
    glm::vec3 char_world_pos{0.f};
    if (has_char) {
        u8 third = 0;
        if (!r.read_u64(char_pid) || !r.read_f32(char_hp.max_hp) || !r.read_f32(char_hp.hp)
            || !r.read_f32(char_cc.yaw) || !r.read_f32(char_cc.pitch) || !r.read_u8(third)
            || !r.read_vec3(char_cc.velocity) || !r.read_u32(char_item.item_id)
            || !r.read_u32(char_item.ammo) || !r.read_u32(char_item.ammo_max)
            || !r.read_u8(char_attached)) {
            return false;
        }
        char_cc.third_person = third != 0;
        if (char_attached) {
            if (!r.read_u64(char_ship_pid) || !r.read_vec3(char_local_pos)
                || !r.read_quat(char_local_ori)) {
                return false;
            }
        } else if (!r.read_vec3(char_world_pos)) {
            return false;
        }
    }

    if (!r.ok) {
        log::log_error(log::LogCategory::Game, "load: truncated or corrupt payload");
        return false;
    }

    // --- Apply (only after full parse succeeds) --------------------------------
    world.set<ecs::ControlMode>({static_cast<ecs::ControlModeKind>(mode)});
    world.set<world::FloatingOrigin>(fo);
    world.set<PersistentIdCounter>({next_id});
    world.set<economy::PlayerWallet>({credits});
    world.set<economy::FactionReputation>(rep);

    {
        economy::MissionActivePool pool{};
        pool.pool.init();
        pool.rng_state = rng_state;
        for (u32 i = 0; i < mission_count; ++i) {
            const u32 idx = loaded_indices[i];
            // Force-activate slot idx: Pool acquire order may differ — write directly.
            pool.pool.active[idx] = true;
            pool.pool.slots[idx]  = loaded_missions[i];
            ++pool.pool.alive;
            // Remove idx from free_stack if present (rebuild free list).
        }
        // Rebuild free_stack from inactive slots.
        pool.pool.free_count = 0;
        for (std::size_t i = 0; i < economy::kMaxActiveMissions; ++i) {
            if (!pool.pool.active[i]) {
                pool.pool.free_stack[pool.pool.free_count++] = i;
            }
        }
        world.set<economy::MissionActivePool>(pool);
    }

    if (has_ship) {
        flecs::entity ship = find_by_persistent_id(world, ship_pid);
        if (!ship.is_alive()) {
            ship = find_player_ship(world);
        }
        if (ship.is_alive()) {
            ship.set<PersistentId>({ship_pid});
            ship.set<flight::RigidBody6DOF>(ship_rb);
            ship.set<flight::ShipHull>(ship_hull);
            ship.set<flight::PowerPlant>(ship_plant);
            ship.set<flight::ShieldGenerator>(ship_shield);
            if (flight::FlightControl* fc = ship.try_get_mut<flight::FlightControl>()) {
                fc->coupled      = ship_coupled != 0;
                fc->thrust_input = glm::vec3{0.f};
                fc->torque_input = glm::vec3{0.f};
            }
            ship.set<economy::CargoHold>(ship_cargo);
            const ecs::Position pos{ship_rb.position.x, ship_rb.position.y, ship_rb.position.z};
            ship.set<ecs::Position>(pos);
            ship.set<ecs::PreviousPosition>({pos.x, pos.y, pos.z});
            ship.set<ecs::Orientation>({ship_rb.orientation});
            ship.set<ecs::Velocity>(
                {ship_rb.linear_vel.x, ship_rb.linear_vel.y, ship_rb.linear_vel.z});
        } else {
            log::log_warn(log::LogCategory::Game, "load: no player ship entity to restore");
        }
    }

    if (has_char) {
        flecs::entity ch = find_by_persistent_id(world, char_pid);
        if (!ch.is_alive()) {
            ch = find_player_character(world);
        }
        if (ch.is_alive()) {
            ch.set<PersistentId>({char_pid});
            if (character::Health* hp = ch.try_get_mut<character::Health>()) {
                hp->max_hp = char_hp.max_hp;
                hp->hp     = char_hp.hp;
            } else {
                ch.set<character::Health>(char_hp);
            }
            if (character::CharacterController* cc =
                    ch.try_get_mut<character::CharacterController>()) {
                cc->yaw          = char_cc.yaw;
                cc->pitch        = char_cc.pitch;
                cc->third_person = char_cc.third_person;
                cc->velocity     = char_cc.velocity;
            }
            if (character::EquippedItem* item = ch.try_get_mut<character::EquippedItem>()) {
                item->item_id  = char_item.item_id;
                item->ammo     = char_item.ammo;
                item->ammo_max = char_item.ammo_max;
            }

            if (char_attached) {
                flecs::entity ship_e = find_by_persistent_id(world, char_ship_pid);
                if (!ship_e.is_alive()) {
                    ship_e = find_player_ship(world);
                }
                character::LocalToShip local{};
                local.ship_entity       = ship_e.is_alive() ? ship_e.id() : 0;
                local.local_position    = char_local_pos;
                local.local_orientation = char_local_ori;
                ch.set<character::LocalToShip>(local);

                if (ship_e.is_alive()) {
                    if (const flight::RigidBody6DOF* rb =
                            ship_e.try_get<flight::RigidBody6DOF>()) {
                        const glm::vec3 wpos =
                            rb->position + rb->orientation * char_local_pos;
                        const glm::quat wori = rb->orientation * char_local_ori;
                        ch.set<ecs::Position>({wpos.x, wpos.y, wpos.z});
                        ch.set<ecs::PreviousPosition>({wpos.x, wpos.y, wpos.z});
                        ch.set<ecs::Orientation>({wori});
                    }
                }
            } else {
                if (ch.has<character::LocalToShip>()) {
                    ch.remove<character::LocalToShip>();
                }
                ch.set<ecs::Position>(
                    {char_world_pos.x, char_world_pos.y, char_world_pos.z});
                ch.set<ecs::PreviousPosition>(
                    {char_world_pos.x, char_world_pos.y, char_world_pos.z});
            }
        } else {
            log::log_warn(log::LogCategory::Game, "load: no player character to restore");
        }
    }

    log::log_info(
        log::LogCategory::Game,
        "load: applied schema v%u (credits=%d missions=%u ship=%d char=%d)",
        kSaveSchemaVersion,
        credits,
        mission_count,
        static_cast<int>(has_ship),
        static_cast<int>(has_char));
    return true;
}

}  // namespace

const char* slot_path(SaveSlot slot)
{
    switch (slot) {
    case SaveSlot::Autosave:
        return kAutosavePath;
    case SaveSlot::Quick:
        return kQuicksavePath;
    case SaveSlot::Slot0:
    default:
        return kSlot0Path;
    }
}

void register_systems(flecs::world& world)
{
    ensure_singletons(world);
}

void ensure_singletons(flecs::world& world)
{
    if (world.try_get<SaveInProgress>() == nullptr) {
        world.set<SaveInProgress>({false});
    }
    if (world.try_get<PersistentIdCounter>() == nullptr) {
        world.set<PersistentIdCounter>({1});
    }
}

u64 assign_persistent_id(flecs::world& world, flecs::entity e)
{
    ensure_singletons(world);
    if (const PersistentId* existing = e.try_get<PersistentId>()) {
        if (existing->id != 0) {
            return existing->id;
        }
    }
    PersistentIdCounter* counter = world.try_get_mut<PersistentIdCounter>();
    if (counter == nullptr) {
        world.set<PersistentIdCounter>({1});
        counter = world.try_get_mut<PersistentIdCounter>();
    }
    const u64 id = counter->next_id++;
    e.set<PersistentId>({id});
    return id;
}

bool ensure_save_directory()
{
    struct stat st {};
    if (stat(kSaveDir, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true;
        }
        log::log_error(log::LogCategory::Game, "save: '%s' exists but is not a directory", kSaveDir);
        return false;
    }
    if (mkdir(kSaveDir, 0755) != 0 && errno != EEXIST) {
        log::log_error(
            log::LogCategory::Game,
            "save: mkdir '%s' failed (errno=%d)",
            kSaveDir,
            errno);
        return false;
    }
    return true;
}

bool save_game(flecs::world& world, const char* path)
{
    if (path == nullptr || path[0] == '\0') {
        log::log_error(log::LogCategory::Game, "save: empty path");
        return false;
    }
    ensure_singletons(world);
    if (const SaveInProgress* sip = world.try_get<SaveInProgress>();
        sip != nullptr && sip->active) {
        log::log_warn(log::LogCategory::Game, "save: already in progress — ignored");
        return false;
    }

    set_in_progress(world, true);
    if (!ensure_save_directory()) {
        set_in_progress(world, false);
        return false;
    }

    Writer w;
    w.buf.reserve(4096);
    if (!serialize_world(world, w) || !write_file(path, w)) {
        set_in_progress(world, false);
        return false;
    }

    set_in_progress(world, false);
    log::log_info(
        log::LogCategory::Game,
        "save: wrote '%s' (%zu bytes, schema v%u)",
        path,
        w.buf.size(),
        kSaveSchemaVersion);
    return true;
}

bool load_game(flecs::world& world, const char* path)
{
    if (path == nullptr || path[0] == '\0') {
        log::log_error(log::LogCategory::Game, "load: empty path");
        return false;
    }
    ensure_singletons(world);
    if (const SaveInProgress* sip = world.try_get<SaveInProgress>();
        sip != nullptr && sip->active) {
        log::log_warn(log::LogCategory::Game, "load: save already in progress — ignored");
        return false;
    }

    set_in_progress(world, true);

    std::vector<u8> bytes;
    if (!read_file(path, bytes)) {
        set_in_progress(world, false);
        return false;
    }

    Reader r{};
    r.data = bytes.data();
    r.size = bytes.size();
    const bool ok = apply_loaded(world, r);
    set_in_progress(world, false);
    if (!ok) {
        // apply_loaded already logged; world unchanged if header rejected early,
        // or partially applied only after full parse — parse failures return before apply.
        return false;
    }
    log::log_info(log::LogCategory::Game, "load: success from '%s'", path);
    return true;
}

bool save_to_slot(flecs::world& world, SaveSlot slot)
{
    return save_game(world, slot_path(slot));
}

bool load_from_slot(flecs::world& world, SaveSlot slot)
{
    return load_game(world, slot_path(slot));
}

void frame_poll(flecs::world& world, const input::ActionState& actions)
{
    ensure_singletons(world);
    if (const SaveInProgress* sip = world.try_get<SaveInProgress>();
        sip != nullptr && sip->active) {
        return;
    }

    if (actions.just_pressed[static_cast<u16>(input::Action::QuickSave)]) {
        if (save_to_slot(world, SaveSlot::Quick)) {
            log::log_info(log::LogCategory::Game, "QuickSave (F5) → %s", kQuicksavePath);
        }
    }
    if (actions.just_pressed[static_cast<u16>(input::Action::QuickLoad)]) {
        if (load_from_slot(world, SaveSlot::Quick)) {
            log::log_info(log::LogCategory::Game, "QuickLoad (F9) ← %s", kQuicksavePath);
        }
    }
}

bool run_smoke_test(flecs::world& world)
{
    log::log_info(log::LogCategory::Game, "CSC_SAVE_SMOKE: begin");

    SnapshotKey before{};
    capture_key(world, before);

    if (!save_game(world, kSmokeSavePath)) {
        log::log_error(log::LogCategory::Game, "CSC_SAVE_SMOKE: FAIL (save)");
        return false;
    }

    // Mutate so a no-op load would fail the comparison.
    if (economy::PlayerWallet* w = world.try_get_mut<economy::PlayerWallet>()) {
        w->credits = before.credits + 9999;
    }
    if (economy::FactionReputation* r = world.try_get_mut<economy::FactionReputation>()) {
        r->values[0] = before.rep0 + 50.f;
    }
    flecs::entity ship = find_player_ship(world);
    if (ship.is_alive()) {
        if (flight::RigidBody6DOF* rb = ship.try_get_mut<flight::RigidBody6DOF>()) {
            rb->position.x += 123.f;
            rb->position.z += 456.f;
        }
        if (flight::ShipHull* h = ship.try_get_mut<flight::ShipHull>()) {
            h->hp = 1.f;
        }
    }

    if (!load_game(world, kSmokeSavePath)) {
        log::log_error(log::LogCategory::Game, "CSC_SAVE_SMOKE: FAIL (load)");
        return false;
    }

    SnapshotKey after{};
    capture_key(world, after);
    if (!keys_match(before, after)) {
        log::log_error(
            log::LogCategory::Game,
            "CSC_SAVE_SMOKE: FAIL (mismatch credits %d→%d cargo %u→%u missions %u→%u "
            "rep0 %.2f→%.2f ship(%.1f,%.1f,%.1f)→(%.1f,%.1f,%.1f) hull %.0f→%.0f)",
            before.credits,
            after.credits,
            before.cargo_units,
            after.cargo_units,
            before.active_missions,
            after.active_missions,
            static_cast<double>(before.rep0),
            static_cast<double>(after.rep0),
            static_cast<double>(before.ship_x),
            static_cast<double>(before.ship_y),
            static_cast<double>(before.ship_z),
            static_cast<double>(after.ship_x),
            static_cast<double>(after.ship_y),
            static_cast<double>(after.ship_z),
            static_cast<double>(before.hull_hp),
            static_cast<double>(after.hull_hp));
        return false;
    }

    // Unknown schema version must reject without crash.
    {
        Writer bad;
        bad.write_u32(kSaveMagic);
        bad.write_u32(kSaveSchemaVersion + 99u);
        bad.write_u8(0);
        if (!ensure_save_directory() || !write_file("saves/bad_version.sav", bad)) {
            log::log_error(log::LogCategory::Game, "CSC_SAVE_SMOKE: FAIL (write bad version)");
            return false;
        }
        SnapshotKey guard{};
        capture_key(world, guard);
        const bool rejected = !load_game(world, "saves/bad_version.sav");
        SnapshotKey guard_after{};
        capture_key(world, guard_after);
        if (!rejected || !keys_match(guard, guard_after)) {
            log::log_error(
                log::LogCategory::Game,
                "CSC_SAVE_SMOKE: FAIL (unknown version not rejected cleanly)");
            return false;
        }
        log::log_info(
            log::LogCategory::Game,
            "CSC_SAVE_SMOKE: unknown version rejected safely");
    }

    log::log_info(log::LogCategory::Game, "CSC_SAVE_SMOKE: PASS");
    return true;
}

}  // namespace csc::game::save
