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
find_package(Git QUIET)
set(JAMRACK_GIT_HASH "nogit")
if(GIT_FOUND)
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
                  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                  OUTPUT_VARIABLE JAMRACK_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
                  RESULT_VARIABLE git_result ERROR_QUIET)
  execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
                  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                  OUTPUT_VARIABLE git_dirty OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT git_result EQUAL 0)
    set(JAMRACK_GIT_HASH "nogit")
  elseif(git_dirty)
    string(APPEND JAMRACK_GIT_HASH "+")
  endif()
  # Re-run this configure step (and so refresh the hash) after every commit,
  # checkout or staged change, so that a later `cmake --build` never ships a
  # build labelled with an old hash.
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --absolute-git-dir
                  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                  OUTPUT_VARIABLE git_dir OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  foreach(f HEAD index logs/HEAD)
    if(EXISTS "${git_dir}/${f}")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_dir}/${f}")
    endif()
  endforeach()
endif()
message(STATUS "JAMRACK plugin ${PROJECT_VERSION} (${JAMRACK_GIT_HASH})")

if(JAMRACK_DSP_ONLY)
  # The JUCE-free DSP library (dsp/) arrives in M1; nothing to build in M0.
  message(STATUS "JAMRACK_DSP_ONLY: JUCE not fetched, no plugin target")
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
    NEEDS_MIDI_INPUT FALSE
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
    JAMRACK_IS_INST=$<IF:$<BOOL:${is_synth}>,1,0>
    JAMRACK_GIT_HASH="${JAMRACK_GIT_HASH}")

  target_link_libraries(${target} PRIVATE
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
endfunction()

jamrack_add_m0_prototype(jamrack_gtm_fx   "JAMRACK GTM Fx"   Gtm0 FALSE "Fx;Tools")
jamrack_add_m0_prototype(jamrack_gtm_inst "JAMRACK GTM Inst" Gtm9 TRUE  "Instrument;Tools")
