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
    THE FIXED-BUFFER ENCODER (devplan Phase 3c): byte for byte what Codec.h's
    encode() produces, into a buffer the caller owns, refusing what does not
    fit and touching nothing past what it wrote. The addresses here are the
    two-letter fixtures of this file, as the devplan allows a test to hold.
*/

#include <doctest/doctest.h>

#include <oscquery/osc/Bundler.h>
#include <oscquery/osc/Codec.h>
#include <oscquery/osc/FixedEncoder.h>

#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace oqa::oscquery::osc;

namespace
{
    /** Codec.h's bytes for the same message. */
    std::vector<std::uint8_t> reference (const std::string& address, const std::vector<Value>& args)
    {
        std::string error;
        auto bytes = encode (Packet::message (address, args), error);
        REQUIRE_MESSAGE (bytes.has_value(), error);
        return *bytes;
    }

    constexpr std::uint8_t sentinel = 0xa5;
}

TEST_CASE ("fixed encoder: a message matches the codec byte for byte, for every tag v1 sends")
{
    struct Case { std::string address; std::vector<Value> args; };

    const std::vector<Case> cases {
        { "/a",         {} },
        { "/ab/c",      { Value::float32 (1.5f) } },
        { "/ab/cd",     { Value::int32 (-7) } },
        { "/ab/cd/e",   { Value::boolean (true) } },
        { "/ab/cd/ef",  { Value::boolean (false) } },
        { "/x/y/z",     { Value::float32 (0.25f), Value::float32 (-2.5f), Value::float32 (3.0f) } },
        { "/s",         { Value::string ("hello"), Value::int32 (3) } },
        { "/s/t",       { Value::string ("abc") } },                    // 3 chars: one pad byte
        { "/s/u",       { Value::string ("abcd") } },                   // 4 chars: NUL and three pads
        { "/h",         { Value::int64 (1234567890123LL), Value::float64 (2.5) } },
        { "/n",         { Value::nil(), Value::impulse(), Value::timeTag (TimeTag { 42 }) } },
        { "/b",         { Value::blob (Blob { { 1, 2, 3, 4, 5 } }) } },
    };

    for (const auto& c : cases)
    {
        INFO ("address " << c.address << " with " << c.args.size() << " argument(s)");

        const auto expected = reference (c.address, c.args);

        std::array<std::uint8_t, 256> buffer {};
        buffer.fill (sentinel);

        const auto written = encodeMessageInto (buffer.data(), buffer.size(), c.address, c.args.data(), c.args.size());

        REQUIRE (written == expected.size());
        CHECK (std::memcmp (buffer.data(), expected.data(), expected.size()) == 0);
        CHECK (buffer[written] == sentinel);

        CHECK (encodedMessageSize (c.address, c.args.data(), c.args.size()) == expected.size());

        /*  The result decodes to the packet that went in. */
        const auto decoded = decode (buffer.data(), written);
        REQUIRE (decoded.ok);
        CHECK (decoded.packet.address == c.address);
        CHECK (decoded.packet.args == c.args);
    }
}

TEST_CASE ("fixed encoder: what does not fit is not written, and nothing past the answer is touched")
{
    const std::vector<Value> args { Value::float32 (1.0f), Value::float32 (2.0f), Value::float32 (3.0f) };
    const std::string address = "/abc/def";
    const auto needed = reference (address, args).size();

    for (std::size_t capacity = 0; capacity < needed; ++capacity)
    {
        std::vector<std::uint8_t> buffer (needed + 8, sentinel);
        INFO ("capacity " << capacity << " of " << needed);

        CHECK (encodeMessageInto (buffer.data(), capacity, address, args.data(), args.size()) == 0);

        for (std::size_t i = capacity; i < buffer.size(); ++i)
            CHECK (buffer[i] == sentinel);
    }

    std::vector<std::uint8_t> exact (needed, sentinel);
    CHECK (encodeMessageInto (exact.data(), exact.size(), address, args.data(), args.size()) == needed);
}

TEST_CASE ("fixed encoder: refused as the codec refuses")
{
    std::array<std::uint8_t, 128> buffer {};

    const Value one = Value::float32 (1.0f);
    CHECK (encodeMessageInto (buffer.data(), buffer.size(), "no/slash", &one, 1) == 0);
    CHECK (encodeMessageInto (buffer.data(), buffer.size(), "/a b", &one, 1) == 0);
    CHECK (encodeMessageInto (buffer.data(), buffer.size(), "/a/", &one, 1) == 0);

    const Value nan = Value::float32 (std::numeric_limits<float>::quiet_NaN());
    CHECK (encodeMessageInto (buffer.data(), buffer.size(), "/a", &nan, 1) == 0);

    const Value withNul = Value::string (std::string ("a\0b", 3));
    CHECK (encodeMessageInto (buffer.data(), buffer.size(), "/a", &withNul, 1) == 0);

    CHECK (encodeMessageInto (nullptr, 64, "/a", &one, 1) == 0);
    CHECK (encodeMessageInto (buffer.data(), buffer.size(), "/a", nullptr, 1) == 0);
    CHECK (encodedMessageSize ("/a", nullptr, 1) == 0);
}

TEST_CASE ("bundle writer: a bundle matches the bundler byte for byte and splits by refusing")
{
    std::vector<Packet> messages;
    std::vector<std::vector<Value>> argsOf;

    for (int i = 0; i < 10; ++i)
    {
        argsOf.push_back ({ Value::float32 (static_cast<float> (i)), Value::float32 (0.5f), Value::float32 (-1.0f) });
        messages.push_back (Packet::message ("/p/q/" + std::to_string (i), argsOf.back()));
    }

    const TimeTag when { 0x0123456789abcdefULL };

    SUBCASE ("everything fits in one")
    {
        const auto expected = bundle (messages, when, 4096);
        REQUIRE (expected.datagrams.size() == 1);

        std::array<std::uint8_t, 4096> buffer {};
        BundleWriter writer (buffer.data(), buffer.size());
        REQUIRE (writer.begin (when));
        CHECK (writer.size() == BundleWriter::headerBytes);
        CHECK (writer.empty());

        for (std::size_t i = 0; i < messages.size(); ++i)
            REQUIRE (writer.add (messages[i].address, argsOf[i].data(), argsOf[i].size()));

        CHECK (writer.count() == 10);
        REQUIRE (writer.size() == expected.datagrams[0].size());
        CHECK (std::memcmp (writer.data(), expected.datagrams[0].data(), writer.size()) == 0);

        const auto decoded = decode (writer.data(), writer.size());
        REQUIRE (decoded.ok);
        CHECK (decoded.packet.isBundle());
        CHECK (decoded.packet.time == when);
        CHECK (decoded.packet.elements.size() == 10);
    }

    SUBCASE ("at a small MTU the writer refuses where the bundler would have split")
    {
        constexpr std::size_t mtu = 100;
        const auto expected = bundle (messages, when, mtu);
        REQUIRE (expected.datagrams.size() > 1);

        std::vector<std::vector<std::uint8_t>> datagrams;
        std::array<std::uint8_t, mtu> buffer {};
        BundleWriter writer (buffer.data(), buffer.size());
        REQUIRE (writer.begin (when));

        for (std::size_t i = 0; i < messages.size(); ++i)
        {
            if (! writer.add (messages[i].address, argsOf[i].data(), argsOf[i].size()))
            {
                REQUIRE_FALSE (writer.empty());
                datagrams.emplace_back (writer.data(), writer.data() + writer.size());
                REQUIRE (writer.begin (when));
                REQUIRE (writer.add (messages[i].address, argsOf[i].data(), argsOf[i].size()));
            }
        }

        datagrams.emplace_back (writer.data(), writer.data() + writer.size());

        CHECK (datagrams == expected.datagrams);
    }

    SUBCASE ("a refused add leaves the bundle as it was")
    {
        std::array<std::uint8_t, 64> buffer {};
        buffer.fill (sentinel);
        BundleWriter writer (buffer.data(), buffer.size());
        REQUIRE (writer.begin (when));
        REQUIRE (writer.add (messages[0].address, argsOf[0].data(), argsOf[0].size()));

        const auto before = writer.size();
        std::vector<std::uint8_t> snapshot (buffer.begin(), buffer.end());

        CHECK_FALSE (writer.add (messages[1].address, argsOf[1].data(), argsOf[1].size()));
        CHECK (writer.size() == before);
        CHECK (writer.count() == 1);

        /*  The byte count slot of the refused message may have been tried;
            what was there before the add is what the bundle reads. */
        const auto decoded = decode (writer.data(), writer.size());
        REQUIRE (decoded.ok);
        CHECK (decoded.packet.elements.size() == 1);
        CHECK (std::equal (snapshot.begin(), snapshot.begin() + static_cast<std::ptrdiff_t> (before), buffer.begin()));
    }

    SUBCASE ("no room for the header, no bundle")
    {
        std::array<std::uint8_t, 12> small {};
        BundleWriter writer (small.data(), small.size());
        CHECK_FALSE (writer.begin (when));
        CHECK (writer.size() == 0);
        CHECK_FALSE (writer.add ("/a", nullptr, 0));
    }
}
