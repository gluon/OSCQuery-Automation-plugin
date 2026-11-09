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
    WHO MAY WRITE TO A SERVER (devplan Phase 6b-1; spec draft C6 "ACCESS as
    reported to the requesting client", L3 "OSC over the websocket obeys the
    same allow-list as UDP"). One setting, one reading of it for HTTP (the
    ACCESS clamp), for websocket frames and for UDP: "" is loopback only, "any"
    is everyone, anything else is addresses separated by commas. A client's
    address is normalised first: the server is dual-stack, so an IPv4 client
    arrives as ::ffff:a.b.c.d.
*/

#include <juce_core/juce_core.h>

#include <string>

namespace oqa::oscquery::server
{
    struct AccessPolicy
    {
        /** `::ffff:a.b.c.d` to `a.b.c.d`; brackets off an IPv6 literal. */
        static std::string normaliseAddress (std::string address)
        {
            auto s = juce::String (address).trim();

            if (s.startsWithChar ('[') && s.endsWithChar (']'))
                s = s.substring (1, s.length() - 1);

            if (s.startsWithIgnoreCase ("::ffff:") && s.containsChar ('.'))
                s = s.fromLastOccurrenceOf (":", false, false);

            return s.toStdString();
        }

        static bool isLoopback (const std::string& normalised)
        {
            return normalised == "::1" || juce::String (normalised).startsWith ("127.");
        }

        /** Whether `clientAddress` (as the socket names it) may write under `setting`. */
        static bool allows (const std::string& setting, const std::string& clientAddress)
        {
            const auto client = normaliseAddress (clientAddress);
            const auto rule = juce::String (setting).trim();

            if (rule.isEmpty())
                return isLoopback (client);

            if (rule.equalsIgnoreCase ("any"))
                return true;

            juce::StringArray allowed;
            allowed.addTokens (rule, ",", "");
            allowed.trim();
            allowed.removeEmptyStrings();

            for (const auto& a : allowed)
                if (normaliseAddress (a.toStdString()) == client || (a.equalsIgnoreCase ("loopback") && isLoopback (client)))
                    return true;

            return false;
        }
    };
}
