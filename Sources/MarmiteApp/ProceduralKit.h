#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "FastRandom.h"
#include "SampleBuffer.h"

// Synthesizes Marmite's default 8-voice kit in-process at startup —
// deliberately not shipping any third-party sample audio (licensing),
// so the "default kit" is just a bootstrap: the engine is a real sample
// player from day one, "Load Sample..." replaces any of these with a
// real WAV. Plain C++ (no JUCE) — same testability convention as the
// rest of the engine; Main.cpp is the only caller.
//
// Aesthetic brief: dry and understated, not polished — no gated-reverb
// snare, no chorus/shimmer, no "epic" cymbal wash (that reads as cheap
// 80s synth-pop). Hats are tight and dry. The snare is mostly noise —
// non-resonant by design; the live Chaos/Motion macros are what let a
// voice get more excited/unstable during a performance, not the sample
// itself. The kick is deep but leaves headroom rather than filling the
// whole low end. A shared, sparse "crackle" (a few quiet, short ticks
// per hit) runs through most voices — subtle dust, not a clean sheen.
namespace ProceduralKit {

namespace detail {

constexpr double kTwoPi = 6.283185307179586;
constexpr double kPi = 3.141592653589793;

inline int samplesFor(double sampleRate, double seconds) {
    return static_cast<int>(sampleRate * seconds);
}

// Raised-cosine fade-in over attackSamples, 1.0 thereafter. Keeps
// transients from clicking on digital-silence-to-full-amplitude jumps,
// without softening the hit enough to read as "smooth"/polished.
inline double softAttack(int i, int attackSamples) {
    if (attackSamples <= 0 || i >= attackSamples) {
        return 1.0;
    }
    return 0.5 - 0.5 * std::cos(kPi * static_cast<double>(i) / static_cast<double>(attackSamples));
}

// Gentle tanh saturation: rounds off peaks instead of hard-clipping,
// normalized so drive doesn't change the resting gain. Kept low by
// default — saturation is what makes a hit feel "thick"/dense, and
// most voices here want to stay lean.
inline double softSaturate(double x, double drive = 1.15) {
    return std::tanh(x * drive) / std::tanh(drive);
}

// One-pole lowpass — tames raw white noise into something rounder
// before further shaping.
class OnePoleLowpass {
public:
    explicit OnePoleLowpass(double coefficient) : coefficient_(coefficient) {}
    double process(double x) {
        state_ += coefficient_ * (x - state_);
        return state_;
    }

private:
    double coefficient_;
    double state_ = 0.0;
};

// Band-limited noise: a lowpass rounds off raw hiss, then a
// DC-blocking highpass trims low-end rumble. brightness (0..1-ish,
// higher = closer to raw noise) is the one knob each call site tunes.
class WarmNoise {
public:
    WarmNoise(std::uint32_t seed, double brightness)
        : random_(seed), lowpass_(std::clamp(brightness, 0.05, 1.0)) {}

    double next() {
        const double raw = static_cast<double>(random_.nextFloatRange(-1.0f, 1.0f));
        const double smoothed = lowpass_.process(raw);
        const double highpassed = kHighpassCoefficient * (highpassPrevOut_ + smoothed - highpassPrevIn_);
        highpassPrevIn_ = smoothed;
        highpassPrevOut_ = highpassed;
        return highpassed;
    }

private:
    static constexpr double kHighpassCoefficient = 0.995;
    FastRandom random_;
    OnePoleLowpass lowpass_;
    double highpassPrevIn_ = 0.0;
    double highpassPrevOut_ = 0.0;
};

// Sparse, quiet ticks scattered through a hit — a needle catching dust,
// not distortion. Most samples produce silence; every so often a tiny
// 2-4 sample click fires at low amplitude. This is the shared "cracks
// and skips" texture — deliberately understated, easy to miss on a
// single hit and only read as character once a pattern is looping.
class Crackle {
public:
    explicit Crackle(std::uint32_t seed) : random_(seed) {}

    double next() {
        if (holdRemaining_ > 0) {
            --holdRemaining_;
            return held_;
        }
        if (random_.nextFloat01() < kFireProbability) {
            held_ = static_cast<double>(random_.nextFloatRange(-1.0f, 1.0f)) * kAmplitude;
            holdRemaining_ = 2 + static_cast<int>(random_.nextFloat01() * 3.0f);
            return held_;
        }
        held_ = 0.0;
        return 0.0;
    }

private:
    static constexpr float kFireProbability = 0.0018f;
    static constexpr double kAmplitude = 0.05;
    FastRandom random_;
    double held_ = 0.0;
    int holdRemaining_ = 0;
};

// Slow, low-passed random drift — used to wobble a pitch envelope by a
// tiny amount so it doesn't sound like a perfectly clean digital
// exponential sweep. Not audible as pitch modulation on its own; it
// just keeps the curve from being mathematically pristine.
class Wobble {
public:
    explicit Wobble(std::uint32_t seed) : random_(seed), lowpass_(0.0015) {}

    double next() {
        return lowpass_.process(static_cast<double>(random_.nextFloatRange(-1.0f, 1.0f)));
    }

private:
    FastRandom random_;
    OnePoleLowpass lowpass_;
};

}  // namespace detail

// A pitch-swept sine (down to a genuinely low fundamental) for depth,
// kept lean rather than thick. A slow pitch wobble and a touch of
// 2nd-harmonic content keep it from sounding like a mathematically
// clean digital sweep — a little organic imperfection instead of a
// video-game kick.
inline SampleBuffer makeKick(double sampleRate) {
    SampleBuffer buffer;
    buffer.sampleRate = sampleRate;
    const int length = detail::samplesFor(sampleRate, 0.28);
    buffer.samples.resize(static_cast<std::size_t>(length));

    detail::Crackle crackle(0x4b19c3du);
    detail::Wobble wobble(0x8e21f4au);
    const int attackSamples = detail::samplesFor(sampleRate, 0.002);
    double phase = 0.0;
    for (int i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double freq = 38.0 + 100.0 * std::exp(-t * 21.0) + wobble.next() * 2.5;
        phase += freq * detail::kTwoPi / sampleRate;
        const double amp = std::exp(-t * 11.0) * detail::softAttack(i, attackSamples);
        const double harmonic = std::sin(phase * 2.0) * 0.1;
        const double body = detail::softSaturate(std::sin(phase) + harmonic, 1.1) * amp * 0.8;
        buffer.samples[static_cast<std::size_t>(i)] =
            static_cast<float>(body + crackle.next() * amp);
    }
    return buffer;
}

// Mostly noise, deliberately non-resonant — only a whisper of pitched
// content under the crack, fast decay so nothing rings out. A tuned
// tone here is the classic gated-reverb-snare tell; this stays dry.
inline SampleBuffer makeSnare(double sampleRate) {
    SampleBuffer buffer;
    buffer.sampleRate = sampleRate;
    const int length = detail::samplesFor(sampleRate, 0.14);
    buffer.samples.resize(static_cast<std::size_t>(length));

    detail::WarmNoise noise(0x7e2a91fu, 0.65);
    detail::Crackle crackle(0x51de9a2u);
    const int attackSamples = detail::samplesFor(sampleRate, 0.0005);
    double phase = 0.0;
    for (int i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double amp = std::exp(-t * 30.0) * detail::softAttack(i, attackSamples);
        phase += 190.0 * detail::kTwoPi / sampleRate;
        const double tone = std::sin(phase) * 0.05;
        const double noiseSample = noise.next() * 0.7;
        buffer.samples[static_cast<std::size_t>(i)] =
            static_cast<float>((tone + noiseSample) * amp + crackle.next() * amp);
    }
    return buffer;
}

// Short, bright, dry band-limited noise — no modulation, no wash, gone
// almost as soon as it starts.
inline SampleBuffer makeClosedHat(double sampleRate) {
    SampleBuffer buffer;
    buffer.sampleRate = sampleRate;
    const int length = detail::samplesFor(sampleRate, 0.045);
    buffer.samples.resize(static_cast<std::size_t>(length));

    detail::WarmNoise noise(0x1f3d5b7u, 0.72);
    const int attackSamples = detail::samplesFor(sampleRate, 0.0004);
    for (int i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double amp = std::exp(-t * 80.0) * detail::softAttack(i, attackSamples);
        buffer.samples[static_cast<std::size_t>(i)] = static_cast<float>(noise.next() * amp * 0.85);
    }
    return buffer;
}

// Same dry texture as the closed hat, just a longer decay — no shimmer
// or tremolo added; the only difference from closed is how long it
// takes to die out.
inline SampleBuffer makeOpenHat(double sampleRate) {
    SampleBuffer buffer;
    buffer.sampleRate = sampleRate;
    const int length = detail::samplesFor(sampleRate, 0.28);
    buffer.samples.resize(static_cast<std::size_t>(length));

    detail::WarmNoise noise(0x9c1e77du, 0.68);
    const int attackSamples = detail::samplesFor(sampleRate, 0.0006);
    for (int i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double amp = std::exp(-t * 9.5) * detail::softAttack(i, attackSamples);
        buffer.samples[static_cast<std::size_t>(i)] = static_cast<float>(noise.next() * amp * 0.85);
    }
    return buffer;
}

// 3-4 tight, dry noise bursts — a flam, not a wash. Kept short so it
// reads as a pat rather than a hand-clap-with-reverb.
inline SampleBuffer makeClap(double sampleRate) {
    SampleBuffer buffer;
    buffer.sampleRate = sampleRate;
    const int length = detail::samplesFor(sampleRate, 0.18);
    buffer.samples.assign(static_cast<std::size_t>(length), 0.0f);

    detail::WarmNoise noise(0x2ad64f1u, 0.65);
    constexpr int burstCount = 4;
    const int burstSpacing = detail::samplesFor(sampleRate, 0.011);
    const int burstLength = detail::samplesFor(sampleRate, 0.02);
    const int burstAttack = detail::samplesFor(sampleRate, 0.0008);
    for (int burst = 0; burst < burstCount; ++burst) {
        const int start = burst * burstSpacing;
        for (int i = 0; i < burstLength && start + i < length; ++i) {
            const double t = static_cast<double>(i) / sampleRate;
            const double amp = std::exp(-t * 62.0) * detail::softAttack(i, burstAttack);
            buffer.samples[static_cast<std::size_t>(start + i)] +=
                static_cast<float>(noise.next() * amp * 0.5);
        }
    }
    // Short quiet tail after the flams, so it reads as one hit rather
    // than trailing off into a room sound.
    const int tailStart = burstCount * burstSpacing;
    for (int i = tailStart; i < length; ++i) {
        const double t = static_cast<double>(i - tailStart) / sampleRate;
        const double amp = std::exp(-t * 26.0) * 0.2;
        buffer.samples[static_cast<std::size_t>(i)] += static_cast<float>(noise.next() * amp);
    }
    return buffer;
}

// A dry click at a fixed (not swept) pitch — two inharmonic partials
// plus a burst of noise, decaying almost immediately. A fast downward
// pitch sweep on a sine is the classic sci-fi "laser" sound; this
// deliberately avoids any pitch movement so it reads as a struck small
// object (closer to a rimshot/clave) instead.
inline SampleBuffer makePerc(double sampleRate) {
    SampleBuffer buffer;
    buffer.sampleRate = sampleRate;
    const int length = detail::samplesFor(sampleRate, 0.08);
    buffer.samples.resize(static_cast<std::size_t>(length));

    detail::WarmNoise noise(0xa317c5eu, 0.55);
    detail::Crackle crackle(0x5c9e21fu);
    const int attackSamples = detail::samplesFor(sampleRate, 0.0004);
    constexpr double freqA = 860.0;
    constexpr double freqB = 1180.0;
    double phaseA = 0.0;
    double phaseB = 0.0;
    for (int i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double amp = std::exp(-t * 65.0) * detail::softAttack(i, attackSamples);
        phaseA += freqA * detail::kTwoPi / sampleRate;
        phaseB += freqB * detail::kTwoPi / sampleRate;
        const double body = std::sin(phaseA) * 0.55 + std::sin(phaseB) * 0.35;
        const double noiseSample = noise.next() * 0.45;
        buffer.samples[static_cast<std::size_t>(i)] =
            static_cast<float>((body * 0.4 + noiseSample) * amp + crackle.next() * amp);
    }
    return buffer;
}

// A small, dry metallic tick rather than a big cymbal wash — a few
// detuned partials with a short decay and no shimmer, closer to a
// struck found object than an "epic" crash.
inline SampleBuffer makeCrash(double sampleRate) {
    SampleBuffer buffer;
    buffer.sampleRate = sampleRate;
    const int length = detail::samplesFor(sampleRate, 0.55);
    buffer.samples.resize(static_cast<std::size_t>(length));

    detail::WarmNoise noise(0x6b2f8e3u, 0.6);
    detail::Crackle crackle(0x3f8c1a6u);
    constexpr int toneCount = 5;
    const std::array<double, toneCount> freqs{2600.0, 3100.0, 3700.0, 4300.0, 5100.0};
    std::array<double, toneCount> phases{};
    const int attackSamples = detail::samplesFor(sampleRate, 0.004);
    for (int i = 0; i < length; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double amp = std::exp(-t * 6.5) * detail::softAttack(i, attackSamples);
        double tones = 0.0;
        for (int h = 0; h < toneCount; ++h) {
            phases[static_cast<std::size_t>(h)] +=
                freqs[static_cast<std::size_t>(h)] * detail::kTwoPi / sampleRate;
            tones += std::sin(phases[static_cast<std::size_t>(h)]);
        }
        tones /= static_cast<double>(toneCount);
        const double noiseSample = noise.next() * 0.3;
        buffer.samples[static_cast<std::size_t>(i)] =
            static_cast<float>((tones * 0.4 + noiseSample) * amp + crackle.next() * amp);
    }
    return buffer;
}

// Dry and short, same family as the hats/perc — no pitched content at
// all. A crude bitcrush (sample-and-hold downsampling) on band-limited
// noise gives it a digital, skipping quality on its own, without a
// tone underneath doing the work.
inline SampleBuffer makeGlitch(double sampleRate) {
    SampleBuffer buffer;
    buffer.sampleRate = sampleRate;
    const int length = detail::samplesFor(sampleRate, 0.07);
    buffer.samples.resize(static_cast<std::size_t>(length));

    detail::WarmNoise noise(0xd45a1c9u, 0.6);
    detail::Crackle crackle(0x3ac91f7u);
    constexpr int holdSamples = 5;  // crude bitcrush via sample-and-hold
    const int attackSamples = detail::samplesFor(sampleRate, 0.0004);
    float held = 0.0f;
    for (int i = 0; i < length; ++i) {
        if (i % holdSamples == 0) {
            held = static_cast<float>(noise.next());
        }
        const double t = static_cast<double>(i) / sampleRate;
        const double amp = std::exp(-t * 70.0) * detail::softAttack(i, attackSamples);
        buffer.samples[static_cast<std::size_t>(i)] =
            static_cast<float>(held * amp + crackle.next() * amp);
    }
    return buffer;
}

// Order matches the 8-voice kit: Kick, Snare, Clap, Closed Hat, Open
// Hat, Perc, Crash, Glitch.
inline std::array<SampleBuffer, 8> makeDefaultKit(double sampleRate) {
    return {makeKick(sampleRate),      makeSnare(sampleRate),   makeClap(sampleRate),
            makeClosedHat(sampleRate), makeOpenHat(sampleRate), makePerc(sampleRate),
            makeCrash(sampleRate),     makeGlitch(sampleRate)};
}

}  // namespace ProceduralKit
