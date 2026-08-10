#include <cassert>
#include <iostream>

#include "MidiBindingManager.h"

int main() {
    // Unbound event resolves to nothing.
    {
        MidiBindingManager manager;
        MidiEvent event{MidiEvent::Type::ControlChange, 1, 21, 0.5f};
        assert(!manager.handleEvent(event).has_value());
    }

    // Learn flow: arming, then feeding a CC completes the binding and
    // subsequent identical events resolve to that target.
    {
        MidiBindingManager manager;
        assert(!manager.isLearning());

        manager.armLearn(MidiTarget::VoiceVolume);
        assert(manager.isLearning());
        assert(manager.learningTarget() == MidiTarget::VoiceVolume);

        MidiEvent learnEvent{MidiEvent::Type::ControlChange, 1, 21, 0.0f};
        const auto duringLearn = manager.handleEvent(learnEvent);
        assert(!duringLearn.has_value());
        assert(!manager.isLearning());

        MidiEvent laterEvent{MidiEvent::Type::ControlChange, 1, 21, 0.77f};
        const auto resolved = manager.handleEvent(laterEvent);
        assert(resolved.has_value());
        assert(*resolved == MidiTarget::VoiceVolume);

        const auto binding = manager.getBinding(MidiTarget::VoiceVolume);
        assert(binding.has_value());
        assert(binding->type == MidiEvent::Type::ControlChange);
        assert(binding->channel == 1);
        assert(binding->number == 21);
    }

    // Binding the same physical control to a second target steals it from
    // the first — one knob/pad should never silently drive two things.
    {
        MidiBindingManager manager;
        MidiEvent event{MidiEvent::Type::ControlChange, 2, 7, 0.5f};

        manager.armLearn(MidiTarget::VoiceVolume);
        manager.handleEvent(event);
        assert(manager.getBinding(MidiTarget::VoiceVolume).has_value());

        manager.armLearn(MidiTarget::VoiceTone);
        manager.handleEvent(event);
        assert(!manager.getBinding(MidiTarget::VoiceVolume).has_value());
        assert(manager.getBinding(MidiTarget::VoiceTone).has_value());

        const auto resolved = manager.handleEvent(event);
        assert(resolved.has_value());
        assert(*resolved == MidiTarget::VoiceTone);
    }

    // NoteOn bindings (pads) round-trip the same way CC bindings do.
    {
        MidiBindingManager manager;
        manager.armLearn(MidiTarget::SelectVoice1);
        MidiEvent padEvent{MidiEvent::Type::NoteOn, 10, 36, 1.0f};
        manager.handleEvent(padEvent);

        const auto resolved = manager.handleEvent(padEvent);
        assert(resolved.has_value());
        assert(*resolved == MidiTarget::SelectVoice1);

        // A CC on the same number/channel is a different event type and
        // must not match the NoteOn binding.
        MidiEvent ccSameNumbers{MidiEvent::Type::ControlChange, 10, 36, 1.0f};
        assert(!manager.handleEvent(ccSameNumbers).has_value());
    }

    // clearBinding / clearAll.
    {
        MidiBindingManager manager;
        manager.setBinding(MidiTarget::MasterVolume,
                           MidiBinding{MidiEvent::Type::ControlChange, 1, 7});
        manager.setBinding(MidiTarget::ReverbRoom,
                           MidiBinding{MidiEvent::Type::ControlChange, 1, 8});
        assert(manager.getBinding(MidiTarget::MasterVolume).has_value());

        manager.clearBinding(MidiTarget::MasterVolume);
        assert(!manager.getBinding(MidiTarget::MasterVolume).has_value());
        assert(manager.getBinding(MidiTarget::ReverbRoom).has_value());

        manager.clearAll();
        assert(!manager.getBinding(MidiTarget::ReverbRoom).has_value());
    }

    // Target count sanity check: 11 per-voice + 8 voice-select +
    // 4 transport + 11 global (9 original + Wild + Meter) = 34.
    { assert(kAllMidiTargets.size() == 34); }

    // Name <-> target round-trip, used by preset (de)serialization.
    {
        for (const auto target : kAllMidiTargets) {
            const auto name = std::string(midiTargetName(target));
            const auto roundTripped = midiTargetFromName(name);
            assert(roundTripped.has_value());
            assert(*roundTripped == target);
        }
        assert(!midiTargetFromName("NotARealTarget").has_value());
    }

    // equals() — used by PresetControls to detect dirty/matching state.
    {
        MidiBindingManager a;
        MidiBindingManager b;
        assert(a.equals(b));  // both empty

        a.setBinding(MidiTarget::VoiceVolume, MidiBinding{MidiEvent::Type::ControlChange, 1, 21});
        assert(!a.equals(b));

        b.setBinding(MidiTarget::VoiceVolume, MidiBinding{MidiEvent::Type::ControlChange, 1, 21});
        assert(a.equals(b));

        // A single differing field (channel) is enough to break equality.
        b.setBinding(MidiTarget::VoiceVolume, MidiBinding{MidiEvent::Type::ControlChange, 2, 21});
        assert(!a.equals(b));
    }

    std::cout << "MidiBindingManager tests passed" << std::endl;
    return 0;
}
