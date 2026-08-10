#pragma once

#include <JuceHeader.h>

#include "GrooveProfiles.h"
#include "MarmiteTheme.h"

// A thin strip of lights, one per pulse group of the current time
// signature (see GrooveProfiles::MeterDef) — e.g. 4 lights for 4/4, 3 for
// 7/8's 2+2+3 grouping. The first light (the true downbeat) is always the
// accent color; whichever light corresponds to the currently-playing
// pulse group is brightened, the rest sit dim. Polled from the editor's
// existing 30Hz timerCallback via refresh() rather than owning its own
// juce::Timer.
class BeatPulseIndicator : public juce::Component {
public:
    void refresh(int numerator, int denominator, int currentSlot16) {
        const int meterIndex = GrooveProfiles::findMeterIndex(numerator, denominator);
        const int group = groupForSlot(meterIndex, currentSlot16);
        if (meterIndex != meterIndex_ || group != activeGroup_) {
            meterIndex_ = meterIndex;
            activeGroup_ = group;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override {
        const auto& meter = GrooveProfiles::kMeters[static_cast<std::size_t>(meterIndex_)];
        const int count = meter.groupCount;
        if (count <= 0) {
            return;
        }

        constexpr float gap = 6.0f;
        const float totalGap = gap * static_cast<float>(count - 1);
        const float lightWidth = (static_cast<float>(getWidth()) - totalGap) / static_cast<float>(count);
        const float h = static_cast<float>(getHeight());

        for (int i = 0; i < count; ++i) {
            const float x = static_cast<float>(i) * (lightWidth + gap);
            const bool isDownbeat = i == 0;
            juce::Colour colour = isDownbeat ? MarmiteTheme::accent : MarmiteTheme::textSecondary;
            colour = (i == activeGroup_) ? colour.brighter(0.7f) : colour.withAlpha(0.3f);
            g.setColour(colour);
            g.fillRoundedRectangle(x, 0.0f, lightWidth, h, h * 0.3f);
        }
    }

private:
    int groupForSlot(int meterIndex, int slot) const {
        if (slot < 0) {
            return -1;
        }
        const auto& meter = GrooveProfiles::kMeters[static_cast<std::size_t>(meterIndex)];
        int running = 0;
        for (int g = 0; g < meter.groupCount; ++g) {
            running += meter.groupLengths[static_cast<std::size_t>(g)];
            if (slot < running) {
                return g;
            }
        }
        return meter.groupCount - 1;
    }

    int meterIndex_ = GrooveProfiles::kDefaultMeterIndex;
    int activeGroup_ = -1;
};
