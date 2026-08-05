#include <cassert>
#include <cmath>
#include <iostream>

#include "DrumEvolutionEngine.h"
#include "DrumVoiceModel.h"

namespace {

constexpr double kSampleRate = 44100.0;

DrumVoiceModel makeTestVoice() { return DrumVoiceModel("Test", true, 0.8f, 0.5f, 0.3f, 0.4f, 0.6f); }

}  // namespace

int main() {
    // At amount == 0, update() is provably inert.
    {
        DrumVoiceModel voice = makeTestVoice();
        DrumEvolutionEngine engine(1u);
        engine.setSampleRate(kSampleRate);
        engine.resetTo(0.7f, 0.5f, 0.3f, 0.4f, 0.6f);

        for (int i = 0; i < static_cast<int>(kSampleRate) * 2; ++i) {
            engine.update(voice, 0.0f, 1.0f);
        }

        assert(voice.getVolume() == 0.8f);
        assert(voice.getTone() == 0.5f);
        assert(voice.getMotion() == 0.3f);
        assert(voice.getDensity() == 0.4f);
        assert(voice.getChaos() == 0.6f);
    }

    // At amount == speed == 1, over enough samples every macro moves.
    {
        DrumVoiceModel voice = makeTestVoice();
        DrumEvolutionEngine engine(2u);
        engine.setSampleRate(kSampleRate);
        engine.resetTo(0.7f, 0.5f, 0.3f, 0.4f, 0.6f);

        bool volumeChanged = false, toneChanged = false, motionChanged = false;
        bool densityChanged = false, chaosChanged = false;

        for (int i = 0; i < static_cast<int>(kSampleRate) * 5; ++i) {
            engine.update(voice, 1.0f, 1.0f);
            if (voice.getVolume() != 0.8f) volumeChanged = true;
            if (voice.getTone() != 0.5f) toneChanged = true;
            if (voice.getMotion() != 0.3f) motionChanged = true;
            if (voice.getDensity() != 0.4f) densityChanged = true;
            if (voice.getChaos() != 0.6f) chaosChanged = true;
        }

        assert(volumeChanged);
        assert(toneChanged);
        assert(motionChanged);
        assert(densityChanged);
        assert(chaosChanged);
    }

    // Speed: identical seeds, speed=1 moves a parameter dramatically more
    // than speed=0 over the same duration.
    {
        DrumVoiceModel voiceSlow = makeTestVoice();
        DrumEvolutionEngine engineSlow(4u);
        engineSlow.setSampleRate(kSampleRate);
        engineSlow.resetTo(0.7f, 0.5f, 0.3f, 0.4f, 0.6f);

        DrumVoiceModel voiceFast = makeTestVoice();
        DrumEvolutionEngine engineFast(4u);
        engineFast.setSampleRate(kSampleRate);
        engineFast.resetTo(0.7f, 0.5f, 0.3f, 0.4f, 0.6f);

        for (int i = 0; i < static_cast<int>(kSampleRate) * 5; ++i) {
            engineSlow.update(voiceSlow, 1.0f, 0.0f);
            engineFast.update(voiceFast, 1.0f, 1.0f);
        }

        const float slowMovement = std::abs(voiceSlow.getTone() - 0.5f);
        const float fastMovement = std::abs(voiceFast.getTone() - 0.5f);
        assert(slowMovement < 0.05f);
        assert(fastMovement > slowMovement);
    }

    // Per-parameter opt-out: disabling Tone stops it from ever moving,
    // even at amount=speed=1, while other (still-enabled) macros keep
    // evolving normally.
    {
        DrumVoiceModel voice = makeTestVoice();
        DrumEvolutionEngine engine(5u);
        engine.setSampleRate(kSampleRate);
        engine.resetTo(0.7f, 0.5f, 0.3f, 0.4f, 0.6f);
        engine.setToneEnabled(false);
        assert(!engine.isToneEnabled());
        assert(engine.isMotionEnabled());

        bool motionChanged = false;
        for (int i = 0; i < static_cast<int>(kSampleRate) * 5; ++i) {
            engine.update(voice, 1.0f, 1.0f);
            if (voice.getMotion() != 0.3f) motionChanged = true;
        }

        assert(voice.getTone() == 0.5f);
        assert(motionChanged);
    }

    // Re-enabling a disabled parameter resyncs it to the live value
    // first, so it doesn't jump to a stale target picked while disabled.
    {
        DrumVoiceModel voice = makeTestVoice();
        DrumEvolutionEngine engine(6u);
        engine.setSampleRate(kSampleRate);
        engine.resetTo(0.7f, 0.5f, 0.3f, 0.4f, 0.6f);
        engine.setToneEnabled(false);

        for (int i = 0; i < static_cast<int>(kSampleRate) * 5; ++i) {
            engine.update(voice, 1.0f, 1.0f);
        }
        assert(voice.getTone() == 0.5f);

        voice.setTone(0.9f);
        engine.resyncTone(0.9f);
        engine.setToneEnabled(true);

        engine.update(voice, 1.0f, 1.0f);
        assert(std::abs(voice.getTone() - 0.9f) < 0.05f);
    }

    std::cout << "DrumEvolutionEngine tests passed" << std::endl;
    return 0;
}
