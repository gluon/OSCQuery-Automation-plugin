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

#include <oscquery/osc/Bundler.h>

#include <algorithm>

namespace oqa::oscquery::osc
{
    namespace
    {
        void appendUInt32 (std::vector<std::uint8_t>& out, std::uint32_t value)
        {
            out.push_back (static_cast<std::uint8_t> ((value >> 24) & 0xff));
            out.push_back (static_cast<std::uint8_t> ((value >> 16) & 0xff));
            out.push_back (static_cast<std::uint8_t> ((value >> 8) & 0xff));
            out.push_back (static_cast<std::uint8_t> (value & 0xff));
        }

        void flattenInto (const Packet& packet, std::vector<const Packet*>& out)
        {
            if (! packet.isBundle())
            {
                out.push_back (&packet);
                return;
            }

            for (const auto& element : packet.elements)
                flattenInto (element, out);
        }
    }

    std::vector<std::uint8_t> bundleOf (const std::vector<std::vector<std::uint8_t>>& encodedMessages, TimeTag when)
    {
        std::vector<std::uint8_t> out;

        std::size_t total = bundleHeaderBytes;

        for (const auto& message : encodedMessages)
            total += bundleElementOverhead + message.size();

        out.reserve (total);

        static constexpr std::uint8_t marker[8] = { '#', 'b', 'u', 'n', 'd', 'l', 'e', 0 };
        out.insert (out.end(), marker, marker + 8);
        appendUInt32 (out, static_cast<std::uint32_t> (when.raw >> 32));
        appendUInt32 (out, static_cast<std::uint32_t> (when.raw & 0xffffffffu));

        for (const auto& message : encodedMessages)
        {
            appendUInt32 (out, static_cast<std::uint32_t> (message.size()));
            out.insert (out.end(), message.begin(), message.end());
        }

        return out;
    }

    Bundled bundle (const std::vector<Packet>& messages, TimeTag when, std::size_t mtu)
    {
        Bundled result;

        //  Below this nothing fits; the caller asked for something absurd.
        mtu = std::max (mtu, bundleHeaderBytes + bundleElementOverhead + 8);

        std::vector<const Packet*> flat;

        for (const auto& packet : messages)
            flattenInto (packet, flat);

        std::vector<std::vector<std::uint8_t>> current;
        std::size_t currentSize = bundleHeaderBytes;

        const auto flush = [&]
        {
            if (! current.empty())
            {
                result.datagrams.push_back (bundleOf (current, when));
                current.clear();
                currentSize = bundleHeaderBytes;
            }
        };

        for (std::size_t i = 0; i < flat.size(); ++i)
        {
            std::string error;
            auto encoded = encode (*flat[i], error);

            if (! encoded.has_value())
            {
                result.refused.push_back (i);
                result.reasons.push_back (error);
                continue;
            }

            const auto cost = bundleElementOverhead + encoded->size();

            if (bundleHeaderBytes + cost > mtu)
            {
                result.refused.push_back (i);
                result.reasons.push_back ("a message of " + std::to_string (encoded->size())
                                          + " bytes cannot fit a bundle of " + std::to_string (mtu));
                continue;
            }

            if (currentSize + cost > mtu)
                flush();

            currentSize += cost;
            current.push_back (std::move (*encoded));
        }

        flush();
        return result;
    }
}
