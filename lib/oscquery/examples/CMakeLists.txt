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

# The library's example, and Phase 2's done-when: oscquery-dump prints a device's
# HOST_INFO and tree, browses DNS-SD, and follows one path's values. A plain
# executable: JUCE from oqa_juce, compiled once.

add_executable(oscquery-dump oscquery-dump/main.cpp)
target_link_libraries(oscquery-dump PRIVATE oqa::oscquery oqa_juce oqa::warnings)
