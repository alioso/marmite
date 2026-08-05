#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "DelayLine.h"

int main() {
    // Wet=0: output should be untouched regardless of input, since the
    // delayed signal never gets added in.
    {
        DelayLine delay;
        delay.setSampleRate(44100.0);
        delay.setDelaySamples(100);
        delay.setFeedback(0.5f);
        delay.setWet(0.0f);

        float left = 1.0f, right = -1.0f;
        delay.processSample(left, right);
        assert(left == 1.0f);
        assert(right == -1.0f);
    }

    // Wet=1, feedback=0: a single impulse reappears exactly delaySamples
    // later, and only once (no repeats without feedback).
    {
        DelayLine delay;
        delay.setSampleRate(44100.0);
        constexpr int delaySamples = 50;
        delay.setDelaySamples(delaySamples);
        delay.setFeedback(0.0f);
        delay.setWet(1.0f);

        bool sawImpulseAtExpectedSample = false;
        for (int i = 0; i < delaySamples + 20; ++i) {
            float left = (i == 0) ? 1.0f : 0.0f;
            float right = 0.0f;
            delay.processSample(left, right);
            if (i == delaySamples) {
                assert(std::abs(left - 1.0f) < 1e-5f);
                sawImpulseAtExpectedSample = true;
            } else if (i != 0) {
                // No repeats: everywhere else (other than the original
                // impulse sample and its single delayed echo) is silent.
                assert(std::abs(left) < 1e-5f);
            }
        }
        assert(sawImpulseAtExpectedSample);
    }

    // Feedback > 0: the impulse repeats more than once, each time
    // quieter than the last.
    {
        DelayLine delay;
        delay.setSampleRate(44100.0);
        constexpr int delaySamples = 30;
        delay.setDelaySamples(delaySamples);
        delay.setFeedback(0.5f);
        delay.setWet(1.0f);

        std::vector<float> echoPeaks;
        for (int i = 0; i < delaySamples * 4; ++i) {
            float left = (i == 0) ? 1.0f : 0.0f;
            float right = 0.0f;
            delay.processSample(left, right);
            if (i > 0 && (i % delaySamples) == 0) {
                echoPeaks.push_back(left);
            }
        }

        assert(echoPeaks.size() >= 3);
        for (std::size_t i = 1; i < echoPeaks.size(); ++i) {
            assert(echoPeaks[i] < echoPeaks[i - 1]);
        }
    }

    std::cout << "DelayLine tests passed" << std::endl;
    return 0;
}
