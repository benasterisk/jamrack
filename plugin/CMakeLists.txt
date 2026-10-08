# JAMRACK Guitar -> MIDI, VST3 plugin (Windows). Plan: docs/plugin-plan.md.
#
# M0 (this file's current state): a throw-away prototype that measures what
# Ableton Live 12 accepts. Two targets built from the same sources, each with
# its own PLUGIN_CODE so that Live never confuses them (and never confuses
# either with the real plugin, Gtm1, from M2 on):
#   JAMRACK GTM Fx    PLUGIN_CODE Gtm0  audio effect (passes audio, emits MIDI)
#   JAMRACK GTM Inst  PLUGIN_CODE Gtm9  instrument with one auxiliary
#                                       "Sidechain" input (audio arrives by side-chain)
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
project(jamrack-plugin VERSION 0.0.1 LANGUAGES C CXX)

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

# One prototype target. IS_INST selects the instrument variant (auxiliary
# side-chain input, no main input) at compile time.
function(jamrack_add_m0_prototype target product code is_synth categories)
  juce_add_plugin(${target}
    PRODUCT_NAME "${product}"
    COMPANY_NAME "JAMRACK"
    PLUGIN_MANUFACTURER_CODE Jamr
    PLUGIN_CODE ${code}
    VERSION ${PROJECT_VERSION}
    FORMATS VST3
    IS_SYNTH ${is_synth}
    # Live refuses a VST3 instrument without an event input bus ("plugin has
    # instrument category, but no valid event input bus"): the Inst gets one
    NEEDS_MIDI_INPUT ${is_synth}
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES ${categories}
    COPY_PLUGIN_AFTER_BUILD ${JAMRACK_COPY_AFTER_BUILD}
    VST3_COPY_DIR "${JAMRACK_VST3_DIR}")

  target_sources(${target} PRIVATE
    m0/PluginProcessor.cpp
    m0/PluginEditor.cpp)

  target_compile_definitions(${target} PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JAMRACK_IS_INST=$<IF:$<BOOL:${is_synth}>,1,0>)

  add_dependencies(${target} jamrack_git_hash)
  target_include_directories(${target} PRIVATE "${JAMRACK_GEN_DIR}")

  target_link_libraries(${target} PRIVATE
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
endfunction()

jamrack_add_m0_prototype(jamrack_gtm_fx   "JAMRACK GTM Fx"   Gtm0 FALSE "Fx;Tools")
jamrack_add_m0_prototype(jamrack_gtm_inst "JAMRACK GTM Inst" Gtm9 TRUE  "Instrument;Tools")

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
