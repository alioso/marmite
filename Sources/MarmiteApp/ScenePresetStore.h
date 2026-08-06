#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "PresetNameValidation.h"
#include "SceneState.h"

// Named full-state-snapshot persistence — same shape and conventions as
// MidiPresetStore (injected directory, .mscene files, one key=value pair
// per line, isValidPresetName guard against path traversal, malformed
// lines skipped rather than crashing), just serializing a SceneState
// instead of a binding table. Ported from Jerrican's ScenePresetStore.h.
class ScenePresetStore {
public:
    explicit ScenePresetStore(std::filesystem::path directory) : directory_(std::move(directory)) {}

    std::vector<std::string> listPresetNames() const {
        std::vector<std::string> names;
        if (!std::filesystem::exists(directory_)) {
            return names;
        }
        for (const auto& entry : std::filesystem::directory_iterator(directory_)) {
            if (entry.is_regular_file() && entry.path().extension() == kExtension) {
                names.push_back(entry.path().stem().string());
            }
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    bool save(const std::string& name, const SceneState& state) const {
        if (!isValidPresetName(name)) {
            return false;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(directory_, errorCode);

        std::ofstream file(pathFor(name));
        if (!file.is_open()) {
            return false;
        }

        file << serialize(state);
        return true;
    }

    bool load(const std::string& name, SceneState& state) const {
        if (!isValidPresetName(name)) {
            return false;
        }

        std::ifstream file(pathFor(name));
        if (!file.is_open()) {
            return false;
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        state = deserialize(buffer.str());
        return true;
    }

    // Same key=value text shape as the on-disk .mscene format, exposed so
    // MarmiteAudioProcessor::getStateInformation/setStateInformation can
    // reuse it for a plugin instance's own state (a different concern
    // from named preset files, but identical serialization needs).
    static std::string serialize(const SceneState& state) {
        std::ostringstream file;
        for (std::size_t i = 0; i < state.voices.size(); ++i) {
            const auto& voice = state.voices[i];
            const std::string prefix = "voice" + std::to_string(i) + ".";
            file << prefix << "enabled=" << (voice.enabled ? 1 : 0) << "\n";
            file << prefix << "volume=" << voice.volume << "\n";
            file << prefix << "tone=" << voice.tone << "\n";
            file << prefix << "motion=" << voice.motion << "\n";
            file << prefix << "density=" << voice.density << "\n";
            file << prefix << "chaos=" << voice.chaos << "\n";
            file << prefix << "volumeEvoEnabled=" << (voice.volumeEvoEnabled ? 1 : 0) << "\n";
            file << prefix << "toneEvoEnabled=" << (voice.toneEvoEnabled ? 1 : 0) << "\n";
            file << prefix << "motionEvoEnabled=" << (voice.motionEvoEnabled ? 1 : 0) << "\n";
            file << prefix << "densityEvoEnabled=" << (voice.densityEvoEnabled ? 1 : 0) << "\n";
            file << prefix << "chaosEvoEnabled=" << (voice.chaosEvoEnabled ? 1 : 0) << "\n";
            // Not a number — written raw. A path containing a literal
            // newline would break this line-based format, but that's not
            // a byte a file dialog or Finder rename ever produces, so
            // it's not worth escaping for.
            file << prefix << "samplePath=" << voice.samplePath << "\n";
        }
        file << "global.tempo=" << state.tempo << "\n";
        file << "global.evolutionAmount=" << state.evolutionAmount << "\n";
        file << "global.evolutionSpeed=" << state.evolutionSpeed << "\n";
        file << "global.space=" << state.space << "\n";
        file << "global.reverbRoom=" << state.reverbRoom << "\n";
        file << "global.reverbDecay=" << state.reverbDecay << "\n";
        file << "global.delayBeatFraction=" << state.delayBeatFraction << "\n";
        file << "global.delayFeedback=" << state.delayFeedback << "\n";
        file << "global.masterVolume=" << state.masterVolume << "\n";
        return file.str();
    }

    static SceneState deserialize(const std::string& text) {
        SceneState state;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) {
            parseLine(line, state);
        }
        return state;
    }

    bool remove(const std::string& name) const {
        if (!isValidPresetName(name)) {
            return false;
        }

        std::error_code errorCode;
        return std::filesystem::remove(pathFor(name), errorCode);
    }

private:
    static constexpr const char* kExtension = ".mscene";

    std::filesystem::path pathFor(const std::string& name) const {
        return directory_ / (name + kExtension);
    }

    // samplePath is the one non-numeric field in an otherwise all-float
    // format — field name is checked before any numeric parse is
    // attempted, rather than the old shape (parse a float up front, then
    // dispatch by name), which would have thrown on every sample path and
    // silently dropped it via the malformed-line catch below.
    static void parseLine(const std::string& line, SceneState& state) {
        const auto equalsPos = line.find('=');
        if (equalsPos == std::string::npos) {
            return;
        }
        const std::string key = line.substr(0, equalsPos);
        const std::string valueToken = line.substr(equalsPos + 1);

        if (key.rfind("global.", 0) == 0) {
            const std::string field = key.substr(7);
            float value = 0.0f;
            if (!parseFloat(valueToken, value)) {
                return;
            }
            if (field == "tempo") state.tempo = value;
            else if (field == "evolutionAmount") state.evolutionAmount = value;
            else if (field == "evolutionSpeed") state.evolutionSpeed = value;
            else if (field == "space") state.space = value;
            else if (field == "reverbRoom") state.reverbRoom = value;
            else if (field == "reverbDecay") state.reverbDecay = value;
            else if (field == "delayBeatFraction") state.delayBeatFraction = value;
            else if (field == "delayFeedback") state.delayFeedback = value;
            else if (field == "masterVolume") state.masterVolume = value;
            return;
        }

        if (key.rfind("voice", 0) != 0) {
            return;
        }
        const auto dotPos = key.find('.');
        if (dotPos == std::string::npos) {
            return;
        }
        int index = -1;
        try {
            index = std::stoi(key.substr(5, dotPos - 5));
        } catch (const std::exception&) {
            return;
        }
        if (index < 0 || static_cast<std::size_t>(index) >= state.voices.size()) {
            return;
        }

        auto& voice = state.voices[static_cast<std::size_t>(index)];
        const std::string field = key.substr(dotPos + 1);

        if (field == "samplePath") {
            voice.samplePath = valueToken;
            return;
        }

        float value = 0.0f;
        if (!parseFloat(valueToken, value)) {
            return;
        }
        const bool boolValue = value != 0.0f;
        if (field == "enabled") voice.enabled = boolValue;
        else if (field == "volume") voice.volume = value;
        else if (field == "tone") voice.tone = value;
        else if (field == "motion") voice.motion = value;
        else if (field == "density") voice.density = value;
        else if (field == "chaos") voice.chaos = value;
        else if (field == "volumeEvoEnabled") voice.volumeEvoEnabled = boolValue;
        else if (field == "toneEvoEnabled") voice.toneEvoEnabled = boolValue;
        else if (field == "motionEvoEnabled") voice.motionEvoEnabled = boolValue;
        else if (field == "densityEvoEnabled") voice.densityEvoEnabled = boolValue;
        else if (field == "chaosEvoEnabled") voice.chaosEvoEnabled = boolValue;
    }

    // Malformed numeric line (hand-edited or corrupted scene file) — skip
    // it rather than letting stof's exception crash the app.
    static bool parseFloat(const std::string& token, float& out) {
        try {
            out = std::stof(token);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    std::filesystem::path directory_;
};
