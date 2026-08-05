#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "DrumVoiceModel.h"
#include "FastRandom.h"

// One per voice. Ported from Jerrican's EvolutionEngine.h — same
// mechanic (occasionally pick a new random target per macro, smooth
// toward it, each macro independently opt-in/out-able), just tracking
// DrumVoiceModel's five macros (volume/tone/motion/density/chaos)
// instead of Jerrican's six. Unlike Jerrican, there's no paired
// center/width special case here — every macro here is a single value,
// so all five setters/resyncs are uniform.
//
// resetTo()/setXEnabled()/resyncX() are called from the UI thread
// (Stop/Reset, Randomize, per-parameter toggles), update() runs on the
// audio thread — every field crossing that boundary is atomic (relaxed:
// each field is independent, no ordering needed between them).
class DrumEvolutionEngine {
public:
    explicit DrumEvolutionEngine(std::uint32_t seed) : random_(seed) {}

    void setSampleRate(double sampleRate) { sampleRate_ = sampleRate; }

    void resetTo(float volume, float tone, float motion, float density, float chaos) {
        store(volumeCurrent_, volumeTarget_, volume);
        store(toneCurrent_, toneTarget_, tone);
        store(motionCurrent_, motionTarget_, motion);
        store(densityCurrent_, densityTarget_, density);
        store(chaosCurrent_, chaosTarget_, chaos);
        sampleCounter_.store(0, std::memory_order_relaxed);
    }

    void setVolumeEnabled(bool enabled) { volumeEnabled_.store(enabled, std::memory_order_relaxed); }
    void setToneEnabled(bool enabled) { toneEnabled_.store(enabled, std::memory_order_relaxed); }
    void setMotionEnabled(bool enabled) { motionEnabled_.store(enabled, std::memory_order_relaxed); }
    void setDensityEnabled(bool enabled) { densityEnabled_.store(enabled, std::memory_order_relaxed); }
    void setChaosEnabled(bool enabled) { chaosEnabled_.store(enabled, std::memory_order_relaxed); }

    bool isVolumeEnabled() const { return volumeEnabled_.load(std::memory_order_relaxed); }
    bool isToneEnabled() const { return toneEnabled_.load(std::memory_order_relaxed); }
    bool isMotionEnabled() const { return motionEnabled_.load(std::memory_order_relaxed); }
    bool isDensityEnabled() const { return densityEnabled_.load(std::memory_order_relaxed); }
    bool isChaosEnabled() const { return chaosEnabled_.load(std::memory_order_relaxed); }

    // Snap a parameter's internal current/target to a live value — call
    // whenever the UI writes that parameter directly (manual drag, or
    // re-enabling a toggle), so the engine picks up and drifts onward
    // from the live value instead of fighting it with a stale one.
    void resyncVolume(float value) { store(volumeCurrent_, volumeTarget_, value); }
    void resyncTone(float value) { store(toneCurrent_, toneTarget_, value); }
    void resyncMotion(float value) { store(motionCurrent_, motionTarget_, value); }
    void resyncDensity(float value) { store(densityCurrent_, densityTarget_, value); }
    void resyncChaos(float value) { store(chaosCurrent_, chaosTarget_, value); }

    // Called once per sample. Provably inert at amount <= 0.
    void update(DrumVoiceModel& voice, float amount, float speed) {
        if (amount <= 0.0f) {
            return;
        }

        const float retargetProbabilityPerSample =
            (amount * retargetRateSpanHz) / static_cast<float>(sampleRate_);
        if (random_.nextFloat01() < retargetProbabilityPerSample) {
            volumeTarget_.store(random_.nextFloat01(), std::memory_order_relaxed);
            toneTarget_.store(random_.nextFloat01(), std::memory_order_relaxed);
            motionTarget_.store(random_.nextFloat01(), std::memory_order_relaxed);
            densityTarget_.store(random_.nextFloat01(), std::memory_order_relaxed);
            chaosTarget_.store(random_.nextFloat01(), std::memory_order_relaxed);
        }

        // Exponential mapping so Speed is useful across a huge dynamic
        // range: ~60s glides at speed=0 up to ~10ms (near-instant) at
        // speed=1 — same mapping Jerrican's EvolutionEngine uses.
        const float smoothing =
            slowSmoothingCoefficient *
            std::pow(fastSmoothingCoefficient / slowSmoothingCoefficient, speed);

        const bool shouldWrite = ++sampleCounter_ >= writeIntervalSamples;
        if (shouldWrite) {
            sampleCounter_ = 0;
        }

        smoothAndMaybeWrite(volumeCurrent_, volumeTarget_, smoothing, volumeEnabled_, shouldWrite,
                            [&](float value) { voice.setVolume(value); });
        smoothAndMaybeWrite(toneCurrent_, toneTarget_, smoothing, toneEnabled_, shouldWrite,
                            [&](float value) { voice.setTone(value); });
        smoothAndMaybeWrite(motionCurrent_, motionTarget_, smoothing, motionEnabled_, shouldWrite,
                            [&](float value) { voice.setMotion(value); });
        smoothAndMaybeWrite(densityCurrent_, densityTarget_, smoothing, densityEnabled_, shouldWrite,
                            [&](float value) { voice.setDensity(value); });
        smoothAndMaybeWrite(chaosCurrent_, chaosTarget_, smoothing, chaosEnabled_, shouldWrite,
                            [&](float value) { voice.setChaos(value); });
    }

private:
    static void store(std::atomic<float>& current, std::atomic<float>& target, float value) {
        current.store(value, std::memory_order_relaxed);
        target.store(value, std::memory_order_relaxed);
    }

    template <typename WriteFn>
    static void smoothAndMaybeWrite(std::atomic<float>& current, std::atomic<float>& target,
                                    float smoothing, const std::atomic<bool>& enabled,
                                    bool shouldWrite, WriteFn&& write) {
        if (!enabled.load(std::memory_order_relaxed)) {
            return;
        }

        float value = current.load(std::memory_order_relaxed);
        value += (target.load(std::memory_order_relaxed) - value) * smoothing;
        current.store(value, std::memory_order_relaxed);

        if (shouldWrite) {
            write(value);
        }
    }

    static constexpr int writeIntervalSamples = 64;
    static constexpr float retargetRateSpanHz = 0.3f;
    static constexpr float slowSmoothingCoefficient = 3.78e-7f;  // ~60s glide
    static constexpr float fastSmoothingCoefficient = 2.268e-3f;  // ~10ms glide

    FastRandom random_;
    double sampleRate_ = 44100.0;
    std::atomic<int> sampleCounter_{0};
    std::atomic<float> volumeCurrent_{0.5f};
    std::atomic<float> volumeTarget_{0.5f};
    std::atomic<float> toneCurrent_{0.5f};
    std::atomic<float> toneTarget_{0.5f};
    std::atomic<float> motionCurrent_{0.5f};
    std::atomic<float> motionTarget_{0.5f};
    std::atomic<float> densityCurrent_{0.5f};
    std::atomic<float> densityTarget_{0.5f};
    std::atomic<float> chaosCurrent_{0.5f};
    std::atomic<float> chaosTarget_{0.5f};
    std::atomic<bool> volumeEnabled_{true};
    std::atomic<bool> toneEnabled_{true};
    std::atomic<bool> motionEnabled_{true};
    std::atomic<bool> densityEnabled_{true};
    std::atomic<bool> chaosEnabled_{true};
};
