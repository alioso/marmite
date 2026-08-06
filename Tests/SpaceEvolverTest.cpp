#include <atomic>
#include <cassert>
#include <cmath>
#include <iostream>

#include "SpaceEvolver.h"

namespace {
constexpr double kSampleRate = 44100.0;
}  // namespace

int main() {
    // At amount == 0, update() is provably inert.
    {
        SpaceEvolver evolver(1u, 1.0f);
        std::atomic<float> display{1.0f};
        float last = 1.0f;
        for (int i = 0; i < static_cast<int>(kSampleRate) * 2; ++i) {
            last = evolver.update(0.0f, 1.0f, kSampleRate, display);
        }
        assert(last == 1.0f);
    }

    // At amount == speed == 1, over enough samples the value visits both
    // low (sparse) and high (busy) territory — the whole point of this
    // class existing instead of relying on per-voice averages.
    {
        SpaceEvolver evolver(2u, 1.0f);
        std::atomic<float> display{1.0f};
        float minSeen = 1.0f;
        float maxSeen = 0.0f;
        for (int i = 0; i < static_cast<int>(kSampleRate) * 20; ++i) {
            const float value = evolver.update(1.0f, 1.0f, kSampleRate, display);
            minSeen = std::min(minSeen, value);
            maxSeen = std::max(maxSeen, value);
        }
        assert(minSeen < 0.25f);
        assert(maxSeen > 0.75f);
    }

    // resync() snaps both current and target immediately, so a manual
    // knob drag doesn't get pulled back toward a stale autonomous target.
    {
        SpaceEvolver evolver(3u, 1.0f);
        std::atomic<float> display{1.0f};
        for (int i = 0; i < static_cast<int>(kSampleRate); ++i) {
            evolver.update(1.0f, 1.0f, kSampleRate, display);
        }
        evolver.resync(0.2f);
        const float immediatelyAfter = evolver.update(1.0f, 0.0f, kSampleRate, display);
        assert(std::abs(immediatelyAfter - 0.2f) < 0.01f);
    }

    // The display mirror gets written to periodically while evolving.
    {
        SpaceEvolver evolver(4u, 1.0f);
        std::atomic<float> display{1.0f};
        evolver.resync(0.5f);
        for (int i = 0; i < 1000; ++i) {
            evolver.update(1.0f, 1.0f, kSampleRate, display);
        }
        assert(std::abs(display.load() - 0.5f) < 0.2f);
    }

    std::cout << "SpaceEvolver tests passed" << std::endl;
    return 0;
}
