#pragma once

#include <array>
#include <cmath>
#include <string>

// A full snapshot of every control's value — every voice's five macros,
// enabled state, per-parameter Evolution toggles, and loaded-sample path,
// plus the global Tempo/Evolution/Reverb/Delay/Volume controls.
// Deliberately excludes transport run/stop state (isPlaying_ isn't a
// "control", it's transient session state) — a Scene is "how the
// instrument is set up", not "whether it's currently making sound". Plain
// C++, JUCE-free, same convention as MidiBinding — Main.cpp is the only
// place that reads/writes this against the live
// DrumVoiceModel/DrumEvolutionEngine/atomics. Ported from Jerrican's
// SceneState.h.
struct VoiceSceneState {
    bool enabled = true;
    float volume = 0.0f;
    float tone = 0.5f;
    float motion = 0.0f;
    float density = 0.0f;
    float chaos = 0.0f;
    bool volumeEvoEnabled = true;
    bool toneEvoEnabled = true;
    bool motionEvoEnabled = true;
    bool densityEvoEnabled = true;
    bool chaosEvoEnabled = true;
    // Absolute path of a loaded WAV/AIFF/FLAC/MP3/OGG replacing this
    // voice's procedural default, or empty to mean "using the default".
    std::string samplePath;
};

struct SceneState {
    std::array<VoiceSceneState, 8> voices;
    float tempo = 120.0f;
    float evolutionAmount = 0.0f;
    float evolutionSpeed = 0.5f;
    float space = 1.0f;
    float reverbRoom = 0.0f;
    float reverbDecay = 0.0f;
    float delayBeatFraction = 0.5f;
    float delayFeedback = 0.0f;
    float masterVolume = 1.0f;
};

namespace SceneStateDetail {
inline constexpr float kFloatEpsilon = 1e-4f;
inline bool nearlyEqual(float a, float b) { return std::abs(a - b) < kFloatEpsilon; }
}  // namespace SceneStateDetail

// Used by PresetControls to detect dirty/matching state, the same way
// MidiBindingManager::equals() is for bindings. Floats are compared with
// a small epsilon rather than bit-for-bit, since round-tripping through
// text (ScenePresetStore's save/load) isn't guaranteed to be exact.
inline bool operator==(const VoiceSceneState& a, const VoiceSceneState& b) {
    using SceneStateDetail::nearlyEqual;
    return a.enabled == b.enabled && nearlyEqual(a.volume, b.volume) &&
           nearlyEqual(a.tone, b.tone) && nearlyEqual(a.motion, b.motion) &&
           nearlyEqual(a.density, b.density) && nearlyEqual(a.chaos, b.chaos) &&
           a.volumeEvoEnabled == b.volumeEvoEnabled && a.toneEvoEnabled == b.toneEvoEnabled &&
           a.motionEvoEnabled == b.motionEvoEnabled && a.densityEvoEnabled == b.densityEvoEnabled &&
           a.chaosEvoEnabled == b.chaosEvoEnabled && a.samplePath == b.samplePath;
}

inline bool operator==(const SceneState& a, const SceneState& b) {
    using SceneStateDetail::nearlyEqual;
    for (std::size_t i = 0; i < a.voices.size(); ++i) {
        if (!(a.voices[i] == b.voices[i])) {
            return false;
        }
    }
    return nearlyEqual(a.tempo, b.tempo) && nearlyEqual(a.evolutionAmount, b.evolutionAmount) &&
           nearlyEqual(a.evolutionSpeed, b.evolutionSpeed) && nearlyEqual(a.space, b.space) &&
           nearlyEqual(a.reverbRoom, b.reverbRoom) && nearlyEqual(a.reverbDecay, b.reverbDecay) &&
           nearlyEqual(a.delayBeatFraction, b.delayBeatFraction) &&
           nearlyEqual(a.delayFeedback, b.delayFeedback) &&
           nearlyEqual(a.masterVolume, b.masterVolume);
}
