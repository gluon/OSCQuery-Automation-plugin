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
    FOUND AND TYPED DEVICES, PROBED (PRD, Discovery: "Each discovered device is
    probed with GET /?HOST_INFO before it is listed. Devices that fail the probe
    show as unreachable, not hidden"; devplan Phase 2: "a manual host:port entry
    path that goes through the same probe").

    DNS-SD is answered by a responder of the test's own on loopback, asked
    directly (the browser's `peers`), so no part of the network's 5353 is
    touched; the responder is the shape of MdnsTests' FakeResponder, cut down.
*/

#include <doctest/doctest.h>

#include "TestSupport.h"
#include "MockDevice.h"
#include "support/WsTestClient.h"

#include <oscquery/Finder.h>
#include <oscquery/mdns/Mdns.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace oqa::oscquery;
using oqatest::waitUntil;

namespace
{
    const std::string loopback = "127.0.0.1";

    juce::var loadFixture()
    {
        return juce::JSON::parse (oqatest::fixture ("trees/generic-device.json"));
    }

    struct Rig
    {
        oqatest::MockDevice device;
        bool started = false;

        explicit Rig (oqatest::MockDevice::Options options = {}) : device (std::move (options))
        {
            started = device.loadTree (loadFixture()) && device.start();
        }

        int port() const { return device.httpPort(); }
        std::string name() const { return device.options().name.toStdString(); }
    };

    /*  A responder on loopback answering every PTR question about the service
        with the instances it was given: PTR, then SRV, TXT and A as additional
        records, as a real responder does. */
    struct Responder
    {
        struct Instance { std::string label; std::string hostLabel; int port; };

        explicit Responder (std::vector<Instance> toAnnounce) : instances (std::move (toAnnounce))
        {
            REQUIRE (socket.bindToPort (0, "127.0.0.1"));
            thread = std::thread ([this] { run(); });
        }

        ~Responder()
        {
            stopping.store (true);
            thread.join();
            socket.shutdown();
        }

        int port() const { return socket.getBoundPort(); }

        std::vector<std::uint8_t> answerFor() const
        {
            const auto service = mdns::nameOf ("_oscjson._tcp.local");
            std::vector<mdns::Record> answers, additionals;

            for (const auto& each : instances)
            {
                mdns::Name instance { each.label };
                instance.insert (instance.end(), service.begin(), service.end());
                const mdns::Name host { each.hostLabel, "local" };

                mdns::Record ptr;
                ptr.name = service;
                ptr.type = mdns::type::ptr;
                ptr.ttl = 120;
                ptr.target = instance;
                answers.push_back (ptr);

                mdns::Record srv;
                srv.name = instance;
                srv.type = mdns::type::srv;
                srv.cacheFlush = true;
                srv.ttl = 120;
                srv.port = static_cast<std::uint16_t> (each.port);
                srv.target = host;
                additionals.push_back (srv);

                mdns::Record txt;
                txt.name = instance;
                txt.type = mdns::type::txt;
                txt.cacheFlush = true;
                txt.ttl = 120;
                txt.txt = { "txtvers=1" };
                additionals.push_back (txt);

                mdns::Record a;
                a.name = host;
                a.type = mdns::type::a;
                a.cacheFlush = true;
                a.ttl = 120;
                a.address = { 127, 0, 0, 1 };
                additionals.push_back (a);
            }

            return mdns::encodeResponse (answers, additionals);
        }

        void run()
        {
            std::vector<std::uint8_t> buffer (9000);
            const auto service = mdns::nameOf ("_oscjson._tcp.local");

            while (! stopping.load())
            {
                if (socket.waitUntilReady (true, 20) != 1)
                    continue;

                juce::String fromIp;
                int fromPort = 0;
                const auto got = socket.read (buffer.data(), static_cast<int> (buffer.size()), false, fromIp, fromPort);
                const auto message = got > 0 ? mdns::decode (buffer.data(), static_cast<std::size_t> (got)) : std::nullopt;

                if (! message || message->response)
                    continue;

                for (const auto& question : message->questions)
                {
                    if (question.type == mdns::type::ptr && mdns::sameName (question.name, service))
                    {
                        const auto bytes = answerFor();
                        socket.write (fromIp, fromPort, bytes.data(), static_cast<int> (bytes.size()));
                        break;
                    }
                }
            }
        }

        std::vector<Instance> instances;
        juce::DatagramSocket socket { false };
        std::atomic<bool> stopping { false };
        std::thread thread;
    };

    Finder::Options manualOnly()
    {
        Finder::Options options;
        options.browse = false;
        options.httpTimeoutMs = 1000;
        options.tickMs = 20;
        return options;
    }

    Finder::Options browsingVia (int responderPort)
    {
        Finder::Options options;
        options.browse = true;
        options.discovery.port = 0;
        options.discovery.peers = { { "127.0.0.1", responderPort } };
        options.httpTimeoutMs = 1000;
        options.tickMs = 20;
        return options;
    }

    std::optional<Finder::Candidate> candidateAt (const Finder& finder, int port)
    {
        for (const auto& c : finder.candidates())
            if (c.port == port)
                return c;

        return std::nullopt;
    }
}

//==============================================================================
TEST_CASE ("finder: a typed device is probed, listed by its NAME, and stays listed when it does not answer")
{
    INFO ("locale in effect: " << std::string (oqatest::appliedLocaleName()));

    Rig rig;
    REQUIRE (rig.started);

    Finder finder;
    REQUIRE (finder.start (manualOnly()));
    CHECK (finder.isRunning());
    CHECK (finder.browseMode() == "off");
    CHECK (finder.candidates().empty());

    const auto before = finder.revision();
    finder.addManual (loopback, rig.port());

    REQUIRE (waitUntil ([&] { auto c = candidateAt (finder, rig.port()); return c && c->probed; }));
    CHECK (finder.revision() > before);

    auto live = candidateAt (finder, rig.port());
    REQUIRE (live.has_value());
    CHECK (live->manual);
    CHECK_FALSE (live->advertised);
    CHECK (live->reachable);
    CHECK (live->name == rig.name());                 // HOST_INFO's NAME, not host:port
    CHECK (live->host == loopback);
    CHECK (live->error.empty());
    REQUIRE (live->hostInfo.has_value());
    CHECK (live->hostInfo->oscPort == rig.device.oscPort());

    //  A second typed entry that nobody answers: listed, unreachable, with a reason.
    const auto deadPort = rig.port() == 65000 ? 65001 : rig.port() + 1;
    {
        //  Hold the port so nothing else can take it during the case.
        juce::StreamingSocket holder;
        const bool held = holder.createListener (deadPort, loopback);
        holder.close();
        (void) held;
    }

    finder.addManual (loopback, deadPort);
    REQUIRE (waitUntil ([&] { auto c = candidateAt (finder, deadPort); return c && c->probed; }));

    auto dead = candidateAt (finder, deadPort);
    REQUIRE (dead.has_value());
    CHECK (dead->manual);
    CHECK_FALSE (dead->reachable);
    CHECK_FALSE (dead->error.empty());
    CHECK (dead->name == loopback + ":" + std::to_string (deadPort));
    CHECK (finder.candidates().size() == 2);

    //  Removed, it goes.
    finder.removeManual (loopback, deadPort);
    REQUIRE (waitUntil ([&] { return ! candidateAt (finder, deadPort).has_value(); }));
    CHECK (finder.candidates().size() == 1);

    //  A device that stops answering is still listed, unreachable, with its last name.
    const auto port = rig.port();                      // the mock forgets it once stopped
    rig.device.stop();
    finder.refresh();
    REQUIRE (waitUntil ([&] { auto c = candidateAt (finder, port); return c && ! c->reachable; }));
    CHECK (candidateAt (finder, port)->name == rig.name());
    CHECK_FALSE (candidateAt (finder, port)->error.empty());

    finder.stop();
    CHECK_FALSE (finder.isRunning());
}

TEST_CASE ("finder: devices found over DNS-SD are probed, named by HOST_INFO, and shown unreachable when they fail")
{
    Rig rig;
    REQUIRE (rig.started);

    //  Two instances announced: one at the mock, one at a port nobody holds.
    const auto deadPort = rig.port() == 65000 ? 65001 : rig.port() + 1;
    Responder responder ({ { "Announced mock", "mock-pc", rig.port() },
                           { "Ghost", "ghost-pc", deadPort } });

    Finder finder;
    REQUIRE (finder.start (browsingVia (responder.port())));
    CHECK (finder.browseMode() == "unicast");
    CHECK (finder.browseProblem().empty());

    REQUIRE (waitUntil ([&]
                        {
                            auto a = candidateAt (finder, rig.port());
                            auto b = candidateAt (finder, deadPort);
                            return a && a->probed && b && b->probed;
                        }));

    const auto live = candidateAt (finder, rig.port());
    REQUIRE (live.has_value());
    CHECK (live->advertised);
    CHECK_FALSE (live->manual);
    CHECK (live->reachable);
    CHECK (live->name == rig.name());                     // HOST_INFO's NAME over the DNS-SD label
    CHECK (live->host == loopback);
    CHECK (live->txt == std::vector<std::string> { "txtvers=1" });
    REQUIRE (live->hostInfo.has_value());

    const auto ghost = candidateAt (finder, deadPort);
    REQUIRE (ghost.has_value());
    CHECK (ghost->advertised);
    CHECK_FALSE (ghost->reachable);
    CHECK (ghost->name == "Ghost");                       // nothing better to call it
    CHECK_FALSE (ghost->error.empty());

    //  The same device typed as well is one row, now both found and typed.
    finder.addManual (loopback, rig.port());
    REQUIRE (waitUntil ([&] { auto c = candidateAt (finder, rig.port()); return c && c->manual; }));
    CHECK (finder.candidates().size() == 2);
    CHECK (candidateAt (finder, rig.port())->advertised);

    finder.stop();
}
