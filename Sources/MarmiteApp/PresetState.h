#pragma once

#include <array>
#include <cmath>
#include <string>

// A full snapshot of every control's value — every voice's five macros,
// enabled state, per-parameter Evolution toggles, and loaded-sample path,
// plus the global Tempo/Evolution/Reverb/Delay/Volume controls.
// Deliberately excludes transport run/stop state (isPlaying_ isn't a
// "control", it's transient session state) — a Preset is "how the
// instrument is set up", not "whether it's currently making sound". Plain
// C++, JUCE-free, same convention as MidiBinding — Main.cpp is the only
// place that reads/writes this against the live
// DrumVoiceModel/DrumEvolutionEngine/atomics. Ported from Jerrican's
// PresetState.h.
struct VoicePresetState {
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

struct PresetState {
    std::array<VoicePresetState, 8> voices;
    float tempo = 120.0f;
    float evolutionAmount = 0.0f;
    float evolutionSpeed = 0.5f;
    float space = 1.0f;
    float reverbRoom = 0.0f;
    float reverbDecay = 0.0f;
    float delayBeatFraction = 0.5f;
    float delayFeedback = 0.0f;
    float masterVolume = 1.0f;
    // Global baseline on the ACDC(0)/Jungle(0.5)/Squarepusher(1) groove-
    // complexity curve — see GroovePattern.h. Each voice's Busy knob
    // offsets around this baseline.
    float wild = 0.0f;
    // Selected time signature — see GrooveProfiles::MeterDef. Stored as an
    // explicit numerator/denominator pair rather than a list index, so
    // reordering the meter table later can't corrupt a saved Preset.
    int meterNumerator = 4;
    int meterDenominator = 4;
};

namespace PresetStateDetail {
inline constexpr float kFloatEpsilon = 1e-4f;
inline bool nearlyEqual(float a, float b) { return std::abs(a - b) < kFloatEpsilon; }
}  // namespace PresetStateDetail

// Used by PresetControls to detect dirty/matching state, the same way
// MidiBindingManager::equals() is for bindings. Floats are compared with
// a small epsilon rather than bit-for-bit, since round-tripping through
// text (PresetStore's save/load) isn't guaranteed to be exact.
inline bool operator==(const VoicePresetState& a, const VoicePresetState& b) {
    using PresetStateDetail::nearlyEqual;
    return a.enabled == b.enabled && nearlyEqual(a.volume, b.volume) &&
           nearlyEqual(a.tone, b.tone) && nearlyEqual(a.motion, b.motion) &&
           nearlyEqual(a.density, b.density) && nearlyEqual(a.chaos, b.chaos) &&
           a.volumeEvoEnabled == b.volumeEvoEnabled && a.toneEvoEnabled == b.toneEvoEnabled &&
           a.motionEvoEnabled == b.motionEvoEnabled && a.densityEvoEnabled == b.densityEvoEnabled &&
           a.chaosEvoEnabled == b.chaosEvoEnabled && a.samplePath == b.samplePath;
}

inline bool operator==(const PresetState& a, const PresetState& b) {
    using PresetStateDetail::nearlyEqual;
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
           nearlyEqual(a.masterVolume, b.masterVolume) && nearlyEqual(a.wild, b.wild) &&
           a.meterNumerator == b.meterNumerator && a.meterDenominator == b.meterDenominator;
}

// Like operator==, but this is what the Presets popup actually uses to
// decide whether to show Override — a saved preset with Evolution Amount
// > 0 keeps drifting its own macros forever by design (that's the whole
// point of Evolution), so a bit-exact compare would show Override within
// moments of loading the very preset it's comparing against, permanently.
// A per-voice macro only counts as diverged if either Evolution isn't
// currently driving it (evolutionAmount is 0) or its own EvoEnabled
// toggle is off (pinned under manual control); Space and Wild have no
// per-field toggle of their own — they drift whenever evolutionAmount >
// 0, full stop — so they're excluded from the compare on that condition
// alone. Genuinely changing a pinned macro, or any of the other fields
// below, still shows Override as before.
inline bool matchesIgnoringEvolutionDrift(const PresetState& saved, const PresetState& live) {
    using PresetStateDetail::nearlyEqual;
    const bool evolving = live.evolutionAmount > 0.0f;
    auto valueMatches = [evolving](bool evoEnabled, float a, float b) {
        return (evolving && evoEnabled) || nearlyEqual(a, b);
    };
    for (std::size_t i = 0; i < saved.voices.size(); ++i) {
        const auto& a = saved.voices[i];
        const auto& b = live.voices[i];
        if (a.enabled != b.enabled || a.samplePath != b.samplePath) {
            return false;
        }
        if (a.volumeEvoEnabled != b.volumeEvoEnabled || a.toneEvoEnabled != b.toneEvoEnabled ||
            a.motionEvoEnabled != b.motionEvoEnabled || a.densityEvoEnabled != b.densityEvoEnabled ||
            a.chaosEvoEnabled != b.chaosEvoEnabled) {
            return false;
        }
        if (!valueMatches(a.volumeEvoEnabled, a.volume, b.volume) ||
            !valueMatches(a.toneEvoEnabled, a.tone, b.tone) ||
            !valueMatches(a.motionEvoEnabled, a.motion, b.motion) ||
            !valueMatches(a.densityEvoEnabled, a.density, b.density) ||
            !valueMatches(a.chaosEvoEnabled, a.chaos, b.chaos)) {
            return false;
        }
    }
    return nearlyEqual(saved.tempo, live.tempo) &&
           nearlyEqual(saved.evolutionAmount, live.evolutionAmount) &&
           nearlyEqual(saved.evolutionSpeed, live.evolutionSpeed) &&
           (evolving || nearlyEqual(saved.space, live.space)) &&
           nearlyEqual(saved.reverbRoom, live.reverbRoom) &&
           nearlyEqual(saved.reverbDecay, live.reverbDecay) &&
           nearlyEqual(saved.delayBeatFraction, live.delayBeatFraction) &&
           nearlyEqual(saved.delayFeedback, live.delayFeedback) &&
           nearlyEqual(saved.masterVolume, live.masterVolume) &&
           (evolving || nearlyEqual(saved.wild, live.wild)) &&
           saved.meterNumerator == live.meterNumerator && saved.meterDenominator == live.meterDenominator;
}
