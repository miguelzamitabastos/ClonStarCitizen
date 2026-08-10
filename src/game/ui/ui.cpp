#include "game/ui/ui.hpp"

#include "engine/ecs/world.hpp"
#include "engine/log/log.hpp"
#include "game/audio/audio.hpp"
#include "game/character/character.hpp"
#include "game/economy/economy.hpp"
#include "game/flight/flight.hpp"
#include "game/save/save.hpp"
#include "game/world/world.hpp"

#ifndef CSC_DEBUG_UI
#define CSC_DEBUG_UI 0
#endif

#if CSC_DEBUG_UI
#include "imgui.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif

#include <cstdio>

namespace csc::game::ui {
namespace {

#if CSC_DEBUG_UI
void set_cursor_unlocked(GLFWwindow* window, bool unlocked)
{
    if (window == nullptr) {
        return;
    }
    if (unlocked) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        }
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
    }
}

void draw_flight_hud(flecs::world& world)
{
    f32  speed = 0.f;
    f32  energy = 0.f;
    f32  energy_cap = 0.f;
    f32  shield_pct = 0.f;
    f32  hull_hp = 0.f;
    f32  hull_max = 0.f;
    bool coupled = false;
    bool found = false;
    flight::fill_player_telemetry(
        world, speed, energy, energy_cap, shield_pct, hull_hp, hull_max, coupled, found);
    if (!found) {
        return;
    }

    char line[64]{};
    ImGui::SetNextWindowPos(ImVec2(12.f, 220.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.55f);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoTitleBar;
    if (ImGui::Begin("##FlightHUD", nullptr, flags)) {
        ImGui::TextUnformatted("FLIGHT");
        std::snprintf(line, sizeof(line), "SPD  %.0f m/s", static_cast<double>(speed));
        ImGui::TextUnformatted(line);
        std::snprintf(
            line,
            sizeof(line),
            "NRG  %.0f / %.0f",
            static_cast<double>(energy),
            static_cast<double>(energy_cap));
        ImGui::TextUnformatted(line);
        std::snprintf(
            line, sizeof(line), "SHD  %.0f%%", static_cast<double>(shield_pct * 100.f));
        ImGui::TextUnformatted(line);
        std::snprintf(
            line,
            sizeof(line),
            "HUL  %.0f / %.0f",
            static_cast<double>(hull_hp),
            static_cast<double>(hull_max));
        ImGui::TextUnformatted(line);
        std::snprintf(line, sizeof(line), "CPL  %s", coupled ? "ON" : "OFF");
        ImGui::TextUnformatted(line);
    }
    ImGui::End();
}

void draw_on_foot_hud(flecs::world& world)
{
    f32  health = 0.f;
    f32  health_max = 0.f;
    u32  ammo = 0;
    u32  ammo_max = 0;
    bool grounded = false;
    bool eva = false;
    bool found = false;
    character::fill_player_telemetry(
        world, health, health_max, ammo, ammo_max, grounded, eva, found);
    if (!found) {
        return;
    }

    char line[64]{};
    ImGui::SetNextWindowPos(ImVec2(12.f, 360.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.55f);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoTitleBar;
    if (ImGui::Begin("##OnFootHUD", nullptr, flags)) {
        ImGui::TextUnformatted("ON FOOT");
        std::snprintf(
            line,
            sizeof(line),
            "HP   %.0f / %.0f",
            static_cast<double>(health),
            static_cast<double>(health_max));
        ImGui::TextUnformatted(line);
        std::snprintf(line, sizeof(line), "AMMO %u / %u", ammo, ammo_max);
        ImGui::TextUnformatted(line);
        std::snprintf(
            line,
            sizeof(line),
            "%s%s",
            grounded ? "GROUNDED" : "AIR",
            eva ? " | EVA" : "");
        ImGui::TextUnformatted(line);

        if (const character::InteractionFocus* focus =
                world.try_get<character::InteractionFocus>()) {
            if (focus->target != 0 && focus->prompt[0] != '\0') {
                std::snprintf(line, sizeof(line), "[F] %s", focus->prompt);
                ImGui::Separator();
                ImGui::TextUnformatted(line);
            }
        }
    }
    ImGui::End();
}

void draw_system_map(flecs::world& world)
{
    const world::StarSystemData* sys = world.try_get<world::StarSystemData>();
    ImGui::SetNextWindowSize(ImVec2(360.f, 240.f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("System Map", nullptr, ImGuiWindowFlags_NoCollapse)) {
        if (sys == nullptr || sys->body_count == 0) {
            ImGui::TextUnformatted("(no star system data)");
        } else {
            ImGui::Text("System: %s", sys->system_name);
            ImGui::Separator();
            char line[96]{};
            for (u32 i = 0; i < sys->body_count; ++i) {
                const world::CelestialBody& b = sys->bodies[i];
                const char* type = "?";
                switch (b.type) {
                case world::CelestialBodyType::Star:
                    type = "Star";
                    break;
                case world::CelestialBodyType::Planet:
                    type = "Planet";
                    break;
                case world::CelestialBodyType::Station:
                    type = "Station";
                    break;
                case world::CelestialBodyType::LandingZone:
                    type = "LZ";
                    break;
                }
                std::snprintf(
                    line,
                    sizeof(line),
                    "%s  %s  (%.0f, %.0f, %.0f) r=%.0f",
                    type,
                    b.name,
                    static_cast<double>(b.position.x),
                    static_cast<double>(b.position.y),
                    static_cast<double>(b.position.z),
                    static_cast<double>(b.radius));
                ImGui::TextUnformatted(line);
            }
        }
    }
    ImGui::End();
}

void draw_cargo(flecs::world& world)
{
    ImGui::SetNextWindowSize(ImVec2(320.f, 220.f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Cargo / Inventory", nullptr, ImGuiWindowFlags_NoCollapse)) {
        const economy::PlayerWallet* wallet = world.try_get<economy::PlayerWallet>();
        char                         line[64]{};
        if (wallet != nullptr) {
            std::snprintf(line, sizeof(line), "Credits: %d", wallet->credits);
            ImGui::TextUnformatted(line);
        }

        const economy::CommodityTable* table = world.try_get<economy::CommodityTable>();
        economy::CargoHold*            hold  = nullptr;
        world.each([&](flecs::entity e, economy::CargoHold& h) {
            if (hold != nullptr) {
                return;
            }
            if (e.has<flight::PlayerShip>()) {
                hold = &h;
            }
        });
        if (hold == nullptr) {
            world.each([&](flecs::entity /*e*/, economy::CargoHold& h) {
                if (hold == nullptr) {
                    hold = &h;
                }
            });
        }

        ImGui::Separator();
        if (hold == nullptr) {
            ImGui::TextUnformatted("(no cargo hold)");
        } else {
            bool any = false;
            for (u32 i = 0; i < economy::kMaxCargoSlots; ++i) {
                const economy::CargoSlot& s = hold->slots[i];
                if (s.qty == 0) {
                    continue;
                }
                any              = true;
                const char* name = "item";
                if (table != nullptr) {
                    for (u32 c = 0; c < table->count; ++c) {
                        if (table->items[c].id == s.commodity_id) {
                            name = table->items[c].name;
                            break;
                        }
                    }
                }
                std::snprintf(line, sizeof(line), "%s x%u", name, s.qty);
                ImGui::TextUnformatted(line);
            }
            if (!any) {
                ImGui::TextUnformatted("(empty)");
            }
        }
    }
    ImGui::End();
}

void draw_mission_log(flecs::world& world)
{
    ImGui::SetNextWindowSize(ImVec2(360.f, 220.f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Mission Log", nullptr, ImGuiWindowFlags_NoCollapse)) {
        const economy::MissionActivePool* pool = world.try_get<economy::MissionActivePool>();
        const economy::MissionTemplateTable* templates =
            world.try_get<economy::MissionTemplateTable>();
        if (pool == nullptr || pool->pool.alive == 0) {
            ImGui::TextUnformatted("(no active missions)");
        } else {
            char line[96]{};
            for (std::size_t i = 0; i < economy::kMaxActiveMissions; ++i) {
                if (!pool->pool.is_active(i)) {
                    continue;
                }
                const economy::MissionActive& m = pool->pool.slots[i];
                const char*                   name = "Mission";
                if (templates != nullptr) {
                    for (u32 t = 0; t < templates->count; ++t) {
                        if (templates->items[t].id == m.template_id) {
                            name = templates->items[t].name;
                            break;
                        }
                    }
                }
                std::snprintf(
                    line,
                    sizeof(line),
                    "%s  %u/%u  reward %d  ->M%u",
                    name,
                    m.qty_delivered,
                    m.qty_required,
                    m.reward_credits,
                    m.to_market);
                ImGui::TextUnformatted(line);
            }
        }
    }
    ImGui::End();
}

void draw_pause_menu(flecs::world& world, UiMenuState& menus)
{
    ImGui::SetNextWindowPos(
        ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("PAUSED", nullptr, flags)) {
        ImGui::TextUnformatted("Simulation frozen");
        ImGui::Separator();

        auto click_ui = [&]() {
            if (audio::AudioEngineRef* ref = world.try_get_mut<audio::AudioEngineRef>()) {
                if (ref->engine != nullptr) {
                    (void)audio::play_ui(*ref->engine);
                }
            }
        };

        if (ImGui::Button("Resume (Esc)", ImVec2(200.f, 0.f))) {
            click_ui();
            menus.pause_open = false;
            world.set<SimulationPaused>({false});
        }
        if (ImGui::Button("System Map", ImVec2(200.f, 0.f))) {
            click_ui();
            menus.system_map_open = !menus.system_map_open;
        }
        if (ImGui::Button("Cargo / Inventory", ImVec2(200.f, 0.f))) {
            click_ui();
            menus.cargo_open = !menus.cargo_open;
        }
        if (ImGui::Button("Mission Log", ImVec2(200.f, 0.f))) {
            click_ui();
            menus.mission_log_open = !menus.mission_log_open;
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Save / Load (P1F)");
        if (ImGui::Button("Save Slot 0", ImVec2(200.f, 0.f))) {
            click_ui();
            (void)save::save_to_slot(world, save::SaveSlot::Slot0);
        }
        if (ImGui::Button("Load Slot 0", ImVec2(200.f, 0.f))) {
            click_ui();
            (void)save::load_from_slot(world, save::SaveSlot::Slot0);
        }
        if (ImGui::Button("Quicksave (F5)", ImVec2(200.f, 0.f))) {
            click_ui();
            (void)save::save_to_slot(world, save::SaveSlot::Quick);
        }
        if (ImGui::Button("Quickload (F9)", ImVec2(200.f, 0.f))) {
            click_ui();
            (void)save::load_from_slot(world, save::SaveSlot::Quick);
        }
        if (const save::SaveInProgress* sip = world.try_get<save::SaveInProgress>()) {
            if (sip->active) {
                ImGui::TextUnformatted("Saving…");
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("HUD mode");
        int mode = static_cast<int>(menus.hud_mode);
        if (ImGui::RadioButton("Auto", &mode, static_cast<int>(HudDisplayMode::Auto))) {
            menus.hud_mode = HudDisplayMode::Auto;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Flight", &mode, static_cast<int>(HudDisplayMode::Flight))) {
            menus.hud_mode = HudDisplayMode::Flight;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("OnFoot", &mode, static_cast<int>(HudDisplayMode::OnFoot))) {
            menus.hud_mode = HudDisplayMode::OnFoot;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Both", &mode, static_cast<int>(HudDisplayMode::Both))) {
            menus.hud_mode = HudDisplayMode::Both;
        }

        if (audio::AudioBuses* buses = world.try_get_mut<audio::AudioBuses>()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Audio buses");
            ImGui::SliderFloat(
                "SFX", &buses->volume[static_cast<u32>(audio::AudioCategory::Sfx)], 0.f, 1.f);
            ImGui::SliderFloat(
                "Music",
                &buses->volume[static_cast<u32>(audio::AudioCategory::Music)],
                0.f,
                1.f);
            ImGui::SliderFloat(
                "UI", &buses->volume[static_cast<u32>(audio::AudioCategory::Ui)], 0.f, 1.f);
        }

        if (audio::AudioEngineRef* ref = world.try_get_mut<audio::AudioEngineRef>()) {
            if (ref->engine != nullptr) {
                char line[48]{};
                std::snprintf(
                    line,
                    sizeof(line),
                    "Voices: %u / %u",
                    audio::active_voice_count(*ref->engine),
                    audio::kMaxConcurrentVoices);
                ImGui::TextUnformatted(line);
            }
        }
    }
    ImGui::End();
}
#endif  // CSC_DEBUG_UI

}  // namespace

void register_systems(flecs::world& world)
{
    ensure_singletons(world);
}

void ensure_singletons(flecs::world& world)
{
    if (world.try_get<SimulationPaused>() == nullptr) {
        world.set<SimulationPaused>({false});
    }
    if (world.try_get<UiMenuState>() == nullptr) {
        world.set<UiMenuState>(UiMenuState{});
    }
}

bool is_simulation_paused(const flecs::world& world)
{
    const SimulationPaused* p = world.try_get<SimulationPaused>();
    return p != nullptr && p->paused;
}

void frame_update(
    flecs::world&             world,
    const input::ActionState& actions,
    GLFWwindow*               window,
    bool&                     cursor_unlocked_out)
{
    ensure_singletons(world);
    UiMenuState* menus = world.try_get_mut<UiMenuState>();
    if (menus == nullptr) {
        cursor_unlocked_out = false;
        return;
    }

    if (actions.just_pressed[static_cast<u16>(input::Action::Pause)]) {
        menus->pause_open = !menus->pause_open;
        world.set<SimulationPaused>({menus->pause_open});
        if (!menus->pause_open) {
            menus->system_map_open  = false;
            menus->cargo_open       = false;
            menus->mission_log_open = false;
        }
        if (audio::AudioEngineRef* ref = world.try_get_mut<audio::AudioEngineRef>()) {
            if (ref->engine != nullptr) {
                (void)audio::play_ui(*ref->engine);
            }
        }
    }

    const bool want_cursor = menus->pause_open;
    cursor_unlocked_out    = want_cursor;
#if CSC_DEBUG_UI
    set_cursor_unlocked(window, want_cursor);
#else
    (void)window;
#endif
}

void frame_draw(flecs::world& world)
{
#if !CSC_DEBUG_UI
    (void)world;
#else
    ensure_singletons(world);
    UiMenuState* menus = world.try_get_mut<UiMenuState>();
    if (menus == nullptr) {
        return;
    }

    bool show_flight  = false;
    bool show_on_foot = false;
    switch (menus->hud_mode) {
    case HudDisplayMode::Flight:
        show_flight = true;
        break;
    case HudDisplayMode::OnFoot:
        show_on_foot = true;
        break;
    case HudDisplayMode::Both:
        show_flight  = true;
        show_on_foot = true;
        break;
    case HudDisplayMode::Auto:
    default: {
        const ecs::ControlMode* mode = world.try_get<ecs::ControlMode>();
        if (mode != nullptr && mode->mode == ecs::ControlModeKind::ShipPilot) {
            show_flight = true;
        } else if (mode != nullptr && mode->mode == ecs::ControlModeKind::OnFoot) {
            show_on_foot = true;
        } else {
            // FreeLook demos: show whatever telemetry exists.
            show_flight  = true;
            show_on_foot = true;
        }
        break;
    }
    }

    if (show_flight) {
        draw_flight_hud(world);
    }
    if (show_on_foot) {
        draw_on_foot_hud(world);
    }

    if (const save::SaveInProgress* sip = world.try_get<save::SaveInProgress>()) {
        if (sip->active) {
            ImGui::SetNextWindowPos(ImVec2(16.f, 16.f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.7f);
            if (ImGui::Begin(
                    "SaveStatus",
                    nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
                        | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav)) {
                ImGui::TextUnformatted("Saving…");
            }
            ImGui::End();
        }
    }

    if (menus->pause_open) {
        draw_pause_menu(world, *menus);
    }
    if (menus->system_map_open) {
        draw_system_map(world);
    }
    if (menus->cargo_open) {
        draw_cargo(world);
    }
    if (menus->mission_log_open) {
        draw_mission_log(world);
    }
#endif
}

}  // namespace csc::game::ui
