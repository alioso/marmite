#include <cassert>
#include <iostream>

#include "DrumVoiceModel.h"

int main() {
    DrumVoiceModel voice("Kick", true, 0.8f, 0.5f, 0.2f, 0.6f, 0.1f);

    assert(voice.getName() == "Kick");
    assert(voice.isEnabled());
    assert(voice.getVolume() == 0.8f);
    assert(voice.getTone() == 0.5f);
    assert(voice.getMotion() == 0.2f);
    assert(voice.getDensity() == 0.6f);
    assert(voice.getChaos() == 0.1f);

    // Values are clamped to [0, 1] on both construction and every setter.
    DrumVoiceModel outOfRange("Snare", false, 1.8f, -0.2f, 2.0f, -1.0f, 3.0f);
    assert(outOfRange.getVolume() == 1.0f);
    assert(outOfRange.getTone() == 0.0f);
    assert(outOfRange.getMotion() == 1.0f);
    assert(outOfRange.getDensity() == 0.0f);
    assert(outOfRange.getChaos() == 1.0f);

    voice.setEnabled(false);
    voice.setVolume(1.5f);
    voice.setTone(-0.5f);
    voice.setMotion(0.3f);
    voice.setDensity(0.9f);
    voice.setChaos(0.4f);

    assert(!voice.isEnabled());
    assert(voice.getVolume() == 1.0f);
    assert(voice.getTone() == 0.0f);
    assert(voice.getMotion() == 0.3f);
    assert(voice.getDensity() == 0.9f);
    assert(voice.getChaos() == 0.4f);

    std::cout << "DrumVoiceModel tests passed" << std::endl;
    return 0;
}
