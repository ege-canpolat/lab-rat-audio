cmake_minimum_required(VERSION 3.22)

# Must come before project() to take effect. Runs on macOS 11+.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(ELSEWHERE VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)

include(../developer.cmake)
add_subdirectory(../JUCE ${CMAKE_BINARY_DIR}/JUCE)

juce_add_binary_data(ElsewhereAssets
    SOURCES
        Assets/LuckiestGuy-Regular.ttf
        Assets/Fredoka.ttf)

juce_add_plugin(Elsewhere
    COMPANY_NAME "${DEVELOPER_NAME}"
    BUNDLE_ID com.${DEVELOPER_ID}.elsewhere
    PLUGIN_MANUFACTURER_CODE ${DEVELOPER_CODE}
    PLUGIN_CODE Elsw
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Elsewhere"
    DESCRIPTION "Hear your mix from somewhere else"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Tools
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD TRUE)

set(ELSEWHERE_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Scenes.cpp
    Source/Cartoon.cpp
    Source/SceneArt.cpp)

target_sources(Elsewhere PRIVATE ${ELSEWHERE_SOURCES})

target_compile_definitions(Elsewhere
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Elsewhere
    PRIVATE
        ElsewhereAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline renderer: bounces a test mix through every scene and snapshots the UI.
juce_add_console_app(ElsewhereRender PRODUCT_NAME "ElsewhereRender")
target_sources(ElsewhereRender PRIVATE Tools/Render.cpp ${ELSEWHERE_SOURCES})
target_compile_definitions(ElsewhereRender PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ElsewhereRender
    PRIVATE
        ElsewhereAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Loads the built plugin through JUCE's VST3/AU hosting, like a DAW would.
juce_add_console_app(ElsewhereHostTest PRODUCT_NAME "ElsewhereHostTest")
target_sources(ElsewhereHostTest PRIVATE Tools/HostTest.cpp)
target_compile_definitions(ElsewhereHostTest PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1)
target_link_libraries(ElsewhereHostTest PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
