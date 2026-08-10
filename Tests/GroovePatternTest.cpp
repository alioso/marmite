#include <array>
#include <cassert>
#include <cmath>
#include <iostream>

#include "GroovePattern.h"
#include "GrooveProfiles.h"

namespace {

// Small, deterministic samplesPerSubdivision so any delayed trigger (up
// to half a subdivision at effectiveWild=1) resolves within a handful of
// flush calls, rather than needing to simulate real per-sample audio.
constexpr double kSamplesPerSubdivision = 100.0;
// Most tests below exercise a plain 16-slot (4/4-equivalent) bar; the
// profile array itself is always GrooveProfiles::kMaxSlotsPerBar-sized
// now (the largest supported meter, 12/8), with only the first kSlots
// entries meaningful here.
constexpr int kSlots = 16;

// Ticks one grid boundary at `slot`, then flushes enough non-boundary
// calls to resolve any pending delayed trigger (see GroovePattern's
// half-subdivision-max delay, scaled by effectiveWild) before reporting
// whether this slot fired at all this bar.
bool tickSlotAndCheckFired(GroovePattern& pattern, int slot, float density, float wild, float motion,
                          float evolutionAmount) {
    if (pattern.update(true, slot, density, wild, motion, evolutionAmount, kSamplesPerSubdivision)
            .has_value()) {
        return true;
    }
    for (int i = 0; i < 64; ++i) {
        if (pattern
                .update(false, slot, density, wild, motion, evolutionAmount, kSamplesPerSubdivision)
                .has_value()) {
            return true;
        }
    }
    return false;
}

// Runs one full kSlots-slot bar, returning which slots fired.
std::array<bool, kSlots> runBar(GroovePattern& pattern, float density, float wild, float motion,
                                float evolutionAmount) {
    std::array<bool, kSlots> fired{};
    for (int slot = 0; slot < kSlots; ++slot) {
        fired[static_cast<std::size_t>(slot)] =
            tickSlotAndCheckFired(pattern, slot, density, wild, motion, evolutionAmount);
    }
    return fired;
}

}  // namespace

int main() {
    // A slot with accentProfile weight exactly 0 has slotWeight = 0 at
    // effectiveWild=0 (slotWeight = profileWeight + (1-profileWeight)*wild
    // = 0), so its fire probability is exactly 0 regardless of density or
    // how many bars/mutations run — deterministic, not statistical.
    {
        GrooveProfiles::AccentProfile profile{};
        profile.fill(0.0f);
        profile[3] = 1.0f;  // one active slot so the pattern isn't silent
        GroovePattern pattern(0x1234u, &profile, kSlots);

        bool everFiredAtZeroSlot = false;
        for (int bar = 0; bar < 20; ++bar) {
            const auto fired = runBar(pattern, /*density=*/1.0f, /*wild=*/0.0f, /*motion=*/0.0f,
                                      /*evolutionAmount=*/1.0f);
            if (fired[0]) {
                everFiredAtZeroSlot = true;
            }
        }
        assert(!everFiredAtZeroSlot);
    }

    // A slot with accentProfile weight exactly 1 has slotWeight = 1
    // regardless of wild, so at density=1 it fires deterministically
    // every bar once activated (the very first bar's mask is always
    // force-regenerated, even with evolutionAmount=0).
    {
        GrooveProfiles::AccentProfile profile{};
        profile.fill(1.0f);
        GroovePattern pattern(0x5678u, &profile, kSlots);

        for (int bar = 0; bar < 20; ++bar) {
            const auto fired = runBar(pattern, /*density=*/1.0f, /*wild=*/0.0f, /*motion=*/0.0f,
                                      /*evolutionAmount=*/1.0f);
            for (bool slotFired : fired) {
                assert(slotFired);
            }
        }
    }

    // evolutionAmount=0 still leaves a small nonzero baseline mutation
    // chance per bar (so a pattern can never get permanently stuck on one
    // unlucky initial roll), but with density/wild held fixed across
    // bars, an all-0-or-1 profile keeps the *outcome* deterministic
    // regardless of whether a slot happens to reroll that bar (weight=1
    // slots activate with probability 1, weight=0 with probability 0,
    // whether freshly rolled or not) — so the fired-slot pattern must
    // still reproduce exactly every bar even with mutation happening
    // under the hood.
    {
        GrooveProfiles::AccentProfile profile{};
        profile.fill(0.0f);
        profile[2] = 1.0f;
        profile[9] = 1.0f;
        GroovePattern pattern(0x9abcu, &profile, kSlots);

        const auto firstBar = runBar(pattern, /*density=*/1.0f, /*wild=*/0.0f, /*motion=*/0.0f,
                                     /*evolutionAmount=*/0.0f);
        for (int bar = 0; bar < 50; ++bar) {
            const auto laterBar = runBar(pattern, /*density=*/1.0f, /*wild=*/0.0f, /*motion=*/0.0f,
                                         /*evolutionAmount=*/0.0f);
            assert(laterBar == firstBar);
        }
    }

    // Regression test for the bug this was built to fix: a genuine
    // change to Density between bars must take effect immediately (the
    // very next bar), even with evolutionAmount=0 — turning a knob can't
    // be a no-op just because Evolution isn't drifting anything.
    {
        GrooveProfiles::AccentProfile profile{};
        profile.fill(1.0f);
        GroovePattern pattern(0x2468u, &profile, kSlots);

        const auto silentBar = runBar(pattern, /*density=*/0.0f, /*wild=*/0.0f, /*motion=*/0.0f,
                                      /*evolutionAmount=*/0.0f);
        for (bool fired : silentBar) {
            assert(!fired);
        }

        const auto loudBar = runBar(pattern, /*density=*/1.0f, /*wild=*/0.0f, /*motion=*/0.0f,
                                    /*evolutionAmount=*/0.0f);
        for (bool fired : loudBar) {
            assert(fired);
        }
    }

    // Raising effectiveWild flattens a zero-weight slot's profile
    // influence toward uniform (slotWeight -> 1 as wild -> 1), so the
    // same zero-weight slot that never fires at wild=0 fires on every
    // bar at wild=1 (density=1, so probability becomes deterministically
    // 1 once the profile's influence is fully washed out).
    {
        GrooveProfiles::AccentProfile profile{};
        profile.fill(0.0f);
        GroovePattern silentAtLowWild(0xdef0u, &profile, kSlots);
        GroovePattern loudAtHighWild(0xdef0u, &profile, kSlots);

        bool everFiredAtWildZero = false;
        for (int bar = 0; bar < 20; ++bar) {
            const auto fired = runBar(silentAtLowWild, /*density=*/1.0f, /*wild=*/0.0f, /*motion=*/0.0f,
                                      /*evolutionAmount=*/1.0f);
            if (fired[5]) {
                everFiredAtWildZero = true;
            }
        }
        assert(!everFiredAtWildZero);

        bool alwaysFiredAtWildOne = true;
        for (int bar = 0; bar < 20; ++bar) {
            const auto fired = runBar(loudAtHighWild, /*density=*/1.0f, /*wild=*/1.0f, /*motion=*/0.0f,
                                      /*evolutionAmount=*/1.0f);
            if (!fired[5]) {
                alwaysFiredAtWildOne = false;
            }
        }
        assert(alwaysFiredAtWildOne);
    }

    // Meter change support: setAccentProfile + forceRegenerateNextBoundary
    // reseats the pattern onto a shorter bar (e.g. 3/4's 12 slots) and the
    // next boundary regenerates against the new length/profile rather than
    // reusing the stale 16-slot mask.
    {
        GrooveProfiles::AccentProfile profileA{};
        profileA.fill(1.0f);
        GroovePattern pattern(0x1357u, &profileA, kSlots);
        runBar(pattern, /*density=*/1.0f, /*wild=*/0.0f, /*motion=*/0.0f, /*evolutionAmount=*/0.0f);

        GrooveProfiles::AccentProfile profileB{};
        profileB.fill(0.0f);
        constexpr int kShorterBar = 12;
        pattern.setAccentProfile(&profileB, kShorterBar);
        pattern.forceRegenerateNextBoundary();

        bool everFired = false;
        for (int slot = 0; slot < kShorterBar; ++slot) {
            if (tickSlotAndCheckFired(pattern, slot, /*density=*/1.0f, /*wild=*/0.0f, /*motion=*/0.0f,
                                      /*evolutionAmount=*/0.0f)) {
                everFired = true;
            }
        }
        assert(!everFired);
    }

    // GrooveProfiles::generateProfile sanity checks across a non-4/4
    // meter: Crash fires only the true bar downbeat, Kick fires on every
    // pulse group's downbeat.
    {
        const auto& sevenEight = GrooveProfiles::kMeters[GrooveProfiles::findMeterIndex(7, 8)];
        assert(sevenEight.totalSlots == 14);
        assert(sevenEight.groupCount == 3);

        const auto crash = GrooveProfiles::generateProfile(6, sevenEight);
        assert(crash[0] == 1.0f);
        for (int s = 1; s < sevenEight.totalSlots; ++s) {
            assert(crash[static_cast<std::size_t>(s)] < 0.5f);
        }

        const auto kick = GrooveProfiles::generateProfile(0, sevenEight);
        int running = 0;
        for (int g = 0; g < sevenEight.groupCount; ++g) {
            assert(kick[static_cast<std::size_t>(running)] == 1.0f);
            running += sevenEight.groupLengths[static_cast<std::size_t>(g)];
        }
    }

    // Every supported meter's total slot count matches numerator*16/denominator.
    {
        for (const auto& meter : GrooveProfiles::kMeters) {
            const int expected = meter.numerator * 16 / meter.denominator;
            assert(meter.totalSlots == expected);
            assert(meter.totalSlots <= static_cast<int>(GrooveProfiles::kMaxSlotsPerBar));
        }
    }

    std::cout << "GroovePattern tests passed" << std::endl;
    return 0;
}
