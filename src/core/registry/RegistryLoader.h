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
    FINDING THE SHARED CORE LIBRARY, OR DOING WITHOUT (devplan Phase 3; the
    pattern is WFS-DIY's BridgeLoader, the outcome is not: where WFS-DIY's
    plugins go silent without their bridge, this one runs on the core linked
    into its own binary and says so in the status bar).

    WHERE IT LOOKS, in order:
      1. the file named by the environment variable OQA_CORE_LIBRARY, if set
         (a bench or a test pointing at a build);
      2. the directory of this binary's own module (the plugin bundle's
         binary, found by the address of a function in it, never the host's
         executable), then up to five parents of it, for the library's file
         name: a library installed beside the plugin bundles is found from
         inside any of them;
      3. the per-OS fixed directory the installer uses.

    WHAT IT CHECKS: the one export oqa_core_api() exists, its table says
    OQA_CORE_ABI_VERSION exactly and is at least as large as this binary
    knows. Anything else is a reason, kept in words, and the static table is
    used instead. The library once opened is never closed: the registry in it
    owns threads and sockets for the life of the process.

    load() decides once per binary; every instance in it then uses the same
    table. tryLoad() is for tests, and for a bench that wants to know why.
*/

#include <core/registry/RegistryApi.h>

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace oqa::core
{
    struct LoadedCore
    {
        /** The table to call through: the library's, or staticCoreApi(). Never null. */
        const OqaCoreApi* api = nullptr;

        /** True when `api` is the shared library's. */
        bool shared = false;

        /** The library's path when shared; empty otherwise. */
        std::string path;

        /** Why the static copy is in use, in words; empty when shared. */
        std::string note;

        /** The system's handle (HMODULE or dlopen's), never closed: the registry in a library owns threads. */
        void* handle = nullptr;
    };

    class RegistryLoader
    {
    public:
        /** The binary's decision, made on the first call. */
        static const LoadedCore& load();

        /** The library's file name on this platform. */
        static const char* libraryFileName() noexcept;

        /** This binary's own module file (the plugin's, not the host's). */
        static juce::File ownModuleFile();

        /** Every file that would be tried, in order, whether or not it exists. */
        static std::vector<juce::File> candidates();

        /*  Open one file and check it. The result's api is the library's on
            success, else staticCoreApi() with the reason in `note`. */
        static LoadedCore tryLoad (const juce::File& file);

        /** The static fallback, with a note. */
        static LoadedCore fallback (const std::string& note);
    };
}
