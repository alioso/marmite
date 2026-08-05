#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

#include "MidiBindingManager.h"
#include "MidiPresetStore.h"

namespace {

std::filesystem::path makeTempDir() {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("marmite_midi_preset_test_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

int main() {
    const auto tempDir = makeTempDir();
    MidiPresetStore store(tempDir);

    // Nothing saved yet.
    assert(store.listPresetNames().empty());

    // Save -> load round-trip produces an equivalent binding table.
    {
        MidiBindingManager original;
        original.setBinding(MidiTarget::VoiceVolume,
                            MidiBinding{MidiEvent::Type::ControlChange, 1, 21});
        original.setBinding(MidiTarget::SelectVoice2,
                            MidiBinding{MidiEvent::Type::NoteOn, 10, 37});
        original.setBinding(MidiTarget::MasterVolume,
                            MidiBinding{MidiEvent::Type::ControlChange, 1, 7});

        assert(store.save("MPK Mini", original));

        MidiBindingManager loaded;
        assert(store.load("MPK Mini", loaded));

        for (const auto target : kAllMidiTargets) {
            const auto originalBinding = original.getBinding(target);
            const auto loadedBinding = loaded.getBinding(target);
            assert(originalBinding.has_value() == loadedBinding.has_value());
            if (originalBinding.has_value()) {
                assert(originalBinding->type == loadedBinding->type);
                assert(originalBinding->channel == loadedBinding->channel);
                assert(originalBinding->number == loadedBinding->number);
            }
        }
    }

    // listPresetNames reflects what's been saved, sorted.
    {
        MidiBindingManager empty;
        store.save("Zebra", empty);
        store.save("Alpha", empty);

        const auto names = store.listPresetNames();
        assert(names.size() == 3);
        assert(names[0] == "Alpha");
        assert(names[1] == "MPK Mini");
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
        // Removing something that no longer exists just fails cleanly.
        assert(!store.remove("Zebra"));
    }

    // load() on a name that was never saved leaves the manager untouched
    // rather than crashing.
    {
        MidiBindingManager manager;
        assert(!store.load("DoesNotExist", manager));
    }

    // Path-traversal attempts are rejected outright, not resolved outside
    // the preset directory.
    {
        MidiBindingManager manager;
        const auto outsideFile =
            std::filesystem::temp_directory_path() / "marmite_midi_preset_traversal_test.mbind";
        std::filesystem::remove(outsideFile);

        assert(!store.save("../marmite_midi_preset_traversal_test", manager));
        assert(!store.load("../marmite_midi_preset_traversal_test", manager));
        assert(!store.remove("../marmite_midi_preset_traversal_test"));
        assert(!store.save("nested/name", manager));
        assert(!store.save("", manager));
        assert(!store.save(".", manager));
        assert(!store.save("..", manager));
        assert(!std::filesystem::exists(outsideFile));
    }

    // A preset file with a non-numeric channel/number is skipped rather
    // than crashing the loader.
    {
        const auto malformedPath = tempDir / "Malformed.mbind";
        std::ofstream malformedFile(malformedPath);
        malformedFile << "VoiceVolume=CC:not-a-number:21\n";
        malformedFile << "MasterVolume=CC:1:7\n";
        malformedFile.close();

        MidiBindingManager manager;
        assert(store.load("Malformed", manager));
        assert(!manager.getBinding(MidiTarget::VoiceVolume).has_value());
        assert(manager.getBinding(MidiTarget::MasterVolume).has_value());
    }

    std::filesystem::remove_all(tempDir);

    std::cout << "MidiPresetStore tests passed" << std::endl;
    return 0;
}
