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
    THE HTTP SIDE OF THE LIBRARY AGAINST THE MOCK DEVICE: HOST_INFO, the whole
    tree, one subtree (C7), the attribute answers (200 / 204 / 400 / 404, spec
    draft §6), a device that answers HOST_INFO and nothing else, and a port
    nobody listens on, which must come back as an error within the timeout and
    never as an exception.

    No address is typed: every path comes from the fixture by what the node is.
*/

#include <doctest/doctest.h>

#include "TestSupport.h"
#include "MockDevice.h"

#include <oscquery/HostInfo.h>
#include <oscquery/net/HttpGet.h>
#include <oscquery/tree/Tree.h>

#include <chrono>
#include <functional>
#include <string>
#include <vector>

using namespace oqa::oscquery;

namespace
{
    constexpr int timeoutMs = 3000;
    const std::string loopback = "127.0.0.1";

    juce::var loadFixture()
    {
        return juce::JSON::parse (oqatest::fixture ("trees/generic-device.json"));
    }

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

    std::string pathOf (const juce::var& node)
    {
        return node.getProperty ("FULL_PATH", {}).toString().toStdString();
    }

    bool isLeaf (const juce::var& node) { return node.hasProperty ("TYPE"); }
    bool isContainer (const juce::var& node) { return ! isLeaf (node) && node.hasProperty ("CONTENTS"); }

    juce::var first (const juce::var& root, const std::function<bool (const juce::var&)>& what,
                     const char* description)
    {
        for (const auto& node : nodesOf (root))
            if (what (node))
                return node;

        FAIL ("the fixture has no " << description);
        return {};
    }

    struct Rig
    {
        juce::var fixture { loadFixture() };
        oqatest::MockDevice device;
        bool started = false;

        explicit Rig (oqatest::MockDevice::Options options = {}) : device (std::move (options))
        {
            started = device.loadTree (fixture) && device.start();
        }

        int port() const { return device.httpPort(); }
    };
}

//==============================================================================
TEST_CASE ("http: HOST_INFO, the tree and a subtree come back and parse")
{
    INFO ("locale in effect: " << std::string (oqatest::appliedLocaleName()));

    Rig rig;
    REQUIRE (rig.started);

    const auto info = net::httpGet (loopback, rig.port(), "/", "HOST_INFO", timeoutMs);
    INFO (info.error);
    REQUIRE (info.ok);
    CHECK (info.status == 200);

    std::string error;
    const auto hostInfo = parseHostInfo (info.body, error);
    INFO (error);
    REQUIRE (hostInfo.has_value());
    CHECK (hostInfo->name == rig.device.options().name.toStdString());
    CHECK (hostInfo->oscPort == rig.device.oscPort());

    const auto whole = net::httpGet (loopback, rig.port(), "/", "", timeoutMs);
    REQUIRE (whole.ok);
    REQUIRE (whole.status == 200);

    const auto tree = Tree::parse (whole.body, error);
    INFO (error);
    REQUIRE (tree.has_value());
    CHECK (tree->nodeCount() == nodesOf (rig.fixture).size());

    //  C7: a subtree is that node and its descendants, in the same shape.
    const auto container = first (rig.fixture, [] (auto& n) { return isContainer (n) && pathOf (n) != "/"; },
                                  "container below the root");
    const auto subtree = net::httpGet (loopback, rig.port(), pathOf (container), "", timeoutMs);
    REQUIRE (subtree.ok);
    REQUIRE (subtree.status == 200);

    juce::var parsedSubtree;
    REQUIRE (juce::JSON::parse (juce::String (subtree.body), parsedSubtree).wasOk());

    const auto node = Tree::parseNode (parsedSubtree, pathOf (container), error);
    REQUIRE (node.has_value());
    CHECK (node->fullPath == pathOf (container));

    //  It holds exactly the nodes the fixture has under that container.
    std::vector<juce::var> under;
    collect (container, under);

    Tree alone;
    alone.merge (*node);
    CHECK (alone.nodeCount() == under.size() + 1);      // plus the root it hangs from

    //  Merged into the full tree it changes nothing.
    auto merged = *tree;
    REQUIRE (merged.merge (parsedSubtree, pathOf (container), error));
    CHECK (merged.nodeCount() == tree->nodeCount());
    CHECK (merged.treeHash() == tree->treeHash());
}

TEST_CASE ("http: the attribute answers are read off the wire, 204 and 400 both accepted")
{
    Rig rig;
    REQUIRE (rig.started);

    //  A leaf with a VALUE: 200 with {"VALUE": [...]}, typed by the tree's TYPE.
    const auto valued = first (rig.fixture, [] (auto& n) { return isLeaf (n) && n.hasProperty ("VALUE"); },
                               "leaf with a value");
    const auto reply = net::httpGet (loopback, rig.port(), pathOf (valued), "VALUE", timeoutMs);
    REQUIRE (reply.ok);
    REQUIRE (reply.status == 200);

    juce::var parsed;
    REQUIRE (juce::JSON::parse (juce::String (reply.body), parsed).wasOk());
    const auto values = valuesFromVar (parsed["VALUE"], valued["TYPE"].toString().toStdString());

    std::string error;
    const auto tree = Tree::parse (rig.fixture, error);
    REQUIRE (tree.has_value());
    const auto* node = tree->find (pathOf (valued));
    REQUIRE (node != nullptr);
    CHECK (values == node->value);

    //  An attribute this node lacks: 204, empty body, still ok.
    const auto unranged = first (rig.fixture, [] (auto& n) { return isLeaf (n) && ! n.hasProperty ("RANGE"); },
                                 "leaf without RANGE");
    const auto absent = net::httpGet (loopback, rig.port(), pathOf (unranged), "RANGE", timeoutMs);
    REQUIRE (absent.ok);
    CHECK (absent.status == 204);
    CHECK (absent.body.empty());

    //  VALUE of a write-only node: 204 as well.
    const auto writeOnly = first (rig.fixture, [] (auto& n) { return isLeaf (n) && static_cast<int> (n["ACCESS"]) == 2; },
                                  "write-only leaf");
    const auto unreadable = net::httpGet (loopback, rig.port(), pathOf (writeOnly), "VALUE", timeoutMs);
    REQUIRE (unreadable.ok);
    CHECK (unreadable.status == 204);

    //  Not an attribute at all: 400.
    const auto unknown = net::httpGet (loopback, rig.port(), pathOf (valued), "NOT_AN_ATTRIBUTE", timeoutMs);
    REQUIRE (unknown.ok);
    CHECK (unknown.status == 400);

    //  No such node: 404.
    const auto missing = net::httpGet (loopback, rig.port(), pathOf (valued) + "/nothing/here", "", timeoutMs);
    REQUIRE (missing.ok);
    CHECK (missing.status == 404);

    //  The query goes on verbatim: "?VALUE=" would be 400 from the mock, as from the proposal.
    const auto rewritten = net::httpGet (loopback, rig.port(), pathOf (valued), "VALUE=", timeoutMs);
    REQUIRE (rewritten.ok);
    CHECK (rewritten.status == 400);
}

TEST_CASE ("http: a device that answers HOST_INFO and nothing else is reachable without a tree")
{
    oqatest::MockDevice::Options options;
    options.hostInfoOnly = true;

    Rig rig (options);
    REQUIRE (rig.started);

    const auto info = net::httpGet (loopback, rig.port(), "/", "HOST_INFO", timeoutMs);
    REQUIRE (info.ok);
    CHECK (info.status == 200);

    std::string error;
    CHECK (parseHostInfo (info.body, error).has_value());

    const auto whole = net::httpGet (loopback, rig.port(), "/", "", timeoutMs);
    REQUIRE (whole.ok);                    // the exchange completed...
    CHECK (whole.status == 404);           // ...and the device has no tree to give
    CHECK_FALSE (Tree::parse (whole.body, error).has_value());
    CHECK_FALSE (error.empty());
}

TEST_CASE ("http: a port nobody listens on is an error within the timeout, not an exception")
{
    Rig rig;
    REQUIRE (rig.started);

    const auto port = rig.port();
    rig.device.stop();

    const auto started = std::chrono::steady_clock::now();
    const auto reply = net::httpGet (loopback, port, "/", "HOST_INFO", timeoutMs);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds> (
                           std::chrono::steady_clock::now() - started).count();

    CHECK_FALSE (reply.ok);
    CHECK (reply.status == 0);
    CHECK_FALSE (reply.error.empty());
    CHECK (elapsed < timeoutMs + 1000);

    //  And an address that is no address at all.
    CHECK_FALSE (net::httpGet ("", port, "/", "HOST_INFO", timeoutMs).ok);
    CHECK_FALSE (net::httpGet (loopback, 0, "/", "HOST_INFO", timeoutMs).ok);
    CHECK_FALSE (net::httpGet (loopback, 70000, "/", "HOST_INFO", timeoutMs).ok);
}
