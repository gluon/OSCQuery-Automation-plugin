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

# THE EDITOR (devplan Phase 3e, first cut): a client of the core, nothing more. An
# INTERFACE target whose sources compile inside whichever plugin links it, with that
# plugin's own JUCE GUI modules; the core and the client library never see a GUI module.

add_library(oqa_editor INTERFACE)
add_library(oqa::editor ALIAS oqa_editor)

target_sources(oqa_editor INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/EditorRoot.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/EditorRoot.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DeviceList.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/DeviceList.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TreePane.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/TreePane.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SlotTable.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/SlotTable.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/StatusBar.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/StatusBar.cpp")

target_include_directories(oqa_editor INTERFACE "${CMAKE_SOURCE_DIR}/src")
