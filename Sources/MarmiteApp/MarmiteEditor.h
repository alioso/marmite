#pragma once

#include <JuceHeader.h>

#include <BinaryData.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

#include "MarmiteLookAndFeel.h"
#include "MarmiteProcessor.h"
#include "MarmiteTheme.h"
#include "MidiBindingManager.h"
#include "MidiPresetStore.h"
#include "SceneState.h"
#include "ScenePresetStore.h"

// For StandalonePluginHolder::getInstance()/showAudioSettingsDialog() —
// the Audio/MIDI Settings button below only does anything when running
// as Standalone (see its onClick), but this header compiles fine
// regardless of format (same as StandaloneApp.h), so it's included
// unconditionally rather than guarded per-format.
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

// Content shown in the help popup (launched from the "?" button). Read-
// only, scrollable if the window is short, styled to match the rest of
// the app rather than the OS-native AlertWindow look.
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
            "Each of the 8 voices has its own beat-aware accent profile "
            "(Kick favors beats 1/3, Snare favors 2/4, etc.) shaping a "
            "bar-persistent pattern that holds steady rather than "
            "repeating a fixed step grid, mutating gradually over time "
            "(tied to Evolution Amount) rather than looping identically. "
            "The global Wild knob sets how strongly that structure is "
            "honored, from a locked-in basic groove to glitchy chaos.\n\n"
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
            "Busy - this voice's offset from the room's Wild setting (see "
            "below) - 0.5 (its default) means \"follow the room exactly,\" "
            "lower/higher pulls just this voice calmer/wilder than the "
            "rest of the kit.\n"
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
            "Wild - the room's baseline groove complexity: 0 (ACDC-rigid) "
            "through 50 (Jungle/breakbeat) to 100 (Squarepusher-glitchy). "
            "Each voice has its own accent profile (Kick favors beats "
            "1/3, Snare favors 2/4, etc.); Wild continuously reshapes how "
            "strongly that profile is honored and how often the pattern "
            "mutates (tied to Evolution Amount), rather than switching "
            "between fixed presets. The pattern itself holds steady "
            "across bars rather than re-rolling every hit - a per-voice "
            "Busy knob (see above) offsets that voice above or below "
            "this room-wide setting.\n"
            "Reverb - Room/Decay, a global send for the whole mix. Both "
            "are 0 by default (no reverb, output unchanged) and never "
            "evolve on their own.\n"
            "Delay - Time (a tempo-synced note division) and Feedback, a "
            "fixed-level send.\n"
            "Volume - master output level, applied after everything else. "
            "Full by default (unchanged output).\n\n"
            "OUTPUT / MIDI\n"
            "Audio device and MIDI input/output routing are handled "
            "outside this window: in Standalone, via Options > Audio/MIDI "
            "Settings; as an AU/VST3 plugin, by your host. MIDI In "
            "controls Marmite (MIDI Learn) - it does not trigger drum "
            "hits directly, the pattern engine plays itself. Bindings "
            "opens MIDI Learn, where each per-voice control applies to "
            "whichever voice is currently focused (switch focus with "
            "Voice Select pads); Transport is bindable too, as a global "
            "action. MIDI Out mirrors every pattern trigger as a note on "
            "a GM percussion key (channel 10), so an external drum VST/"
            "hardware module can be driven by the same generative "
            "pattern instead of - or alongside - Marmite's own kit. "
            "Scenes saves/loads a full snapshot of every knob and toggle "
            "(transport state isn't included).\n\n"
            "RECORDING (Standalone only)\n"
            "Record captures the exact final mix (everything, post-"
            "Reverb) to a timestamped WAV under ~/Music/Marmite "
            "Recordings - click again to stop and finalize the file. "
            "Independent of the transport: you can record silence as "
            "easily as a running pattern. \"Open Folder\" reveals the "
            "most recent recording in Finder. As an AU/VST3 plugin, use "
            "your host's own recording/bounce workflow instead.\n\n" +
            juce::String(juce::CharPointer_UTF8("\xc2\xa9")) +
            " 2026 Alban Bailly. All rights reserved.";

        editor_.setText(bodyText, false);
        addAndMakeVisible(editor_);
    }

    void resized() override { editor_.setBounds(getLocalBounds()); }

private:
    juce::TextEditor editor_;
};

class MarmiteAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Button::Listener,
                                    private juce::Slider::Listener,
                                    private juce::Timer {
public:
    // One voice's card in the 4x2 grid: name + enable LED, the five
    // macro knobs, a per-macro Evolution toggle row, and a Load Sample
    // button. References straight into the owning processor's voice/
    // engine arrays rather than owning any model state itself.
    class DrumVoiceRow : public juce::Component,
                         private juce::Button::Listener,
                         private juce::Slider::Listener {
    public:
        DrumVoiceRow(DrumVoiceModel& voice, DrumEvolutionEngine& evolutionEngine,
                    std::function<void()> onLoadSampleClicked,
                    std::function<void()> onClearSampleClicked)
            : voiceRef_(voice), evolutionEngineRef_(evolutionEngine),
              onLoadSampleClicked_(std::move(onLoadSampleClicked)),
              onClearSampleClicked_(std::move(onClearSampleClicked)) {
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
            setUpKnob(chaosSlider_, chaosLabel_, "Busy");
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

            addAndMakeVisible(clearSampleButton_);
            clearSampleButton_.setButtonText("Clear");
            clearSampleButton_.setEnabled(false);
            clearSampleButton_.onClick = onClearSampleClicked_;
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
        // MIDI-bound per-voice knobs currently drive.
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
            constexpr int clearButtonWidth = 56;
            constexpr int buttonGap = 6;
            const int loadButtonWidth = contentWidth - clearButtonWidth - buttonGap;
            loadSampleButton_.setBounds(padding, loadButtonY, loadButtonWidth, 22);
            clearSampleButton_.setBounds(padding + loadButtonWidth + buttonGap, loadButtonY,
                                         clearButtonWidth, 22);
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

        // Called after Reset/Randomize/MIDI change the model out from
        // under this row, so the knobs catch up. Skips any control the
        // user is actively dragging.
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
        // five toggle LEDs, without touching the values they control —
        // needed since MIDI Learn can flip these flags from off-screen.
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

        // Reflects which sample is currently loaded (or the default kit,
        // or a broken reference) onto the Load Sample button — the
        // processor's sampleFiles_ array is the source of truth, this is
        // purely a display update.
        void refreshSampleLabel(const juce::File& file, bool missing) {
            if (file == juce::File()) {
                loadSampleButton_.setButtonText("Load Sample...");
                loadSampleButton_.setColour(juce::TextButton::textColourOffId,
                                            MarmiteTheme::textPrimary);
                clearSampleButton_.setEnabled(false);
                return;
            }

            loadSampleButton_.setButtonText((missing ? "Missing: " : "") + file.getFileName());
            loadSampleButton_.setColour(juce::TextButton::textColourOffId,
                                        missing ? MarmiteTheme::danger : MarmiteTheme::textPrimary);
            clearSampleButton_.setEnabled(true);
        }

    private:
        static constexpr std::size_t kEvolutionToggleCount = 5;
        static constexpr const char* kEvolutionCaptions[kEvolutionToggleCount] = {
            "Volume", "Tone", "Motion", "Density", "Busy"};

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
        std::function<void()> onClearSampleClicked_;
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
        juce::TextButton clearSampleButton_;
    };

    // Shared preset row (Combo + Override/Revert/Save As/Delete), used by
    // both MidiBindingsPopup and ScenesPopup.
    class PresetControls : public juce::Component, private juce::Timer {
    public:
        struct Callbacks {
            std::function<std::vector<std::string>()> listNames;
            std::function<bool(const std::string&)> loadNamed;
            std::function<bool(const std::string&)> saveNamed;
            std::function<bool(const std::string&)> removeNamed;
            std::function<bool(const std::string&)> matchesNamed;
            std::function<bool()> hasMeaningfulContent;
            std::function<void()> onDeleted;
            std::function<juce::String()> getCurrentName;
            std::function<void(const juce::String&)> setCurrentName;
        };

        static constexpr int kPreferredHeight = 22 + 6 + 22;

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

    // Scenes popup, opened from the "Scenes" button.
    class ScenesPopup : public juce::Component {
    public:
        explicit ScenesPopup(MarmiteAudioProcessorEditor* owner)
            : presetControls_(
                  "Scene",
                  PresetControls::Callbacks{
                      .listNames = [owner] { return owner->processor_.scenePresetStore_.listPresetNames(); },
                      .loadNamed =
                          [owner](const std::string& name) {
                              SceneState scene;
                              if (!owner->processor_.scenePresetStore_.load(name, scene)) {
                                  return false;
                              }
                              owner->applySceneState(scene);
                              return true;
                          },
                      .saveNamed =
                          [owner](const std::string& name) {
                              return owner->processor_.scenePresetStore_.save(name, owner->captureSceneState());
                          },
                      .removeNamed = [owner](const std::string& name) {
                          return owner->processor_.scenePresetStore_.remove(name);
                      },
                      .matchesNamed =
                          [owner](const std::string& name) {
                              SceneState scene;
                              return owner->processor_.scenePresetStore_.load(name, scene) &&
                                     scene == owner->captureSceneState();
                          },
                      .hasMeaningfulContent = [] { return true; },
                      .onDeleted = [owner] { owner->handleResetPressed(); },
                      .getCurrentName = [owner] { return owner->processor_.currentSceneName_; },
                      .setCurrentName = [owner](const juce::String& name) {
                          owner->processor_.currentSceneName_ = name;
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

    // MIDI Learn popup, opened from the "Bindings" button.
    class MidiBindingsPopup : public juce::Component, private juce::Timer {
    public:
        explicit MidiBindingsPopup(MarmiteAudioProcessorEditor* owner)
            : owner_(owner),
              presetControls_(
                  "Preset",
                  PresetControls::Callbacks{
                      .listNames = [owner] { return owner->processor_.midiPresetStore_.listPresetNames(); },
                      .loadNamed =
                          [owner](const std::string& name) {
                              return owner->processor_.midiPresetStore_.load(name, owner->processor_.midiBindings_);
                          },
                      .saveNamed =
                          [owner](const std::string& name) {
                              return owner->processor_.midiPresetStore_.save(name, owner->processor_.midiBindings_);
                          },
                      .removeNamed = [owner](const std::string& name) {
                          return owner->processor_.midiPresetStore_.remove(name);
                      },
                      .matchesNamed =
                          [owner](const std::string& name) {
                              MidiBindingManager temp;
                              return owner->processor_.midiPresetStore_.load(name, temp) &&
                                     temp.equals(owner->processor_.midiBindings_);
                          },
                      .hasMeaningfulContent =
                          [owner] {
                              for (const auto target : kAllMidiTargets) {
                                  if (owner->processor_.midiBindings_.getBinding(target).has_value()) {
                                      return true;
                                  }
                              }
                              return false;
                          },
                      .onDeleted = [owner] { owner->processor_.midiBindings_.clearAll(); },
                      .getCurrentName = [owner] { return owner->processor_.currentMidiPresetName_; },
                      .setCurrentName = [owner](const juce::String& name) {
                          owner->processor_.currentMidiPresetName_ = name;
                      },
                  }) {
            addAndMakeVisible(presetControls_);

            // 32 target rows don't reliably fit a fixed popup height on
            // smaller screens — everything below the preset row lives in
            // rowsContainer_, scrolled via viewport_.
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
                learnButtons_[i].onClick = [this, target] { owner_->processor_.midiBindings_.armLearn(target); };

                rowsContainer_.addAndMakeVisible(clearButtons_[i]);
                clearButtons_[i].setButtonText("Clear");
                clearButtons_[i].onClick = [this, target] {
                    owner_->processor_.midiBindings_.clearBinding(target);
                };
            }

            refreshReadouts();
            startTimerHz(10);
        }

        void resized() override {
            constexpr int padding = 12;
            const int contentWidth = getWidth() - padding * 2;

            presetControls_.setBounds(padding, padding, contentWidth, PresetControls::kPreferredHeight);
            const int viewportTop = padding + PresetControls::kPreferredHeight + 10;

            viewport_.setBounds(0, viewportTop, getWidth(), getHeight() - viewportTop);

            const int rowsContentWidth = getWidth() - viewport_.getScrollBarThickness();
            const int innerContentWidth = rowsContentWidth - padding * 2;
            int y = padding;
            y = layoutSection(perVoiceSectionLabel_, 0, 11, padding, innerContentWidth, y);
            y = layoutSection(voiceSelectSectionLabel_, 11, 19, padding, innerContentWidth, y);
            y = layoutSection(transportSectionLabel_, 19, 23, padding, innerContentWidth, y);
            y = layoutSection(globalSectionLabel_, 23, 33, padding, innerContentWidth, y);
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
                if (owner_->processor_.midiBindings_.isLearning() &&
                    owner_->processor_.midiBindings_.learningTarget() == target) {
                    targetReadouts_[i].setText("Listening...", juce::dontSendNotification);
                    continue;
                }
                targetReadouts_[i].setText(
                    describeBinding(owner_->processor_.midiBindings_.getBinding(target)),
                    juce::dontSendNotification);
            }
        }

        void timerCallback() override { refreshReadouts(); }

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
                case MidiTarget::VoiceChaos: return "Busy";
                case MidiTarget::VoiceEnabledToggle: return "Enabled toggle";
                case MidiTarget::VoiceVolumeEvoToggle: return "Evolve: Volume";
                case MidiTarget::VoiceToneEvoToggle: return "Evolve: Tone";
                case MidiTarget::VoiceMotionEvoToggle: return "Evolve: Motion";
                case MidiTarget::VoiceDensityEvoToggle: return "Evolve: Density";
                case MidiTarget::VoiceChaosEvoToggle: return "Evolve: Busy";
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
                case MidiTarget::Wild: return "Wild";
            }
            return "";
        }

        MarmiteAudioProcessorEditor* owner_;
        PresetControls presetControls_;
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

    explicit MarmiteAudioProcessorEditor(MarmiteAudioProcessor& processorRef)
        : juce::AudioProcessorEditor(&processorRef), processor_(processorRef) {
        for (std::size_t i = 0; i < voiceRows_.size(); ++i) {
            voiceRows_[i] = std::make_unique<DrumVoiceRow>(
                processor_.voice(i), processor_.evolutionEngine(i), [this, i] { loadSampleForVoice(i); },
                [this, i] { clearSample(i); });
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

        addAndMakeVisible(helpButton);
        helpButton.setButtonText("?");
        helpButton.onClick = [this] { showHelpPopup(); };

        // Standalone's own window chrome used to have an "Options" menu
        // (Audio/MIDI Settings/Save state/Load state/Reset) baked into
        // its title bar — switching to a native title bar (see
        // StandaloneApp.h) makes JUCE's version of that button collapse
        // to zero height, since it's positioned relative to the
        // non-native title bar height JUCE itself no longer draws. Audio
        // device/MIDI routing still needs to be reachable somehow, so
        // this reproduces just that one entry point, themed to match the
        // rest of the header. Only meaningful in Standalone — AU/VST3
        // hosts own that routing entirely, so it's hidden there.
        addAndMakeVisible(audioSettingsButton);
        audioSettingsButton.setButtonText("Audio/MIDI...");
        audioSettingsButton.setVisible(processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone);
        audioSettingsButton.onClick = [] {
            if (auto* holder = juce::StandalonePluginHolder::getInstance()) {
                holder->showAudioSettingsDialog();
            }
        };

        addAndMakeVisible(recordButton);
        recordButton.setButtonText("Record");
        recordButton.setClickingTogglesState(false);
        recordButton.onClick = [this] { toggleRecording(); };
        // A DAW hosting this as a plugin already has its own record/
        // bounce workflow — a plugin silently writing its own WAV to
        // ~/Music independent of the host is redundant there. Standalone
        // has no such host, so it's the only place this earns its keep.
        recordButton.setVisible(processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone);

        addAndMakeVisible(scenesButton);
        scenesButton.setButtonText("Scenes");
        scenesButton.onClick = [this] { showScenesPopup(); };

        addAndMakeVisible(bindingsButton);
        bindingsButton.setButtonText("Bindings");
        bindingsButton.onClick = [this] { showBindingsPopup(); };

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
        tempoSlider.setValue(processor_.tempo().load(std::memory_order_relaxed));

        addAndMakeVisible(evolutionTitleLabel);
        evolutionTitleLabel.setText("Evolution", juce::dontSendNotification);
        evolutionTitleLabel.setFont(juce::Font(juce::FontOptions(13.0f)).withStyle(juce::Font::bold));
        evolutionTitleLabel.setColour(juce::Label::textColourId, MarmiteTheme::textPrimary);
        evolutionTitleLabel.setJustificationType(juce::Justification::centred);

        setUpKnob(evolutionAmountSlider, evolutionAmountLabel, "Amount");
        evolutionAmountSlider.setRange(0.0, 1.0);
        evolutionAmountSlider.setValue(processor_.evolutionAmount().load(std::memory_order_relaxed));

        setUpKnob(evolutionSpeedSlider, evolutionSpeedLabel, "Speed");
        evolutionSpeedSlider.setRange(0.0, 1.0);
        evolutionSpeedSlider.setValue(processor_.evolutionSpeed().load(std::memory_order_relaxed));

        setUpKnob(spaceSlider, spaceLabel, "Space");
        spaceSlider.setRange(0.0, 1.0);
        spaceSlider.setValue(processor_.spaceDisplay().load(std::memory_order_relaxed));

        // The room's baseline on the ACDC/Jungle/Squarepusher groove-
        // complexity curve — see GroovePattern.h. Each voice's Busy knob
        // offsets around this.
        setUpKnob(wildSlider, wildLabel, "Wild");
        wildSlider.setRange(0.0, 1.0);
        wildSlider.setValue(processor_.wildDisplay().load(std::memory_order_relaxed));

        addAndMakeVisible(reverbTitleLabel);
        reverbTitleLabel.setText("Reverb", juce::dontSendNotification);
        reverbTitleLabel.setFont(juce::Font(juce::FontOptions(13.0f)).withStyle(juce::Font::bold));
        reverbTitleLabel.setColour(juce::Label::textColourId, MarmiteTheme::textPrimary);
        reverbTitleLabel.setJustificationType(juce::Justification::centred);

        setUpKnob(roomSlider, roomLabel, "Room");
        roomSlider.setRange(0.0, 1.0);
        roomSlider.setValue(processor_.reverbRoom().load(std::memory_order_relaxed));

        setUpKnob(decaySlider, decayLabel, "Decay");
        decaySlider.setRange(0.0, 1.0);
        decaySlider.setValue(processor_.reverbDecay().load(std::memory_order_relaxed));

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
        for (int i = 0; i < static_cast<int>(MarmiteAudioProcessor::kDelayDivisions.size()); ++i) {
            delayTimeBox.addItem(MarmiteAudioProcessor::kDelayDivisions[static_cast<std::size_t>(i)].label,
                                 i + 1);
        }
        delayTimeBox.setSelectedId(2, juce::dontSendNotification);  // defaults to 1/8
        delayTimeBox.onChange = [this] {
            const int index = delayTimeBox.getSelectedId() - 1;
            if (index >= 0 && index < static_cast<int>(MarmiteAudioProcessor::kDelayDivisions.size())) {
                processor_.delayBeatFraction().store(
                    MarmiteAudioProcessor::kDelayDivisions[static_cast<std::size_t>(index)].beatFraction,
                    std::memory_order_relaxed);
            }
        };

        setUpKnob(delayFeedbackSlider, delayFeedbackLabel, "Feedback");
        delayFeedbackSlider.setRange(0.0, 1.0);
        delayFeedbackSlider.setValue(processor_.delayFeedback().load(std::memory_order_relaxed));

        addAndMakeVisible(masterVolumeTitleLabel);
        masterVolumeTitleLabel.setText("Volume", juce::dontSendNotification);
        masterVolumeTitleLabel.setFont(juce::Font(juce::FontOptions(13.0f)).withStyle(juce::Font::bold));
        masterVolumeTitleLabel.setColour(juce::Label::textColourId, MarmiteTheme::textPrimary);
        masterVolumeTitleLabel.setJustificationType(juce::Justification::centred);

        setUpKnob(masterVolumeSlider, masterVolumeLabel, "");
        masterVolumeSlider.setRange(0.0, 1.0);
        masterVolumeSlider.setValue(processor_.masterVolume().load(std::memory_order_relaxed));

        addAndMakeVisible(statusLabel);
        statusLabel.setText("Transport stopped", juce::dontSendNotification);
        statusLabel.setFont(juce::Font(juce::FontOptions(12.5f)));
        statusLabel.setColour(juce::Label::textColourId, MarmiteTheme::textSecondary);

        addAndMakeVisible(openRecordingFolderButton);
        openRecordingFolderButton.setButtonText("Open Folder");
        openRecordingFolderButton.setVisible(false);
        openRecordingFolderButton.onClick = [this] { processor_.getCurrentRecordingFile().revealToUser(); };

        for (std::size_t i = 0; i < voiceRows_.size(); ++i) {
            voiceRows_[i]->refreshFromModel();
            voiceRows_[i]->refreshEvolutionToggles();
            voiceRows_[i]->refreshSampleLabel(processor_.getSampleFile(i), processor_.isSampleMissing(i));
        }

        setSize(1460, 820);
        // Fixed-position bottom knob row assumes at least this much room
        // (it runs out to a fixed x, not width-relative — see
        // volumeBlockX in resized()), so shrinking below the design size
        // would clip content. Growing is fine, everything else is
        // already width/height-relative.
        setResizable(true, true);
        setResizeLimits(1460, 820, 2400, 1400);
        startTimerHz(30);
    }

    ~MarmiteAudioProcessorEditor() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override { g.fillAll(MarmiteTheme::background); }

    void resized() override {
        logoImage_.setBounds(40, 16, 34, 34);
        titleLabel.setBounds(82, 16, 300, 34);
        subtitleLabel.setBounds(82, 48, getWidth() - 340, 22);

        // Header cluster: Help (rightmost), Bindings, Scenes, Record,
        // Audio/MIDI (leftmost) — no Output/MIDI In/Out pickers here,
        // those are the host's/Standalone's job now. Set/Free lives down
        // by the transport row instead (see below) — a physical-style
        // switch, not a header button.
        helpButton.setBounds(getWidth() - 64, 32, 24, 24);
        bindingsButton.setBounds(getWidth() - 152, 32, 80, 24);
        scenesButton.setBounds(getWidth() - 232, 32, 70, 24);
        recordButton.setBounds(getWidth() - 322, 32, 80, 24);
        audioSettingsButton.setBounds(getWidth() - 442, 32, 110, 24);

        // Status + Open Folder sit on their own line under the header
        // button row, right-anchored over the same span — freed up here
        // by moving them off the bottom knob row (see below), which is
        // what let the window shrink from 1870 down to design width
        // without the two fighting over horizontal space. Left edge
        // flush with Audio/MIDI (leftmost header button), right edge of
        // Open Folder flush with Help (rightmost).
        statusLabel.setJustificationType(juce::Justification::centredRight);
        statusLabel.setBounds(getWidth() - 442, 60, 300, 18);
        openRecordingFolderButton.setBounds(getWidth() - 132, 60, 92, 18);

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
        const int evolutionBlockWidth = knobColumnWidth * 4;
        const int evolutionTitleTop = knobLabelTop - titleGap - titleHeight;
        evolutionTitleLabel.setBounds(evolutionBlockX, evolutionTitleTop, evolutionBlockWidth,
                                      titleHeight);

        const int amountColumnX = evolutionBlockX;
        const int speedColumnX = evolutionBlockX + knobColumnWidth;
        const int spaceColumnX = evolutionBlockX + knobColumnWidth * 2;
        const int wildColumnX = evolutionBlockX + knobColumnWidth * 3;
        evolutionAmountLabel.setBounds(amountColumnX, knobLabelTop, knobColumnWidth, knobLabelHeight);
        evolutionAmountSlider.setBounds(amountColumnX + (knobColumnWidth - knobSize) / 2, knobBoxTop,
                                        knobSize, knobSize + knobTextBoxHeight);
        evolutionSpeedLabel.setBounds(speedColumnX, knobLabelTop, knobColumnWidth, knobLabelHeight);
        evolutionSpeedSlider.setBounds(speedColumnX + (knobColumnWidth - knobSize) / 2, knobBoxTop,
                                       knobSize, knobSize + knobTextBoxHeight);
        spaceLabel.setBounds(spaceColumnX, knobLabelTop, knobColumnWidth, knobLabelHeight);
        wildLabel.setBounds(wildColumnX, knobLabelTop, knobColumnWidth, knobLabelHeight);
        wildSlider.setBounds(wildColumnX + (knobColumnWidth - knobSize) / 2, knobBoxTop, knobSize,
                             knobSize + knobTextBoxHeight);
        spaceSlider.setBounds(spaceColumnX + (knobColumnWidth - knobSize) / 2, knobBoxTop, knobSize,
                              knobSize + knobTextBoxHeight);

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

        const int volumeBlockX = delayBlockX + delayBlockWidth + 30;
        const int volumeBlockWidth = knobColumnWidth;
        masterVolumeTitleLabel.setBounds(volumeBlockX, evolutionTitleTop, volumeBlockWidth, titleHeight);
        masterVolumeLabel.setBounds(volumeBlockX, knobLabelTop, volumeBlockWidth, knobLabelHeight);
        masterVolumeSlider.setBounds(volumeBlockX + (volumeBlockWidth - knobSize) / 2, knobBoxTop,
                                     knobSize, knobSize + knobTextBoxHeight);

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

    // Toggled from the Record button.
    void toggleRecording() {
        if (processor_.isRecording()) {
            processor_.toggleRecording();
            recordButton.setToggleState(false, juce::dontSendNotification);
            statusLabel.setText("Recording saved", juce::dontSendNotification);
            openRecordingFolderButton.setVisible(true);
            return;
        }

        openRecordingFolderButton.setVisible(false);
        if (processor_.toggleRecording()) {
            recordButton.setToggleState(true, juce::dontSendNotification);
            statusLabel.setText("Recording...", juce::dontSendNotification);
        } else {
            statusLabel.setText("Couldn't start recording", juce::dontSendNotification);
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
        content->setSize(440, 660);
        juce::CallOutBox::launchAsynchronously(std::move(content), bindingsButton.getScreenBounds(),
                                               nullptr);
    }

    void showScenesPopup() {
        auto content = std::make_unique<ScenesPopup>(this);
        content->setSize(400, 140);
        juce::CallOutBox::launchAsynchronously(std::move(content), scenesButton.getScreenBounds(),
                                               nullptr);
    }

    SceneState captureSceneState() const { return processor_.captureSceneState(); }

    // Applies a Scene to the processor, then refreshes every Component
    // that reflects engine state.
    void applySceneState(const SceneState& scene) {
        processor_.applySceneState(scene);
        for (std::size_t i = 0; i < voiceRows_.size(); ++i) {
            voiceRows_[i]->refreshFromModel();
            voiceRows_[i]->refreshEvolutionToggles();
            voiceRows_[i]->refreshSampleLabel(processor_.getSampleFile(i), processor_.isSampleMissing(i));
        }

        tempoSlider.setValue(scene.tempo, juce::dontSendNotification);
        refreshGlobalKnobFromAtomic(evolutionAmountSlider, processor_.evolutionAmount());
        refreshGlobalKnobFromAtomic(evolutionSpeedSlider, processor_.evolutionSpeed());
        refreshGlobalKnobFromAtomic(spaceSlider, processor_.spaceDisplay());
        refreshGlobalKnobFromAtomic(wildSlider, processor_.wildDisplay());
        refreshGlobalKnobFromAtomic(roomSlider, processor_.reverbRoom());
        refreshGlobalKnobFromAtomic(decaySlider, processor_.reverbDecay());
        refreshGlobalKnobFromAtomic(delayFeedbackSlider, processor_.delayFeedback());
        refreshGlobalKnobFromAtomic(masterVolumeSlider, processor_.masterVolume());

        for (int i = 0; i < static_cast<int>(MarmiteAudioProcessor::kDelayDivisions.size()); ++i) {
            if (std::abs(MarmiteAudioProcessor::kDelayDivisions[static_cast<std::size_t>(i)].beatFraction -
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
        processor_.handlePlayPressed();
        playButton.setToggleState(true, juce::dontSendNotification);
        stopButton.setEnabled(true);
        statusLabel.setText("Transport running", juce::dontSendNotification);
    }

    void handleStopPressed() {
        processor_.handleStopPressed();
        playButton.setToggleState(false, juce::dontSendNotification);
        stopButton.setEnabled(false);
        statusLabel.setText("Transport stopped", juce::dontSendNotification);
    }

    void handleResetPressed() {
        processor_.handleResetPressed();
        for (std::size_t i = 0; i < voiceRows_.size(); ++i) {
            voiceRows_[i]->refreshFromModel();
            voiceRows_[i]->resetEvolutionToggles();
            voiceRows_[i]->refreshSampleLabel(processor_.getSampleFile(i), processor_.isSampleMissing(i));
        }
        refreshGlobalKnobFromAtomic(spaceSlider, processor_.spaceDisplay());
        statusLabel.setText("Voices reset to defaults", juce::dontSendNotification);
    }

    void handleRandomizePressed() {
        processor_.handleRandomizePressed();
        for (auto& row : voiceRows_) {
            row->refreshFromModel();
        }
        statusLabel.setText("Voices randomized", juce::dontSendNotification);
    }

    // Opens a file picker for voiceIndex and, if a file is chosen, hands
    // it to the processor to read/mix/resample/install. Runs on the
    // message thread (FileChooser callbacks always do).
    void loadSampleForVoice(std::size_t voiceIndex) {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Load sample for " + juce::String(processor_.voice(voiceIndex).getName()),
            juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");

        const auto flags = juce::FileBrowserComponent::openMode |
                           juce::FileBrowserComponent::canSelectFiles;

        fileChooser_->launchAsync(flags, [this, voiceIndex](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (file == juce::File()) {
                return;
            }
            if (processor_.applyLoadedSample(voiceIndex, file)) {
                voiceRows_[voiceIndex]->refreshSampleLabel(file, false);
            } else {
                statusLabel.setText("Couldn't read " + file.getFileName(), juce::dontSendNotification);
            }
        });
    }

    void clearSample(std::size_t voiceIndex) {
        processor_.clearSample(voiceIndex);
        voiceRows_[voiceIndex]->refreshSampleLabel(juce::File(), false);
    }

    void sliderValueChanged(juce::Slider* slider) override {
        if (slider == &tempoSlider) {
            processor_.setTempo(static_cast<float>(tempoSlider.getValue()));
        } else if (slider == &evolutionAmountSlider) {
            processor_.evolutionAmount().store(static_cast<float>(evolutionAmountSlider.getValue()),
                                               std::memory_order_relaxed);
        } else if (slider == &evolutionSpeedSlider) {
            processor_.evolutionSpeed().store(static_cast<float>(evolutionSpeedSlider.getValue()),
                                              std::memory_order_relaxed);
        } else if (slider == &spaceSlider) {
            processor_.setSpace(static_cast<float>(spaceSlider.getValue()));
        } else if (slider == &wildSlider) {
            processor_.setWild(static_cast<float>(wildSlider.getValue()));
        } else if (slider == &roomSlider) {
            processor_.reverbRoom().store(static_cast<float>(roomSlider.getValue()),
                                          std::memory_order_relaxed);
        } else if (slider == &decaySlider) {
            processor_.reverbDecay().store(static_cast<float>(decaySlider.getValue()),
                                           std::memory_order_relaxed);
        } else if (slider == &delayFeedbackSlider) {
            processor_.delayFeedback().store(static_cast<float>(delayFeedbackSlider.getValue()),
                                             std::memory_order_relaxed);
        } else if (slider == &masterVolumeSlider) {
            processor_.masterVolume().store(static_cast<float>(masterVolumeSlider.getValue()),
                                            std::memory_order_relaxed);
        }
    }

    void timerCallback() override {
        // Voice rows, focus highlight, transport buttons, and the global
        // knobs all need to reflect state regardless of who changed it —
        // MIDI can change any of it at any time now.
        const int focused = processor_.getFocusedVoiceIndex();
        for (std::size_t i = 0; i < voiceRows_.size(); ++i) {
            voiceRows_[i]->refreshFromModel();
            voiceRows_[i]->refreshEvolutionToggles();
            voiceRows_[i]->setFocused(static_cast<int>(i) == focused);
        }

        if (playButton.getToggleState() != processor_.isPlaying()) {
            playButton.setToggleState(processor_.isPlaying(), juce::dontSendNotification);
            stopButton.setEnabled(processor_.isPlaying());
        }

        refreshGlobalKnobFromAtomic(evolutionAmountSlider, processor_.evolutionAmount());
        refreshGlobalKnobFromAtomic(evolutionSpeedSlider, processor_.evolutionSpeed());
        refreshGlobalKnobFromAtomic(spaceSlider, processor_.spaceDisplay());
        refreshGlobalKnobFromAtomic(wildSlider, processor_.wildDisplay());
        refreshGlobalKnobFromAtomic(roomSlider, processor_.reverbRoom());
        refreshGlobalKnobFromAtomic(decaySlider, processor_.reverbDecay());
        refreshGlobalKnobFromAtomic(delayFeedbackSlider, processor_.delayFeedback());
        refreshGlobalKnobFromAtomic(masterVolumeSlider, processor_.masterVolume());
        if (!tempoSlider.isMouseButtonDown()) {
            tempoSlider.setValue(processor_.tempo().load(std::memory_order_relaxed),
                                 juce::dontSendNotification);
        }
    }

    static void refreshGlobalKnobFromAtomic(juce::Slider& slider, std::atomic<float>& value) {
        if (!slider.isMouseButtonDown()) {
            slider.setValue(value.load(std::memory_order_relaxed), juce::dontSendNotification);
        }
    }

private:
    MarmiteAudioProcessor& processor_;
    MarmiteLookAndFeel lookAndFeel_;
    juce::ImageComponent logoImage_;
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::TextButton helpButton;
    juce::TextButton audioSettingsButton;
    juce::TextButton recordButton;
    juce::TextButton scenesButton;
    juce::TextButton bindingsButton;
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
    juce::Label wildLabel;
    juce::Slider wildSlider;
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

    std::array<std::unique_ptr<DrumVoiceRow>, 8> voiceRows_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
};
