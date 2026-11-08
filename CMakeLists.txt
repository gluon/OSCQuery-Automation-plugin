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

# The root only sets things up, in order. Targets live in cmake/OqaThirdParty.cmake
# (everything vendor), lib/oscquery/ (the client library and its tests), src/ (the plugin)
# and tests/ (the suite and the mock device).
#
# Quick start:   cmake --preset vs        && cmake --build --preset vs-debug     (Windows)
#                cmake --preset dev       && cmake --build --preset dev-debug    (anywhere, Ninja)
#                ctest --preset vs-debug  (or ci-linux / ci-macos)

# JUCE's own floor, and Ubuntu 22.04's apt version.
cmake_minimum_required(VERSION 3.22)

# Before project(), or the first compiler probe has already picked a target. 11.0 is the
# first macOS with arm64 at all, so a universal binary could not go lower anyway. A cache
# STRING, so a preset or a -D can still override it.
if(CMAKE_HOST_APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(oqa VERSION 0.0.1 LANGUAGES C CXX)

# JUCE's Apple sources are Objective-C++.
if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Hidden by default, everywhere: the shared core library (src/core/core-shared) exports one
# function and nothing else, so the JUCE compiled into it never meets the JUCE compiled into
# a plugin that loads it. juce_add_plugin sets the same for the plugins; executables do not
# mind. Set before any target exists so the static libraries the shared one links have it.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# The DLL runtime on every config, set before any third-party target exists so that JUCE,
# the plugin and the tests agree. A plugin loaded into a host must not drag a static CRT
# of its own across the boundary.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(OqaOptions)
include(OqaThirdParty)

# enable_testing(), not include(CTest): CTest's module adds a dozen dashboard targets and
# cache entries nobody here uses. Called before the first directory that adds a test:
# lib/oscquery carries its own.
if(OQA_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(lib/oscquery)
add_subdirectory(src)

if(OQA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
