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
    A DEVICE'S TIMING, PROCESS-WIDE (devplan Phase 6a-3; PRD, Transport:
    "a per-device setting the user ticks in the Monitor", "set once per rig"):
    set by hand from one instance (or the Monitor) it stands when another
    instance pushes its chunk, and both chunks carry it on their next save; a
    chunk's own timing is taken until a hand has spoken.
*/

#include <doctest/doctest.h>

#include "RegistryRig.h"
#include "TestSupport.h"

#include <core/Instance.h>
#include <core/StateChunk.h>
#include <core/registry/Registry.h>

#include <string>

using namespace oqa::core;
using namespace oqa::oscquery;
using oqatest::Rig;
using oqatest::isRangedFloat;

namespace
{
    const OqaCoreApi* api() { return oqatest::coreApi(); }

    const bool shutdownRegistered = (oqatest::registerShutdownHook ([] { Registry::shared().shutdown(); }), true);

    juce::var deviceStatus (const Instance& instance)
    {
        return juce::JSON::parse (juce::String (instance.statusJson())).getProperty ("devices", {})[0];
    }

    StateChunk saved (Instance& instance)
    {
        std::string error;
        auto chunk = StateChunk::fromJson (instance.saveState(), instance.slotCount(), error);
        REQUIRE_MESSAGE (chunk.has_value(), error);
        return *chunk;
    }
}

//==============================================================================
TEST_CASE ("timing: set by hand from one instance it stands when another pushes its chunk, and both save it")
{
    Rig rig;
    const auto& gain = rig.leafWhere ([] (const Node& n) { return isRangedFloat (n) && n.argumentCount() == 1; });
    const auto& pan  = rig.leafWhere ([&] (const Node& n) { return isRangedFloat (n) && n.argumentCount() == 1 && n.fullPath != gain.fullPath; });

    Instance a (api(), 8, "timing-a");
    Instance b (api(), 8, "timing-b");
    std::string why;
    REQUIRE (a.bind (0, gain, 0, a.addDevice (rig.device ("timing")), why));
    REQUIRE (b.bind (1, pan, 0, b.addDevice (rig.device ("timing")), why));
    a.connect();
    b.connect();
    rig.waitReady (a);

    /*  A chunk's word, nobody's hand yet. */
    CHECK (deviceStatus (a).getProperty ("timing_by_hand", true) == juce::var (false));
    CHECK (deviceStatus (a).getProperty ("honours_timetags", true) == juce::var (false));

    /*  One hand: the device honours timetags, 12.5 ms early. */
    a.setDeviceTiming (0, true, -12.5);
    CHECK (deviceStatus (a).getProperty ("timing_by_hand", false) == juce::var (true));
    CHECK (deviceStatus (b).getProperty ("honours_timetags", false) == juce::var (true));
    CHECK (static_cast<double> (deviceStatus (b).getProperty ("offset_ms", 0.0)) == doctest::Approx (-12.5));

    /*  The other instance pushes its chunk, whose device says the opposite:
        the hand stands. */
    REQUIRE (b.bind (2, gain, 0, 0, why));
    CHECK (deviceStatus (b).getProperty ("honours_timetags", false) == juce::var (true));
    CHECK (static_cast<double> (deviceStatus (a).getProperty ("offset_ms", 0.0)) == doctest::Approx (-12.5));

    /*  Both chunks carry it on their next save. */
    const auto chunkA = saved (a);
    const auto chunkB = saved (b);
    CHECK (chunkA.devices[0].honoursTimetags);
    CHECK (chunkA.devices[0].offsetMs == doctest::Approx (-12.5));
    CHECK (chunkB.devices[0].honoursTimetags);
    CHECK (chunkB.devices[0].offsetMs == doctest::Approx (-12.5));

    /*  Another hand, from the other side, replaces it. */
    b.setDeviceTiming (0, false, 3.0);
    CHECK (deviceStatus (a).getProperty ("honours_timetags", true) == juce::var (false));
    CHECK (static_cast<double> (deviceStatus (a).getProperty ("offset_ms", 0.0)) == doctest::Approx (3.0));
    CHECK (saved (a).devices[0].offsetMs == doctest::Approx (3.0));
}

TEST_CASE ("timing: through the registry's own call, as the Monitor sets it, with no instance of its own")
{
    Rig rig;
    const auto& gain = rig.leafWhere ([] (const Node& n) { return isRangedFloat (n) && n.argumentCount() == 1; });

    Instance a (api(), 8, "timing-monitor");
    std::string why;
    REQUIRE (a.bind (0, gain, 0, a.addDevice (rig.device ("timing-monitor")), why));
    a.connect();
    rig.waitReady (a);

    CHECK (Registry::shared().setDeviceTiming ("timing-monitor", "", true, 7.25));
    CHECK_FALSE (Registry::shared().setDeviceTiming ("no such device", "", true, 0.0));

    CHECK (deviceStatus (a).getProperty ("timing_by_hand", false) == juce::var (true));
    CHECK (static_cast<double> (deviceStatus (a).getProperty ("offset_ms", 0.0)) == doctest::Approx (7.25));
    CHECK (saved (a).devices[0].honoursTimetags);
    CHECK (saved (a).devices[0].offsetMs == doctest::Approx (7.25));
}
