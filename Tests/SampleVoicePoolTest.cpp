#include <cassert>
#include <cmath>
#include <iostream>

#include "SamplePlayer.h"
#include "SampleVoicePool.h"

namespace {

SampleBuffer makeConstantBuffer(std::size_t length, float value) {
    SampleBuffer buffer;
    buffer.samples.assign(length, value);
    buffer.sampleRate = 44100.0;
    return buffer;
}

}  // namespace

int main() {
    // SamplePlayer: idle by default, safe to render before any trigger.
    {
        SamplePlayer player;
        assert(!player.isActive());
        const auto sample = player.renderSample();
        assert(sample.left == 0.0f && sample.right == 0.0f);
    }

    // Trigger, render through to completion, confirm it becomes inactive
    // once the buffer is exhausted.
    {
        const auto buffer = makeConstantBuffer(100, 1.0f);
        SamplePlayer player;
        player.trigger(&buffer, 0.0f, 1.0f, 0.5f);
        assert(player.isActive());

        int rendered = 0;
        while (player.isActive() && rendered < 1000) {
            player.renderSample();
            ++rendered;
        }
        assert(!player.isActive());
        assert(rendered > 0 && rendered < 1000);
    }

    // Pitch-shifting up an octave (pitchSemitones=12, rate=2x) consumes
    // the buffer in roughly half as many render calls as unshifted
    // playback of the same buffer.
    {
        const auto buffer = makeConstantBuffer(1000, 1.0f);

        SamplePlayer unshifted;
        unshifted.trigger(&buffer, 0.0f, 1.0f, 0.5f);
        int unshiftedCount = 0;
        while (unshifted.isActive() && unshiftedCount < 10000) {
            unshifted.renderSample();
            ++unshiftedCount;
        }

        SamplePlayer shiftedUp;
        shiftedUp.trigger(&buffer, 12.0f, 1.0f, 0.5f);
        int shiftedCount = 0;
        while (shiftedUp.isActive() && shiftedCount < 10000) {
            shiftedUp.renderSample();
            ++shiftedCount;
        }

        assert(shiftedCount < unshiftedCount);
        // Roughly half, allowing generous slack for the interpolation
        // boundary and fade-in.
        assert(shiftedCount < unshiftedCount * 0.65);
    }

    // Gain (velocity) scales output level.
    {
        const auto buffer = makeConstantBuffer(100, 1.0f);
        SamplePlayer quiet;
        quiet.trigger(&buffer, 0.0f, 0.25f, 0.5f);
        for (int i = 0; i < 40; ++i) quiet.renderSample();  // past the fade-in
        const auto sample = quiet.renderSample();
        assert(sample.left > 0.0f && sample.left < 0.3f);
    }

    // SampleVoicePool: a null buffer is a safe no-op.
    {
        SampleVoicePool pool;
        pool.trigger(nullptr, 0.0f, 1.0f);
        const auto sample = pool.renderSample();
        assert(sample.left == 0.0f && sample.right == 0.0f);
    }

    // Triggering up to the pool size all sound simultaneously; output
    // stays within [-1, 1] regardless of how many overlap.
    {
        const auto buffer = makeConstantBuffer(1000, 1.0f);
        SampleVoicePool pool;
        for (int i = 0; i < SampleVoicePool::kPoolSize; ++i) {
            pool.trigger(&buffer, 0.0f, 1.0f);
        }
        for (int i = 0; i < 40; ++i) {
            const auto sample = pool.renderSample();
            assert(sample.left >= -1.0f && sample.left <= 1.0f);
            assert(sample.right >= -1.0f && sample.right <= 1.0f);
        }
    }

    // Pool exhaustion: triggering beyond kPoolSize while everything is
    // still active is dropped gracefully, not a crash — the pool simply
    // keeps rendering whatever it already has.
    {
        const auto buffer = makeConstantBuffer(1000, 1.0f);
        SampleVoicePool pool;
        for (int i = 0; i < SampleVoicePool::kPoolSize + 5; ++i) {
            pool.trigger(&buffer, 0.0f, 1.0f);
        }
        const auto sample = pool.renderSample();
        assert(sample.left >= -1.0f && sample.left <= 1.0f);
    }

    std::cout << "SampleVoicePool tests passed" << std::endl;
    return 0;
}
