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

# THE CLAP SHIM (devplan Phase 5b): one module, "OSCQuery Automation.clap", whose factory lists
# the five slot-count variants. Written against the CLAP C API with clap-helpers; neither
# clap-juce-extensions nor clap-wrapper (decided 2026-11-04).
#
# PLUGIN IDENTITY (decided 2026-11-09). The CLAP ids are the bundle ids the JUCE shim already
# uses, io.github.pob31.oqa.<code> for Q008 Q016 Q032 Q064 Q128 (Descriptors.h). Once a
# session has saved one, it never changes, or the session no longer finds the plugin. The
# macOS bundle's own id is a sixth string, io.github.pob31.oqa.clap: one bundle, five plugins.
#
# The module compiles its own JUCE (juce_gui_basics for the editor, the modules the core and
# the client library need), as the JUCE shim does through juce_add_plugin. The core's and the
# library's object files go into the module's own sources for the GNU ld reason the JUCE
# shim's CMakeLists records.

add_library(oqa_clap MODULE
    Descriptors.h
    Entry.cpp
    ClapPlugin.h
    ClapPlugin.cpp
    ClapParams.cpp
    ClapRecord.cpp
    ClapGui.cpp
    LinuxEventPump.h
    LinuxEventPump.cpp
    StatusExtension.h
    $<TARGET_OBJECTS:oqa_core>
    $<TARGET_OBJECTS:oqa_oscquery>)

set_target_properties(oqa_clap PROPERTIES
    OUTPUT_NAME "OSCQuery Automation"
    PREFIX ""
    SUFFIX ".clap"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/oqa_clap_artefacts/$<CONFIG>")

if(APPLE)
    # A CLAP on macOS is a bundle: Contents/Info.plist and the module in Contents/MacOS/.
    set_target_properties(oqa_clap PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.pob31.oqa.clap"
        MACOSX_BUNDLE_BUNDLE_NAME "OSCQuery Automation"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()

target_compile_definitions(oqa_clap PRIVATE
    JUCE_DISPLAY_SPLASH_SCREEN=0      # GPLv3 build; nothing of JUCE's is shown
    JUCE_STANDALONE_APPLICATION=0)

target_link_libraries(oqa_clap
    PRIVATE
        clap
        clap-helpers
        oqa::core
        oqa::oscquery
        oqa::editor
        juce::juce_gui_basics
        juce::juce_cryptography
        juce::juce_data_structures
        juce_simpleweb
        oqa::juce_config
        oqa::warnings
        juce::juce_recommended_config_flags)

# The shared core library beside the module after every build, where the loader looks first,
# as the JUCE shim does for its bundles. On Windows and Linux the module is one file; on macOS
# the bundle's folder is where it goes.
add_dependencies(oqa_clap oqa_core_shared)
if(APPLE)
    add_custom_command(TARGET oqa_clap POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:oqa_core_shared>"
                "$<TARGET_BUNDLE_DIR:oqa_clap>/../"
        # Phase 1's lesson: a bundle a host is meant to load needs a signature over the whole
        # bundle, ad hoc until Phase 7 signs for real.
        COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:oqa_clap>"
        COMMENT "Copying the shared core library beside the CLAP bundle and sealing it")
else()
    add_custom_command(TARGET oqa_clap POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:oqa_core_shared>"
                "$<TARGET_FILE_DIR:oqa_clap>/"
        COMMENT "Copying the shared core library beside the CLAP module")
endif()
