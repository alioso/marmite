#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

// A simple stereo feedback delay line — no JUCE dependency (unlike
// juce::dsp::DelayLine, which would drag in the whole juce_dsp module
// for one small effect), same testability convention as the rest of the
// engine. Delay time is set in samples so tempo-synced note-division
// math lives in the caller (Main.cpp), which already knows the BPM.
class DelayLine {
public:
    void setSampleRate(double sampleRate) {
        const int maxDelaySamples = static_cast<int>(sampleRate * 2.0);  // up to 2s
        bufferLeft_.assign(static_cast<std::size_t>(std::max(1, maxDelaySamples)), 0.0f);
        bufferRight_.assign(bufferLeft_.size(), 0.0f);
        writeIndex_ = 0;
    }

    void setDelaySamples(int delaySamples) {
        if (bufferLeft_.empty()) {
            return;
        }
        delaySamples_ = std::clamp(delaySamples, 1, static_cast<int>(bufferLeft_.size()) - 1);
    }

    void setFeedback(float feedback) { feedback_ = std::clamp(feedback, 0.0f, 0.95f); }
    void setWet(float wet) { wet_ = std::clamp(wet, 0.0f, 1.0f); }

    void processSample(float& left, float& right) {
        if (bufferLeft_.empty()) {
            return;
        }

        const std::size_t size = bufferLeft_.size();
        const std::size_t readIndex =
            (writeIndex_ + size - static_cast<std::size_t>(delaySamples_)) % size;

        const float delayedLeft = bufferLeft_[readIndex];
        const float delayedRight = bufferRight_[readIndex];

        bufferLeft_[writeIndex_] = left + delayedLeft * feedback_;
        bufferRight_[writeIndex_] = right + delayedRight * feedback_;

        left += delayedLeft * wet_;
        right += delayedRight * wet_;

        writeIndex_ = (writeIndex_ + 1) % size;
    }

private:
    std::vector<float> bufferLeft_;
    std::vector<float> bufferRight_;
    std::size_t writeIndex_ = 0;
    int delaySamples_ = 1;
    float feedback_ = 0.0f;
    float wet_ = 0.0f;
};
