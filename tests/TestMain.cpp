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
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace oqatest
{
    namespace
    {
        std::string appliedLocale { "C" };

        std::vector<std::function<void()>>& shutdownHooks()
        {
            static std::vector<std::function<void()>> hooks;
            return hooks;
        }
    }

    const char* appliedLocaleName()
    {
        return appliedLocale.c_str();
    }

    void registerShutdownHook (std::function<void()> hook)
    {
        shutdownHooks().push_back (std::move (hook));
    }

    void runShutdownHooks()
    {
        for (auto& hook : shutdownHooks())
            hook();

        shutdownHooks().clear();
    }
}

//==============================================================================
#if defined (_WIN32)
 #include <windows.h>

namespace
{
    /*  A crashed case says WHERE: doctest reports the signal and nothing more,
        and there is no debugger on the CI runners. First chance, so it runs
        before doctest's own filter; the backtrace is JUCE's, with symbols in
        a Debug build. */
    LONG WINAPI printBacktraceOnCrash (EXCEPTION_POINTERS* info)
    {
        if (info != nullptr && info->ExceptionRecord != nullptr
            && info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        {
            std::cerr << "ACCESS VIOLATION on thread " << GetCurrentThreadId() << ":" << std::endl
                      << juce::SystemStats::getStackBacktrace() << std::endl;
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    const bool crashHandlerInstalled = (AddVectoredExceptionHandler (1, printBacktraceOnCrash), true);
}
#else
 #include <csignal>
 #include <cstdlib>
 #include <exception>
 #include <execinfo.h>
 #include <unistd.h>

namespace
{
    /*  The POSIX twin (Phase 5b, when a case aborted on the macOS runner with
        no word of where): doctest reports the signal, restores the previous
        handlers and raises it again, so this one then prints the frames.
        backtrace_symbols_fd is async-signal-safe enough for a process that
        is dying anyway; a Debug build has the names. std::terminate (an
        exception out of a noexcept function, clap-helpers' Terminate) says
        so too, with the frames, before the abort. */
    void printBacktraceOnSignal (int signal)
    {
        const char* name = signal == SIGABRT ? "SIGABRT" : signal == SIGSEGV ? "SIGSEGV" : signal == SIGBUS ? "SIGBUS" : "signal";
        const char head[] = "\nfatal ";
        write (STDERR_FILENO, head, sizeof (head) - 1);
        write (STDERR_FILENO, name, std::strlen (name));
        write (STDERR_FILENO, ", backtrace:\n", 13);

        void* frames[64];
        const int count = backtrace (frames, 64);
        backtrace_symbols_fd (frames, count, STDERR_FILENO);

        std::signal (signal, SIG_DFL);
        std::raise (signal);
    }

    void printBacktraceOnTerminate()
    {
        const char head[] = "\nstd::terminate called";
        write (STDERR_FILENO, head, sizeof (head) - 1);

        if (const auto current = std::current_exception())
        {
            try { std::rethrow_exception (current); }
            catch (const std::exception& e) { const char with[] = " with exception: "; write (STDERR_FILENO, with, sizeof (with) - 1); write (STDERR_FILENO, e.what(), std::strlen (e.what())); }
            catch (...) { const char with[] = " with a non-std exception"; write (STDERR_FILENO, with, sizeof (with) - 1); }
        }

        write (STDERR_FILENO, ", backtrace:\n", 13);
        void* frames[64];
        const int count = backtrace (frames, 64);
        backtrace_symbols_fd (frames, count, STDERR_FILENO);
        std::abort();
    }

    const bool crashHandlerInstalled = []
    {
        std::signal (SIGABRT, printBacktraceOnSignal);
        std::signal (SIGSEGV, printBacktraceOnSignal);
        std::signal (SIGBUS, printBacktraceOnSignal);
        std::set_terminate (printBacktraceOnTerminate);
        return true;
    }();
}
#endif

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
    const int result = context.run();

    oqatest::runShutdownHooks();
    return result;
}
