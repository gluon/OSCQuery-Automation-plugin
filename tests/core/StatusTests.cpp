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
    THE MONITOR'S DATA (devplan Phase 6a-2; its done-when: "the Monitor shows 32
    instances against two devices with live counters"): the process-wide status
    lists every instance with its bound slots and their standing, the devices
    carry rates over the last second, a bound slot carries the device's own
    RANGE, and an instance the host has stopped stays registered, inactive,
    with its id.
*/

#include <doctest/doctest.h>

#include "RegistryRig.h"
#include "TestSupport.h"

#include <core/Instance.h>
#include <core/registry/Registry.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace oqa::core;
using namespace oqa::oscquery;
using oqatest::Rig;
using oqatest::waitUntil;
using oqatest::isRangedFloat;

namespace
{
    const OqaCoreApi* api() { return oqatest::coreApi(); }

    const bool shutdownRegistered = (oqatest::registerShutdownHook ([] { Registry::shared().shutdown(); }), true);

    juce::var processStatus()
    {
        return Registry::shared().status();
    }

    juce::var instanceWithId (const juce::var& status, juce::int64 id)
    {
        if (const auto* instances = status.getProperty ("instances", {}).getArray())
            for (const auto& i : *instances)
                if (static_cast<juce::int64> (i.getProperty ("id", -1)) == id)
                    return i;

        return {};
    }

    juce::var deviceNamed (const juce::var& status, const std::string& name)
    {
        if (const auto* devices = status.getProperty ("devices", {}).getArray())
            for (const auto& d : *devices)
                if (d.getProperty ("name", "").toString().toStdString() == name)
                    return d;

        return {};
    }
}

//==============================================================================
TEST_CASE ("status: 32 instances against two devices are listed with their bound slots, and both devices show live rates")
{
    Rig rigA;
    Rig rigB;
    const auto& gainA = rigA.leafWhere ([] (const Node& n) { return isRangedFloat (n) && n.argumentCount() == 1 && ! n.unit.empty() && ! n.unit[0].empty(); });
    const auto& panB  = rigB.leafWhere ([&] (const Node& n) { return isRangedFloat (n) && n.argumentCount() == 1 && n.fullPath != gainA.fullPath; });

    const auto refA = rigA.device ("monitor-A");
    const auto refB = rigB.device ("monitor-B");

    std::vector<std::unique_ptr<Instance>> instances;

    for (int i = 0; i < 32; ++i)
    {
        auto instance = std::make_unique<Instance> (api(), 8, "monitor #" + std::to_string (i + 1));
        std::string why;
        const bool toA = i % 2 == 0;
        REQUIRE (instance->bind (i % 8, toA ? gainA : panB, 0, instance->addDevice (toA ? refA : refB), why));
        instance->setDisplayName (i % 8, "lane " + std::to_string (i));
        instance->connect();
        instances.push_back (std::move (instance));
    }

    rigA.waitReady (*instances[0]);
    rigB.waitReady (*instances[1]);

    /*  Blocks at 100 Hz, every lane moving, until the resolver's tick has
        sampled a second of them on both devices. */
    float values[8] = {};
    int block = 0;

    const bool ratesUp = waitUntil ([&]
    {
        ++block;

        for (std::size_t i = 0; i < instances.size(); ++i)
        {
            values[i % 8] = static_cast<float> ((block + static_cast<int> (i)) % 100) / 100.0f;
            instances[i]->processBlock (values, 8);
        }

        std::this_thread::sleep_for (std::chrono::milliseconds (10));

        const auto status = processStatus();
        const auto a = deviceNamed (status, "monitor-A").getProperty ("rates", {});
        const auto b = deviceNamed (status, "monitor-B").getProperty ("rates", {});
        return static_cast<double> (a.getProperty ("bundles_s", 0.0)) > 10.0
            && static_cast<double> (b.getProperty ("bundles_s", 0.0)) > 10.0;
    }, 8000);

    REQUIRE (ratesUp);

    const auto status = processStatus();
    CHECK (static_cast<int> (status.getProperty ("instance_count", 0)) == 32);
    const auto* list = status.getProperty ("instances", {}).getArray();
    REQUIRE (list != nullptr);
    REQUIRE (list->size() == 32);

    const auto a = deviceNamed (status, "monitor-A");
    CHECK (static_cast<double> (a.getProperty ("rates", {}).getProperty ("messages_s", 0.0)) > 10.0);
    CHECK (static_cast<juce::int64> (a.getProperty ("last_error_ms_ago", 0)) == -1);

    int withA = 0, withB = 0;

    for (const auto& i : *list)
    {
        CHECK (i.getProperty ("label", "").toString().startsWith ("monitor #"));
        CHECK (static_cast<int> (i.getProperty ("slot_count", 0)) == 8);
        CHECK (i.getProperty ("active", false) == juce::var (true));

        const auto* slots = i.getProperty ("slots", {}).getArray();
        REQUIRE (slots != nullptr);
        REQUIRE (slots->size() == 1);

        const auto& s = (*slots)[0];
        CHECK (s.getProperty ("state", "").toString() == "ok");
        CHECK (s.getProperty ("display_name", "").toString().startsWith ("lane "));
        CHECK (s.getProperty ("direction", "").toString() == "send");
        CHECK (s.getProperty ("type", "").toString() == "f");
        CHECK (s.hasProperty ("device_range_min"));

        const auto device = s.getProperty ("device", "").toString();

        if (device == "monitor-A")
        {
            ++withA;
            CHECK (s.getProperty ("path", "").toString().toStdString() == gainA.fullPath);
            CHECK (static_cast<double> (s.getProperty ("device_range_min", 1e9)) == doctest::Approx (*gainA.argument (0).range->min));
            CHECK (static_cast<double> (s.getProperty ("device_range_max", 1e9)) == doctest::Approx (*gainA.argument (0).range->max));
        }
        else if (device == "monitor-B")
            ++withB;
    }

    CHECK (withA == 16);
    CHECK (withB == 16);

    /*  The same through an instance, for the Monitor view. */
    const auto viaInstance = juce::JSON::parse (juce::String (instances[0]->processStatusJson()));
    CHECK (viaInstance.getProperty ("instances", {}).size() == 32);
}

TEST_CASE ("status: an instance the host has stopped stays registered, inactive, with its id; a user-overridden range keeps the device's beside it")
{
    Rig rig;
    const auto& gain = rig.leafWhere ([] (const Node& n) { return isRangedFloat (n) && n.argumentCount() == 1; });

    Instance a (api(), 8, "stop-and-go");
    std::string why;
    REQUIRE (a.bind (2, gain, 0, a.addDevice (rig.device ("stop-and-go")), why));
    a.connect();
    rig.waitReady (a);

    auto status = juce::JSON::parse (juce::String (a.statusJson()));
    CHECK (status.getProperty ("active", false) == juce::var (true));

    const auto* slots = status.getProperty ("slots", {}).getArray();
    REQUIRE (slots != nullptr);
    REQUIRE (slots->size() == 1);
    CHECK (static_cast<double> ((*slots)[0].getProperty ("device_range_min", 1e9)) == doctest::Approx (*gain.argument (0).range->min));

    /*  The id is the row's identity: found once, the same after a stop. */
    juce::int64 id = -1;
    const auto whole = processStatus();     // held: an array pointer from a temporary var dangles

    for (const auto& i : *whole.getProperty ("instances", {}).getArray())
        if (i.getProperty ("label", "").toString() == "stop-and-go")
            id = static_cast<juce::int64> (i.getProperty ("id", -1));

    REQUIRE (id >= 0);

    a.setActive (false);
    CHECK (juce::JSON::parse (juce::String (a.statusJson())).getProperty ("active", true) == juce::var (false));
    CHECK (instanceWithId (processStatus(), id).getProperty ("active", true) == juce::var (false));
    CHECK (instanceWithId (processStatus(), id).getProperty ("label", "").toString() == "stop-and-go");

    a.connect();
    a.setActive (true);
    CHECK (instanceWithId (processStatus(), id).getProperty ("active", false) == juce::var (true));

    /*  A range the user set: the slot's own in the chunk, the device's still
        beside it in the status. */
    a.setRange (2, -3.0, 3.0);
    CHECK (a.chunk().slots[2].rangeOverridden);
    status = juce::JSON::parse (juce::String (a.statusJson()));
    CHECK (static_cast<double> (status.getProperty ("slots", {})[0].getProperty ("device_range_max", -1e9)) == doctest::Approx (*gain.argument (0).range->max));
}
