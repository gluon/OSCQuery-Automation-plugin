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

# OqaThirdParty.cmake — the one file that knows JUCE and juce_simpleweb exist. The shape
# is go.dot's cmake/WfgThirdParty.cmake (at 1c8aaa0a), cut down to what this project has.
#
# Targets defined here:
#
#   oqa::juce_config  INTERFACE  the JUCE and asio configuration switches, cxx_std_20 and
#                                the MSVC flags. Linked by EVERYTHING that compiles JUCE
#                                code, the plugin included, so the switches are stated once.
#   oqa::deps         INTERFACE  oqa::juce_config plus JUCE's headers and the definitions
#                                JUCE would otherwise attach to its module targets. What a
#                                translation unit needs to #include a JUCE header, and
#                                nothing that compiles code. lib/oscquery links this
#                                (Phase 2) so it can go into the plugin and into the tests
#                                without compiling JUCE twice in either.
#   oqa::warnings     INTERFACE  the warning policy; our targets only, never vendor sources.
#   oqa_juce          STATIC     the JUCE and juce_simpleweb module sources, compiled once,
#                                for every executable that is not a plugin (the tests, the
#                                mock device runner, later the conformance CLI). A plugin
#                                gets its own JUCE from juce_add_plugin instead.

# ---------------------------------------------------------------------------
# 1. JUCE
# ---------------------------------------------------------------------------
# Not JUCE_MODULES_ONLY, unlike go.dot: the plugin needs juce_add_plugin, which that option
# removes. The cost is the juceaide helper built once at configure.
add_subdirectory("${CMAKE_SOURCE_DIR}/ThirdParty/JUCE" juce)

# ---------------------------------------------------------------------------
# 2. juce_simpleweb — HTTP and WebSocket on one port, TLS compiled out
# ---------------------------------------------------------------------------
juce_add_module("${CMAKE_SOURCE_DIR}/ThirdParty/juce_simpleweb")

# Cleared outright. The module declares OpenSSL once per platform, in three spellings
# (ssl,crypto / libssl,libcrypto,z / libssl,libcrypto), and a build with
# SIMPLEWEB_SECURE_SUPPORTED=0 calls none of it. go.dot's reasoning, which found that a
# REMOVE_ITEM list misses Linux's spelling; the module declares nothing else here.
set_target_properties(juce_simpleweb PROPERTIES INTERFACE_LINK_LIBRARIES "")

# The module's .cpp files #include <JuceHeader.h>; this stub is on its interface only.
target_include_directories(juce_simpleweb INTERFACE "${CMAKE_SOURCE_DIR}/cmake/JuceHeaderStub")

# ---------------------------------------------------------------------------
# 3. oqa::juce_config — the switches, stated once
# ---------------------------------------------------------------------------
add_library(oqa_juce_config INTERFACE)
add_library(oqa::juce_config ALIAS oqa_juce_config)

target_compile_features(oqa_juce_config INTERFACE cxx_std_20)

target_compile_definitions(oqa_juce_config INTERFACE
    JUCE_WEB_BROWSER=0              # no webkit on Linux, no WebView2 on Windows
    JUCE_USE_CURL=0                 # no libcurl; HTTP is juce_simpleweb's or a raw socket
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0    # a modal loop inside a host is a hang

    # The VST3 class ID is derived differently under each value of this switch, so it is
    # part of the plugin's identity and never changes once a session has saved the plugin.
    # 0 as WFS-DIY: there is no VST2 version to replace.
    JUCE_VST3_CAN_REPLACE_VST2=0

    # juce_simpleweb without TLS. Upstream defaults it on, which compiles asio's OpenSSL
    # paths and fails to link in a build that ships no OpenSSL.
    SIMPLEWEB_SECURE_SUPPORTED=0

    # C++20 removed std::result_of. This asio still reaches for it unless this is defined,
    # and only MSVC defines it on its own (asio config.hpp); clang and GCC need it stated.
    ASIO_HAS_STD_INVOKE_RESULT=1)

if(MSVC)
    # /bigobj: JUCE attaches it to a few of its own modules only. /utf-8: our sources are
    # UTF-8 (em dashes, accented names) and MSVC reads them in the system codepage without it.
    target_compile_options(oqa_juce_config INTERFACE /bigobj /utf-8)
endif()

find_package(Threads REQUIRED)
target_link_libraries(oqa_juce_config INTERFACE Threads::Threads)

# ---------------------------------------------------------------------------
# 4. oqa::deps — JUCE's headers and the definitions its module targets carry
# ---------------------------------------------------------------------------
add_library(oqa_deps INTERFACE)
add_library(oqa::deps ALIAS oqa_deps)

target_link_libraries(oqa_deps INTERFACE oqa::juce_config juce::juce_recommended_config_flags)

# SYSTEM, so vendor warnings never meet -Werror on our code. ThirdParty itself is what
# makes <juce_simpleweb/juce_simpleweb.h> resolve.
target_include_directories(oqa_deps SYSTEM INTERFACE
    "${CMAKE_SOURCE_DIR}/ThirdParty/JUCE/modules"
    "${CMAKE_SOURCE_DIR}/ThirdParty")

# Restated here because oqa_juce links the modules PRIVATE, so their own usage requirements
# reach no consumer. JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED is mandatory: without it the first
# JUCE header any of our translation units includes is a hard #error.
target_compile_definitions(oqa_deps INTERFACE
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    $<$<CONFIG:Debug>:DEBUG=1>
    $<$<CONFIG:Debug>:_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:_NDEBUG=1>
    $<$<PLATFORM_ID:Linux>:LINUX=1>
    JUCE_MODULE_AVAILABLE_juce_core=1
    JUCE_MODULE_AVAILABLE_juce_events=1
    JUCE_MODULE_AVAILABLE_juce_cryptography=1
    JUCE_MODULE_AVAILABLE_juce_simpleweb=1
    OQA_VERSION_STRING="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# 5. oqa::warnings — our code only
# ---------------------------------------------------------------------------
add_library(oqa_warnings INTERFACE)
add_library(oqa::warnings ALIAS oqa_warnings)

target_link_libraries(oqa_warnings INTERFACE juce::juce_recommended_warning_flags)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC warns on the #pragma clang / #pragma warning lines JUCE puts in its headers.
    target_compile_options(oqa_warnings INTERFACE -Wno-unknown-pragmas)
endif()

if(OQA_WARNINGS_AS_ERRORS)
    target_compile_options(oqa_warnings INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/WX>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>)
endif()

# ---------------------------------------------------------------------------
# 6. oqa_juce — the module sources, compiled once, for non-plugin executables
# ---------------------------------------------------------------------------
# A STATIC library with no sources of its own: JUCE's module targets are INTERFACE
# libraries whose INTERFACE_SOURCES are the module .cpp files, so linking them PRIVATE is
# what compiles them, here and only here. PUBLIC would recompile them in every consumer.
add_library(oqa_juce STATIC)
set_target_properties(oqa_juce PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(oqa_juce
    PUBLIC  oqa::deps
    PRIVATE
        juce::juce_core           # String, File, var, JSON, sockets
        juce::juce_events         # ScopedJuceInitialiser_GUI, Thread, MessageManager
        juce::juce_cryptography   # juce_simpleweb includes it unconditionally
        juce_simpleweb)           # HTTP + WebSocket server and WebSocket client
