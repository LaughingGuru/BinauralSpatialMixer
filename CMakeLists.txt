cmake_minimum_required(VERSION 3.22)

project(BinauralSpatialMixer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(JUCE_ROOT "" CACHE PATH "Path to a local JUCE checkout. If empty, CMake checks external/JUCE, /Applications/JUCE, then FetchContent.")

if(JUCE_ROOT AND EXISTS "${JUCE_ROOT}/CMakeLists.txt")
    add_subdirectory("${JUCE_ROOT}" "${CMAKE_BINARY_DIR}/JUCE")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" "${CMAKE_BINARY_DIR}/JUCE")
elseif(APPLE AND EXISTS "/Applications/JUCE/CMakeLists.txt")
    add_subdirectory("/Applications/JUCE" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG master)
    FetchContent_MakeAvailable(JUCE)
endif()

find_package(ZLIB REQUIRED)

set(MYSOFA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/BinauralMix/libmysofa")

add_library(binaural_mysofa STATIC
    "${MYSOFA_ROOT}/src/resampler/speex_resampler.c"
    "${MYSOFA_ROOT}/src/hdf/btree.c"
    "${MYSOFA_ROOT}/src/hdf/dataobject.c"
    "${MYSOFA_ROOT}/src/hdf/fractalhead.c"
    "${MYSOFA_ROOT}/src/hdf/gcol.c"
    "${MYSOFA_ROOT}/src/hdf/gunzip.c"
    "${MYSOFA_ROOT}/src/hdf/superblock.c"
    "${MYSOFA_ROOT}/src/hrtf/cache.c"
    "${MYSOFA_ROOT}/src/hrtf/check.c"
    "${MYSOFA_ROOT}/src/hrtf/easy.c"
    "${MYSOFA_ROOT}/src/hrtf/interpolate.c"
    "${MYSOFA_ROOT}/src/hrtf/kdtree.c"
    "${MYSOFA_ROOT}/src/hrtf/neighbors.c"
    "${MYSOFA_ROOT}/src/hrtf/minphase.c"
    "${MYSOFA_ROOT}/src/hrtf/loudness.c"
    "${MYSOFA_ROOT}/src/hrtf/lookup.c"
    "${MYSOFA_ROOT}/src/hrtf/reader.c"
    "${MYSOFA_ROOT}/src/hrtf/resample.c"
    "${MYSOFA_ROOT}/src/hrtf/spherical.c"
    "${MYSOFA_ROOT}/src/hrtf/tools.c")

target_include_directories(binaural_mysofa
    PUBLIC
        "${MYSOFA_ROOT}/src"
        "${MYSOFA_ROOT}/src/hdf"
        "${MYSOFA_ROOT}/src/hrtf"
        "${MYSOFA_ROOT}/src/resampler")

target_link_libraries(binaural_mysofa PUBLIC ZLIB::ZLIB)
set_target_properties(binaural_mysofa PROPERTIES POSITION_INDEPENDENT_CODE ON)

function(binaural_apply_common_plugin_settings target)
    target_compile_definitions(${target}
        PUBLIC
            JUCE_STRICT_REFCOUNTEDPOINTER=1
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_USE_CURL=0)

    target_link_libraries(${target}
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

juce_add_plugin(BinuaralMix
    VERSION 1.0.0
    COMPANY_NAME "yourcompany"
    COMPANY_WEBSITE "www.yourcompany.com"
    BUNDLE_ID "com.yourcompany.BinuaralMix"
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE Kkp2
    FORMATS AU VST3
    PRODUCT_NAME "BinuaralMix"
    PLUGIN_NAME "BinuaralMix"
    DESCRIPTION "BinuaralMix"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Spatial Stereo Surround)

juce_generate_juce_header(BinuaralMix)

target_sources(BinuaralMix
    PRIVATE
        BinauralMix/Source/PluginProcessor.cpp
        BinauralMix/Source/PluginProcessor.h
        BinauralMix/Source/PluginEditor.cpp
        BinauralMix/Source/PluginEditor.h
        BinauralMix/Source/SharedObjectAudioTransport.h
        BinauralMix/JuceLibraryCode/BinaryData.cpp
        BinauralMix/JuceLibraryCode/BinaryData.h)

target_include_directories(BinuaralMix
    PRIVATE
        BinauralMix/Source
        "${MYSOFA_ROOT}/src"
        "${MYSOFA_ROOT}/src/hdf"
        "${MYSOFA_ROOT}/src/hrtf"
        "${MYSOFA_ROOT}/src/resampler")

target_link_libraries(BinuaralMix
    PRIVATE
        binaural_mysofa
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_javascript)

binaural_apply_common_plugin_settings(BinuaralMix)

target_compile_definitions(BinuaralMix
    PUBLIC
        JUCE_WEB_BROWSER=1
        $<$<PLATFORM_ID:Windows>:JUCE_USE_WIN_WEBVIEW2=1>)

juce_add_plugin(BinuaralSender
    VERSION 1.0.0
    COMPANY_NAME "yourcompany"
    COMPANY_WEBSITE "www.yourcompany.com"
    BUNDLE_ID "com.yourcompany.BinuaralSender"
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE Bnsn
    FORMATS AU VST3
    PRODUCT_NAME "BinuaralSender"
    PLUGIN_NAME "BinuaralSender"
    DESCRIPTION "BinuaralSender"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx)

juce_generate_juce_header(BinuaralSender)

target_sources(BinuaralSender
    PRIVATE
        BinauralSender/Source/PluginProcessor.cpp
        BinauralSender/Source/PluginProcessor.h
        BinauralSender/Source/PluginEditor.cpp
        BinauralSender/Source/PluginEditor.h
        BinauralSender/Source/SharedObjectAudioTransport.h)

target_include_directories(BinuaralSender
    PRIVATE
        BinauralSender/Source)

target_link_libraries(BinuaralSender
    PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_extra)

binaural_apply_common_plugin_settings(BinuaralSender)

target_compile_definitions(BinuaralSender
    PUBLIC
        JUCE_WEB_BROWSER=0)
