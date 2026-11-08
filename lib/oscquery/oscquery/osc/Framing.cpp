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

#include <oscquery/osc/Framing.h>

#include <algorithm>
#include <cctype>

namespace oqa::oscquery::osc
{
    const char* framingName (Framing framing) noexcept
    {
        return framing == Framing::slip ? "slip" : "length";
    }

    std::optional<Framing> framingFromName (const std::string& name)
    {
        std::string lowered;

        for (const char c : name)
            lowered.push_back (static_cast<char> (std::tolower (static_cast<unsigned char> (c))));

        if (lowered == "slip")
            return Framing::slip;

        if (lowered == "length" || lowered == "length-prefix" || lowered == "lengthprefix" || lowered == "count")
            return Framing::lengthPrefix;

        return std::nullopt;
    }

    //==============================================================================
    std::vector<std::uint8_t> slipEncode (const std::uint8_t* data, std::size_t size)
    {
        std::vector<std::uint8_t> out;
        out.reserve (size + 2 + size / 32);

        out.push_back (slip::end);

        for (std::size_t i = 0; i < size; ++i)
        {
            const auto byte = data[i];

            if (byte == slip::end)
            {
                out.push_back (slip::esc);
                out.push_back (slip::escEnd);
            }
            else if (byte == slip::esc)
            {
                out.push_back (slip::esc);
                out.push_back (slip::escEsc);
            }
            else
            {
                out.push_back (byte);
            }
        }

        out.push_back (slip::end);
        return out;
    }

    std::vector<std::uint8_t> lengthPrefixEncode (const std::uint8_t* data, std::size_t size)
    {
        std::vector<std::uint8_t> out;
        out.reserve (size + 4);

        const auto count = static_cast<std::uint32_t> (size);
        out.push_back (static_cast<std::uint8_t> ((count >> 24) & 0xff));
        out.push_back (static_cast<std::uint8_t> ((count >> 16) & 0xff));
        out.push_back (static_cast<std::uint8_t> ((count >> 8) & 0xff));
        out.push_back (static_cast<std::uint8_t> (count & 0xff));
        out.insert (out.end(), data, data + size);
        return out;
    }

    std::vector<std::uint8_t> frame (Framing framing, const std::vector<std::uint8_t>& packet)
    {
        return framing == Framing::slip ? slipEncode (packet.data(), packet.size())
                                        : lengthPrefixEncode (packet.data(), packet.size());
    }

    //==============================================================================
    Deframer::Deframer (Framing framing, std::size_t maxPacketSize)
        : kind (framing), maxSize (std::max<std::size_t> (1, maxPacketSize))
    {
    }

    void Deframer::reset()
    {
        partial.clear();
        ready.clear();
        escaping = false;
        discarding = false;
    }

    void Deframer::feed (const std::uint8_t* data, std::size_t size)
    {
        if (data == nullptr || size == 0)
            return;

        if (kind == Framing::slip)
            feedSlip (data, size);
        else
            feedLength (data, size);
    }

    std::optional<std::vector<std::uint8_t>> Deframer::next()
    {
        if (ready.empty())
            return std::nullopt;

        auto packet = std::move (ready.front());
        ready.erase (ready.begin());
        return packet;
    }

    void Deframer::feedSlip (const std::uint8_t* data, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
        {
            const auto byte = data[i];

            if (byte == slip::end)
            {
                /*  Two ENDs in a row frame nothing, which is how a sender that
                    begins each packet with END (OSC 1.1) reads on a receiver
                    that saw the previous packet's closing END. */
                if (! partial.empty() && ! discarding)
                    ready.push_back (std::move (partial));

                partial.clear();
                escaping = false;
                discarding = false;
                continue;
            }

            if (discarding)
                continue;                   // over the limit: nothing until the next END

            std::uint8_t decoded = byte;

            if (escaping)
            {
                escaping = false;

                if (byte == slip::escEnd)
                    decoded = slip::end;
                else if (byte == slip::escEsc)
                    decoded = slip::esc;
                else
                    ++violationCount;       // not an escape: the byte stands, as RFC 1055 recommends
            }
            else if (byte == slip::esc)
            {
                escaping = true;
                continue;
            }

            if (partial.size() >= maxSize)
            {
                ++droppedCount;
                partial.clear();
                discarding = true;
                continue;
            }

            partial.push_back (decoded);
        }
    }

    void Deframer::feedLength (const std::uint8_t* data, std::size_t size)
    {
        partial.insert (partial.end(), data, data + size);

        for (;;)
        {
            if (partial.size() < 4)
                return;

            const auto count = (static_cast<std::uint32_t> (partial[0]) << 24)
                             | (static_cast<std::uint32_t> (partial[1]) << 16)
                             | (static_cast<std::uint32_t> (partial[2]) << 8)
                             |  static_cast<std::uint32_t> (partial[3]);

            /*  A count that cannot be an OSC packet - nought, over the limit,
                or with the sign bit set - means the stream is not where this
                thinks it is. Drop one byte and look again: there is no END to
                skip to, and a sender that framed correctly will line up within
                a few bytes. */
            if (count == 0 || count > maxSize || (count & 0x80000000u) != 0)
            {
                ++droppedCount;
                partial.erase (partial.begin());
                continue;
            }

            if (partial.size() < 4 + count)
                return;

            ready.emplace_back (partial.begin() + 4, partial.begin() + 4 + static_cast<std::ptrdiff_t> (count));
            partial.erase (partial.begin(), partial.begin() + 4 + static_cast<std::ptrdiff_t> (count));
        }
    }
}
