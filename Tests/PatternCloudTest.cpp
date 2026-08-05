#include <cassert>
#include <cmath>
#include <iostream>

#include "PatternClock.h"
#include "PatternCloud.h"

int main() {
    // PatternClock: over a long run, the number of grid boundaries
    // crossed should match sampleCount / samplesPerSubdivision, within a
    // tolerance of one (rounding at the start/end of the run).
    {
        PatternClock clock;
        clock.setSampleRate(44100.0);
        clock.setBpm(120.0f);

        constexpr int sampleCount = 441000;  // 10 seconds
        int boundaryCount = 0;
        for (int i = 0; i < sampleCount; ++i) {
            if (clock.tick()) {
                ++boundaryCount;
            }
        }

        const double expected = sampleCount / clock.getSamplesPerSubdivision();
        assert(std::abs(boundaryCount - expected) <= 1.0);
    }

    // Doubling BPM halves samplesPerSubdivision.
    {
        PatternClock clock;
        clock.setSampleRate(44100.0);
        clock.setBpm(120.0f);
        const double at120 = clock.getSamplesPerSubdivision();

        clock.setBpm(240.0f);
        const double at240 = clock.getSamplesPerSubdivision();

        assert(std::abs(at240 - at120 / 2.0) < 1e-6);
    }

    // PatternCloud: density=0 never fires, even on every boundary.
    {
        PatternClock clock;
        clock.setSampleRate(44100.0);
        clock.setBpm(120.0f);
        PatternCloud cloud(1u);

        bool everFired = false;
        for (int i = 0; i < 44100 * 4; ++i) {
            const bool boundary = clock.tick();
            if (cloud.update(boundary, 0.0f, 0.0f, 0.0f, clock.getSamplesPerSubdivision())
                    .has_value()) {
                everFired = true;
            }
        }
        assert(!everFired);
    }

    // density=1, chaos=0: fires on every boundary, exactly on the
    // boundary sample (zero delay).
    {
        PatternClock clock;
        clock.setSampleRate(44100.0);
        clock.setBpm(120.0f);
        PatternCloud cloud(2u);

        int fireCount = 0;
        int boundaryCount = 0;
        for (int i = 0; i < 44100 * 4; ++i) {
            const bool boundary = clock.tick();
            if (boundary) {
                ++boundaryCount;
            }
            const auto trigger =
                cloud.update(boundary, 1.0f, 0.0f, 0.0f, clock.getSamplesPerSubdivision());
            if (trigger.has_value()) {
                ++fireCount;
                // Zero chaos means zero delay — the trigger comes back on
                // the very same sample as the boundary that caused it.
                assert(boundary);
            }
        }
        assert(fireCount == boundaryCount);
    }

    // chaos=1 introduces a delay for at least some hits — proves chaos
    // actually displaces timing rather than being a no-op. Deterministic
    // given FastRandom's fixed seed, not a flaky statistical check.
    {
        PatternClock clock;
        clock.setSampleRate(44100.0);
        clock.setBpm(120.0f);
        PatternCloud cloud(3u);

        bool sawDelayedTrigger = false;
        for (int i = 0; i < 44100 * 4; ++i) {
            const bool boundary = clock.tick();
            const auto trigger =
                cloud.update(boundary, 1.0f, 1.0f, 0.0f, clock.getSamplesPerSubdivision());
            if (trigger.has_value() && !boundary) {
                sawDelayedTrigger = true;
            }
        }
        assert(sawDelayedTrigger);
    }

    // motion widens pitch jitter — with motion=1 across many hits, pitch
    // should vary rather than always being exactly 0.
    {
        PatternClock clock;
        clock.setSampleRate(44100.0);
        clock.setBpm(120.0f);
        PatternCloud cloud(4u);

        bool sawNonZeroPitch = false;
        for (int i = 0; i < 44100 * 4; ++i) {
            const bool boundary = clock.tick();
            const auto trigger =
                cloud.update(boundary, 1.0f, 0.0f, 1.0f, clock.getSamplesPerSubdivision());
            if (trigger.has_value() && trigger->pitchSemitones != 0.0f) {
                sawNonZeroPitch = true;
            }
        }
        assert(sawNonZeroPitch);
    }

    std::cout << "PatternCloud tests passed" << std::endl;
    return 0;
}
