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
    L5, MEASURED ON LOOPBACK (devplan Phase 2: "a 10,000-leaf tree in under
    2 s (L5, measured on loopback)"): the mock serves a generated tree of
    10,000 leaves, and the client fetches and parses it.

    The two seconds are asserted in a Release build. A Debug build reports the
    time and holds it to a looser bound, because an unoptimised JSON parser on
    a megabyte is not what the clause measures; CI runs the Debug suite, so
    the number it prints is the one to read in the log.
*/

#include <doctest/doctest.h>

#include "TestSupport.h"
#include "MockDevice.h"
#include "support/TreeGen.h"

#include <oscquery/Device.h>
#include <oscquery/net/HttpGet.h>

#include <chrono>
#include <string>

using namespace oqa::oscquery;

namespace
{
    long long millisecondsOf (const std::function<void()>& work)
    {
        const auto started = std::chrono::steady_clock::now();
        work();
        return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now() - started).count();
    }
}

TEST_CASE ("large tree: 10,000 leaves are fetched and parsed, and the tree is marked large")
{
    INFO ("locale in effect: " << std::string (oqatest::appliedLocaleName()));

    constexpr int leaves = 10000;

    oqatest::MockDevice mock;
    REQUIRE (mock.loadTree (oqatest::generateTree (leaves, 100)));
    REQUIRE (mock.start());

    //  The GET alone: what the device takes to serialise and send a megabyte.
    net::HttpReply reply;
    const auto getMs = millisecondsOf ([&] { reply = net::httpGet ("127.0.0.1", mock.httpPort(), "/", "", 30000); });
    REQUIRE (reply.ok);
    REQUIRE (reply.status == 200);
    const auto bytes = reply.body.size();

    //  Then the whole thing through the client.
    Device::Options options;
    options.host = "127.0.0.1";
    options.port = mock.httpPort();
    options.httpTimeoutMs = 30000;
    Device device (options);
    REQUIRE (device.probe());

    const auto fetchMs = millisecondsOf ([&] { REQUIRE (device.fetchTree()); });

   #ifdef NDEBUG
    constexpr const char* configuration = "Release";
   #else
    constexpr const char* configuration = "Debug";
   #endif

    MESSAGE ("L5 on loopback: GET / of " << bytes << " bytes in " << getMs << " ms; fetched and parsed in "
             << fetchMs << " ms (" << leaves << " leaves, " << std::string (configuration) << ")");

    const auto tree = device.tree();                  // one copy; pointers below are into it
    CHECK (tree.leafCount() == leaves);
    CHECK (tree.nodeCount() == leaves + leaves / 100 + 1);
    CHECK (device.treeIsLarge());                     // above the default threshold of 5,000 nodes

   #ifdef NDEBUG
    CHECK (fetchMs < 2000);
   #else
    CHECK (fetchMs < 20000);
   #endif

    //  A subtree of a large tree is read on its own, as the client does for every later change.
    REQUIRE_FALSE (tree.root().children.empty());
    const auto subtreePath = tree.root().children.begin()->second.fullPath;
    const auto subtreeMs = millisecondsOf ([&] { REQUIRE (device.fetchSubtree (subtreePath)); });
    MESSAGE ("one subtree of 100 leaves re-read in " << subtreeMs << " ms");
    CHECK (subtreeMs < 2000);
}
