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
    WHAT A SLOT'S ROW SAYS, AS PURE FUNCTIONS (devplan Phase 6d-1; PRD, Access
    control and failure states: "every failure is visible in the editor, never
    silent", with the table's rows). The colour as a shade and the tooltip's
    words come from the slot's standing alone, so a headless test asserts them
    (tests/core/PresentationTests.cpp) and the table only paints. No JUCE here.

    The words are the implementer's defaults (Phase 6 plan), the author's to
    change in one place.
*/

#include <cstdint>
#include <string>

namespace oqa::editor::presentation
{
    enum class Shade { grey, amber, red, orangeRed, green };

    /** What the row knows of its slot. */
    struct Standing
    {
        bool bound = false;
        bool active = false;                ///< bound and its range known
        std::string state;                  ///< the registry's word: ok, pending, missing, retyped, renamed, unauthorised, inactive, ""
        std::string detail;                 ///< the registry's detail for that word
        std::string renamedTo;
        bool deviceReachable = true;
        bool waitingForPort = false;        ///< receiving from a device without LISTEN that has not been told the port
        std::int64_t deviceLastSeenMs = -1; ///< for the unreachable words; -1 never
        std::int64_t loopGuarded = 0;
        std::int64_t rateLimited = 0;
        std::int64_t datagramsDropped = 0;
    };

    inline bool stale (const std::string& state)
    {
        return state == "missing" || state == "retyped" || state == "renamed";
    }

    inline Shade shadeOf (const Standing& s)
    {
        if (! s.bound)
            return Shade::grey;

        if (! s.active)
            return Shade::amber;

        if (stale (s.state))
            return Shade::red;

        if (s.state == "unauthorised")
            return Shade::amber;

        if (! s.deviceReachable)
            return Shade::orangeRed;

        if (s.waitingForPort)
            return Shade::amber;

        return Shade::green;
    }

    inline std::string agoWords (std::int64_t ms)
    {
        if (ms < 0)
            return "never";

        if (ms < 2000)
            return "just now";

        if (ms < 120000)
            return std::to_string (ms / 1000) + " s ago";

        return std::to_string (ms / 60000) + " min ago";
    }

    /** The tooltip, from the PRD's failure table; empty for a slot with nothing to say. */
    inline std::string tooltipOf (const Standing& s)
    {
        if (! s.bound)
            return "Unbound: select the row, then click a leaf in the tree, or drop one here.";

        if (! s.active)
            return "Range unknown: the leaf has no RANGE. Enter min and max; the slot is inactive until then.";

        if (stale (s.state))
        {
            std::string words = s.state == "renamed" && ! s.renamedTo.empty()
                                    ? "Stale: the path moved to " + s.renamedTo + "."
                                    : "Stale: " + (s.detail.empty() ? std::string ("the path is gone or retyped") : s.detail) + ".";
            return words + " The lane keeps its automation; nothing is sent. Rebind binds it again.";
        }

        if (s.state == "unauthorised")
            return "Unauthorised: " + (s.detail.empty() ? std::string ("the device reports read-only ACCESS to this client") : s.detail)
                 + ". Not on the device's allow-list; the slot still receives when its direction says so.";

        if (! s.deviceReachable)
            return "Unreachable: no HOST_INFO answer from the device, last seen " + agoWords (s.deviceLastSeenMs) + ". Nothing is sent.";

        if (s.waitingForPort)
            return "Waiting: the device has no LISTEN. Type this plugin's receive port into the device; record starts with its first datagram.";

        std::string words;

        if (s.loopGuarded > 0)
            words += std::to_string (s.loopGuarded) + " echoes dropped by the loop guard. ";

        if (s.rateLimited > 0 || s.datagramsDropped > 0)
            words += std::to_string (s.rateLimited) + " held by the rate limiter, " + std::to_string (s.datagramsDropped) + " datagrams dropped. ";

        return words;
    }
}
