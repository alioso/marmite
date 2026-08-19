#pragma once

#include <algorithm>
#include <atomic>
#include <string>

// A drum voice's five macros, ported from Jerrican's VoiceModel — same
// atomic, lock-free shape (written from the UI thread, read on the
// real-time audio thread), just renamed/reinterpreted for rhythm instead
// of continuous pitched texture:
//   volume   - overall level, unchanged in spirit.
//   tone     - sample pitch-shift (Timbre's replacement).
//   motion   - per-hit pitch/tone "breathing" jitter amount (replaces
//              GrainCloud's pitch-range-wander sense of Motion).
//   density  - probability this voice fires on any given grid subdivision
//              (Complexity's replacement).
//   chaos    - Dissonance's rhythmic sibling: 0 = hits locked exactly to
//              the beat grid, 1 = wide/unpredictable timing+velocity
//              looseness. The macro that lets one instrument span rock
//              4/4 to IDM-glitch territory.
class DrumVoiceModel {
public:
    DrumVoiceModel(std::string name, bool enabled, float volume, float tone, float motion,
                   float density, float chaos)
        : name_(std::move(name)),
          enabled_(enabled),
          volume_(clamp01(volume)),
          tone_(clamp01(tone)),
          motion_(clamp01(motion)),
          density_(clamp01(density)),
          chaos_(clamp01(chaos)) {}

    const std::string& getName() const { return name_; }
    bool isEnabled() const { return enabled_.load(std::memory_order_relaxed); }
    bool isSoloed() const { return soloed_.load(std::memory_order_relaxed); }
    float getVolume() const { return volume_.load(std::memory_order_relaxed); }
    float getTone() const { return tone_.load(std::memory_order_relaxed); }
    float getMotion() const { return motion_.load(std::memory_order_relaxed); }
    float getDensity() const { return density_.load(std::memory_order_relaxed); }
    float getChaos() const { return chaos_.load(std::memory_order_relaxed); }

    void setEnabled(bool enabled) { enabled_.store(enabled, std::memory_order_relaxed); }
    void setSoloed(bool soloed) { soloed_.store(soloed, std::memory_order_relaxed); }
    void setVolume(float volume) { volume_.store(clamp01(volume), std::memory_order_relaxed); }
    void setTone(float tone) { tone_.store(clamp01(tone), std::memory_order_relaxed); }
    void setMotion(float motion) { motion_.store(clamp01(motion), std::memory_order_relaxed); }
    void setDensity(float density) { density_.store(clamp01(density), std::memory_order_relaxed); }
    void setChaos(float chaos) { chaos_.store(clamp01(chaos), std::memory_order_relaxed); }

private:
    static float clamp01(float value) { return std::max(0.0f, std::min(1.0f, value)); }

    std::string name_;
    std::atomic<bool> enabled_;
    std::atomic<bool> soloed_{false};
    std::atomic<float> volume_;
    std::atomic<float> tone_;
    std::atomic<float> motion_;
    std::atomic<float> density_;
    std::atomic<float> chaos_;
};
