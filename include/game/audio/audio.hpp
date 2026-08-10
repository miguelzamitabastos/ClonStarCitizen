#pragma once

#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"

#include <flecs.h>

namespace csc::game::audio {

/// Fixed voice pool — never allocate beyond this at runtime (P1E-05).
inline constexpr u32 kMaxConcurrentVoices = 16;
inline constexpr u32 kMaxPcmSamples       = 48000; // ≤1s @ 48kHz mono scratch per clip
inline constexpr u32 kClipSampleRate      = 48000;

enum class AudioCategory : u8 {
    Sfx   = 0,
    Music = 1,
    Ui    = 2,
    Count
};

inline constexpr u32 kAudioCategoryCount = static_cast<u32>(AudioCategory::Count);

/// Built-in synthesized clips (no external WAV required for P1E).
enum class SoundId : u16 {
    Fire = 0,
    Thruster,
    Impact,
    UiClick,
    ToneNear,   // positional demo A
    ToneMid,    // positional demo B
    ToneFar,    // positional demo C
    Count
};

inline constexpr u16 kSoundCount = static_cast<u16>(SoundId::Count);

/// Higher value = more important; exhausted pool drops lowest-priority voice.
inline constexpr u8 kPriorityLow    = 16;
inline constexpr u8 kPriorityNormal = 64;
inline constexpr u8 kPriorityHigh   = 192;
inline constexpr u8 kPriorityUi     = 220;

struct SoundClip {
    f32 samples[kMaxPcmSamples]{};
    u32 sample_count = 0;
};

struct Voice {
    bool          active      = false;
    bool          looping     = false;
    bool          positional  = false;
    AudioCategory category    = AudioCategory::Sfx;
    u8            priority    = kPriorityNormal;
    SoundId       sound       = SoundId::Fire;
    f32           gain        = 1.f;
    glm::vec3     position{0.f}; // relative frame (floating-origin aware)
    u32           cursor      = 0;
    /// Sticky handle for looping emitters (0 = one-shot / unbound).
    u32           emitter_key = 0;
};

/// Flecs singleton — bus volumes + listener (relative coords, same frame as entities).
struct AudioBuses {
    f32 volume[kAudioCategoryCount]{1.f, 0.55f, 0.9f};
};

struct AudioListener {
    glm::vec3 position{0.f};
    glm::vec3 forward{0.f, 0.f, -1.f};
    glm::vec3 up{0.f, 1.f, 0.f};
};

/// Marker for demo / looping positional sources (P1E-07).
struct PositionalEmitter {
    SoundId sound      = SoundId::ToneNear;
    u8      priority   = kPriorityNormal;
    f32     gain       = 1.f;
    bool    looping    = true;
    u32     emitter_key = 0; // unique non-zero id assigned at spawn
};

/// Opaque engine state (device + clips + voices). Not a Flecs component —
/// owned by main; pointer stored in AudioEngineRef singleton for systems.
struct AudioEngine;

struct AudioEngineRef {
    AudioEngine* engine = nullptr;
};

void register_systems(flecs::world& world);

/// Level-load / init: open miniaudio device, synthesize clips into fixed buffers.
[[nodiscard]] bool audio_init(AudioEngine& engine);

void audio_shutdown(AudioEngine& engine);

[[nodiscard]] AudioEngine& audio_engine_storage();

/// Bind engine pointer into Flecs for fixed_step / frame hooks.
void audio_bind_world(flecs::world& world, AudioEngine& engine);

/// Fixed-step: gameplay cues (fire / thruster / impact) + keep positional loops alive.
void fixed_step(flecs::world& world, f32 dt);

/// Frame: sync listener from camera (relative eye) + bus attenuation uses same coords.
void frame_update(flecs::world& world, AudioEngine& engine);

/// Play one-shot (or restart) — drops lowest priority if pool full and request loses.
[[nodiscard]] bool play(
    AudioEngine&  engine,
    SoundId       sound,
    AudioCategory category,
    u8            priority,
    f32           gain,
    bool          positional,
    const glm::vec3& position);

[[nodiscard]] bool play_ui(AudioEngine& engine, SoundId sound = SoundId::UiClick);

/// Start / refresh a looping positional voice keyed by emitter_key.
[[nodiscard]] bool play_looped_positional(
    AudioEngine&     engine,
    u32              emitter_key,
    SoundId          sound,
    u8               priority,
    f32              gain,
    const glm::vec3& position);

void stop_emitter(AudioEngine& engine, u32 emitter_key);

void set_listener(
    AudioEngine&     engine,
    const glm::vec3& position,
    const glm::vec3& forward,
    const glm::vec3& up);

[[nodiscard]] u32 active_voice_count(const AudioEngine& engine);

}  // namespace csc::game::audio
