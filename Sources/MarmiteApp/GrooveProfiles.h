#pragma once

#include <array>
#include <cstddef>

// Pure data: one 16-slot (one bar of 16th notes) accent-weight profile
// per default voice archetype, used by GroovePattern in Set mode. A
// weight near 1.0 means "this voice naturally wants to land here"; near
// 0 means "essentially never, unless Wild pushes the profile's influence
// down." At effectiveWild=0 these fully govern which slots a voice can
// fire on (a legible, steady groove); at effectiveWild=1 they're ignored
// entirely (uniform, busy, unconstrained). See GroovePattern.h.
//
// Order matches the established 8-voice kit ordering used throughout the
// engine (MarmiteProcessor.h's kInitialVoices, ProceduralKit::
// makeDefaultKit): Kick, Snare, Clap, Closed Hat, Open Hat, Perc, Crash,
// Glitch. Slot indices: 0=beat 1, 4=beat 2, 8=beat 3, 12=beat 4 (the "e/&/a"
// subdivisions fall in between). These starting weights are a first pass,
// meant to be tuned by ear rather than treated as final.
namespace GrooveProfiles {

inline constexpr std::size_t kSlotsPerBar = 16;
using AccentProfile = std::array<float, kSlotsPerBar>;

// Kick: rock-solid on 1 and 3, with a light syncopated push before 3.
inline constexpr AccentProfile kKick{
    1.0f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.15f, 0.05f,
    1.0f, 0.05f, 0.10f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f,
};

// Snare: classic backbeat on 2 and 4, with a faint ghost note ahead of 4.
inline constexpr AccentProfile kSnare{
    0.05f, 0.05f, 0.05f, 0.05f, 1.0f, 0.05f, 0.05f, 0.05f,
    0.05f, 0.05f, 0.05f, 0.05f, 1.0f, 0.15f, 0.05f, 0.05f,
};

// Clap: doubles the backbeat but with its own secondary accent, so it
// isn't a pure copy of Snare when both are active.
inline constexpr AccentProfile kClap{
    0.05f, 0.05f, 0.05f, 0.05f, 0.9f, 0.05f, 0.05f, 0.05f,
    0.05f, 0.05f, 0.20f, 0.05f, 0.9f, 0.05f, 0.05f, 0.05f,
};

// Closed Hat: steady 8th-note pulse (every even slot), light 16th
// activity in between.
inline constexpr AccentProfile kClosedHat{
    0.6f, 0.10f, 0.6f, 0.10f, 0.6f, 0.10f, 0.6f, 0.10f,
    0.6f, 0.10f, 0.6f, 0.10f, 0.6f, 0.10f, 0.6f, 0.10f,
};

// Open Hat: sparser, accents the off-beats ("and" of each beat) to
// complement the Closed Hat's on-beat pulse.
inline constexpr AccentProfile kOpenHat{
    0.05f, 0.05f, 0.5f, 0.05f, 0.05f, 0.05f, 0.5f, 0.05f,
    0.05f, 0.05f, 0.5f, 0.05f, 0.05f, 0.05f, 0.5f, 0.05f,
};

// Perc: a free-roaming fill voice — low-moderate weight everywhere,
// slightly favoring the syncopated 16th slots over the strong beats
// (which Kick/Snare already own).
inline constexpr AccentProfile kPerc{
    0.2f, 0.35f, 0.2f, 0.35f, 0.2f, 0.35f, 0.2f, 0.35f,
    0.2f, 0.35f, 0.2f, 0.35f, 0.2f, 0.35f, 0.2f, 0.35f,
};

// Crash: a rare downbeat-only accent, marking the top of the phrase.
inline constexpr AccentProfile kCrash{
    1.0f, 0.02f, 0.02f, 0.02f, 0.02f, 0.02f, 0.02f, 0.02f,
    0.02f, 0.02f, 0.02f, 0.02f, 0.02f, 0.02f, 0.02f, 0.02f,
};

// Glitch: already the "wild by identity" voice (its Free-mode default
// Chaos is far higher than the rest of the kit) — kept close to flat
// even at effectiveWild=0, so it reads as a bit noisier/unpredictable
// than the rest of the kit even in a mostly-ACDC room.
inline constexpr AccentProfile kGlitch{
    0.45f, 0.4f, 0.45f, 0.4f, 0.45f, 0.4f, 0.45f, 0.4f,
    0.45f, 0.4f, 0.45f, 0.4f, 0.45f, 0.4f, 0.45f, 0.4f,
};

// Positional array, index matches the 8-voice kit order exactly.
inline constexpr std::array<const AccentProfile*, 8> kByVoiceIndex{
    &kKick, &kSnare, &kClap, &kClosedHat, &kOpenHat, &kPerc, &kCrash, &kGlitch,
};

}  // namespace GrooveProfiles
