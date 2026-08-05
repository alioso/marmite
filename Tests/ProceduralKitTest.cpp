#include <cassert>
#include <cmath>
#include <iostream>

#include "ProceduralKit.h"

namespace {

// Every synthesized sample should be non-empty, contain some actual
// signal (not silence), and stay within a sane amplitude range (no
// NaN/inf, no wild clipping far beyond unity).
void checkSample(const SampleBuffer& buffer) {
    assert(!buffer.samples.empty());
    assert(buffer.sampleRate == 44100.0);

    bool hasSignal = false;
    for (const float sample : buffer.samples) {
        assert(std::isfinite(sample));
        assert(sample > -4.0f && sample < 4.0f);
        if (std::abs(sample) > 1e-4f) {
            hasSignal = true;
        }
    }
    assert(hasSignal);
}

}  // namespace

int main() {
    const double sampleRate = 44100.0;

    checkSample(ProceduralKit::makeKick(sampleRate));
    checkSample(ProceduralKit::makeSnare(sampleRate));
    checkSample(ProceduralKit::makeClosedHat(sampleRate));
    checkSample(ProceduralKit::makeOpenHat(sampleRate));
    checkSample(ProceduralKit::makeClap(sampleRate));
    checkSample(ProceduralKit::makePerc(sampleRate));
    checkSample(ProceduralKit::makeCrash(sampleRate));
    checkSample(ProceduralKit::makeGlitch(sampleRate));

    const auto kit = ProceduralKit::makeDefaultKit(sampleRate);
    assert(kit.size() == 8);
    for (const auto& sample : kit) {
        assert(!sample.samples.empty());
    }

    // Open hat should decay much slower than closed hat (that's the
    // whole point of the distinction) — closed hat's buffer is shorter.
    assert(ProceduralKit::makeClosedHat(sampleRate).samples.size() <
           ProceduralKit::makeOpenHat(sampleRate).samples.size());

    std::cout << "ProceduralKit tests passed" << std::endl;
    return 0;
}
