#pragma once

#include <optional>

#include "FastRandom.h"

// One per drum voice. Decides, sample by sample, whether/when/how this
// voice fires — the GrainCloud analog, but scheduling discrete hits
// against PatternClock's shared grid instead of continuously spawning
// grains. Deliberately decoupled from SampleVoicePool/SampleBuffer (it
// returns a Trigger for the caller to act on) so it stays testable
// without a real sample buffer, and so the audio callback — not this
// class — owns the decision of what a "trigger" actually does.
class PatternCloud {
public:
    struct Trigger {
        float pitchSemitones = 0.0f;
        float velocity = 1.0f;
    };

    explicit PatternCloud(std::uint32_t seed) : random_(seed) {}

    // Called once per sample, in lockstep with the shared PatternClock's
    // own tick(). onGridBoundary is that sample's tick() result.
    // density/chaos/motion are the voice's live macros;
    // samplesPerSubdivision is PatternClock::getSamplesPerSubdivision().
    //
    // On a boundary, rolls against density to decide whether to fire at
    // all. Chaos then delays the actual trigger up to half a subdivision
    // after the grid point (0 = exactly on the grid, 1 = loose/glitchy
    // timing) — capped at half so a delayed trigger always resolves
    // before the *next* boundary arrives a full subdivision later,
    // meaning there's never more than one pending trigger to track.
    // Motion scales per-hit velocity/pitch jitter, so a voice "breathes"
    // instead of repeating identically every time — the same
    // never-plays-the-same-way-twice idea GrainCloud's drift embodies,
    // just applied to a discrete hit instead of continuous grains.
    std::optional<Trigger> update(bool onGridBoundary, float density, float chaos, float motion,
                                  double samplesPerSubdivision) {
        if (onGridBoundary && random_.nextFloat01() < density) {
            const int maxDelaySamples =
                static_cast<int>(chaos * static_cast<float>(samplesPerSubdivision) * 0.5f);
            pendingDelaySamples_ =
                maxDelaySamples > 0
                    ? static_cast<int>(random_.nextFloat01() * static_cast<float>(maxDelaySamples))
                    : 0;
            pendingVelocity_ = 1.0f - random_.nextFloat01() * motion * 0.5f;
            pendingPitchSemitones_ = (random_.nextFloat01() * 2.0f - 1.0f) * motion * 2.0f;
            hasPendingTrigger_ = true;
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
    FastRandom random_;
    bool hasPendingTrigger_ = false;
    int pendingDelaySamples_ = 0;
    float pendingVelocity_ = 1.0f;
    float pendingPitchSemitones_ = 0.0f;
};
