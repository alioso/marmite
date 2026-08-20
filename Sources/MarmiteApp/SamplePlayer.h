#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "SampleBuffer.h"

// A single playback of a one-shot sample: Jerrican's Grain, replayed
// against a stored buffer instead of synthesized fresh each time. Lives
// entirely on the audio thread, re-triggered in place by
// SampleVoicePool (no allocation). Idle players (isActive() == false)
// are silent and safe to re-trigger at any time.
class SamplePlayer {
public:
    struct StereoSample {
        float left = 0.0f;
        float right = 0.0f;
    };

    // buffer is not owned — it must outlive this trigger (the pool's
    // owner is responsible for that, same non-owning-pointer contract a
    // sampler engine always has to honor around live buffer swaps).
    // pan is 0 (full left) .. 1 (full right), 0.5 = center.
    void trigger(const SampleBuffer* buffer, float pitchSemitones, float velocity, float pan) {
        buffer_ = buffer;
        position_ = 0.0;
        playbackRate_ = std::pow(2.0f, pitchSemitones / 12.0f);
        gain_ = std::max(0.0f, std::min(1.0f, velocity));

        const float clampedPan = std::max(0.0f, std::min(1.0f, pan));
        const float angle = clampedPan * halfPi;
        leftGain_ = std::cos(angle);
        rightGain_ = std::sin(angle);

        fadeInSamplesRemaining_ = kFadeInSamples;
    }

    bool isActive() const {
        return buffer_ != nullptr &&
               position_ < static_cast<double>(buffer_->samples.size()) - 1.0;
    }

    StereoSample renderSample() {
        if (!isActive()) {
            return {};
        }

        const auto index = static_cast<std::size_t>(position_);
        const float frac = static_cast<float>(position_ - static_cast<double>(index));
        const float a = buffer_->samples[index];
        const float b =
            (index + 1 < buffer_->samples.size()) ? buffer_->samples[index + 1] : 0.0f;
        // Linear interpolation between neighboring samples — needed since
        // pitch-shifting reads at a fractional rate, not one sample per
        // tick, and interpolating avoids harsh aliasing on the readback.
        const float interpolated = a + (b - a) * frac;

        // A short fade-in avoids a hard click at trigger onset. A short
        // fade-out likewise guards the end: some buffers (e.g. the
        // procedural Kick, whose decay envelope is still ~5% of full
        // amplitude when the buffer ends) don't actually reach zero on
        // their own, so without this the playback head hitting the end
        // of the buffer is an audible click, not silence.
        float envelope = 1.0f;
        if (fadeInSamplesRemaining_ > 0) {
            envelope = 1.0f - static_cast<float>(fadeInSamplesRemaining_) /
                                   static_cast<float>(kFadeInSamples);
            --fadeInSamplesRemaining_;
        }
        const double samplesRemaining =
            static_cast<double>(buffer_->samples.size()) - 1.0 - position_;
        if (samplesRemaining < kFadeOutSamples) {
            envelope *= static_cast<float>(std::max(0.0, samplesRemaining) / kFadeOutSamples);
        }

        const float windowed = interpolated * gain_ * envelope;
        position_ += playbackRate_;
        return {windowed * leftGain_, windowed * rightGain_};
    }

private:
    static constexpr float halfPi = 1.5707963267948966f;
    static constexpr int kFadeInSamples = 32;
    static constexpr double kFadeOutSamples = 64.0;

    const SampleBuffer* buffer_ = nullptr;
    double position_ = 0.0;
    float playbackRate_ = 1.0f;
    float gain_ = 0.0f;
    float leftGain_ = 0.7071f;
    float rightGain_ = 0.7071f;
    int fadeInSamplesRemaining_ = 0;
};
