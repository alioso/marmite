#pragma once

#include <vector>

// A single mono one-shot sample, plain C++ (no JUCE dependency) — same
// testability convention as the rest of the engine. Main.cpp is the only
// place that touches juce::AudioFormatManager/juce::AudioBuffer; loaded
// or procedurally-synthesized audio gets converted into this before
// anything in the engine ever sees it.
struct SampleBuffer {
    std::vector<float> samples;
    double sampleRate = 44100.0;
};
