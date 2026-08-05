#pragma once

#include <string>

// Shared by every preset store (MidiPresetStore, ScenePresetStore) that
// turns a free-text, user-typed name (a "Save As..." prompt) directly
// into part of a filesystem path. Without this, a name like
// "../../Library/LaunchAgents/x" would resolve outside the intended
// presets directory via std::filesystem's operator/ — reject anything
// that could escape it rather than trusting it as a bare filename.
inline bool isValidPresetName(const std::string& name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    return name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}
