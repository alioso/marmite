cmake_minimum_required(VERSION 3.24)
project(Marmite LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Universal binary: Apple Silicon + Intel in one build.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
# Oldest macOS an Intel Mac running this is likely to have; also caps what
# API/SDK surface we can rely on.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")

add_subdirectory(JUCE)

# Standalone + AU + VST3 from one source tree — deliberately excludes
# AUv3/Unity/LV2/VST2 (not targeted). PLUGIN_MANUFACTURER_CODE/PLUGIN_CODE
# are permanent once shipped (hosts key cached plugin identity off them),
# so chosen deliberately now rather than left at a JUCE default.
juce_add_plugin(MarmiteApp
    PRODUCT_NAME "Marmite"
    VERSION "0.1.0"
    COMPANY_NAME "Alban Bailly"
    BUNDLE_ID "com.albanbailly.marmite"
    PLUGIN_MANUFACTURER_CODE Abal
    PLUGIN_CODE Mrmt
    FORMATS Standalone AU VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Drum
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/AppIcon.png"
)

juce_generate_juce_header(MarmiteApp)

juce_add_binary_data(MarmiteAssets
    SOURCES
        Resources/AppIcon.png
)

target_sources(MarmiteApp
    PRIVATE
        Sources/MarmiteApp/Main.cpp
)

target_include_directories(MarmiteApp
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Sources/MarmiteApp
)

target_compile_definitions(MarmiteApp
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        # No VST2 version of this plugin has ever been (or will be)
        # released, so there's no VST2/VST3 parameter-automation
        # migration to preserve — this only silences JUCE's build-time
        # warning about that concern, not a real compatibility issue here.
        JUCE_VST3_CAN_REPLACE_VST2=0
        # See Sources/MarmiteApp/StandaloneApp.h — restores a native
        # macOS title bar (rounded corners, traffic lights) that JUCE's
        # default Standalone window doesn't use.
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:MarmiteApp,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:MarmiteApp,JUCE_VERSION>")

target_link_libraries(MarmiteApp
    PRIVATE
        MarmiteAssets
        juce::juce_gui_extra
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Engine classes are plain C++ (no JUCE dependency) so their tests link
# as bare executables with no JUCE linkage — cheap to build, no app
# bundle/audio device needed to run them.
function(add_marmite_test name)
    add_executable(${name} Tests/${name}.cpp)
    target_include_directories(${name}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Sources/MarmiteApp
    )
endfunction()

add_marmite_test(DrumVoiceModelTest)
add_marmite_test(SampleVoicePoolTest)
add_marmite_test(PatternCloudTest)
add_marmite_test(DrumEvolutionEngineTest)
add_marmite_test(ProceduralKitTest)
add_marmite_test(DelayLineTest)
add_marmite_test(MidiBindingManagerTest)
add_marmite_test(MidiPresetStoreTest)
add_marmite_test(ScenePresetStoreTest)
add_marmite_test(SpaceEvolverTest)
