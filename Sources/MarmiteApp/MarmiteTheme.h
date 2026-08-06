#pragma once

#include <JuceHeader.h>

// Single source of truth for the app's palette. Terminal-monochrome:
// near-black graphite with a single phosphor-green accent — matches the
// dry, understated "geek, not jock" sound design rather than the
// original ember/charcoal placeholder, which read as too loud/warm once
// the kit itself went minimal. Danger stays red (Stop, destructive
// confirmations) — a deliberate exception, since a warning color needs
// to read as distinct from the phosphor accent, not part of its family.
namespace MarmiteTheme {

inline const juce::Colour background{0xff0a0b0au};
inline const juce::Colour panel{0xff131513u};
inline const juce::Colour panelBorder{0xff262a26u};
inline const juce::Colour accent{0xff6fdc8cu};
inline const juce::Colour accentDeep{0xff3f9d5cu};
inline const juce::Colour textPrimary{0xffd7ded8u};
inline const juce::Colour textSecondary{0xff6f766fu};
inline const juce::Colour trackOff{0xff2a2d2au};

inline const juce::Colour danger{0xffef4444u};
inline const juce::Colour dangerDeep{0xffb91c1cu};

}  // namespace MarmiteTheme
