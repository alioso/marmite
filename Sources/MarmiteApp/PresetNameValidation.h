#pragma once

#include <cctype>
#include <string>

// Shared by every preset store (MidiPresetStore, PresetStore) that
// turns a free-text, user-typed name (a "Save As..." prompt) directly
// into part of a filesystem path. Without this, a name like
// "../../Library/LaunchAgents/x" would resolve outside the intended
// presets directory via std::filesystem's operator/ — reject anything
// that could escape it rather than trusting it as a bare filename.
inline bool isValidPresetName(const std::string& name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    // Leading/trailing whitespace should be trimmed by the caller (the
    // Save As prompt does) before it ever reaches here — rejecting it
    // instead of silently trimming ourselves means a name like " Calm
    // Copy" can't slip through as a distinct, hard-to-notice file (it
    // did once, before the prompt trimmed).
    if (std::isspace(static_cast<unsigned char>(name.front())) ||
        std::isspace(static_cast<unsigned char>(name.back()))) {
        return false;
    }
    return name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}
