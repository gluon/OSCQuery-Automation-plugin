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
    THE C ABI OVER THE REGISTRY. Compiled into every binary as
    oqa::core::staticCoreApi(); the shared core library exports the same table
    as oqa_core_api() (core-shared/CoreExport.cpp). The functions are the
    same; only the registry behind them differs: the library's is the one the
    whole process shares.
*/

#include <core/registry/RegistryApi.h>

#include <core/registry/Registry.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
    using oqa::core::Registry;

    Registry::InstanceRecord* recordOf (OqaInstance* instance)
    {
        return reinterpret_cast<Registry::InstanceRecord*> (instance);
    }

    /** A C string the caller frees with freeString; malloc so free pairs with it across the boundary. */
    char* duplicate (const std::string& text)
    {
        auto* out = static_cast<char*> (std::malloc (text.size() + 1));

        if (out != nullptr)
            std::memcpy (out, text.c_str(), text.size() + 1);

        return out;
    }

    void setError (char** error, const std::string& text)
    {
        if (error != nullptr)
            *error = duplicate (text);
    }

    //==============================================================================
    OqaInstance* instanceRegister (const char* label, int slotCount)
    {
        return reinterpret_cast<OqaInstance*> (Registry::shared().registerInstance (label != nullptr ? label : "", slotCount));
    }

    void instanceUnregister (OqaInstance* instance)
    {
        Registry::shared().unregisterInstance (recordOf (instance));
    }

    int instanceSetBindings (OqaInstance* instance, const char* chunkJson, char** error)
    {
        if (instance == nullptr || chunkJson == nullptr)
        {
            setError (error, "no instance or no chunk");
            return 1;
        }

        std::string why;

        if (Registry::shared().setBindingsJson (recordOf (instance), chunkJson, why))
            return 0;

        setError (error, why);
        return 1;
    }

    void instancePost (OqaInstance* instance, int slot, float normalised)
    {
        Registry::shared().post (recordOf (instance), slot, normalised);
    }

    void instanceEndBlock (OqaInstance* instance)
    {
        Registry::shared().endBlock (recordOf (instance));
    }

    int instanceTestSend (OqaInstance* instance, int slot, float normalised, char** error)
    {
        std::string why;

        if (Registry::shared().testSend (recordOf (instance), slot, normalised, why))
            return 0;

        setError (error, why);
        return 1;
    }

    char* instanceDeviceTreeJson (OqaInstance* instance, int deviceIndex, const char* path)
    {
        const auto tree = Registry::shared().deviceTree (recordOf (instance), deviceIndex, path != nullptr ? path : "/");

        if (! tree.has_value())
            return nullptr;

        return duplicate (juce::JSON::toString (*tree, false).toStdString());
    }

    char* instanceStatusJson (OqaInstance* instance)
    {
        return duplicate (juce::JSON::toString (Registry::shared().status (recordOf (instance)), false).toStdString());
    }

    char* statusJson()
    {
        return duplicate (juce::JSON::toString (Registry::shared().status(), false).toStdString());
    }

    int deviceSetAddress (const char* name, const char* identityValue, const char* host, int port)
    {
        if (name == nullptr || host == nullptr)
            return 1;

        return Registry::shared().setDeviceAddress (name, identityValue != nullptr ? identityValue : "", host, port) ? 0 : 1;
    }

    int settingsSetJson (const char* json, char** error)
    {
        if (json == nullptr)
        {
            setError (error, "no settings");
            return 1;
        }

        juce::var parsed;
        const auto result = juce::JSON::parse (juce::String (json), parsed);

        if (result.failed())
        {
            setError (error, "not JSON: " + result.getErrorMessage().toStdString());
            return 1;
        }

        auto settings = Registry::shared().settings();
        settings.applyVar (parsed);
        Registry::shared().setSettings (settings);
        return 0;
    }

    char* settingsJson()
    {
        return duplicate (juce::JSON::toString (Registry::shared().settings().toVar(), false).toStdString());
    }

    void freeString (char* text)
    {
        std::free (text);
    }

    void shutdownRegistry()
    {
        Registry::shared().shutdown();
    }

    void instanceSetReceiver (OqaInstance* instance, void (*wake) (void* context), void* context)
    {
        Registry::shared().setReceiver (recordOf (instance), wake, context);
    }

    int instancePollReceived (OqaInstance* instance, int* slots, float* values, int capacity)
    {
        return Registry::shared().pollReceived (recordOf (instance), slots, values, capacity);
    }

    const OqaCoreApi table {
        OQA_CORE_ABI_VERSION,
        sizeof (OqaCoreApi),
        instanceRegister,
        instanceUnregister,
        instanceSetBindings,
        instancePost,
        instanceEndBlock,
        instanceTestSend,
        instanceDeviceTreeJson,
        instanceStatusJson,
        statusJson,
        deviceSetAddress,
        settingsSetJson,
        settingsJson,
        freeString,
        shutdownRegistry,
        instanceSetReceiver,
        instancePollReceived
    };
}

namespace oqa::core
{
    const OqaCoreApi* staticCoreApi() noexcept
    {
        return &table;
    }
}
