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

# The plugin's core (devplan Phase 3): slots, scaling, the state chunk, the registry and
# the send path. One STATIC target, oqa::core, that the format shims and the editor link;
# no JUCE GUI module, no juce::Timer, no MessageManager, so a headless test drives all of
# it. Includes as <core/...>. The registry's shared-library build (3c) wraps this same
# target behind its C ABI.

add_library(oqa_core STATIC
    # 3a: host 0–1 <-> device units, and the PRD's slot table as a struct.
    Scaling.h
    Scaling.cpp
    Slot.h
    Slot.cpp

    # 3b: the state chunk, JSON, versioned; every version ever shipped stays loadable.
    StateChunk.h
    StateChunk.cpp

    # 3c: the lipogram's regions and counters; the ring from the audio threads to a
    # device's network thread; time tags from the clock; the per-slot rate limiter.
    rt/RtCheck.h
    registry/ChangeRing.h
    send/TimeTags.h
    send/TimeTags.cpp
    send/RateLimiter.h
    send/RateLimiter.cpp)
add_library(oqa::core ALIAS oqa_core)

set_target_properties(oqa_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(oqa_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(oqa_core PUBLIC oqa::oscquery PRIVATE oqa::warnings)
