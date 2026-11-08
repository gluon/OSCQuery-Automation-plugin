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
    THE NON-BLOCKING UDP SENDER (devplan Phase 3c): resolves once, sends
    without waiting, counts what the system took and what it refused. On
    loopback, against a UdpEndpoint and against the mock device's OSC_PORT.
*/

#include <doctest/doctest.h>

#include "TestSupport.h"
#include "support/WsTestClient.h"

#include <MockDevice.h>

#include <oscquery/HostInfo.h>
#include <oscquery/net/RawUdpSender.h>
#include <oscquery/net/UdpEndpoint.h>
#include <oscquery/osc/Codec.h>
#include <oscquery/osc/FixedEncoder.h>

#include <array>
#include <mutex>
#include <string>
#include <vector>

using namespace oqa::oscquery;
using oqatest::waitUntil;

TEST_CASE ("raw udp: a datagram sent without waiting arrives whole at a receiver on loopback")
{
    std::mutex lock;
    std::vector<net::Datagram> got;

    net::UdpEndpoint receiver;
    REQUIRE (receiver.start (0, [&] (net::Datagram d)
    {
        const std::lock_guard<std::mutex> guard (lock);
        got.push_back (std::move (d));
    }));

    net::RawUdpSender sender;
    std::string error;
    REQUIRE_MESSAGE (sender.open ("127.0.0.1", receiver.boundPort(), error), error);
    CHECK (sender.isOpen());
    CHECK (sender.resolvedAddress() == "127.0.0.1:" + std::to_string (receiver.boundPort()));
    CHECK (sender.sendBuffer() > 0);

    const osc::Value args[] { osc::Value::float32 (0.75f) };
    std::array<std::uint8_t, 64> buffer {};
    const auto size = osc::encodeMessageInto (buffer.data(), buffer.size(), "/ab/c", args, 1);
    REQUIRE (size > 0);

    CHECK (sender.send (buffer.data(), size));
    CHECK (sender.sent() == 1);
    CHECK (sender.dropped() == 0);
    CHECK (sender.failures() == 0);

    REQUIRE (waitUntil ([&] { const std::lock_guard<std::mutex> guard (lock); return got.size() == 1; }));

    const std::lock_guard<std::mutex> guard (lock);
    CHECK (got[0].bytes == std::vector<std::uint8_t> (buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t> (size)));
    CHECK (got[0].senderIp == "127.0.0.1");

    receiver.stop();
}

TEST_CASE ("raw udp: a thousand datagrams in a burst are sent or counted, never waited for")
{
    net::UdpEndpoint receiver;
    REQUIRE (receiver.start (0, [] (net::Datagram) {}));

    net::RawUdpSender sender;
    std::string error;
    REQUIRE_MESSAGE (sender.open ("127.0.0.1", receiver.boundPort(), error), error);

    std::array<std::uint8_t, 1200> payload {};
    payload.fill (0x5a);

    const auto start = juce::Time::getMillisecondCounterHiRes();

    for (int i = 0; i < 1000; ++i)
        sender.send (payload.data(), payload.size());

    const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - start;

    CHECK (sender.sent() + sender.dropped() + sender.failures() == 1000);
    CHECK (sender.failures() == 0);
    MESSAGE ("1000 x 1200 bytes: " << sender.sent() << " sent, " << sender.dropped() << " dropped, in "
             << elapsedMs << " ms; send buffer " << sender.sendBuffer() << " bytes");

    /*  Generous: this is "did not block", not a benchmark. */
    CHECK (elapsedMs < 2000.0);

    receiver.stop();
}

TEST_CASE ("raw udp: the mock device's OSC_PORT receives what the sender sends")
{
    oqatest::MockDevice mock;
    REQUIRE (mock.loadTree (juce::JSON::parse (oqatest::fixture ("trees/generic-device.json"))));
    REQUIRE (mock.start());

    std::string error;
    const auto info = parseHostInfo (mock.hostInfo(), error);
    REQUIRE_MESSAGE (info.has_value(), error);

    net::RawUdpSender sender;
    REQUIRE_MESSAGE (sender.open (info->oscHost ("127.0.0.1"), info->oscPort, error), error);

    std::array<std::uint8_t, 256> buffer {};
    osc::BundleWriter writer (buffer.data(), buffer.size());
    REQUIRE (writer.begin (osc::TimeTag {}));
    const osc::Value xyz[] { osc::Value::float32 (1.0f), osc::Value::float32 (2.0f), osc::Value::float32 (3.0f) };
    REQUIRE (writer.add ("/p/q", xyz, 3));

    CHECK (sender.send (writer.data(), writer.size()));
    REQUIRE (waitUntil ([&] { return mock.receivedOsc().size() == 1; }));

    const auto decoded = osc::decode (mock.receivedOsc()[0].data(), mock.receivedOsc()[0].size());
    REQUIRE (decoded.ok);
    CHECK (decoded.packet.isBundle());
    REQUIRE (decoded.packet.elements.size() == 1);
    CHECK (decoded.packet.elements[0].args.size() == 3);

    mock.stop();
}

TEST_CASE ("raw udp: what cannot open says why, and a closed sender sends nothing")
{
    net::RawUdpSender sender;
    std::string error;

    CHECK_FALSE (sender.open ("", 9000, error));
    CHECK_FALSE (error.empty());
    CHECK_FALSE (sender.isOpen());

    CHECK_FALSE (sender.open ("127.0.0.1", 0, error));
    CHECK_FALSE (sender.open ("127.0.0.1", 70000, error));

    CHECK_FALSE (sender.open ("no-such-host.invalid", 9000, error));
    CHECK (error.find ("resolve") != std::string::npos);
    CHECK_FALSE (sender.isOpen());

    const std::uint8_t byte = 0;
    CHECK_FALSE (sender.send (&byte, 1));
    CHECK (sender.sent() == 0);

    REQUIRE (sender.open ("127.0.0.1", 9, error));
    CHECK_FALSE (sender.send (nullptr, 1));
    CHECK_FALSE (sender.send (&byte, 0));

    sender.close();
    CHECK_FALSE (sender.isOpen());
    CHECK (sender.sendBuffer() == 0);
    CHECK (sender.resolvedAddress().empty());

    /*  Reopening after a close works, and resets the counters. */
    REQUIRE (sender.open ("127.0.0.1", 9, error));
    CHECK (sender.sent() == 0);
}
