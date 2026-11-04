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

# One test executable, run twice by CTest: once under C, once under French
# (OQA_LOCALE_FR), because RANGE and VALUE are decimals and every serialisation runs
# under both (devplan, cross-cutting).

add_executable(oqa_tests
    TestMain.cpp
    TestSupport.h
    LocaleTests.cpp)

# doctest from its own submodule, SYSTEM so its warnings stay off our -Werror.
target_include_directories(oqa_tests SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/ThirdParty/doctest")

target_compile_definitions(oqa_tests PRIVATE
    OQA_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    OQA_LOCALE_FR="${OQA_LOCALE_FR}")

target_link_libraries(oqa_tests PRIVATE oqa_juce oqa::warnings)

add_test(NAME unit.C COMMAND oqa_tests --oqa-locale=C)
add_test(NAME unit.fr_FR COMMAND oqa_tests --oqa-locale=${OQA_LOCALE_FR})
