#pragma once

#include <JuceHeader.h>

// Single source of truth for the app's palette. Placeholder for now —
// picked a cast-iron-pot-adjacent charcoal/ember palette (distinct from
// Jerrican's amber) mostly so the two apps don't look identical while
// this is still a bare shell; worth revisiting once the actual visual
// identity gets designed.
namespace MarmiteTheme {

inline const juce::Colour background{0xff0a0a0au};
inline const juce::Colour panel{0xff181615u};
inline const juce::Colour panelBorder{0xff2c2825u};
inline const juce::Colour accent{0xffe8562eu};
inline const juce::Colour accentDeep{0xffb8391cu};
inline const juce::Colour textPrimary{0xffffffffu};
inline const juce::Colour textSecondary{0xffb8b0a8u};
inline const juce::Colour trackOff{0xff36322fu};

inline const juce::Colour danger{0xffef4444u};
inline const juce::Colour dangerDeep{0xffb91c1cu};

}  // namespace MarmiteTheme
