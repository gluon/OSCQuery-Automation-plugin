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
    THE SHARED CORE LIBRARY AND ITS LOADER (devplan Phase 3c): the library
    built by this same build loads, answers the table at the right version,
    and runs a registry of its own, distinct from the one in this executable;
    a missing or wrong file falls back to the static table with the reason.
*/

#include <doctest/doctest.h>

#include "TestSupport.h"
#include "support/WsTestClient.h"

#include <MockDevice.h>

#include <core/Instance.h>
#include <core/registry/Registry.h>
#include <core/registry/RegistryLoader.h>

#include <oscquery/osc/Codec.h>
#include <oscquery/tree/Tree.h>

#include <string>

using namespace oqa::core;
using oqatest::waitUntil;

TEST_CASE ("loader: the library's file name is this platform's, and the candidates start where this binary is")
{
    const std::string name = RegistryLoader::libraryFileName();
    CHECK (name.find ("OSCQuery-Automation-Core") != std::string::npos);

    const auto own = RegistryLoader::ownModuleFile();
    CHECK (own.existsAsFile());
    MESSAGE ("own module: " << own.getFullPathName().toStdString());

    const auto candidates = RegistryLoader::candidates();
    REQUIRE (candidates.size() >= 2);
    CHECK (candidates.front().getFileName().toStdString() == name);
    CHECK (candidates.front().getParentDirectory() == own.getParentDirectory());
}

TEST_CASE ("loader: the shared library built here loads, is version 1, and runs a registry of its own")
{
    const juce::File library (juce::String (OQA_CORE_SHARED_PATH));
    REQUIRE_MESSAGE (library.existsAsFile(), library.getFullPathName().toStdString());

    const auto loaded = RegistryLoader::tryLoad (library);
    REQUIRE_MESSAGE (loaded.shared, loaded.note);
    REQUIRE (loaded.api != nullptr);
    CHECK (loaded.api != staticCoreApi());
    CHECK (loaded.api->abiVersion == OQA_CORE_ABI_VERSION);
    CHECK (loaded.api->size == sizeof (OqaCoreApi));
    CHECK (loaded.path == library.getFullPathName().toStdString());
    CHECK (loaded.note.empty());

    /*  An instance through the library's table lives in the library's
        registry, which this executable's own knows nothing about. */
    const auto before = Registry::shared().instanceCount();

    {
        Instance viaLibrary (loaded.api, 4, "via the library");
        viaLibrary.connect();

        CHECK (Registry::shared().instanceCount() == before);

        const auto status = juce::JSON::parse (juce::String (viaLibrary.statusJson()));
        CHECK (status.isObject());

        char* all = loaded.api->statusJson();
        REQUIRE (all != nullptr);
        const auto whole = juce::JSON::parse (juce::String (all));
        loaded.api->freeString (all);
        CHECK (static_cast<int> (whole.getProperty ("instances", 0)) == 1);
    }

    /*  The library's table is version 1 with the additive shutdown present. */
    REQUIRE (loaded.api->size >= sizeof (OqaCoreApi));
    REQUIRE (loaded.api->shutdown != nullptr);
    oqatest::registerShutdownHook ([api = loaded.api] { api->shutdown(); });

    /*  And it sends: a mock device, a slot bound, one block, one bundle. */
    oqatest::MockDevice mock;
    REQUIRE (mock.loadTree (juce::JSON::parse (oqatest::fixture ("trees/generic-device.json"))));
    REQUIRE (mock.start());

    std::string error;
    auto tree = oqa::oscquery::Tree::parse (mock.node ("/"), error);
    REQUIRE_MESSAGE (tree.has_value(), error);

    const oqa::oscquery::Node* leaf = nullptr;

    for (const auto* l : tree->leaves())
        if (l->type == "f" && l->bindableForSend() && l->argument (0).range.has_value() && l->argument (0).range->hasMinMax() && ! l->critical)
        {
            leaf = l;
            break;
        }

    REQUIRE (leaf != nullptr);

    Instance one (loaded.api, 4, "via the library, sending");
    DeviceRef ref;
    ref.name = "loader-mock";
    ref.lastHost = "127.0.0.1";
    ref.lastPort = mock.httpPort();
    REQUIRE (one.bind (0, *leaf, 0, one.addDevice (ref), error));
    one.connect();

    REQUIRE (waitUntil ([&]
    {
        const auto status = juce::JSON::parse (juce::String (one.statusJson()));
        const auto* devices = status.getProperty ("devices", {}).getArray();
        return devices != nullptr && ! devices->isEmpty() && static_cast<bool> ((*devices)[0].getProperty ("sender_ready", false));
    }));

    float values[4] = { 0.5f, 0.0f, 0.0f, 0.0f };
    one.processBlock (values, 4);

    REQUIRE (waitUntil ([&] { return ! mock.receivedOsc().empty(); }));
    const auto decoded = oqa::oscquery::osc::decode (mock.receivedOsc()[0].data(), mock.receivedOsc()[0].size());
    REQUIRE (decoded.ok);
    REQUIRE (decoded.packet.isBundle());
    REQUIRE (decoded.packet.elements.size() == 1);
    CHECK (decoded.packet.elements[0].address == leaf->fullPath);

    one.disconnect();
}

TEST_CASE ("loader: a missing file, and a file that is not the library, fall back with the reason")
{
    const auto missing = RegistryLoader::tryLoad (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                      .getChildFile ("no-such-OSCQuery-Automation-Core.dll"));
    CHECK_FALSE (missing.shared);
    CHECK (missing.api == staticCoreApi());
    CHECK (missing.note.find ("not found") != std::string::npos);

    /*  This executable is a module without the export (or not loadable as a
        library at all): either way the static table, with words. */
    const auto wrong = RegistryLoader::tryLoad (RegistryLoader::ownModuleFile());
    CHECK_FALSE (wrong.shared);
    CHECK (wrong.api == staticCoreApi());
    CHECK_FALSE (wrong.note.empty());
    MESSAGE ("own module as a library: " << wrong.note);
}

TEST_CASE ("loader: the binary's decision is made once and is never null")
{
    const auto& first = RegistryLoader::load();
    const auto& second = RegistryLoader::load();

    CHECK (&first == &second);
    REQUIRE (first.api != nullptr);
    CHECK (first.api->abiVersion == OQA_CORE_ABI_VERSION);

    if (! first.shared)
    {
        CHECK (first.note.find ("running alone") != std::string::npos);
        MESSAGE ("this executable: " << first.note);
    }
    else
    {
        MESSAGE ("this executable found the library at " << first.path);
    }
}
