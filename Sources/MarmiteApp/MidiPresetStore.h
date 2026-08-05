#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "MidiBindingManager.h"
#include "PresetNameValidation.h"

// Named binding-preset persistence, plain std::filesystem/fstream (no
// JUCE, no JSON dependency) — same testability convention as the rest of
// Sources/MarmiteApp: the directory is injected rather than hardcoded, so
// tests point it at a temp directory the same way DrumEvolutionEngine
// takes an injected seed. Main.cpp wires in the real app-support path.
//
// One file per preset, named "<preset>.mbind" inside the given directory.
// Format: one line per bound target, "<TargetName>=<Type>:<Channel>:<Number>"
// — small and human-diffable, no library needed for something this simple.
class MidiPresetStore {
public:
    explicit MidiPresetStore(std::filesystem::path directory) : directory_(std::move(directory)) {}

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

    bool save(const std::string& name, const MidiBindingManager& manager) const {
        if (!isValidPresetName(name)) {
            return false;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(directory_, errorCode);

        std::ofstream file(pathFor(name));
        if (!file.is_open()) {
            return false;
        }

        for (const auto target : kAllMidiTargets) {
            const auto binding = manager.getBinding(target);
            if (!binding.has_value()) {
                continue;
            }
            file << midiTargetName(target) << "="
                 << (binding->type == MidiEvent::Type::ControlChange ? "CC" : "Note") << ":"
                 << binding->channel << ":" << binding->number << "\n";
        }
        return true;
    }

    bool load(const std::string& name, MidiBindingManager& manager) const {
        if (!isValidPresetName(name)) {
            return false;
        }

        std::ifstream file(pathFor(name));
        if (!file.is_open()) {
            return false;
        }

        manager.clearAll();
        std::string line;
        while (std::getline(file, line)) {
            parseLine(line, manager);
        }
        return true;
    }

    bool remove(const std::string& name) const {
        if (!isValidPresetName(name)) {
            return false;
        }

        std::error_code errorCode;
        return std::filesystem::remove(pathFor(name), errorCode);
    }

private:
    static constexpr const char* kExtension = ".mbind";

    std::filesystem::path pathFor(const std::string& name) const {
        return directory_ / (name + kExtension);
    }

    static void parseLine(const std::string& line, MidiBindingManager& manager) {
        const auto equalsPos = line.find('=');
        if (equalsPos == std::string::npos) {
            return;
        }
        const auto target = midiTargetFromName(line.substr(0, equalsPos));
        if (!target.has_value()) {
            return;
        }

        std::istringstream rest(line.substr(equalsPos + 1));
        std::string typeToken, channelToken, numberToken;
        if (!std::getline(rest, typeToken, ':') || !std::getline(rest, channelToken, ':') ||
            !std::getline(rest, numberToken)) {
            return;
        }

        MidiBinding binding;
        binding.type = typeToken == "CC" ? MidiEvent::Type::ControlChange : MidiEvent::Type::NoteOn;
        try {
            binding.channel = std::stoi(channelToken);
            binding.number = std::stoi(numberToken);
        } catch (const std::exception&) {
            // Malformed line (hand-edited or corrupted preset file) — skip
            // it rather than letting stoi's exception crash the app.
            return;
        }
        manager.setBinding(*target, binding);
    }

    std::filesystem::path directory_;
};
