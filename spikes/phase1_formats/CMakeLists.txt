# This file is part of the OSCQuery Automation Plugin —
# https://github.com/pob31/OSCQuery-Automation-plugin
#
# Copyright (C) 2026 Pierre-Olivier Boulant, Julien Bayle
#
# The OSCQuery Automation Plugin is free software: you can redistribute it
# and/or modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation, either version 3 of the License,
# or (at your option) any later version. It is distributed in the hope that it
# will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
# of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
# Public License (LICENSE, at the repository root) for more details.
#
# SPDX-License-Identifier: GPL-3.0-or-later

# SPIKE, PHASE 1 (devplan): throwaway. Never migrates into src/; deleted once
# docs/spikes/phase1-formats.md is written.
#
# A project of its own, configured on its own:
#
#   cmake -S spikes/phase1_formats -B build/spike-phase1
#   cmake --build build/spike-phase1 --config Release
#
# It builds against a COPY of the pinned JUCE, extracted into the build folder and
# patched there with juce-au-ishidden.patch (AudioProcessorParameter::isHidden(), read by
# the AU wrapper), so the main tree's ThirdParty/JUCE is never touched. Decided by the
# author 2026-11-04: measure IsHidden rather than record that JUCE cannot set it.

cmake_minimum_required(VERSION 3.22)

if(CMAKE_HOST_APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(oqa_spike_phase1 VERSION 0.0.1 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# --- The patched JUCE: the pinned commit, archived out of the submodule, patched -------
set(_repo "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(_juce "${CMAKE_BINARY_DIR}/JUCE-patched")
set(_patch "${CMAKE_CURRENT_SOURCE_DIR}/juce-au-ishidden.patch")

if(NOT EXISTS "${_juce}/.patched")
    find_package(Git REQUIRED)
    file(REMOVE_RECURSE "${_juce}")
    file(MAKE_DIRECTORY "${_juce}")

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${_repo}/ThirdParty/JUCE" archive --format=tar
                -o "${CMAKE_BINARY_DIR}/juce.tar" HEAD
        RESULT_VARIABLE _rc)
    if(_rc)
        message(FATAL_ERROR "spike: git archive of ThirdParty/JUCE failed (is the submodule checked out?)")
    endif()

    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xf "${CMAKE_BINARY_DIR}/juce.tar"
                    WORKING_DIRECTORY "${_juce}" RESULT_VARIABLE _rc)
    if(_rc)
        message(FATAL_ERROR "spike: extracting JUCE failed")
    endif()
    file(REMOVE "${CMAKE_BINARY_DIR}/juce.tar")

    # A repository of its own, so `git apply` patches this tree and not the enclosing one.
    execute_process(COMMAND "${GIT_EXECUTABLE}" init -q WORKING_DIRECTORY "${_juce}")
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${_patch}"
                    WORKING_DIRECTORY "${_juce}" RESULT_VARIABLE _rc)
    if(_rc)
        message(FATAL_ERROR "spike: juce-au-ishidden.patch does not apply to the pinned JUCE")
    endif()

    file(WRITE "${_juce}/.patched" "juce-au-ishidden.patch\n")
    message(STATUS "spike: JUCE extracted and patched in ${_juce}")
endif()

add_subdirectory("${_juce}" juce)

# --- The plugin ------------------------------------------------------------------------
# Its own plugin code, Qsp8, so it never collides with a real slot-count variant.
set(_formats VST3)
if(APPLE)
    list(APPEND _formats AU)
endif()

juce_add_plugin(oqa_spike8
    COMPANY_NAME                "Pix et Bel"
    PLUGIN_MANUFACTURER_CODE    "PxBl"
    PLUGIN_CODE                 "Qsp8"
    BUNDLE_ID                   "io.github.pob31.oqa.spike8"
    PRODUCT_NAME                "OQA Spike 8"
    DESCRIPTION                 "Phase 1 format bench, throwaway"
    VERSION                     "${PROJECT_VERSION}"
    FORMATS                     ${_formats}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             "Fx" "Network"
    AU_MAIN_TYPE                "kAudioUnitType_Effect")

target_sources(oqa_spike8 PRIVATE SpikeProcessor.h SpikeProcessor.cpp)

target_compile_definitions(oqa_spike8 PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(oqa_spike8 PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(oqa_spike8 PRIVATE /utf-8)
endif()
