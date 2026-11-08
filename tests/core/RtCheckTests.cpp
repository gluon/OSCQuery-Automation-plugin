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
    THE COUNTER CAN COUNT (devplan Phase 3, "instrument and assert"): before
    any test trusts a zero from rt::violations(), this proves the allocator
    replacement is in and hears a `new` inside a region, and nothing outside
    one. The gate that was green because it was deaf is go.dot's story
    (namespace draft §23.1), and this is the control that keeps it from being
    ours.
*/

#include <doctest/doctest.h>

#include <core/rt/RtCheck.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace oqa::core;

TEST_CASE ("rt: the allocator replacement is in and announces itself")
{
    REQUIRE (rt::isCounting());
}

TEST_CASE ("rt: a new inside a region is a violation; outside one it is nothing")
{
    rt::resetCounts();
    CHECK (rt::violations() == 0);
    CHECK (rt::regions() == 0);

    {
        auto outside = std::make_unique<int> (1);
        (void) outside;
    }

    CHECK (rt::violations() == 0);

    {
        rt::ScopedRealtimeCheck region;
        auto inside = std::make_unique<int> (2);
        (void) inside;
    }

    CHECK (rt::violations() >= 1);
    CHECK (rt::regions() == 1);

    const auto seen = rt::violations();

    {
        rt::ScopedRealtimeCheck region;
        std::vector<char> big (4096);
        (void) big;
    }

    CHECK (rt::violations() > seen);
    CHECK (rt::regions() == 2);
}

TEST_CASE ("rt: regions nest and count once; a region is per thread")
{
    rt::resetCounts();

    {
        rt::ScopedRealtimeCheck outer;
        {
            rt::ScopedRealtimeCheck inner;
            auto p = std::make_unique<int> (3);
            (void) p;
        }
        CHECK (rt::regionDepth == 1);
    }

    CHECK (rt::regionDepth == 0);
    CHECK (rt::regions() == 1);
    CHECK (rt::violations() >= 1);

    /*  Another thread's allocation, while this one is in a region, is not ours. */
    rt::resetCounts();

    {
        rt::ScopedRealtimeCheck region;
        std::thread other ([] { std::string s (100, 'x'); (void) s; });
        other.join();
    }

    /*  The join and the thread object may allocate on this thread; only an
        allocation on the other thread must not count. Starting a std::thread
        allocates on the starting thread, so nothing exact is asserted here
        beyond the region count; the control above is the one that matters. */
    CHECK (rt::regions() == 1);
}

TEST_CASE ("rt: a region that allocates nothing reports zero")
{
    rt::resetCounts();

    {
        rt::ScopedRealtimeCheck region;
        volatile int sink = 0;

        for (int i = 0; i < 1000; ++i)
            sink = sink + i;
    }

    CHECK (rt::violations() == 0);
    CHECK (rt::regions() == 1);
}
