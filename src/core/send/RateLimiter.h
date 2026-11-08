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
    THE PER-SLOT RATE LIMITER, DEFAULT OFF (PRD, Transport: "No rate limit by
    default. Every block that has changes is sent and the device smooths. The
    per-slot limiter exists as protection for lossy links or slow devices,
    default off"; Binding model: "sends only on value change, with an optional
    periodic resend for lossy links").

    THE SHAPE IS WFS-DIY's RateLimiter (Plugin/Source/Shared): the latest
    value per key survives a window, and what is sent is compared with what
    was last sent. The plumbing is not: no mutex, no timer, no map that grows.
    It runs on a device's network thread, which asks it two things:

      offer (key, value, now)    may this go now? yes: it is recorded as sent;
                                 no: it is held, superseding anything held
      flushDue (now, send)       send what is held and due, and resend what is
                                 unchanged and overdue when a resend period is on

    Keys are dense indices the registry hands out per bound slot; storage is
    sized by resize() before the thread runs.
*/

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace oqa::core
{
    class RateLimiter
    {
    public:
        struct Options
        {
            /** Sends per second per key at most; 0 is off (every change goes). */
            double maxHz = 0.0;

            /** Resend an unchanged value after this long; 0 is off. */
            double resendMs = 0.0;
        };

        RateLimiter() = default;
        explicit RateLimiter (std::size_t keys) { resize (keys); }

        void resize (std::size_t keys);
        std::size_t size() const noexcept { return entries.size(); }

        void setOptions (Options options) noexcept;
        Options options() const noexcept { return current; }

        /** True: send now (recorded). False: held for flushDue. An unknown key answers true. */
        bool offer (std::size_t key, double value, std::int64_t nowNanos) noexcept;

        /** The held and due, then the unchanged and overdue. */
        void flushDue (std::int64_t nowNanos, const std::function<void (std::size_t key, double value)>& send);

        /** Forget what was sent and held, for every key. */
        void reset() noexcept;

        std::uint64_t held() const noexcept   { return heldCount; }
        std::uint64_t resent() const noexcept { return resentCount; }

    private:
        struct Entry
        {
            double lastSent = 0.0;
            double pending = 0.0;
            std::int64_t lastSentNanos = 0;
            bool hasSent = false;
            bool hasPending = false;
        };

        std::int64_t intervalNanos() const noexcept;
        std::int64_t resendNanos() const noexcept;

        Options current;
        std::vector<Entry> entries;
        std::uint64_t heldCount = 0;
        std::uint64_t resentCount = 0;
    };
}
