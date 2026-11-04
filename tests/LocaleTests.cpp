/*
    This file is part of Go.dot — https://github.com/pob31/go.dot

    Copyright (C) 2026 Pierre-Olivier Boulant

    Go.dot is free software: you can redistribute it and/or modify it under the
    terms of the GNU General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later
    version. Go.dot is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
    (LICENSE, at the repository root) for more details.

    SPDX-License-Identifier: GPL-3.0-or-later

    Origin: go.dot tests/LocaleTests.cpp at 1c8aaa0a, comments cut to this
    project, oqatest for wfgtest.
*/

/*
    The locale obligation (devplan, cross-cutting): everything that serialises
    numbers runs under fr_FR as well as C. `RANGE` and `VALUE` in a device tree
    are decimals, and a client that writes "0,5" into a JSON tree or a state
    chunk on a French machine writes a file that reopens wrong elsewhere.

    The locale is set in-process with std::setlocale by TestMain, the only
    reading that behaves the same on glibc, macOS and the Windows UCRT.
*/

#include <doctest/doctest.h>

#include "TestSupport.h"

#include <juce_core/juce_core.h>

#include <clocale>
#include <cstdio>
#include <string>

//==============================================================================
TEST_CASE ("locale: the requested locale actually took effect")
{
    /*  The anti-no-op guard. A locale test that merely runs twice proves
        nothing if setlocale silently did nothing, so this asserts the
        DIFFERENCE: the C library formats with a comma under fr_FR and a full
        stop under C. */
    const std::string applied { oqatest::appliedLocaleName() };
    const std::string french { OQA_LOCALE_FR };

    INFO ("locale in effect: " << applied);

    char formatted[32] = {};
    std::snprintf (formatted, sizeof (formatted), "%.1f", 0.5);

    const std::string decimalPoint { std::localeconv()->decimal_point };

    if (applied == french)
    {
        CHECK (decimalPoint == ",");
        CHECK (std::string (formatted) == "0,5");
    }
    else if (applied == "C")
    {
        CHECK (decimalPoint == ".");
        CHECK (std::string (formatted) == "0.5");
    }
    else
    {
        MESSAGE ("no decimal-separator expectation for locale \"" << applied << "\"");
    }
}

//==============================================================================
TEST_CASE ("locale: JUCE number formatting and JSON are locale-independent")
{
    // std::string, not the bare const char*: doctest prints a char pointer as an address.
    INFO ("locale in effect: " << std::string (oqatest::appliedLocaleName()));

    CHECK (juce::String (0.5).toStdString() == "0.5");
    CHECK (juce::String (-0.25).toStdString() == "-0.25");
    CHECK (juce::String ("0.5").getDoubleValue() == doctest::Approx (0.5));

    // What every tree, state chunk and export goes through.
    const auto parsed = juce::JSON::parse (R"({"MIN": -180.5, "MAX": 0.25})");
    CHECK (static_cast<double> (parsed["MIN"]) == doctest::Approx (-180.5));
    CHECK (juce::JSON::toString (parsed, true).toStdString() == R"({"MIN": -180.5, "MAX": 0.25})");
}
