#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

#include "FastRandom.h"
#include "GrooveProfiles.h"

// One per drum voice — the engine behind every voice's pattern (see
// MarmiteProcessor.h). Holds a persistent 16-slot mask — one bar's worth
// of on/off state per voice — that's mostly held steady and only mutates
// slot-by-slot on bar boundaries, rather than rolling a fresh independent
// decision on every 16th-note subdivision with no idea which subdivision
// it is. Two things can trigger a slot to re-roll: a genuine manual edit
// to Density/Wild/Busy since the last regeneration (so turning a knob is
// never a no-op, regardless of Evolution Amount — every other control in
// this app takes effect the instant you move it, and this should be no
// different), or autonomous mutation tied to Evolution Amount (a small
// nonzero floor even at Amount=0, so a pattern never gets permanently
// stuck on one unlucky initial roll). Which slots *can* go active is
// governed by GrooveProfiles' per-voice accent weights, blended against
// effectiveWild: at effectiveWild=0 the profile fully governs (a legible,
// steady groove — Kick locks to 1/3, Snare to 2/4, etc.); at
// effectiveWild=1 the profile's influence vanishes (uniform, busy,
// unconstrained). This is what makes the pattern "steady yet alive"
// instead of either frozen or fully random.
class GroovePattern {
public:
    struct Trigger {
        float pitchSemitones = 0.0f;
        float velocity = 1.0f;
    };

    GroovePattern(std::uint32_t seed, const GrooveProfiles::AccentProfile* accentProfile,
                  int activeSlotCount)
        : random_(seed), accentProfile_(accentProfile), activeSlotCount_(activeSlotCount) {}

    // Called whenever the meter changes — the accent profile (regenerated
    // by GrooveProfiles::generateProfile for the new meter) and the new
    // bar's slot count. The caller must also call forceRegenerateNextBoundary()
    // so the stale mask (sized/shaped for the old meter) isn't reused.
    void setAccentProfile(const GrooveProfiles::AccentProfile* accentProfile, int activeSlotCount) {
        accentProfile_ = accentProfile;
        activeSlotCount_ = activeSlotCount;
    }

    // Forces a full mask regeneration on the next bar-boundary update(),
    // regardless of Density/Wild having changed — used when the meter (and
    // therefore the mask's shape/length) has just changed.
    void forceRegenerateNextBoundary() { initialized_ = false; }

    // Called once per sample, in lockstep with the shared PatternClock's
    // tick() (via onGridBoundary). currentSlot is a shared 0-15
    // bar-position counter owned by the caller (MarmiteAudioProcessor),
    // incremented on every grid tick. effectiveWild is this voice's own
    // position on the ACDC/Squarepusher curve (the global Wild knob plus
    // this voice's Busy-offset).
    std::optional<Trigger> update(bool onGridBoundary, int currentSlot, float density,
                                  float effectiveWild, float motion, float evolutionAmount,
                                  double samplesPerSubdivision) {
        if (onGridBoundary) {
            // Regenerate the whole bar's mask exactly once per bar (slot
            // 0), plus once unconditionally on the very first call so the
            // voice isn't silent for its entire first bar. Evaluating
            // per-slot here (rather than only when we pass over that
            // slot) means every slot's fate for this bar is already
            // decided by the time we reach it, matching "persistent
            // pattern" rather than "decided just in time."
            if (!initialized_ || currentSlot == 0) {
                regenerateBar(density, effectiveWild, evolutionAmount);
                initialized_ = true;
                lastDensity_ = density;
                lastEffectiveWild_ = effectiveWild;
            }

            if (slotActive_[static_cast<std::size_t>(currentSlot)]) {
                // A half-subdivision-max random delay for micro-timing
                // looseness, scaled by effectiveWild — 0 = locks exactly
                // to the grid, 1 = up to half a subdivision late.
                const int maxDelaySamples = static_cast<int>(
                    effectiveWild * static_cast<float>(samplesPerSubdivision) * 0.5f);
                pendingDelaySamples_ =
                    maxDelaySamples > 0
                        ? static_cast<int>(random_.nextFloat01() * static_cast<float>(maxDelaySamples))
                        : 0;
                pendingVelocity_ = 1.0f - random_.nextFloat01() * motion * 0.5f;
                pendingPitchSemitones_ = (random_.nextFloat01() * 2.0f - 1.0f) * motion * 2.0f;
                hasPendingTrigger_ = true;
            }
        }

        if (hasPendingTrigger_) {
            if (pendingDelaySamples_ <= 0) {
                hasPendingTrigger_ = false;
                return Trigger{pendingPitchSemitones_, pendingVelocity_};
            }
            --pendingDelaySamples_;
        }

        return std::nullopt;
    }

private:
    void regenerateBar(float density, float effectiveWild, float evolutionAmount) {
        // A genuine manual edit to Density or Wild/Busy since the last
        // regeneration always forces a full fresh re-roll, independent of
        // Evolution Amount — turning a knob has to do something
        // immediately, the same as every other control in this app.
        // Otherwise, per-slot mutation is tied to Amount, plus a small
        // nonzero floor even at Amount=0 so the pattern can't get
        // permanently stuck on one unlucky initial roll forever. Higher
        // Wild raises the ceiling on top of that — consistent with Wild
        // reading as "less predictable" at the Squarepusher end.
        const bool paramsChanged = !initialized_ ||
                                   std::abs(density - lastDensity_) > kParamChangeThreshold ||
                                   std::abs(effectiveWild - lastEffectiveWild_) > kParamChangeThreshold;
        const float mutationProbability =
            kBaselineMutationProbability + evolutionAmount * (0.1f + 0.5f * effectiveWild);

        for (int slot = 0; slot < activeSlotCount_; ++slot) {
            if (paramsChanged || random_.nextFloat01() < mutationProbability) {
                // lerp(accentProfile[slot], 1.0, effectiveWild) — the
                // profile fully shapes slot likelihood at wild=0, and its
                // influence fades to nothing (uniform) at wild=1.
                const float profileWeight = (*accentProfile_)[static_cast<std::size_t>(slot)];
                const float slotWeight = profileWeight + (1.0f - profileWeight) * effectiveWild;
                slotActive_[static_cast<std::size_t>(slot)] = random_.nextFloat01() < density * slotWeight;
            }
        }
    }

    static constexpr float kParamChangeThreshold = 0.01f;
    static constexpr float kBaselineMutationProbability = 0.03f;

    FastRandom random_;
    const GrooveProfiles::AccentProfile* accentProfile_;
    int activeSlotCount_;
    std::array<bool, GrooveProfiles::kMaxSlotsPerBar> slotActive_{};
    bool initialized_ = false;
    float lastDensity_ = 0.0f;
    float lastEffectiveWild_ = 0.0f;
    bool hasPendingTrigger_ = false;
    int pendingDelaySamples_ = 0;
    float pendingVelocity_ = 1.0f;
    float pendingPitchSemitones_ = 0.0f;
};
