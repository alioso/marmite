#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>

// Pure C++, JUCE-free — same convention as DrumVoiceModel/
// DrumEvolutionEngine/PatternCloud, which keeps this testable as a bare
// add_executable target with no JUCE linkage (see Tests/*Test.cpp).
// Main.cpp is the only place that knows about juce::MidiMessage; it
// translates each one into a MidiEvent before calling in here. Ported
// near-verbatim from Jerrican's MidiBindingManager.h — this class is
// fully data-agnostic; only the MidiTarget enum below differs.

// The full set of things a MIDI control can drive. Per-voice targets
// (VoiceVolume..VoiceChaosEvoToggle) are bound once and apply to
// whichever voice is currently "focused" — see
// MarmiteEditor::focusedVoiceIndex_ — rather than being bound per voice.
// SelectVoice1..8 move that focus. Transport targets are the one
// exception to "apply to the focused voice" — they're global actions,
// same as Play/Stop/Reset/Randomize's on-screen buttons.
enum class MidiTarget {
    // Per-voice (11): applies to the focused voice.
    VoiceVolume,
    VoiceTone,
    VoiceMotion,
    VoiceDensity,
    VoiceChaos,
    VoiceEnabledToggle,
    VoiceVolumeEvoToggle,
    VoiceToneEvoToggle,
    VoiceMotionEvoToggle,
    VoiceDensityEvoToggle,
    VoiceChaosEvoToggle,
    // Voice focus (8).
    SelectVoice1,
    SelectVoice2,
    SelectVoice3,
    SelectVoice4,
    SelectVoice5,
    SelectVoice6,
    SelectVoice7,
    SelectVoice8,
    // Transport (4).
    TransportPlay,
    TransportStop,
    TransportReset,
    TransportRandomize,
    // Global (8).
    Tempo,
    EvolutionAmount,
    EvolutionSpeed,
    ReverbRoom,
    ReverbDecay,
    DelayTime,
    DelayFeedback,
    MasterVolume,
};

inline constexpr std::array<MidiTarget, 31> kAllMidiTargets{
    MidiTarget::VoiceVolume,
    MidiTarget::VoiceTone,
    MidiTarget::VoiceMotion,
    MidiTarget::VoiceDensity,
    MidiTarget::VoiceChaos,
    MidiTarget::VoiceEnabledToggle,
    MidiTarget::VoiceVolumeEvoToggle,
    MidiTarget::VoiceToneEvoToggle,
    MidiTarget::VoiceMotionEvoToggle,
    MidiTarget::VoiceDensityEvoToggle,
    MidiTarget::VoiceChaosEvoToggle,
    MidiTarget::SelectVoice1,
    MidiTarget::SelectVoice2,
    MidiTarget::SelectVoice3,
    MidiTarget::SelectVoice4,
    MidiTarget::SelectVoice5,
    MidiTarget::SelectVoice6,
    MidiTarget::SelectVoice7,
    MidiTarget::SelectVoice8,
    MidiTarget::TransportPlay,
    MidiTarget::TransportStop,
    MidiTarget::TransportReset,
    MidiTarget::TransportRandomize,
    MidiTarget::Tempo,
    MidiTarget::EvolutionAmount,
    MidiTarget::EvolutionSpeed,
    MidiTarget::ReverbRoom,
    MidiTarget::ReverbDecay,
    MidiTarget::DelayTime,
    MidiTarget::DelayFeedback,
    MidiTarget::MasterVolume,
};

// Single source of truth for target <-> name, used by the bindings popup
// UI and by MidiPresetStore's (de)serialization.
inline const char* midiTargetName(MidiTarget target) {
    switch (target) {
        case MidiTarget::VoiceVolume: return "VoiceVolume";
        case MidiTarget::VoiceTone: return "VoiceTone";
        case MidiTarget::VoiceMotion: return "VoiceMotion";
        case MidiTarget::VoiceDensity: return "VoiceDensity";
        case MidiTarget::VoiceChaos: return "VoiceChaos";
        case MidiTarget::VoiceEnabledToggle: return "VoiceEnabledToggle";
        case MidiTarget::VoiceVolumeEvoToggle: return "VoiceVolumeEvoToggle";
        case MidiTarget::VoiceToneEvoToggle: return "VoiceToneEvoToggle";
        case MidiTarget::VoiceMotionEvoToggle: return "VoiceMotionEvoToggle";
        case MidiTarget::VoiceDensityEvoToggle: return "VoiceDensityEvoToggle";
        case MidiTarget::VoiceChaosEvoToggle: return "VoiceChaosEvoToggle";
        case MidiTarget::SelectVoice1: return "SelectVoice1";
        case MidiTarget::SelectVoice2: return "SelectVoice2";
        case MidiTarget::SelectVoice3: return "SelectVoice3";
        case MidiTarget::SelectVoice4: return "SelectVoice4";
        case MidiTarget::SelectVoice5: return "SelectVoice5";
        case MidiTarget::SelectVoice6: return "SelectVoice6";
        case MidiTarget::SelectVoice7: return "SelectVoice7";
        case MidiTarget::SelectVoice8: return "SelectVoice8";
        case MidiTarget::TransportPlay: return "TransportPlay";
        case MidiTarget::TransportStop: return "TransportStop";
        case MidiTarget::TransportReset: return "TransportReset";
        case MidiTarget::TransportRandomize: return "TransportRandomize";
        case MidiTarget::Tempo: return "Tempo";
        case MidiTarget::EvolutionAmount: return "EvolutionAmount";
        case MidiTarget::EvolutionSpeed: return "EvolutionSpeed";
        case MidiTarget::ReverbRoom: return "ReverbRoom";
        case MidiTarget::ReverbDecay: return "ReverbDecay";
        case MidiTarget::DelayTime: return "DelayTime";
        case MidiTarget::DelayFeedback: return "DelayFeedback";
        case MidiTarget::MasterVolume: return "MasterVolume";
    }
    return "";
}

inline std::optional<MidiTarget> midiTargetFromName(const std::string& name) {
    for (const auto target : kAllMidiTargets) {
        if (name == midiTargetName(target)) {
            return target;
        }
    }
    return std::nullopt;
}

// A controller message reduced to just what binding/dispatch cares about.
// Main.cpp only ever constructs this for CC messages and NoteOn messages
// with velocity > 0 — note-off and everything else is filtered upstream.
struct MidiEvent {
    enum class Type { ControlChange, NoteOn };
    Type type = Type::ControlChange;
    int channel = 1;  // 1-16
    int number = 0;   // CC number, or note number for NoteOn
    float value = 0.0f;  // normalized 0..1 (CC/127, or velocity/127)
};

struct MidiBinding {
    MidiEvent::Type type = MidiEvent::Type::ControlChange;
    int channel = 1;
    int number = 0;

    bool matches(const MidiEvent& event) const {
        return type == event.type && channel == event.channel && number == event.number;
    }

    bool operator==(const MidiBinding& other) const {
        return type == other.type && channel == other.channel && number == other.number;
    }
};

// Owns the binding table (target -> physical control) and the learn-mode
// state machine. Knows nothing about DrumVoiceModel/DrumEvolutionEngine/
// audio — Main.cpp's MIDI callback resolves a MidiTarget from
// handleEvent() and decides what to do with it.
class MidiBindingManager {
public:
    // Guards bindings_/learning_/learningTarget_ below. This class is only
    // ever touched from the MIDI thread (handleEvent, via
    // MarmiteEditor::handleIncomingMidiMessage) and the UI thread (the
    // bindings popup's Learn/Clear clicks and its refresh timer) — never
    // the audio thread — so a plain mutex is the right tool here, unlike
    // DrumVoiceModel/DrumEvolutionEngine's atomics, which exist
    // specifically to stay lock-free for the audio thread. Blocking
    // briefly on the MIDI thread is harmless; MIDI input runs on its own
    // callback thread, not the realtime audio device thread.
    void armLearn(MidiTarget target) {
        std::lock_guard<std::mutex> lock(mutex_);
        learning_ = true;
        learningTarget_ = target;
    }

    void cancelLearn() {
        std::lock_guard<std::mutex> lock(mutex_);
        learning_ = false;
    }

    bool isLearning() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return learning_;
    }

    MidiTarget learningTarget() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return learningTarget_;
    }

    // While learning: completes the binding for the armed target (stealing
    // it from any other target that held the same physical control, so one
    // knob/pad never silently drives two things), exits learn mode, and
    // returns nullopt. Otherwise: resolves the event to a bound target, if
    // any.
    std::optional<MidiTarget> handleEvent(const MidiEvent& event) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (learning_) {
            for (auto& binding : bindings_) {
                if (binding.has_value() && binding->matches(event)) {
                    binding.reset();
                }
            }
            bindings_[indexOf(learningTarget_)] =
                MidiBinding{event.type, event.channel, event.number};
            learning_ = false;
            return std::nullopt;
        }

        for (const auto target : kAllMidiTargets) {
            const auto& binding = bindings_[indexOf(target)];
            if (binding.has_value() && binding->matches(event)) {
                return target;
            }
        }
        return std::nullopt;
    }

    void clearBinding(MidiTarget target) {
        std::lock_guard<std::mutex> lock(mutex_);
        bindings_[indexOf(target)].reset();
    }

    std::optional<MidiBinding> getBinding(MidiTarget target) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return bindings_[indexOf(target)];
    }

    void setBinding(MidiTarget target, MidiBinding binding) {
        std::lock_guard<std::mutex> lock(mutex_);
        bindings_[indexOf(target)] = binding;
    }

    void clearAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& binding : bindings_) {
            binding.reset();
        }
        learning_ = false;
    }

    // Whether this manager's binding table matches another's — used by
    // PresetControls to tell whether the live bindings still match a
    // selected preset (not dirty) or a saved preset happens to match the
    // live state (so it should auto-select). Ignores learn-mode state,
    // which isn't part of what a preset captures.
    bool equals(const MidiBindingManager& other) const {
        for (const auto target : kAllMidiTargets) {
            if (getBinding(target) != other.getBinding(target)) {
                return false;
            }
        }
        return true;
    }

private:
    static std::size_t indexOf(MidiTarget target) {
        return static_cast<std::size_t>(
            std::distance(kAllMidiTargets.begin(),
                          std::find(kAllMidiTargets.begin(), kAllMidiTargets.end(), target)));
    }

    mutable std::mutex mutex_;
    std::array<std::optional<MidiBinding>, kAllMidiTargets.size()> bindings_{};
    bool learning_ = false;
    MidiTarget learningTarget_ = MidiTarget::VoiceVolume;
};
