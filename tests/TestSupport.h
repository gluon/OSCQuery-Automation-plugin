/*
    This file is part of the OSCQuery Automation Plugin —
    https://github.com/pob31/OSCQuery-Automation-plugin

    Copyright (C) 2026 Pierre-Olivier Boulant, Julien Bayle

    The OSCQuery Automation Plugin is free software: you can redistribute it
    and/or modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the License,
    or (at your option) any later version. It is distributed in the hope that it
    will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
    Public License (LICENSE, at the repository root) for more details.

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

/*
    Shared declarations for the test suite. Declarations and tiny helpers only:
    a helper with real behaviour in it needs its own test.
*/

#include <juce_core/juce_core.h>

#include <string>

namespace oqatest
{
    /*  The locale the runner applied, as spelled on its command line. Defined in
        TestMain.cpp. */
    const char* appliedLocaleName();

    /** True when the suite is running under the French locale rather than C. */
    inline bool runningUnderFrenchLocale()
    {
        return std::string (appliedLocaleName()) == std::string (OQA_LOCALE_FR);
    }

    /** A file under tests/fixtures/, by its path relative to that folder. */
    inline juce::File fixture (const juce::String& relativePath)
    {
        return juce::File (juce::String (OQA_TEST_FIXTURES_DIR)).getChildFile (relativePath);
    }
}
