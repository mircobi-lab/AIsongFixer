cmake_minimum_required(VERSION 3.22)

# MBStudio91 AI Song Fixer — pulizia dei brani generati con l'AI (Suno, Udio): VST3 / AU / Standalone
project(MBStudio91AISongFixer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Binario universale: Mac Intel (MacBook Pro 2019, Mac mini) e Apple Silicon
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    endif()
endif()

# JUCE: cartella locale (-DJUCE_DIR=...) oppure download automatico della versione fissata
set(JUCE_DIR "" CACHE PATH "Percorso di una copia locale di JUCE")
if(JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(ASF_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND ASF_FORMATS AU)
endif()

juce_add_plugin(MBStudio91AISongFixer
    COMPANY_NAME "MBStudio91"
    COMPANY_WEBSITE "https://mbstudio91.fun"
    BUNDLE_ID "fun.mbstudio91.aisongfixer"
    PRODUCT_NAME "MBStudio91 AI Song Fixer"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Mbst
    PLUGIN_CODE Mbaf
    FORMATS ${ASF_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Restoration Mastering
    AU_MAIN_TYPE kAudioUnitType_Effect)

include(MBStudioUI/MBStudioUI.cmake)
mbstudio_ui_add(MBStudio91AISongFixer)

target_sources(MBStudio91AISongFixer PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SpectrumView.cpp
    Source/Dsp/SpectralCleaner.cpp)

target_compile_definitions(MBStudio91AISongFixer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_JACK=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(MBStudio91AISongFixer
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Test offline (null test, latenza, ceiling): cmake --build build --target FixerTests
option(FIXER_TESTS "Compila i test offline" ON)
if(FIXER_TESTS)
    juce_add_console_app(FixerTests PRODUCT_NAME "FixerTests")
    target_sources(FixerTests PRIVATE tests/OfflineTest.cpp
        Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/SpectrumView.cpp Source/Dsp/SpectralCleaner.cpp)
    target_include_directories(FixerTests PRIVATE Source)
    mbstudio_ui_add(FixerTests)
    target_compile_definitions(FixerTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(FixerTests PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
