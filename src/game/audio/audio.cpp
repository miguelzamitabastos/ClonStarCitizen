#include "game/audio/audio.hpp"

#include "engine/ecs/world.hpp"
#include "engine/input/actions.hpp"
#include "engine/log/log.hpp"
#include "game/flight/flight.hpp"

#include <cmath>
#include <cstring>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace csc::game::audio {

struct AudioEngine {
    ma_device     device{};
    bool          ready = false;
    SoundClip     clips[kSoundCount]{};
    Voice         voices[kMaxConcurrentVoices]{};
    AudioBuses    buses{};
    AudioListener listener{};
    f32           prev_ship_integrity = -1.f;
};

namespace {

constexpr f32 kRefDistance = 4.f;
constexpr f32 kMaxDistance = 120.f;
constexpr f32 kTwoPi       = 6.28318530718f;

// Unique sticky keys for system loops (avoid colliding with PositionalEmitter keys ≥100).
constexpr u32 kKeyThrusterLoop = 1u;

AudioEngine g_engine_storage{};

[[nodiscard]] f32 clampf(f32 v, f32 lo, f32 hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void fill_sine(SoundClip& clip, f32 freq_hz, f32 duration_s, f32 amp, f32 fade_frac)
{
    const u32 n = static_cast<u32>(
        clampf(duration_s, 0.01f, 1.f) * static_cast<f32>(kClipSampleRate));
    clip.sample_count = n < kMaxPcmSamples ? n : kMaxPcmSamples;
    const f32 fade_n  = static_cast<f32>(clip.sample_count) * fade_frac;
    for (u32 i = 0; i < clip.sample_count; ++i) {
        const f32 t   = static_cast<f32>(i) / static_cast<f32>(kClipSampleRate);
        f32       env = 1.f;
        if (fade_n > 1.f) {
            if (static_cast<f32>(i) < fade_n) {
                env = static_cast<f32>(i) / fade_n;
            } else if (static_cast<f32>(i) > static_cast<f32>(clip.sample_count) - fade_n) {
                env = static_cast<f32>(clip.sample_count - i) / fade_n;
            }
        }
        clip.samples[i] = amp * env * std::sin(kTwoPi * freq_hz * t);
    }
}

void fill_noise_burst(SoundClip& clip, f32 duration_s, f32 amp)
{
    const u32 n = static_cast<u32>(
        clampf(duration_s, 0.01f, 1.f) * static_cast<f32>(kClipSampleRate));
    clip.sample_count = n < kMaxPcmSamples ? n : kMaxPcmSamples;
    u32 rng           = 0xC0FFEEu;
    for (u32 i = 0; i < clip.sample_count; ++i) {
        rng             = rng * 1664525u + 1013904223u;
        const f32 white = (static_cast<f32>(rng >> 8) / 16777215.f) * 2.f - 1.f;
        const f32 env =
            1.f - (static_cast<f32>(i) / static_cast<f32>(clip.sample_count));
        clip.samples[i] = amp * env * env * white;
    }
}

void fill_thruster_loop(SoundClip& clip)
{
    fill_sine(clip, 55.f, 0.25f, 0.22f, 0.05f);
    SoundClip over{};
    fill_sine(over, 110.f, 0.25f, 0.08f, 0.05f);
    const u32 n =
        clip.sample_count < over.sample_count ? clip.sample_count : over.sample_count;
    for (u32 i = 0; i < n; ++i) {
        clip.samples[i] += over.samples[i];
    }
}

void synthesize_all_clips(AudioEngine& engine)
{
    fill_noise_burst(engine.clips[static_cast<u16>(SoundId::Fire)], 0.08f, 0.55f);
    fill_thruster_loop(engine.clips[static_cast<u16>(SoundId::Thruster)]);
    fill_sine(engine.clips[static_cast<u16>(SoundId::Impact)], 90.f, 0.18f, 0.7f, 0.2f);
    fill_sine(engine.clips[static_cast<u16>(SoundId::UiClick)], 880.f, 0.04f, 0.35f, 0.35f);
    fill_sine(engine.clips[static_cast<u16>(SoundId::ToneNear)], 440.f, 0.5f, 0.4f, 0.02f);
    fill_sine(engine.clips[static_cast<u16>(SoundId::ToneMid)], 330.f, 0.5f, 0.4f, 0.02f);
    fill_sine(engine.clips[static_cast<u16>(SoundId::ToneFar)], 220.f, 0.5f, 0.4f, 0.02f);
}

[[nodiscard]] f32 distance_gain(f32 distance)
{
    if (distance <= kRefDistance) {
        return 1.f;
    }
    if (distance >= kMaxDistance) {
        return 0.f;
    }
    const f32 t = (distance - kRefDistance) / (kMaxDistance - kRefDistance);
    return clampf(1.f - t, 0.f, 1.f);
}

[[nodiscard]] f32 stereo_pan(const AudioEngine& engine, const glm::vec3& pos)
{
    const glm::vec3 to = pos - engine.listener.position;
    const f32       d2 = glm::dot(to, to);
    if (d2 < 1e-6f) {
        return 0.f;
    }
    const glm::vec3 dir = to * (1.f / std::sqrt(d2));
    const glm::vec3 right =
        glm::normalize(glm::cross(engine.listener.forward, engine.listener.up));
    return clampf(glm::dot(dir, right), -1.f, 1.f);
}

void mix_voices(AudioEngine& engine, f32* out_interleaved, ma_uint32 frame_count)
{
    std::memset(out_interleaved, 0, sizeof(f32) * frame_count * 2u);

    for (u32 vi = 0; vi < kMaxConcurrentVoices; ++vi) {
        Voice& v = engine.voices[vi];
        if (!v.active) {
            continue;
        }
        const SoundClip& clip = engine.clips[static_cast<u16>(v.sound)];
        if (clip.sample_count == 0) {
            v.active = false;
            continue;
        }

        f32 atten = 1.f;
        f32 pan   = 0.f;
        if (v.positional) {
            const f32 dist = glm::length(v.position - engine.listener.position);
            atten          = distance_gain(dist);
            pan            = stereo_pan(engine, v.position);
        }

        const u32 cat  = static_cast<u32>(v.category);
        const f32 bus  = (cat < kAudioCategoryCount) ? engine.buses.volume[cat] : 1.f;
        const f32 gain = v.gain * bus * atten;

        const f32 gain_l = gain * (1.f - pan) * 0.5f;
        const f32 gain_r = gain * (1.f + pan) * 0.5f;

        for (ma_uint32 f = 0; f < frame_count; ++f) {
            if (v.cursor >= clip.sample_count) {
                if (v.looping) {
                    v.cursor = 0;
                } else {
                    v.active = false;
                    break;
                }
            }
            const f32 s = clip.samples[v.cursor++];
            out_interleaved[f * 2u + 0u] += s * gain_l;
            out_interleaved[f * 2u + 1u] += s * gain_r;
        }
    }

    for (ma_uint32 i = 0; i < frame_count * 2u; ++i) {
        out_interleaved[i] = clampf(out_interleaved[i], -1.f, 1.f);
    }
}

void data_callback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frame_count)
{
    auto* engine = static_cast<AudioEngine*>(device->pUserData);
    if (engine == nullptr) {
        std::memset(output, 0, sizeof(f32) * frame_count * 2u);
        return;
    }
    mix_voices(*engine, static_cast<f32*>(output), frame_count);
}

[[nodiscard]] i32 find_voice_slot(AudioEngine& engine, u8 priority)
{
    for (u32 i = 0; i < kMaxConcurrentVoices; ++i) {
        if (!engine.voices[i].active) {
            return static_cast<i32>(i);
        }
    }
    u32 worst_i = 0;
    u8  worst_p = 255;
    for (u32 i = 0; i < kMaxConcurrentVoices; ++i) {
        if (engine.voices[i].priority < worst_p) {
            worst_p = engine.voices[i].priority;
            worst_i = i;
        }
    }
    if (priority > worst_p) {
        engine.voices[worst_i].active = false;
        return static_cast<i32>(worst_i);
    }
    return -1;
}

[[nodiscard]] glm::vec3 player_or_camera_pos(flecs::world& world)
{
    glm::vec3 pos{0.f};
    bool      found = false;
    world.each([&](flecs::entity e, const flight::RigidBody6DOF& rb) {
        if (found || !e.has<flight::PlayerShip>()) {
            return;
        }
        pos   = rb.position;
        found = true;
    });
    if (found) {
        return pos;
    }
    ecs::Camera3D cam{};
    if (ecs::world_try_get_primary_camera(world, cam)) {
        return cam.eye;
    }
    return pos;
}

}  // namespace

AudioEngine& audio_engine_storage()
{
    return g_engine_storage;
}

bool audio_init(AudioEngine& engine)
{
    engine = AudioEngine{};
    synthesize_all_clips(engine);
    engine.buses = AudioBuses{};

    ma_device_config config  = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate        = kClipSampleRate;
    config.dataCallback      = data_callback;
    config.pUserData         = &engine;

    if (ma_device_init(nullptr, &config, &engine.device) != MA_SUCCESS) {
        log::log_warn(log::LogCategory::Game, "miniaudio: device init failed (no playback).");
        engine.ready = false;
        return false;
    }
    if (ma_device_start(&engine.device) != MA_SUCCESS) {
        log::log_warn(log::LogCategory::Game, "miniaudio: device start failed.");
        ma_device_uninit(&engine.device);
        engine.ready = false;
        return false;
    }

    engine.ready = true;
    log::log_info(
        log::LogCategory::Game,
        "AudioEngine ready — voices=%u clips=%u (miniaudio)",
        kMaxConcurrentVoices,
        static_cast<u32>(kSoundCount));
    return true;
}

void audio_shutdown(AudioEngine& engine)
{
    if (engine.ready) {
        ma_device_uninit(&engine.device);
    }
    engine = AudioEngine{};
}

void audio_bind_world(flecs::world& world, AudioEngine& engine)
{
    world.set<AudioEngineRef>(AudioEngineRef{&engine});
    world.set<AudioBuses>(engine.buses);
    world.set<AudioListener>(engine.listener);
}

void register_systems(flecs::world& /*world*/)
{
    // Audio is driven from fixed_step / frame_update (no Flecs OnUpdate system).
}

bool play(
    AudioEngine&     engine,
    SoundId          sound,
    AudioCategory    category,
    u8               priority,
    f32              gain,
    bool             positional,
    const glm::vec3& position)
{
    if (!engine.ready || sound >= SoundId::Count) {
        return false;
    }
    const i32 slot = find_voice_slot(engine, priority);
    if (slot < 0) {
        return false;
    }
    Voice& v      = engine.voices[static_cast<u32>(slot)];
    v             = Voice{};
    v.active      = true;
    v.looping     = false;
    v.positional  = positional;
    v.category    = category;
    v.priority    = priority;
    v.sound       = sound;
    v.gain        = gain;
    v.position    = position;
    v.cursor      = 0;
    v.emitter_key = 0;
    return true;
}

bool play_ui(AudioEngine& engine, SoundId sound)
{
    return play(engine, sound, AudioCategory::Ui, kPriorityUi, 1.f, false, {});
}

bool play_looped_positional(
    AudioEngine&     engine,
    u32              emitter_key,
    SoundId          sound,
    u8               priority,
    f32              gain,
    const glm::vec3& position)
{
    if (!engine.ready || emitter_key == 0 || sound >= SoundId::Count) {
        return false;
    }
    for (u32 i = 0; i < kMaxConcurrentVoices; ++i) {
        Voice& v = engine.voices[i];
        if (v.active && v.emitter_key == emitter_key) {
            v.position = position;
            v.gain     = gain;
            return true;
        }
    }
    const i32 slot = find_voice_slot(engine, priority);
    if (slot < 0) {
        return false;
    }
    Voice& v      = engine.voices[static_cast<u32>(slot)];
    v             = Voice{};
    v.active      = true;
    v.looping     = true;
    v.positional  = true;
    v.category    = AudioCategory::Sfx;
    v.priority    = priority;
    v.sound       = sound;
    v.gain        = gain;
    v.position    = position;
    v.cursor      = 0;
    v.emitter_key = emitter_key;
    return true;
}

void stop_emitter(AudioEngine& engine, u32 emitter_key)
{
    if (emitter_key == 0) {
        return;
    }
    for (u32 i = 0; i < kMaxConcurrentVoices; ++i) {
        if (engine.voices[i].emitter_key == emitter_key) {
            engine.voices[i].active = false;
        }
    }
}

void set_listener(
    AudioEngine&     engine,
    const glm::vec3& position,
    const glm::vec3& forward,
    const glm::vec3& up)
{
    engine.listener.position = position;
    const f32 fl             = glm::length(forward);
    engine.listener.forward =
        (fl > 1e-6f) ? (forward / fl) : glm::vec3{0.f, 0.f, -1.f};
    const f32 ul       = glm::length(up);
    engine.listener.up = (ul > 1e-6f) ? (up / ul) : glm::vec3{0.f, 1.f, 0.f};
}

u32 active_voice_count(const AudioEngine& engine)
{
    u32 n = 0;
    for (u32 i = 0; i < kMaxConcurrentVoices; ++i) {
        if (engine.voices[i].active) {
            ++n;
        }
    }
    return n;
}

void fixed_step(flecs::world& world, f32 /*dt*/)
{
    AudioEngineRef* ref = world.try_get_mut<AudioEngineRef>();
    if (ref == nullptr || ref->engine == nullptr || !ref->engine->ready) {
        return;
    }
    AudioEngine& engine = *ref->engine;

    const ecs::InputActions* in  = world.try_get<ecs::InputActions>();
    const glm::vec3          pos = player_or_camera_pos(world);

    if (in != nullptr
        && in->state.just_pressed[static_cast<u16>(input::Action::Fire)]) {
        (void)play(
            engine, SoundId::Fire, AudioCategory::Sfx, kPriorityHigh, 0.85f, true, pos);
    }

    const ecs::ControlMode* mode = world.try_get<ecs::ControlMode>();
    const bool thrusting =
        in != nullptr && in->state.pressed[static_cast<u16>(input::Action::Thrust)]
        && mode != nullptr && mode->mode == ecs::ControlModeKind::ShipPilot;
    if (thrusting) {
        (void)play_looped_positional(
            engine, kKeyThrusterLoop, SoundId::Thruster, kPriorityNormal, 0.45f, pos);
    } else {
        stop_emitter(engine, kKeyThrusterLoop);
    }

    world.each([&](flecs::entity e, const flight::ShipHull& hull,
                   const flight::ShieldGenerator& shield) {
        if (!e.has<flight::PlayerShip>()) {
            return;
        }
        const f32 integrity =
            (hull.max_hp > 0.f ? hull.hp / hull.max_hp : 0.f)
            + (shield.max_capacity > 0.f ? shield.current / shield.max_capacity : 0.f);
        if (engine.prev_ship_integrity >= 0.f
            && integrity < engine.prev_ship_integrity - 0.001f) {
            glm::vec3 emit = pos;
            if (const flight::RigidBody6DOF* rb = e.try_get<flight::RigidBody6DOF>()) {
                emit = rb->position;
            }
            (void)play(
                engine, SoundId::Impact, AudioCategory::Sfx, kPriorityHigh, 1.f, true, emit);
        }
        engine.prev_ship_integrity = integrity;
    });

    world.each([&](flecs::entity e, const PositionalEmitter& em, const ecs::Position& p) {
        if (!em.looping || em.emitter_key == 0) {
            return;
        }
        (void)e;
        (void)play_looped_positional(
            engine,
            em.emitter_key,
            em.sound,
            em.priority,
            em.gain,
            glm::vec3{p.x, p.y, p.z});
    });

    if (const AudioBuses* buses = world.try_get<AudioBuses>()) {
        engine.buses = *buses;
    }
}

void frame_update(flecs::world& world, AudioEngine& engine)
{
    if (!engine.ready) {
        return;
    }
    ecs::Camera3D cam{};
    if (!ecs::world_try_get_primary_camera(world, cam)) {
        return;
    }
    const glm::vec3 forward = glm::normalize(cam.target - cam.eye);
    set_listener(engine, cam.eye, forward, glm::vec3{0.f, 1.f, 0.f});
    world.set<AudioListener>(engine.listener);

    world.each([&](flecs::entity /*e*/, const PositionalEmitter& em, const ecs::Position& p) {
        if (!em.looping || em.emitter_key == 0) {
            return;
        }
        (void)play_looped_positional(
            engine,
            em.emitter_key,
            em.sound,
            em.priority,
            em.gain,
            glm::vec3{p.x, p.y, p.z});
    });
}

}  // namespace csc::game::audio
