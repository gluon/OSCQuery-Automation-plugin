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

    Origin: the three "udp:" cases are go.dot tests/OscCodecTests.cpp at 1c8aaa0a
    (the UdpEndpoint cases left out of OscCodecTests.cpp here), addresses and
    namespaces changed; the rest is this project's.
*/

/*
    OSC ON THE WIRE, BOTH WAYS (devplan Phase 2: "OSC: messages, bundles with
    timetags, MTU-aware splitting, UDP and SLIP-over-TCP senders, a UDP receiver
    for the Phase 4 fallback").

    The framing fixtures are written by hand from RFC 1055 and OSC 1.0, not
    from the encoders here. Addresses typed in this file are fixtures of it.
*/

#include <doctest/doctest.h>

#include "TestSupport.h"
#include "MockDevice.h"
#include "support/WsTestClient.h"

#include <oscquery/HostInfo.h>
#include <oscquery/osc/Bundler.h>
#include <oscquery/osc/Codec.h>
#include <oscquery/osc/Framing.h>
#include <oscquery/net/TcpSender.h>
#include <oscquery/net/UdpEndpoint.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

using namespace oqa::oscquery;
using namespace oqa::oscquery::osc;
using oqatest::waitUntil;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    Bytes encoded (const Packet& packet)
    {
        std::string error;
        auto bytes = encode (packet, error);
        INFO (error);
        REQUIRE (bytes.has_value());
        return *bytes;
    }

    Packet floats3 (const std::string& address, float x, float y, float z)
    {
        return Packet::message (address, { Value::float32 (x), Value::float32 (y), Value::float32 (z) });
    }
}

//==============================================================================
TEST_CASE ("framing: SLIP wraps a packet in END and escapes END and ESC inside it")
{
    //  RFC 1055: END 0xC0 -> ESC ESC_END (0xDB 0xDC); ESC 0xDB -> ESC ESC_ESC (0xDB 0xDD).
    const Bytes packet { 0x01, 0xC0, 0x02, 0xDB, 0x03 };
    const Bytes expected { 0xC0, 0x01, 0xDB, 0xDC, 0x02, 0xDB, 0xDD, 0x03, 0xC0 };

    CHECK (slipEncode (packet.data(), packet.size()) == expected);
    CHECK (frame (Framing::slip, packet) == expected);
    CHECK (slipEncode (nullptr, 0) == Bytes { 0xC0, 0xC0 });

    //  Decoded whole, and decoded a byte at a time, the same.
    Deframer whole (Framing::slip);
    whole.feed (expected);
    auto got = whole.next();
    REQUIRE (got.has_value());
    CHECK (*got == packet);
    CHECK_FALSE (whole.next().has_value());

    Deframer trickle (Framing::slip);

    for (const auto byte : expected)
        trickle.feed (&byte, 1);

    got = trickle.next();
    REQUIRE (got.has_value());
    CHECK (*got == packet);

    //  Two packets back to back, with the ENDs doubled between them, are two.
    Bytes two = expected;
    two.insert (two.end(), expected.begin(), expected.end());
    Deframer pair (Framing::slip);
    pair.feed (two);
    CHECK (pair.next().has_value());
    CHECK (pair.next().has_value());
    CHECK_FALSE (pair.next().has_value());
    CHECK (pair.dropped() == 0);
    CHECK (pair.violations() == 0);
}

TEST_CASE ("framing: a SLIP packet over the limit is dropped, and a false escape keeps its byte")
{
    Deframer small (Framing::slip, 4);
    const Bytes big { 0xC0, 1, 2, 3, 4, 5, 0xC0, 9, 0xC0 };
    small.feed (big);

    //  The five-byte packet went; the one-byte packet after it arrived.
    auto got = small.next();
    REQUIRE (got.has_value());
    CHECK (*got == Bytes { 9 });
    CHECK_FALSE (small.next().has_value());
    CHECK (small.dropped() == 1);

    //  ESC followed by something that is not an escape: the byte stands, counted.
    Deframer lenient (Framing::slip);
    lenient.feed (Bytes { 0xC0, 0xDB, 0x41, 0xC0 });
    got = lenient.next();
    REQUIRE (got.has_value());
    CHECK (*got == Bytes { 0x41 });
    CHECK (lenient.violations() == 1);

    //  reset() forgets a half packet.
    Deframer half (Framing::slip);
    half.feed (Bytes { 0xC0, 1, 2 });
    half.reset();
    half.feed (Bytes { 3, 0xC0 });
    got = half.next();
    REQUIRE (got.has_value());
    CHECK (*got == Bytes { 3 });
}

TEST_CASE ("framing: a length prefix is the byte count, big-endian, then the packet")
{
    const Bytes packet { 0x2f, 0x78, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00 };   // "/x\0\0,\0\0\0"
    const Bytes expected { 0, 0, 0, 8, 0x2f, 0x78, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00 };

    CHECK (lengthPrefixEncode (packet.data(), packet.size()) == expected);
    CHECK (frame (Framing::lengthPrefix, packet) == expected);

    //  Split anywhere, including inside the count.
    Deframer reader (Framing::lengthPrefix);
    reader.feed (Bytes (expected.begin(), expected.begin() + 2));
    CHECK_FALSE (reader.next().has_value());
    reader.feed (Bytes (expected.begin() + 2, expected.begin() + 7));
    CHECK_FALSE (reader.next().has_value());
    reader.feed (Bytes (expected.begin() + 7, expected.end()));
    auto got = reader.next();
    REQUIRE (got.has_value());
    CHECK (*got == packet);

    //  Two in one read.
    Bytes two = expected;
    two.insert (two.end(), expected.begin(), expected.end());
    reader.feed (two);
    CHECK (reader.next().has_value());
    CHECK (reader.next().has_value());
    CHECK_FALSE (reader.next().has_value());

    //  A count that cannot be right loses a byte and tries again, until a packet lines up.
    Deframer resync (Framing::lengthPrefix, 64);
    Bytes rubbish { 0xff, 0xff };
    rubbish.insert (rubbish.end(), expected.begin(), expected.end());
    resync.feed (rubbish);
    got = resync.next();
    REQUIRE (got.has_value());
    CHECK (*got == packet);
    CHECK (resync.dropped() >= 1);

    CHECK (framingName (Framing::slip) == std::string ("slip"));
    CHECK (framingFromName ("SLIP") == Framing::slip);
    CHECK (framingFromName ("length") == Framing::lengthPrefix);
    CHECK_FALSE (framingFromName ("morse").has_value());
}

//==============================================================================
TEST_CASE ("bundler: messages become bundles under the MTU, in order, with the time tag")
{
    INFO ("locale in effect: " << std::string (oqatest::appliedLocaleName()));

    //  Sixty-four tracks moving xyz: 64 three-float messages, which the PRD
    //  says is one datagram each 40-45 messages at about 1,400 bytes.
    std::vector<Packet> messages;

    for (int i = 0; i < 64; ++i)
        messages.push_back (floats3 ("/track/" + std::to_string (i) + "/xyz",
                                     static_cast<float> (i), 0.5f, -1.0f));

    const TimeTag when { 0x0102030405060708ull };
    const auto result = bundle (messages, when, 1400);

    CHECK (result.refused.empty());
    REQUIRE (result.datagrams.size() >= 2);

    std::vector<std::string> order;
    std::size_t total = 0;

    for (const auto& datagram : result.datagrams)
    {
        CHECK (datagram.size() <= 1400);
        CHECK (datagram.size() % 4 == 0);

        const auto decoded = decode (datagram.data(), datagram.size());
        INFO (decoded.error);
        REQUIRE (decoded.ok);
        REQUIRE (decoded.packet.isBundle());
        CHECK (decoded.packet.time == when);

        for (const auto& element : decoded.packet.elements)
        {
            order.push_back (element.address);
            REQUIRE (element.args.size() == 3);
            ++total;
        }
    }

    CHECK (total == messages.size());

    for (std::size_t i = 0; i < order.size(); ++i)
        CHECK (order[i] == messages[i].address);

    //  One message is still one bundle, tag and all.
    const auto one = bundle ({ messages.front() }, TimeTag { 1 }, 1400);
    REQUIRE (one.datagrams.size() == 1);
    const auto decodedOne = decode (one.datagrams[0].data(), one.datagrams[0].size());
    REQUIRE (decodedOne.ok);
    CHECK (decodedOne.packet.isBundle());
    CHECK (decodedOne.packet.time == TimeTag { 1 });
    CHECK (decodedOne.packet.elements.size() == 1);

    //  Nothing in, nothing out.
    CHECK (bundle ({}, when).datagrams.empty());
}

TEST_CASE ("bundler: a message that cannot fit alone is refused by index, and an input bundle is flattened")
{
    std::string big (2000, 'a');
    std::vector<Packet> messages {
        Packet::message ("/small", { Value::int32 (1) }),
        Packet::message ("/big", { Value::string (big) }),
        Packet::message ("/also-small", { Value::int32 (2) }),
        Packet::message ("not an address")
    };

    const auto result = bundle (messages, TimeTag { 1 }, 1400);

    REQUIRE (result.refused.size() == 2);
    CHECK (result.refused[0] == 1);
    CHECK (result.refused[1] == 3);
    CHECK (result.reasons.size() == 2);
    CHECK (result.reasons[0].find ("1400") != std::string::npos);
    REQUIRE (result.datagrams.size() == 1);

    const auto decoded = decode (result.datagrams[0].data(), result.datagrams[0].size());
    REQUIRE (decoded.ok);
    REQUIRE (decoded.packet.elements.size() == 2);
    CHECK (decoded.packet.elements[0].address == "/small");
    CHECK (decoded.packet.elements[1].address == "/also-small");

    //  A bundle given as input contributes its messages, under the sender's tag.
    const auto nested = Packet::bundle (TimeTag { 99 }, { Packet::message ("/a"), Packet::message ("/b") });
    const auto flat = bundle ({ nested, Packet::message ("/c") }, TimeTag { 7 }, 1400);
    REQUIRE (flat.datagrams.size() == 1);
    const auto decodedFlat = decode (flat.datagrams[0].data(), flat.datagrams[0].size());
    REQUIRE (decodedFlat.ok);
    CHECK (decodedFlat.packet.time == TimeTag { 7 });
    REQUIRE (decodedFlat.packet.elements.size() == 3);
    CHECK (decodedFlat.packet.elements[2].address == "/c");
}

//==============================================================================
TEST_CASE ("udp: a datagram arrives with the sender's address and port on it")
{
    net::UdpEndpoint receiver;

    std::mutex mutex;
    std::vector<net::Datagram> arrived;

    REQUIRE (receiver.start (0, [&mutex, &arrived] (net::Datagram datagram)
                             {
                                 const std::lock_guard<std::mutex> lock { mutex };
                                 arrived.push_back (std::move (datagram));
                             }));

    CHECK (receiver.isRunning());
    REQUIRE (receiver.boundPort() > 0);

    net::UdpEndpoint sender;
    REQUIRE (sender.start (0, [] (net::Datagram) {}));
    REQUIRE (sender.boundPort() > 0);

    const auto packet = Packet::message ("/x/next");
    const auto bytes = encoded (packet);
    REQUIRE (sender.send ("127.0.0.1", receiver.boundPort(), bytes));

    REQUIRE (waitUntil ([&mutex, &arrived]
                        {
                            const std::lock_guard<std::mutex> lock { mutex };
                            return ! arrived.empty();
                        }));

    receiver.stop();
    sender.stop();

    REQUIRE (arrived.size() == 1);
    const auto& datagram = arrived.front();
    CHECK (datagram.bytes == bytes);
    CHECK (datagram.senderIp == "127.0.0.1");
    CHECK (datagram.senderPort == sender.boundPort());
    CHECK (datagram.origin() == "udp:127.0.0.1:" + std::to_string (sender.boundPort()));

    const auto result = decode (datagram.bytes.data(), datagram.bytes.size());
    REQUIRE (result.ok);
    CHECK (result.packet == packet);
}

TEST_CASE ("udp: start and stop are safe to repeat, and a bad handler is refused")
{
    net::UdpEndpoint endpoint;

    endpoint.stop();
    CHECK_FALSE (endpoint.start (0, nullptr));
    CHECK_FALSE (endpoint.isRunning());

    REQUIRE (endpoint.start (0, [] (net::Datagram) {}));
    CHECK_FALSE (endpoint.start (0, [] (net::Datagram) {}));
    CHECK (endpoint.boundPort() > 0);

    endpoint.stop();
    endpoint.stop();
    CHECK_FALSE (endpoint.isRunning());

    REQUIRE (endpoint.start (0, [] (net::Datagram) {}));
    CHECK (endpoint.boundPort() > 0);
    endpoint.stop();
}

TEST_CASE ("udp: the bound socket asks its system for room for a burst")
{
    net::UdpEndpoint endpoint;
    CHECK (endpoint.receiveBuffer() == 0);

    REQUIRE (endpoint.start (0, [] (net::Datagram) {}));
    CHECK (endpoint.receiveBuffer() >= 65536);

   #if defined (_WIN32)
    CHECK (endpoint.receiveBuffer() >= net::UdpEndpoint::receiveBufferWanted);
   #endif

    endpoint.stop();
}

//==============================================================================
TEST_CASE ("transport: a bundle sent over UDP to the device's OSC_PORT arrives whole")
{
    oqatest::MockDevice mock;
    REQUIRE (mock.loadTree (juce::JSON::parse (oqatest::fixture ("trees/generic-device.json"))));
    REQUIRE (mock.start());

    std::string error;
    const auto info = parseHostInfo (mock.hostInfo(), error);
    REQUIRE (info.has_value());
    CHECK_FALSE (info->oscOverTcp());
    CHECK (info->oscPort == mock.oscPort());

    const auto bundled = bundle ({ floats3 ("/a", 1.0f, 2.0f, 3.0f), floats3 ("/b", 4.0f, 5.0f, 6.0f) }, TimeTag { 1 });
    REQUIRE (bundled.datagrams.size() == 1);

    net::UdpEndpoint sender;
    REQUIRE (sender.send (info->oscHost ("127.0.0.1"), info->oscPort, bundled.datagrams[0]));
    REQUIRE (waitUntil ([&] { return mock.receivedOsc().size() == 1; }));

    const auto received = mock.receivedOsc();
    CHECK (received[0] == bundled.datagrams[0]);

    const auto decoded = decode (received[0].data(), received[0].size());
    REQUIRE (decoded.ok);
    REQUIRE (decoded.packet.elements.size() == 2);
    CHECK (decoded.packet.elements[1].address == "/b");
}

TEST_CASE ("transport: a device whose OSC_TRANSPORT is TCP gets SLIP-framed packets on one connection")
{
    oqatest::MockDevice::Options options;
    options.oscTransport = "TCP";

    oqatest::MockDevice mock (options);
    REQUIRE (mock.loadTree (juce::JSON::parse (oqatest::fixture ("trees/generic-device.json"))));
    REQUIRE (mock.start());

    std::string error;
    const auto info = parseHostInfo (mock.hostInfo(), error);
    REQUIRE (info.has_value());
    CHECK (info->oscOverTcp());
    CHECK (info->oscPort == mock.oscPort());
    CHECK (info->oscPort > 0);

    net::TcpSender::Options sending;
    sending.host = info->oscHost ("127.0.0.1");
    sending.port = info->oscPort;
    sending.framing = Framing::slip;
    sending.connectTimeoutMs = 2000;

    net::TcpSender sender (sending);
    CHECK_FALSE (sender.isConnected());

    const std::vector<Packet> packets { floats3 ("/a", 1.0f, 2.0f, 3.0f),
                                        Packet::message ("/b", { Value::string ("text") }),
                                        Packet::bundle (TimeTag { 5 }, { Packet::message ("/c") }) };

    for (const auto& packet : packets)
    {
        error.clear();
        REQUIRE (sender.send (packet, error));
        INFO (error);
    }

    CHECK (sender.isConnected());
    CHECK (sender.packetsSent() == 3);
    CHECK (sender.failures() == 0);

    //  The stream, de-framed, is the three packets in order.
    std::size_t expected = 0;

    for (const auto& packet : packets)
        expected += encoded (packet).size() + 2;

    REQUIRE (waitUntil ([&] { return mock.receivedOscStream().size() >= expected; }));

    Deframer deframer (Framing::slip);
    deframer.feed (mock.receivedOscStream());

    for (const auto& packet : packets)
    {
        const auto got = deframer.next();
        REQUIRE (got.has_value());
        const auto decoded = decode (got->data(), got->size());
        REQUIRE (decoded.ok);
        CHECK (decoded.packet == packet);
    }

    CHECK_FALSE (deframer.next().has_value());

    //  Something that cannot be encoded is refused before the socket sees it.
    CHECK_FALSE (sender.send (Packet::message ("not an address"), error));
    CHECK (sender.failures() == 1);

    sender.disconnect();
    CHECK_FALSE (sender.isConnected());
}

TEST_CASE ("transport: the TCP sender can frame with a length prefix instead, and reports a dead port")
{
    oqatest::MockDevice::Options options;
    options.oscTransport = "TCP";

    oqatest::MockDevice mock (options);
    REQUIRE (mock.loadTree (juce::JSON::parse (oqatest::fixture ("trees/generic-device.json"))));
    REQUIRE (mock.start());

    net::TcpSender::Options sending;
    sending.host = "127.0.0.1";
    sending.port = mock.oscPort();
    sending.framing = Framing::lengthPrefix;

    net::TcpSender sender (sending);
    const auto packet = Packet::message ("/x", { Value::int32 (42) });

    std::string error;
    REQUIRE (sender.send (packet, error));

    const auto bytes = encoded (packet);
    REQUIRE (waitUntil ([&] { return mock.receivedOscStream().size() >= bytes.size() + 4; }));

    Deframer deframer (Framing::lengthPrefix);
    deframer.feed (mock.receivedOscStream());
    const auto got = deframer.next();
    REQUIRE (got.has_value());
    CHECK (*got == bytes);

    //  A port nobody holds: false with a reason, counted, and no exception.
    const auto deadPort = mock.oscPort();
    mock.stop();

    net::TcpSender::Options dead = sending;
    dead.port = deadPort;
    dead.connectTimeoutMs = 1000;
    net::TcpSender nobody (dead);

    error.clear();
    CHECK_FALSE (nobody.send (packet, error));
    CHECK_FALSE (error.empty());
    CHECK (nobody.failures() == 1);
    CHECK (nobody.packetsSent() == 0);
}
