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

# THE CLAP SHIM'S TESTS (devplan Phase 5b): a host of our own (TestHost.h) loads the .clap
# this build makes, as a DAW would, and drives it through the C API against the mock device.
# CTest: once under C, once under French, like the core's.

add_executable(oqa_clap_tests
    ../TestMain.cpp
    ../support/RtAllocCounter.cpp
    TestHost.h
    ClapTests.cpp
    ClapParamsTests.cpp
    ClapRecordTests.cpp
    ClapGuiTests.cpp
    ClapServerTests.cpp
    ClapMonitorTests.cpp)

target_include_directories(oqa_clap_tests PRIVATE "${CMAKE_SOURCE_DIR}/tests" "${CMAKE_SOURCE_DIR}/src")
target_include_directories(oqa_clap_tests SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/ThirdParty/doctest")

# The module and the shared core library this build makes. The harness points the module's
# loader at the library (OQA_CORE_LIBRARY), so the plugin and the test share one registry.
target_compile_definitions(oqa_clap_tests PRIVATE
    OQA_TEST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
    OQA_TEST_STATE_DIR="${CMAKE_SOURCE_DIR}/tests/state"
    OQA_CLAP_PATH="$<TARGET_FILE:oqa_clap>"
    OQA_CORE_SHARED_PATH="$<TARGET_FILE:oqa_core_shared>"
    OQA_LOCALE_FR="${OQA_LOCALE_FR}")

add_dependencies(oqa_clap_tests oqa_clap oqa_core_shared)

# The same link order as the core's tests, for the GNU ld reason recorded there.
target_link_libraries(oqa_clap_tests PRIVATE clap oqa::core oqa::oscquery oqa_mock_device oqa_juce oqa::warnings)

if(NOT WIN32)
    target_link_libraries(oqa_clap_tests PRIVATE ${CMAKE_DL_LIBS})
endif()

add_test(NAME clap.C COMMAND oqa_clap_tests --oqa-locale=C)
add_test(NAME clap.fr_FR COMMAND oqa_clap_tests --oqa-locale=${OQA_LOCALE_FR})
