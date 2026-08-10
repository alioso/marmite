#pragma once

#include <JuceHeader.h>

#include "GrooveProfiles.h"
#include "MarmiteTheme.h"

// A simple counter: the current beat number (1..meter numerator — every
// quarter note in a x/4 meter, every eighth note in a x/8 meter, so 6/8
// counts 1-6, 12/8 counts 1-12), large and centered. The downbeat (1)
// displays in the accent color, every other beat in plain text color —
// no geometry, just a number that counts up and resets. Polled from the
// editor's existing 30Hz timerCallback via refresh() rather than owning
// its own juce::Timer. Clicking it fires onClick — the editor wires this
// to re-snap the pattern's phase back to beat 1, a lightweight "resync
// the clock" action that's independent of Reset (which reverts every
// knob to its defaults) or Scene recall.
class BeatPulseIndicator : public juce::Component {
public:
    std::function<void()> onClick;

    void refresh(int numerator, int denominator, int currentSlot16) {
        const int meterIndex = GrooveProfiles::findMeterIndex(numerator, denominator);
        const int beat = beatForSlot(meterIndex, currentSlot16);
        if (meterIndex != meterIndex_ || beat != activeBeat_) {
            meterIndex_ = meterIndex;
            activeBeat_ = beat;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(MarmiteTheme::panel);
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(MarmiteTheme::panelBorder);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

        if (activeBeat_ < 0) {
            return;
        }
        g.setColour(activeBeat_ == 0 ? MarmiteTheme::accent : MarmiteTheme::textPrimary);
        g.setFont(juce::Font(juce::FontOptions(24.0f)).withStyle(juce::Font::bold));
        g.drawText(juce::String(activeBeat_ + 1), bounds, juce::Justification::centred);
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (onClick) {
            onClick();
        }
    }

    void mouseEnter(const juce::MouseEvent&) override {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

private:
    // Every counted beat spans the same number of 16th-note slots —
    // 16/denominator, which divides evenly for every supported meter
    // (denominator is always 4 or 8).
    int beatForSlot(int meterIndex, int slot) const {
        if (slot < 0) {
            return -1;
        }
        const auto& meter = GrooveProfiles::kMeters[static_cast<std::size_t>(meterIndex)];
        const int slotsPerBeat = 16 / meter.denominator;
        return juce::jlimit(0, meter.numerator - 1, slot / slotsPerBeat);
    }

    int meterIndex_ = GrooveProfiles::kDefaultMeterIndex;
    int activeBeat_ = -1;
};
