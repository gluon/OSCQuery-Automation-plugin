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

/*
    THE ENTRY POINT AND THE FACTORY (devplan Phase 5b): clap_entry, the one
    symbol the module exports, and a factory listing the five slot-count
    variants and the Monitor (Descriptors.h). Every method is thread-safe
    and quick, as a host scanning plugins wants.

    This is the one translation unit that instantiates clap-helpers' Plugin
    and HostProxy templates: the other files include the .hh declarations
    only.
*/

#include "ClapPlugin.h"
#include "Descriptors.h"

#include <core/registry/Registry.h>
#include <core/registry/RegistryApi.h>

#include <clap/helpers/host-proxy.hxx>
#include <clap/helpers/plugin.hxx>

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstring>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

template class clap::helpers::Plugin<oqa::clapshim::misbehaviour, oqa::clapshim::checking>;
template class clap::helpers::HostProxy<oqa::clapshim::misbehaviour, oqa::clapshim::checking>;

namespace oqa::clapshim
{
    namespace
    {
        const char* const features[] = { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr };
        const char* const monitorFeatures[] = { CLAP_PLUGIN_FEATURE_ANALYZER, CLAP_PLUGIN_FEATURE_UTILITY, nullptr };

        constexpr const char* url        = "https://github.com/pob31/OSCQuery-Automation-plugin";
        constexpr const char* description = "Automation lanes bound to OSCQuery parameters";
        constexpr const char* monitorDescription = "The registry's view: devices, every slot across instances, rates, errors, settings";

        constexpr clap_plugin_descriptor_t makeDescriptor (const Variant& v) noexcept
        {
            return clap_plugin_descriptor_t {
                CLAP_VERSION_INIT,
                v.id,
                v.name,
                "Pix et Bel",
                url,
                url,
                url,
                OQA_VERSION_STRING,
                v.slots > 0 ? description : monitorDescription,
                v.slots > 0 ? features : monitorFeatures
            };
        }

        constexpr clap_plugin_descriptor_t descriptors[] = {
            makeDescriptor (variants[0]),
            makeDescriptor (variants[1]),
            makeDescriptor (variants[2]),
            makeDescriptor (variants[3]),
            makeDescriptor (variants[4]),
            makeDescriptor (variants[5]),
        };

        static_assert (sizeof (descriptors) / sizeof (descriptors[0]) == static_cast<std::size_t> (variantCount));

        std::atomic<int> pluginsCreated { 0 };
    }

    const clap_plugin_descriptor_t* descriptor (int index) noexcept
    {
        return index >= 0 && index < variantCount ? &descriptors[index] : nullptr;
    }

    const Variant* variantFor (const char* id) noexcept
    {
        if (id == nullptr)
            return nullptr;

        for (const auto& v : variants)
            if (std::strcmp (v.id, id) == 0)
                return &v;

        return nullptr;
    }

    //==============================================================================
    namespace
    {
        uint32_t CLAP_ABI getPluginCount (const clap_plugin_factory_t*)
        {
            return static_cast<uint32_t> (variantCount);
        }

        const clap_plugin_descriptor_t* CLAP_ABI getPluginDescriptor (const clap_plugin_factory_t*, uint32_t index)
        {
            return descriptor (static_cast<int> (index));
        }

        const clap_plugin_t* CLAP_ABI createPlugin (const clap_plugin_factory_t*, const clap_host_t* host, const char* id)
        {
            if (host == nullptr || ! clap_version_is_compatible (host->clap_version))
                return nullptr;

            const auto* variant = variantFor (id);

            if (variant == nullptr)
                return nullptr;

            const auto index = static_cast<int> (variant - variants);
            auto* plugin = new ClapPlugin (descriptor (index), host, variant->slots);
            ++pluginsCreated;
            return plugin->clapPlugin();
        }

        const clap_plugin_factory_t factory = {
            getPluginCount,
            getPluginDescriptor,
            createPlugin,
        };

        bool CLAP_ABI entryInit (const char*)
        {
            return true;
        }

        void CLAP_ABI entryDeinit()
        {
            /*  A host unloads the module after this. The shared core library,
                once found, is never closed and its threads live on; a
                registry of this binary's own (the fallback) would die with
                the module, so it is stopped here, and the first instance
                registered after a later init starts it again. */
            if (pluginsCreated.load() > 0)
            {
                const auto& loaded = core::RegistryLoader::load();

                if (! loaded.shared && loaded.api != nullptr && loaded.api->shutdown != nullptr)
                    loaded.api->shutdown();
            }
        }

        const void* CLAP_ABI entryGetFactory (const char* id)
        {
            return id != nullptr && std::strcmp (id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
        }
    }
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    oqa::clapshim::entryInit,
    oqa::clapshim::entryDeinit,
    oqa::clapshim::entryGetFactory,
};

#if defined (_WIN32)
/*  As JUCE's own wrappers do: the module's instance handle, for the windows
    JUCE creates in this module (Phase 5b-4). */
extern "C" BOOL WINAPI DllMain (HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        juce::Process::setCurrentModuleInstanceHandle (instance);

    /*  The process is ending: a registry of this module's own (the static
        fallback) is left to the OS, its threads being dead already. */
    if (reason == DLL_PROCESS_DETACH && reserved != nullptr)
        oqa::core::noteProcessExiting();

    return TRUE;
}
#endif
