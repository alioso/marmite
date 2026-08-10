#pragma once

#include <array>
#include <cstddef>

// Meter structure and per-voice accent-weight generation for
// GroovePattern's Set-mode grid. A weight near 1.0 means "this voice
// naturally wants to land here"; near 0 means "essentially never, unless
// Wild pushes the profile's influence down." At effectiveWild=0 these
// fully govern which slots a voice can fire on (a legible, steady
// groove); at effectiveWild=1 they're ignored entirely (uniform, busy,
// unconstrained). See GroovePattern.h.
//
// Voice order matches the established 8-voice kit ordering used
// throughout the engine (MarmiteProcessor.h's kInitialVoices,
// ProceduralKit::makeDefaultKit): Kick, Snare, Clap, Closed Hat, Open
// Hat, Perc, Crash, Glitch.
//
// Rather than one hand-authored 16-slot table per voice per meter (7
// meters x 8 voices = 56 tables to keep in sync by ear), each meter
// supplies its "pulse grouping" — how its bar divides into natural
// pulses, in 16th-note slots (e.g. 7/8's 2+2+3 eighths = 4+4+6
// sixteenths) — and generateProfile() derives each voice's character
// from that grouping: Kick on group downbeats, Snare/Clap on the
// backbeat groups, hats broad/off-beat, Crash on the true bar downbeat
// only, Perc/Glitch meter-agnostic and flat. This keeps every voice's
// personality consistent across all 7 supported meters without hand
// re-tuning each one.
namespace GrooveProfiles {

inline constexpr std::size_t kMaxSlotsPerBar = 24;  // 12/8, the longest supported bar
inline constexpr std::size_t kMaxPulseGroups = 5;   // 5/4's 5 quarter-note pulses
using AccentProfile = std::array<float, kMaxSlotsPerBar>;

struct MeterDef {
    const char* label;
    int numerator;
    int denominator;
    int totalSlots;
    std::array<int, kMaxPulseGroups> groupLengths;  // in 16th-note slots; only first groupCount entries valid
    int groupCount;
};

// slots = numerator * 16 / denominator. Groupings: 4/4 and 3/4 split
// evenly into quarter-note pulses; 5/4 groups as 3+2 quarters
// ("long-short", e.g. Mission Impossible); 6/8 and 12/8 split into
// dotted-quarter pulses; 7/8 groups as 2+2+3 eighths; 9/8 as an even
// 3+3+3 compound triple.
inline constexpr std::array<MeterDef, 7> kMeters{{
    {"4/4", 4, 4, 16, {4, 4, 4, 4, 0}, 4},
    {"3/4", 3, 4, 12, {4, 4, 4, 0, 0}, 3},
    {"5/4", 5, 4, 20, {12, 8, 0, 0, 0}, 2},
    {"6/8", 6, 8, 12, {6, 6, 0, 0, 0}, 2},
    {"7/8", 7, 8, 14, {4, 4, 6, 0, 0}, 3},
    {"9/8", 9, 8, 18, {6, 6, 6, 0, 0}, 3},
    {"12/8", 12, 8, 24, {6, 6, 6, 6, 0}, 4},
}};

inline constexpr int kDefaultMeterIndex = 0;  // 4/4

inline int findMeterIndex(int numerator, int denominator) {
    for (std::size_t i = 0; i < kMeters.size(); ++i) {
        if (kMeters[i].numerator == numerator && kMeters[i].denominator == denominator) {
            return static_cast<int>(i);
        }
    }
    return kDefaultMeterIndex;
}

namespace detail {

inline std::array<int, kMaxPulseGroups> groupStarts(const MeterDef& meter) {
    std::array<int, kMaxPulseGroups> starts{};
    int running = 0;
    for (int g = 0; g < meter.groupCount; ++g) {
        starts[static_cast<std::size_t>(g)] = running;
        running += meter.groupLengths[static_cast<std::size_t>(g)];
    }
    return starts;
}

}  // namespace detail

// voiceIndex order: 0=Kick, 1=Snare, 2=Clap, 3=ClosedHat, 4=OpenHat,
// 5=Perc, 6=Crash, 7=Glitch. Only entries [0, meter.totalSlots) of the
// returned profile are meaningful; trailing slots are left at baseline
// and never read by GroovePattern.
inline AccentProfile generateProfile(int voiceIndex, const MeterDef& meter) {
    AccentProfile profile{};
    const auto starts = detail::groupStarts(meter);
    const int n = meter.totalSlots;

    switch (voiceIndex) {
        case 0: {  // Kick: every group's downbeat, a light syncopated push near the next group.
            for (int s = 0; s < n; ++s) profile[static_cast<std::size_t>(s)] = 0.05f;
            for (int g = 0; g < meter.groupCount; ++g) {
                const int start = starts[static_cast<std::size_t>(g)];
                const int len = meter.groupLengths[static_cast<std::size_t>(g)];
                profile[static_cast<std::size_t>(start)] = 1.0f;
                const int pushSlot = len >= 4 ? start + len - 2 : start + len - 1;
                if (pushSlot < n) profile[static_cast<std::size_t>(pushSlot)] = len >= 4 ? 0.15f : 0.10f;
            }
            break;
        }
        case 1:    // Snare
        case 2: {  // Clap: same backbeat shape, slightly different weights/ghost.
            const bool isClap = voiceIndex == 2;
            for (int s = 0; s < n; ++s) profile[static_cast<std::size_t>(s)] = 0.05f;
            if (meter.groupCount <= 1) {
                profile[static_cast<std::size_t>(n / 2)] = isClap ? 0.9f : 1.0f;
            } else {
                for (int g = 1; g < meter.groupCount; g += 2) {
                    profile[static_cast<std::size_t>(starts[static_cast<std::size_t>(g)])] =
                        isClap ? 0.9f : 1.0f;
                }
            }
            const int ghostSlot = n - 2;
            if (ghostSlot >= 0) profile[static_cast<std::size_t>(ghostSlot)] = isClap ? 0.20f : 0.15f;
            break;
        }
        case 3: {  // Closed Hat: steady alternating pulse, meter-agnostic.
            for (int s = 0; s < n; ++s) profile[static_cast<std::size_t>(s)] = (s % 2 == 0) ? 0.6f : 0.10f;
            break;
        }
        case 4: {  // Open Hat: the "and" (midpoint) of each pulse group.
            for (int s = 0; s < n; ++s) profile[static_cast<std::size_t>(s)] = 0.05f;
            for (int g = 0; g < meter.groupCount; ++g) {
                const int start = starts[static_cast<std::size_t>(g)];
                const int len = meter.groupLengths[static_cast<std::size_t>(g)];
                profile[static_cast<std::size_t>(start + len / 2)] = 0.5f;
            }
            break;
        }
        case 5: {  // Perc: flat alternating, meter-agnostic.
            for (int s = 0; s < n; ++s) profile[static_cast<std::size_t>(s)] = (s % 2 == 0) ? 0.2f : 0.35f;
            break;
        }
        case 6: {  // Crash: true bar downbeat only, meter-agnostic.
            for (int s = 0; s < n; ++s) profile[static_cast<std::size_t>(s)] = 0.02f;
            profile[0] = 1.0f;
            break;
        }
        default: {  // Glitch: flat, already-noisy-by-identity, meter-agnostic.
            for (int s = 0; s < n; ++s) profile[static_cast<std::size_t>(s)] = (s % 2 == 0) ? 0.45f : 0.4f;
            break;
        }
    }
    return profile;
}

}  // namespace GrooveProfiles
