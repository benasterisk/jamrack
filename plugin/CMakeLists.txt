# MidPluck: guitar -> MIDI, VST3 audio effect (Windows), MIT. Plan: docs/plugin-plan.md.
#
# Targets:
#   midpluck_dsp        static library, JUCE-free: the C++ ports of the browser
#                       engines (dsp/: MONO = js/audio/guitar/tracker.js,
#                       POLY = js/audio/guitar/poly/*.js)
#   test_*              closed-form tests, one ctest each (dsp/tests)
#   dump_events         WAV -> note-event JSON, the oracle against the JS (tools/dump_*.cpp)
#   midpluck            the plugin (src/): "MidPluck", VST3 audio effect with MIDI
#                       output, OpenMindLab / Omlb / Mdpk; bundle MidPluck.vst3
#   jamrack_vst3_probe  command-line VST3 host that runs an installed plugin on a
#                       WAV and records its MIDI (tools/vst3_probe.cpp)
#   midpluck_ui_snapshot  renders the editor to PNG in every display state, no DAW
#                       needed (tools/ui_snapshot.cpp)
# The M0 prototypes (m0/, JAMRACK GTM Fx / Inst, codes Gtm0 / Gtm9) are no
# longer built; their sources stay in m0/ for reference.
#
#   cmake -S plugin -B plugin/build -G "Visual Studio 17 2022" -A x64
#   cmake --build plugin/build --config Release
#
# Options:
#   JAMRACK_COPY_AFTER_BUILD  ON  copy each .vst3 into JAMRACK_VST3_DIR after the build
#                                 (close Live and REAPER first: a loaded DLL is locked)
#   JAMRACK_VST3_DIR          D:/VST3
#   JAMRACK_DSP_ONLY          OFF build only the JUCE-free DSP library and tools (no JUCE download)
#   JAMRACK_ASAN              OFF MSVC /fsanitize=address (RelWithDebInfo, never Debug; rule 3.3-7)

cmake_minimum_required(VERSION 3.22)
project(midpluck VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(JAMRACK_COPY_AFTER_BUILD "Copy the built .vst3 bundles into JAMRACK_VST3_DIR" ON)
set(JAMRACK_VST3_DIR "D:/VST3" CACHE PATH "Folder the .vst3 bundles are copied to after the build")
option(JAMRACK_DSP_ONLY "Build only the JUCE-free DSP library and tools" OFF)
option(JAMRACK_ASAN "Build with AddressSanitizer (MSVC /fsanitize=address)" OFF)

# Static MSVC runtime: a downloaded .vst3 then loads without the VC++
# redistributable (otherwise Live skips it silently).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Numerical portability with the JavaScript reference (rule 3.3-9): no fused
# multiply-add, no fast-math, no instruction set beyond baseline x86-64.
if(MSVC)
  add_compile_options(/fp:precise /utf-8 /W3)
else()
  add_compile_options(-ffp-contract=off -fno-fast-math)
endif()

if(JAMRACK_ASAN)
  if(MSVC)
    foreach(flag_var CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELWITHDEBINFO)
      string(REPLACE "/RTC1" "" ${flag_var} "${${flag_var}}")
      string(REPLACE "/ZI" "/Zi" ${flag_var} "${${flag_var}}")
    endforeach()
    add_compile_options(/fsanitize=address /Zi)
    add_link_options(/INCREMENTAL:NO)
  else()
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

# Short git hash of the build, shown in the plugin editor so that every
# owner's report names the exact build it tested ("+" = uncommitted changes).
# Computed at EVERY build (not at configure time) by a custom target that
# rewrites a generated header only when the hash changes; only the editor and
# the processor's label function include it, so a new commit recompiles two
# files, not JUCE.
set(JAMRACK_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_target(jamrack_git_hash ALL
  COMMAND "${CMAKE_COMMAND}" "-DSRC=${CMAKE_CURRENT_SOURCE_DIR}" "-DOUT=${JAMRACK_GEN_DIR}/jamrack_git_hash.h"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/git_hash.cmake"
  BYPRODUCTS "${JAMRACK_GEN_DIR}/jamrack_git_hash.h"
  COMMENT "JAMRACK: git hash of the build"
  VERBATIM)

# ---- MidPluck engines, JUCE-free (plan section 2): dsp/ holds the C++ ports of
# js/audio/guitar/tracker.js (MONO) and js/audio/guitar/poly/*.js (POLY).
# Sources are globbed so that each engine adds files without touching this one.
file(GLOB_RECURSE MIDPLUCK_DSP_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/dsp/src/*.cpp")
add_library(midpluck_dsp STATIC ${MIDPLUCK_DSP_SOURCES})
target_include_directories(midpluck_dsp
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/dsp/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/dsp/src")
set_target_properties(midpluck_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Closed-form tests: one executable per dsp/tests/test_*.cpp (asserts, exit 1 on failure).
enable_testing()
file(GLOB MIDPLUCK_DSP_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/dsp/tests/test_*.cpp")
foreach(test_src ${MIDPLUCK_DSP_TESTS})
  get_filename_component(test_name "${test_src}" NAME_WE)
  add_executable(${test_name} "${test_src}")
  target_link_libraries(${test_name} PRIVATE midpluck_dsp)
  add_test(NAME ${test_name} COMMAND ${test_name})
  set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "ASAN_OPTIONS=halt_on_error=1")
endforeach()

# dump_events: WAV -> note-event JSON, same format as test/dump-events.mjs (plan 2, 4).
file(GLOB MIDPLUCK_DUMP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/dump_*.cpp")
add_executable(dump_events ${MIDPLUCK_DUMP_SOURCES})
target_link_libraries(dump_events PRIVATE midpluck_dsp)
set_target_properties(dump_events PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tools/Release"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tools/Debug"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/tools/RelWithDebInfo")

# Equivalence oracle on the synthetic corpus (plan 4.1): renders the recipes,
# runs the JS engines and dump_events on the same WAVs, diffs note for note.
find_program(NODE node)
if(NODE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../test/render-plucks.mjs")
  add_test(NAME oracle_synthetic
    COMMAND "${NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/../test/render-plucks.mjs"
            --check --dump "$<TARGET_FILE:dump_events>" --out "${CMAKE_BINARY_DIR}/oracle")
endif()

if(JAMRACK_DSP_ONLY)
  message(STATUS "JAMRACK_DSP_ONLY: JUCE not fetched, engines, tests and dump_events only")
  return()
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.15
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# The plugin (plan sections 2 and 3). An audio effect, not an instrument: it
# needs the audio input of the guitar track (owner decision, 8 October; the M0
# instrument variant was dropped). PLUGIN_CODE Mdpk is MidPluck's own: JUCE
# derives the VST3 class id from manufacturer + plugin codes, so it never
# collides with the M0 prototypes (Jamr Gtm0 / Gtm9) a host may still list.
juce_add_plugin(midpluck
  PRODUCT_NAME "MidPluck"
  COMPANY_NAME "OpenMindLab"
  PLUGIN_MANUFACTURER_CODE Omlb
  PLUGIN_CODE Mdpk
  VERSION ${PROJECT_VERSION}
  FORMATS VST3
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT TRUE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  VST3_CATEGORIES "Fx" "Tools"
  # the copy step is enabled below, after the licence files are in the bundle
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_COPY_DIR "${JAMRACK_VST3_DIR}")

# The fonts embedded in the editor are under the SIL OFL 1.1, whose condition 2
# asks every copy to carry the copyright notice and the licence: the three
# OFL.txt files go into the bundle (MidPluck.vst3/Contents/Resources), so any
# zip or Release of the bundle ships them. POST_BUILD commands run in the order
# they are added: this one before JUCE's copy into JAMRACK_VST3_DIR.
get_target_property(MIDPLUCK_VST3_BUNDLE midpluck_VST3 JUCE_PLUGIN_ARTEFACT_FILE)
add_custom_command(TARGET midpluck_VST3 POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${MIDPLUCK_VST3_BUNDLE}/Contents/Resources"
  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/unbounded/OFL.txt"
          "${MIDPLUCK_VST3_BUNDLE}/Contents/Resources/OFL-Unbounded.txt"
  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/barlowcondensed/OFL.txt"
          "${MIDPLUCK_VST3_BUNDLE}/Contents/Resources/OFL-BarlowCondensed.txt"
  COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/splinesansmono/OFL.txt"
          "${MIDPLUCK_VST3_BUNDLE}/Contents/Resources/OFL-SplineSansMono.txt"
  COMMENT "MidPluck: font licences into the bundle"
  VERBATIM)
if(JAMRACK_COPY_AFTER_BUILD)
  juce_enable_copy_plugin_step(midpluck)
endif()

# The editor (src/PluginEditor.cpp + src/ui/): the JAMRACK look, with the web
# app's three typefaces embedded as binary data (resources/fonts, SIL OFL 1.1,
# static Latin subsets: about 270 kB, see resources/fonts/README.md).
juce_add_binary_data(midpluck_ui_assets
  HEADER_NAME "MidPluckAssets.h"
  NAMESPACE MidPluckAssets
  SOURCES
    resources/fonts/unbounded/Unbounded-Medium.ttf
    resources/fonts/unbounded/Unbounded-ExtraBold.ttf
    resources/fonts/barlowcondensed/BarlowCondensed-Regular.ttf
    resources/fonts/barlowcondensed/BarlowCondensed-Medium.ttf
    resources/fonts/barlowcondensed/BarlowCondensed-SemiBold.ttf
    resources/fonts/splinesansmono/SplineSansMono-Regular.ttf
    resources/fonts/splinesansmono/SplineSansMono-Medium.ttf)
set_target_properties(midpluck_ui_assets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(MIDPLUCK_EDITOR_SOURCES
  src/PluginEditor.cpp
  src/ui/JamrackLookAndFeel.cpp
  src/ui/Widgets.cpp
  src/ui/EngineDisplay.cpp)

target_sources(midpluck PRIVATE
  src/PluginProcessor.cpp
  ${MIDPLUCK_EDITOR_SOURCES})

target_compile_definitions(midpluck PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

add_dependencies(midpluck jamrack_git_hash)
target_include_directories(midpluck PRIVATE "${JAMRACK_GEN_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(midpluck PRIVATE
  midpluck_dsp
  midpluck_ui_assets
  juce::juce_audio_utils
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags)

# vst3_probe: a command-line VST3 host that loads an installed .vst3, feeds
# it a WAV and records the MIDI it emits (plugin/tools/vst3_probe.cpp). Used
# to check the plugins headlessly, without a DAW.
juce_add_console_app(jamrack_vst3_probe PRODUCT_NAME "vst3_probe")
target_sources(jamrack_vst3_probe PRIVATE tools/vst3_probe.cpp)
target_compile_definitions(jamrack_vst3_probe PRIVATE
  JUCE_PLUGINHOST_VST3=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0)
target_link_libraries(jamrack_vst3_probe PRIVATE
  juce::juce_audio_utils
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

# midpluck_ui_snapshot: renders the editor to PNG without a DAW, for every
# display state (MONO idle / playing, POLY, POLY loading, ECO, BYPASSED,
# warnings) at 100 % and 150 % scaling (plugin/tools/ui_snapshot.cpp). It
# compiles the plugin's own processor and editor sources with the plugin's
# JucePlugin_Name and version, so what it renders is what the host shows.
#   midpluck_ui_snapshot <output folder>
juce_add_console_app(midpluck_ui_snapshot PRODUCT_NAME "midpluck_ui_snapshot")
target_sources(midpluck_ui_snapshot PRIVATE
  tools/ui_snapshot.cpp
  src/PluginProcessor.cpp
  ${MIDPLUCK_EDITOR_SOURCES})
target_compile_definitions(midpluck_ui_snapshot PRIVATE
  JucePlugin_Name="MidPluck"
  JucePlugin_VersionString="${PROJECT_VERSION}"
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0)
add_dependencies(midpluck_ui_snapshot jamrack_git_hash)
target_include_directories(midpluck_ui_snapshot PRIVATE "${JAMRACK_GEN_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(midpluck_ui_snapshot PRIVATE
  midpluck_dsp
  midpluck_ui_assets
  juce::juce_audio_utils
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)
