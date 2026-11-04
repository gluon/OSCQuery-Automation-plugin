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

    Origin: go.dot tests/TestMain.cpp at 1c8aaa0a, without go.dot's child-process
    dispatch, on doctest's own header, with --oqa-locale for --wfg-locale.
*/

/*
    The doctest entry point and the locale switch.

    The ONE translation unit that defines DOCTEST_CONFIG_IMPLEMENT. Other test
    files include <doctest/doctest.h> plainly.
*/

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "TestSupport.h"

#include <juce_events/juce_events.h>

#include <clocale>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace oqatest
{
    namespace
    {
        std::string appliedLocale { "C" };
    }

    const char* appliedLocaleName()
    {
        return appliedLocale.c_str();
    }
}

//==============================================================================
int main (int argc, char** argv)
{
    /*  JUCE is initialised once, for the whole process. go.dot learnt on macOS
        that bringing it up and down around each case fails non-deterministically
        in whichever case runs next. Held for the life of main. */
    const juce::ScopedJuceInitialiser_GUI juceForTheWholeSuite;

    /*  --oqa-locale=<name> is ours, not doctest's: doctest would read an unknown
        argument as a test-name filter, so it is stripped before doctest sees the
        command line. The name comes from OQA_LOCALE_FR (cmake/OqaOptions.cmake),
        because the Windows UCRT wants "fr-FR" and glibc "fr_FR.UTF-8". */
    static constexpr const char* localeFlag = "--oqa-locale=";
    const auto localeFlagLength = std::strlen (localeFlag);

    std::vector<const char*> forwarded;
    forwarded.reserve (static_cast<std::size_t> (argc));

    const char* requestedLocale = nullptr;

    for (int i = 0; i < argc; ++i)
    {
        if (i > 0 && std::strncmp (argv[i], localeFlag, localeFlagLength) == 0)
            requestedLocale = argv[i] + localeFlagLength;
        else
            forwarded.push_back (argv[i]);
    }

    if (requestedLocale != nullptr)
    {
        /*  A HARD FAILURE, not a skip. If the locale is missing, every
            locale-dependent assertion would quietly run under "C" and report
            green: a suite that proves nothing while looking as if it did. On the
            Linux runner the prerequisite is `locale-gen fr_FR.UTF-8`. */
        if (std::setlocale (LC_ALL, requestedLocale) == nullptr)
        {
            std::cerr << "FATAL: locale \"" << requestedLocale
                      << "\" is not available on this system" << std::endl;
            return 2;
        }

        oqatest::appliedLocale = requestedLocale;
    }

    doctest::Context context;
    context.applyCommandLine (static_cast<int> (forwarded.size()), forwarded.data());
    return context.run();
}
