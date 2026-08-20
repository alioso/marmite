#pragma once

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

// The presets a fresh install (a genuinely empty Presets/ directory —
// no prior installation to migrate from) is seeded with, so a release
// build handed to someone else has something real to load rather than
// an empty Presets menu. Content is a snapshot of an actual tuned
// preset (Demo), in the exact key=value text PresetStore already reads/
// writes — same format, just embedded here instead of read from disk.
inline std::vector<std::pair<std::string, std::string>> factoryPresets() {
    return {
        {"Demo",
         "voice0.enabled=1\n"
         "voice0.volume=0.538382\n"
         "voice0.tone=0.483766\n"
         "voice0.motion=0.400078\n"
         "voice0.density=0.489328\n"
         "voice0.chaos=0\n"
         "voice0.volumeEvoEnabled=1\n"
         "voice0.toneEvoEnabled=1\n"
         "voice0.motionEvoEnabled=1\n"
         "voice0.densityEvoEnabled=1\n"
         "voice0.chaosEvoEnabled=1\n"
         "voice0.samplePath=\n"
         "voice1.enabled=1\n"
         "voice1.volume=0.254257\n"
         "voice1.tone=0.0538391\n"
         "voice1.motion=0.570448\n"
         "voice1.density=0.172506\n"
         "voice1.chaos=0.70588\n"
         "voice1.volumeEvoEnabled=1\n"
         "voice1.toneEvoEnabled=1\n"
         "voice1.motionEvoEnabled=1\n"
         "voice1.densityEvoEnabled=1\n"
         "voice1.chaosEvoEnabled=1\n"
         "voice1.samplePath=\n"
         "voice2.enabled=1\n"
         "voice2.volume=0.326785\n"
         "voice2.tone=0.680445\n"
         "voice2.motion=0.576414\n"
         "voice2.density=0.243461\n"
         "voice2.chaos=0.657209\n"
         "voice2.volumeEvoEnabled=1\n"
         "voice2.toneEvoEnabled=1\n"
         "voice2.motionEvoEnabled=1\n"
         "voice2.densityEvoEnabled=1\n"
         "voice2.chaosEvoEnabled=1\n"
         "voice2.samplePath=\n"
         "voice3.enabled=1\n"
         "voice3.volume=0.646891\n"
         "voice3.tone=0.0645779\n"
         "voice3.motion=0.747647\n"
         "voice3.density=0.845044\n"
         "voice3.chaos=0.17306\n"
         "voice3.volumeEvoEnabled=1\n"
         "voice3.toneEvoEnabled=1\n"
         "voice3.motionEvoEnabled=1\n"
         "voice3.densityEvoEnabled=1\n"
         "voice3.chaosEvoEnabled=1\n"
         "voice3.samplePath=\n"
         "voice4.enabled=1\n"
         "voice4.volume=0.0826584\n"
         "voice4.tone=0.651795\n"
         "voice4.motion=0.586489\n"
         "voice4.density=0.291411\n"
         "voice4.chaos=0.00130763\n"
         "voice4.volumeEvoEnabled=1\n"
         "voice4.toneEvoEnabled=1\n"
         "voice4.motionEvoEnabled=1\n"
         "voice4.densityEvoEnabled=1\n"
         "voice4.chaosEvoEnabled=1\n"
         "voice4.samplePath=\n"
         "voice5.enabled=1\n"
         "voice5.volume=0.593622\n"
         "voice5.tone=0.879203\n"
         "voice5.motion=0.531035\n"
         "voice5.density=0.236656\n"
         "voice5.chaos=0.32506\n"
         "voice5.volumeEvoEnabled=1\n"
         "voice5.toneEvoEnabled=1\n"
         "voice5.motionEvoEnabled=1\n"
         "voice5.densityEvoEnabled=1\n"
         "voice5.chaosEvoEnabled=1\n"
         "voice5.samplePath=\n"
         "voice6.enabled=1\n"
         "voice6.volume=0.838605\n"
         "voice6.tone=0.828001\n"
         "voice6.motion=0.218223\n"
         "voice6.density=0.0700819\n"
         "voice6.chaos=0.52528\n"
         "voice6.volumeEvoEnabled=1\n"
         "voice6.toneEvoEnabled=1\n"
         "voice6.motionEvoEnabled=1\n"
         "voice6.densityEvoEnabled=1\n"
         "voice6.chaosEvoEnabled=1\n"
         "voice6.samplePath=\n"
         "voice7.enabled=1\n"
         "voice7.volume=0.0958728\n"
         "voice7.tone=0.440234\n"
         "voice7.motion=0.192586\n"
         "voice7.density=0.0430745\n"
         "voice7.chaos=0.871037\n"
         "voice7.volumeEvoEnabled=1\n"
         "voice7.toneEvoEnabled=1\n"
         "voice7.motionEvoEnabled=1\n"
         "voice7.densityEvoEnabled=1\n"
         "voice7.chaosEvoEnabled=1\n"
         "voice7.samplePath=\n"
         "global.tempo=208.094\n"
         "global.evolutionAmount=0\n"
         "global.evolutionSpeed=0.5\n"
         "global.space=0.810796\n"
         "global.reverbRoom=0.256234\n"
         "global.reverbDecay=0.203813\n"
         "global.delayBeatFraction=1\n"
         "global.delayFeedback=0\n"
         "global.masterVolume=1\n"
         "global.wild=0\n"
         "global.meterNumerator=4\n"
         "global.meterDenominator=4\n"},
    };
}

// True for any preset name that ships baked into the install (see
// factoryPresets() above) — used to keep Override/Delete off-limits for
// them in the Presets popup, so the shipped starting point can't be lost
// by mistake; Save As under a new name is always still available.
inline bool isFactoryPresetName(const std::string& name) {
    const auto factory = factoryPresets();
    return std::any_of(factory.begin(), factory.end(),
                        [&name](const auto& entry) { return entry.first == name; });
}
