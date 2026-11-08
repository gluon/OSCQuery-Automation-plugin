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
    NTP TIME TAGS (devplan Phase 3: "immediate timetag unless the device's
    honours timetags setting is on, in which case timetag = intended playback
    time plus the per-device offset"). Known conversions, the round trip, the
    clock, the offset.
*/

#include <doctest/doctest.h>

#include <core/send/TimeTags.h>

#include <cstdlib>

using namespace oqa::core;
using oqa::oscquery::osc::TimeTag;

TEST_CASE ("time tags: the Unix epoch is 2,208,988,800 NTP seconds, fraction zero")
{
    const auto tag = ntpFromUnixNanos (0);
    CHECK ((tag.raw >> 32) == ntpEpochOffsetSeconds);
    CHECK ((tag.raw & 0xffffffffULL) == 0);

    CHECK (unixNanosFromNtp (tag) == 0);
}

TEST_CASE ("time tags: half a second is 2^31 in the fraction")
{
    const auto tag = ntpFromUnixNanos (500'000'000LL);
    CHECK ((tag.raw >> 32) == ntpEpochOffsetSeconds);
    CHECK ((tag.raw & 0xffffffffULL) == 0x80000000ULL);
}

TEST_CASE ("time tags: a known instant converts to the expected words")
{
    /*  2026-11-08 12:00:00 UTC = Unix 1'794'484'800. */
    const std::int64_t unix = 1'794'484'800LL;
    const auto tag = ntpFromUnixNanos (unix * 1'000'000'000LL + 250'000'000LL);

    CHECK ((tag.raw >> 32) == static_cast<std::uint64_t> (unix) + ntpEpochOffsetSeconds);
    CHECK ((tag.raw & 0xffffffffULL) == 0x40000000ULL);
}

TEST_CASE ("time tags: the round trip is exact to the resolution of the fraction (233 ps)")
{
    /*  Up to 2035: NTP's era 0 ends on 7 February 2036, where the seconds wrap. */
    for (std::int64_t nanos : { 0LL, 1LL, 999'999'999LL, 1'794'484'800'123'456'789LL, 2'050'000'000'000'000'000LL })
    {
        const auto back = unixNanosFromNtp (ntpFromUnixNanos (nanos));
        INFO ("nanos " << nanos << " came back as " << back);
        CHECK (std::llabs (back - nanos) <= 1);
    }
}

TEST_CASE ("time tags: past the end of era 0 the seconds wrap as NTP's do")
{
    /*  2^32 - 2'208'988'800 = 2'085'978'496 Unix seconds is the rollover. */
    const std::int64_t rollover = 2'085'978'496LL;
    CHECK ((ntpFromUnixNanos ((rollover - 1) * 1'000'000'000LL).raw >> 32) == 0xffffffffULL);
    CHECK ((ntpFromUnixNanos (rollover * 1'000'000'000LL).raw >> 32) == 0);
}

TEST_CASE ("time tags: before 1900 clamps to zero rather than wrapping")
{
    const auto tag = ntpFromUnixNanos (-static_cast<std::int64_t> (ntpEpochOffsetSeconds + 10) * 1'000'000'000LL);
    CHECK (tag.raw == 0);
}

TEST_CASE ("time tags: now is now, and the offset moves it by the offset")
{
    const auto before = unixNanosNow();
    const auto tag = ntpNow (0.0);
    const auto after = unixNanosNow();

    const auto tagged = unixNanosFromNtp (tag);
    CHECK (tagged >= before - 1);
    CHECK (tagged <= after + 1);

    const auto shifted = unixNanosFromNtp (ntpNow (-12.5));
    const auto now = unixNanosNow();
    const auto expected = now - 12'500'000LL;

    /*  Within a few milliseconds of the expected point: the two clock reads
        are not simultaneous. */
    CHECK (std::llabs (shifted - expected) < 5'000'000LL);

    const auto forward = unixNanosFromNtp (ntpNow (+100.0));
    CHECK (forward > unixNanosNow() + 90'000'000LL);
}

TEST_CASE ("time tags: the immediate tag is raw 1")
{
    CHECK (immediateTimeTag.raw == 1);
    CHECK (immediateTimeTag == TimeTag {});
}
