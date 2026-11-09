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

#include <core/registry/RegistryLoader.h>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <dlfcn.h>
 #if defined (__APPLE__)
  #include <mach-o/dyld.h>
 #endif
#endif

namespace oqa::core
{
    namespace
    {
        /*  A function whose address is in this module, whichever binary this
            is linked into. */
        void marker() {}

        constexpr int parentsToWalk = 5;

        using ApiFn = const OqaCoreApi* (*) (void);

      #if defined (_WIN32)
        void* openLibrary (const juce::String& path)         { return LoadLibraryW (path.toWideCharPointer()); }
        void* findFunction (void* handle, const char* name)  { return reinterpret_cast<void*> (GetProcAddress (static_cast<HMODULE> (handle), name)); }
        void closeLibrary (void* handle)                     { FreeLibrary (static_cast<HMODULE> (handle)); }
      #else
        void* openLibrary (const juce::String& path)         { return dlopen (path.toRawUTF8(), RTLD_LOCAL | RTLD_NOW); }
        void* findFunction (void* handle, const char* name)  { return dlsym (handle, name); }
        void closeLibrary (void* handle)                     { dlclose (handle); }
      #endif
    }

    const char* RegistryLoader::libraryFileName() noexcept
    {
      #if defined (_WIN32)
        return "OSCQuery-Automation-Core.dll";
      #elif defined (__APPLE__)
        return "libOSCQuery-Automation-Core.dylib";
      #else
        return "libOSCQuery-Automation-Core.so";
      #endif
    }

    juce::File RegistryLoader::ownModuleFile()
    {
      #if defined (_WIN32)
        HMODULE module = nullptr;

        if (! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCWSTR> (&marker), &module))
            return {};

        wchar_t path[MAX_PATH * 4] = {};
        const auto length = GetModuleFileNameW (module, path, static_cast<DWORD> (sizeof (path) / sizeof (path[0])));

        if (length == 0)
            return {};

        return juce::File (juce::String (path, length));
      #else
        Dl_info info {};

        if (dladdr (reinterpret_cast<void*> (&marker), &info) == 0 || info.dli_fname == nullptr)
            return {};

        return juce::File (juce::String::fromUTF8 (info.dli_fname));
      #endif
    }

    juce::File RegistryLoader::alreadyLoaded()
    {
        const auto name = juce::String (libraryFileName());

      #if defined (_WIN32)
        /*  By base name: a module loaded from any path answers to it. */
        HMODULE module = nullptr;

        if (! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, name.toWideCharPointer(), &module) || module == nullptr)
            return {};

        wchar_t path[MAX_PATH * 4] = {};
        const auto length = GetModuleFileNameW (module, path, static_cast<DWORD> (sizeof (path) / sizeof (path[0])));
        return length > 0 ? juce::File (juce::String (path, length)) : juce::File();
      #elif defined (__APPLE__)
        /*  dyld's list of images, by leaf name. */
        const auto count = _dyld_image_count();

        for (uint32_t i = 0; i < count; ++i)
        {
            if (const auto* imageName = _dyld_get_image_name (i))
            {
                const juce::File image (juce::String::fromUTF8 (imageName));

                if (image.getFileName() == name)
                    return image;
            }
        }

        return {};
      #else
        /*  RTLD_NOLOAD answers only for a library already loaded, by its
            soname; the path comes from a symbol in it. */
        void* handle = dlopen (name.toRawUTF8(), RTLD_LAZY | RTLD_NOLOAD);

        if (handle == nullptr)
            return {};

        juce::File out;
        Dl_info info {};

        if (void* symbol = dlsym (handle, "oqa_core_api"); symbol != nullptr && dladdr (symbol, &info) != 0 && info.dli_fname != nullptr)
            out = juce::File (juce::String::fromUTF8 (info.dli_fname));

        dlclose (handle);    // the NOLOAD handle's own count only; the library stays, loaded by whoever loaded it
        return out;
      #endif
    }

    std::vector<juce::File> RegistryLoader::candidates()
    {
        std::vector<juce::File> out;

        if (const auto env = juce::SystemStats::getEnvironmentVariable ("OQA_CORE_LIBRARY", {}); env.isNotEmpty())
            out.emplace_back (env);

        if (const auto loaded = alreadyLoaded(); loaded != juce::File())
            out.push_back (loaded);

        const auto name = juce::String (libraryFileName());

        if (const auto own = ownModuleFile(); own != juce::File())
        {
            auto directory = own.getParentDirectory();

            for (int i = 0; i <= parentsToWalk && directory != juce::File(); ++i)
            {
                out.push_back (directory.getChildFile (name));
                const auto parent = directory.getParentDirectory();

                if (parent == directory)
                    break;

                directory = parent;
            }
        }

      #if defined (_WIN32)
        out.push_back (juce::File ("C:/Program Files/Common Files/Pix et Bel/OSCQuery Automation").getChildFile (name));
      #elif defined (__APPLE__)
        out.push_back (juce::File ("/Library/Application Support/Pix et Bel/OSCQuery Automation").getChildFile (name));
      #else
        out.push_back (juce::File ("/usr/local/lib/oscquery-automation").getChildFile (name));
        out.push_back (juce::File ("/usr/lib/oscquery-automation").getChildFile (name));
      #endif

        return out;
    }

    LoadedCore RegistryLoader::fallback (const std::string& note)
    {
        LoadedCore out;
        out.api = staticCoreApi();
        out.shared = false;
        out.note = note;
        return out;
    }

    LoadedCore RegistryLoader::tryLoad (const juce::File& file)
    {
        if (! file.existsAsFile())
            return fallback (file.getFullPathName().toStdString() + ": not found");

        /*  The system's own loader, and NEVER CLOSED once the table is taken:
            the registry in a library owns threads and sockets, and unloading
            it under them is how a host crashes on exit. A file that turns out
            not to be the library is closed again. No JUCE object is involved,
            so nothing is counted at exit. */
        const auto path = file.getFullPathName();
        void* handle = openLibrary (path);

        if (handle == nullptr)
            return fallback (path.toStdString() + ": would not load");

        const auto entry = reinterpret_cast<ApiFn> (findFunction (handle, "oqa_core_api"));

        if (entry == nullptr)
        {
            closeLibrary (handle);
            return fallback (path.toStdString() + ": is not the core library (no oqa_core_api)");
        }

        const auto* api = entry();

        if (api == nullptr)
        {
            closeLibrary (handle);
            return fallback (path.toStdString() + ": answered no table");
        }

        if (api->abiVersion != OQA_CORE_ABI_VERSION || api->size < sizeof (OqaCoreApi))
        {
            const auto found = api->abiVersion;
            closeLibrary (handle);
            return fallback (path.toStdString() + ": is ABI version " + std::to_string (found)
                             + ", this plugin needs " + std::to_string (OQA_CORE_ABI_VERSION));
        }

        LoadedCore out;
        out.api = api;
        out.shared = true;
        out.path = path.toStdString();
        out.handle = handle;
        return out;
    }

    const LoadedCore& RegistryLoader::load()
    {
        static const LoadedCore decided = []
        {
            std::string notes;

            for (const auto& candidate : candidates())
            {
                auto attempt = tryLoad (candidate);

                if (attempt.shared)
                    return attempt;

                /*  A file that exists but is wrong is worth its own words; a
                    place with nothing in it is just a place looked at. */
                if (candidate.existsAsFile())
                    notes += (notes.empty() ? "" : "; ") + attempt.note;
            }

            return fallback (notes.empty() ? std::string ("running alone: no shared core library found beside this plugin")
                                           : "running alone: " + notes);
        }();

        return decided;
    }
}
