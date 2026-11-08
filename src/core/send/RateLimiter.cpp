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

#include <core/send/RateLimiter.h>

#include <cmath>

namespace oqa::core
{
    void RateLimiter::resize (std::size_t keys)
    {
        entries.assign (keys, Entry {});
    }

    void RateLimiter::setOptions (Options options) noexcept
    {
        current.maxHz    = std::isfinite (options.maxHz) && options.maxHz > 0.0 ? options.maxHz : 0.0;
        current.resendMs = std::isfinite (options.resendMs) && options.resendMs > 0.0 ? options.resendMs : 0.0;
    }

    std::int64_t RateLimiter::intervalNanos() const noexcept
    {
        return current.maxHz > 0.0 ? static_cast<std::int64_t> (std::llround (1'000'000'000.0 / current.maxHz)) : 0;
    }

    std::int64_t RateLimiter::resendNanos() const noexcept
    {
        return current.resendMs > 0.0 ? static_cast<std::int64_t> (std::llround (current.resendMs * 1'000'000.0)) : 0;
    }

    bool RateLimiter::offer (std::size_t key, double value, std::int64_t nowNanos) noexcept
    {
        if (key >= entries.size())
            return true;

        auto& e = entries[key];
        const auto interval = intervalNanos();

        if (interval > 0 && e.hasSent && nowNanos - e.lastSentNanos < interval)
        {
            e.pending = value;
            e.hasPending = true;
            ++heldCount;
            return false;
        }

        e.lastSent = value;
        e.lastSentNanos = nowNanos;
        e.hasSent = true;
        e.hasPending = false;
        return true;
    }

    void RateLimiter::flushDue (std::int64_t nowNanos, const std::function<void (std::size_t, double)>& send)
    {
        const auto interval = intervalNanos();
        const auto resend = resendNanos();

        for (std::size_t key = 0; key < entries.size(); ++key)
        {
            auto& e = entries[key];

            if (e.hasPending && (interval == 0 || nowNanos - e.lastSentNanos >= interval))
            {
                e.lastSent = e.pending;
                e.lastSentNanos = nowNanos;
                e.hasSent = true;
                e.hasPending = false;
                send (key, e.lastSent);
                continue;
            }

            if (resend > 0 && e.hasSent && ! e.hasPending && nowNanos - e.lastSentNanos >= resend)
            {
                e.lastSentNanos = nowNanos;
                ++resentCount;
                send (key, e.lastSent);
            }
        }
    }

    void RateLimiter::reset() noexcept
    {
        for (auto& e : entries)
            e = Entry {};
    }
}
