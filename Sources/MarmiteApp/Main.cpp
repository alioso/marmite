// Entry point for every plugin wrapper (Standalone/AU/VST3) — each one
// links this single translation unit and calls createPluginFilter()
// (defined at the bottom of MarmiteProcessor.h) to obtain the one
// juce::AudioProcessor instance they host. All actual engine/UI code
// lives in MarmiteProcessor.h/MarmiteEditor.h.
#include "MarmiteProcessor.h"

// Only the Standalone build actually creates a StandaloneFilterWindow;
// compiling this into AU/VST3 too would be harmless (it's never
// instantiated there) but pointless, so it's guarded out.
#if JucePlugin_Build_Standalone
#include "StandaloneApp.h"
#endif
