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
    OSC TIME TAGS FROM THE CLOCK (PRD, Transport, "Timetags"): a device that
    honours bundle time tags receives timetag = intended playback time plus
    the per-device offset and schedules the change itself; every other device
    receives the immediate tag (raw 1). Whether a device honours them is a
    per-device setting, default off, since OSCQuery has no field for it.

    NTP: seconds since 1900-01-01 in the high 32 bits, the fraction of a
    second in 2^-32 steps in the low 32. The Unix epoch is 2,208,988,800 s
    after NTP's. The wall clock (system_clock) is what a device can agree
    with; the plugin's steady clock is nobody else's.

    No bench device honours time tags yet (WFS-DIY discards them, checked
    2026-11-08), so this is verified against the mock device only.
*/

#include <oscquery/osc/Value.h>

#include <cstdint>

namespace oqa::core
{
    inline constexpr std::uint64_t ntpEpochOffsetSeconds = 2208988800ULL;

    /** The tag that means "now": raw 1. */
    inline constexpr oscquery::osc::TimeTag immediateTimeTag {};

    /*  Nanoseconds since the Unix epoch to an NTP tag; negative times clamp to
        NTP zero, and the seconds wrap at 2^32 (7 February 2036) as NTP's do. */
    oscquery::osc::TimeTag ntpFromUnixNanos (std::int64_t unixNanos) noexcept;

    /** An NTP tag to nanoseconds since the Unix epoch (rounded to the nearest). */
    std::int64_t unixNanosFromNtp (oscquery::osc::TimeTag tag) noexcept;

    /** The wall clock now, shifted by `offsetMs` (the per-device offset), as a tag. */
    oscquery::osc::TimeTag ntpNow (double offsetMs) noexcept;

    /** Nanoseconds since the Unix epoch, now. */
    std::int64_t unixNanosNow() noexcept;
}
