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

#include <core/send/TimeTags.h>

#include <chrono>
#include <cmath>

namespace oqa::core
{
    namespace
    {
        constexpr std::int64_t nanosPerSecond = 1'000'000'000LL;
        constexpr double fractionScale = 4294967296.0;   // 2^32
    }

    oscquery::osc::TimeTag ntpFromUnixNanos (std::int64_t unixNanos) noexcept
    {
        const auto ntpNanos = unixNanos + static_cast<std::int64_t> (ntpEpochOffsetSeconds) * nanosPerSecond;

        if (ntpNanos < 0)
            return oscquery::osc::TimeTag { 0 };

        /*  NTP's seconds are 32 bits and roll over in 2036 (the end of era 0);
            the tag wraps as NTP does, and a device in that era reads it right. */
        const auto seconds = static_cast<std::uint64_t> (ntpNanos / nanosPerSecond) & 0xffffffffULL;
        const auto remainder = static_cast<double> (ntpNanos % nanosPerSecond) / static_cast<double> (nanosPerSecond);
        auto fraction = static_cast<std::uint64_t> (std::llround (remainder * fractionScale));

        if (fraction > 0xffffffffULL)
            fraction = 0xffffffffULL;

        return oscquery::osc::TimeTag { (seconds << 32) | fraction };
    }

    std::int64_t unixNanosFromNtp (oscquery::osc::TimeTag tag) noexcept
    {
        const auto seconds = static_cast<std::int64_t> (tag.raw >> 32);
        const auto fraction = static_cast<double> (tag.raw & 0xffffffffULL) / fractionScale;

        return (seconds - static_cast<std::int64_t> (ntpEpochOffsetSeconds)) * nanosPerSecond
             + static_cast<std::int64_t> (std::llround (fraction * static_cast<double> (nanosPerSecond)));
    }

    std::int64_t unixNanosNow() noexcept
    {
        using namespace std::chrono;
        return duration_cast<nanoseconds> (system_clock::now().time_since_epoch()).count();
    }

    oscquery::osc::TimeTag ntpNow (double offsetMs) noexcept
    {
        const auto offsetNanos = std::isfinite (offsetMs) ? static_cast<std::int64_t> (std::llround (offsetMs * 1'000'000.0)) : 0;
        return ntpFromUnixNanos (unixNanosNow() + offsetNanos);
    }
}
