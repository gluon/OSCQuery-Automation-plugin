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
    THE MOCK DEVICE, READ BACK (devplan Phase 0, done-when: "the mock device
    serves a fixture tree and a test reads it back").

    NO ADDRESS IS TYPED IN THIS FILE. Every path is found in the loaded fixture
    by what the node is (a ranged leaf, a write-only leaf, a container), so the
    same cases hold against any other fixture with those kinds of node. Run
    under C and under fr_FR, like the whole suite: RANGE and VALUE are decimals.
*/

#include <doctest/doctest.h>

#include "../TestSupport.h"
#include "../support/HttpGet.h"
#include "../support/WsTestClient.h"
#include "MockDevice.h"

#include <cstring>
#include <functional>
#include <vector>

namespace
{
    using oqatest::MockDevice;

    juce::var loadFixture()
    {
        return juce::JSON::parse (oqatest::fixture ("trees/generic-device.json"));
    }

    //  Every node of a tree, root first, depth first, in the tree's own order.
    void collect (const juce::var& node, std::vector<juce::var>& out)
    {
        out.push_back (node);

        if (auto* contents = node.getProperty ("CONTENTS", {}).getDynamicObject())
            for (const auto& child : contents->getProperties())
                collect (child.value, out);
    }

    std::vector<juce::var> nodesOf (const juce::var& root)
    {
        std::vector<juce::var> nodes;
        collect (root, nodes);
        return nodes;
    }

    juce::String pathOf (const juce::var& node)
    {
        return node.getProperty ("FULL_PATH", {}).toString();
    }

    int accessOf (const juce::var& node)
    {
        return static_cast<int> (node.getProperty ("ACCESS", 3));
    }

    bool isLeaf (const juce::var& node) { return node.hasProperty ("TYPE"); }
    bool isContainer (const juce::var& node) { return ! isLeaf (node) && node.hasProperty ("CONTENTS"); }

    //  The first node of the fixture matching `what`; fails the case if none does.
    juce::var first (const juce::var& root, const std::function<bool (const juce::var&)>& what,
                     const char* description)
    {
        for (const auto& node : nodesOf (root))
            if (what (node))
                return node;

        FAIL ("the fixture has no " << description);
        return {};
    }

    //  JSON compared as JSON: both sides through the same parser and writer, so a
    //  difference is a difference in content, not in spacing.
    std::string canonical (const juce::var& value)
    {
        return juce::JSON::toString (value, true).toStdString();
    }

    std::string canonical (const std::string& body)
    {
        return canonical (juce::JSON::parse (juce::String (body)));
    }

    std::string target (const juce::String& path, const juce::String& query = {})
    {
        return (query.isEmpty() ? path : path + "?" + query).toStdString();
    }

    juce::String commandJson (const juce::String& name, const juce::String& path)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("COMMAND", name);
        object->setProperty ("DATA", path);
        return juce::JSON::toString (juce::var (object), true);
    }

    //  A started device holding the fixture.
    struct Rig
    {
        juce::var fixture { loadFixture() };
        MockDevice device;
        bool started = false;

        explicit Rig (MockDevice::Options options = {}) : device (std::move (options))
        {
            started = device.loadTree (fixture) && device.start();
        }

        int port() const { return device.httpPort(); }
    };

    //  The OSC string at `offset`, and the offset after its padding.
    std::string oscStringAt (const std::vector<std::uint8_t>& bytes, std::size_t& offset)
    {
        std::string text;

        while (offset < bytes.size() && bytes[offset] != 0)
            text.push_back (static_cast<char> (bytes[offset++]));

        offset = (offset + 4) & ~static_cast<std::size_t> (3);
        return text;
    }

    float oscFloatAt (const std::vector<std::uint8_t>& bytes, std::size_t offset)
    {
        const std::uint32_t bits = (std::uint32_t (bytes[offset]) << 24) | (std::uint32_t (bytes[offset + 1]) << 16)
                                   | (std::uint32_t (bytes[offset + 2]) << 8) | std::uint32_t (bytes[offset + 3]);
        float value = 0.0f;
        std::memcpy (&value, &bits, sizeof (value));
        return value;
    }

    juce::var lastCommand (oqatest::WsTestClient& client)
    {
        const auto frames = client.textFrames();
        return frames.empty() ? juce::var() : juce::JSON::parse (frames.back());
    }
}

//==============================================================================
TEST_CASE ("mock device: HOST_INFO names the device and its real ports")
{
    Rig rig;
    REQUIRE (rig.started);

    const auto reply = oqatest::get (rig.port(), "/?HOST_INFO");
    REQUIRE (reply.status == 200);
    CHECK (reply.header ("Content-Type") == "application/json");

    const auto info = juce::JSON::parse (juce::String (reply.body));
    CHECK (info["NAME"].toString() == rig.device.options().name);
    CHECK (rig.device.oscPort() > 0);
    CHECK (static_cast<int> (info["OSC_PORT"]) == rig.device.oscPort());
    CHECK (info["OSC_TRANSPORT"].toString() == "UDP");
    CHECK (static_cast<int> (info["WS_PORT"]) == rig.port());
    CHECK (static_cast<bool> (info["EXTENSIONS"]["LISTEN"]));

    //  "The path in the URL is functionally irrelevant" (the proposal).
    const auto container = first (rig.fixture, [] (auto& n) { return isContainer (n) && pathOf (n) != "/"; },
                                  "container below the root");
    const auto elsewhere = oqatest::get (rig.port(), target (pathOf (container), "HOST_INFO"));
    CHECK (canonical (elsewhere.body) == canonical (reply.body));
}

TEST_CASE ("mock device: GET / serves the fixture, and every node its own subtree")
{
    Rig rig;
    REQUIRE (rig.started);

    const auto nodes = nodesOf (rig.fixture);
    REQUIRE (nodes.size() > 10);

    for (const auto& node : nodes)
    {
        const auto path = pathOf (node);
        INFO ("GET " << path.toStdString());

        const auto reply = oqatest::get (rig.port(), target (path));
        REQUIRE (reply.status == 200);
        CHECK (reply.header ("Content-Type") == "application/json");
        CHECK (canonical (reply.body) == canonical (node));
    }
}

TEST_CASE ("mock device: unknown paths are 404; attribute queries 200, 204 or 400")
{
    Rig rig;
    REQUIRE (rig.started);

    const auto ranged = first (rig.fixture, [] (auto& n) { return isLeaf (n) && n.hasProperty ("RANGE") && (accessOf (n) & 1); },
                               "readable leaf with RANGE");
    const auto unranged = first (rig.fixture, [] (auto& n) { return isLeaf (n) && ! n.hasProperty ("RANGE"); },
                                 "leaf without RANGE");
    const auto writeOnly = first (rig.fixture, [] (auto& n) { return isLeaf (n) && accessOf (n) == 2; },
                                  "write-only leaf");

    CHECK (oqatest::get (rig.port(), target (pathOf (ranged) + "/absent")).status == 404);

    const auto range = oqatest::get (rig.port(), target (pathOf (ranged), "RANGE"));
    REQUIRE (range.status == 200);
    auto* expected = new juce::DynamicObject();
    expected->setProperty ("RANGE", ranged["RANGE"]);
    CHECK (canonical (range.body) == canonical (juce::var (expected)));

    const auto noRange = oqatest::get (rig.port(), target (pathOf (unranged), "RANGE"));
    CHECK (noRange.status == 204);
    CHECK (noRange.body.empty());

    CHECK (oqatest::get (rig.port(), target (pathOf (writeOnly), "VALUE")).status == 204);
    CHECK (oqatest::get (rig.port(), target (pathOf (ranged), "NOT_AN_ATTRIBUTE")).status == 400);
    CHECK (oqatest::get (rig.port(), target (pathOf (ranged) + "/absent", "VALUE")).status == 404);
}

TEST_CASE ("mock device: LISTEN streams a value as one OSC message, IGNORE stops it")
{
    Rig rig;
    REQUIRE (rig.started);

    const auto single = first (rig.fixture, [] (auto& n) { return n["TYPE"].toString() == "f" && accessOf (n) == 3; },
                               "read-write f leaf");
    const auto triple = first (rig.fixture, [] (auto& n) { return n["TYPE"].toString() == "fff"; },
                               "fff leaf");

    oqatest::WsTestClient client;
    REQUIRE (client.connect (rig.port()));
    REQUIRE (oqatest::waitUntil ([&] { return rig.device.connectionCount() == 1; }));

    client.sendText (commandJson ("LISTEN", pathOf (single)));
    client.sendText (commandJson ("LISTEN", pathOf (triple)));
    REQUIRE (oqatest::waitUntil ([&] { return rig.device.listenerCount (pathOf (single)) == 1
                                            && rig.device.listenerCount (pathOf (triple)) == 1; }));

    SUBCASE ("one float")
    {
        REQUIRE (rig.device.setValue (pathOf (single), { 0.25 }));
        REQUIRE (oqatest::waitUntil ([&] { return client.binaryCount() == 1; }));

        const auto frame = client.binaryFrames().front();
        std::size_t offset = 0;
        CHECK (oscStringAt (frame, offset) == pathOf (single).toStdString());
        CHECK (oscStringAt (frame, offset) == ",f");
        REQUIRE (frame.size() == offset + 4);
        CHECK (oscFloatAt (frame, offset) == 0.25f);

        CHECK (static_cast<double> (rig.device.node (pathOf (single))["VALUE"][0]) == doctest::Approx (0.25));
    }

    SUBCASE ("three floats in one message")
    {
        REQUIRE (rig.device.setValue (pathOf (triple), { 1.5, -2.0, 3.25 }));
        REQUIRE (oqatest::waitUntil ([&] { return client.binaryCount() == 1; }));

        const auto frame = client.binaryFrames().front();
        std::size_t offset = 0;
        CHECK (oscStringAt (frame, offset) == pathOf (triple).toStdString());
        CHECK (oscStringAt (frame, offset) == ",fff");
        REQUIRE (frame.size() == offset + 12);
        CHECK (oscFloatAt (frame, offset) == 1.5f);
        CHECK (oscFloatAt (frame, offset + 4) == -2.0f);
        CHECK (oscFloatAt (frame, offset + 8) == 3.25f);
    }

    SUBCASE ("IGNORE")
    {
        client.sendText (commandJson ("IGNORE", pathOf (single)));
        REQUIRE (oqatest::waitUntil ([&] { return rig.device.listenerCount (pathOf (single)) == 0; }));

        REQUIRE (rig.device.setValue (pathOf (single), { 0.5 }));
        juce::Thread::sleep (200);
        CHECK (client.binaryCount() == 0);
    }
}

TEST_CASE ("mock device: rename, remove, retype and add are announced and then served")
{
    Rig rig;
    REQUIRE (rig.started);

    oqatest::WsTestClient client;
    REQUIRE (client.connect (rig.port()));
    REQUIRE (oqatest::waitUntil ([&] { return rig.device.connectionCount() == 1; }));

    const auto leaves = [&]
    {
        std::vector<juce::var> found;
        for (const auto& node : nodesOf (rig.fixture))
            if (isLeaf (node) && node["TYPE"].toString() == "f")
                found.push_back (node);
        return found;
    }();
    REQUIRE (leaves.size() >= 3);

    SUBCASE ("rename a leaf")
    {
        const auto oldPath = pathOf (leaves[0]);
        const auto newPath = oldPath + "-moved";

        REQUIRE (rig.device.rename (oldPath, newPath));
        REQUIRE (oqatest::waitUntil ([&] { return client.textCount() == 1; }));

        const auto told = lastCommand (client);
        CHECK (told["COMMAND"].toString() == "PATH_RENAMED");
        CHECK (told["DATA"]["OLD"].toString() == oldPath);
        CHECK (told["DATA"]["NEW"].toString() == newPath);

        CHECK (oqatest::get (rig.port(), target (oldPath)).status == 404);
        const auto moved = oqatest::get (rig.port(), target (newPath));
        REQUIRE (moved.status == 200);
        CHECK (juce::JSON::parse (juce::String (moved.body))["FULL_PATH"].toString() == newPath);
    }

    SUBCASE ("rename a container: FULL_PATH follows down the subtree")
    {
        const auto container = first (rig.fixture, [] (auto& n) { return isContainer (n) && pathOf (n) != "/"; },
                                      "container below the root");
        const auto oldPath = pathOf (container);
        const auto newPath = oldPath + "-moved";

        REQUIRE (rig.device.rename (oldPath, newPath));

        for (const auto& node : nodesOf (container))
        {
            const auto movedPath = newPath + pathOf (node).substring (oldPath.length());
            const auto reply = oqatest::get (rig.port(), target (movedPath));
            INFO ("GET " << movedPath.toStdString());
            REQUIRE (reply.status == 200);
            CHECK (juce::JSON::parse (juce::String (reply.body))["FULL_PATH"].toString() == movedPath);
        }
    }

    SUBCASE ("remove")
    {
        const auto path = pathOf (leaves[1]);

        REQUIRE (rig.device.remove (path));
        REQUIRE (oqatest::waitUntil ([&] { return client.textCount() == 1; }));

        const auto told = lastCommand (client);
        CHECK (told["COMMAND"].toString() == "PATH_REMOVED");
        CHECK (told["DATA"].toString() == path);
        CHECK (oqatest::get (rig.port(), target (path)).status == 404);
    }

    SUBCASE ("retype")
    {
        const auto path = pathOf (leaves[2]);

        REQUIRE (rig.device.retype (path, "i"));
        REQUIRE (oqatest::waitUntil ([&] { return client.textCount() == 1; }));

        const auto told = lastCommand (client);
        CHECK (told["COMMAND"].toString() == "PATH_CHANGED");
        CHECK (told["DATA"].toString() == path);

        const auto type = oqatest::get (rig.port(), target (path, "TYPE"));
        REQUIRE (type.status == 200);
        CHECK (juce::JSON::parse (juce::String (type.body))["TYPE"].toString() == "i");
    }

    SUBCASE ("add")
    {
        const auto parent = first (rig.fixture, [] (auto& n) { return isContainer (n) && pathOf (n) != "/"; },
                                   "container below the root");
        const auto path = pathOf (parent) + "/added";

        REQUIRE (rig.device.add (path, leaves[0]));
        REQUIRE (oqatest::waitUntil ([&] { return client.textCount() == 1; }));

        const auto told = lastCommand (client);
        CHECK (told["COMMAND"].toString() == "PATH_ADDED");
        CHECK (told["DATA"].toString() == path);

        const auto added = oqatest::get (rig.port(), target (path));
        REQUIRE (added.status == 200);
        CHECK (juce::JSON::parse (juce::String (added.body))["FULL_PATH"].toString() == path);
    }

    SUBCASE ("a change to a path that does not exist changes nothing and says nothing")
    {
        const auto absent = pathOf (leaves[0]) + "/absent";
        CHECK_FALSE (rig.device.remove (absent));
        CHECK_FALSE (rig.device.retype (absent, "i"));
        CHECK_FALSE (rig.device.rename (absent, absent + "-moved"));
        juce::Thread::sleep (200);
        CHECK (client.textCount() == 0);
    }
}

TEST_CASE ("mock device: a notification not listed in EXTENSIONS is not sent")
{
    MockDevice::Options options;
    options.extensions.removeString ("PATH_REMOVED");

    Rig rig (options);
    REQUIRE (rig.started);

    const auto info = juce::JSON::parse (juce::String (oqatest::get (rig.port(), "/?HOST_INFO").body));
    CHECK_FALSE (info["EXTENSIONS"].hasProperty ("PATH_REMOVED"));

    oqatest::WsTestClient client;
    REQUIRE (client.connect (rig.port()));
    REQUIRE (oqatest::waitUntil ([&] { return rig.device.connectionCount() == 1; }));

    const auto leaf = first (rig.fixture, [] (auto& n) { return isLeaf (n); }, "leaf");
    REQUIRE (rig.device.remove (pathOf (leaf)));
    juce::Thread::sleep (200);
    CHECK (client.textCount() == 0);
    CHECK (oqatest::get (rig.port(), target (pathOf (leaf))).status == 404);
}

TEST_CASE ("mock device: without a websocket, HOST_INFO has no WS_PORT")
{
    MockDevice::Options options;
    options.websocket = false;

    Rig rig (options);
    REQUIRE (rig.started);

    const auto info = juce::JSON::parse (juce::String (oqatest::get (rig.port(), "/?HOST_INFO").body));
    CHECK_FALSE (info.hasProperty ("WS_PORT"));
    CHECK (oqatest::get (rig.port(), "/").status == 200);
}

TEST_CASE ("mock device: stops, and starts again on a fresh port")
{
    Rig rig;
    REQUIRE (rig.started);
    CHECK (oqatest::get (rig.port(), "/").status == 200);

    rig.device.stop();
    CHECK_FALSE (rig.device.isRunning());
    CHECK (rig.device.httpPort() == 0);
    rig.device.stop();   // twice is harmless

    REQUIRE (rig.device.start());
    CHECK (rig.device.httpPort() > 0);
    CHECK (oqatest::get (rig.port(), "/").status == 200);
}

TEST_CASE ("mock device: the CLI's fixture loader refuses what is not a tree")
{
    MockDevice device;
    const auto notJson = juce::File::createTempFile (".json");
    notJson.replaceWithText ("this is not JSON");
    CHECK_FALSE (device.loadTreeFromFile (notJson));
    notJson.deleteFile();

    CHECK_FALSE (device.loadTree (juce::var (42)));
    CHECK (device.loadTreeFromFile (oqatest::fixture ("trees/generic-device.json")));
}
