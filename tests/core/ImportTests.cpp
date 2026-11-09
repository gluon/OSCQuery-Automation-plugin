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
    EXPORT AND IMPORT (devplan Phase 5c; PRD "Export and import"). The export
    file is the state chunk itself: what one instance saves, a fresh one loads
    identically, and saves identically again, under C and fr_FR. What
    tools/convert-mapping.py writes from a ToscA or OSCar mapping (the
    committed expected outputs in tests/fixtures/mappings/) loads as a chunk
    with the lanes it declares, and imported against a device that does not
    serve those paths shows them stale rather than silent.

    Paths appear only through the fixtures, never typed here.
*/

#include <doctest/doctest.h>

#include "RegistryRig.h"
#include "TestSupport.h"

#include <core/Instance.h>
#include <core/StateChunk.h>

#include <string>

using namespace oqa::core;
using oqatest::waitUntil;
using oqatest::Rig;
using oqatest::isRangedFloat;

namespace
{
    const OqaCoreApi* api() { return oqatest::coreApi(); }

    std::string mappingFixture (const char* name)
    {
        return oqatest::fixture (juce::String ("mappings/") + name).loadFileAsString().toStdString();
    }

    std::string slotState (const Instance& instance, int id)
    {
        const auto status = juce::JSON::parse (juce::String (instance.statusJson()));

        if (const auto* slots = status.getProperty ("slots", {}).getArray())
            for (const auto& s : *slots)
                if (static_cast<int> (s.getProperty ("id", -1)) == id)
                    return s.getProperty ("state", "").toString().toStdString();

        return {};
    }
}

//==============================================================================
TEST_CASE ("import: what one instance exports, a fresh one imports identically and exports identically again")
{
    INFO ("locale in effect: " << std::string (oqatest::appliedLocaleName()));

    Rig rig;
    const auto& gain = rig.leafWhere (isRangedFloat);
    const auto& position = rig.leafWhere ([] (const oqa::oscquery::Node& n) { return n.argumentCount() == 3; });

    Instance source (api(), 8, "export-source");
    const int device = source.addDevice (rig.device ("export"));
    std::string why;
    REQUIRE (source.bind (0, gain, 0, device, why));
    source.setDirection (0, Direction::both);
    source.setScaling (0, Scaling::logarithmic);
    source.setRange (0, 0.5, 12.25);
    source.setDisplayName (0, "Gain, overridden");
    REQUIRE_MESSAGE (source.bindAll (2, position, device, why), why);
    source.setDeviceTreeHash (device, rig.tree.treeHash());
    source.setDeviceIdentity (device, "/some/leaf", "SERIAL-1");

    const auto exported = source.saveState();

    Instance target (api(), 8, "export-target");
    std::string error;
    REQUIRE_MESSAGE (target.loadState (exported, error), error);
    CHECK (target.warnings().empty());

    const auto& a = source.chunk();
    const auto& b = target.chunk();
    REQUIRE (b.devices.size() == 1);
    CHECK (b.devices[0].name == a.devices[0].name);
    CHECK (b.devices[0].lastPort == a.devices[0].lastPort);
    CHECK (b.devices[0].treeHash == rig.tree.treeHash());
    CHECK (b.devices[0].identityPath == a.devices[0].identityPath);
    CHECK (b.devices[0].identityValue == a.devices[0].identityValue);
    CHECK (b.slots.boundCount() == a.slots.boundCount());

    for (int i = 0; i < 8; ++i)
    {
        INFO ("slot " << i);
        CHECK (b.slots[i].path == a.slots[i].path);
        CHECK (b.slots[i].argIndex == a.slots[i].argIndex);
        CHECK (b.slots[i].type == a.slots[i].type);
        CHECK (b.slots[i].direction == a.slots[i].direction);
        CHECK (b.slots[i].scaling == a.slots[i].scaling);
        CHECK (b.slots[i].rangeMin == a.slots[i].rangeMin);
        CHECK (b.slots[i].rangeMax == a.slots[i].rangeMax);
        CHECK (b.slots[i].rangeOverridden == a.slots[i].rangeOverridden);
        CHECK (b.slots[i].displayName == a.slots[i].displayName);
    }

    /*  Byte for byte: the file a second DAW reads is the file the first wrote. */
    CHECK (target.saveState() == exported);
}

TEST_CASE ("import: an export from a bigger variant loads what fits, with a warning per slot left out")
{
    Rig rig;
    const auto& gain = rig.leafWhere (isRangedFloat);

    Instance big (api(), 32, "export-big");
    const int device = big.addDevice (rig.device ("export-big"));
    std::string why;
    REQUIRE (big.bind (1, gain, 0, device, why));
    REQUIRE (big.bind (20, gain, 0, device, why));

    Instance small (api(), 8, "export-small");
    std::string error;
    REQUIRE_MESSAGE (small.loadState (big.saveState(), error), error);
    CHECK (small.chunk().slots[1].bound());
    CHECK (small.chunk().slots.boundCount() == 1);
    REQUIRE (small.warnings().size() == 1);
    CHECK (small.warnings()[0].find ("20") != std::string::npos);
}

TEST_CASE ("import: the converter's output from a ToscA mapping loads as bindings addressed by host_param_id")
{
    INFO ("locale in effect: " << std::string (oqatest::appliedLocaleName()));

    const auto expectedText = mappingFixture ("tosca-example.expected.json");
    const auto expected = juce::JSON::parse (juce::String (expectedText));
    REQUIRE (expected.isObject());

    Instance one (api(), 8, "import-tosca");
    std::string error;
    REQUIRE_MESSAGE (one.loadState (expectedText, error), error);
    CHECK (one.warnings().empty());

    const auto& chunk = one.chunk();
    REQUIRE (chunk.devices.size() == 1);
    CHECK (chunk.devices[0].name == expected["devices"][0]["name"].toString().toStdString());
    CHECK (chunk.devices[0].lastPort == static_cast<int> (expected["devices"][0]["last_port"]));

    const auto* slots = expected["slots"].getArray();
    REQUIRE (slots != nullptr);
    CHECK (chunk.slots.boundCount() == static_cast<std::size_t> (slots->size()));

    for (const auto& s : *slots)
    {
        const int id = static_cast<int> (s["host_param_id"]);
        INFO ("slot " << id);
        const auto& slot = chunk.slots[id];
        CHECK (slot.bound());
        CHECK (slot.path == s["path"].toString().toStdString());
        CHECK (slot.displayName == s["display_name"].toString().toStdString());
        CHECK (slot.type == 'f');
        CHECK (slot.direction == Direction::both);
        CHECK (slot.rangeOverridden);
        CHECK (slot.rangeMin == static_cast<double> (s["range"]["min"]));
        CHECK (slot.rangeMax == static_cast<double> (s["range"]["max"]));
        CHECK (std::string (scalingName (slot.scaling)) == s["scaling"].toString().toStdString());
    }

    /*  The ToscA index 6 landed on host_param_id 5, the gap left as the mapping had it. */
    CHECK_FALSE (chunk.slots[4].bound());
    CHECK (chunk.slots[5].bound());
}

TEST_CASE ("import: the converter's output from an OSCar mapping loads, the track's id substituted in every pattern")
{
    const auto expectedText = mappingFixture ("oscar-example.expected.json");
    const auto expected = juce::JSON::parse (juce::String (expectedText));
    REQUIRE (expected.isObject());

    Instance one (api(), 8, "import-oscar");
    std::string error;
    REQUIRE_MESSAGE (one.loadState (expectedText, error), error);
    CHECK (one.warnings().empty());

    const auto* slots = expected["slots"].getArray();
    REQUIRE (slots != nullptr);
    CHECK (one.chunk().slots.boundCount() == static_cast<std::size_t> (slots->size()));

    for (const auto& s : *slots)
    {
        const auto path = s["path"].toString().toStdString();
        CHECK (path.find ('$') == std::string::npos);
        CHECK (one.chunk().slots[static_cast<int> (s["host_param_id"])].path == path);
    }
}

TEST_CASE ("import: a converted mapping against a device that does not serve its paths shows every slot stale, none silent")
{
    Rig rig;
    rig.mock.setName ("Renderer");             // the converter's --device NAME; the paths are the mapping's, not this tree's

    auto imported = juce::JSON::parse (juce::String (mappingFixture ("tosca-example.expected.json")));
    auto* device = imported["devices"].getArray()->getReference (0).getDynamicObject();
    device->setProperty ("last_host", "127.0.0.1");
    device->setProperty ("last_port", rig.mock.httpPort());

    Instance one (api(), 8, "import-stale");
    std::string error;
    REQUIRE_MESSAGE (one.loadState (juce::JSON::toString (imported).toStdString(), error), error);
    one.connect();
    rig.waitReady (one);

    const auto* slots = imported["slots"].getArray();
    REQUIRE (slots != nullptr);

    for (const auto& s : *slots)
    {
        const int id = static_cast<int> (s["host_param_id"]);
        INFO ("slot " << id);
        REQUIRE (waitUntil ([&] { return slotState (one, id) == "missing"; }));
    }
}
