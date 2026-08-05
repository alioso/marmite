#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "SamplePlayer.h"

// One per drum voice. Owns a fixed-size pool of SamplePlayers so a voice
// can retrigger while a previous hit is still ringing out (hi-hat rolls,
// fast rolls under high Density) without cutting itself off — the
// GrainCloud analog, same pooling/normalization approach.
class SampleVoicePool {
public:
    static constexpr int kPoolSize = 6;

    void trigger(const SampleBuffer* buffer, float pitchSemitones, float velocity) {
        if (buffer == nullptr) {
            return;
        }
        for (auto& player : players_) {
            if (!player.isActive()) {
                // Pan is fixed center for now — there's no per-voice Pan
                // macro yet, unlike Jerrican's grains, which benefit from
                // per-grain stereo spread for continuous texture; a drum
                // hit jumping around the stereo field per trigger would
                // just sound broken.
                player.trigger(buffer, pitchSemitones, velocity, 0.5f);
                return;
            }
        }
        // Pool exhausted — drop the trigger, same graceful degradation
        // GrainCloud::maybeSpawnGrain already uses when its pool is full.
    }

    SamplePlayer::StereoSample renderSample() {
        float left = 0.0f;
        float right = 0.0f;
        int activeCount = 0;
        for (auto& player : players_) {
            if (player.isActive()) {
                const auto sample = player.renderSample();
                left += sample.left;
                right += sample.right;
                ++activeCount;
            }
        }

        const float normalization =
            activeCount > 0 ? 1.0f / std::sqrt(static_cast<float>(activeCount)) : 1.0f;
        return {std::max(-1.0f, std::min(1.0f, left * normalization)),
                std::max(-1.0f, std::min(1.0f, right * normalization))};
    }

private:
    std::array<SamplePlayer, kPoolSize> players_;
};
