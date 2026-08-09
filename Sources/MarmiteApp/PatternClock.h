#pragma once

#include <algorithm>

// A single shared, sample-accurate tempo clock, driving every voice's
// GroovePattern off the same grid. Ticked once per sample from the audio
// callback (same per-sample call convention Jerrican's GrainCloud uses)
// rather than batched per-block, so there's no need to track multiple
// boundary crossings within one call — simpler, and avoids any
// real-time allocation.
class PatternClock {
public:
    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate;
        recompute();
    }

    void setBpm(float bpm) {
        bpm_ = std::max(20.0f, std::min(300.0f, bpm));
        recompute();
    }

    void reset() { samplePosition_ = 0.0; }

    double getSamplesPerSubdivision() const { return samplesPerSubdivision_; }

    // Call once per sample. Returns true exactly on the sample a new
    // 16th-note subdivision begins.
    bool tick() {
        samplePosition_ += 1.0;
        if (samplePosition_ >= samplesPerSubdivision_) {
            samplePosition_ -= samplesPerSubdivision_;
            return true;
        }
        return false;
    }

private:
    void recompute() {
        // 16th notes: 4 per beat. samplesPerBeat = sampleRate * 60 / bpm.
        samplesPerSubdivision_ = (sampleRate_ * 60.0) / (static_cast<double>(bpm_) * 4.0);
    }

    double sampleRate_ = 44100.0;
    float bpm_ = 120.0f;
    double samplesPerSubdivision_ = (44100.0 * 60.0) / (120.0 * 4.0);
    double samplePosition_ = 0.0;
};
