#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

#include "FastRandom.h"

// A single evolving scalar, using the same retarget/smoothing mechanism
// as DrumEvolutionEngine (occasionally pick a new random target, smooth
// toward it — see DrumEvolutionEngine.h for the full rationale), but for
// one ensemble-wide value rather than a per-voice macro.
//
// Used for the global Space macro: how "busy" the whole kit is right
// now. Every voice's density retarget already wanders within its own
// range (DrumEvolutionEngine::setDensityRange), but 8 independent
// per-voice random walks essentially never land on collective silence
// at the same moment — statistically, something is always active. Space
// multiplies every voice's effective density together and drifts on its
// own, so the ensemble periodically breathes into genuinely sparse
// passages and back, the same autonomous way every other parameter
// already drifts.
//
// current_/target_ are atomics (not plain floats) specifically so a
// manual knob drag on the UI thread (resync()) can safely happen
// concurrently with update() running on the audio thread — the same
// cross-thread contract every other evolving parameter in this codebase
// follows.
class SpaceEvolver {
public:
    explicit SpaceEvolver(std::uint32_t seed, float initial = 1.0f)
        : random_(seed), current_(initial), target_(initial) {}

    void resync(float value) {
        current_.store(value, std::memory_order_relaxed);
        target_.store(value, std::memory_order_relaxed);
    }

    // Called once per sample from the audio thread. Returns the smoothed
    // current value for that sample's density math, and periodically
    // mirrors it into displayValue (same write-throttling
    // DrumEvolutionEngine uses) so the UI thread can move the Space knob
    // to reflect autonomous drift.
    float update(float amount, float speed, double sampleRate, std::atomic<float>& displayValue) {
        if (amount <= 0.0f) {
            return current_.load(std::memory_order_relaxed);
        }

        const float retargetProbabilityPerSample =
            (amount * retargetRateSpanHz) / static_cast<float>(sampleRate);
        if (random_.nextFloat01() < retargetProbabilityPerSample) {
            target_.store(random_.nextFloat01(), std::memory_order_relaxed);
        }

        const float smoothing =
            slowSmoothingCoefficient *
            std::pow(fastSmoothingCoefficient / slowSmoothingCoefficient, speed);

        float value = current_.load(std::memory_order_relaxed);
        value += (target_.load(std::memory_order_relaxed) - value) * smoothing;
        current_.store(value, std::memory_order_relaxed);

        if (++sampleCounter_ >= writeIntervalSamples) {
            sampleCounter_ = 0;
            displayValue.store(value, std::memory_order_relaxed);
        }
        return value;
    }

private:
    static constexpr int writeIntervalSamples = 64;
    static constexpr float retargetRateSpanHz = 0.3f;
    static constexpr float slowSmoothingCoefficient = 3.78e-7f;  // ~60s glide
    static constexpr float fastSmoothingCoefficient = 2.268e-3f;  // ~10ms glide

    FastRandom random_;
    std::atomic<float> current_;
    std::atomic<float> target_;
    int sampleCounter_ = 0;  // audio-thread-only, no cross-thread access
};
