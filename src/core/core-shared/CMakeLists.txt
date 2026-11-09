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

# THE SHARED CORE LIBRARY (devplan Phase 3, after WFS-DIY's Bridge): the registry every
# plugin binary in a process shares, so one host process has one OSCQuery client, one
# socket and one network thread per device whatever mix of binaries it loads. One export,
# oqa_core_api() (registry/RegistryApi.h); everything else hidden, so the JUCE inside it
# never meets the JUCE inside a plugin. It carries the core, the client library and the
# JUCE modules they need, compiled once more for this target as oqa_juce.
#
# Found by registry/RegistryLoader next to the plugin bundles (walking up from the plugin's
# own module), else at the per-OS fixed path, else the plugin runs on its own copy and says
# so. Phase 7's installers place it; a dev build copies it beside the plugin artefact
# (src/shims/juce/CMakeLists.txt).

add_library(oqa_core_shared SHARED
    CoreExport.cpp)

set_target_properties(oqa_core_shared PROPERTIES
    OUTPUT_NAME "OSCQuery-Automation-Core"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

if(APPLE)
    set_target_properties(oqa_core_shared PROPERTIES INSTALL_NAME_DIR "@loader_path")
endif()

# ONE EXPORT, SAID TO THE LINKER TOO (Phase 5b): hidden visibility is the compiler's word, and
# the macOS bench found JUCE's constexpr singletons (juce::var's types) coalesced across the
# test executable and the CLAP module all the same, a String of one copy freed by the other.
# An export list on Apple and a version script with -Bsymbolic on GNU ld bind everything but
# the one entry inside this image, whatever the compiler emitted.
if(APPLE)
    target_link_options(oqa_core_shared PRIVATE "LINKER:-exported_symbol,_oqa_core_api")
elseif(NOT WIN32)
    target_link_options(oqa_core_shared PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map"
        "LINKER:-Bsymbolic")
    set_property(TARGET oqa_core_shared APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
endif()

# oqa::oscquery named explicitly for the same archive-order reason as tests/core.
target_link_libraries(oqa_core_shared PRIVATE oqa::core oqa::oscquery oqa_juce oqa::warnings)
