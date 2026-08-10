#pragma once

#include <JuceHeader.h>

#include <array>
#include <mutex>
#include <optional>

#include "AudioRecorder.h"
#include "DelayLine.h"
#include "DrumEvolutionEngine.h"
#include "DrumVoiceModel.h"
#include "FastRandom.h"
#include "GroovePattern.h"
#include "GrooveProfiles.h"
#include "MidiBindingManager.h"
#include "MidiPresetStore.h"
#include "PatternClock.h"
#include "ProceduralKit.h"
#include "SampleVoicePool.h"
#include "SceneState.h"
#include "ScenePresetStore.h"
#include "SpaceEvolver.h"

class MarmiteAudioProcessorEditor;  // defined in MarmiteEditor.h, included at the bottom.

// Owns every piece of live engine state (voices, groove patterns,
// evolution, reverb/delay, MIDI Learn/Scenes stores, sample buffers, the
// recorder) and does all audio/MIDI processing — the AudioProcessor half
// of the Processor/Editor split. Any juce::Component work belongs in
// MarmiteAudioProcessorEditor instead; this class must stay usable with
// no editor ever created (headless hosts, offline rendering).
class MarmiteAudioProcessor : public juce::AudioProcessor {
public:
    using VoiceModel = DrumVoiceModel;

    struct InitialDrumVoice {
        const char* name;
        bool enabled;
        float volume;
        float tone;
        float motion;
        float density;
        float chaos;
        // Bounds autonomous Density retargeting (DrumEvolutionEngine::
        // setDensityRange) — gives each voice a lasting character (some
        // consistently sparse, some consistently busy) instead of every
        // voice's density independently averaging toward the same
        // "medium, always something" over time. Manual edits and
        // Randomize both ignore this and can set density anywhere.
        float densityRangeLow;
        float densityRangeHigh;
    };

    // Rough "basic rock kit" starting point: Kick/Snare locked to the
    // grid (chaos=0) at a quarter/backbeat-ish density, hats busier,
    // Crash rare, Glitch deliberately leaning into chaos by identity.
    // Order matches ProceduralKit::makeDefaultKit.
    static constexpr std::array<InitialDrumVoice, 8> kInitialVoices{{
        {"Kick", true, 0.9f, 0.5f, 0.1f, 0.25f, 0.0f, 0.10f, 0.45f},
        {"Snare", true, 0.85f, 0.5f, 0.15f, 0.15f, 0.05f, 0.05f, 0.35f},
        {"Clap", true, 0.6f, 0.5f, 0.15f, 0.08f, 0.1f, 0.0f, 0.25f},
        {"Closed Hat", true, 0.5f, 0.5f, 0.2f, 0.8f, 0.1f, 0.4f, 1.0f},
        {"Open Hat", true, 0.45f, 0.5f, 0.2f, 0.1f, 0.15f, 0.0f, 0.3f},
        {"Perc", true, 0.4f, 0.5f, 0.3f, 0.2f, 0.3f, 0.0f, 0.5f},
        {"Crash", true, 0.55f, 0.5f, 0.2f, 0.03f, 0.1f, 0.0f, 0.15f},
        {"Glitch", true, 0.35f, 0.5f, 0.4f, 0.06f, 0.7f, 0.0f, 0.6f},
    }};

    // GM percussion key per voice (order matches kInitialVoices/
    // ProceduralKit::makeDefaultKit), so a generic drum VST/hardware
    // module receiving MIDI Out lands on roughly the right sound without
    // any manual mapping. Channel 10 is the GM convention for drums.
    static constexpr std::array<int, 8> kVoiceMidiNotes{
        36,  // Kick — Bass Drum 1
        38,  // Snare — Acoustic Snare
        39,  // Clap — Hand Clap
        42,  // Closed Hat — Closed Hi-Hat
        46,  // Open Hat — Open Hi-Hat
        47,  // Perc — Low-Mid Tom
        49,  // Crash — Crash Cymbal 1
        37,  // Glitch — Side Stick (closest GM stand-in for a non-kit voice)
    };
    static constexpr int kMidiDrumChannel = 10;

    // How far a voice's Busy knob can push that voice's effective Wild
    // away from the room's global Wild baseline. The knob's own 0.5
    // center means "no offset, follow the room exactly."
    static constexpr float kWildSpread = 0.8f;

    struct DelayDivision {
        const char* label;
        float beatFraction;
    };
    // A quarter note is 1 full beat; the rest are fractions of it.
    // Dotted values are 1.5x their base division.
    static constexpr std::array<DelayDivision, 6> kDelayDivisions{{
        {"1/4", 1.0f},
        {"1/8", 0.5f},
        {"1/8.", 0.75f},
        {"1/16", 0.25f},
        {"1/16.", 0.375f},
        {"1/32", 0.125f},
    }};

    static VoiceModel makeVoiceModel(const InitialDrumVoice& initial) {
        return VoiceModel(initial.name, initial.enabled, initial.volume, initial.tone,
                          initial.motion, initial.density, initial.chaos);
    }

    // Generates the starting (4/4) accent profile for all 8 voices — used
    // to initialize currentMeterProfiles_ before groovePatterns_ (which
    // holds pointers into it) is constructed.
    static std::array<GrooveProfiles::AccentProfile, 8> makeInitialMeterProfiles() {
        std::array<GrooveProfiles::AccentProfile, 8> profiles{};
        const auto& meter = GrooveProfiles::kMeters[GrooveProfiles::kDefaultMeterIndex];
        for (std::size_t i = 0; i < profiles.size(); ++i) {
            profiles[i] = GrooveProfiles::generateProfile(static_cast<int>(i), meter);
        }
        return profiles;
    }

    MarmiteAudioProcessor()
        : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo())),
          voices_{makeVoiceModel(kInitialVoices[0]), makeVoiceModel(kInitialVoices[1]),
                  makeVoiceModel(kInitialVoices[2]), makeVoiceModel(kInitialVoices[3]),
                  makeVoiceModel(kInitialVoices[4]), makeVoiceModel(kInitialVoices[5]),
                  makeVoiceModel(kInitialVoices[6]), makeVoiceModel(kInitialVoices[7])},
          evolutionEngines_{DrumEvolutionEngine(0x37a1f2c9u), DrumEvolutionEngine(0x6b4d8e12u),
                             DrumEvolutionEngine(0xa9c3f501u), DrumEvolutionEngine(0xe1d47b6au),
                             DrumEvolutionEngine(0x2c5f9a3du), DrumEvolutionEngine(0x8b1e64f7u),
                             DrumEvolutionEngine(0xf4d27a19u), DrumEvolutionEngine(0x593bce82u)},
          currentMeterProfiles_(makeInitialMeterProfiles()),
          groovePatterns_{
              GroovePattern(0x6a1c2e3du, &currentMeterProfiles_[0], currentMeterSlotCount_),
              GroovePattern(0x1f4b8d9au, &currentMeterProfiles_[1], currentMeterSlotCount_),
              GroovePattern(0x9c3e7a2bu, &currentMeterProfiles_[2], currentMeterSlotCount_),
              GroovePattern(0x5d8f1c4eu, &currentMeterProfiles_[3], currentMeterSlotCount_),
              GroovePattern(0xb2a6d9c1u, &currentMeterProfiles_[4], currentMeterSlotCount_),
              GroovePattern(0x37e4f8b0u, &currentMeterProfiles_[5], currentMeterSlotCount_),
              GroovePattern(0xe1c5a3f7u, &currentMeterProfiles_[6], currentMeterSlotCount_),
              GroovePattern(0x4a9d2e6cu, &currentMeterProfiles_[7], currentMeterSlotCount_),
          } {
        for (std::size_t i = 0; i < voices_.size(); ++i) {
            const auto& initial = kInitialVoices[i];
            evolutionEngines_[i].resetTo(initial.volume, initial.tone, initial.motion,
                                         initial.density, initial.chaos);
            evolutionEngines_[i].setDensityRange(initial.densityRangeLow, initial.densityRangeHigh);
        }
        audioFormatManager_.registerBasicFormats();
        recordingThread_.startThread();
    }

    ~MarmiteAudioProcessor() override {
        recorder_.stop();
        recordingThread_.stopThread(2000);
    }

    const juce::String getName() const override { return "Marmite"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void prepareToPlay(double sampleRate, int /*samplesPerBlock*/) override {
        patternClock_.setSampleRate(sampleRate);
        patternClock_.setBpm(tempo_.load(std::memory_order_relaxed));
        for (auto& engine : evolutionEngines_) {
            engine.setSampleRate(sampleRate);
        }
        // Procedural kit is synthesized once the real device sample rate
        // is known, so playback pitch is correct regardless of device.
        // Kept separately from sampleBuffers_ (the live playback array) so
        // Clear/Reset can restore a voice to its default without
        // resynthesizing the whole kit each time.
        defaultKit_ = ProceduralKit::makeDefaultKit(sampleRate);
        {
            const std::lock_guard<std::mutex> lock(sampleBuffersMutex_);
            sampleBuffers_ = defaultKit_;
        }
        reverb_.setSampleRate(sampleRate);
        delayLine_.setSampleRate(sampleRate);
        sampleRate_ = sampleRate;
    }

    void releaseResources() override {}

    // Runs on the audio thread. Every write below goes through the same
    // atomic DrumVoiceModel/DrumEvolutionEngine setters already used from
    // the UI thread — safe under the lock-free pattern already
    // established throughout this codebase.
    void handleMidiMessage(const juce::MidiMessage& message) {
        std::optional<MidiEvent> event;
        if (message.isController()) {
            event = MidiEvent{MidiEvent::Type::ControlChange, message.getChannel(),
                              message.getControllerNumber(),
                              static_cast<float>(message.getControllerValue()) / 127.0f};
        } else if (message.isNoteOn()) {
            event = MidiEvent{MidiEvent::Type::NoteOn, message.getChannel(),
                              message.getNoteNumber(),
                              static_cast<float>(message.getVelocity()) / 127.0f};
        } else {
            return;
        }

        const auto target = midiBindings_.handleEvent(*event);
        if (target.has_value()) {
            applyMidiTarget(*target, event->value);
        }
    }

    void applyMidiTarget(MidiTarget target, float value) {
        const int focused = focusedVoiceIndex_.load(std::memory_order_relaxed);
        auto& voice = voices_[static_cast<std::size_t>(focused)];
        auto& evolution = evolutionEngines_[static_cast<std::size_t>(focused)];

        switch (target) {
            case MidiTarget::VoiceVolume:
                voice.setVolume(value);
                evolution.resyncVolume(value);
                break;
            case MidiTarget::VoiceTone:
                voice.setTone(value);
                evolution.resyncTone(value);
                break;
            case MidiTarget::VoiceMotion:
                voice.setMotion(value);
                evolution.resyncMotion(value);
                break;
            case MidiTarget::VoiceDensity:
                voice.setDensity(value);
                evolution.resyncDensity(value);
                break;
            case MidiTarget::VoiceChaos:
                voice.setChaos(value);
                evolution.resyncChaos(value);
                break;
            case MidiTarget::VoiceEnabledToggle:
                voice.setEnabled(!voice.isEnabled());
                break;
            case MidiTarget::VoiceVolumeEvoToggle: {
                const bool on = !evolution.isVolumeEnabled();
                evolution.setVolumeEnabled(on);
                if (on) evolution.resyncVolume(voice.getVolume());
                break;
            }
            case MidiTarget::VoiceToneEvoToggle: {
                const bool on = !evolution.isToneEnabled();
                evolution.setToneEnabled(on);
                if (on) evolution.resyncTone(voice.getTone());
                break;
            }
            case MidiTarget::VoiceMotionEvoToggle: {
                const bool on = !evolution.isMotionEnabled();
                evolution.setMotionEnabled(on);
                if (on) evolution.resyncMotion(voice.getMotion());
                break;
            }
            case MidiTarget::VoiceDensityEvoToggle: {
                const bool on = !evolution.isDensityEnabled();
                evolution.setDensityEnabled(on);
                if (on) evolution.resyncDensity(voice.getDensity());
                break;
            }
            case MidiTarget::VoiceChaosEvoToggle: {
                const bool on = !evolution.isChaosEnabled();
                evolution.setChaosEnabled(on);
                if (on) evolution.resyncChaos(voice.getChaos());
                break;
            }
            case MidiTarget::SelectVoice1: focusedVoiceIndex_.store(0, std::memory_order_relaxed); break;
            case MidiTarget::SelectVoice2: focusedVoiceIndex_.store(1, std::memory_order_relaxed); break;
            case MidiTarget::SelectVoice3: focusedVoiceIndex_.store(2, std::memory_order_relaxed); break;
            case MidiTarget::SelectVoice4: focusedVoiceIndex_.store(3, std::memory_order_relaxed); break;
            case MidiTarget::SelectVoice5: focusedVoiceIndex_.store(4, std::memory_order_relaxed); break;
            case MidiTarget::SelectVoice6: focusedVoiceIndex_.store(5, std::memory_order_relaxed); break;
            case MidiTarget::SelectVoice7: focusedVoiceIndex_.store(6, std::memory_order_relaxed); break;
            case MidiTarget::SelectVoice8: focusedVoiceIndex_.store(7, std::memory_order_relaxed); break;
            case MidiTarget::TransportPlay:
                handlePlayPressed();
                break;
            case MidiTarget::TransportStop:
                handleStopPressed();
                break;
            case MidiTarget::TransportReset:
                handleResetPressed();
                break;
            case MidiTarget::TransportRandomize:
                handleRandomizePressed();
                break;
            case MidiTarget::Tempo: {
                const float bpm = 40.0f + value * (240.0f - 40.0f);
                tempo_.store(bpm, std::memory_order_relaxed);
                patternClock_.setBpm(bpm);
                break;
            }
            case MidiTarget::EvolutionAmount:
                evolutionAmount_.store(value, std::memory_order_relaxed);
                break;
            case MidiTarget::EvolutionSpeed:
                evolutionSpeed_.store(value, std::memory_order_relaxed);
                break;
            case MidiTarget::Space:
                spaceDisplay_.store(value, std::memory_order_relaxed);
                spaceEvolver_.resync(value);
                break;
            case MidiTarget::ReverbRoom:
                reverbRoom_.store(value, std::memory_order_relaxed);
                break;
            case MidiTarget::ReverbDecay:
                reverbDecay_.store(value, std::memory_order_relaxed);
                break;
            // Quantizes the continuous 0..1 MIDI value onto the fixed set
            // of note divisions, same domain the Time combo box offers.
            case MidiTarget::DelayTime: {
                const int index = juce::jlimit(
                    0, static_cast<int>(kDelayDivisions.size()) - 1,
                    static_cast<int>(value * static_cast<float>(kDelayDivisions.size())));
                delayBeatFraction_.store(kDelayDivisions[static_cast<std::size_t>(index)].beatFraction,
                                         std::memory_order_relaxed);
                break;
            }
            case MidiTarget::DelayFeedback:
                delayFeedback_.store(value, std::memory_order_relaxed);
                break;
            case MidiTarget::MasterVolume:
                masterVolume_.store(value, std::memory_order_relaxed);
                break;
            case MidiTarget::Wild:
                wildDisplay_.store(value, std::memory_order_relaxed);
                wildEvolver_.resync(value);
                break;
            // Quantizes onto the fixed set of 7 meters, same idiom as
            // DelayTime above.
            case MidiTarget::Meter: {
                const int index = juce::jlimit(
                    0, static_cast<int>(GrooveProfiles::kMeters.size()) - 1,
                    static_cast<int>(value * static_cast<float>(GrooveProfiles::kMeters.size())));
                requestMeter(GrooveProfiles::kMeters[static_cast<std::size_t>(index)].numerator,
                             GrooveProfiles::kMeters[static_cast<std::size_t>(index)].denominator);
                break;
            }
        }
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ScopedNoDenormals noDenormals;

        for (const auto metadata : midiMessages) {
            handleMidiMessage(metadata.getMessage());
        }
        // The same MidiBuffer doubles as the output buffer — clear the
        // input we already consumed above, then fill it with outgoing
        // GM-note mirrors of this block's pattern triggers below.
        midiMessages.clear();

        const int pendingMeter = pendingMeterIndex_.exchange(-1, std::memory_order_relaxed);
        if (pendingMeter >= 0) {
            applyMeterChange(pendingMeter);
        }
        if (pendingPhaseReset_.exchange(false, std::memory_order_relaxed)) {
            resetPatternPhase();
        }

        processHostSync();

        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
        const int numSamples = buffer.getNumSamples();

        const bool playing = isPlaying_.load(std::memory_order_relaxed);
        const float evolutionAmount = evolutionAmount_.load(std::memory_order_relaxed);
        const float evolutionSpeed = evolutionSpeed_.load(std::memory_order_relaxed);
        const float masterVolume = masterVolume_.load(std::memory_order_relaxed);

        const float beatFraction = delayBeatFraction_.load(std::memory_order_relaxed);
        const double samplesPerBeat = patternClock_.getSamplesPerSubdivision() * 4.0;
        delayLine_.setDelaySamples(static_cast<int>(samplesPerBeat * beatFraction));
        delayLine_.setFeedback(delayFeedback_.load(std::memory_order_relaxed));
        delayLine_.setWet(0.3f);

        // sampleBuffers_ can be swapped out from under this callback by
        // Load Sample on the message thread (see sampleBuffersMutex_).
        // Held for the whole block rather than per-trigger: cheap since
        // it's essentially always uncontended, and it guarantees every
        // sample in this block sees one consistent set of buffers.
        const std::lock_guard<std::mutex> sampleBuffersLock(sampleBuffersMutex_);

        for (int sample = 0; sample < numSamples; ++sample) {
            const bool onGridBoundary = playing && patternClock_.tick();
            if (onGridBoundary) {
                currentSlot16_ = (currentSlot16_ + 1) % currentMeterSlotCount_;
                currentSlot16Display_.store(currentSlot16_, std::memory_order_relaxed);
            }

            float mixedLeft = 0.0f;
            float mixedRight = 0.0f;

            const float space = spaceEvolver_.update(playing ? evolutionAmount : 0.0f, evolutionSpeed,
                                                     sampleRate_, spaceDisplay_);
            const float wild = wildEvolver_.update(playing ? evolutionAmount : 0.0f, evolutionSpeed,
                                                   sampleRate_, wildDisplay_);

            for (std::size_t i = 0; i < voices_.size(); ++i) {
                auto& voice = voices_[i];

                evolutionEngines_[i].update(voice, playing ? evolutionAmount : 0.0f, evolutionSpeed);

                if (playing && voice.isEnabled()) {
                    // The per-voice Busy knob offsets this voice's own
                    // effective Wild away from the room's global Wild
                    // baseline — 0.5 (its default/center) means "follow
                    // the room exactly."
                    const float effectiveWild = juce::jlimit(
                        0.0f, 1.0f, wild + (voice.getChaos() - 0.5f) * kWildSpread);
                    std::optional<float> triggerVelocity;
                    float triggerPitchSemitones = 0.0f;
                    if (const auto trigger = groovePatterns_[i].update(
                            onGridBoundary, currentSlot16_, voice.getDensity() * space, effectiveWild,
                            voice.getMotion(), evolutionAmount,
                            patternClock_.getSamplesPerSubdivision())) {
                        triggerVelocity = trigger->velocity;
                        triggerPitchSemitones = trigger->pitchSemitones;
                    }
                    if (triggerVelocity.has_value()) {
                        // Tone sets the voice's base pitch; Motion's
                        // per-hit jitter (triggerPitchSemitones, from
                        // whichever engine just fired) rides on top of
                        // it — previously computed by both engines but
                        // never actually applied here, so Motion only
                        // ever affected velocity, not pitch.
                        const float pitchSemitones =
                            (voice.getTone() - 0.5f) * 24.0f + triggerPitchSemitones;
                        const float velocity = *triggerVelocity * voice.getVolume();
                        samplePools_[i].trigger(&sampleBuffers_[i], pitchSemitones, velocity);

                        // Mirrors the trigger as a GM note in the output
                        // MidiBuffer, so an external drum VST/hardware can
                        // be driven by the same generative pattern instead
                        // of (or alongside) the internal kit. The host (or
                        // JUCE's Standalone MIDI-output device picker)
                        // handles actual routing — this processor only
                        // ever writes into its own output buffer.
                        const auto midiVelocity = static_cast<juce::uint8>(
                            juce::jlimit(1, 127, static_cast<int>(velocity * 127.0f)));
                        const int note = kVoiceMidiNotes[i];
                        midiMessages.addEvent(juce::MidiMessage::noteOn(kMidiDrumChannel, note, midiVelocity),
                                              sample);
                        midiMessages.addEvent(juce::MidiMessage::noteOff(kMidiDrumChannel, note),
                                              std::min(sample + 1, numSamples - 1));
                    }
                }

                const auto voiceSample = samplePools_[i].renderSample();
                mixedLeft += voiceSample.left;
                mixedRight += voiceSample.right;
            }

            delayLine_.processSample(mixedLeft, mixedRight);

            constexpr float headroom = 0.5f;
            left[sample] = mixedLeft * headroom * masterVolume;
            if (right != nullptr) {
                right[sample] = mixedRight * headroom * masterVolume;
            }
        }

        if (right != nullptr) {
            const float room = reverbRoom_.load(std::memory_order_relaxed);
            const float decay = reverbDecay_.load(std::memory_order_relaxed);

            juce::Reverb::Parameters reverbParams;
            reverbParams.wetLevel = room * 0.5f;
            reverbParams.dryLevel = 0.5f;
            reverbParams.roomSize = juce::jlimit(0.0f, 1.0f, 0.25f + decay * 0.65f + room * 0.1f);
            reverbParams.damping = juce::jlimit(0.0f, 1.0f, 1.0f - decay * 0.75f);
            reverbParams.width = 1.0f;
            reverbParams.freezeMode = 0.0f;
            reverb_.setParameters(reverbParams);
            reverb_.processStereo(left, right, numSamples);
        }

        const float* recordChannels[2] = {left, right != nullptr ? right : left};
        recorder_.recordBlock(recordChannels, numSamples);
    }

    void getStateInformation(juce::MemoryBlock& destData) override {
        const auto text = ScenePresetStore::serialize(captureSceneState());
        destData.replaceAll(text.data(), text.size());
    }

    void setStateInformation(const void* data, int sizeInBytes) override {
        const std::string text(static_cast<const char*>(data), static_cast<size_t>(sizeInBytes));
        applySceneState(ScenePresetStore::deserialize(text));
    }

    // Reads every control's current value — everything a Scene (or a
    // plugin instance's own save/reload) captures, deliberately excluding
    // transport run/stop state (isPlaying_).
    SceneState captureSceneState() const {
        SceneState scene;
        for (std::size_t i = 0; i < voices_.size(); ++i) {
            const auto& voice = voices_[i];
            const auto& evolution = evolutionEngines_[i];
            auto& voiceScene = scene.voices[i];
            voiceScene.enabled = voice.isEnabled();
            voiceScene.volume = voice.getVolume();
            voiceScene.tone = voice.getTone();
            voiceScene.motion = voice.getMotion();
            voiceScene.density = voice.getDensity();
            voiceScene.chaos = voice.getChaos();
            voiceScene.volumeEvoEnabled = evolution.isVolumeEnabled();
            voiceScene.toneEvoEnabled = evolution.isToneEnabled();
            voiceScene.motionEvoEnabled = evolution.isMotionEnabled();
            voiceScene.densityEvoEnabled = evolution.isDensityEnabled();
            voiceScene.chaosEvoEnabled = evolution.isChaosEnabled();
            voiceScene.samplePath = sampleFiles_[i] == juce::File()
                                        ? std::string()
                                        : sampleFiles_[i].getFullPathName().toStdString();
        }
        scene.tempo = tempo_.load(std::memory_order_relaxed);
        scene.evolutionAmount = evolutionAmount_.load(std::memory_order_relaxed);
        scene.evolutionSpeed = evolutionSpeed_.load(std::memory_order_relaxed);
        scene.space = spaceDisplay_.load(std::memory_order_relaxed);
        scene.reverbRoom = reverbRoom_.load(std::memory_order_relaxed);
        scene.reverbDecay = reverbDecay_.load(std::memory_order_relaxed);
        scene.delayBeatFraction = delayBeatFraction_.load(std::memory_order_relaxed);
        scene.delayFeedback = delayFeedback_.load(std::memory_order_relaxed);
        scene.masterVolume = masterVolume_.load(std::memory_order_relaxed);
        scene.wild = wildDisplay_.load(std::memory_order_relaxed);
        scene.meterNumerator = meterNumeratorDisplay_.load(std::memory_order_relaxed);
        scene.meterDenominator = meterDenominatorDisplay_.load(std::memory_order_relaxed);
        return scene;
    }

    // Writes a full snapshot back. Touches VoiceModel/EvolutionEngine/
    // atomics and (for sample recall) sampleBuffers_ under its mutex —
    // safe from any thread. Any attached editor re-syncs its own
    // Components from these on its next timer tick, so this stays
    // editor-agnostic.
    void applySceneState(const SceneState& scene) {
        for (std::size_t i = 0; i < voices_.size(); ++i) {
            const auto& voiceScene = scene.voices[i];
            voices_[i].setEnabled(voiceScene.enabled);
            voices_[i].setVolume(voiceScene.volume);
            voices_[i].setTone(voiceScene.tone);
            voices_[i].setMotion(voiceScene.motion);
            voices_[i].setDensity(voiceScene.density);
            voices_[i].setChaos(voiceScene.chaos);

            evolutionEngines_[i].resetTo(voiceScene.volume, voiceScene.tone, voiceScene.motion,
                                         voiceScene.density, voiceScene.chaos);
            evolutionEngines_[i].setVolumeEnabled(voiceScene.volumeEvoEnabled);
            evolutionEngines_[i].setToneEnabled(voiceScene.toneEvoEnabled);
            evolutionEngines_[i].setMotionEnabled(voiceScene.motionEvoEnabled);
            evolutionEngines_[i].setDensityEnabled(voiceScene.densityEvoEnabled);
            evolutionEngines_[i].setChaosEnabled(voiceScene.chaosEvoEnabled);

            // Recall the loaded sample (if any). A path that no longer
            // exists, or that fails to load (moved/corrupted/unsupported
            // since the Scene was saved), falls back to the procedural
            // default audibly rather than silently breaking, with the
            // editor flagging it so it's not a silent surprise either.
            const juce::File sampleFile =
                voiceScene.samplePath.empty() ? juce::File() : juce::File(voiceScene.samplePath);
            if (sampleFile == juce::File()) {
                clearSample(i);
            } else if (!sampleFile.existsAsFile() || !applyLoadedSample(i, sampleFile)) {
                markSampleMissing(i, sampleFile);
            }
        }

        tempo_.store(scene.tempo, std::memory_order_relaxed);
        patternClock_.setBpm(scene.tempo);
        evolutionAmount_.store(scene.evolutionAmount, std::memory_order_relaxed);
        evolutionSpeed_.store(scene.evolutionSpeed, std::memory_order_relaxed);
        spaceDisplay_.store(scene.space, std::memory_order_relaxed);
        spaceEvolver_.resync(scene.space);
        reverbRoom_.store(scene.reverbRoom, std::memory_order_relaxed);
        reverbDecay_.store(scene.reverbDecay, std::memory_order_relaxed);
        delayBeatFraction_.store(scene.delayBeatFraction, std::memory_order_relaxed);
        delayFeedback_.store(scene.delayFeedback, std::memory_order_relaxed);
        masterVolume_.store(scene.masterVolume, std::memory_order_relaxed);
        wildDisplay_.store(scene.wild, std::memory_order_relaxed);
        wildEvolver_.resync(scene.wild);
        requestMeter(scene.meterNumerator, scene.meterDenominator);
    }

    void handlePlayPressed() { isPlaying_.store(true, std::memory_order_relaxed); }
    void handleStopPressed() { isPlaying_.store(false, std::memory_order_relaxed); }

    void handleResetPressed() {
        for (std::size_t i = 0; i < voices_.size(); ++i) {
            const auto& initial = kInitialVoices[i];
            voices_[i].setEnabled(initial.enabled);
            voices_[i].setVolume(initial.volume);
            voices_[i].setTone(initial.tone);
            voices_[i].setMotion(initial.motion);
            voices_[i].setDensity(initial.density);
            voices_[i].setChaos(initial.chaos);
            evolutionEngines_[i].resetTo(initial.volume, initial.tone, initial.motion,
                                         initial.density, initial.chaos);
            evolutionEngines_[i].setDensityRange(initial.densityRangeLow, initial.densityRangeHigh);
            clearSample(i);
        }
        // Space isn't a per-voice control, but it's part of the same
        // "back to defaults" contract — full busy-ness, matching what
        // every voice's own density defaults already assume.
        spaceDisplay_.store(1.0f, std::memory_order_relaxed);
        spaceEvolver_.resync(1.0f);
        // Same "back to defaults" contract for Wild — reset to the
        // ACDC-rigid end.
        wildDisplay_.store(0.0f, std::memory_order_relaxed);
        wildEvolver_.resync(0.0f);
        // Same "back to defaults" contract for the time signature.
        requestMeter(4, 4);
    }

    void handleRandomizePressed() {
        // Rerolls the five macros per voice, regardless of transport
        // state. Tempo, Space, and sample assignment are identity/
        // ensemble-level, not per-voice levers, so none of them are
        // touched here.
        //
        // Density also gets a freshly rolled range (a random center +
        // half-width, density itself landing inside it) rather than just
        // a bare value — so Randomize can hand one voice a narrow,
        // consistently-sparse range and another a wide, unpredictable
        // one, instead of every voice sharing the same full 0..1
        // autonomous-drift range.
        for (std::size_t i = 0; i < voices_.size(); ++i) {
            const float volume = randomizeRandom_.nextFloat01();
            const float tone = randomizeRandom_.nextFloat01();
            const float motion = randomizeRandom_.nextFloat01();
            const float chaos = randomizeRandom_.nextFloat01();

            const float halfWidth = 0.08f + randomizeRandom_.nextFloat01() * 0.27f;
            const float center = halfWidth + randomizeRandom_.nextFloat01() * (1.0f - 2.0f * halfWidth);
            const float densityRangeLow = center - halfWidth;
            const float densityRangeHigh = center + halfWidth;
            const float density =
                densityRangeLow + randomizeRandom_.nextFloat01() * (densityRangeHigh - densityRangeLow);

            voices_[i].setVolume(volume);
            voices_[i].setTone(tone);
            voices_[i].setMotion(motion);
            voices_[i].setDensity(density);
            voices_[i].setChaos(chaos);
            evolutionEngines_[i].resetTo(volume, tone, motion, density, chaos);
            evolutionEngines_[i].setDensityRange(densityRangeLow, densityRangeHigh);
        }
    }

    // Returns whether the load succeeded — callable both from the
    // editor's file chooser callback (a fresh user pick) and from
    // applySceneState (a path recalled from a saved Scene, which might no
    // longer exist or might have changed on disk since). Runs on the
    // message thread; the actual buffer swap is guarded by
    // sampleBuffersMutex_ so the audio thread never reads a buffer
    // mid-resize.
    bool applyLoadedSample(std::size_t voiceIndex, const juce::File& file) {
        std::unique_ptr<juce::AudioFormatReader> reader(
            audioFormatManager_.createReaderFor(file));

        // reader->lengthInSamples is a reader-reported int64 and
        // reader->numChannels is unsigned — both come straight from the
        // file's header, which a corrupt or deliberately malformed file
        // can claim is anything. Narrowing an unbounded int64 to int
        // (for AudioBuffer's constructor) without a cap risks silent
        // truncation into a negative/garbage size, and an unbounded
        // frame count or channel count risks an attacker-controlled huge
        // allocation (a trivial DoS via one bad file). Reject anything
        // outside a generous but firm sanity range instead of trusting
        // the header.
        constexpr juce::int64 kMaxSourceFrames = 44100LL * 60 * 20;  // 20 minutes at 44.1kHz
        constexpr unsigned int kMaxChannels = 32;
        if (reader == nullptr || reader->lengthInSamples <= 0 ||
            reader->lengthInSamples > kMaxSourceFrames || reader->numChannels == 0 ||
            reader->numChannels > kMaxChannels) {
            return false;
        }

        const auto numSourceFrames = static_cast<int>(reader->lengthInSamples);
        juce::AudioBuffer<float> sourceBuffer(static_cast<int>(reader->numChannels), numSourceFrames);
        reader->read(&sourceBuffer, 0, numSourceFrames, 0, true, true);

        // Mix down to mono — the engine's sample-playback path is
        // mono-only (see SampleBuffer.h), matching the procedural kit.
        std::vector<float> mono(static_cast<std::size_t>(numSourceFrames), 0.0f);
        const int numChannels = sourceBuffer.getNumChannels();
        for (int channel = 0; channel < numChannels; ++channel) {
            const float* channelData = sourceBuffer.getReadPointer(channel);
            for (int frame = 0; frame < numSourceFrames; ++frame) {
                mono[static_cast<std::size_t>(frame)] += channelData[frame] / static_cast<float>(numChannels);
            }
        }

        // Linear resample from the file's native rate to the device's
        // current rate, so playback pitch matches what was recorded
        // regardless of which rate the audio device happens to be
        // running at.
        SampleBuffer resampled;
        resampled.sampleRate = sampleRate_;
        const double sourceRate = reader->sampleRate > 0.0 ? reader->sampleRate : sampleRate_;
        const double ratio = sourceRate / sampleRate_;
        const auto numOutFrames =
            static_cast<std::size_t>(static_cast<double>(numSourceFrames) / ratio);
        // A file claiming a very low native sample rate would otherwise
        // blow this count up far past kMaxSourceFrames once divided by a
        // small ratio — cap it independently rather than trusting the
        // arithmetic to stay bounded just because the input was.
        if (numOutFrames == 0 || numOutFrames > static_cast<std::size_t>(kMaxSourceFrames)) {
            return false;
        }
        resampled.samples.resize(numOutFrames);
        for (std::size_t i = 0; i < numOutFrames; ++i) {
            const double sourcePosition = static_cast<double>(i) * ratio;
            const auto index = static_cast<std::size_t>(sourcePosition);
            const float frac = static_cast<float>(sourcePosition - static_cast<double>(index));
            const float a = mono[index];
            const float b = (index + 1 < mono.size()) ? mono[index + 1] : 0.0f;
            resampled.samples[i] = a + (b - a) * frac;
        }

        {
            const std::lock_guard<std::mutex> lock(sampleBuffersMutex_);
            sampleBuffers_[voiceIndex] = std::move(resampled);
        }

        sampleFiles_[voiceIndex] = file;
        sampleMissing_[voiceIndex] = false;
        return true;
    }

    // Reverts a voice back to its procedural default — used by the Clear
    // button, Reset, and as the fallback when a Scene references a sample
    // that's missing/unreadable.
    void clearSample(std::size_t voiceIndex) {
        {
            const std::lock_guard<std::mutex> lock(sampleBuffersMutex_);
            sampleBuffers_[voiceIndex] = defaultKit_[voiceIndex];
        }
        sampleFiles_[voiceIndex] = juce::File();
        sampleMissing_[voiceIndex] = false;
    }

    // Falls back to the procedural default audibly (same buffer swap as
    // clearSample) but keeps the path on record, so a missing/broken
    // Scene reference reads as an error state (via the editor's
    // refreshSampleLabel) rather than silently reverting with no
    // explanation.
    void markSampleMissing(std::size_t voiceIndex, const juce::File& file) {
        {
            const std::lock_guard<std::mutex> lock(sampleBuffersMutex_);
            sampleBuffers_[voiceIndex] = defaultKit_[voiceIndex];
        }
        sampleFiles_[voiceIndex] = file;
        sampleMissing_[voiceIndex] = true;
    }

    const juce::File& getSampleFile(std::size_t voiceIndex) const { return sampleFiles_[voiceIndex]; }
    bool isSampleMissing(std::size_t voiceIndex) const { return sampleMissing_[voiceIndex]; }
    juce::AudioFormatManager& getAudioFormatManager() { return audioFormatManager_; }

    // Toggled from the Editor's Record button. See AudioRecorder.h.
    bool toggleRecording() {
        if (recorder_.isRecording()) {
            recorder_.stop();
            return true;
        }

        const auto directory =
            juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Marmite Recordings");
        const auto filename =
            "Marmite-" + juce::Time::getCurrentTime().formatted("%Y-%m-%d-%H%M%S") + ".wav";
        currentRecordingFile_ = directory.getChildFile(filename);
        return recorder_.startRecording(currentRecordingFile_, sampleRate_);
    }

    bool isRecording() const { return recorder_.isRecording(); }
    const juce::File& getCurrentRecordingFile() const { return currentRecordingFile_; }

    bool isPlaying() const { return isPlaying_.load(std::memory_order_relaxed); }
    int getFocusedVoiceIndex() const { return focusedVoiceIndex_.load(std::memory_order_relaxed); }

    VoiceModel& voice(std::size_t index) { return voices_[index]; }
    DrumEvolutionEngine& evolutionEngine(std::size_t index) { return evolutionEngines_[index]; }

    std::atomic<float>& tempo() { return tempo_; }
    std::atomic<float>& evolutionAmount() { return evolutionAmount_; }
    std::atomic<float>& evolutionSpeed() { return evolutionSpeed_; }
    std::atomic<float>& spaceDisplay() { return spaceDisplay_; }
    std::atomic<float>& reverbRoom() { return reverbRoom_; }
    std::atomic<float>& reverbDecay() { return reverbDecay_; }
    std::atomic<float>& delayFeedback() { return delayFeedback_; }
    std::atomic<float>& delayBeatFraction() { return delayBeatFraction_; }
    std::atomic<float>& masterVolume() { return masterVolume_; }

    void setTempo(float bpm) {
        tempo_.store(bpm, std::memory_order_relaxed);
        patternClock_.setBpm(bpm);
    }

    void setSpace(float value) {
        spaceDisplay_.store(value, std::memory_order_relaxed);
        spaceEvolver_.resync(value);
    }

    std::atomic<float>& wildDisplay() { return wildDisplay_; }
    void setWild(float value) {
        wildDisplay_.store(value, std::memory_order_relaxed);
        wildEvolver_.resync(value);
    }

    // Queues a meter change, consumed on the audio thread at the top of
    // the next processBlock (see applyMeterChange) — meter changes touch
    // non-atomic per-voice arrays, so they can't be applied directly from
    // the UI/MIDI thread. Falls back to 4/4 if the pair isn't one of the
    // 7 supported meters.
    void requestMeter(int numerator, int denominator) {
        pendingMeterIndex_.store(GrooveProfiles::findMeterIndex(numerator, denominator),
                                 std::memory_order_relaxed);
    }

    // Re-snaps the pattern back to beat 1 without touching any knob or
    // Scene state — a lightweight "resync the clock" independent of
    // Reset (which also reverts every voice to its defaults). Clicked
    // from the beat counter itself (see BeatPulseIndicator::onClick).
    // Same queued-request pattern as requestMeter, for the same reason:
    // touches non-atomic per-voice pattern state, so it can't be applied
    // directly from the UI thread.
    void requestPhaseReset() { pendingPhaseReset_.store(true, std::memory_order_relaxed); }

    int meterNumeratorDisplay() const { return meterNumeratorDisplay_.load(std::memory_order_relaxed); }
    int meterDenominatorDisplay() const { return meterDenominatorDisplay_.load(std::memory_order_relaxed); }
    int currentSlot16Display() const { return currentSlot16Display_.load(std::memory_order_relaxed); }

    bool hostSyncEnabled() const { return hostSyncEnabled_.load(std::memory_order_relaxed); }
    void setHostSyncEnabled(bool enabled) { hostSyncEnabled_.store(enabled, std::memory_order_relaxed); }

    MidiBindingManager midiBindings_;
    MidiPresetStore midiPresetStore_{
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Marmite")
            .getChildFile("MidiPresets")
            .getFullPathName()
            .toStdString()};
    ScenePresetStore scenePresetStore_{
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Marmite")
            .getChildFile("Scenes")
            .getFullPathName()
            .toStdString()};
    juce::String currentMidiPresetName_;
    juce::String currentSceneName_;

private:
    // Snaps back to beat 1 (slot 0) without touching the meter or any
    // voice/knob state — just re-phases the existing pattern, same as
    // the phase-snap half of applyMeterChange but with no profile
    // regeneration since the meter itself hasn't changed.
    void resetPatternPhase() {
        currentSlot16_ = -1;
        currentSlot16Display_.store(-1, std::memory_order_relaxed);
        for (auto& pattern : groovePatterns_) {
            pattern.forceRegenerateNextBoundary();
        }
    }

    // Regenerates every voice's accent profile for the new meter,
    // reseats each GroovePattern onto it, and forces a fresh mask (the
    // old one was sized/shaped for the previous meter). Only ever called
    // from the audio thread (top of processBlock), so no locking needed
    // around currentMeterProfiles_/groovePatterns_.
    void applyMeterChange(int meterIndex) {
        const auto& meter = GrooveProfiles::kMeters[static_cast<std::size_t>(meterIndex)];
        for (std::size_t i = 0; i < groovePatterns_.size(); ++i) {
            currentMeterProfiles_[i] = GrooveProfiles::generateProfile(static_cast<int>(i), meter);
            groovePatterns_[i].setAccentProfile(&currentMeterProfiles_[i], meter.totalSlots);
            groovePatterns_[i].forceRegenerateNextBoundary();
        }
        currentMeterSlotCount_ = meter.totalSlots;
        currentSlot16_ = -1;
        currentSlot16Display_.store(-1, std::memory_order_relaxed);
        meterNumeratorDisplay_.store(meter.numerator, std::memory_order_relaxed);
        meterDenominatorDisplay_.store(meter.denominator, std::memory_order_relaxed);
    }

    // Reads the host's transport once per block when Host Sync is
    // enabled: mirrors host Play/Stop into isPlaying_, snaps the pattern
    // phase to the host's bar position on a transport start (or a large
    // ppq jump mid-playback, e.g. a host loop point), and continuously
    // locks tempo to the host's reported BPM. Standalone's playhead never
    // reports real transport/tempo data, so this is a no-op there even if
    // somehow left enabled.
    void processHostSync() {
        if (!hostSyncEnabled_.load(std::memory_order_relaxed)) {
            return;
        }
        auto* playHead = getPlayHead();
        if (playHead == nullptr) {
            return;
        }
        const auto position = playHead->getPosition();
        if (!position.hasValue()) {
            return;
        }

        const bool hostPlaying = position->getIsPlaying();
        bool shouldSnap = false;

        if (hostPlaying && !lastHostPlaying_) {
            shouldSnap = true;
        } else if (hostPlaying) {
            if (const auto ppq = position->getPpqPosition()) {
                const double barBeats = static_cast<double>(currentMeterSlotCount_) / 4.0;
                if (hasLastHostPpq_ && (*ppq < lastHostPpq_ - 1.0e-6 || *ppq - lastHostPpq_ > barBeats)) {
                    shouldSnap = true;
                }
                lastHostPpq_ = *ppq;
                hasLastHostPpq_ = true;
            }
        } else {
            hasLastHostPpq_ = false;
        }

        if (shouldSnap) {
            isPlaying_.store(true, std::memory_order_relaxed);
            const auto ppq = position->getPpqPosition();
            const auto barStart = position->getPpqPositionOfLastBarStart();
            if (ppq.hasValue() && barStart.hasValue()) {
                const double ppqIntoBar = *ppq - *barStart;
                const int rawSlot = static_cast<int>(ppqIntoBar * 4.0) % currentMeterSlotCount_;
                const int slot = ((rawSlot % currentMeterSlotCount_) + currentMeterSlotCount_) %
                                 currentMeterSlotCount_;
                currentSlot16_ = slot - 1;  // the next tick's ++ lands exactly on `slot`
                patternClock_.reset();
                for (auto& pattern : groovePatterns_) {
                    pattern.forceRegenerateNextBoundary();
                }
            }
        } else if (!hostPlaying && lastHostPlaying_) {
            isPlaying_.store(false, std::memory_order_relaxed);
        }
        lastHostPlaying_ = hostPlaying;

        if (const auto bpm = position->getBpm()) {
            const float bpmFloat = static_cast<float>(*bpm);
            tempo_.store(bpmFloat, std::memory_order_relaxed);
            patternClock_.setBpm(bpmFloat);
        }
    }

    std::array<VoiceModel, 8> voices_;
    std::array<DrumEvolutionEngine, 8> evolutionEngines_;
    // Declared (and thus constructed) before currentMeterProfiles_/
    // groovePatterns_ below, since both depend on it — C++ initializes
    // members in declaration order regardless of the initializer list's
    // order.
    int currentMeterSlotCount_ = GrooveProfiles::kMeters[GrooveProfiles::kDefaultMeterIndex].totalSlots;
    std::array<GrooveProfiles::AccentProfile, 8> currentMeterProfiles_;
    std::array<GroovePattern, 8> groovePatterns_;
    std::array<SampleVoicePool, 8> samplePools_;
    std::array<SampleBuffer, 8> sampleBuffers_;
    std::array<SampleBuffer, 8> defaultKit_;
    std::array<juce::File, 8> sampleFiles_;
    std::array<bool, 8> sampleMissing_{};
    // Guards sampleBuffers_ against a torn read/write between the audio
    // thread (reads via SamplePlayer's non-owning pointer, potentially
    // across many blocks while a sample rings out) and the message
    // thread (replaces a whole buffer wholesale on Load Sample) — the
    // one place in the engine a lock is warranted instead of an atomic,
    // since what's shared is a variable-size buffer, not a scalar.
    std::mutex sampleBuffersMutex_;
    juce::AudioFormatManager audioFormatManager_;

    PatternClock patternClock_;
    juce::Reverb reverb_;
    DelayLine delayLine_;
    FastRandom randomizeRandom_{0xc0ffeeu};

    std::atomic<bool> isPlaying_{false};
    std::atomic<float> tempo_{120.0f};
    std::atomic<float> evolutionAmount_{0.0f};
    std::atomic<float> evolutionSpeed_{0.5f};
    std::atomic<float> spaceDisplay_{1.0f};
    // floor=0.3/biasExponent=0.4: autonomous drift can thin the kit down
    // to 30% busy-ness but never past it, and mostly sits well above the
    // floor (biasExponent<1 skews the draw toward 1.0) — so Space reads
    // as "breathing into something sparse," not "silence for a while."
    // Wild's own SpaceEvolver instance below is untouched (its defaults
    // reproduce the original unshaped uniform-draw behavior exactly),
    // since Wild=0 (ACDC-rigid) is an intentional, valid destination for
    // it to land on, unlike Space's near-silence case.
    SpaceEvolver spaceEvolver_{0x1e7ad03u, 1.0f, 0.3f, 0.4f};
    std::atomic<float> reverbRoom_{0.0f};
    std::atomic<float> reverbDecay_{0.0f};
    std::atomic<float> delayFeedback_{0.0f};
    std::atomic<float> delayBeatFraction_{0.5f};
    std::atomic<float> masterVolume_{1.0f};
    std::atomic<int> focusedVoiceIndex_{0};
    std::atomic<float> wildDisplay_{0.0f};
    SpaceEvolver wildEvolver_{0x7c2f91du};
    // Shared 0..(currentMeterSlotCount_-1) bar-position counter for
    // GroovePattern — advanced once per PatternClock grid tick,
    // independent of PatternClock itself (which stays untouched, only
    // used for its sample-accurate 16th-note timing). Starts at -1 so the
    // first tick's increment lands on slot 0, not 1.
    int currentSlot16_ = -1;
    double sampleRate_ = 44100.0;

    // UI-thread mirror of currentSlot16_/the active meter, for the
    // beat-pulse indicator and Scene capture — same write-throttle-free
    // mirror-atomic convention as spaceDisplay_/wildDisplay_ (this is just
    // an int, negligible cost even written every sample's grid tick).
    std::atomic<int> currentSlot16Display_{-1};
    std::atomic<int> meterNumeratorDisplay_{
        GrooveProfiles::kMeters[GrooveProfiles::kDefaultMeterIndex].numerator};
    std::atomic<int> meterDenominatorDisplay_{
        GrooveProfiles::kMeters[GrooveProfiles::kDefaultMeterIndex].denominator};
    // Meter changes touch non-atomic per-voice arrays (currentMeterProfiles_,
    // each GroovePattern's mask), so a change requested from the UI/MIDI
    // thread is queued here and consumed once per block on the audio
    // thread (applyMeterChange), rather than racing a torn write.
    std::atomic<int> pendingMeterIndex_{-1};
    // Same queued-request reasoning as pendingMeterIndex_ above, for
    // requestPhaseReset().
    std::atomic<bool> pendingPhaseReset_{false};

    // Host transport sync (see setHostSyncEnabled()/processHostSync()).
    // hostSyncEnabled_ is cross-thread (set from the UI thread); the rest
    // are plain, audio-thread-only — only ever read/written from
    // processBlock.
    std::atomic<bool> hostSyncEnabled_{false};
    bool lastHostPlaying_ = false;
    bool hasLastHostPpq_ = false;
    double lastHostPpq_ = 0.0;

    juce::TimeSliceThread recordingThread_{"Marmite Recording Thread"};
    AudioRecorder recorder_{recordingThread_};
    juce::File currentRecordingFile_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarmiteAudioProcessor)
};

#include "MarmiteEditor.h"

inline juce::AudioProcessorEditor* MarmiteAudioProcessor::createEditor() {
    return new MarmiteAudioProcessorEditor(*this);
}

// Standard JUCE plugin factory hook — called by every wrapper format
// (Standalone/AU/VST3) to create the one processor instance they host.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MarmiteAudioProcessor(); }
