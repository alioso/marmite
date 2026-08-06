#pragma once

// JUCE's default Standalone app (juce_audio_plugin_client_Standalone.cpp)
// creates its window with setUsingNativeTitleBar() never called, so it
// falls back to a custom-drawn, non-native title bar — flat, no traffic
// lights, no OS rounded corners. This is a near-identical copy of that
// default JUCEApplication, existing solely to flip the window to a real
// native macOS title bar (which brings rounded corners and the standard
// traffic-light buttons back for free — the OS draws them). Everything
// else (settings persistence, MIDI auto-open policy, quit handling) is
// unchanged from JUCE's own implementation. Enabled via
// JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 (CMakeLists.txt), which only
// affects the Standalone target's translation unit — AU/VST3 never see
// this file. Ported from Jerrican's StandaloneApp.h.
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace MarmiteStandalone {

class App final : public juce::JUCEApplication {
public:
    App() {
        juce::PropertiesFile::Options options;
        options.applicationName = juce::CharPointer_UTF8(JucePlugin_Name);
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName = "";
        appProperties.setStorageParameters(options);
    }

    const juce::String getApplicationName() override { return juce::CharPointer_UTF8(JucePlugin_Name); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String&) override {}

    void initialise(const juce::String&) override {
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty()) {
            jassertfalse;
            return;
        }

        mainWindow = std::make_unique<juce::StandaloneFilterWindow>(
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
            appProperties.getUserSettings(), false);
        mainWindow->setUsingNativeTitleBar(true);
        mainWindow->setVisible(true);
    }

    void shutdown() override {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override {
        if (mainWindow != nullptr) {
            mainWindow->pluginHolder->savePluginState();
        }

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents()) {
            juce::Timer::callAfterDelay(100, [] {
                if (auto* app = juce::JUCEApplicationBase::getInstance()) {
                    app->systemRequestedQuit();
                }
            });
        } else {
            quit();
        }
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;
};

}  // namespace MarmiteStandalone

// Declared extern in juce_audio_plugin_client_Standalone.cpp when
// JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 — this is that definition.
juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new MarmiteStandalone::App(); }
