#include <JuceHeader.h>

#include <BinaryData.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>

#include "AudioRecorder.h"
#include "DelayLine.h"
#include "DrumEvolutionEngine.h"
#include "DrumVoiceModel.h"
#include "FastRandom.h"
#include "MarmiteLookAndFeel.h"
#include "MarmiteTheme.h"
#include "MidiBindingManager.h"
#include "MidiPresetStore.h"
#include "PatternClock.h"
#include "PatternCloud.h"
#include "PresetNameValidation.h"
#include "ProceduralKit.h"
#include "SampleVoicePool.h"
#include "SceneState.h"
#include "ScenePresetStore.h"
#include "SpaceEvolver.h"

// Content shown in the help popup (launched from the "?" button next to
// Output). Read-only, scrollable if the window is short, styled to match
// the rest of the app rather than the OS-native AlertWindow look. Ported
// from Jerrican's HelpContent — same shape, rewritten for Marmite's own
// controls.
class HelpContent : public juce::Component {
public:
    HelpContent() {
        editor_.setMultiLine(true);
        editor_.setReadOnly(true);
        editor_.setScrollbarsShown(true);
        editor_.setCaretVisible(false);
        editor_.setPopupMenuEnabled(false);
        editor_.setColour(juce::TextEditor::backgroundColourId, MarmiteTheme::panel);
        editor_.setColour(juce::TextEditor::textColourId, MarmiteTheme::textPrimary);
        editor_.setColour(juce::TextEditor::outlineColourId, MarmiteTheme::panelBorder);
        editor_.setColour(juce::TextEditor::focusedOutlineColourId, MarmiteTheme::panelBorder);
        editor_.setFont(juce::Font(juce::FontOptions(12.5f)));

        const juce::String bodyText =
            "Marmite\n"
            "A generative drum machine by Alban Bailly.\n\n"
            "Each of the 8 voices is a PatternCloud: instead of a fixed step "
            "grid, it decides probabilistically whether/when/how to fire "
            "against a shared tempo clock, drawing fresh timing/velocity/"
            "pitch jitter from the ranges you set - so the pattern never "
            "loops identically twice.\n\n"
            "TRANSPORT\n"
            "Play - starts new hits triggering. Stop - halts new triggers "
            "(already-sounding hits ring out on their own). Reset - snaps "
            "every voice back to its starting values. Randomize - rerolls "
            "every voice's levers (and each voice's Density range - see "
            "below), whether playing or stopped.\n"
            "Evolution Amount - how often a voice's levers wander on their "
            "own while playing; 0, or Stop, leaves them alone.\n"
            "Evolution Speed - how fast a change glides in once Amount "
            "picks one - near-instant to almost imperceptibly slow.\n\n"
            "PER-VOICE CONTROLS\n"
            "Enabled - mutes/unmutes the voice. Volume - overall level.\n"
            "Tone - sample pitch-shift.\n"
            "Motion - per-hit pitch/velocity jitter, so a voice \"breathes\" "
            "instead of repeating identically.\n"
            "Density - how likely this voice is to fire on any given grid "
            "subdivision. Autonomous drift stays within a range unique to "
            "each voice, so some voices stay consistently sparse and "
            "others consistently busy rather than every voice averaging "
            "toward the same medium busy-ness over time.\n"
            "Chaos - the rhythmic sibling of dissonance: 0 locks hits "
            "exactly to the beat grid (a basic rock 4/4 feel), 1 pushes "
            "them off-grid with wide, unpredictable timing (IDM/glitch "
            "territory) - the one macro that lets this instrument span "
            "that whole range.\n"
            "Load Sample... - replaces this voice's procedurally-"
            "synthesized default hit with your own WAV/AIFF/FLAC/MP3/OGG.\n\n"
            "Each control has its own small Evolution switch (on by "
            "default, phosphor green) - turn one off to keep it under "
            "manual control while the rest keep drifting.\n\n"
            "GLOBAL\n"
            "Tempo - the shared clock every voice's grid is measured "
            "against.\n"
            "Space - how \"busy\" the whole kit is right now. Autonomously "
            "drifts alongside Evolution Amount/Speed, scaling every "
            "voice's effective Density together, so the ensemble "
            "periodically breathes into genuinely sparse, quiet passages "
            "and back - not just one voice going quiet while the rest "
            "carry on.\n"
            "Reverb - Room/Decay, a global send for the whole mix. Both "
            "are 0 by default (no reverb, output unchanged) and never "
            "evolve on their own.\n"
            "Delay - Time (a tempo-synced note division) and Feedback, a "
            "fixed-level send.\n"
            "Volume - master output level, applied after everything else. "
            "Full by default (unchanged output).\n\n"
            "OUTPUT / MIDI\n"
            "Output chooses which audio device the instrument plays "
            "through. MIDI In connects a controller for controlling "
            "Marmite (MIDI Learn) - it does not trigger drum hits "
            "directly, the pattern engine plays itself. Bindings opens "
            "MIDI Learn, where each per-voice control applies to "
            "whichever voice is currently focused (switch focus with "
            "Voice Select pads); Transport is bindable too, as a global "
            "action. MIDI Out mirrors every pattern trigger as a note on "
            "a GM percussion key (channel 10), so an external drum VST/"
            "hardware module can be driven by the same generative "
            "pattern instead of - or alongside - Marmite's own kit. "
            "Scenes saves/loads a full snapshot of every knob and toggle "
            "(transport state isn't included).\n\n"
            "RECORDING\n"
            "Record captures the exact final mix (everything, post-"
            "Reverb) to a timestamped WAV under ~/Music/Marmite "
            "Recordings - click again to stop and finalize the file. "
            "Independent of the transport: you can record silence as "
            "easily as a running pattern. \"Open Folder\" reveals the "
            "most recent recording in Finder.\n\n" +
            juce::String(juce::CharPointer_UTF8("\xc2\xa9")) +
            " 2026 Alban Bailly. All rights reserved.";

        editor_.setText(bodyText, false);
        addAndMakeVisible(editor_);
    }

    void resized() override { editor_.setBounds(getLocalBounds()); }

private:
    juce::TextEditor editor_;
};

class MarmiteEditor : public juce::AudioAppComponent,
                      private juce::Button::Listener,
                      private juce::Slider::Listener,
                      private juce::MidiInputCallback,
                      private juce::Timer {
public:
    // One voice's card in the 4x2 grid: name + enable LED, the five
    // macro knobs, a per-macro Evolution toggle row, and a Load Sample
    // button — the VoiceRow analog. References straight into the
    // owning MarmiteEditor's voice/engine arrays rather than owning any
    // model state itself, same relationship VoiceRow has with
    // JerricanEditor.
    class DrumVoiceRow : public juce::Component,
                         private juce::Button::Listener,
                         private juce::Slider::Listener {
    public:
        DrumVoiceRow(DrumVoiceModel& voice, DrumEvolutionEngine& evolutionEngine,
                    std::function<void()> onLoadSampleClicked)
            : voiceRef_(voice), evolutionEngineRef_(evolutionEngine),
              onLoadSampleClicked_(std::move(onLoadSampleClicked)) {
            addAndMakeVisible(nameLabel_);
            nameLabel_.setText(voiceRef_.getName(), juce::dontSendNotification);
            nameLabel_.setFont(juce::Font(juce::FontOptions(15.0f)).withStyle(juce::Font::bold));
            nameLabel_.setColour(juce::Label::textColourId, MarmiteTheme::textPrimary);

            addAndMakeVisible(enabledButton_);
            enabledButton_.setButtonText("");
            enabledButton_.setToggleState(voiceRef_.isEnabled(), juce::dontSendNotification);
            enabledButton_.addListener(this);

            setUpKnob(volumeSlider_, volumeLabel_, "Volume");
            volumeSlider_.setValue(voiceRef_.getVolume());
            setUpKnob(toneSlider_, toneLabel_, "Tone");
            toneSlider_.setValue(voiceRef_.getTone());
            setUpKnob(motionSlider_, motionLabel_, "Motion");
            motionSlider_.setValue(voiceRef_.getMotion());
            setUpKnob(densitySlider_, densityLabel_, "Density");
            densitySlider_.setValue(voiceRef_.getDensity());
            setUpKnob(chaosSlider_, chaosLabel_, "Chaos");
            chaosSlider_.setValue(voiceRef_.getChaos());

            addAndMakeVisible(evolutionSectionLabel_);
            evolutionSectionLabel_.setText("Evolution", juce::dontSendNotification);
            evolutionSectionLabel_.setFont(juce::Font(juce::FontOptions(10.0f)).withStyle(juce::Font::bold));
            evolutionSectionLabel_.setColour(juce::Label::textColourId, MarmiteTheme::accent);
            evolutionSectionLabel_.setJustificationType(juce::Justification::centredLeft);

            for (std::size_t i = 0; i < kEvolutionToggleCount; ++i) {
                setUpEvolutionCaption(*evolutionCaptionLabels()[i], kEvolutionCaptions[i]);
                setUpEvolutionToggle(*evolutionToggles()[i]);
            }

            addAndMakeVisible(loadSampleButton_);
            loadSampleButton_.setButtonText("Load Sample...");
            loadSampleButton_.onClick = onLoadSampleClicked_;
        }

        void paint(juce::Graphics& g) override {
            const auto bounds = getLocalBounds().toFloat();
            g.setColour(MarmiteTheme::panel);
            g.fillRoundedRectangle(bounds, 8.0f);
            g.setColour(focused_ ? MarmiteTheme::accent : MarmiteTheme::panelBorder);
            g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, focused_ ? 2.0f : 1.0f);
            if (dividerY_ > 0) {
                g.setColour(MarmiteTheme::panelBorder);
                g.drawHorizontalLine(dividerY_, 12.0f, static_cast<float>(getWidth() - 12));
            }
        }

        // Highlights this card when it's the "focused" voice — the one
        // MIDI-bound per-voice knobs currently drive (see
        // MarmiteEditor::focusedVoiceIndex_). No-op if unchanged, so the
        // timer polling this stays cheap.
        void setFocused(bool focused) {
            if (focused_ != focused) {
                focused_ = focused;
                repaint();
            }
        }

        void resized() override {
            constexpr int padding = 12;
            const int contentWidth = getWidth() - padding * 2;

            nameLabel_.setBounds(padding, padding, contentWidth - 26, 20);
            enabledButton_.setBounds(getWidth() - padding - 18, padding + 1, 18, 18);

            constexpr int knobCount = 5;
            const int knobColumnWidth = contentWidth / knobCount;
            const int knobSize = std::min(52, knobColumnWidth - 10);
            constexpr int knobLabelHeight = 12;
            constexpr int knobTextBoxHeight = 16;
            const int knobRowY = padding + 20 + 10;

            juce::Slider* knobs[knobCount] = {&volumeSlider_, &toneSlider_, &motionSlider_,
                                              &densitySlider_, &chaosSlider_};
            juce::Label* knobLabels[knobCount] = {&volumeLabel_, &toneLabel_, &motionLabel_,
                                                  &densityLabel_, &chaosLabel_};

            for (int i = 0; i < knobCount; ++i) {
                const int columnX = padding + i * knobColumnWidth;
                const int knobX = columnX + (knobColumnWidth - knobSize) / 2;
                knobLabels[i]->setBounds(columnX, knobRowY, knobColumnWidth, knobLabelHeight);
                knobs[i]->setBounds(knobX, knobRowY + knobLabelHeight + 2, knobSize,
                                    knobSize + knobTextBoxHeight);
            }

            const int knobRowBottom = knobRowY + knobLabelHeight + 2 + knobSize + knobTextBoxHeight;
            dividerY_ = knobRowBottom + 10;

            const int evolutionLabelY = dividerY_ + 8;
            evolutionSectionLabel_.setBounds(padding, evolutionLabelY, 80, 12);

            const int toggleRowY = evolutionLabelY + 16;
            const int toggleColumnWidth = contentWidth / static_cast<int>(kEvolutionToggleCount);
            constexpr int toggleCaptionHeight = 10;
            constexpr int toggleSize = 16;

            for (std::size_t i = 0; i < kEvolutionToggleCount; ++i) {
                const int columnX = padding + static_cast<int>(i) * toggleColumnWidth;
                evolutionCaptionLabels()[i]->setBounds(columnX, toggleRowY, toggleColumnWidth,
                                                       toggleCaptionHeight);
                const int toggleX = columnX + (toggleColumnWidth - toggleSize) / 2;
                evolutionToggles()[i]->setBounds(toggleX, toggleRowY + toggleCaptionHeight + 2,
                                                 toggleSize, toggleSize);
            }

            const int loadButtonY = toggleRowY + toggleCaptionHeight + 2 + toggleSize + 12;
            loadSampleButton_.setBounds(padding, loadButtonY, contentWidth, 22);
        }

        void buttonClicked(juce::Button* button) override {
            if (button == &enabledButton_) {
                voiceRef_.setEnabled(enabledButton_.getToggleState());
            } else if (button == &volumeEvoToggle_) {
                const bool on = volumeEvoToggle_.getToggleState();
                evolutionEngineRef_.setVolumeEnabled(on);
                if (on) evolutionEngineRef_.resyncVolume(voiceRef_.getVolume());
            } else if (button == &toneEvoToggle_) {
                const bool on = toneEvoToggle_.getToggleState();
                evolutionEngineRef_.setToneEnabled(on);
                if (on) evolutionEngineRef_.resyncTone(voiceRef_.getTone());
            } else if (button == &motionEvoToggle_) {
                const bool on = motionEvoToggle_.getToggleState();
                evolutionEngineRef_.setMotionEnabled(on);
                if (on) evolutionEngineRef_.resyncMotion(voiceRef_.getMotion());
            } else if (button == &densityEvoToggle_) {
                const bool on = densityEvoToggle_.getToggleState();
                evolutionEngineRef_.setDensityEnabled(on);
                if (on) evolutionEngineRef_.resyncDensity(voiceRef_.getDensity());
            } else if (button == &chaosEvoToggle_) {
                const bool on = chaosEvoToggle_.getToggleState();
                evolutionEngineRef_.setChaosEnabled(on);
                if (on) evolutionEngineRef_.resyncChaos(voiceRef_.getChaos());
            }
        }

        void sliderValueChanged(juce::Slider* slider) override {
            if (slider == &volumeSlider_) {
                const float value = static_cast<float>(volumeSlider_.getValue());
                voiceRef_.setVolume(value);
                evolutionEngineRef_.resyncVolume(value);
            } else if (slider == &toneSlider_) {
                const float value = static_cast<float>(toneSlider_.getValue());
                voiceRef_.setTone(value);
                evolutionEngineRef_.resyncTone(value);
            } else if (slider == &motionSlider_) {
                const float value = static_cast<float>(motionSlider_.getValue());
                voiceRef_.setMotion(value);
                evolutionEngineRef_.resyncMotion(value);
            } else if (slider == &densitySlider_) {
                const float value = static_cast<float>(densitySlider_.getValue());
                voiceRef_.setDensity(value);
                evolutionEngineRef_.resyncDensity(value);
            } else if (slider == &chaosSlider_) {
                const float value = static_cast<float>(chaosSlider_.getValue());
                voiceRef_.setChaos(value);
                evolutionEngineRef_.resyncChaos(value);
            }
        }

        // Called after Reset/Randomize change the model out from under
        // this row, so the knobs catch up. Skips any control the user is
        // actively dragging, same guard Jerrican's VoiceRow uses.
        void refreshFromModel() {
            if (!enabledButton_.isMouseButtonDown())
                enabledButton_.setToggleState(voiceRef_.isEnabled(), juce::dontSendNotification);
            if (!volumeSlider_.isMouseButtonDown())
                volumeSlider_.setValue(voiceRef_.getVolume(), juce::dontSendNotification);
            if (!toneSlider_.isMouseButtonDown())
                toneSlider_.setValue(voiceRef_.getTone(), juce::dontSendNotification);
            if (!motionSlider_.isMouseButtonDown())
                motionSlider_.setValue(voiceRef_.getMotion(), juce::dontSendNotification);
            if (!densitySlider_.isMouseButtonDown())
                densitySlider_.setValue(voiceRef_.getDensity(), juce::dontSendNotification);
            if (!chaosSlider_.isMouseButtonDown())
                chaosSlider_.setValue(voiceRef_.getChaos(), juce::dontSendNotification);
        }

        void resetEvolutionToggles() {
            for (auto* toggle : evolutionToggles()) {
                toggle->setToggleState(true, juce::dontSendNotification);
            }
            evolutionEngineRef_.setVolumeEnabled(true);
            evolutionEngineRef_.setToneEnabled(true);
            evolutionEngineRef_.setMotionEnabled(true);
            evolutionEngineRef_.setDensityEnabled(true);
            evolutionEngineRef_.setChaosEnabled(true);
        }

        // Reflects DrumEvolutionEngine's current opt-in/out flags into the
        // five toggle LEDs, without touching the values they control — the
        // counterpart to refreshFromModel() above, needed now that MIDI
        // Learn can flip these flags from off-screen (a manual click
        // already updates its own toggle directly, so this is purely for
        // externally-driven changes).
        void refreshEvolutionToggles() {
            volumeEvoToggle_.setToggleState(evolutionEngineRef_.isVolumeEnabled(),
                                            juce::dontSendNotification);
            toneEvoToggle_.setToggleState(evolutionEngineRef_.isToneEnabled(),
                                          juce::dontSendNotification);
            motionEvoToggle_.setToggleState(evolutionEngineRef_.isMotionEnabled(),
                                            juce::dontSendNotification);
            densityEvoToggle_.setToggleState(evolutionEngineRef_.isDensityEnabled(),
                                             juce::dontSendNotification);
            chaosEvoToggle_.setToggleState(evolutionEngineRef_.isChaosEnabled(),
                                           juce::dontSendNotification);
        }

    private:
        static constexpr std::size_t kEvolutionToggleCount = 5;
        static constexpr const char* kEvolutionCaptions[kEvolutionToggleCount] = {
            "Volume", "Tone", "Motion", "Density", "Chaos"};

        void setUpLabel(juce::Label& label, const char* labelText) {
            addAndMakeVisible(label);
            label.setText(labelText, juce::dontSendNotification);
            label.setFont(juce::Font(juce::FontOptions(10.0f)));
            label.setColour(juce::Label::textColourId, MarmiteTheme::textSecondary);
            label.setJustificationType(juce::Justification::centred);
        }

        void setUpKnob(juce::Slider& slider, juce::Label& label, const char* labelText) {
            setUpLabel(label, labelText);
            addAndMakeVisible(slider);
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setRange(0.0, 1.0);
            slider.setNumDecimalPlacesToDisplay(2);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 46, 16);
            slider.addListener(this);
        }

        void setUpEvolutionCaption(juce::Label& label, const char* labelText) {
            addAndMakeVisible(label);
            label.setText(labelText, juce::dontSendNotification);
            label.setFont(juce::Font(juce::FontOptions(8.0f)));
            label.setColour(juce::Label::textColourId, MarmiteTheme::textSecondary);
            label.setJustificationType(juce::Justification::centred);
        }

        void setUpEvolutionToggle(juce::ToggleButton& toggle) {
            addAndMakeVisible(toggle);
            toggle.setButtonText("");
            toggle.setToggleState(true, juce::dontSendNotification);
            toggle.addListener(this);
        }

        std::array<juce::Label*, kEvolutionToggleCount> evolutionCaptionLabels() {
            return {&volumeEvoLabel_, &toneEvoLabel_, &motionEvoLabel_, &densityEvoLabel_,
                   &chaosEvoLabel_};
        }
        std::array<juce::ToggleButton*, kEvolutionToggleCount> evolutionToggles() {
            return {&volumeEvoToggle_, &toneEvoToggle_, &motionEvoToggle_, &densityEvoToggle_,
                   &chaosEvoToggle_};
        }

        DrumVoiceModel& voiceRef_;
        DrumEvolutionEngine& evolutionEngineRef_;
        std::function<void()> onLoadSampleClicked_;
        int dividerY_ = 0;
        bool focused_ = false;

        juce::Label nameLabel_;
        juce::ToggleButton enabledButton_;
        juce::Label volumeLabel_, toneLabel_, motionLabel_, densityLabel_, chaosLabel_;
        juce::Slider volumeSlider_, toneSlider_, motionSlider_, densitySlider_, chaosSlider_;
        juce::Label evolutionSectionLabel_;
        juce::Label volumeEvoLabel_, toneEvoLabel_, motionEvoLabel_, densityEvoLabel_, chaosEvoLabel_;
        juce::ToggleButton volumeEvoToggle_, toneEvoToggle_, motionEvoToggle_, densityEvoToggle_,
            chaosEvoToggle_;
        juce::TextButton loadSampleButton_;
    };

    // Shared preset row (Combo + Override/Revert/Save As/Delete), used by
    // both MidiBindingsPopup and ScenesPopup — ported near-verbatim from
    // Jerrican's PresetControls, which is fully data-agnostic (driven
    // entirely by the injected Callbacks).
    class PresetControls : public juce::Component, private juce::Timer {
    public:
        struct Callbacks {
            std::function<std::vector<std::string>()> listNames;
            std::function<bool(const std::string&)> loadNamed;
            std::function<bool(const std::string&)> saveNamed;
            std::function<bool(const std::string&)> removeNamed;
            std::function<bool(const std::string&)> matchesNamed;
            std::function<bool()> hasMeaningfulContent;
            // Called after a confirmed delete, so the live state doesn't
            // keep pointing at a preset that no longer exists on disk —
            // e.g. clearing bindings, or resetting voices to defaults.
            std::function<void()> onDeleted;
            // PresetControls itself is rebuilt from scratch every time its
            // popup reopens (a fresh CallOutBox each click), so "which
            // preset am I on" can't live only in this Component's combo
            // box — it has to be persisted by the owner (MarmiteEditor)
            // across opens/closes, otherwise editing a loaded preset and
            // reopening the popup loses track of it, leaving only the
            // "Save As" path (no way to Override) even though you're
            // still clearly working from that preset.
            std::function<juce::String()> getCurrentName;
            std::function<void(const juce::String&)> setCurrentName;
        };

        static constexpr int kPreferredHeight = 22 + 6 + 22;

        // enableRevert: whether to offer a "Revert" button (discards
        // unsaved edits, reloads the selected preset's saved values) —
        // Scenes-only, same as Jerrican; Bindings' UI stays as-is.
        PresetControls(const juce::String& labelText, Callbacks callbacks,
                       bool enableRevert = false)
            : callbacks_(std::move(callbacks)), enableRevert_(enableRevert) {
            addAndMakeVisible(label_);
            label_.setText(labelText, juce::dontSendNotification);
            label_.setFont(juce::Font(juce::FontOptions(12.0f)));
            label_.setColour(juce::Label::textColourId, MarmiteTheme::textSecondary);

            addAndMakeVisible(combo_);
            combo_.onChange = [this] {
                const auto name = combo_.getText();
                if (name.isNotEmpty()) {
                    callbacks_.loadNamed(name.toStdString());
                }
                callbacks_.setCurrentName(name);
                refreshState();
            };

            addAndMakeVisible(overrideButton_);
            overrideButton_.setVisible(false);
            overrideButton_.onClick = [this] {
                const auto name = combo_.getText();
                if (name.isNotEmpty()) {
                    callbacks_.saveNamed(name.toStdString());
                    refreshState();
                }
            };

            addAndMakeVisible(revertButton_);
            revertButton_.setButtonText("Revert");
            revertButton_.setVisible(false);
            revertButton_.onClick = [this] {
                const auto name = combo_.getText();
                if (name.isNotEmpty()) {
                    callbacks_.loadNamed(name.toStdString());
                    refreshState();
                }
            };

            addAndMakeVisible(saveAsButton_);
            saveAsButton_.setButtonText("Save As...");
            saveAsButton_.onClick = [this] { showSaveAsPrompt(); };

            addAndMakeVisible(deleteButton_);
            deleteButton_.setButtonText("Delete");
            deleteButton_.onClick = [this] { showDeleteConfirmPrompt(); };

            // Re-hydrate from whatever preset the owner remembers being
            // "on", so reopening this popup after editing a loaded preset
            // still offers Override rather than only Save As.
            combo_.setText(callbacks_.getCurrentName(), juce::dontSendNotification);
            refreshNames();
            refreshState();
            startTimerHz(10);
        }

        void resized() override {
            const int width = getWidth();
            label_.setBounds(0, 0, 50, 22);
            combo_.setBounds(54, 0, width - 54, 22);

            constexpr int buttonHeight = 22;
            constexpr int buttonGap = 6;
            constexpr int saveAsWidth = 90;
            constexpr int deleteWidth = 70;
            const int row2Y = 22 + 6;

            int rightX = width;
            deleteButton_.setBounds(rightX - deleteWidth, row2Y, deleteWidth, buttonHeight);
            rightX -= deleteWidth + buttonGap;
            saveAsButton_.setBounds(rightX - saveAsWidth, row2Y, saveAsWidth, buttonHeight);
            rightX -= saveAsWidth + buttonGap;

            if (revertButton_.isVisible()) {
                constexpr int revertWidth = 64;
                revertButton_.setBounds(rightX - revertWidth, row2Y, revertWidth, buttonHeight);
                rightX -= revertWidth + buttonGap;
            }

            if (overrideButton_.isVisible()) {
                overrideButton_.setBounds(0, row2Y, std::max(0, rightX - buttonGap), buttonHeight);
            }
        }

    private:
        void refreshNames() {
            const auto currentText = combo_.getText();
            combo_.clear(juce::dontSendNotification);
            const auto names = callbacks_.listNames();
            for (int i = 0; i < static_cast<int>(names.size()); ++i) {
                combo_.addItem(names[static_cast<std::size_t>(i)], i + 1);
            }
            combo_.setText(currentText, juce::dontSendNotification);
        }

        void refreshState() {
            auto currentName = combo_.getText();

            if (currentName.isEmpty()) {
                for (const auto& name : callbacks_.listNames()) {
                    if (callbacks_.matchesNamed(name)) {
                        combo_.setText(name, juce::dontSendNotification);
                        currentName = name;
                        callbacks_.setCurrentName(name);
                        break;
                    }
                }
            }

            const bool wasOverrideVisible = overrideButton_.isVisible();
            const bool wasRevertVisible = revertButton_.isVisible();
            if (currentName.isEmpty()) {
                overrideButton_.setVisible(false);
                revertButton_.setVisible(false);
                saveAsButton_.setEnabled(callbacks_.hasMeaningfulContent());
                deleteButton_.setEnabled(false);
            } else {
                const bool matches = callbacks_.matchesNamed(currentName.toStdString());
                overrideButton_.setVisible(!matches);
                if (!matches) {
                    overrideButton_.setButtonText("Override \"" + currentName + "\"");
                }
                // Only worth offering once there's actually something to
                // discard — if live state already matches the saved
                // preset, Revert would be a no-op.
                revertButton_.setVisible(enableRevert_ && !matches);
                saveAsButton_.setEnabled(true);
                deleteButton_.setEnabled(true);
            }

            if (overrideButton_.isVisible() != wasOverrideVisible ||
                revertButton_.isVisible() != wasRevertVisible) {
                resized();
            }
        }

        void showSaveAsPrompt() {
            auto* window = new juce::AlertWindow("Save Preset", "Name this preset:",
                                                 juce::MessageBoxIconType::NoIcon);
            window->addTextEditor("name", combo_.getText(), "");
            window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
            window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

            // Same use-after-free guard as everywhere else this popup
            // pattern is used — the modal can outlive this component if
            // its hosting CallOutBox gets dismissed first.
            juce::Component::SafePointer<PresetControls> safeThis(this);
            window->enterModalState(
                true,
                juce::ModalCallbackFunction::create([safeThis, window](int result) {
                    if (result != 1 || safeThis == nullptr) {
                        return;
                    }
                    const auto name = window->getTextEditorContents("name");
                    if (name.isEmpty()) {
                        return;
                    }

                    // Save As is framed as "create a new preset" —
                    // silently overwriting an existing one because you
                    // happened to type (or mistype into) a name that
                    // already exists would be a destructive surprise, so
                    // confirm first.
                    const auto existingNames = safeThis->callbacks_.listNames();
                    const bool alreadyExists =
                        std::find(existingNames.begin(), existingNames.end(),
                                 name.toStdString()) != existingNames.end();
                    if (alreadyExists) {
                        safeThis->showOverwriteConfirmPrompt(name);
                    } else {
                        safeThis->commitSaveAs(name);
                    }
                }),
                true);
        }

        void showOverwriteConfirmPrompt(const juce::String& name) {
            auto* window =
                new juce::AlertWindow("Overwrite Preset",
                                      "A preset named \"" + name +
                                          "\" already exists. Overwrite it?",
                                      juce::MessageBoxIconType::WarningIcon);
            window->addButton("Overwrite", 1, juce::KeyPress(juce::KeyPress::returnKey));
            window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

            juce::Component::SafePointer<PresetControls> safeThis(this);
            window->enterModalState(
                true,
                juce::ModalCallbackFunction::create([safeThis, name](int result) {
                    if (result == 1 && safeThis != nullptr) {
                        safeThis->commitSaveAs(name);
                    }
                }),
                true);
        }

        void commitSaveAs(const juce::String& name) {
            callbacks_.saveNamed(name.toStdString());
            callbacks_.setCurrentName(name);
            refreshNames();
            combo_.setText(name, juce::dontSendNotification);
            refreshState();
        }

        void showDeleteConfirmPrompt() {
            const auto name = combo_.getText();
            if (name.isEmpty()) {
                return;
            }

            auto* window =
                new juce::AlertWindow("Delete Preset",
                                      "Are you sure you want to delete \"" + name + "\"?",
                                      juce::MessageBoxIconType::WarningIcon);
            window->addButton("Delete", 1, juce::KeyPress(juce::KeyPress::returnKey));
            window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

            juce::Component::SafePointer<PresetControls> safeThis(this);
            window->enterModalState(
                true,
                juce::ModalCallbackFunction::create([safeThis, name](int result) {
                    if (result == 1 && safeThis != nullptr) {
                        safeThis->callbacks_.removeNamed(name.toStdString());
                        safeThis->combo_.setText("", juce::dontSendNotification);
                        safeThis->callbacks_.setCurrentName("");
                        safeThis->refreshNames();
                        if (safeThis->callbacks_.onDeleted) {
                            safeThis->callbacks_.onDeleted();
                        }
                        safeThis->refreshState();
                    }
                }),
                true);
        }

        void timerCallback() override { refreshState(); }

        Callbacks callbacks_;
        bool enableRevert_;
        juce::Label label_;
        juce::ComboBox combo_;
        juce::TextButton overrideButton_;
        juce::TextButton revertButton_;
        juce::TextButton saveAsButton_;
        juce::TextButton deleteButton_;
    };

    // Scenes popup, opened from the "Scenes" button next to Bindings — a
    // full state snapshot (every voice's knobs/enabled/Evolution-toggle
    // state, plus the global Tempo/Evolution/Reverb/Delay/Volume
    // controls), distinct from MIDI Learn's controller-mapping presets.
    class ScenesPopup : public juce::Component {
    public:
        explicit ScenesPopup(MarmiteEditor* owner)
            : presetControls_(
                  "Scene",
                  PresetControls::Callbacks{
                      .listNames = [owner] { return owner->scenePresetStore_.listPresetNames(); },
                      .loadNamed =
                          [owner](const std::string& name) {
                              SceneState scene;
                              if (!owner->scenePresetStore_.load(name, scene)) {
                                  return false;
                              }
                              owner->applySceneState(scene);
                              return true;
                          },
                      .saveNamed =
                          [owner](const std::string& name) {
                              return owner->scenePresetStore_.save(name, owner->captureSceneState());
                          },
                      .removeNamed = [owner](const std::string& name) {
                          return owner->scenePresetStore_.remove(name);
                      },
                      .matchesNamed =
                          [owner](const std::string& name) {
                              SceneState scene;
                              return owner->scenePresetStore_.load(name, scene) &&
                                     scene == owner->captureSceneState();
                          },
                      // A Scene is always a complete, meaningful snapshot
                      // — unlike an empty MIDI binding table, there's no
                      // "nothing to save" state, so Save As is never
                      // gated off here.
                      .hasMeaningfulContent = [] { return true; },
                      // Deleting the Scene you're on shouldn't leave live
                      // state pointing at a preset that no longer exists
                      // — fall back to the same defaults Reset restores.
                      .onDeleted = [owner] { owner->handleResetPressed(); },
                      .getCurrentName = [owner] { return owner->currentSceneName_; },
                      .setCurrentName = [owner](const juce::String& name) {
                          owner->currentSceneName_ = name;
                      },
                  },
                  /*enableRevert=*/true) {
            addAndMakeVisible(presetControls_);

            addAndMakeVisible(hintLabel_);
            hintLabel_.setText("Captures every knob/toggle. Transport state isn't included.",
                               juce::dontSendNotification);
            hintLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
            hintLabel_.setColour(juce::Label::textColourId, MarmiteTheme::textSecondary);
            hintLabel_.setJustificationType(juce::Justification::topLeft);
        }

        void resized() override {
            constexpr int padding = 12;
            const int contentWidth = getWidth() - padding * 2;

            presetControls_.setBounds(padding, padding, contentWidth, PresetControls::kPreferredHeight);
            hintLabel_.setBounds(padding, padding + PresetControls::kPreferredHeight + 10, contentWidth,
                                 40);
        }

    private:
        PresetControls presetControls_;
        juce::Label hintLabel_;
    };

    // MIDI Learn popup, opened from the "Bindings" button next to the MIDI
    // input dropdown. One row per MidiTarget (label, live binding readout,
    // Learn, Clear), grouped per scope: per-voice controls (apply to
    // whichever voice is currently focused), voice select (moves focus),
    // transport, and global controls. A preset row at the top saves/
    // loads/deletes named binding sets — explicit Save only, nothing is
    // written to disk until "Save As..." is clicked. Runs its own
    // lightweight Timer to poll owner_->midiBindings_ and keep every row's
    // readout current, including while a Learn is in flight.
    class MidiBindingsPopup : public juce::Component, private juce::Timer {
    public:
        explicit MidiBindingsPopup(MarmiteEditor* owner)
            : owner_(owner),
              presetControls_(
                  "Preset",
                  PresetControls::Callbacks{
                      .listNames = [owner] { return owner->midiPresetStore_.listPresetNames(); },
                      .loadNamed =
                          [owner](const std::string& name) {
                              return owner->midiPresetStore_.load(name, owner->midiBindings_);
                          },
                      .saveNamed =
                          [owner](const std::string& name) {
                              return owner->midiPresetStore_.save(name, owner->midiBindings_);
                          },
                      .removeNamed = [owner](const std::string& name) {
                          return owner->midiPresetStore_.remove(name);
                      },
                      .matchesNamed =
                          [owner](const std::string& name) {
                              MidiBindingManager temp;
                              return owner->midiPresetStore_.load(name, temp) &&
                                     temp.equals(owner->midiBindings_);
                          },
                      .hasMeaningfulContent =
                          [owner] {
                              for (const auto target : kAllMidiTargets) {
                                  if (owner->midiBindings_.getBinding(target).has_value()) {
                                      return true;
                                  }
                              }
                              return false;
                          },
                      // Deleting the preset you're on shouldn't leave the
                      // live bindings pointing at a saved file that no
                      // longer exists — clear them so it's an honest
                      // blank slate.
                      .onDeleted = [owner] { owner->midiBindings_.clearAll(); },
                      .getCurrentName = [owner] { return owner->currentMidiPresetName_; },
                      .setCurrentName = [owner](const juce::String& name) {
                          owner->currentMidiPresetName_ = name;
                      },
                  }) {
            addAndMakeVisible(presetControls_);

            addAndMakeVisible(midiWarningLabel_);
            midiWarningLabel_.setText(
                "No MIDI controller connected " +
                    juce::String(juce::CharPointer_UTF8("\xe2\x80\x94")) + " pick one from MIDI In.",
                juce::dontSendNotification);
            midiWarningLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
            midiWarningLabel_.setColour(juce::Label::textColourId, MarmiteTheme::accentDeep);
            midiWarningLabel_.setVisible(owner_->currentMidiInputId_.isEmpty());

            // 31 target rows don't reliably fit a fixed popup height on
            // smaller screens — everything below the preset row lives in
            // rowsContainer_, scrolled via viewport_, so the preset row
            // (always relevant) stays pinned at the top regardless.
            addAndMakeVisible(viewport_);
            viewport_.setViewedComponent(&rowsContainer_, false);
            viewport_.setScrollBarsShown(true, false);

            setUpSectionLabel(perVoiceSectionLabel_, "Per-Voice Controls (focused voice)");
            setUpSectionLabel(voiceSelectSectionLabel_, "Voice Select");
            setUpSectionLabel(transportSectionLabel_, "Transport");
            setUpSectionLabel(globalSectionLabel_, "Global");

            for (std::size_t i = 0; i < kTargetCount; ++i) {
                const auto target = kAllMidiTargets[i];
                rowsContainer_.addAndMakeVisible(targetLabels_[i]);
                targetLabels_[i].setText(friendlyTargetLabel(target), juce::dontSendNotification);
                targetLabels_[i].setFont(juce::Font(juce::FontOptions(12.0f)));
                targetLabels_[i].setColour(juce::Label::textColourId, MarmiteTheme::textPrimary);

                rowsContainer_.addAndMakeVisible(targetReadouts_[i]);
                targetReadouts_[i].setFont(juce::Font(juce::FontOptions(11.0f)));
                targetReadouts_[i].setColour(juce::Label::textColourId, MarmiteTheme::textSecondary);

                rowsContainer_.addAndMakeVisible(learnButtons_[i]);
                learnButtons_[i].setButtonText("Learn");
                learnButtons_[i].onClick = [this, target] { owner_->midiBindings_.armLearn(target); };

                rowsContainer_.addAndMakeVisible(clearButtons_[i]);
                clearButtons_[i].setButtonText("Clear");
                clearButtons_[i].onClick = [this, target] {
                    owner_->midiBindings_.clearBinding(target);
                };
            }

            refreshReadouts();
            startTimerHz(10);
        }

        void resized() override {
            constexpr int padding = 12;
            const int contentWidth = getWidth() - padding * 2;

            presetControls_.setBounds(padding, padding, contentWidth, PresetControls::kPreferredHeight);
            int viewportTop = padding + PresetControls::kPreferredHeight + 10;

            if (midiWarningLabel_.isVisible()) {
                midiWarningLabel_.setBounds(padding, viewportTop, contentWidth, 18);
                viewportTop += 18 + 8;
            }

            viewport_.setBounds(0, viewportTop, getWidth(), getHeight() - viewportTop);

            // rowsContainer_ is sized to its actual content height (which
            // can exceed the popup's own height) — that's what makes
            // viewport_ scroll instead of clipping.
            const int rowsContentWidth = getWidth() - viewport_.getScrollBarThickness();
            const int innerContentWidth = rowsContentWidth - padding * 2;
            int y = padding;
            y = layoutSection(perVoiceSectionLabel_, 0, 11, padding, innerContentWidth, y);
            y = layoutSection(voiceSelectSectionLabel_, 11, 19, padding, innerContentWidth, y);
            y = layoutSection(transportSectionLabel_, 19, 23, padding, innerContentWidth, y);
            y = layoutSection(globalSectionLabel_, 23, 32, padding, innerContentWidth, y);
            rowsContainer_.setSize(rowsContentWidth, y + padding);
        }

    private:
        static constexpr std::size_t kTargetCount = kAllMidiTargets.size();

        int layoutSection(juce::Label& sectionLabel, std::size_t begin, std::size_t end, int padding,
                          int contentWidth, int y) {
            sectionLabel.setBounds(padding, y, contentWidth, 16);
            y += 16 + 4;

            for (std::size_t i = begin; i < end; ++i) {
                targetLabels_[i].setBounds(padding, y, 150, 22);
                targetReadouts_[i].setBounds(padding + 150, y, 130, 22);
                learnButtons_[i].setBounds(padding + 150 + 130 + 4, y, 60, 22);
                clearButtons_[i].setBounds(padding + 150 + 130 + 4 + 64, y, 56, 22);
                y += 22;
            }
            return y + 12;
        }

        void setUpSectionLabel(juce::Label& label, const char* text) {
            rowsContainer_.addAndMakeVisible(label);
            label.setText(text, juce::dontSendNotification);
            label.setFont(juce::Font(juce::FontOptions(12.0f)).withStyle(juce::Font::bold));
            label.setColour(juce::Label::textColourId, MarmiteTheme::accent);
        }

        void refreshReadouts() {
            for (std::size_t i = 0; i < kTargetCount; ++i) {
                const auto target = kAllMidiTargets[i];
                if (owner_->midiBindings_.isLearning() && owner_->midiBindings_.learningTarget() == target) {
                    targetReadouts_[i].setText("Listening...", juce::dontSendNotification);
                    continue;
                }
                targetReadouts_[i].setText(describeBinding(owner_->midiBindings_.getBinding(target)),
                                           juce::dontSendNotification);
            }
        }

        void timerCallback() override {
            refreshReadouts();
            const bool shouldWarn = owner_->currentMidiInputId_.isEmpty();
            if (shouldWarn != midiWarningLabel_.isVisible()) {
                midiWarningLabel_.setVisible(shouldWarn);
                resized();
            }
        }

        static juce::String describeBinding(std::optional<MidiBinding> binding) {
            if (!binding.has_value()) {
                return "Not bound";
            }
            const juce::String kind = binding->type == MidiEvent::Type::ControlChange ? "CC" : "Note";
            return kind + " " + juce::String(binding->number) + " ch" + juce::String(binding->channel);
        }

        static const char* friendlyTargetLabel(MidiTarget target) {
            switch (target) {
                case MidiTarget::VoiceVolume: return "Volume";
                case MidiTarget::VoiceTone: return "Tone";
                case MidiTarget::VoiceMotion: return "Motion";
                case MidiTarget::VoiceDensity: return "Density";
                case MidiTarget::VoiceChaos: return "Chaos";
                case MidiTarget::VoiceEnabledToggle: return "Enabled toggle";
                case MidiTarget::VoiceVolumeEvoToggle: return "Evolve: Volume";
                case MidiTarget::VoiceToneEvoToggle: return "Evolve: Tone";
                case MidiTarget::VoiceMotionEvoToggle: return "Evolve: Motion";
                case MidiTarget::VoiceDensityEvoToggle: return "Evolve: Density";
                case MidiTarget::VoiceChaosEvoToggle: return "Evolve: Chaos";
                case MidiTarget::SelectVoice1: return "Voice 1";
                case MidiTarget::SelectVoice2: return "Voice 2";
                case MidiTarget::SelectVoice3: return "Voice 3";
                case MidiTarget::SelectVoice4: return "Voice 4";
                case MidiTarget::SelectVoice5: return "Voice 5";
                case MidiTarget::SelectVoice6: return "Voice 6";
                case MidiTarget::SelectVoice7: return "Voice 7";
                case MidiTarget::SelectVoice8: return "Voice 8";
                case MidiTarget::TransportPlay: return "Play";
                case MidiTarget::TransportStop: return "Stop";
                case MidiTarget::TransportReset: return "Reset";
                case MidiTarget::TransportRandomize: return "Randomize";
                case MidiTarget::Tempo: return "Tempo";
                case MidiTarget::EvolutionAmount: return "Evolution Amount";
                case MidiTarget::EvolutionSpeed: return "Evolution Speed";
                case MidiTarget::Space: return "Space";
                case MidiTarget::ReverbRoom: return "Reverb Room";
                case MidiTarget::ReverbDecay: return "Reverb Decay";
                case MidiTarget::DelayTime: return "Delay Time";
                case MidiTarget::DelayFeedback: return "Delay Feedback";
                case MidiTarget::MasterVolume: return "Master Volume";
            }
            return "";
        }

        MarmiteEditor* owner_;
        PresetControls presetControls_;
        juce::Label midiWarningLabel_;
        juce::Viewport viewport_;
        juce::Component rowsContainer_;
        juce::Label perVoiceSectionLabel_;
        juce::Label voiceSelectSectionLabel_;
        juce::Label transportSectionLabel_;
        juce::Label globalSectionLabel_;
        std::array<juce::Label, kTargetCount> targetLabels_;
        std::array<juce::Label, kTargetCount> targetReadouts_;
        std::array<juce::TextButton, kTargetCount> learnButtons_;
        std::array<juce::TextButton, kTargetCount> clearButtons_;
    };

    MarmiteEditor()
        : voices_{makeVoiceModel(kInitialVoices[0]), makeVoiceModel(kInitialVoices[1]),
                  makeVoiceModel(kInitialVoices[2]), makeVoiceModel(kInitialVoices[3]),
                  makeVoiceModel(kInitialVoices[4]), makeVoiceModel(kInitialVoices[5]),
                  makeVoiceModel(kInitialVoices[6]), makeVoiceModel(kInitialVoices[7])},
          evolutionEngines_{DrumEvolutionEngine(0x37a1f2c9u), DrumEvolutionEngine(0x6b4d8e12u),
                             DrumEvolutionEngine(0xa9c3f501u), DrumEvolutionEngine(0xe1d47b6au),
                             DrumEvolutionEngine(0x2c5f9a3du), DrumEvolutionEngine(0x8b1e64f7u),
                             DrumEvolutionEngine(0xf4d27a19u), DrumEvolutionEngine(0x593bce82u)},
          patternClouds_{PatternCloud(0x1a2b3c4du), PatternCloud(0x5e6f7081u),
                         PatternCloud(0x92a3b4c5u), PatternCloud(0xd6e7f809u),
                         PatternCloud(0x4b8e2c61u), PatternCloud(0x7f19d3a2u),
                         PatternCloud(0xc03e6b58u), PatternCloud(0x2d9a17f4u)} {
        for (std::size_t i = 0; i < voices_.size(); ++i) {
            const auto& initial = kInitialVoices[i];
            evolutionEngines_[i].resetTo(initial.volume, initial.tone, initial.motion,
                                         initial.density, initial.chaos);
            evolutionEngines_[i].setDensityRange(initial.densityRangeLow, initial.densityRangeHigh);
        }

        audioFormatManager_.registerBasicFormats();

        for (std::size_t i = 0; i < voices_.size(); ++i) {
            voiceRows_[i] = std::make_unique<DrumVoiceRow>(
                voices_[i], evolutionEngines_[i], [this, i] { loadSampleForVoice(i); });
            addAndMakeVisible(*voiceRows_[i]);
        }

        setLookAndFeel(&lookAndFeel_);

        logoImage_.setImage(
            juce::ImageCache::getFromMemory(BinaryData::AppIcon_png, BinaryData::AppIcon_pngSize));
        addAndMakeVisible(logoImage_);

        addAndMakeVisible(titleLabel);
        titleLabel.setText("Marmite", juce::dontSendNotification);
        titleLabel.setFont(juce::Font(juce::FontOptions(28.0f)).withStyle(juce::Font::bold));
        titleLabel.setJustificationType(juce::Justification::centredLeft);
        titleLabel.setColour(juce::Label::textColourId, MarmiteTheme::textPrimary);

        addAndMakeVisible(subtitleLabel);
        subtitleLabel.setText("A generative drum machine by Alban Bailly",
                              juce::dontSendNotification);
        subtitleLabel.setFont(juce::Font(juce::FontOptions(16.0f)));
        subtitleLabel.setJustificationType(juce::Justification::centredLeft);
        subtitleLabel.setColour(juce::Label::textColourId, MarmiteTheme::textSecondary);

        addAndMakeVisible(outputLabel);
        outputLabel.setText("Output", juce::dontSendNotification);
        outputLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
        outputLabel.setColour(juce::Label::textColourId, MarmiteTheme::textSecondary);

        addAndMakeVisible(outputDeviceBox);

        addAndMakeVisible(helpButton);
        helpButton.setButtonText("?");
        helpButton.onClick = [this] { showHelpPopup(); };

        addAndMakeVisible(recordButton);
        recordButton.setButtonText("Record");
        recordButton.setClickingTogglesState(false);
        recordButton.onClick = [this] { toggleRecording(); };
        recordingThread_.startThread();

        addAndMakeVisible(scenesButton);
        scenesButton.setButtonText("Scenes");
        scenesButton.onClick = [this] { showScenesPopup(); };

        addAndMakeVisible(bindingsButton);
        bindingsButton.setButtonText("Bindings");
        bindingsButton.onClick = [this] { showBindingsPopup(); };

        addAndMakeVisible(midiInputLabel);
        midiInputLabel.setText("MIDI In", juce::dontSendNotification);
        midiInputLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
        midiInputLabel.setColour(juce::Label::textColourId, MarmiteTheme::textSecondary);

        addAndMakeVisible(midiInputDeviceBox);

        // MIDI Out: mirrors every pattern trigger as a note on the
        // matching GM percussion key (see kVoiceMidiNotes), so Marmite
        // can drive an external drum VST/hardware instead of — or
        // alongside — its own procedural/loaded kit.
        addAndMakeVisible(midiOutputLabel);
        midiOutputLabel.setText("MIDI Out", juce::dontSendNotification);
        midiOutputLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
        midiOutputLabel.setColour(juce::Label::textColourId, MarmiteTheme::textSecondary);

        addAndMakeVisible(midiOutputDeviceBox);

        addAndMakeVisible(playButton);
        playButton.setButtonText("Play");
        playButton.setClickingTogglesState(false);
        playButton.addListener(this);

        addAndMakeVisible(stopButton);
        stopButton.setButtonText("Stop");
        stopButton.setEnabled(false);
        stopButton.addListener(this);

        addAndMakeVisible(resetButton);
        resetButton.setButtonText("Reset");
        resetButton.addListener(this);

        addAndMakeVisible(randomizeButton);
        randomizeButton.setButtonText("Randomize");
        randomizeButton.addListener(this);

        setUpKnob(tempoSlider, tempoLabel, "Tempo");
        tempoSlider.setRange(40.0, 240.0);
        tempoSlider.setValue(120.0);

        addAndMakeVisible(evolutionTitleLabel);
        evolutionTitleLabel.setText("Evolution", juce::dontSendNotification);
        evolutionTitleLabel.setFont(juce::Font(juce::FontOptions(13.0f)).withStyle(juce::Font::bold));
        evolutionTitleLabel.setColour(juce::Label::textColourId, MarmiteTheme::textPrimary);
        evolutionTitleLabel.setJustificationType(juce::Justification::centred);

        setUpKnob(evolutionAmountSlider, evolutionAmountLabel, "Amount");
        evolutionAmountSlider.setRange(0.0, 1.0);
        evolutionAmountSlider.setValue(0.0);

        setUpKnob(evolutionSpeedSlider, evolutionSpeedLabel, "Speed");
        evolutionSpeedSlider.setRange(0.0, 1.0);
        evolutionSpeedSlider.setValue(0.5);

        // Space: how "busy" the whole kit is right now, autonomously
        // drifting alongside Amount/Speed (see SpaceEvolver.h) so the
        // ensemble periodically breathes into genuinely sparse passages
        // instead of 8 independent per-voice densities always averaging
        // out to "something is playing". 1.0 = no attenuation (today's
        // behavior), lower values scale every voice's effective density
        // down together.
        setUpKnob(spaceSlider, spaceLabel, "Space");
        spaceSlider.setRange(0.0, 1.0);
        spaceSlider.setValue(1.0);

        addAndMakeVisible(reverbTitleLabel);
        reverbTitleLabel.setText("Reverb", juce::dontSendNotification);
        reverbTitleLabel.setFont(juce::Font(juce::FontOptions(13.0f)).withStyle(juce::Font::bold));
        reverbTitleLabel.setColour(juce::Label::textColourId, MarmiteTheme::textPrimary);
        reverbTitleLabel.setJustificationType(juce::Justification::centred);

        setUpKnob(roomSlider, roomLabel, "Room");
        roomSlider.setRange(0.0, 1.0);
        roomSlider.setValue(0.0);

        setUpKnob(decaySlider, decayLabel, "Decay");
        decaySlider.setRange(0.0, 1.0);
        decaySlider.setValue(0.0);

        addAndMakeVisible(delayTitleLabel);
        delayTitleLabel.setText("Delay", juce::dontSendNotification);
        delayTitleLabel.setFont(juce::Font(juce::FontOptions(13.0f)).withStyle(juce::Font::bold));
        delayTitleLabel.setColour(juce::Label::textColourId, MarmiteTheme::textPrimary);
        delayTitleLabel.setJustificationType(juce::Justification::centred);

        addAndMakeVisible(delayTimeLabel);
        delayTimeLabel.setText("Time", juce::dontSendNotification);
        delayTimeLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
        delayTimeLabel.setColour(juce::Label::textColourId, MarmiteTheme::textSecondary);
        delayTimeLabel.setJustificationType(juce::Justification::centred);

        addAndMakeVisible(delayTimeBox);
        for (int i = 0; i < static_cast<int>(kDelayDivisions.size()); ++i) {
            delayTimeBox.addItem(kDelayDivisions[static_cast<std::size_t>(i)].label, i + 1);
        }
        delayTimeBox.setSelectedId(2, juce::dontSendNotification);  // defaults to 1/8
        delayTimeBox.onChange = [this] {
            const int index = delayTimeBox.getSelectedId() - 1;
            if (index >= 0 && index < static_cast<int>(kDelayDivisions.size())) {
                delayBeatFraction_.store(kDelayDivisions[static_cast<std::size_t>(index)].beatFraction,
                                         std::memory_order_relaxed);
            }
        };

        setUpKnob(delayFeedbackSlider, delayFeedbackLabel, "Feedback");
        delayFeedbackSlider.setRange(0.0, 1.0);
        delayFeedbackSlider.setValue(0.0);

        addAndMakeVisible(masterVolumeTitleLabel);
        masterVolumeTitleLabel.setText("Volume", juce::dontSendNotification);
        masterVolumeTitleLabel.setFont(juce::Font(juce::FontOptions(13.0f)).withStyle(juce::Font::bold));
        masterVolumeTitleLabel.setColour(juce::Label::textColourId, MarmiteTheme::textPrimary);
        masterVolumeTitleLabel.setJustificationType(juce::Justification::centred);

        // Blank sub-label — same reasoning as Jerrican's master volume:
        // with only one knob, the group title above it already names it.
        setUpKnob(masterVolumeSlider, masterVolumeLabel, "");
        masterVolumeSlider.setRange(0.0, 1.0);
        masterVolumeSlider.setValue(1.0);

        addAndMakeVisible(statusLabel);
        statusLabel.setText("Transport stopped", juce::dontSendNotification);
        statusLabel.setFont(juce::Font(juce::FontOptions(14.0f)));
        statusLabel.setColour(juce::Label::textColourId, MarmiteTheme::textSecondary);

        addAndMakeVisible(openRecordingFolderButton);
        openRecordingFolderButton.setButtonText("Open Folder");
        openRecordingFolderButton.setVisible(false);
        openRecordingFolderButton.onClick = [this] { currentRecordingFile_.revealToUser(); };

        setSize(1870, 820);
        setAudioChannels(0, 2);
        populateOutputDeviceBox();
        outputDeviceBox.onChange = [this] { outputDeviceSelected(); };
        populateMidiInputBox();
        midiInputDeviceBox.onChange = [this] { midiInputDeviceSelected(); };
        populateMidiOutputBox();
        midiOutputDeviceBox.onChange = [this] { midiOutputDeviceSelected(); };
        startTimerHz(30);
    }

    ~MarmiteEditor() override {
        if (currentMidiInputId_.isNotEmpty()) {
            deviceManager.removeMidiInputDeviceCallback(currentMidiInputId_, this);
        }
        recorder_.stop();
        recordingThread_.stopThread(2000);
        shutdownAudio();
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override { g.fillAll(MarmiteTheme::background); }

    void resized() override {
        logoImage_.setBounds(40, 16, 34, 34);
        titleLabel.setBounds(82, 16, 300, 34);
        subtitleLabel.setBounds(82, 48, getWidth() - 340, 22);

        outputLabel.setBounds(getWidth() - 260, 16, 220, 14);
        helpButton.setBounds(getWidth() - 292, 32, 24, 24);
        outputDeviceBox.setBounds(getWidth() - 260, 32, 220, 24);

        // MIDI cluster sits left of Output/Help, same top-aligned row.
        midiOutputLabel.setBounds(getWidth() - 442, 16, 140, 14);
        midiOutputDeviceBox.setBounds(getWidth() - 442, 32, 140, 24);
        midiInputLabel.setBounds(getWidth() - 592, 16, 140, 14);
        midiInputDeviceBox.setBounds(getWidth() - 592, 32, 140, 24);
        bindingsButton.setBounds(getWidth() - 682, 32, 80, 24);
        scenesButton.setBounds(getWidth() - 762, 32, 70, 24);
        recordButton.setBounds(getWidth() - 852, 32, 80, 24);

        const int bottomY = getHeight() - 20;

        constexpr int buttonColumnWidth = 150;
        constexpr int buttonColumnGap = 10;
        constexpr int buttonRowHeight = 36;
        constexpr int buttonRowGap = 8;
        const int buttonCol1X = 40;
        const int buttonCol2X = buttonCol1X + buttonColumnWidth + buttonColumnGap;
        const int buttonRow2Y = bottomY - buttonRowHeight;
        const int buttonRow1Y = buttonRow2Y - buttonRowGap - buttonRowHeight;

        playButton.setBounds(buttonCol1X, buttonRow1Y, buttonColumnWidth, buttonRowHeight);
        stopButton.setBounds(buttonCol2X, buttonRow1Y, buttonColumnWidth, buttonRowHeight);
        resetButton.setBounds(buttonCol1X, buttonRow2Y, buttonColumnWidth, buttonRowHeight);
        randomizeButton.setBounds(buttonCol2X, buttonRow2Y, buttonColumnWidth, buttonRowHeight);

        constexpr int knobSize = 56;
        constexpr int knobLabelHeight = 14;
        constexpr int knobTextBoxHeight = 16;
        constexpr int knobColumnWidth = 84;
        constexpr int titleHeight = 16;
        constexpr int titleGap = 4;

        const int knobBoxTop = bottomY - knobSize - knobTextBoxHeight;
        const int knobLabelTop = knobBoxTop - knobLabelHeight - 2;

        const int tempoBlockX = 420;
        tempoLabel.setBounds(tempoBlockX, knobLabelTop, knobColumnWidth, knobLabelHeight);
        tempoSlider.setBounds(tempoBlockX + (knobColumnWidth - knobSize) / 2, knobBoxTop, knobSize,
                              knobSize + knobTextBoxHeight);

        const int evolutionBlockX = tempoBlockX + knobColumnWidth + 30;
        const int evolutionBlockWidth = knobColumnWidth * 3;
        const int evolutionTitleTop = knobLabelTop - titleGap - titleHeight;
        evolutionTitleLabel.setBounds(evolutionBlockX, evolutionTitleTop, evolutionBlockWidth,
                                      titleHeight);

        const int amountColumnX = evolutionBlockX;
        const int speedColumnX = evolutionBlockX + knobColumnWidth;
        const int spaceColumnX = evolutionBlockX + knobColumnWidth * 2;
        evolutionAmountLabel.setBounds(amountColumnX, knobLabelTop, knobColumnWidth, knobLabelHeight);
        evolutionAmountSlider.setBounds(amountColumnX + (knobColumnWidth - knobSize) / 2, knobBoxTop,
                                        knobSize, knobSize + knobTextBoxHeight);
        evolutionSpeedLabel.setBounds(speedColumnX, knobLabelTop, knobColumnWidth, knobLabelHeight);
        evolutionSpeedSlider.setBounds(speedColumnX + (knobColumnWidth - knobSize) / 2, knobBoxTop,
                                       knobSize, knobSize + knobTextBoxHeight);
        spaceLabel.setBounds(spaceColumnX, knobLabelTop, knobColumnWidth, knobLabelHeight);
        spaceSlider.setBounds(spaceColumnX + (knobColumnWidth - knobSize) / 2, knobBoxTop, knobSize,
                              knobSize + knobTextBoxHeight);

        // Reverb: same title-over-two-knobs shape as Evolution.
        const int reverbBlockX = evolutionBlockX + evolutionBlockWidth + 30;
        const int reverbBlockWidth = knobColumnWidth * 2;
        reverbTitleLabel.setBounds(reverbBlockX, evolutionTitleTop, reverbBlockWidth, titleHeight);

        const int roomColumnX = reverbBlockX;
        const int decayColumnX = reverbBlockX + knobColumnWidth;
        roomLabel.setBounds(roomColumnX, knobLabelTop, knobColumnWidth, knobLabelHeight);
        roomSlider.setBounds(roomColumnX + (knobColumnWidth - knobSize) / 2, knobBoxTop, knobSize,
                             knobSize + knobTextBoxHeight);
        decayLabel.setBounds(decayColumnX, knobLabelTop, knobColumnWidth, knobLabelHeight);
        decaySlider.setBounds(decayColumnX + (knobColumnWidth - knobSize) / 2, knobBoxTop, knobSize,
                              knobSize + knobTextBoxHeight);

        // Delay: title over [Time combo, Feedback knob] — the combo sits
        // in the same column position/height range a knob would.
        const int delayBlockX = reverbBlockX + reverbBlockWidth + 30;
        const int delayBlockWidth = knobColumnWidth * 2;
        delayTitleLabel.setBounds(delayBlockX, evolutionTitleTop, delayBlockWidth, titleHeight);

        const int delayTimeColumnX = delayBlockX;
        const int delayFeedbackColumnX = delayBlockX + knobColumnWidth;
        delayTimeLabel.setBounds(delayTimeColumnX, knobLabelTop, knobColumnWidth, knobLabelHeight);
        delayTimeBox.setBounds(delayTimeColumnX + 6, knobBoxTop + (knobSize - 24) / 2, knobColumnWidth - 12,
                               24);
        delayFeedbackLabel.setBounds(delayFeedbackColumnX, knobLabelTop, knobColumnWidth,
                                     knobLabelHeight);
        delayFeedbackSlider.setBounds(delayFeedbackColumnX + (knobColumnWidth - knobSize) / 2, knobBoxTop,
                                      knobSize, knobSize + knobTextBoxHeight);

        // Master Volume: single knob, same shape as Jerrican's.
        const int volumeBlockX = delayBlockX + delayBlockWidth + 30;
        const int volumeBlockWidth = knobColumnWidth;
        masterVolumeTitleLabel.setBounds(volumeBlockX, evolutionTitleTop, volumeBlockWidth, titleHeight);
        masterVolumeLabel.setBounds(volumeBlockX, knobLabelTop, volumeBlockWidth, knobLabelHeight);
        masterVolumeSlider.setBounds(volumeBlockX + (volumeBlockWidth - knobSize) / 2, knobBoxTop,
                                     knobSize, knobSize + knobTextBoxHeight);

        // Floated from the right edge (rather than left-anchored after
        // the knob blocks) so it stays put regardless of window width —
        // a fixed left-anchor + offset for the button broke as soon as
        // the window got narrower than expected (e.g. clamped to fit a
        // smaller screen).
        constexpr int statusBlockWidth = 300;
        const int statusRight = getWidth() - 40;
        statusLabel.setJustificationType(juce::Justification::centredRight);
        statusLabel.setBounds(statusRight - statusBlockWidth, bottomY - 48, statusBlockWidth, 22);
        openRecordingFolderButton.setBounds(statusRight - 100, bottomY - 22, 100, 22);

        // 4x2 voice-card grid, filling the space between the header and
        // the global transport/knob row above.
        constexpr int gridColumns = 4;
        constexpr int gridRows = 2;
        constexpr int gridMargin = 40;
        constexpr int gridGap = 12;
        const int gridTop = 90;
        const int gridBottom = evolutionTitleTop - 20;
        const int gridWidth = getWidth() - gridMargin * 2;
        const int gridHeight = gridBottom - gridTop;
        const int cardWidth = (gridWidth - gridGap * (gridColumns - 1)) / gridColumns;
        const int cardHeight = (gridHeight - gridGap * (gridRows - 1)) / gridRows;

        for (std::size_t i = 0; i < voiceRows_.size(); ++i) {
            const int column = static_cast<int>(i) % gridColumns;
            const int row = static_cast<int>(i) / gridColumns;
            const int cardX = gridMargin + column * (cardWidth + gridGap);
            const int cardY = gridTop + row * (cardHeight + gridGap);
            voiceRows_[i]->setBounds(cardX, cardY, cardWidth, cardHeight);
        }
    }

    void setUpKnob(juce::Slider& slider, juce::Label& label, const char* labelText) {
        addAndMakeVisible(label);
        label.setText(labelText, juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(12.0f)));
        label.setColour(juce::Label::textColourId, MarmiteTheme::textSecondary);
        label.setJustificationType(juce::Justification::centred);

        addAndMakeVisible(slider);
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setNumDecimalPlacesToDisplay(2);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
        slider.addListener(this);
    }

    // Toggled from the Record button. No file-save dialog on start — like
    // an instrument's own record button rather than a DAW export flow, it
    // starts immediately into a timestamped file under ~/Music, and Stop
    // finalizes it. Recording state is independent of the transport: you
    // can record silence (Stop) as easily as a running pattern.
    void toggleRecording() {
        if (recorder_.isRecording()) {
            recorder_.stop();
            recordButton.setToggleState(false, juce::dontSendNotification);
            statusLabel.setText("Recording saved", juce::dontSendNotification);
            openRecordingFolderButton.setVisible(true);
            return;
        }

        openRecordingFolderButton.setVisible(false);
        const auto directory =
            juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Marmite Recordings");
        const auto filename =
            "Marmite-" + juce::Time::getCurrentTime().formatted("%Y-%m-%d-%H%M%S") + ".wav";
        currentRecordingFile_ = directory.getChildFile(filename);

        if (recorder_.startRecording(currentRecordingFile_, sampleRate_)) {
            recordButton.setToggleState(true, juce::dontSendNotification);
            statusLabel.setText("Recording...", juce::dontSendNotification);
        } else {
            statusLabel.setText("Couldn't start recording", juce::dontSendNotification);
        }
    }

    void populateOutputDeviceBox() {
        outputDeviceBox.clear(juce::dontSendNotification);

        auto* deviceType = deviceManager.getCurrentDeviceTypeObject();
        if (deviceType == nullptr) {
            return;
        }

        deviceType->scanForDevices();
        const auto deviceNames = deviceType->getDeviceNames(false);
        for (int i = 0; i < deviceNames.size(); ++i) {
            outputDeviceBox.addItem(deviceNames[i], i + 1);
        }

        if (auto* currentDevice = deviceManager.getCurrentAudioDevice()) {
            const int index = deviceNames.indexOf(currentDevice->getName());
            if (index >= 0) {
                outputDeviceBox.setSelectedId(index + 1, juce::dontSendNotification);
            }
        }
    }

    void outputDeviceSelected() {
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.outputDeviceName = outputDeviceBox.getText();
        deviceManager.setAudioDeviceSetup(setup, true);
    }

    void populateMidiInputBox() {
        midiInputDeviceBox.clear(juce::dontSendNotification);
        midiInputDeviceBox.addItem("None", 1);

        const auto devices = juce::MidiInput::getAvailableDevices();
        for (int i = 0; i < devices.size(); ++i) {
            midiInputDeviceBox.addItem(devices[i].name, i + 2);
        }
        midiInputDeviceBox.setSelectedId(1, juce::dontSendNotification);
    }

    void midiInputDeviceSelected() {
        if (currentMidiInputId_.isNotEmpty()) {
            deviceManager.setMidiInputDeviceEnabled(currentMidiInputId_, false);
            deviceManager.removeMidiInputDeviceCallback(currentMidiInputId_, this);
            currentMidiInputId_ = {};
        }

        const int selectedId = midiInputDeviceBox.getSelectedId();
        if (selectedId <= 1) {
            return;  // "None"
        }

        const auto devices = juce::MidiInput::getAvailableDevices();
        const int index = selectedId - 2;
        if (index < 0 || index >= devices.size()) {
            return;
        }

        currentMidiInputId_ = devices[index].identifier;
        deviceManager.setMidiInputDeviceEnabled(currentMidiInputId_, true);
        deviceManager.addMidiInputDeviceCallback(currentMidiInputId_, this);
    }

    void populateMidiOutputBox() {
        midiOutputDeviceBox.clear(juce::dontSendNotification);
        midiOutputDeviceBox.addItem("None", 1);

        const auto devices = juce::MidiOutput::getAvailableDevices();
        for (int i = 0; i < devices.size(); ++i) {
            midiOutputDeviceBox.addItem(devices[i].name, i + 2);
        }
        midiOutputDeviceBox.setSelectedId(1, juce::dontSendNotification);
    }

    // Unlike MIDI In (managed through AudioDeviceManager's callback
    // registration), AudioDeviceManager has no equivalent concept of a
    // MIDI *output* device — juce::MidiOutput is opened and owned
    // directly.
    void midiOutputDeviceSelected() {
        midiOutput_.reset();
        currentMidiOutputId_ = {};

        const int selectedId = midiOutputDeviceBox.getSelectedId();
        if (selectedId <= 1) {
            return;  // "None"
        }

        const auto devices = juce::MidiOutput::getAvailableDevices();
        const int index = selectedId - 2;
        if (index < 0 || index >= devices.size()) {
            return;
        }

        currentMidiOutputId_ = devices[index].identifier;
        midiOutput_ = juce::MidiOutput::openDevice(currentMidiOutputId_);
    }

    // Runs on the MIDI thread. Every write below goes through the same
    // atomic DrumVoiceModel/DrumEvolutionEngine setters already used from
    // the UI thread (see DrumVoiceRow::sliderValueChanged) — safe under
    // the lock-free pattern already established throughout this codebase,
    // no new synchronization needed.
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override {
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
            // Each flips the matching Evolution opt-in/out flag for the
            // focused voice and, when switching on, resyncs it — the same
            // logic DrumVoiceRow::buttonClicked's manual toggle click
            // already applies, just against the focused voice's engine
            // instead of a fixed one. Only touches DrumEvolutionEngine's
            // atomics, so — unlike transport — this is safe to apply
            // directly from the MIDI thread.
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
            // Transport is the one exception to "just touch atomics" —
            // handling a press means touching JUCE Components
            // (playButton/stopButton/statusLabel), which are only safe on
            // the message thread. Dispatch there via callAsync, guarded by
            // a SafePointer so a message still in flight when the app
            // quits becomes a no-op instead of touching a freed
            // MarmiteEditor (same pattern used for the Save-As dialog in
            // MidiBindingsPopup).
            case MidiTarget::TransportPlay: {
                juce::Component::SafePointer<MarmiteEditor> safeThis(this);
                juce::MessageManager::callAsync([safeThis] {
                    if (safeThis != nullptr) safeThis->handlePlayPressed();
                });
                break;
            }
            case MidiTarget::TransportStop: {
                juce::Component::SafePointer<MarmiteEditor> safeThis(this);
                juce::MessageManager::callAsync([safeThis] {
                    if (safeThis != nullptr) safeThis->handleStopPressed();
                });
                break;
            }
            case MidiTarget::TransportReset: {
                juce::Component::SafePointer<MarmiteEditor> safeThis(this);
                juce::MessageManager::callAsync([safeThis] {
                    if (safeThis != nullptr) safeThis->handleResetPressed();
                });
                break;
            }
            case MidiTarget::TransportRandomize: {
                juce::Component::SafePointer<MarmiteEditor> safeThis(this);
                juce::MessageManager::callAsync([safeThis] {
                    if (safeThis != nullptr) safeThis->handleRandomizePressed();
                });
                break;
            }
            case MidiTarget::Tempo: {
                const float bpm = 40.0f + value * (240.0f - 40.0f);
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
        }
    }

    void showHelpPopup() {
        auto content = std::make_unique<HelpContent>();
        content->setSize(480, 620);
        juce::CallOutBox::launchAsynchronously(std::move(content), helpButton.getScreenBounds(),
                                               nullptr);
    }

    void showBindingsPopup() {
        auto content = std::make_unique<MidiBindingsPopup>(this);
        content->setSize(440, 720);
        juce::CallOutBox::launchAsynchronously(std::move(content), bindingsButton.getScreenBounds(),
                                               nullptr);
    }

    void showScenesPopup() {
        auto content = std::make_unique<ScenesPopup>(this);
        content->setSize(400, 140);
        juce::CallOutBox::launchAsynchronously(std::move(content), scenesButton.getScreenBounds(),
                                               nullptr);
    }

    // Reads every control's current value — everything a Scene captures,
    // deliberately excluding transport run/stop state (isPlaying_).
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
        }
        scene.tempo = static_cast<float>(tempoSlider.getValue());
        scene.evolutionAmount = evolutionAmount_.load(std::memory_order_relaxed);
        scene.evolutionSpeed = evolutionSpeed_.load(std::memory_order_relaxed);
        scene.space = spaceDisplay_.load(std::memory_order_relaxed);
        scene.reverbRoom = reverbRoom_.load(std::memory_order_relaxed);
        scene.reverbDecay = reverbDecay_.load(std::memory_order_relaxed);
        scene.delayBeatFraction = delayBeatFraction_.load(std::memory_order_relaxed);
        scene.delayFeedback = delayFeedback_.load(std::memory_order_relaxed);
        scene.masterVolume = masterVolume_.load(std::memory_order_relaxed);
        return scene;
    }

    // Writes a full snapshot back — only ever called from the message
    // thread (the Scenes popup's UI), same as Randomize, so no threading
    // concerns despite touching Components (via refreshFromModel()/
    // refreshEvolutionToggles()) as well as atomics. Transport run/stop
    // state is untouched, matching what a Scene does and doesn't capture.
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

            voiceRows_[i]->refreshFromModel();
            voiceRows_[i]->refreshEvolutionToggles();
        }

        tempoSlider.setValue(scene.tempo, juce::dontSendNotification);
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

        refreshGlobalKnobFromAtomic(evolutionAmountSlider, evolutionAmount_);
        refreshGlobalKnobFromAtomic(evolutionSpeedSlider, evolutionSpeed_);
        refreshGlobalKnobFromAtomic(spaceSlider, spaceDisplay_);
        refreshGlobalKnobFromAtomic(roomSlider, reverbRoom_);
        refreshGlobalKnobFromAtomic(decaySlider, reverbDecay_);
        refreshGlobalKnobFromAtomic(delayFeedbackSlider, delayFeedback_);
        refreshGlobalKnobFromAtomic(masterVolumeSlider, masterVolume_);

        for (int i = 0; i < static_cast<int>(kDelayDivisions.size()); ++i) {
            if (std::abs(kDelayDivisions[static_cast<std::size_t>(i)].beatFraction -
                        scene.delayBeatFraction) < 1e-4f) {
                delayTimeBox.setSelectedId(i + 1, juce::dontSendNotification);
                break;
            }
        }

        statusLabel.setText("Scene loaded", juce::dontSendNotification);
    }

    void buttonClicked(juce::Button* button) override {
        if (button == &playButton) {
            handlePlayPressed();
        } else if (button == &stopButton) {
            handleStopPressed();
        } else if (button == &resetButton) {
            handleResetPressed();
        } else if (button == &randomizeButton) {
            handleRandomizePressed();
        }
    }

    void handlePlayPressed() {
        isPlaying_.store(true, std::memory_order_relaxed);
        playButton.setToggleState(true, juce::dontSendNotification);
        stopButton.setEnabled(true);
        statusLabel.setText("Transport running", juce::dontSendNotification);
    }

    void handleStopPressed() {
        isPlaying_.store(false, std::memory_order_relaxed);
        playButton.setToggleState(false, juce::dontSendNotification);
        stopButton.setEnabled(false);
        statusLabel.setText("Transport stopped", juce::dontSendNotification);
    }

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
            voiceRows_[i]->refreshFromModel();
            voiceRows_[i]->resetEvolutionToggles();
        }
        // Space isn't a per-voice control, but it's part of the same
        // "back to defaults" contract — full busy-ness, matching what
        // every voice's own density defaults already assume.
        spaceDisplay_.store(1.0f, std::memory_order_relaxed);
        spaceEvolver_.resync(1.0f);
        statusLabel.setText("Voices reset to defaults", juce::dontSendNotification);
    }

    void handleRandomizePressed() {
        // Rerolls the five macros per voice, regardless of transport
        // state — matches Jerrican's Randomize scope. Tempo, Space, and
        // sample assignment are identity/ensemble-level, not per-voice
        // levers, so none of them are touched here.
        //
        // Density also gets a freshly rolled range (a random center +
        // half-width, density itself landing inside it) rather than just
        // a bare value — so Randomize can hand one voice a narrow,
        // consistently-sparse range and another a wide, unpredictable
        // one, instead of every voice sharing the same full 0..1
        // autonomous-drift range and slowly averaging toward "medium,
        // always busy" (see DrumEvolutionEngine::setDensityRange).
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
            voiceRows_[i]->refreshFromModel();
        }
        statusLabel.setText("Voices randomized", juce::dontSendNotification);
    }

    // Opens a file picker for voiceIndex and, if a file is chosen, reads
    // it, mixes it down to mono, and resamples it to the device's
    // current sample rate before swapping it in. Runs on the message
    // thread (FileChooser callbacks always do) — the actual buffer swap
    // is guarded by sampleBuffersMutex_ so the audio thread never reads
    // a buffer mid-resize (see sampleBuffersMutex_ for why a lock is
    // used here despite the rest of the engine being lock-free).
    void loadSampleForVoice(std::size_t voiceIndex) {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Load sample for " + juce::String(voices_[voiceIndex].getName()),
            juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");

        const auto flags = juce::FileBrowserComponent::openMode |
                           juce::FileBrowserComponent::canSelectFiles;

        fileChooser_->launchAsync(flags, [this, voiceIndex](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (file != juce::File()) {
                applyLoadedSample(voiceIndex, file);
            }
        });
    }

    void applyLoadedSample(std::size_t voiceIndex, const juce::File& file) {
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
            statusLabel.setText("Couldn't read " + file.getFileName(), juce::dontSendNotification);
            return;
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
            statusLabel.setText("Couldn't read " + file.getFileName(), juce::dontSendNotification);
            return;
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

        statusLabel.setText(juce::String(voices_[voiceIndex].getName()) + ": loaded " +
                                file.getFileName(),
                            juce::dontSendNotification);
    }

    void sliderValueChanged(juce::Slider* slider) override {
        if (slider == &tempoSlider) {
            patternClock_.setBpm(static_cast<float>(tempoSlider.getValue()));
        } else if (slider == &evolutionAmountSlider) {
            evolutionAmount_.store(static_cast<float>(evolutionAmountSlider.getValue()),
                                   std::memory_order_relaxed);
        } else if (slider == &evolutionSpeedSlider) {
            evolutionSpeed_.store(static_cast<float>(evolutionSpeedSlider.getValue()),
                                  std::memory_order_relaxed);
        } else if (slider == &spaceSlider) {
            const float value = static_cast<float>(spaceSlider.getValue());
            spaceDisplay_.store(value, std::memory_order_relaxed);
            spaceEvolver_.resync(value);
        } else if (slider == &roomSlider) {
            reverbRoom_.store(static_cast<float>(roomSlider.getValue()), std::memory_order_relaxed);
        } else if (slider == &decaySlider) {
            reverbDecay_.store(static_cast<float>(decaySlider.getValue()), std::memory_order_relaxed);
        } else if (slider == &delayFeedbackSlider) {
            delayFeedback_.store(static_cast<float>(delayFeedbackSlider.getValue()),
                                 std::memory_order_relaxed);
        } else if (slider == &masterVolumeSlider) {
            masterVolume_.store(static_cast<float>(masterVolumeSlider.getValue()),
                                std::memory_order_relaxed);
        }
    }

    void timerCallback() override {
        // Voice rows, focus highlight, and the global knobs all need to
        // reflect state regardless of transport — MIDI can change any of
        // it at any time now. refreshFromModel()/the drag-guard below
        // already skip any control the user is actively touching.
        const int focused = focusedVoiceIndex_.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < voiceRows_.size(); ++i) {
            voiceRows_[i]->refreshFromModel();
            voiceRows_[i]->refreshEvolutionToggles();
            voiceRows_[i]->setFocused(static_cast<int>(i) == focused);
        }

        refreshGlobalKnobFromAtomic(evolutionAmountSlider, evolutionAmount_);
        refreshGlobalKnobFromAtomic(evolutionSpeedSlider, evolutionSpeed_);
        refreshGlobalKnobFromAtomic(roomSlider, reverbRoom_);
        refreshGlobalKnobFromAtomic(decaySlider, reverbDecay_);
        refreshGlobalKnobFromAtomic(delayFeedbackSlider, delayFeedback_);
        refreshGlobalKnobFromAtomic(masterVolumeSlider, masterVolume_);
    }

    static void refreshGlobalKnobFromAtomic(juce::Slider& slider, std::atomic<float>& value) {
        if (!slider.isMouseButtonDown()) {
            slider.setValue(value.load(std::memory_order_relaxed), juce::dontSendNotification);
        }
    }

    void prepareToPlay(int /*samplesPerBlockExpected*/, double sampleRate) override {
        patternClock_.setSampleRate(sampleRate);
        patternClock_.setBpm(static_cast<float>(tempoSlider.getValue()));
        for (auto& engine : evolutionEngines_) {
            engine.setSampleRate(sampleRate);
        }
        // Procedural kit is synthesized once the real device sample rate
        // is known, so playback pitch is correct regardless of device.
        sampleBuffers_ = ProceduralKit::makeDefaultKit(sampleRate);

        reverb_.setSampleRate(sampleRate);
        delayLine_.setSampleRate(sampleRate);
        sampleRate_ = sampleRate;
    }

    void releaseResources() override {}

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override {
        auto* left = bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample);
        auto* right = bufferToFill.buffer->getNumChannels() > 1
                          ? bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample)
                          : nullptr;

        const bool playing = isPlaying_.load(std::memory_order_relaxed);
        const float evolutionAmount = evolutionAmount_.load(std::memory_order_relaxed);
        const float evolutionSpeed = evolutionSpeed_.load(std::memory_order_relaxed);
        const float masterVolume = masterVolume_.load(std::memory_order_relaxed);

        // Delay time is tempo-synced (a note division of the shared
        // clock's beat), recomputed once per block rather than per
        // sample — cheap, and tempo/division don't need sample-accurate
        // updates the way trigger timing does.
        const float beatFraction = delayBeatFraction_.load(std::memory_order_relaxed);
        const double samplesPerBeat = patternClock_.getSamplesPerSubdivision() * 4.0;
        delayLine_.setDelaySamples(static_cast<int>(samplesPerBeat * beatFraction));
        delayLine_.setFeedback(delayFeedback_.load(std::memory_order_relaxed));
        // No separate mix/amount knob in this pass (just Time +
        // Feedback) — a fixed, moderate send level, same "always
        // present, shaped by the other knob" spirit as a classic
        // slapback delay at feedback=0.
        delayLine_.setWet(0.3f);

        // sampleBuffers_ can be swapped out from under this callback by
        // Load Sample on the message thread (see sampleBuffersMutex_).
        // Held for the whole block rather than per-trigger: cheap since
        // it's essentially always uncontended, and it guarantees every
        // sample in this block sees one consistent set of buffers.
        const std::lock_guard<std::mutex> sampleBuffersLock(sampleBuffersMutex_);

        for (int sample = 0; sample < bufferToFill.numSamples; ++sample) {
            const bool onGridBoundary = playing && patternClock_.tick();

            float mixedLeft = 0.0f;
            float mixedRight = 0.0f;

            const float space = spaceEvolver_.update(playing ? evolutionAmount : 0.0f, evolutionSpeed,
                                                     sampleRate_, spaceDisplay_);

            for (std::size_t i = 0; i < voices_.size(); ++i) {
                auto& voice = voices_[i];

                evolutionEngines_[i].update(voice, playing ? evolutionAmount : 0.0f, evolutionSpeed);

                if (playing && voice.isEnabled()) {
                    const auto trigger = patternClouds_[i].update(
                        onGridBoundary, voice.getDensity() * space, voice.getChaos(), voice.getMotion(),
                        patternClock_.getSamplesPerSubdivision());
                    if (trigger.has_value()) {
                        const float pitchSemitones = (voice.getTone() - 0.5f) * 24.0f;
                        const float velocity = trigger->velocity * voice.getVolume();
                        samplePools_[i].trigger(&sampleBuffers_[i], pitchSemitones, velocity);

                        // Mirrors the trigger out as MIDI, so an external
                        // drum VST/hardware can be driven by the same
                        // generative pattern instead of (or alongside)
                        // the internal kit. sendMessageNow from the
                        // audio thread isn't hard-realtime-guaranteed,
                        // but is the standard, widely-used approach for
                        // this — CoreMIDI dispatch is fast and triggers
                        // are sparse (a handful of hits per beat, not
                        // per-sample), so the glitch risk is negligible
                        // in practice.
                        if (midiOutput_ != nullptr) {
                            const auto midiVelocity = static_cast<juce::uint8>(
                                juce::jlimit(1, 127, static_cast<int>(velocity * 127.0f)));
                            const int note = kVoiceMidiNotes[i];
                            midiOutput_->sendMessageNow(
                                juce::MidiMessage::noteOn(kMidiDrumChannel, note, midiVelocity));
                            midiOutput_->sendMessageNow(juce::MidiMessage::noteOff(kMidiDrumChannel, note));
                        }
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

        // Reverb is a block-based send, same placement Jerrican uses —
        // Room=0/Decay=0 (the defaults) give wetLevel=0 and unity dry
        // gain, so it's a no-op until deliberately opened up.
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
            reverb_.processStereo(left, right, bufferToFill.numSamples);
        }

        // Tap the exact signal being sent to the output device — after
        // every other stage, so a recording matches what's actually
        // audible. Duplicates left into both channels for mono devices,
        // since the recorder always writes a stereo file.
        const float* recordChannels[2] = {left, right != nullptr ? right : left};
        recorder_.recordBlock(recordChannels, bufferToFill.numSamples);
    }

private:
    // Simple alias so the array-of-InitialDrumVoice constructor calls
    // above read cleanly.
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
    // Crash rare, Glitch deliberately leaning into chaos by identity —
    // demonstrates the Chaos macro's range without the user touching
    // anything yet. Order matches ProceduralKit::makeDefaultKit.
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

    static VoiceModel makeVoiceModel(const InitialDrumVoice& initial) {
        return VoiceModel(initial.name, initial.enabled, initial.volume, initial.tone,
                          initial.motion, initial.density, initial.chaos);
    }

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

    MarmiteLookAndFeel lookAndFeel_;
    juce::ImageComponent logoImage_;
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label outputLabel;
    juce::ComboBox outputDeviceBox;
    juce::TextButton helpButton;
    juce::TextButton recordButton;
    juce::TimeSliceThread recordingThread_{"Marmite Recording Thread"};
    AudioRecorder recorder_{recordingThread_};
    juce::File currentRecordingFile_;
    juce::TextButton scenesButton;
    juce::TextButton bindingsButton;
    juce::Label midiInputLabel;
    juce::ComboBox midiInputDeviceBox;
    juce::String currentMidiInputId_;
    juce::Label midiOutputLabel;
    juce::ComboBox midiOutputDeviceBox;
    juce::String currentMidiOutputId_;
    std::unique_ptr<juce::MidiOutput> midiOutput_;
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::TextButton resetButton;
    juce::TextButton randomizeButton;
    juce::Label tempoLabel;
    juce::Slider tempoSlider;
    juce::Label evolutionTitleLabel;
    juce::Label evolutionAmountLabel;
    juce::Slider evolutionAmountSlider;
    juce::Label evolutionSpeedLabel;
    juce::Slider evolutionSpeedSlider;
    juce::Label spaceLabel;
    juce::Slider spaceSlider;
    juce::Label reverbTitleLabel;
    juce::Label roomLabel;
    juce::Slider roomSlider;
    juce::Label decayLabel;
    juce::Slider decaySlider;
    juce::Label delayTitleLabel;
    juce::Label delayTimeLabel;
    juce::ComboBox delayTimeBox;
    juce::Label delayFeedbackLabel;
    juce::Slider delayFeedbackSlider;
    juce::Label masterVolumeTitleLabel;
    juce::Label masterVolumeLabel;
    juce::Slider masterVolumeSlider;
    juce::Label statusLabel;
    juce::TextButton openRecordingFolderButton;

    std::atomic<bool> isPlaying_{false};
    std::atomic<float> evolutionAmount_{0.0f};
    std::atomic<float> evolutionSpeed_{0.5f};
    std::atomic<float> spaceDisplay_{1.0f};
    SpaceEvolver spaceEvolver_{0x1e7ad03u};
    std::atomic<float> reverbRoom_{0.0f};
    std::atomic<float> reverbDecay_{0.0f};
    std::atomic<float> delayFeedback_{0.0f};
    std::atomic<float> delayBeatFraction_{0.5f};
    std::atomic<float> masterVolume_{1.0f};
    double sampleRate_ = 44100.0;

    PatternClock patternClock_;
    std::array<VoiceModel, 8> voices_;
    std::array<DrumEvolutionEngine, 8> evolutionEngines_;
    std::array<PatternCloud, 8> patternClouds_;
    std::array<SampleVoicePool, 8> samplePools_;
    std::array<SampleBuffer, 8> sampleBuffers_;
    // Guards sampleBuffers_ against a torn read/write between the audio
    // thread (reads via SamplePlayer's non-owning pointer, potentially
    // across many blocks while a sample rings out) and the message
    // thread (replaces a whole buffer wholesale on Load Sample) — the
    // one place in the engine a lock is warranted instead of an atomic,
    // since what's shared is a variable-size buffer, not a scalar.
    std::mutex sampleBuffersMutex_;
    juce::Reverb reverb_;
    DelayLine delayLine_;
    FastRandom randomizeRandom_{0xc0ffeeu};

    std::array<std::unique_ptr<DrumVoiceRow>, 8> voiceRows_;
    juce::AudioFormatManager audioFormatManager_;
    std::unique_ptr<juce::FileChooser> fileChooser_;

    std::atomic<int> focusedVoiceIndex_{0};
    // Which preset each popup is "on", persisted here rather than in the
    // popup itself — MidiBindingsPopup/ScenesPopup are rebuilt from
    // scratch every time their CallOutBox reopens, so without this,
    // reopening after editing a loaded preset would lose track of which
    // one to Override.
    juce::String currentMidiPresetName_;
    juce::String currentSceneName_;
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
};

class MarmiteMainWindow : public juce::DocumentWindow {
public:
    MarmiteMainWindow()
        : juce::DocumentWindow("Marmite", juce::Colours::black, juce::DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setContentOwned(new MarmiteEditor(), true);
        setResizable(true, true);
        centreWithSize(1870, 820);
        setVisible(true);
    }

    void closeButtonPressed() override {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class MarmiteApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Marmite"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override {
        window = std::make_unique<MarmiteMainWindow>();
    }

    void shutdown() override { window = nullptr; }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<MarmiteMainWindow> window;
};

START_JUCE_APPLICATION(MarmiteApplication)
