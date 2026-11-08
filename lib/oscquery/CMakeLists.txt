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

# The OSCQuery client library (devplan Phase 2). One STATIC target, oqa::oscquery, that
# links oqa::deps only: JUCE's headers and switches, never a JUCE module's sources. The
# plugin (which compiles its own JUCE through juce_add_plugin) and the tests and tools
# (which get theirs from oqa_juce) both link it without JUCE compiling twice in either.
#
# Working name, decided by the author on 2026-11-08: namespace oqa::oscquery, includes as
# <oscquery/...>. Phase 8 renames it when the library becomes a repository of its own.
#
# Files copied from go.dot keep go.dot's header and carry an Origin: line
# (scripts/check-headers.py).

add_library(oqa_oscquery STATIC
    # OSC 1.1 on the wire: value type, address rules, codec. Pure: no socket, no JUCE.
    oscquery/osc/Value.h
    oscquery/osc/Value.cpp
    oscquery/osc/Address.h
    oscquery/osc/Address.cpp
    oscquery/osc/Codec.h
    oscquery/osc/Codec.cpp
    oscquery/osc/Framing.h          # SLIP and length-prefix framing for a stream
    oscquery/osc/Framing.cpp
    oscquery/osc/Bundler.h          # messages into bundles under the MTU
    oscquery/osc/Bundler.cpp

    # One HTTP GET over a raw socket (juce::URL rewrites `?HOST_INFO`); the UDP
    # endpoint that names each datagram's sender; the TCP sender.
    oscquery/net/HttpGet.h
    oscquery/net/HttpGet.cpp
    oscquery/net/SocketBuffers.h
    oscquery/net/SocketBuffers.cpp
    oscquery/net/UdpEndpoint.h
    oscquery/net/UdpEndpoint.cpp
    oscquery/net/TcpSender.h
    oscquery/net/TcpSender.cpp

    # HOST_INFO as the Core profile reads it, and the tree model.
    oscquery/HostInfo.h
    oscquery/HostInfo.cpp
    oscquery/tree/Node.h
    oscquery/tree/Tree.h
    oscquery/tree/Tree.cpp

    # One device session: HOST_INFO, tree, the websocket stream, reconnect.
    oscquery/Device.h
    oscquery/Device.cpp

    # DNS-SD: the multicast DNS codec (pure), the browser and announcer sharing
    # port 5353 with the system's responder, the per-interface group join.
    oscquery/mdns/Mdns.h
    oscquery/mdns/Mdns.cpp
    oscquery/mdns/MulticastJoin.h
    oscquery/mdns/MulticastJoin.cpp
    oscquery/mdns/Discovery.h
    oscquery/mdns/Discovery.cpp

    # Found and typed devices, each probed with HOST_INFO before it is listed.
    oscquery/Finder.h
    oscquery/Finder.cpp

    # The per-user recents store, a juce::PropertiesFile at the standard location.
    oscquery/Recents.h
    oscquery/Recents.cpp)
add_library(oqa::oscquery ALIAS oqa_oscquery)

set_target_properties(oqa_oscquery PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(oqa_oscquery PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(oqa_oscquery PUBLIC oqa::deps PRIVATE oqa::warnings)

if(OQA_BUILD_TESTS)
    add_subdirectory(tests)
endif()

add_subdirectory(examples)
