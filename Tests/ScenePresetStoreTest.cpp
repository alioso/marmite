#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

#include "ScenePresetStore.h"
#include "SceneState.h"

namespace {

std::filesystem::path makeTempDir() {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("marmite_scene_preset_test_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir);
    return dir;
}

SceneState makeTestScene() {
    SceneState scene;
    for (std::size_t i = 0; i < scene.voices.size(); ++i) {
        auto& voice = scene.voices[i];
        const float base = static_cast<float>(i) * 0.1f;
        voice.enabled = (i % 2 == 0);
        voice.volume = base + 0.1f;
        voice.tone = base + 0.2f;
        voice.motion = base + 0.3f;
        voice.density = base + 0.4f;
        voice.chaos = base + 0.5f;
        voice.volumeEvoEnabled = (i % 2 == 0);
        voice.toneEvoEnabled = (i % 2 != 0);
        voice.motionEvoEnabled = true;
        voice.densityEvoEnabled = false;
        voice.chaosEvoEnabled = true;
        // Only some voices have a loaded sample — exercises both the
        // populated and empty ("using the default kit") cases in the
        // same round trip.
        if (i % 2 == 0) {
            voice.samplePath = "/Users/test/Music/Samples/voice" + std::to_string(i) + ".wav";
        }
    }
    scene.tempo = 140.0f;
    scene.evolutionAmount = 0.42f;
    scene.evolutionSpeed = 0.77f;
    scene.space = 0.6f;
    scene.reverbRoom = 0.33f;
    scene.reverbDecay = 0.66f;
    scene.delayBeatFraction = 0.25f;
    scene.delayFeedback = 0.5f;
    scene.masterVolume = 0.88f;
    return scene;
}

void assertScenesEqual(const SceneState& a, const SceneState& b) {
    for (std::size_t i = 0; i < a.voices.size(); ++i) {
        const auto& va = a.voices[i];
        const auto& vb = b.voices[i];
        assert(va.enabled == vb.enabled);
        assert(std::abs(va.volume - vb.volume) < 1e-4f);
        assert(std::abs(va.tone - vb.tone) < 1e-4f);
        assert(std::abs(va.motion - vb.motion) < 1e-4f);
        assert(std::abs(va.density - vb.density) < 1e-4f);
        assert(std::abs(va.chaos - vb.chaos) < 1e-4f);
        assert(va.volumeEvoEnabled == vb.volumeEvoEnabled);
        assert(va.toneEvoEnabled == vb.toneEvoEnabled);
        assert(va.motionEvoEnabled == vb.motionEvoEnabled);
        assert(va.densityEvoEnabled == vb.densityEvoEnabled);
        assert(va.chaosEvoEnabled == vb.chaosEvoEnabled);
        assert(va.samplePath == vb.samplePath);
    }
    assert(std::abs(a.tempo - b.tempo) < 1e-4f);
    assert(std::abs(a.evolutionAmount - b.evolutionAmount) < 1e-4f);
    assert(std::abs(a.evolutionSpeed - b.evolutionSpeed) < 1e-4f);
    assert(std::abs(a.space - b.space) < 1e-4f);
    assert(std::abs(a.reverbRoom - b.reverbRoom) < 1e-4f);
    assert(std::abs(a.reverbDecay - b.reverbDecay) < 1e-4f);
    assert(std::abs(a.delayBeatFraction - b.delayBeatFraction) < 1e-4f);
    assert(std::abs(a.delayFeedback - b.delayFeedback) < 1e-4f);
    assert(std::abs(a.masterVolume - b.masterVolume) < 1e-4f);
}

}  // namespace

int main() {
    const auto tempDir = makeTempDir();
    ScenePresetStore store(tempDir);

    assert(store.listPresetNames().empty());

    // Save -> load round-trip preserves every field.
    {
        const auto original = makeTestScene();
        assert(store.save("Live Set 1", original));

        SceneState loaded;
        assert(store.load("Live Set 1", loaded));
        assertScenesEqual(original, loaded);
    }

    // listPresetNames reflects saved files, sorted.
    {
        SceneState scene;
        store.save("Zebra", scene);
        store.save("Alpha", scene);

        const auto names = store.listPresetNames();
        assert(names.size() == 3);
        assert(names[0] == "Alpha");
        assert(names[1] == "Live Set 1");
        assert(names[2] == "Zebra");
    }

    // remove deletes the preset file.
    {
        assert(store.remove("Zebra"));
        const auto names = store.listPresetNames();
        assert(names.size() == 2);
        for (const auto& name : names) {
            assert(name != "Zebra");
        }
        assert(!store.remove("Zebra"));
    }

    // Path-traversal attempts are rejected, matching MidiPresetStore.
    {
        SceneState scene;
        assert(!store.save("../escape", scene));
        assert(!store.load("../escape", scene));
        assert(!store.remove("../escape"));
        assert(!store.save("nested/name", scene));
    }

    // A malformed value is skipped rather than crashing the loader.
    {
        const auto malformedPath = tempDir / "Malformed.mscene";
        std::ofstream malformedFile(malformedPath);
        malformedFile << "voice0.volume=not-a-number\n";
        malformedFile << "global.masterVolume=0.5\n";
        malformedFile.close();

        SceneState loaded;
        assert(store.load("Malformed", loaded));
        assert(std::abs(loaded.masterVolume - 0.5f) < 1e-4f);
    }

    // samplePath is the one non-numeric field in an otherwise all-float
    // format — confirm it round-trips correctly and doesn't disturb
    // parsing of the numeric fields on neighboring lines.
    {
        const auto samplePathPath = tempDir / "SamplePath.mscene";
        std::ofstream sampleFile(samplePathPath);
        sampleFile << "voice0.volume=0.75\n";
        sampleFile << "voice0.samplePath=/Users/test/Music/kick.wav\n";
        sampleFile << "voice1.samplePath=\n";
        sampleFile << "voice2.volume=0.25\n";
        sampleFile.close();

        SceneState loaded;
        assert(store.load("SamplePath", loaded));
        assert(std::abs(loaded.voices[0].volume - 0.75f) < 1e-4f);
        assert(loaded.voices[0].samplePath == "/Users/test/Music/kick.wav");
        assert(loaded.voices[1].samplePath.empty());
        assert(std::abs(loaded.voices[2].volume - 0.25f) < 1e-4f);
    }

    // operator== — used by PresetControls to detect dirty/matching state.
    {
        const auto a = makeTestScene();
        auto b = makeTestScene();
        assert(a == b);

        b.voices[0].volume += 0.5f;
        assert(!(a == b));
        b.voices[0].volume -= 0.5f;
        assert(a == b);

        b.voices[2].toneEvoEnabled = !b.voices[2].toneEvoEnabled;
        assert(!(a == b));
        b.voices[2].toneEvoEnabled = a.voices[2].toneEvoEnabled;
        assert(a == b);

        b.voices[1].samplePath = "/somewhere/else.wav";
        assert(!(a == b));
        b.voices[1].samplePath = a.voices[1].samplePath;
        assert(a == b);

        b.space += 0.3f;
        assert(!(a == b));
        b.space = a.space;
        assert(a == b);

        b.masterVolume += 0.5f;
        assert(!(a == b));

        // Tiny float differences (well within round-trip tolerance) still
        // compare equal.
        auto c = makeTestScene();
        c.masterVolume += 1e-6f;
        assert(a == c);
    }

    std::filesystem::remove_all(tempDir);

    std::cout << "ScenePresetStore tests passed" << std::endl;
    return 0;
}
