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
    THE $instance TOKEN (devplan Phase 6d-2; PRD, Binding model): the pure
    substitution; a slot bound through a pattern keeps it and follows the
    instance's number; the chunk carries `instance` and `path_pattern`; a
    Monitor's command binds a pattern in another instance, which substitutes
    its own number.
*/

#include <doctest/doctest.h>

#include "RegistryRig.h"
#include "TestSupport.h"

#include <core/Instance.h>
#include <core/PathPattern.h>
#include <core/StateChunk.h>
#include <core/registry/Registry.h>

#include <string>

using namespace oqa::core;
using namespace oqa::oscquery;
using oqatest::Rig;
using oqatest::waitUntil;

namespace
{
    const OqaCoreApi* api() { return oqatest::coreApi(); }

    const bool shutdownRegistered = (oqatest::registerShutdownHook ([] { Registry::shared().shutdown(); }), true);

    std::string stateFixture (const char* name)
    {
        return juce::File (juce::String (OQA_TEST_STATE_DIR)).getChildFile (name).loadFileAsString().toStdString();
    }

    /** A path under /mixer/channel/<n>/ in the generic fixture, with the number swapped for the token. */
    std::string channelPattern (const Rig& rig, const char* leaf)
    {
        for (const auto* node : rig.tree.leaves())
        {
            const auto& p = node->fullPath;
            const auto tail = std::string ("/1/") + leaf;

            if (p.size() > tail.size() && p.compare (p.size() - tail.size(), tail.size(), tail) == 0)
                return p.substr (0, p.size() - tail.size()) + "/$instance/" + leaf;
        }

        FAIL ("the fixture has no numbered " << leaf);
        return {};
    }

    bool mentions (const std::vector<std::string>& warnings, const char* needle)
    {
        for (const auto& w : warnings)
            if (w.find (needle) != std::string::npos)
                return true;

        return false;
    }
}

//==============================================================================
TEST_CASE ("pattern: every $instance is the number; a path without the token is itself")
{
    CHECK (substituteInstance ("/mixer/channel/$instance/gain", 3) == "/mixer/channel/3/gain");
    CHECK (substituteInstance ("/a/$instance/b/$instance", 12) == "/a/12/b/12");
    CHECK (substituteInstance ("/plain/path", 7) == "/plain/path");
    CHECK (substituteInstance ("$instance", 1) == "1");
    CHECK (substituteInstance ("", 1).empty());
    CHECK (hasInstanceToken ("/x/$instance"));
    CHECK_FALSE (hasInstanceToken ("/x/instance"));
}

TEST_CASE ("pattern: a slot bound through a pattern keeps it, follows the instance's number, and says why when the number has no path")
{
    Rig rig;
    const auto pattern = channelPattern (rig, "gain");

    Instance a (api(), 8, "patterned");
    const auto device = a.addDevice (rig.device ("patterned-device"));
    a.connect();
    rig.waitReady (a);

    std::string why;
    REQUIRE_MESSAGE (a.bindPattern (0, device, pattern, 0, why), why);
    CHECK (a.chunk().slots[0].path == substituteInstance (pattern, 1));
    CHECK (a.chunk().slots[0].pathPattern == pattern);
    CHECK (a.chunk().slots[0].bound());

    /*  A plain path beside it: no pattern kept. */
    REQUIRE (a.bindPattern (1, device, substituteInstance (channelPattern (rig, "pan"), 1), 0, why));
    CHECK (a.chunk().slots[1].pathPattern.empty());

    /*  The number moves: the patterned slot follows, the plain one stays. */
    a.setInstanceNumber (2);
    CHECK (a.instanceNumber() == 2);
    CHECK (a.chunk().slots[0].path == substituteInstance (pattern, 2));
    CHECK (a.chunk().slots[0].pathPattern == pattern);
    CHECK (a.chunk().slots[1].path == substituteInstance (channelPattern (rig, "pan"), 1));

    /*  A number the device has no path for: the slot keeps its binding, the
        warning says which path was not served. */
    a.setInstanceNumber (9);
    CHECK (a.instanceNumber() == 9);
    CHECK (a.chunk().slots[0].path == substituteInstance (pattern, 2));
    CHECK (mentions (a.warnings(), substituteInstance (pattern, 9).c_str()));

    /*  The chunk carries both, and reads them back. */
    const auto json = a.saveState();
    std::string error;
    const auto chunk = StateChunk::fromJson (json, 8, error);
    REQUIRE_MESSAGE (chunk.has_value(), error);
    CHECK (chunk->instanceNumber == 9);
    CHECK (chunk->slots[0].pathPattern == pattern);
    CHECK (chunk->slots[1].pathPattern.empty());
    CHECK (json.find ("path_pattern") != std::string::npos);

    /*  A click-bind to a leaf drops the pattern: the explicit path is the model. */
    const auto& pan = rig.leafWhere ([&] (const Node& n) { return n.fullPath == substituteInstance (channelPattern (rig, "pan"), 1); });
    REQUIRE (a.bind (0, pan, 0, device, why));
    CHECK (a.chunk().slots[0].pathPattern.empty());

    a.unbind (1);
    CHECK (a.chunk().slots[1].pathPattern.empty());
}

TEST_CASE ("v1-instance: a chunk with `instance` and a `path_pattern` reads back as saved, and writes the pattern only where there is one")
{
    std::string error;
    const auto chunk = StateChunk::fromJson (stateFixture ("v1-instance.json"), 8, error);
    REQUIRE_MESSAGE (chunk.has_value(), error);
    CHECK (chunk->warnings.empty());
    CHECK (chunk->instanceNumber == 2);
    CHECK (chunk->slots[0].pathPattern == "/mixer/channel/$instance/gain");
    CHECK (chunk->slots[0].path == "/mixer/channel/2/gain");
    CHECK (chunk->slots[1].pathPattern.empty());

    const auto written = chunk->toJson();
    const auto parsed = juce::JSON::parse (juce::String (written));
    const auto* slots = parsed.getProperty ("slots", {}).getArray();
    REQUIRE (slots != nullptr);
    REQUIRE (slots->size() == 2);
    CHECK ((*slots)[0].hasProperty ("path_pattern"));
    CHECK_FALSE ((*slots)[1].hasProperty ("path_pattern"));
    CHECK (static_cast<int> (parsed.getProperty ("instance", 1)) == 2);
}

TEST_CASE ("pattern: a Monitor's command binds a pattern in another instance, which substitutes its own number")
{
    Rig rig;
    const auto pattern = channelPattern (rig, "gain");

    Instance target (api(), 8, "commanded");
    target.addDevice (rig.device ("commanded-device"));
    target.setInstanceNumber (2);
    target.connect();
    rig.waitReady (target);

    Instance monitor (api(), 0, "commanding-monitor");
    monitor.connect();

    const auto status = juce::JSON::parse (juce::String (monitor.processStatusJson()));
    juce::int64 targetId = -1;

    if (const auto* instances = status.getProperty ("instances", {}).getArray())
        for (const auto& i : *instances)
            if (i.getProperty ("label", "").toString() == "commanded")
                targetId = static_cast<juce::int64> (i.getProperty ("id", -1));

    REQUIRE (targetId >= 0);

    int woken = 0;
    target.setCommandWake ([&] { ++woken; });

    REQUIRE (monitor.commandInstance (targetId, 3, "bind", pattern, "commanded-device", 0.0));
    REQUIRE (waitUntil ([&] { return woken > 0; }));
    CHECK (target.pollCommands() == 1);
    CHECK (target.chunk().slots[3].path == substituteInstance (pattern, 2));
    CHECK (target.chunk().slots[3].pathPattern == pattern);

    /*  Nobody of that id: refused at the registry. */
    CHECK_FALSE (monitor.commandInstance (targetId + 100000, 0, "bind", pattern, "", 0.0));
}
