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
    RE-IDENTIFICATION (devplan Phase 5b; PRD "Reopening a session"), driven
    through core::Instance against mock devices on loopback and a DNS-SD
    responder of the test's own. A session saved against a device on one
    address reopens against the same device on another, found by NAME over
    DNS-SD, every slot live, the new address saved; two devices sharing a NAME
    are told apart by their identity leaf, or by their tree hash; with neither,
    the device is unresolved with both as candidates until pointed at once, and
    every instance follows; a different device at the last address is a
    mismatch that is never driven; a device pointed at by hand is used whatever
    NAME answers, with a warning.

    NO ADDRESS IS TYPED: devices are on loopback at the ports the system gave
    them, and every node is found in the fixture by what it is.
*/

#include <doctest/doctest.h>

#include "RegistryRig.h"
#include "TestSupport.h"
#include "support/MdnsResponder.h"

#include <core/Instance.h>
#include <core/registry/Registry.h>

#include <oscquery/Finder.h>
#include <oscquery/tree/Tree.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace oqa::core;
using namespace oqa::oscquery;
using oqatest::waitUntil;
using oqatest::Rig;
using oqatest::MdnsResponder;
using oqatest::bundlesReceived;
using oqatest::messagesIn;
using oqatest::isRangedFloat;

namespace
{
    const OqaCoreApi* api() { return oqatest::coreApi(); }

    constexpr int resolveTimeoutMs = 20000;
    const std::string loopback = "127.0.0.1";

    void sleepMs (int ms) { std::this_thread::sleep_for (std::chrono::milliseconds (ms)); }

    /** DNS-SD asked of the test's responder only; probes quick. */
    Finder::Options finderVia (int responderPort)
    {
        Finder::Options options;
        options.browse = true;
        options.discovery.port = 0;
        options.discovery.peers = { { loopback, responderPort } };
        options.httpTimeoutMs = 1000;
        options.reprobeMs = 1000;
        options.tickMs = 50;
        return options;
    }

    /** A port nothing answers on: a mock started and stopped. */
    int deadPort()
    {
        oqatest::MockDevice dead;
        REQUIRE (dead.loadTree (juce::JSON::parse (oqatest::fixture ("trees/generic-device.json"))));
        REQUIRE (dead.start());
        const int port = dead.httpPort();
        dead.stop();
        return port;
    }

    std::string resolution (const Instance& instance, int deviceIndex = 0)
    {
        return Rig::deviceStatus (instance, deviceIndex).getProperty ("resolution", "").toString().toStdString();
    }

    int portOf (const Instance& instance, int deviceIndex = 0)
    {
        return static_cast<int> (Rig::deviceStatus (instance, deviceIndex).getProperty ("port", 0));
    }

    int candidateCount (const Instance& instance, int deviceIndex = 0)
    {
        const auto status = Rig::deviceStatus (instance, deviceIndex);
        const auto* list = status.getProperty ("candidates", {}).getArray();
        return list != nullptr ? list->size() : 0;
    }

    bool anyWarningContains (const Instance& instance, const std::string& text)
    {
        const auto status = juce::JSON::parse (juce::String (instance.statusJson()));

        if (const auto* warnings = status.getProperty ("warnings", {}).getArray())
            for (const auto& w : *warnings)
                if (w.toString().toStdString().find (text) != std::string::npos)
                    return true;

        return false;
    }

    int savedPort (Instance& instance, int deviceIndex = 0)
    {
        const auto saved = juce::JSON::parse (juce::String (instance.saveState()));
        return static_cast<int> ((*saved["devices"].getArray())[deviceIndex].getProperty ("last_port", 0));
    }

    bool isIdentityLeaf (const Node& n)
    {
        return n.type == "s" && n.bindableForRecord() && ! n.bindableForSend();
    }

    /** `root` (a `GET /` tree) without the node at `path`; false if absent. */
    bool removeFromTree (juce::var& root, const std::string& path)
    {
        juce::var node = root;
        std::size_t start = 1;

        while (start <= path.size())
        {
            const auto end = path.find ('/', start);
            const auto segment = juce::String (path.substr (start, end == std::string::npos ? std::string::npos : end - start));
            auto* contents = node.getProperty ("CONTENTS", {}).getDynamicObject();

            if (contents == nullptr || ! contents->hasProperty (segment))
                return false;

            if (end == std::string::npos)
            {
                contents->removeProperty (segment);
                return true;
            }

            node = contents->getProperty (segment);
            start = end + 1;
        }

        return false;
    }
}

//==============================================================================
TEST_CASE ("reidentify: a session saved against a device on one address reopens against it on another, found by NAME over DNS-SD")
{
    Rig rig;
    const auto& gain = rig.leafWhere (isRangedFloat);
    const std::string name = "Moved Device";

    std::string chunk;

    {
        Instance bound (api(), 4, "reid-move-bind");
        std::string why;
        REQUIRE (bound.bind (0, gain, 0, bound.addDevice (rig.device (name)), why));
        chunk = bound.saveState();
    }

    /*  The device comes up on another port. */
    const int before = rig.mock.httpPort();
    rig.mock.stop();
    REQUIRE (rig.mock.start());
    int after = rig.mock.httpPort();

    for (int tries = 0; after == before && tries < 5; ++tries)
    {
        rig.mock.stop();
        REQUIRE (rig.mock.start());
        after = rig.mock.httpPort();
    }

    REQUIRE (after != before);

    MdnsResponder responder ({ { name, "moved-pc", after } });
    Registry::shared().setFinderOptions (finderVia (responder.port()));

    Instance one (api(), 4, "reid-move");
    std::string error;
    REQUIRE_MESSAGE (one.loadState (chunk, error), error);
    CHECK (one.chunk().devices[0].lastPort == before);
    one.connect();

    REQUIRE (waitUntil ([&] { return resolution (one) == "confirmed" && portOf (one) == after; }, resolveTimeoutMs));
    rig.waitReady (one);
    rig.waitListening (one);

    float values[4] = { 0.4f, 0.0f, 0.0f, 0.0f };
    one.processBlock (values, 4);
    REQUIRE (waitUntil ([&] { return messagesIn (bundlesReceived (rig.mock)) >= 1; }));

    /*  The next save carries where the device is now. */
    CHECK (savedPort (one) == after);
    CHECK (one.chunk().devices[0].lastPort == after);
}

TEST_CASE ("reidentify: two devices sharing a NAME are told apart by the identity leaf")
{
    const std::string name = "Twin";
    Rig a, b;
    a.mock.setName (name);
    b.mock.setName (name);

    const auto& gain = a.leafWhere (isRangedFloat);
    const auto& serial = a.leafWhere (isIdentityLeaf);
    REQUIRE (a.mock.setValue (serial.fullPath, { "TWIN-A" }));
    REQUIRE (b.mock.setValue (serial.fullPath, { "TWIN-B" }));

    /*  Bonjour would have named the second instance "(2)": two instances, one NAME. */
    MdnsResponder responder ({ { name, "twin-a", a.mock.httpPort() }, { name + " (2)", "twin-b", b.mock.httpPort() } });
    Registry::shared().setFinderOptions (finderVia (responder.port()));

    DeviceRef ref;
    ref.name = name;
    ref.lastHost = loopback;
    ref.lastPort = deadPort();
    ref.identityPath = serial.fullPath;
    ref.identityValue = "TWIN-B";

    Instance one (api(), 4, "reid-twin");
    const int device = one.addDevice (ref);
    std::string why;
    REQUIRE (one.bind (0, gain, 0, device, why));
    one.connect();

    REQUIRE (waitUntil ([&] { return resolution (one) == "confirmed"; }, resolveTimeoutMs));
    CHECK (portOf (one) == b.mock.httpPort());
    CHECK (Rig::deviceStatus (one).getProperty ("identity_live", "").toString() == "TWIN-B");
    b.waitReady (one);

    float values[4] = { 0.4f, 0.0f, 0.0f, 0.0f };
    one.processBlock (values, 4);
    REQUIRE (waitUntil ([&] { return messagesIn (bundlesReceived (b.mock)) >= 1; }));
    sleepMs (100);
    CHECK (a.mock.receivedOsc().empty());
}

TEST_CASE ("reidentify: two devices sharing a NAME and no identity leaf are unresolved, with both as candidates, until pointed at once; every instance follows")
{
    const std::string name = "Twins Alike";
    Rig a, b;
    a.mock.setName (name);
    b.mock.setName (name);
    const auto& gain = a.leafWhere (isRangedFloat);

    MdnsResponder responder ({ { name, "alike-a", a.mock.httpPort() }, { name + " (2)", "alike-b", b.mock.httpPort() } });
    Registry::shared().setFinderOptions (finderVia (responder.port()));

    DeviceRef ref;
    ref.name = name;
    ref.lastHost = loopback;
    ref.lastPort = deadPort();

    Instance x (api(), 4, "reid-alike-x");
    Instance y (api(), 4, "reid-alike-y");
    std::string why;
    const int dx = x.addDevice (ref);
    const int dy = y.addDevice (ref);
    REQUIRE (x.bind (0, gain, 0, dx, why));
    REQUIRE (y.bind (0, gain, 0, dy, why));
    x.connect();
    y.connect();

    REQUIRE (waitUntil ([&] { return resolution (x) == "unresolved" && candidateCount (x) == 2; }, resolveTimeoutMs));
    CHECK (resolution (y) == "unresolved");
    CHECK (anyWarningContains (x, "2 found under its name"));

    /*  Both are in the Finder's list, for the editor to offer. */
    {
        const auto status = juce::JSON::parse (juce::String (x.statusJson()));
        const auto* found = status.getProperty ("found", {}).getArray();
        REQUIRE (found != nullptr);
        int ours = 0;

        for (const auto& f : *found)
            if (f.getProperty ("name", "").toString() == juce::String (name))
                ++ours;

        CHECK (ours == 2);
    }

    /*  Nothing is driven while unresolved. */
    float values[4] = { 0.4f, 0.0f, 0.0f, 0.0f };
    x.processBlock (values, 4);
    sleepMs (100);
    CHECK (a.mock.receivedOsc().empty());
    CHECK (b.mock.receivedOsc().empty());

    /*  Pointed once from one instance: the other follows. */
    REQUIRE (x.pointDevice (dx, loopback, b.mock.httpPort()));
    REQUIRE (waitUntil ([&] { return resolution (x) == "pointed" && resolution (y) == "pointed"; }, resolveTimeoutMs));
    b.waitReady (x);
    b.waitReady (y);

    values[0] = 0.6f;
    x.processBlock (values, 4);
    values[0] = 0.7f;
    y.processBlock (values, 4);
    REQUIRE (waitUntil ([&] { return messagesIn (bundlesReceived (b.mock)) >= 2; }));
    CHECK (a.mock.receivedOsc().empty());

    /*  The instance that did not point saves the pointed address too. */
    CHECK (savedPort (y, dy) == b.mock.httpPort());
}

TEST_CASE ("reidentify: a different device at the last address is a mismatch: nothing sent, nothing LISTENed")
{
    Rig other;
    other.mock.setName ("Other Thing");
    const auto& gain = other.leafWhere (isRangedFloat);

    MdnsResponder responder ({});
    Registry::shared().setFinderOptions (finderVia (responder.port()));

    DeviceRef ref;
    ref.name = "Expected Device";
    ref.lastHost = loopback;
    ref.lastPort = other.mock.httpPort();

    Instance one (api(), 4, "reid-mismatch");
    const int device = one.addDevice (ref);
    std::string why;
    REQUIRE (one.bind (0, gain, 0, device, why));
    one.connect();

    REQUIRE (waitUntil ([&] { return resolution (one) == "mismatch"; }, resolveTimeoutMs));
    const auto status = Rig::deviceStatus (one);
    CHECK (status.getProperty ("answered_name", "").toString() == "Other Thing");
    CHECK_FALSE (static_cast<bool> (status.getProperty ("sender_ready", true)));
    CHECK (anyWarningContains (one, "answers as 'Other Thing'"));

    float values[4] = { 0.4f, 0.0f, 0.0f, 0.0f };
    one.processBlock (values, 4);
    sleepMs (200);
    CHECK (other.mock.receivedOsc().empty());
    CHECK (other.mock.listenerCount (gain.fullPath) == 0);

    /*  The slot says why it waits. */
    const auto s = juce::JSON::parse (juce::String (one.statusJson()));
    const auto* slots = s.getProperty ("slots", {}).getArray();
    REQUIRE (slots != nullptr);
    REQUIRE (slots->size() == 1);
    CHECK ((*slots)[0].getProperty ("state", "").toString() == "pending");
    CHECK ((*slots)[0].getProperty ("detail", "").toString().contains ("Other Thing"));
}

TEST_CASE ("reidentify: a device pointed at by hand is used whatever NAME answers, with a warning")
{
    Rig other;
    other.mock.setName ("Renamed At The Venue");
    const auto& gain = other.leafWhere (isRangedFloat);

    MdnsResponder responder ({});
    Registry::shared().setFinderOptions (finderVia (responder.port()));

    DeviceRef ref;
    ref.name = "Studio Name";
    ref.lastHost = loopback;
    ref.lastPort = deadPort();

    Instance one (api(), 4, "reid-hand");
    const int device = one.addDevice (ref);
    std::string why;
    REQUIRE (one.bind (0, gain, 0, device, why));
    one.connect();
    REQUIRE (waitUntil ([&] { return resolution (one) == "searching"; }, resolveTimeoutMs));

    REQUIRE (one.pointDevice (device, loopback, other.mock.httpPort()));
    REQUIRE (waitUntil ([&] { return resolution (one) == "pointed"; }, resolveTimeoutMs));
    other.waitReady (one);
    CHECK (anyWarningContains (one, "answers as 'Renamed At The Venue'"));
    CHECK (Rig::deviceStatus (one).getProperty ("answered_name", "").toString() == "Renamed At The Venue");

    float values[4] = { 0.4f, 0.0f, 0.0f, 0.0f };
    one.processBlock (values, 4);
    REQUIRE (waitUntil ([&] { return messagesIn (bundlesReceived (other.mock)) >= 1; }));

    /*  The chunk keeps its name; the address is the pointed one. */
    CHECK (one.chunk().devices[0].name == "Studio Name");
    CHECK (savedPort (one) == other.mock.httpPort());
}

TEST_CASE ("reidentify: two devices sharing a NAME and no identity leaf are told apart by the tree hash when the chunk has one")
{
    const std::string name = "Hashed Twins";
    std::string parseError;

    Rig a;
    a.mock.setName (name);
    const auto& gain = a.leafWhere (isRangedFloat);

    /*  The other serves the fixture less one leaf. */
    auto root = juce::JSON::parse (oqatest::fixture ("trees/generic-device.json"));
    std::vector<const Node*> floats;

    for (const auto* leaf : a.tree.leaves())
        if (isRangedFloat (*leaf))
            floats.push_back (leaf);

    REQUIRE (floats.size() >= 2);
    REQUIRE (removeFromTree (root, floats[1]->fullPath));
    Rig b ({}, root);
    b.mock.setName (name);
    REQUIRE (a.tree.treeHash() != b.tree.treeHash());

    MdnsResponder responder ({ { name, "hashed-a", a.mock.httpPort() }, { name + " (2)", "hashed-b", b.mock.httpPort() } });
    Registry::shared().setFinderOptions (finderVia (responder.port()));

    DeviceRef ref;
    ref.name = name;
    ref.lastHost = loopback;
    ref.lastPort = deadPort();
    ref.treeHash = a.tree.treeHash();

    Instance one (api(), 4, "reid-hash");
    const int device = one.addDevice (ref);
    std::string why;
    REQUIRE (one.bind (0, gain, 0, device, why));
    one.connect();

    REQUIRE (waitUntil ([&] { return resolution (one) == "confirmed"; }, resolveTimeoutMs));
    CHECK (portOf (one) == a.mock.httpPort());

    const auto status = Rig::deviceStatus (one);
    const auto* candidates = status.getProperty ("candidates", {}).getArray();
    REQUIRE (candidates != nullptr);
    CHECK (candidates->size() == 2);
    int matches = 0;

    for (const auto& c : *candidates)
        if (static_cast<bool> (c.getProperty ("hash_match", false)))
            ++matches;

    CHECK (matches == 1);
    a.waitReady (one);
    CHECK_FALSE (static_cast<bool> (Rig::deviceStatus (one).getProperty ("tree_changed", true)));
}
