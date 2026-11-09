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

#pragma once

/*
    A DNS-SD RESPONDER ON LOOPBACK for tests: answers every PTR question about
    `_oscjson._tcp.local` with the instances it was given (PTR, then SRV, TXT
    and A as additional records, as a real responder does), asked directly
    through the browser's `peers` so no part of the network's 5353 is touched.
    First written in the library's FinderTests (Phase 2); shared since Phase 5b
    with the core's re-identification tests. The instances can be replaced
    while it runs, as a device coming up on another port would be announced.
*/

#include <doctest/doctest.h>

#include <juce_core/juce_core.h>

#include <oscquery/mdns/Mdns.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace oqatest
{
    struct MdnsResponder
    {
        struct Instance
        {
            std::string label;
            std::string hostLabel;
            int port = 0;
        };

        explicit MdnsResponder (std::vector<Instance> toAnnounce) : instances (std::move (toAnnounce))
        {
            REQUIRE (socket.bindToPort (0, "127.0.0.1"));
            thread = std::thread ([this] { run(); });
        }

        ~MdnsResponder()
        {
            stopping.store (true);
            thread.join();
            socket.shutdown();
        }

        int port() const { return socket.getBoundPort(); }

        void setInstances (std::vector<Instance> toAnnounce)
        {
            const std::lock_guard<std::mutex> guard (lock);
            instances = std::move (toAnnounce);
        }

        std::vector<std::uint8_t> answerFor() const
        {
            using namespace oqa::oscquery;

            const auto service = mdns::nameOf ("_oscjson._tcp.local");
            std::vector<mdns::Record> answers, additionals;
            std::vector<Instance> current;

            {
                const std::lock_guard<std::mutex> guard (lock);
                current = instances;
            }

            for (const auto& each : current)
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
            using namespace oqa::oscquery;

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

        mutable std::mutex lock;
        std::vector<Instance> instances;
        juce::DatagramSocket socket { false };
        std::atomic<bool> stopping { false };
        std::thread thread;
    };
}
