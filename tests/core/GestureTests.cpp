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
    THE GESTURE TRACKER (devplan Phase 4c): opened by the first value, held
    while values keep coming inside the timeout, closed after it, reopened by
    the next; many slots at once; all closed on demand. The clock is the
    test's, so nothing waits.
*/

#include <doctest/doctest.h>

#include <core/receive/Gesture.h>

#include <vector>

using oqa::core::GestureTracker;

namespace
{
    struct Closed
    {
        std::vector<int> slots;
        std::function<void (int)> fn() { return [this] (int slot) { slots.push_back (slot); }; }
    };
}

TEST_CASE ("gesture: the first value opens, the next ones do not, and silence past the timeout closes")
{
    GestureTracker gestures (4);
    Closed closed;

    CHECK (gestures.openCount() == 0);
    CHECK (gestures.received (1, 1000.0));          // opens
    CHECK_FALSE (gestures.received (1, 1050.0));    // held
    CHECK_FALSE (gestures.received (1, 1100.0));
    CHECK (gestures.isOpen (1));
    CHECK (gestures.openCount() == 1);

    /*  Inside the timeout from the LAST value: still open. */
    CHECK (gestures.expire (1290.0, 200.0, closed.fn()) == 0);
    CHECK (gestures.isOpen (1));
    CHECK (closed.slots.empty());

    /*  Past it: closed, once, and said so. */
    CHECK (gestures.expire (1301.0, 200.0, closed.fn()) == 1);
    CHECK_FALSE (gestures.isOpen (1));
    REQUIRE (closed.slots.size() == 1);
    CHECK (closed.slots[0] == 1);

    CHECK (gestures.expire (2000.0, 200.0, closed.fn()) == 0);
    CHECK (closed.slots.size() == 1);

    /*  The next value opens again. */
    CHECK (gestures.received (1, 2000.0));
    CHECK (gestures.isOpen (1));
}

TEST_CASE ("gesture: a value every 10 ms at 100 Hz keeps one gesture open for as long as it streams")
{
    GestureTracker gestures (2);
    Closed closed;
    int opened = 0;

    for (int i = 0; i < 500; ++i)
    {
        if (gestures.received (0, i * 10.0))
            ++opened;

        gestures.expire (i * 10.0, 200.0, closed.fn());
    }

    CHECK (opened == 1);
    CHECK (closed.slots.empty());
    CHECK (gestures.isOpen (0));

    CHECK (gestures.expire (4990.0 + 201.0, 200.0, closed.fn()) == 1);
    CHECK_FALSE (gestures.isOpen (0));
}

TEST_CASE ("gesture: many slots at once, each on its own clock; closeAll closes what is open and names it")
{
    GestureTracker gestures (32);
    Closed closed;

    for (int slot = 0; slot < 32; slot += 2)
        CHECK (gestures.received (slot, 100.0 + slot));

    CHECK (gestures.openCount() == 16);

    /*  Slot 0 last heard at 100, slot 30 at 130: a sweep at 320 with 200 ms
        closes the ones silent longer than that (0 to 18) and keeps the rest. */
    CHECK (gestures.expire (320.0, 200.0, closed.fn()) == 10);
    CHECK (gestures.openCount() == 6);
    CHECK_FALSE (gestures.isOpen (0));
    CHECK (gestures.isOpen (20));
    CHECK (gestures.isOpen (30));

    closed.slots.clear();
    CHECK (gestures.closeAll (closed.fn()) == 6);
    CHECK (gestures.openCount() == 0);
    REQUIRE (closed.slots.size() == 6);
    CHECK (closed.slots.front() == 20);
    CHECK (closed.slots.back() == 30);

    CHECK (gestures.closeAll (closed.fn()) == 0);
}

TEST_CASE ("gesture: a slot out of range opens nothing")
{
    GestureTracker gestures (4);
    CHECK_FALSE (gestures.received (-1, 0.0));
    CHECK_FALSE (gestures.received (4, 0.0));
    CHECK_FALSE (gestures.isOpen (4));
    CHECK (gestures.openCount() == 0);
}
